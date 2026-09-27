# GUI - Calculation Bridge

This document describes `anaf::BRIDGE`, the shared state between the GUI thread and the calculation worker. It covers what the bridge stores, who reads and writes each field, and which synchronization rule protects it.

> **Document status**
> Verified against: `v0.1.2-alpha` + working tree, 2026-09-28.

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
|                            hasTrussPreview                                            |
|  atomics ------------------> m_isRunning, m_isGeneratingPreview, m_progress,          |
|                              dataVersion, m_isValid, m_energyDiff, m_objectType       |
|  workerThread (std::jthread)                                                          |
|  allMaterials / createdMaterials                                                      |
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
| `activeMesh` | `shared_ptr<const MeshData>` | Worker (publish), control panel (loads, deform scale, clear) | Viewport, model tree, control panel | `dataMutex` |
| `dataVersion` | `atomic<uint64_t>` | Every publisher, after swapping `activeMesh` | Viewport (reload check) | atomic, `memory_order_release` on increment |
| `fixedDOFsByNode` | `FixedDOFMap` = `unordered_map<uint32_t, array<bool,3>>` | Control panel ("Apply Fixity", demo, clear) | Control panel (copies it for the worker), preview worker, viewport, model tree | `dataMutex`; the solver worker only sees a copy |
| `selectedNodeId` | `uint32_t`, `UINT32_MAX` = none | Viewport picking, control panel | Control panel, viewport | `dataMutex` |
| `hasTrussPreview` | `bool` | Worker, control panel | Panels | `dataMutex` |
| `m_isRunning` | `atomic<bool>` | Control panel (set), worker (clear) | Control panel (button state) | atomic |
| `m_isGeneratingPreview` | `atomic<bool>` | Control panel, preview worker | Control panel | atomic |
| `m_progress` | `atomic<float>` 0..1 | Worker (`Truss_SQPT` steps) | Control panel progress bar | atomic |
| `m_isValid`, `m_energyDiff` | atomics | `Truss_SQPT::calculate()` | Panels | atomic (written under `dataMutex`) |
| `m_objectType` | `atomic<ObjectType>` | - | Model tree | atomic |
| `workerThread` | `std::jthread` | Control panel | `initgui()` shutdown | GUI thread only |
| `allMaterials` | `vector<Material>` | `setStaticInfo()`, `setDynamicMaterialInfo()` | Control panel (material combo, copies it for the worker) | `dataMutex` for the worker copy; the solver worker only sees a copy |
| `createdMaterials` | `vector<Material>` | - (reserved) | - | - |

`ObjectType` is `truss_SQPT`, `truss_imported_or_entered` or `no_type`. `getObjectTypeName()` converts it to a string for the model tree.

## 3. `MeshData`: the published snapshot

| Field | Type | Meaning |
|---|---|---|
| `trussNodes` | `vector<FEM::TRUSS::Node>` | ID, location, displacement, movable flags, allowed motion basis |
| `trussElements` | `vector<RenderElement>` | `node1`, `node2`, `stress` (float, Pa), `isStressExceeded`, `materialID`, `crossSectionArea` (m²) |
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

## 5. Worker lifecycle

| Action (control panel) | Bridge effect |
|---|---|
| Generate Preview | `m_isGeneratingPreview = true`. A new worker builds geometry only and publishes it with fixity overlaid. `selectedNodeId = 0`. |
| Run Solver for Truss | `m_isRunning = true`, `m_progress = 0`. `fixedDOFsByNode` and `allMaterials` are copied under `dataMutex` and moved into the worker. The worker runs the full pipeline ([CALCULATIONS.md](CALCULATIONS.md)) with those copies, overlays the fixity it actually used onto the snapshot nodes, and publishes it. `m_progress = 1`, `m_isRunning = false`. |
| Load Demo | Stops the running worker. Sets fixed nodes 0, 10, 220, 230 and a demo load. |
| Clear All | `request_stop()`, resets flags, clears `activeMesh`, fixity, and selection, bumps `dataVersion`. |
| File > Import (FileIoPanel) | Refused while `m_isRunning` / `m_isGeneratingPreview`. The I/O thread builds the snapshot. The GUI thread then sets `activeMesh`, `fixedDOFsByNode`, `hasTrussPreview`, clears the selection and sets `m_objectType = truss_imported_or_entered` under `dataMutex`, then bumps `dataVersion`. |
| File > Export (FileIoPanel) | Copies the `activeMesh` pointer and `fixedDOFsByNode` under `dataMutex`; conversion and writing run on the I/O thread. |
| Window close | `initgui()` calls `request_stop()`, then joins by assigning an empty `std::jthread`. |

Assigning a new `std::jthread` to `workerThread` destroys the old one. `std::jthread`'s destructor calls `request_stop()` and joins, so two workers never run at the same time. Long loops in the solver check `stop_token` between steps.

## 6. Rules for new worker code

- Copy every mutable bridge container the worker needs under `dataMutex` **before** creating the `std::jthread`, and move the copies into the lambda. Never read `bridge.<container>` from the worker.
- Atomics (`m_progress`, `m_isRunning`, ...) may be written from the worker directly.
- Publish results only through the protocol in section 4.
- `buildBridge()` returns a process-wide singleton. The bridge stays a core type on purpose: a planned CLI executable will build a CLI-side bridge instead of the GUI side. Prefer passing `Gui_Calc_Bridge&` explicitly, as `Truss_SQPT` does.

## 7. Related source files

- Bridge and `MeshData`: [src/bridge/generalStatus.hpp](../src/bridge/generalStatus.hpp), [src/bridge/generalStatus.cpp](../src/bridge/generalStatus.cpp)
- Worker creation and publishing: [src/gui/panels/truss/simpleQuadrangleTruss/trussControlPanel.cpp](../src/gui/panels/truss/simpleQuadrangleTruss/trussControlPanel.cpp)
- Snapshot consumer: [src/gui/panels/viewportPanel.cpp](../src/gui/panels/viewportPanel.cpp), [src/gui/panels/modelTree.cpp](../src/gui/panels/modelTree.cpp) (copies the pointer under the lock; no deep copy per frame)
- Import / export publisher: [src/gui/panels/fileIoPanel.cpp](../src/gui/panels/fileIoPanel.cpp)
- Built-in materials: [src/material/properties.hpp](../src/material/properties.hpp)
