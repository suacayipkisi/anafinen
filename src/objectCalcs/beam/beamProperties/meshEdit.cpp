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

#include "meshEdit.hpp"

#include <limits>
#include <vector>

namespace FEM::BEAM {

  void dropResults(MeshData& mesh) {
    if (!mesh.hasResults) return;
    for (auto& node : mesh.nodes) {
      node.setDisplacement({0.0, 0.0, 0.0});
      node.setRotation({0.0, 0.0, 0.0});
    }
    for (auto& element : mesh.elements) {
      element.sectionForces = {};
      element.stress = {};
    }
    mesh.hasResults = false;
  }

  void deleteNode(MeshData& mesh, const std::uint32_t k) {
    deleteNodes(mesh, std::span<const std::uint32_t>(&k, 1));
  }

  void deleteNodes(MeshData& mesh, const std::span<const std::uint32_t> ids) {
    // New id of every old node, removed = max.
    constexpr std::uint32_t removed = std::numeric_limits<std::uint32_t>::max();
    std::vector<std::uint32_t> newId(mesh.nodes.size(), 0);
    for (const auto id : ids) newId[id] = removed;
    std::uint32_t next = 0;
    for (auto& id : newId) id = id == removed ? removed : next++;

    // Nodes are trivially copyable: moved down in place, support bases and all.
    std::size_t kept = 0;
    for (std::size_t i = 0; i < mesh.nodes.size(); ++i) {
      if (newId[i] == removed) continue;
      mesh.nodes[kept] = mesh.nodes[i];
      mesh.nodes[kept].setNodeID(newId[i]);
      ++kept;
    }
    mesh.nodes.resize(kept);

    removeElements(mesh, [&](const BeamElement& element) { return newId[element.node1] == removed || newId[element.node2] == removed; });
    for (auto& element : mesh.elements) {
      element.node1 = newId[element.node1];
      element.node2 = newId[element.node2];
    }
    std::erase_if(mesh.nodalLoads, [&](const NodalLoad& load) { return newId[load.node] == removed; });
    for (auto& load : mesh.nodalLoads) load.node = newId[load.node];
  }

} // namespace FEM::BEAM end
