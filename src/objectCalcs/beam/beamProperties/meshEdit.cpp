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
    const auto shift = [k](const std::uint32_t id) { return id > k ? id - 1 : id; };
    std::vector<Node> nodes;
    nodes.reserve(mesh.nodes.size());
    for (const auto& node : mesh.nodes) {
      if (node.getNodeID() == k) continue;
      const auto& p = node.getLocation();
      Node renumbered(shift(node.getNodeID()), p[0], p[1], p[2]);
      renumbered.setAllowedMotionDirections(node.getAllowedMotionDirections());
      renumbered.setAllowedRotationAxes(node.getAllowedRotationAxes());
      nodes.push_back(std::move(renumbered));
    }
    mesh.nodes = std::move(nodes);
    removeElements(mesh, [k](const BeamElement& element) { return element.node1 == k || element.node2 == k; });
    for (auto& element : mesh.elements) {
      element.node1 = shift(element.node1);
      element.node2 = shift(element.node2);
    }
    std::erase_if(mesh.nodalLoads, [k](const NodalLoad& load) { return load.node == k; });
    for (auto& load : mesh.nodalLoads) load.node = shift(load.node);
  }

} // namespace FEM::BEAM end
