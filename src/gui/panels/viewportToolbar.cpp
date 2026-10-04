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

#include <algorithm>
#include <array>
#include <cstddef>
#include <string>
#include <utility>

namespace anaf::GUI {

  namespace {
    constexpr float kPadding = 6.0f;

    // Hover and press keep the button's color, so only a toggle that is on stands out.
    bool stateButton(const char* label, const bool on) {
      const ImVec4 color = ImGui::GetStyleColorVec4(on ? ImGuiCol_ButtonActive : ImGuiCol_Button);
      ImGui::PushStyleColor(ImGuiCol_Button, color);
      ImGui::PushStyleColor(ImGuiCol_ButtonHovered, color);
      ImGui::PushStyleColor(ImGuiCol_ButtonActive, color);
      const bool pressed = ImGui::Button(label);
      ImGui::PopStyleColor(3);
      return pressed;
    }
    // Button that shows "label: current" and opens a list of choices; true when one was picked.
    template <typename Enum, std::size_t N>
    bool choiceButton(const char* label, Enum& value, const std::array<const char*, N>& names, const char* tooltip) {
      const auto index = static_cast<std::size_t>(value);
      const std::string text = std::string(label) + ": " + names[index] + "##" + label;
      if (stateButton(text.c_str(), value != Enum{})) ImGui::OpenPopup(label);
      ImGui::SetItemTooltip("%s", tooltip);
      bool picked = false;
      if (ImGui::BeginPopup(label)) {
        for (std::size_t i = 0; i < N; ++i) {
          if (ImGui::Selectable(names[i], i == index)) {
            value = static_cast<Enum>(i);
            picked = true;
          }
        }
        ImGui::EndPopup();
      }
      return picked;
    }
  } // namespace end

  float ViewportToolbar::windowHeight() {
    return std::max(ImGui::GetFrameHeight() + 2.0f * kPadding, ImGui::GetStyle().WindowMinSize.y);
  }

  ViewportToolbar::ViewportToolbar(std::shared_ptr<ViewportDisplayOptions> options, const IPanel* viewport) :
    m_options(std::move(options)),
    m_viewport(viewport)
  {}

  void ViewportToolbar::onImGuiRender() {
    if (!m_viewport->isOpen) return;

    constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
                                       ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(kPadding, kPadding));
    const bool visible = ImGui::Begin(kWindowName, nullptr, flags);
    ImGui::PopStyleVar();
    if (!visible) {
      ImGui::End();
      return;
    }

    auto& options = *m_options;
    const auto toggleButton = [&options](const char* label, bool& value, const char* tooltip) {
      if (stateButton(label, value)) {
        value = !value;
        options.changed = true;
      }
      ImGui::SetItemTooltip("%s (%s)", tooltip, value ? "on" : "off");
    };

    // Keeps the camera reset accessible without keyboard focus on the viewport.
    if (stateButton("Reset Camera", false)) {
      options.resetCameraRequested = true;
    }
    ImGui::SetItemTooltip("Fit the camera to the model (R)");
    ImGui::SameLine();
    toggleButton("Grid", options.showGrid, "x-z ground grid");
    ImGui::SameLine();
    toggleButton("Axes", options.showAxes, "X / Y / Z axis lines");
    ImGui::SameLine();
    constexpr std::array<const char*, 3> nodeStyles{"Off", "Square", "Sphere"};
    if (choiceButton("Nodes", options.nodeStyle, nodeStyles, "Nodes as screen squares or 3D spheres; labels and picking")) options.changed = true;
    ImGui::SameLine();
    toggleButton("Forces", options.showForces, "Applied forces, moments and distributed loads");
    ImGui::SameLine();
    constexpr std::array<const char*, 3> colorings{"Off", "Stress", "Displacement"};
    if (choiceButton("Color", options.coloring, colorings, "Element colors: stress (truss axial, beam von Mises) or displacement")) options.changed = true;

    ImGui::End();
  }

} // namespace anaf::GUI end
