# GUI and Rendering

This document describes the window, the ImGui panel system, the frame loop, and the OpenGL viewport render pipeline, including entity picking.

> **Document status**
> Verified against: `v0.1.3-alpha` working tree (unreleased), 2026-09-28.

## 1. Overall flow (one frame)

```text
initgui()
  GLFW window (OpenGL 4.6 core) -> GLAD -> ImGuiLayer::init (docking, fonts, theme)
  { GL resource scope:
      Framebuffer fbo(1280x720) ; PanelManager ; openPanels() ; bindAnalysisFlow()
      while (!glfwWindowShouldClose):
      +------------------------------------------------------------------------------+
      | glfwPollEvents()                                                             |
      |                                                                              |
      | ViewportPanel::renderSceneOpenGL()          <- OpenGL pass into offscreen FBO|
      |   reload MeshData if dataVersion changed -> buildSceneBatches() -> upload    |
      |   fbo.bind(); fbo.clear(); renderGrid(); render(); node labels; renderText() |
      |   fbo.unbind()  -> bind 0, then MSAA resolve (color + entity ID)             |
      |                                                                              |
      | clear default framebuffer                                                    |
      | ImGuiLayer::beginFrame()                                                     |
      | PanelManager::onImGuiRender()               <- every open IPanel             |
      |   ViewportPanel::onImGuiRender(): ImGui::Image(fbo texture), picking,        |
      |                                   camera input, 2D overlay                   |
      | ImGuiLayer::endFrame()   -> ImGui_ImplOpenGL3_RenderDrawData                 |
      | glfwSwapBuffers()                                                            |
      +------------------------------------------------------------------------------+
      stop + join worker
  }  <- GL objects destroyed here, context still current
  imguiLayer.shutdown(); glfwDestroyWindow(); glfwTerminate()
```

The 3D scene is drawn **before** the ImGui frame, into an offscreen framebuffer. The viewport panel then shows that texture as an `ImGui::Image`. So the image shown is the scene rendered at the start of the current frame, using the camera state from the previous frame's input.

## 2. Panel system

- `IPanel` (`guiMaterials/iPanel.hpp`) has a virtual `onImGuiRender()` and a public `isOpen` flag.
- `PanelManager::addPanel<T>(args...)` creates a panel as a `shared_ptr` and stores it. `PanelManager::onImGuiRender()` renders every open panel in registration order.
- Panels do not know each other. `bindAnalysisFlow()` in `gui.cpp` wires them together with `std::function` callbacks:

```text
MainDockSpaceHost --on_select_analyze_structure(Truss_1D)--> TrussSelector.isOpen = true
TrussSelector     --onSelected(simpleQuadranglePrism)------> TrussControlPanel.isOpen, ModelTree.isOpen
TrussControlPanel --onOpenMaterialHandler()----------------> MaterialHandler.isOpen = true
MainDockSpaceHost --on_import_mesh / on_export_results-----> FileIoPanel.requestImport() / requestExport()
FileIoPanel       --onImported()---------------------------> ModelTree.isOpen = true, ViewportPanel.requestFit()
```

| Panel | File | Window title | Status |
|---|---|---|---|
| `MainDockSpaceHost` | `panels/mainDockSpaceHost.cpp` | full-screen dockspace + menu bar | File: "Import Mesh / CAD..." (Ctrl+O), "Export Model..." (Ctrl+E), Exit; Analyze: Truss 1D; Help: "About anafinen...". Builds the default dock layout once (left: analysis set, right: model tree, bottom: console, center: viewport). |
| `ViewportPanel` | `panels/viewportPanel.cpp` | "3D Simulation Viewport" | Camera, picking, overlays, legends |
| `TrussSelector` | `panels/truss/trussTypePanel.cpp` | truss type picker | "Simple Quadrangle"; imported/self-built "(coming soon)" |
| `TrussControlPanel` | `panels/truss/simpleQuadrangleTruss/trussControlPanel.cpp` | "Truss(1D) Analysis Set" | Geometry, material, loads, fixity, deform scale, preview/solve/demo/clear, starts the worker. The material combo keeps the stable material ID and resolves it to an index when a job starts (falls back to the first material if the selected one was removed). |
| `ModelTree` | `panels/modelTree.cpp` | "Model Tree" | Boundary conditions, elements over yield (MPa), node displacements (mm) |
| `MaterialHandler` | `panels/materialHandler.cpp` | "Material Handler" (floating, not dockable) | Table of all materials (E, G, K in GPa; yield / ultimate in MPa; density; ν; ductility in %). Form to add a user material (engineering units, converted to SI); user materials are saved to the user config directory. "Delete" only on user materials; refusals (in use, worker running) are shown in the panel. See [BRIDGE.md](BRIDGE.md) section 5.1. |
| `LogTerminal` | `panels/logTerminal.cpp` | "Console" | Colored log view (max 10,000 lines); process CPU %, process RAM, system RAM % from `/proc` (Linux) |
| `AboutPanel` | `panels/aboutPanel.cpp` | "About anafinen" (modal) | GPLv3 "Appropriate Legal Notices": version, copyright, no-warranty text, full `LICENSE` and `THIRD_PARTY_LICENSES.md` (read from the exe folder, `/usr/share/doc/anafinen` or the source tree). The same notice is logged at startup (`main.cpp`). |
| `FileIoPanel` | `panels/fileIoPanel.cpp` | none (popups + bottom-right overlay) | Import / export: native file chooser, CAD and export option dialogs, progress bar with Cancel, result notices. Always "open"; draws only while needed. |

### 2.1 ImGui layer (`guiMaterials/imGuiLayer.*`)

- Config flags: keyboard navigation and docking enabled.
- Fonts: `Inter-Medium.ttf` for the UI and `CascadiaMono.ttf` for the console, both 18 px times the render scale. Falls back to the ImGui default font when the files are missing.
- Custom theme: `setupSpecialTheme()`.
- OpenGL backend initialized with `#version 460`.

### 2.2 Log sink

`anaf::LOG::setCallback()` sends every formatted line to `anafUILogSink()`, which appends to `g_ui_logs` under `g_log_mutex`. Worker threads can therefore log safely, and the console panel reads the buffer on the GUI thread.

### 2.3 File import / export (`FileIoPanel`)

```text
File > Import Mesh / CAD... (Ctrl+O)
  -> NativeFileDialog::openFile     OS chooser; ready() polled each frame (returns in ~0.2 ms)
  -> CAD file? -> "CAD Import Options" modal: bars / surfaces / volumes, element size, order
  -> IoService::runAsync: readMesh + ADAPTER::toMeshData       (I/O thread)
  -> overlay: description, progress bar, stage, Cancel
  -> done: activeMesh, fixedDOFsByNode, m_objectType = truss_imported_or_entered,
           dataVersion++ (GUI thread, under dataMutex); log notes / warnings; tree opens, camera fits

File > Export Model... (Ctrl+E)
  -> "Export Model" modal: MSH 4.1 / MSH 2.2 / VTU / VTK 5.1 / VTK 4.2 / STEP, binary, zlib
  -> NativeFileDialog::saveFile (extension added when missing)
  -> snapshot pointer + fixity copied under dataMutex
  -> IoService::runAsync: ADAPTER::toMeshModel + writeMesh     (I/O thread)
```

- **Native dialogs:** `portable-file-dialogs` behind `fileDialogs/nativeFileDialog.*`. It is the only translation unit that includes the header. On Linux it runs `zenity` / `kdialog` as a child process; closing the application kills an open chooser. When no backend exists the panel reports it instead of failing.
- **Blocking during a calculation:** import is refused while the solver or preview worker runs, so the worker cannot overwrite the imported snapshot.
- **Solving imported models:** surface / volume meshes are shown as wireframe. Solving imported trusses is not implemented yet (`Truss_Imported_or_Entered`).

## 3. Viewport render pipeline

### 3.1 Resources

| Object | Class | GL objects |
|---|---|---|
| Offscreen target | `Framebuffer` (`guiMaterials/framebuffer.*`) | MSAA FBO: RGBA8 + R32I + D24S8 renderbuffers. Resolve FBO: RGBA8 + R32I textures (immutable storage). |
| Batches | `ViewportRenderer` (`panels/viewportRenderer.*`) | 5 VAO/VBO pairs: grid, lines, glow lines, points, text |
| Programs | `ViewportRenderer` | `scene` (lines/points), `grid`, `text` |

Every GL object is owned by a move-only `GlHandle` (`guiMaterials/glHandle.hpp`) and created through DSA (`glCreate*`, `glNamed*`, `glVertexArray*`). No `glGen*` or bind-to-edit.

### 3.2 Vertex layouts (binding 0 for every VAO)

| Batch | Struct | loc 0 | loc 1 | loc 2 | loc 3 |
|---|---|---|---|---|---|
| lines, glow lines | `Vertex3D` | vec3 position | vec4 color | int entityID (`IFormat`) | - |
| points | `Point3D` | vec3 position | vec4 color | int entityID | float size |
| text | `TextVertex` | vec2 NDC position | vec2 uv | vec4 color | - |
| grid | `glm::vec3` | vec3 position | - | - | - |

Dynamic batches are re-uploaded with `glNamedBufferData(..., GL_DYNAMIC_DRAW)` (orphaning) only when the mesh or visibility changes. The grid quad uses immutable `glNamedBufferStorage`.

### 3.3 Shaders (inline raw strings, `#version 460 core`)

| Program | Vertex | Fragment outputs |
|---|---|---|
| `scene` | `u_MVP * pos`, passes color, flat entity ID, `gl_PointSize` | `location 0`: color, `location 1`: entity ID |
| `grid` | world position to fragment | Anti-aliased procedural grid in the XZ plane (`fract` + `fwidth`), axis gap around X/Z axes; entity ID = -1 |
| `text` | NDC passthrough | Samples ImGui's font atlas (`.r` = coverage); entity ID = -1 |

`buildProgram()` checks compile and link status and logs the driver's info log through `anaf::LOG::error`. A failing program leaves an empty handle, and the batch then draws nothing.

### 3.4 Draw order inside `renderSceneOpenGL()`

1. If `dataVersion` changed or `m_meshNeedsUpdate` is set: copy `activeMesh` under `dataMutex`, then `buildSceneBatches()`:
   - Elements become lines colored by `sqrt(|σ| / |σ|_max)` on a blue → green → red ramp. Color shows magnitude only; the sign is visible in the model tree.
   - Nodes (if visible) become points colored by displacement magnitude. The selected node is orange and larger; fixed nodes are red.
   - Applied forces become arrows with a fixed world length of 3 m: a shaft plus a 4-line head, each duplicated as a glow line.
   - Draw position = `location + displacement * deformScale`.
2. `fbo.bind()`, depth test on, `fbo.clear(color, entity = -1)`.
3. `renderGrid()`: blended, depth writes off, spacing `10^floor(log10(max(0.25, distance/12)))`.
4. `render()`:
   - lines at 1.5 px with `GL_LINE_SMOOTH` + alpha blend
   - glow lines at 6 px with additive blend and depth writes off
   - points with `GL_PROGRAM_POINT_SIZE`
5. Node ID labels: glyph quads from ImGui's baked font. Hidden when the camera distance is ≥ 15, except for the selected node.
6. `renderText()`: depth test off, blended.
7. `fbo.unbind()`: bind framebuffer 0 **first**, then `resolve()` blits color and entity ID from MSAA to the resolve FBO (`GL_NEAREST`).

Resolve order matters. On radeonsi, a blit from an MSAA FBO that is still bound as the draw framebuffer misses the last blended draws (found with a pixel-comparison test). `resolve()` is private, so it can only run through `unbind()`.

### 3.5 Camera (`ViewportPanel`)

| Input (viewport hovered) | Action |
|---|---|
| Right drag | Orbit (yaw/pitch, pitch clamped) |
| Middle drag, or Shift + right drag | Pan target |
| Mouse wheel | Zoom (distance × (1 − 0.15 × wheel), clamped 0.5 … 500) |
| `R` or "Reset Camera" button | Fit to mesh bounds (distance = 2.2 × radius) |

Projection: `perspective(45°, aspect, 0.1, 10000)`. View: `lookAt(target + spherical(yaw, pitch, distance), target, +Y)`.

### 3.6 Picking

```text
left click in viewport
   -> mouse position relative to image origin, Y flipped (OpenGL origin bottom-left)
   -> Framebuffer::readEntityID(x, y)      glGetTextureSubImage on the R32I resolve texture
   -> id >= 0 ? bridge.selectedNodeId = id : UINT32_MAX     (under dataMutex)
   -> m_meshNeedsUpdate = true (re-color selection)
```

Only node points write a real entity ID. Lines, grid, and text write -1, so only nodes are pickable, and only while "Nodes: Visible" is on. The ID is the integer `nodeID` stored in the R32I attachment; there is no color encoding. The read is synchronous and stalls the pipeline for one pixel, which is acceptable for click-rate reads.

### 3.7 2D overlay (ImGui draw list)

Drawn by `renderOverlay2D()` on top of the image:
- axis gizmo (camera rotation only)
- "Nodes: Visible/Hidden" toggle
- FPS counter, red below 30 FPS
- stress colorbar (`|Stress| (MPa)`, 0 … max magnitude) and displacement colorbar (`Disp (mm)`)

## 4. Window and platform details

- On Linux, GLFW uses `GLFW_ANY_PLATFORM` (Wayland or X11). The app ID and X11 class are `anafinen`, matching `anafinen.desktop`. `platform_utils::setupSystemCursor()` reads the GNOME cursor theme and size through `gsettings` and exports them as `XCURSOR_THEME` / `XCURSOR_SIZE`.
- On Windows, the `NvOptimusEnablement` / `AmdPowerXpressRequestHighPerformance` exports request the discrete GPU.
- The window icon is loaded with libpng from the first existing asset path (see [BUILD_SYSTEM.md](BUILD_SYSTEM.md) section 7).
- VSync is on (`glfwSwapInterval(1)`).

## 5. Adding a new panel

1. Derive from `IPanel`, implement `onImGuiRender()`, and put the file under `src/gui/panels/`.
2. Add the `.cpp` to `ANAFINEN_SOURCES` in `CMakeLists.txt`.
3. Register it in `openPanels()` with `panelManager.addPanel<T>(...)` and add it to `UIPanels`.
4. Connect it to other panels only through callbacks in `bindAnalysisFlow()`.
5. Read calculation data only through the bridge publication protocol ([BRIDGE.md](BRIDGE.md) section 4).

## 6. Rules for GL code

- All GL objects are owned by a `GlHandle` and created with DSA. No raw `GLuint` ownership.
- GL-owning objects must be destroyed while the context is current. Keep them inside the scope in `initgui()` that ends before `imguiLayer.shutdown()` / `glfwDestroyWindow()`.
- Only the GUI thread may call GL or ImGui.
- Do not use legacy or fixed-function GL (`glBegin`, `glMatrixMode`, client arrays).
- ImGuizmo and ImPlot are linked but not used yet.

## 7. Related source files

- Frame loop and wiring: [src/gui/gui.hpp](../src/gui/gui.hpp), [src/gui/gui.cpp](../src/gui/gui.cpp)
- Infrastructure: [iPanel.hpp](../src/gui/guiMaterials/iPanel.hpp), [imGuiLayer.hpp](../src/gui/guiMaterials/imGuiLayer.hpp), [imGuiLayer.cpp](../src/gui/guiMaterials/imGuiLayer.cpp), [glHandle.hpp](../src/gui/guiMaterials/glHandle.hpp), [framebuffer.hpp](../src/gui/guiMaterials/framebuffer.hpp), [framebuffer.cpp](../src/gui/guiMaterials/framebuffer.cpp)
- Viewport: [viewportPanel.hpp](../src/gui/panels/viewportPanel.hpp), [viewportPanel.cpp](../src/gui/panels/viewportPanel.cpp), [viewportRenderer.hpp](../src/gui/panels/viewportRenderer.hpp), [viewportRenderer.cpp](../src/gui/panels/viewportRenderer.cpp)
- Panels: [mainDockSpaceHost.cpp](../src/gui/panels/mainDockSpaceHost.cpp), [trussControlPanel.cpp](../src/gui/panels/truss/simpleQuadrangleTruss/trussControlPanel.cpp), [trussTypePanel.cpp](../src/gui/panels/truss/trussTypePanel.cpp), [modelTree.cpp](../src/gui/panels/modelTree.cpp), [materialHandler.cpp](../src/gui/panels/materialHandler.cpp), [logTerminal.cpp](../src/gui/panels/logTerminal.cpp)
- Platform: [linuxCursor.hpp](../src/gui/linuxCursor.hpp), [getExecutableDirectory.cpp](../src/directory/getExecutableDirectory.cpp)
