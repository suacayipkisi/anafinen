# ANAFINEN Architecture Overview

This document is the entry point for the project documentation. It describes how the program is split into modules, how those modules talk to each other, and where each topic is documented in detail.

> **Document status**
> Verified against: `v0.1.3-alpha` (released 2026-10-01), content checked 2026-10-03 (beam / rotational data in `anaf_io`, known issue 5).
> Update this file set on every version bump or structural change (see section 7).

## 1. Documentation map

| Document | Topic |
|---|---|
| [ARCHITECTURE.md](ARCHITECTURE.md) | This overview: modules, targets, threads, startup/shutdown, known issues |
| [BUILD_SYSTEM.md](BUILD_SYSTEM.md) | CMake modules, dependency detection, targets, packaging |
| [BRIDGE.md](BRIDGE.md) | `Gui_Calc_Bridge`, `MeshData` snapshots, synchronization rules |
| [CALCULATIONS.md](CALCULATIONS.md) | Truss FEM pipeline, stiffness assembly, solver portfolio, validator |
| [FILE_HANDLING.md](FILE_HANDLING.md) | `anaf_io`: format-neutral mesh model, MSH / VTK / VTU / STEP / IGES / BREP, async I/O service |
| [IO_USAGE.md](IO_USAGE.md) | How callers use `anaf_io`: public headers, `readMesh` / `writeMesh`, `IoService`, building and reading a `MeshModel`, solver adapters |
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
- `anaf_core` is self-contained (since 2026-10-02): the model types (`MeshData`), `solveStatic()`, the material library, the log and the asset lookup. A front end links it and needs nothing from the GUI.
- `anafinen` adds the GUI, the bridge (`Gui_Calc_Bridge`: snapshot publication, worker, materials for the panels) and the entry point.

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
| `anaf::LOG` | `src/log/` | `std::format` logging (`info` / `warn` / `error` / `success` / `core`) with file, GUI callback and stdout sinks, chosen at run time. A floating-point value states its own precision in the format string (`{:.3e}` for round-off sized energy differences, `{:.6g}` for results); there is no global precision setting. |
| `anaf::PLATFORM` | `src/platform/` | The one place that reads the hardware (Linux and Windows). `systemInfo.*` is in `anaf_core`: `querySystemInfo()` (CPU name, threads, total RAM), `queryMemory()` (total / available RAM), `queryVideoMemoryGiB()`, the short CPU / GPU name rules; used by the status bar, the resource monitor and the solver referee's log. `ResourceMonitor` (process CPU / RAM, system RAM %) is GUI-only. |
| `anaf::DIRECTORY` | `src/directory/` | Executable directory lookup; `findAssetPath()` is the single asset search used by fonts, icon and material library; `getUserConfigDirectory()` for per-user data |
| `platform_utils` | `src/gui/linuxCursor.hpp` | Linux cursor theme setup for GLFW |

## 4. Threads

| Thread | Created by | Work | Talks to others through |
|---|---|---|---|
| Main (GUI) thread | OS | GLFW events, ImGui frame, OpenGL rendering | `Gui_Calc_Bridge` (mutex + atomics) |
| Worker thread | `TrussControlPanel` or `TrussModelEditor` (`bridge.workerThread`, `std::jthread`) | Preview mesh generation or a full solve | Publishes a new `MeshData`, bumps `dataVersion` |
| OpenMP team | Inside the worker (`#pragma omp parallel`) | Mesh generation, assembly, reductions, Block-CG | Joins before the worker continues |
| I/O thread | `IoService` owned by `FileIoPanel` | Import / export: parsing, writing, snapshot ↔ model conversion | `IoTask` polled every frame; results published through the bridge on the GUI thread |

Rules:
- Only the main thread touches OpenGL and ImGui.
- Only one worker exists at a time. A panel calls `bridge.joinWorker()` before it sets `m_isRunning` / `m_isGeneratingPreview` and starts a new `std::jthread`, because a worker that is still finishing clears those flags on exit.
- `bridge.resetModel(type)` (object type change, Clear, Load Demo, a new import) bumps `modelGeneration` and requests stop without joining. A worker publishes only if `modelGeneration` still has the value it took at start, so a solve that outlives a reset never brings the old model back ([BRIDGE.md](BRIDGE.md) section 4.1).
- The worker never mutates a published `MeshData`. It builds a new one and swaps the `shared_ptr` under `dataMutex`.
- The worker never reads mutable bridge containers directly. `allMaterials` is copied under `dataMutex` on the GUI thread and moved into the worker lambda; supports and loads travel inside the immutable snapshot.
- Thread count: `configureOpenMPForWorker()` (`gui/panels/truss/trussWorker.hpp`) sets OpenMP/Eigen to `cores - 2` when there are more than 4 cores. `main.cpp` calls it once at startup; every worker calls it again because OpenMP thread settings are per thread.

## 5. Startup sequence

1. `anaf::LOG::setCallback(anafUILogSink)` routes every log line into the GUI console buffer.
2. `anaf::LOG::init("anafinen_run.log")` opens the log file in the working directory.
3. `main()` builds the bridge singleton (`buildBridge()`) and loads the built-in materials from `assets/bridge/materialProperties.json` (`setStaticInfo()`). This runs after the log init so a missing or broken file is reported. `loadUserMaterials()` then adds the saved user materials from the user config directory ([BRIDGE.md](BRIDGE.md) section 5.1).
4. OpenMP and Eigen thread counts are configured.
5. `anaf::GUI::initgui()`:
   1. Initializes GLFW and creates an OpenGL 4.6 core window.
   2. Loads GLAD (startup stops with an error if the OpenGL functions cannot be loaded), then ImGui and the fonts.
   3. Creates the `Framebuffer` and registers the panels.
   4. Enters the frame loop.

## 6. Shutdown sequence

1. The frame loop exits when the window is closed.
2. Inside the GL resource scope in `initgui()`:
   - The worker receives `request_stop()` and is joined (`workerThread = std::jthread{}`).
   - Panels, the renderer, and the framebuffer are destroyed while the GL context is still current. `FileIoPanel` destroys its `IoService`, which cancels queued jobs and joins the I/O thread.
3. `imguiLayer.shutdown()` destroys ImGui backends and context.
4. `glfwDestroyWindow()` and `glfwTerminate()` run.
5. `main()` closes the log and returns 1 if `initgui()` failed.
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
| `anaf_io` public API change (`meshIo.hpp`, `ioService.hpp`, `MeshModel` members, options) | [IO_USAGE.md](IO_USAGE.md), [FILE_HANDLING.md](FILE_HANDLING.md) |
| New dependency, CMake option, or package target | [BUILD_SYSTEM.md](BUILD_SYSTEM.md) |
| A known issue is fixed | Remove it from section 8 below |

## 8. Known issues (found while writing these documents)

| # | Issue | Location | Effect |
|---|---|---|---|
| 2 | The Gmsh 4.15 build on Fedora aborts when it opens any binary MSH 4.1 file (its own too). | Gmsh (external) | Only affects opening our binary 4.1 files **in Gmsh**; anafinen reads MSH natively. |
| 4 | Debian 13's `libgmsh4.13` (4.13.1+ds1) is built with Eigen assertions on and aborts inside its own second-order 3D meshing (`gmsh::model::mesh::generate` → `MElement::signedInvCondNumRange` → Eigen `invalid matrix product`). | Gmsh (external), Debian package | `anaf_io_tests` aborts in `highOrderNodeOrderingMatchesGmshVtkWriter` on Debian; the other tests pass when run one by one. A CAD import with element order 2 may abort the application on Debian as well. Fedora and Arch are not affected. |
| 5 | The truss adapter (`trussMeshAdapter.cpp` `toMeshData`) reads only `NodeConstraint::fixed` and `NodalLoad::force`. Rotational fixity, prescribed rotations, nodal moments and `ElementFormulation` (beam) are dropped without a warning. | `anaf_core`, truss adapter | A frame file imported into the truss solver loses its moments; the energy check still passes, because the dropped loads never enter the work term. Fix: warn on (or reject) rotational data and beam formulations until a beam solver exists. |

### 8.1 Deferred by design

- The CLI executable itself does not exist yet. Its prerequisite is done (2026-10-02): `anaf_core` links on its own and `FEM::TRUSS::solveStatic()` takes no GUI type, so a CLI is `main()` + argument parsing + `anaf_io` + `solveStatic()` (with `anaf::LOG::setConsoleOutput(true)`).

### 8.2 Fixed

| Issue | Fixed on | Fix |
|---|---|---|
| Logged floating-point values went through a global fixed-point precision (`setFloatPrecision`, a `FloatArg` wrapper with its own `std::formatter`), so round-off sized values printed as zero (an energy difference of 4e-15 J as `0.0000000000`) and small displacements lost their digits | 2026-10-02 | Wrapper and global setting removed; every floating-point log argument has an explicit format (`{:.3e}`, `{:.6g}`, `{:.1f}`). |
| `anaf_core` could not be linked on its own: the solver took `Gui_Calc_Bridge&`, and the log / asset lookup sources were compiled into the GUI executable (tests added them by hand) | 2026-10-02 | `FEM::TRUSS::solveStatic()` with a progress callback and a `StaticResult`; `MeshData` / `RenderElement` moved to `trussProperties/meshData.hpp` (bridge aliases); log and directory sources in `anaf_core`; the log's `ANAF_GUI` / `ANAF_CLI` macros replaced by a run-time console switch. A failed stiffness solve is an error now instead of zero displacements with a trivially passing energy check, and the logged max displacement is the vector magnitude (was the largest component). |
| Inclined supports (allowed-motion basis) were stored but not solved: the solver used only the axis flags, so a roller on an inclined rail was locked in every axis its rail is not parallel to, and the basis was lost in `setModel()` and on export | 2026-09-29 | The container solves `(Tᵀ K T) q = Tᵀ f` over the allowed directions; `setModel()` and `toMeshModel()` keep the basis ([CALCULATIONS.md](CALCULATIONS.md) section 6). |
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
| Build failed with GCC 14 (Debian 13): `std::vector<std::string>` passed to `std::format` (range formatting needs libstdc++ 15) | 2026-09-28 | `trussSolver_SQPT.cpp` joined the fixed-node list by hand (the file is gone since the single solve path, 2026-10-02). |
| With the AUR `gmsh-bin` SDK (built by an older GCC), Gmsh lost physical names: our `std::map<std::pair<int, int>, std::string>` instantiation was exported from the executable and interposed libgmsh's own copy | 2026-09-28 | `mshFormat.cpp` keys its maps with a file-local `IntPair` type, so the instantiations have internal linkage. See [FILE_HANDLING.md](FILE_HANDLING.md) section 10. |
| Linux packaging: `package.sh` only worked from `package/` on Arch; `PKGBUILD` required `spectra` (AUR, header-only) and `eigen` at run time and LLVM `openmp`; without ImageMagick the SVG was installed as `anafinen.png`; Debian's ImageMagick rendered an empty icon | 2026-09-28 | See [BUILD_SYSTEM.md](BUILD_SYSTEM.md) sections 5 and 8. |
| `readsFilesWrittenByAnafinen012` failed with Gmsh 4.13: the STEP fixture was written in millimetres | 2026-09-28 | The fixture sets `Geometry.OCCTargetUnit` to `M`, like the STEP writer. |
| Imported trusses could not be solved (`Truss_Imported_or_Entered` stub) and there was no way to build a truss by hand | 2026-09-28 | `Truss_Imported_or_Entered` solves any snapshot with the same container / referee / solvers ([CALCULATIONS.md](CALCULATIONS.md) section 3.1); `TrussModelEditor` panel ([GUI.md](GUI.md)). |
| Changing the object type left the old model, fixity, selection and panel inputs behind; Clear All / Load Demo only requested stop, so a running solve could publish the old model afterwards | 2026-09-28 | `Gui_Calc_Bridge::resetModel()` + `modelGeneration`; panels have `resetState()`; the fixity checkboxes of `TrussControlPanel` were function statics and are members now. |
| Energy check reported INVALID on correct solves of models with many inclined bars (e.g. the built-in crane jib, Pratt roof, K-truss) | 2026-09-28 | `TrussElement_1D` stored the direction cosines as `float`, so every stiffness entry `c_i c_j A E / L` carried ~1e-7 relative error, the same size as the validator's threshold. Cosines are `double` now; energy differences dropped from ~1e-3 J to ~1e-10 J. |
| Imported models without sections had to be fixed bar by bar; no ready-made examples | 2026-09-28 | "Whole Model: Section & Material" in the model editor; 31 built-in, generated and tested trusses (4 aircraft structures added 2026-09-29; airship, geodetic fuselage, airliner wing box, oval stadium, stadium arch, Kiewitt dome, three-span bridge and 300 m tower added 2026-10-02) in `assets/objects/truss/truss1D` ([CALCULATIONS.md](CALCULATIONS.md) section 3.2). |
| Viewport: grid lines bent and swam when zoomed in; pitch was clamped to ±89°, zoom to 0.5 … 500 m, the clip planes fixed at 0.1 … 10000 | 2026-10-02 | Fullscreen ray-cast grid with eye-relative coordinates and a density fade; free orbit with a pitch-following up vector; zoom and clip planes scale with the scene ([GUI.md](GUI.md) sections 3.4 and 3.5). |
| Unused code: stub types `Truss` / `TrussBuild` (`truss.hpp`, `selectTrussType.hpp`, known issue 1), `anafGen::IdGenerator` (`src/gen/`), the Eigen determinant self-check (`src/test/`), `Material` setters, unused include paths and includes (Spectra, ImGui backends in `main.cpp`) | 2026-09-28 | Removed. Node labels in the viewport sampled the font atlas `.r` channel (always 1 in ImGui 1.92's RGBA32 atlas) and are now drawn from `.a`; element range errors used printf placeholders with `std::format`. |
| Paths converted with `path::string()` / built from pfd's UTF-8 strings in `anaf_io` (error texts, `report.path`, extension detection, task names, Gmsh `importShapes` / `write` in `cadFormat.cpp`), `fileIoPanel.cpp` and `nativeFileDialog.cpp` (known issue 3): wrong names or an MSVC exception on Windows for files such as `köprü.msh` | 2026-09-28 | All go through `anaf::IO::pathToUtf8()` / `pathFromUtf8()`; the CAD sidecar path is built with `path +=`. Tests still use `string()` on their ASCII temp paths. |
| `m_objectType` started as `truss_SQPT` (value-initialized atomic); `TrussSelector::m_trussType` was uninitialized; the model tree never advanced the node number | 2026-09-28 | Starts as `no_type`; selector starts at Simple Quadrangle; counter fixed. |
