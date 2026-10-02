# GUI and Rendering

This document describes the window, the ImGui panel system, the frame loop, and the OpenGL viewport render pipeline, including entity picking.

> **Document status**
> Verified against: `v0.1.3-alpha` (released 2026-10-01), content checked 2026-10-02.

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
TrussSelector     --onSelected(type)-----------------------> type changed? FileIoPanel.cancelImport(),
                                                              bridge.resetModel(type), both truss panels resetState();
                                                              TrussControlPanel / TrussModelEditor .isOpen by type,
                                                              ModelTree.isOpen
TrussControlPanel --onOpenMaterialHandler()----------------> MaterialHandler.isOpen = true
TrussModelEditor  --onOpenMaterialHandler / onRequestImport-> MaterialHandler.isOpen / FileIoPanel.requestImport()
MainDockSpaceHost --on_import_mesh / on_export_results-----> FileIoPanel.requestImport() / requestExport()
FileIoPanel       --onImported()---------------------------> panels resetState(), TrussModelEditor + ModelTree open,
                                                              ViewportPanel.requestFit()
```

Object type switch: selecting the type that is already active only reopens its panel. Selecting a different type goes through `Gui_Calc_Bridge::resetModel()` ([BRIDGE.md](BRIDGE.md) section 4.1), so no model, fixity, selection, result or panel input of the previous type survives, and a solve still running for it cannot publish.

| Panel | File | Window title | Status |
|---|---|---|---|
| `MainDockSpaceHost` | `panels/mainDockSpaceHost.cpp` | full-screen dockspace + menu bar | File: "Import Mesh / CAD..." (Ctrl+O), "Export Model..." (Ctrl+E), Exit; Analyze: Truss 1D; Help: "About anafinen...". Builds the default dock layout once (left: analysis set and model editor, right: model tree, bottom: console, center: viewport). |
| `ViewportPanel` | `panels/viewportPanel.cpp` | "3D Simulation Viewport" | Camera, picking, overlays, legends |
| `TrussSelector` | `panels/truss/trussTypePanel.cpp` | "Select Truss Type" | "Simple Quadrangle" (generated, export only) or "Imported / Self-Built". Warns that a type change clears the model. |
| `TrussControlPanel` | `panels/truss/simpleQuadrangleTruss/trussControlPanel.cpp` | "Truss(1D) Analysis Set" | Geometry, material, loads, fixity, deform scale, preview/solve/demo/clear, starts the worker. The material combo keeps the stable material ID and resolves it to an index when a job starts (falls back to the first material if the selected one was removed). `resetState()` restores the default inputs. |
| `TrussModelEditor` | `panels/truss/importedTruss/trussModelEditor.cpp` | "Truss(1D) Model Editor" | For `truss_imported_or_entered`, see section 2.4. |
| `ModelTree` | `panels/modelTree.cpp` | "Model Tree" | Boundary conditions, elements over yield (MPa), node displacements (mm) |
| `MaterialHandler` | `panels/materialHandler.cpp` | "Material Handler" (floating, not dockable) | Table of all materials (E, G, K in GPa; yield / ultimate in MPa; density; ν; ductility in %). Form to add a user material (engineering units, converted to SI); user materials are saved to the user config directory. "Delete" only on user materials; refusals (in use, worker running) are shown in the panel. See [BRIDGE.md](BRIDGE.md) section 5.1. |
| `LogTerminal` | `panels/logTerminal.cpp` | "Console" | Colored log view (max 10,000 lines, trimmed under the log mutex). "Wrap lines" (default on) wraps at the panel width; off gives one row per entry and a horizontal scrollbar. |
| `StatusBar` (component, not a panel) | `panels/statusBar.cpp` | footer of "Console" | One line under the log, so it spans the console's width. Left: worker state (IDLE / SOLVING n% / PREVIEW) and, right next to it, a hardware summary built once on the first frame (`Ryzen 7 7735HS 16T  \|  Radeon 680M 2 GB  \|  13.3 GB RAM`, also logged at startup). Right: process CPU %, process memory, system RAM % from `PLATFORM::ResourceMonitor`, sampled every 500 ms. GPU name: `GL_RENDERER` shortened; VRAM: `GL_NVX_gpu_memory_info` (NVIDIA, Mesa), else amdgpu sysfs (Linux) or DXGI (Windows). |
| `AboutPanel` | `panels/aboutPanel.cpp` | "About anafinen" (modal) | GPLv3 "Appropriate Legal Notices": version, copyright, no-warranty text, full `LICENSE` and `THIRD_PARTY_LICENSES.md` (read from the exe folder, `/usr/share/doc/anafinen` or the source tree). The same notice is logged at startup (`main.cpp`). |
| `FileIoPanel` | `panels/fileIoPanel.cpp` | none (popups + bottom-right overlay) | Import / export: native file chooser, CAD and export option dialogs, progress bar with Cancel, result notices. Always "open"; draws only while needed. |

### 2.1 ImGui layer (`guiMaterials/imGuiLayer.*`)

- Config flags: keyboard navigation and docking enabled.
- Fonts: `Inter-Medium.ttf` for the UI and `CascadiaMono.ttf` for the console, both 18 px. Falls back to the ImGui default font when the files are missing.
- Custom theme: `setupSpecialTheme()`.
- OpenGL backend initialized with `#version 460`.

### 2.2 Log sink

`main()` installs `anafUILogSink()` with `anaf::LOG::setCallback()` before the GUI exists, so startup lines are kept. The sink appends to `g_ui_logs` under `g_log_mutex` and drops the oldest entry above `g_ui_log_max_num` ("Max Output" in the console). Worker threads can therefore log safely, and the console panel reads the buffer on the GUI thread.

### 2.3 File import / export (`FileIoPanel`)

```text
File > Import Mesh / CAD... (Ctrl+O)
  -> NativeFileDialog::openFile     OS chooser; ready() polled each frame (returns in ~0.2 ms)
  -> CAD file? -> "CAD Import Options" modal: bars / surfaces / volumes, element size, order
  -> IoService::runAsync: readMesh + ADAPTER::toMeshData       (I/O thread)
  -> overlay: description, progress bar, stage, Cancel
  -> done: model reset meanwhile (modelGeneration)? discard. Else resetModel(truss_imported_or_entered),
           activeMesh, dataVersion++ (GUI thread, under dataMutex); log notes / warnings;
           model editor and tree open, camera fits

File > Export Model... (Ctrl+E)
  -> "Export Model" modal: MSH 4.1 / MSH 2.2 / VTU / VTK 5.1 / VTK 4.2 / STEP, binary, zlib
  -> NativeFileDialog::saveFile (extension added when missing)
  -> snapshot pointer + fixity copied under dataMutex
  -> IoService::runAsync: ADAPTER::toMeshModel + writeMesh     (I/O thread)
```

- **Native dialogs:** `portable-file-dialogs` behind `fileDialogs/nativeFileDialog.*`. It is the only translation unit that includes the header. On Linux it runs `zenity` / `kdialog` as a child process; closing the application kills an open chooser. When no backend exists the panel reports it instead of failing.
- **Blocking during a calculation:** import is refused while the solver or preview worker runs, so the worker cannot overwrite the imported snapshot.
- **Object type:** an import always switches to `truss_imported_or_entered` (from `no_type` or `truss_SQPT` too) and opens the model editor. An import still running when the model is reset (type change, Clear) is discarded: `FileIoPanel` compares `modelGeneration` with the value taken at start.
- **Solving imported models:** bars are solved in the model editor (section 2.4). Surface / volume meshes are shown as wireframe edges (`RenderElement::isWireframe`) and are never solved.

### 2.4 Model editor (`TrussModelEditor`)

```text
+-- Truss(1D) Model Editor ------------------------------+
| [Import File...]                                       |
| Nodes / Bars / wireframe edges / loads / supports      |
| results state + last edit message                      |
|-- Built-in Models (collapsed) ------------------------|
| combo grouped by category, description                |
| [Load Built-in Model]   (read-only; copy in memory)   |
|-- Whole Model: Section & Material --------------------|
| Material, Area [cm^2], [ ] also wireframe edges       |
| [Apply to Whole Model]                                |
|-- Nodes ----------------------------------------------|
| Position [m] x y z            [Add Node]              |
| Selected node (viewport click or typed id)            |
| Position [m] x y z  [Move Node] [Delete Node]         |
|-- Bars -----------------------------------------------|
| Material, [Open Material Handler], Area [cm^2]        |
| Node A - B                    [Add Bar]               |
| bar list (clipped, click to select)                   |
| [Apply to Selected] [Delete Bar]                      |
|-- Supports & Loads (selected node) -------------------|
| (o) Global axes  ( ) Inclined / skewed                |
|   axes:     Fix X / Y / Z                             |
|   inclined: Restrained / Allowed motion, Vectors 1 2 3|
|             d1..d3 x y z, resulting line / plane      |
|                               [Apply Support]         |
| Force [N] x y z   [Apply Load] [Remove Load]          |
|-------------------------------------------------------|
| Deformation Scale, [Run Solver for Truss], progress   |
| [Clear Model]                                         |
+-------------------------------------------------------+
```

1. Every edit copies the active snapshot (an empty one if there is none), changes it, drops stale results and publishes it ([BRIDGE.md](BRIDGE.md) section 5). Editing is disabled while a worker runs, because the solve result would overwrite the edit.
2. Node ids stay `0..n-1`. "Delete Node" removes the node's bars, load and support and moves later ids down by one.
3. "Add Bar" refuses missing or identical nodes, coincident positions, an existing bar between the same nodes, a missing material or an area ≤ 0. The next bar starts at the last end node, so chains are quick to enter.
4. "Apply to Whole Model" sets one material and area on every bar in a single edit; use it after importing a file without a `CrossSectionArea` attribute. With "also turn the wireframe edges into bars", the edges of an imported surface mesh become bars too (e.g. a triangulated shell becomes a space truss).
5. "Built-in Models" lists the library from `assets/objects/truss/truss1D/index.json` ([CALCULATIONS.md](CALCULATIONS.md) section 3.2). "Load Built-in Model" imports the file through `FileIoPanel::importFile()` like File > Import; the file itself is never written, and File > Export refuses a target inside the library folder.
6. "Run Solver for Truss" solves the snapshot with `Truss_Imported_or_Entered` ([CALCULATIONS.md](CALCULATIONS.md) section 3.1). If the model cannot be solved, the reason is logged as "Solver not started: ...".
7. "Clear Model" calls `resetModel(truss_imported_or_entered)` and `resetState()`.
8. Supports: "Global axes" fixes x / y / z (`Node::setMovable`). "Inclined / skewed" takes 1 to 3 direction vectors, read as the restrained directions (1 = roller on a plane, 2 = guide along a line, 3 = pin) or as the allowed motion (1 = line, 2 = plane); `FEM::TRUSS::orthonormalize()` / `orthogonalComplement()` turn them into the allowed-motion basis for `Node::setAllowedMotionDirections()`. Switching between the two readings replaces the vectors by their complement, so the support stays the same. Dependent or zero vectors disable "Apply Support". The support is stored on the node only (red point, model tree, export and the solve read it there). The SQPT control panel keeps its X / Y / Z checkboxes; its supports are panel input put on every grid it builds.

## 3. Viewport render pipeline

### 3.1 Resources

| Object | Class | GL objects |
|---|---|---|
| Offscreen target | `Framebuffer` (`guiMaterials/framebuffer.*`) | MSAA FBO: RGBA8 + R32I + D24S8 renderbuffers. Resolve FBO: RGBA8 + R32I textures (immutable storage). |
| Batches | `ViewportRenderer` (`panels/viewportRenderer.*`) | 6 VAO/VBO pairs: grid, lines, glow lines, translucent triangles, points, text |
| Programs | `ViewportRenderer` | `scene` (lines/points), `grid`, `text` |

Every GL object is owned by a move-only `GlHandle` (`guiMaterials/glHandle.hpp`) and created through DSA (`glCreate*`, `glNamed*`, `glVertexArray*`). No `glGen*` or bind-to-edit.

### 3.2 Vertex layouts (binding 0 for every VAO)

| Batch | Struct | loc 0 | loc 1 | loc 2 | loc 3 |
|---|---|---|---|---|---|
| lines, glow lines, triangles | `Vertex3D` | vec3 position | vec4 color | int entityID (`IFormat`) | - |
| points | `Point3D` | vec3 position | vec4 color | int entityID | float size |
| text | `TextVertex` | vec2 NDC position | vec2 uv | vec4 color | - |
| grid | `glm::vec3` | vec3 NDC position (fullscreen triangle) | - | - | - |

Dynamic batches are re-uploaded with `glNamedBufferData(..., GL_DYNAMIC_DRAW)` (orphaning) only when the mesh or visibility changes. The grid's fullscreen triangle uses immutable `glNamedBufferStorage`.

### 3.3 Shaders (inline raw strings, `#version 460 core`)

| Program | Vertex | Fragment outputs |
|---|---|---|
| `scene` | `u_MVP * pos`, passes color, flat entity ID, `gl_PointSize` | `location 0`: color, `location 1`: entity ID |
| `grid` | view ray per vertex (`forward + x·right + y·up`) | Ray / y = 0 plane intersection per pixel, in coordinates relative to a grid-aligned origin near the eye; anti-aliased minor + major (×10) lines (`fract` + `fwidth`) that fade out once a cell is a few pixels wide (no moiré) and towards `fadeDistance`; axis gap around X/Z axes; entity ID = -1 |
| `text` | NDC passthrough | Samples ImGui's font atlas (RGBA32, `.a` = coverage); entity ID = -1 |

`buildProgram()` checks compile and link status and logs the driver's info log through `anaf::LOG::error`. A failing program leaves an empty handle, and the batch then draws nothing.

### 3.4 Draw order inside `renderSceneOpenGL()`

1. If `dataVersion` changed or `m_meshNeedsUpdate` is set: copy `activeMesh` under `dataMutex`, then `buildSceneBatches()`:
   - Elements become lines colored by `sqrt(|σ| / |σ|_max)` on a blue → green → red ramp. Color shows magnitude only; the sign is visible in the model tree.
   - Nodes (if visible) become points colored by displacement magnitude. The selected node is orange and larger; fixed nodes are red.
   - Inclined supports (`Node::hasInclinedSupport()`), drawn red at the drawn node position with a size of 4 % of the scene radius: an allowed plane as a translucent square (`addTriangle()`, blended after the lines without depth writes) with an outline, an allowed line as a double arrow with a glow line.
   - Applied forces become arrows with a fixed world length of 3 m: a shaft plus a 4-line head, each duplicated as a glow line.
   - Draw position = `location + displacement * deformScale`, with `deformScale` read from the bridge (`Gui_Calc_Bridge::deformScale`, a view setting) when the snapshot is reloaded.
2. `fbo.bind()`, depth test on, `fbo.clear(color, entity = -1)`.
3. `renderGrid(GridView)`: blended, depth writes off, minor spacing `10^floor(log10(distance/12))`, fade distance `max(40 × distance, 6 × scene radius)`. The grid used to be one ±8000 m quad; close to the camera its clipped, interpolated world positions lost precision and the lines bent and swam.
4. `render()`:
   - lines at 1.5 px with `GL_LINE_SMOOTH` + alpha blend
   - glow lines at 6 px with additive blend and depth writes off
   - translucent triangles with alpha blend and depth writes off
   - points with `GL_PROGRAM_POINT_SIZE`
5. Node ID labels: glyph quads from ImGui's baked font. Hidden when the camera distance is ≥ 15, except for the selected node.
6. `renderText()`: depth test off, blended.
7. `fbo.unbind()`: bind framebuffer 0 **first**, then `resolve()` blits color and entity ID from MSAA to the resolve FBO (`GL_NEAREST`).

Resolve order matters. On radeonsi, a blit from an MSAA FBO that is still bound as the draw framebuffer misses the last blended draws (found with a pixel-comparison test). `resolve()` is private, so it can only run through `unbind()`.

### 3.5 Camera (`ViewportPanel`)

| Input (viewport hovered) | Action |
|---|---|
| Right drag | Orbit (yaw/pitch, no pitch limit: the camera passes over the poles and turns upside down) |
| Middle drag, or Shift + right drag | Pan target |
| Mouse wheel | Zoom (distance × (1 − 0.15 × wheel), from 0.001 to max(2000, 50 × scene radius)) |
| `R` or "Reset Camera" button | Fit to mesh bounds (distance = 2.2 × radius) |

View: `lookAt(target + orbitDirection() × distance, target, orbitUp())`. `orbitUp()` is −∂(orbitDirection)/∂pitch, so it is +Y at zero pitch and stays perpendicular to the view direction at any pitch (no gimbal flip at ±90°). Upside down, the yaw drag is mirrored so the view still follows the mouse.

Projection: `perspective(45°, aspect, near, far)` with `far = 2 × (distance + |target − scene centre| + scene radius)` and `near = max(0.005 × distance, 1e-6 × far)`, so the depth range follows the zoom from millimetre parts to 300 m stadiums. The axis lines reach `max(8000, 200 × scene radius)`, beyond the largest far plane. Scene centre and radius are recomputed (`updateSceneBounds()`) whenever a new snapshot is drawn.

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
- Panels: [statusBar.cpp](../src/gui/panels/statusBar.cpp), [mainDockSpaceHost.cpp](../src/gui/panels/mainDockSpaceHost.cpp), [trussControlPanel.cpp](../src/gui/panels/truss/simpleQuadrangleTruss/trussControlPanel.cpp), [trussModelEditor.cpp](../src/gui/panels/truss/importedTruss/trussModelEditor.cpp), [trussWorker.hpp](../src/gui/panels/truss/trussWorker.hpp), [trussTypePanel.cpp](../src/gui/panels/truss/trussTypePanel.cpp), [modelTree.cpp](../src/gui/panels/modelTree.cpp), [materialHandler.cpp](../src/gui/panels/materialHandler.cpp), [logTerminal.cpp](../src/gui/panels/logTerminal.cpp)
- Platform: [linuxCursor.hpp](../src/gui/linuxCursor.hpp), [getExecutableDirectory.cpp](../src/directory/getExecutableDirectory.cpp)
