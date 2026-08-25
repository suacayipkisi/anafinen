#pragma once

#include "node.hpp"
#include <array>
#include <memory>
class ForceApplied{
private:
    std::shared_ptr<Node> m_appliedNode;
    std::array<double, 3> m_force;
public:
    ForceApplied(std::shared_ptr<Node> node, std::array<double, 3> force):
        m_appliedNode(node), m_force(force)
    {}

    inline void updateForce(std::array<double, 3> force){m_force = force;}

    inline std::shared_ptr<Node> getApliedNode() const {return m_appliedNode;}
    inline const std::array<double, 3>& getForce() const {return m_force;}
};
