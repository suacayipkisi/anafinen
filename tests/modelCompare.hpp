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

// Order-independent model comparison: nodes and elements are matched by their tags, so the
// block order a format produces does not matter. Values must match exactly (bit for bit).

#include <io/model/meshModel.hpp>

#include <algorithm>
#include <format>
#include <map>
#include <string>
#include <vector>

namespace anaf::TESTING {

  struct CompareOptions {
    bool singleStep{false};      // expected Time fields reduced to their last step, times ignored
    bool stepLabels{true};       // compare Field::stepLabels (VTK formats do not store them)
    bool singleStepTimes{true};  // compare the time of fields with one step (.pvd folds them)
    bool entityTags{true};
    bool setTags{false};
    bool setDimensions{true};
  };

  inline std::vector<std::string> compareModels(const anaf::IO::MeshModel& expected, const anaf::IO::MeshModel& actual,
                                                const CompareOptions& options = {}) {
    using namespace anaf::IO;
    std::vector<std::string> diffs;
    auto diff = [&](std::string text) { if (diffs.size() < 25) diffs.push_back(std::move(text)); };

    // Nodes by tag.
    if (expected.nodes.size() != actual.nodes.size()) diff(std::format("node count {} vs {}", expected.nodes.size(), actual.nodes.size()));
    const auto actualNodeIndex = actual.nodeIndexByTag();
    std::vector<std::uint32_t> nodeMap(expected.nodes.size(), UINT32_MAX); // expected index -> actual index
    for (std::size_t i = 0; i < expected.nodes.size(); ++i) {
      const auto it = actualNodeIndex.find(expected.nodes[i].tag);
      if (it == actualNodeIndex.end()) {
        diff(std::format("node tag {} missing", expected.nodes[i].tag));
        continue;
      }
      nodeMap[i] = it->second;
      if (expected.nodes[i].position != actual.nodes[it->second].position) {
        diff(std::format("node {} position differs: ({},{},{}) vs ({},{},{})", expected.nodes[i].tag,
          expected.nodes[i].position[0], expected.nodes[i].position[1], expected.nodes[i].position[2],
          actual.nodes[it->second].position[0], actual.nodes[it->second].position[1], actual.nodes[it->second].position[2]));
      }
    }

    // Elements by tag.
    struct ElementRef { ElementType type; std::vector<std::uint32_t> nodes; int entity; std::size_t global; };
    auto collect = [](const MeshModel& model) {
      std::map<std::uint64_t, ElementRef> byTag;
      std::size_t global = 0;
      for (const auto& block : model.blocks) {
        const int n = elementInfo(block.type).nodeCount;
        for (std::size_t e = 0; e < block.size(); ++e, ++global) {
          byTag[block.tags[e]] = ElementRef{block.type,
            std::vector<std::uint32_t>(block.connectivity.begin() + static_cast<std::ptrdiff_t>(e * n),
                                       block.connectivity.begin() + static_cast<std::ptrdiff_t>((e + 1) * n)),
            block.entityTags.empty() ? 0 : block.entityTags[e], global};
        }
      }
      return byTag;
    };
    const auto expectedElements = collect(expected);
    const auto actualElements = collect(actual);
    if (expectedElements.size() != actualElements.size()) {
      diff(std::format("element count {} vs {}", expectedElements.size(), actualElements.size()));
    }
    std::vector<std::size_t> elementMap(expected.elementCount(), SIZE_MAX); // expected global -> actual global
    for (const auto& [tag, ref] : expectedElements) {
      const auto it = actualElements.find(tag);
      if (it == actualElements.end()) {
        diff(std::format("element tag {} missing", tag));
        continue;
      }
      elementMap[ref.global] = it->second.global;
      if (ref.type != it->second.type) diff(std::format("element {} type {} vs {}", tag, elementInfo(ref.type).name, elementInfo(it->second.type).name));
      std::vector<std::uint32_t> mapped;
      for (const auto n : ref.nodes) mapped.push_back(n < nodeMap.size() ? nodeMap[n] : UINT32_MAX);
      if (mapped != it->second.nodes) diff(std::format("element {} ({}) connectivity differs", tag, elementInfo(ref.type).name));
      if (options.entityTags && ref.entity != it->second.entity) diff(std::format("element {} entity {} vs {}", tag, ref.entity, it->second.entity));
    }

    // Sets by (name, kind).
    for (const auto& set : expected.sets) {
      const auto* other = actual.findSet(set.name, set.kind);
      if (!other) {
        diff(std::format("set '{}' missing", set.name));
        continue;
      }
      std::vector<std::uint32_t> mapped;
      for (const auto m : set.members) {
        mapped.push_back(set.kind == SetKind::Node ? nodeMap[m] : static_cast<std::uint32_t>(elementMap[m]));
      }
      std::ranges::sort(mapped);
      auto actualMembers = other->members;
      std::ranges::sort(actualMembers);
      if (mapped != actualMembers) diff(std::format("set '{}' members differ ({} vs {})", set.name, mapped.size(), actualMembers.size()));
      if (options.setDimensions && set.kind == SetKind::Element && set.dimension != other->dimension) {
        diff(std::format("set '{}' dimension {} vs {}", set.name, set.dimension, other->dimension));
      }
      if (options.setTags && set.tag != other->tag) diff(std::format("set '{}' tag {} vs {}", set.name, set.tag, other->tag));
    }
    if (expected.sets.size() != actual.sets.size()) diff(std::format("set count {} vs {}", expected.sets.size(), actual.sets.size()));

    // Fields.
    for (const auto& field : expected.fields) {
      const auto* other = actual.findField(field.name, field.location);
      if (!other) {
        diff(std::format("field '{}' missing", field.name));
        continue;
      }
      if (field.components != other->components) {
        diff(std::format("field '{}' components {} vs {}", field.name, field.components, other->components));
        continue;
      }
      if (field.stepKind != other->stepKind) diff(std::format("field '{}' step kind differs", field.name));
      if (options.stepLabels && field.stepLabels != other->stepLabels) diff(std::format("field '{}' step labels differ", field.name));
      const bool reduced = options.singleStep && field.stepKind == StepKind::Time;
      const bool compareTimes = !reduced && (options.singleStepTimes || field.steps.size() > 1);
      const std::size_t firstStep = reduced ? field.steps.size() - 1 : 0;
      const std::size_t expectedSteps = field.steps.size() - firstStep;
      if (other->steps.size() != expectedSteps) {
        diff(std::format("field '{}' steps {} vs {}", field.name, expectedSteps, other->steps.size()));
        continue;
      }
      for (std::size_t s = 0; s < expectedSteps; ++s) {
        const auto& values = field.steps[firstStep + s];
        const auto& otherValues = other->steps[s];
        if (compareTimes && field.times[firstStep + s] != other->times[s]) diff(std::format("field '{}' time differs", field.name));
        const bool onNodes = field.location == FieldLocation::Node;
        const std::size_t entities = onNodes ? expected.nodes.size() : expected.elementCount();
        const auto c = static_cast<std::size_t>(field.components);
        for (std::size_t e = 0; e < entities; ++e) {
          const std::size_t target = onNodes ? nodeMap[e] : elementMap[e];
          for (std::size_t k = 0; k < c; ++k) {
            if (values[e * c + k] != otherValues[target * c + k]) {
              diff(std::format("field '{}' step {} entity {} comp {}: {} vs {}", field.name, s, e, k, values[e * c + k], otherValues[target * c + k]));
              e = entities;
              break;
            }
          }
        }
      }
    }
    if (expected.fields.size() != actual.fields.size()) {
      std::string names;
      for (const auto& f : actual.fields) names += f.name + " ";
      diff(std::format("field count {} vs {} [{}]", expected.fields.size(), actual.fields.size(), names));
    }

    // Constraints, loads, attributes.
    auto constraintsOf = [](const MeshModel& model, const std::vector<std::uint32_t>* map) {
      std::map<std::uint32_t, NodeConstraint> result;
      for (auto c : model.constraints) {
        if (map) c.node = (*map)[c.node];
        result[c.node] = c;
      }
      return result;
    };
    const auto expectedConstraints = constraintsOf(expected, &nodeMap);
    const auto actualConstraints = constraintsOf(actual, nullptr);
    if (expectedConstraints.size() != actualConstraints.size()) diff(std::format("constraint count {} vs {}", expectedConstraints.size(), actualConstraints.size()));
    for (const auto& [node, c] : expectedConstraints) {
      const auto it = actualConstraints.find(node);
      if (it == actualConstraints.end()) {
        diff(std::format("constraint on node {} missing", node));
        continue;
      }
      if (c.fixed != it->second.fixed) diff(std::format("constraint on node {} fixity differs", node));
      if (!c.allowedMotion.empty() && c.allowedMotion != it->second.allowedMotion) diff(std::format("constraint on node {} basis differs", node));
    }
    std::map<std::uint32_t, std::array<double, 3>> expectedLoads, actualLoads;
    for (const auto& l : expected.loads) expectedLoads[nodeMap[l.node]] = l.force;
    for (const auto& l : actual.loads) actualLoads[l.node] = l.force;
    if (expectedLoads != actualLoads) diff("loads differ");

    for (const auto& [name, values] : expected.elementAttributes) {
      const auto it = actual.elementAttributes.find(name);
      if (it == actual.elementAttributes.end()) {
        diff(std::format("attribute '{}' missing", name));
        continue;
      }
      for (std::size_t e = 0; e < values.size(); ++e) {
        if (values[e] != it->second[elementMap[e]]) {
          diff(std::format("attribute '{}' element {} differs", name, e));
          break;
        }
      }
    }
    if (expected.elementAttributes.size() != actual.elementAttributes.size()) diff("attribute count differs");

    for (const auto& global : expected.globalData) {
      const auto* other = actual.findGlobal(global.name);
      if (!other) diff(std::format("global '{}' missing", global.name));
      else if (other->components != global.components || other->values != global.values) diff(std::format("global '{}' differs", global.name));
    }
    if (expected.globalData.size() != actual.globalData.size()) {
      std::string names;
      for (const auto& g : actual.globalData) names += g.name + " ";
      diff(std::format("global count {} vs {} [{}]", expected.globalData.size(), actual.globalData.size(), names));
    }
    return diffs;
  }

} // namespace anaf::TESTING end
