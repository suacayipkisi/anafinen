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

  // Physical values of one material, SI units. Filled with designated initializers, e.g.
  // MaterialProperties{.name = "S355", .elasticityModulus = 210e9, ...}.
  // G and K are stored, not derived from E and nu: the wood entries are orthotropic
  // (G = G_LR along the grain), where the isotropic relations do not hold.
  struct MaterialProperties {
    std::string name;
    double elasticityModulus{};       // E, Pa: the axial stiffness of a bar
    double shearModulus{};            // G, Pa
    double bulkModulus{};             // K, Pa
    double yieldTensileStrength{};    // Pa
    double ultimateTensileStrength{}; // Pa
    double density{};                 // kg/m^3
    double poissonsRatio{};           // -
    double ductility{};               // elongation at break, fraction (0.25 = 25 %)
  };

  class Material {
  private:
    MaterialProperties m_properties;
    bool m_isBuiltin{false};
    std::uint32_t m_materialID{}; // stable ID, never reused (built-ins: 0..n-1 from the library file)

  public:
    explicit Material(MaterialProperties properties, const bool isBuiltin = false, const std::uint32_t materialID = 0) :
      m_properties(std::move(properties)),
      m_isBuiltin(isBuiltin),
      m_materialID(materialID)
    {}

    const MaterialProperties& getProperties() const {return m_properties;}
    bool getIsBuiltin() const {return m_isBuiltin;}
    std::uint32_t getMaterialID() const {return m_materialID;}
    std::string_view getMaterialType() const {return m_properties.name;}
    double getElasticityModulus() const {return m_properties.elasticityModulus;}
    double getShearModulus() const {return m_properties.shearModulus;}
    double getBulkModulus() const {return m_properties.bulkModulus;}
    double getYieldTensile() const {return m_properties.yieldTensileStrength;}
    double getUltTensile() const {return m_properties.ultimateTensileStrength;}
    double getDensity() const {return m_properties.density;}
    double getPoisson() const {return m_properties.poissonsRatio;}
    double getDuctility() const {return m_properties.ductility;}
  };

} // namespace anaf::MATERIAL end
