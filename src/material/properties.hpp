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

#pragma once

#include <cstdint>
#include <string>
#include <string_view>

class Material{
private:
    std::uint32_t m_materialID;

    float m_ductility{}; // elongation at break / %
    float m_poissonsRatio{}; // uniteless
    double m_youngModulus{}; // GPa (only axial)
    double m_ultimateTensileStrength{}; // GPa
    double m_yieldTensileStrength{}; // GPa
    double m_bulkModulus{}; // GPa (all side force)
    double m_shearModulus{}; // GPa

    double m_elasticityModulus{}; // GPa

    std::string m_materialType{};
public:
    Material(
        std::string name,
        const double elasticityModulusE,
        const double shearModulusG,
        const double bulkModulusK,
        const double yieldTensileStrength,
        const double ultimateTensileStrength,
        const double youngModulus,
        const float poissonsRatio,
        const float ductility,
        const std::uint32_t materialID
    ):
        m_materialType(name),
        m_elasticityModulus(elasticityModulusE),
        m_shearModulus(shearModulusG),
        m_bulkModulus(bulkModulusK),
        m_yieldTensileStrength(yieldTensileStrength),
        m_ultimateTensileStrength(ultimateTensileStrength),
        m_youngModulus(youngModulus),
        m_poissonsRatio(poissonsRatio),
        m_ductility(ductility),
        m_materialID(materialID)
    {}

    // if all properties not determined we can determine later
    inline void setDuctility(const float ductility) {
        if(m_ductility == 0) m_ductility = ductility;
    }
    inline void setPoisson(const float poissons) {
        if(m_poissonsRatio == 0) m_poissonsRatio = poissons;
    }
    inline void setYoungModulus(const double youngModulus) {
        if(m_youngModulus == 0) m_youngModulus = youngModulus;
    }
    inline void setUltTensile(const double ultimateTensileStrength) {
        if(m_ultimateTensileStrength == 0) m_ultimateTensileStrength = ultimateTensileStrength;
    }
    inline void setYieldTensile(const double yieldTensileStrength)  {
        if(m_yieldTensileStrength == 0) m_yieldTensileStrength = yieldTensileStrength;
    }
    inline void setBulkModulus(const double bulkModulus) {
        if(m_bulkModulus == 0) m_bulkModulus = bulkModulus;
    }
    inline void setShearModulues(const double shearModulus) {
        if(m_shearModulus == 0) m_shearModulus = shearModulus;
    }
    inline void setElasticityModulues(const double elasticityModulus) {
        if(m_elasticityModulus == 0) m_elasticityModulus = elasticityModulus;
    }
    inline void setMaterialType(const std::string type) {
        if(m_materialType.empty()) m_materialType = type;
    }

    inline const std::uint32_t getMaterialID() const {return m_materialID;}
    inline const float getDuctility() const {return m_ductility;}
    inline const float getPoisson() const {return m_poissonsRatio;}
    inline const double getYoungModulus() const {return m_youngModulus;}
    inline const double getUltTensile() const {return m_ultimateTensileStrength;}
    inline const double getYieldTensile() const {return m_yieldTensileStrength;}
    inline const double getBulkModulus() const {return m_bulkModulus;}
    inline const double getShearModulues() const {return m_shearModulus;}
    inline const double getElasticityModulues() const {return m_elasticityModulus;}
    inline const std::string_view getMaterialType() const {return m_materialType;}
};
