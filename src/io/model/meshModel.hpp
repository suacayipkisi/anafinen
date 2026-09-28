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

  // Kinematic constraint of one node. `fixed[axis] == true` removes that global DOF.
  // `allowedMotion` optionally stores an orthonormal basis of the free directions
  // (inclined supports); empty means "derive from `fixed`".
  struct NodeConstraint {
    std::uint32_t node{};
    std::array<bool, 3> fixed{};
    std::vector<std::array<double, 3>> allowedMotion;
  };

  struct NodalLoad {
    std::uint32_t node{};
    std::array<double, 3> force{}; // N
  };

  struct ElementLocation {
    std::size_t block{};
    std::size_t local{};
  };

  // Well-known element attribute names shared by all formats and solvers.
  namespace Attribute {
    inline constexpr const char* MaterialId = "MaterialID";
    inline constexpr const char* CrossSectionArea = "CrossSectionArea"; // m^2
  }

  // Well-known field names shared by all formats and solvers.
  namespace FieldName {
    inline constexpr const char* Displacement = "Displacement"; // node, 3 components, m
    inline constexpr const char* Stress = "Stress";             // element, Pa (bars: axial, tension > 0)
    inline constexpr const char* AxialForce = "AxialForce";     // element, N (tension > 0)
  }

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
    // Per-element scalar attributes indexed by global element index (MaterialID, CrossSectionArea, ...).
    std::map<std::string, std::vector<double>> elementAttributes;
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
    GlobalArray* findGlobal(const std::string& name);
    const GlobalArray* findGlobal(const std::string& name) const;

    // Returns an empty vector when the model is consistent, otherwise one message per problem.
    std::vector<std::string> validate() const;
  };

} // namespace anaf::IO end
