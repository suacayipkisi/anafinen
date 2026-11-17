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

#include <guiMaterials/imGuiLayer.hpp>
#include <guiMaterials/theme.hpp>
#include <guiMaterials/userSettings.hpp>

#include "imgui.h"
#include "imgui_internal.h" // DockBuilder API

#include <algorithm>
#include <array>
#include <cmath>
#include <string>

namespace anaf::GUI {

  void MainDockSpaceHost::renderPanelsMenu() {
    if (!ImGui::BeginMenu("Panels")) return;
    for (auto& entry : m_panelMenu) {
      const bool available = !entry.isAvailable || entry.isAvailable();
      // A panel of another analysis type stays closed; the checkmark shows what is open now.
      if (ICONS::iconMenuItem(entry.icon, entry.label, nullptr, entry.panel->isOpen, available)) entry.panel->isOpen = !entry.panel->isOpen;
      if (!available && entry.unavailableHint && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip("%s", entry.unavailableHint);
      }
    }
    ImGui::EndMenu();
  }

  void MainDockSpaceHost::renderSettingsMenu() {
    if (!ImGui::BeginMenu("Settings")) return;
    if (ICONS::iconBeginMenu(ICONS::E_IconId::ActionSettings, "Theme")) {
      for (const THEME::E_ThemeId id : THEME::allThemes) {
        if (ImGui::MenuItem(THEME::palette(id).name, nullptr, THEME::currentTheme() == id)) {
          THEME::applyTheme(id);
          SETTINGS::settings().theme = THEME::palette(id).key;
          SETTINGS::save();
        }
      }
      ImGui::EndMenu();
    }
    if (ICONS::iconBeginMenu(ICONS::E_IconId::PanelProperties, "UI Font Size")) {
      constexpr std::array<float, 6> sizes{13.0f, 14.0f, 15.0f, 16.0f, 17.0f, 18.0f};
      for (const float size : sizes) {
        const std::string label = std::to_string(static_cast<int>(size)) + " px" + (size == 15.0f ? " (default)" : "");
        if (ImGui::MenuItem(label.c_str(), nullptr, SETTINGS::settings().uiFontSize == size)) {
          ImGuiLayer::setUiFontSize(size);
          SETTINGS::settings().uiFontSize = size;
          SETTINGS::save();
        }
      }
      ImGui::EndMenu();
    }
    if (ICONS::iconBeginMenu(ICONS::E_IconId::PanelViewport, "Viewport")) {
      SETTINGS::UserSettings& settings = SETTINGS::settings();
      ImGui::SeparatorText("Background");
      if (ImGui::MenuItem("Flat (default)", nullptr, !settings.gradientBackground)) {
        settings.gradientBackground = false;
        SETTINGS::save();
      }
      if (ImGui::MenuItem("Gradient", nullptr, settings.gradientBackground)) {
        settings.gradientBackground = true;
        SETTINGS::save();
      }
      ImGui::SetItemTooltip("Vertical gradient derived from the theme's viewport color.");

      ImGui::SeparatorText("Result Colors");
      if (ImGui::MenuItem("Banded (default)", nullptr, settings.bandedResults)) {
        settings.bandedResults = true;
        SETTINGS::save();
      }
      ImGui::SetItemTooltip("Stress and displacement colors in discrete bands, the legend labels every band boundary.");
      if (ImGui::MenuItem("Continuous", nullptr, !settings.bandedResults)) {
        settings.bandedResults = false;
        SETTINGS::save();
      }
      if (ImGui::BeginMenu("Color Bands", settings.bandedResults)) {
        constexpr std::array<int, 6> bandCounts{6, 8, 9, 10, 12, 16};
        for (const int bands : bandCounts) {
          const std::string label = std::to_string(bands) + (bands == 9 ? " (default)" : "");
          if (ImGui::MenuItem(label.c_str(), nullptr, settings.colorBands == bands)) {
            settings.colorBands = bands;
            SETTINGS::save();
          }
        }
        ImGui::EndMenu();
      }

      ImGui::Separator();
      if (ImGui::MenuItem("Reset Legend Positions")) {
        for (auto* legend : {&settings.stressLegend, &settings.displacementLegend}) *legend = {};
        SETTINGS::save();
      }
      ImGui::SetItemTooltip("Put the result legends back into the top-left corner of the viewport, expanded.");
      ImGui::EndMenu();
    }
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
      const bool modelActive = !hasModel || hasModel();
      const bool solvable = !canRunSolver || canRunSolver();
      if (ImGui::BeginMenu("File")) {
        if (ICONS::iconMenuItem(ICONS::E_IconId::ModelLibrary, "Open Example...") && onOpenLibrary) onOpenLibrary();
        if (ICONS::iconMenuItem(ICONS::E_IconId::ActionImport, "Import Mesh / CAD...", "Ctrl+O") && onImportMesh) onImportMesh();
        if (ICONS::iconMenuItem(ICONS::E_IconId::ActionExport, "Export Model...", "Ctrl+E") && onExportResults) onExportResults();
        ImGui::Separator();
        if (ICONS::iconMenuItem(ICONS::E_IconId::ActionClear, "Clear Model", nullptr, false, modelActive) && onClearModel) onClearModel();
        ImGui::Separator();
        if (ImGui::MenuItem("Exit", "Alt+F4")) {
          glfwSetWindowShouldClose(m_window, GLFW_TRUE);
        }
        ImGui::EndMenu();
      }

      if (ImGui::BeginMenu("Analyze")) {
        if (ICONS::iconMenuItem(ICONS::E_IconId::ModelTruss, "Truss (1D Element)...") && onSelectTruss) onSelectTruss();
        if (ICONS::iconMenuItem(ICONS::E_IconId::ModelFrame, "Beam / Frame (3D Element)...") && onSelectBeam) onSelectBeam();
        ImGui::Separator();
        if (ICONS::iconMenuItem(ICONS::E_IconId::AnalysisStatic, "Analysis Settings...", nullptr, false, modelActive) && onAnalysisSettings) {
          onAnalysisSettings();
        }
        if (ICONS::iconMenuItem(ICONS::E_IconId::ActionSolve, "Run Solver", "F5", false, solvable) && onRunSolver) onRunSolver();
        ImGui::EndMenu();
      }

      renderPanelsMenu();
      renderSettingsMenu();

      if (ImGui::BeginMenu("Help")) {
        if (ICONS::iconMenuItem(ICONS::E_IconId::ActionHelp, "Welcome...") && onShowWelcome) onShowWelcome();
        if (ICONS::iconMenuItem(ICONS::E_IconId::ActionInfo, "About anafinen...") && onShowAbout) onShowAbout();
        ImGui::EndMenu();
      }

      ImGui::EndMenuBar();
      // F5 anywhere except while typing into a field.
      if (solvable && onRunSolver && !ImGui::GetIO().WantTextInput && ImGui::IsKeyPressed(ImGuiKey_F5, false)) onRunSolver();
    }

    ImGuiID dockspaceId = ImGui::GetID("AppMainDockSpace");
    // No window menu button (the triangle left of the tabs): panels open and close from the Panels menu.
    ImGuiDockNodeFlags dockspaceFlags = ImGuiDockNodeFlags_PassthruCentralNode;
    dockspaceFlags |= ImGuiDockNodeFlags_NoWindowMenuButton; // internal flag (imgui_internal.h)
    dockspaceFlags |= ImGuiDockNodeFlags_NoCloseButton;      // internal; each tab keeps its own close button
    ImGui::DockSpace(dockspaceId, ImVec2(0.0f, 0.0f), dockspaceFlags);

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
        dockMainId, ImGuiDir_Left, 0.21f, nullptr, &dockMainId);

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
      ImGui::DockBuilderDockWindow("Grid Truss Generator", dockLeftId);
      // The Properties panels share a title; "###" gives each its own window (and dock) ID.
      ImGui::DockBuilderDockWindow("Properties###truss_properties", dockLeftId);
      ImGui::DockBuilderDockWindow("Properties###beam_properties", dockLeftId);
      // "Beam Diagrams" splits the Model Tree's node itself when it opens (BeamDiagramPanel).
      ImGui::DockBuilderDockWindow("Model Tree", dockRightId);
      ImGui::DockBuilderDockWindow("Console", dockBottomId);
      ImGui::DockBuilderDockWindow(ViewportToolbar::windowName, dockToolbarId);
      ImGui::DockBuilderDockWindow("3D Simulation Viewport", dockMainId);

      ImGui::DockBuilderFinish(dockspaceId);
    }

    // The toolbar strip has a fixed height (NoResizeY), so it follows the frame height by hand when
    // the UI font size or the theme's frame padding changes.
    if (const ImGuiWindow* toolbar = ImGui::FindWindowByName(ViewportToolbar::windowName); toolbar && toolbar->DockNode) {
      ImGuiDockNode* node = toolbar->DockNode;
      const float height = ViewportToolbar::windowHeight();
      if (node->IsLeafNode() && node->ParentNode && std::abs(node->Size.y - height) > 0.5f) {
        node->Size.y = height;
        node->SizeRef.y = height;
        node->WantLockSizeOnce = true;
      }
    }

    ImGui::End();
  }

} // namespace anaf::GUI end
