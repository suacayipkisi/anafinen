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

#include <utility>
#include <vector>

namespace FEM::TRUSS {

  void dropResults(MeshData& mesh) {
    if (!mesh.hasResults) return;
    for (auto& node : mesh.trussNodes) node.setDisplacements({0.0, 0.0, 0.0});
    for (auto& element : mesh.trussElements) {
      element.stress = 0.0f;
      element.isStressExceeded = false;
    }
    mesh.hasResults = false;
  }

  void deleteNode(MeshData& mesh, const std::uint32_t k) {
    const auto shift = [k](const std::uint32_t id) { return id > k ? id - 1 : id; };

    std::vector<Node> nodes;
    nodes.reserve(mesh.trussNodes.size());
    for (const auto& node : mesh.trussNodes) {
      if (node.getNodeID() == k) continue;
      Node renumbered(shift(node.getNodeID()), node.getLocX(), node.getLocY(), node.getLocZ());
      renumbered.setAllowedMotionDirections(node.getAllowedMotionDirections()); // keeps the support
      renumbered.setDisplacements(node.getDisplacement());
      nodes.push_back(std::move(renumbered));
    }
    mesh.trussNodes = std::move(nodes);

    std::erase_if(mesh.trussElements, [k](const RenderElement& element) {
      return element.node1 == k || element.node2 == k;
    });
    for (auto& element : mesh.trussElements) {
      element.node1 = shift(element.node1);
      element.node2 = shift(element.node2);
    }

    std::vector<ForceApplied> forces;
    for (const auto& force : mesh.appliedForces) {
      if (force.getAppliedNode() != k) forces.emplace_back(shift(force.getAppliedNode()), force.getForce());
    }
    mesh.appliedForces = std::move(forces);
  }

} // namespace FEM::TRUSS end
