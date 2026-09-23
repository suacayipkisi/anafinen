// Copyright (c) 2026 Ufuk Deniz Konuk
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
#include <memory>

namespace anaf::GUI {

  bool ModelTree::createModelTree_truss_SQPT(anaf::BRIDGE::Gui_Calc_Bridge& bridge){
    std::shared_ptr<BRIDGE::MeshData> meshData;
    if(bridge.activeMesh){
      meshData = std::make_shared<BRIDGE::MeshData>(*bridge.activeMesh);
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
              dofs[2] ? "fixed" : "free");
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

      ImGui::Text("NodeNum / stress");
      {
        std::lock_guard lock(bridge.dataMutex);
        if (!meshData || meshData->trussElements.empty()) {
          ImGui::TextDisabled("No element yet...");
        }
        else {
          ImGui::BeginChild("ElemetList", ImVec2(0, 200), true);
          std::uint32_t eleNum = 0;
          for (const auto& elemenet : meshData->trussElements) {
            // is stress is bigger than elasticity modulus, render it as red colored
            if (elemenet.isStressExceeded) {
              ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
            }
            ImGui::Text("%u: %.3f", eleNum, elemenet.stress);
            if (elemenet.isStressExceeded) {
              ImGui::PopStyleColor();
            }
            eleNum ++;
          }
          ImGui::EndChild();
        }
      }

      ImGui::TreePop();
    }
    
    return true;
  }

  bool ModelTree::createModelTree_truss_imported_or_entered(anaf::BRIDGE::Gui_Calc_Bridge& bridge){
    return true;
  }

  void ModelTree::onImGuiRender() {
    auto& bridge = anaf::BRIDGE::buildBridge();

    ImGui::Begin("Model Tree");

    static anaf::BRIDGE::ObjectType latest_type;
    std::string a_tempName = "Root Assembly";

    if (latest_type || latest_type != bridge.m_objectType) {
      latest_type = bridge.m_objectType;
      const auto objectTypeName = anaf::BRIDGE::getObjectTypeName(latest_type);
      a_tempName.assign(objectTypeName.data(), objectTypeName.size());
    }

    if (ImGui::TreeNode(a_tempName.c_str())) {
      if(latest_type == anaf::BRIDGE::ObjectType::truss_SQPT) {
        if(!createModelTree_truss_SQPT(bridge)) {
          anaf::LOG::warn("Model tree for truss_SQPT couldn't created succesfully");
          if(ImGui::TreeNode("Tree is not created")) {
            ImGui::TreePop();
          }
        }
      }
      ImGui::TreePop();
    }
    ImGui::End();
  }

} // namespace anaf::GUI end
