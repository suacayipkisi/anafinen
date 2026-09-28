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

#include "gui.hpp"

#include <glad/gl.h>
#include <GLFW/glfw3.h>

#include <png.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

#include <log/anaf_info.hpp>
#include <bridge/generalStatus.hpp>

#include <directory/getExecutableDirectory.hpp>

#include "guiMaterials/framebuffer.hpp"
#include "guiMaterials/imGuiLayer.hpp"
#include "guiMaterials/iPanel.hpp"

#include "materialHandler.hpp"
#include "panels/logTerminal.hpp"
#include "panels/mainDockSpaceHost.hpp"
#include "panels/modelTree.hpp"
#include "panels/truss/simpleQuadrangleTruss/trussControlPanel.hpp"
#include "panels/truss/trussTypePanel.hpp"
#include "panels/viewportPanel.hpp"
#include "panels/statusBar.hpp"

#include "linuxCursor.hpp"


namespace anaf::GUI {

  namespace {
    void setWindowIcon(GLFWwindow* window) {
      const std::filesystem::path icon_subpath = std::filesystem::path("icons") / "anafinen.png";
      const std::filesystem::path icon_path = anaf::DIRECTORY::findAssetPath(icon_subpath);
      if (icon_path.empty()) {
        anaf::LOG::warn("[GUI] Application icon not found.");
        return;
      }

      // Read through std::ifstream: libpng's *_from_file uses fopen(), which cannot open
      // non-ASCII paths on Windows (e.g. a ZIP extracted under "C:\Users\Şule").
      std::ifstream file(icon_path, std::ios::binary);
      const std::vector<char> encoded{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};

      png_image image{};
      image.version = PNG_IMAGE_VERSION;
      if (encoded.empty() || !png_image_begin_read_from_memory(&image, encoded.data(), encoded.size())) {
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

    panels.control->onOpenMaterialHandler = [panels] {
      panels.matWindow->isOpen = true;
    };

    panels.dock->on_show_about = [panels] { panels.about->isOpen = true; };
    panels.dock->on_import_mesh = [panels] { panels.fileIo->requestImport(); };
    panels.dock->on_export_results = [panels] { panels.fileIo->requestExport(); };
    panels.fileIo->onImported = [panels] {
      panels.tree->isOpen = true;
      panels.viewport->requestFit();
    };
  }

  std::shared_ptr<ViewportPanel> openPanels(PanelManager& panelManager, GLFWwindow* window, std::shared_ptr<Framebuffer>& fbo) {
    auto dock = panelManager.addPanel<MainDockSpaceHost>(window);
    panelManager.addPanel<StatusBar>(); // bottom bar: worker state and resource usage
    auto viewport = panelManager.addPanel<ViewportPanel>(fbo);
    auto tree = panelManager.addPanel<ModelTree>();
    auto trussSelector = panelManager.addPanel<TrussSelector>();
    auto trussControl = panelManager.addPanel<TrussControlPanel>();
    auto log = panelManager.addPanel<LogTerminal>();
    auto matWindow = panelManager.addPanel<MaterialHandler>();
    auto fileIo = panelManager.addPanel<FileIoPanel>();
    auto about = panelManager.addPanel<AboutPanel>();

    UIPanels panels{
      dock.get(),
      viewport.get(),
      trussSelector.get(),
      trussControl.get(),
      tree.get(),
      log.get(),
      matWindow.get(),
      fileIo.get(),
      about.get()
    };

    trussSelector->isOpen = false;
    trussControl->isOpen = false;
    tree->isOpen = false;
    matWindow->isOpen = false;

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

    glfwWindowHint(GLFW_MAXIMIZED, GLFW_TRUE);

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

    // Every object owning GL resources lives in this scope, so its destructor
    // runs while the GL context and the ImGui context are still alive.
    {
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

      auto& calculationBridge = anaf::BRIDGE::buildBridge();
      calculationBridge.workerThread.request_stop();
      calculationBridge.workerThread = std::jthread{};
    } // GL resources released here, before the context is destroyed

    imguiLayer.shutdown();
    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
  }

} // namespace anaf::GUI end
