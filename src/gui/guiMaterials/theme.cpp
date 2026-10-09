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

    // Linear blend, t = 0 gives a, t = 1 gives b (alpha from a).
    constexpr ImVec4 mix(const ImVec4& a, const ImVec4& b, const float t) {
      return ImVec4(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w);
    }

    // Status colors shared by the dark themes; tuned to stay readable on every panel background.
    constexpr ImVec4 kGood = rgb(0x5FD38D);
    constexpr ImVec4 kWarn = rgb(0xF2B84B);
    constexpr ImVec4 kBad = rgb(0xF2665F);
    constexpr ImVec4 kInfo = rgb(0x6AAEF5);
    constexpr ImVec4 kCore = rgb(0x3FD6D0);

    constexpr ThemePalette kSteelCyan{
      .name = "Steel Blue / Cyan",
      .base = rgb(0x111418),
      .panel = rgb(0x171B21),
      .raised = rgb(0x1D222A),
      .input = rgb(0x242A34),
      .header = rgb(0x253041),
      .border = rgb(0x2F3742),
      .text = rgb(0xE3E8EF),
      .textDim = rgb(0x8A94A3),
      .accent = rgb(0x2EA8D6),
      .primary = rgb(0x1C7BA2),
      .primaryHovered = rgb(0x2690BB),
      .primaryActive = rgb(0x166685),
      .good = kGood,
      .warn = kWarn,
      .bad = kBad,
      .info = kInfo,
      .core = kCore,
      .note = rgb(0xA9B2BF),
      .sceneTop = rgb(0x2A3340),
      .sceneBottom = rgb(0x0E1115),
      .grid = rgb(0x8FA6BD),
      .sceneLabel = rgb(0xE6EDF5),
      .member = rgb(0xA7B9CD),
      .memberNoResult = rgb(0x6B7380),
      .overlayBg = rgb(0x12161B, 0.86f),
      .overlayBorder = rgb(0x3A4553, 0.90f),
      .overlayText = rgb(0xE3E8EF),
      .overlayTextDim = rgb(0x8A94A3),
    };

    constexpr ThemePalette kGraphiteOrange{
      .name = "Graphite / Orange",
      .base = rgb(0x141416),
      .panel = rgb(0x1B1B1E),
      .raised = rgb(0x222226),
      .input = rgb(0x2A2A2F),
      .header = rgb(0x33302D),
      .border = rgb(0x38383E),
      .text = rgb(0xE8E6E3),
      .textDim = rgb(0x8F8D8A),
      .accent = rgb(0xE8863A),
      .primary = rgb(0xB15E22),
      .primaryHovered = rgb(0xC86E2E),
      .primaryActive = rgb(0x944D1B),
      .good = kGood,
      .warn = rgb(0xF5CF5A), // yellower than the shared warn, so it does not read as the accent
      .bad = kBad,
      .info = kInfo,
      .core = kCore,
      .note = rgb(0xAEABA6),
      .sceneTop = rgb(0x3A3C42),
      .sceneBottom = rgb(0x131416),
      .grid = rgb(0xA3A3AA),
      .sceneLabel = rgb(0xEDEBE8),
      .member = rgb(0xB9BCC2),
      .memberNoResult = rgb(0x6E6E73),
      .overlayBg = rgb(0x151517, 0.86f),
      .overlayBorder = rgb(0x45444A, 0.90f),
      .overlayText = rgb(0xE8E6E3),
      .overlayTextDim = rgb(0x8F8D8A),
    };

    constexpr ThemePalette kClassicFem{
      .name = "Classic FEM",
      .base = rgb(0x16191E),
      .panel = rgb(0x1E2126),
      .raised = rgb(0x24282F),
      .input = rgb(0x2A2F37),
      .header = rgb(0x263752),
      .border = rgb(0x363C47),
      .text = rgb(0xE4E8EE),
      .textDim = rgb(0x8B93A0),
      .accent = rgb(0x4C95FF),
      .primary = rgb(0x2A68C8),
      .primaryHovered = rgb(0x3779DC),
      .primaryActive = rgb(0x2257A8),
      .good = kGood,
      .warn = kWarn,
      .bad = kBad,
      .info = rgb(0x7DB8FF),
      .core = kCore,
      .note = rgb(0xA7AFBB),
      .sceneTop = rgb(0x5A7CA8),
      .sceneBottom = rgb(0x0D1828),
      .grid = rgb(0xD5E2F2),
      .sceneLabel = rgb(0xFFFFFF),
      .member = rgb(0xD3D8DE),
      .memberNoResult = rgb(0x7D848E),
      .overlayBg = rgb(0x0E1622, 0.80f),
      .overlayBorder = rgb(0x6F8AAE, 0.85f),
      .overlayText = rgb(0xF0F4F9),
      .overlayTextDim = rgb(0xA4B2C4),
    };

    constexpr ThemePalette kMidnightViolet{
      .name = "Midnight / Violet",
      .base = rgb(0x0F1020),
      .panel = rgb(0x151729),
      .raised = rgb(0x1B1E33),
      .input = rgb(0x23263F),
      .header = rgb(0x2A2850),
      .border = rgb(0x30334D),
      .text = rgb(0xE4E4F2),
      .textDim = rgb(0x8C8EAA),
      .accent = rgb(0x9A86FF),
      .primary = rgb(0x6650D8),
      .primaryHovered = rgb(0x765FEA),
      .primaryActive = rgb(0x5641B8),
      .good = kGood,
      .warn = kWarn,
      .bad = kBad,
      .info = rgb(0x8FA8FF),
      .core = kCore,
      .note = rgb(0xA9AAC4),
      .sceneTop = rgb(0x2B2C4A),
      .sceneBottom = rgb(0x0B0C17),
      .grid = rgb(0x9C9CCB),
      .sceneLabel = rgb(0xEDEBFF),
      .member = rgb(0xB4B6D6),
      .memberNoResult = rgb(0x6C6E8A),
      .overlayBg = rgb(0x111226, 0.86f),
      .overlayBorder = rgb(0x45476E, 0.90f),
      .overlayText = rgb(0xE4E4F2),
      .overlayTextDim = rgb(0x8C8EAA),
    };

    constexpr ThemePalette kEmeraldSlate{
      .name = "Slate / Emerald",
      .base = rgb(0x111615),
      .panel = rgb(0x171D1C),
      .raised = rgb(0x1D2524),
      .input = rgb(0x242E2C),
      .header = rgb(0x20382F),
      .border = rgb(0x2F3B39),
      .text = rgb(0xE2EAE7),
      .textDim = rgb(0x879592),
      .accent = rgb(0x2FC48D),
      .primary = rgb(0x1C8A62),
      .primaryHovered = rgb(0x239E71),
      .primaryActive = rgb(0x167351),
      .good = rgb(0x9BE36A), // lime, so success does not read as the accent
      .warn = kWarn,
      .bad = kBad,
      .info = kInfo,
      .core = kCore,
      .note = rgb(0xA6B3B0),
      .sceneTop = rgb(0x2B3735),
      .sceneBottom = rgb(0x0D1110),
      .grid = rgb(0x93ABA5),
      .sceneLabel = rgb(0xE8F2EF),
      .member = rgb(0xAFC0BC),
      .memberNoResult = rgb(0x6B7875),
      .overlayBg = rgb(0x111716, 0.86f),
      .overlayBorder = rgb(0x3A4A47, 0.90f),
      .overlayText = rgb(0xE2EAE7),
      .overlayTextDim = rgb(0x879592),
    };

    // Light theme: dark text, deeper status colors (readable on white), light viewport gradient.
    constexpr ThemePalette kStudioLight{
      .name = "Studio Light",
      .light = true,
      .base = rgb(0xD9DDE3),
      .panel = rgb(0xEBEDF0),
      .raised = rgb(0xF5F6F8),
      .input = rgb(0xDCE1E7),
      .header = rgb(0xD2DCE9),
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
      .sceneTop = rgb(0xF4F6F9),
      .sceneBottom = rgb(0xB4BECB),
      .grid = rgb(0x4A5563),
      .sceneLabel = rgb(0x1B2129),
      .member = rgb(0x56677C),
      .memberNoResult = rgb(0x9AA3AE),
      .overlayBg = rgb(0xFFFFFF, 0.88f),
      .overlayBorder = rgb(0xAEB7C2, 0.95f),
      .overlayText = rgb(0x1D232B),
      .overlayTextDim = rgb(0x5E6875),
    };

    ThemeId g_current = ThemeId::SteelCyan;
    std::uint32_t g_revision = 0;

    void applyStyleColors(const ThemePalette& p) {
      ImGuiStyle& style = ImGui::GetStyle();
      // Every color not set below keeps ImGui's default of the same brightness.
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

  const ThemePalette& palette(const ThemeId id) {
    switch (id) {
      case ThemeId::GraphiteOrange: return kGraphiteOrange;
      case ThemeId::ClassicFem: return kClassicFem;
      case ThemeId::MidnightViolet: return kMidnightViolet;
      case ThemeId::EmeraldSlate: return kEmeraldSlate;
      case ThemeId::StudioLight: return kStudioLight;
      case ThemeId::SteelCyan: break;
    }
    return kSteelCyan;
  }

  const ThemePalette& theme() { return palette(g_current); }

  ThemeId currentTheme() { return g_current; }

  std::uint32_t themeRevision() { return g_revision; }

  void applyTheme(const ThemeId id) {
    g_current = id;
    ++g_revision;
    applyStyleColors(palette(id));
  }

} // namespace anaf::GUI::THEME end
