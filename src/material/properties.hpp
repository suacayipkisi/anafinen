// Copyright (c) 2026 Abdurrahman Konuk (professionally known as Ufuk Deniz Konuk)
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
#include <utility>

namespace anaf::MATERIAL {

  class Material{
  private:
    bool m_isBuiltin{false};
    
    std::uint32_t m_materialID{};

    float m_ductility{}; // elongation at break, fraction (0.25 = 25 %)
    float m_poissonsRatio{}; // unitless

    double m_density{}; // kg/m^3
    double m_youngModulus{}; // Pa (only axial)
    double m_ultimateTensileStrength{}; // Pa
    double m_yieldTensileStrength{}; // Pa
    double m_bulkModulus{}; // Pa (all side force)
    double m_shearModulus{}; // Pa

    double m_elasticityModulus{}; // Pa, used by the solver

    std::string m_materialType;
  public:
    Material(
      bool isBuiltin,
      std::string name,
      const double elasticityModulusE,
      const double shearModulusG,
      const double bulkModulusK,
      const double yieldTensileStrength,
      const double ultimateTensileStrength,
      const double youngModulus,
      const double density,
      const float poissonsRatio,
      const float ductility,
      const std::uint32_t materialID
    ):
      // Declaration order (-Wreorder); the parameter order is the public API.
      m_isBuiltin(isBuiltin),
      m_materialID(materialID),
      m_ductility(ductility),
      m_poissonsRatio(poissonsRatio),
      m_density(density),
      m_youngModulus(youngModulus),
      m_ultimateTensileStrength(ultimateTensileStrength),
      m_yieldTensileStrength(yieldTensileStrength),
      m_bulkModulus(bulkModulusK),
      m_shearModulus(shearModulusG),
      m_elasticityModulus(elasticityModulusE),
      m_materialType(std::move(name))
    {}

    inline bool getIsBuiltin() const {return m_isBuiltin;}
    inline std::uint32_t getMaterialID() const {return m_materialID;}
    inline float getDuctility() const {return m_ductility;}
    inline float getPoisson() const {return m_poissonsRatio;}
    inline double getDensity() const {return m_density;}
    inline double getYoungModulus() const {return m_youngModulus;}
    inline double getUltTensile() const {return m_ultimateTensileStrength;}
    inline double getYieldTensile() const {return m_yieldTensileStrength;}
    inline double getBulkModulus() const {return m_bulkModulus;}
    inline double getShearModulus() const {return m_shearModulus;}
    inline double getElasticityModulus() const {return m_elasticityModulus;}
    inline std::string_view getMaterialType() const {return m_materialType;}
  };

} // namespace anaf::MATERIAL end
