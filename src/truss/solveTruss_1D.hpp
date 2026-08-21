#pragma once

#include "element.hpp"
#include "node.hpp"
#include <Eigen/SparseCore>
#include <vector>
#include <array>

class Truss_1D_Calculated{
private:
    std::vector<double> m_forceVec;
    std::vector<Eigen::Triplet<double>> m_globalStiffnessMatrix;
    std::vector<Node> m_allNodes;
    std::vector<TrussElement_1D> m_allElements;

    std::vector<std::array<double, 3>> m_resultDisplacements;
    std::vector<std::array<double, 3>> m_resultForces;
public:
    Truss_1D_Calculated(
        const std::vector<double>& forceVec,
        const std::vector<Eigen::Triplet<double>>& stiffnessMatrix,
        const std::vector<Node>& allNodes,
        const std::vector<TrussElement_1D> allElements
    ): 
        m_forceVec(forceVec),
        m_globalStiffnessMatrix(stiffnessMatrix),
        m_allNodes(allNodes),
        m_allElements(allElements)
    {}

    void calculateDisplacements();
    
    void calculateElementForces();
};
