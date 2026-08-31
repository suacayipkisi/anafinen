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

#include "../../log/anaf_info.hpp"
#include "../guiMaterials/iPanel.hpp"

#include <cstddef>
#include <mutex>
#include <string>
#include <vector>

namespace anaf::GUI {

    struct LogEntry {
        anaf::LOG::Level level;
        std::string text;
    };

    inline std::vector<LogEntry> g_ui_logs;
    inline size_t g_ui_log_max_num{10000};
    inline std::mutex g_log_mutex;

    inline void anafUILogSink(anaf::LOG::Level level, const char* message) {
        std::lock_guard<std::mutex> lock(g_log_mutex);
        g_ui_logs.push_back({level, std::string(message)});
    }

    class LogTerminal : public IPanel {
    private:
        bool m_autoScroll {true};
    public:
        LogTerminal();

        void onImGuiRender() override;
        
    };

} //namespace anaf::GUI end
