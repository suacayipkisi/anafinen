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

#include <format>
#include <fstream>
#include <functional>
#include <iostream>
#include <mutex>
#include <string>
#include <string_view>

namespace anaf::LOG {

    enum class Level {
        INFO,
        WARN,
        ERR,
        SUCCESS,
        CORE
    };

    inline constexpr std::string_view COLOR_RESET  = "\033[0m";
    inline constexpr std::string_view COLOR_BOLD   = "\033[1m";
    inline constexpr std::string_view COLOR_RED    = "\033[31m";
    inline constexpr std::string_view COLOR_GREEN  = "\033[32m";
    inline constexpr std::string_view COLOR_YELLOW = "\033[33m";
    inline constexpr std::string_view COLOR_BLUE   = "\033[34m";
    inline constexpr std::string_view COLOR_CYAN   = "\033[36m";

    struct LoggerContext {
        std::mutex mtx;
        std::ofstream logFile;
        std::function<void(Level, std::string_view)> callback = nullptr;
    };

    inline LoggerContext& getContext() noexcept {
        static LoggerContext instance;
        return instance;
    }

    void write(Level level, std::string_view formattedMessage);

    inline void setCallback(std::function<void(Level, std::string_view)> cb) {
        std::lock_guard<std::mutex> lock(getContext().mtx);
        getContext().callback = std::move(cb);
    }

    inline bool init(const std::string& filepath) {
        auto& ctx = getContext();
        std::lock_guard<std::mutex> lock(ctx.mtx);
        ctx.logFile.open(filepath, std::ios::out | std::ios::trunc);
        return ctx.logFile.is_open();
    }

    inline void close() {
        auto& ctx = getContext();
        std::lock_guard<std::mutex> lock(ctx.mtx);
        if (ctx.logFile.is_open()) {
            ctx.logFile.close();
        }
    }

    template <typename... Args>
    void info(std::format_string<Args...> fmt, Args&&... args) {
        write(Level::INFO, std::format(fmt, std::forward<Args>(args)...));
    }

    template <typename... Args>
    void warn(std::format_string<Args...> fmt, Args&&... args) {
        write(Level::WARN, std::format(fmt, std::forward<Args>(args)...));
    }

    template <typename... Args>
    void error(std::format_string<Args...> fmt, Args&&... args) {
        write(Level::ERR, std::format(fmt, std::forward<Args>(args)...));
    }

    template <typename... Args>
    void success(std::format_string<Args...> fmt, Args&&... args) {
        write(Level::SUCCESS, std::format(fmt, std::forward<Args>(args)...));
    }

    template <typename... Args>
    void core(std::format_string<Args...> fmt, Args&&... args) {
        write(Level::CORE, std::format(fmt, std::forward<Args>(args)...));
    }

} // namespace anaf::LOG end
