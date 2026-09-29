# Calculations: Truss FEM Engine

This document describes the finite element calculation for 3D truss structures built from 1D two-node bar elements. It covers the data types, the math, the solver portfolio, and the energy validator.

> **Document status**
> Verified against: `v0.1.3-alpha` working tree (unreleased), 2026-09-29.
> Implemented: static displacement under nodal loads + self-weight.
> Not implemented yet: mass matrix, modal analysis (Spectra), beam/frame elements, CST.

## 1. Overall flow

Two orchestrators feed the same pipeline: `Truss_SQPT` for the generated grid truss (below) and `Truss_Imported_or_Entered` for imported or hand-built models (section 3.1). Everything from `calculate()` on is shared (`detail::runStaticSolve()`).

```text
Truss_SQPT (trussSolver_SQPT.cpp)                       progress
   |
   +-- trussSetAndSetFix_SQPT(fixedDOFs copy)             0.20
   |     SimpleTruss::setTruss()  -> nodes + elements
   |     fixity copy              -> Node::setMovable()
   |
   +-- trussSetForce_SQPT()                               0.25
   |     ForceApplied list        -> m_forceVec[3*id + axis]
   |
   +-- setContainer()                                     0.30
   |     Truss_1D_Container gets std::span views (no ownership)
   |
   +-- calculate()
         |
         +-- assembleStiffness()       upper-triangle triplets     0.50
         +-- considerWeight()          -rho*A*L*g split to nodes    0.55
         +-- calculateDisplacements()
         |     u = T q (allowed directions) -> T^T K T, T^T f
         |     SOLVER::solveSelected()  (referee)                   0.85
         |     scatter back to nodes
         +-- max |displacement| for the log (node locations stay undeformed)
         +-- calculateElementForcesAndStress()                      0.90
         +-- runValidator()   energy check                          0.95
         +-- write m_isValid / m_energyDiff to bridge
```

## 2. Data types

| Type | File | Holds |
|---|---|---|
| `Node` | `trussProperties/node.hpp` | `m_nodeID` (0-based), `m_Location[3]`, `m_displacement[3]`, `m_isMovable[3]`, `m_allowedMotionDirections` (orthonormal basis, up to 3 vectors) |
| `TrussElement_1D` | `trussProperties/element.hpp` | Material index (`m_type`), area, two node IDs; computed length and direction cosines (`double`: in `float` the stiffness entries carried ~1e-7 relative error and the energy check failed); results: elongation, axial force, stress |
| `ForceApplied` | `trussProperties/appliedForce.hpp` | Node ID + force vector [N] |
| `Material` | `material/properties.hpp` | E, G, K, yield/ultimate strength, density, Poisson, ductility, ID, built-in flag. Built-ins are loaded from `assets/bridge/materialProperties.json` ([BRIDGE.md](BRIDGE.md) section 5.1). |
| `SimpleTruss` | `trussTypes/simpleQuadranglePrismTrussCreate.hpp` | Owns node and element vectors of the generated prism truss |
| `Truss_1D_Container` | `trussEngine/trussSolver/deformationUnderConstForce.hpp` | Non-owning spans over force vector, nodes, elements; triplets; results; energy values |
| `Truss_SQPT` | `trussEngine/trussSolver.hpp` | Orchestrates one solve for the simple quadrangle prism truss |
| `Truss_Imported_or_Entered` | `trussEngine/trussSolver.hpp` | Orchestrates one solve for an imported or hand-built snapshot (section 3.1) |

Units are SI throughout: m, m², N, Pa, kg/m³. The GUI enters the cross-section in cm²; `Truss_SQPT`, the preview and the model editor all multiply by `1e-4` (snapshots store m²). `Material` has both `m_elasticityModulus` and `m_youngModulus`; the solver uses `m_elasticityModulus`.

The element constructor rejects invalid input by throwing `std::invalid_argument` / `std::out_of_range`:
- area ≤ 0
- identical nodes
- out-of-range node index
- zero length

### 2.1 Node constraints

- `setMovable({x, y, z})`: `true` means the DOF is free. It rebuilds the allowed-motion basis from the free axes.
- `setAllowedMotionDirections()` accepts arbitrary directions, orthonormalizes them with Gram-Schmidt, and derives `m_isMovable` from them. An axis is movable only if it lies in the span of the basis.
- The solver works on the allowed-motion basis, not on `m_isMovable` (section 6). For an inclined support `m_isMovable` is only a summary: an axis counts as movable only when it lies fully in the span, so a roller along (1, 1, 0) reports x and y as fixed.
- `hasInclinedSupport()` is true when a basis vector is not a global axis. `Truss_Imported_or_Entered::setModel()` keeps such a node's basis instead of the axis fixity map, and `ADAPTER::toMeshModel()` writes it as `NodeConstraint::allowedMotion`.

## 3. Mesh generation: simple quadrangle prism truss

Input: cube counts `(nx, ny, nz)`, edge length `a`, area `A`, material index.

- Nodes: `(nx+1)(ny+1)(nz+1)` on a grid, `id = i + j*(nx+1) + k*(nx+1)(ny+1)`, position `(a*i, a*j, a*k)`.
- Elements, written into preallocated ranges, so the element index is deterministic:

| Range | Count |
|---|---|
| X edges | `nx (ny+1)(nz+1)` |
| Y edges | `(nx+1) ny (nz+1)` |
| Z edges | `(nx+1)(ny+1) nz` |
| XY face diagonals (2 per face) | `2 nx ny (nz+1)` |
| XZ face diagonals | `2 nx (ny+1) nz` |
| YZ face diagonals | `2 (nx+1) ny nz` |

Every loop is `#pragma omp parallel for collapse(3)`. Each iteration writes its own slot, so no synchronization is needed.

### 3.1 Imported / self-built trusses (`Truss_Imported_or_Entered`)

The model is not generated: it is the `MeshData` snapshot that File > Import or the model editor published. Only the model source differs; the container, the referee and the solvers are the same as for `Truss_SQPT` (both call `detail::runStaticSolve()`, `trussSolver_static.cpp`).

```text
Truss_Imported_or_Entered (trussSolver_Imported.cpp)       progress
   |
   +-- setModel(snapshot, fixity copy, materials copy)      0.20
   |     nodes: ids must equal positions 0..n-1
   |     bars:  every RenderElement except isWireframe
   |            -> TrussElement_1D(materialID, area, node1, node2)
   |     nodes used by no bar -> all DOFs fixed (warning)
   |     fixity copy          -> Node::setMovable()
   |
   +-- setForce(snapshot loads)                             0.25
   +-- setContainer()                                       0.30
   +-- calculate()  = detail::runStaticSolve()              0.50 .. 0.95
   +-- buildResultMesh(snapshot)
         copy of the snapshot; displacement on every node, stress on the
         solved bars; wireframe edges keep stress 0; hasResults = true
```

`setModel()` returns an error text instead of solving when:

| Condition | Why |
|---|---|
| No nodes, or no bars (a surface / volume import has only wireframe edges) | Nothing to assemble |
| A bar has area ≤ 0 | Imported files without a `CrossSectionArea` attribute; the editor's "Apply to All Bars" sets one |
| A bar's material index is outside the material list | Stiffness and weight need E and ρ |
| A node id differs from its position | Element node indices address the node vector directly |
| A bar references a missing node, both ends are the same node, or its length is 0 | The `TrussElement_1D` constructor would throw |

A structure that is still a mechanism (for example too few supports) reaches the referee and fails there, as in section 7.

Verified by `anaf_truss_io_tests`: a generated truss written to MSH, read back and solved through this path matches the `Truss_SQPT` result, and a two-bar truss matches the hand solution σ = −P / (2 A sin 45°), v = P L / (2 A E sin² 45°).

### 3.2 Built-in truss library (`FEM::TRUSS::LIBRARY`)

Ready-to-solve models in [assets/objects/truss/truss1D/](../assets/objects/truss/truss1D/): one MSH 4.1 ASCII file per model (bars, per-bar `CrossSectionArea`, `Material:<name>` set, supports, nodal loads) and `index.json` (id, name, category, description). They are loaded through the normal import path into `truss_imported_or_entered`.

```text
trussLibrary.cpp  buildLibrary()  --- anaf_truss_library_tool --->  assets/objects/truss/truss1D/*.msh + index.json
       |                                                                  |
       +--- anaf_truss_io_tests: regenerate in a temp folder, byte-compare +--> GUI: Model Editor > Built-in Models
```

1. The files are generated, never edited: change `trussLibrary.cpp`, run `build/tests/anaf_truss_library_tool`, commit the result. The test `builtInTrussLibraryMatchesTheGenerator` fails on any difference.
2. `builtInTrussesAreStableAndSolve` checks every model: materials resolve to the built-ins by name, every bar has a section, the reduced stiffness matrix has no zero eigenvalue (λmin / λmax > 1e-12, i.e. no mechanism), the solve passes the energy check, the deflection stays below L/250 of the model extent and every bar stays below yield.
3. Read-only in the application: loading makes an in-memory copy; File > Export refuses to write into the library folder (Linux packages also install it read-only).

Planar trusses (roofs, bridges, the grandstand) are made spatial by `extrude()`: copies of the plane truss are tied by a strut at every node and a brace in every face swept by a member (purlins + roof bracing, floor beams + wind bracing), which is a stable space truss. Pin at one end (x, y, z), roller at the other (y).

The fuselage and both wings are built by `boxGirder()`: stations of four corners joined by chords, a triangulated frame at every station and one diagonal (or a cross) in every face of every bay, so each bay is a closed triangulated polyhedron. The brace direction is chosen per face (`Brace::Rising`, `Falling`, `WarrenRising`, `WarrenFalling`, `Cross`); the biplane uses it to put only the flying wires in the strut planes (landing wires are slack under positive g and a linear truss would load them in compression).

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
| Stadium | `stadium_grandstand_cantilever` | 20 m cantilever grandstand roof |
| Stadium | `stadium_space_frame` | 24 × 24 m double-layer grid |
| Stadium | `stadium_schwedler_dome` | Schwedler dome, 36.8 m |
| Stadium | `stadium_geodesic_dome` | 3V geodesic dome, r = 10 m (aluminum) |
| Tower & Platform | `tower_transmission` | 30 m transmission tower with cross-arms |
| Tower & Platform | `platform_offshore_jacket` | 40 m four-leg offshore jacket |
| Tower & Platform | `tower_crane_jib` | 24 m triangular crane jib |
| Aircraft | `aircraft_tube_fuselage` | 5.9 m welded 4130 tube fuselage (Warren), 3.8 g |
| Aircraft | `aircraft_engine_mount` | Four-point welded engine mount, engine CG on stiff links |
| Aircraft | `aircraft_strut_braced_wing` | 5 m strut-braced half wing, truss spars, Schrenk lift (aluminum) |
| Aircraft | `aircraft_biplane_wing_cell` | Two-bay biplane wing cell: struts, flying wires, drag wires |

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

1. Reduced DOFs are numbered node by node. `nodeDofSlots[3 n + k]` is the reduced DOF of direction k of node n (`-1` when unused); Block-CG uses it for its node blocks.
2. Every global DOF gets its links `(reduced DOF, b_k[axis])`, one for an axis-aligned node and up to three for an inclined one.
3. Each stored upper triplet `K(i, j)` and its mirror `K(j, i)` are expanded over the links of i and j; only upper-triangle entries of `Tᵀ K T` are kept. This is done in parallel: per-thread count, prefix sum, parallel scatter.
4. A reduced `SparseMatrix` (upper triangle only) and `Tᵀ f` are built.
5. After the solve, `u_n = Σ_k b_k q_k` gives every node's `m_displacement`; fully fixed nodes get 0.

`anaf_truss_io_tests` (`inclinedSupportsMatchTheRotatedModel`) turns a triangle truss about the gravity axis so that its roller and in-plane supports become inclined, sends it through an MSH file, and checks that the displacements turn with the model and the stresses stay the same.

Supports are homogeneous (zero prescribed displacement). Reaction forces are not computed yet.

## 7. Solver portfolio (`FEM::TRUSS::SOLVER`)

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
| `solveCholmod` | `solver_cholmod.cpp` | Eigen `CholmodSupernodalLLT<..., Upper>` | Only when `ANAFINEN_HAS_CHOLMOD`, otherwise a stub that returns "not available" |
| `solveSimplicialLDLT` | `solver_simplicial.cpp` | Eigen `SimplicialLDLT<..., Upper>` | Always available |
| `solveBlockCG` | `solver_iterative.cpp` | Preconditioned CG, 3x3 per-node block-Jacobi preconditioner | Tolerance 1e-8, max 50,000 iterations, logs every 200, honors `stop_token` |

Every result is a `Result` record: `type` (`SOLVER::Type`), `available`, `converged`, `iterations`, `relativeResidual = ‖f − K u‖ / ‖f‖`, `elapsedSeconds`, `message`. The referee also logs a hardware summary: CPU model, thread counts, and free/total RAM on Linux.

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

Sign convention: **tension > 0, compression < 0** for both stress and axial force. `σ_g` is an engineering approximation that raises the magnitude in the direction of the FE result, so `|stress|` is the peak-magnitude envelope. Consumers that need a magnitude use `std::abs`: yield check, viewport color, log maximum.

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

## 11. Known issues

- Reaction forces are not computed.

## 12. Planned (not in code yet)

- Consistent/lumped mass matrix and the generalized eigenproblem `K φ = ω² M φ` with Spectra `SymGEigsShiftSolver` (shift-invert).
- 2D/3D beam/frame elements (Euler-Bernoulli, Timoshenko) and 2D CST.
- Imported BCs beyond `fixed`, `allowedMotion` and `force` (prescribed displacements, amplitudes, thermal loads) are read by `anaf_io` but not used by either truss solver.

## 13. Related source files

- Orchestration: [src/objectCalcs/truss_1D/trussEngine/trussSolver.hpp](../src/objectCalcs/truss_1D/trussEngine/trussSolver.hpp), [trussSolver_SQPT.cpp](../src/objectCalcs/truss_1D/trussEngine/trussSolver_SQPT.cpp), [trussSolver_Imported.cpp](../src/objectCalcs/truss_1D/trussEngine/trussSolver_Imported.cpp), [trussSolver_static.cpp](../src/objectCalcs/truss_1D/trussEngine/trussSolver_static.cpp) (shared static solve)
- Container: [deformationUnderConstForce.hpp](../src/objectCalcs/truss_1D/trussEngine/trussSolver/deformationUnderConstForce.hpp), [deformationUnderConstForce.cpp](../src/objectCalcs/truss_1D/trussEngine/trussSolver/deformationUnderConstForce.cpp)
- Solvers: [solverPortfolio.hpp](../src/objectCalcs/truss_1D/trussEngine/trussSolver/solverPortfolio.hpp), [solver_referee.cpp](../src/objectCalcs/truss_1D/trussEngine/trussSolver/solver_referee.cpp), [solver_cholmod.cpp](../src/objectCalcs/truss_1D/trussEngine/trussSolver/solver_cholmod.cpp), [solver_simplicial.cpp](../src/objectCalcs/truss_1D/trussEngine/trussSolver/solver_simplicial.cpp), [solver_iterative.cpp](../src/objectCalcs/truss_1D/trussEngine/trussSolver/solver_iterative.cpp)
- Types: [node.hpp](../src/objectCalcs/truss_1D/trussProperties/node.hpp), [element.hpp](../src/objectCalcs/truss_1D/trussProperties/element.hpp), [appliedForce.hpp](../src/objectCalcs/truss_1D/trussProperties/appliedForce.hpp), [properties.hpp](../src/material/properties.hpp)
- Generator: [simpleQuadranglePrismTrussCreate.cpp](../src/objectCalcs/truss_1D/trussTypes/simpleQuadranglePrismTrussCreate.cpp)
- Built-in library: [trussLibrary.hpp](../src/objectCalcs/truss_1D/trussTypes/trussLibrary.hpp), [trussLibrary.cpp](../src/objectCalcs/truss_1D/trussTypes/trussLibrary.cpp), [tests/trussLibraryTool.cpp](../tests/trussLibraryTool.cpp), [assets/objects/truss/truss1D/](../assets/objects/truss/truss1D/)
