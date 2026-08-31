// Copyright (c) 2026 Ufuk Deniz Konuk
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program. If not, see <https://www.gnu.org/licenses/>.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "element.hpp"
#include <Eigen/Core>
#include <Eigen/Dense>
#include <array>
#include "../log/anaf_info.hpp"

namespace FEM::TRUSS {

    //unnecesary if you want to reach global stiffness matrix
    void TrussElement_1D::determineEleStiffnessMatrix(const std::span<const anaf::MATERIAL::Material> allMaterials){

        if(allMaterials[m_type].getElasticityModulues() == 0){
            anaf::LOG::warn("No Elasticity modulus defined for this type of material!");
            m_stiffnessMatrix = Eigen::Matrix<double, 6, 6>::Zero();
            return;
        }

        double AE_L = (m_crossSectionArea * allMaterials[m_type].getElasticityModulues()) / m_length;

        Eigen::Matrix3d lambda;
        std::array<float, 3>& cos {m_cosinuses};

        // lambda is symmetrix
        lambda << (cos[0] * cos[0] * AE_L), (cos[0] * cos[1] * AE_L), (cos[0] * cos[2] * AE_L),
                (cos[0] * cos[1] * AE_L), (cos[1] * cos[1] * AE_L), (cos[1] * cos[2] * AE_L),
                (cos[0] * cos[2] * AE_L), (cos[1] * cos[2] * AE_L), (cos[2] * cos[2] * AE_L);
        
        m_stiffnessMatrix << lambda, -lambda,
                                -lambda, lambda;
    }

} // namespace FEM::TRUSS end
