# ANAFINEN Architecture Overview

This document is the entry point for the project documentation. It describes how the program is split into modules, how those modules talk to each other, and where each topic is documented in detail.

> **Document status**
> Verified against: `v0.1.3-alpha` working tree (unreleased), 2026-09-28.
> Update this file set on every version bump or structural change (see section 7).

## 1. Documentation map

| Document | Topic |
|---|---|
| [ARCHITECTURE.md](ARCHITECTURE.md) | This overview: modules, targets, threads, startup/shutdown, known issues |
| [BUILD_SYSTEM.md](BUILD_SYSTEM.md) | CMake modules, dependency detection, targets, packaging |
| [BRIDGE.md](BRIDGE.md) | `Gui_Calc_Bridge`, `MeshData` snapshots, synchronization rules |
| [CALCULATIONS.md](CALCULATIONS.md) | Truss FEM pipeline, stiffness assembly, solver portfolio, validator |
| [FILE_HANDLING.md](FILE_HANDLING.md) | `anaf_io`: format-neutral mesh model, MSH / VTK / VTU / STEP / IGES / BREP, async I/O service |
| [GUI.md](GUI.md) | Frame loop, panels, viewport render pipeline, picking |
| [MESH_DATA_FLOW.md](MESH_DATA_FLOW.md) | End-to-end path of one truss mesh from the panel to the screen |
| [AIM.md](AIM.md) | Master plan and phase checklist |

## 2. Module map

```text
+---------------------------------------------------------------------------+
| anafinen (executable)                                                     |
|                                                                           |
|  main.cpp ---> LOG init ---> materials ---> OpenMP ---> GUI::initgui()    |
|                                                                           |
|  +-------------------+     +--------------------+     +----------------+  |
|  | GUI               |     | BRIDGE             |     | LOG            |  |
|  | src/gui/          |<--->| src/bridge/        |     | src/log/       |  |
|  | panels, viewport, |     | Gui_Calc_Bridge    |     | file + stdout  |  |
|  | framebuffer, GL   |     | MeshData snapshot  |     | + GUI sink     |  |
|  +---------+---------+     +---------+----------+     +----------------+  |
|            | std::jthread worker      ^                                   |
|            v                          | publish snapshot                  |
|  +--------------------------------------------------------------------+   |
|  | anaf_core (static library)                                         |   |
|  |  FEM::TRUSS: generator, container, solver portfolio                |   |
|  |  FEM::TRUSS::ADAPTER: MeshModel <-> MeshData (truss_1D/trussIO/)   |   |
|  +---------------------------------+----------------------------------+   |
|                                    | links                                |
|  +---------------------------------v----------------------------------+   |
|  | anaf_io (static library, no solver / GUI dependency)               |   |
|  |  anaf::IO: MeshModel, MSH / VTK / VTU (native), STEP / IGES / BREP |   |
|  |  (Gmsh + OCC), IoService (own I/O thread)                          |   |
|  +--------------------------------------------------------------------+   |
+---------------------------------------------------------------------------+
```

- `anaf_io` is the file layer shared by every front end: GUI now, CLI later, and the tests. It does not know about any solver.
- `anaf_core` holds the FEM code and the solver-specific adapters.
- `anafinen` adds the GUI, the bridge, the log, and the entry point.

`anaf_core` also includes `bridge/generalStatus.hpp` (`Truss_SQPT` takes the bridge by reference). This is intentional until the CLI executable exists; see section 8.1.

## 3. Namespaces

| Namespace | Location | Responsibility |
|---|---|---|
| `FEM::TRUSS` | `src/objectCalcs/truss_1D/` | Node, element, load types; truss generator; FEM container |
| `FEM::TRUSS::SOLVER` | `src/objectCalcs/truss_1D/trussEngine/trussSolver/` | Linear solver portfolio and referee |
| `anaf::BRIDGE` | `src/bridge/` | Shared state between GUI thread and worker thread |
| `anaf::GUI` | `src/gui/` | Window, ImGui layer, panels, OpenGL renderer |
| `anaf::IO` | `src/io/` | Format-neutral mesh model, readers / writers, async I/O service (library `anaf_io`) |
| `FEM::TRUSS::ADAPTER` | `src/objectCalcs/truss_1D/trussIO/` | `MeshModel` ↔ truss snapshot conversion |
| `anaf::MATERIAL` | `src/material/` | `Material` property record; material library loader and validation (`materialLibrary.*`, in `anaf_core`) |
| `anaf::LOG` | `src/log/` | Formatted logging with file, stdout and GUI sinks |
| `anaf::DIRECTORY` | `src/directory/` | Executable directory lookup; `findAssetPath()` is the single asset search used by fonts, icon and material library; `getUserConfigDirectory()` for per-user data |
| `anaf::TEST` | `src/test/` | Startup self-check (Eigen determinant) |
| `anafGen` | `src/gen/` | Hash-based ID generator (not used yet) |
| `platform_utils` | `src/gui/linuxCursor.hpp` | Linux cursor theme setup for GLFW |

## 4. Threads

| Thread | Created by | Work | Talks to others through |
|---|---|---|---|
| Main (GUI) thread | OS | GLFW events, ImGui frame, OpenGL rendering | `Gui_Calc_Bridge` (mutex + atomics) |
| Worker thread | `TrussControlPanel` (`bridge.workerThread`, `std::jthread`) | Preview mesh generation or a full solve | Publishes a new `MeshData`, bumps `dataVersion` |
| OpenMP team | Inside the worker (`#pragma omp parallel`) | Mesh generation, assembly, reductions, Block-CG | Joins before the worker continues |
| I/O thread | `IoService` owned by `FileIoPanel` | Import / export: parsing, writing, snapshot ↔ model conversion | `IoTask` polled every frame; results published through the bridge on the GUI thread |

Rules:
- Only the main thread touches OpenGL and ImGui.
- Only one worker exists at a time. Assigning a new `std::jthread` to `bridge.workerThread` requests stop on the previous worker and joins it.
- The worker never mutates a published `MeshData`. It builds a new one and swaps the `shared_ptr` under `dataMutex`.
- The worker never reads mutable bridge containers directly. `fixedDOFsByNode` and `allMaterials` are copied under `dataMutex` on the GUI thread and moved into the worker lambda.
- Thread count: `main.cpp` and `configureOpenMPForWorker()` both set OpenMP/Eigen to `cores - 2` when there are more than 4 cores.

## 5. Startup sequence

1. `anaf::LOG::setCallback()` routes every log line into the GUI console buffer (`anafUILogSink`).
2. `anaf::LOG::init("anafinen_run.log")` opens the log file in the working directory.
3. `main()` builds the bridge singleton (`buildBridge()`) and loads the built-in materials from `assets/bridge/materialProperties.json` (`setStaticInfo()`). This runs after the log init so a missing or broken file is reported. `loadUserMaterials()` then adds the saved user materials from the user config directory ([BRIDGE.md](BRIDGE.md) section 5.1).
4. OpenMP and Eigen thread counts are configured.
5. `anaf::TEST::AllStatus` runs the Eigen self-check.
6. `anaf::GUI::initgui()`:
   1. Initializes GLFW and creates an OpenGL 4.6 core window.
   2. Loads GLAD, then ImGui and the fonts.
   3. Creates the `Framebuffer` and registers the panels.
   4. Enters the frame loop.

## 6. Shutdown sequence

1. The frame loop exits when the window is closed.
2. Inside the GL resource scope in `initgui()`:
   - The worker receives `request_stop()` and is joined (`workerThread = std::jthread{}`).
   - Panels, the renderer, and the framebuffer are destroyed while the GL context is still current. `FileIoPanel` destroys its `IoService`, which cancels queued jobs and joins the I/O thread.
3. `imguiLayer.shutdown()` destroys ImGui backends and context.
4. `glfwDestroyWindow()` and `glfwTerminate()` run.
5. `main()` closes the log.
6. At static destruction, `GmshSessionManager` finalizes Gmsh if it was ever initialized.

GL objects must never outlive the context; see [GUI.md](GUI.md) section 6.

## 7. Keeping these documents current

Update the documents when any of the following happens:

| Change | Documents to touch |
|---|---|
| Version bump (`project(... VERSION ...)`) | Every document's status line; [BUILD_SYSTEM.md](BUILD_SYSTEM.md) packaging names |
| New element type, solver, or analysis | [CALCULATIONS.md](CALCULATIONS.md), [MESH_DATA_FLOW.md](MESH_DATA_FLOW.md), [AIM.md](AIM.md) |
| New field in `MeshData` or `Gui_Calc_Bridge` | [BRIDGE.md](BRIDGE.md) |
| New panel, shader, or GL resource | [GUI.md](GUI.md) |
| New file format or format change | [FILE_HANDLING.md](FILE_HANDLING.md) |
| New dependency, CMake option, or package target | [BUILD_SYSTEM.md](BUILD_SYSTEM.md) |
| A known issue is fixed | Remove it from section 8 below |

## 8. Known issues (found while writing these documents)

| # | Issue | Location | Effect |
|---|---|---|---|
| 1 | Stub types are declared but not implemented: `Truss`, `TrussBuild`, `Truss_Imported_or_Entered::setImportedData`. | `truss.hpp`, `selectTrussType.hpp`, `trussSolver.hpp` | Imported trusses can be viewed, exported and inspected, but not solved yet. |
| 2 | The Gmsh 4.15 build on Fedora aborts when it opens any binary MSH 4.1 file (its own too). | Gmsh (external) | Only affects opening our binary 4.1 files **in Gmsh**; anafinen reads MSH natively. |
| 3 | Paths are converted with `path::string()` in code older than 0.1.3: `anaf_io` (error texts, `report.path`, Gmsh calls in `cadFormat.cpp`), `fileIoPanel.cpp`, `nativeFileDialog.cpp` (path from a pfd UTF-8 string). On Windows this is the ANSI code page: MSVC throws and Gmsh / pfd get wrong names for files such as `köprü.msh`. Linux is unaffected. | see list | Fix: `anaf::IO::pathToUtf8()` / `pathFromUtf8()` (`io/core/pathUtf8.hpp`), as the 0.1.3 material and asset code does. |

### 8.1 Deferred by design

- `anaf_core` includes `bridge/generalStatus.hpp` (`Truss_SQPT` takes `Gui_Calc_Bridge&`) and calls `anaf::LOG`. Their sources (`generalStatus.cpp`, `anaf_info.cpp`, and `getExecutableDirectory.cpp` for the material library lookup) are compiled into the GUI executable only, so `anaf_core` cannot be linked on its own yet; `anaf_truss_io_tests` adds these files explicitly. The material library loader itself (`materialLibrary.cpp`) is already in `anaf_core` and has no GUI or log dependency. This is intentional for now. A pure CLI executable is planned for a later phase; at that point the bridge gets a CLI-side counterpart and the core is built against that instead of the GUI side.

### 8.2 Fixed

| Issue | Fixed on | Fix |
|---|---|---|
| Displacement drawn twice (`location += displacement` in `calculate()`) | 2026-09-27 | Node locations stay undeformed. The displacement is stored only in `m_displacement`. |
| Worker read `fixedDOFsByNode` / `allMaterials` without `dataMutex` | 2026-09-27 | The GUI thread copies both under the lock and moves the copies into the worker. The solver takes the fixity map as an argument. |
| Default gravity `{0, -9,80665, 0}` | 2026-09-27 | Now `{0.0, -9.80665, 0.0}`. The old value was a latent compile error: Eigen's static assert fires as soon as the default is used. |
| Turkish comments in `fileSTEP.cpp` | 2026-09-27 | Translated (the file was later replaced by `anaf_io`). |
| Element stress stored as an absolute value | 2026-09-27 | Stress and axial force are signed (tension > 0, compression < 0). Magnitudes are unchanged. |
| Built-in materials hard-coded in `generalStatus.cpp`; Material Handler was a stub; unused `createdMaterials` / `setDynamicMaterialInfo()` | 2026-09-28 | Built-ins come from `assets/bridge/materialProperties.json` (validated); user materials can be added and removed without shifting indices under the active mesh. |
| Three copies of the asset search path (fonts, icon) | 2026-09-28 | `anaf::DIRECTORY::findAssetPath()`. |
| No compiler warning flags; 90 warnings under `-Wall -Wextra -Wpedantic -Wshadow -Wconversion` | 2026-09-28 | Flags enabled for first-party targets, `ANAFINEN_WARNINGS_AS_ERRORS` option. Fixed: `Material` / `SimpleTruss` init order, `const` on by-value returns, narrowing in the truss generator (signed OpenMP index cast once per iteration), unused variables / fields / the force-taking `Node` constructor, `Result` initializers, no-CHOLMOD stubs. `-march=x86-64` in `ANAFINEN_NATIVE_OPTIMIZATIONS` replaced by `-march=native`. See [BUILD_SYSTEM.md](BUILD_SYSTEM.md) section 4. |
| Exported files referred to materials only by index | 2026-09-28 | `Material:<name>` element sets; import matches by name (see [FILE_HANDLING.md](FILE_HANDLING.md) section 3). |
| Windows: exe directory read with `GetModuleFileNameA` (non-ASCII folders became `?`); icon read with `fopen` | 2026-09-28 | `GetModuleFileNameW`; icon read through `std::ifstream` + `png_image_begin_read_from_memory`; fonts get UTF-8 paths. |
| MinGW cross-build did not configure (`install(RUNTIME_DEPENDENCIES)` is not allowed when cross-compiling) and `main.cpp` included the unused `gmsh.h` without linking Gmsh | 2026-09-28 | Guarded with `NOT CMAKE_CROSSCOMPILING`; include removed. The cross-build and its tests (under Wine) pass. |
| Material comments said GPa for values stored in Pa | 2026-09-27 | Comments corrected to Pa. The aluminum yield literal `276.0e9 / 1e3` was simplified to `276.0e6` (same value). |
| Stress colorbar labelled MPa but divided by 1e3 (showed kPa numbers) | 2026-09-27 | Divides by 1e6; label is now `\|Stress\| (MPa)`. |
| VTK / sidecar written with 6 significant digits | 2026-09-28 | Every text format writes shortest round-trip doubles (`std::to_chars`); round trips are bit-exact (tested). |
| `exportMSH()` wrote the file twice | 2026-09-28 | MSH is written natively in one pass. |
| `.msh` import through `gmsh::open` crashed the application on binary MSH 4.1 (Gmsh 4.15 bug) | 2026-09-28 | Native MSH reader. |
| STEP import split crossing bars (X-bracing) and connected them | 2026-09-28 | No fragmenting for bar import; coincident nodes are merged after meshing, duplicate edges removed. |
| STEP sidecar matched elements by centroid (crossing bars share it) | 2026-09-28 | Sidecar v2 matches elements by their end nodes. |
| File I/O ran on the calling thread | 2026-09-28 | `IoService` runs all file work (and snapshot conversion) on its own thread. |
| `ModelTree` read `bridge.activeMesh` without the lock and deep-copied the mesh every frame | 2026-09-28 | Pointer copy under `dataMutex`; snapshots are immutable. |
| Preview used the cross-section in cm² while the solver used m² | 2026-09-28 | Preview converts to m² like the solver. |
