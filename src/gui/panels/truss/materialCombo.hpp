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

#include <bridge/generalStatus.hpp>

#include "imgui.h"

#include <cstdint>
#include <mutex>

namespace anaf::GUI {

  // Material picker over bridge.allMaterials, shared by the truss panels. materialID is the
  // stable Material ID; it falls back to the first material when the selected one was
  // removed in the Material Handler.
  inline void materialCombo(BRIDGE::Gui_Calc_Bridge& bridge, const char* label, std::uint32_t& materialID) {
    std::lock_guard lock(bridge.dataMutex);
    if (bridge.allMaterials.empty()) {
      ImGui::TextDisabled("No materials available");
      return;
    }
    if (!bridge.findMaterialIndex(materialID)) materialID = bridge.allMaterials.front().getMaterialID();
    const auto& selected = bridge.allMaterials[*bridge.findMaterialIndex(materialID)];
    if (ImGui::BeginCombo(label, selected.getMaterialType().data())) {
      for (const auto& material : bridge.allMaterials) {
        ImGui::PushID(static_cast<int>(material.getMaterialID()));
        if (ImGui::Selectable(material.getMaterialType().data(), material.getMaterialID() == materialID)) {
          materialID = material.getMaterialID();
        }
        ImGui::PopID();
      }
      ImGui::EndCombo();
    }
  }

} // namespace anaf::GUI end
