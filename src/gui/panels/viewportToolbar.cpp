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

#include "viewportToolbar.hpp"

#include "imgui.h"

#include <bridge/generalStatus.hpp>
#include <guiMaterials/iconAtlas.hpp>
#include <guiMaterials/theme.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <mutex>
#include <optional>
#include <string>
#include <utility>

namespace anaf::GUI {

  namespace {
    constexpr float padding = 4.0f;       // window padding of the strip
    constexpr float buttonPadding = 4.0f; // around each icon, so the buttons are square

    struct Choice {
      const char* name;
      ICONS::E_IconId icon;
    };

    // Icon button showing the current choice; its first choice (Off) shows offIcon unselected.
    // Opens a list of the choices with their icons; true when one was picked.
    template <typename Enum, std::size_t N>
    bool choiceButton(const char* label, Enum& value, const std::array<Choice, N>& choices, const ICONS::E_IconId offIcon,
                      const char* tooltip) {
      const auto index = static_cast<std::size_t>(value);
      const bool on = value != Enum{};
      const std::string fallback = std::string(label) + ": " + choices[index].name;
      if (ICONS::iconButton(label, on ? choices[index].icon : offIcon, on, fallback.c_str(), true)) ImGui::OpenPopup(label);
      ImGui::SetItemTooltip("%s: %s\n%s", label, choices[index].name, tooltip);
      bool picked = false;
      if (ImGui::BeginPopup(label)) {
        ImGui::TextDisabled("%s", label);
        for (std::size_t i = 0; i < N; ++i) {
          if (ICONS::iconSelectable(choices[i].icon, choices[i].name, i == index)) {
            value = static_cast<Enum>(i);
            picked = true;
          }
        }
        ImGui::EndPopup();
      }
      return picked;
    }

    // Thin vertical line between button groups.
    void groupSeparator() {
      ImGui::SameLine();
      const ImVec2 cursor = ImGui::GetCursorScreenPos();
      const float height = ICONS::toolbarSize() + 2.0f * buttonPadding;
      ImGui::GetWindowDrawList()->AddLine(ImVec2(cursor.x, cursor.y + 3.0f), ImVec2(cursor.x, cursor.y + height - 3.0f),
                                          THEME::toU32(THEME::theme().border));
      ImGui::Dummy(ImVec2(1.0f, height));
      ImGui::SameLine();
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

  float ViewportToolbar::windowHeight() {
    return std::max(ICONS::toolbarSize() + 2.0f * buttonPadding + 2.0f * padding, ImGui::GetStyle().WindowMinSize.y);
  }

  ViewportToolbar::ViewportToolbar(std::shared_ptr<ViewportDisplayOptions> options, const IPanel* viewport) :
    m_options(std::move(options)),
    m_viewport(viewport)
  {}

  void ViewportToolbar::onImGuiRender() {
    if (!m_viewport->isOpen) return;

    constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
                                       ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(padding, padding));
    const bool visible = ImGui::Begin(windowName, nullptr, flags);
    ImGui::PopStyleVar();
    if (!visible) {
      ImGui::End();
      return;
    }
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(buttonPadding, buttonPadding));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(2.0f, ImGui::GetStyle().ItemSpacing.y));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f); // flat buttons, also in themes with framed inputs

    auto& options = *m_options;
    const auto toggleButton = [&options](const char* label, const ICONS::E_IconId icon, bool& value, const char* tooltip) {
      if (ICONS::iconButton(label, icon, value, label)) {
        value = !value;
        options.changed = true;
      }
      ImGui::SetItemTooltip("%s (%s)\n%s", label, value ? "on" : "off", tooltip);
    };

    renderModelCommands();
    groupSeparator();

    // Keeps the camera reset accessible without keyboard focus on the viewport.
    if (ICONS::iconButton("Fit", ICONS::E_IconId::ViewFit, false, "Reset Camera")) {
      options.resetCameraRequested = true;
    }
    ImGui::SetItemTooltip("Fit the camera to the model (R)");
    groupSeparator();
    struct ViewButton {
      const char* label;
      ICONS::E_IconId icon;
      E_StandardView view;
      const char* tooltip;
    };
    constexpr std::array<ViewButton, 4> views{{{"Front", ICONS::E_IconId::ViewFront, E_StandardView::Front, "Front view (looking along -Z)"},
                                               {"Top", ICONS::E_IconId::ViewTop, E_StandardView::Top, "Top view (looking down -Y)"},
                                               {"Right", ICONS::E_IconId::ViewRight, E_StandardView::Right, "Right view (looking along -X)"},
                                               {"Iso", ICONS::E_IconId::ViewIso, E_StandardView::Iso, "Isometric view"}}};
    for (std::size_t i = 0; i < views.size(); ++i) {
      if (i > 0) ImGui::SameLine();
      if (ICONS::iconButton(views[i].label, views[i].icon, false, views[i].label)) options.viewRequested = views[i].view;
      ImGui::SetItemTooltip("%s", views[i].tooltip);
    }
    groupSeparator();
    toggleButton("Grid", ICONS::E_IconId::ViewGrid, options.showGrid, "x-z ground grid");
    ImGui::SameLine();
    toggleButton("Axes", ICONS::E_IconId::ViewAxes, options.showAxes, "X / Y / Z axis lines");
    groupSeparator();
    constexpr std::array<Choice, 3> nodeStyles{{{"Off", ICONS::E_IconId::DisplayHidden},
                                                {"Square", ICONS::E_IconId::DisplayNodesSquare},
                                                {"Sphere", ICONS::E_IconId::DisplayNodesSphere}}};
    if (choiceButton("Nodes", options.nodeStyle, nodeStyles, ICONS::E_IconId::DisplayNodesSquare,
                     "Nodes as screen squares or 3D spheres; labels and picking")) {
      options.changed = true;
    }
    ImGui::SameLine();
    toggleButton("Forces", ICONS::E_IconId::DisplayLoads, options.showForces, "Applied forces, moments and distributed loads");
    ImGui::SameLine();
    constexpr std::array<Choice, 3> supportStyles{{{"Off", ICONS::E_IconId::DisplayHidden},
                                                   {"Symbols", ICONS::E_IconId::DisplaySupportsSymbols},
                                                   {"DOF", ICONS::E_IconId::DisplaySupportsDof}}};
    if (choiceButton("Supports", options.supportStyle, supportStyles, ICONS::E_IconId::DisplaySupportsSymbols,
                     "Symbols: support types (pin, roller, slider, clamp, cyan hinge axles); "
                     "DOF: a red cone per restrained translation, an amber double cone per restrained rotation")) {
      options.changed = true;
    }
    groupSeparator();
    constexpr std::array<Choice, 3> colorings{{{"Off", ICONS::E_IconId::DisplayHidden},
                                               {"Stress", ICONS::E_IconId::DisplayColorStress},
                                               {"Displacement", ICONS::E_IconId::DisplayColorDisplacement}}};
    if (choiceButton("Color", options.coloring, colorings, ICONS::E_IconId::DisplayColorStress,
                     "Element colors: stress (truss axial, beam von Mises) or displacement")) {
      options.changed = true;
    }
    ImGui::SameLine();
    renderDeformation();

    ImGui::PopStyleVar(3);
    ImGui::End();
  }

  void ViewportToolbar::renderModelCommands() {
    struct Command {
      E_ModelCommand command;
      ICONS::E_IconId icon;
      const char* label;
      const char* tooltip;
      bool groupEnd; // separator after it
    };
    constexpr std::array<Command, 8> buttons{{
      {E_ModelCommand::Library, ICONS::E_IconId::ModelLibrary, "Examples", "Open Example...\nBuilt-in beam and truss models", true},
      {E_ModelCommand::AddNode, ICONS::E_IconId::ModelNode, "Add Node", "Add Node...\nNew node at typed coordinates", false},
      {E_ModelCommand::AddElement, ICONS::E_IconId::ModelElementBeam, "Add Element", "Add Element...\nNew element between two nodes", false},
      {E_ModelCommand::Materials, ICONS::E_IconId::ModelMaterial, "Materials", "Material Handler", false},
      {E_ModelCommand::Sections, ICONS::E_IconId::ModelSection, "Sections", "Section Handler (beam sections)", true},
      {E_ModelCommand::AnalysisSettings, ICONS::E_IconId::AnalysisStatic, "Analysis", "Analysis Settings...\nStatic or dynamic", false},
      {E_ModelCommand::Solve, ICONS::E_IconId::ActionSolve, "Solve", "Run Solver (F5)", false},
      {E_ModelCommand::Diagrams, ICONS::E_IconId::ResultDiagram, "Diagrams", "Beam Diagrams\nSection forces, displacements and stresses along an element", true},
    }};
    const bool truss = BRIDGE::buildBridge().objectType.load() != BRIDGE::E_ObjectType::BeamFrame;
    for (std::size_t i = 0; i < buttons.size(); ++i) {
      const Command& button = buttons[i];
      if (i > 0) {
        if (buttons[i - 1].groupEnd) groupSeparator();
        else ImGui::SameLine();
      }
      const bool available = !commands.isAvailable || commands.isAvailable(button.command);
      // The element command follows the model: a bar for trusses.
      const ICONS::E_IconId icon = button.command == E_ModelCommand::AddElement && truss ? ICONS::E_IconId::ModelElementBar : button.icon;
      const char* tooltip = button.command == E_ModelCommand::AddElement && truss ? "Add Bar...\nNew bar between two nodes" : button.tooltip;
      ImGui::BeginDisabled(!available);
      if (ICONS::iconButton(button.label, icon, false, button.label) && commands.run) commands.run(button.command);
      ImGui::EndDisabled();
      ImGui::SetItemTooltip("%s", tooltip);
    }
  }

  void ViewportToolbar::renderDeformation() {
    auto& bridge = BRIDGE::buildBridge();
    const double scale = bridge.deformScale.load();
    if (ICONS::iconButton("Deformation", ICONS::E_IconId::ResultDisplacement, scale != 1.0, "Deformation", true)) {
      ImGui::OpenPopup("Deformation");
    }
    ImGui::SetItemTooltip("Deformation scale: %.4g\nThe drawn shape is location + displacement x scale (rotations too)", scale);
    if (!ImGui::BeginPopup("Deformation")) return;
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(5.0f, 3.0f));
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
    ImGui::PopStyleVar();
    ImGui::EndPopup();
  }

} // namespace anaf::GUI end
