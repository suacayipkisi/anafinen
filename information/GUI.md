# GUI and Rendering

This document describes the window, the ImGui panel system, the frame loop, and the OpenGL viewport render pipeline, including entity picking.

> **Document status**
> Verified against: `v0.2.0-alpha` (released 2026-10-05; previous release `v0.1.3-alpha`, 2026-10-01), content checked 2026-10-05 (v0.2.0-alpha release: the selector lists "Dynamic Load (not available yet)" and the dynamic Analysis tab warns that no dynamic solver exists, section 2; support symbols: toolbar "Supports: Off / Symbols / DOF", off by default, textbook symbols by support type or CAD style cones per restrained DOF, sections 3.4, 3.7 and 3.9; end releases drawn as pins along the released bending axes and collars for released torsion instead of balls, section 3.9; 2026-10-04: node squares depth tested with an eye-ward lift, hidden by members in front, section 3.4; editor layout: the three left editors share a summary card, Model / Supports & Loads / Analysis tabs and a fixed footer with the run button, `panels/editorLayout.hpp`, section 2.4; analysis selector: "Select Analysis" asks for the load kind (constant / dynamic) after Analyze > Truss or Beam; dynamic shows the dynamic inputs in the Analysis tab instead of the static loads and solve, section 2; end releases: Frame Editor "End Releases (Hinges)" table and presets, hinge markers in the viewport, hinged end rotations in the drawn shape, sections 2.5 and 3.9; Frame Editor "Built-in Models" (49 beam models, model or solved results), export refused in both built-in library folders; beam rendering: real sections with rotations, node squares / spheres, coloring modes, element picking, level of detail, sections 3.1-3.9; beam panels: Beam(3D) Frame Editor with inclined supports, Section Handler, Beam Diagrams (ImPlot) under the Model Tree, beam import / export, sections 2.3 and 2.5-2.7; support editor uses `FEM::SUPPORT`; window icon: 128 px + 32 px; StartupNotify=false; viewport toolbar: Reset Camera, Grid, Axes, Nodes, Forces, Stress).

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
MainDockSpaceHost --on_select_truss / on_select_beam()-----> AnalysisSelector.open(truss / beam)
AnalysisSelector  --onTrussSelected(type, loadKind)--------> bridge.m_loadKind = loadKind;
                                                              type changed? FileIoPanel.cancelImport(),
                                                              bridge.resetModel(type), both truss panels resetState();
                                                              TrussControlPanel / TrussModelEditor .isOpen by type,
                                                              ModelTree.isOpen
TrussControlPanel --onOpenMaterialHandler()----------------> MaterialHandler.isOpen = true
TrussModelEditor  --onOpenMaterialHandler / onRequestImport-> MaterialHandler.isOpen / FileIoPanel.requestImport()
MainDockSpaceHost --on_import_mesh / on_export_results-----> FileIoPanel.requestImport() / requestExport()
FileIoPanel       --onImported()---------------------------> panels resetState(), TrussModelEditor + ModelTree open,
                                                              beam panels closed, ViewportPanel.requestFit()
AnalysisSelector  --onBeamSelected(loadKind)---------------> bridge.m_loadKind = loadKind;
                                                              type changed? FileIoPanel.cancelImport(),
                                                              resetModel(beam_frame), panels resetState();
                                                              BeamModelEditor + ModelTree open, BeamDiagramPanel
                                                              open for constant loads only, truss panels closed
BeamModelEditor   --onOpenMaterialHandler / onOpenSectionHandler-> MaterialHandler / SectionHandler .isOpen = true
FileIoPanel       --onImportedBeam()-----------------------> panels resetState(), beam panels + ModelTree open
```

Load kind (`BRIDGE::LoadKind`, `bridge.m_loadKind`): "Constant Load (Static)" or "Dynamic Load (not available yet)", picked in the selector together with the type. v0.2.0-alpha has no dynamic solver: the dynamic kind only shows the inputs that modal analysis will use. It only changes what the editors show, so changing it alone keeps the model (the model is the same for both; modal analysis needs no loads). The three editors read it every frame:

| Panel | Constant | Dynamic |
|---|---|---|
| `TrussControlPanel` | "Supports & Loads" tab with the force input; footer: deformation scale, Run Solver for Truss | "Supports" tab (fixity only); Analysis tab with the dynamic inputs; footer: Run Modal Analysis (disabled) |
| `TrussModelEditor` | "Supports & Loads" tab; footer: deformation scale, Run Solver | "Supports" tab (no force input); dynamic inputs; Run Modal Analysis (disabled) |
| `BeamModelEditor` | "Supports & Loads" tab with nodal loads, distributed loads and self weight; footer: deformation scale, Run Solver | "Supports" tab (no loads); dynamic inputs; Run Modal Analysis (disabled) |

The Analysis tab (`renderAnalysisTab()` in `panels/dynamicAnalysisInputs.hpp`, one `DynamicAnalysisInputs` per editor) names the load kind; for constant loads it says what the static solve uses, for dynamic loads it has the analysis type (only Modal selectable; Harmonic and Transient listed disabled), number of modes and mass matrix (consistent / lumped). Nothing reads these yet: there is no dynamic solver in v0.2.0-alpha (modal analysis comes next), so the tab starts with a warning in `LAYOUT::kWarn` ("Not available in this version ... use Constant Load (Static) to solve the model") and the run button stays disabled with the tooltip "Not available yet". Loads entered in the constant mode stay in the model and the panel inputs, only hidden.

Object type switch: selecting the type that is already active only reopens its panel. Selecting a different type goes through `Gui_Calc_Bridge::resetModel()` ([BRIDGE.md](BRIDGE.md) section 4.1), so no model, fixity, selection, result or panel input of the previous type survives, and a solve still running for it cannot publish.

| Panel | File | Window title | Status |
|---|---|---|---|
| `MainDockSpaceHost` | `panels/mainDockSpaceHost.cpp` | full-screen dockspace + menu bar | File: "Import Mesh / CAD..." (Ctrl+O), "Export Model..." (Ctrl+E), Exit; Analyze: Truss 1D, Beam / Frame 3D; Panels: reopen closed panels (section 2.0); Help: "About anafinen...". Builds the default dock layout once (left: analysis set, truss model editor and beam frame editor, right: model tree, bottom: console, center: viewport with the viewport toolbar strip above it). |
| `ViewportToolbar` | `panels/viewportToolbar.cpp` | "Viewport Toolbar" | Reset Camera and the display toggles, docked above the viewport (section 3.7) |
| `ViewportPanel` | `panels/viewportPanel.cpp` | "3D Simulation Viewport" | Camera, picking (nodes, beam elements), overlays, legends; truss as lines, beams with their sections (section 3.9) |
| `AnalysisSelector` | `panels/analysisSelector.cpp` | "Select Analysis" | Opened by Analyze > Truss / Beam (`open(StructureFamily)`). Truss: "Imported / Self-Built" (first, preselected) or "Simple Quadrangle" (generated grid). Both: load type "Constant Load (Static)" or "Dynamic Load (not available yet)" (preselects the active one; the dynamic choice shows "Not available in this version." in `LAYOUT::kWarn`). Warns that a type change clears the model. |
| `TrussControlPanel` | `panels/truss/simpleQuadrangleTruss/trussControlPanel.cpp` | "Truss(1D) Analysis Set" | Layout of section 2.4: summary card (grid, bars, supports, loads, result state); tabs Grid (cells, edge, material, area, Generate Preview, Load Demo), Supports & Loads (node, fixity, force), Analysis; footer deformation scale, Run Solver, Clear All. Starts the worker. The material combo keeps the stable material ID and resolves it to an index when a job starts (falls back to the first material if the selected one was removed). `resetState()` restores the default inputs. |
| `TrussModelEditor` | `panels/truss/importedTruss/trussModelEditor.cpp` | "Truss(1D) Model Editor" | For `truss_imported_or_entered`, see section 2.4. |
| `BeamModelEditor` | `panels/beam/beamModelEditor.cpp` | "Beam(3D) Frame Editor" | For `beam_frame`, see section 2.5. |
| `SectionHandler` | `panels/beam/sectionHandler.cpp` | "Section Handler" (floating, not dockable) | Beam sections, see section 2.6. |
| `BeamDiagramPanel` | `panels/beam/beamDiagramPanel.cpp` | "Beam Diagrams" | ImPlot diagrams of one beam element, docked under the model tree, see section 2.7. |
| `ModelTree` | `panels/modelTree.cpp` | "Model Tree" | Truss: boundary conditions, elements over yield (MPa), node displacements (mm). Beam (`renderBeamTree`): supports (free translations / rotations), nodal and distributed loads, self weight, max von Mises per element (red over yield), node displacement (mm) and rotation (mrad). Every list is drawn with `ImGuiListClipper` (only visible rows); the filtered row indices (supported nodes, bars over yield) are rebuilt only when the snapshot pointer changes. |
| `MaterialHandler` | `panels/materialHandler.cpp` | "Material Handler" (floating, not dockable) | Table of all materials (E, G, K in GPa; yield / ultimate in MPa; density; ν; ductility in %). Form to add a user material (engineering units, converted to SI); user materials are saved to the user config directory. "Delete" only on user materials; refusals (in use, worker running) are shown in the panel. See [BRIDGE.md](BRIDGE.md) section 5.1. |
| `LogTerminal` | `panels/logTerminal.cpp` | "Console" | Colored log view (max 10,000 lines, trimmed under the log mutex). "Wrap lines" (default on) wraps at the panel width; off gives one row per entry and a horizontal scrollbar. With wrapping off the rows go through `ImGuiListClipper`; wrapped rows differ in height, so then every line is laid out. |
| `StatusBar` (component, not a panel) | `panels/statusBar.cpp` | footer of "Console" | One line under the log, so it spans the console's width. Left: worker state (IDLE / SOLVING n% / PREVIEW) and, right next to it, a hardware summary built once on the first frame (`Ryzen 7 7735HS 16T  \|  Radeon 680M 2 GB  \|  13.3 GB RAM`, also logged at startup). Right: process CPU %, process memory, system RAM % from `PLATFORM::ResourceMonitor`, sampled every 500 ms (system RAM through `PLATFORM::queryMemory()`, the same reading the solver referee logs). GPU name: `GL_RENDERER` shortened; VRAM: `GL_NVX_gpu_memory_info` (NVIDIA, Mesa), else amdgpu sysfs (Linux) or DXGI (Windows). |
| `AboutPanel` | `panels/aboutPanel.cpp` | "About anafinen" (modal) | GPLv3 "Appropriate Legal Notices": version, copyright, no-warranty text, full `LICENSE` and `THIRD_PARTY_LICENSES.md` (read from the exe folder, `/usr/share/doc/anafinen` or the source tree). The same notice is logged at startup (`main.cpp`). |
| `FileIoPanel` | `panels/fileIoPanel.cpp` | none (popups + bottom-right overlay) | Import / export: native file chooser, CAD and export option dialogs, progress bar with Cancel, result notices. Always "open"; draws only while needed. |

### 2.0 Panels menu

The menu bar's "Panels" menu reopens (or closes) the panels that have a close button. `gui.cpp` `bindAnalysisFlow()` registers them with `MainDockSpaceHost::addPanelMenuEntry()`; each entry points at the panel's `isOpen` (checkmark = open) and may carry an availability check on `bridge.m_objectType`, so only the panels of the active analysis can be opened:

| Entry | Available when | Disabled hint |
|---|---|---|
| 3D Simulation Viewport | always | - |
| Truss(1D) Analysis Set | `truss_SQPT` | Simple Quadrangle only |
| Truss(1D) Model Editor | `truss_imported_or_entered` | imported / self-built only |
| Beam(3D) Frame Editor | `beam_frame` | beam / frame only |
| Beam Diagrams | `beam_frame` | beam / frame only |
| Section Handler | `beam_frame` | beam / frame only |
| Material Handler | any type selected | select an analysis first |
| Model Tree | any type selected | select an analysis first |
| Console | always | - |

The type selector is reopened through Analyze. The status bar is the console's footer, so it is hidden while the console is closed. The "Viewport Toolbar" draws only while the viewport is open (`ViewportToolbar` holds a pointer to the viewport panel), so it closes and reopens with it.

### 2.1 ImGui layer (`guiMaterials/imGuiLayer.*`)

- Config flags: keyboard navigation and docking enabled.
- Fonts: `Inter-Medium.ttf` for the UI and `CascadiaMono.ttf` for the console, both 18 px. Falls back to the ImGui default font when the files are missing.
- Custom theme: `setupSpecialTheme()`.
- OpenGL backend initialized with `#version 460`.
- ImPlot context created right after the ImGui context and destroyed before it (beam diagrams).

### 2.2 Log sink

`main()` installs `anafUILogSink()` with `anaf::LOG::setCallback()` before the GUI exists, so startup lines are kept. The sink appends to `g_ui_logs` (a `std::deque`, so dropping the oldest line is O(1)) under `g_log_mutex` and drops the oldest entry above `g_ui_log_max_num` ("Max Output" in the console). Worker threads can therefore log safely, and the console panel reads the buffer on the GUI thread.

### 2.3 File import / export (`FileIoPanel`)

```text
File > Import Mesh / CAD... (Ctrl+O)
  -> NativeFileDialog::openFile     OS chooser; ready() polled each frame (returns in ~0.2 ms)
  -> CAD file? -> "CAD Import Options" modal: bars / surfaces / volumes, element size, order
  -> IoService::runAsync: readMesh, then                       (I/O thread)
       BEAM::ADAPTER::isBeamModel()? BEAM::ADAPTER::toMeshData (materials + sections copied at start)
                                   : TRUSS::ADAPTER::toMeshData
  -> overlay: description, progress bar, stage, Cancel
  -> done: model reset meanwhile (modelGeneration)? discard.
     truss: resetModel(truss_imported_or_entered), activeMesh, dataVersion++; model editor and tree open, camera fits
     beam:  section list changed meanwhile? discard. Else resetModel(beam_frame), the file's new sections
            added as user sections (addUserSection), activeBeamMesh, dataVersion++; beam panels open
     both:  notes / warnings logged

File > Export Model... (Ctrl+E)
  -> "Export Model" modal: MSH 4.1 / MSH 2.2 / VTU / VTK 5.1 / VTK 4.2 / STEP, binary, zlib
  -> NativeFileDialog::saveFile (extension added when missing)
  -> snapshot pointer (truss or beam), materials and sections copied under dataMutex
  -> IoService::runAsync: TRUSS / BEAM ADAPTER::toMeshModel + writeMesh   (I/O thread);
     the beam adapter's model.warnings (inclined rotation supports) go into the WriteReport warnings
```

- **Native dialogs:** `portable-file-dialogs` behind `fileDialogs/nativeFileDialog.*`. It is the only translation unit that includes the header. On Linux it runs `zenity` / `kdialog` as a child process; closing the application kills an open chooser. When no backend exists the panel reports it instead of failing.
- **Blocking during a calculation:** import is refused while the solver or preview worker runs, so the worker cannot overwrite the imported snapshot.
- **Object type:** a file with beam elements (`ElementFormulation` 1 / 2) becomes a `beam_frame` model; every other file switches to `truss_imported_or_entered` (from any type) and opens the truss model editor. An import still running when the model is reset (type change, Clear) is discarded: `FileIoPanel` compares `modelGeneration` with the value taken at start.
- **Solving imported models:** bars are solved in the model editor (section 2.4). Surface / volume meshes are shown as wireframe edges (`RenderElement::isWireframe`) and are never solved.

### 2.4 Editor layout and the truss model editor (`TrussModelEditor`)

The three editors docked on the left (`TrussControlPanel`, `TrussModelEditor`, `BeamModelEditor`) share one layout, built from `panels/editorLayout.hpp` (`anaf::GUI::LAYOUT`):

| Part | Helper | Content |
|---|---|---|
| Summary card | `beginCard()` / `endCard()`, `beginStats()` + `stat()` | Bordered child sized to its content: a four-column grid of counts, the result state (green: solved, energy check passed), the last edit message |
| Tab bar | ImGui tab bar | Model (Grid for the SQPT panel) / Supports & Loads (Supports for dynamic loads, same tab through a `###` ID) / Analysis |
| Tab body | `beginBody()` / `endBody()` | Scrolling child that ends where the footer starts, so long tabs never push the run button off screen |
| Footer | `footerHeight()`, `primaryButton()` | Deformation scale, progress bar while solving, the accent-colored run button, model buttons (two equal halves via `splitWidth()`) |
| Fields | `field()` | Label on the left at a fixed column (7 em), the widget fills the rest; every input row lines up |

Built-in model descriptions sit in a framed box of at most five lines that scrolls. The footer height is computed from its rows each frame (one more while the progress bar shows).

```text
+-- Truss(1D) Model Editor ------------------------------+
| +- summary --------------------------------------------+|
| | Nodes  n   Bars n  Supports n  Loads n               ||
| | wireframe edges (if any); results state; last edit   ||
| +------------------------------------------------------+|
| [ Model ][ Supports & Loads ][ Analysis ]              |
| Model:                                                 |
|   Built-in Models (collapsed): combo, description box, |
|     [Load Built-in Model] (read-only; copy in memory)  |
|   Nodes: New [m] x y z [Add Node]; Selected (viewport  |
|     click or id); Position [m]; [Move Node][Delete Node]|
|   Bars: Material, [Open Material Handler], Area [cm^2],|
|     Node A - B, [Add Bar], bar list (clipped),         |
|     [Apply to Selected][Delete Bar]                    |
|   Whole Model (collapsed): Material, Area, [ ] also    |
|     wireframe edges, [Apply to Whole Model]            |
| Supports & Loads (selected node):                      |
|   (o) Global axes ( ) Inclined / skewed                |
|     axes: Fix X / Y / Z | inclined: Restrained /       |
|     Allowed motion, Vectors 1 2 3, d1..d3, line / plane|
|   [Apply Support]; Nodal Load: Force [N] x y z,        |
|   [Apply Load][Remove Load]                            |
| Analysis: load kind, static note or dynamic inputs     |
|--------------------------------------------------------|
| Deformation [scale]                                    |
| [######## Run Solver for Truss ########], progress     |
| [Import File...] [Clear Model]                         |
+--------------------------------------------------------+
```

1. Every edit copies the active snapshot (an empty one if there is none), changes it, drops stale results and publishes it ([BRIDGE.md](BRIDGE.md) section 5). Editing is disabled while a worker runs, because the solve result would overwrite the edit.
2. Node ids stay `0..n-1`. "Delete Node" removes the node's bars, load and support and moves later ids down by one.
3. "Add Bar" refuses missing or identical nodes, coincident positions, an existing bar between the same nodes, a missing material or an area ≤ 0. The next bar starts at the last end node, so chains are quick to enter.
4. "Apply to Whole Model" sets one material and area on every bar in a single edit; use it after importing a file without a `CrossSectionArea` attribute. With "also turn the wireframe edges into bars", the edges of an imported surface mesh become bars too (e.g. a triangulated shell becomes a space truss).
5. "Built-in Models" lists the library from `assets/objects/truss/truss1D/index.json` ([CALCULATIONS.md](CALCULATIONS.md) section 3.2). "Load Built-in Model" imports the file through `FileIoPanel::importFile()` like File > Import; the file itself is never written, and File > Export refuses a target inside the library folder.
6. "Run Solver for Truss" solves the snapshot with `FEM::TRUSS::solveStatic()` through `TRUSS_WORKER::startSolve()` ([CALCULATIONS.md](CALCULATIONS.md) section 3.1). If the model cannot be solved, the reason is logged as "Solver failed: ...".
7. "Clear Model" calls `resetModel(truss_imported_or_entered)` and `resetState()`.
8. Supports: "Global axes" fixes x / y / z (`Node::setMovable`). "Inclined / skewed" takes 1 to 3 direction vectors, read as the restrained directions (1 = roller on a plane, 2 = guide along a line, 3 = pin) or as the allowed motion (1 = line, 2 = plane); `FEM::SUPPORT::orthonormalize()` / `orthogonalComplement()` turn them into the allowed-motion basis for `Node::setAllowedMotionDirections()`. Switching between the two readings replaces the vectors by their complement, so the support stays the same. Dependent or zero vectors disable "Apply Support". The support is stored on the node only (red point, model tree, export and the solve read it there). The SQPT control panel keeps its X / Y / Z checkboxes; its supports are panel input put on every grid it builds.

### 2.5 Beam frame editor (`BeamModelEditor`)

Built-in Models (first collapsing header of the Model tab): a combo grouped by category from `assets/objects/beam/beam3D/index.json`, the model description (scrolling box), and two buttons. "Load Model" imports `<id>.msh`, "Load Solved" imports `<id>_solved.msh`; both call `onLoadBuiltin`, which `gui.cpp` binds to `FileIoPanel::importFile`. File > Export refuses targets inside the truss or beam library folders ([CALCULATIONS_BEAM.md](CALCULATIONS_BEAM.md) section 12).

Layout as in section 2.4 (summary card, tabs, fixed footer):

```text
+-- Beam(3D) Frame Editor -------------------------------+
| +- summary --------------------------------------------+|
| | Nodes n  Elements n  Supports n  Self weight on/off  ||
| | Nodal loads n  Line loads n                          ||
| | energy check, max displacement, max von Mises        ||
| | (element), elements over yield, last edit message    ||
| +------------------------------------------------------+|
| [ Model ][ Supports & Loads ][ Analysis ]              |
| Model:                                                 |
|   Built-in Models (collapsed): combo, description box, |
|     [Load Model] [Load Solved]                         |
|   Nodes: New [m] [Add Node]; Selected (viewport click  |
|     or id); Position [m]; [Move Node] [Delete Node]    |
|   Elements: Material, Section, [Materials...]          |
|     [Sections...], Formulation, Orientation v,         |
|     End Releases (Hinges) table + presets;             |
|     Add / Edit: Node A - B, [Add Element], element list|
|     (section, EB/TI, hinges, max von Mises; red over   |
|     yield), [Apply to Selected] [Delete Element]       |
|   Whole Model (collapsed): formulation, material and   |
|     section for all, [Remove All End Releases]         |
| Supports & Loads:                                      |
|   Supports & Nodal Loads (selected node): presets      |
|     Fixed / Pinned / Free; Translation and Rotation:   |
|     global axes or inclined vectors; [Apply Support];  |
|     Nodal Load: Force [N], Moment [N m],               |
|     [Apply Load] [Remove Load]                         |
|   Distributed Loads & Self Weight: [x] self weight;    |
|     loads of the selected element, q [N/m], Axes,      |
|     [Add Load] [Remove Loads]                          |
| Analysis: load kind, static note or dynamic inputs     |
|--------------------------------------------------------|
| Deformation [scale] [Auto]                             |
| [######## Run Solver for Beam ########], progress      |
| [Load Example Frame] [Clear Model]                     |
+--------------------------------------------------------+
```

0. "Deformation" (footer, above Run Solver) sets `bridge.deformScale`; "Auto" picks the scale that draws the largest nodal displacement as 5 % of the model's bounding-box diagonal.
1. Every edit copies `bridge.activeBeamMesh` (an empty one if there is none), changes it, drops stale results (displacements, rotations, section forces, stresses) and publishes it. Editing is disabled while a worker runs.
2. Node ids stay `0..n-1`. "Delete Node" removes the node's elements, nodal loads and the distributed loads on those elements, and moves later ids down. "Delete Element" keeps the distributed loads of the other elements pointing at them.
3. Supports: both modes of each group (translations, rotations) become one basis of allowed directions (`SupportInput::allowedBasis()`), written with `Node::setAllowedMotionDirections()` / `setAllowedRotationAxes()`: the global-axis checkboxes are turned into unit vectors the same way as inclined input, so the node only ever stores vectors. Inclined vectors are read as restrained or allowed directions, as in the truss editor (section 2.4 item 8); switching mode or reading keeps the support. A node whose stored basis is not along the global axes is shown in inclined mode. An inclined rotation support is solved as given, but files keep rotational fixity per global axis only, so export warns (the panel says so).
4. "Add Element" checks the nodes (existing, different, no duplicate), the material and section, and the orientation with `FEM::BEAM::localAxes()` (zero length, v parallel to the axis). Selecting an element in the list copies its properties into the inputs and sets `bridge.selectedElementId`, shared with the diagram panel.
5. End releases: the table edits `m_releases` (the `FEM::BEAM::RELEASE` bits, local axes of the element); "Add Element" and "Apply to Selected" write it into `BeamElement::endReleases`, selecting an element loads it. "Hinge A" / "Hinge B" release My + Mz at one end, "Pinned Both Ends" at both (torsion stays connected); any other combination is ticked by hand. The tooltip states the rules of [CALCULATIONS_BEAM.md](CALCULATIONS_BEAM.md) section 6.1 (release one side of a joint; N or T at both ends is a mechanism). Invalid combinations are reported by the solver, not the editor. The element list shows "hinge A", "hinge B" or "hinge A B".
6. "Run Solver for Beam" starts `BEAM_WORKER::startSolve()`: copies of the material and section lists, `FEM::BEAM::solveStatic()` on the worker thread, publication into `activeBeamMesh` unless the model was reset ([BRIDGE.md](BRIDGE.md) section 5).
7. "Load Example Frame" builds a 3D portal frame (HEB 200 columns, IPE 300 girder with a uniform load, a lateral and an out-of-plane nodal load, clamped bases, self weight).
8. The viewport draws the beam model (section 3.9); nodes and elements can be picked there or selected by id.

### 2.6 Section Handler (`SectionHandler`)

- Table of `bridge.allSections` with a name filter: name, catalogue / user, shape, A (cm²), Iy, Iz, J (cm⁴); "Remove" on user sections (refused while the beam model uses it or a worker runs, see [BRIDGE.md](BRIDGE.md) section 5.2).
- "Selected": the clicked section's outline (`sectionOutline()`, local z to the right, local y up, seen from node 1 towards node 2) and its properties (shear areas shown for ν = 0.3; the solve uses each element's material).
- "New Section": name, shape (general, rectangle, circle, pipe, box, I / H), dimensions in mm (general: cm² / cm⁴), live outline and properties, the `validateShape()` message when invalid; "Add Section" calls `addUserSection()` (saved to `userSections.json`).

### 2.7 Beam diagrams (`BeamDiagramPanel`)

- Element chooser (typed id, ◀ ▶, "Highest stress" = largest von Mises), synchronised with the editor through `bridge.selectedElementId`.
- Quantities: N, Vy, Vz, T, My, Mz (kN, kN m), local displacement u / v / w (mm), normal stress max / min and von Mises (MPa); 61 samples from `sampleElement()` and `sectionStress()`, rebuilt only when the snapshot or the element changes. Below the plot: end forces (one row per component, so it fits a narrow column) and the element's stress summary.
- Docking: the default layout has no node for it. When the panel opens (also after it was closed: a gap in its frame counter), it splits the Model Tree's dock node at runtime (`DockBuilderSplitNode(..., ImGuiDir_Down, 0.5)`) and docks itself into the lower half; closing it merges the node back, so the Model Tree gets the full height again in truss mode. A panel the user docked elsewhere stays there.

## 3. Viewport render pipeline

### 3.1 Resources

| Object | Class | GL objects |
|---|---|---|
| Offscreen target | `Framebuffer` (`guiMaterials/framebuffer.*`) | MSAA FBO: RGBA8 + R32I + D24S8 renderbuffers. Resolve FBO: RGBA8 + R32I textures (immutable storage). |
| Batches | `ViewportRenderer` (`panels/viewportRenderer.*`) | 6 VAO/VBO pairs: grid, lines, glow lines, translucent triangles, points, text |
| Programs | `ViewportRenderer` | `scene` (lines/points), `grid`, `text` |
| Beam sections, node spheres | `BeamSceneRenderer` (`panels/beamSceneRenderer.*`) | per mesh (one per section and level of detail, plus a 2-vertex line mesh): VAO with the mesh vertices (binding 0, immutable storage) and an instance buffer (binding 1, divisor 1, re-specified on upload); one unit-sphere VAO with its instance buffer; programs `beam` and `sphere` |

`buildShaderProgram()` (`guiMaterials/shaderProgram.*`) compiles and links every program of both renderers.

Every GL object is owned by a move-only `GlHandle` (`guiMaterials/glHandle.hpp`) and created through DSA (`glCreate*`, `glNamed*`, `glVertexArray*`). No `glGen*` or bind-to-edit.

### 3.2 Vertex layouts (binding 0 for every VAO)

| Batch | Struct | loc 0 | loc 1 | loc 2 | loc 3 |
|---|---|---|---|---|---|
| lines, glow lines, triangles | `Vertex3D` | vec3 position | vec4 color | int entityID (`IFormat`) | - |
| points | `Point3D` | vec3 position | vec4 color | int entityID | float size |
| text | `TextVertex` | vec2 NDC position | vec2 uv | vec4 color | - |
| grid | `glm::vec3` | vec3 NDC position (fullscreen triangle) | - | - | - |
| beam meshes | `MeshVertex` (binding 0) + `BeamInstance` (binding 1) | vec3 local (x 0..1 along, y / z in m) | vec3 local normal | instance: start, end, axisY0, axisZ0, axisY1, axisZ1 (loc 2-7), color0, color1 (8, 9), int entityID (10) | - |
| spheres | `glm::vec3` (unit sphere) + `SphereInstance` | vec3 position = normal | - | instance: vec4 centre + radius (2), color (3), int entityID (4) | - |

Dynamic batches are re-uploaded with `glNamedBufferData(..., GL_DYNAMIC_DRAW)` (orphaning) only when the mesh or visibility changes. The grid's fullscreen triangle uses immutable `glNamedBufferStorage`.

### 3.3 Shaders (inline raw strings, `#version 460 core`)

| Program | Vertex | Fragment outputs |
|---|---|---|
| `scene` | `u_MVP * pos`, passes color, flat entity ID, `gl_PointSize` | `location 0`: color, `location 1`: entity ID |
| `grid` | view ray per vertex (`forward + x·right + y·up`) | Ray / y = 0 plane intersection per pixel, in coordinates relative to a grid-aligned origin near the eye; anti-aliased minor + major (×10) lines (`fract` + `fwidth`) that fade out once a cell is a few pixels wide (no moiré) and towards `fadeDistance`; axis gap around X/Z axes; entity ID = -1 |
| `text` | NDC passthrough | Samples ImGui's font atlas (RGBA32, `.a` = coverage); entity ID = -1 |
| `beam` | places the unit-length section between the instance's start and end; section axes `normalize(mix(axis0, axis1, x))` so a rotating section stays continuous; color mixed along x | shared lit stage: color × (0.38 + 0.62 \|n · view\|) (light at the eye, two-sided), entity ID |
| `sphere` | `centre + radius × position` | the same lit stage |

`buildShaderProgram()` checks compile and link status and logs the driver's info log through `anaf::LOG::error`. A failing program leaves an empty handle, and the batch then draws nothing.

### 3.4 Draw order inside `renderSceneOpenGL()`

1. If `dataVersion` changed, `m_meshNeedsUpdate` is set or a bridge selection changed: copy `activeMesh` / `activeBeamMesh` under `dataMutex`, then `buildSceneBatches()` (truss: `buildTrussScene()`, beam: `buildBeamScene()`, section 3.9):
   - Truss elements become lines. "Color: Stress": `sqrt(|σ| / |σ|_max)` on the jet ramp (magnitude only; the sign is in the model tree); "Displacement": mean of the end nodes' |u|; "Off": the unsolved element color.
   - Nodes ("Nodes: Square") become points colored by displacement magnitude, or ("Sphere") instanced spheres. The selected node is orange and larger; supported nodes are red.
   - Supports, by "Supports" (section 3.7), at the drawn node position with a size of 4 % of the scene radius:
     - "Off": only inclined supports (`Node::hasInclinedSupport()`), red: an allowed plane as a translucent square (`addTriangle()`, blended after the lines without depth writes) with an outline, an allowed line as a double arrow with a glow line.
     - "Symbols" (`addSupportSymbol()`): every supported node gets a textbook symbol from its allowed motion basis (rotation is always free for a truss): no motion = a pyramid with its tip at the node on a hatched plate; one direction = the same on two rollers (instanced spheres) along it, with a double arrow under the plate; a plane = four rollers on a translucent plate. The ground side is -Y where the basis allows it.
     - "DOF" (`addDofRestraints()`): a red cone pointing at the node for every restrained direction (`FEM::SUPPORT::orthogonalComplement()` of the allowed basis), signed so its largest component is positive.
   - Applied forces become arrows with a fixed world length of 3 m: a shaft plus a 4-line head, each duplicated as a glow line.
   - Draw position = `location + displacement * deformScale`, with `deformScale` read from the bridge (`Gui_Calc_Bridge::deformScale`, a view setting) when the snapshot is reloaded.
2. `fbo.bind()`, depth test on, `fbo.clear(color, entity = -1)`.
3. `renderGrid(GridView)` (only while the "Grid" toggle is on, off by default): blended, depth writes off, minor spacing `10^floor(log10(distance/12))`, fade distance `max(40 × distance, 6 × scene radius)`. The grid used to be one ±8000 m quad; close to the camera its clipped, interpolated world positions lost precision and the lines bent and swam.
4. `BeamSceneRenderer::render()` (beam sections and node spheres, opaque, depth-tested), then `ViewportRenderer::render()`:
   - lines at 1.5 px with `GL_LINE_SMOOTH` + alpha blend
   - glow lines at 6 px with additive blend and depth writes off
   - translucent triangles with alpha blend and depth writes off
   - points with `GL_PROGRAM_POINT_SIZE`, depth tested: the vertex shader moves each node square along its eye ray (same screen position) by `Point3D::depthLift` or by its own screen radius in world units (`0.5 × size × distance × u_WorldPerPixel`), whichever is larger. Beam nodes get 1.2 × the largest section half size at the node, so their own section never hides them, while a member in front of the node does (and then also takes the pick). Truss nodes only get the screen-radius lift, so the bars meeting at the node never cut the square
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
| `R` or "Reset Camera" toolbar button | Fit to mesh bounds (distance = 2.2 × radius) |

View: `lookAt(target + orbitDirection() × distance, target, orbitUp())`. `orbitUp()` is −∂(orbitDirection)/∂pitch, so it is +Y at zero pitch and stays perpendicular to the view direction at any pitch (no gimbal flip at ±90°). Upside down, the yaw drag is mirrored so the view still follows the mouse.

Projection: `perspective(45°, aspect, near, far)` with `far = 2 × (distance + |target − scene centre| + scene radius)` and `near = max(0.005 × distance, 1e-6 × far)`, so the depth range follows the zoom from millimetre parts to 300 m stadiums. The axis lines (drawn only with the "Axes" toggle on) reach `max(8000, 200 × scene radius)`, beyond the largest far plane. Scene centre and radius are recomputed (`updateSceneBounds()`) whenever a new snapshot is drawn.

### 3.6 Picking

```text
left click in viewport
   -> mouse position relative to image origin, Y flipped (OpenGL origin bottom-left)
   -> Framebuffer::readEntityID(x, y)      glGetTextureSubImage on the R32I resolve texture
   -> id >= 0  : bridge.selectedNodeId = id                     (under dataMutex)
      id <= -2 : bridge.selectedElementId = -(id + 2)          (beam elements)
      id == -1 : both selections cleared
   -> m_meshNeedsUpdate = true (re-color selection)
```

Node squares and spheres write the node ID, beam element instances `-(index + 2)`; truss lines, supports, arrows, grid and text write -1. Nodes are pickable while "Nodes" is not "Off", beam elements always. The selection is shared with the editors and the diagram panel through the bridge; the viewport compares it every frame, so a selection made in a panel redraws the highlight too. The ID is the integer `nodeID` stored in the R32I attachment; there is no color encoding. The read is synchronous and stalls the pipeline for one pixel, which is acceptable for click-rate reads.

### 3.7 Viewport toolbar

`ViewportToolbar` (`panels/viewportToolbar.*`) is its own `IPanel` with its own window, "Viewport Toolbar" (`ViewportToolbar::kWindowName`). `MainDockSpaceHost` splits it off the top of the viewport's dock node, so it has the viewport's width; the node has no tab bar, no vertical resize and accepts no other window, and its height is `ViewportToolbar::windowHeight()` (frame height + 2 × 6 px padding, at least `WindowMinSize.y`). The buttons never cover the scene or trigger picking.

```text
ViewportToolbar::onImGuiRender()        writes   ViewportDisplayOptions (shared_ptr, made in gui.cpp openPanels())
   toggle / choice -> field set, changed = true
   Reset Camera -> resetCameraRequested = true
ViewportPanel::renderSceneOpenGL()       next frame, before the snapshot check
   changed              -> m_meshNeedsUpdate = true, changed = false
   resetCameraRequested -> resetCamera(),         flag cleared
```

Both run on the GUI thread, so the struct needs no lock.

| Button | `ViewportDisplayOptions` field | Default | Effect |
|---|---|---|---|
| Reset Camera | - | - | `resetCamera()`, same as `R` |
| Grid | `showGrid` | off | `renderGrid()` pass |
| Axes | `showAxes` | off | 3D X / Y / Z axis lines in `buildSceneBatches()`; the corner gizmo is always drawn |
| Nodes: Off / Square / Sphere | `nodeStyle` | Square | node squares (screen markers) or 3D spheres, node labels, picking, displacement colorbar |
| Forces | `showForces` | on | force arrows; beam: also moments (double head) and distributed loads |
| Supports: Off / Symbols / DOF | `supportStyle` | Off | "Off": inclined supports only; "Symbols": textbook symbol per support type; "DOF": CAD style cones per restrained DOF (sections 3.4, 3.9) |
| Color: Off / Stress / Displacement | `coloring` | Stress | element colors (truss: \|axial stress\|, beam: von Mises along the element; displacement magnitude) and their colorbar |

Hover and press do not change a button's color: a toggle that is on (or a choice other than "Off") is drawn in `ButtonActive`, everything else in `Button`. The three choice buttons show "label: current" and open a popup list. Every change makes the viewport rebuild its batches on the next frame.

### 3.8 2D overlay (ImGui draw list)

Drawn by `renderOverlay2D()` on top of the image:
- axis gizmo (camera rotation only)
- FPS counter, red below 30 FPS
- element colorbar with "Color: Stress" (`|Stress| (MPa)` for trusses, `von Mises (MPa)` for beams) and displacement colorbar (`Disp (mm)`) with "Color: Displacement" or visible nodes

### 3.9 Beam models

```text
snapshot changed      -> buildBeamStations()       (sections and lists copied from the bridge)
   per section in use: full mesh = extrudeSection(shape, 4 segments / quarter)
                       simple mesh = 8-sided cylinder (circle, pipe) or box of the outline's extent
   per element (OpenMP): local axes, sampleElement() at 9 stations (5 with level of detail):
                       undeformed position, displacement, x / L, von Mises (sectionStress), |u|
deformation scale changed -> applyBeamDeformation()
   stations = base + scale × displacement
   section frame per station: twist = scale × local rx, linear between the nodes; bending: the
   frame turned (Rodrigues, smallest rotation) from the undeformed axis onto the drawn tangent,
   which is x + scale × (rz y − ry z) at the nodes and a central difference inside
every rebuild           -> buildBeamScene(): node squares / spheres, translation release arrows, supports,
                           force, moment and distributed-load arrows, then pushBeamInstances()
                           (sections, release pins and collars)
camera moved (level of detail only) -> pushBeamInstances() alone
```

1. `extrudeSection()`: side walls from `sectionOutline()` with normals smoothed across corners under 40° (arcs look round, corners stay sharp), end caps from `FEM::BEAM::triangulateSection()` (ear clipping with hole bridging). A general section is drawn as the rectangle with the same A, Iy, Iz.
2. Every segment between two stations is one instance of its section mesh: start, end, both section frames and both colors, entity ID `-(element + 2)`. Colors per station: von Mises / |u| on the jet ramp over the model's maximum, a general section (no stress) gray, the selected element orange.
3. The bending rotation shown is the slope of the drawn axis, exact for Euler-Bernoulli; for Timoshenko the section rotation differs from it by the shear angle. Rotations are scaled like the displacements, so a large scale over-twists visibly.
4. Spheres: radius = 1.3 × the largest section half size at the node (larger than the elements), selected × 1.25.
5. Level of detail, only above 4000 elements: the on-screen section size `2 × halfSize × focal / distance` picks the real section (≥ 10 px), the simple mesh (≥ 2 px) or a line; the selected element always gets the real section. Only the instance lists change with the camera (the meshes stay on the GPU); 13 120 elements drew at 59 FPS on a Radeon 680M.
6. End releases are drawn just inside the released end (`releaseFrame()`: set in from the node by 1.4 × the section half size, at most a quarter of the element), so they mark the member, not the joint, in light grey (selected: orange), with the element's entity ID (picking selects the element):
   - a released bending rotation (`momentY` / `momentZ`) is a pin: an instance of a per-section cylinder mesh (diameter 0.4 × half size) along the drawn local y / z, 1.35 × half size to each side. Both released (`hinge`) gives crossed pins: kinematically a universal joint (forces and torsion carried, no bending moment);
   - released torsion is a collar (pipe mesh 2.5 × half size wide) around the member;
   - released translations (`axial`, `shearY`, `shearZ`) are double arrows along the local axis (lines, built in `buildBeamScene()`).

   Pins and collars are instances, so `pushBeamInstances()` adds them; with level of detail they are left out where the member is drawn as a line. With results, a hinged end's rotation comes from `elementEndDisplacements()` (the element end's own rotation), so the drawn sections kink at the hinge.
7. Arrows are sized from the scene radius (15 % for forces and moments, 45 % of that for distributed loads).
8. Supports ("Supports" toolbar choice) are sized max(4 % of the scene radius, 3 × the largest section half size at the node). "Symbols" reads both bases of the node: a free rotation keeps the pyramid; a fixed rotation (clamp) has no body but a hatched wall through the node, facing away from the members (opposite the sum of the unit directions of the members leaving it; -Y when they cancel), on rollers with a shoe plate when the node may move; one or two allowed rotation axes add cyan axles through the node; a node free in translation with a restrained rotation gets a wire cube. "DOF" adds an amber double cone from the +c side for every restrained rotation axis, so a translation and a rotation cone on the same axis do not overlap.

## 4. Window and platform details

- GLFW picks the platform itself (Wayland or X11 on Linux; no init hint, no retry). Right after `glfwInit()` the log records it, e.g. `Window system: Wayland (session type: wayland)`: `glfwGetPlatform()` (GLFW 3.4+) and, on Linux, `XDG_SESSION_TYPE`. They differ when GLFW runs through XWayland in a Wayland session. The app ID and X11 class are `anafinen`, matching `anafinen.desktop`. GLFW does not follow the desktop's cursor settings, so `platform_utils::setupSystemCursor()` (`linuxCursor.hpp`) looks up the theme and size the user picked and exports them as `XCURSOR_THEME` / `XCURSOR_SIZE` before `glfwInit()`:
  - A value already in the environment is kept (the user's or the session's explicit choice); with both set, nothing is queried.
  - Source order follows `XDG_CURRENT_DESKTOP`: on KDE Plasma `kreadconfig6` / `kreadconfig5` (`kcminputrc`, group `Mouse`) first, elsewhere `gsettings` (`org.gnome.desktop.interface`) first, because on Plasma `gsettings` answers with GNOME's default when the GNOME schemas are installed. X resources (`Xcursor.theme` / `Xcursor.size`) come last, then Adwaita / 24.
- On Windows, the `NvOptimusEnablement` / `AmdPowerXpressRequestHighPerformance` exports request the discrete GPU.
- The window icon is set from two PNGs, each loaded with libpng from the first existing asset path (see [BUILD_SYSTEM.md](BUILD_SYSTEM.md) section 7): `icons/anafinen.png` (128 px) and `icons/anafinen-32.png` (drawn from the small-size SVG). GLFW picks the closest size for the title bar and taskbar, so small sizes are not shrunk from 128 px. On Wayland GLFW ignores it; the compositor uses the hicolor icon named in `anafinen.desktop`.
- `anafinen.desktop` sets `StartupNotify=false`. GLFW 3.4 never reads `XDG_ACTIVATION_TOKEN` / `DESKTOP_STARTUP_ID`, so with `true` GNOME Shell waits for a startup sequence that never completes, and an unpinned dock (dash-to-dock) shows the running icon ~20-30 s late.
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
- ImGuizmo is built and linked but not used yet (kept for planned gizmo work). ImPlot draws the beam diagrams.

## 7. Related source files

- Frame loop and wiring: [src/gui/gui.hpp](../src/gui/gui.hpp), [src/gui/gui.cpp](../src/gui/gui.cpp)
- Infrastructure: [iPanel.hpp](../src/gui/guiMaterials/iPanel.hpp), [imGuiLayer.hpp](../src/gui/guiMaterials/imGuiLayer.hpp), [imGuiLayer.cpp](../src/gui/guiMaterials/imGuiLayer.cpp), [glHandle.hpp](../src/gui/guiMaterials/glHandle.hpp), [framebuffer.hpp](../src/gui/guiMaterials/framebuffer.hpp), [framebuffer.cpp](../src/gui/guiMaterials/framebuffer.cpp)
- Viewport: [viewportPanel.hpp](../src/gui/panels/viewportPanel.hpp), [viewportPanel.cpp](../src/gui/panels/viewportPanel.cpp), [viewportRenderer.hpp](../src/gui/panels/viewportRenderer.hpp), [viewportRenderer.cpp](../src/gui/panels/viewportRenderer.cpp), [beamSceneRenderer.hpp](../src/gui/panels/beamSceneRenderer.hpp), [beamSceneRenderer.cpp](../src/gui/panels/beamSceneRenderer.cpp), [shaderProgram.cpp](../src/gui/guiMaterials/shaderProgram.cpp)
- Panels: [statusBar.cpp](../src/gui/panels/statusBar.cpp), [mainDockSpaceHost.cpp](../src/gui/panels/mainDockSpaceHost.cpp), [trussControlPanel.cpp](../src/gui/panels/truss/simpleQuadrangleTruss/trussControlPanel.cpp), [trussModelEditor.cpp](../src/gui/panels/truss/importedTruss/trussModelEditor.cpp), [trussWorker.hpp](../src/gui/panels/truss/trussWorker.hpp), [analysisSelector.cpp](../src/gui/panels/analysisSelector.cpp), [dynamicAnalysisInputs.hpp](../src/gui/panels/dynamicAnalysisInputs.hpp), [editorLayout.hpp](../src/gui/panels/editorLayout.hpp), [modelTree.cpp](../src/gui/panels/modelTree.cpp), [beamModelEditor.cpp](../src/gui/panels/beam/beamModelEditor.cpp), [beamWorker.cpp](../src/gui/panels/beam/beamWorker.cpp), [sectionHandler.cpp](../src/gui/panels/beam/sectionHandler.cpp), [sectionCombo.hpp](../src/gui/panels/beam/sectionCombo.hpp), [beamDiagramPanel.cpp](../src/gui/panels/beam/beamDiagramPanel.cpp), [materialHandler.cpp](../src/gui/panels/materialHandler.cpp), [logTerminal.cpp](../src/gui/panels/logTerminal.cpp)
- Platform: [linuxCursor.hpp](../src/gui/linuxCursor.hpp), [getExecutableDirectory.cpp](../src/directory/getExecutableDirectory.cpp)
