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

#include "statusBar.hpp"

#include <bridge/generalStatus.hpp>

#include "imgui.h"
#include "imgui_internal.h" // BeginViewportSideBar

#include <cstdio>

namespace anaf::GUI {

  void StatusBar::onImGuiRender() {
    if (!isOpen) return;

    const auto& bridge = BRIDGE::buildBridge();
    const bool solving = bridge.m_isRunning.load();
    const bool previewing = bridge.m_isGeneratingPreview.load();

    constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_MenuBar;
    if (ImGui::BeginViewportSideBar("##MainStatusBar", ImGui::GetMainViewport(), ImGuiDir_Down, ImGui::GetFrameHeight(), flags)) {
      if (ImGui::BeginMenuBar()) {
        // Left: what the worker is doing.
        const ImVec4 accent = (solving || previewing) ? ImVec4(0.95f, 0.75f, 0.25f, 1.0f) : ImVec4(0.45f, 0.85f, 0.55f, 1.0f);
        char state[48];
        if (solving) std::snprintf(state, sizeof(state), "SOLVING %.0f%%", static_cast<double>(bridge.m_progress.load() * 100.0f));
        else if (previewing) std::snprintf(state, sizeof(state), "GENERATING PREVIEW");
        else std::snprintf(state, sizeof(state), "IDLE");
        ImGui::TextColored(accent, "%s", state);

        // Right: resource usage, right-aligned.
        const auto& usage = m_monitor.sample();
        char resources[128];
        if (usage.available) {
          std::snprintf(resources, sizeof(resources), "CPU %.0f%%   RAM %.0f MiB   System RAM %.0f%%   GPU N/A",
                        static_cast<double>(usage.processCpuPercent), static_cast<double>(usage.processRamMiB),
                        static_cast<double>(usage.systemRamPercent));
        } else {
          std::snprintf(resources, sizeof(resources), "Resource usage not available on this platform");
        }
        const float width = ImGui::CalcTextSize(resources).x + ImGui::GetStyle().ItemSpacing.x;
        const float right = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - width;
        if (right > ImGui::GetCursorPosX()) ImGui::SameLine(right);
        else ImGui::SameLine();
        ImGui::TextDisabled("%s", resources);

        ImGui::EndMenuBar();
      }
    }
    ImGui::End();
  }

} // namespace anaf::GUI end
