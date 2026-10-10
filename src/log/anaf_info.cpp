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

#include "anaf_info.hpp"

#include <chrono>
#include <ctime>
#include <iostream>
#include <mutex>
#include <string>

namespace {
  inline void portableLocalTime(const std::time_t* timer, std::tm* buf) {
#if defined(_WIN32) || defined(_MSC_VER)
    localtime_s(buf, timer);
#else
    localtime_r(timer, buf);
#endif
  }
} // namespace

namespace anaf::LOG {

  std::string getLogFileLoc() {
    return "anafinen_run.log";
  }

  void write(E_Level level, std::string_view formattedMessage) {
    std::string_view tag;
    std::string_view tagColor;

    switch (level) {
      case E_Level::INFO:
        tag = "[INFO]";
        tagColor = colorBlue;
        break;
      case E_Level::WARN:
        tag = "[WARN]";
        tagColor = colorYellow;
        break;
      case E_Level::ERR:
        tag = "[ERROR]";
        tagColor = colorRed;
        break;
      case E_Level::SUCCESS:
        tag = "[SUCCESS]";
        tagColor = colorGreen;
        break;
      case E_Level::CORE:
        tag = "[ANAFINEN]";
        tagColor = colorCyan;
        break;
    }

    const auto rawTime = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm tmBuf{};
    portableLocalTime(&rawTime, &tmBuf);

    const std::string timeStr = std::format("{:02d}:{:02d}:{:02d}", tmBuf.tm_hour, tmBuf.tm_min, tmBuf.tm_sec);
    auto& ctx = getContext();
    std::lock_guard<std::mutex> lock(ctx.mtx);

    if (ctx.consoleOutput) {
      std::cout << std::format("[{}] {}{}{}{} {}\n", timeStr, colorBold, tagColor, tag, colorReset, formattedMessage);
    }

    // File output
    if (ctx.logFile.is_open()) {
      ctx.logFile << std::format("[{}] {} {}\n", timeStr, tag, formattedMessage);
      ctx.logFile.flush();
    }

    if (ctx.callback) {
      ctx.callback(level, std::format("[{}] {} {}", timeStr, tag, formattedMessage));
    }
  }

} // namespace anaf::LOG end
