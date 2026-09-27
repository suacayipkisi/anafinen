# Mesh Data and Calculation Flow

This document describes how mesh data for the Simple Quadrangle Prism Truss is created, stored, passed through the solver, and finally displayed in the viewport.

> **Document status**
> Verified against: `v0.1.2-alpha` + working tree, 2026-09-28.
> Part of the documentation set indexed in [ARCHITECTURE.md](ARCHITECTURE.md). Module details: [CALCULATIONS.md](CALCULATIONS.md), [BRIDGE.md](BRIDGE.md), [GUI.md](GUI.md).

## 1. Overall flow

The following diagram uses a terminal-style layout to show the main data path:

```text
+-------------------------------+
| TrussControlPanel             |
| onImGuiRender()               |
| geometry / material / loads   |
| fixed DOFs                    |
+---------------+---------------+
      |
      +-- Generate Preview
      |       |
      |       v
      |  +-------------+
      |  | Preview     |
      |  | worker      |
      |  +------+------+
      |         |
      |         v
      |  +-------------+
      |  | SimpleTruss |
      |  | setTruss()  |
      |  +------+------+
      |         |
      |         v
      |  +-------------+
      |  | Preview     |
      |  | MeshData    |
      |  +------+------+
      |         |
      |         +-- publish --> activeMesh
      |
      +-- Run Solver
         |
         v
         +-------------+
         | Solver      |
         | std::jthread|
         +------+------+
           |
           v
         +-------------+
         | Truss_SQPT  |
         | m_truss     |
         +------+------+
           |
           +-- fixedDOFsByNode
           |   -> Node::setMovable()
           +-- ForceApplied -> m_forceVec
           |                  (3 DOF per node)
           v
         +------------------------+
         | Truss_1D_Container     |
         | spans: nodes/elements  |
         +------------+-----------+
            |
            v
         +------------------------+
         | assembleStiffness()    |
         | global stiffness       |
         | triplets               |
         +------------+-----------+
            |
            v
         +------------------------+
         | solveDisplacements()   |
         | remove fixed DOFs      |
         | solve sparse system    |
         +------------+-----------+
            |
            v
         +------------------------+
         | displacement / stress |
         | force / energy check   |
         +------------+-----------+
            |
            v
         +------------------------+
         | Solved MeshData        |
         +------------+-----------+
            |
            +-- publish --> activeMesh

          +-----------------------+
          | activeMesh            |
          | shared_ptr<const      |
          | MeshData>             |
          +-----------+-----------+
            |
            v
          +-----------------------+
          | ViewportPanel         |
          | m_currentMesh         |
          | buildSceneBatches()   |
          +-----------+-----------+
            |
        +-------------+-------------+
        |             |             |
        v             v             v
        element lines  node points   force arrows
        stress colors  fixity        applied loads
```

## 2. Where data is stored

| Data | Main owner | Storage field | Role in the lifecycle |
|---|---|---|---|
| Mesh nodes | `SimpleTruss` or snapshot | `m_allNodes` / `MeshData::trussNodes` | Stores node IDs, original positions, movable state, and displacements. |
| Mesh elements | `SimpleTruss` or snapshot | `m_allElements` / `MeshData::trussElements` | Stores node IDs, length, direction cosines, area, material, stress, and force. |
| GUI mesh snapshot | `Gui_Calc_Bridge` | `activeMesh` | Shared publication point for preview or solver results. |
| Boundary conditions | `Gui_Calc_Bridge` | `fixedDOFsByNode` | Stores `nodeId -> {fixedX, fixedY, fixedZ}` and becomes `Node::setMovable` state before solving. |
| Applied loads | Panel and snapshot | `m_appliedForces`, `MeshData::appliedForces` | Stores user loads by node and later feeds the global DOF vector. |
| Global force vector | `Truss_SQPT` and container span | `m_forceVec` | Uses `index = 3 * nodeId + axis` for X/Y/Z DOFs; element weight is added here. |
| Global stiffness data | `Truss_1D_Container` | `m_globalStiffnessMatrix` | Created as 36 Eigen triplets per element. |
| Reduced system | Local variables in `calculateDisplacements()` | `reducedStiffnessMatrix`, `reducedForceVec` | Solver system after fixed DOFs are removed. |
| Displacement results | Container and nodes | `m_resultDisplacements`, `Node::m_displacement` | Written to nodes after solving and then copied into the GUI snapshot. |
| Element results | Element objects | elongation, axial force, stress | Used by `calculateElementForcesAndStress()` and viewport stress coloring. |
| Validation results | Bridge and container | `m_isValid`, `m_energyDiff`, energy/work fields | Compares internal elastic energy with external work. |
| Render mesh | `ViewportPanel` | `m_currentMesh` | Local read-side snapshot used to draw the active mesh. |

## 3. Calculation sequence

1. The panel collects geometry and material parameters from its local GUI state.
2. `Truss_SQPT` calls `SimpleTruss::setTruss()`.
3. `setTruss()` creates grid nodes and X/Y/Z edge elements plus XY/XZ/YZ diagonal elements.
4. A copy of `fixedDOFsByNode` (taken under `dataMutex` before the worker starts) is converted into `movable = !fixed` for each node.
5. `ForceApplied` records are written to `m_forceVec[3 * nodeId + axis]`.
6. `setContainer()` binds the container to the `m_truss` vectors through `std::span`. The container does not own the nodes or elements.
7. `assembleStiffness()` creates global stiffness-matrix triplets from the elements.
8. `considerWeight()` adds element weights to the global force vector.
9. `calculateDisplacements()` removes fixed DOFs, solves the sparse system, and writes displacements to the nodes.
10. Node locations stay undeformed; the displacement lives only in `Node::m_displacement`. Element elongation, axial force, and stress are calculated.
11. `runValidator()` performs the energy check and writes status values to the bridge.
12. Nodes and elements are copied into a new `MeshData` snapshot and published through `bridge.activeMesh`.
13. `dataVersion` is incremented. The viewport reads the new snapshot and draws elements, nodes, and force arrows.

## 4. Difference between preview and solver

```text
Preview:
  SimpleTruss::setTruss()
      -> MeshData(node + element + loads)
      -> activeMesh
      -> Viewport

Solver:
  Truss_SQPT::m_truss.setTruss()
     -> boundary conditions + force vector
     -> Truss_1D_Container calculation
     -> displacement/stress/force results
     -> solved MeshData snapshot
     -> activeMesh
     -> Viewport
```

The preview mesh contains the geometric mesh and GUI visualization state. The solver mesh contains the same node and element structure after displacement, element force, and stress fields have been calculated.

## 5. Snapshot and thread flow

- Preview and solver run in separate `std::jthread` workers.
- `activeMesh` is replaced under `dataMutex` after a new `std::shared_ptr<MeshData>` snapshot is ready.
- The viewport obtains the snapshot pointer under `dataMutex` and then reads it through `m_currentMesh`.
- `dataVersion` signals that the viewport must reload the mesh.
- `MeshData` is published as `shared_ptr<const MeshData>`, so the viewport cannot modify the active snapshot.
- `deformScale` does not change mesh geometry; it affects render position through `location + displacement * deformScale`.

## 6. Import and export

```text
Export:  activeMesh (+ fixedDOFsByNode) --ADAPTER::toMeshModel--> anaf::IO::MeshModel --writeMesh--> .msh / .vtu / .vtk / .step
Import:  file --readMesh--> anaf::IO::MeshModel --ADAPTER::toMeshData--> new MeshData --> activeMesh (+ fixedDOFsByNode)
```

- Both directions run on the `IoService` thread; only the final pointer swap happens on the GUI thread ([BRIDGE.md](BRIDGE.md) section 5).
- A solved snapshot survives every writable format bit-exactly: positions, displacements, stresses, materials, areas, fixity and loads (`anaf_truss_io_tests`).
- Details: [FILE_HANDLING.md](FILE_HANDLING.md).

## 7. Related source files

- GUI and worker flow: [src/gui/panels/truss/simpleQuadrangleTruss/trussControlPanel.cpp](../src/gui/panels/truss/simpleQuadrangleTruss/trussControlPanel.cpp)
- Bridge and `MeshData`: [src/bridge/generalStatus.hpp](../src/bridge/generalStatus.hpp)
- Solver orchestration: [src/objectCalcs/truss_1D/trussEngine/trussSolver_SQPT.cpp](../src/objectCalcs/truss_1D/trussEngine/trussSolver_SQPT.cpp)
- Solver class: [src/objectCalcs/truss_1D/trussEngine/trussSolver.hpp](../src/objectCalcs/truss_1D/trussEngine/trussSolver.hpp)
- Container calculations: [src/objectCalcs/truss_1D/trussEngine/trussSolver/deformationUnderConstForce.cpp](../src/objectCalcs/truss_1D/trussEngine/trussSolver/deformationUnderConstForce.cpp)
- Container data fields: [src/objectCalcs/truss_1D/trussEngine/trussSolver/deformationUnderConstForce.hpp](../src/objectCalcs/truss_1D/trussEngine/trussSolver/deformationUnderConstForce.hpp)
- Mesh generation: [src/objectCalcs/truss_1D/trussTypes/simpleQuadranglePrismTrussCreate.cpp](../src/objectCalcs/truss_1D/trussTypes/simpleQuadranglePrismTrussCreate.cpp)
- Mesh generator class: [src/objectCalcs/truss_1D/trussTypes/simpleQuadranglePrismTrussCreate.hpp](../src/objectCalcs/truss_1D/trussTypes/simpleQuadranglePrismTrussCreate.hpp)
- Viewport snapshot reading and drawing: [src/gui/panels/viewportPanel.cpp](../src/gui/panels/viewportPanel.cpp)
- Import / export adapter: [src/objectCalcs/truss_1D/trussIO/trussMeshAdapter.cpp](../src/objectCalcs/truss_1D/trussIO/trussMeshAdapter.cpp)
