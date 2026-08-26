#include "element.hpp"
#include <Eigen/Core>
#include <Eigen/Dense>
#include <array>
#include "../log/anaf_info.h"

//unnecesary if you want to reach global stiffness matrix
void TrussElement_1D::determineEleStiffnessMatrix(){

    if(m_type->getElasticityModulues() == 0){
        anaf_warn("No Elasticity modulus defined for this type of material!");
        m_stiffnessMatrix = Eigen::Matrix<double, 6, 6>::Zero();
        return;
    }

    double AE_L = (m_crossSectionArea * m_type->getElasticityModulues()) / m_length;

    Eigen::Matrix3d lambda;
    std::array<float, 3>& cos {m_cosinuses};

    // lambda is symmetrix
    lambda << (cos[0] * cos[0] * AE_L), (cos[0] * cos[1] * AE_L), (cos[0] * cos[2] * AE_L),
            (cos[0] * cos[1] * AE_L), (cos[1] * cos[1] * AE_L), (cos[1] * cos[2] * AE_L),
            (cos[0] * cos[2] * AE_L), (cos[1] * cos[2] * AE_L), (cos[2] * cos[2] * AE_L);
    
    m_stiffnessMatrix << lambda, -lambda,
                            -lambda, lambda;
}
