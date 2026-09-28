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

#include "trussSolver.hpp"
#include <log/anaf_info.hpp>
#include <bridge/generalStatus.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <exception>
#include <format>
#include <stop_token>
#include <string>
#include <vector>

namespace FEM::TRUSS {

  std::expected<void, std::string> Truss_Imported_or_Entered::setModel(
    anaf::BRIDGE::Gui_Calc_Bridge& bridge,
    std::stop_token st,
    const anaf::BRIDGE::MeshData& mesh,
    const anaf::BRIDGE::FixedDOFMap& fixedDOFsByNode,
    std::span<const anaf::MATERIAL::Material> materials
  ) {
    m_nodes.clear();
    m_elements.clear();
    m_renderIndex.clear();
    if (st.stop_requested()) return {};

    const std::size_t nodeCount = mesh.trussNodes.size();
    if (nodeCount == 0) return std::unexpected("the model has no nodes");

    // Element node indices address this vector, so ids must equal positions.
    m_nodes.reserve(nodeCount);
    for (std::size_t i = 0; i < nodeCount; ++i) {
      const auto& source = mesh.trussNodes[i];
      if (source.getNodeID() != i) {
        return std::unexpected(std::format("node at position {} has id {}; node ids must be 0..{} in order",
                                           i, source.getNodeID(), nodeCount - 1));
      }
      m_nodes.emplace_back(static_cast<std::uint32_t>(i), source.getLocX(), source.getLocY(), source.getLocZ());
    }

    std::size_t withoutArea = 0;
    std::size_t unknownMaterial = 0;
    std::vector<bool> usedByBar(nodeCount, false);
    m_elements.reserve(mesh.trussElements.size());
    for (std::size_t e = 0; e < mesh.trussElements.size(); ++e) {
      const auto& element = mesh.trussElements[e];
      if (element.isWireframe) continue;
      if (!(element.crossSectionArea > 0.0)) {
        ++withoutArea;
        continue;
      }
      if (element.materialID >= materials.size()) {
        ++unknownMaterial;
        continue;
      }
      if (element.node1 >= nodeCount || element.node2 >= nodeCount) {
        return std::unexpected(std::format("bar {} references node {}, but the model has {} nodes",
                                           e, std::max(element.node1, element.node2), nodeCount));
      }
      if (element.node1 == element.node2) {
        return std::unexpected(std::format("bar {} starts and ends at node {}", e, element.node1));
      }
      try {
        m_elements.emplace_back(element.materialID, element.crossSectionArea, element.node1, element.node2,
                                std::span<const Node>(m_nodes));
      } catch (const std::exception& exception) {
        return std::unexpected(std::format("bar {} (nodes {} - {}): {}", e, element.node1, element.node2, exception.what()));
      }
      m_renderIndex.push_back(e);
      usedByBar[element.node1] = true;
      usedByBar[element.node2] = true;
    }

    if (withoutArea > 0) {
      return std::unexpected(std::format("{} bars have no cross-section area; set one in the model editor", withoutArea));
    }
    if (unknownMaterial > 0) {
      return std::unexpected(std::format("{} bars use a material that is not in the material list", unknownMaterial));
    }
    if (m_elements.empty()) {
      return std::unexpected("the model has no bars (surface and volume meshes are shown as wireframe only)");
    }

    std::size_t isolated = 0;
    std::size_t fixedNodes = 0;
    for (auto& node : m_nodes) {
      const std::uint32_t id = node.getNodeID();
      if (!usedByBar[id]) {
        node.setMovable({false, false, false});
        ++isolated;
        continue;
      }
      const auto it = fixedDOFsByNode.find(id);
      if (it != fixedDOFsByNode.end() && (it->second[0] || it->second[1] || it->second[2])) {
        node.setMovable({!it->second[0], !it->second[1], !it->second[2]});
        ++fixedNodes;
      } else {
        node.setMovable({true, true, true});
      }
    }
    if (isolated > 0) anaf::LOG::warn("{} nodes are not connected to any bar; they are held fixed", isolated);
    anaf::LOG::info("Model: {} nodes, {} bars, {} supported nodes", nodeCount, m_elements.size(), fixedNodes);
    bridge.m_progress = 0.20f;
    return {};
  }

  void Truss_Imported_or_Entered::setForce(
    anaf::BRIDGE::Gui_Calc_Bridge& bridge,
    std::stop_token st,
    const std::vector<ForceApplied>& force
  ) {
    if (st.stop_requested()) return;
    m_forceVec.assign(m_nodes.size() * 3, 0.0);

    std::size_t skipped = 0;
    for (const auto& load : force) {
      const std::uint32_t nodeId = load.getApliedNode();
      if (nodeId >= m_nodes.size()) {
        ++skipped;
        continue;
      }
      const auto& value = load.getForce();
      for (std::size_t axis = 0; axis < 3; ++axis) {
        m_forceVec[3U * nodeId + axis] += value[axis];
      }
    }
    if (skipped > 0) anaf::LOG::warn("{} loads reference missing nodes and were skipped", skipped);
    anaf::LOG::info("Applied {} nodal loads", force.size() - skipped);
    bridge.m_progress = 0.25f;
  }

  void Truss_Imported_or_Entered::setContainer(anaf::BRIDGE::Gui_Calc_Bridge& bridge, std::stop_token st) {
    if (st.stop_requested()) return;
    m_container.set(
      m_forceVec,
      std::span<Node>{m_nodes.data(), m_nodes.size()},
      std::span<TrussElement_1D>{m_elements.data(), m_elements.size()}
    );
    bridge.m_progress = 0.30f;
  }

  void Truss_Imported_or_Entered::calculate(
    anaf::BRIDGE::Gui_Calc_Bridge& bridge,
    std::stop_token st,
    std::span<const anaf::MATERIAL::Material> materials
  ) {
    detail::runStaticSolve(bridge, st, m_container, m_nodes, m_elements, materials);
  }

  std::shared_ptr<anaf::BRIDGE::MeshData> Truss_Imported_or_Entered::buildResultMesh(
    const anaf::BRIDGE::MeshData& mesh,
    std::span<const anaf::MATERIAL::Material> materials
  ) const {
    auto result = std::make_shared<anaf::BRIDGE::MeshData>(mesh);
    for (std::size_t i = 0; i < m_nodes.size() && i < result->trussNodes.size(); ++i) {
      result->trussNodes[i].setDisplacements(m_nodes[i].getDisplacement());
    }
    for (auto& element : result->trussElements) {
      element.stress = 0.0f;
      element.isStressExceeded = false;
    }
    for (std::size_t b = 0; b < m_elements.size(); ++b) {
      const auto& solved = m_elements[b];
      auto& target = result->trussElements[m_renderIndex[b]];
      target.stress = static_cast<float>(solved.getEleStress());
      target.isStressExceeded = std::abs(solved.getEleStress()) > materials[solved.getEleProperties()].getYieldTensile();
    }
    result->hasResults = true;
    return result;
  }

} // namespace FEM::TRUSS end
