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

#include <algorithm>
#include <array>

namespace anaf::GUI {

  // Dynamic analysis inputs, shared by the truss and beam editors (LoadKind::dynamic). GUI only
  // so far: nothing reads them until the modal solver exists.
  struct DynamicAnalysisInputs {
    int analysisType{0}; // row in kDynamicAnalysisTypes
    int modeCount{10};   // lowest modes to extract
    int massMatrix{0};   // row in kMassMatrixTypes
  };

  inline constexpr std::array<const char*, 3> kDynamicAnalysisTypes{"Modal (natural frequencies)", "Harmonic", "Transient"};
  inline constexpr std::array<const char*, 2> kMassMatrixTypes{"Consistent", "Lumped"};

  // One line under the panel summary: which load kind the panel shows.
  inline void renderLoadKindLine(const BRIDGE::LoadKind kind) {
    ImGui::TextDisabled("Load type: %s", kind == BRIDGE::LoadKind::dynamic ? "Dynamic" : "Constant (static)");
    ImGui::SetItemTooltip("Change it in the Analyze menu; the model is kept.");
  }

  // "Dynamic Analysis" section: analysis type, mode count, mass matrix.
  inline void renderDynamicAnalysisInputs(DynamicAnalysisInputs& inputs) {
    if (!ImGui::CollapsingHeader("Dynamic Analysis", ImGuiTreeNodeFlags_DefaultOpen)) return;
    ImGui::PushID("dynamic_analysis");
    if (ImGui::BeginCombo("Analysis", kDynamicAnalysisTypes[static_cast<std::size_t>(inputs.analysisType)])) {
      for (std::size_t i = 0; i < kDynamicAnalysisTypes.size(); ++i) {
        // Only modal is planned next; the others are listed to show where this goes.
        const ImGuiSelectableFlags flags = i == 0 ? ImGuiSelectableFlags_None : ImGuiSelectableFlags_Disabled;
        if (ImGui::Selectable(kDynamicAnalysisTypes[i], inputs.analysisType == static_cast<int>(i), flags)) {
          inputs.analysisType = static_cast<int>(i);
        }
      }
      ImGui::EndCombo();
    }
    if (ImGui::InputInt("Number of Modes", &inputs.modeCount)) inputs.modeCount = std::clamp(inputs.modeCount, 1, 1000);
    ImGui::Combo("Mass Matrix", &inputs.massMatrix, kMassMatrixTypes.data(), static_cast<int>(kMassMatrixTypes.size()));
    ImGui::SetItemTooltip("Consistent: from the element shape functions.\nLumped: diagonal, half of the element mass at each node.");
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("Uses supports, materials (density) and sections; static loads are not used.");
    ImGui::PopTextWrapPos();
    ImGui::PopID();
  }

  // Run button of the dynamic analysis, disabled until the solver exists.
  inline void renderDynamicRunButton() {
    ImGui::BeginDisabled();
    ImGui::Button("Run Modal Analysis##dynamic", ImVec2(-1.0f, 32.0f));
    ImGui::EndDisabled();
    ImGui::SetItemTooltip("Not implemented yet");
  }

} // namespace anaf::GUI end
