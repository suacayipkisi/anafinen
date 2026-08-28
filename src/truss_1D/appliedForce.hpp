#pragma once

#include "node.hpp"
#include <array>
#include <span>
class ForceApplied{
private:
    std::span<const Node> m_nodes;
    std::array<double, 3> m_force;
public:
    ForceApplied(std::span<const Node> nodes, std::array<double, 3> force):
        m_nodes(nodes), m_force(force)
    {}

    inline void updateForce(std::array<double, 3> force){m_force = force;}
    inline std::span<const Node> getApliedNode() const {return m_nodes;}
    inline const std::array<double, 3>& getForce() const {return m_force;}
};
