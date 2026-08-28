#pragma once

#ifdef __linux__
#include <cstdlib>
#include <array>
#include <cstdio>
#include <memory>
#include <string>

namespace platform_utils {

struct PipeCloser {
    void operator()(FILE* pipe) const noexcept {
        if (pipe != nullptr) {
            pclose(pipe);
        }
    }
};

inline std::string execCommand(const char* cmd) {
    std::array<char, 128> buffer;
    std::string result;
    std::unique_ptr<FILE, PipeCloser> pipe(popen(cmd, "r"));
    if (pipe && fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
        result = buffer.data();
        std::erase(result, '\'');
        std::erase(result, '\"');
        std::erase(result, '\n');
    }
    return result;
}

inline void setupSystemCursor() {
    // try GNOME / GTK settings
    std::string theme = execCommand("gsettings get org.gnome.desktop.interface cursor-theme 2>/dev/null");
    std::string size  = execCommand("gsettings get org.gnome.desktop.interface cursor-size 2>/dev/null");

    // fallback to KDE Plasma if GNOME query failed
    if (theme.empty()) {
        theme = execCommand("kreadconfig6 --group Mouse --key cursorTheme 2>/dev/null");
        if (theme.empty()) {
            theme = execCommand("kreadconfig5 --group Mouse --key cursorTheme 2>/dev/null");
        }
    }
    if (size.empty()) {
        size = execCommand("kreadconfig6 --group Mouse --key cursorSize 2>/dev/null");
        if (size.empty()) {
            size = execCommand("kreadconfig5 --group Mouse --key cursorSize 2>/dev/null");
        }
    }

    // fallback to Xresources (X11 / WMs)
    if (theme.empty()) {
        theme = execCommand("xrdb -query 2>/dev/null | grep -i 'Xcursor.theme' | cut -f2");
    }
    if (size.empty()) {
        size = execCommand("xrdb -query 2>/dev/null | grep -i 'Xcursor.size' | cut -f2");
    }

    // default fallbacks if everything else fails
    if (theme.empty()) theme = "Adwaita";
    if (size.empty())  size  = "24";

    setenv("XCURSOR_THEME", theme.c_str(), 1);
    setenv("XCURSOR_SIZE", size.c_str(), 1);
}

} // namespace platform_utils

#else

namespace platform_utils {
inline void setupSystemCursor() {}
} // namespace platform_utils

#endif // __linux__
