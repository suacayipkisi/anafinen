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

#include <platform/gpuSelection.hpp>

#include <filesystem>
#include <string>

// Per-user GUI settings (theme, UI font size, title bar, GPU, Welcome panel, viewport background, result colors and legends).
//
//   assets/settings/userSettings.json        defaults, shipped with the build and the packages
//   <user config>/userSettings.json           the user's values (~/.config/anafinen, %APPDATA%\anafinen)
//
// load: compiled defaults <- assets file <- user file (each key that is present and valid wins).
// save: the user file only, written in full; keys this version does not know are kept.
namespace anaf::GUI::SETTINGS {

  // One result legend in the viewport. Without a custom position it is stacked in the
  // top-left corner; a dragged legend keeps its header's top-left corner as a fraction of
  // the viewport size, so it stays in place when the viewport is resized.
  struct LegendSettings {
    bool collapsed{false};
    bool placed{false};
    float x{0.0f};
    float y{0.0f};
  };

  // UI font size in pixels (style.FontSizeBase); values outside the range are clamped on load.
  inline constexpr float minUiFontSize = 12.0f;
  inline constexpr float maxUiFontSize = 22.0f;

  // Number of discrete color bands of the result colors and legends (banded mode).
  inline constexpr int minColorBands = 4;
  inline constexpr int maxColorBands = 16;

  struct UserSettings {
    std::string theme{"professional"}; // THEME::ThemePalette::key
    float uiFontSize{15.0f};
    // Title bar: integrated into the menu bar (default; window buttons drawn by the app, menus in the
    // title bar row) or the window system's own title bar above the menu bar.
    bool integratedTitleBar{true};
    // GPU on hybrid-graphics machines; read once before the OpenGL context exists (next start).
    PLATFORM::E_GpuPreference gpuPreference{PLATFORM::E_GpuPreference::HighPerformance};
    bool showWelcomeOnStartup{true};
    bool gradientBackground{false}; // viewport background: flat (default) or a vertical gradient from the theme
    bool bandedResults{true};       // result colors and legends in discrete bands (else a continuous scale)
    int colorBands{9};
    LegendSettings stressLegend;
    LegendSettings displacementLegend;
  };

  // Loaded on first use.
  UserSettings& settings();

  // Writes settings() to the user file; logs a warning when that fails.
  void save();

  std::filesystem::path userSettingsPath();

} // namespace anaf::GUI::SETTINGS end
