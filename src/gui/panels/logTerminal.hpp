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
#include <deque>
#include <mutex>
#include <string>
#include <string_view>

namespace anaf::GUI {

  struct LogEntry {
    anaf::LOG::E_Level level;
    std::string text;
  };

  inline std::deque<LogEntry> g_uiLogs; // deque: dropping the oldest line at the limit is O(1)
  inline std::uint32_t g_uiLogMaxNum{10000}; // edited as ImGuiDataType_U32 in the console
  inline std::mutex g_logMutex;

  // anaf::LOG callback, installed in main() before the GUI exists so startup lines are kept.
  inline void anafUILogSink(anaf::LOG::E_Level level, std::string_view message) {
    std::lock_guard<std::mutex> lock(g_logMutex);
    g_uiLogs.push_back({level, std::string(message)});
    if (g_uiLogs.size() > g_uiLogMaxNum) {
      g_uiLogs.pop_front();
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
