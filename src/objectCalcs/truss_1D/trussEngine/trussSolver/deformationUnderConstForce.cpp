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
#include <cstdlib>
#include <trussProperties/element.hpp>
#include <log/anaf_info.hpp>

#include <Eigen/Core>
#include <Eigen/Sparse>
#include <Eigen/SparseCholesky>
#include <Eigen/SparseCore>
#ifdef ANAFINEN_HAS_CHOLMOD
#include <Eigen/CholmodSupport>
#endif
#include <array>
#include <cstddef>
#include <cstdint>
#include <algorithm>
#include <cmath>
#include <span>
#include <vector>
#include <omp.h>

namespace FEM::TRUSS {

  void Truss_1D_Container::assembleStiffness(const std::vector<TrussElement_1D>& elements, const std::span<const anaf::MATERIAL::Material> allMaterials) {
    const std::size_t elementNum = elements.size();
    const std::size_t totalTriplets = elementNum * 36;
    std::vector<Eigen::Triplet<double>> globalStiffnessMatrix(totalTriplets);
    const auto nodeNum{m_allNodes.size()};

    #pragma omp parallel for schedule(static)
    for (long long index = 0; index < elementNum; ++index) {
      const auto& element = elements[index];
      const auto& elementNodes = element.getEleNodes();
      
      const std::uint32_t globalMatrixIndex_1 = elementNodes[0];
      const std::uint32_t globalMatrixIndex_2 = elementNodes[1];
      
      const std::uint32_t zeroPos_1 = 3 * globalMatrixIndex_1;
      const std::uint32_t zeroPos_2 = 3 * globalMatrixIndex_2;

      double AE_L = (element.getEleCrossSection() * allMaterials[element.getEleProperties()].getElasticityModulues()) / element.getEleLength();
      const auto& cos = element.getEleCosinuses();

      std::size_t tripletOffset = index * 36;
      std::size_t localCounter = 0;

      // filling global stiff. matrix directly (not calculate and assemble element by element)
      for (std::size_t lambda_topIndex = 0; lambda_topIndex < 3; ++lambda_topIndex) {
        for (std::size_t lambda_rightIndex = 0; lambda_rightIndex < 3; ++lambda_rightIndex) {
          double val = cos[lambda_rightIndex] * cos[lambda_topIndex] * AE_L;

          // lambda top left
          globalStiffnessMatrix[tripletOffset + localCounter++] = 
            Eigen::Triplet<double>(zeroPos_1 + lambda_rightIndex, zeroPos_1 + lambda_topIndex, val);
          
          // -lambda top right 
          globalStiffnessMatrix[tripletOffset + localCounter++] = 
            Eigen::Triplet<double>(zeroPos_1 + lambda_rightIndex, zeroPos_2 + lambda_topIndex, -val);
          
          // -lambda bottom left
          globalStiffnessMatrix[tripletOffset + localCounter++] = 
            Eigen::Triplet<double>(zeroPos_2 + lambda_rightIndex, zeroPos_1 + lambda_topIndex, -val);
          
          // lambda bottom right
          globalStiffnessMatrix[tripletOffset + localCounter++] = 
            Eigen::Triplet<double>(zeroPos_2 + lambda_rightIndex, zeroPos_2 + lambda_topIndex, val);
        }
      }
    }
    
    anaf::LOG::info("Global Stiffness Matrix Created, size: {}x{}", (nodeNum * 3), (nodeNum * 3));
    m_globalStiffnessMatrix = std::move(globalStiffnessMatrix);
  }

  void Truss_1D_Container::considerWeight(const std::vector<TrussElement_1D>& elements, std::span<const anaf::MATERIAL::Material> allMaterial) {
    constexpr double gravityConst {-9.80665};
    #pragma omp parallel for schedule(static)
    for (long long eleNum = 0; eleNum < elements.size(); ++eleNum) {
      auto& element = elements[eleNum];
      double weight = allMaterial[element.getEleProperties()].getDensity() * element.getEleCrossSection() * element.getEleLength() * gravityConst;
      const auto& nodes = element.getEleNodes();
      std::uint32_t dofNum1 = 3 * nodes[0] + 1;
      std::uint32_t dofNum2 = 3 * nodes[1] + 1;
      m_forceVec[dofNum1] += weight / 2;
      m_forceVec[dofNum2] += weight / 2;
    }
  }

  void Truss_1D_Container::calculateDisplacements() {
    #pragma omp parallel
    {
      #pragma omp single
      {
        anaf::LOG::info(
          "OpenMP team: {}, max threads: {}",
          omp_get_num_threads(),
          omp_get_max_threads()
        );
      }
    }
    const std::uint32_t totalNodes = static_cast<std::uint32_t>(m_allNodes.size());
    m_resultDisplacements.resize(totalNodes);
    const std::uint32_t totalDofs = totalNodes * 3;

    std::vector<bool> isFixed(totalDofs, false);
    for (std::uint32_t i = 0; i < totalNodes; ++i) {
      const auto& nodeMovablility = m_allNodes[i].getMovable();
      for (std::uint32_t j = 0; j < 3; ++j) {
        if (!nodeMovablility[j]) {
          isFixed[3 * i + j] = true;
        }
      }
    }

    std::vector<std::int32_t> remapTable(totalDofs, -1);
    std::uint32_t activeDofCount = 0;
    for (std::uint32_t i = 0; i < totalDofs; ++i) {
      if (!isFixed[i]) {
        remapTable[i] = static_cast<std::int32_t>(activeDofCount++);
      }
    }

    // Count valid triplets first so the parallel fill can write directly.
    const int threadCount = omp_get_max_threads();
    std::vector<std::size_t> tripletCounts(threadCount, 0);

    #pragma omp parallel
    {
      const int threadIndex = omp_get_thread_num();
      std::size_t validTripletCount = 0;

      #pragma omp for schedule(static)
      for (long long i = 0; i < m_globalStiffnessMatrix.size(); ++i) {
        const auto& triplet = m_globalStiffnessMatrix[i];
        auto r = static_cast<std::uint32_t>(triplet.row());
        auto c = static_cast<std::uint32_t>(triplet.col());

        if (!isFixed[r] && !isFixed[c]) {
          ++validTripletCount;
        }
      }

      tripletCounts[threadIndex] = validTripletCount;
    }

    std::vector<std::size_t> tripletOffsets(threadCount + 1, 0);
    for (int i = 0; i < threadCount; ++i) {
      tripletOffsets[i + 1] = tripletOffsets[i] + tripletCounts[i];
    }

    std::vector<Eigen::Triplet<double>> reducedTriplets(tripletOffsets.back());

    #pragma omp parallel
    {
      const int threadIndex = omp_get_thread_num();
      std::size_t outputIndex = tripletOffsets[threadIndex];

      #pragma omp for schedule(static)
      for (long long i = 0; i < m_globalStiffnessMatrix.size(); ++i) {
        const auto& triplet = m_globalStiffnessMatrix[i];
        auto r = static_cast<std::uint32_t>(triplet.row());
        auto c = static_cast<std::uint32_t>(triplet.col());

        if (!isFixed[r] && !isFixed[c]) {
          reducedTriplets[outputIndex++] = Eigen::Triplet<double>(
            remapTable[r],
            remapTable[c],
            triplet.value()
          );
        }
      }
    }

    Eigen::SparseMatrix<double> reducedStiffnessMatrix(activeDofCount, activeDofCount);
    reducedStiffnessMatrix.setFromTriplets(reducedTriplets.begin(), reducedTriplets.end());
    reducedStiffnessMatrix.makeCompressed();

    Eigen::VectorXd reducedForceVec(activeDofCount);
    #pragma omp parallel for schedule(static)
    for (long long i = 0; i < totalDofs; ++i) {
      if (!isFixed[i]) {
        reducedForceVec[remapTable[i]] = m_forceVec[i];
      }
    }

  #ifdef ANAFINEN_HAS_CHOLMOD
    Eigen::CholmodSupernodalLLT<Eigen::SparseMatrix<double>> solver;
  #else
    Eigen::SimplicialLDLT<Eigen::SparseMatrix<double>> solver;
  #endif
    solver.compute(reducedStiffnessMatrix);
    if (solver.info() != Eigen::Success) {
      anaf::LOG::error("Stiffness matrix factorization failed; the truss may be under-constrained");
      m_resultDisplacements.assign(totalNodes, {0.0, 0.0, 0.0});
      for (auto& node : m_allNodes) {
        node.setDisplacements({0.0, 0.0, 0.0});
      }
      return;
    }

    Eigen::VectorXd reducedDisplacements = solver.solve(reducedForceVec);
    if (solver.info() != Eigen::Success || !reducedDisplacements.allFinite()) {
      anaf::LOG::error("Stiffness solve produced non-finite displacements; the truss may be under-constrained");
      m_resultDisplacements.assign(totalNodes, {0.0, 0.0, 0.0});
      for (auto& node : m_allNodes) {
        node.setDisplacements({0.0, 0.0, 0.0});
      }
      return;
    }

    Eigen::VectorXd d_full = Eigen::VectorXd::Zero(totalDofs);
    #pragma omp parallel for schedule(static)
    for (long long i = 0; i < totalDofs; ++i) {
      if (!isFixed[i]) {
        std::int32_t reduced_idx = remapTable[i];
        d_full(i) = reducedDisplacements(reduced_idx);
      }
    }

    #pragma omp parallel for schedule(static)
    for (long long i = 0; i < totalNodes; ++i) {
      std::array<double, 3> disp = {
        d_full[3 * i + 0],
        d_full[3 * i + 1],
        d_full[3 * i + 2]
      };
      m_resultDisplacements[i] = disp;
      m_allNodes[i].setDisplacements(disp);
    }
  }

  void Truss_1D_Container::calculateElementForcesAndStress(
    const std::span<const anaf::MATERIAL::Material> allMaterials, 
    const Eigen::Vector3d gravityVector
  ) {
    const std::size_t totalElements = m_allElements.size();

    #pragma omp parallel for schedule(static)
    for (long long eleNum = 0; eleNum < totalElements; ++eleNum) {
      auto& element = m_allElements[eleNum];
      const auto& elementNodes = element.getEleNodes();
      
      const std::uint32_t nodeID_1 = elementNodes[0];
      const std::uint32_t nodeID_2 = elementNodes[1];

      Eigen::Vector<double, 6> elementGlobalDispVec;
      for (std::uint8_t i = 0; i < 3; ++i) {
        elementGlobalDispVec[i] = m_resultDisplacements[nodeID_1][i];
        elementGlobalDispVec[i + 3] = m_resultDisplacements[nodeID_2][i];
      }

      // fetch precomputed cosines directly from element
      const auto& eleCosinuses = element.getEleCosinuses();

      const Eigen::Vector3d axialDir(
        static_cast<double>(eleCosinuses[0]),
        static_cast<double>(eleCosinuses[1]),
        static_cast<double>(eleCosinuses[2])
      );

      Eigen::Vector<double, 6> elementTransformationVec;
      elementTransformationVec << -axialDir[0], -axialDir[1], -axialDir[2],
                    axialDir[0], axialDir[1], axialDir[2];

      const double elongation = elementTransformationVec.dot(elementGlobalDispVec);
      element.setEleElongation(elongation);

      const double eleCrossSection = element.getEleCrossSection();
      const double eleLength = element.getEleLength();

      const auto& material = allMaterials[element.getEleProperties()];
      const double elasticity = material.getElasticityModulues();
      const double density = material.getDensity();

      double baseEleForce = (elongation / eleLength) * elasticity * eleCrossSection;
      const double baseEleStress = baseEleForce / eleCrossSection;

      const double axialGravity = gravityVector.dot(axialDir);
      const double deltaStress = 0.5 * density * eleLength * std::abs(axialGravity);
      const double deltaForce = deltaStress * eleCrossSection;

      const double maxStressMagnitude = std::abs(baseEleStress) + deltaStress;
      const double maxForceMagnitude = std::abs(baseEleForce) + deltaForce;
              
      element.setEleAxialForce(maxForceMagnitude);
      element.setEleStress(maxStressMagnitude);
    }
  }

  void Truss_1D_Container::runValidator(const std::span<const anaf::MATERIAL::Material> allMaterials) {
    const std::size_t totalElements = m_allElements.size();
    const std::size_t totalNodes = m_resultDisplacements.size();

    // internal strain energy (U = 0.5 * k * delta_L^2)
    // uses element elongation directly to avoid peak-force corruption from self-weight post-processing
    double totalInternalEnergy = 0.0;

    #pragma omp parallel for schedule(static) reduction(+:totalInternalEnergy)
    for (long long eleIndex = 0; eleIndex < static_cast<long long>(totalElements); ++eleIndex) {
      const auto& element = m_allElements[eleIndex];
      const double deltaL = element.getEleElongation();
      const double length = element.getEleLength();
      const double area = element.getEleCrossSection();
      const double elasticity = allMaterials[element.getEleProperties()].getElasticityModulues();

      // k = (E * A) / L -> U = 0.5 * k * deltaL^2
      const double elementStiffness = (elasticity * area) / length;
      totalInternalEnergy += 0.5 * elementStiffness * deltaL * deltaL;
    }
    m_elasticDeformationEnergy_internal = totalInternalEnergy;

    // external work (w_ext = 0.5 * F . d)
    // m_forceVec already contains both external loads and assembled self-weight nodal loads
    double totalExternalWork = 0.0;

    #pragma omp parallel for schedule(static) reduction(+:totalExternalWork)
    for (long long nodeIndex = 0; nodeIndex < static_cast<long long>(totalNodes); ++nodeIndex) {
      const auto& u = m_resultDisplacements[nodeIndex];
      const std::size_t baseIdx = 3 * nodeIndex;

      totalExternalWork += m_forceVec[baseIdx] * u[0]
                +  m_forceVec[baseIdx + 1] * u[1]
                +  m_forceVec[baseIdx + 2] * u[2];
    }

    m_workDone_external = totalExternalWork;
    const double externalEnergy = 0.5 * m_workDone_external;

    // validation
    m_energyDiff = std::abs(m_elasticDeformationEnergy_internal - externalEnergy);

    const double energyScale = std::max({
      std::abs(m_elasticDeformationEnergy_internal),
      std::abs(externalEnergy),
      1.0
    });
    m_energyRelativeDiff = m_energyDiff / energyScale;

    constexpr double absoluteTolerance = 1e-12;
    constexpr double relativeTolerance = 1e-7;
    m_isCalculationValid = (m_energyDiff <= absoluteTolerance) || (m_energyRelativeDiff <= relativeTolerance);
  }

} // namespace FEM::TRUSS end
