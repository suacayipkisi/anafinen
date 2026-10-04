# Calculations: Beam / Frame FEM Engine

This document describes the linear static calculation of 3D frames built from two-node beam elements (Euler-Bernoulli and Timoshenko): data types, local axes, element matrices, loads, supports, results along the element and the tests.

> **Document status**
> Verified against: `v0.2.0-alpha` (released 2026-10-05; previous release `v0.1.3-alpha`, 2026-10-01), content checked 2026-10-05 (v0.2.0-alpha release check; 2026-10-04: catalogue data provenance, section 2.2; end releases / hinges: section 6.1, `BeamElement::endReleases`, library category Hinges & Pins with 6 models, Large Structures with 3 (stadium, airport terminal, airliner airframe, about 3000 elements each), 49 in all; built-in beam library `FEM::BEAM::LIBRARY`, section 12, catalogue grown to 118 profiles; section face triangulation for rendering, section 2.2; file adapter: section 11; GUI: [GUI.md](GUI.md) sections 2.5-2.7; stresses: section 7.2, `BeamElement::stress`; cross-section library: shapes, catalogue, `sectionID`, section 2.2; first version the same day: `FEM::BEAM::solveStatic()`, diagrams along the element, `anaf_beam_tests`).
> Implemented in `anaf_core`: static solve under nodal forces / moments, uniform distributed loads and self weight; supports as allowed motion / rotation bases; end releases (hinges) by static condensation; section forces; displacement and internal forces at any point of an element; cross-section library (general, rectangle, circle, pipe, box, I) with a catalogue of 118 standard profiles; normal, shear and von Mises stresses with a yield check.
> Not implemented yet: point-wise stresses inside the section, channels / angles / tees, partial (spring) end fixity, reactions, dynamic analysis (no mass matrix; modal / harmonic / transient are not available in v0.2.0-alpha).

## 1. Overall flow

`FEM::BEAM::solveStatic(mesh, materials, sections, stop_token, progress)` (`beam/beamEngine/beamSolver.hpp`) takes a `FEM::BEAM::MeshData` and returns a `StaticResult` (solved copy + energy check) or the reason it cannot solve. It has no GUI types, like the truss entry point ([CALCULATIONS.md](CALCULATIONS.md) section 1).

```text
solveStatic(mesh, materials, sections, st, progress)  (beamSolver.cpp)       progress
   |
   +-- buildSolverModel()   copy of the model; ids, materials, sections and     0.20
   |                        load indices checked; computeProperties(shape, v)
   |                        per element; unused nodes fixed
   +-- Beam_3D_Container                                (deformationUnderConstForce.cpp)
   |     +-- buildElements()     local axes R, k (12x12), releases condensed     0.40
   |     |                       (k*), T^T k* T per element
   |     +-- applyLoads()        nodal F / M; elementLocalLoads() -> wL/2,
   |     |                       wL^2/12 equivalent loads (condensed f0*) -> f
   |     +-- buildNodeDofs()     node bases; directions free at hinges held      0.50
   |     |                       (or a mechanism error when loaded)
   |     +-- calculateDisplacements()   u = T q per node (6 slots), B^T K_e B
   |     |                              straight into reduced triplets,
   |     |                              SOLVER::solveSelected(dofsPerNode = 6)   0.85
   |     +-- calculateSectionForces()   p = k* (T u_e) - f0*, section convention
   +-- calculateStresses()   elementStress() per element: extremes along it,
   |                         yield check                                         0.90
   |     +-- runValidator()             U = W / 2                                0.95
   +-- logResult()   energy, max |u|, max |rotation|, max |N|, max |M|
   +-- StaticResult                                                              1.00

after the solve (beamDiagrams.cpp):
   sectionAt() / sampleElement() / sampleAllElements()
        exact displacement and {N, Vy, Vz, T, My, Mz} at any x of an element
   elementEndDisplacements()   end values incl. the released DOFs (u_r recovered)
```

## 2. Data types

| Type | File | Holds |
|---|---|---|
| `Node` | `beamProperties/node.hpp` | id (= position), location, displacement [m], rotation [rad, rotation vector in global axes], `allowedMotionDirections` and `allowedRotationAxes` (orthonormal bases, 0..3 vectors each) |
| `SectionProperties` | `beamProperties/element.hpp` | A, Iy, Iz, J (St. Venant), Asy, Asz (= κA); local principal axes; what the solver uses, computed per element |
| `SectionShape` | `beamSection/beamSection.hpp` | `std::variant` of `GeneralSection`, `RectangleSection`, `CircleSection`, `PipeSection`, `BoxSection`, `ISection` (dimensions in m) |
| `BeamSection` | `beamSection/beamSection.hpp` | name, `SectionShape`, built-in flag, stable ID: the library object elements reference |
| `Formulation` | `beamProperties/element.hpp` | `EulerBernoulli`, `Timoshenko` |
| `BeamElement` | `beamProperties/element.hpp` | node1, node2, material index, section index, `Formulation`, orientation vector v, `endReleases` (`RELEASE` bits, section 6.1), result `sectionForces[12]` |
| `NodalLoad` | `beamProperties/loads.hpp` | node, force [N], moment [N m], global axes |
| `DistributedLoad` | `beamProperties/loads.hpp` | element, uniform value [N/m], `LoadFrame::Global` or `Local` |
| `MeshData` | `beamProperties/meshData.hpp` | nodes, elements, nodal loads, distributed loads, `gravity` (default {0, −9.80665, 0}; zero = no self weight), `hasResults` |
| `Beam_3D_Container` | `beamEngine/beamSolver/deformationUnderConstForce.hpp` | Spans over nodes and elements, per-element frame (length, R, condensed k*, Tᵀ k* T, condensed fixed-end loads), node DOF bases (`NodeDofs`), global load vector (6 per node), energies |
| `SectionState` | `beamEngine/beamDiagrams.hpp` | position x, undeformed location, global and local displacement, {N, Vy, Vz, T, My, Mz} |
| `SectionStress` | `beamSection/sectionStress.hpp` | one cross-section: max / min σx and their points {y, z}, τ from shear force, τ from torsion, von Mises |
| `BeamStress` | `beamProperties/element.hpp` | result `BeamElement::stress`: available (false for a general section), max / min σx, max τ, max von Mises and its position, `isStressExceeded` |

Where the data is stored:

| Data | Owner | Lifetime |
|---|---|---|
| Input model | caller's `MeshData` | never changed by the solve |
| Section list | caller (`std::span<const BeamSection>`, like the material list) | the catalogue from `assets/bridge/sectionCatalog.json`, user sections later from the user config folder |
| Element section properties | `solveStatic()` local vector | one solve; diagrams compute them again |
| Solved model | `StaticResult::mesh` (`shared_ptr<MeshData>`) | copy of the input with displacements, rotations, section forces, `hasResults = true` |
| Element matrices, load vector | `Beam_3D_Container` | one solve |
| Diagram samples | return value of `sampleAllElements()` | caller |

### 2.1 Choosing the formulation

The formulation is per element, as in `anaf_io` (`ElementFormulation`). `setFormulationForAll(mesh, formulation)` sets one formulation on every element; single elements are changed afterwards (for example all Timoshenko and one element Euler-Bernoulli). Euler-Bernoulli ignores the shear areas; Timoshenko requires Asy, Asz > 0.

### 2.2 Cross-sections (`beamSection/`)

Sections are library objects, like materials: an element stores `sectionID`, an index into the list given to `solveStatic()`. The solver never sees a shape; `computeProperties(shape, ν)` turns it into `SectionProperties` for each element, with the element's material.

```text
assets/bridge/sectionCatalog.json --loadSectionLibrary()--> vector<BeamSection> (built-in, IDs 0..n-1)
user file (later, Section Handler) --loadUserSectionFile()--> appended user sections
BeamElement::sectionID --> BeamSection::getShape() --computeProperties(shape, material ν)--> SectionProperties
                                                   --sectionOutline()--> loops for preview / extrusion
```

Section plane: origin at the centroid, local y along the height (the web of an I-section), local z along the width. With the default orientation (section 3) a horizontal I-beam therefore stands upright and bends about its strong axis z under gravity.

| Shape | Dimensions | A, I | J | Asy, Asz (Cowper 1966, depends on ν) |
|---|---|---|---|---|
| general | A, Iy, Iz, J, Asy, Asz | given | given | given (0 allowed for Euler-Bernoulli) |
| rectangle | height, width | exact | a c³ [1/3 − 0.21 (c/a)(1 − c⁴/12a⁴)] (Roark) | 10(1+ν)/(12+11ν) · A |
| circle | diameter | exact | exact | 6(1+ν)/(7+6ν) · A |
| pipe | outer diameter, wall | exact | exact (2I) | hollow circle, m = d/D |
| box (RHS / SHS) | height, width, wall, outer / inner corner radius | exact with radii (composite parts) | EN 10219-2 / 10210-2: t³p/3 + 2 K A_h, centre line with R_c = (r_o + r_i)/2 | thin-walled box per direction, centre-line dimensions |
| I (IPE, HEA, HEB) | height, flange width, web, flange, root radius | exact with the fillets (composite parts) | ArcelorMittal fillet formula | Asy: thin-walled I (shear along the web); Asz = κ_rect · 2 b t_f |

| Decision | Reason |
|---|---|
| Doubly symmetric shapes only | The element assumes principal axes and the shear center at the centroid. Channels and angles need a principal angle and a shear center offset; a silently wrong U-section is worse than none. |
| Catalogue stores dimensions only | One formula set; tests compare it with published tables instead of copying their numbers. |
| Shear areas from ν at solve time | Cowper's coefficients depend on Poisson's ratio, which belongs to the material. |
| Exact A and I with fillets / radii instead of the rounded catalogue formulas | The composite of rectangles, quarter discs and spandrels is exact; the outline integral checks it. |

Rendering: `triangulateSection(shape, segmentsPerQuarter)` (`beamSection/sectionTriangulation.*`) cuts a section face into triangles for the end caps the viewport draws: the outline loops are merged into one polygon (each hole bridged to the closest visible outer vertex) and ear-clipped ([GUI.md](GUI.md) section 3.9).

Catalogue (`assets/bridge/sectionCatalog.json`, schema 1, lengths in m): IPE 80–600, HEA 100–600, HEB 100–600, 10 CHS, 10 SHS, 6 RHS (hot finished: outer corner radius 1.5 t, inner 1.0 t), plus project-chosen tubes, bars, boxes and spar I sections. Data provenance: nominal dimensions only, taken from freely available manufacturer catalogues (e.g. the ArcelorMittal sales programme); the series conform to EN 10365 / EN 10210-2, which are named for identification only. Nothing is copied from the standard documents (copyrighted, sold by CEN members), and no property tables are copied: A, I, J and shear areas are computed. The tube / bar / box / spar I groups are representative sizes chosen for this project. Formulas taken from a source (Roark, Bredt, Cowper, the ArcelorMittal fillet formula, the EN 10219-2 box torsion formula) are cited methods, not copied data. The file format and the user file follow the material library (`sectionLibrary.hpp`): built-in IDs 0..n−1, names unique ignoring ASCII case, no quotes or control characters, user file written next to the target and renamed over it.

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

### 6.1 End releases (hinges)

`BeamElement::endReleases` frees chosen local DOFs of an element end from its node. Bit k (`FEM::BEAM::RELEASE`, `element.hpp`) is local DOF k in the order {ux, uy, uz, rx, ry, rz}, so it names the section force that is zero at that end: {N, Vy, Vz, T, My, Mz}. Bits 0..5 belong to node 1, bits 6..11 to node 2 (`RELEASE::atNode2()`). `RELEASE::hinge` = My + Mz (bending hinge, torsion carried).

```text
element (local)        k u = p + f0,   p = end forces of the nodes,   p_r = 0 on the released DOFs r
   u_r = k_rr^-1 (f0_r - k_rc u_c)                         element end's own motion
   k*  = k_cc - k_cr k_rr^-1 k_rc   (zero rows / cols at r)   -> T^T k* T into the global system
   f0* = f0_c - k_cr k_rr^-1 f0_r   (zero at r)               -> equivalent loads
node                   diagonal 6x6 block of every node next to a release
   null direction d  -> no element end at the node stiffens d (all released there)
      unloaded       -> d removed from the node basis (held at zero, changes nothing)
      f . d != 0     -> error "node n can rotate / move freely ..."  (a mechanism)
```

1. `condenseReleases(bits, k, f0)` (`deformationUnderConstForce.hpp`) does the condensation in `buildElements()` (k) and `applyLoads()` (f0, from a fresh uncondensed k). It refuses a singular k_rr, checked on the unit-diagonal scaled matrix (smallest eigenvalue ≤ 1e-10 × largest): the released DOFs alone let the element move, e.g. N or T released at both ends, or a pin at both ends plus a shear release. `solveStatic()` reports it as "the end releases make the element a mechanism".
2. `buildNodeDofs()` runs after `applyLoads()`. K is positive semi-definite, so a null vector of a node's diagonal block is coupled to nothing; it is a pure translation or a pure rotation (a released DOF is one in local axes, and intersections keep that), so the 3x3 translation and rotation groups are checked on their own (eigenvalue ≤ 1e-9 × largest of the group, in the node's allowed basis). Such a direction is dropped from the node's DOF slots and counted in the log; the reported node rotation along it is zero. A load component along it larger than 1e-9 × the largest load of its kind is a mechanism (error).
3. Section forces use k* and f0*, so released ends report exactly zero. The energy check uses Tᵀ k* T: the condensed DOFs carry no external work.
4. `elementEndDisplacements()` (`beamDiagrams.hpp`) recomputes k and f0 and recovers u_r with `recoverReleasedDisplacements()`; `sectionAt()` builds the displacement field from these end values, so the deformed shape kinks at a hinge and the end rotation on the hinged side differs from the node's. Nothing extra is stored, so a model read from a file gets the same field.
5. Modelling rule: release one side of a joint. A pin-ended member that ends at a foundation or airframe fitting goes to a clamped node; the release is the pin. When every member at a free node is released about the same axis, the node rotation about it has no meaning (held at zero, a moment on it is a mechanism).

| Choice | Reason |
|---|---|
| Static condensation per element | Exact (k* is the stiffness of the released element), keeps the 6-DOF node layout, no extra unknowns; RFEM, SAP2000 and Nastran (CBAR PA / PB) do the same. A separate hinge node with coupled translations needs constraint equations or a penalty. |
| Bits in local DOF order | The same order as `BeamSectionForce` and Nastran's PA / PB digits (1..6), one integer in the file. |
| Free node directions held, not a tiny spring | No conditioning damage, no invented stiffness; the result is the exact hinged answer. |
| u_r recomputed, not stored | Survives every file format and a model read from disk without a new result field. |

An inclined rotation support (a rotation basis that is not global axes) is solved, but `anaf_io` stores rotational fixity only per global axis ([FILE_HANDLING.md](FILE_HANDLING.md) section 3.3, node-local frames are future work); the adapter will have to warn when it cannot write one.

## 7. Element results

Section forces, local axes, `BeamElement::sectionForces`:

```text
p  = k (T u_e) - f0                    end forces the nodes apply to the element
s1 = -p[0..5]   {N, Vy, Vz, T, My, Mz} at node 1
s2 = +p[6..11]  {N, Vy, Vz, T, My, Mz} at node 2
```

N > 0 is tension at both ends (the `BeamSectionForce` convention of `anaf_io`). For a cantilever along +x clamped at node 1 with a tip load P_y: Vy = P_y along the element, Mz = P_y L at the clamp and 0 at the tip.

Stresses: section 7.2.

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

### 7.2 Stresses (`sectionStress.hpp`, `beamStress.hpp`)

`sectionStress(shape, {N, Vy, Vz, T, My, Mz})` gives the stresses of one cross-section; a general section has no shape and gets none.

| Quantity | Formula | Exactness |
|---|---|---|
| σx(y, z) | N/A − Mz y / Iz + My z / Iy (M = E I dθ/dx, so ε = u′ − y v″ − z w″) | exact (beam theory) |
| max / min σx | N/A ± h(a, b), h = support function of the shape, a = −Mz/Iz, b = My/Iy; point = support point | exact: σ is linear, extremes lie on the convex hull; every shape is centrally symmetric |
| τ from Vy, Vz | Jourawski V Q / (I t) at the neutral axis, Q with fillets / corner radii; I weak axis 1.5 Vz / (2 b t_f); both directions added | Jourawski; added = upper bound |
| τ from T | circle / pipe T r / J; box T / (2 A_h t) (Bredt); rectangle T (3a + 1.8c) / (a² c²) (Roark); I T t_max / J | exact / thin-walled / Roark / thin-walled open |
| von Mises | √(max\|σ\|² + 3 (τ_V + τ_T)²) | upper bound: the largest σ and τ usually act at different points |

Support functions: rectangle and I |a| h/2 + |b| w/2 (the I's convex hull is its bounding rectangle); circle and pipe R √(a² + b²); box |a|(h/2 − r_o) + |b|(w/2 − r_o) + r_o √(a² + b²).

von Mises is the distortion energy criterion σ_v = √(3 J₂), an invariant (no orientation); for σx and τ it is √(σ² + 3τ²). Tresca (twice the largest shear stress on a rotated plane) would be √(σ² + 4τ²), up to 1.155 times larger.

`elementStress(element, q, L, shape, f_y)` searches along the element: both ends, the stationary points of Mz (x = Vy0 / q_y) and My (x = Vz0 / q_z), and 16 evenly spaced points; `solveStatic()` stores the result in `BeamElement::stress`, with `isStressExceeded` = max von Mises > the material's yield strength (no partial safety factors, no code check).

## 8. Validator

The same energy balance as the truss ([CALCULATIONS.md](CALCULATIONS.md) section 9): U = Σ ½ u_eᵀ (Tᵀ k T) u_e, W = Σ f · u over all 6 DOFs per node (equivalent loads included), valid when |U − W/2| ≤ 1e-12 or the relative difference ≤ 1e-7.

## 9. Tests

`anaf_beam_tests` (`tests/beamTests.cpp`) links `anaf_core` only. Most solver tests use a general section with Iy ≠ Iz and Asy ≠ Asz, so a swapped axis gives a wrong number. Every closed-form test runs with both formulations.

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
| `releaseCondensationKeepsTheElementConsistent` | k* symmetric with zero rows / columns at the released DOFs; a recovered u_r makes the released end forces zero and keeps the others; three in-element mechanisms refused, k unchanged |
| `hingeInAClampedBeamGivesTwoCantilevers` | Clamped-clamped, hinge at mid span, load at the hinge: P L³ / 6EI (+ P L / 2 G As), clamp moments P L / 2, zero moment at the hinge, opposite end rotations on the two sides, shear jump |
| `hingeOverASupportGivesSimpleSpans` | Two spans with a hinge over the middle support: q L² / 8 and 5 q L⁴ / 384EI at mid span (diagrams), end slopes ± q L³ / 24EI on either side |
| `pinnedGirderAndThreeHingedFrame` | Girder pinned at both ends between clamped columns (no column moment); three-hinged frame H = q L² / (8 h), column top moment H h, zero at the crown and the bases |
| `freeHingeDirectionsAreHeldOrReported` | Both beams at a node released about z: held and solved without a moment, mechanism error with one, torsion / My still carried; element mechanism and unknown bits reported |
| `endReleasesSurviveTheFileAdapter` | `EndReleases` written only when used, read back bit for bit, an invalid code warns |
| `invalidModelsAreReported` | Error texts: no nodes / elements, J, Iy / Iz, area, missing shear areas, material, node references, parallel v, missing load targets, node ids, mechanisms |
| `cancelledSolveAndProgress` | Stop request, non-decreasing progress, input unchanged |
| `sectionPropertiesMatchClosedForms` | Rectangle (J against Roark's β for a/c = 1 and 3), circle, pipe, sharp box (subtraction, Bredt), sharp I; Cowper limits: thin pipe, square tube 20(1+ν)/(48+39ν), I without flanges = rectangle |
| `sectionPropertiesMatchTheirOutline` | A, Iy, Iz of every shape (fillets and corner radii included) equal the Green's theorem integral over its own outline (2048-gon, 5e-6); centroid at the origin |
| `catalogMatchesPublishedTables` | IPE 200 / 300, HEA 200, HEB 200 / 300: A, I strong / weak, J within 0.1 %; CHS 114.3x5 A and I; SHS 100x100x6.3 A and J (534.00 cm⁴), SHS 100x100x5.6 J (484.00 cm⁴), cross-checked against the free Dlubal online table for hot finished SHS; every entry valid |
| `invalidSectionsAreRefused` | Dimension limits of every shape, names |
| `userSectionFileRoundTrip` | Every shape written and read back; built-ins are not saved; a missing file is an empty list |
| `normalStressFollowsTheStrainField` | E ε with ε = u′ − y v″ − z w″ from the displacement field (central differences) equals σx at the max / min points: the sign convention, independent of the formula |
| `normalStressExtremesMatchTheOutline` | No outline vertex exceeds the support-function extremes and the returned points carry them (every shape, four load cases) |
| `shearAndTorsionStressesMatchClosedForms` | 1.5 V/A, 4V/3A, thin pipe 2V/A, sharp box and I Jourawski; Q with fillets / radii against the clipped outline integral; torsion formulas; von Mises limits |
| `elementStressAlongTheElement` | IPE 300 cantilever P L c / I at the clamp and the yield check; simply supported beam q L²/8 at mid span (found as a stationary point without samples), 1.5 (qL/2)/A shear at the supports; general section without stresses |
| `beamModelSurvivesEveryWritableFormat` | A solved frame (every section shape, mixed formulations, a brace pinned at both ends, inclined translational supports, nodal force and moment, global and local line loads, gravity, results) bit-exact through MSH 4.1 / 2.2, VTU and VTK legacy; sections found again by name and shape; the imported model solves the same |
| `beamModelSurvivesStepWithSidecar` | The same through STEP + `.anafFields` (meshing may renumber): counts, sections, loads, results, solve |
| `beamImportAddsUnknownSections` | A missing section and one with the same name but other dimensions come back as new sections ("My I (imported)"); known ones are reused; the solve with the extended list matches |
| `inclinedRotationSupportIsReported` | An inclined rotation support is written as fixed about all global axes outside its span, with a warning; the translational basis is kept |
| `beamFilesAreTellApartFromTrussFiles` | `isBeamModel()` false for a truss export; a beam without section data is refused; A, Iy, Iz, J alone give a general "Imported section 1" |
| `sectionFacesAreTriangulated` | Every shape (concave I, box and pipe with holes, 1 / 4 / 8 segments per quarter): all triangles counter-clockwise, their areas sum to the outline polygon's area |
| `solverUsesTheSectionShape` | Timoshenko rectangle with Cowper's κ(ν = 0.3); catalogue IPE 300 bending about its strong axis |

The tests were checked against injected faults: in the adapter local loads written as global, swapped box corner radii, lost rotational fixity and lost Timoshenko formulation; in the stresses the Mz sign, a support function without the corner radius, the I fillet in Q (caught after the clipped outline integral was added), a box corner term in Q, the Bredt factor and a missing stationary point; in the sections a wrong J coefficient of the I formula (caught after the table tolerance went from 0.5 % to 0.1 %), the corner disc sign and swapped κ axes of the box, the spandrel's own inertia, and the box corner radius in J (caught only after the Dlubal value was added); in the solver φ built from the wrong inertia, the sign of the x-z fixed-end moment (caught only after `cantileverUnderUniformLoadInBothPlanes` was added), the section sign, and in the diagrams the w rotation sign, the Mz load term, the Timoshenko particular part and a wrong shape function; each makes tests fail.

## 10. Known issues

- A mechanism made of pinned members (for example the inner column lines of a frame whose beams are all pinned, without floor bracing) is not detected either when the loads hardly excite it; the solve returns huge displacements. The built-in models avoid it (plan bracing in `hinge_simple_connection_frame`).
- An unloaded mechanism is not detected: when the loads do not excite a mechanism (axial load on a beam pinned at both ends that may spin about its own axis) the singular system still gets a finite answer, because the referee has no singularity check; it is shared with the truss solver ([ARCHITECTURE.md](ARCHITECTURE.md) section 8, item 6). `invalidModelsAreReported` loads its torsion mechanism with a torque on purpose.
- Reactions are not computed.
- The von Mises value is an upper bound (largest σ and largest τ combined, both shear directions added). Point-wise stresses at stress points of the section (as RFEM reports them) are future work.
- Asy of an I-section comes from Cowper's thin-walled I (IPE 300: 20.3 cm², about the web area h t_w = 21.3 cm²). It is a stiffness value for shear deformation, not the larger plastic shear area A_v of EN 1993-1-1 (25.7 cm²), which is a design resistance quantity.

## 11. File adapter (`beamIO/beamMeshAdapter.*`)

`FEM::BEAM::ADAPTER` converts between `anaf::IO::MeshModel` and `FEM::BEAM::MeshData`, like the truss adapter. The file layout and the reasons behind it are in [FILE_HANDLING.md](FILE_HANDLING.md) section 3.3.

| Model data | In the file |
|---|---|
| Element, formulation, orientation | Line2, `ElementFormulation` 1 / 2, `beamOrientation` |
| End releases | `EndReleases` (the `RELEASE` bits as a number, 0..4095), written only when some element has one |
| Material | `Material:<name>` set (+ `MaterialID`) |
| Section | `Section:<name>` set, `SectionShape` + `SectionDimension1..5`; numbers `CrossSectionArea`, `SecondMomentY/Z`, `TorsionConstant`, `ShearAreaY/Z` (shear areas with the element's ν) |
| Supports | `NodeConstraint`: `fixed` + `allowedMotion` (inclined), `fixedRotation` (global axes only) |
| Loads | `NodalLoad` force + moment; `UniformLoadGlobalX/Y/Z`, `UniformLoadLocalX/Y/Z` (sums per element); global `Gravity` |
| Results | `Displacement`, `Rotation`, `BeamSectionForce`, `AxialForce` (mean of the ends), `VonMisesStress` |

Import (`toMeshData(model, materials, sections)`):

1. `isBeamModel()` decides first: any element with `ElementFormulation` ≥ 1. The GUI routes such files here, every other file to the truss adapter.
2. Only Line2 elements become beams; others are skipped with a warning. A bar (formulation 0) in a beam file becomes an Euler-Bernoulli beam (note).
3. Materials by name, as in the truss adapter (unknown name: material 0 with a warning).
4. Sections: the shape comes from `SectionShape` (or, without it, a general section from the numbers; A, Iy, Iz, J must be positive, otherwise the import fails). A section with a known name and the same shape (relative 1e-9) reuses the list entry; anything else becomes a new section in `ImportedBeam::newSections` (one per distinct name and shape), named after the file (or "Imported section N"), with " (imported)" added when the name is taken. Element indices from `sections.size()` on refer to them; the GUI appends them as user sections.
5. Supports: `allowedMotion` (or the free axes of `fixed`) and the free axes of `fixedRotation`. Prescribed values and amplitudes are reported as ignored.
6. `EndReleases` must be an integer in 0..4095; anything else is ignored (rigid ends) with a warning.
7. Results need `Displacement` and `BeamSectionForce`; the stresses are recomputed with `elementStress()`.

Export (`toMeshModel(mesh, materials, sections)`) writes everything above; an inclined rotation support goes out as fixed about every global axis outside its span, with a message in `model.warnings` (the GUI adds it to the export report).

## 12. Built-in beam library (`beamTypes/beamLibrary.*`)

`FEM::BEAM::LIBRARY` builds 49 ready-made frames in eight categories (Building, Bridge, Industrial, Energy & Tower, Machine & Vehicle, Aerospace, Hinges & Pins, Large Structures; 12 of them aerospace / space: wing spar, strut-braced wing, skid gear, engine pylon, satellite bus, station truss, lunar lander legs, thrust frame, quadcopter, fuselage frame, tail boom, solar array boom).

Hinges & Pins (end releases, section 6.1):

| id | Shows |
|---|---|
| `hinge_three_hinged_frame` | Statically determinate portal: pinned bases, ridge hinge (one rafter released about z), pinned ties and wall braces |
| `hinge_gerber_girder` | Cantilever-and-suspended-span bridge: two hinges in the main span |
| `hinge_simple_connection_frame` | Beams pinned at both ends (shear connections), pinned base plates, pin-ended vertical and plan bracing |
| `hinge_pinned_web_truss` | Pratt trusses: continuous chords, pin-ended posts and diagonals, rigid end posts |
| `hinge_loader_crane` | Boom pinned to the column head, luffing cylinder pinned at both ends and free to spin |
| `hinge_braced_landing_gear` | Gear leg on a trunnion, side and drag braces pinned to clamped airframe fittings |

Large Structures (rigid joints, about 1000 nodes and 3000 elements each; every one passes the same library checks):

| id | Nodes / elements | Shows |
|---|---|---|
| `large_stadium` | 1344 / 3312 | Elliptical bowl, 48 radial frames: raking beams on columns, 40 m tapered cantilever roof trusses tied down at the back, ring beams, a compression ring, roof bracing |
| `large_airport_terminal` | 1029 / 3098 | 144 x 72 m hall: 91 columns on a 12 m grid, departures floor grillage, 3 m deep square-on-square-offset roof space frame |
| `large_airliner_airframe` | 961 / 2910 | 38 m airframe on its gear: frames, stringers and skin diagonals; swept wing boxes with a centre box, tail boxes, engines on pylons. Generic, A320 / 737-class proportions only: not a real aircraft's structure (skin as diagonals, far weaker than a skin-stringer shell; 1 g ground case only, no pressurisation or flight loads) |

Their files take about 8.6 MB (MSH 4.1 ASCII, model and solved results); all 49 models solve in about 1.5 s on 16 threads. Sections and materials are referenced by catalogue / built-in name; a missing name throws.

```
beamLibrary.cpp --buildLibrary()--> MeshData x 49
      |                                  |
anaf_beam_library_tool          ADAPTER::toMeshModel  /  solveStatic + toMeshModel
      v                                  v
assets/objects/beam/beam3D/  <id>.msh (model)   <id>_solved.msh (model + results)   index.json
```

| Where | What |
| --- | --- |
| `assets/objects/beam/beam3D/<id>.msh` | MSH 4.1 ASCII model, no results |
| `assets/objects/beam/beam3D/<id>_solved.msh` | the same model with displacement, rotation, section force and stress fields |
| `assets/objects/beam/beam3D/index.json` | id, name, category, description per model |

1. Never edit the files by hand: change `beamLibrary.cpp` (or the catalogue), run `anaf_beam_library_tool`, commit the result.
2. The tests read the repository's `assets/` (`MAIN_DIR`), not `findAssetPath()`, which prefers an installed package (`/usr/share/anafinen/assets`) that may hold an older library.
3. `builtInBeamLibraryMatchesTheGenerator` compares index.json byte-exact and the `.msh` files by content (fields with a relative tolerance).
4. `builtInBeamsAreStableAndReasonable` imports and solves every model: no warnings, energy check passes, a 1 N / 1 N·m probe at every node finds no mechanism (displacement < 0.1 × extent), deflection < extent / 10, von Mises utilisation < 1, and the `_solved` displacements match a fresh solve.
5. The GUI loads both files through the Frame Editor's "Built-in Models" section ([GUI.md](GUI.md) section 2.5); export into the library folder is refused.

## 13. Related source files

- Built-in library: [beamLibrary.hpp](../src/objectCalcs/beam/beamTypes/beamLibrary.hpp), [beamLibrary.cpp](../src/objectCalcs/beam/beamTypes/beamLibrary.cpp), tool [beamLibraryTool.cpp](../tests/beamLibraryTool.cpp)

- Entry point: [beamSolver.hpp](../src/objectCalcs/beam/beamEngine/beamSolver.hpp), [beamSolver.cpp](../src/objectCalcs/beam/beamEngine/beamSolver.cpp)
- Container and element math: [deformationUnderConstForce.hpp](../src/objectCalcs/beam/beamEngine/beamSolver/deformationUnderConstForce.hpp), [deformationUnderConstForce.cpp](../src/objectCalcs/beam/beamEngine/beamSolver/deformationUnderConstForce.cpp)
- Stresses: [sectionStress.hpp](../src/objectCalcs/beam/beamSection/sectionStress.hpp), [sectionStress.cpp](../src/objectCalcs/beam/beamSection/sectionStress.cpp), [beamStress.hpp](../src/objectCalcs/beam/beamEngine/beamStress.hpp), [beamStress.cpp](../src/objectCalcs/beam/beamEngine/beamStress.cpp)
- Results along the element: [beamDiagrams.hpp](../src/objectCalcs/beam/beamEngine/beamDiagrams.hpp), [beamDiagrams.cpp](../src/objectCalcs/beam/beamEngine/beamDiagrams.cpp)
- Rendering helper: [sectionTriangulation.hpp](../src/objectCalcs/beam/beamSection/sectionTriangulation.hpp), [sectionTriangulation.cpp](../src/objectCalcs/beam/beamSection/sectionTriangulation.cpp)
- Cross-sections: [beamSection.hpp](../src/objectCalcs/beam/beamSection/beamSection.hpp), [beamSection.cpp](../src/objectCalcs/beam/beamSection/beamSection.cpp), [sectionLibrary.hpp](../src/objectCalcs/beam/beamSection/sectionLibrary.hpp), [sectionLibrary.cpp](../src/objectCalcs/beam/beamSection/sectionLibrary.cpp), [assets/bridge/sectionCatalog.json](../assets/bridge/sectionCatalog.json)
- Types: [node.hpp](../src/objectCalcs/beam/beamProperties/node.hpp), [node.cpp](../src/objectCalcs/beam/beamProperties/node.cpp), [element.hpp](../src/objectCalcs/beam/beamProperties/element.hpp), [loads.hpp](../src/objectCalcs/beam/beamProperties/loads.hpp), [meshData.hpp](../src/objectCalcs/beam/beamProperties/meshData.hpp)
- Support bases (shared with the truss): [supportBasis.hpp](../src/objectCalcs/common/supportBasis.hpp), [supportBasis.cpp](../src/objectCalcs/common/supportBasis.cpp)
- Solvers: [solverPortfolio.hpp](../src/solvers/solverPortfolio.hpp)
- File adapter: [beamMeshAdapter.hpp](../src/objectCalcs/beam/beamIO/beamMeshAdapter.hpp), [beamMeshAdapter.cpp](../src/objectCalcs/beam/beamIO/beamMeshAdapter.cpp)
- GUI: [beamModelEditor.cpp](../src/gui/panels/beam/beamModelEditor.cpp), [sectionHandler.cpp](../src/gui/panels/beam/sectionHandler.cpp), [beamDiagramPanel.cpp](../src/gui/panels/beam/beamDiagramPanel.cpp), [beamWorker.cpp](../src/gui/panels/beam/beamWorker.cpp)
- Tests: [tests/beamTests.cpp](../tests/beamTests.cpp)
- File side of the beam data: [FILE_HANDLING.md](FILE_HANDLING.md) section 3.3
