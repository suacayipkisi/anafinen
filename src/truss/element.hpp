#pragma once

#include "../material/properties.hpp"
#include "node.hpp"
#include "../anafInfo.hpp"
#include <memory>

class TrussElement{
private:
    double m_elementLength{};
    double m_elementCrossSectionArea{};
    double m_elementElongation{};
    std::shared_ptr<Material> m_elementProperties;
    std::shared_ptr<Node> m_node_1;
    std::shared_ptr<Node> m_node_2;
protected:
public:
    TrussElement(
        std::shared_ptr<Material> type,
        double area,
        std::shared_ptr<Node> node_1,
        std::shared_ptr<Node> node_2
    ):
        m_elementProperties(type),
        m_elementCrossSectionArea(area),
        m_node_1(node_1),
        m_node_2(node_2)
    {
        m_elementLength = nodeDistance(m_node_1, m_node_2);
    }

    // static determined properties of element
    inline void setNode1(std::shared_ptr<Node> node1_ID) {m_node_1 = node1_ID;}
    inline void setNode2(std::shared_ptr<Node> node2_ID) {m_node_2 = node2_ID;}
    inline void setEleLength() {
        if(m_node_1 && m_node_2){
            m_elementLength = nodeDistance(m_node_1, m_node_2);
        }
        else{
            anafLog::warn("You have not initialize all required nodes yet for calculating distance between them!");
        }
    }
    inline void setEleCrossSectionArea(const double area) {m_elementCrossSectionArea = area;}
    inline void setElementProperty(std::shared_ptr<Material> material) {m_elementProperties = material;}

    // calcuilated properties of element
    inline void setElementElongation(const double elongation) {m_elementElongation = elongation;}
};
