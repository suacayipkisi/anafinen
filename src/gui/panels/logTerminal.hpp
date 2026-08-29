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

#pragma once

#include "../../log/anaf_info.h"

#include "../guiMaterials/iPanel.hpp"
#include "imgui.h"
#include <mutex>
#include <string>
#include <vector>

struct LogEntry {
    AnafLogLevel level;
    std::string text;
};

inline std::vector<LogEntry> g_ui_logs;
inline std::mutex g_log_mutex;

inline void anafUILogSink(AnafLogLevel level, const char* message) {
    std::lock_guard<std::mutex> lock(g_log_mutex);
    g_ui_logs.push_back({level, std::string(message)});
}

class LogTerminal : public IPanel {
private:
    bool m_autoScroll {true};
public:
    LogTerminal() {
        anaf_logger_set_callback(anafUILogSink);
    }

    void  onImGuiRender() override {
        ImGui::Begin("Console");

        if (ImGui::Button("Clear")) {
            std::lock_guard<std::mutex> lock(g_log_mutex);
            g_ui_logs.clear();
        }

        ImGui::SameLine();
        ImGui::Checkbox("Auto-scroll", &m_autoScroll);

        ImGui::Separator();
        
        ImGui::BeginChild("LogScrollRegion", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);
        {
            std::lock_guard<std::mutex> lock(g_log_mutex);
            for (const auto& log : g_ui_logs) {
                ImVec4 color = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
                switch (log.level) {
                    case ANAF_LOG_INFO:    color = ImVec4(0.4f, 0.7f, 1.0f, 1.0f); break;
                    case ANAF_LOG_WARN:    color = ImVec4(1.0f, 0.8f, 0.2f, 1.0f); break;
                    case ANAF_LOG_ERROR:   color = ImVec4(1.0f, 0.3f, 0.3f, 1.0f); break;
                    case ANAF_LOG_SUCCESS: color = ImVec4(0.3f, 1.0f, 0.3f, 1.0f); break;
                    case ANAF_LOG_CORE:    color = ImVec4(0.0f, 0.9f, 0.9f, 1.0f); break;
                }
                ImGui::PushStyleColor(ImGuiCol_Text, color);
                ImGui::TextUnformatted(log.text.c_str());
                ImGui::PopStyleColor();
            }
        }

        if (m_autoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) {
            ImGui::SetScrollHereY(1.0f);
        }

        ImGui::EndChild();
        ImGui::End();
    }
    
};



