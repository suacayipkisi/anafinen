# Mesh Data and Calculation Flow

This document describes how truss mesh data is created (Simple Quadrangle generator, built-in library, import, model editor), stored, passed through the one solver pipeline, and finally displayed in the viewport.

> **Document status**
> Verified against: `v0.3.0-alpha` (in development; latest release `v0.2.0-alpha`, 2026-10-05), content checked 2026-10-08 (version 0.3.0; 2026-10-05: v0.2.0-alpha release check; 2026-10-04: beam models and their rendering, section 6.1).
> Part of the documentation set indexed in [ARCHITECTURE.md](ARCHITECTURE.md). Module details: [CALCULATIONS.md](CALCULATIONS.md), [BRIDGE.md](BRIDGE.md), [GUI.md](GUI.md).

## 1. Overall flow

Every model reaches the solver as a `MeshData` snapshot; only the source differs.

```text
+---------------------------+   +---------------------------+   +-------------------+
| TrussControlPanel (SQPT)  |   | TrussModelEditor          |   | FileIoPanel       |
| grid / material / loads   |   | nodes / bars / supports   |   | import, library   |
+-------------+-------------+   +-------------+-------------+   +---------+---------+
              |                               |                           |
   Generate Preview / Run Solver        edits (copy + publish)     readMesh -> toMeshData
              |                               |                           |
              v                               |                           |
   buildSimpleTruss() -> MeshData             |                           |
              |                               |                           |
              +---------------+---------------+-------------+-------------+
                              |                             |
                              v                             v
                    +-------------------+          bridge.activeMesh
                    | TRUSS_WORKER::    |          (shared_ptr<const MeshData>)
                    | startSolve(source)|                   |
                    | std::jthread      |                   |
                    +---------+---------+                   |
                              |                             |
                              v                             |
            +------------------------------------+          |
            | FEM::TRUSS::solveStatic()          |          |
            |  buildSolverModel: snapshot ->     |          |
            |    TrussElement_1D, node supports  |          |
            |  setForce: loads -> m_forceVec     |          |
            |  Truss_1D_Container: K triplets,   |          |
            |    self weight, T^T K T q = T^T f, |          |
            |    stress, energy check            |          |
            |  -> StaticResult (solved copy)     |          |
            +------------------+-----------------+          |
                               |                            |
                               +-- publish (same model generation) --> activeMesh
                                                                 |
                                                                 v
                                                  +---------------------------+
                                                  | ViewportPanel             |
                                                  | m_currentMesh             |
                                                  | buildSceneBatches()       |
                                                  +-------------+-------------+
                                                                |
                                                 element lines, node points,
                                                 supports, force arrows
```

## 2. Where data is stored

| Data | Main owner | Storage field | Role in the lifecycle |
|---|---|---|---|
| Mesh nodes | snapshot, solver copy | `MeshData::trussNodes`, `SolverModel::nodes` (inside `solveStatic()`) | Node IDs, original positions, movable state / inclined basis, displacements. |
| Mesh elements | snapshot, solver copy | `MeshData::trussElements` (`RenderElement`), `m_elements` (`TrussElement_1D`) | Node IDs, area, material, stress; the solver copy adds length, direction cosines, elongation and force. |
| GUI mesh snapshot | `Gui_Calc_Bridge` | `activeMesh` | Shared publication point for preview or solver results. |
| Supports | snapshot nodes | `Node::m_allowedMotionDirections` (+ `m_isMovable` summary) | The only place a support is stored: axis fixity or an inclined basis. Editors change it on a snapshot copy; the Simple Quadrangle panel keeps its supports as input (`m_supports`) and puts them on every grid it builds. |
| Applied loads | Panel and snapshot | `m_appliedForces`, `MeshData::appliedForces` | Stores user loads by node and later feeds the global DOF vector. |
| Global force vector | `SolverModel::force` and container span | `m_forceVec` | Uses `index = 3 * nodeId + axis` for X/Y/Z DOFs; element weight is added here. |
| Global stiffness data | `Truss_1D_Container` | `m_globalStiffnessMatrix` | Created as 21 upper-triangle Eigen triplets per element. |
| Reduced system | Local variables in `calculateDisplacements()` | `reducedStiffnessMatrix`, `reducedForceVec` | Solver system over the allowed motion directions (`Tᵀ K T`, `Tᵀ f`). |
| Displacement results | Container and nodes | `m_resultDisplacements`, `Node::m_displacement` | Written to nodes after solving and then copied into the GUI snapshot. |
| Element results | Element objects | elongation, stress | Used by `calculateElementForcesAndStress()` and viewport stress coloring. |
| Validation results | Bridge and container | `m_isValid`, `m_energyDiff`, energy/work fields | Compares internal elastic energy with external work. |
| Render mesh | `ViewportPanel` | `m_currentMesh` | Local read-side snapshot used to draw the active mesh. |

## 3. Calculation sequence

1. A source makes the snapshot: the Simple Quadrangle panel calls `buildSimpleTruss()` on the worker and adds its loads; the model editor passes the active snapshot.
2. `TRUSS_WORKER::startSolve()` copies the material list under `dataMutex`, notes `modelGeneration` and starts the worker.
3. `solveStatic()` (`buildSolverModel()`) turns the snapshot into solver nodes and bars; each node keeps the allowed motion of its snapshot node (its support).
4. `ForceApplied` records are written to `m_forceVec[3 * nodeId + axis]`.
5. `setContainer()` binds the container to the solver vectors through `std::span`. The container does not own the nodes or elements.
6. `assembleStiffness()` creates global stiffness-matrix triplets from the elements.
7. `considerWeight()` adds element weights to the global force vector.
8. `calculateDisplacements()` reduces the system to the allowed motion directions of each node (fixed DOFs drop out, inclined supports are rotated in), solves it, and writes displacements to the nodes.
9. Node locations stay undeformed; the displacement lives only in `Node::m_displacement`. Element elongation and stress are calculated.
10. `runValidator()` performs the energy check and writes status values to the bridge.
11. `solveStatic()` returns a copy of the snapshot with displacements and stresses plus the energy check (`StaticResult`); `startSolve()` publishes it through `bridge.activeMesh` (and `m_isValid` / `m_energyDiff`) if the model was not reset meanwhile.
12. `dataVersion` is incremented. The viewport reads the new snapshot and draws elements, nodes, supports and force arrows.

## 4. Difference between preview and solver

```text
Preview (Simple Quadrangle only):
  buildSimpleTruss() -> MeshData (nodes + bars + loads) -> activeMesh -> Viewport

Solve (every model):
  source() -> solveStatic() -> StaticResult.mesh -> activeMesh -> Viewport
```

The preview contains the geometry and the loads. The solve rebuilds the grid from the panel inputs (so a changed input is used without a new preview) and adds displacements and stresses.

## 5. Snapshot and thread flow

- Preview and solver run in separate `std::jthread` workers.
- `activeMesh` is replaced under `dataMutex` after a new `std::shared_ptr<MeshData>` snapshot is ready.
- The viewport obtains the snapshot pointer under `dataMutex` and then reads it through `m_currentMesh`.
- `dataVersion` signals that the viewport must reload the mesh.
- `MeshData` is published as `shared_ptr<const MeshData>`, so the viewport cannot modify the active snapshot.
- `Gui_Calc_Bridge::deformScale` is a view setting outside the snapshot; it does not change mesh geometry, only the render position `location + displacement * deformScale`. Changing it bumps `dataVersion` without copying the mesh.

## 6. Import and export

```text
Export:  activeMesh --ADAPTER::toMeshModel--> anaf::IO::MeshModel --writeMesh--> .msh / .vtu / .vtk / .step
Import:  file --readMesh--> anaf::IO::MeshModel --ADAPTER::toMeshData--> new MeshData (supports on the nodes) --> activeMesh
```

- Both directions run on the `IoService` thread; only the final pointer swap happens on the GUI thread ([BRIDGE.md](BRIDGE.md) section 5).
- A solved snapshot survives every writable format bit-exactly: positions, displacements, stresses, materials, areas, fixity and loads (`anaf_truss_io_tests`).
- Details: [FILE_HANDLING.md](FILE_HANDLING.md).

### 6.1 Beam models

```text
beam editor edits / Load Example / File > Import (beam file) --> activeBeamMesh (shared_ptr<const FEM::BEAM::MeshData>)
Run Solver for Beam --> BEAM_WORKER::startSolve --> FEM::BEAM::solveStatic (copies of materials, sections) --> activeBeamMesh
Export:  activeBeamMesh --BEAM::ADAPTER::toMeshModel--> MeshModel --writeMesh--> file
Import:  file --readMesh--> MeshModel --isBeamModel? BEAM::ADAPTER::toMeshData--> activeBeamMesh (+ new user sections)
```

The beam snapshot follows the same rules as the truss one (immutable, swapped under `dataMutex`, `dataVersion` bumped). The viewport draws it with the real sections: the exact displacement field is sampled once per snapshot, the deformation scale only moves the stations and turns the section frames (displacements and rotations scaled alike), all on the CPU ([GUI.md](GUI.md) section 3.9). The beam editor, the diagram panel and the model tree read the same snapshot. Details: [CALCULATIONS_BEAM.md](CALCULATIONS_BEAM.md), [GUI.md](GUI.md) sections 2.5-2.7.

## 7. Related source files

- GUI and worker flow: [src/gui/panels/truss/trussWorker.cpp](../src/gui/panels/truss/trussWorker.cpp), [src/gui/panels/truss/simpleQuadrangleTruss/trussControlPanel.cpp](../src/gui/panels/truss/simpleQuadrangleTruss/trussControlPanel.cpp), [src/gui/panels/truss/importedTruss/trussModelEditor.cpp](../src/gui/panels/truss/importedTruss/trussModelEditor.cpp)
- Bridge: [src/bridge/generalStatus.hpp](../src/bridge/generalStatus.hpp)
- Solver entry point: [src/objectCalcs/truss_1D/trussEngine/trussSolver.cpp](../src/objectCalcs/truss_1D/trussEngine/trussSolver.cpp)
- Model types: [src/objectCalcs/truss_1D/trussProperties/meshData.hpp](../src/objectCalcs/truss_1D/trussProperties/meshData.hpp)
- Solver class: [src/objectCalcs/truss_1D/trussEngine/trussSolver.hpp](../src/objectCalcs/truss_1D/trussEngine/trussSolver.hpp)
- Container calculations: [src/objectCalcs/truss_1D/trussEngine/trussSolver/deformationUnderConstForce.cpp](../src/objectCalcs/truss_1D/trussEngine/trussSolver/deformationUnderConstForce.cpp)
- Container data fields: [src/objectCalcs/truss_1D/trussEngine/trussSolver/deformationUnderConstForce.hpp](../src/objectCalcs/truss_1D/trussEngine/trussSolver/deformationUnderConstForce.hpp)
- Mesh generation: [src/objectCalcs/truss_1D/trussTypes/simpleQuadranglePrismTrussCreate.cpp](../src/objectCalcs/truss_1D/trussTypes/simpleQuadranglePrismTrussCreate.cpp)
- Mesh generator: [src/objectCalcs/truss_1D/trussTypes/simpleQuadranglePrismTrussCreate.hpp](../src/objectCalcs/truss_1D/trussTypes/simpleQuadranglePrismTrussCreate.hpp)
- Viewport snapshot reading and drawing: [src/gui/panels/viewportPanel.cpp](../src/gui/panels/viewportPanel.cpp)
- Import / export adapter: [src/objectCalcs/truss_1D/trussIO/trussMeshAdapter.cpp](../src/objectCalcs/truss_1D/trussIO/trussMeshAdapter.cpp)
