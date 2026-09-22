// Copyright (c) 2026 Ufuk Deniz Konuk
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
#include "solverPortfolio.hpp"
#include <log/anaf_info.hpp>

#include <Eigen/Core>
#include <Eigen/SparseCore>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <omp.h>
#include <span>
#include <vector>

namespace FEM::TRUSS {

  void Truss_1D_Container::assembleStiffness(
    const std::vector<TrussElement_1D>& elements,
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
        * allMaterials[element.getEleProperties()].getElasticityModulues()
        / element.getEleLength();
      const auto& cosines = element.getEleCosinuses();
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

  void Truss_1D_Container::considerWeight(
    const std::vector<TrussElement_1D>& elements,
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
      #pragma omp atomic update
      m_forceVec[dof1] += weight / 2.0;
      #pragma omp atomic update
      m_forceVec[dof2] += weight / 2.0;
    }
  }

  void Truss_1D_Container::calculateDisplacements(const std::stop_token stopToken) {
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

    std::vector<bool> isFixed(totalDofs, false);
    for (std::uint32_t node = 0; node < nodeCount; ++node) {
      const auto& movable = m_allNodes[node].getMovable();
      for (std::uint32_t axis = 0; axis < 3; ++axis) {
        isFixed[3 * node + axis] = !movable[axis];
      }
    }

    std::vector<std::int32_t> remapTable(totalDofs, -1);
    std::uint32_t activeDofCount = 0;
    for (std::uint32_t dof = 0; dof < totalDofs; ++dof) {
      if (!isFixed[dof]) remapTable[dof] = static_cast<std::int32_t>(activeDofCount++);
    }

    const int threadCount = omp_get_max_threads();
    std::vector<std::size_t> validCounts(threadCount, 0);
    #pragma omp parallel
    {
      const int thread = omp_get_thread_num();
      #pragma omp for schedule(static)
      for (long long index = 0; index < static_cast<long long>(m_globalStiffnessMatrix.size()); ++index) {
        const auto& triplet = m_globalStiffnessMatrix[index];
        if (!isFixed[triplet.row()] && !isFixed[triplet.col()]) ++validCounts[thread];
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
        const auto& triplet = m_globalStiffnessMatrix[index];
        if (!isFixed[triplet.row()] && !isFixed[triplet.col()]) {
          reducedTriplets[output++] = {
            remapTable[triplet.row()], remapTable[triplet.col()], triplet.value()
          };
        }
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
    for (long long dof = 0; dof < totalDofs; ++dof) {
      if (!isFixed[dof]) reducedForce[remapTable[dof]] = m_forceVec[dof];
    }

    Eigen::VectorXd reducedDisplacements(activeDofCount);
    const auto solverResult = SOLVER::solveSelected(
      reducedStiffnessMatrix,
      reducedForce,
      nodeCount,
      remapTable,
      stopToken,
      reducedDisplacements
    );

    if (!solverResult.converged) {
      anaf::LOG::error(
        "Stiffness solve failed using {}: {}",
        SOLVER::toString(solverResult.kind), solverResult.message
      );
      m_resultDisplacements.assign(nodeCount, {0.0, 0.0, 0.0});
      for (auto& node : m_allNodes) node.setDisplacements({0.0, 0.0, 0.0});
      return;
    }

    Eigen::VectorXd fullDisplacements = Eigen::VectorXd::Zero(totalDofs);
    #pragma omp parallel for schedule(static)
    for (long long dof = 0; dof < totalDofs; ++dof) {
      if (!isFixed[dof]) fullDisplacements[dof] = reducedDisplacements[remapTable[dof]];
    }

    #pragma omp parallel for schedule(static)
    for (long long node = 0; node < nodeCount; ++node) {
      const std::array<double, 3> displacement{
        fullDisplacements[3 * node],
        fullDisplacements[3 * node + 1],
        fullDisplacements[3 * node + 2]
      };
      m_resultDisplacements[node] = displacement;
      m_allNodes[node].setDisplacements(displacement);
    }
  }

  void Truss_1D_Container::calculateElementForcesAndStress(
    const std::span<const anaf::MATERIAL::Material> materials,
    const Eigen::Vector3d gravityVector
  ) {
    #pragma omp parallel for schedule(static)
    for (long long index = 0; index < static_cast<long long>(m_allElements.size()); ++index) {
      auto& element = m_allElements[index];
      const auto& nodes = element.getEleNodes();
      const auto& direction = element.getEleCosinuses();
      Eigen::Vector<double, 6> displacement;
      for (std::uint8_t axis = 0; axis < 3; ++axis) {
        displacement[axis] = m_resultDisplacements[nodes[0]][axis];
        displacement[axis + 3] = m_resultDisplacements[nodes[1]][axis];
      }

      const Eigen::Vector3d axis(
        static_cast<double>(direction[0]),
        static_cast<double>(direction[1]),
        static_cast<double>(direction[2])
      );
      Eigen::Vector<double, 6> transformation;
      transformation << -axis[0], -axis[1], -axis[2], axis[0], axis[1], axis[2];
      const double elongation = transformation.dot(displacement);
      element.setEleElongation(elongation);

      const auto& material = materials[element.getEleProperties()];
      const double area = element.getEleCrossSection();
      const double length = element.getEleLength();
      const double force = (elongation / length) * material.getElasticityModulues() * area;
      const double stress = force / area;
      const double gravityStress = 0.5 * material.getDensity() * length
        * std::abs(gravityVector.dot(axis));
      element.setEleAxialForce(std::abs(force) + gravityStress * area);
      element.setEleStress(std::abs(stress) + gravityStress);
    }
  }

  void Truss_1D_Container::runValidator(
    const std::span<const anaf::MATERIAL::Material> materials
  ) {
    double internalEnergy = 0.0;
    #pragma omp parallel for schedule(static) reduction(+:internalEnergy)
    for (long long index = 0; index < static_cast<long long>(m_allElements.size()); ++index) {
      const auto& element = m_allElements[index];
      const double stiffness = materials[element.getEleProperties()].getElasticityModulues()
        * element.getEleCrossSection() / element.getEleLength();
      const double elongation = element.getEleElongation();
      internalEnergy += 0.5 * stiffness * elongation * elongation;
    }
    m_elasticDeformationEnergy_internal = internalEnergy;

    double externalWork = 0.0;
    #pragma omp parallel for schedule(static) reduction(+:externalWork)
    for (long long node = 0; node < static_cast<long long>(m_resultDisplacements.size()); ++node) {
      const auto& displacement = m_resultDisplacements[node];
      const std::size_t base = 3 * node;
      externalWork += m_forceVec[base] * displacement[0]
        + m_forceVec[base + 1] * displacement[1]
        + m_forceVec[base + 2] * displacement[2];
    }
    m_workDone_external = externalWork;

    const double externalEnergy = 0.5 * externalWork;
    m_energyDiff = std::abs(internalEnergy - externalEnergy);
    const double scale = std::max({std::abs(internalEnergy), std::abs(externalEnergy), 1.0});
    m_energyRelativeDiff = m_energyDiff / scale;
    m_isCalculationValid = m_energyDiff <= 1e-12 || m_energyRelativeDiff <= 1e-7;
  }

} // namespace FEM::TRUSS
