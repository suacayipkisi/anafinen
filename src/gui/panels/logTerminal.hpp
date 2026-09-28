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

#pragma once

#include <log/anaf_info.hpp>
#include <guiMaterials/iPanel.hpp>
#include "statusBar.hpp"

#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace anaf::GUI {

  struct LogEntry {
    anaf::LOG::Level level;
    std::string text;
  };

  inline std::vector<LogEntry> g_ui_logs;
  inline std::uint32_t g_ui_log_max_num{10000}; // edited as ImGuiDataType_U32 in the console
  inline std::mutex g_log_mutex;

  // anaf::LOG callback, installed in main() before the GUI exists so startup lines are kept.
  inline void anafUILogSink(anaf::LOG::Level level, std::string_view message) {
    std::lock_guard<std::mutex> lock(g_log_mutex);
    g_ui_logs.push_back({level, std::string(message)});
    if (g_ui_logs.size() > g_ui_log_max_num) {
      g_ui_logs.erase(g_ui_logs.begin());
    }
  }

  class LogTerminal : public IPanel {
  private:
    bool m_autoScroll {true};
    bool m_wrapLines {true}; // wrap at the panel width; off: one line per entry + horizontal scrollbar
    StatusBar m_statusBar;    // footer: worker state, hardware, resource usage
  public:
    void onImGuiRender() override;

  };

} //namespace anaf::GUI end
