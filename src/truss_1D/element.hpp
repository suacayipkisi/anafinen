#pragma once

#include "../material/properties.hpp"
#include "node.hpp"
#include "../anafInfo.hpp"
#include <Eigen/Core>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <array>
#include <stdexcept>
#include <string>

// initializing 1D element in 3D space
// 2 nodes, one size_dimention
class TrussElement_1D{
private:
    double m_length{};
    double m_crossSectionArea{};
    double m_elongation{};
    double m_axialForce{};
    double m_stress{};
    std::shared_ptr<Material> m_type;
    std::array<float, 3> m_cosinuses;
    std::array<std::uint32_t, 2> m_nodes{};
    Eigen::Matrix<double, 6, 6> m_stiffnessMatrix;
protected:
public:
    TrussElement_1D() = default;
    TrussElement_1D(
        std::shared_ptr<Material> type,
        double area,
        const Node& node_1,
        const Node& node_2
    ):
        m_type(type),
        m_crossSectionArea(area),
        m_nodes({node_1.getNodeID(), node_2.getNodeID()})
    {
        // if type, area and nodes are not given, destroy element
        if(!type){
            anafLog::info(
                "Destroying element that under construction: 'type: {}', 'area: {}', 'nodes: {},{}'", 
                "unknownType",
                area,
                std::to_string(node_1.getNodeID()),
                std::to_string(node_2.getNodeID())
            );
            throw std::invalid_argument("Element type cannot be undefined");
        }
        if(area <= 0.0){
            anafLog::info(
                "Destroying element that under construction: 'type: {}', 'area: {}', 'nodes: {},{}'", 
                type->getMaterialType(),
                0.0,
                std::to_string(node_1.getNodeID()),
                std::to_string(node_2.getNodeID())
            );
            throw std::invalid_argument("Element cross sectional area must be greater than 0");
        }
        if(node_1.getNodeID() == node_2.getNodeID()){
            throw std::invalid_argument("An elements nodes cannot be same");
        }//destroyment finish
        
        //calculate second degree element info
        double dx = node_2.getLocX() - node_1.getLocX();
        double dy = node_2.getLocY() - node_1.getLocY();
        double dz = node_2.getLocZ() - node_1.getLocZ();
        m_length = std::sqrt(dx * dx + dy * dy + dz * dz);
        std::array<double, 3> loc1{node_1.getLocation()};
        std::array<double, 3> loc2{node_2.getLocation()};
        for (std::size_t i{0}; i < 3; ++i){
            m_cosinuses[i] = (loc2[i] - loc1[i]) / m_length;
        }
    } // TrussElement_1D(.......) Contructor end

    // calculated properties of element
    inline void setEleElongation(const double elongation) {m_elongation = elongation;}
    inline void setEleAxialForce(const double axialForce) {m_axialForce = axialForce;}
    inline void setEleStress(const double stress) {m_stress = stress;}

    inline const double getEleLength() const {return m_length;}
    inline const double getEleCrossSection() const {return m_crossSectionArea;}
    inline const double getEleElongation() const {return m_elongation;}
    inline const double getEleAxialForces() const {return m_axialForce;}
    inline const double getEleStress() const {return m_stress;}
    inline const std::shared_ptr<Material> getEleProperties() const {return m_type;}
    inline const std::array<float, 3>& getEleCosinuses() const {return m_cosinuses;}
    inline const std::array<std::uint32_t, 2>& getEleNodes() const {return m_nodes;}

    void determineEleStiffnessMatrix();
    inline const Eigen::Matrix<double, 6, 6>& getEleStiffness() const {return m_stiffnessMatrix;}

};
