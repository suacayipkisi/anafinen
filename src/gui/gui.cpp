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

#include "gui.hpp"

#include <glad/gl.h>
#include <GLFW/glfw3.h>

#include <png.h>

#include <filesystem>
#include <memory>
#include <string>
#include <vector>



#include "../log/anaf_info.hpp"

#include "../directory/getExecutableDirectory.hpp"

#include "guiMaterials/framebuffer.hpp"
#include "guiMaterials/imGuiLayer.hpp"
#include "guiMaterials/iPanel.hpp"
#include "panels/logTerminal.hpp"
#include "panels/mainDockSpaceHost.hpp"
#include "panels/modelTree.hpp"
#include "panels/truss/simpleQuadrangleTruss/trussControlPanel.hpp"
#include "panels/truss/trussTypePanel.hpp"
#include "panels/viewportPanel.hpp"
#include "linuxCursor.hpp"


namespace anaf::GUI {

    namespace {
        void setWindowIcon(GLFWwindow* window) {
            const std::filesystem::path icon_subpath = std::filesystem::path("icons") / "anafinen.png";
            const std::vector<std::filesystem::path> candidates = {
                anaf::DIRECTORY::getExecutableDirectory() / "assets" / icon_subpath,
                std::filesystem::path("assets") / icon_subpath,
#ifdef MAIN_DIR
                std::filesystem::path(MAIN_DIR) / "assets" / icon_subpath,
#endif
                std::filesystem::path("/usr/share/anafinen/assets") / icon_subpath
            };

            std::filesystem::path icon_path;
            for (const auto& candidate : candidates) {
                if (std::filesystem::exists(candidate)) {
                    icon_path = candidate;
                    break;
                }
            }
            if (icon_path.empty()) {
                anaf::LOG::warn("[GUI] Application icon not found.");
                return;
            }

            png_image image{};
            image.version = PNG_IMAGE_VERSION;
            if (!png_image_begin_read_from_file(&image, icon_path.string().c_str())) {
                anaf::LOG::warn("[GUI] Failed to read application icon.");
                return;
            }
            image.format = PNG_FORMAT_RGBA;
            std::vector<png_byte> pixels(PNG_IMAGE_SIZE(image));
            if (!png_image_finish_read(&image, nullptr, pixels.data(), 0, nullptr)) {
                png_image_free(&image);
                anaf::LOG::warn("[GUI] Failed to decode application icon.");
                return;
            }

            GLFWimage glfw_icon{
                static_cast<int>(image.width),
                static_cast<int>(image.height),
                pixels.data()
            };
            glfwSetWindowIcon(window, 1, &glfw_icon);
            png_image_free(&image);
        }
    }

    void bindAnalysisFlow(UIPanels panels) {
        panels.dock->on_select_analyze_structure = [panels](AnalyzeStructureType type) {
            if (type == Truss_1D) {
                panels.selector->isOpen = true;
            }
        };

        panels.selector->onSelected = [panels](TrussTypes type) {
            panels.control->isOpen = (type == simpleQuadranglePrism);
            panels.tree->isOpen = (type == simpleQuadranglePrism);
        };
    }

    std::shared_ptr<ViewportPanel> openPanels(PanelManager& panelManager, GLFWwindow* window, std::shared_ptr<Framebuffer>& fbo) {
        auto dock = panelManager.addPanel<MainDockSpaceHost>(window);
        auto viewport = panelManager.addPanel<ViewportPanel>(fbo);
        auto tree = panelManager.addPanel<ModelTree>();
        auto trussSelector = panelManager.addPanel<TrussSelector>();
        auto trussControl = panelManager.addPanel<TrussControlPanel>();
        auto log = panelManager.addPanel<LogTerminal>();

        UIPanels panels{
            dock.get(),
            viewport.get(),
            trussSelector.get(),
            trussControl.get(),
            tree.get(),
            log.get()
        };

        trussSelector->isOpen = false;
        trussControl->isOpen = false;
        tree->isOpen = false;

        bindAnalysisFlow(panels);

        return viewport;
    }

    int initgui(){
#ifdef __linux__
        platform_utils::setupSystemCursor();
        glfwInitHint(GLFW_PLATFORM, GLFW_ANY_PLATFORM);
#endif

        if(!glfwInit()){
            anaf::LOG::error("Failed to initialize GLFW");
            glfwInitHint(GLFW_PLATFORM, GLFW_ANY_PLATFORM);
            if (!glfwInit()) {
                anaf::LOG::error("Fatal: GLFW initialization failed completely");
                return -1;
            }
        }

        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

#ifdef __APPLE__
        glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif
#ifdef __linux__
    glfwWindowHintString(GLFW_WAYLAND_APP_ID, "anafinen");
    glfwWindowHintString(GLFW_X11_CLASS_NAME, "anafinen");
    glfwWindowHintString(GLFW_X11_INSTANCE_NAME, "anafinen");
#endif
        GLFWwindow* window = glfwCreateWindow(1600, 900, "Anafinen", nullptr, nullptr);

        if (!window) {
            anaf::LOG::error("Failed to create GLFW window");
            glfwTerminate();
            return -1;
        }

        glfwMakeContextCurrent(window);
        glfwSwapInterval(1);
        setWindowIcon(window);
        gladLoadGL((GLADloadfunc)glfwGetProcAddress);

        //core system initialization
        ImGuiLayer imguiLayer;
        imguiLayer.init(window);

        auto fbo = std::make_shared<Framebuffer>(1280, 720);

        // register UI panels
        PanelManager panelManager;
        auto viewport = openPanels(panelManager, window, fbo);
        

        // game loop
        while (!glfwWindowShouldClose(window)){
            glfwPollEvents();

            if (viewport && viewport->isOpen) {
                viewport->renderSceneOpenGL();
            }

            //clear default framebuffer and render imgui panels
            int w, h;
            glfwGetFramebufferSize(window, &w, &h);
            glViewport(0, 0, w, h);
            glClearColor(0.12f, 0.12f, 0.12f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);

            imguiLayer.beginFrame();
            
            panelManager.onImGuiRender();
            imguiLayer.endFrame();

            glfwSwapBuffers(window);

        }

        imguiLayer.shutdown();
        glfwDestroyWindow(window);
        glfwTerminate();

        return 0;
    }

} // namespace anaf::GUI end
