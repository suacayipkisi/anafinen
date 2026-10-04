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

#include "imgui.h"

#include "analysisSelector.hpp"

#include <panels/editorLayout.hpp>

namespace anaf::GUI {
  void AnalysisSelector::open(const StructureFamily family) {
    m_family = family;
    m_loadKind = BRIDGE::buildBridge().m_loadKind.load();
    isOpen = true;
  }

  void AnalysisSelector::onImGuiRender() {
    if (!isOpen) return;

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImVec2 center = ImVec2(
      viewport->WorkPos.x + viewport->WorkSize.x * 0.5f,
      viewport->WorkPos.y + viewport->WorkSize.y * 0.5f
    );

    // center on screen when appearing
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(420.0f, 260.0f), ImGuiCond_FirstUseEver);

    const bool truss = m_family == StructureFamily::truss;
    if (ImGui::Begin("Select Analysis", &isOpen)) {
      ImGui::SeparatorText(truss ? "Truss (1D Element)" : "Beam / Frame (3D Element)");

      if (truss) {
        if (ImGui::BeginCombo("Truss Type", kTrussTypes[static_cast<std::size_t>(m_trussType)].data())) {
          for (std::size_t i = 0; i < kTrussTypes.size(); ++i) {
            if (ImGui::Selectable(kTrussTypes[i].data(), m_trussType == static_cast<TrussTypes>(i))) {
              m_trussType = static_cast<TrussTypes>(i);
            }
          }
          ImGui::EndCombo();
        }
        if (m_trussType == simpleQuadranglePrism) {
          ImGui::TextWrapped("Generated grid truss. Importing a file switches to Imported / Self-Built.");
        } else {
          ImGui::TextWrapped("Import a mesh / CAD file or build the truss node by node.");
        }
      }

      const auto loadIndex = static_cast<std::size_t>(m_loadKind);
      if (ImGui::BeginCombo("Load Type", kLoadKinds[loadIndex].data())) {
        for (std::size_t i = 0; i < kLoadKinds.size(); ++i) {
          if (ImGui::Selectable(kLoadKinds[i].data(), loadIndex == i)) m_loadKind = static_cast<BRIDGE::LoadKind>(i);
        }
        ImGui::EndCombo();
      }
      if (m_loadKind == BRIDGE::LoadKind::constant) {
        ImGui::TextWrapped("Static solve under nodal loads%s.", truss ? " and self weight" : ", distributed loads and self weight");
      } else {
        ImGui::TextColored(LAYOUT::kWarn, "Not available in this version.");
        ImGui::TextWrapped("Natural frequencies and mode shapes (modal analysis) come next. The inputs can be set, "
                           "but there is no dynamic solver yet, so nothing can be run.");
      }
      ImGui::TextDisabled("Changing the %s type clears the current model.", truss ? "truss" : "object");

      if (ImGui::Button("Select", ImVec2(-1, 32))) {
        if (truss && onTrussSelected) onTrussSelected(m_trussType, m_loadKind);
        if (!truss && onBeamSelected) onBeamSelected(m_loadKind);
        isOpen = false;
      }
    }

    ImGui::End();
  }
} // namespace anaf::GUI end
