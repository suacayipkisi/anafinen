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

#include "anaf_info.hpp"
#include "generalStatus.hpp"
#include "imgui.h"
#include <array>
#include <cmath>
#include <memory>

namespace anaf::GUI {

  bool ModelTree::createModelTree_truss_SQPT(anaf::BRIDGE::Gui_Calc_Bridge& bridge){
    // Snapshots are immutable: copying the pointer under the lock is enough (no deep copy per frame).
    std::shared_ptr<const BRIDGE::MeshData> meshData;
    {
      std::lock_guard lock(bridge.dataMutex);
      meshData = bridge.activeMesh;
    }

    if (ImGui::TreeNode("Boundary Conditions (fix and forces)")) {
      ImGui::Text("Applied Fixity");
      {
        std::lock_guard lock(bridge.dataMutex);
        if (bridge.fixedDOFsByNode.empty()) {
          ImGui::TextDisabled("No fixed DOFs yet...");
        } else {
          ImGui::BeginChild("Applied Fixity List", ImVec2(0, 110), true);
          for (const auto& [nodeId, dofs] : bridge.fixedDOFsByNode) {
            ImGui::Text("Node %u: X=%s, Y=%s, Z=%s",
              nodeId,
              dofs[0] ? "fixed" : "free",
              dofs[1] ? "fixed" : "free",
              dofs[2] ? "fixed" : "free"
            );
          }
          ImGui::EndChild();
        }
      }

      ImGui::Text("Applied Forces");
      {
        std::lock_guard lock(bridge.dataMutex);
        if (!meshData || meshData->appliedForces.empty()) {
          ImGui::TextDisabled("No applied forces yet...");
        } else {
          ImGui::BeginChild("AppliedForceList", ImVec2(0, 110), true);
          for (const auto& force : meshData->appliedForces) {
            ImGui::Text("Node %u: Fx=%.3f, Fy=%.3f, Fz=%.3f",
              force.getApliedNode(),
              force.getForce()[0],
              force.getForce()[1],
              force.getForce()[2]);
          }
          ImGui::EndChild();
        }
      }

      

      ImGui::TreePop();
    }

    if (ImGui::TreeNode("Truss Elements")) {

      ImGui::Text("ElementNum / stress");
      {
        std::lock_guard lock(bridge.dataMutex);
        if (!meshData || meshData->trussElements.empty()) {
          ImGui::TextDisabled("No element yet...");
        }
        else {
          ImGui::BeginChild("ElemetList", ImVec2(0, 200), true);
          std::uint32_t eleNum = 0;
          std::uint32_t exceedStressEleNum = 0;
          for (const auto& elemenet : meshData->trussElements) {
            // |stress| above the material's yield strength: render in red (sign: tension > 0)
            if (elemenet.isStressExceeded) {
              exceedStressEleNum ++;
              ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
              ImGui::Text("%u: %.3f(MPa)", eleNum, elemenet.stress/1e6);
              ImGui::PopStyleColor();
            }
            eleNum ++;
          }
          if(exceedStressEleNum == 0) {
            ImGui::TextDisabled("No elements exceeded max stress");
          }
          ImGui::EndChild();
        }
      }

      ImGui::Text("NodeNum / disp");
      {
        std::lock_guard lock(bridge.dataMutex);
        if(!meshData || meshData->trussNodes.empty()) {
          ImGui::TextDisabled("No nodes yet...");
        }
        else {
          ImGui::BeginChild("NodeList", ImVec2(0, 200), true);
          std::uint32_t nodeNum = 0;
          for (const auto& node : meshData->trussNodes) {
            const auto& nodeDisp3D = node.getDisplacement();
            double nodeDisp{0};
            for(int i = 0; i < 3; ++i) {
              nodeDisp += nodeDisp3D[i] * nodeDisp3D[i];
            }
            nodeDisp = sqrt(nodeDisp);
            ImGui::Text("%u: %.3f(mm)", nodeNum, nodeDisp * 1e3);
            nodeNum++;
          }
          ImGui::EndChild();
        }
      }


      ImGui::TreePop();
    }
    
    return true;
  }

  bool ModelTree::createModelTree_truss_imported_or_entered(anaf::BRIDGE::Gui_Calc_Bridge& bridge){
    // Imported trusses are published as the same snapshot type, so the same tree applies.
    return createModelTree_truss_SQPT(bridge);
  }

  void ModelTree::onImGuiRender() {
    auto& bridge = anaf::BRIDGE::buildBridge();

    ImGui::Begin("Model Tree");

    // Read every frame, so the tree follows a type change (and its reset) immediately.
    const anaf::BRIDGE::ObjectType latest_type = bridge.m_objectType.load();
    std::string a_tempName = latest_type == anaf::BRIDGE::ObjectType::no_type
      ? std::string("Root Assembly")
      : std::string(anaf::BRIDGE::getObjectTypeName(latest_type));
    a_tempName += "###ModelTreeRoot"; // same ImGui ID (open state) for every type

    if (ImGui::TreeNode(a_tempName.c_str())) {
      if(latest_type == anaf::BRIDGE::ObjectType::truss_SQPT && !createModelTree_truss_SQPT(bridge)) {
        anaf::LOG::warn("Model tree for truss_SQPT couldn't created succesfully");
        if(ImGui::TreeNode("Tree is not created")) {
          ImGui::TreePop();
        }
      }
      if(latest_type == anaf::BRIDGE::ObjectType::truss_imported_or_entered && !createModelTree_truss_imported_or_entered(bridge)) {
        anaf::LOG::warn("Model tree for the imported model couldn't be created");
      }
      ImGui::TreePop();
    }
    ImGui::End();
  }

} // namespace anaf::GUI end
