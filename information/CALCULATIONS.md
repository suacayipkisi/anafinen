# Calculations: Truss FEM Engine

This document describes the finite element calculation for 3D truss structures built from 1D two-node bar elements. It covers the data types, the math, the solver portfolio, and the energy validator.

> **Document status**
> Verified against: `v0.1.2-alpha` + working tree, 2026-09-27.
> Implemented: static displacement under nodal loads + self-weight.
> Not implemented yet: mass matrix, modal analysis (Spectra), beam/frame elements, CST.

## 1. Overall flow

```text
Truss_SQPT (trussSolver_SQPT.cpp)                       progress
   |
   +-- trussSetAndSetFix_SQPT(fixedDOFs copy)             0.20
   |     SimpleTruss::setTruss()  -> nodes + elements
   |     fixity copy              -> Node::setMovable()
   |
   +-- trussSetForce_SQRT()                               0.25
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
         |     remove fixed DOFs -> reduced K, f
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
| `TrussElement_1D` | `trussProperties/element.hpp` | Material index (`m_type`), area, two node IDs; computed length and direction cosines; results: elongation, axial force, stress |
| `ForceApplied` | `trussProperties/appliedForce.hpp` | Node ID + force vector [N] |
| `Material` | `material/properties.hpp` | E, G, K, yield/ultimate strength, density, Poisson, ductility, ID, built-in flag |
| `SimpleTruss` | `trussTypes/simpleQuadranglePrismTrussCreate.hpp` | Owns node and element vectors of the generated prism truss |
| `Truss_1D_Container` | `trussEngine/trussSolver/deformationUnderConstForce.hpp` | Non-owning spans over force vector, nodes, elements; triplets; results; energy values |
| `Truss_SQPT` | `trussEngine/trussSolver.hpp` | Orchestrates one solve for the simple quadrangle prism truss |

Units are SI throughout: m, m², N, Pa, kg/m³. The GUI enters the cross-section in cm², and `Truss_SQPT` multiplies by `1e-4`. `Material` has both `m_elasticityModulus` and `m_youngModulus`; the solver uses `m_elasticityModulus`.

The element constructor rejects invalid input by throwing `std::invalid_argument` / `std::out_of_range`:
- area ≤ 0
- identical nodes
- out-of-range node index
- zero length

### 2.1 Node constraints

- `setMovable({x, y, z})`: `true` means the DOF is free. It rebuilds the allowed-motion basis from the free axes.
- `setAllowedMotionDirections()` accepts arbitrary directions, orthonormalizes them with Gram-Schmidt, and derives `m_isMovable` from them. An axis is movable only if it lies in the span of the basis.
- The solver currently uses only `m_isMovable`, i.e. axis-aligned fixity. The allowed-motion basis (inclined supports) is stored and round-tripped through VTK, but it is not yet applied in the stiffness system.

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

The modulus used is `Material::getElasticityModulues()` (E).

## 5. Load vector

- `m_forceVec` has `3 * nodeCount` entries, indexed `3 * nodeId + axis`.
- User loads are written first.
- `considerWeight()` adds the self-weight `W = ρ A L g` with `g = -9.80665` along +Y. Half goes to each end node's Y DOF. This is lumped gravity, with `#pragma omp atomic` for the shared nodes.

## 6. Boundary conditions and the reduced system

1. `isFixed[dof] = !movable[axis]`.
2. `remapTable[dof]` maps each free DOF to its compact index, and each fixed DOF to `-1`.
3. Triplets touching a fixed row or column are dropped. The kept triplets are compacted in parallel: per-thread count, prefix sum, parallel scatter.
4. A reduced `SparseMatrix` (upper triangle only) and a reduced force vector are built.
5. After the solve, the displacements are scattered back to full size. Fixed DOFs get 0, and every node's `m_displacement` is set.

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

Every result is a `Result` record: `kind`, `available`, `converged`, `iterations`, `relativeResidual = ‖f − K u‖ / ‖f‖`, `elapsedSeconds`, `message`. The referee also logs a hardware summary: CPU model, thread counts, and free/total RAM on Linux.

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
- Allowed-motion directions (inclined supports) are stored but not applied in the solve.

## 12. Planned (not in code yet)

- Consistent/lumped mass matrix and the generalized eigenproblem `K φ = ω² M φ` with Spectra `SymGEigsShiftSolver` (shift-invert).
- 2D/3D beam/frame elements (Euler-Bernoulli, Timoshenko) and 2D CST.
- Imported/self-built trusses through `anaf::FILE::MeshImportData` (`Truss_Imported_or_Entered`).

## 13. Related source files

- Orchestration: [src/objectCalcs/truss_1D/trussEngine/trussSolver.hpp](../src/objectCalcs/truss_1D/trussEngine/trussSolver.hpp), [trussSolver_SQPT.cpp](../src/objectCalcs/truss_1D/trussEngine/trussSolver_SQPT.cpp)
- Container: [deformationUnderConstForce.hpp](../src/objectCalcs/truss_1D/trussEngine/trussSolver/deformationUnderConstForce.hpp), [deformationUnderConstForce.cpp](../src/objectCalcs/truss_1D/trussEngine/trussSolver/deformationUnderConstForce.cpp)
- Solvers: [solverPortfolio.hpp](../src/objectCalcs/truss_1D/trussEngine/trussSolver/solverPortfolio.hpp), [solver_referee.cpp](../src/objectCalcs/truss_1D/trussEngine/trussSolver/solver_referee.cpp), [solver_cholmod.cpp](../src/objectCalcs/truss_1D/trussEngine/trussSolver/solver_cholmod.cpp), [solver_simplicial.cpp](../src/objectCalcs/truss_1D/trussEngine/trussSolver/solver_simplicial.cpp), [solver_iterative.cpp](../src/objectCalcs/truss_1D/trussEngine/trussSolver/solver_iterative.cpp)
- Types: [node.hpp](../src/objectCalcs/truss_1D/trussProperties/node.hpp), [element.hpp](../src/objectCalcs/truss_1D/trussProperties/element.hpp), [appliedForce.hpp](../src/objectCalcs/truss_1D/trussProperties/appliedForce.hpp), [properties.hpp](../src/material/properties.hpp)
- Generator: [simpleQuadranglePrismTrussCreate.cpp](../src/objectCalcs/truss_1D/trussTypes/simpleQuadranglePrismTrussCreate.cpp)
