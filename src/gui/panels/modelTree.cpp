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

#include "modelTree.hpp"

#include "imgui.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>

namespace anaf::GUI {

  void ModelTree::renderMeshTree(anaf::BRIDGE::Gui_Calc_Bridge& bridge) {
    // Snapshots are immutable: copying the pointer under the lock is enough (no deep copy per frame).
    std::shared_ptr<const BRIDGE::MeshData> meshData;
    {
      std::lock_guard lock(bridge.dataMutex);
      meshData = bridge.activeMesh;
    }

    if (ImGui::TreeNode("Boundary Conditions (fix and forces)")) {
      ImGui::Text("Applied Fixity");
      const auto supported = meshData
        ? std::ranges::count_if(meshData->trussNodes, [](const FEM::TRUSS::Node& node) { return node.isSupported(); })
        : 0;
      if (supported == 0) {
        ImGui::TextDisabled("No fixed DOFs yet...");
      } else {
        ImGui::BeginChild("Applied Fixity List", ImVec2(0, 110), true);
        for (const auto& node : meshData->trussNodes) {
          if (!node.isSupported()) continue;
          if (node.hasInclinedSupport()) {
            const auto& allowed = node.getAllowedMotionDirections();
            ImGui::Text("Node %u: inclined, moves %s", node.getNodeID(),
                        allowed.empty() ? "nowhere" : (allowed.size() == 1 ? "along a line" : "on a plane"));
            continue;
          }
          const auto& movable = node.getMovable();
          ImGui::Text("Node %u: X=%s, Y=%s, Z=%s", node.getNodeID(),
            movable[0] ? "free" : "fixed",
            movable[1] ? "free" : "fixed",
            movable[2] ? "free" : "fixed"
          );
        }
        ImGui::EndChild();
      }

      ImGui::Text("Applied Forces");
      if (!meshData || meshData->appliedForces.empty()) {
        ImGui::TextDisabled("No applied forces yet...");
      } else {
        ImGui::BeginChild("AppliedForceList", ImVec2(0, 110), true);
        for (const auto& force : meshData->appliedForces) {
          ImGui::Text("Node %u: Fx=%.3f, Fy=%.3f, Fz=%.3f",
            force.getAppliedNode(),
            force.getForce()[0],
            force.getForce()[1],
            force.getForce()[2]);
        }
        ImGui::EndChild();
      }

      ImGui::TreePop();
    }

    if (ImGui::TreeNode("Truss Elements")) {
      ImGui::Text("ElementNum / stress");
      if (!meshData || meshData->trussElements.empty()) {
        ImGui::TextDisabled("No element yet...");
      } else {
        ImGui::BeginChild("ElementList", ImVec2(0, 200), true);
        std::uint32_t eleNum = 0;
        std::uint32_t exceedStressEleNum = 0;
        for (const auto& element : meshData->trussElements) {
          // |stress| above the material's yield strength: render in red (sign: tension > 0)
          if (element.isStressExceeded) {
            ++exceedStressEleNum;
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
            ImGui::Text("%u: %.3f(MPa)", eleNum, element.stress / 1e6);
            ImGui::PopStyleColor();
          }
          ++eleNum;
        }
        if (exceedStressEleNum == 0) {
          ImGui::TextDisabled("No elements exceeded max stress");
        }
        ImGui::EndChild();
      }

      ImGui::Text("NodeNum / disp");
      if (!meshData || meshData->trussNodes.empty()) {
        ImGui::TextDisabled("No nodes yet...");
      } else {
        ImGui::BeginChild("NodeList", ImVec2(0, 200), true);
        std::uint32_t nodeNum = 0;
        for (const auto& node : meshData->trussNodes) {
          const auto& disp = node.getDisplacement();
          const double nodeDisp = std::sqrt(disp[0] * disp[0] + disp[1] * disp[1] + disp[2] * disp[2]);
          ImGui::Text("%u: %.3f(mm)", nodeNum, nodeDisp * 1e3);
          ++nodeNum;
        }
        ImGui::EndChild();
      }

      ImGui::TreePop();
    }
  }

  void ModelTree::onImGuiRender() {
    auto& bridge = anaf::BRIDGE::buildBridge();

    ImGui::Begin("Model Tree");

    // Read every frame, so the tree follows a type change (and its reset) immediately.
    const anaf::BRIDGE::ObjectType latestType = bridge.m_objectType.load();
    std::string rootLabel = latestType == anaf::BRIDGE::ObjectType::no_type
      ? std::string("Root Assembly")
      : std::string(anaf::BRIDGE::getObjectTypeName(latestType));
    rootLabel += "###ModelTreeRoot"; // same ImGui ID (open state) for every type

    if (ImGui::TreeNode(rootLabel.c_str())) {
      if (latestType != anaf::BRIDGE::ObjectType::no_type) renderMeshTree(bridge);
      ImGui::TreePop();
    }
    ImGui::End();
  }

} // namespace anaf::GUI end
