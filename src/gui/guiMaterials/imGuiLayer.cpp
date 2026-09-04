// Copyright (c) 2026 Ufuk Deniz Konuk
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

#include <filesystem>
#include <string>
#include <vector>
#include <glad/gl.h>
#include <GLFW/glfw3.h>

#include "../../directory/getExecutableDirectory.hpp"

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#include "imGuiLayer.hpp"
#include "../../log/anaf_info.hpp"

namespace anaf::GUI {

    namespace {
        std::filesystem::path resolveAssetPath(const std::filesystem::path& relative_subpath) {
            const std::filesystem::path exe_dir = anaf::DIRECTORY::getExecutableDirectory();

            const std::vector<std::filesystem::path> search_paths = {
                std::filesystem::path("/usr/share/anafinen/assets") / relative_subpath,
                exe_dir / "assets" / relative_subpath,
                std::filesystem::path("assets") / relative_subpath,
#ifdef MAIN_DIR
                std::filesystem::path(MAIN_DIR) / "assets" / relative_subpath
#endif
            };

            for (const auto& candidate : search_paths) {
                if (std::filesystem::exists(candidate)) {
                    return candidate;
                }
            }

            return {};
        }
    }

    void setupSpecialTheme(){
        ImGuiStyle& style = ImGui::GetStyle();

        style.WindowRounding    = 0.0f;
        style.ChildRounding     = 0.0f;
        style.FrameRounding     = 3.0f;
        style.PopupRounding     = 3.0f;
        style.ScrollbarRounding = 3.0f;
        style.GrabRounding      = 3.0f;
        style.TabRounding       = 3.0f;

        style.WindowBorderSize  = 0.0f;
        style.FrameBorderSize   = 0.0f;
        style.PopupBorderSize   = 1.0f;

        style.WindowPadding     = ImVec2(8.0f, 8.0f);
        style.FramePadding      = ImVec2(6.0f, 4.0f);
        style.ItemSpacing       = ImVec2(8.0f, 6.0f);
        style.ItemInnerSpacing  = ImVec2(6.0f, 4.0f);
        style.ScrollbarSize     = 8.0f;

        ImVec4* colors = style.Colors;
        colors[ImGuiCol_WindowBg]             = ImVec4(0.10f, 0.105f, 0.11f, 1.00f);
        colors[ImGuiCol_ChildBg]              = ImVec4(0.14f, 0.15f, 0.17f, 1.00f);
        colors[ImGuiCol_PopupBg]              = ImVec4(0.13f, 0.14f, 0.16f, 0.98f);
        colors[ImGuiCol_Border]               = ImVec4(0.22f, 0.24f, 0.28f, 0.60f);

        colors[ImGuiCol_TitleBg]              = ImVec4(0.10f, 0.105f, 0.11f, 1.0f);
        colors[ImGuiCol_TitleBgActive]        = ImVec4(0.15f, 0.1505f, 0.151f, 1.0f);
        colors[ImGuiCol_TitleBgCollapsed]     = ImVec4(0.10f, 0.105f, 0.11f, 1.0f);
        colors[ImGuiCol_Header]               = ImVec4(0.20f, 0.205f, 0.21f, 1.0f);
        colors[ImGuiCol_HeaderHovered]        = ImVec4(0.30f, 0.305f, 0.31f, 1.0f);
        colors[ImGuiCol_HeaderActive]         = ImVec4(0.15f, 0.1505f, 0.151f, 1.0f);

        colors[ImGuiCol_Button]               = ImVec4(0.20f, 0.205f, 0.21f, 1.0f);
        colors[ImGuiCol_ButtonHovered]        = ImVec4(0.30f, 0.305f, 0.31f, 1.0f);
        colors[ImGuiCol_ButtonActive]         = ImVec4(0.15f, 0.1505f, 0.151f, 1.0f);

        colors[ImGuiCol_FrameBg]              = ImVec4(0.15f, 0.1505f, 0.151f, 1.0f);
        colors[ImGuiCol_FrameBgHovered]       = ImVec4(0.22f, 0.225f, 0.23f, 1.0f);
        colors[ImGuiCol_FrameBgActive]        = ImVec4(0.12f, 0.1205f, 0.121f, 1.0f);
        colors[ImGuiCol_CheckMark]            = ImVec4(0.35f, 0.68f, 1.00f, 1.00f);
        colors[ImGuiCol_SliderGrab]           = ImVec4(0.30f, 0.58f, 0.90f, 1.00f);
        colors[ImGuiCol_SliderGrabActive]     = ImVec4(0.40f, 0.70f, 1.00f, 1.00f);

        colors[ImGuiCol_Tab]                  = ImVec4(0.15f, 0.1505f, 0.151f, 1.0f);
        colors[ImGuiCol_TabHovered]           = ImVec4(0.38f, 0.3805f, 0.381f, 1.0f);
        colors[ImGuiCol_TabActive]            = ImVec4(0.28f, 0.2805f, 0.281f, 1.0f);
        colors[ImGuiCol_TabUnfocused]         = ImVec4(0.15f, 0.1505f, 0.151f, 1.0f);
        colors[ImGuiCol_TabUnfocusedActive]   = ImVec4(0.20f, 0.205f, 0.21f, 1.0f);

        colors[ImGuiCol_Text]                 = ImVec4(0.92f, 0.93f, 0.95f, 1.00f);
        colors[ImGuiCol_TextDisabled]         = ImVec4(0.50f, 0.53f, 0.58f, 1.00f);
    }

    void ImGuiLayer::init(GLFWwindow* window) {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

        io.IniFilename = nullptr;

        float xscale = 1.0f;
        float yscale = 1.0f;
        glfwGetWindowContentScale(window, &xscale, &yscale);
        constexpr float render_scale = 1.0f;

        const std::filesystem::path ui_font_subpath = std::filesystem::path("fonts") / "Inter" / "ttf" / "Inter-Medium.ttf";
        const std::filesystem::path console_font_subpath = std::filesystem::path("fonts") / "CascadiaCode" / "ttf" / "CascadiaMono.ttf";

        const std::filesystem::path ui_font_resolved = resolveAssetPath(ui_font_subpath);
        const std::filesystem::path console_font_resolved = resolveAssetPath(console_font_subpath);

        if (!ui_font_resolved.empty()) {
            font_ui = io.Fonts->AddFontFromFileTTF(ui_font_resolved.string().c_str(), 18.0f * render_scale);
        } else {
            anaf::LOG::warn("[ImGuiLayer] UI font missing, using fallback.");
            font_ui = io.Fonts->AddFontDefault();
        }

        if (!console_font_resolved.empty()) {
            font_console = io.Fonts->AddFontFromFileTTF(console_font_resolved.string().c_str(), 18.0f * render_scale);
        } else {
            font_console = font_ui;
        }

        io.FontGlobalScale = 1.0f / render_scale;

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

