# Interoperability Plan: Models and Results Across Programs

This document is the plan for making ANAFINEN exchange complete models and results with other FEM programs. It lists the target programs, the gaps in the current data model, the decisions taken before any code is written, and the order of the work.

Nothing in this document is implemented yet. The current state of every format is in [FILE_HANDLING.md](FILE_HANDLING.md).

> **Document status**
> Verified against: `v0.2.0-alpha` (released 2026-10-05; previous release `v0.1.3-alpha`, 2026-10-01), plan written 2026-10-03 on top of `98d33d4` (phase 2.10), links checked 2026-10-05 (v0.2.0-alpha release check; 2026-10-04: beam conventions moved to CALCULATIONS_BEAM.md; beam files are routed to the beam adapter, which narrows P0 item 8). No code exists for any step below.
> When a step is done, move its content into [FILE_HANDLING.md](FILE_HANDLING.md) / [IO_USAGE.md](IO_USAGE.md) and mark the step done in section 6.

## 1. Goal

Three requirements, in this order:

1. **Take models in.** A model built in another program (mesh, materials, sections, supports, loads, analysis settings) can be imported, and every part ANAFINEN cannot represent is reported, never dropped silently.
2. **Keep models intact.** A model read from a format and written back to the same format keeps everything ANAFINEN understood: ids, sets, materials, sections, supports, loads, amplitudes, steps, units.
3. **Send models and results out completely.** What ANAFINEN writes opens in the target programs with the model **and** its results: displacements, rotations, reactions, element results, mode shapes and frequencies.

```text
                         import                                  export
  Abaqus / CalculiX  ── .inp ──┐                        ┌── .inp ─────────► Abaqus / CalculiX (model)
  FreeCAD FEM, PrePoMax        │                        ├── .frd ─────────► CalculiX CGX, PrePoMax, FreeCAD (model + results)
  Nastran tools      ── .bdf ──┤                        ├── .bdf ─────────► Femap, Patran, HyperMesh (model)
  Salome, I-DEAS     ── .unv / .med (via Gmsh, mesh) ─┐ │
  Gmsh, meshio       ── .msh ──┤      +-----------+   │ ├── .msh ─────────► Gmsh (model + results)   [done]
  ParaView, solvers  ── .vtu/.pvtu/.vtm/.vtp ──┼────► | MeshModel | ──┼─── .vtu / .pvd ──► ParaView, VisIt (results) [done]
  CAD                ── .step/.iges/.brep ─────┘      +-----------+   └── .step + sidecar ► CAD (geometry)  [done]
```

## 2. Where the industry stands (why these formats)

| Kind of data | Situation | Consequence for ANAFINEN |
|---|---|---|
| Model / input deck | Text decks are the common language: Abaqus `.inp` (also CalculiX, FreeCAD FEM, PrePoMax) and Nastran `.bdf`. Preprocessors (HyperMesh, ANSA, Femap) translate between solvers. | `.inp` first, `.bdf` second. Both are plain text with documented keywords. |
| Results | Native result files are mostly closed: Abaqus `.odb` (API only, version-locked), COMSOL `.mph`. Ansys `.rst`, Nastran `.op2` and LS-DYNA `d3plot` are partly documented. | Do not read or write closed result formats. Results go out as VTU / PVD (ParaView), MSH (Gmsh) and CalculiX `.frd` (documented text, read by CGX, PrePoMax and FreeCAD). |
| Open research / HPC | VTK, Gmsh MSH, XDMF / HDF5, Exodus II, CGNS. | VTK and MSH are done. Exodus / CGNS wait until large models or CFD need HDF5. |
| CAD | Native formats are closed; STEP AP242 is the neutral format. | Done (STEP / IGES / BREP through OCC). |

## 3. Target matrix

What each target program should get from ANAFINEN, and what ANAFINEN should read from it, once the plan is done.

| Program | Read from it | Write for it | Model | Results | Step |
|---|---|---|---|---|---|
| Gmsh | `.msh` | `.msh` | yes | yes | done (P0 adds materials, reactions) |
| ParaView / VisIt / PyVista | `.vtu`, `.pvd`, `.vtk`, `.pvtu`, `.vtm`, `.vtp` | `.vtu`, `.pvd`, `.vtk` | mesh, sets, BC arrays | yes | write done; read gaps in P5 |
| Abaqus | `.inp` | `.inp` | yes | no (`.odb` is closed) | P1, P2 |
| CalculiX (ccx, CGX) | `.inp`, `.frd` | `.inp`, `.frd` | yes | yes | P1–P3 |
| PrePoMax, FreeCAD FEM | `.inp`, `.frd` | `.inp`, `.frd` | yes | yes | P1–P3 |
| Femap, Patran, HyperMesh, Simcenter | `.bdf` | `.bdf` | yes | no (OP2 deferred) | P6 |
| Salome / Code_Aster, I-DEAS based tools | `.unv`, `.med` (mesh only, through Gmsh) | – | mesh, groups | no | P4 |
| CAD programs | `.step`, `.iges`, `.brep` | `.step` | geometry | no | done |

## 4. Gaps in the current data model

These must be closed before a deck format can be complete. They are format-neutral and go into `MeshModel` and the codec, so every existing format carries them too.

| # | Gap | Today | Why it blocks interoperability |
|---|---|---|---|
| G1 | Material properties | Files carry only the material name (`Material:<name>` set) and `MaterialID`. E, ν, ρ, yield live in the app's material library. | Another program (or another ANAFINEN installation without that user material) cannot solve the exported model. `.inp` / `.bdf` require `*MATERIAL` / `MAT1`. |
| G2 | Analysis steps | No step concept. All BCs and loads are active at once; amplitudes give time histories. | `.inp` is organized in `*STEP` blocks (static, frequency, heat transfer); `.bdf` in subcases. Loads and BCs belong to steps. |
| G3 | Unit system | `lengthUnit` string only, default `m`; readers never convert. | `.inp` and `.bdf` carry no units. N-mm-t-s decks are common; read as SI they are 1000× too large. |
| G4 | Reaction forces | Not computed by the solver, no field name. | A result set without reactions is not complete; every solver exports them (`RF`, `FORC`, `SPCFORCES`). |
| G5 | Solver element type names | Only the geometric type (`Hex8`, …) and `ElementFormulation` (bar / beam). | `C3D8` vs `C3D8R`, `S4` vs `S4R`, `CPS4` vs `CPE4` differ in formulation. Round trip `.inp` → `.inp` must keep the name. |
| G6 | Import report | Readers append text to `MeshModel::warnings`; the GUI logs them. | The user must see what was dropped (contact, plasticity, pressure loads, …) before trusting a solve. |
| G7 | Truss adapter drops rotational data silently | ARCHITECTURE.md section 8 item 5. | Imported frames from `.inp` / `.bdf` would lose moments without a word. |

## 5. Decisions (taken before implementation)

Each row follows the FILE_HANDLING.md section 3.3 rule: keep files and readers of 0.1.3 working, and write nothing new for a model that does not use the feature.

| Topic | Chosen | Rejected | Reason |
|---|---|---|---|
| Material storage (G1) | `MeshModel::materials`: `vector<MaterialDefinition{name, map<string, double> properties}>` with well-known keys (`YoungsModulus`, `PoissonRatio`, `ShearModulus`, `Density`, `YieldStrength`, `UltimateStrength`, later thermal keys). Encoded as global arrays `MaterialProperty:<material>:<key>` (1 × 1). | Element arrays per property; reusing `anaf::MATERIAL::MaterialProperties` in `anaf_io` | Globals already survive every format (MSH `$AnafData`, VTK `FieldData`, sidecar). `anaf_io` must not depend on `anaf_core` types. A key map lets decks carry properties the app does not model yet. |
| Material matching on import | The adapter matches by name (as today). A name that is unknown, or known with different values, becomes a **model material** for this session, shown in the Material Handler as such. It is saved to `userMaterials.json` only when the user asks. | Silently using the library values; writing imported materials to the user file automatically | The file's values are the model's truth; a library material with the same name may differ. Auto-saving would fill the user's library with every imported deck. |
| Analysis steps (G2) | `MeshModel::steps`: `vector<AnalysisStep{name, procedure, parameters}>` (`Static`, `Frequency`, `HeatTransferSteady`, `HeatTransferTransient`, `DynamicImplicit`). BCs and loads get an appended `step` member: empty = base state, active in every step. | A step id inside the amplitude name; one model per step | Mirrors `*STEP` and Nastran subcases. Appending keeps positional aggregate init valid (3.3). |
| Step encoding in arrays | New prefixes: `StepFixity:<step>`, `StepNodalForce:<step>[:<amplitude>]`, … Base-state data keeps the old names. | `NodalForce:<amplitude>@<step>` | 0.1.3 readers parse `NodalForce:<x>` as amplitude `x`; an unknown amplitude fails `validate()` and the **whole file** is rejected. New prefixes are just ignored by old readers. |
| `*BOUNDARY, OP=NEW` / BC removal between steps | Not modeled in the first version; the reader warns and keeps the BCs cumulative. | Full Abaqus propagation semantics | Rare in the target models (truss, frame); full semantics need per-step deactivation lists. Revisit with transient analyses. |
| Units (G3) | `MeshModel::unitSystem` enum (`SI` = N-m-kg-s, `SI_mm` = N-mm-t-s-MPa, `Unknown`), kept next to `lengthUnit`. Deck readers take the unit system from `ReadOptions` (GUI asks on import); deck writers write it as a `**` / `$` comment and readers pick that comment up. A separate, explicit `anaf::IO::convertUnits(model, to)` helper converts using the codec's array table. Readers still never convert. | Converting inside readers; guessing units from magnitudes | Silent conversion hides the assumption; guessing is wrong for small models. One explicit conversion point is testable. |
| Reactions (G4) | Solver computes `R = K u − f` on restrained DOFs (in the support frame for inclined supports, written in global axes). Field name `FieldName::ReactionForce` (node, 3) and later `ReactionMoment` (node, 3). | Only the reaction sum in the log | Every target format has a reaction field; ParaView users expect it next to `Displacement`. |
| Solver element names (G5) | Element sets named `ElementType:<name>` (e.g. `ElementType:C3D8R`), like `Material:<name>`. Writers of a deck format use the set when present, else a default name per type and formulation. | A string member in `ElementBlock`; a numeric code attribute | Sets already survive every format with no new codec code. Strings cannot go into numeric arrays. |
| Unknown deck keywords | Dropped with a warning that names keyword, line and count. No verbatim pass-through. | Keeping unknown keyword blocks and re-emitting them | Pass-through blocks reference node / element ids and set names; after editing or renumbering they would silently point at the wrong entities. |
| Import report (G6) | `MeshModel::warnings` grouped by category (unsupported keyword, unsupported element type, approximation, unit assumption). The GUI shows them in a modal after import; the CLI prints them. | Log lines only | The user must decide whether the imported model is still the intended one. |
| `.inp` dialect | One writer with `WriteOptions::inpDialect` (`Abaqus`, `CalculiX`). Flat deck (no `*PART` / `*INSTANCE`), which both accept. CalculiX mode avoids keywords ccx does not support and maps general beam sections to the closest supported one with a warning (check against the ccx manual of the version used in tests). | Two writers; writing parts and instances | Flat decks are valid in both programs; parts add nothing for a single solved model. |
| Results for Abaqus users | Not provided natively. Results go out as VTU / PVD / FRD. | Writing `.odb` or `.fil` | `.odb` needs Abaqus libraries. Out of scope for an open-source tool. |
| Results in `.frd` | Nodal fields directly (`DISP`, `FORC`, `NDTEMP`, mode shapes with their frequency). Element results (`AxialForce`, `Stress`, `BeamSectionForce`) are written as nodal-averaged tensors with a warning; the exact element values stay in VTU / MSH. | Leaving element results out of `.frd` | `.frd` stores nodal data; CGX / PrePoMax users still expect a stress plot. The exact per-element values remain available in the other formats. |
| Higher-order and polyhedral VTK cells | Lagrange cells (types 68–72) are read when their order is 2 and mapped to the existing quadratic types; higher orders and polyhedra (42) stay unsupported with a clear message. | New element types for arbitrary order | The solver has no use for them yet; mapping order 2 covers dolfinx / deal.II quadratic output. |
| Mesh-only formats Gmsh already reads (UNV, MED, STL, Medit, BDF mesh) | Read through the existing `GmshSession` with `gmsh::merge`, then the same entity / physical-group extraction as the CAD path. | Native parsers for each | Native parsers are large; Gmsh is already a dependency. BC / load data in those files is not covered by this path, so native `.bdf` stays in P6. |

## 6. Steps

Each step ends with `package/tools/check.sh all`, updated documents (ARCHITECTURE.md section 7 table) and an updated REVIEW_GUIDE.md. Suggested commit subjects follow `phase 2.x: <what changed>`; the phase number is the next free one when the step starts.

| Step | Content | Depends on | Status |
|---|---|---|---|
| P0 | Data model prerequisites: G1–G7 | – | planned |
| P1 | `.inp` reader | P0 | planned |
| P2 | `.inp` writer | P0 (P1 for round-trip tests) | planned |
| P3 | `.frd` writer and reader, CalculiX reference test | P2 | planned |
| P4 | Gmsh-bridged mesh import (UNV, MED, STL, Medit, BDF mesh) | – (independent, can run any time) | planned |
| P5 | VTK read gaps: `.pvtu`, `.vtm`, `.vtp`, order-2 Lagrange cells | – (independent) | planned |
| P6 | Nastran `.bdf` reader and writer | P0 | planned |
| Later | OP2 results, Exodus II / CGNS / XDMF, MED write, STEP AP209 | as needed | not scheduled |

### P0: data model prerequisites

1. `MeshModel::materials` + codec globals `MaterialProperty:<material>:<key>`; `validate()` checks that every `Material:<name>` set has a definition when `materials` is not empty.
2. Truss adapter: `toMeshModel()` writes the used materials with their values; `toMeshData()` applies the matching rule of section 5 (model materials).
3. `MeshModel::steps` and the appended `step` member on BCs and loads; codec prefixes `Step*:<step>`; MSH `$AnafData` record for step definitions.
4. `MeshModel::unitSystem`, `ReadOptions::unitSystem`, `anaf::IO::convertUnits()`; the truss adapter converts to SI before solving and back for export.
5. Reactions in `FEM::TRUSS::solveStatic()` (`StaticResult`), `FieldName::ReactionForce`, exported by `toMeshModel()`. Closed-form check in `anaf_core_tests` (sum of reactions = − sum of loads incl. self-weight).
6. `ElementType:<name>` set convention (documentation and helper only; no reader change needed).
7. Warning categories in `MeshModel` and the GUI import report modal.
8. Truss adapter warning for rotational data (closes ARCHITECTURE.md section 8 item 5). Beam formulations no longer reach the truss adapter: since 2026-10-04 `FEM::BEAM::ADAPTER::isBeamModel()` sends such files to the beam adapter.

Tests: every new member round-trips through MSH 2.2 / 4.1, VTK, VTU, `.pvd` and the STEP sidecar; a 0.1.3 truss file still reads unchanged; files written by P0 still open in the 0.1.3 reader (unknown arrays ignored, no `validate()` failure).

### P1: `.inp` reader (`formats/inpFormat.cpp`)

1. Lexer: case-insensitive keywords, `**` comments, continuation lines (trailing comma), `*INCLUDE, INPUT=` (relative to the deck), `GENERATE` on `*NSET` / `*ELSET`.
2. Assembly: `*PART` / `*INSTANCE` / `*ASSEMBLY` with instance translation and rotation; instance-qualified names (`Part-1-1.Set-1`) become plain set names; flat decks need no special case.
3. Mesh: `*NODE` (with `NSET=`), `*ELEMENT, TYPE=` through an Abaqus ↔ Gmsh node-order table (C3D10 and C3D20 differ from Gmsh order). Element names go into `ElementType:<name>` sets.
4. Sets: `*NSET`, `*ELSET`.
5. Model data: `*MATERIAL` (`*ELASTIC`, `*DENSITY`, `*PLASTIC` first point as yield with a warning), `*SOLID SECTION` (area for trusses), `*BEAM SECTION` / `*BEAM GENERAL SECTION` (section values, orientation vector), `*SHELL SECTION` (thickness attribute), `*TRANSFORM` (to `allowedMotion`), `*AMPLITUDE`, `*INITIAL CONDITIONS`.
6. Steps: `*STEP` with `*STATIC` / `*FREQUENCY` / `*HEAT TRANSFER` / `*DYNAMIC`; inside: `*BOUNDARY` (incl. `ENCASTRE`, `PINNED`, `XSYMM`, … shorthands), `*CLOAD`, `*DLOAD` `GRAV`, `*CFLUX`, `*DFLUX` `BF`. Output requests are ignored.
7. Everything else (contact, `*SURFACE`, pressure `*DLOAD`, `*EQUATION`, `*COUPLING`, …): category "unsupported keyword" warning.

Tests: hand-written fixture decks (flat and part / instance) committed under `tests/fixtures/inp/`; decks written by Gmsh's INP writer for every element type (node order check, same method as the Gmsh ↔ VTK permutation test).

### P2: `.inp` writer

1. `*HEADING` with the ANAFINEN version and the unit system comment.
2. `*NODE`, `*ELEMENT` per (type, `ElementType:` name), sets, materials, sections grouped by (material, section values), `*TRANSFORM` for inclined supports, amplitudes, initial conditions.
3. Base-state BCs and loads in the first step; per-step data in its step; a model without steps gets one `*STATIC` step.
4. `*NODE PRINT` / `*EL PRINT` and `*NODE FILE` / `*EL FILE` requests for `U`, `RF`, `S`, so a ccx run produces `.dat` and `.frd` output.
5. `WriteOptions::inpDialect` (section 5).

Tests: round trip `.inp` → `MeshModel` → `.inp` → `MeshModel` identical; every built-in truss from the library written and read back bit-exact.

### P3: `.frd` writer and reader (`formats/frdFormat.cpp`)

1. Writer: nodes (`2C`), elements (`3C`) with the FRD element type table, result blocks (`100C`) for `DISP`, `FORC` (reactions), `NDTEMP`, mode shapes with their frequency in the step header, element results as nodal-averaged tensors (section 5).
2. Reader: ASCII and binary `.frd` written by ccx; `DISP`, `STRESS`, `TOSTRAIN`, `FORC`, `NDTEMP`, eigenmodes into `StepKind::Mode` fields with `NaturalFrequency` globals.
3. CalculiX reference test (`ccx_reference_check`, like `vtk_reference_check`, skipped when `ccx` is not installed): write a truss / frame from the library as `.inp`, run ccx, read its `.frd`, compare displacements and reactions with `solveStatic()` within a tolerance. CalculiX expands trusses and beams into 3D elements internally, so the comparison uses the nodes of the original model.

### P4: Gmsh-bridged mesh import

1. `FileFormat::Unv`, `Med`, `Stl`, `Medit`, `BdfMesh` (read only), extensions in `formatTable()`.
2. `formats/gmshMeshFormat.cpp`: `gmsh::merge` inside `GmshSession`, then the existing node / element / physical-group extraction from `cadFormat.cpp` (moved to a shared helper).
3. The import report says "mesh and groups only".

MED needs a Gmsh build with MED support; the reader reports a clear error when the build lacks it.

### P5: VTK read gaps

1. `.pvtu`: read every piece and merge (the `.vtu` reader already merges pieces inside one file).
2. `.vtm`: read every `UnstructuredGrid` block (and `PolyData` blocks), one element set per block name.
3. `.vtp`: XML `PolyData` (vertices, lines, polys, strips), same splitting as legacy `POLYDATA`.
4. Order-2 Lagrange cells (68–72) mapped to the quadratic types.

### P6: Nastran `.bdf`

1. Reader: small, large and free field; continuation cards; `INCLUDE`; `GRID` (with `CP` / `CD` coordinate systems), `CORD2R`; `CROD`, `CBAR`, `CBEAM`, `CTRIA3/6`, `CQUAD4/8`, `CTETRA`, `CPENTA`, `CHEXA`; `MAT1`; `PROD`, `PBAR`, `PBARL`, `PBEAM`, `PSHELL`, `PSOLID`; `SPC`, `SPC1`, `SPCD`; `FORCE`, `MOMENT`, `GRAV`, `LOAD`; case control `SUBCASE` → steps; `SOL 101 / 103`.
2. Writer: free field, the same subset.
3. Tests: fixture decks; round trip; `.bdf` → `.inp` → `.bdf` cross chain.

## 7. Where data will be stored (after P0)

| Data | `MeshModel` member | MSH | VTK / VTU / `.pvd` | STEP sidecar | `.inp` | `.frd` | `.bdf` |
|---|---|---|---|---|---|---|---|
| Material values | `materials` | `$AnafData` globals | `FieldData` | `GLOBAL` | `*MATERIAL` | – | `MAT1` |
| Analysis steps | `steps` | `$AnafData` record | `FieldData` | `GLOBAL` | `*STEP` | step headers | `SUBCASE` |
| Step BCs / loads | `constraints`, `loads` (`step`) | `Step*:<step>` arrays | `Step*:<step>` arrays | fields | inside `*STEP` | – | load / SPC sets |
| Unit system | `unitSystem` | `$AnafData` | `FieldData` | `UNIT` line | `**` comment | – | `$` comment |
| Reactions | field `ReactionForce` | `$NodeData` | point data | field | – (solver output) | `FORC` | – |
| Solver element names | set `ElementType:<name>` | physical group | `ElementSet:` array | field | `*ELEMENT, TYPE=` | – | card name |

## 8. Open questions (to decide when the step starts)

- P0: are G and K stored as material keys too (they are stored in `MaterialProperties` for orthotropic wood), or derived on export? Decks take E and ν for isotropic materials.
- P1: should an imported deck with several `*STEP`s let the user pick the step to solve, or solve all steps in sequence once the solver supports it?
- P3: binary `.frd` writing, or ASCII only?
- P6: which coordinate system features (`CORD1*`, cylindrical / spherical `CORD2C/S`) are needed for the first version?

## 9. Related files

- Current formats and the data model: [FILE_HANDLING.md](FILE_HANDLING.md), [IO_USAGE.md](IO_USAGE.md)
- Known issue fixed by P0 item 8: [ARCHITECTURE.md](ARCHITECTURE.md) section 8 item 5
- Solver conventions the beam data must follow: [CALCULATIONS_BEAM.md](CALCULATIONS_BEAM.md) sections 3 and 7
- Code that changes: [meshModel.hpp](../src/io/model/meshModel.hpp), [modelCodec.cpp](../src/io/detail/modelCodec.cpp), [meshIo.cpp](../src/io/meshIo.cpp), [cadFormat.cpp](../src/io/formats/cadFormat.cpp), [trussMeshAdapter.cpp](../src/objectCalcs/truss_1D/trussIO/trussMeshAdapter.cpp), [fileIoPanel.cpp](../src/gui/panels/fileIoPanel.cpp)
