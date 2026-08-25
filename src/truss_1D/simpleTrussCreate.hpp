#pragma once

#include "element.hpp"
#include "node.hpp"
#include <cstdint>
#include <array>
#include <memory>
#include <vector>

// class SimpleTrussNode: public Node{
// private:
//     std::array<std::uint32_t, 3> m_nodeVirtualLocation;
// public:
//     // Explicit constructor matching the forwarded arguments:
//     SimpleTrussNode(const std::uint32_t ID, 
//                     const double locX, 
//                     const double locY, 
//                     const double locZ)
//         : Node(ID, locX, locY, locZ), m_nodeVirtualLocation{} 
//     {}

//     // Constructor with virtual location directly included:
//     SimpleTrussNode(const std::uint32_t ID, 
//                     const double locX, 
//                     const double locY, 
//                     const double locZ,
//                     const std::array<std::uint32_t, 3>& virtualLoc)
//         : Node(ID, locX, locY, locZ), m_nodeVirtualLocation(virtualLoc) 
//     {}

//     inline void setVirtualLoc(const std::array<std::uint32_t, 3>& virtualLoc){ m_nodeVirtualLocation = virtualLoc;}
//     inline const std::array<std::uint32_t, 3>& getVirtualLoc() const {return m_nodeVirtualLocation;}
// };

class SimpleTruss{
private:
    std::array<std::uint32_t, 3> m_cubeNum{1, 1, 1}; // x y z added unit truss cube, cannot be zero none of them
    std::vector<std::shared_ptr<Node>> m_allNodes;
    std::vector<std::shared_ptr<TrussElement_1D>> m_allElements;
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