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

#include "welcomePanel.hpp"

#include <beam/beamTypes/beamLibrary.hpp>
#include <bridge/generalStatus.hpp>
#include <directory/getExecutableDirectory.hpp>
#include <guiMaterials/userSettings.hpp>
#include <log/anaf_info.hpp>
#include <panels/editorLayout.hpp>

#include "imgui.h"
#include "imgui_internal.h" // MovingWindow

#include <algorithm>
#include <cfloat>
#include <format>
#include <string>
#include <string_view>

namespace anaf::GUI {

  namespace {
    // Built-in beam library model opened by "Open Example": small, rigid joints, every load type
    // (line loads, wind, self weight), so the solved diagrams and stress colors show at once.
    constexpr std::string_view kExampleId = "building_portal_frame";
    constexpr std::string_view kExampleName = "Portal frame (pitched roof)";

    // Button on the left, wrapped explanation next to it, both centered on the taller of the two;
    // true when clicked.
    bool actionRow(const char* label, const char* text, const bool enabled = true) {
      ImGui::TableNextRow();
      const float buttonHeight = LAYOUT::kRunButtonHeight;

      ImGui::TableSetColumnIndex(1);
      const float rowTop = ImGui::GetCursorPosY();
      const float textHeight = ImGui::CalcTextSize(text, nullptr, false, ImGui::GetContentRegionAvail().x).y;
      const float rowHeight = std::max(buttonHeight, textHeight);
      ImGui::SetCursorPosY(rowTop + (rowHeight - textHeight) * 0.5f);
      ImGui::PushTextWrapPos(0.0f);
      ImGui::TextDisabled("%s", text);
      ImGui::PopTextWrapPos();

      ImGui::TableSetColumnIndex(0);
      ImGui::SetCursorPosY(rowTop + (rowHeight - buttonHeight) * 0.5f);
      ImGui::BeginDisabled(!enabled);
      const bool clicked = ImGui::Button(label, ImVec2(-FLT_MIN, buttonHeight));
      ImGui::EndDisabled();
      return clicked;
    }
  } // namespace end

  WelcomePanel::WelcomePanel() {
    m_showOnStartup = SETTINGS::settings().showWelcomeOnStartup;
    isOpen = m_showOnStartup;
    m_examplePath = anaf::DIRECTORY::findAssetPath(
      std::filesystem::path(FEM::BEAM::LIBRARY::kLibrarySubdir) / (std::string(kExampleId) + "_solved.msh"));
    if (m_examplePath.empty()) anaf::LOG::warn("Welcome: example model '{}' not found in the assets", kExampleId);
  }

  void WelcomePanel::open() {
    isOpen = true;
    m_focusRequested = true;
    m_autoCenter = true;
  }

  void WelcomePanel::onImGuiRender() {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    // Centered every frame: on the first frames the window is not maximized yet and the panel
    // height is not known, so a one-time centering lands too high.
    ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + viewport->WorkSize.x * 0.5f, viewport->WorkPos.y + viewport->WorkSize.y * 0.5f),
                            m_autoCenter ? ImGuiCond_Always : ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    // Fixed width, height follows the content.
    ImGui::SetNextWindowSize(ImVec2(ImGui::GetFontSize() * 36.0f, 0.0f), ImGuiCond_Always);
    if (m_focusRequested) {
      ImGui::SetNextWindowFocus();
      m_focusRequested = false;
    }
    constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings;
    if (!ImGui::Begin("Welcome to anafinen", &isOpen, flags)) {
      ImGui::End();
      return;
    }

    if (ImGui::GetCurrentContext()->MovingWindow == ImGui::GetCurrentWindow()) m_autoCenter = false;

    ImGui::TextWrapped("Linear static analysis of 3D trusses and beam / frame structures. "
                       "Pick a starting point; every one of them is also in the menus above.");
    ImGui::Spacing();

    const bool hasModel = anaf::BRIDGE::buildBridge().m_objectType.load() != anaf::BRIDGE::ObjectType::no_type;
    bool close = false;
    if (ImGui::BeginTable("##welcome_actions", 2, ImGuiTableFlags_SizingStretchProp)) {
      ImGui::TableSetupColumn("##button", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 11.0f);
      ImGui::TableSetupColumn("##text", ImGuiTableColumnFlags_WidthStretch);

      const std::string exampleText = m_examplePath.empty()
        ? std::string("The example model was not found in the installed assets.")
        : std::format("{}: solved, with stress colors and section force diagrams. "
                      "Edit it in the Frame Editor and run it again.", kExampleName);
      if (actionRow("Open Example", exampleText.c_str(), !m_examplePath.empty()) && onOpenExample) {
        onOpenExample(m_examplePath);
        close = true;
      }
      if (actionRow("New Beam / Frame", "Build a frame node by node, or load one of the built-in models "
                    "(Analyze > Beam / Frame).") && onNewBeam) {
        onNewBeam();
        close = true;
      }
      if (actionRow("New Truss", "Generate a grid truss or build one node by node (Analyze > Truss).") && onNewTruss) {
        onNewTruss();
        close = true;
      }
      if (actionRow("Import File...", "Mesh, result or CAD file: MSH, VTK / VTU, STEP / IGES / BREP (File > Import).") && onImport) {
        onImport();
        close = true;
      }
      ImGui::EndTable();
    }
    if (hasModel) LAYOUT::wrappedColored(THEME::theme().warn, "Opening the example or importing a file replaces the current model.");

    ImGui::Spacing();
    ImGui::TextColored(THEME::theme().note, "Dynamic analysis (modal, harmonic, transient) is not available in this version.");
    ImGui::Separator();
    if (ImGui::Checkbox("Show on startup", &m_showOnStartup)) {
      SETTINGS::settings().showWelcomeOnStartup = m_showOnStartup;
      SETTINGS::save();
    }
    ImGui::SameLine();
    ImGui::TextDisabled("(Help > Welcome opens it again)");

    ImGui::End();
    if (close) isOpen = false;
  }

} // namespace anaf::GUI end
