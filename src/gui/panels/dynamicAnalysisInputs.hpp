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
#include <panels/editorLayout.hpp>

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

  // Dynamic inputs: analysis type, mode count, mass matrix.
  inline void renderDynamicAnalysisInputs(DynamicAnalysisInputs& inputs) {
    ImGui::PushID("dynamic_analysis");
    LAYOUT::field("Analysis");
    if (ImGui::BeginCombo("##type", kDynamicAnalysisTypes[static_cast<std::size_t>(inputs.analysisType)])) {
      for (std::size_t i = 0; i < kDynamicAnalysisTypes.size(); ++i) {
        // Only modal is planned next; the others are listed to show where this goes.
        const ImGuiSelectableFlags flags = i == 0 ? ImGuiSelectableFlags_None : ImGuiSelectableFlags_Disabled;
        if (ImGui::Selectable(kDynamicAnalysisTypes[i], inputs.analysisType == static_cast<int>(i), flags)) {
          inputs.analysisType = static_cast<int>(i);
        }
      }
      ImGui::EndCombo();
    }
    LAYOUT::field("Modes");
    if (ImGui::InputInt("##modes", &inputs.modeCount)) inputs.modeCount = std::clamp(inputs.modeCount, 1, 1000);
    ImGui::SetItemTooltip("Number of lowest natural frequencies and mode shapes to extract");
    LAYOUT::field("Mass matrix");
    ImGui::Combo("##mass", &inputs.massMatrix, kMassMatrixTypes.data(), static_cast<int>(kMassMatrixTypes.size()));
    ImGui::SetItemTooltip("Consistent: from the element shape functions.\nLumped: diagonal, half of the element mass at each node.");
    ImGui::PopID();
  }

  // Content of the editors' "Analysis" tab: the load kind and what the run button will do.
  inline void renderAnalysisTab(const BRIDGE::LoadKind kind, DynamicAnalysisInputs& inputs) {
    const bool dynamic = kind == BRIDGE::LoadKind::dynamic;
    ImGui::SeparatorText(dynamic ? "Dynamic Analysis" : "Linear Static Analysis");
    ImGui::TextDisabled("Load type: %s", dynamic ? "Dynamic" : "Constant (static)");
    ImGui::SetItemTooltip("Change it in the Analyze menu; the model is kept.");
    ImGui::PushTextWrapPos(0.0f);
    if (dynamic) {
      ImGui::PopTextWrapPos();
      LAYOUT::wrappedColored(THEME::theme().warn, "Not available in this version: there is no dynamic solver yet (modal, "
                                            "harmonic or transient). The inputs below are a preview; use Constant Load "
                                            "(Static) to solve the model.");
      ImGui::PushTextWrapPos(0.0f);
      ImGui::TextDisabled("Uses supports, materials (density) and sections; static loads are not used.");
      ImGui::PopTextWrapPos();
      ImGui::Spacing();
      renderDynamicAnalysisInputs(inputs);
    } else {
      ImGui::TextDisabled("Solves K u = f for the supports and loads of the Supports & Loads tab. The solver "
                          "(CHOLMOD, LDLT or Block CG) is chosen from the model size and the free memory.");
      ImGui::PopTextWrapPos();
    }
  }

  // Run button of the dynamic analysis, disabled until the solver exists.
  inline void renderDynamicRunButton() {
    ImGui::BeginDisabled();
    LAYOUT::primaryButton("Run Modal Analysis##dynamic");
    ImGui::EndDisabled();
    ImGui::SetItemTooltip("Not available yet: dynamic analysis has no solver in this version");
  }

} // namespace anaf::GUI end
