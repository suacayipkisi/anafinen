// Copyright (c) 2026 Abdurrahman Konuk (professionally known as Ufuk Deniz Konuk)
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program. If not, see <https://www.gnu.org/licenses/>.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "deformationUnderConstForce.hpp"
#include "trussEngine/trussSolver/assembleStiffness.hpp"
#include <solvers/solverPortfolio.hpp>
#include <log/anaf_info.hpp>

#include <Eigen/Core>
#include <Eigen/SparseCore>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <omp.h>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace FEM::TRUSS {

  namespace {
    // The mechanism of a singular solve in words: the node and direction moving the most in its
    // mode, and how many nodes move with it.
    std::string mechanismMessage(
      const FEM::SOLVER::Result& result,
      const std::vector<std::int32_t>& nodeDofSlots,
      const std::span<const Node> nodes
    ) {
      constexpr const char* advice = "check the supports";
      if (result.mechanismDof < 0) return std::format("the structure is a mechanism ({}): {}", result.message, advice);
      std::vector<Eigen::Vector3d> motion(nodes.size(), Eigen::Vector3d::Zero());
      std::size_t worstNode = 0;
      for (std::size_t node = 0; node < nodes.size(); ++node) {
        const auto& directions = nodes[node].getAllowedMotionDirections();
        for (std::size_t k = 0; k < directions.size(); ++k) {
          const auto reduced = nodeDofSlots[3 * node + k];
          if (reduced == result.mechanismDof) worstNode = node;
          motion[node] += result.mechanismMode[reduced] * Eigen::Vector3d(directions[k][0], directions[k][1], directions[k][2]);
        }
      }
      const Eigen::Vector3d direction = motion[worstNode].normalized();
      const double reference = motion[worstNode].norm();
      const auto moving = std::ranges::count_if(motion, [&](const Eigen::Vector3d& m) { return m.norm() > 1e-6 * reference; });
      return std::format(
        "the structure is a mechanism: node {} can move freely along ({:.3g}, {:.3g}, {:.3g}) without straining any element{}; {}",
        worstNode, direction[0], direction[1], direction[2],
        moving > 1 ? std::format(" ({} more {} with it)", moving - 1, moving == 2 ? "node" : "nodes") : std::string{}, advice);
    }
  } // namespace end
  
  void Truss1DContainer::setStiffness(
    const std::vector<TrussElement1D>& elements,
    std::span<const anaf::MATERIAL::Material> allMaterials
  ) {
    m_dofs = ReducedDofs(m_allNodes);
    if (m_dofs.inclinedNodes() > 0) anaf::LOG::info("Inclined supports on {} nodes", m_dofs.inclinedNodes());
    m_reducedStiffness = assembleStiffness(elements, allMaterials, m_allNodes.size(), m_dofs);
  }

  void Truss1DContainer::considerWeight(
    const std::vector<TrussElement1D>& elements,
    std::span<const anaf::MATERIAL::Material> materials
  ) {
    #pragma omp parallel for schedule(static)
    for (long long index = 0; index < static_cast<long long>(elements.size()); ++index) {
      const auto& element = elements[index];
      const double mass = materials[element.getEleProperties()].getDensity()
        * element.getEleCrossSection() * element.getEleLength();
      const auto& nodes = element.getEleNodes();
      for (std::uint32_t axis = 0; axis < 3; ++axis) {
        const double halfWeight = 0.5 * mass * m_gravity[axis];
        if (halfWeight == 0.0) continue;
        #pragma omp atomic
        m_forceVec[3 * nodes[0] + axis] += halfWeight;
        #pragma omp atomic
        m_forceVec[3 * nodes[1] + axis] += halfWeight;
      }
    }
  }

  std::expected<void, std::string> Truss1DContainer::calculateDisplacements(const std::stop_token stopToken) {
    #pragma omp parallel
    {
      #pragma omp single
      anaf::LOG::info(
        "OpenMP team: {}, max threads: {}",
        omp_get_num_threads(), omp_get_max_threads()
      );
    }

    const std::uint32_t nodeCount = static_cast<std::uint32_t>(m_allNodes.size());
    const std::int32_t activeDofCount = m_dofs.count();
    const auto& nodeDofSlots = m_dofs.nodeDofSlots();

    const Eigen::SparseMatrix<double> reducedStiffnessMatrix = std::move(m_reducedStiffness);
    m_reducedStiffness = Eigen::SparseMatrix<double>();

    Eigen::VectorXd reducedForce(activeDofCount);
    #pragma omp parallel for schedule(static)
    for (long long node = 0; node < nodeCount; ++node) {
      const auto& directions = m_allNodes[node].getAllowedMotionDirections();
      for (std::size_t k = 0; k < directions.size(); ++k) {
        reducedForce[nodeDofSlots[3 * node + k]] = directions[k][0] * m_forceVec[3 * node]
          + directions[k][1] * m_forceVec[3 * node + 1]
          + directions[k][2] * m_forceVec[3 * node + 2];
      }
    }

    Eigen::VectorXd reducedDisplacements(activeDofCount);
    const auto solverResult = FEM::SOLVER::solveSelected(
      reducedStiffnessMatrix,
      reducedForce,
      nodeCount,
      3, // DOF slots per node in nodeDofSlots (translations only)
      nodeDofSlots,
      stopToken,
      reducedDisplacements
    );

    if (!solverResult.converged) {
      anaf::LOG::error(
        "Stiffness solve failed using {}: {}",
        FEM::SOLVER::toString(solverResult.type), solverResult.message
      );
      for (auto& node : m_allNodes) node.setDisplacements({0.0, 0.0, 0.0});
      if (solverResult.singular) return std::unexpected(mechanismMessage(solverResult, nodeDofSlots, m_allNodes));
      return std::unexpected("the stiffness solve failed (is the structure a mechanism? check the supports)");
    }

    #pragma omp parallel for schedule(static)
    for (long long node = 0; node < nodeCount; ++node) {
      const auto& directions = m_allNodes[node].getAllowedMotionDirections();
      std::array<double, 3> displacement{};
      for (std::size_t k = 0; k < directions.size(); ++k) {
        const double amount = reducedDisplacements[nodeDofSlots[3 * node + k]];
        for (std::size_t axis = 0; axis < 3; ++axis) displacement[axis] += directions[k][axis] * amount;
      }
      m_allNodes[node].setDisplacements(displacement);
    }
    return {};
  }

  void Truss1DContainer::calculateElementForcesAndStress(
    const std::span<const anaf::MATERIAL::Material> materials
  ) {
    #pragma omp parallel for schedule(static)
    for (long long index = 0; index < static_cast<long long>(m_allElements.size()); ++index) {
      auto& element = m_allElements[index];
      const auto& nodes = element.getEleNodes();
      const auto& direction = element.getEleCosines();
      Eigen::Vector<double, 6> displacement;
      for (std::uint8_t axis = 0; axis < 3; ++axis) {
        displacement[axis] = m_allNodes[nodes[0]].getDisplacement()[axis];
        displacement[axis + 3] = m_allNodes[nodes[1]].getDisplacement()[axis];
      }

      const Eigen::Vector3d axis(direction[0], direction[1], direction[2]);
      Eigen::Vector<double, 6> transformation;
      transformation << -axis[0], -axis[1], -axis[2], axis[0], axis[1], axis[2];
      const double elongation = transformation.dot(displacement);
      element.setEleElongation(elongation);

      const auto& material = materials[element.getEleProperties()];
      const double length = element.getEleLength();
      const double stress = elongation / length * material.getElasticityModulus();
      const double gravityStress = 0.5 * material.getDensity() * length
        * std::abs(m_gravity.dot(axis));
      // Sign convention: tension > 0, compression < 0. The self-weight term raises the
      // magnitude in the direction of the FE result, so |stress| is the same envelope as before.
      // An explicit comparison instead of std::copysign: -ffast-math does not keep the sign of
      // zero, and a zero FE stress takes the tension side of the envelope.
      element.setEleStress(stress < 0.0 ? stress - gravityStress : stress + gravityStress);
    }
  }

  void Truss1DContainer::runValidator(
    const std::span<const anaf::MATERIAL::Material> materials
  ) {
    double internalEnergy = 0.0;
    #pragma omp parallel for schedule(static) reduction(+:internalEnergy)
    for (long long index = 0; index < static_cast<long long>(m_allElements.size()); ++index) {
      const auto& element = m_allElements[index];
      const double stiffness = materials[element.getEleProperties()].getElasticityModulus()
        * element.getEleCrossSection() / element.getEleLength();
      const double elongation = element.getEleElongation();
      internalEnergy += 0.5 * stiffness * elongation * elongation;
    }
    m_elasticDeformationEnergyInternal = internalEnergy;

    double externalWork = 0.0;
    #pragma omp parallel for schedule(static) reduction(+:externalWork)
    for (long long node = 0; node < static_cast<long long>(m_allNodes.size()); ++node) {
      const auto& displacement = m_allNodes[node].getDisplacement();
      const std::size_t base = 3 * node;
      externalWork += m_forceVec[base] * displacement[0]
        + m_forceVec[base + 1] * displacement[1]
        + m_forceVec[base + 2] * displacement[2];
    }
    m_workDoneExternal = externalWork;

    const double externalEnergy = 0.5 * externalWork;
    m_energyDiff = std::abs(internalEnergy - externalEnergy);
    const double scale = std::max({std::abs(internalEnergy), std::abs(externalEnergy), 1.0});
    m_energyRelativeDiff = m_energyDiff / scale;
    m_isCalculationValid = m_energyDiff <= 1e-12 || m_energyRelativeDiff <= 1e-7;
  }

} // namespace FEM::TRUSS
