# ANAFINEN Architecture Overview

This document is the entry point for the project documentation. It describes how the program is split into modules, how those modules talk to each other, and where each topic is documented in detail.

> **Document status**
> Verified against: `v0.1.2-alpha` + working tree, 2026-09-27.
> Update this file set on every version bump or structural change (see section 7).

## 1. Documentation map

| Document | Topic |
|---|---|
| [ARCHITECTURE.md](ARCHITECTURE.md) | This overview: modules, targets, threads, startup/shutdown, known issues |
| [BUILD_SYSTEM.md](BUILD_SYSTEM.md) | CMake modules, dependency detection, targets, packaging |
| [BRIDGE.md](BRIDGE.md) | `Gui_Calc_Bridge`, `MeshData` snapshots, synchronization rules |
| [CALCULATIONS.md](CALCULATIONS.md) | Truss FEM pipeline, stiffness assembly, solver portfolio, validator |
| [FILE_HANDLING.md](FILE_HANDLING.md) | STEP / MSH / VTK import-export and the Gmsh session |
| [GUI.md](GUI.md) | Frame loop, panels, viewport render pipeline, picking |
| [MESH_DATA_FLOW.md](MESH_DATA_FLOW.md) | End-to-end path of one truss mesh from the panel to the screen |
| [AIM.md](AIM.md) | Master plan and phase checklist |

## 2. Module map

```text
+---------------------------------------------------------------------------+
| anafinen (executable)                                                     |
|                                                                           |
|  main.cpp ---> LOG init ---> OpenMP setup ---> GUI::initgui()             |
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
|  |                                                                    |   |
|  |  +-----------------------------+     +--------------------------+  |   |
|  |  | FEM::TRUSS                  |     | anaf::FILE               |  |   |
|  |  | src/objectCalcs/truss_1D/   |     | src/fileOperations/      |  |   |
|  |  | generator, container,       |     | STEP / MSH / VTK,        |  |   |
|  |  | solver portfolio            |     | Gmsh session             |  |   |
|  |  +-----------------------------+     +--------------------------+  |   |
|  +--------------------------------------------------------------------+   |
+---------------------------------------------------------------------------+
```

`anaf_core` holds everything that does not need a window: FEM and file I/O. `anafinen` adds the GUI, bridge, log, and entry point.

`anaf_core` also includes `bridge/generalStatus.hpp` (`Truss_SQPT` takes the bridge by reference). This is intentional until the CLI executable exists; see section 8.1.

## 3. Namespaces

| Namespace | Location | Responsibility |
|---|---|---|
| `FEM::TRUSS` | `src/objectCalcs/truss_1D/` | Node, element, load types; truss generator; FEM container |
| `FEM::TRUSS::SOLVER` | `src/objectCalcs/truss_1D/trussEngine/trussSolver/` | Linear solver portfolio and referee |
| `anaf::BRIDGE` | `src/bridge/` | Shared state between GUI thread and worker thread |
| `anaf::GUI` | `src/gui/` | Window, ImGui layer, panels, OpenGL renderer |
| `anaf::FILE` | `src/fileOperations/` | Mesh import/export, Gmsh session |
| `anaf::MATERIAL` | `src/material/` | `Material` property record |
| `anaf::LOG` | `src/log/` | Formatted logging with file, stdout and GUI sinks |
| `anaf::DIRECTORY` | `src/directory/` | Executable directory lookup (asset resolution) |
| `anaf::TEST` | `src/test/` | Startup self-check (Eigen determinant) |
| `anafGen` | `src/gen/` | Hash-based ID generator (not used yet) |
| `platform_utils` | `src/gui/linuxCursor.hpp` | Linux cursor theme setup for GLFW |

## 4. Threads

| Thread | Created by | Work | Talks to others through |
|---|---|---|---|
| Main (GUI) thread | OS | GLFW events, ImGui frame, OpenGL rendering | `Gui_Calc_Bridge` (mutex + atomics) |
| Worker thread | `TrussControlPanel` (`bridge.workerThread`, `std::jthread`) | Preview mesh generation or a full solve | Publishes a new `MeshData`, bumps `dataVersion` |
| OpenMP team | Inside the worker (`#pragma omp parallel`) | Mesh generation, assembly, reductions, Block-CG | Joins before the worker continues |

Rules:
- Only the main thread touches OpenGL and ImGui.
- Only one worker exists at a time. Assigning a new `std::jthread` to `bridge.workerThread` requests stop on the previous worker and joins it.
- The worker never mutates a published `MeshData`. It builds a new one and swaps the `shared_ptr` under `dataMutex`.
- The worker never reads mutable bridge containers directly. `fixedDOFsByNode` and `allMaterials` are copied under `dataMutex` on the GUI thread and moved into the worker lambda.
- Thread count: `main.cpp` and `configureOpenMPForWorker()` both set OpenMP/Eigen to `cores - 2` when there are more than 4 cores.

## 5. Startup sequence

1. `main()` builds the bridge singleton (`buildBridge()`) and loads the built-in materials (`setStaticInfo()`).
2. `anaf::LOG::setCallback()` routes every log line into the GUI console buffer (`anafUILogSink`).
3. `anaf::LOG::init("anafinen_run.log")` opens the log file in the working directory.
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
   - Panels, the renderer, and the framebuffer are destroyed while the GL context is still current.
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
| 1 | Stream output uses the default 6 significant digits. | `fileVTK.cpp`, `fileSTEP.cpp` (sidecar) | A VTK or sidecar round trip rounds coordinates and results. |
| 2 | `exportMSH()` writes the file twice. | `fileMSH.cpp` | Redundant I/O. |
| 3 | Element stress is stored as an absolute value. | `calculateElementForcesAndStress` | Tension and compression cannot be told apart. |
| 4 | Material field comments say GPa, but values are stored in Pa. | `material/properties.hpp` | Misleading only; the solver treats values as SI. |
| 5 | Stub types are declared but not implemented: `Truss`, `TrussBuild`, `Truss_Imported_or_Entered::setImportedData`. | `truss.hpp`, `selectTrussType.hpp`, `trussSolver.hpp` | Placeholders for imported/self-built trusses. |

### 8.1 Deferred by design

- `anaf_core` includes `bridge/generalStatus.hpp` (`Truss_SQPT` takes `Gui_Calc_Bridge&`). This is intentional for now. A pure CLI executable is planned for a later phase; at that point the bridge gets a CLI-side counterpart and the core is built against that instead of the GUI side.

### 8.2 Fixed

| Issue | Fixed on | Fix |
|---|---|---|
| Displacement drawn twice (`location += displacement` in `calculate()`) | 2026-09-27 | Node locations stay undeformed. The displacement is stored only in `m_displacement`. |
| Worker read `fixedDOFsByNode` / `allMaterials` without `dataMutex` | 2026-09-27 | The GUI thread copies both under the lock and moves the copies into the worker. The solver takes the fixity map as an argument. |
| Default gravity `{0, -9,80665, 0}` | 2026-09-27 | Now `{0.0, -9.80665, 0.0}`. The old value was a latent compile error: Eigen's static assert fires as soon as the default is used. |
| Turkish comments in `fileSTEP.cpp` | 2026-09-27 | Translated. |
