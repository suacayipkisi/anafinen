#pragma once

#include "../material/properties.hpp"
#include "node.hpp"
#include "../log/anaf_info.h"

#include <Eigen/Core>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <array>
#include <span>
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
        const std::uint32_t node_1,
        const std::uint32_t node_2,
        std::span<const Node> allNodes
    ):
        m_type(type),
        m_crossSectionArea(area),
        m_nodes({node_1, node_2})
    {
        // if type, area and nodes are not given, destroy element
        if (!type) {
            anaf_error("Destroying invalid element: material type is null, area=%.4f, nodes=[%u, %u]", 
                       area, node_1, node_2);
            throw std::invalid_argument("Element type cannot be undefined");
        }

        if (area <= 0.0) {
            anaf_error("Destroying invalid element: area must be positive (got %.4f), material=%s, nodes=[%u, %u]", 
                       area, type->getMaterialType().c_str(), node_1, node_2);
            throw std::invalid_argument("Element cross sectional area must be greater than 0");
        }

        if (node_1 == node_2) {
            anaf_error("Destroying invalid element: node indices cannot be identical (%u == %u)", node_1, node_2);
            throw std::invalid_argument("An element's nodes cannot be same");
        }

        if (node_1 >= allNodes.size() || node_2 >= allNodes.size()) {
            anaf_error("Node index out of range: n1=%u, n2=%u, total_nodes=%zu", node_1, node_2, allNodes.size());
            throw std::out_of_range("Node index is outside the node span");
        }//destroyment finish
        
        //calculate second degree element info
        double dx = allNodes[node_2].getLocX() - allNodes[node_1].getLocX();
        double dy = allNodes[node_2].getLocY() - allNodes[node_1].getLocY();
        double dz = allNodes[node_2].getLocZ() - allNodes[node_1].getLocZ();
        m_length = std::sqrt(dx * dx + dy * dy + dz * dz);
        if (m_length <= 0.0) {
            throw std::invalid_argument("Element length must be greater than zero");
        }
        std::array<double, 3> loc1{allNodes[node_1].getLocation()};
        std::array<double, 3> loc2{allNodes[node_2].getLocation()};
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
