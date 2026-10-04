# Calculations: Truss FEM Engine

This document describes the finite element calculation for 3D truss structures built from 1D two-node bar elements. It covers the data types, the math, the solver portfolio, and the energy validator.

> **Document status**
> Verified against: `v0.2.0-alpha` (in development; last release `v0.1.3-alpha`, 2026-10-01), content checked 2026-10-04 (solver portfolio moved to `src/solvers/`, namespace `FEM::SOLVER`, Block-CG takes `dofsPerNode`, sections 7 and 11; 2026-10-03: beam data in `anaf_io`, section 13).
> Implemented: static displacement under nodal loads + self-weight.
> Not implemented yet: mass matrix, modal analysis (Spectra), beam/frame elements, CST.

## 1. Overall flow

One function solves every model: `FEM::TRUSS::solveStatic(mesh, materials, stop_token, progress)` (`trussEngine/trussSolver.hpp`) takes a `MeshData` snapshot, whatever made it (the Simple Quadrangle generator `buildSimpleTruss()`, a built-in library model, File > Import or the model editor), and returns a `StaticResult` (solved copy + energy check) or the reason it cannot solve. It has no GUI types: the GUI calls it from `TRUSS_WORKER::startSolve()` (`src/gui/panels/truss/trussWorker.cpp`), which copies the material list, builds the model on the worker thread, forwards the progress to `bridge.m_progress` and publishes the result and the energy check unless the model was reset meanwhile; a CLI or a test calls it directly.

```text
solveStatic(mesh, materials, st, progress)  (trussSolver.cpp)      progress
   |
   +-- buildSolverModel()   snapshot -> Node / TrussElement_1D, supports, loads   0.30
   +-- Truss_1D_Container
   |     +-- assembleStiffness()       upper-triangle triplets     0.50
   |     +-- considerWeight()          -rho*A*L*g split to nodes    0.55
   |     +-- calculateDisplacements()  false: solve failed / stopped
   |     |     u = T q (allowed directions) -> T^T K T, T^T f
   |     |     SOLVER::solveSelected()  (referee)                   0.85
   |     |     scatter back to nodes
   |     +-- calculateElementForcesAndStress()                      0.90
   |     +-- runValidator()   energy check                          0.95
   +-- logResult()   energy, max |u| (vector magnitude), max |stress|
   +-- solved copy of the snapshot -> StaticResult                  1.00
```

## 2. Data types

| Type | File | Holds |
|---|---|---|
| `Node` | `trussProperties/node.hpp` | `m_nodeID` (0-based), `m_Location[3]`, `m_displacement[3]`, `m_isMovable[3]`, `m_allowedMotionDirections` (orthonormal basis, up to 3 vectors) |
| `TrussElement_1D` | `trussProperties/element.hpp` | Material index (`m_type`), area, two node IDs; computed length and direction cosines (`double`: in `float` the stiffness entries carried ~1e-7 relative error and the energy check failed); results: elongation, stress (the axial force is σ A; it was stored but never read and is not kept since 2026-10-02) |
| `ForceApplied` | `trussProperties/appliedForce.hpp` | Node ID + force vector [N] |
| `Material` | `material/properties.hpp` | `MaterialProperties` (name, E, G, K, yield / ultimate strength, density, Poisson, ductility; all `double`), stable ID, built-in flag. Built-ins are loaded from `assets/bridge/materialProperties.json` ([BRIDGE.md](BRIDGE.md) section 5.1). |
| `Truss_1D_Container` | `trussEngine/trussSolver/deformationUnderConstForce.hpp` | Non-owning spans over force vector, nodes, elements; triplets; results; energy values |
| `MeshData`, `RenderElement` | `trussProperties/meshData.hpp` | The model / snapshot: nodes (with supports), bars, loads, `hasResults` (`anaf::BRIDGE` keeps aliases) |
| `StaticResult` | `trussEngine/trussSolver.hpp` | Result of `solveStatic()`: solved snapshot copy, energy check |

Units are SI throughout: m, m², N, Pa, kg/m³. The GUI enters the cross-section in cm²; the Simple Quadrangle panel and the model editor multiply by `1e-4` (snapshots store m²). `Material` wraps a `MaterialProperties` aggregate (filled with designated initializers) plus the built-in flag and the stable ID; the solver uses `elasticityModulus` (E).

The element constructor rejects invalid input by throwing `std::invalid_argument` / `std::out_of_range`:
- area ≤ 0
- identical nodes
- out-of-range node index
- zero length

### 2.1 Node constraints

- `setMovable({x, y, z})`: `true` means the DOF is free. It rebuilds the allowed-motion basis from the free axes.
- `setAllowedMotionDirections()` accepts arbitrary directions, orthonormalizes them with Gram-Schmidt (`FEM::TRUSS::orthonormalize()`, which throws on zero or dependent vectors, relative tolerance 1e-9), and derives `m_isMovable` from them. An axis is movable only if it lies in the span of the basis.
- `FEM::TRUSS::orthogonalComplement()` gives the perpendicular directions of a basis (each step takes the global axis with the largest part outside the span). The model editor uses it to turn restrained directions into the allowed motion.
- The solver works on the allowed-motion basis, not on `m_isMovable` (section 6). For an inclined support `m_isMovable` is only a summary: an axis counts as movable only when it lies fully in the span, so a roller along (1, 1, 0) reports x and y as fixed.
- `hasInclinedSupport()` is true when a basis vector is not a global axis. `isSupported()` is true when fewer than three directions are allowed. The node is the only place a support is stored (there is no separate fixity map since 2026-10-02): `solveStatic()` copies every node's basis, and `ADAPTER::toMeshModel()` writes a `NodeConstraint` for every supported node (an inclined one with `allowedMotion`).

## 3. Mesh generation: simple quadrangle prism truss

`buildSimpleTruss(cubeNum, edgeLength, areaM2, materialIndex)` (`trussTypes/simpleQuadranglePrismTrussCreate.cpp`) returns a `MeshData` snapshot without supports, loads or results; the panel adds the loads and the solve is the common one (section 1).

- It refuses (`std::unexpected`) a zero cube number, a length or area that is not finite and > 0, and grids whose DOF count does not fit the `int` triplet indices. Before 2026-10-02 the generator ran in OpenMP loops and a zero area threw from the element constructor inside them, which called `std::terminate`.
- Nodes: `(nx+1)(ny+1)(nz+1)` on a grid, `id = i + j*(nx+1) + k*(nx+1)(ny+1)`, position `(a*i, a*j, a*k)`.
- Bars, in this order (plain serial loops, `k` outermost, `i` innermost):

| Group | Count |
|---|---|
| X edges | `nx (ny+1)(nz+1)` |
| Y edges | `(nx+1) ny (nz+1)` |
| Z edges | `(nx+1)(ny+1) nz` |
| XY face diagonals (2 per face) | `2 nx ny (nz+1)` |
| XZ face diagonals | `2 nx (ny+1) nz` |
| YZ face diagonals | `2 (nx+1) ny nz` |

### 3.1 Solving a snapshot (`solveStatic()`)

The input is a `MeshData` snapshot: generated (section 3), a library model (section 3.2), imported, or entered in the model editor. `buildSolverModel()` (inside `trussSolver.cpp`) turns it into solver objects:

```text
buildSolverModel(snapshot, materials)
   nodes: ids must equal positions 0..n-1
   bars:  every RenderElement except isWireframe
          -> TrussElement_1D(materialID, area, node1, node2)
   nodes used by no bar  -> all DOFs fixed (warning)
   snapshot node support -> setAllowedMotionDirections()
   loads -> force vector [3 * id + axis]
```

The result is a copy of the snapshot: displacement on every node, stress on the solved bars, wireframe edges keep stress 0, `hasResults = true`.

`solveStatic()` returns an error text instead of a result when:

| Condition | Why |
|---|---|
| No nodes, or no bars (a surface / volume import has only wireframe edges) | Nothing to assemble |
| A bar has area ≤ 0 | Imported files without a `CrossSectionArea` attribute; the editor's "Apply to All Bars" sets one |
| A bar's material index is outside the material list | Stiffness and weight need E and ρ |
| A node id differs from its position | Element node indices address the node vector directly |
| A bar references a missing node, both ends are the same node, or its length is 0 | The `TrussElement_1D` constructor would throw |

A structure that is still a mechanism (for example too few supports) reaches the referee and fails there, as in section 7; `calculateDisplacements()` then returns false and `solveStatic()` reports the failed solve (before 2026-10-02 the zero displacements went on to the energy check, which passed trivially). A stop request returns "cancelled".

Verified by `anaf_truss_io_tests` (a solved generated grid written to MSH, read back and solved again matches the first result) and by the closed-form cases of `anaf_core_tests` (section 11).

### 3.2 Built-in truss library (`FEM::TRUSS::LIBRARY`)

Ready-to-solve models in [assets/objects/truss/truss1D/](../assets/objects/truss/truss1D/): one MSH 4.1 ASCII file per model (bars, per-bar `CrossSectionArea`, `Material:<name>` set, supports, nodal loads) and `index.json` (id, name, category, description). They are loaded through the normal import path into `truss_imported_or_entered`.

```text
trussLibrary.cpp  buildLibrary()  --- anaf_truss_library_tool --->  assets/objects/truss/truss1D/*.msh + index.json
       |                                                                  |
       +--- anaf_truss_io_tests: regenerate in a temp folder, byte-compare +--> GUI: Model Editor > Built-in Models
```

1. The files are generated, never edited: change `trussLibrary.cpp`, run `build/tests/anaf_truss_library_tool`, commit the result. The test `builtInTrussLibraryMatchesTheGenerator` fails on any difference.
2. `builtInTrussesAreStableAndSolve` checks every model: materials resolve to the built-ins by name, every bar has a section, the reduced stiffness matrix has no zero eigenvalue (λmin / λmax > 1e-12, i.e. no mechanism; above 1200 free DOFs the smallest / largest `SimplicialLDLT` pivot is used instead, which lies in [λmin, λmax] and avoids a dense eigen solve), the solve passes the energy check, the deflection stays below L/250 of the model extent and every bar stays below yield.
3. Read-only in the application: loading makes an in-memory copy; File > Export refuses to write into the library folder (Linux packages also install it read-only).

Planar trusses (roofs, bridges, the grandstand) are made spatial by `extrude()`: copies of the plane truss are tied by a strut at every node and a brace in every face swept by a member (purlins + roof bracing, floor beams + wind bracing), which is a stable space truss. Pin at one end (x, y, z), roller at the other (y).

The fuselage and both wings are built by `boxGirder()`: stations of four corners joined by chords, a triangulated frame at every station and one diagonal (or a cross) in every face of every bay, so each bay is a closed triangulated polyhedron. The brace direction is chosen per face (`Brace::Rising`, `Falling`, `WarrenRising`, `WarrenFalling`, `Cross`); the biplane uses it to put only the flying wires in the strut planes (landing wires are slack under positive g and a linear truss would load them in compression). The airliner wing box passes a per-bay cap area (`BoxGirder::chordAreaOf`) to taper the spar caps.

The large models are generated directly as space trusses: the oval stadium closes 48 radial cantilevers into a ring with the `extrude()` bracing pattern; the Kiewitt dome and the geodetic fuselage are fully triangulated surfaces (the fuselage with six triangulated bulkheads, which stop the ovalisation mechanism of an open tube); the airship rings are wire-braced wheels around an axial wire anchored in the nose and tail cones. `latticeTower()` takes per-level half width and areas, so the 300 m tower follows an exponential profile.

| Category | Id | Model |
|---|---|---|
| Roof | `roof_king_post` | King post, span 8 m |
| Roof | `roof_queen_post` | Queen post, span 10 m |
| Roof | `roof_fink` | Fink (W), span 12 m |
| Roof | `roof_howe` | Howe, span 16 m, 8 panels |
| Roof | `roof_pratt` | Pratt, span 20 m, 10 panels |
| Roof | `roof_scissors` | Scissors, span 10 m, sloped ceiling |
| Roof | `roof_bowstring` | Bowstring (hangar), span 24 m |
| Bridge | `bridge_pratt` | Pratt through truss, 36 m |
| Bridge | `bridge_howe` | Howe through truss, 36 m |
| Bridge | `bridge_warren` | Warren, 30 m |
| Bridge | `bridge_k_truss` | K-truss, 48 m |
| Bridge | `bridge_parker` | Parker (camelback), 48 m |
| Bridge | `bridge_continuous_three_span` | Three-span continuous railway truss, 3 × 120 m, haunched over the piers (S355) |
| Stadium | `stadium_grandstand_cantilever` | 20 m cantilever grandstand roof |
| Stadium | `stadium_space_frame` | 24 × 24 m double-layer grid |
| Stadium | `stadium_schwedler_dome` | Schwedler dome, 36.8 m |
| Stadium | `stadium_geodesic_dome` | 3V geodesic dome, r = 10 m (aluminum) |
| Stadium | `stadium_oval_ring_roof` | Full oval stadium roof, 240 × 190 m, 48 radial cantilever trusses, 720 nodes (S355) |
| Stadium | `stadium_wembley_arch` | Leaning 315 m triangular lattice arch carrying the roof cables (S355) |
| Stadium | `stadium_kiewitt_dome` | Kiewitt (lamella) dome, 210 m span, 469 nodes (S355) |
| Tower & Platform | `tower_transmission` | 30 m transmission tower with cross-arms |
| Tower & Platform | `platform_offshore_jacket` | 40 m four-leg offshore jacket |
| Tower & Platform | `tower_crane_jib` | 24 m triangular crane jib |
| Tower & Platform | `tower_eiffel_style` | 300 m lattice tower with an exponential profile, platforms and wind (mild steel) |
| Aircraft | `aircraft_tube_fuselage` | 5.9 m welded 4130 tube fuselage (Warren), 3.8 g |
| Aircraft | `aircraft_engine_mount` | Four-point welded engine mount, engine CG on stiff links |
| Aircraft | `aircraft_strut_braced_wing` | 5 m strut-braced half wing, truss spars, Schrenk lift (aluminum) |
| Aircraft | `aircraft_biplane_wing_cell` | Two-bay biplane wing cell: struts, flying wires, drag wires |
| Aircraft | `aircraft_rigid_airship` | 200 m Zeppelin-type hull: wire-braced rings, gas-cell lift, moored (duralumin) |
| Aircraft | `aircraft_geodetic_fuselage` | 18 m Wellington-style geodetic (diagrid) fuselage, 3 g, 620 nodes (duralumin) |
| Aircraft | `aircraft_airliner_wing_box` | 34 m full-span swept airliner wing box with engines, 1 g cruise (7075-T6) |

Dimensions, loads and sections of each model are in its `description` (shown in the GUI).

## 4. Global stiffness assembly

For element *e* with nodes *i*, *j*, length *L*, area *A*, modulus *E*, and direction cosines **c** = (cx, cy, cz), Logan ch. 3 gives:

```text
k = (A E / L) * |  C  -C |      C = c c^T  (3x3)
                | -C   C |
```

Only the upper triangle of the symmetric global matrix is stored:

| Block | Triplets per element |
|---|---|
| Diagonal block (i,i), upper part | 6 |
| Diagonal block (j,j), upper part | 6 |
| Off-diagonal block (min(i,j), max(i,j)), full 3x3, negative | 9 |
| **Total** | **21** |

Triplets are written in parallel into a preallocated vector (`index * 21`). Eigen's `setFromTriplets` sums the duplicates.

The modulus used is `Material::getElasticityModulus()` (E).

## 5. Load vector

- `m_forceVec` has `3 * nodeCount` entries, indexed `3 * nodeId + axis`.
- User loads are written first.
- `considerWeight()` adds the self-weight `W = ρ A L g` with `g = -9.80665` along +Y. Half goes to each end node's Y DOF. This is lumped gravity, with `#pragma omp atomic` for the shared nodes.

## 6. Boundary conditions and the reduced system

Each node owns one reduced DOF `q_k` per allowed-motion direction `b_k` (orthonormal, 0 to 3 of them). The global displacements follow from `u = T q`, with `T(3 n + axis, k) = b_k[axis]`, and the reduced system is `(Tᵀ K T) q = Tᵀ f` (inclined supports, Logan ch. 3). For supports along the global axes `T` only selects columns, so this is the classic fixed-DOF removal with the same matrix.

1. Reduced DOFs are numbered node by node. `nodeDofSlots[3 n + k]` is the reduced DOF of direction k of node n (`-1` when unused); Block-CG uses it for its node blocks (the container passes `dofsPerNode = 3`).
2. Every global DOF gets its links `(reduced DOF, b_k[axis])`, one for an axis-aligned node and up to three for an inclined one.
3. Each stored upper triplet `K(i, j)` and its mirror `K(j, i)` are expanded over the links of i and j; only upper-triangle entries of `Tᵀ K T` are kept. This is done in parallel: per-thread count, prefix sum, parallel scatter.
4. A reduced `SparseMatrix` (upper triangle only) and `Tᵀ f` are built.
5. After the solve, `u_n = Σ_k b_k q_k` gives every node's `m_displacement`; fully fixed nodes get 0.

`anaf_truss_io_tests` (`inclinedSupportsMatchTheRotatedModel`) turns a triangle truss about the gravity axis so that its roller and in-plane supports become inclined, sends it through an MSH file, and checks that the displacements turn with the model and the stresses stay the same.

Supports are homogeneous (zero prescribed displacement). Reaction forces are not computed yet.

## 7. Solver portfolio (`FEM::SOLVER`)

The portfolio lives in `src/solvers/`, outside `truss_1D/`, so that later element types (beam, CST) use the same solvers. It only sees the reduced system `K_upper u = f`; assembly and DOF reduction stay in each element's container. The truss container calls `FEM::SOLVER::solveSelected()`.

```text
src/solvers/
  solverPortfolio.hpp     Type, Result, every solve* declaration
  solver_referee.cpp      solveSelected(): picks a solver by DOF count, hardware log
  direct/                 solver_cholmod.cpp, solver_simplicial.cpp
  iterative/              solver_iterative.cpp (Block-CG)
```

The direct solvers do not care how DOFs map to nodes. Block-CG does: `solveSelected()` and `solveBlockCG()` take `totalNodes`, `dofsPerNode` and a `remapTable` with `totalNodes * dofsPerNode` entries, where `remapTable[dofsPerNode * node + k]` is the reduced DOF of slot k of that node (`-1` when the slot is fixed or unused).

| Element | `dofsPerNode` | Slots |
|---|---|---|
| Truss (3D bar) | 3 | allowed motion directions (`u = T q`, section 6) |
| 3D beam (planned) | 6 | 3 translations + 3 rotations |

The preconditioner inverts one `dofsPerNode x dofsPerNode` block per node over its used slots; the rows and columns of unused slots stay zero. `FEM::SOLVER::maxDofsPerNode = 6` bounds the block (a stack array per node, no allocation in the CG loop). A `dofsPerNode` outside 1 … 6 or a remap table of the wrong size returns `converged = false` with a message instead of reading out of range.

`solveSelected()` is the referee:

```text
dofs <= 400,000 ?
   |yes                                         |no
   v                                            v
 CHOLMOD available?                          OpenMP Block-CG
   |yes              |no                      accept if residual <= 1e-7
   v                 v
 CholmodSupernodalLLT   SimplicialLDLT
   |
   residual <= 1e-7 && finite ?
   |no --> fallback: SimplicialLDLT
```

| Solver | File | Method | Notes |
|---|---|---|---|
| `solveCholmod` | `direct/solver_cholmod.cpp` | Eigen `CholmodSupernodalLLT<..., Upper>` | Only when `ANAFINEN_HAS_CHOLMOD`, otherwise a stub that returns "not available" |
| `solveSimplicialLDLT` | `direct/solver_simplicial.cpp` | Eigen `SimplicialLDLT<..., Upper>` | Always available |
| `solveBlockCG` | `iterative/solver_iterative.cpp` | Preconditioned CG, per-node block-Jacobi preconditioner (`dofsPerNode x dofsPerNode` blocks) | Tolerance 1e-8, max 50,000 iterations, logs every 200, honors `stop_token` |

Every result is a `Result` record: `type` (`SOLVER::Type`), `available`, `converged`, `iterations`, `relativeResidual = ‖f − K u‖ / ‖f‖`, `elapsedSeconds`, `message`. The referee also logs a hardware summary: CPU name, hardware / OpenMP / Eigen threads, and available / total RAM. It reads them from `anaf::PLATFORM` (`querySystemInfo()` once, `queryMemory()` per solve; Linux and Windows), the same source as the GUI status bar. "Available" is the memory the OS can hand out without swapping (Linux `MemAvailable`, page cache included).

If the solve is rejected, every displacement is set to zero and an error is logged. The pipeline continues, so the validator then runs on a zero solution.

## 8. Element results

For each element:

```text
elongation  δ = [-c, c] · [u_i, u_j]
force       N = (δ / L) E A
stress      σ = N / A
gravity add σ_g = 0.5 ρ L |g · c|          (self-weight contribution along the bar axis)
stored:     axialForce = N + sign(N) σ_g A,  stress = σ + sign(σ) σ_g      (sign = std::copysign)
```

Sign convention: **tension > 0, compression < 0** for stress (and so for the axial force σ A). `σ_g` is an engineering approximation that raises the magnitude in the direction of the FE result, so `|stress|` is the peak-magnitude envelope. Consumers that need a magnitude use `std::abs`: yield check, viewport color, log maximum.

Verified with a single-cube test: a downward load on a top node gives compression in the vertical bar below it, and an upward load gives tension.

`isStressExceeded = |stress| > yieldTensileStrength` is evaluated when the snapshot is built.

## 9. Validator: energy balance

By Clapeyron's theorem, for a linear elastic system under static loads:

```text
U_internal = Σ_e ½ (E A / L) δ_e²
W_external = Σ_dof f · u
check:  |U_internal − ½ W_external|  ≤ 1e-12   or   relative diff ≤ 1e-7
```

The result is written to `bridge.m_isValid` and `bridge.m_energyDiff` and logged as VALID/INVALID with both energies. The check catches assembly or solve errors. Because it uses the same δ as the stress calculation, it does not catch modeling errors such as wrong units or material.

## 10. Parallelism summary

| Step | Parallel construct |
|---|---|
| Node/element generation | `omp parallel for collapse(3)` |
| Fixity application | `omp parallel for` over nodes |
| Assembly | `omp parallel for`, disjoint output slots |
| Self-weight | `omp parallel for` + `omp atomic` |
| Triplet reduction | per-thread counts + prefix sum + scatter |
| Factorization | CHOLMOD / BLAS internal threads; Eigen `setNbThreads` |
| Block-CG | `omp parallel for` preconditioner, Eigen SpMV |
| Stress, energy | `omp parallel for` with `reduction(+)` |

## 11. Tests

`anaf_core_tests` (`tests/coreTests.cpp`) tests the core alone: it links `anaf_core` only, so it also fails to build if the core ever needs the bridge or the GUI again. Materials are built in the test (`MaterialProperties`), not read from the JSON. Physics checks compare `solveStatic()` with closed-form results:

| Test | Checks |
|---|---|
| `elementGeometryAndValidation` | Length and direction cosines of a (3, 4, 12) bar; the constructor refuses area 0, one node twice, a node outside the list, coincident nodes |
| `nodeSupportBasis`, `supportDirectionsAndTheirComplement` | `setMovable` basis, Gram-Schmidt of arbitrary directions, `isSupported` / `hasInclinedSupport`, `m_isMovable` summary, `orthonormalize` errors, `orthogonalComplement` both ways |
| `simpleTrussGridAndInvalidParameters` | `buildSimpleTruss()` node / bar counts and the id formula; refused parameters (zero area, length, cube number, NaN) |
| `axialBarMatchesPLoverAE` | One skewed bar on a rail along itself: u = P L / (A E), σ = P / A |
| `twoBarTrussMatchesTheHandSolution` | σ = −P / (2 A sin 45°), v = −P L / (2 A E sin² 45°), symmetric apex, wireframe edge and isolated node untouched |
| `indeterminateThreeBarTruss` | Statically indeterminate three-bar hanger: F_v = P / (1 + 2 cos³ θ), F_i = P cos² θ / (1 + 2 cos³ θ), v = F_v L / (E A) |
| `hangingBarUnderItsOwnWeight` | Self weight lumped half per node (both node orders of the bar): u = ρ g L² / (2 E); stress envelope ρ g L |
| `inclinedRailCarriesTheLoadAlongItself` | `u = T q` with a skewed rail: s = 2 P / k for a rail at 45° to the bar |
| `unsolvableModelsAreReported`, `mechanismIsAnErrorNotAResult` | Error texts instead of results (no nodes / bars, area, material, ids, a mechanism) |
| `cancelledSolveAndProgress` | A stop request returns "cancelled"; progress is non-decreasing and ends at 1 |
| `blockCgMatchesTheDirectSolver` | Block-CG against SimplicialLDLT on SPD 200-node chains with 3 and 6 DOFs per node, each with and without unused slots (`-1` in the remap table); the referee picks Block-CG only above 400k DOFs |
| `blockCgRejectsABadNodeLayout` | `dofsPerNode` of 0 or above `maxDofsPerNode`, or a remap table of the wrong size, is refused instead of read out of range |
| `materialValidation` | `validateMaterial()` limits and name rules, `sameMaterialName()` |

The tests were checked against injected faults: a wrong self-weight split and a wrong energy balance each make tests fail. `anaf_truss_io_tests` covers the paths through files, the bridge and the built-in library (section 3.2).

## 12. Known issues

- Reaction forces are not computed.

## 13. Planned (not in code yet)

- Consistent/lumped mass matrix and the generalized eigenproblem `K φ = ω² M φ` with Spectra `SymGEigsShiftSolver` (shift-invert).
- 2D/3D beam/frame elements (Euler-Bernoulli, Timoshenko) and 2D CST. The file side is ready since phase 2.10: `anaf_io` stores rotational fixity, prescribed rotations, nodal moments, section properties (`SecondMomentY/Z`, `TorsionConstant`, `ShearAreaY/Z`), `ElementFormulation` and the orientation vector, and defines `Rotation` and `BeamSectionForce` results ([FILE_HANDLING.md](FILE_HANDLING.md) sections 3 and 3.3). A beam solver must:
  - build the local frame from `MeshModel::beamOrientation`: x = node 0 → node 1, z = normalize(x × v), y = z × x; a zero v needs a default rule, e.g. global Z as the reference, global X for vertical members;
  - read `ElementFormulation` (missing = bar) and take `ShearAreaY/Z` for Timoshenko only;
  - write `BeamSectionForce` in section convention: the values at node 0 are −(k·u) there and those at node 1 are +(k·u), so N > 0 is tension at both ends;
  - also write `AxialForce`, and write mode shapes as `Displacement` + `Rotation`.
- Imported BCs beyond `fixed`, `allowedMotion` and `force` (prescribed displacements, amplitudes, thermal loads, rotational fixity, prescribed rotations, nodal moments) are read by `anaf_io` but not used by either truss solver. The rotational ones are dropped without a warning ([ARCHITECTURE.md](ARCHITECTURE.md) section 8, item 5).

## 14. Related source files

- Orchestration: [src/objectCalcs/truss_1D/trussEngine/trussSolver.hpp](../src/objectCalcs/truss_1D/trussEngine/trussSolver.hpp), [trussSolver.cpp](../src/objectCalcs/truss_1D/trussEngine/trussSolver.cpp); model types: [trussProperties/meshData.hpp](../src/objectCalcs/truss_1D/trussProperties/meshData.hpp)
- Container: [deformationUnderConstForce.hpp](../src/objectCalcs/truss_1D/trussEngine/trussSolver/deformationUnderConstForce.hpp), [deformationUnderConstForce.cpp](../src/objectCalcs/truss_1D/trussEngine/trussSolver/deformationUnderConstForce.cpp)
- Solvers: [solverPortfolio.hpp](../src/solvers/solverPortfolio.hpp), [solver_referee.cpp](../src/solvers/solver_referee.cpp), [direct/solver_cholmod.cpp](../src/solvers/direct/solver_cholmod.cpp), [direct/solver_simplicial.cpp](../src/solvers/direct/solver_simplicial.cpp), [iterative/solver_iterative.cpp](../src/solvers/iterative/solver_iterative.cpp)
- Types: [node.hpp](../src/objectCalcs/truss_1D/trussProperties/node.hpp), [element.hpp](../src/objectCalcs/truss_1D/trussProperties/element.hpp), [appliedForce.hpp](../src/objectCalcs/truss_1D/trussProperties/appliedForce.hpp), [properties.hpp](../src/material/properties.hpp)
- Generator: [simpleQuadranglePrismTrussCreate.cpp](../src/objectCalcs/truss_1D/trussTypes/simpleQuadranglePrismTrussCreate.cpp)
- Tests: [tests/coreTests.cpp](../tests/coreTests.cpp)
- Built-in library: [trussLibrary.hpp](../src/objectCalcs/truss_1D/trussTypes/trussLibrary.hpp), [trussLibrary.cpp](../src/objectCalcs/truss_1D/trussTypes/trussLibrary.cpp), [tests/trussLibraryTool.cpp](../tests/trussLibraryTool.cpp), [assets/objects/truss/truss1D/](../assets/objects/truss/truss1D/)
