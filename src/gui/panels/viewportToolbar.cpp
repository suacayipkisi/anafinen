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

#include <guiMaterials/iconAtlas.hpp>
#include <guiMaterials/theme.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
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

    ImGui::PopStyleVar(3);
    ImGui::End();
  }

} // namespace anaf::GUI end
