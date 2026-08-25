#include "solveTruss_1D_StaticDeflection.hpp"
#include "element.hpp"
#include "node.hpp"
#include "../anafInfo.hpp"

#include <Eigen/Core>
#include <Eigen/Sparse>
#include <Eigen/SparseCholesky>
#include <Eigen/SparseCore>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>
#include <omp.h>

void Truss_1D_Container::assembleStiffness(const std::vector<TrussElement_1D>& elements, const std::uint32_t nodeNum) {
    const std::size_t elementNum = elements.size();
    
    // each 1D truss element contributes 36 entries (6x6)
    const std::size_t totalTriplets = elementNum * 36;
    std::vector<Eigen::Triplet<double>> globalStiffnessMatrix(totalTriplets);

    #pragma omp parallel for schedule(static)
    for (std::size_t index = 0; index < elementNum; ++index) {
        const Eigen::Matrix<double, 6, 6>& elementStiffness = elements[index].getEleStiffness();
        const std::array<std::shared_ptr<Node>, 2>& elementNodes = elements[index].getEleNodes();
        
        const std::uint32_t globalMatrixIndex_1 = elementNodes[0]->getNodeID();
        const std::uint32_t globalMatrixIndex_2 = elementNodes[1]->getNodeID();
        
        // 3 DOFs per node (x, y, z)
        const std::uint32_t zeroPos_1 = 3 * globalMatrixIndex_1;
        const std::uint32_t zeroPos_2 = 3 * globalMatrixIndex_2;

        std::size_t tripletOffset = index * 36;
        std::size_t localCounter = 0;

        for (std::size_t lambda_topIndex = 0; lambda_topIndex < 3; ++lambda_topIndex) {
            for (std::size_t lambda_rightIndex = 0; lambda_rightIndex < 3; ++lambda_rightIndex) {
                globalStiffnessMatrix[tripletOffset + localCounter++] = 
                    Eigen::Triplet<double>(
                        zeroPos_1 + lambda_rightIndex, 
                        zeroPos_1 + lambda_topIndex, 
                        elementStiffness(
                            lambda_rightIndex, 
                            lambda_topIndex
                        )
                    );
                
                globalStiffnessMatrix[tripletOffset + localCounter++] = 
                    Eigen::Triplet<double>(
                        zeroPos_1 + lambda_rightIndex, 
                        zeroPos_2 + lambda_topIndex, 
                        elementStiffness(
                            lambda_rightIndex, 
                            lambda_topIndex + 3
                        )
                    );
                
                globalStiffnessMatrix[tripletOffset + localCounter++] = 
                    Eigen::Triplet<double>(zeroPos_2 + lambda_rightIndex, 
                        zeroPos_1 + lambda_topIndex, 
                        elementStiffness(
                            lambda_rightIndex + 3, 
                            lambda_topIndex
                        )
                    );
                
                globalStiffnessMatrix[tripletOffset + localCounter++] = 
                    Eigen::Triplet<double>(
                        zeroPos_2 + lambda_rightIndex, 
                        zeroPos_2 + lambda_topIndex, 
                        elementStiffness(
                            lambda_rightIndex + 3, 
                            lambda_topIndex + 3
                        )
                    );
            }
        }
    }
    
    anafLog::info("Global Stiffness Matrix Created, size: {}x{}", (nodeNum * 3), (nodeNum * 3));
    m_globalStiffnessMatrix = std::move(globalStiffnessMatrix);
}

void Truss_1D_Container::calculateDisplacements() {
    const std::uint32_t totalNodes = static_cast<std::uint32_t>(m_allNodes.size());
    const std::uint32_t totalDofs = totalNodes * 3;

    std::vector<bool> isFixed(totalDofs, false);
    for (std::uint32_t i = 0; i < totalNodes; ++i) {
        const std::array<bool, 3>& nodeMovablility = m_allNodes[i].getMovable();
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

    // Parallel filtering of triplets via private vector merge
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

void Truss_1D_Container::calculateElementForcesAndStress() {
    const std::size_t totalElements = m_allElements.size();

    #pragma omp parallel for schedule(static)
    for (std::size_t eleNum = 0; eleNum < totalElements; ++eleNum) {
        TrussElement_1D& element = m_allElements[eleNum];
        const std::array<std::shared_ptr<Node>, 2>& elementNodes = element.getEleNodes();
        
        const std::uint32_t nodeID_1 = elementNodes[0]->getNodeID();
        const std::uint32_t nodeID_2 = elementNodes[1]->getNodeID();

        Eigen::Vector<double, 6> elementGlobalDispVec;
        for (std::uint8_t i = 0; i < 3; ++i) {
            elementGlobalDispVec[i]     = m_resultDisplacements[nodeID_1][i];
            elementGlobalDispVec[i + 3] = m_resultDisplacements[nodeID_2][i];
        }

        // Fetch precomputed cosines directly from element
        const std::array<float, 3>& eleCosinuses = element.getEleCosinuses();

        Eigen::Vector<double, 6> elementTransformationVec;
        elementTransformationVec << -eleCosinuses[0], -eleCosinuses[1], -eleCosinuses[2],
                                     eleCosinuses[0],  eleCosinuses[1],  eleCosinuses[2];

        double elongation = elementTransformationVec.dot(elementGlobalDispVec);
        element.setEleElongation(elongation);

        double eleForce = (elongation / element.getEleLength()) * 
                          element.getEleProperties()->getElasticityModulues() * 
                          element.getEleCrossSection();
                          
        element.setEleAxialForce(eleForce);
        element.setEleStress(eleForce / element.getEleCrossSection());
    }
}
