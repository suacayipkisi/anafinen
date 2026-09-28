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
#include <trussTypes/simpleQuadranglePrismTrussCreate.hpp>
#include <trussProperties/appliedForce.hpp>

#include <array>
#include <cstddef>
#include <format>
#include <stop_token>
#include <string>
#include <vector>

namespace FEM::TRUSS {

  void Truss_SQPT::trussSetAndSetFix_SQPT(
    anaf::BRIDGE::Gui_Calc_Bridge& bridge,
    std::stop_token st,
    const anaf::BRIDGE::FixedDOFMap& fixedDOFsByNode
  ) {
    if (st.stop_requested()) return;
    m_truss.setTruss();

    auto& nodes = m_truss.getNodes();
    #pragma omp parallel for schedule(static)
    for (auto& node : nodes) {
      const auto it = fixedDOFsByNode.find(node.getNodeID());
      if (it != fixedDOFsByNode.end()) {
        node.setMovable({ !it->second[0], !it->second[1], !it->second[2] });
      }
      else {
        node.setMovable({true, true, true});
      }
    }

    // Joined by hand: range formatting of std::vector needs libstdc++ 15 (Debian 13 ships GCC 14).
    std::string fixInfo;
    for (const auto& [nodeId, dofs] : fixedDOFsByNode) {
      if (!fixInfo.empty()) fixInfo += ", ";
      fixInfo += std::format("{}:{}{}{}", nodeId, dofs[0] ? 'x' : '-', dofs[1] ? 'y' : '-', dofs[2] ? 'z' : '-');
    }
    anaf::LOG::info("Fixed nodes [{}]", fixInfo);
    bridge.m_progress = 0.20f;
  }

  void Truss_SQPT::trussSetForce_SQRT(
    anaf::BRIDGE::Gui_Calc_Bridge& bridge,
    std::stop_token st,
    std::vector<ForceApplied> force
  ) {
    if (st.stop_requested()) return;
    m_forceVec.assign(m_truss.getNodeNum() * 3, 0.0);

    for (std::size_t i = 0; i < force.size(); ++i) {
      const std::uint32_t nodeId = force[i].getApliedNode();
      const std::array<double, 3> currentNodeForce = force[i].getForce();

      if (nodeId >= m_truss.getNodeNum()) {
        continue;
      }

      anaf::LOG::info("Apply load to node {}: [{}, {}, {}]", nodeId, currentNodeForce[0], currentNodeForce[1], currentNodeForce[2]);
      for (std::size_t axis = 0; axis < 3; ++axis) {
        m_forceVec[3U * nodeId + axis] = currentNodeForce[axis];
      }
    }
    bridge.m_progress = 0.25f;
  }

  void Truss_SQPT::setContainer(
    anaf::BRIDGE::Gui_Calc_Bridge& bridge,
    std::stop_token st
  ) {
    if (st.stop_requested()) return;
    auto& nodes = m_truss.getNodes();
    auto& elements = m_truss.getElements();
    m_container.set(
      m_forceVec,
      std::span<Node>{nodes.data(), nodes.size()},
      std::span<TrussElement_1D>{elements.data(), elements.size()}
    );
    bridge.m_progress = 0.30f;
  }

  void Truss_SQPT::calculate(
    anaf::BRIDGE::Gui_Calc_Bridge& bridge,
    std::stop_token st,
    std::span<anaf::MATERIAL::Material> materials
  ){
    detail::runStaticSolve(bridge, st, m_container, m_truss.getNodes(), m_truss.getElements(), materials);
  }
  
} // namespace FEM::TRUSS end
