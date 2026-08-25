#pragma once

#include "../element.hpp"
#include "../node.hpp"

#include <cstdint>
#include <array>
#include <memory>
#include <vector>

class SimpleTruss{
private:
    std::array<std::uint32_t, 3> m_cubeNum{1, 1, 1}; // x y z added unit truss cube, cannot be zero none of them
    std::vector<Node> m_allNodes;
    std::vector<TrussElement_1D> m_allElements;
    std::shared_ptr<Material> m_type;
    double m_cubeEdgeLength{};
    double m_area{};
public:
    SimpleTruss(
        std::array<std::uint32_t, 3> cubeNum,
        double cubeEdgeLength,
        double area,
        std::shared_ptr<Material> type
    ): 
        m_cubeNum(cubeNum),
        m_cubeEdgeLength(cubeEdgeLength),
        m_area(area),
        m_type(type)
    {}

    void setTruss();
};
