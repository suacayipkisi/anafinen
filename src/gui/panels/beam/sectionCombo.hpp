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

  // Section picker over bridge.allSections (like materialCombo). sectionID is the stable
  // BeamSection ID; it falls back to the first section when the selected one was removed.
  inline void sectionCombo(BRIDGE::Gui_Calc_Bridge& bridge, const char* label, std::uint32_t& sectionID) {
    std::lock_guard lock(bridge.dataMutex);
    if (bridge.allSections.empty()) {
      ImGui::TextDisabled("No sections available");
      return;
    }
    if (!bridge.findSectionIndex(sectionID)) sectionID = bridge.allSections.front().getSectionID();
    const auto& selected = bridge.allSections[*bridge.findSectionIndex(sectionID)];
    if (ImGui::BeginCombo(label, selected.getName().c_str(), ImGuiComboFlags_HeightLarge)) {
      for (const auto& section : bridge.allSections) {
        ImGui::PushID(static_cast<int>(section.getSectionID()));
        if (ImGui::Selectable(section.getName().c_str(), section.getSectionID() == sectionID)) {
          sectionID = section.getSectionID();
        }
        ImGui::PopID();
      }
      ImGui::EndCombo();
    }
  }

} // namespace anaf::GUI end
