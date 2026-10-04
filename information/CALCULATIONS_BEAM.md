# Calculations: Beam / Frame FEM Engine

This document describes the linear static calculation of 3D frames built from two-node beam elements (Euler-Bernoulli and Timoshenko): data types, local axes, element matrices, loads, supports, results along the element and the tests.

> **Document status**
> Verified against: `v0.2.0-alpha` (in development; last release `v0.1.3-alpha`, 2026-10-01), content checked 2026-10-04 (first version: `FEM::BEAM::solveStatic()`, diagrams along the element, `anaf_beam_tests`).
> Implemented in `anaf_core`: static solve under nodal forces / moments, uniform distributed loads and self weight; supports as allowed motion / rotation bases; section forces; displacement and internal forces at any point of an element.
> Not implemented yet: `anaf_io` adapter (`MeshModel` ↔ `FEM::BEAM::MeshData`), GUI, stresses, end releases (hinges), mass matrix, reactions.

## 1. Overall flow

`FEM::BEAM::solveStatic(mesh, materials, stop_token, progress)` (`beam/beamEngine/beamSolver.hpp`) takes a `FEM::BEAM::MeshData` and returns a `StaticResult` (solved copy + energy check) or the reason it cannot solve. It has no GUI types, like the truss entry point ([CALCULATIONS.md](CALCULATIONS.md) section 1).

```text
solveStatic(mesh, materials, st, progress)  (beamSolver.cpp)                 progress
   |
   +-- buildSolverModel()   copy of the model; ids, sections, materials,        0.20
   |                        load indices checked; unused nodes fixed
   +-- Beam_3D_Container                                (deformationUnderConstForce.cpp)
   |     +-- buildElements()     local axes R, k (12x12), T^T k T per element    0.40
   |     +-- applyLoads()        nodal F / M; elementLocalLoads() -> wL/2,       0.50
   |     |                       wL^2/12 equivalent loads -> global f
   |     +-- calculateDisplacements()   u = T q per node (6 slots), B^T K_e B
   |     |                              straight into reduced triplets,
   |     |                              SOLVER::solveSelected(dofsPerNode = 6)   0.85
   |     +-- calculateSectionForces()   p = k (T u_e) - f0, section convention   0.90
   |     +-- runValidator()             U = W / 2                                0.95
   +-- logResult()   energy, max |u|, max |rotation|, max |N|, max |M|
   +-- StaticResult                                                              1.00

after the solve (beamDiagrams.cpp):
   sectionAt() / sampleElement() / sampleAllElements()
        exact displacement and {N, Vy, Vz, T, My, Mz} at any x of an element
```

## 2. Data types

| Type | File | Holds |
|---|---|---|
| `Node` | `beamProperties/node.hpp` | id (= position), location, displacement [m], rotation [rad, rotation vector in global axes], `allowedMotionDirections` and `allowedRotationAxes` (orthonormal bases, 0..3 vectors each) |
| `Section` | `beamProperties/element.hpp` | A, Iy, Iz, J (St. Venant), Asy, Asz (= κA); local principal axes |
| `Formulation` | `beamProperties/element.hpp` | `EulerBernoulli`, `Timoshenko` |
| `BeamElement` | `beamProperties/element.hpp` | node1, node2, material index, `Section`, `Formulation`, orientation vector v, result `sectionForces[12]` |
| `NodalLoad` | `beamProperties/loads.hpp` | node, force [N], moment [N m], global axes |
| `DistributedLoad` | `beamProperties/loads.hpp` | element, uniform value [N/m], `LoadFrame::Global` or `Local` |
| `MeshData` | `beamProperties/meshData.hpp` | nodes, elements, nodal loads, distributed loads, `gravity` (default {0, −9.80665, 0}; zero = no self weight), `hasResults` |
| `Beam_3D_Container` | `beamEngine/beamSolver/deformationUnderConstForce.hpp` | Spans over nodes and elements, per-element frame (length, R, k, Tᵀ k T, fixed-end loads), global load vector (6 per node), energies |
| `SectionState` | `beamEngine/beamDiagrams.hpp` | position x, undeformed location, global and local displacement, {N, Vy, Vz, T, My, Mz} |

Where the data is stored:

| Data | Owner | Lifetime |
|---|---|---|
| Input model | caller's `MeshData` | never changed by the solve |
| Solved model | `StaticResult::mesh` (`shared_ptr<MeshData>`) | copy of the input with displacements, rotations, section forces, `hasResults = true` |
| Element matrices, load vector | `Beam_3D_Container` | one solve |
| Diagram samples | return value of `sampleAllElements()` | caller |

### 2.1 Choosing the formulation

The formulation is per element, as in `anaf_io` (`ElementFormulation`). `setFormulationForAll(mesh, formulation)` sets one formulation on every element; single elements are changed afterwards (for example all Timoshenko and one element Euler-Bernoulli). Euler-Bernoulli ignores the shear areas; Timoshenko requires Asy, Asz > 0.

## 3. Local axes

Nastran CBEAM / ANSYS convention, identical to the `anaf_io` `BeamOrientation` rule ([FILE_HANDLING.md](FILE_HANDLING.md) section 3.3):

```text
x = normalize(node2 - node1)
z = normalize(x × v)          v: orientation vector, global axes, lies in the local x-y plane
y = z × x
R = [x; y; z]  (rows)         u_local = R u_global
```

1. A zero v selects the default: v = global +Y (the project's up axis, gravity is −Y), so a horizontal member has local y pointing up and carries gravity through Iz.
2. A member parallel to Y (`|x × Y| < 1e-6`) uses v = global +X instead: local y = +X, local z = −Z.
3. A given v parallel to the axis (`|x × v| ≤ 1e-9 |v|`) is an error.
4. Iz is the bending stiffness about local z (deflection v along y, plane x-y); Iy about local y (deflection w along z, plane x-z). Asy carries Vy, Asz carries Vz.

The default switches by 90° when a member crosses the vertical tolerance; give v explicitly for members close to vertical.

## 4. Element stiffness

DOF order per node {ux, uy, uz, rx, ry, rz}, element vector {node1, node2}. Przemieniecki (Theory of Matrix Structural Analysis, ch. 5), the same as Logan 5th ed. ch. 5 for φ = 0:

```text
axial     EA/L  [ 1 -1; -1 1 ]                         on {u1, u2}
torsion   GJ/L  [ 1 -1; -1 1 ]                         on {rx1, rx2}
plane x-y  E Iz / ((1 + φy) L³) ·                      on {v1, rz1, v2, rz2}, rz = +dv/dx
   [ 12     6L          -12    6L         ]
   [ 6L     (4+φy)L²    -6L    (2-φy)L²   ]
   [ -12    -6L          12   -6L         ]
   [ 6L     (2-φy)L²    -6L    (4+φy)L²   ]
plane x-z  same with Iy, φz and the 6L terms negated   on {w1, ry1, w2, ry2}, ry = -dw/dx

φy = 12 E Iz / (G Asy L²),  φz = 12 E Iy / (G Asz L²)   (Timoshenko);  φ = 0 (Euler-Bernoulli)
```

| Choice | Reason |
|---|---|
| Interdependent interpolation element (exact Timoshenko stiffness) | No shear locking, exact at the nodes, reduces to Euler-Bernoulli at φ = 0 (tested). A linear Timoshenko element with reduced integration is cruder and needs many elements. |
| Global matrix Tᵀ k T with T = blockdiag(R, R, R, R) | One rotation per element; k stays the textbook matrix. |

## 5. Loads

| Load | Enters as |
|---|---|
| `NodalLoad` | force and moment added to the global load vector (6 per node) |
| `DistributedLoad` | `elementLocalLoads()`: local value directly, global value turned by R; summed per element |
| Self weight | `density · A · gravity` per length, a global uniform load, also in `elementLocalLoads()` |

The total local load q of an element becomes work-equivalent nodal loads (`equivalentNodalLoads()`):

```text
node 1: Fx = qx L/2, Fy = qy L/2, Fz = qz L/2, My = -qz L²/12, Mz = +qy L²/12
node 2: Fx = qx L/2, Fy = qy L/2, Fz = qz L/2, My = +qz L²/12, Mz = -qy L²/12
```

They are the same for both formulations: the Timoshenko shape functions integrate to L/2 and L²/12 for any φ. With these consistent loads the nodal displacements are exact for uniform loads (a self-weight cantilever needs one element). The same vector f0 is subtracted from the element end forces (section 6).

## 6. Supports and the reduced system

1. Every node has two orthonormal bases in global axes: allowed motion (0..3 vectors) and allowed rotation (0..3 vectors). `setMovable` / `setRotatable` build them from global axes; `setAllowedMotionDirections` / `setAllowedRotationAxes` take any vectors (Gram-Schmidt, `FEM::SUPPORT::orthonormalize`); `fixAll()` empties both.
2. Node basis B_n (6 x 6): column k < 3 = motion direction k, column 3 + k = rotation axis k, unused slots zero. u_n = B_n q_n.
3. Slot table: `nodeDofSlots[6 n + s]` = reduced DOF of slot s of node n, −1 when unused. It is passed to `FEM::SOLVER::solveSelected()` with `dofsPerNode = 6` (Block-CG node blocks, [CALCULATIONS.md](CALCULATIONS.md) section 7).
4. Each element adds Bᵀ (Tᵀ k T) B, B = blockdiag(B_n1, B_n2), straight into reduced upper-triangle triplets. With n used slots on its two nodes an element writes exactly n (n + 1) / 2 entries, so the output slots are known in advance (prefix sum) and the loop is parallel without a global triplet list.
5. Reduced loads f_r = B_nᵀ f_n; after the solve u_n = B_n q_n gives displacement and rotation.
6. Nodes used by no element are fixed (warning). Supports are homogeneous.

An inclined rotation support (a rotation basis that is not global axes) is solved, but `anaf_io` stores rotational fixity only per global axis ([FILE_HANDLING.md](FILE_HANDLING.md) section 3.3, node-local frames are future work); the adapter will have to warn when it cannot write one.

## 7. Element results

Section forces, local axes, `BeamElement::sectionForces`:

```text
p  = k (T u_e) - f0                    end forces the nodes apply to the element
s1 = -p[0..5]   {N, Vy, Vz, T, My, Mz} at node 1
s2 = +p[6..11]  {N, Vy, Vz, T, My, Mz} at node 2
```

N > 0 is tension at both ends (the `BeamSectionForce` convention of `anaf_io`). For a cantilever along +x clamped at node 1 with a tip load P_y: Vy = P_y along the element, Mz = P_y L at the clamp and 0 at the tip.

Stresses are not computed: σ = N/A ± M c / I needs the extreme fiber distances (or section moduli), which the section does not carry yet.

### 7.1 Along the element (`beamDiagrams.hpp`)

`sectionAt(solved, element, ξ, q, materials)` returns the exact state at x = ξ L for nodal and uniform loads (not an interpolation):

| Quantity | Formula |
|---|---|
| u (axial) | (1 − ξ) u1 + ξ u2 + q_x x (L − x) / (2 E A) |
| v (plane x-y) | N(ξ, φy) · {v1, rz1, v2, rz2} + q_y [x² (L − x)² / (24 E Iz) + x (L − x) / (2 G Asy)] |
| w (plane x-z) | N(ξ, φz) · {w1, −ry1, w2, −ry2} + q_z [x² (L − x)² / (24 E Iy) + x (L − x) / (2 G Asz)] |
| N, Vy, Vz | N0 − q_x x, Vy0 − q_y x, Vz0 − q_z x |
| T | T0 |
| My, Mz | My0 + Vz0 x − q_z x² / 2, Mz0 − Vy0 x + q_y x² / 2 |

N(ξ, φ) are the interdependent interpolation shape functions (Hermite cubics at φ = 0); the shear terms (2 G As) apply to Timoshenko only. The particular parts are the clamped-clamped solutions, zero at both ends. Hence dMz/dx = −Vy, dMy/dx = Vz, and x = L gives the node 2 section forces (tested).

q is the element's total local load from `elementLocalLoads()`. `sampleElement()` returns `count` (≥ 2) evenly spaced states; `sampleAllElements()` does it for every element and computes q once. Displacements are returned in global axes (for drawing the deformed shape) and in local axes.

## 8. Validator

The same energy balance as the truss ([CALCULATIONS.md](CALCULATIONS.md) section 9): U = Σ ½ u_eᵀ (Tᵀ k T) u_e, W = Σ f · u over all 6 DOFs per node (equivalent loads included), valid when |U − W/2| ≤ 1e-12 or the relative difference ≤ 1e-7.

## 9. Tests

`anaf_beam_tests` (`tests/beamTests.cpp`) links `anaf_core` only. The section has Iy ≠ Iz and Asy ≠ Asz, so a swapped axis gives a wrong number. Every closed-form test runs with both formulations.

| Test | Checks |
|---|---|
| `localAxesFollowTheOrientationRule` | Default v for horizontal and vertical members, explicit v not normal to the axis, orthonormal right-handed R, errors (v parallel, zero length) |
| `stiffnessIsSymmetricAndTimoshenkoTendsToEulerBernoulli` | Symmetry, exactly 6 rigid body modes, φ → 0 limit, EB ignores the shear areas |
| `cantileverTipLoadsMatchTheHandSolution` | Tip force (3 components) and torque: PL/EA, PL³/3EI (+ PL/GAs), PL²/2EI, TL/GJ; section forces at both ends |
| `clampedBeamUnderUniformLoad` | Two elements, global and local load: qL⁴/384EI (+ qL²/8GAs), qL²/12 and qL²/24, continuity at the middle node |
| `cantileverUnderUniformLoadInBothPlanes` | Local q_y and q_z on one element: qL⁴/8EI (+ qL²/2GAs), qL³/6EI, clamp moments |
| `cantileverUnderItsOwnWeight` | wL⁴/8EI and wL²/2 from `density · A · g` |
| `orientationSelectsTheBendingInertia` | v = +Z makes a Y load use Iy; a column uses Iz for an X load |
| `rotatedFrameGivesRotatedResults` | 3D frame with every feature (mixed formulations, nodal F and M, global and local distributed loads, self weight, inclined roller and rotation support) turned by R: displacements and rotations turn, section forces equal; the roller moves only in its plane |
| `formulationForAllThenSingleElements` | `setFormulationForAll` + one element switched back; shear deflection of the Timoshenko half only |
| `diagramsAlongACantileverMatchTheHandSolution` | u, v, w, N, V, T, M along the element against the closed form, both formulations |
| `diagramsOfAClampedBeamUnderUniformLoad` | One element under (q_x, q_y, q_z): mid-span values without a node there, end values = section forces |
| `diagramsOfACantileverUnderItsOwnWeight` | v(x) = −w x² (6L² − 4Lx + x²) / 24EI, Mz(x), Vy(x) |
| `diagramsTurnWithTheFrame` | Along every element of the turned frame: forces equal, displacements turned, ends equal the nodes, dM/dx = ∓V |
| `diagramArgumentsAreChecked` | No results, bad element, ξ outside [0, 1] or NaN, fewer than 2 samples |
| `invalidModelsAreReported` | Error texts: no nodes / elements, J, Iy / Iz, area, missing shear areas, material, node references, parallel v, missing load targets, node ids, mechanisms |
| `cancelledSolveAndProgress` | Stop request, non-decreasing progress, input unchanged |

The tests were checked against injected faults: φ built from the wrong inertia, the sign of the x-z fixed-end moment (caught only after `cantileverUnderUniformLoadInBothPlanes` was added), the section sign, and in the diagrams the w rotation sign, the Mz load term, the Timoshenko particular part and a wrong shape function; each makes tests fail.

## 10. Known issues

- An unloaded mechanism is not detected: when the loads do not excite a mechanism (axial load on a beam pinned at both ends that may spin about its own axis) the singular system still gets a finite answer, because the referee has no singularity check; it is shared with the truss solver ([ARCHITECTURE.md](ARCHITECTURE.md) section 8, item 6). `invalidModelsAreReported` loads its torsion mechanism with a torque on purpose.
- Reactions and stresses are not computed.

## 11. Related source files

- Entry point: [beamSolver.hpp](../src/objectCalcs/beam/beamEngine/beamSolver.hpp), [beamSolver.cpp](../src/objectCalcs/beam/beamEngine/beamSolver.cpp)
- Container and element math: [deformationUnderConstForce.hpp](../src/objectCalcs/beam/beamEngine/beamSolver/deformationUnderConstForce.hpp), [deformationUnderConstForce.cpp](../src/objectCalcs/beam/beamEngine/beamSolver/deformationUnderConstForce.cpp)
- Results along the element: [beamDiagrams.hpp](../src/objectCalcs/beam/beamEngine/beamDiagrams.hpp), [beamDiagrams.cpp](../src/objectCalcs/beam/beamEngine/beamDiagrams.cpp)
- Types: [node.hpp](../src/objectCalcs/beam/beamProperties/node.hpp), [node.cpp](../src/objectCalcs/beam/beamProperties/node.cpp), [element.hpp](../src/objectCalcs/beam/beamProperties/element.hpp), [loads.hpp](../src/objectCalcs/beam/beamProperties/loads.hpp), [meshData.hpp](../src/objectCalcs/beam/beamProperties/meshData.hpp)
- Support bases (shared with the truss): [supportBasis.hpp](../src/objectCalcs/common/supportBasis.hpp), [supportBasis.cpp](../src/objectCalcs/common/supportBasis.cpp)
- Solvers: [solverPortfolio.hpp](../src/solvers/solverPortfolio.hpp)
- Tests: [tests/beamTests.cpp](../tests/beamTests.cpp)
- File side of the beam data: [FILE_HANDLING.md](FILE_HANDLING.md) section 3.3
