#include "element.hpp"
#include <Eigen/Core>
#include <Eigen/Dense>
#include <array>
#include "../anafInfo.hpp"

void TrussElement_1D::determineEleStiffnessMatrix(){

    if(m_length == 0){
        setEleLength();
    }
    if(m_crossSectionArea == 0){
        anafLog::warn("Element cross section have not assigned, cant calcualte stiffness matrix!");
        m_stiffnessMatrix = Eigen::Matrix<double, 6, 6>::Zero();
        return;
    }
    if(m_properties){
        anafLog::warn("Assign an element propert for calculating stiffness matrix!");
        m_stiffnessMatrix = Eigen::Matrix<double, 6, 6>::Zero();
        return;
    }
    if(m_properties->getElasticityModulues() == 0){
        anafLog::warn("No Elasticity modulus defined for this type of material!");
        m_stiffnessMatrix = Eigen::Matrix<double, 6, 6>::Zero();
        return;
    }

    double AE_L = (m_crossSectionArea * m_properties->getElasticityModulues()) / m_length;

    Eigen::Matrix3d lambda;
    std::array<float, 3>& cos {m_cosinuses};

    // lambda is symmetrix
    lambda << (cos[0] * cos[0] * AE_L), (cos[0] * cos[1] * AE_L), (cos[0] * cos[2] * AE_L),
            (cos[0] * cos[1] * AE_L), (cos[1] * cos[1] * AE_L), (cos[1] * cos[2] * AE_L),
            (cos[0] * cos[2] * AE_L), (cos[1] * cos[2] * AE_L), (cos[2] * cos[2] * AE_L);
    
    m_stiffnessMatrix << lambda, -lambda,
                            -lambda, lambda;
}
