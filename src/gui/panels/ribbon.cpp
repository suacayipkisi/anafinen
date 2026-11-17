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


#include "ribbon.hpp"

#include <bridge/generalStatus.hpp>
#include <guiMaterials/iconAtlas.hpp>
#include <guiMaterials/theme.hpp>

#include "imgui.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <mutex>
#include <optional>
#include <string_view>

namespace anaf::GUI {

  namespace {
    constexpr float padding = 4.0f;       // window padding of the strip
    constexpr float buttonPadding = 5.0f; // around the icon and the label inside a button
    constexpr float labelScale = 0.87f;   // button labels, relative to the UI font size
    constexpr float captionScale = 0.78f; // group captions

    float iconSize() { return std::round(ImGui::GetFontSize() * 1.6f); }
    float labelSize() { return std::round(ImGui::GetFontSize() * labelScale); }
    float captionSize() { return std::round(ImGui::GetFontSize() * captionScale); }
    float buttonHeight() { return iconSize() + labelSize() + 3.0f * buttonPadding; }

    struct CommandInfo {
      E_ModelCommand command;
      ICONS::E_IconId icon;
      const char* label;
      const char* tooltip;
    };

    constexpr std::array<CommandInfo, 12> commandInfos{{
      {E_ModelCommand::Library, ICONS::E_IconId::ModelLibrary, "Examples", "Open Example...\nBuilt-in beam and truss models"},
      {E_ModelCommand::Import, ICONS::E_IconId::ActionImport, "Import", "Import Mesh / CAD... (Ctrl+O)"},
      {E_ModelCommand::Export, ICONS::E_IconId::ActionExport, "Export", "Export Model... (Ctrl+E)"},
      {E_ModelCommand::AddNode, ICONS::E_IconId::ModelNode, "Node", "Add Node...\nNew node at typed coordinates"},
      {E_ModelCommand::AddElement, ICONS::E_IconId::ModelElementBeam, "Element", "Add Element...\nNew element between two nodes"},
      {E_ModelCommand::AssignAll, ICONS::E_IconId::ActionSettings, "Assign", "Assign to All...\nMaterial, section / area and formulation of every member"},
      {E_ModelCommand::Materials, ICONS::E_IconId::ModelMaterial, "Materials", "Material Handler"},
      {E_ModelCommand::Sections, ICONS::E_IconId::ModelSection, "Sections", "Section Handler (beam sections)"},
      {E_ModelCommand::SelfWeight, ICONS::E_IconId::LoadGravity, "Self Weight", "Self weight on / off (density x area x g, -Y)\nTrusses always carry it"},
      {E_ModelCommand::AnalysisSettings, ICONS::E_IconId::AnalysisStatic, "Settings", "Analysis Settings...\nStatic or dynamic"},
      {E_ModelCommand::Solve, ICONS::E_IconId::ActionSolve, "Solve", "Run Solver (F5)"},
      {E_ModelCommand::Diagrams, ICONS::E_IconId::ResultDiagram, "Diagrams", "Beam Diagrams\nSection forces, displacements and stresses along an element"},
    }};

    const CommandInfo& info(const E_ModelCommand command) {
      return *std::ranges::find_if(commandInfos, [command](const CommandInfo& entry) { return entry.command == command; });
    }

    // Large flat button: icon above its label, grey hover, accent tint and line when active.
    bool largeButton(const char* id, const ICONS::E_IconId icon, const char* label, const bool active, const bool dropdown) {
      const THEME::ThemePalette& palette = THEME::theme();
      const float iconPixels = iconSize();
      const float labelFont = labelSize();
      ImGui::PushFont(nullptr, labelFont);
      const float labelWidth = ImGui::CalcTextSize(label).x;
      ImGui::PopFont();
      const ImVec2 size(std::max(iconPixels, labelWidth) + 2.0f * buttonPadding + 2.0f, buttonHeight());
      const bool clicked = ImGui::InvisibleButton(id, size);
      const bool hovered = ImGui::IsItemHovered();
      const bool held = ImGui::IsItemActive();
      const ImVec2 min = ImGui::GetItemRectMin();
      const ImVec2 max = ImGui::GetItemRectMax();
      ImDrawList* drawList = ImGui::GetWindowDrawList();

      if (active) drawList->AddRectFilled(min, max, ImGui::GetColorU32(THEME::mix(palette.input, palette.accent, 0.22f)));
      if (hovered || held) {
        drawList->AddRectFilled(min, max, ImGui::GetColorU32(THEME::toU32(palette.text, held ? 0.14f : 0.08f)));
      }
      if (active) drawList->AddRectFilled(ImVec2(min.x, max.y - 2.0f), max, ImGui::GetColorU32(palette.accent));

      if (const std::optional<ICONS::IconImage> image = ICONS::icon(icon, iconPixels)) {
        const ImVec2 iconMin(std::round(min.x + (size.x - iconPixels) * 0.5f), std::round(min.y + buttonPadding));
        drawList->AddImage(image->texture, iconMin, ImVec2(iconMin.x + iconPixels, iconMin.y + iconPixels), image->uv0, image->uv1,
                           ImGui::GetColorU32(ImVec4(1.0f, 1.0f, 1.0f, 1.0f)));
      }
      ImGui::PushFont(nullptr, labelFont);
      drawList->AddText(ImVec2(std::round(min.x + (size.x - labelWidth) * 0.5f), min.y + 2.0f * buttonPadding + iconPixels),
                        ImGui::GetColorU32(ImGuiCol_Text), label);
      ImGui::PopFont();
      if (dropdown) {
        const float side = std::max(3.0f, std::round(iconPixels * 0.18f));
        const ImVec2 corner(max.x - 3.0f, min.y + buttonPadding + iconPixels);
        drawList->AddTriangleFilled(ImVec2(corner.x, corner.y - side), corner, ImVec2(corner.x - side, corner.y),
                                    ImGui::GetColorU32(palette.textDim));
      }
      return clicked;
    }

    // Group of buttons with its caption centered below and a vertical line after it.
    template <typename Buttons>
    void group(const char* caption, Buttons&& buttons) {
      const THEME::ThemePalette& palette = THEME::theme();
      const ImVec2 start = ImGui::GetCursorScreenPos();
      ImGui::BeginGroup();
      buttons();
      const float width = std::max(ImGui::GetItemRectMax().x - start.x, 1.0f);
      const float rowHeight = buttonHeight(); // before the caption font changes GetFontSize()
      // The last button left the cursor on its line; the caption row starts under the buttons.
      ImGui::SetCursorScreenPos(ImVec2(start.x, start.y + rowHeight));
      ImGui::PushFont(nullptr, captionSize());
      const float captionWidth = ImGui::CalcTextSize(caption).x;
      ImGui::GetWindowDrawList()->AddText(ImVec2(std::round(start.x + (width - captionWidth) * 0.5f), start.y + rowHeight + 1.0f),
                                          ImGui::GetColorU32(palette.textDim), caption);
      ImGui::PopFont();
      ImGui::Dummy(ImVec2(width, captionSize() + 2.0f));
      ImGui::EndGroup();
      ImGui::SameLine(0.0f, 6.0f);
      const ImVec2 line = ImGui::GetCursorScreenPos();
      ImGui::GetWindowDrawList()->AddLine(ImVec2(line.x, line.y + 4.0f), ImVec2(line.x, line.y + buttonHeight() + captionSize() - 2.0f),
                                          ImGui::GetColorU32(palette.border));
      ImGui::Dummy(ImVec2(1.0f, 1.0f));
      ImGui::SameLine(0.0f, 6.0f);
    }

    // Scale that draws the largest nodal displacement as 5 % of the model's bounding-box diagonal.
    template <typename Nodes>
    std::optional<double> fittedScale(const Nodes& nodes) {
      double largest = 0.0;
      std::array<double, 3> low{1e300, 1e300, 1e300}, high{-1e300, -1e300, -1e300};
      for (const auto& node : nodes) {
        for (std::size_t axis = 0; axis < 3; ++axis) {
          low[axis] = std::min(low[axis], node.getLocation()[axis]);
          high[axis] = std::max(high[axis], node.getLocation()[axis]);
        }
        const auto& d = node.getDisplacement();
        largest = std::max(largest, std::hypot(d[0], d[1], d[2]));
      }
      const double size = std::hypot(high[0] - low[0], high[1] - low[1], high[2] - low[2]);
      if (!(largest > 0.0) || !(size > 0.0)) return std::nullopt;
      return 0.05 * size / largest;
    }

    // Auto scale of the active model's results; nothing without results.
    std::optional<double> autoDeformationScale(BRIDGE::GuiCalcBridge& bridge) {
      std::lock_guard lock(bridge.dataMutex);
      if (bridge.objectType.load() == BRIDGE::E_ObjectType::BeamFrame) {
        if (!bridge.activeBeamMesh || !bridge.activeBeamMesh->hasResults) return std::nullopt;
        return fittedScale(bridge.activeBeamMesh->nodes);
      }
      if (!bridge.activeMesh || !bridge.activeMesh->hasResults) return std::nullopt;
      return fittedScale(bridge.activeMesh->trussNodes);
    }
  } // namespace end

  float Ribbon::windowHeight() {
    return std::max(buttonHeight() + captionSize() + 2.0f + 2.0f * padding, ImGui::GetStyle().WindowMinSize.y);
  }

  void Ribbon::commandButton(const E_ModelCommand command) {
    const CommandInfo& entry = info(command);
    const bool available = !commands.isAvailable || commands.isAvailable(command);
    const bool active = commands.isActive && commands.isActive(command);
    // The element command follows the model: a bar for trusses.
    const bool truss = BRIDGE::buildBridge().objectType.load() != BRIDGE::E_ObjectType::BeamFrame;
    const bool bar = command == E_ModelCommand::AddElement && truss;
    ImGui::BeginDisabled(!available);
    if (largeButton(entry.label, bar ? ICONS::E_IconId::ModelElementBar : entry.icon, bar ? "Bar" : entry.label, active, false) && commands.run) {
      commands.run(command);
    }
    ImGui::EndDisabled();
    ImGui::SetItemTooltip("%s", bar ? "Add Bar...\nNew bar between two nodes" : entry.tooltip);
    ImGui::SameLine(0.0f, 2.0f);
  }

  void Ribbon::renderDeformation() {
    auto& bridge = BRIDGE::buildBridge();
    const double scale = bridge.deformScale.load();
    if (largeButton("Deformation", ICONS::E_IconId::ResultDisplacement, "Deform", scale != 1.0, true)) ImGui::OpenPopup("##deformation");
    ImGui::SetItemTooltip("Deformation scale: %.4g\nThe drawn shape is location + displacement x scale (rotations too)", scale);
    ImGui::SameLine(0.0f, 2.0f);
    if (!ImGui::BeginPopup("##deformation")) return;
    ImGui::TextDisabled("Deformation scale");
    double typed = scale;
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 7.0f);
    if (ImGui::InputDouble("##deformation_scale", &typed, 0.0, 0.0, "%.4g")) {
      bridge.deformScale = std::max(typed, 0.0);
      bridge.dataVersion.fetch_add(1, std::memory_order_release);
    }
    const std::optional<double> fitted = autoDeformationScale(bridge);
    ImGui::BeginDisabled(!fitted);
    if (ImGui::Button("Auto") && fitted) {
      bridge.deformScale = *fitted;
      bridge.dataVersion.fetch_add(1, std::memory_order_release);
    }
    ImGui::SetItemTooltip("Draw the largest nodal displacement as 5 %% of the model size (needs results)");
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("True Size")) {
      bridge.deformScale = 1.0;
      bridge.dataVersion.fetch_add(1, std::memory_order_release);
    }
    ImGui::EndPopup();
  }

  void Ribbon::onImGuiRender() {
    constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar |
                                       ImGuiWindowFlags_NoScrollWithMouse;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(padding + 2.0f, padding));
    const bool visible = ImGui::Begin(windowName, nullptr, flags);
    ImGui::PopStyleVar();
    if (!visible) {
      ImGui::End();
      return;
    }
    group("FILE", [&] {
      commandButton(E_ModelCommand::Library);
      commandButton(E_ModelCommand::Import);
      commandButton(E_ModelCommand::Export);
    });
    group("MODEL", [&] {
      commandButton(E_ModelCommand::AddNode);
      commandButton(E_ModelCommand::AddElement);
      commandButton(E_ModelCommand::AssignAll);
      commandButton(E_ModelCommand::Materials);
      commandButton(E_ModelCommand::Sections);
    });
    group("LOADS", [&] { commandButton(E_ModelCommand::SelfWeight); });
    group("ANALYSIS", [&] {
      commandButton(E_ModelCommand::AnalysisSettings);
      commandButton(E_ModelCommand::Solve);
    });
    group("RESULTS", [&] {
      renderDeformation();
      commandButton(E_ModelCommand::Diagrams);
    });
    ImGui::End();
  }

} // namespace anaf::GUI end
