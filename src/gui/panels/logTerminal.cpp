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

#include "../../log/anaf_info.hpp"
#include "../guiMaterials/imGuiLayer.hpp"

#include "imgui.h"
#include <mutex>
#include <string>
#include <vector>

namespace anaf::GUI {

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

        ImGui::SameLine();

        const float input_width = 80.0f;
        const char* label_text = "Max Output";
        const float text_width = ImGui::CalcTextSize(label_text).x;
        const float style_spacing = ImGui::GetStyle().ItemSpacing.x;
        const float total_width = text_width + style_spacing + input_width;

        float right_cursor_x = ImGui::GetWindowContentRegionMax().x - total_width;

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
            for (const auto& log : g_ui_logs) {
                ImVec4 color = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
                switch (log.level) {
                    case anaf::LOG::Level::INFO: color = ImVec4(0.4f, 0.7f, 1.0f, 1.0f); break;
                    case anaf::LOG::Level::WARN: color = ImVec4(1.0f, 0.8f, 0.2f, 1.0f); break;
                    case anaf::LOG::Level::ERROR: color = ImVec4(1.0f, 0.3f, 0.3f, 1.0f); break;
                    case anaf::LOG::Level::SUCCESS: color = ImVec4(0.3f, 1.0f, 0.3f, 1.0f); break;
                    case anaf::LOG::Level::CORE: color = ImVec4(0.0f, 0.9f, 0.9f, 1.0f); break;
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

        ImGui::PopFont();
    }

} // namespace anaf::GUI end
