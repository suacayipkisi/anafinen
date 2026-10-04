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
    toggleButton("Nodes", options.showNodes, "Node points, labels and picking");
    ImGui::SameLine();
    toggleButton("Forces", options.showForces, "Applied force arrows");
    ImGui::SameLine();
    toggleButton("Stress", options.showStress, "Stress coloring of the elements");

    ImGui::End();
  }

} // namespace anaf::GUI end
