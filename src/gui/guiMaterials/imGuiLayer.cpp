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

#include <filesystem>

namespace anaf::GUI {

  void setupSpecialTheme(){
    ImGuiStyle& style = ImGui::GetStyle();

    style.WindowRounding    = 0.0f;
    style.ChildRounding     = 3.0f;
    style.FrameRounding     = 3.0f;
    style.PopupRounding     = 4.0f;
    style.ScrollbarRounding = 3.0f;
    style.GrabRounding      = 3.0f;
    style.TabRounding       = 3.0f;

    style.WindowBorderSize  = 0.0f;
    style.ChildBorderSize   = 1.0f;
    style.FrameBorderSize   = 0.0f;
    style.PopupBorderSize   = 1.0f;
    style.TabBarBorderSize  = 1.0f;
    style.TabBarOverlineSize = 2.0f;

    style.WindowPadding     = ImVec2(8.0f, 8.0f);
    style.FramePadding      = ImVec2(6.0f, 4.0f);
    style.CellPadding       = ImVec2(6.0f, 3.0f);
    style.ItemSpacing       = ImVec2(8.0f, 6.0f);
    style.ItemInnerSpacing  = ImVec2(6.0f, 4.0f);
    style.ScrollbarSize     = 10.0f;

    // Colors come from the saved theme (Settings > Theme switches and saves it at run time).
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

    constexpr float fontSize = 18.0f;
    const std::filesystem::path ui_font_subpath = std::filesystem::path("fonts") / "Inter" / "ttf" / "Inter-Medium.ttf";
    const std::filesystem::path console_font_subpath = std::filesystem::path("fonts") / "CascadiaCode" / "ttf" / "CascadiaMono.ttf";

    const std::filesystem::path ui_font_resolved = anaf::DIRECTORY::findAssetPath(ui_font_subpath);
    const std::filesystem::path console_font_resolved = anaf::DIRECTORY::findAssetPath(console_font_subpath);

    if (!ui_font_resolved.empty()) {
      font_ui = io.Fonts->AddFontFromFileTTF(anaf::IO::pathToUtf8(ui_font_resolved).c_str(), fontSize);
    } else {
      anaf::LOG::warn("[ImGuiLayer] UI font missing, using fallback.");
      font_ui = io.Fonts->AddFontDefault();
    }

    if (!console_font_resolved.empty()) {
      font_console = io.Fonts->AddFontFromFileTTF(anaf::IO::pathToUtf8(console_font_resolved).c_str(), fontSize);
    } else {
      font_console = font_ui;
    }

    setupSpecialTheme();

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 460");
  }

  void ImGuiLayer::beginFrame() {
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
  }

} // namespace anaf::GUI end

