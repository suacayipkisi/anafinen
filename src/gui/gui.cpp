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
#include <mutex>
#include <optional>
#include <string_view>
#include <thread>
#include <vector>

#include <bridge/generalStatus.hpp>
#include <directory/getExecutableDirectory.hpp>
#include <log/anaf_info.hpp>

#include "guiMaterials/framebuffer.hpp"
#include "guiMaterials/iconAtlas.hpp"
#include "guiMaterials/imGuiLayer.hpp"
#include "guiMaterials/iPanel.hpp"

#include "panels/aboutPanel.hpp"
#include "panels/analysisSelector.hpp"
#include "panels/analysisSettingsPanel.hpp"
#include "panels/beam/beamDiagramPanel.hpp"
#include "panels/beam/beamModelEditor.hpp"
#include "panels/beam/sectionHandler.hpp"
#include "panels/fileIoPanel.hpp"
#include "panels/libraryDialog.hpp"
#include "panels/logTerminal.hpp"
#include "panels/mainDockSpaceHost.hpp"
#include "panels/materialHandler.hpp"
#include "panels/modelTree.hpp"
#include "panels/truss/importedTruss/trussModelEditor.hpp"
#include "panels/truss/simpleQuadrangleTruss/trussControlPanel.hpp"
#include "panels/viewportPanel.hpp"
#include "panels/viewportToolbar.hpp"
#include "panels/welcomePanel.hpp"

#include "linuxCursor.hpp"

namespace anaf::GUI {

  namespace {
    struct DecodedIcon {
      int width = 0;
      int height = 0;
      std::vector<png_byte> pixels; // RGBA8
    };

    std::optional<DecodedIcon> loadIconPng(std::string_view fileName) {
      const std::filesystem::path iconPath =
        anaf::DIRECTORY::findAssetPath(std::filesystem::path("icons") / fileName);
      if (iconPath.empty()) {
        return std::nullopt;
      }

      // Read through std::ifstream: libpng's *_from_file uses fopen(), which cannot open
      // non-ASCII paths on Windows (e.g. a ZIP extracted under "C:\Users\Şule").
      std::ifstream file(iconPath, std::ios::binary);
      const std::vector<char> encoded{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};

      png_image image{};
      image.version = PNG_IMAGE_VERSION;
      if (encoded.empty() || !png_image_begin_read_from_memory(&image, encoded.data(), encoded.size())) {
        anaf::LOG::warn("[GUI] Failed to read application icon {}.", fileName);
        return std::nullopt;
      }
      image.format = PNG_FORMAT_RGBA;
      DecodedIcon icon{static_cast<int>(image.width), static_cast<int>(image.height),
                       std::vector<png_byte>(PNG_IMAGE_SIZE(image))};
      if (!png_image_finish_read(&image, nullptr, icon.pixels.data(), 0, nullptr)) {
        png_image_free(&image);
        anaf::LOG::warn("[GUI] Failed to decode application icon {}.", fileName);
        return std::nullopt;
      }
      png_image_free(&image);
      return icon;
    }

    // anafinen-32.png is drawn from the small-size SVG; GLFW picks the closest size for the
    // title bar / taskbar instead of shrinking the 128 px image (which closes the A's counter).
    void setWindowIcon(GLFWwindow* window) {
      std::vector<DecodedIcon> icons;
      for (const std::string_view fileName : {"anafinen.png", "anafinen-32.png"}) {
        if (auto icon = loadIconPng(fileName)) {
          icons.push_back(std::move(*icon));
        }
      }
      if (icons.empty()) {
        anaf::LOG::warn("[GUI] Application icon not found.");
        return;
      }

      std::vector<GLFWimage> glfwIcons;
      glfwIcons.reserve(icons.size());
      for (DecodedIcon& icon : icons) {
        glfwIcons.push_back(GLFWimage{icon.width, icon.height, icon.pixels.data()});
      }
      glfwSetWindowIcon(window, static_cast<int>(glfwIcons.size()), glfwIcons.data());
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
      AnalysisSelector* selector = nullptr;
      TrussControlPanel* control = nullptr;
      ModelTree* tree = nullptr;
      LogTerminal* console = nullptr;
      MaterialHandler* matWindow = nullptr;
      FileIoPanel* fileIo = nullptr;
      AboutPanel* about = nullptr;
      TrussModelEditor* editor = nullptr;
      BeamModelEditor* beamEditor = nullptr;
      SectionHandler* sections = nullptr;
      BeamDiagramPanel* diagrams = nullptr;
      WelcomePanel* welcome = nullptr;
      ViewportToolbar* toolbar = nullptr;
      LibraryDialog* library = nullptr;
      AnalysisSettingsPanel* analysisSettings = nullptr;
    };

    anaf::BRIDGE::E_ObjectType activeType() { return anaf::BRIDGE::buildBridge().objectType.load(); }

    // A static solve can start: a model type with something to solve, no job running, static loads.
    bool canSolve() {
      auto& bridge = anaf::BRIDGE::buildBridge();
      if (bridge.isRunning.load() || bridge.isGeneratingPreview.load()) return false;
      if (bridge.loadKind.load() == anaf::BRIDGE::E_LoadKind::Dynamic) return false;
      std::lock_guard lock(bridge.dataMutex);
      switch (bridge.objectType.load()) {
        case anaf::BRIDGE::E_ObjectType::BeamFrame: return bridge.activeBeamMesh && !bridge.activeBeamMesh->elements.empty();
        case anaf::BRIDGE::E_ObjectType::TrussSqpt: return true; // built from the panel inputs
        case anaf::BRIDGE::E_ObjectType::TrussImportedOrEntered: return bridge.activeMesh && !bridge.activeMesh->trussElements.empty();
        default: return false;
      }
    }

    void solveActive(const UIPanels& panels) {
      switch (activeType()) {
        case anaf::BRIDGE::E_ObjectType::BeamFrame: panels.beamEditor->solve(); break;
        case anaf::BRIDGE::E_ObjectType::TrussSqpt: panels.control->solve(); break;
        case anaf::BRIDGE::E_ObjectType::TrussImportedOrEntered: panels.editor->solve(); break;
        default: break;
      }
    }

    void clearActiveModel(const UIPanels& panels) {
      auto& bridge = anaf::BRIDGE::buildBridge();
      if (bridge.isRunning.load() || bridge.isGeneratingPreview.load()) return;
      const auto type = activeType();
      if (type == anaf::BRIDGE::E_ObjectType::NoType) return;
      bridge.resetModel(type);
      panels.control->resetState();
      panels.editor->resetState();
      panels.beamEditor->resetState();
    }

    void bindCommands(UIPanels panels) {
      panels.toolbar->commands.isAvailable = [](const E_ModelCommand command) {
        const auto type = activeType();
        const bool editable = type == anaf::BRIDGE::E_ObjectType::BeamFrame || type == anaf::BRIDGE::E_ObjectType::TrussImportedOrEntered;
        const bool busy = anaf::BRIDGE::buildBridge().isRunning.load();
        switch (command) {
          case E_ModelCommand::Library: return true;
          case E_ModelCommand::AddNode:
          case E_ModelCommand::AddElement: return editable && !busy;
          case E_ModelCommand::Materials:
          case E_ModelCommand::AnalysisSettings: return type != anaf::BRIDGE::E_ObjectType::NoType;
          case E_ModelCommand::Sections:
          case E_ModelCommand::Diagrams: return type == anaf::BRIDGE::E_ObjectType::BeamFrame;
          case E_ModelCommand::Solve: return canSolve();
        }
        return false;
      };
      panels.toolbar->commands.run = [panels](const E_ModelCommand command) {
        const bool beam = activeType() == anaf::BRIDGE::E_ObjectType::BeamFrame;
        switch (command) {
          case E_ModelCommand::Library: panels.library->open(); break;
          case E_ModelCommand::AddNode:
            if (beam) panels.beamEditor->openAddNode();
            else panels.editor->openAddNode();
            break;
          case E_ModelCommand::AddElement:
            if (beam) panels.beamEditor->openAddElement();
            else panels.editor->openAddBar();
            break;
          case E_ModelCommand::Materials: panels.matWindow->isOpen = true; break;
          case E_ModelCommand::Sections: panels.sections->isOpen = true; break;
          case E_ModelCommand::Solve: solveActive(panels); break;
          case E_ModelCommand::AnalysisSettings: panels.analysisSettings->open(); break;
          case E_ModelCommand::Diagrams: panels.diagrams->isOpen = !panels.diagrams->isOpen; break;
        }
      };

      panels.dock->onOpenLibrary = [panels] { panels.library->open(); };
      panels.dock->onClearModel = [panels] { clearActiveModel(panels); };
      panels.dock->onRunSolver = [panels] { solveActive(panels); };
      panels.dock->canRunSolver = [] { return canSolve(); };
      panels.dock->onAnalysisSettings = [panels] { panels.analysisSettings->open(); };
      panels.dock->hasModel = [] { return activeType() != anaf::BRIDGE::E_ObjectType::NoType; };

      // The library models are solved for the static loads, so the editors must show those.
      panels.library->onLoad = [panels](const std::filesystem::path& path) {
        anaf::BRIDGE::buildBridge().loadKind = anaf::BRIDGE::E_LoadKind::Constant;
        panels.fileIo->importFile(path);
      };

      panels.tree->onAssignAll = [panels] {
        if (activeType() == anaf::BRIDGE::E_ObjectType::BeamFrame) panels.beamEditor->openAssignAll();
        else panels.editor->openAssignAll();
      };
      panels.tree->onRemoveAllReleases = [panels] { panels.beamEditor->removeAllReleases(); };
      panels.tree->onOpenMaterials = [panels] { panels.matWindow->isOpen = true; };
      panels.tree->onOpenSections = [panels] { panels.sections->isOpen = true; };
      panels.tree->onOpenDiagrams = [panels] { panels.diagrams->isOpen = true; };
    }

    void closeBeamPanels(const UIPanels& panels) {
      panels.beamEditor->isOpen = false;
      panels.diagrams->isOpen = false;
      panels.sections->isOpen = false;
    }

    void bindAnalysisFlow(UIPanels panels) {
      panels.dock->onSelectTruss = [panels] { panels.selector->open(E_StructureFamily::Truss); };
      panels.dock->onSelectBeam = [panels] { panels.selector->open(E_StructureFamily::Beam); };

      // Only a change of type resets: selecting the current type again just reopens its panel.
      // The load kind only changes what the editors show, so it never resets the model.
      panels.selector->onTrussSelected = [panels](E_TrussTypes type, anaf::BRIDGE::E_LoadKind loadKind) {
        const auto objectType = type == E_TrussTypes::SimpleQuadranglePrism
          ? anaf::BRIDGE::E_ObjectType::TrussSqpt
          : anaf::BRIDGE::E_ObjectType::TrussImportedOrEntered;
        auto& bridge = anaf::BRIDGE::buildBridge();
        bridge.loadKind = loadKind;
        if (bridge.objectType.load() != objectType) {
          panels.fileIo->cancelImport();
          bridge.resetModel(objectType);
          panels.control->resetState();
          panels.editor->resetState();
        }
        panels.control->isOpen = (type == E_TrussTypes::SimpleQuadranglePrism);
        panels.editor->isOpen = (type == E_TrussTypes::NodeEntered);
        panels.tree->isOpen = true;
        closeBeamPanels(panels);
      };

      panels.selector->onBeamSelected = [panels](anaf::BRIDGE::E_LoadKind loadKind) {
        auto& bridge = anaf::BRIDGE::buildBridge();
        bridge.loadKind = loadKind;
        if (bridge.objectType.load() != anaf::BRIDGE::E_ObjectType::BeamFrame) {
          panels.fileIo->cancelImport();
          bridge.resetModel(anaf::BRIDGE::E_ObjectType::BeamFrame);
          panels.control->resetState();
          panels.editor->resetState();
          panels.beamEditor->resetState();
        }
        panels.control->isOpen = false;
        panels.editor->isOpen = false;
        panels.beamEditor->isOpen = true;
        panels.tree->isOpen = true;
        // Static section force diagrams; reopened from the Panels menu if wanted.
        panels.diagrams->isOpen = loadKind == anaf::BRIDGE::E_LoadKind::Constant;
      };
      panels.beamEditor->onOpenMaterialHandler = [panels] { panels.matWindow->isOpen = true; };
      panels.beamEditor->onOpenSectionHandler = [panels] { panels.sections->isOpen = true; };
      panels.beamEditor->onOpenLibrary = [panels] { panels.library->open(); };
      panels.beamEditor->onOpenDiagrams = [panels] { panels.diagrams->isOpen = true; };

      panels.control->onOpenMaterialHandler = [panels] {
        panels.matWindow->isOpen = true;
      };
      panels.editor->onOpenMaterialHandler = [panels] {
        panels.matWindow->isOpen = true;
      };
      panels.editor->onRequestImport = [panels] { panels.fileIo->requestImport(); };
      panels.editor->onOpenLibrary = [panels] { panels.library->open(); };

      // Panels menu: only the panels the active analysis uses can be reopened.
      panels.dock->addPanelMenuEntry({"3D Simulation Viewport", panels.viewport, {}, nullptr, ICONS::E_IconId::PanelViewport});
      panels.dock->addPanelMenuEntry({"Grid Truss Generator", panels.control,
        [] { return activeType() == anaf::BRIDGE::E_ObjectType::TrussSqpt; },
        "Only for the Simple Quadrangle truss (Analyze > Truss (1D Element))", ICONS::E_IconId::ModelTruss});
      panels.dock->addPanelMenuEntry({"Properties (Truss)", panels.editor,
        [] { return activeType() == anaf::BRIDGE::E_ObjectType::TrussImportedOrEntered; },
        "Only for an imported / self-built truss (Analyze > Truss (1D Element))", ICONS::E_IconId::PanelProperties});
      const auto beamActive = [] { return activeType() == anaf::BRIDGE::E_ObjectType::BeamFrame; };
      panels.dock->addPanelMenuEntry({"Properties (Beam / Frame)", panels.beamEditor, beamActive,
        "Only for a beam / frame model (Analyze > Beam / Frame (3D Element))", ICONS::E_IconId::PanelProperties});
      panels.dock->addPanelMenuEntry({"Beam Diagrams", panels.diagrams, beamActive,
        "Only for a beam / frame model (Analyze > Beam / Frame (3D Element))", ICONS::E_IconId::ResultDiagram});
      panels.dock->addPanelMenuEntry({"Section Handler", panels.sections, beamActive,
        "Only for a beam / frame model (Analyze > Beam / Frame (3D Element))", ICONS::E_IconId::ModelSection});
      panels.dock->addPanelMenuEntry({"Material Handler", panels.matWindow,
        [] { return activeType() != anaf::BRIDGE::E_ObjectType::NoType; },
        "Select an analysis first (Analyze menu)", ICONS::E_IconId::ModelMaterial});
      panels.dock->addPanelMenuEntry({"Model Tree", panels.tree,
        [] { return activeType() != anaf::BRIDGE::E_ObjectType::NoType; },
        "Select an analysis first (Analyze menu)", ICONS::E_IconId::PanelTree});
      panels.dock->addPanelMenuEntry({"Console", panels.console, {}, nullptr, ICONS::E_IconId::PanelConsole});

      panels.dock->onShowAbout = [panels] { panels.about->isOpen = true; };
      panels.dock->onShowWelcome = [panels] { panels.welcome->open(); };
      panels.welcome->onNewBeam = [panels] { panels.selector->open(E_StructureFamily::Beam); };
      panels.welcome->onNewTruss = [panels] { panels.selector->open(E_StructureFamily::Truss); };
      panels.welcome->onImport = [panels] { panels.fileIo->requestImport(); };
      // The example is solved for the static loads, so the editors must show those.
      panels.welcome->onOpenExample = [panels](const std::filesystem::path& path) {
        anaf::BRIDGE::buildBridge().loadKind = anaf::BRIDGE::E_LoadKind::Constant;
        panels.fileIo->importFile(path);
      };
      panels.dock->onImportMesh = [panels] { panels.fileIo->requestImport(); };
      panels.dock->onExportResults = [panels] { panels.fileIo->requestExport(); };
      panels.fileIo->onImportedBeam = [panels] {
        panels.control->resetState();
        panels.editor->resetState();
        panels.beamEditor->resetState();
        panels.control->isOpen = false;
        panels.editor->isOpen = false;
        panels.beamEditor->isOpen = true;
        panels.tree->isOpen = true;
        panels.diagrams->isOpen = true;
        panels.viewport->requestFit();
      };
      // The import itself reset the bridge to TrussImportedOrEntered (FileIoPanel::pollTasks).
      panels.fileIo->onImported = [panels] {
        panels.control->resetState();
        panels.editor->resetState();
        panels.control->isOpen = false;
        panels.editor->isOpen = true;
        panels.tree->isOpen = true;
        closeBeamPanels(panels);
        panels.viewport->requestFit();
      };
    }

    std::shared_ptr<ViewportPanel> openPanels(PanelManager& panelManager, GLFWwindow* window, const std::shared_ptr<Framebuffer>& fbo) {
      auto dock = panelManager.addPanel<MainDockSpaceHost>(window);
      // Both share the display toggles; the viewport reads them in renderSceneOpenGL().
      auto display = std::make_shared<ViewportDisplayOptions>();
      auto viewport = panelManager.addPanel<ViewportPanel>(fbo, display);
      auto toolbar = panelManager.addPanel<ViewportToolbar>(display, viewport.get());
      auto tree = panelManager.addPanel<ModelTree>();
      auto analysisSelector = panelManager.addPanel<AnalysisSelector>();
      auto trussControl = panelManager.addPanel<TrussControlPanel>();
      auto console = panelManager.addPanel<LogTerminal>();
      auto matWindow = panelManager.addPanel<MaterialHandler>();
      auto fileIo = panelManager.addPanel<FileIoPanel>();
      auto about = panelManager.addPanel<AboutPanel>();
      auto editor = panelManager.addPanel<TrussModelEditor>();
      auto beamEditor = panelManager.addPanel<BeamModelEditor>();
      auto sections = panelManager.addPanel<SectionHandler>();
      auto diagrams = panelManager.addPanel<BeamDiagramPanel>();
      auto library = panelManager.addPanel<LibraryDialog>();
      auto analysisSettings = panelManager.addPanel<AnalysisSettingsPanel>();
      // Last, so it is drawn (and focused) on top of the docked panels.
      auto welcome = panelManager.addPanel<WelcomePanel>();

      UIPanels panels{
        dock.get(),
        viewport.get(),
        analysisSelector.get(),
        trussControl.get(),
        tree.get(),
        console.get(),
        matWindow.get(),
        fileIo.get(),
        about.get(),
        editor.get(),
        beamEditor.get(),
        sections.get(),
        diagrams.get(),
        welcome.get(),
        toolbar.get(),
        library.get(),
        analysisSettings.get()
      };

      analysisSelector->isOpen = false;
      trussControl->isOpen = false;
      editor->isOpen = false;
      tree->isOpen = false;
      matWindow->isOpen = false;
      beamEditor->isOpen = false;
      sections->isOpen = false;
      diagrams->isOpen = false;

      bindAnalysisFlow(panels);
      bindCommands(panels);

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
        ICONS::beginFrame(); // drops the icon textures after a scale or dark / light change

        panelManager.onImGuiRender();
        imguiLayer.endFrame();

        glfwSwapBuffers(window);
      }

      auto& calculationBridge = anaf::BRIDGE::buildBridge();
      calculationBridge.workerThread.request_stop();
      calculationBridge.workerThread = std::jthread{};
      ICONS::release(); // icon textures
    } // GL resources released here, before the context is destroyed

    imguiLayer.shutdown();
    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
  }

} // namespace anaf::GUI end
