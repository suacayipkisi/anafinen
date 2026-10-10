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

#include "theme.hpp"

namespace anaf::GUI::THEME {

  namespace {

    // 0xRRGGBB (sRGB, as written) to an ImVec4.
    constexpr ImVec4 rgb(const std::uint32_t hex, const float alpha = 1.0f) {
      return ImVec4(static_cast<float>((hex >> 16) & 0xFFu) / 255.0f, static_cast<float>((hex >> 8) & 0xFFu) / 255.0f,
                    static_cast<float>(hex & 0xFFu) / 255.0f, alpha);
    }

    constexpr ImVec4 withAlpha(const ImVec4& color, const float alpha) { return ImVec4(color.x, color.y, color.z, alpha); }

    // Every dark theme is built on the look before the themes (v0.2.0, commit 8c4242b): neutral
    // grey surfaces with few levels, colors only in the results. A theme changes the accent (marks,
    // run buttons) and the flat viewport background; status colors only where they would read as
    // the accent.
    constexpr ThemePalette darkTheme(const char* key, const char* name, const ImVec4 accent, const ImVec4 primary,
                                     const ImVec4 primaryHovered, const ImVec4 primaryActive, const ImVec4 scene) {
      return ThemePalette{
        .key = key,
        .name = name,
        .base = ImVec4(0.10f, 0.105f, 0.11f, 1.0f),
        .panel = ImVec4(0.10f, 0.105f, 0.11f, 1.0f),
        .raised = ImVec4(0.14f, 0.15f, 0.17f, 1.0f),
        .input = ImVec4(0.19f, 0.195f, 0.20f, 1.0f),
        .header = ImVec4(0.20f, 0.205f, 0.21f, 1.0f),
        .border = ImVec4(0.22f, 0.24f, 0.28f, 0.60f),
        .text = ImVec4(0.92f, 0.93f, 0.95f, 1.0f),
        .textDim = ImVec4(0.50f, 0.53f, 0.58f, 1.0f),
        .accent = accent,
        .primary = primary,
        .primaryHovered = primaryHovered,
        .primaryActive = primaryActive,
        .good = ImVec4(0.55f, 0.95f, 0.6f, 1.0f),
        .warn = ImVec4(1.0f, 0.75f, 0.35f, 1.0f),
        .bad = ImVec4(1.0f, 0.45f, 0.45f, 1.0f),
        .info = ImVec4(0.4f, 0.7f, 1.0f, 1.0f),
        .core = ImVec4(0.0f, 0.9f, 0.9f, 1.0f),
        .note = ImVec4(0.7f, 0.7f, 0.7f, 1.0f),
        .sceneTop = scene,
        .sceneBottom = scene,
        .grid = ImVec4(0.62f, 0.70f, 0.78f, 1.0f),
        .sceneLabel = ImVec4(0.9f, 0.9f, 0.9f, 1.0f),
        .member = ImVec4(0.62f, 0.70f, 0.80f, 1.0f),
        .memberNoResult = ImVec4(0.55f, 0.55f, 0.55f, 1.0f),
        .overlayBg = rgb(0x0F1116, 0.86f),
        .overlayBorder = ImVec4(0.22f, 0.24f, 0.28f, 0.60f),
        .overlayText = rgb(0xE6E6E6),
        .overlayTextDim = rgb(0x8C8E96),
      };
    }

    constexpr ThemePalette steelCyan = darkTheme("steel_cyan", "Steel Blue / Cyan", rgb(0x2EA8D6), rgb(0x1C7BA2), rgb(0x2690BB),
                                                 rgb(0x166685), ImVec4(0.12f, 0.14f, 0.17f, 1.0f));

    constexpr ThemePalette graphiteOrange = [] {
      ThemePalette p = darkTheme("graphite_orange", "Graphite / Orange", rgb(0xE8863A), rgb(0xB15E22), rgb(0xC86E2E),
                                 rgb(0x944D1B), ImVec4(0.14f, 0.14f, 0.15f, 1.0f));
      p.warn = rgb(0xF5CF5A); // yellower than the shared warn, so it does not read as the accent
      return p;
    }();

    // Grey viewport like the classic pre / post processors; the dark blue result colors stay readable on it.
    constexpr ThemePalette classicFem = darkTheme("classic_fem", "Classic FEM", rgb(0x4C95FF), rgb(0x2A68C8), rgb(0x3779DC),
                                                  rgb(0x2257A8), rgb(0x4B5058));

    constexpr ThemePalette midnightViolet = darkTheme("midnight_violet", "Midnight / Violet", rgb(0x9A86FF), rgb(0x6650D8),
                                                      rgb(0x765FEA), rgb(0x5641B8), ImVec4(0.13f, 0.13f, 0.18f, 1.0f));

    constexpr ThemePalette emeraldSlate = [] {
      ThemePalette p = darkTheme("slate_emerald", "Slate / Emerald", rgb(0x2FC48D), rgb(0x1C8A62), rgb(0x239E71),
                                 rgb(0x167351), ImVec4(0.12f, 0.15f, 0.14f, 1.0f));
      p.good = rgb(0x9BE36A); // lime, so success does not read as the accent
      return p;
    }();

    // The look before the themes: ImGui colors from applyLegacyStyleColors() (v0.2.0 with lifted
    // frames), blue truss bars; the viewport is lighter than v0.2.0's 0.08 / 0.09 / 0.11 so dark blue
    // members stay visible.
    constexpr ThemePalette oldTheme = [] {
      ThemePalette p = darkTheme("old", "Old Theme (v0.2)", ImVec4(0.35f, 0.68f, 1.0f, 1.0f), ImVec4(0.16f, 0.36f, 0.62f, 1.0f),
                                 ImVec4(0.22f, 0.45f, 0.75f, 1.0f), ImVec4(0.13f, 0.30f, 0.52f, 1.0f),
                                 ImVec4(0.13f, 0.14f, 0.16f, 1.0f));
      p.legacyStyle = true;
      p.trussMember = ImVec4(0.4f, 0.6f, 0.85f, 1.0f);
      return p;
    }();

    // Light theme: dark text, deeper status colors (readable on white), flat light viewport.
    constexpr ThemePalette studioLight{
      .key = "studio_light",
      .name = "Studio Light",
      .light = true,
      .base = rgb(0xD9DDE3),
      .panel = rgb(0xEBEDF0),
      .raised = rgb(0xF5F6F8),
      .input = rgb(0xDCE1E7),
      .header = rgb(0xD9DEE5),
      .border = rgb(0xB9C1CC),
      .text = rgb(0x1D232B),
      .textDim = rgb(0x66707D),
      .accent = rgb(0x1F6FD1),
      .primary = rgb(0x1F6FD1),
      .primaryHovered = rgb(0x2C80E6),
      .primaryActive = rgb(0x195BB0),
      .good = rgb(0x1E8A4C),
      .warn = rgb(0xA86A12),
      .bad = rgb(0xC6352A),
      .info = rgb(0x1F6FD1),
      .core = rgb(0x0B827E),
      .note = rgb(0x55606C),
      .sceneTop = rgb(0xE6E9EE),
      .sceneBottom = rgb(0xE6E9EE),
      .grid = rgb(0x4A5563),
      .sceneLabel = rgb(0x1B2129),
      .member = rgb(0x56677C),
      .memberNoResult = rgb(0x9AA3AE),
      .overlayBg = rgb(0xFFFFFF, 0.88f),
      .overlayBorder = rgb(0xAEB7C2, 0.95f),
      .overlayText = rgb(0x1D232B),
      .overlayTextDim = rgb(0x5E6875),
    };

    E_ThemeId g_current = E_ThemeId::SteelCyan;
    std::uint32_t g_revision = 0;

    // ImGuiLayer's colors before the themes, set over ImGui's dark defaults (frames lifted, see below).
    void applyLegacyStyleColors(ImGuiStyle& style) {
      ImGui::StyleColorsDark(&style);
      ImVec4* c = style.Colors;
      c[ImGuiCol_WindowBg] = ImVec4(0.10f, 0.105f, 0.11f, 1.00f);
      c[ImGuiCol_ChildBg] = ImVec4(0.14f, 0.15f, 0.17f, 1.00f);
      c[ImGuiCol_PopupBg] = ImVec4(0.13f, 0.14f, 0.16f, 0.98f);
      c[ImGuiCol_Border] = ImVec4(0.22f, 0.24f, 0.28f, 0.60f);

      c[ImGuiCol_TitleBg] = ImVec4(0.10f, 0.105f, 0.11f, 1.0f);
      c[ImGuiCol_TitleBgActive] = ImVec4(0.15f, 0.1505f, 0.151f, 1.0f);
      c[ImGuiCol_TitleBgCollapsed] = ImVec4(0.10f, 0.105f, 0.11f, 1.0f);
      c[ImGuiCol_Header] = ImVec4(0.20f, 0.205f, 0.21f, 1.0f);
      c[ImGuiCol_HeaderHovered] = ImVec4(0.30f, 0.305f, 0.31f, 1.0f);
      c[ImGuiCol_HeaderActive] = ImVec4(0.15f, 0.1505f, 0.151f, 1.0f);

      c[ImGuiCol_Button] = ImVec4(0.20f, 0.205f, 0.21f, 1.0f);
      c[ImGuiCol_ButtonHovered] = ImVec4(0.30f, 0.305f, 0.31f, 1.0f);
      c[ImGuiCol_ButtonActive] = ImVec4(0.15f, 0.1505f, 0.151f, 1.0f);

      // Frames are lighter than in v0.2.0 (0.15), which matched the child cards (0.14) of the
      // current editor layout and hid inputs and check boxes there.
      c[ImGuiCol_FrameBg] = ImVec4(0.19f, 0.195f, 0.20f, 1.0f);
      c[ImGuiCol_FrameBgHovered] = ImVec4(0.25f, 0.255f, 0.26f, 1.0f);
      c[ImGuiCol_FrameBgActive] = ImVec4(0.16f, 0.1605f, 0.161f, 1.0f);
      c[ImGuiCol_CheckMark] = ImVec4(0.35f, 0.68f, 1.00f, 1.00f);
      c[ImGuiCol_SliderGrab] = ImVec4(0.30f, 0.58f, 0.90f, 1.00f);
      c[ImGuiCol_SliderGrabActive] = ImVec4(0.40f, 0.70f, 1.00f, 1.00f);

      c[ImGuiCol_Tab] = ImVec4(0.15f, 0.1505f, 0.151f, 1.0f);
      c[ImGuiCol_TabHovered] = ImVec4(0.38f, 0.3805f, 0.381f, 1.0f);
      c[ImGuiCol_TabSelected] = ImVec4(0.28f, 0.2805f, 0.281f, 1.0f);
      c[ImGuiCol_TabDimmed] = ImVec4(0.15f, 0.1505f, 0.151f, 1.0f);
      c[ImGuiCol_TabDimmedSelected] = ImVec4(0.20f, 0.205f, 0.21f, 1.0f);

      c[ImGuiCol_Text] = ImVec4(0.92f, 0.93f, 0.95f, 1.00f);
      c[ImGuiCol_TextDisabled] = ImVec4(0.50f, 0.53f, 0.58f, 1.00f);
    }

    // Dark themes: the accent replaces ImGui's default blue on marks only (check marks, sliders, tab
    // overlines, selection, separators and grips while dragged); surfaces and hovers stay grey.
    void applyAccentMarks(ImGuiStyle& style, const ImVec4& accent) {
      ImVec4* c = style.Colors;
      c[ImGuiCol_CheckMark] = accent;
      c[ImGuiCol_SliderGrab] = mix(accent, ImVec4(0.0f, 0.0f, 0.0f, 1.0f), 0.12f);
      c[ImGuiCol_SliderGrabActive] = mix(accent, ImVec4(1.0f, 1.0f, 1.0f, 1.0f), 0.20f);
      c[ImGuiCol_TextSelectedBg] = withAlpha(accent, 0.35f);
      c[ImGuiCol_TextLink] = accent;
      c[ImGuiCol_TabSelectedOverline] = accent;
      c[ImGuiCol_TabDimmedSelectedOverline] = withAlpha(accent, 0.40f);
      c[ImGuiCol_SeparatorHovered] = withAlpha(accent, 0.70f);
      c[ImGuiCol_SeparatorActive] = accent;
      c[ImGuiCol_ResizeGrip] = withAlpha(accent, 0.15f);
      c[ImGuiCol_ResizeGripHovered] = withAlpha(accent, 0.60f);
      c[ImGuiCol_ResizeGripActive] = accent;
      c[ImGuiCol_DockingPreview] = withAlpha(accent, 0.40f);
      c[ImGuiCol_DragDropTarget] = accent;
      c[ImGuiCol_NavCursor] = accent;
      c[ImGuiCol_PlotLines] = accent;
      c[ImGuiCol_PlotHistogram] = accent;
    }

    void applyStyleColors(const ThemePalette& p) {
      ImGuiStyle& style = ImGui::GetStyle();
      if (!p.light) {
        applyLegacyStyleColors(style);
        if (!p.legacyStyle) applyAccentMarks(style, p.accent);
        return;
      }
      // Light theme: every color not set below keeps ImGui's light default of the same brightness.
      if (p.light) ImGui::StyleColorsLight(&style);
      else ImGui::StyleColorsDark(&style);
      ImVec4* c = style.Colors;

      const ImVec4 hover = mix(p.input, p.accent, 0.18f);
      const ImVec4 accentSoft = withAlpha(p.accent, 0.30f);
      const ImVec4 accentMid = withAlpha(p.accent, 0.45f);

      c[ImGuiCol_Text] = p.text;
      c[ImGuiCol_TextDisabled] = p.textDim;
      c[ImGuiCol_TextSelectedBg] = withAlpha(p.accent, 0.35f);
      c[ImGuiCol_TextLink] = p.accent;
      c[ImGuiCol_InputTextCursor] = p.accent;

      c[ImGuiCol_WindowBg] = p.panel;
      c[ImGuiCol_ChildBg] = p.raised;
      c[ImGuiCol_PopupBg] = withAlpha(p.raised, 0.98f);
      c[ImGuiCol_MenuBarBg] = p.base;
      c[ImGuiCol_Border] = p.border;
      c[ImGuiCol_BorderShadow] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);

      c[ImGuiCol_TitleBg] = p.base;
      c[ImGuiCol_TitleBgActive] = mix(p.base, p.accent, 0.10f);
      c[ImGuiCol_TitleBgCollapsed] = p.base;

      c[ImGuiCol_FrameBg] = p.input;
      c[ImGuiCol_FrameBgHovered] = hover;
      c[ImGuiCol_FrameBgActive] = mix(p.input, p.accent, 0.28f);

      c[ImGuiCol_Button] = p.input;
      c[ImGuiCol_ButtonHovered] = hover;
      c[ImGuiCol_ButtonActive] = mix(p.input, p.accent, 0.40f);

      c[ImGuiCol_Header] = p.header;
      c[ImGuiCol_HeaderHovered] = accentSoft;
      c[ImGuiCol_HeaderActive] = accentMid;

      c[ImGuiCol_CheckMark] = p.accent;
      c[ImGuiCol_SliderGrab] = p.accent;
      c[ImGuiCol_SliderGrabActive] = mix(p.accent, ImVec4(1.0f, 1.0f, 1.0f, 1.0f), 0.25f);

      c[ImGuiCol_Separator] = p.border;
      c[ImGuiCol_SeparatorHovered] = accentMid;
      c[ImGuiCol_SeparatorActive] = p.accent;
      c[ImGuiCol_ResizeGrip] = withAlpha(p.accent, 0.15f);
      c[ImGuiCol_ResizeGripHovered] = accentMid;
      c[ImGuiCol_ResizeGripActive] = p.accent;

      c[ImGuiCol_ScrollbarBg] = withAlpha(p.base, 0.60f);
      c[ImGuiCol_ScrollbarGrab] = mix(p.input, p.textDim, 0.25f);
      c[ImGuiCol_ScrollbarGrabHovered] = mix(p.input, p.textDim, 0.45f);
      c[ImGuiCol_ScrollbarGrabActive] = p.accent;

      // Tabs: the selected tab joins the panel below it and carries an accent overline.
      c[ImGuiCol_Tab] = p.base;
      c[ImGuiCol_TabHovered] = mix(p.panel, p.accent, 0.30f);
      c[ImGuiCol_TabSelected] = p.panel;
      c[ImGuiCol_TabSelectedOverline] = p.accent;
      c[ImGuiCol_TabDimmed] = p.base;
      c[ImGuiCol_TabDimmedSelected] = p.panel;
      c[ImGuiCol_TabDimmedSelectedOverline] = withAlpha(p.accent, 0.40f);

      c[ImGuiCol_DockingPreview] = withAlpha(p.accent, 0.40f);
      c[ImGuiCol_DockingEmptyBg] = p.base;

      c[ImGuiCol_PlotLines] = p.accent;
      c[ImGuiCol_PlotLinesHovered] = p.warn;
      c[ImGuiCol_PlotHistogram] = p.accent;
      c[ImGuiCol_PlotHistogramHovered] = p.warn;

      c[ImGuiCol_TableHeaderBg] = p.raised;
      c[ImGuiCol_TableBorderStrong] = p.border;
      c[ImGuiCol_TableBorderLight] = withAlpha(p.border, 0.60f);
      c[ImGuiCol_TableRowBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
      c[ImGuiCol_TableRowBgAlt] = withAlpha(p.text, 0.03f);

      c[ImGuiCol_TreeLines] = p.border;
      c[ImGuiCol_DragDropTarget] = p.accent;
      c[ImGuiCol_NavCursor] = p.accent;
      c[ImGuiCol_ModalWindowDimBg] = ImVec4(0.0f, 0.0f, 0.0f, p.light ? 0.30f : 0.55f);
    }

  } // namespace

  const ThemePalette& palette(const E_ThemeId id) {
    switch (id) {
      case E_ThemeId::GraphiteOrange: return graphiteOrange;
      case E_ThemeId::ClassicFem: return classicFem;
      case E_ThemeId::MidnightViolet: return midnightViolet;
      case E_ThemeId::EmeraldSlate: return emeraldSlate;
      case E_ThemeId::StudioLight: return studioLight;
      case E_ThemeId::Old: return oldTheme;
      case E_ThemeId::SteelCyan: break;
    }
    return steelCyan;
  }

  E_ThemeId themeFromKey(const std::string_view key) {
    for (const E_ThemeId id : allThemes) {
      if (key == palette(id).key) return id;
    }
    return E_ThemeId::SteelCyan;
  }

  const ThemePalette& theme() { return palette(g_current); }

  E_ThemeId currentTheme() { return g_current; }

  std::uint32_t themeRevision() { return g_revision; }

  void applyTheme(const E_ThemeId id) {
    g_current = id;
    ++g_revision;
    applyStyleColors(palette(id));
  }

} // namespace anaf::GUI::THEME end
