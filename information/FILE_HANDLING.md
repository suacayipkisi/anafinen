# File Handling: the anaf_io Library

This document describes `anaf_io`, the mesh import/export library:
- its format-neutral data model
- every supported format and version
- the HDF5 array store for matrices, vectors and tensors (section 4.5)
- the asynchronous service
- how the GUI (and a future CLI) use it

For a caller-side guide (public headers, functions, code examples), see [IO_USAGE.md](IO_USAGE.md).

> **Document status**
> Verified against: `v0.2.0-alpha` (in development; last release `v0.1.3-alpha`, 2026-10-01), content checked 2026-10-04 (beam end releases as the `EndReleases` element attribute, section 3.3; built-in beam library files `assets/objects/beam/beam3D/` written through the beam adapter, [CALCULATIONS_BEAM.md](CALCULATIONS_BEAM.md) section 12; beam adapter `FEM::BEAM::ADAPTER`: section shapes, uniform line loads and gravity as well-known names, section 3.3; beam files are routed to it; HDF5 array store `src/io/array/`; 2026-10-03: rotational constraints, nodal moments, beam section attributes, `ElementFormulation`, `beamOrientation`, beam / dynamic result names, link to the interoperability plan; earlier: truss adapter supports, step kinds, global data, `.pvd`, thermal BCs, amplitudes, initial conditions, damping).
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
                 |                                                                             |
  .h5 (HDF5) <-->|  array/arrayFile.hpp: ArrayFile (dense / sparse arrays, groups, attributes) |
                 |     + header-only adapters: stdArrays, eigenArrays, cholmodArrays           |
                 +-----------------------------------------------------------------------------+
                            ^                                   ^
                            |                                   |
      anaf_core: truss_1D/trussIO/trussMeshAdapter      GUI: panels/fileIoPanel + native dialog
      MeshModel <-> BRIDGE::MeshData                    (future CLI: readMesh / writeMesh directly)
```

## 2. Target and dependency rules

| Rule | Why |
|---|---|
| `anaf_io` depends only on Gmsh (CAD formats), zlib (VTU compression) and HDF5 (array store), all PRIVATE | GUI, CLI and tests link the same library without pulling in solver or GUI code |
| The Eigen and CHOLMOD adapters (`io/array/eigenArrays.hpp`, `cholmodArrays.hpp`) are header-only | `anaf_io` compiles and links without Eigen or CHOLMOD; only the code that includes an adapter needs that library |
| Public headers: `io/meshIo.hpp`, `io/service/ioService.hpp`, `io/model/*.hpp`, `io/core/ioTypes.hpp`, `io/array/*.hpp` | `io/detail/*` (including `h5Handle.hpp`, the only place besides `arrayFile.cpp` that includes `hdf5.h`) and `io/formats/*` are internal |
| Solver-specific conversions live outside `anaf_io` (`trussMeshAdapter`, `beamMeshAdapter` in `anaf_core`) | New object types (beams, shells, solids) add their own adapter; formats do not change |
| No GUI macros (`ANAF_GUI`) in shared headers | Libraries are compiled once and linked into several executables; a macro that changes a type would break the ODR |

## 3. The data model (`io/model/meshModel.hpp`)

`MeshModel` follows the concepts used by general FEM codes (Gmsh, Exodus, MED, Abaqus input): nodes, typed element blocks, named sets, and fields per time step.

| Member | Type | Meaning |
|---|---|---|
| `nodes` | `vector<Node{tag, position}>` | `tag` is the id from the file; everything else uses the 0-based index |
| `blocks` | `vector<ElementBlock>` | One block per element type: `tags`, `connectivity` (node indices, **Gmsh local order**), `entityTags` (geometric entity per element) |
| `sets` | `vector<EntitySet>` | Named node or element sets: `name`, `kind`, `dimension`, `tag` (physical tag), `members` |
| `fields` | `vector<Field>` | `name`, `location` (node / element), `components`, `times[s]`, `steps[s][entity * components + c]`, `stepKind`, optional `stepLabels[s]` |
| `constraints` | `vector<NodeConstraint>` | `fixed[3]` per global axis, optional `allowedMotion` basis (inclined supports), `prescribed[3]` imposed displacement of the fixed DOFs [m], `amplitude`; rotations: `fixedRotation[3]` about global X, Y, Z, `prescribedRotation[3]` [rad, small-rotation vector] of the fixed rotational DOFs, `amplitudeRotation` (independent of `amplitude`; empty = constant 1) |
| `loads` | `vector<NodalLoad>` | Nodal `force` [N] and `moment` [N·m] in global axes, one `amplitude` for both; a moment with another time history is another entry |
| `temperatureConstraints` | `vector<TemperatureConstraint>` | Prescribed nodal temperature [K], `amplitude` |
| `heatLoads` | `vector<HeatLoad>` | Concentrated heat flow into a node [W] (> 0 heats), `amplitude` |
| `amplitudes` | `vector<Amplitude{name, times, factors}>` | Piecewise linear time factor, held constant outside its range (`factorAt(t)`) |
| `initialConditions` | `vector<InitialCondition{quantity, components, values}>` | Nodal initial state: `InitialQuantity::Displacement` (3), `Velocity` (3), `Rotation` (3), `AngularVelocity` (3), `Temperature` (1), or any name |
| `damping` | `optional<Damping>` | Rayleigh `alpha` [1/s], `beta` [s] (C = αM + βK) and `modalRatios` (one per mode) |
| `elementAttributes` | `map<string, vector<double>>` | Per-element scalars: `MaterialID`, `CrossSectionArea` [m²], beam section data `SecondMomentY` / `SecondMomentZ` / `TorsionConstant` [m⁴], `ShearAreaY` / `ShearAreaZ` [m², k·A], `ElementFormulation` (0 / missing = bar, 1 = Euler-Bernoulli, 2 = Timoshenko), beam section shape `SectionShape` + `SectionDimension1..5` [m], uniform line loads `UniformLoadGlobalX/Y/Z` and `UniformLoadLocalX/Y/Z` [N/m], beam end releases `EndReleases` (bit set 0..4095), and any other attribute (thickness, …) |
| `beamOrientation` | `vector<array<double, 3>>` | Per-element reference vector v [global axes], empty = none. Local x = node 0 → node 1, v in the local x–y plane, z = x × v, y = z × x; a zero vector leaves the choice to the solver (`FEM::BEAM`: +Y, or +X for members parallel to Y, [CALCULATIONS_BEAM.md](CALCULATIONS_BEAM.md) section 3) |
| `globalData` | `vector<GlobalArray{name, components, values}>` | Model-level arrays that belong to no node or element (natural frequencies, modal masses, …) |
| `lengthUnit`, `title`, `warnings` | | Metadata; readers append non-fatal issues to `warnings` |

- Global element index: block 0 elements first, then block 1, and so on. Sets, element fields and attributes are indexed by it.
- `validate()` returns a message for every inconsistency (sizes, indices, unknown amplitude names, unsorted amplitude times, amplitude names with quotes or line breaks, a prescribed rotation on a free rotational DOF, an unknown or non-line `ElementFormulation`, a beam orientation on a non-line element or parallel to the element axis). Every writer refuses an invalid model; every reader validates its result.
- **Amplitude references:** a BC or load names its amplitude; an empty name is a constant factor 1. The stored value is the reference magnitude that the amplitude scales.
- Well-known names: `FieldName::Displacement` (node, 3), `Rotation` (node, 3, rad), `Velocity`, `Acceleration`, `AngularVelocity`, `AngularAcceleration` (node, 3), `Stress` (element, Pa, tension > 0), `AxialForce` (element, N, tension > 0), `BeamSectionForce` (element, 12: N, Vy, Vz, T, My, Mz at node 0 then node 1, local axes, section sign convention, so N > 0 is tension at both ends), the `Attribute::*` names above, `GlobalName::NaturalFrequency` (Hz, one tuple per mode). Mode shapes of beams are a `Displacement` and a `Rotation` field, both with `StepKind::Mode`.
- Units follow `lengthUnit` (N·mm and mm⁴ when it is `mm`); `anaf_io` never converts them.

**Step kinds** (`Field::stepKind`) say what `times[s]` holds:

| `StepKind` | Steps are | `times[s]` |
|---|---|---|
| `Time` (default) | a transient or pseudo-time history | time [s] |
| `Frequency` | a frequency response | excitation frequency [Hz] |
| `Mode` | eigenmodes; mode number = s + 1 | natural frequency [Hz] |
| `LoadCase` | independent static load cases | load case number; the name goes to `stepLabels[s]` |

**Where each format keeps steps, kinds, labels and global data:**

| Format | Time history | Modes / frequencies / load cases | Kind + labels | Global data |
|---|---|---|---|---|
| MSH | every step in `$NodeData` / `$ElementData` | same | `$AnafData` `STEPS` record | `$AnafData` `GLOBAL` record |
| VTK / VTU | one step (`timeStep`) + `TimeValue` | one array per step, `<name>_<Kind>_NNN`, and global `<name>_<Kind>_Values` | kind in the array names; labels dropped (warning) | dataset `FIELD` (legacy) / `<FieldData>` (VTU) |
| `.pvd` | one `.vtu` per time value | inside every `.vtu`, as for VTU | as for VTU | inside every `.vtu` |
| STEP sidecar | every step | same | `KIND` / `LABEL` lines (version 3) | `GLOBAL` records (version 3) |

- VTK readers group `<name>_<Kind>_NNN` arrays (at least 3 digits) back into one field and consume the matching `_Values` global. A `TimeValue` global becomes the time of every Time field and is removed from `globalData`.
- Shared helpers: `detail::flattenSteps()` / `detail::unflattenSteps()` in `modelCodec.*`.

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
| `FixityRotation` (node, 3) | rotational fixity about X, Y, Z; only written when a constraint fixes a rotation |
| `AllowedMotionBasis` (node, 10) | rank + 3×3 free-direction basis; only written when a node has an inclined support |
| `NodalForce` (node, 3) | loads [N]; `NodalForce:<amplitude>` for loads with an amplitude (loads on one node with the same amplitude are summed) |
| `NodalMoment[:<amplitude>]` (node, 3) | moments [N·m]; on read, force and moment of one (node, amplitude) merge into one `NodalLoad` |
| `PrescribedDisplacement[:<amplitude>]` (node, 4) | member flag, ux, uy, uz [m]; only written when a constraint has a non-zero value or an amplitude |
| `PrescribedRotation[:<amplitude>]` (node, 4) | member flag, rx, ry, rz [rad]; keyed by `amplitudeRotation`, only written when a rotation is non-zero or `amplitudeRotation` is set |
| `PrescribedTemperature[:<amplitude>]` (node, 2) | member flag, temperature [K] |
| `NodalHeat[:<amplitude>]` (node, 1) | heat loads [W] |
| `Initial:<quantity>` (node, any) | initial conditions |
| `HeatGeneration` (element, 1) | volumetric heat source [W/m³], an element attribute |
| `MaterialID`, `CrossSectionArea`, `Attribute:<name>` (element, 1) | element attributes; the beam section names and `ElementFormulation` use the `Attribute:` prefix, so anafinen 0.1.3 already reads them into `elementAttributes` |
| `BeamOrientation` (element, 3) | `beamOrientation`; only written when it is set. Gmsh and ParaView show it as a vector (Glyph works) |
| `NodeSet:<name>` / `ElementSet:<name>` (1) | set membership (1 = member) |
| `NodeTag`, `ElementTag`, `EntityTag` (1) | original ids (VTK / VTU only; MSH stores them natively) |

Global arrays (`encodeModelGlobals()`), stored like `globalData` in every format:

| Global array (components) | Holds |
|---|---|
| `Amplitude:<name>` (2) | one (time, factor) tuple per point |
| `RayleighDamping` (2) | one tuple: alpha, beta |
| `ModalDampingRatio` (1) | one ratio per mode; only written when not empty |

**Materials by name (truss adapter, 0.1.3):** `toMeshModel()` writes one element set `Material:<material name>` per material used, in addition to `MaterialID`. Sets survive every format (MSH physical groups, `ElementSet:Material:<name>` arrays in VTK / VTU / sidecar), so `toMeshData()` matches bars to the current material list by name (ASCII case-insensitive). An unknown name, or a `MaterialID` outside the list, falls back to material 0 with a warning note. Files without material sets (anafinen ≤ 0.1.2) use `MaterialID`, which matches the built-in order (0 steel, 1 aluminum). MSH 4.1 stores elements per entity, so a multi-material model reads back grouped by material; node pairs, materials and results stay matched (tested for MSH 4.1 / 2.2, VTK, VTU, STEP + sidecar).

**Compatibility of the rotational and beam data:** all of it uses new array names, and nothing new is written for a model without it (a truss writes exactly `Fixity`, `NodalForce`, … as before; `anaf_truss_io_tests` compares the committed library files). An older reader keeps the unknown arrays as plain fields and still reads supports and forces. The reasons behind each choice are in section 3.3.

On read, the legacy names written by anafinen ≤ 0.1.2 are accepted too: `FixityX/Y/Z`, `AllowedMotionRank` + 9-component `AllowedMotionBasis`, and `FixityDirection_*`.

### 3.3 Beam and rotational data: design decisions (phase 2.10)

The beam / frame data was added without breaking files, readers or callers of anafinen 0.1.3. Every choice below has a reason that is easy to undo by accident.

| Decision | Chosen | Rejected | Reason |
|---|---|---|---|
| New struct members | Appended at the end of `NodeConstraint` / `NodalLoad`; no constructors | Inserted next to the translational members; user-declared constructors | Every caller uses positional aggregate init (`NodeConstraint{n, fixed, {}, {}, {}}`). Inserting members shifts the initializers, and `NodalLoad{n, f, {}}` would even compile silently with the wrong meaning. Constructors would break designated initializers. |
| Rotational fixity | Two groups: `fixed[3]` + `fixedRotation[3]` | One `array<bool, 6>`; a widened 6-component `Fixity` array | Different units (m vs rad), and `allowedMotion` is a translational basis. Most important: a 6-component `Fixity` fails the `components == 3` check of older readers, which would then drop **all** supports. A separate `FixityRotation` array is just ignored by them. |
| Skewed rotational supports | Not representable (`fixedRotation` in global axes only) | `allowedRotation` free-direction basis | Two independent bases cannot express "the support frame is rotated" consistently, and the decoder turned translation-only nodes into identity rotation bases (the same trap as `AllowedMotionBasis`). The right model is a node-local frame for all 6 DOFs; it can come later. |
| Time history of imposed rotations | `amplitudeRotation`, independent; empty = constant 1 | Sharing `amplitude`; "empty = inherit `amplitude`" | Settlement and imposed rotation may follow different histories. "Inherit" made the round trip non-idempotent: `{"Ramp", ""}` read back as `{"Ramp", "Ramp"}`. |
| Time history of moments | The load's one `amplitude`; another history is another `NodalLoad` entry | `amplitudeMoment` | `loads` is a vector of additive entries, so a second amplitude field is a second way to say the same thing, and the decoder could not reproduce the original representation. On read, force and moment of one (node, amplitude) merge into one entry. |
| Bar or beam | Explicit `ElementFormulation` attribute; missing = bar | Inferring "beam" from the presence of `SecondMomentZ` | Bars and beams are both `Line2`. Inference would turn a truss exported with section data into a frame silently. "Missing = bar" keeps every existing file's meaning. |
| Section attribute names | `SecondMomentY/Z`, `TorsionConstant`, `ShearAreaY/Z`, string = constant name, written as `Attribute:<name>` | `"Iyy"` / `"Izz"`, `MomentOfInertia*`, unprefixed names | anafinen 0.1.3 decodes `Attribute:<name>` into `elementAttributes` already: full compatibility at zero cost. Unprefixed new names would stay plain fields there. "Moment of inertia" reads as mass inertia. Principal axes are assumed (no `Iyz`); `TorsionConstant` is St. Venant J, not the polar moment. |
| Beam orientation | Typed `MeshModel::beamOrientation`, written as one 3-component element array `BeamOrientation` | Three scalar attributes `OrientationX/Y/Z`; a result `Field`; a third reference node | One 3-component array is a vector in Gmsh and ParaView (Glyph works), and cannot exist half-written. `fields` is for results. Reference nodes are unconnected mesh nodes that renumbering and "remove unused nodes" break. |
| Orientation convention | v in the local x–y plane: x = node 0 → node 1, z = normalize(x × v), y = z × x; zero v = solver default | Roll angle | A roll angle depends on the solver's default reference rule, which is ambiguous for vertical members. `validate()` rejects v parallel to the axis. |
| Beam internal forces | One 12-component `BeamSectionForce` (N, Vy, Vz, T, My, Mz at node 0, then node 1), section sign convention; `AxialForce` kept | Separate `BendingMoment` / `ShearForce` / `TorsionalMoment`, one value per element | One value per element cannot hold a linear moment diagram, the main beam result. Section convention (node 0 = −k·u, node 1 = +k·u) makes N > 0 tension at both ends, consistent with the truss `AxialForce`. |
| Rotation results | Separate `Rotation` field (node, 3); mode shapes = `Displacement` + `Rotation` with `StepKind::Mode` | A 6-component displacement | ParaView "Warp By Vector" needs a 3-component displacement. |
| Section shapes (2026-10-04) | `Section:<name>` element sets (matched by name, like materials) + per-element attributes `SectionShape` (code) and `SectionDimension1..5`; the numbers (`CrossSectionArea`, `SecondMomentY/Z`, ...) are written as well | A section table as a new `MeshModel` member | Attributes and sets already survive every format (MSH, VTK, VTU, STEP sidecar) and older readers keep them as plain fields: no codec change. A table would need its own encoding in four formats. The numbers keep the file useful for programs that know no shapes. |
| Uniform beam loads (2026-10-04) | Per-element attributes `UniformLoadGlobalX/Y/Z` and `UniformLoadLocalX/Y/Z`, N/m: the sums of the element's loads in each frame | An element-load list in `MeshModel` (the element-face load gap) | Same reason as above. The sum is the same static load; only the split into several entries is lost (an import gives at most one global and one local load per element). Surface / volume face loads remain the open gap. |
| Self weight (2026-10-04) | Global array `Gravity` (3 components, m/s²; zero = none) | An attribute per element | It belongs to the model, like natural frequencies; `globalData` survives every format. |
| Beam end releases (2026-10-04) | One element attribute `EndReleases`: bit k = local DOF k {ux, uy, uz, rx, ry, rz} of node 0 (k = 0..5) or node 1 (k = 6..11), stored as a double; written only when an element has a release | Two attributes `ReleaseNode0` / `ReleaseNode1`; six booleans per end; a hinge flag | One number cannot be half-written, and 12 bits are exact in a double. The DOF order is that of `BeamSectionForce` and of Nastran's PA / PB (digits 1..6), so a later `.bdf` writer maps it directly. Attributes survive every format without a codec change; files without releases keep their exact old content. |
| Beam stresses (2026-10-04) | Element field `VonMisesStress` (largest along the element, an upper bound); import recomputes the stresses from `BeamSectionForce` | Reading the field back | The field is for viewers; the solver's own values come from the section forces and loads, so nothing can disagree. |

Not covered yet, left for later work:
- partial end fixity (rotational springs at element ends);
- element-face loads on surfaces and volumes (pressure, heat flux, convection); beam line loads are stored since 2026-10-04 (table above);
- concentrated nodal mass and rotary inertia;
- node-local frames (skewed rotational supports);
- springs / dashpots;
- a `FieldLocation::ElementNode` (Gmsh `$ElementNodeData`) for per-element-node results;
- modal global names (`ModalMass`, `ParticipationFactor`, `EffectiveModalMass`), with the modal analysis step.

## 4. Formats

### 4.1 Gmsh MSH: native (`formats/mshFormat.cpp`)

| Version | Read | Write |
|---|---|---|
| 1.0 (`$NOD` / `$ELM`) | yes | – |
| 2.0 / 2.1 / 2.2 | ASCII + binary | 2.2 ASCII + binary |
| 4.0 | ASCII (Gmsh cannot write binary 4.0 either) | – |
| 4.1 | ASCII + binary | ASCII + binary |

- **Sections read:** `$MeshFormat`, `$PhysicalNames`, `$Entities`, `$Nodes`, `$Elements`, `$NodeData` / `$ElementData` (every time step) and our own `$AnafData`. All other sections (`$InterpolationScheme`, `$Periodic`, …) are skipped.
- **`$AnafData`** (always ASCII, also in binary files; Gmsh skips it, tested):
  ```text
  $AnafData
  1                                  format version
  <record count>
  GLOBAL <components> <tuples>       + quoted name line + values (one tuple per line)
  STEPS <N|E> <kind> <steps> <0|1>   + quoted field name line + one quoted label line per step when the flag is 1
  $EndAnafData
  ```
  Global names and labels escape `\`, `"` and line breaks with a backslash. Field names follow the `$NodeData` rule (`"` becomes `'`) so both sections name a field alike. Gmsh's own string tags are not used for this: Gmsh reads the second string tag as an interpolation scheme name.
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
- **Attributes read:** `SCALARS`, `COLOR_SCALARS`, `VECTORS`, `NORMALS`, `TEXTURE_COORDINATES`, `TENSORS`, `TENSORS6`, `GLOBAL_IDS`, `PEDIGREE_IDS`, `FIELD` arrays. `METADATA` blocks are skipped. Dataset-level `FIELD` data (before `POINTS`) becomes `globalData`, and is written there too.
- **Composite cells are split:** poly-vertex → points, poly-line → Line2 segments, triangle strip → triangles, polygon → Tri3 / Quad4 / fan triangles, pixel → Quad4, voxel → Hex8. Cell data is copied to every produced element.
- **Array names** with whitespace are escaped as `%XX`, like VTK does.
- **Precision:** an ASCII `float` array is read with float precision, exactly as VTK reads it.
- **Time steps:** one time step per file. The step is chosen with `WriteOptions::timeStep` (default: last), and a warning is added when other steps are dropped. `TimeValue` is written when a field has a history or a non-zero time. Modes, frequencies and load cases are all written (see section 3).

### 4.3 VTK XML `.vtu`: native (`formats/vtuFormat.cpp`)

- **Read:**
  - file versions 0.1 / 1.0 / 2.x
  - `header_type` UInt32 or UInt64; little or big endian
  - `DataArray` format `ascii`, `binary` (base64), or `appended` (raw or base64)
  - `vtkZLibDataCompressor`
  - any number of `<Piece>` elements (merged)
  - VTK 9 `<InformationKey>` children inside `DataArray`s are skipped
- **Write:** version 1.0, UInt64 headers, little endian, `ascii` or inline `binary`, optional zlib compression.
- **Global data:** `<UnstructuredGrid><FieldData>` (with `NumberOfTuples`) ↔ `globalData`, including `TimeValue`. `String` arrays are skipped.
- **Steps:** as legacy VTK (section 4.2).

### 4.3.1 ParaView collection `.pvd` (`formats/vtuFormat.cpp`)

- **Write:** `name.pvd` plus the folder `name/` with `name_0000.vtu`, `name_0001.vtu`, … (`WriteReport::extraFiles` lists them).
  1. The time values are the sorted union of the times of every Time field with more than one step.
  2. File k holds each Time field at the step whose time equals time k (fields without that time are left out), every single-step field, and every mode / load case field.
  3. Each `.vtu` carries its own `TimeValue`, so it also reads correctly on its own.
  4. `file` paths are relative, UTF-8, with forward slashes (portable between Windows and Linux).
- **Read:** every `DataSet` with `part="0"`, sorted by `timestep`; each `.vtu` is read with the VTU reader. The first file gives the mesh, sets, BCs and non-Time fields; later files must have the same node and element count. A Time field that is identical in every file folds back into one step (its time becomes the first time value).
- Progress and cancellation cover all files (`IoContext`), so `IoService` shows "step k of n".
- Not supported: multi-part collections (`part > 0` is skipped with a warning).- **Not supported:** LZ4 / LZMA compressors; the reader reports a clear error. Only `UnstructuredGrid` files are read.

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

**Sidecar `<file>.anafFields`, version 3** (everything STEP cannot carry):
```text
ANAFINEN_SIDECAR 3
UNIT m
NODES <n>
x y z                                  (all model nodes, shortest round-trip doubles)
ELEMENTS <m>
2 <node index> <node index>            (exported line elements, indices into NODES)
FIELD <N|E> <components> <steps> <name>
KIND <Time|Frequency|Mode|LoadCase> <0|1>
LABEL <text>                           (one per step, only when the KIND flag is 1)
TIME <t>
<values, one row per node / element>   (fields + codec arrays: BCs, loads, attributes, sets)
GLOBAL <components> <tuples> <name>
<values, one tuple per line>
END
```
- **Matching on read:**
  - Nodes are matched by position: grid hash, tolerance 1e-6 × model size, O(1) per node.
  - Elements are matched by their matched end nodes. Centroids are not used, because crossing X-braces share their midpoint.
- **Version 2** (anafinen 0.1.3 before step kinds: no `KIND`, `LABEL`, `GLOBAL`) is still read.
- **Version 1** (anafinen ≤ 0.1.2: `NODES … x y z dx dy dz`, `ELEMENTS … mx my mz material area stress`) is still read, by position.

**Gmsh session (`detail/gmshSession.*`):**
- One process-wide mutex serializes every Gmsh call.
- Each session clears the model and restores all option defaults, so settings such as mesh size limits cannot leak into the next import.
- Gmsh is initialized without reading the user's `gmshrc`.

### 4.5 HDF5 array store (`array/arrayFile.cpp`)

Binary storage for solver data that is not a mesh: stiffness / mass matrices, load and result vectors, eigenvector sets, tensors. It is a separate API (`anaf::IO::ARRAY::ArrayFile`), not a `FileFormat` of `readMesh` / `writeMesh`. The layout follows the conventions of h5py, SciPy and anndata, so Python, MATLAB (`h5read`) and HDFView read the files without anaf code.

```text
  caller                         anaf_io (anaf::IO::ARRAY)                      file (.h5)
  ------                         -------------------------                      ----------
  std::vector / array / mdspan -> stdArrays.hpp    write / readVector / readRows / asMdspan
  Eigen dense / SparseMatrix ---> eigenArrays.hpp  write / readEigen<T>
  cholmod_sparse / dense -------> cholmodArrays.hpp write / readCholmodSparse / readCholmodDense
                                        |
                                        v
                                 arrayFile.hpp: ArrayFile  writeDense / readDense,
                                 (span + shape, CSC/CSR)   writeSparse / readSparse,
                                        |                  info / list / exists / remove,
                                        v                  setAttribute / attribute
                                 arrayFile.cpp + detail/h5Handle.hpp  -- HDF5 C API --> /group/dataset
                                 (one process-wide mutex around every HDF5 call)
```

Where data is stored:

| Data | HDF5 object | Notes |
|---|---|---|
| Dense array of rank N | dataset with N dimensions | Row-major (C order). Rank 0 = scalar dataspace. Eigen ColMajor matrices are transposed while copying, so `(rows, cols)` on disk is the mathematical shape. |
| Sparse matrix | group with datasets `data`, `indices` (int64), `indptr` (int64) | Attributes `encoding-type` = `csc_matrix` / `csr_matrix`, `encoding-version` = `0.1.0`, `shape` = int64[2] (anndata convention; `scipy.sparse.csc_matrix((data, indices, indptr), shape)` rebuilds it). |
| Symmetric CHOLMOD matrix | same group + attribute `cholmod-stype` | Only one triangle is stored, as in CHOLMOD. Other readers see a triangular matrix. |
| Complex values | compound type `{r, i}` of float or double | The h5py convention. |
| Attributes | HDF5 attributes | int64, double, variable-length UTF-8 string, int64[] or double[]. Fixed-length strings written by other tools are read too. |
| Groups | HDF5 groups | Missing parents are created on write (`results/step_001/u`). Link names are UTF-8. |

Element types: `float`, `double`, `int32`, `int64`, `uint64`, `complex<float>`, `complex<double>`. A read must ask for the stored type or a lossless widening (float → double, int32 → int64, complex64 → complex128); anything else is `TypeMismatch`.

Write sequence (`writeDense`):

1. Lock the HDF5 mutex; turn off HDF5's automatic error printing (it is per thread in thread-safe builds).
2. Check the file is open and writable, normalize the path (`a//b`, `.` and `..` are rejected), check `values.size()` against the shape.
3. When the path exists: delete the link (`overwrite = true`, the default) or fail with `AlreadyExists`.
4. Create the dataspace and the dataset with intermediate groups; with `deflateLevel` 1–9 the dataset is chunked (at most 1 MiB per chunk) and zlib-compressed.
5. Write the whole buffer in one `H5Dwrite`. Errors carry the HDF5 error stack in `ArrayError::message`.

Read sequence (`readSparse`): `info()` reads the attributes and the `data` size, then the three datasets are read (`indices` / `indptr` converted to int64 by HDF5) and validated: pointer count, monotonic pointers, last pointer = non-zeros, every index inside the matrix. A broken file gives `InvalidData`, never undefined behaviour in Eigen or CHOLMOD. Eigen needs sorted inner indices; unsorted ones (allowed by CSC, produced by some CHOLMOD routines) are rebuilt through triplets, which also sums duplicates.

Thread safety: distribution HDF5 builds are not thread-safe (`h5cc -showconfig`: `Threadsafety: no`). Every `ArrayFile` call holds one process-wide mutex, so different files can be used from different threads (tested with four threads). A single `ArrayFile` object must not be shared between threads without external locking.

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
| `toMeshModel(MeshData, materials)` | export | Nodes (tag = id + 1), Line2 bars with `MaterialID` / `CrossSectionArea`, a constraint for every supported node (`Node::isSupported()`, inclined basis included), loads; `Displacement` and `Stress` when `MeshData::hasResults` |
| `toMeshData(MeshModel, materials)` | import | Line2 → bars; Line3 → two straight segments; surface / volume elements → unique edges (wireframe preview); points ignored. Constraints → node supports (axis fixity or inclined basis); loads; results; `isStressExceeded` from the material yield strength |

## 8. GUI integration

See [GUI.md](GUI.md) section 2.3. In short:
- `FileIoPanel` opens the operating system's own file chooser (portable-file-dialogs: Windows common dialog, zenity / kdialog on Linux) without blocking.
- It shows CAD / export options and a progress overlay with Cancel.
- On success it publishes the imported snapshot through the bridge.

## 9. Tests (`tests/`, `-DANAFINEN_BUILD_TESTS=ON`, run with `ctest`)

| Test | What it proves |
|---|---|
| `anaf_io_tests` | Round trips of a model with all 17 element types, non-contiguous tags, sets, multi-step fields, BCs (incl. inclined), loads and awkward doubles: MSH 2.2 / 4.1 ASCII / binary, VTK 4.2 / 5.1 ASCII / binary, VTU ASCII / binary / zlib; cross-format chain; Gmsh-written MSH 1 / 2.2 / 4.0 / 4.1 incl. views; Gmsh reads our files; high-order node order against Gmsh's VTK writer; STEP + sidecar (v3: step kinds, labels, globals, thermal BCs, amplitudes, damping); prescribed displacements, thermal BCs, amplitudes, initial conditions and damping in every format, checked in the file text too; amplitude interpolation; `validate()` of broken references; step kinds, labels and global data in every format; `TimeValue`; `.pvd` series with a late-starting field, progress and cancellation; STEP / IGES / BREP solids; files from anafinen 0.1.2; error codes; async service and cancellation; rotational constraints, nodal moments (force + moment merge on read), section attributes, `ElementFormulation`, `beamOrientation`, `Rotation` mode shapes and 12-component `BeamSectionForce` in every format (STEP sidecar per element, edge direction kept); a truss model writes exactly the arrays it wrote before; `validate()` of broken beam data |
| `anaf_array_tests` | HDF5 array store: dense round trips of every element type and rank 0–4 (incl. zero-size), widening reads and refused narrowing, groups / list / exists / remove / overwrite, every attribute kind, error codes (missing file, non-HDF5 file, read-only, moved-from object, kind / shape mismatch), deflate shrinks a repetitive array more than tenfold, sparse validation on write and on read (hand-made broken group), std containers and `mdspan` (`layout_right` in place, `layout_left` gathered), Eigen dense (ColMajor on disk is row-major, blocks, strided rows, fixed sizes, `Array`, complex) and sparse (CSC / CSR in either Eigen order, uncompressed, unsorted indices, complex), CHOLMOD sparse (stype, CSR → CSC) and dense (leading dimension) when available, UTF-8 file names, four threads writing their own files |
| `vtk_reference_check` | Python + official VTK 9.5: 124 files written by VTK in every legacy / XML variant are read exactly as VTK reads them; VTK reads every variant anaf_io writes. The grids include field data, `TimeValue` and `_Mode_NNN` arrays. Skipped when the Python `vtk` module is missing |
| `anaf_core_tests` | The FEM core alone, against closed-form results ([CALCULATIONS.md](CALCULATIONS.md) section 11) |
| `anaf_beam_tests` | Beam solver, sections, stresses, and the beam adapter: a solved frame with every section shape, inclined supports, line loads, gravity and results bit-exact through MSH / VTU / VTK and through STEP + sidecar; unknown sections imported as new ones; inclined rotation supports reported ([CALCULATIONS_BEAM.md](CALCULATIONS_BEAM.md) section 9) |
| `anaf_truss_io_tests` | The GUI data path without the GUI: solve → snapshot → adapter → every format → adapter → identical snapshot (bit-exact); inclined supports through MSH and the solver; STEP with X-bracing; wireframe preview; conversion off the calling thread; material library loading, user material add / remove / save (temporary files only, never the real user config); materials matched by name after the list changes (all writable formats); material files under a non-ASCII folder |

## 10. Known issues and limits

The plan for the format and data model gaps below (Abaqus `.inp`, CalculiX `.frd`, Nastran `.bdf`, UNV / MED through Gmsh, `.pvtu` / `.vtm` / `.vtp`, material values, analysis steps, units, reactions) is [INTEROP_PLAN.md](INTEROP_PLAN.md).

- Material values are not written: files carry the material name and `MaterialID` only, so another program cannot solve an exported model without its own material data (INTEROP_PLAN.md G1).
- No analysis step concept and no unit system beyond `lengthUnit`; MSH / VTK files are read as metres (INTEROP_PLAN.md G2, G3).
- No reaction forces in the results (INTEROP_PLAN.md G4).
- XML VTK: only `UnstructuredGrid` (`.vtu`, `.pvd`); `.pvtu`, `.vtm`, `.vtp`, Lagrange cells (68–72) and polyhedra (42) are not read.
- VTK / VTU store one time step per file. Time series need MSH or a `.pvd` collection.
- Step labels (load case names) are not stored in VTK / VTU / `.pvd`.
- No complex values (harmonic response with phase) yet.
- BCs and loads are nodal only: no element-face loads (pressure, surface heat flux, convection), no nodal springs or dashpots.
- A file with beam elements (`ElementFormulation` 1 / 2) goes to the beam adapter (`FEM::BEAM::ADAPTER`, [CALCULATIONS_BEAM.md](CALCULATIONS_BEAM.md) section 11). The truss adapter still reads only `fixed` and `force` of the files it gets (no beam formulation): rotational fixity and moments in such a file are dropped without a warning ([ARCHITECTURE.md](ARCHITECTURE.md) section 8, item 5).
- An inclined rotation support of the beam solver cannot be written (rotational fixity per global axis only); the beam adapter writes the global axes outside its span as fixed and warns.
- Not covered yet (section 3.3): beam end releases, surface / volume face loads, concentrated nodal mass / rotary inertia, node-local frames for skewed rotational supports, `ElementNode` field location.
- STEP export writes line elements only; the rest of the model is in the sidecar.
- Gmsh-based CAD import cannot be interrupted inside Gmsh; cancellation waits for the current Gmsh call.
- Binary MSH 4.1 files cannot be opened by the Gmsh 4.15 build on Fedora (its bug, see 4.1). Our files are valid; use MSH 2.2 or ASCII 4.1 for that Gmsh version.
- Debian 13's Gmsh 4.13 package aborts inside its own second-order 3D meshing (Eigen assertion; see [ARCHITECTURE.md](ARCHITECTURE.md) section 8, item 4). CAD import with element order 2 may hit it.
- `anaf_io` must not instantiate standard templates on types that Gmsh also uses internally (for example `std::map<std::pair<int, int>, std::string>`, Gmsh's physical-name map). Such an instantiation is exported from the executable and replaces libgmsh's copy at run time; with a Gmsh SDK built by another GCC (AUR `gmsh-bin`) the two libstdc++ versions then share one tree and Gmsh loses physical names. `mshFormat.cpp` uses the file-local `IntPair` key for this reason.
- HDF5 store: deleting or overwriting an object does not shrink the file (HDF5 does not reclaim the space; `h5repack` does). Only whole datasets are read and written: no partial (hyperslab) access or appending yet. `std::mdspan` support needs a standard library that has it (GCC 15+, MSVC 17.9+); with GCC 14 (Debian 13) the `mdspan` overloads are left out and their test is skipped.
- Element types beyond the table are skipped with a warning. Binary files with such types are rejected, because their node count is needed to skip them.

## 11. Related source files

- Public API: [src/io/meshIo.hpp](../src/io/meshIo.hpp), [src/io/core/ioTypes.hpp](../src/io/core/ioTypes.hpp), [src/io/service/ioService.hpp](../src/io/service/ioService.hpp)
- Model: [src/io/model/meshModel.hpp](../src/io/model/meshModel.hpp), [src/io/model/elementType.cpp](../src/io/model/elementType.cpp)
- Formats: [mshFormat.cpp](../src/io/formats/mshFormat.cpp), [vtkLegacyFormat.cpp](../src/io/formats/vtkLegacyFormat.cpp), [vtuFormat.cpp](../src/io/formats/vtuFormat.cpp), [cadFormat.cpp](../src/io/formats/cadFormat.cpp)
- HDF5 array store: [arrayTypes.hpp](../src/io/array/arrayTypes.hpp), [arrayFile.hpp](../src/io/array/arrayFile.hpp), [arrayFile.cpp](../src/io/array/arrayFile.cpp), [stdArrays.hpp](../src/io/array/stdArrays.hpp), [eigenArrays.hpp](../src/io/array/eigenArrays.hpp), [cholmodArrays.hpp](../src/io/array/cholmodArrays.hpp), [h5Handle.hpp](../src/io/detail/h5Handle.hpp)
- Internals: [modelCodec.cpp](../src/io/detail/modelCodec.cpp), [vtkCommon.cpp](../src/io/detail/vtkCommon.cpp), [textIo.hpp](../src/io/detail/textIo.hpp), [gmshSession.cpp](../src/io/detail/gmshSession.cpp)
- Adapter: [trussMeshAdapter.cpp](../src/objectCalcs/truss_1D/trussIO/trussMeshAdapter.cpp)
- GUI: [fileIoPanel.cpp](../src/gui/panels/fileIoPanel.cpp), [nativeFileDialog.cpp](../src/gui/fileDialogs/nativeFileDialog.cpp)
- Tests: [arrayTests.cpp](../tests/arrayTests.cpp), [ioTests.cpp](../tests/ioTests.cpp), [vtkReferenceCheck.py](../tests/vtkReferenceCheck.py), [trussIoTests.cpp](../tests/trussIoTests.cpp), [ioTool.cpp](../tests/ioTool.cpp)
