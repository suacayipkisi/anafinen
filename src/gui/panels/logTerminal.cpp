// Copyright (c) 2026 Ufuk Deniz Konuk
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
#include <algorithm>
#include <cstddef>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>
#include <string_view>

#include <bridge/generalStatus.hpp>

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

  void LogTerminal::updateUsage() {
    const auto now = std::chrono::steady_clock::now();
    if (m_lastUsageSample.time_since_epoch().count() != 0
        && now - m_lastUsageSample < std::chrono::milliseconds(500)) {
      return;
    }

    std::ifstream processStat("/proc/self/stat");
    std::string statLine;
    if (std::getline(processStat, statLine)) {
      const std::size_t commandEnd = statLine.rfind(") ");
      if (commandEnd != std::string::npos) {
        std::istringstream fields(statLine.substr(commandEnd + 2));
        std::string field;
        std::vector<unsigned long long> values;
        while (fields >> field) {
          try {
            values.push_back(std::stoull(field));
          }
          catch (const std::exception&) {
            values.push_back(0);
          }
        }

        if (values.size() > 12) {
          const unsigned long long processTicks = values[11] + values[12];
          std::ifstream systemStat("/proc/stat");
          std::string cpuLine;
          std::getline(systemStat, cpuLine);
          std::istringstream cpuFields(cpuLine);
          cpuFields >> field;
          unsigned long long systemTicks = 0;
          unsigned long long tick = 0;
          while (cpuFields >> tick) systemTicks += tick;

          if (m_lastSystemTicks != 0 && systemTicks > m_lastSystemTicks) {
            const double processDelta = static_cast<double>(processTicks - m_lastProcessTicks);
            const double systemDelta = static_cast<double>(systemTicks - m_lastSystemTicks);
            m_processCpuPercent = static_cast<float>(
              std::clamp(100.0 * processDelta / systemDelta, 0.0, 100.0)
            );
          }
          m_lastProcessTicks = processTicks;
          m_lastSystemTicks = systemTicks;
        }
      }
    }

    std::ifstream status("/proc/self/status");
    std::string line;
    while (std::getline(status, line)) {
      if (line.starts_with("VmRSS:")) {
        std::istringstream fields(line.substr(6));
        float kib = 0.0f;
        fields >> kib;
        m_processRamMiB = kib / 1024.0f;
        break;
      }
    }

    std::ifstream memory("/proc/meminfo");
    unsigned long long totalKiB = 0;
    unsigned long long availableKiB = 0;
    while (std::getline(memory, line)) {
      std::istringstream fields(line);
      std::string key;
      unsigned long long value = 0;
      fields >> key >> value;
      if (key == "MemTotal:") totalKiB = value;
      if (key == "MemAvailable:") availableKiB = value;
    }
    if (totalKiB != 0) {
      m_systemRamPercent = static_cast<float>(
        std::clamp(100.0 * (totalKiB - availableKiB) / totalKiB, 0.0, 100.0)
      );
    }
    m_lastUsageSample = now;
  }

  void LogTerminal::renderUsage() {
    updateUsage();
    const auto& bridge = anaf::BRIDGE::buildBridge();
    const bool busy = bridge.m_isRunning.load() || bridge.m_isGeneratingPreview.load();
    const ImVec4 accent = busy
      ? ImVec4(0.95f, 0.75f, 0.25f, 1.0f)
      : ImVec4(0.45f, 0.85f, 0.55f, 1.0f);

    const char* state = busy ? "WORKING" : "IDLE";
    ImGui::PushStyleColor(ImGuiCol_Text, accent);
    ImGui::Text("%s  CPU %.0f%%  RAM %.0f MiB  |  System %.0f%%  |  GPU N/A", state,
      m_processCpuPercent, m_processRamMiB, m_systemRamPercent);
    ImGui::PopStyleColor();
  }

  void LogTerminal::onImGuiRender() {
    ImGui::PushFont(ImGuiLayer::font_console);

    ImGui::Begin("Console");

    if (ImGui::Button("Clear")) {
      std::lock_guard<std::mutex> lock(g_log_mutex);
      g_ui_logs.clear();
    }

    if (g_ui_logs.size() > g_ui_log_max_num) {
      g_ui_logs.erase(g_ui_logs.begin());
    }

    ImGui::SameLine();
    ImGui::Checkbox("Auto-scroll", &m_autoScroll);

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
    const float text_width = ImGui::CalcTextSize(label_text).x;
    const float style_spacing = ImGui::GetStyle().ItemSpacing.x;
    const float total_width = text_width + style_spacing + input_width;
    const float usage_width = 520.0f;
    const float content_right = ImGui::GetWindowContentRegionMax().x;
    const float usage_cursor_x = content_right - total_width - style_spacing - usage_width;

    if (usage_cursor_x > ImGui::GetCursorPosX()) {
      ImGui::SameLine(usage_cursor_x);
    } else {
      ImGui::SameLine();
    }
    renderUsage();

    float right_cursor_x = content_right - total_width;

    if (right_cursor_x > ImGui::GetCursorPosX()) {
      ImGui::SameLine(right_cursor_x);
    } else {
      ImGui::SameLine();
    }

    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label_text);

    ImGui::SameLine();
    ImGui::SetNextItemWidth(input_width);
    ImGui::InputScalar("##output_num", ImGuiDataType_U32, &g_ui_log_max_num);

    ImGui::Separator();
    
    ImGui::BeginChild("LogScrollRegion", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);
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
    ImGui::End();

    ImGui::PopFont();
  }

} // namespace anaf::GUI end
