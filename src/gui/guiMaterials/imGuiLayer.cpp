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

#include "imGuiLayer.hpp"
#include "theme.hpp"
#include "userSettings.hpp"

#include <directory/getExecutableDirectory.hpp>
#include <io/core/pathUtf8.hpp> // ImGui opens files from UTF-8 names
#include <log/anaf_info.hpp>

#ifdef IMGUI_ENABLE_FREETYPE
#include "imgui_freetype.h"
#endif

#include <algorithm>
#include <filesystem>

namespace anaf::GUI {

  void setupSpecialTheme(){
    ImGuiStyle& style = ImGui::GetStyle();

    // Shared by every theme. Rounding, frame border, frame padding, item spacing and scrollbar
    // size belong to the theme preset (THEME::ThemeStyle) and are set by applyTheme() below.
    style.WindowRounding    = 0.0f;

    style.WindowBorderSize  = 0.0f;
    style.ChildBorderSize   = 1.0f;
    style.PopupBorderSize   = 1.0f;
    style.TabBarBorderSize  = 1.0f;
    style.TabBarOverlineSize = 2.0f;

    style.WindowPadding     = ImVec2(8.0f, 8.0f);
    style.CellPadding       = ImVec2(6.0f, 3.0f);
    style.ItemInnerSpacing  = ImVec2(6.0f, 4.0f);

    // Colors and preset sizes come from the saved theme (Settings > Theme switches and saves it at run time).
    THEME::applyTheme(THEME::themeFromKey(SETTINGS::settings().theme));
  }

  void ImGuiLayer::init(GLFWwindow* window) {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImPlot::CreateContext(); // beam diagrams
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    io.IniFilename = nullptr;

    // Fonts are rasterized on demand at the size in use (ImGui 1.92+), so the size setting needs no atlas rebuild.
    const float fontSize = std::clamp(SETTINGS::settings().uiFontSize, SETTINGS::minUiFontSize, SETTINGS::maxUiFontSize);
    ImGui::GetStyle().FontSizeBase = fontSize;

    ImFontConfig uiFontConfig;
    ImFontConfig consoleFontConfig;
#ifdef IMGUI_ENABLE_FREETYPE
    // Light hinting snaps glyphs to the pixel grid vertically only: sharp baselines and x-heights,
    // Inter's spacing kept. The monospace console font keeps FreeType's default hinting.
    uiFontConfig.FontLoaderFlags = ImGuiFreeTypeLoaderFlags_LightHinting;
#endif
    const std::filesystem::path uiFontSubpath = std::filesystem::path("fonts") / "Inter" / "ttf" / "Inter-Medium.ttf";
    const std::filesystem::path consoleFontSubpath = std::filesystem::path("fonts") / "CascadiaCode" / "ttf" / "CascadiaMono.ttf";

    const std::filesystem::path uiFontResolved = anaf::DIRECTORY::findAssetPath(uiFontSubpath);
    const std::filesystem::path consoleFontResolved = anaf::DIRECTORY::findAssetPath(consoleFontSubpath);

    if (!uiFontResolved.empty()) {
      g_fontUi = io.Fonts->AddFontFromFileTTF(anaf::IO::pathToUtf8(uiFontResolved).c_str(), fontSize, &uiFontConfig);
    } else {
      anaf::LOG::warn("[ImGuiLayer] UI font missing, using fallback.");
      g_fontUi = io.Fonts->AddFontDefault();
    }

    if (!consoleFontResolved.empty()) {
      g_fontConsole = io.Fonts->AddFontFromFileTTF(anaf::IO::pathToUtf8(consoleFontResolved).c_str(), fontSize, &consoleFontConfig);
    } else {
      g_fontConsole = g_fontUi;
    }

    setupSpecialTheme();

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 460");
  }

  void ImGuiLayer::setUiFontSize(const float size) {
    // Takes effect at the next NewFrame(); writing FontSizeBase mid-frame is undone by ImGui at the
    // end of the frame (the same route as ImGui's own style editor).
    ImGui::GetStyle()._NextFrameFontSizeBase = std::clamp(size, SETTINGS::minUiFontSize, SETTINGS::maxUiFontSize);
  }

  void ImGuiLayer::beginFrame() {
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
  }

} // namespace anaf::GUI end

