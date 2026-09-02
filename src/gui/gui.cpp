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

#include <memory>

#include "../log/anaf_info.hpp"

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

    void openPanels(PanelManager& panelManager, GLFWwindow* window, std::shared_ptr<Framebuffer>& fbo) {
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

        GLFWwindow* window = glfwCreateWindow(1600, 900, "Modular Simulation Engine", nullptr, nullptr);

        if (!window) {
            anaf::LOG::error("Failed to create GLFW window");
            glfwTerminate();
            return -1;
        }

        glfwMakeContextCurrent(window);
        glfwSwapInterval(1);
        gladLoadGL((GLADloadfunc)glfwGetProcAddress);

        //core system initialization
        ImGuiLayer imguiLayer;
        imguiLayer.init(window);

        auto fbo = std::make_shared<Framebuffer>(1280, 720);

        // register UI panels
        PanelManager panelManager;
        openPanels(panelManager, window, fbo);
        

        // game loop
        while (!glfwWindowShouldClose(window)){
            glfwPollEvents();

            // render 3d sim scene into custom fbo
            fbo->bind();
            glEnable(GL_DEPTH_TEST);
            glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

            //native 3d draw calls
            fbo->unbind();

            //clear default framebuffer and render imgui panels
            int w, h;
            glfwGetFramebufferSize(window, &w, &h);
            glViewport(0, 0, w, h);
            glClearColor(0.2f, 0.3f, 0.3f, 1.0f);
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
