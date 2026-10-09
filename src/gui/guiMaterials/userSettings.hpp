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

#include <filesystem>
#include <string>

// Per-user GUI settings (theme, Welcome panel, viewport legends).
//
//   assets/settings/userSettings.json        defaults, shipped with the build and the packages
//   <user config>/userSettings.json           the user's values (~/.config/anafinen, %APPDATA%\anafinen)
//
// load: compiled defaults <- assets file <- user file (each key that is present and valid wins).
// save: the user file only, written in full; keys this version does not know are kept.
namespace anaf::GUI::SETTINGS {

  // One result legend in the viewport. Without a custom position it is stacked in the
  // bottom-left corner; a dragged legend keeps its header's top-left corner as a fraction of
  // the viewport size, so it stays in place when the viewport is resized.
  struct LegendSettings {
    bool collapsed{false};
    bool placed{false};
    float x{0.0f};
    float y{0.0f};
  };

  struct UserSettings {
    std::string theme{"steel_cyan"}; // THEME::ThemePalette::key
    bool showWelcomeOnStartup{true};
    LegendSettings stressLegend;
    LegendSettings displacementLegend;
  };

  // Loaded on first use.
  UserSettings& settings();

  // Writes settings() to the user file; logs a warning when that fails.
  void save();

  std::filesystem::path userSettingsPath();

} // namespace anaf::GUI::SETTINGS end
