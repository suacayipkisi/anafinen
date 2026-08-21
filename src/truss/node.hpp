#pragma once

#include <array>
#include <cstdint>
#include <memory>
class Node{
private:
    std::uint32_t m_nodeID{}; // implemented with assuming definition starting with "0 (zero)"
    std::array<double, 3> m_Location{};
    std::array<bool, 3> m_isMovable{true, true, true};

    std::array<double, 3> m_displacement{};

    // node forces are the total forces applies on this node
    std::array<double, 3> m_force{};
public:
    Node(
        const std::uint32_t ID,
        const double locX,
        const double locY,
        const double locZ,
        const double forceX,
        const double forceY,
        const double forceZ
    ):
        m_nodeID(ID),
        m_Location({locX, locY, locZ}),
        m_force({forceX, forceY, forceZ})
    {}

    Node(
        const std::uint32_t ID,
        const double locX,
        const double locY,
        const double locZ,
        const double forceX,
        const double forceY,
        const double forceZ,
        const double dispX,
        const double dispY,
        const double dispZ
    ):
        m_nodeID(ID),
        m_Location({locX, locY, locZ}),
        m_force({forceX, forceY, forceZ}),
        m_displacement({dispX, dispY, dispZ})
    {}

    inline void setMovable(const std::array<bool, 3> movable) {m_isMovable = movable;}

    const std::uint32_t getNodeID() const {return m_nodeID;}
    const std::array<double, 3>& getLocation() const {return m_Location;}
    const std::array<double, 3>& getDisplacmenet() const {return m_displacement;}
    const std::array<double, 3>& getForce() const {return m_force;}
    const std::array<bool, 3>& getMovable() const {return m_isMovable;}
};

const double nodeDistance(std::array<std::shared_ptr<Node>, 2> nodes);
