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

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
#include <string_view>
#include <thread>
#include <vector>

#include <bridge/generalStatus.hpp>
#include <directory/getExecutableDirectory.hpp>
#include <log/anaf_info.hpp>

#include "guiMaterials/framebuffer.hpp"
#include "guiMaterials/imGuiLayer.hpp"
#include "guiMaterials/iPanel.hpp"

#include "panels/aboutPanel.hpp"
#include "panels/fileIoPanel.hpp"
#include "panels/logTerminal.hpp"
#include "panels/mainDockSpaceHost.hpp"
#include "panels/materialHandler.hpp"
#include "panels/modelTree.hpp"
#include "panels/truss/importedTruss/trussModelEditor.hpp"
#include "panels/truss/simpleQuadrangleTruss/trussControlPanel.hpp"
#include "panels/truss/trussTypePanel.hpp"
#include "panels/viewportPanel.hpp"
#include "panels/viewportToolbar.hpp"

#include "linuxCursor.hpp"

namespace anaf::GUI {

  namespace {
    struct DecodedIcon {
      int width = 0;
      int height = 0;
      std::vector<png_byte> pixels; // RGBA8
    };

    std::optional<DecodedIcon> loadIconPng(std::string_view file_name) {
      const std::filesystem::path icon_path =
        anaf::DIRECTORY::findAssetPath(std::filesystem::path("icons") / file_name);
      if (icon_path.empty()) {
        return std::nullopt;
      }

      // Read through std::ifstream: libpng's *_from_file uses fopen(), which cannot open
      // non-ASCII paths on Windows (e.g. a ZIP extracted under "C:\Users\Şule").
      std::ifstream file(icon_path, std::ios::binary);
      const std::vector<char> encoded{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};

      png_image image{};
      image.version = PNG_IMAGE_VERSION;
      if (encoded.empty() || !png_image_begin_read_from_memory(&image, encoded.data(), encoded.size())) {
        anaf::LOG::warn("[GUI] Failed to read application icon {}.", file_name);
        return std::nullopt;
      }
      image.format = PNG_FORMAT_RGBA;
      DecodedIcon icon{static_cast<int>(image.width), static_cast<int>(image.height),
                       std::vector<png_byte>(PNG_IMAGE_SIZE(image))};
      if (!png_image_finish_read(&image, nullptr, icon.pixels.data(), 0, nullptr)) {
        png_image_free(&image);
        anaf::LOG::warn("[GUI] Failed to decode application icon {}.", file_name);
        return std::nullopt;
      }
      png_image_free(&image);
      return icon;
    }

    // anafinen-32.png is drawn from the small-size SVG; GLFW picks the closest size for the
    // title bar / taskbar instead of shrinking the 128 px image (which closes the A's counter).
    void setWindowIcon(GLFWwindow* window) {
      std::vector<DecodedIcon> icons;
      for (const std::string_view file_name : {"anafinen.png", "anafinen-32.png"}) {
        if (auto icon = loadIconPng(file_name)) {
          icons.push_back(std::move(*icon));
        }
      }
      if (icons.empty()) {
        anaf::LOG::warn("[GUI] Application icon not found.");
        return;
      }

      std::vector<GLFWimage> glfw_icons;
      glfw_icons.reserve(icons.size());
      for (DecodedIcon& icon : icons) {
        glfw_icons.push_back(GLFWimage{icon.width, icon.height, icon.pixels.data()});
      }
      glfwSetWindowIcon(window, static_cast<int>(glfw_icons.size()), glfw_icons.data());
    }

    // Which window system GLFW picked. On Linux it can differ from the session: under a
    // Wayland session GLFW falls back to X11 (XWayland) when its Wayland backend is missing.
    void logWindowSystem() {
      std::string_view platform = "unknown";
#if GLFW_VERSION_MAJOR > 3 || (GLFW_VERSION_MAJOR == 3 && GLFW_VERSION_MINOR >= 4)
      switch (glfwGetPlatform()) {
        case GLFW_PLATFORM_WAYLAND: platform = "Wayland"; break;
        case GLFW_PLATFORM_X11: platform = "X11"; break;
        case GLFW_PLATFORM_WIN32: platform = "Win32"; break;
        case GLFW_PLATFORM_COCOA: platform = "Cocoa"; break;
        case GLFW_PLATFORM_NULL: platform = "none (headless)"; break;
        default: break;
      }
#endif
#ifdef __linux__
      const char* session = std::getenv("XDG_SESSION_TYPE");
      anaf::LOG::info("Window system: {} (session type: {})", platform, session && *session ? session : "unknown");
#else
      anaf::LOG::info("Window system: {}", platform);
#endif
    }

    struct UIPanels {
      MainDockSpaceHost* dock = nullptr;
      ViewportPanel* viewport = nullptr;
      TrussSelector* selector = nullptr;
      TrussControlPanel* control = nullptr;
      ModelTree* tree = nullptr;
      LogTerminal* console = nullptr;
      MaterialHandler* matWindow = nullptr;
      FileIoPanel* fileIo = nullptr;
      AboutPanel* about = nullptr;
      TrussModelEditor* editor = nullptr;
    };

    void bindAnalysisFlow(UIPanels panels) {
      panels.dock->on_select_truss = [panels] { panels.selector->isOpen = true; };

      // Only a change of type resets: selecting the current type again just reopens its panel.
      panels.selector->onSelected = [panels](TrussTypes type) {
        const auto objectType = type == simpleQuadranglePrism
          ? anaf::BRIDGE::ObjectType::truss_SQPT
          : anaf::BRIDGE::ObjectType::truss_imported_or_entered;
        auto& bridge = anaf::BRIDGE::buildBridge();
        if (bridge.m_objectType.load() != objectType) {
          panels.fileIo->cancelImport();
          bridge.resetModel(objectType);
          panels.control->resetState();
          panels.editor->resetState();
        }
        panels.control->isOpen = (type == simpleQuadranglePrism);
        panels.editor->isOpen = (type == nodeEntered);
        panels.tree->isOpen = true;
      };

      panels.control->onOpenMaterialHandler = [panels] {
        panels.matWindow->isOpen = true;
      };
      panels.editor->onOpenMaterialHandler = [panels] {
        panels.matWindow->isOpen = true;
      };
      panels.editor->onRequestImport = [panels] { panels.fileIo->requestImport(); };
      panels.editor->onLoadBuiltin = [panels](const std::filesystem::path& path) { panels.fileIo->importFile(path); };

      // Panels menu: only the panels the active analysis uses can be reopened.
      const auto activeType = [] { return anaf::BRIDGE::buildBridge().m_objectType.load(); };
      panels.dock->addPanelMenuEntry({"3D Simulation Viewport", panels.viewport, {}, nullptr});
      panels.dock->addPanelMenuEntry({"Truss(1D) Analysis Set", panels.control,
        [activeType] { return activeType() == anaf::BRIDGE::ObjectType::truss_SQPT; },
        "Only for the Simple Quadrangle truss (Analyze > Truss (1D Element))"});
      panels.dock->addPanelMenuEntry({"Truss(1D) Model Editor", panels.editor,
        [activeType] { return activeType() == anaf::BRIDGE::ObjectType::truss_imported_or_entered; },
        "Only for an imported / self-built truss (Analyze > Truss (1D Element))"});
      panels.dock->addPanelMenuEntry({"Material Handler", panels.matWindow,
        [activeType] { return activeType() != anaf::BRIDGE::ObjectType::no_type; },
        "Select an analysis first (Analyze > Truss (1D Element))"});
      panels.dock->addPanelMenuEntry({"Model Tree", panels.tree,
        [activeType] { return activeType() != anaf::BRIDGE::ObjectType::no_type; },
        "Select an analysis first (Analyze > Truss (1D Element))"});
      panels.dock->addPanelMenuEntry({"Console", panels.console, {}, nullptr});

      panels.dock->on_show_about = [panels] { panels.about->isOpen = true; };
      panels.dock->on_import_mesh = [panels] { panels.fileIo->requestImport(); };
      panels.dock->on_export_results = [panels] { panels.fileIo->requestExport(); };
      // The import itself reset the bridge to truss_imported_or_entered (FileIoPanel::pollTasks).
      panels.fileIo->onImported = [panels] {
        panels.control->resetState();
        panels.editor->resetState();
        panels.control->isOpen = false;
        panels.editor->isOpen = true;
        panels.tree->isOpen = true;
        panels.viewport->requestFit();
      };
    }

    std::shared_ptr<ViewportPanel> openPanels(PanelManager& panelManager, GLFWwindow* window, const std::shared_ptr<Framebuffer>& fbo) {
      auto dock = panelManager.addPanel<MainDockSpaceHost>(window);
      // Both share the display toggles; the viewport reads them in renderSceneOpenGL().
      auto display = std::make_shared<ViewportDisplayOptions>();
      auto viewport = panelManager.addPanel<ViewportPanel>(fbo, display);
      panelManager.addPanel<ViewportToolbar>(display, viewport.get());
      auto tree = panelManager.addPanel<ModelTree>();
      auto trussSelector = panelManager.addPanel<TrussSelector>();
      auto trussControl = panelManager.addPanel<TrussControlPanel>();
      auto console = panelManager.addPanel<LogTerminal>();
      auto matWindow = panelManager.addPanel<MaterialHandler>();
      auto fileIo = panelManager.addPanel<FileIoPanel>();
      auto about = panelManager.addPanel<AboutPanel>();
      auto editor = panelManager.addPanel<TrussModelEditor>();

      UIPanels panels{
        dock.get(),
        viewport.get(),
        trussSelector.get(),
        trussControl.get(),
        tree.get(),
        console.get(),
        matWindow.get(),
        fileIo.get(),
        about.get(),
        editor.get()
      };

      trussSelector->isOpen = false;
      trussControl->isOpen = false;
      editor->isOpen = false;
      tree->isOpen = false;
      matWindow->isOpen = false;

      bindAnalysisFlow(panels);

      return viewport;
    }
  } // namespace end

  int initgui(){
    platform_utils::setupSystemCursor();

    // GLFW picks the platform itself (Wayland or X11 on Linux); a retry would change nothing.
    if (!glfwInit()) {
      anaf::LOG::error("Failed to initialize GLFW");
      return -1;
    }
    logWindowSystem();

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
    if (gladLoadGL(glfwGetProcAddress) == 0) {
      anaf::LOG::error("Failed to load the OpenGL 4.6 functions");
      glfwDestroyWindow(window);
      glfwTerminate();
      return -1;
    }

    ImGuiLayer imguiLayer;
    imguiLayer.init(window);

    // Every object owning GL resources lives in this scope, so its destructor
    // runs while the GL context and the ImGui context are still alive.
    {
      auto fbo = std::make_shared<Framebuffer>(1280, 720);

      PanelManager panelManager;
      auto viewport = openPanels(panelManager, window, fbo);

      while (!glfwWindowShouldClose(window)){
        glfwPollEvents();

        if (viewport && viewport->isOpen) {
          viewport->renderSceneOpenGL();
        }

        // Clear the default framebuffer, then draw the ImGui panels over it.
        int w = 0, h = 0;
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
