#pragma once

#include <memory>
class Node{
private:
    double m_nodeLocX{};
    double m_nodeLocY{};
    double m_nodeLocZ{};
};

const double nodeDistance(std::shared_ptr<Node> node_1, std::shared_ptr<Node> node_2);
