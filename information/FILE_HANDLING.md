# File Handling: the anaf_io Library

This document describes `anaf_io`, the mesh import/export library:
- its format-neutral data model
- every supported format and version
- the asynchronous service
- how the GUI (and a future CLI) use it

> **Document status**
> Verified against: `v0.1.3-alpha` working tree (unreleased), 2026-09-28.
> Replaces the former `src/fileOperations` module (STEP/MSH through the Gmsh API, custom VTK), which was removed.

## 1. Overall flow

```text
                                   anaf_io (static library, no solver / GUI dependency)
                 +-----------------------------------------------------------------------------+
  file on disk   |  meshIo.hpp: readMesh / writeMesh  (synchronous, any thread)                |
  .msh .vtk .vtu |     |                                                                       |
  .step .stp     |     +-- detectFormat (extension, then content sniffing)                     |
  .iges .igs     |     +-- formats/mshFormat.cpp        native MSH 1 / 2.x / 4.0 / 4.1         |
  .brep          |     +-- formats/vtkLegacyFormat.cpp  native legacy VTK 2.0 ... 5.1          |
       <-------->|     +-- formats/vtuFormat.cpp        native VTK XML (.vtu), zlib            |
                 |     +-- formats/cadFormat.cpp        STEP / IGES / BREP via Gmsh + OCC      |
                 |                  |                                                          |
                 |                  v                                                          |
                 |          model/meshModel.hpp: MeshModel (nodes, element blocks, sets,       |
                 |          fields with time steps, constraints, loads, element attributes)    |
                 |                                                                             |
                 |  service/ioService.hpp: IoService (one I/O thread, IoTask polling, cancel)  |
                 +-----------------------------------------------------------------------------+
                            ^                                   ^
                            |                                   |
      anaf_core: truss_1D/trussIO/trussMeshAdapter      GUI: panels/fileIoPanel + native dialog
      MeshModel <-> BRIDGE::MeshData                    (future CLI: readMesh / writeMesh directly)
```

## 2. Target and dependency rules

| Rule | Why |
|---|---|
| `anaf_io` depends only on Gmsh (CAD formats) and zlib (VTU compression), both PRIVATE | GUI, CLI and tests link the same library without pulling in solver or GUI code |
| Public headers: `io/meshIo.hpp`, `io/service/ioService.hpp`, `io/model/*.hpp`, `io/core/ioTypes.hpp` | `io/detail/*` and `io/formats/*` are internal |
| Solver-specific conversions live outside `anaf_io` (`trussMeshAdapter` in `anaf_core`) | New object types (beams, shells, solids) add their own adapter; formats do not change |
| No GUI macros (`ANAF_GUI`) in shared headers | Libraries are compiled once and linked into several executables; a macro that changes a type would break the ODR |

## 3. The data model (`io/model/meshModel.hpp`)

`MeshModel` follows the concepts used by general FEM codes (Gmsh, Exodus, MED, Abaqus input): nodes, typed element blocks, named sets, and fields per time step.

| Member | Type | Meaning |
|---|---|---|
| `nodes` | `vector<Node{tag, position}>` | `tag` is the id from the file; everything else uses the 0-based index |
| `blocks` | `vector<ElementBlock>` | One block per element type: `tags`, `connectivity` (node indices, **Gmsh local order**), `entityTags` (geometric entity per element) |
| `sets` | `vector<EntitySet>` | Named node or element sets: `name`, `kind`, `dimension`, `tag` (physical tag), `members` |
| `fields` | `vector<Field>` | `name`, `location` (node / element), `components`, `times[s]`, `steps[s][entity * components + c]` |
| `constraints` | `vector<NodeConstraint>` | `fixed[3]` per global axis plus optional `allowedMotion` basis (inclined supports) |
| `loads` | `vector<NodalLoad>` | Nodal force [N] |
| `elementAttributes` | `map<string, vector<double>>` | Per-element scalars: `MaterialID`, `CrossSectionArea` [m²], and any future attribute (thickness, …) |
| `lengthUnit`, `title`, `warnings` | | Metadata; readers append non-fatal issues to `warnings` |

- Global element index: block 0 elements first, then block 1, and so on. Sets, element fields and attributes are indexed by it.
- `validate()` returns a message for every inconsistency (sizes, indices). Every writer refuses an invalid model; every reader validates its result.
- Well-known names: `FieldName::Displacement` (node, 3), `FieldName::Stress` (element, Pa, tension > 0), `FieldName::AxialForce`, `Attribute::MaterialId`, `Attribute::CrossSectionArea`.

### 3.1 Element types (`io/model/elementType.*`)

One table is the single source of truth for every format. Each row holds the dimension, node count, Gmsh type id, VTK cell id, the Gmsh → VTK node permutation, and the edge list used for wireframe previews.

| Type | Gmsh | VTK | Permuted between Gmsh and VTK |
|---|---|---|---|
| Point1, Line2, Line3, Tri3, Tri6, Quad4, Quad8, Quad9 | 15, 1, 8, 2, 9, 3, 16, 10 | 1, 3, 21, 5, 22, 9, 23, 28 | no |
| Tet4, Tet10 | 4, 11 | 10, 24 | Tet10: nodes 8 ↔ 9 |
| Hex8, Hex20, Hex27 | 5, 17, 12 | 12, 25, 29 | Hex20 / Hex27: mid-edge (and face) nodes |
| Prism6, Prism15 | 6, 18 | 13, 26 | Prism15 |
| Pyramid5, Pyramid13 | 7, 19 | 14, 27 | Pyramid13 |

The permutations are verified by a test that compares Gmsh's own VTK writer against its MSH output. A `static_assert` keeps the table in sync with the enum.

**Adding an element type:** add the enum value, add one table row, and extend `vtkFromGmsh` / `edges` if needed. Every format supports it automatically.

### 3.2 Model codec (`io/detail/modelCodec.*`)

Formats that store named arrays (VTK, VTU, the MSH data sections, the STEP sidecar) map the structured parts of the model to arrays. They all use the same names, so a model written in one format reads back identically from another.

| Array (location, components) | Holds |
|---|---|
| `Fixity` (node, 3) | 1 = fixed, 0 = free |
| `AllowedMotionBasis` (node, 10) | rank + 3×3 free-direction basis; only written when a node has an inclined support |
| `NodalForce` (node, 3) | loads [N] |
| `MaterialID`, `CrossSectionArea`, `Attribute:<name>` (element, 1) | element attributes |
| `NodeSet:<name>` / `ElementSet:<name>` (1) | set membership (1 = member) |
| `NodeTag`, `ElementTag`, `EntityTag` (1) | original ids (VTK / VTU only; MSH stores them natively) |

**Materials by name (truss adapter, 0.1.3):** `toMeshModel()` writes one element set `Material:<material name>` per material used, in addition to `MaterialID`. Sets survive every format (MSH physical groups, `ElementSet:Material:<name>` arrays in VTK / VTU / sidecar), so `toMeshData()` matches bars to the current material list by name (ASCII case-insensitive). An unknown name, or a `MaterialID` outside the list, falls back to material 0 with a warning note. Files without material sets (anafinen ≤ 0.1.2) use `MaterialID`, which matches the built-in order (0 steel, 1 aluminum). MSH 4.1 stores elements per entity, so a multi-material model reads back grouped by material; node pairs, materials and results stay matched (tested for MSH 4.1 / 2.2, VTK, VTU, STEP + sidecar).

On read, the legacy names written by anafinen ≤ 0.1.2 are accepted too: `FixityX/Y/Z`, `AllowedMotionRank` + 9-component `AllowedMotionBasis`, and `FixityDirection_*`.

## 4. Formats

### 4.1 Gmsh MSH: native (`formats/mshFormat.cpp`)

| Version | Read | Write |
|---|---|---|
| 1.0 (`$NOD` / `$ELM`) | yes | – |
| 2.0 / 2.1 / 2.2 | ASCII + binary | 2.2 ASCII + binary |
| 4.0 | ASCII (Gmsh cannot write binary 4.0 either) | – |
| 4.1 | ASCII + binary | ASCII + binary |

- **Sections read:** `$MeshFormat`, `$PhysicalNames`, `$Entities`, `$Nodes`, `$Elements`, and `$NodeData` / `$ElementData` (every time step). All other sections (`$InterpolationScheme`, `$Periodic`, …) are skipped.
- **Physical groups ↔ element sets.** Groups of dimension 0 become node sets. Groups that share a name across dimensions merge into one set.
- **MSH 2.2 write:** an element that belongs to several sets is listed once per group with the same tag. This is Gmsh's own convention, and the reader merges the duplicates back.
- **MSH 4.1 write:** one entity per (dimension, source entity, element type, set membership). The source entity tag is kept when that is unambiguous. Gmsh drops elements when one entity mixes element orders, which is why element type is part of the key.
- **Values:** node and element tags are preserved, including tags of unreferenced nodes. Doubles are written in shortest round-trip form (`std::to_chars`), so ASCII values are exact.
- **Why native instead of the Gmsh API** (all verified while writing the module):
  1. The Gmsh 4.15 build on Fedora aborts (`_GLIBCXX_ASSERTIONS`, `readMSH4Physicals`) on **any binary MSH 4.1 file**, including the ones it writes itself. The old importer would have crashed the whole application on such a file.
  2. Gmsh's MSH 2.2 writer renumbers node and element tags and drops unreferenced nodes.
  3. Gmsh writes ASCII doubles with 16 significant digits, which does not round-trip exactly.
  4. With `Mesh.SaveAll = 1`, Gmsh's MSH 2 writer writes physical tag 0 for every element.
  5. The Gmsh API holds global state and needs a process-wide lock; native code runs on any thread.

### 4.2 Legacy VTK: native (`formats/vtkLegacyFormat.cpp`)

- **Versions:** read 2.0 … 5.1 (5.x `OFFSETS` / `CONNECTIVITY` layout); write 4.2 (classic `CELLS`, readable everywhere) or 5.1.
- **Encoding:** ASCII and binary. Binary data is big-endian by specification.
- **Datasets read:** `UNSTRUCTURED_GRID` and `POLYDATA` (`VERTICES`, `LINES`, `POLYGONS`, `TRIANGLE_STRIPS`).
- **Attributes read:** `SCALARS`, `COLOR_SCALARS`, `VECTORS`, `NORMALS`, `TEXTURE_COORDINATES`, `TENSORS`, `TENSORS6`, `GLOBAL_IDS`, `PEDIGREE_IDS`, `FIELD` arrays. `METADATA` blocks and dataset-level `FIELD` data are skipped.
- **Composite cells are split:** poly-vertex → points, poly-line → Line2 segments, triangle strip → triangles, polygon → Tri3 / Quad4 / fan triangles, pixel → Quad4, voxel → Hex8. Cell data is copied to every produced element.
- **Array names** with whitespace are escaped as `%XX`, like VTK does.
- **Precision:** an ASCII `float` array is read with float precision, exactly as VTK reads it.
- **Time steps:** one step per file. The step is chosen with `WriteOptions::timeStep` (default: last), and a warning is added when other steps are dropped.

### 4.3 VTK XML `.vtu`: native (`formats/vtuFormat.cpp`)

- **Read:**
  - file versions 0.1 / 1.0 / 2.x
  - `header_type` UInt32 or UInt64; little or big endian
  - `DataArray` format `ascii`, `binary` (base64), or `appended` (raw or base64)
  - `vtkZLibDataCompressor`
  - any number of `<Piece>` elements (merged)
  - VTK 9 `<InformationKey>` children inside `DataArray`s are skipped
- **Write:** version 1.0, UInt64 headers, little endian, `ascii` or inline `binary`, optional zlib compression.
- **Not supported:** LZ4 / LZMA compressors; the reader reports a clear error. Only `UnstructuredGrid` files are read.

### 4.4 CAD: STEP / IGES / BREP through Gmsh + OpenCASCADE (`formats/cadFormat.cpp`)

**Import** (`ReadOptions`):

| Option | Default | Effect |
|---|---|---|
| `cadMeshDimension` | 1 | 1 = bars (one per CAD edge, trusses and frames), 2 = surface triangles, 3 = volume tetrahedra |
| `cadMeshSize` | 0 | Element size in metres; 0 = one element per edge (dim 1) or automatic size (dim 2/3) |
| `cadElementOrder` | 1 | 1 = linear, 2 = quadratic |
| `cadKeepLowerDimensions` | false | Also keep boundary elements (e.g. triangles of a tet mesh) |
| `readSidecar` | true | Merge `<file>.anafFields` if present |

- Geometry is converted to metres (`Geometry.OCCTargetUnit = M`).
- **Bars:** crossing members (X-bracing) are **not** fragmented. The old importer split them at their intersection and connected them, which changes the structure. Coincident end nodes are merged after meshing, and coincident duplicate edges (IGES / multi-body files repeat edges) are removed so no member counts twice.
- **Surfaces / volumes:** the geometry is fragmented first so touching bodies get conformal interfaces.
- **IGES:** OCC exports the faces of a solid, not the solid itself. Mesh IGES solids as surfaces (dimension 2).

**Export (STEP only):**
- Every node becomes a CAD vertex and every line element a straight edge. Line3 is written as a straight edge (mid node dropped).
- Other element types are skipped with a warning; STEP is a geometry format.
- OCC labels STEP lengths in millimetres and scales the coordinates accordingly (0.1 m → `100.`), so CAD tools read the right size.

**Sidecar `<file>.anafFields`, version 2** (everything STEP cannot carry):
```text
ANAFINEN_SIDECAR 2
UNIT m
NODES <n>
x y z                                  (all model nodes, shortest round-trip doubles)
ELEMENTS <m>
2 <node index> <node index>            (exported line elements, indices into NODES)
FIELD <N|E> <components> <steps> <name>
TIME <t>
<values, one row per node / element>   (fields + codec arrays: BCs, loads, attributes, sets)
END
```
- **Matching on read:**
  - Nodes are matched by position: grid hash, tolerance 1e-6 × model size, O(1) per node.
  - Elements are matched by their matched end nodes. Centroids are not used, because crossing X-braces share their midpoint.
- **Version 1** (anafinen ≤ 0.1.2: `NODES … x y z dx dy dz`, `ELEMENTS … mx my mz material area stress`) is still read, by position.

**Gmsh session (`detail/gmshSession.*`):**
- One process-wide mutex serializes every Gmsh call.
- Each session clears the model and restores all option defaults, so settings such as mesh size limits cannot leak into the next import.
- Gmsh is initialized without reading the user's `gmshrc`.

## 5. Synchronous API (`io/meshIo.hpp`)

```cpp
std::expected<MeshModel, IoError>   readMesh(path, ReadOptions = {}, IoContext = {});
std::expected<WriteReport, IoError> writeMesh(path, const MeshModel&, WriteOptions = {}, IoContext = {});
FileFormat detectFormat(path);                          // extension, then content sniffing
std::span<const FormatDescriptor> supportedFormats();   // names, extensions, read/write flags
```

- **No exceptions cross the API.** `IoError::Code` is one of `Cancelled`, `FileNotFound`, `UnsupportedFormat`, `ParseError`, `WriteError`, `InvalidModel`, `BackendError`.
- **`IoContext`** carries `isCancelled()` and `onProgress(fraction, stage)`. Readers and writers check for cancellation between stages and chunks.
- **`WriteOptions`:**
  - `format` (Auto = from the extension)
  - `encoding` (Ascii / Binary)
  - `mshVersion` (2.2 / 4.1)
  - `vtkVersion` (4.2 / 5.1)
  - `compress` (VTU zlib)
  - `timeStep` (single-step formats)
  - `writeSidecar`
  - `writeTags`

## 6. Asynchronous service (`io/service/ioService.hpp`)

```text
GUI frame loop                         IoService worker thread
--------------                         -----------------------
task = service.runAsync<T>(desc, job) -> queue -> job(IoContext) -> task->finish(result)
every frame: task->ready()?  (never blocks)
             task->progress(), task->stage()
             task->cancel()        -> context.cancelled() becomes true at the next checkpoint
```

- `importAsync(path, options)` and `exportAsync(path, shared_ptr<const MeshModel>, options)` are thin wrappers over `runAsync`.
- `runAsync<T>` runs any job on the I/O thread. The GUI uses it to run "read + convert to snapshot" and "convert snapshot + write", so no step runs on the frame loop.
- Jobs run one at a time, in submission order.
- The destructor requests stop and drains the queue: pending jobs finish as `Cancelled`, so no `IoTask` is left without a result.
- A Gmsh call in progress (e.g. OCC import of a large STEP) cannot be interrupted inside Gmsh. Cancellation takes effect at the next checkpoint.

## 7. Truss adapter (`anaf_core`: `truss_1D/trussIO/trussMeshAdapter.*`)

| Function | Direction | Details |
|---|---|---|
| `toMeshModel(MeshData, FixedDOFMap)` | export | Nodes (tag = id + 1), Line2 bars with `MaterialID` / `CrossSectionArea`, constraints from the fixity map, loads; `Displacement` and `Stress` when `MeshData::hasResults` |
| `toMeshData(MeshModel, materials)` | import | Line2 → bars; Line3 → two straight segments; surface / volume elements → unique edges (wireframe preview); points ignored. Constraints → fixity map + node movability; loads; results; `isStressExceeded` from the material yield strength |

## 8. GUI integration

See [GUI.md](GUI.md) section 2.3. In short:
- `FileIoPanel` opens the operating system's own file chooser (portable-file-dialogs: Windows common dialog, zenity / kdialog on Linux) without blocking.
- It shows CAD / export options and a progress overlay with Cancel.
- On success it publishes the imported snapshot through the bridge.

## 9. Tests (`tests/`, `-DANAFINEN_BUILD_TESTS=ON`, run with `ctest`)

| Test | What it proves |
|---|---|
| `anaf_io_tests` | Round trips of a model with all 17 element types, non-contiguous tags, sets, multi-step fields, BCs (incl. inclined), loads and awkward doubles: MSH 2.2 / 4.1 ASCII / binary, VTK 4.2 / 5.1 ASCII / binary, VTU ASCII / binary / zlib; cross-format chain; Gmsh-written MSH 1 / 2.2 / 4.0 / 4.1 incl. views; Gmsh reads our files; high-order node order against Gmsh's VTK writer; STEP + sidecar; STEP / IGES / BREP solids; files from anafinen 0.1.2; error codes; async service and cancellation |
| `vtk_reference_check` | Python + official VTK 9.5: 124 files written by VTK in every legacy / XML variant are read exactly as VTK reads them; VTK reads every variant anaf_io writes. Skipped when the Python `vtk` module is missing |
| `anaf_truss_io_tests` | The GUI data path without the GUI: solve → snapshot → adapter → every format → adapter → identical snapshot (bit-exact); STEP with X-bracing; wireframe preview; conversion off the calling thread; material library loading, user material add / remove / save (temporary files only, never the real user config); materials matched by name after the list changes (all writable formats); material files under a non-ASCII folder |

## 10. Known issues and limits

- VTK / VTU store one time step per file. Time series need MSH, or a `.pvd` collection (not written yet).
- STEP export writes line elements only; the rest of the model is in the sidecar.
- Gmsh-based CAD import cannot be interrupted inside Gmsh; cancellation waits for the current Gmsh call.
- Binary MSH 4.1 files cannot be opened by the Gmsh 4.15 build on Fedora (its bug, see 4.1). Our files are valid; use MSH 2.2 or ASCII 4.1 for that Gmsh version.
- Debian 13's Gmsh 4.13 package aborts inside its own second-order 3D meshing (Eigen assertion; see [ARCHITECTURE.md](ARCHITECTURE.md) section 8, item 4). CAD import with element order 2 may hit it.
- `anaf_io` must not instantiate standard templates on types that Gmsh also uses internally (for example `std::map<std::pair<int, int>, std::string>`, Gmsh's physical-name map). Such an instantiation is exported from the executable and replaces libgmsh's copy at run time; with a Gmsh SDK built by another GCC (AUR `gmsh-bin`) the two libstdc++ versions then share one tree and Gmsh loses physical names. `mshFormat.cpp` uses the file-local `IntPair` key for this reason.
- Element types beyond the table are skipped with a warning. Binary files with such types are rejected, because their node count is needed to skip them.

## 11. Related source files

- Public API: [src/io/meshIo.hpp](../src/io/meshIo.hpp), [src/io/core/ioTypes.hpp](../src/io/core/ioTypes.hpp), [src/io/service/ioService.hpp](../src/io/service/ioService.hpp)
- Model: [src/io/model/meshModel.hpp](../src/io/model/meshModel.hpp), [src/io/model/elementType.cpp](../src/io/model/elementType.cpp)
- Formats: [mshFormat.cpp](../src/io/formats/mshFormat.cpp), [vtkLegacyFormat.cpp](../src/io/formats/vtkLegacyFormat.cpp), [vtuFormat.cpp](../src/io/formats/vtuFormat.cpp), [cadFormat.cpp](../src/io/formats/cadFormat.cpp)
- Internals: [modelCodec.cpp](../src/io/detail/modelCodec.cpp), [vtkCommon.cpp](../src/io/detail/vtkCommon.cpp), [textIo.hpp](../src/io/detail/textIo.hpp), [gmshSession.cpp](../src/io/detail/gmshSession.cpp)
- Adapter: [trussMeshAdapter.cpp](../src/objectCalcs/truss_1D/trussIO/trussMeshAdapter.cpp)
- GUI: [fileIoPanel.cpp](../src/gui/panels/fileIoPanel.cpp), [nativeFileDialog.cpp](../src/gui/fileDialogs/nativeFileDialog.cpp)
- Tests: [ioTests.cpp](../tests/ioTests.cpp), [vtkReferenceCheck.py](../tests/vtkReferenceCheck.py), [trussIoTests.cpp](../tests/trussIoTests.cpp), [ioTool.cpp](../tests/ioTool.cpp)
