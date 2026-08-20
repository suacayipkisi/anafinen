#pragma once

#include <array>
#include <memory>
class Node{
private:
    std::array<double, 3> m_Location{};
    std::array<double, 3> m_displacement{};

    // node forces are the total forces applies on this node
    std::array<double, 3> m_force{};

};

const double nodeDistance(std::array<std::shared_ptr<Node>, 2> nodes);
