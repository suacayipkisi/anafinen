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


#include "analysisSettingsPanel.hpp"

#include <bridge/generalStatus.hpp>
#include <guiMaterials/iconAtlas.hpp>
#include <guiMaterials/theme.hpp>
#include <panels/propertyGrid.hpp>

#include "imgui.h"

#include <algorithm>
#include <array>

namespace anaf::GUI {

  void AnalysisSettingsPanel::open() {
    isOpen = true;
    m_focusRequested = true;
  }

  void AnalysisSettingsPanel::onImGuiRender() {
    if (!isOpen) return;
    auto& bridge = BRIDGE::buildBridge();

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + viewport->WorkSize.x * 0.5f, viewport->WorkPos.y + viewport->WorkSize.y * 0.5f),
                            ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(ImGui::GetFontSize() * 30.0f, 0.0f), ImGuiCond_Appearing);
    if (m_focusRequested) {
      ImGui::SetNextWindowFocus();
      m_focusRequested = false;
    }
    if (!ImGui::Begin("Analysis Settings", &isOpen, ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize)) {
      ImGui::End();
      return;
    }

    const auto kind = bridge.loadKind.load();
    const bool dynamic = kind == BRIDGE::E_LoadKind::Dynamic;
    struct Kind {
      ICONS::E_IconId icon;
      const char* name;
      BRIDGE::E_LoadKind value;
    };
    constexpr std::array<Kind, 2> kinds{{{ICONS::E_IconId::AnalysisStatic, "Linear static", BRIDGE::E_LoadKind::Constant},
                                         {ICONS::E_IconId::AnalysisDynamic, "Dynamic (not available yet)", BRIDGE::E_LoadKind::Dynamic}}};

    ImGui::BeginDisabled(bridge.isRunning.load());
    if (PROPERTY::begin("##analysis_settings", 0.34f)) {
      if (PROPERTY::group("Analysis")) {
        PROPERTY::row("Type", "Changing it keeps the model; the editors show the loads it uses.");
        if (ImGui::BeginCombo("##analysis_kind", kinds[dynamic ? 1 : 0].name)) {
          for (const Kind& entry : kinds) {
            if (ICONS::iconSelectable(entry.icon, entry.name, entry.value == kind)) bridge.loadKind = entry.value;
          }
          ImGui::EndCombo();
        }
      }
      if (dynamic && PROPERTY::group("Dynamic")) {
        PROPERTY::row("Analysis");
        if (ImGui::BeginCombo("##dynamic_type", dynamicAnalysisTypes[static_cast<std::size_t>(m_dynamic.analysisType)])) {
          for (std::size_t i = 0; i < dynamicAnalysisTypes.size(); ++i) {
            // Only modal is planned next; the others are listed to show where this goes.
            ImGui::BeginDisabled(i != 0);
            const ICONS::E_IconId icon = i == 0 ? ICONS::E_IconId::AnalysisModal : ICONS::E_IconId::AnalysisDynamic;
            if (ICONS::iconSelectable(icon, dynamicAnalysisTypes[i], m_dynamic.analysisType == static_cast<int>(i))) {
              m_dynamic.analysisType = static_cast<int>(i);
            }
            ImGui::EndDisabled();
          }
          ImGui::EndCombo();
        }
        PROPERTY::row("Modes", "Number of lowest natural frequencies and mode shapes to extract.");
        if (ImGui::InputInt("##modes", &m_dynamic.modeCount)) m_dynamic.modeCount = std::clamp(m_dynamic.modeCount, 1, 1000);
        PROPERTY::row("Mass matrix", "Consistent: from the element shape functions.\nLumped: diagonal, half of the element mass at each node.");
        ImGui::Combo("##mass", &m_dynamic.massMatrix, massMatrixTypes.data(), static_cast<int>(massMatrixTypes.size()));
      }
      PROPERTY::end();
    }
    ImGui::EndDisabled();

    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 30.0f);
    if (dynamic) {
      ICONS::iconText(ICONS::E_IconId::StatusWarning, "Not available in this version.");
      ImGui::TextDisabled("There is no dynamic solver yet (modal, harmonic or transient); the inputs are a preview. "
                          "Use Linear static to solve the model.");
    } else {
      ImGui::TextDisabled("Solves K u = f for the supports and loads of the model. The solver (CHOLMOD, LDLT or "
                          "Block CG) is chosen from the model size and the free memory. Run it with Analyze > Run "
                          "Solver (F5) or the toolbar.");
    }
    ImGui::PopTextWrapPos();
    ImGui::End();
  }

} // namespace anaf::GUI end
