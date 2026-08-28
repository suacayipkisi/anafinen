#include "solveTrussStaticDeflection.hpp"
#include "../element.hpp"
#include "../../log/anaf_info.h"

#include <Eigen/Core>
#include <Eigen/Sparse>
#include <Eigen/SparseCholesky>
#include <Eigen/SparseCore>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>
#include <omp.h>

void Truss_1D_Container::assembleStiffness(const std::vector<TrussElement_1D>& elements, const std::span<const Material> allMaterials) {
    const std::size_t elementNum = elements.size();
    const std::size_t totalTriplets = elementNum * 36;
    std::vector<Eigen::Triplet<double>> globalStiffnessMatrix(totalTriplets);
    const auto nodeNum{m_allNodes.size()};

    #pragma omp parallel for schedule(static)
    for (std::size_t index = 0; index < elementNum; ++index) {
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
    
    anaf_info("Global Stiffness Matrix Created, size: {}x{}", (nodeNum * 3), (nodeNum * 3));
    m_globalStiffnessMatrix = std::move(globalStiffnessMatrix);
}

void Truss_1D_Container::calculateDisplacements() {
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

    // parallel filtering of triplets via private vector merge
    std::vector<Eigen::Triplet<double>> reducedTriplets;
    
    #pragma omp parallel
    {
        std::vector<Eigen::Triplet<double>> localReducedTriplets;
        localReducedTriplets.reserve(m_globalStiffnessMatrix.size() / omp_get_num_threads());

        #pragma omp for schedule(static) nowait
        for (std::size_t i = 0; i < m_globalStiffnessMatrix.size(); ++i) {
            const auto& triplet = m_globalStiffnessMatrix[i];
            auto r = static_cast<std::uint32_t>(triplet.row());
            auto c = static_cast<std::uint32_t>(triplet.col());

            if (!isFixed[r] && !isFixed[c]) {
                localReducedTriplets.emplace_back(
                    remapTable[r],
                    remapTable[c],
                    triplet.value()
                );
            }
        }

        #pragma omp critical
        reducedTriplets.insert(reducedTriplets.end(), localReducedTriplets.begin(), localReducedTriplets.end());
    }

    Eigen::SparseMatrix<double> reducedStiffnessMatrix(activeDofCount, activeDofCount);
    reducedStiffnessMatrix.setFromTriplets(reducedTriplets.begin(), reducedTriplets.end());
    reducedStiffnessMatrix.makeCompressed();

    Eigen::VectorXd reducedForceVec(activeDofCount);
    for (std::size_t i = 0; i < totalDofs; ++i) {
        if (!isFixed[i]) {
            reducedForceVec[remapTable[i]] = m_forceVec[i];
        }
    }

    Eigen::SimplicialLDLT<Eigen::SparseMatrix<double>> solver;
    solver.compute(reducedStiffnessMatrix);
    Eigen::VectorXd reducedDisplacements = solver.solve(reducedForceVec);

    Eigen::VectorXd d_full = Eigen::VectorXd::Zero(totalDofs);
    #pragma omp parallel for schedule(static)
    for (std::uint32_t i = 0; i < totalDofs; ++i) {
        if (!isFixed[i]) {
            std::int32_t reduced_idx = remapTable[i];
            d_full(i) = reducedDisplacements(reduced_idx);
        }
    }

    #pragma omp parallel for schedule(static)
    for (std::uint32_t i = 0; i < totalNodes; ++i) {
        std::array<double, 3> disp = {
            d_full[3 * i + 0],
            d_full[3 * i + 1],
            d_full[3 * i + 2]
        };
        m_resultDisplacements[i] = disp;
        m_allNodes[i].setDisplacements(disp);
    }
}

void Truss_1D_Container::calculateElementForcesAndStress(const std::span<const Material> allMaterials) {
    const std::size_t totalElements = m_allElements.size();

    #pragma omp parallel for schedule(static)
    for (std::size_t eleNum = 0; eleNum < totalElements; ++eleNum) {
        auto& element = m_allElements[eleNum];
        const auto& elementNodes = element.getEleNodes();
        
        const std::uint32_t nodeID_1 = elementNodes[0];
        const std::uint32_t nodeID_2 = elementNodes[1];

        Eigen::Vector<double, 6> elementGlobalDispVec;
        for (std::uint8_t i = 0; i < 3; ++i) {
            elementGlobalDispVec[i]     = m_resultDisplacements[nodeID_1][i];
            elementGlobalDispVec[i + 3] = m_resultDisplacements[nodeID_2][i];
        }

        // fetch precomputed cosines directly from element
        const auto& eleCosinuses = element.getEleCosinuses();

        Eigen::Vector<double, 6> elementTransformationVec;
        elementTransformationVec << -static_cast<double>(eleCosinuses[0]), 
                                    -static_cast<double>(eleCosinuses[1]), 
                                    -static_cast<double>(eleCosinuses[2]),
                                    static_cast<double>(eleCosinuses[0]),  
                                    static_cast<double>(eleCosinuses[1]), 
                                    static_cast<double>(eleCosinuses[2]);

        const double elongation = elementTransformationVec.dot(elementGlobalDispVec);
        element.setEleElongation(elongation);

        const double eleCrossSection = element.getEleCrossSection();
        const double eleLength = element.getEleLength();
        const double elasticity = allMaterials[element.getEleProperties()].getElasticityModulues();

        double eleForce = (elongation / eleLength) * elasticity * eleCrossSection;
        const double eleStress = eleForce / eleCrossSection;
                          
        element.setEleAxialForce(eleForce);
        element.setEleStress(eleForce / element.getEleCrossSection());
    }
}
