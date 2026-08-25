#pragma once

#include "element.hpp"
#include "node.hpp"
#include <Eigen/SparseCore>
#include <vector>
#include <array>

class Truss_1D_Container{
private:
    std::vector<double> m_forceVec;
    std::vector<Eigen::Triplet<double>> m_globalStiffnessMatrix;
    std::vector<Node> m_allNodes;
    std::vector<TrussElement_1D> m_allElements;

    std::vector<std::array<double, 3>> m_resultDisplacements;
    std::vector<std::array<double, 3>> m_resultForces;
public:
    Truss_1D_Container(
        const std::vector<double>& forceVec,
        const std::vector<Node>& allNodes,
        const std::vector<TrussElement_1D> allElements
    ): 
        m_forceVec(forceVec),
        m_allNodes(allNodes),
        m_allElements(allElements)
    {}

    void assembleStiffness(
        const std::vector<TrussElement_1D>& elements, 
        const std::uint32_t nodeNum
    );

    void calculateDisplacements();
    
    void calculateElementForcesAndStress();
};
