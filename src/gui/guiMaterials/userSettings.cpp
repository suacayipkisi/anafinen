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

#include "userSettings.hpp"

#include <directory/getExecutableDirectory.hpp>
#include <io/core/pathUtf8.hpp>
#include <log/anaf_info.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>
#include <string_view>
#include <system_error>
#include <type_traits>

namespace anaf::GUI::SETTINGS {

  namespace {
    constexpr const char* fileName = "userSettings.json";
    constexpr int formatVersion = 1;
    // Settings file before 2026-10-09: "key=value" lines, only the Welcome panel's flag.
    constexpr const char* legacyFileName = "guiSettings.ini";

    // The whole user document, so keys of other (newer) versions survive a save.
    nlohmann::json g_document = nlohmann::json::object();

    // Reads a JSON file; an empty object when it is missing or invalid.
    nlohmann::json readJson(const std::filesystem::path& path) {
      std::error_code ec;
      if (path.empty() || !std::filesystem::is_regular_file(path, ec)) return nlohmann::json::object();
      std::ifstream file(path);
      nlohmann::json document = nlohmann::json::parse(file, nullptr, false);
      if (document.is_discarded() || !document.is_object()) {
        anaf::LOG::warn("Settings: '{}' is not a valid JSON object, ignored", anaf::IO::pathToUtf8(path));
        return nlohmann::json::object();
      }
      return document;
    }

    template <typename T>
    void readValue(const nlohmann::json& object, const char* key, T& value) {
      const auto it = object.find(key);
      if (it == object.end()) return;
      if constexpr (std::is_same_v<T, bool>) {
        if (it->is_boolean()) value = it->template get<bool>();
      } else if constexpr (std::is_same_v<T, float>) {
        if (it->is_number()) value = std::clamp(it->template get<float>(), 0.0f, 1.0f);
      } else {
        if (it->is_string()) value = it->template get<std::string>();
      }
    }

    void readLegend(const nlohmann::json& legends, const char* key, LegendSettings& legend) {
      const auto it = legends.find(key);
      if (it == legends.end() || !it->is_object()) return;
      readValue(*it, "collapsed", legend.collapsed);
      readValue(*it, "placed", legend.placed);
      readValue(*it, "x", legend.x);
      readValue(*it, "y", legend.y);
    }

    // Applies every valid key of document on top of settings.
    void apply(const nlohmann::json& document, UserSettings& settings) {
      readValue(document, "theme", settings.theme);
      if (const auto welcome = document.find("welcome"); welcome != document.end() && welcome->is_object()) {
        readValue(*welcome, "showOnStartup", settings.showWelcomeOnStartup);
      }
      if (const auto viewport = document.find("viewport"); viewport != document.end() && viewport->is_object()) {
        if (const auto legends = viewport->find("legends"); legends != viewport->end() && legends->is_object()) {
          readLegend(*legends, "stress", settings.stressLegend);
          readLegend(*legends, "displacement", settings.displacementLegend);
        }
      }
    }

    // object[key] as an object, replacing a value of another type (a hand-edited file).
    nlohmann::json& objectAt(nlohmann::json& object, const char* key) {
      nlohmann::json& child = object[key];
      if (!child.is_object()) child = nlohmann::json::object();
      return child;
    }

    nlohmann::json legendJson(const LegendSettings& legend) {
      return {{"collapsed", legend.collapsed}, {"placed", legend.placed}, {"x", legend.x}, {"y", legend.y}};
    }

    // Older installs kept "showWelcomeOnStartup=0|1" in guiSettings.ini.
    void readLegacyFile(UserSettings& settings) {
      const auto dir = anaf::DIRECTORY::getUserConfigDirectory();
      if (dir.empty()) return;
      std::ifstream file(dir / legacyFileName);
      std::string line;
      while (std::getline(file, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        constexpr std::string_view key = "showWelcomeOnStartup=";
        if (line.starts_with(key)) settings.showWelcomeOnStartup = line.substr(key.size()) != "0";
      }
    }

    UserSettings load() {
      UserSettings settings;
      apply(readJson(anaf::DIRECTORY::findAssetPath(std::filesystem::path("settings") / fileName)), settings);
      const auto userPath = userSettingsPath();
      std::error_code ec;
      if (!userPath.empty() && std::filesystem::is_regular_file(userPath, ec)) {
        g_document = readJson(userPath);
        apply(g_document, settings);
      } else {
        readLegacyFile(settings);
      }
      return settings;
    }
  } // namespace end

  std::filesystem::path userSettingsPath() {
    const auto dir = anaf::DIRECTORY::getUserConfigDirectory();
    return dir.empty() ? dir : dir / fileName;
  }

  UserSettings& settings() {
    static UserSettings s_instance = load();
    return s_instance;
  }

  void save() {
    const auto path = userSettingsPath();
    if (path.empty()) return;
    const UserSettings& current = settings();

    g_document["version"] = formatVersion;
    g_document["theme"] = current.theme;
    objectAt(g_document, "welcome")["showOnStartup"] = current.showWelcomeOnStartup;
    nlohmann::json& legends = objectAt(objectAt(g_document, "viewport"), "legends");
    legends["stress"] = legendJson(current.stressLegend);
    legends["displacement"] = legendJson(current.displacementLegend);

    // Written next to the target and renamed over it, so a crash never leaves half a file.
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    std::filesystem::path temporary = path;
    temporary += ".tmp";
    {
      std::ofstream file(temporary, std::ios::trunc);
      if (!file || !(file << g_document.dump(2) << '\n')) {
        anaf::LOG::warn("Settings: could not write '{}'", anaf::IO::pathToUtf8(temporary));
        return;
      }
    }
    std::filesystem::rename(temporary, path, ec);
    if (ec) {
      anaf::LOG::warn("Settings: could not replace '{}': {}", anaf::IO::pathToUtf8(path), ec.message());
      std::filesystem::remove(temporary, ec);
    }
  }

} // namespace anaf::GUI::SETTINGS end
