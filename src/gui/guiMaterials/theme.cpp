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

    // Every preset but Old shares the Professional design (2026-11-17): neutral greys without a
    // colour cast in three clear levels (menu / tab strip, panel, cards and framed inputs), square
    // corners, grey hovers (ThemeStyle::neutralHover) and the accent only on selection, checks, focus,
    // the tab overline and the run button. A preset picks light or dark, the accent and the viewport.
    constexpr ThemeStyle cadStyle{.rounding = 0.0f,
                                  .frameBorderSize = 1.0f,
                                  .framePadding = ImVec2(5.0f, 3.0f),
                                  .itemSpacing = ImVec2(6.0f, 4.0f),
                                  .scrollbarSize = 12.0f,
                                  .focusedTabOverlineOnly = true,
                                  .neutralHover = true};

    // Light family: white framed inputs on a light grey panel, darker strip; deep status colors.
    constexpr ThemePalette cadLight(const char* key, const char* name, const ImVec4 accent, const ImVec4 primaryHovered,
                                    const ImVec4 primaryActive, const ImVec4 panel, const ImVec4 scene) {
      return ThemePalette{
        .key = key,
        .name = name,
        .light = true,
        .paletteColors = true,
        .style = cadStyle,
        .base = mix(panel, rgb(0x000000), 0.10f),
        .panel = panel,
        .raised = mix(panel, rgb(0xFFFFFF), 0.65f),
        .input = rgb(0xFFFFFF),
        .header = mix(panel, rgb(0x000000), 0.05f),
        .border = rgb(0xB3B5B8),
        .text = rgb(0x1F2124),
        .textDim = rgb(0x6B6E73),
        .accent = accent,
        .primary = accent,
        .primaryHovered = primaryHovered,
        .primaryActive = primaryActive,
        .good = rgb(0x1C7F45),
        .warn = rgb(0x9E6210),
        .bad = rgb(0xBF3026),
        .info = accent,
        .core = rgb(0x0B7A76),
        .note = rgb(0x575A5F),
        .sceneTop = scene,
        .sceneBottom = scene,
        .grid = rgb(0x50545A),
        .sceneLabel = rgb(0x1B1D20),
        .member = rgb(0x4F5A66),
        .memberNoResult = rgb(0x9A9DA2),
        .overlayBg = rgb(0xFFFFFF, 0.94f),
        .overlayBorder = rgb(0xB3B5B8),
        .overlayText = rgb(0x1F2124),
        .overlayTextDim = rgb(0x62656A),
      };
    }

    // Dark family: graphite panel, darker strip, inputs set into the panel (darker, framed), light text.
    constexpr ThemePalette cadDark(const char* key, const char* name, const ImVec4 accent, const ImVec4 primary,
                                   const ImVec4 primaryHovered, const ImVec4 primaryActive, const ImVec4 scene) {
      return ThemePalette{
        .key = key,
        .name = name,
        .paletteColors = true,
        .style = cadStyle,
        .base = rgb(0x1C1D1F),
        .panel = rgb(0x28292C),
        .raised = rgb(0x303134),
        .input = rgb(0x1E1F21),
        .header = rgb(0x323336),
        .border = rgb(0x45474B),
        .text = rgb(0xDCDEE1),
        .textDim = rgb(0x8D9095),
        .accent = accent,
        .primary = primary,
        .primaryHovered = primaryHovered,
        .primaryActive = primaryActive,
        .good = ImVec4(0.45f, 0.85f, 0.52f, 1.0f),
        .warn = ImVec4(1.0f, 0.72f, 0.32f, 1.0f),
        .bad = ImVec4(1.0f, 0.45f, 0.42f, 1.0f),
        .info = ImVec4(0.42f, 0.70f, 1.0f, 1.0f),
        .core = ImVec4(0.0f, 0.85f, 0.85f, 1.0f),
        .note = ImVec4(0.70f, 0.70f, 0.70f, 1.0f),
        .sceneTop = scene,
        .sceneBottom = scene,
        .grid = ImVec4(0.62f, 0.68f, 0.75f, 1.0f),
        .sceneLabel = ImVec4(0.9f, 0.9f, 0.9f, 1.0f),
        .member = ImVec4(0.64f, 0.70f, 0.78f, 1.0f),
        .memberNoResult = ImVec4(0.55f, 0.55f, 0.55f, 1.0f),
        .overlayBg = rgb(0x232427, 0.92f),
        .overlayBorder = rgb(0x45474B),
        .overlayText = rgb(0xE4E5E7),
        .overlayTextDim = rgb(0x8D9095),
      };
    }

    // The default (2026-11-17): the light grey of CAD programs, blue accent.
    constexpr ThemePalette professional =
      cadLight("professional", "Professional", rgb(0x1A62AE), rgb(0x2272C4), rgb(0x15518F), rgb(0xEEEEEF), rgb(0xE3E4E6));

    // White panels, teal accent, a cooler light viewport.
    constexpr ThemePalette studioLight =
      cadLight("studio_light", "Light / Teal", rgb(0x0E7C86), rgb(0x12909B), rgb(0x0B646C), rgb(0xF6F6F7), rgb(0xE6E9EC));

    constexpr ThemePalette steelCyan =
      cadDark("steel_cyan", "Dark / Cyan", rgb(0x2EA8D6), rgb(0x1C7BA2), rgb(0x2690BB), rgb(0x166685), rgb(0x24272B));

    constexpr ThemePalette graphiteOrange = [] {
      ThemePalette p = cadDark("graphite_orange", "Dark / Orange", rgb(0xE8863A), rgb(0xB15E22), rgb(0xC86E2E), rgb(0x944D1B),
                               rgb(0x262728));
      p.warn = rgb(0xF5CF5A); // yellower than the shared warn, so it does not read as the accent
      return p;
    }();

    // Grey viewport like the classic pre / post processors; the dark blue result colors stay readable on it.
    constexpr ThemePalette classicFem =
      cadDark("classic_fem", "Dark / Blue, grey viewport", rgb(0x4C95FF), rgb(0x2A68C8), rgb(0x3779DC), rgb(0x2257A8), rgb(0x4B5058));

    constexpr ThemePalette midnightViolet =
      cadDark("midnight_violet", "Dark / Violet", rgb(0x9A86FF), rgb(0x6650D8), rgb(0x765FEA), rgb(0x5641B8), rgb(0x23232C));

    constexpr ThemePalette emeraldSlate = [] {
      ThemePalette p = cadDark("slate_emerald", "Dark / Emerald", rgb(0x2FC48D), rgb(0x1C8A62), rgb(0x239E71), rgb(0x167351),
                               rgb(0x222826));
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

    E_ThemeId g_current = E_ThemeId::Professional; // the default since 2026-11-17
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

    void applyStyleSizes(const ThemeStyle& s) {
      ImGuiStyle& style = ImGui::GetStyle();
      style.ChildRounding = s.rounding;
      style.FrameRounding = s.rounding;
      style.PopupRounding = s.rounding > 0.0f ? s.rounding + 1.0f : 0.0f; // square themes stay square
      style.ScrollbarRounding = s.rounding;
      style.GrabRounding = s.rounding;
      style.TabRounding = s.rounding;
      style.FrameBorderSize = s.frameBorderSize;
      style.FramePadding = s.framePadding;
      style.ItemSpacing = s.itemSpacing;
      style.ScrollbarSize = s.scrollbarSize;
    }

    void applyStyleColors(const ThemePalette& p) {
      ImGuiStyle& style = ImGui::GetStyle();
      if (!p.light && !p.paletteColors) {
        applyLegacyStyleColors(style);
        if (!p.legacyStyle) applyAccentMarks(style, p.accent);
        return;
      }
      // Palette themes: every color not set below keeps ImGui's default of the same brightness.
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
      c[ImGuiCol_TabDimmedSelectedOverline] = withAlpha(p.accent, p.style.focusedTabOverlineOnly ? 0.0f : 0.40f);

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

      if (!p.style.neutralHover) return;
      // Grey steps of the text color over the surface they sit on; the accent stays on selection
      // (Header), checks, focus, tab overline and grips.
      const auto step = [&](const ImVec4& surface, const float amount) { return mix(surface, p.text, amount); };
      c[ImGuiCol_FrameBgHovered] = step(p.input, 0.04f);
      c[ImGuiCol_FrameBgActive] = p.input;
      c[ImGuiCol_Button] = step(p.panel, 0.05f);
      c[ImGuiCol_ButtonHovered] = step(p.panel, 0.10f);
      c[ImGuiCol_ButtonActive] = step(p.panel, 0.16f);
      c[ImGuiCol_Header] = withAlpha(p.accent, p.light ? 0.16f : 0.30f);
      c[ImGuiCol_HeaderHovered] = step(p.panel, 0.07f);
      c[ImGuiCol_HeaderActive] = withAlpha(p.accent, p.light ? 0.24f : 0.40f);
      c[ImGuiCol_TitleBgActive] = p.base;
      c[ImGuiCol_TabHovered] = mix(p.base, p.panel, 0.55f);
      c[ImGuiCol_ScrollbarBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
      c[ImGuiCol_ScrollbarGrab] = step(p.panel, 0.20f);
      c[ImGuiCol_ScrollbarGrabHovered] = step(p.panel, 0.32f);
      c[ImGuiCol_ScrollbarGrabActive] = step(p.panel, 0.45f);
      c[ImGuiCol_TableHeaderBg] = p.header;
      c[ImGuiCol_TableRowBgAlt] = withAlpha(p.text, 0.025f);
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
      case E_ThemeId::SteelCyan: return steelCyan;
      case E_ThemeId::Professional: break;
    }
    return professional;
  }

  E_ThemeId themeFromKey(const std::string_view key) {
    for (const E_ThemeId id : allThemes) {
      if (key == palette(id).key) return id;
    }
    return E_ThemeId::Professional;
  }

  const ThemePalette& theme() { return palette(g_current); }

  E_ThemeId currentTheme() { return g_current; }

  std::uint32_t themeRevision() { return g_revision; }

  void applyTheme(const E_ThemeId id) {
    g_current = id;
    ++g_revision;
    applyStyleSizes(palette(id).style);
    applyStyleColors(palette(id));
  }

} // namespace anaf::GUI::THEME end
