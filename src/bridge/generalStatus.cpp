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

#include "generalStatus.hpp"
#include "anaf_info.hpp"
#include "material/properties.hpp"
#include <algorithm>
#include <string_view>

namespace anaf::BRIDGE {

  std::string_view getObjectTypeName(ObjectType obj) {
    switch (obj) {
      case truss_SQPT:
        return "truss_SQPT";
      case truss_imported_or_entered:
        return "truss_imported_or_entered";
      default:
        return "no_type";
    }
  }

  void Gui_Calc_Bridge::setStaticInfo() {
    allMaterials.push_back({
      true,
      "Structural Steel (AISI 4130)",
      205.0e9,
      78.0e9,
      160.0e9,
      435.0e6,
      670.0e6,
      205.0e9,
      7850.0,
      0.29f,
      0.25f,
      0u
    });
    allMaterials.push_back({
      true,
      "Aluminum 6061-T6",
      68.9e9,
      26.0e9,
      67.5e9,
      276.0e9 / 1e3,
      310.0e6,
      68.9e9,
      2700.0,
      0.33f,
      0.12f,
      1u
    });
  }

  void Gui_Calc_Bridge::setDynamicMaterialInfo(anaf::MATERIAL::Material material, AddRemove operation) {
    switch (operation) {
      case ADD:
        allMaterials.push_back(std::move(material));
        break;
      case REMOVE: {
        if(material.getIsBuiltin()) {
          anaf::LOG::warn("You cannot delete builtin material: {}", material.getMaterialType());
          return;
        }
        const auto materialID = material.getMaterialID();
        const auto materialIt = std::find_if(
          allMaterials.begin(),
          allMaterials.end(),
          [materialID](const anaf::MATERIAL::Material& currentMaterial) {
            return currentMaterial.getMaterialID() == materialID;
          }
        );
        if (materialIt != allMaterials.end()) {
          allMaterials.erase(materialIt);
        }
        break;
      }
    }
  }

  Gui_Calc_Bridge& buildBridge() {
    static Gui_Calc_Bridge bridge{};
    
    Gui_Calc_Bridge& ref = bridge;
    return ref;
  }
  
} // namespace anaf::BRIDGE end
