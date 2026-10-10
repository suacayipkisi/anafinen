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

#include "mainDockSpaceHost.hpp"
#include "viewportToolbar.hpp"

#include <guiMaterials/theme.hpp>
#include <guiMaterials/userSettings.hpp>

#include "imgui.h"
#include "imgui_internal.h" // DockBuilder API

#include <algorithm>

namespace anaf::GUI {

  void MainDockSpaceHost::renderPanelsMenu() {
    if (!ImGui::BeginMenu("Panels")) return;
    for (auto& entry : m_panelMenu) {
      const bool available = !entry.isAvailable || entry.isAvailable();
      // A panel of another analysis type stays closed; the checkmark shows what is open now.
      ImGui::MenuItem(entry.label, nullptr, &entry.panel->isOpen, available);
      if (!available && entry.unavailableHint && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip("%s", entry.unavailableHint);
      }
    }
    ImGui::EndMenu();
  }

  void MainDockSpaceHost::renderSettingsMenu() {
    if (!ImGui::BeginMenu("Settings")) return;
    if (ImGui::BeginMenu("Theme")) {
      for (const THEME::E_ThemeId id : THEME::allThemes) {
        if (ImGui::MenuItem(THEME::palette(id).name, nullptr, THEME::currentTheme() == id)) {
          THEME::applyTheme(id);
          SETTINGS::settings().theme = THEME::palette(id).key;
          SETTINGS::save();
        }
      }
      ImGui::EndMenu();
    }
    if (ImGui::MenuItem("Reset Legend Positions")) {
      for (auto* legend : {&SETTINGS::settings().stressLegend, &SETTINGS::settings().displacementLegend}) *legend = {};
      SETTINGS::save();
    }
    ImGui::SetItemTooltip("Put the result legends back into the bottom-left corner of the viewport, expanded.");
    ImGui::EndMenu();
  }

  void MainDockSpaceHost::onImGuiRender() {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::SetNextWindowViewport(viewport->ID);

    ImGuiWindowFlags hostFlags = ImGuiWindowFlags_NoTitleBar | 
                   ImGuiWindowFlags_NoCollapse | 
                   ImGuiWindowFlags_NoResize | 
                   ImGuiWindowFlags_NoMove | 
                   ImGuiWindowFlags_NoBringToFrontOnFocus | 
                   ImGuiWindowFlags_NoNavFocus | 
                   ImGuiWindowFlags_NoBackground |
                   ImGuiWindowFlags_MenuBar;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));

    ImGui::Begin("MainDockSpaceHostWindow", nullptr, hostFlags);
    ImGui::PopStyleVar(3);

    if (ImGui::BeginMenuBar()) {
      if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("Import Mesh / CAD...", "Ctrl+O")) {
          if (onImportMesh) onImportMesh();
        }
        if (ImGui::MenuItem("Export Model...", "Ctrl+E")) {
          if (onExportResults) onExportResults();
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Exit", "Alt+F4")) {
          glfwSetWindowShouldClose(m_window, GLFW_TRUE);
        }
        ImGui::EndMenu();
      }

      if (ImGui::BeginMenu("Analyze")) {
        if (ImGui::MenuItem("Truss (1D Element)") && onSelectTruss) onSelectTruss();
        if (ImGui::MenuItem("Beam / Frame (3D Element)") && onSelectBeam) onSelectBeam();
        ImGui::EndMenu();
      }

      renderPanelsMenu();
      renderSettingsMenu();

      if (ImGui::BeginMenu("Help")) {
        if (ImGui::MenuItem("Welcome...")) {
          if (onShowWelcome) onShowWelcome();
        }
        if (ImGui::MenuItem("About anafinen...")) {
          if (onShowAbout) onShowAbout();
        }
        ImGui::EndMenu();
      }

      ImGui::EndMenuBar();
    }

    ImGuiID dockspaceId = ImGui::GetID("AppMainDockSpace");
    ImGui::DockSpace(dockspaceId, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_PassthruCentralNode);

    // Layout initialization: only run when dimensions are valid AND it hasn't run yet
    static bool s_layoutBuilt = false;
    if (!s_layoutBuilt && viewport->WorkSize.x > 100.0f && viewport->WorkSize.y > 100.0f) {
      s_layoutBuilt = true;

      ImGui::DockBuilderRemoveNode(dockspaceId);
      ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);
      ImGui::DockBuilderSetNodeSize(dockspaceId, viewport->WorkSize);

      ImGuiID dockMainId = dockspaceId;

      // 1. Split Left (Full height)
      ImGuiID dockLeftId = ImGui::DockBuilderSplitNode(
        dockMainId, ImGuiDir_Left, 0.18f, nullptr, &dockMainId);

      // 2. Split Right (Full height)
      ImGuiID dockRightId = ImGui::DockBuilderSplitNode(
        dockMainId, ImGuiDir_Right, 0.24f, nullptr, &dockMainId);

      // 3. Split Bottom from remaining center
      ImGuiID dockBottomId = ImGui::DockBuilderSplitNode(
        dockMainId, ImGuiDir_Down, 0.25f, nullptr, &dockMainId);

      // 4. Thin toolbar strip on top of the viewport, same width, fixed height and no tab bar
      const float toolbarHeight = ViewportToolbar::windowHeight();
      const float centerHeight = ImGui::DockBuilderGetNode(dockMainId)->Size.y;
      ImGuiID dockToolbarId = ImGui::DockBuilderSplitNode(
        dockMainId, ImGuiDir_Up, std::clamp(toolbarHeight / centerHeight, 0.01f, 0.5f), nullptr, &dockMainId);
      ImGuiDockNode* toolbarNode = ImGui::DockBuilderGetNode(dockToolbarId);
      toolbarNode->LocalFlags |= ImGuiDockNodeFlags_NoTabBar | ImGuiDockNodeFlags_NoResizeY | ImGuiDockNodeFlags_NoDockingOverMe;
      ImGui::DockBuilderSetNodeSize(dockToolbarId, ImVec2(toolbarNode->Size.x, toolbarHeight));

      // Dock windows into respective nodes
      ImGui::DockBuilderDockWindow("Truss(1D) Analysis Set", dockLeftId);
      ImGui::DockBuilderDockWindow("Truss(1D) Model Editor", dockLeftId);
      ImGui::DockBuilderDockWindow("Beam(3D) Frame Editor", dockLeftId);
      // "Beam Diagrams" splits the Model Tree's node itself when it opens (BeamDiagramPanel).
      ImGui::DockBuilderDockWindow("Model Tree", dockRightId);
      ImGui::DockBuilderDockWindow("Console", dockBottomId);
      ImGui::DockBuilderDockWindow(ViewportToolbar::windowName, dockToolbarId);
      ImGui::DockBuilderDockWindow("3D Simulation Viewport", dockMainId);

      ImGui::DockBuilderFinish(dockspaceId);
    }

    ImGui::End();
  }

} // namespace anaf::GUI end
