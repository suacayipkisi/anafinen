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

#include "imgui.h"

#include <array>
#include <cstdint>
#include <string_view>

// UI themes. One palette holds every color the GUI uses outside the scene's semantic colors
// (supports, loads, selection, result colormap): ImGui surfaces, the accent, status colors,
// the viewport background and grid, and the viewport overlays (gizmo, legends). Panels read
// the active palette through theme() every frame, so a theme switch needs no restart.
namespace anaf::GUI::THEME {

  enum class E_ThemeId : std::uint8_t {
    SteelCyan,      // dark blue-steel surfaces, cyan accent
    GraphiteOrange, // neutral graphite surfaces, orange accent
    ClassicFem,     // dark panels, blue accent, light-to-navy viewport gradient (classic FEM pre/post look)
    MidnightViolet, // deep navy surfaces, violet accent
    EmeraldSlate,   // slate surfaces, emerald accent
    StudioLight,    // light grey surfaces, blue accent, light viewport
  };

  inline constexpr std::array<E_ThemeId, 6> allThemes{E_ThemeId::SteelCyan,      E_ThemeId::GraphiteOrange, E_ThemeId::ClassicFem,
                                                     E_ThemeId::MidnightViolet, E_ThemeId::EmeraldSlate,   E_ThemeId::StudioLight};

  struct ThemePalette {
    const char* key{""};  // stable id in userSettings.json
    const char* name{""}; // shown in Settings > Theme
    bool light{false}; // ImGui's light defaults under the palette instead of the dark ones

    // Surfaces (in the dark themes from darkest to lightest).
    ImVec4 base;    // menu bar, title bars, inactive tabs
    ImVec4 panel;   // window background
    ImVec4 raised;  // child windows, popups, cards
    ImVec4 input;   // frames (inputs, combos), buttons
    ImVec4 header;  // tree nodes, selectables, collapsing headers
    ImVec4 border;
    ImVec4 text;
    ImVec4 textDim;

    // Accent: bright for lines and marks, primary for filled buttons (white text stays readable).
    ImVec4 accent;
    ImVec4 primary;
    ImVec4 primaryHovered;
    ImVec4 primaryActive;

    // Status colors (results, log levels, worker state).
    ImVec4 good;
    ImVec4 warn;
    ImVec4 bad;
    ImVec4 info;
    ImVec4 core;
    ImVec4 note;

    // Viewport scene: vertical background gradient, ground grid, node labels, unsolved members.
    ImVec4 sceneTop;
    ImVec4 sceneBottom;
    ImVec4 grid;
    ImVec4 sceneLabel;
    ImVec4 member;
    ImVec4 memberNoResult; // member without the requested result (e.g. stress on an unsolved model)

    // Viewport overlays: cards behind the gizmo, legends and counters.
    ImVec4 overlayBg;
    ImVec4 overlayBorder;
    ImVec4 overlayText;
    ImVec4 overlayTextDim;
  };

  // Active palette.
  const ThemePalette& theme();
  E_ThemeId currentTheme();
  const ThemePalette& palette(E_ThemeId id);
  // Theme with this key; SteelCyan for an unknown key.
  E_ThemeId themeFromKey(std::string_view key);

  // Sets the palette and rewrites the ImGui style colors. ImPlot follows ImGui's colors on its own.
  void applyTheme(E_ThemeId id);

  // Bumped by every applyTheme(), so the viewport can rebuild buffers that bake palette colors in.
  std::uint32_t themeRevision();

  inline ImU32 toU32(const ImVec4& color) { return ImGui::ColorConvertFloat4ToU32(color); }
  inline ImU32 toU32(const ImVec4& color, const float alpha) {
    return ImGui::ColorConvertFloat4ToU32(ImVec4(color.x, color.y, color.z, alpha));
  }

} // namespace anaf::GUI::THEME end
