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

#include <log/anaf_info.hpp>
#include <guiMaterials/imGuiLayer.hpp>

#include "imgui.h"
#include <cstddef>
#include <mutex>
#include <string>
#include <vector>
#include <string_view>


namespace anaf::GUI {

  LogTerminal::LogTerminal() {
    anaf::LOG::setCallback([](anaf::LOG::Level level, std::string_view message) {
      std::lock_guard<std::mutex> lock(g_log_mutex);
      g_ui_logs.push_back({level, std::string(message)});

      if (g_ui_logs.size() > g_ui_log_max_num) {
        g_ui_logs.erase(g_ui_logs.begin());
      }
    });
  }

  void LogTerminal::onImGuiRender() {
    ImGui::PushFont(ImGuiLayer::font_console);

    ImGui::Begin("Console");

    if (ImGui::Button("Clear")) {
      std::lock_guard<std::mutex> lock(g_log_mutex);
      g_ui_logs.clear();
    }

    {
      // Sinks push from worker threads, so trimming needs the lock too.
      std::lock_guard<std::mutex> lock(g_log_mutex);
      if (g_ui_logs.size() > g_ui_log_max_num) {
        const auto excess = static_cast<std::ptrdiff_t>(g_ui_logs.size() - g_ui_log_max_num);
        g_ui_logs.erase(g_ui_logs.begin(), g_ui_logs.begin() + excess);
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
      std::lock_guard<std::mutex> lock(g_log_mutex);
      if(!g_ui_logs.empty()) {
        ImGui::SetClipboardText(g_ui_logs.back().text.c_str());
        s_copiedFeedbackTimer = 1.5f;
        glfwPostEmptyEvent();
      }
    }

    const float input_width = 80.0f;
    const char* label_text = "Max Output";
    const float total_width = ImGui::CalcTextSize(label_text).x + ImGui::GetStyle().ItemSpacing.x + input_width;
    ImGui::SameLine();
    const float right_cursor_x = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - total_width;
    if (right_cursor_x > ImGui::GetCursorPosX()) ImGui::SameLine(right_cursor_x);

    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label_text);

    ImGui::SameLine();
    ImGui::SetNextItemWidth(input_width);
    ImGui::InputScalar("##output_num", ImGuiDataType_U32, &g_ui_log_max_num);

    ImGui::Separator();
    
    // Negative height leaves room for the status footer below the log.
    ImGui::BeginChild("LogScrollRegion", ImVec2(0, -StatusBar::height()), ImGuiChildFlags_None,
                      m_wrapLines ? ImGuiWindowFlags_None : ImGuiWindowFlags_HorizontalScrollbar);
    // 0.0f wraps at the right edge of the region, so lines re-wrap when the panel is resized.
    if (m_wrapLines) ImGui::PushTextWrapPos(0.0f);
    {
      std::lock_guard<std::mutex> lock(g_log_mutex);
      for (std::size_t i{0}; i < g_ui_logs.size(); ++i) {
        const auto& log = g_ui_logs[i];

        ImGui::PushID(static_cast<int>(i));

        ImVec4 color = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
        switch (log.level) {
          case anaf::LOG::Level::INFO: color = ImVec4(0.4f, 0.7f, 1.0f, 1.0f); break;
          case anaf::LOG::Level::WARN: color = ImVec4(1.0f, 0.8f, 0.2f, 1.0f); break;
          case anaf::LOG::Level::ERR: color = ImVec4(1.0f, 0.3f, 0.3f, 1.0f); break;
          case anaf::LOG::Level::SUCCESS: color = ImVec4(0.3f, 1.0f, 0.3f, 1.0f); break;
          case anaf::LOG::Level::CORE: color = ImVec4(0.0f, 0.9f, 0.9f, 1.0f); break;
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
      }
    }
    if (m_wrapLines) ImGui::PopTextWrapPos();

    if (ImGui::BeginPopupContextWindow(nullptr, ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems)) {
      if(ImGui::MenuItem("Copy Last Log")) {
        std::lock_guard<std::mutex> lock(g_log_mutex);
        if(!g_ui_logs.empty()) {
          ImGui::SetClipboardText(g_ui_logs.back().text.c_str());
          glfwPostEmptyEvent();
          s_copiedFeedbackTimer = 1.5f;
        }
      }
      if (ImGui::MenuItem("Copy All Logs")) {
        std::string full_log;
        {
          std::lock_guard<std::mutex> lock(g_log_mutex);
          std::size_t total_size {0};
          for (const auto& log : g_ui_logs) {
            total_size += log.text.size() + 1;
          }
          full_log.reserve(total_size);
          for (const auto& log : g_ui_logs) {
            full_log.append(log.text);
            full_log.push_back('\n');
          }
          ImGui::SetClipboardText(full_log.c_str());
          glfwPostEmptyEvent();
          s_copiedFeedbackTimer = 1.5f;
        }
      }
      if (ImGui::MenuItem("Clear All")) {
        std::lock_guard<std::mutex> lock(g_log_mutex);
        g_ui_logs.clear();
      }
      ImGui::EndPopup();
    }

    if (s_copiedFeedbackTimer > 0.0f) {
      s_copiedFeedbackTimer -= ImGui::GetIO().DeltaTime;
      ImGui::SameLine();
      ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), "Copied!");
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
