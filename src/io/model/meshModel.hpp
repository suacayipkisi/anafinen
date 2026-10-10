// Copyright (c) 2026 Abdurrahman Konuk (professionally known as Ufuk Deniz Konuk)
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program. If not, see <https://www.gnu.org/licenses/>.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "elementType.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace anaf::IO {

  // Format-neutral finite element model: the single exchange type between file formats,
  // the solvers and the front ends (GUI / CLI). It has no dependency on any solver type.
  //
  // Indexing rules:
  //  - Nodes are addressed by a 0-based index into `nodes`. `tag` keeps the id from the file.
  //  - Elements are grouped by type into blocks. The global element index runs over the
  //    blocks in order: block 0 elements first, then block 1, ...
  //  - Connectivity stores node indices (not tags) in Gmsh local node order.
  //  - Units are SI unless `lengthUnit` says otherwise.

  struct Node {
    std::uint64_t tag{};
    std::array<double, 3> position{};
  };

  struct ElementBlock {
    E_ElementType type{E_ElementType::Line2};
    std::vector<std::uint64_t> tags;           // one per element
    std::vector<std::uint32_t> connectivity;   // nodeCount(type) indices per element
    std::vector<int> entityTags;               // geometric entity per element (0 = none)

    std::size_t size() const noexcept { return tags.size(); }
  };

  enum class E_SetKind : std::uint8_t { Node, Element };

  // Named group of nodes or elements (Gmsh physical group, Abaqus NSET/ELSET, ...).
  struct EntitySet {
    std::string name;
    E_SetKind kind{E_SetKind::Element};
    int dimension{-1};                    // topological dimension for element sets, -1 if unknown
    int tag{-1};                          // physical tag from the file, -1 if none
    std::vector<std::uint32_t> members;   // node indices or global element indices
  };

  enum class E_FieldLocation : std::uint8_t { Node, Element };

  // What the steps of a field are, and so what `Field::times` holds.
  enum class E_StepKind : std::uint8_t {
    Time,       // transient / pseudo-time history; times[s] = time (s)
    Frequency,  // frequency response; times[s] = excitation frequency (Hz)
    Mode,       // eigenmodes (modal analysis); times[s] = natural frequency (Hz), mode number = s + 1
    LoadCase    // independent static load cases; times[s] = load case number
  };

  // Result or input field with one or more steps.
  struct Field {
    std::string name;
    E_FieldLocation location{E_FieldLocation::Node};
    int components{1};
    std::vector<double> times;                // one value per step, meaning set by `stepKind`
    std::vector<std::vector<double>> steps;   // steps[s][entity * components + c]
    E_StepKind stepKind{E_StepKind::Time};
    std::vector<std::string> stepLabels;      // empty, or one label per step (load case names, ...)
  };

  // Model-level (dataset) array that belongs to no node or element: natural frequencies,
  // modal masses, solver statistics, ... `values` holds tuples of `components` values.
  struct GlobalArray {
    std::string name;
    int components{1};
    std::vector<double> values;

    std::size_t tuples() const noexcept { return components > 0 ? values.size() / static_cast<std::size_t>(components) : 0; }
  };

  // Boundary conditions and loads refer to an Amplitude by name; an empty name means a
  // constant factor of 1. Values are the reference magnitudes that the amplitude scales.

  // Kinematic constraint of one node. `fixed[axis] == true` removes that global DOF.
  // `allowedMotion` optionally stores an orthonormal basis of the free directions
  // (inclined supports); empty means "derive from `fixed`".
  // `prescribed` is the imposed displacement of the fixed DOFs in global components (m);
  // zero is an ordinary support. For inclined supports the part normal to `allowedMotion` applies.
  // Rotational constraints (beams / frames / shells): `fixedRotation[axis]` (Rx, Ry, Rz),
  // `prescribedRotation` (rad, small-rotation vector), and optional independent `amplitudeRotation`.
  struct NodeConstraint {
    std::uint32_t node{};
    std::array<bool, 3> fixed{};
    std::vector<std::array<double, 3>> allowedMotion;
    std::array<double, 3> prescribed{};
    std::string amplitude;
    std::array<bool, 3> fixedRotation{};
    std::array<double, 3> prescribedRotation{};
    std::string amplitudeRotation{};
  };

  struct NodalLoad {
    std::uint32_t node{};
    std::array<double, 3> force{}; // N
    std::string amplitude;
    std::array<double, 3> moment{}; // concentrated moment: Mx, My, Mz [N*m], global axes
  };

  // Prescribed temperature (Dirichlet) of one node, K.
  struct TemperatureConstraint {
    std::uint32_t node{};
    double temperature{};
    std::string amplitude;
  };

  // Concentrated heat flow into one node, W (> 0 heats the body).
  struct HeatLoad {
    std::uint32_t node{};
    double power{};
    std::string amplitude;
  };

  // Time-dependent scale factor: piecewise linear through (times[i], factors[i]), held
  // constant before the first and after the last point. `times` is non-decreasing.
  struct Amplitude {
    std::string name;
    std::vector<double> times;
    std::vector<double> factors;

    double factorAt(double time) const noexcept;
  };

  // Nodal initial state for transient analyses: values[node * components + c].
  struct InitialCondition {
    std::string quantity;   // InitialQuantity::* or any other name
    int components{1};
    std::vector<double> values;
  };

  // Global damping model: Rayleigh C = alpha * M + beta * K, and / or one damping ratio per
  // mode for modal superposition (modalRatios[i] belongs to mode i + 1).
  struct Damping {
    double rayleighAlpha{}; // 1/s
    double rayleighBeta{};  // s
    std::vector<double> modalRatios;
  };

  struct ElementLocation {
    std::size_t block{};
    std::size_t local{};
  };

  // Well-known element attribute names shared by all formats and solvers.
  // Values follow `lengthUnit` (m^2 / m^4 for "m"); anaf_io never converts units.
  // Beam section properties refer to the principal axes of the local frame (see MeshModel::beamOrientation).
  namespace Attribute {
    inline constexpr const char* materialId = "MaterialID";
    inline constexpr const char* crossSectionArea = "CrossSectionArea"; // m^2
    inline constexpr const char* heatGeneration = "HeatGeneration";     // W/m^3 (volumetric heat source)
    inline constexpr const char* secondMomentY = "SecondMomentY";       // m^4, second moment of area about local y
    inline constexpr const char* secondMomentZ = "SecondMomentZ";       // m^4, second moment of area about local z
    inline constexpr const char* torsionConstant = "TorsionConstant";   // m^4, St. Venant J (not the polar moment)
    inline constexpr const char* shearAreaY = "ShearAreaY";             // m^2, effective shear area k*A along local y; 0 / missing = shear-rigid
    inline constexpr const char* shearAreaZ = "ShearAreaZ";             // m^2, effective shear area k*A along local z; 0 / missing = shear-rigid
    inline constexpr const char* elementFormulation = "ElementFormulation"; // ElementFormulation code; missing = Bar
    // Beam section shape (anafinen sections, FEM::BEAM::SectionShape) so a shape survives a round
    // trip: code 0 general, 1 rectangle, 2 circle, 3 pipe, 4 box, 5 I; dimensions in m:
    //   rectangle: 1 height, 2 width | circle: 1 diameter | pipe: 1 outer diameter, 2 wall
    //   box: 1 height, 2 width, 3 wall, 4 outer corner radius, 5 inner corner radius
    //   I: 1 height, 2 flange width, 3 web, 4 flange thickness, 5 root radius; unused = 0.
    inline constexpr const char* sectionShape = "SectionShape";
    inline constexpr const char* sectionDimension1 = "SectionDimension1";
    inline constexpr const char* sectionDimension2 = "SectionDimension2";
    inline constexpr const char* sectionDimension3 = "SectionDimension3";
    inline constexpr const char* sectionDimension4 = "SectionDimension4";
    inline constexpr const char* sectionDimension5 = "SectionDimension5";
    // Uniform line load over a beam element, N/m: sum of the loads given in global axes and sum
    // of those given in the element's local axes (see beamOrientation).
    inline constexpr const char* uniformLoadGlobalX = "UniformLoadGlobalX";
    inline constexpr const char* uniformLoadGlobalY = "UniformLoadGlobalY";
    inline constexpr const char* uniformLoadGlobalZ = "UniformLoadGlobalZ";
    inline constexpr const char* uniformLoadLocalX = "UniformLoadLocalX";
    inline constexpr const char* uniformLoadLocalY = "UniformLoadLocalY";
    inline constexpr const char* uniformLoadLocalZ = "UniformLoadLocalZ";
    // Beam end releases (hinges) as a bit set stored in a double (0..4095): bit k frees local
    // DOF k {ux, uy, uz, rx, ry, rz} of node 1 (k = 0..5) or node 2 (k = 6..11), i.e. the section
    // force {N, Vy, Vz, T, My, Mz} at that end is zero. Missing / 0 = rigidly connected. A
    // bending hinge at node 1 is 48 (My + Mz), at node 2 3072, at both ends 3120.
    inline constexpr const char* endReleases = "EndReleases";
  }

  // Values of the "ElementFormulation" attribute. A Line2 / Line3 element is a bar (axial only,
  // 3 DOFs per node) unless the attribute says otherwise, so files without it keep their meaning.
  enum class E_ElementFormulation : std::uint8_t {
    Bar = 0,
    EulerBernoulliBeam = 1, // 6 DOFs per node, shear-rigid
    TimoshenkoBeam = 2      // 6 DOFs per node, shear-flexible (uses ShearAreaY / ShearAreaZ)
  };

  // Well-known initial condition quantities.
  namespace InitialQuantity {
    inline constexpr const char* displacement = "Displacement";       // 3 components, m
    inline constexpr const char* velocity = "Velocity";               // 3 components, m/s
    inline constexpr const char* rotation = "Rotation";               // 3 components, rad
    inline constexpr const char* angularVelocity = "AngularVelocity"; // 3 components, rad/s
    inline constexpr const char* temperature = "Temperature";         // 1 component, K
  }

  // Well-known field names shared by all formats and solvers.
  namespace FieldName {
    inline constexpr const char* displacement = "Displacement";                   // node, 3 components, m
    inline constexpr const char* stress = "Stress";                               // element, Pa (bars: axial, tension > 0)
    inline constexpr const char* axialForce = "AxialForce";                       // element, N (tension > 0)
    inline constexpr const char* rotation = "Rotation";                           // node, 3 components, rad
    inline constexpr const char* velocity = "Velocity";                           // node, 3 components, m/s
    inline constexpr const char* acceleration = "Acceleration";                   // node, 3 components, m/s^2
    inline constexpr const char* angularVelocity = "AngularVelocity";             // node, 3 components, rad/s
    inline constexpr const char* angularAcceleration = "AngularAcceleration";     // node, 3 components, rad/s^2
    inline constexpr const char* beamSectionForce = "BeamSectionForce";           // element, 12 components, N and N*m (see below)
    inline constexpr const char* vonMisesStress = "VonMisesStress";               // element, Pa, largest equivalent stress along a beam (upper bound)
  }
  // BeamSectionForce: N, Vy, Vz, T, My, Mz at node 0, then the same at node 1, in the local frame
  // of MeshModel::beamOrientation. Section sign convention, not element end forces: the value at
  // node 0 is minus the end force k*u there, the value at node 1 is plus it. N > 0 is tension at
  // both ends (matches AxialForce), and a constant moment shows the same My / Mz at both ends.

  // Well-known global array names.
  namespace GlobalName {
    inline constexpr const char* naturalFrequency = "NaturalFrequency"; // 1 component per mode, Hz
    inline constexpr const char* gravity = "Gravity";                   // 3 components, m/s^2, global axes (self weight); zero = none
  }

  std::string_view stepKindName(E_StepKind kind) noexcept;              // "Time", "Frequency", "Mode", "LoadCase"
  std::optional<E_StepKind> stepKindFromName(std::string_view name) noexcept;

  class MeshModel {
  public:
    std::vector<Node> nodes;
    std::vector<ElementBlock> blocks;
    std::vector<EntitySet> sets;
    std::vector<Field> fields;
    std::vector<NodeConstraint> constraints;
    std::vector<NodalLoad> loads;
    std::vector<TemperatureConstraint> temperatureConstraints;
    std::vector<HeatLoad> heatLoads;
    std::vector<Amplitude> amplitudes;
    std::vector<InitialCondition> initialConditions;
    std::optional<Damping> damping;
    // Per-element scalar attributes indexed by global element index (MaterialID, CrossSectionArea, ...).
    std::map<std::string, std::vector<double>> elementAttributes;
    // Beam orientation reference vector v per global element index, global axes; empty = none given.
    // Local x runs from node 0 to node 1, v lies in the local x-y plane: z = normalize(x cross v),
    // y = z cross x. A zero vector leaves the choice to the solver's default rule.
    std::vector<std::array<double, 3>> beamOrientation;
    std::vector<GlobalArray> globalData;

    std::string lengthUnit{"m"};
    std::string title;
    std::vector<std::string> warnings; // non-fatal issues collected by readers and writers

    std::size_t nodeCount() const noexcept { return nodes.size(); }
    std::size_t elementCount() const noexcept;
    int maxDimension() const noexcept;

    // Appends a block for `type` or returns the existing one (keeps one block per type).
    ElementBlock& blockFor(E_ElementType type);

    ElementLocation locateElement(std::size_t globalIndex) const;
    std::size_t blockOffset(std::size_t block) const;

    // Node index lookup by file tag; built on demand.
    std::unordered_map<std::uint64_t, std::uint32_t> nodeIndexByTag() const;

    Field* findField(const std::string& name, E_FieldLocation location);
    const Field* findField(const std::string& name, E_FieldLocation location) const;
    const EntitySet* findSet(const std::string& name, E_SetKind kind) const;
    const Amplitude* findAmplitude(const std::string& name) const;
    const InitialCondition* findInitialCondition(const std::string& quantity) const;
    GlobalArray* findGlobal(const std::string& name);
    const GlobalArray* findGlobal(const std::string& name) const;

    // Returns an empty vector when the model is consistent, otherwise one message per problem.
    std::vector<std::string> validate() const;
  };

} // namespace anaf::IO end
