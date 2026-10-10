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

#include "meshModel.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <stdexcept>

namespace anaf::IO {

  std::size_t MeshModel::elementCount() const noexcept {
    std::size_t count = 0;
    for (const auto& block : blocks) count += block.size();
    return count;
  }

  int MeshModel::maxDimension() const noexcept {
    int dimension = -1;
    for (const auto& block : blocks) {
      if (block.size() > 0) dimension = std::max(dimension, elementInfo(block.type).dimension);
    }
    return dimension;
  }

  ElementBlock& MeshModel::blockFor(const E_ElementType type) {
    for (auto& block : blocks) {
      if (block.type == type) return block;
    }
    blocks.push_back(ElementBlock{type, {}, {}, {}});
    return blocks.back();
  }

  std::size_t MeshModel::blockOffset(const std::size_t block) const {
    std::size_t offset = 0;
    for (std::size_t i = 0; i < block && i < blocks.size(); ++i) offset += blocks[i].size();
    return offset;
  }

  ElementLocation MeshModel::locateElement(std::size_t globalIndex) const {
    for (std::size_t block = 0; block < blocks.size(); ++block) {
      if (globalIndex < blocks[block].size()) return {block, globalIndex};
      globalIndex -= blocks[block].size();
    }
    throw std::out_of_range("element index outside the model");
  }

  std::unordered_map<std::uint64_t, std::uint32_t> MeshModel::nodeIndexByTag() const {
    std::unordered_map<std::uint64_t, std::uint32_t> map;
    map.reserve(nodes.size());
    for (std::size_t i = 0; i < nodes.size(); ++i) map.emplace(nodes[i].tag, static_cast<std::uint32_t>(i));
    return map;
  }

  Field* MeshModel::findField(const std::string& name, const E_FieldLocation location) {
    for (auto& field : fields) {
      if (field.name == name && field.location == location) return &field;
    }
    return nullptr;
  }

  const Field* MeshModel::findField(const std::string& name, const E_FieldLocation location) const {
    for (const auto& field : fields) {
      if (field.name == name && field.location == location) return &field;
    }
    return nullptr;
  }

  const EntitySet* MeshModel::findSet(const std::string& name, const E_SetKind kind) const {
    for (const auto& set : sets) {
      if (set.name == name && set.kind == kind) return &set;
    }
    return nullptr;
  }

  double Amplitude::factorAt(const double time) const noexcept {
    const std::size_t count = std::min(times.size(), factors.size());
    if (count == 0) return 1.0;
    if (time <= times.front()) return factors.front();
    for (std::size_t i = 1; i < count; ++i) {
      if (time <= times[i]) {
        const double span = times[i] - times[i - 1];
        if (span <= 0.0) return factors[i];
        return factors[i - 1] + (factors[i] - factors[i - 1]) * (time - times[i - 1]) / span;
      }
    }
    return factors[count - 1];
  }

  const Amplitude* MeshModel::findAmplitude(const std::string& name) const {
    for (const auto& amplitude : amplitudes) {
      if (amplitude.name == name) return &amplitude;
    }
    return nullptr;
  }

  const InitialCondition* MeshModel::findInitialCondition(const std::string& quantity) const {
    for (const auto& condition : initialConditions) {
      if (condition.quantity == quantity) return &condition;
    }
    return nullptr;
  }

  GlobalArray* MeshModel::findGlobal(const std::string& name) {
    for (auto& array : globalData) {
      if (array.name == name) return &array;
    }
    return nullptr;
  }

  const GlobalArray* MeshModel::findGlobal(const std::string& name) const {
    for (const auto& array : globalData) {
      if (array.name == name) return &array;
    }
    return nullptr;
  }

  std::string_view stepKindName(const E_StepKind kind) noexcept {
    switch (kind) {
      case E_StepKind::Time: return "Time";
      case E_StepKind::Frequency: return "Frequency";
      case E_StepKind::Mode: return "Mode";
      case E_StepKind::LoadCase: return "LoadCase";
    }
    return "Time";
  }

  std::optional<E_StepKind> stepKindFromName(const std::string_view name) noexcept {
    for (const auto kind : {E_StepKind::Time, E_StepKind::Frequency, E_StepKind::Mode, E_StepKind::LoadCase}) {
      if (stepKindName(kind) == name) return kind;
    }
    return std::nullopt;
  }

  std::vector<std::string> MeshModel::validate() const {
    std::vector<std::string> problems;
    const std::size_t nodeTotal = nodes.size();
    const std::size_t elementTotal = elementCount();

    for (std::size_t b = 0; b < blocks.size(); ++b) {
      const auto& block = blocks[b];
      const auto& info = elementInfo(block.type);
      if (block.connectivity.size() != block.size() * static_cast<std::size_t>(info.nodeCount)) {
        problems.push_back(std::format("block {} ({}): connectivity size {} does not match {} elements",
          b, info.name, block.connectivity.size(), block.size()));
      }
      if (!block.entityTags.empty() && block.entityTags.size() != block.size()) {
        problems.push_back(std::format("block {} ({}): entity tag count mismatch", b, info.name));
      }
      const auto outOfRange = std::ranges::find_if(block.connectivity, [&](std::uint32_t n) { return n >= nodeTotal; });
      if (outOfRange != block.connectivity.end()) {
        problems.push_back(std::format("block {} ({}): node index {} out of range", b, info.name, *outOfRange));
      }
    }
    for (const auto& set : sets) {
      const std::size_t limit = set.kind == E_SetKind::Node ? nodeTotal : elementTotal;
      if (std::ranges::any_of(set.members, [&](std::uint32_t m) { return m >= limit; })) {
        problems.push_back(std::format("set '{}': member index out of range", set.name));
      }
    }
    for (const auto& field : fields) {
      const std::size_t entities = field.location == E_FieldLocation::Node ? nodeTotal : elementTotal;
      if (field.components < 1) problems.push_back(std::format("field '{}': invalid component count", field.name));
      if (field.times.size() != field.steps.size()) problems.push_back(std::format("field '{}': times/steps mismatch", field.name));
      if (!field.stepLabels.empty() && field.stepLabels.size() != field.steps.size()) {
        problems.push_back(std::format("field '{}': {} step labels for {} steps", field.name, field.stepLabels.size(), field.steps.size()));
      }
      for (const auto& step : field.steps) {
        if (step.size() != entities * static_cast<std::size_t>(std::max(field.components, 1))) {
          problems.push_back(std::format("field '{}': step size {} does not match {} entities x {} components",
            field.name, step.size(), entities, field.components));
          break;
        }
      }
    }
    for (const auto& [name, values] : elementAttributes) {
      if (values.size() != elementTotal) {
        problems.push_back(std::format("attribute '{}': {} values for {} elements", name, values.size(), elementTotal));
      }
    }
    if (!beamOrientation.empty() && beamOrientation.size() != elementTotal) {
      problems.push_back(std::format("beam orientation: {} vectors for {} elements", beamOrientation.size(), elementTotal));
    }
    const auto formulation = elementAttributes.find(Attribute::elementFormulation);
    const bool checkFormulation = formulation != elementAttributes.end() && formulation->second.size() == elementTotal;
    const bool checkOrientation = beamOrientation.size() == elementTotal;
    std::size_t global = 0;
    for (const auto& block : blocks) {
      const auto& info = elementInfo(block.type);
      for (std::size_t e = 0; e < block.size(); ++e, ++global) {
        if (checkFormulation) {
          const double code = formulation->second[global];
          if (code != 0.0 && code != 1.0 && code != 2.0) {
            problems.push_back(std::format("element {}: unknown element formulation {}", global, code));
          } else if (code != 0.0 && info.dimension != 1) {
            problems.push_back(std::format("element {} ({}): beam formulation on a non-line element", global, info.name));
          }
        }
        if (!checkOrientation) continue;
        const auto& v = beamOrientation[global];
        if (v == std::array<double, 3>{}) continue;
        if (info.dimension != 1) {
          problems.push_back(std::format("element {} ({}): beam orientation on a non-line element", global, info.name));
          continue;
        }
        const std::size_t first = e * static_cast<std::size_t>(info.nodeCount);
        if (first + 1 >= block.connectivity.size() || block.connectivity[first] >= nodeTotal || block.connectivity[first + 1] >= nodeTotal) continue;
        const auto& p0 = nodes[block.connectivity[first]].position;
        const auto& p1 = nodes[block.connectivity[first + 1]].position;
        const std::array<double, 3> axis{p1[0] - p0[0], p1[1] - p0[1], p1[2] - p0[2]};
        const double axisLength = std::hypot(axis[0], axis[1], axis[2]);
        const double vLength = std::hypot(v[0], v[1], v[2]);
        const double cosine = axisLength > 0.0 ? std::abs(axis[0] * v[0] + axis[1] * v[1] + axis[2] * v[2]) / (axisLength * vLength) : 1.0;
        if (cosine > 1.0 - 1e-6) {
          problems.push_back(std::format("element {}: beam orientation is parallel to the element axis", global));
        }
      }
    }
    for (std::size_t g = 0; g < globalData.size(); ++g) {
      const auto& array = globalData[g];
      if (array.components < 1 || array.values.size() % static_cast<std::size_t>(std::max(array.components, 1)) != 0) {
        problems.push_back(std::format("global '{}': {} values do not form {}-component tuples", array.name, array.values.size(), array.components));
      }
      for (std::size_t other = 0; other < g; ++other) {
        if (globalData[other].name == array.name) problems.push_back(std::format("global '{}' appears twice", array.name));
      }
    }
    for (std::size_t a = 0; a < amplitudes.size(); ++a) {
      const auto& amplitude = amplitudes[a];
      // Names end up in array names of every format: keep them printable and unambiguous.
      if (amplitude.name.empty() || amplitude.name.find_first_of("\"\n\r") != std::string::npos) {
        problems.push_back(std::format("amplitude '{}': the name must be non-empty without quotes or line breaks", amplitude.name));
      }
      if (amplitude.times.size() != amplitude.factors.size() || amplitude.times.empty()) {
        problems.push_back(std::format("amplitude '{}': {} times for {} factors", amplitude.name, amplitude.times.size(), amplitude.factors.size()));
      }
      if (!std::ranges::is_sorted(amplitude.times)) problems.push_back(std::format("amplitude '{}': times are not sorted", amplitude.name));
      for (std::size_t other = 0; other < a; ++other) {
        if (amplitudes[other].name == amplitude.name) problems.push_back(std::format("amplitude '{}' appears twice", amplitude.name));
      }
    }
    auto checkReference = [&](const std::string& amplitude, const char* what, const std::uint32_t node) {
      if (node >= nodeTotal) problems.push_back(std::format("{} on missing node {}", what, node));
      if (!amplitude.empty() && !findAmplitude(amplitude)) {
        problems.push_back(std::format("{} on node {}: unknown amplitude '{}'", what, node, amplitude));
      }
    };
    for (const auto& constraint : constraints) {
      checkReference(constraint.amplitude, "constraint", constraint.node);
      checkReference(constraint.amplitudeRotation, "rotational constraint", constraint.node);
      for (std::size_t axis = 0; axis < 3; ++axis) {
        if (constraint.prescribedRotation[axis] != 0.0 && !constraint.fixedRotation[axis]) {
          problems.push_back(std::format("constraint on node {}: prescribed rotation about {} on a free rotational DOF", constraint.node, "XYZ"[axis]));
        }
      }
    }
    for (const auto& load : loads) checkReference(load.amplitude, "load", load.node);
    for (const auto& constraint : temperatureConstraints) checkReference(constraint.amplitude, "temperature constraint", constraint.node);
    for (const auto& load : heatLoads) checkReference(load.amplitude, "heat load", load.node);
    for (std::size_t i = 0; i < initialConditions.size(); ++i) {
      const auto& condition = initialConditions[i];
      if (condition.quantity.empty() || condition.components < 1
          || condition.values.size() != nodeTotal * static_cast<std::size_t>(std::max(condition.components, 1))) {
        problems.push_back(std::format("initial condition '{}': {} values for {} nodes x {} components",
          condition.quantity, condition.values.size(), nodeTotal, condition.components));
      }
      for (std::size_t other = 0; other < i; ++other) {
        if (initialConditions[other].quantity == condition.quantity) problems.push_back(std::format("initial condition '{}' appears twice", condition.quantity));
      }
    }
    return problems;
  }

} // namespace anaf::IO end
