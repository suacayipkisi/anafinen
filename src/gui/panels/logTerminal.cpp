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

#include "logTerminal.hpp"

#include <guiMaterials/theme.hpp>

#include <log/anaf_info.hpp>
#include <guiMaterials/imGuiLayer.hpp>

#include "imgui.h"
#include <GLFW/glfw3.h>

#include <cstddef>
#include <mutex>
#include <string>

namespace anaf::GUI {

  void LogTerminal::onImGuiRender() {
    ImGui::PushFont(ImGuiLayer::g_fontConsole);

    ImGui::Begin("Console", &isOpen);

    if (ImGui::Button("Clear")) {
      std::lock_guard<std::mutex> lock(g_logMutex);
      g_uiLogs.clear();
    }

    {
      // Sinks push from worker threads, so trimming needs the lock too. Needed when the
      // limit was lowered in the console: the sink drops only one entry per new line.
      std::lock_guard<std::mutex> lock(g_logMutex);
      if (g_uiLogs.size() > g_uiLogMaxNum) {
        const auto excess = static_cast<std::ptrdiff_t>(g_uiLogs.size() - g_uiLogMaxNum);
        g_uiLogs.erase(g_uiLogs.begin(), g_uiLogs.begin() + excess);
      }
    }

    ImGui::SameLine();
    ImGui::Checkbox("Auto-scroll", &m_autoScroll);
    ImGui::SameLine();
    ImGui::Checkbox("Wrap lines", &m_wrapLines);
    if (ImGui::IsItemHovered()) {
      ImGui::SetTooltip("Off: long lines stay on one row and the console scrolls horizontally");
    }

    static float s_copiedFeedbackTimer = 0.0f;
    ImGui::SameLine();
    if (ImGui::Button("Copy Last Log")) {
      std::lock_guard<std::mutex> lock(g_logMutex);
      if(!g_uiLogs.empty()) {
        ImGui::SetClipboardText(g_uiLogs.back().text.c_str());
        s_copiedFeedbackTimer = 1.5f;
        glfwPostEmptyEvent();
      }
    }

    const float inputWidth = 80.0f;
    const char* labelText = "Max Output";
    const float totalWidth = ImGui::CalcTextSize(labelText).x + ImGui::GetStyle().ItemSpacing.x + inputWidth;
    ImGui::SameLine();
    const float rightCursorX = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - totalWidth;
    if (rightCursorX > ImGui::GetCursorPosX()) ImGui::SameLine(rightCursorX);

    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(labelText);

    ImGui::SameLine();
    ImGui::SetNextItemWidth(inputWidth);
    ImGui::InputScalar("##output_num", ImGuiDataType_U32, &g_uiLogMaxNum);

    ImGui::Separator();
    
    // Negative height leaves room for the status footer below the log.
    ImGui::BeginChild("LogScrollRegion", ImVec2(0, -StatusBar::height()), ImGuiChildFlags_None,
                      m_wrapLines ? ImGuiWindowFlags_None : ImGuiWindowFlags_HorizontalScrollbar);
    // 0.0f wraps at the right edge of the region, so lines re-wrap when the panel is resized.
    if (m_wrapLines) ImGui::PushTextWrapPos(0.0f);
    {
      std::lock_guard<std::mutex> lock(g_logMutex);
      const auto drawLine = [](const std::size_t i) {
        const auto& log = g_uiLogs[i];

        ImGui::PushID(static_cast<int>(i));

        const THEME::ThemePalette& palette = THEME::theme();
        ImVec4 color = palette.text;
        switch (log.level) {
          case anaf::LOG::E_Level::INFO: color = palette.info; break;
          case anaf::LOG::E_Level::WARN: color = palette.warn; break;
          case anaf::LOG::E_Level::ERR: color = palette.bad; break;
          case anaf::LOG::E_Level::SUCCESS: color = palette.good; break;
          case anaf::LOG::E_Level::CORE: color = palette.core; break;
        }
        ImGui::PushStyleColor(ImGuiCol_Text, color);
        ImGui::TextUnformatted(log.text.c_str());
        ImGui::PopStyleColor();

        if (ImGui::BeginPopupContextItem("LogLineContextMenu")) {
          if(ImGui::MenuItem("Copy Line")) {
            ImGui::SetClipboardText(log.text.c_str());
            glfwPostEmptyEvent();
          }
          ImGui::EndPopup();
        }

        ImGui::PopID();
      };
      if (m_wrapLines) {
        // Wrapped lines differ in height, which ImGuiListClipper cannot skip; every line is laid out.
        for (std::size_t i = 0; i < g_uiLogs.size(); ++i) drawLine(i);
      } else {
        // One row per line: only the visible rows are drawn.
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(g_uiLogs.size()));
        while (clipper.Step()) {
          for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) drawLine(static_cast<std::size_t>(row));
        }
      }
    }
    if (m_wrapLines) ImGui::PopTextWrapPos();

    if (ImGui::BeginPopupContextWindow(nullptr, ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems)) {
      if(ImGui::MenuItem("Copy Last Log")) {
        std::lock_guard<std::mutex> lock(g_logMutex);
        if(!g_uiLogs.empty()) {
          ImGui::SetClipboardText(g_uiLogs.back().text.c_str());
          glfwPostEmptyEvent();
          s_copiedFeedbackTimer = 1.5f;
        }
      }
      if (ImGui::MenuItem("Copy All Logs")) {
        std::string fullLog;
        {
          std::lock_guard<std::mutex> lock(g_logMutex);
          std::size_t totalSize {0};
          for (const auto& log : g_uiLogs) {
            totalSize += log.text.size() + 1;
          }
          fullLog.reserve(totalSize);
          for (const auto& log : g_uiLogs) {
            fullLog.append(log.text);
            fullLog.push_back('\n');
          }
          ImGui::SetClipboardText(fullLog.c_str());
          glfwPostEmptyEvent();
          s_copiedFeedbackTimer = 1.5f;
        }
      }
      if (ImGui::MenuItem("Clear All")) {
        std::lock_guard<std::mutex> lock(g_logMutex);
        g_uiLogs.clear();
      }
      ImGui::EndPopup();
    }

    if (s_copiedFeedbackTimer > 0.0f) {
      s_copiedFeedbackTimer -= ImGui::GetIO().DeltaTime;
      ImGui::SameLine();
      ImGui::TextColored(THEME::theme().good, "Copied!");
    }

    if (m_autoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) {
      ImGui::SetScrollHereY(1.0f);
    }

    ImGui::EndChild();
    m_statusBar.render();
    ImGui::End();

    ImGui::PopFont();
  }

} // namespace anaf::GUI end
