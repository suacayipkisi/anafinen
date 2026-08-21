#include "solveTruss_1D.hpp"
#include "element.hpp"
#include "node.hpp"
#include <Eigen/Core>
#include <Eigen/SparseCholesky>
#include <Eigen/SparseCore>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

void Truss_1D_Calculated::calculateDisplacements(){
    const std::uint32_t totalNodes = static_cast<std::uint32_t>(m_allNodes.size());
    const std::uint32_t totalDofs = totalNodes * 3;

    // first clean stiffness matrix according to the no movement nodes
    std::vector<bool> isFixed(totalDofs, false); //these columns and rows will be deleted
    for (std::uint32_t i{0}; i < totalNodes; ++i){
        const std::array<bool, 3>& nodeMovablility{m_allNodes[i].getMovable()};
        for(std::uint32_t j{0}; j < 3; ++j){
            if(!(nodeMovablility[j])){
                isFixed[3 * i + j] = true;
            }
        }
    }

    // give a new smaller matrix indexes
    std::vector<std::int32_t> remapTable(totalDofs, -1);
    std::uint32_t activeDofCount{0};
    for (std::uint32_t i{0}; i < totalDofs; ++i) {
        if (!isFixed[i]) {
            remapTable[i] = static_cast<std::int32_t>(activeDofCount++);
        }
    }

    // filter and shift coordinates in a single linear pass
    std::vector<Eigen::Triplet<double>> reducedTriplets;
    reducedTriplets.reserve(m_globalStiffnessMatrix.size());

    for (const auto& triplet : m_globalStiffnessMatrix) {
        auto r = static_cast<std::uint32_t>(triplet.row());
        auto c = static_cast<std::uint32_t>(triplet.col());

        // keep triplet only if both row and column DOFs are free
        if (!isFixed[r] && !isFixed[c]) {
            reducedTriplets.emplace_back(
                remapTable[r],
                remapTable[c],
                triplet.value()
            );
        }
    }

    // store reduced stiffness matrix
    Eigen::SparseMatrix<double> reducedStiffnessMatrix;
    reducedStiffnessMatrix.setFromTriplets(reducedTriplets.begin(), reducedTriplets.end());
    reducedStiffnessMatrix.makeCompressed();

    //reduce force vector and store
    std::vector<double> reducedForce;
    for(std::size_t i{0}; i < m_forceVec.size(); ++i){
        if(isFixed[i]){
            reducedForce.emplace_back(m_forceVec[i]);
        }
    }
    Eigen::VectorXd reducedForceVec(static_cast<Eigen::Index>(reducedForce.size()));
    for (Eigen::Index i{0}; i < reducedForceVec.size(); ++i) {
        reducedForceVec[i] = reducedForce[static_cast<std::size_t>(i)];
    }

    // solve F = Kd ===> K = L x L_tanspose, 
    Eigen::SimplicialLLT<Eigen::SparseMatrix<double>> choleskySolver;
    choleskySolver.compute(reducedStiffnessMatrix);
    // now solve and find displacements
    Eigen::VectorXd reducedDisplacements = choleskySolver.solve(reducedForceVec);

    // now refill real displacement container(vec<arr_3>)
    Eigen::VectorXd d_full = Eigen::VectorXd::Zero(totalDofs);
    for (std::uint32_t i{0}; i < totalDofs; ++i) {
        if (!isFixed[i]) {
            std::int32_t reduced_idx = remapTable[i];
            d_full(i) = reducedDisplacements(reduced_idx);
        }
    }
    for(std::uint32_t i{0}; i < m_resultDisplacements.size(); ++i){
        for(std::uint8_t j{0}; j < 3; ++j){
            m_resultDisplacements[i][j] = d_full[3 * i + j];
        }
    }
}

void Truss_1D_Calculated::calculateElementForces(){
    for(std::size_t eleNum{0}; eleNum < m_forceVec.size(); ++eleNum){
        TrussElement_1D& element = m_allElements[eleNum];
        // set element global disp vec (6x1) d
        Eigen::Vector<double, 6> elementGlobalDispVec;
        const std::array<std::shared_ptr<Node>, 2> elementNodes{element.getEleNodes()};
        const std::uint32_t nodeID_1{elementNodes[0]->getNodeID()};
        const std::uint32_t nodeID_2{elementNodes[1]->getNodeID()};
        for(std::uint8_t i{0}; i < 3; ++i){
            elementGlobalDispVec[i] = m_resultDisplacements[nodeID_1][i];
            elementGlobalDispVec[i + 3] = m_resultDisplacements[nodeID_2][i];
        }

        // set element transformation vec (1x6) T
        Eigen::Vector<float, 6> elementTransformationVec;
        std::array<float, 3> eleCosinuses;
        for(std::uint8_t i{0}; i < 3; ++i){
            elementTransformationVec[i] = eleCosinuses[i] * (-1);
            elementTransformationVec[i + 3] = eleCosinuses[i];
        }

        // axial deformation Txd
        double elongation{};
        for(std::uint8_t i{0}; i < 6; ++i){
            elongation += elementTransformationVec[i] * elementGlobalDispVec[i];
        }
        element.setEleElongation(elongation);
        element.setEleAxialForce(
            elongation / 
            element.getEleLength() * 
            element.getEleProperties()->getElasticityModulues() * 
            element.getEleCrossSection()
        );
    }
}
