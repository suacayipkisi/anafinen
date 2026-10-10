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

#include <format>
#include <fstream>
#include <functional>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>

namespace anaf::LOG {

  std::string getLogFileLoc();

  enum class E_Level {
    INFO,
    WARN,
    ERR,
    SUCCESS,
    CORE
  };

  inline constexpr std::string_view colorReset  = "\033[0m";
  inline constexpr std::string_view colorBold   = "\033[1m";
  inline constexpr std::string_view colorRed    = "\033[31m";
  inline constexpr std::string_view colorGreen  = "\033[32m";
  inline constexpr std::string_view colorYellow = "\033[33m";
  inline constexpr std::string_view colorBlue   = "\033[34m";
  inline constexpr std::string_view colorCyan   = "\033[36m";

  // Sinks: the log file (init()), the callback (GUI console) and colored stdout (a CLI). The
  // log lives in anaf_core, so the front end picks its sinks at run time, not by macro.
  struct LoggerContext {
    std::mutex mtx;
    std::ofstream logFile;
    std::function<void(E_Level, std::string_view)> callback = nullptr;
    bool consoleOutput{false};
  };

  inline LoggerContext& getContext() noexcept {
    static LoggerContext s_instance;
    return s_instance;
  }

  void write(E_Level level, std::string_view formattedMessage);

  inline void setCallback(std::function<void(E_Level, std::string_view)> cb) {
    std::lock_guard<std::mutex> lock(getContext().mtx);
    getContext().callback = std::move(cb);
  }

  inline void setConsoleOutput(const bool enabled) {
    std::lock_guard<std::mutex> lock(getContext().mtx);
    getContext().consoleOutput = enabled;
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
    write(E_Level::INFO, std::format(fmt, std::forward<Args>(args)...));
  }

  template <typename... Args>
  void warn(std::format_string<Args...> fmt, Args&&... args) {
    write(E_Level::WARN, std::format(fmt, std::forward<Args>(args)...));
  }

  template <typename... Args>
  void error(std::format_string<Args...> fmt, Args&&... args) {
    write(E_Level::ERR, std::format(fmt, std::forward<Args>(args)...));
  }

  template <typename... Args>
  void success(std::format_string<Args...> fmt, Args&&... args) {
    write(E_Level::SUCCESS, std::format(fmt, std::forward<Args>(args)...));
  }

  template <typename... Args>
  void core(std::format_string<Args...> fmt, Args&&... args) {
    write(E_Level::CORE, std::format(fmt, std::forward<Args>(args)...));
  }

} // namespace anaf::LOG end
