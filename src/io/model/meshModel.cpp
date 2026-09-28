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

  ElementBlock& MeshModel::blockFor(const ElementType type) {
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

  Field* MeshModel::findField(const std::string& name, const FieldLocation location) {
    for (auto& field : fields) {
      if (field.name == name && field.location == location) return &field;
    }
    return nullptr;
  }

  const Field* MeshModel::findField(const std::string& name, const FieldLocation location) const {
    for (const auto& field : fields) {
      if (field.name == name && field.location == location) return &field;
    }
    return nullptr;
  }

  const EntitySet* MeshModel::findSet(const std::string& name, const SetKind kind) const {
    for (const auto& set : sets) {
      if (set.name == name && set.kind == kind) return &set;
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

  std::string_view stepKindName(const StepKind kind) noexcept {
    switch (kind) {
      case StepKind::Time: return "Time";
      case StepKind::Frequency: return "Frequency";
      case StepKind::Mode: return "Mode";
      case StepKind::LoadCase: return "LoadCase";
    }
    return "Time";
  }

  std::optional<StepKind> stepKindFromName(const std::string_view name) noexcept {
    for (const auto kind : {StepKind::Time, StepKind::Frequency, StepKind::Mode, StepKind::LoadCase}) {
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
      const std::size_t limit = set.kind == SetKind::Node ? nodeTotal : elementTotal;
      if (std::ranges::any_of(set.members, [&](std::uint32_t m) { return m >= limit; })) {
        problems.push_back(std::format("set '{}': member index out of range", set.name));
      }
    }
    for (const auto& field : fields) {
      const std::size_t entities = field.location == FieldLocation::Node ? nodeTotal : elementTotal;
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
    for (std::size_t g = 0; g < globalData.size(); ++g) {
      const auto& array = globalData[g];
      if (array.components < 1 || array.values.size() % static_cast<std::size_t>(std::max(array.components, 1)) != 0) {
        problems.push_back(std::format("global '{}': {} values do not form {}-component tuples", array.name, array.values.size(), array.components));
      }
      for (std::size_t other = 0; other < g; ++other) {
        if (globalData[other].name == array.name) problems.push_back(std::format("global '{}' appears twice", array.name));
      }
    }
    for (const auto& constraint : constraints) {
      if (constraint.node >= nodeTotal) problems.push_back(std::format("constraint on missing node {}", constraint.node));
    }
    for (const auto& load : loads) {
      if (load.node >= nodeTotal) problems.push_back(std::format("load on missing node {}", load.node));
    }
    return problems;
  }

} // namespace anaf::IO end
