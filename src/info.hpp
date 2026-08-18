#pragma once

#include <iostream>
#include <string_view>
#include <format>

namespace anafinen::logger {

namespace color {
    constexpr std::string_view reset   = "\033[0m";
    constexpr std::string_view bold    = "\033[1m";
    constexpr std::string_view red     = "\033[31m";
    constexpr std::string_view green   = "\033[32m";
    constexpr std::string_view yellow  = "\033[33m";
    constexpr std::string_view blue    = "\033[34m";
    constexpr std::string_view cyan    = "\033[36m";
} // namespace color

enum class Level {
    Info,
    Warn,
    Error,
    Success,
    Core
};

template <typename... Args>
inline void log(Level level, std::format_string<Args...> fmt, Args&&... args) {
    std::string_view tag;
    std::string_view tag_color;

    switch (level) {
        case Level::Info:
            tag = "[INFO]";
            tag_color = color::blue;
            break;
        case Level::Warn:
            tag = "[WARN]";
            tag_color = color::yellow;
            break;
        case Level::Error:
            tag = "[ERROR]";
            tag_color = color::red;
            break;
        case Level::Success:
            tag = "[OK]";
            tag_color = color::green;
            break;
        case Level::Core:
            tag = "[ANAFINEN]";
            tag_color = color::cyan;
            break;
    }

    auto formatted_msg = std::format(fmt, std::forward<Args>(args)...);
    
    if (level == Level::Error) {
        std::cerr << color::bold << tag_color << tag << color::reset << " " 
                  << formatted_msg << "\n";
    } else {
        std::cout << color::bold << tag_color << tag << color::reset << " " 
                  << formatted_msg << "\n";
    }
}

template <typename... Args>
inline void info(std::format_string<Args...> fmt, Args&&... args) {
    log(Level::Info, fmt, std::forward<Args>(args)...);
}

template <typename... Args>
inline void warn(std::format_string<Args...> fmt, Args&&... args) {
    log(Level::Warn, fmt, std::forward<Args>(args)...);
}

template <typename... Args>
inline void error(std::format_string<Args...> fmt, Args&&... args) {
    log(Level::Error, fmt, std::forward<Args>(args)...);
}

template <typename... Args>
inline void success(std::format_string<Args...> fmt, Args&&... args) {
    log(Level::Success, fmt, std::forward<Args>(args)...);
}

template <typename... Args>
inline void core(std::format_string<Args...> fmt, Args&&... args) {
    log(Level::Core, fmt, std::forward<Args>(args)...);
}

} // namespace anafinen::logger
