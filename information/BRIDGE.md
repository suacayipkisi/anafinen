# GUI - Calculation Bridge

This document describes `anaf::BRIDGE`, the shared state between the GUI thread and the calculation worker. It covers what the bridge stores, who reads and writes each field, and which synchronization rule protects it.

> **Document status**
> Verified against: `v0.1.3-alpha` (released 2026-10-01), content checked 2026-09-29.

## 1. Overall flow

```text
           GUI thread                                   Worker thread (std::jthread)
+------------------------------+                  +-------------------------------------+
| TrussControlPanel            |  start worker    | Preview: SimpleTruss::setTruss()    |
|  - geometry / loads / fixity |----------------->| Solve:   Truss_SQPT pipeline        |
|  - writes fixedDOFsByNode    |                  |                                     |
+---------------+--------------+                  |  writes m_progress (atomic)         |
                |                                 |  builds new MeshData                |
                |                                 +------------------+------------------+
                v                                                    |
+-------------------------------------------------------------------+|+-----------------+
| Gui_Calc_Bridge  (process-wide singleton: buildBridge())           v                  |
|                                                                                       |
|  dataMutex ---- guards --> activeMesh, fixedDOFsByNode, selectedNodeId,               |
|                            hasTrussPreview, allMaterials                              |
|  atomics ------------------> m_isRunning, m_isGeneratingPreview, m_progress,          |
|                              dataVersion, m_isValid, m_energyDiff, m_objectType       |
|  workerThread (std::jthread)                                                          |
|  allMaterials  <-- assets/bridge/materialProperties.json + <user config>/userMaterials |
+----------------------------------------+----------------------------------------------+
                                         |
                                         v  dataVersion changed?
                          +------------------------------+
                          | ViewportPanel / ModelTree    |
                          |  copy activeMesh pointer     |
                          |  under dataMutex, then read  |
                          +------------------------------+
```

## 2. Where data is stored

| Field | Type | Written by | Read by | Protection |
|---|---|---|---|---|
| `activeMesh` | `shared_ptr<const MeshData>` | Worker (publish), control panel (loads, deform scale), model editor (every edit), `resetModel()`, File > Import | Viewport, model tree, both truss panels, File > Export | `dataMutex` |
| `modelGeneration` | `atomic<uint64_t>` | `resetModel()` | Workers (taken at start, compared before publishing) | atomic; the comparison runs under `dataMutex` |
| `dataVersion` | `atomic<uint64_t>` | Every publisher, after swapping `activeMesh` | Viewport (reload check) | atomic, `memory_order_release` on increment |
| `fixedDOFsByNode` | `FixedDOFMap` = `unordered_map<uint32_t, array<bool,3>>` | Control panel ("Apply Fixity", demo), model editor ("Apply Support", node delete), File > Import, `resetModel()` | Both truss panels (copy it for the worker), preview worker, viewport, model tree | `dataMutex`; the solver worker only sees a copy |
| `selectedNodeId` | `uint32_t`, `UINT32_MAX` = none | Viewport picking, both truss panels, `resetModel()` | Both truss panels, viewport | `dataMutex` |
| `hasTrussPreview` | `bool` | Worker, control panel | Panels | `dataMutex` |
| `m_isRunning` | `atomic<bool>` | Truss panels (set), worker (clear), `resetModel()` | Truss panels (button state), File > Import, material removal | atomic |
| `m_isGeneratingPreview` | `atomic<bool>` | Control panel, preview worker, `resetModel()` | Control panel | atomic |
| `m_progress` | `atomic<float>` 0..1 | Worker (solver steps) | Progress bars | atomic |
| `m_isValid`, `m_energyDiff` | atomics | `detail::runStaticSolve()` (both solver classes), `resetModel()` | Panels | atomic (written under `dataMutex`) |
| `m_objectType` | `atomic<ObjectType>`, starts as `no_type` | `resetModel()` only | Model tree, File > Import (refused for `truss_SQPT`), File menu | atomic; written under `dataMutex` |
| `workerThread` | `std::jthread` | Truss panels (after `joinWorker()`) | `initgui()` shutdown, `resetModel()` (stop request) | GUI thread only |
| `allMaterials` | `vector<Material>` | `setStaticInfo()` (built-ins from JSON), `addUserMaterial()`, `removeUserMaterial()` | Control panel (material combo, copies it for the worker), Material Handler, File > Import | `dataMutex`; the solver worker only sees a copy |
| `m_nextMaterialID` (private) | `uint32_t` | `setStaticInfo()`, `addUserMaterial()`, `loadUserMaterials()` | - | `dataMutex` |
| `m_userMaterialPath` (private) | `filesystem::path` | `loadUserMaterials()` (startup) | `saveUserMaterials()` | GUI thread only; empty = not persisted |

`ObjectType` is `truss_SQPT`, `truss_imported_or_entered` or `no_type`. `getObjectTypeName()` converts it to a string for the model tree.

| Object type | Model comes from | Panel | Import | Export |
|---|---|---|---|---|
| `no_type` (start) | - | none | yes (switches to `truss_imported_or_entered`) | yes, once a model exists |
| `truss_SQPT` | Grid generator (Generate Preview / Run Solver) | `TrussControlPanel` | **no** (menu item greyed out, Ctrl+O refused) | yes |
| `truss_imported_or_entered` | File > Import, or node / bar edits | `TrussModelEditor` | yes (replaces the model) | yes |

## 3. `MeshData`: the published snapshot

| Field | Type | Meaning |
|---|---|---|
| `trussNodes` | `vector<FEM::TRUSS::Node>` | ID, location, displacement, movable flags, allowed motion basis |
| `trussElements` | `vector<RenderElement>` | `node1`, `node2`, `stress` (float, Pa), `isStressExceeded`, `materialID`, `crossSectionArea` (m²), `isWireframe` (edge of an imported surface / volume element: drawn, never solved) |
| `appliedForces` | `vector<FEM::TRUSS::ForceApplied>` | Loads to draw as arrows |
| `deformScale` | `atomic<double>` | Render-only displacement multiplier |
| `hasResults` | `bool` | Displacements / stresses come from a solve or a result file (controls what export writes) |

`RenderElement` is a slim copy of `TrussElement_1D`. Only what the viewport and model tree need is kept, which reduces snapshot size and copy time. `stress` is signed (tension > 0, compression < 0). `isStressExceeded` is `|stress| > material.yieldTensileStrength`.

`MeshData` has hand-written copy/move operations because `std::atomic<double>` is neither copyable nor movable.

## 4. Publication protocol

Every writer follows the same copy-on-write sequence:

```text
1. Build or copy:   auto next = std::make_shared<MeshData>(...);   // no lock held
2. Modify next      (loads, deformScale, solved nodes, ...)
3. Swap:            { std::lock_guard lock(bridge.dataMutex); bridge.activeMesh = std::move(next); }
4. Signal:          bridge.dataVersion.fetch_add(1, std::memory_order_release);
```

Every reader does:

```text
1. if (bridge.dataVersion != lastRenderedVersion) or local "needs update" flag:
2.    { std::lock_guard lock(bridge.dataMutex); local = bridge.activeMesh; }   // pointer copy only
3.    read *local freely                                                       // const, never mutated
```

Consequences:
- A reader never sees a half-written mesh; it holds its own `shared_ptr` until it is done.
- Changing one value, for example `deformScale`, copies the whole mesh. This is fine at current sizes but becomes relevant for very large models.
- `dataMutex` is held only for pointer swaps and small map reads, never during a solve.

### 4.1 Model reset and generations

`resetModel(type)` is the only way the model is dropped. Callers: an object type change in the truss selector, Clear All / Load Demo (`TrussControlPanel`), Clear Model (`TrussModelEditor`), and a finished File > Import.

1. `modelGeneration` is incremented.
2. `request_stop()` on `workerThread` (no join: a direct factorization cannot be interrupted, and the GUI must not freeze).
3. `m_isRunning`, `m_isGeneratingPreview` and `m_progress` are cleared.
4. Under `dataMutex`: `activeMesh = nullptr`, fixity cleared, no selection, `hasTrussPreview = false`, `m_isValid = false`, `m_energyDiff = 0`, `m_objectType = type`.
5. `dataVersion` is bumped, so the viewport and the model tree redraw the empty model.

The panels clear their own inputs (loads, checkboxes, selected bar) with `resetState()`; `bindAnalysisFlow()` calls both panels' `resetState()` on a type change and after an import.

Every worker takes `modelGeneration` before it starts and publishes only if the value is unchanged, compared under `dataMutex` in the same critical section as the swap. A worker that outlives a reset therefore finishes silently. Before a panel starts a new worker it calls `joinWorker()` (stop + join the previous one), then sets `m_isRunning` / `m_isGeneratingPreview`; otherwise the old worker's exit would clear the new job's flag.

## 5. Worker lifecycle

| Action (control panel) | Bridge effect |
|---|---|
| Generate Preview | `m_isGeneratingPreview = true`. A new worker builds geometry only and publishes it with fixity overlaid. `selectedNodeId = 0`. |
| Run Solver for Truss | `m_isRunning = true`, `m_progress = 0`. `fixedDOFsByNode` and `allMaterials` are copied under `dataMutex` and moved into the worker. The worker runs the full pipeline ([CALCULATIONS.md](CALCULATIONS.md)) with those copies, overlays the fixity it actually used onto the snapshot nodes, and publishes it. `m_progress = 1`, `m_isRunning = false`. |
| Load Demo | `resetModel(truss_SQPT)`, then sets fixed nodes 0, 10, 220, 230 and a demo load. |
| Clear All | `resetModel(truss_SQPT)` and the panel's `resetState()`. |
| Model editor edit (`TrussModelEditor`) | Disabled while a worker runs. Copies `activeMesh` (or starts an empty one), applies the edit, drops stale results (`hasResults = false`, zero displacements and stresses), publishes under `dataMutex` and bumps `dataVersion`. Node ids stay `0..n-1`: deleting a node removes its bars, load and fixity and moves later ids down by one. |
| Run Solver for Truss (model editor) | Like the control panel's solve, but the worker gets the `activeMesh` pointer itself and runs `Truss_Imported_or_Entered` ([CALCULATIONS.md](CALCULATIONS.md) section 3.1). An unsolvable model (no bars, bars without area, ...) is logged as "Solver not started: ..." and nothing is published. |
| File > Import (FileIoPanel) | Refused for `truss_SQPT` and while `m_isRunning` / `m_isGeneratingPreview`. The I/O thread builds the snapshot. The GUI thread discards it if the type was switched to `truss_SQPT` meanwhile; otherwise it calls `resetModel(truss_imported_or_entered)`, sets `activeMesh`, `fixedDOFsByNode` and `hasTrussPreview` under `dataMutex`, then bumps `dataVersion`. |
| File > Export (FileIoPanel) | Copies the `activeMesh` pointer and `fixedDOFsByNode` under `dataMutex`; conversion and writing run on the I/O thread. |
| Window close | `initgui()` calls `request_stop()`, then joins by assigning an empty `std::jthread`. |

`joinWorker()` stops and joins the previous worker before a new one is assigned, so two workers never run at the same time. Long loops in the solver check `stop_token` between steps.

### 5.1 Materials

Elements refer to a material by its **index** in `allMaterials` (`RenderElement::materialID`, `TrussElement_1D::m_type`), and exported files store that index. `Material::getMaterialID()` is a separate, stable ID that is never reused; the GUI keeps selections by ID.

| Range | Source | Built-in flag | ID | Removable |
|---|---|---|---|---|
| `0 .. n-1` | [assets/bridge/materialProperties.json](../assets/bridge/materialProperties.json), in file order | `true` | `id` field, must be `0..n-1` | No |
| `n ..` | `userMaterials.json` in the user config directory, then Material Handler "Add Material" | `false` | `m_nextMaterialID++` at load / add | Yes, with the rules below |

Loading (`setStaticInfo()`, called by `main()` after the log is initialized):

1. `anaf::DIRECTORY::findAssetPath("bridge/materialProperties.json")` searches next to the executable, `/usr/share/anafinen/assets`, the working directory, then `MAIN_DIR`.
2. `anaf::MATERIAL::loadMaterialLibrary()` (`anaf_core`, nlohmann/json) parses the file, checks every entry with `validateMaterial()`, rejects duplicate names and requires IDs `0..n-1`.
3. On success `allMaterials` is replaced under `dataMutex` and `m_nextMaterialID = n`. On failure an error is logged and the list stays empty; Preview / Solve then log "No material selected" instead of starting.

JSON units are SI: Pa for moduli and strengths, kg/m³ for density, ductility as a fraction. `youngModulus` is optional and defaults to `elasticityModulus`. Mesh files identify materials by name ([FILE_HANDLING.md](FILE_HANDLING.md) section 3), so new built-ins may go anywhere in the file. Keep IDs 0 (steel) and 1 (aluminum) for files from anafinen ≤ 0.1.2, which only carry the index. Names: at most 120 bytes of UTF-8, no `"` and no control characters (they are written into MSH physical names and line-based formats).

Built-in set (20 materials, typical handbook values: ASM, MatWeb, EN / ASTM minimums; the file's `units.notes` repeats the assumptions):

| IDs | Group | Materials |
|---|---|---|
| 0, 1 | Original built-ins (fixed IDs) | Structural Steel (AISI 4130), Aluminum 6061-T6 |
| 2 .. 9 | Steel | S235, S355 (EN 10025), ASTM A36, ASTM A992, AISI 1020 (hot rolled), stainless AISI 304 / 316 (annealed), prestressing strand ASTM A416 (1860 MPa) |
| 10 .. 13 | Aluminum | 2024-T3, 5083-H116, 6063-T6, 7075-T6 |
| 14 .. 17 | Other metals | Titanium Grade 2, Ti-6Al-4V (Grade 5), Magnesium AZ31B-H24, Copper C11000 (annealed) |
| 18, 19 | Wood (12 % moisture, along the grain) | Douglas Fir, Sitka Spruce |

- Metals: G = E / (2 (1 + ν)) and K = E / (3 (1 − 2 ν)), so the constants are consistent for an isotropic material.
- Wood is orthotropic; a truss bar only needs the properties along the grain. `yieldTensileStrength` holds the compression strength parallel to the grain (the lower one, so the yield flag in the viewport is conservative), `ultimateTensileStrength` the tension strength parallel to the grain, and `shearModulus` G_LR. K is only a placeholder that passes validation; the solver never uses G or K.

Adding (`addUserMaterial()`):

1. `validateMaterial()`: positive moduli, strengths and density, ultimate ≥ yield, -1 < ν < 0.5, ductility ≥ 0, non-empty name.
2. Under `dataMutex`: reject a name that already exists (case-insensitive), then append with `isBuiltin = false` and a new ID. Appending never shifts existing indices, so it is allowed while a worker runs.

Removing (`removeUserMaterial(id)`), all under `dataMutex`:

1. Refused for built-ins.
2. Refused while `m_isRunning` or `m_isGeneratingPreview`: the worker publishes indices from its own copy of the list.
3. Refused while an element of `activeMesh` uses the material.
4. Erase. If elements of `activeMesh` use higher indices, a shifted copy is published and `dataVersion` is bumped.

Persistence (`loadUserMaterials(path)`, called by `main()` right after `setStaticInfo()`):

| Platform | File |
|---|---|
| Linux | `$XDG_CONFIG_HOME/anafinen/userMaterials.json`, else `~/.config/anafinen/userMaterials.json` |
| Windows | `<Roaming AppData>\anafinen\userMaterials.json` (`SHGetKnownFolderPath(FOLDERID_RoamingAppData)`, wide API; `%APPDATA%` as fallback) |

1. `anaf::DIRECTORY::getUserConfigDirectory()` gives the directory. It is never inside `assets/`, the build tree or the install prefix, so materials added while testing a build cannot end up in a package.
2. A missing file is an empty list. A file that does not parse or validate is renamed to `userMaterials.json.corrupt` and an error is logged, so the next save does not overwrite the user's data.
3. Entries whose name matches an existing material (case-insensitive, e.g. a later built-in) are skipped with a warning.
4. Every successful add / remove rewrites the file (`saveUserMaterialFile()`: written to `userMaterials.json.tmp`, then renamed over the old file). A failed save is logged; the material stays for the session.
5. Same schema as the library, without `id`. The bridge assigns IDs at load time.

Without a `loadUserMaterials()` call (the tests), user materials are session-only and nothing is written.

## 6. Rules for new worker code

- Copy every mutable bridge container the worker needs under `dataMutex` **before** creating the `std::jthread`, and move the copies into the lambda. Never read `bridge.<container>` from the worker.
- Atomics (`m_progress`, `m_isRunning`, ...) may be written from the worker directly.
- Publish results only through the protocol in section 4, and only if `modelGeneration` is still the value taken at start (section 4.1).
- Call `joinWorker()` before setting `m_isRunning` / `m_isGeneratingPreview` for the new job.
- `buildBridge()` returns a process-wide singleton. The bridge stays a core type on purpose: a planned CLI executable will build a CLI-side bridge instead of the GUI side. Prefer passing `Gui_Calc_Bridge&` explicitly, as `Truss_SQPT` does.

## 7. Related source files

- Bridge and `MeshData`: [src/bridge/generalStatus.hpp](../src/bridge/generalStatus.hpp), [src/bridge/generalStatus.cpp](../src/bridge/generalStatus.cpp)
- Worker creation and publishing: [src/gui/panels/truss/simpleQuadrangleTruss/trussControlPanel.cpp](../src/gui/panels/truss/simpleQuadrangleTruss/trussControlPanel.cpp), [src/gui/panels/truss/importedTruss/trussModelEditor.cpp](../src/gui/panels/truss/importedTruss/trussModelEditor.cpp)
- Object type switch and panel resets: [src/gui/gui.cpp](../src/gui/gui.cpp) (`bindAnalysisFlow`)
- Snapshot consumer: [src/gui/panels/viewportPanel.cpp](../src/gui/panels/viewportPanel.cpp), [src/gui/panels/modelTree.cpp](../src/gui/panels/modelTree.cpp) (copies the pointer under the lock; no deep copy per frame)
- Import / export publisher: [src/gui/panels/fileIoPanel.cpp](../src/gui/panels/fileIoPanel.cpp)
- Materials: [src/material/properties.hpp](../src/material/properties.hpp), [src/material/materialLibrary.hpp](../src/material/materialLibrary.hpp), [src/material/materialLibrary.cpp](../src/material/materialLibrary.cpp), [assets/bridge/materialProperties.json](../assets/bridge/materialProperties.json)
- Material editor: [src/gui/panels/materialHandler.cpp](../src/gui/panels/materialHandler.cpp)
- Asset lookup: [src/directory/getExecutableDirectory.cpp](../src/directory/getExecutableDirectory.cpp)
