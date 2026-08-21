#pragma once

#include "../material/properties.hpp"
#include "node.hpp"
#include "../anafInfo.hpp"
#include <Eigen/Core>
#include <memory>
#include <array>

// initializing 1D element in 3D space
// 2 nodes, one size_dimention
class TrussElement_1D{
private:
    std::array<float, 3> m_angle{0.0, 0.0, 0.0};
    double m_length{};
    double m_crossSectionArea{};
    double m_elongation{};
    std::shared_ptr<Material> m_properties;
    std::array<double, 2> m_nodeForce{};
    std::array<std::shared_ptr<Node>, 2> m_nodes{};
    Eigen::Matrix<double, 6, 6> m_stiffnessMatrix;
protected:
public:
    TrussElement_1D(
        std::shared_ptr<Material> type,
        double area,
        std::shared_ptr<Node> node_1,
        std::shared_ptr<Node> node_2
    ):
        m_properties(type),
        m_crossSectionArea(area),
        m_nodes({node_1, node_2})
    {
        m_length = nodeDistance(m_nodes);
    }

    // static determined properties of element
    inline void setNodes(std::array<std::shared_ptr<Node>, 2> nodes){
        m_nodes = nodes;
    }
    inline void setEleLength() {
        if(m_nodes[0] && m_nodes[1]){
            m_length = nodeDistance(m_nodes);
        }
        else{
            anafLog::warn("You have not initialize all required nodes yet for calculating distance between them!");
            return;
        }
    }
    inline void setEleCrossSectionArea(const double area) {m_crossSectionArea = area;}
    inline void setEleProperty(std::shared_ptr<Material> material) {m_properties = material;}

    // calcuilated properties of element
    inline void setEleElongation(const double elongation) {m_elongation = elongation;}

    inline const std::array<float, 3>& getEleAngle() const {return m_angle;}
    inline const double getEleLength() const {return m_length;}
    inline const double getEleCrossSection() const {return m_crossSectionArea;}
    inline const double getEleElongation() const {return m_elongation;}
    inline const std::shared_ptr<Material> getEleProperties() const {return m_properties;}
    inline const std::array<double, 2>& getEleForces() const {return m_nodeForce;}
    inline const std::array<std::shared_ptr<Node>, 2>& getEleNodes() const {return m_nodes;}
    inline const Eigen::Matrix<double, 6, 6>& getEleStiffness() const {return m_stiffnessMatrix;}

    void determineEleStiffnessMatrix();
};
