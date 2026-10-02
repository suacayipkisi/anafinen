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

#ifdef __linux__

#include <array>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <string_view>

// GLFW does not read the desktop's cursor settings, so without this the window shows the
// default cursor theme instead of the one the user picked. The choice is looked up once at
// startup and handed to the cursor library through XCURSOR_THEME / XCURSOR_SIZE.

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

  inline bool environmentHas(const char* name) {
    const char* value = std::getenv(name);
    return value != nullptr && *value != '\0';
  }

  // GNOME / GTK setting, e.g. "cursor-theme".
  inline std::string gnomeCursorSetting(const std::string& key) {
    return execCommand(("gsettings get org.gnome.desktop.interface " + key + " 2>/dev/null").c_str());
  }

  // KDE Plasma setting from kcminputrc, e.g. "cursorTheme" (Plasma 6, then 5).
  inline std::string kdeCursorSetting(const std::string& key) {
    std::string value = execCommand(("kreadconfig6 --file kcminputrc --group Mouse --key " + key + " 2>/dev/null").c_str());
    if (value.empty()) value = execCommand(("kreadconfig5 --file kcminputrc --group Mouse --key " + key + " 2>/dev/null").c_str());
    return value;
  }

  // X resources (X11 window managers), e.g. "Xcursor.theme".
  inline std::string xresourcesCursorSetting(const std::string& key) {
    return execCommand(("xrdb -query 2>/dev/null | grep -i '" + key + "' | cut -f2").c_str());
  }

  inline void setupSystemCursor() {
    // Values already in the environment are the user's (or the session's) explicit choice:
    // keep them, and skip the lookups entirely when both are set.
    const bool haveTheme = environmentHas("XCURSOR_THEME");
    const bool haveSize = environmentHas("XCURSOR_SIZE");
    if (haveTheme && haveSize) return;

    // On Plasma, gsettings still answers when the GNOME schemas are installed, but with GNOME's
    // default ("Adwaita"), not the user's choice; so the desktop decides which source goes first.
    const char* desktop = std::getenv("XDG_CURRENT_DESKTOP");
    const bool plasma = desktop != nullptr && std::string_view(desktop).find("KDE") != std::string_view::npos;
    const auto lookUp = [plasma](const std::string& gnomeKey, const std::string& kdeKey, const std::string& xKey) {
      std::string value = plasma ? kdeCursorSetting(kdeKey) : gnomeCursorSetting(gnomeKey);
      if (value.empty()) value = plasma ? gnomeCursorSetting(gnomeKey) : kdeCursorSetting(kdeKey);
      if (value.empty()) value = xresourcesCursorSetting(xKey);
      return value;
    };

    if (!haveTheme) {
      const std::string theme = lookUp("cursor-theme", "cursorTheme", "Xcursor.theme");
      setenv("XCURSOR_THEME", theme.empty() ? "Adwaita" : theme.c_str(), 1);
    }
    if (!haveSize) {
      const std::string size = lookUp("cursor-size", "cursorSize", "Xcursor.size");
      setenv("XCURSOR_SIZE", size.empty() ? "24" : size.c_str(), 1);
    }
  }

} // namespace platform_utils

#else

namespace platform_utils {

  inline void setupSystemCursor() {}

} // namespace platform_utils

#endif // __linux__
