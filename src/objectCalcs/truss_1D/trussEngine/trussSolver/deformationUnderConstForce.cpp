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

  void Truss1DContainer::assembleStiffness(
    const std::vector<TrussElement1D>& elements,
    const std::span<const anaf::MATERIAL::Material> allMaterials
  ) {
    constexpr std::size_t tripletsPerElement = 21;
    const std::size_t elementCount = elements.size();
    std::vector<Eigen::Triplet<double>> triplets(elementCount * tripletsPerElement);
    const auto nodeCount = m_allNodes.size();

    #pragma omp parallel for schedule(static)
    for (long long index = 0; index < static_cast<long long>(elementCount); ++index) {
      const auto& element = elements[index];
      const auto& nodes = element.getEleNodes();
      const std::uint32_t dof1 = 3 * nodes[0];
      const std::uint32_t dof2 = 3 * nodes[1];
      const std::uint32_t crossRow = std::min(dof1, dof2);
      const std::uint32_t crossCol = std::max(dof1, dof2);
      const double aeOverLength = element.getEleCrossSection()
        * allMaterials[element.getEleProperties()].getElasticityModulus()
        / element.getEleLength();
      const auto& cosines = element.getEleCosines();
      std::size_t output = static_cast<std::size_t>(index) * tripletsPerElement;

      for (std::size_t row = 0; row < 3; ++row) {
        for (std::size_t col = row; col < 3; ++col) {
          const double value = cosines[row] * cosines[col] * aeOverLength;
          triplets[output++] = {
            static_cast<int>(dof1 + row), static_cast<int>(dof1 + col), value
          };
          triplets[output++] = {
            static_cast<int>(dof2 + row), static_cast<int>(dof2 + col), value
          };
        }
      }
      for (std::size_t row = 0; row < 3; ++row) {
        for (std::size_t col = 0; col < 3; ++col) {
          const double value = cosines[row] * cosines[col] * aeOverLength;
          triplets[output++] = {
            static_cast<int>(crossRow + row), static_cast<int>(crossCol + col), -value
          };
        }
      }
    }

    anaf::LOG::info(
      "Global Stiffness Matrix Created, size: {}x{}",
      nodeCount * 3, nodeCount * 3
    );
    m_globalStiffnessMatrix = std::move(triplets);
  }

  void Truss1DContainer::considerWeight(
    const std::vector<TrussElement1D>& elements,
    std::span<const anaf::MATERIAL::Material> materials
  ) {
    constexpr double gravity = -9.80665;
    #pragma omp parallel for schedule(static)
    for (long long index = 0; index < static_cast<long long>(elements.size()); ++index) {
      const auto& element = elements[index];
      const double weight = materials[element.getEleProperties()].getDensity()
        * element.getEleCrossSection() * element.getEleLength() * gravity;
      const auto& nodes = element.getEleNodes();
      const std::uint32_t dof1 = 3 * nodes[0] + 1;
      const std::uint32_t dof2 = 3 * nodes[1] + 1;
      #pragma omp atomic
      m_forceVec[dof1] += weight / 2.0;
      #pragma omp atomic
      m_forceVec[dof2] += weight / 2.0;
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
    const std::uint32_t totalDofs = nodeCount * 3;
    m_resultDisplacements.resize(nodeCount);

    // Every node owns one reduced DOF per allowed motion direction b_k (orthonormal). With
    // u = T q, T(3 n + axis, k) = b_k[axis], the reduced system is (T^T K T) q = T^T f.
    // For supports along the global axes T only selects columns, which is the classic
    // fixed-DOF removal; inclined supports (Logan, 5th ed., ch. 3) need the full product.
    struct DofLink {
      std::int32_t reduced;
      double factor;
    };
    std::vector<std::array<DofLink, 3>> links(totalDofs);
    std::vector<std::uint8_t> linkCount(totalDofs, 0);
    // Reduced DOF of direction k of node n at 3 n + k, -1 when unused (Block-CG node blocks).
    std::vector<std::int32_t> nodeDofSlots(totalDofs, -1);
    std::int32_t activeDofCount = 0;
    std::uint32_t inclinedNodes = 0;
    for (std::uint32_t node = 0; node < nodeCount; ++node) {
      const auto& directions = m_allNodes[node].getAllowedMotionDirections(); // b_k
      if (m_allNodes[node].hasInclinedSupport()) ++inclinedNodes;
      for (std::size_t k = 0; k < directions.size(); ++k) { // one reduced DOF per orthonormal direction b_k
        const std::int32_t reduced = activeDofCount++;
        nodeDofSlots[3 * node + k] = reduced;

        // Link every global x / y / z DOF to this reduced DOF with the direction cosine b_k[axis].
        for (std::uint32_t axis = 0; axis < 3; ++axis) {
          if (directions[k][axis] == 0.0) continue;
          const std::uint32_t dof = 3 * node + axis;
          links[dof][linkCount[dof]++] = {reduced, directions[k][axis]};
        }
      }
    }
    if (inclinedNodes > 0) anaf::LOG::info("Inclined supports on {} nodes", inclinedNodes);

    // Calls emit(p, q, value) for every upper-triangle entry of T^T K T that the stored upper
    // entry K(i, j) and its mirror K(j, i) produce.
    const auto forEachReduced = [&](const Eigen::Triplet<double>& triplet, auto&& emit) {
      const auto visit = [&](const std::uint32_t row, const std::uint32_t col) {
        for (std::uint8_t a = 0; a < linkCount[row]; ++a) {
          for (std::uint8_t b = 0; b < linkCount[col]; ++b) {
            const auto& rowLink = links[row][a];
            const auto& colLink = links[col][b];
            if (rowLink.reduced <= colLink.reduced) {
              emit(rowLink.reduced, colLink.reduced, rowLink.factor * colLink.factor * triplet.value());
            }
          }
        }
      };
      const auto row = static_cast<std::uint32_t>(triplet.row());
      const auto col = static_cast<std::uint32_t>(triplet.col());
      visit(row, col);
      if (row != col) visit(col, row);
    };

    const int threadCount = omp_get_max_threads();
    std::vector<std::size_t> validCounts(threadCount, 0);
    #pragma omp parallel
    {
      const int thread = omp_get_thread_num();
      #pragma omp for schedule(static)
      for (long long index = 0; index < static_cast<long long>(m_globalStiffnessMatrix.size()); ++index) {
        forEachReduced(m_globalStiffnessMatrix[index], [&](std::int32_t, std::int32_t, double) {
          ++validCounts[thread];
        });
      }
    }

    std::vector<std::size_t> offsets(threadCount + 1, 0);
    for (int thread = 0; thread < threadCount; ++thread) {
      offsets[thread + 1] = offsets[thread] + validCounts[thread];
    }
    std::vector<Eigen::Triplet<double>> reducedTriplets(offsets.back());

    #pragma omp parallel
    {
      const int thread = omp_get_thread_num();
      std::size_t output = offsets[thread];
      #pragma omp for schedule(static)
      for (long long index = 0; index < static_cast<long long>(m_globalStiffnessMatrix.size()); ++index) {
        forEachReduced(m_globalStiffnessMatrix[index], [&](const std::int32_t row, const std::int32_t col, const double value) {
          reducedTriplets[output++] = {row, col, value};
        });
      }
    }

    m_globalStiffnessMatrix.clear();
    m_globalStiffnessMatrix.shrink_to_fit();

    Eigen::SparseMatrix<double> reducedStiffnessMatrix(activeDofCount, activeDofCount);
    reducedStiffnessMatrix.setFromTriplets(reducedTriplets.begin(), reducedTriplets.end());
    reducedStiffnessMatrix.makeCompressed();
    reducedTriplets.clear();
    reducedTriplets.shrink_to_fit();

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
      m_resultDisplacements.assign(nodeCount, {0.0, 0.0, 0.0});
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
      m_resultDisplacements[node] = displacement;
      m_allNodes[node].setDisplacements(displacement);
    }
    return {};
  }

  void Truss1DContainer::calculateElementForcesAndStress(
    const std::span<const anaf::MATERIAL::Material> materials,
    const Eigen::Vector3d gravityVector
  ) {
    #pragma omp parallel for schedule(static)
    for (long long index = 0; index < static_cast<long long>(m_allElements.size()); ++index) {
      auto& element = m_allElements[index];
      const auto& nodes = element.getEleNodes();
      const auto& direction = element.getEleCosines();
      Eigen::Vector<double, 6> displacement;
      for (std::uint8_t axis = 0; axis < 3; ++axis) {
        displacement[axis] = m_resultDisplacements[nodes[0]][axis];
        displacement[axis + 3] = m_resultDisplacements[nodes[1]][axis];
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
        * std::abs(gravityVector.dot(axis));
      // Sign convention: tension > 0, compression < 0. The self-weight term raises the
      // magnitude in the direction of the FE result, so |stress| is the same envelope as before.
      element.setEleStress(stress + std::copysign(gravityStress, stress));
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
    for (long long node = 0; node < static_cast<long long>(m_resultDisplacements.size()); ++node) {
      const auto& displacement = m_resultDisplacements[node];
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
