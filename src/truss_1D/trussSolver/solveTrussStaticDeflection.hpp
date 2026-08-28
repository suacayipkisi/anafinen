#pragma once

#include "../element.hpp"
#include "../node.hpp"

#include <Eigen/SparseCore>
#include <span>
#include <vector>
#include <array>

class Truss_1D_Container{
private:
    std::span<const double> m_forceVec;
    std::span<Node> m_allNodes;
    std::span<TrussElement_1D> m_allElements;

    std::vector<Eigen::Triplet<double>> m_globalStiffnessMatrix;
    std::vector<std::array<double, 3>> m_resultDisplacements;
    std::vector<std::array<double, 3>> m_resultForces;
public:
    Truss_1D_Container(
        std::span<const double> forceVec,
        std::span<Node> allNodes,
        std::span<TrussElement_1D> allElements
    ): 
        m_forceVec(forceVec),
        m_allNodes(allNodes),
        m_allElements(allElements)
    {}

    void assembleStiffness(
        const std::vector<TrussElement_1D>& elements,
        std::span<const Material> allMaterials
    );

    void calculateDisplacements();
    void calculateElementForcesAndStress(const std::span<const Material> allMaterials);
};
