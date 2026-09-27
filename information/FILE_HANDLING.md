# File Handling: Mesh Import and Export

This document describes how `anaf::FILE` reads and writes truss meshes and results in STEP, Gmsh MSH, and legacy VTK formats, and how the Gmsh API session is managed.

> **Document status**
> Verified against: `v0.1.2-alpha` + working tree, 2026-09-27.
> The module is implemented and builds into `anaf_core`, but it is **not connected to the GUI yet**: the File menu entries are "(coming soon)".

## 1. Overall flow

```text
                       import                                        export
  file on disk  ----------------------->  MeshImportData  ----------------------->  file on disk
                                           (shared_ptr)
 .step/.stp/.iges/.brep --importSTEP-->  +-----------------+  --exportSTEP--> .step + .step.anafFields
 .msh                   --importMSH--->  | nodes           |  --exportMSH---> .msh (4.1, with views)
 .vtk                   --importVTK--->  | line elements   |  --exportVTK---> .vtk (legacy ASCII)
                                         | source path     |
                                         | success / error |
                                         +--------+--------+
                                                  |
                                                  v   (planned)
                                   Truss_Imported_or_Entered -> solver -> MeshData
```

Every importer returns `std::shared_ptr<MeshImportData>` and never throws. On failure, `isSuccess() == false`, `getErrorMessage()` holds the exception text, and an error is logged. Every exporter returns `bool` and logs its own result.

## 2. Common data model (`trussFileOperations/truss1D.hpp`)

| Type | Field | Meaning |
|---|---|---|
| `LineElement` | `node1`, `node2` | 0-based indices into the node vector |
| | `materialID` | Index into the material catalog (`bridge.allMaterials`) |
| | `crossSectionArea` | m² |
| | `stress` | Axial stress result in Pa (tension > 0, compression < 0), 0 for an un-analyzed mesh |
| `MeshImportData` | `m_nodes` | `vector<FEM::TRUSS::Node>`: location, displacement, fixity, allowed motion basis |
| | `m_elements` | `vector<LineElement>` |
| | `m_sourcePath`, `m_success`, `m_errorMessage` | Import status |

Node indices are always compact and 0-based inside `MeshImportData`. Format-specific tags (Gmsh tags are 1-based and may be sparse) are remapped on import and regenerated on export.

## 3. Gmsh session (`gmshRuntime.cpp`)

Gmsh keeps one global, non-thread-safe API state per process.

| Function | Behavior |
|---|---|
| `resetGmshSession()` | First call: `gmsh::initialize()`, terminal output off, and registers a static guard that finalizes at exit. Every call: `gmsh::clear()` (empty model). |
| `finalizeGmshSession()` | Explicit `gmsh::finalize()` if initialized |

Rules:
- Every STEP/MSH import or export starts with `resetGmshSession()`, so callers never see leftover models.
- Only one thread may use Gmsh at a time. The module has no lock of its own. Call it from a single worker, never from two workers in parallel.
- VTK does not use Gmsh.

## 4. Format details

### 4.1 STEP (`fileSTEP.cpp`)

**Import:**
1. `Mesh.MeshSizeMin/Max = 1e22` forces exactly one line element per CAD curve.
2. `occ::importShapes()` accepts any OCC-supported CAD file: STEP, IGES, BREP.
3. `occ::removeAllDuplicates()` merges coincident vertices and edges, so curves that only touch become connected nodes.
4. `occ::synchronize()`, then `mesh::generate(1)` meshes curves only.
5. Nodes are extracted with `includeBoundary = false` to avoid listing curve endpoints twice. Only 2-node line elements (Gmsh type 1) are kept.
6. The sidecar `<file>.anafFields` is merged if it exists.

**Export:**
1. Each node becomes an OCC point, and each element becomes an OCC line between its two points.
2. `gmsh::write()` produces the STEP file.
3. Material, area, stress, and displacement are written to the sidecar.

**Sidecar `.anafFields` (plain text, this application only):**
```text
# anafinen auxiliary FEA data (Coordinate-Mapped)
NODES <n>
x y z dx dy dz
...
ELEMENTS <m>
mx my mz materialID area stress        (mx,my,mz = element midpoint)
```
STEP cannot carry physical groups or names through Gmsh's writer (verified by a round trip), so the extra data is matched back by **position**:
- nodes within 1e-5 m
- element midpoints within 1e-4 m

Fixity is not stored in STEP or its sidecar.

### 4.2 Gmsh MSH (`fileMSH.cpp`)

**Import:** `gmsh::open()`. All nodes and all type-1 line elements are read. Then the post-processing views written by `exportMSH` are applied:

| View name | Kind | Target |
|---|---|---|
| `Displacement` | `NodeData`, 3 components | `Node::setDisplacements` |
| `Stress` | `ElementData`, 1 component | `LineElement::stress` |
| `MaterialID` | `ElementData` | `LineElement::materialID` |
| `CrossSectionArea` | `ElementData` | `LineElement::crossSectionArea` |

**Export:** all nodes and elements go into one discrete 1D entity. Gmsh tags are `index + 1`. The four views above are added and the file is written in MSH 4.1 with mesh and views together. Fixity is not stored in MSH.

### 4.3 Legacy VTK (`fileVTK.cpp`)

Hand-written ASCII reader/writer (`DATASET UNSTRUCTURED_GRID`, cell type 3 = line). The output opens directly in ParaView.

| Section | Content | Round-trips |
|---|---|---|
| `POINTS n double` | Node coordinates | yes |
| `CELLS` / `CELL_TYPES` | 2-point lines (other cell sizes ignored on import) | yes |
| `POINT_DATA` → `VECTORS Displacement` | Nodal displacement | yes |
| `POINT_DATA` → `SCALARS FixityX/Y/Z int` | 1 = fixed, 0 = free | yes |
| `POINT_DATA` → `FIELD NodeConstraints` | `AllowedMotionRank` (0..3) + `AllowedMotionBasis` (9 doubles per node) | yes |
| `CELL_DATA` → `SCALARS Stress / MaterialID / CrossSectionArea` | Element data | yes |
| Legacy `VECTORS FixityDirection_*` | Older fixed-direction format, import only | converted to an allowed-motion basis |

When a file has no `NodeConstraints` field, the allowed-motion basis is rebuilt as the orthogonal complement of the fixed directions (Gram-Schmidt in `allowedBasisFromFixedDirections`). **VTK is the only format that preserves boundary conditions.**

## 5. Format capability summary

| Capability | STEP | MSH | VTK |
|---|---|---|---|
| Geometry (nodes, lines) | yes (CAD curves) | yes | yes |
| Displacement | sidecar | view | yes |
| Stress / material / area | sidecar | views | yes |
| Fixity / allowed motion | no | no | yes |
| Opens in external tools | CAD tools | Gmsh | ParaView |
| Needs Gmsh | yes | yes | no |

## 6. Known issues and limits

- Stream output uses the default precision of 6 significant digits (`std::ofstream`). A VTK or sidecar round trip rounds coordinates and results, e.g. `123.456789` becomes `123.457`. Fix: `file << std::setprecision(std::numeric_limits<double>::max_digits10)`.
- Sidecar matching is a nested loop, O(n²) for nodes and O(m²) for elements, so it is slow for large models. A spatial hash on rounded coordinates would make it O(n).
- `exportMSH()` calls `gmsh::write()` twice: once for the mesh, and again inside `appendResultFields()`. The first write is redundant.
- Only 2-node line elements are imported. Surfaces and volumes in STEP/MSH are ignored.

## 7. Related source files

- [src/fileOperations/fileSTEP.hpp](../src/fileOperations/fileSTEP.hpp), [fileSTEP.cpp](../src/fileOperations/fileSTEP.cpp)
- [src/fileOperations/fileMSH.hpp](../src/fileOperations/fileMSH.hpp), [fileMSH.cpp](../src/fileOperations/fileMSH.cpp)
- [src/fileOperations/fileVTK.hpp](../src/fileOperations/fileVTK.hpp), [fileVTK.cpp](../src/fileOperations/fileVTK.cpp)
- [src/fileOperations/gmshRuntime.hpp](../src/fileOperations/gmshRuntime.hpp), [gmshRuntime.cpp](../src/fileOperations/gmshRuntime.cpp)
- [src/fileOperations/objectFileOperations/trussFileOperations/truss1D.hpp](../src/fileOperations/objectFileOperations/trussFileOperations/truss1D.hpp)
