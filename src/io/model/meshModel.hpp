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
    ElementType type{ElementType::Line2};
    std::vector<std::uint64_t> tags;           // one per element
    std::vector<std::uint32_t> connectivity;   // nodeCount(type) indices per element
    std::vector<int> entityTags;               // geometric entity per element (0 = none)

    std::size_t size() const noexcept { return tags.size(); }
  };

  enum class SetKind : std::uint8_t { Node, Element };

  // Named group of nodes or elements (Gmsh physical group, Abaqus NSET/ELSET, ...).
  struct EntitySet {
    std::string name;
    SetKind kind{SetKind::Element};
    int dimension{-1};                    // topological dimension for element sets, -1 if unknown
    int tag{-1};                          // physical tag from the file, -1 if none
    std::vector<std::uint32_t> members;   // node indices or global element indices
  };

  enum class FieldLocation : std::uint8_t { Node, Element };

  // What the steps of a field are, and so what `Field::times` holds.
  enum class StepKind : std::uint8_t {
    Time,       // transient / pseudo-time history; times[s] = time (s)
    Frequency,  // frequency response; times[s] = excitation frequency (Hz)
    Mode,       // eigenmodes (modal analysis); times[s] = natural frequency (Hz), mode number = s + 1
    LoadCase    // independent static load cases; times[s] = load case number
  };

  // Result or input field with one or more steps.
  struct Field {
    std::string name;
    FieldLocation location{FieldLocation::Node};
    int components{1};
    std::vector<double> times;                // one value per step, meaning set by `stepKind`
    std::vector<std::vector<double>> steps;   // steps[s][entity * components + c]
    StepKind stepKind{StepKind::Time};
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
    inline constexpr const char* MaterialId = "MaterialID";
    inline constexpr const char* CrossSectionArea = "CrossSectionArea"; // m^2
    inline constexpr const char* HeatGeneration = "HeatGeneration";     // W/m^3 (volumetric heat source)
    inline constexpr const char* SecondMomentY = "SecondMomentY";       // m^4, second moment of area about local y
    inline constexpr const char* SecondMomentZ = "SecondMomentZ";       // m^4, second moment of area about local z
    inline constexpr const char* TorsionConstant = "TorsionConstant";   // m^4, St. Venant J (not the polar moment)
    inline constexpr const char* ShearAreaY = "ShearAreaY";             // m^2, effective shear area k*A along local y; 0 / missing = shear-rigid
    inline constexpr const char* ShearAreaZ = "ShearAreaZ";             // m^2, effective shear area k*A along local z; 0 / missing = shear-rigid
    inline constexpr const char* ElementFormulation = "ElementFormulation"; // ElementFormulation code; missing = Bar
  }

  // Values of the "ElementFormulation" attribute. A Line2 / Line3 element is a bar (axial only,
  // 3 DOFs per node) unless the attribute says otherwise, so files without it keep their meaning.
  enum class ElementFormulation : std::uint8_t {
    Bar = 0,
    EulerBernoulliBeam = 1, // 6 DOFs per node, shear-rigid
    TimoshenkoBeam = 2      // 6 DOFs per node, shear-flexible (uses ShearAreaY / ShearAreaZ)
  };

  // Well-known initial condition quantities.
  namespace InitialQuantity {
    inline constexpr const char* Displacement = "Displacement";       // 3 components, m
    inline constexpr const char* Velocity = "Velocity";               // 3 components, m/s
    inline constexpr const char* Rotation = "Rotation";               // 3 components, rad
    inline constexpr const char* AngularVelocity = "AngularVelocity"; // 3 components, rad/s
    inline constexpr const char* Temperature = "Temperature";         // 1 component, K
  }

  // Well-known field names shared by all formats and solvers.
  namespace FieldName {
    inline constexpr const char* Displacement = "Displacement";                   // node, 3 components, m
    inline constexpr const char* Stress = "Stress";                               // element, Pa (bars: axial, tension > 0)
    inline constexpr const char* AxialForce = "AxialForce";                       // element, N (tension > 0)
    inline constexpr const char* Rotation = "Rotation";                           // node, 3 components, rad
    inline constexpr const char* Velocity = "Velocity";                           // node, 3 components, m/s
    inline constexpr const char* Acceleration = "Acceleration";                   // node, 3 components, m/s^2
    inline constexpr const char* AngularVelocity = "AngularVelocity";             // node, 3 components, rad/s
    inline constexpr const char* AngularAcceleration = "AngularAcceleration";     // node, 3 components, rad/s^2
    inline constexpr const char* BeamSectionForce = "BeamSectionForce";           // element, 12 components, N and N*m (see below)
  }
  // BeamSectionForce: N, Vy, Vz, T, My, Mz at node 0, then the same at node 1, in the local frame
  // of MeshModel::beamOrientation. Section sign convention, not element end forces: the value at
  // node 0 is minus the end force k*u there, the value at node 1 is plus it. N > 0 is tension at
  // both ends (matches AxialForce), and a constant moment shows the same My / Mz at both ends.

  // Well-known global array names.
  namespace GlobalName {
    inline constexpr const char* NaturalFrequency = "NaturalFrequency"; // 1 component per mode, Hz
  }

  std::string_view stepKindName(StepKind kind) noexcept;              // "Time", "Frequency", "Mode", "LoadCase"
  std::optional<StepKind> stepKindFromName(std::string_view name) noexcept;

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
    ElementBlock& blockFor(ElementType type);

    ElementLocation locateElement(std::size_t globalIndex) const;
    std::size_t blockOffset(std::size_t block) const;

    // Node index lookup by file tag; built on demand.
    std::unordered_map<std::uint64_t, std::uint32_t> nodeIndexByTag() const;

    Field* findField(const std::string& name, FieldLocation location);
    const Field* findField(const std::string& name, FieldLocation location) const;
    const EntitySet* findSet(const std::string& name, SetKind kind) const;
    const Amplitude* findAmplitude(const std::string& name) const;
    const InitialCondition* findInitialCondition(const std::string& quantity) const;
    GlobalArray* findGlobal(const std::string& name);
    const GlobalArray* findGlobal(const std::string& name) const;

    // Returns an empty vector when the model is consistent, otherwise one message per problem.
    std::vector<std::string> validate() const;
  };

} // namespace anaf::IO end
