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

    void write(Level level, std::string_view formattedMessage) {
        std::string_view tag;
        std::string_view tagColor;

        switch (level) {
            case Level::INFO:
                tag = "[INFO]";
                tagColor = COLOR_BLUE;
                break;
            case Level::WARN:
                tag = "[WARN]";
                tagColor = COLOR_YELLOW;
                break;
            case Level::ERR:
                tag = "[ERROR]";
                tagColor = COLOR_RED;
                break;
            case Level::SUCCESS:
                tag = "[SUCCESS]";
                tagColor = COLOR_GREEN;
                break;
            case Level::CORE:
                tag = "[ANAFINEN]";
                tagColor = COLOR_CYAN;
                break;
        }

        const auto raw_time = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
        std::tm tm_buf{};
        portableLocalTime(&raw_time, &tm_buf);

        const std::string timeStr = std::format("{:02d}:{:02d}:{:02d}", tm_buf.tm_hour, tm_buf.tm_min, tm_buf.tm_sec);
        auto& ctx = getContext();
        std::lock_guard<std::mutex> lock(ctx.mtx);

        // Terminal output
        std::cout << std::format("[{}] {}{}{}{}\n{}\n", timeStr, COLOR_BOLD, tagColor, tag, COLOR_RESET, formattedMessage);

        // File output
        if (ctx.logFile.is_open()) {
            ctx.logFile << std::format("[{}] {}\n{}\n", timeStr, tag, formattedMessage);
            ctx.logFile.flush();
        }

        // UI callback dispatch
        if (ctx.callback) {
            const std::string uiFormatted = std::format("[{}] {}\n{}", timeStr, tag, formattedMessage);
            ctx.callback(level, uiFormatted);
        }
    }

} // namespace anaf::LOG::detail
