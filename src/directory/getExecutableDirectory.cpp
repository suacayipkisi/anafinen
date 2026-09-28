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

#include "getExecutableDirectory.hpp"

#include <io/core/pathUtf8.hpp>

#include <cstdlib>
#include <filesystem>

#ifdef __linux__
#include <unistd.h>
#include <linux/limits.h>
#endif

#ifdef _WIN32
#include <windows.h>
#include <shlobj.h> // SHGetKnownFolderPath (shell32, ole32, uuid)
#include <string>
#endif

namespace anaf::DIRECTORY {

  std::filesystem::path getExecutableDirectory(){
#ifdef __linux__
      char result[PATH_MAX];
      const ssize_t count = readlink("/proc/self/exe", result, PATH_MAX);
      if (count != -1) {
        return std::filesystem::path(std::string(result, count)).parent_path();
      }
#elif defined(_WIN32)
      // Wide API: the ANSI variant mangles folders such as "C:\Users\Şule" to '?'.
      std::wstring result(MAX_PATH, L'\0');
      for (;;) {
        const DWORD count = GetModuleFileNameW(nullptr, result.data(), static_cast<DWORD>(result.size()));
        if (count == 0) break;
        if (count < result.size()) {
          result.resize(count);
          return std::filesystem::path(result).parent_path();
        }
        result.resize(result.size() * 2); // truncated (long path), retry
      }
#endif
      return std::filesystem::current_path();
  }

  std::filesystem::path findAssetPath(const std::filesystem::path& subpath) {
    const std::filesystem::path candidates[] = {
      getExecutableDirectory() / "assets" / subpath,
#ifndef _WIN32
      std::filesystem::path("/usr/share/anafinen/assets") / subpath,
#endif
      std::filesystem::path("assets") / subpath,
#ifdef MAIN_DIR
      anaf::IO::pathFromUtf8(MAIN_DIR) / "assets" / subpath,
#endif
    };
    for (const auto& candidate : candidates) {
      std::error_code ec;
      if (std::filesystem::exists(candidate, ec)) return candidate;
    }
    return {};
  }

  std::filesystem::path getUserConfigDirectory() {
#ifdef _WIN32
    PWSTR roaming = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, KF_FLAG_DEFAULT, nullptr, &roaming))) {
      std::filesystem::path base(roaming);
      CoTaskMemFree(roaming);
      return base / "anafinen";
    }
    CoTaskMemFree(roaming); // documented: free even on failure
    const wchar_t* appData = _wgetenv(L"APPDATA");
    return (appData && *appData) ? std::filesystem::path(appData) / "anafinen" : std::filesystem::path{};
#else
    const auto fromEnv = [](const char* name) -> std::filesystem::path {
      const char* value = std::getenv(name);
      return (value && *value) ? std::filesystem::path(value) : std::filesystem::path{};
    };
    if (const auto xdg = fromEnv("XDG_CONFIG_HOME"); xdg.is_absolute()) return xdg / "anafinen";
    const auto home = fromEnv("HOME");
    return home.empty() ? home : home / ".config" / "anafinen";
#endif
  }

} // namespace anaf::DIRECTORY end



