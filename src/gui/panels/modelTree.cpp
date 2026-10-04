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

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>

namespace anaf::GUI {

  namespace {
    // Draws only the rows of a count-row list that are inside the scrolled child window.
    template <typename DrawRow>
    void clippedRows(const std::size_t count, DrawRow&& drawRow) {
      ImGuiListClipper clipper;
      clipper.Begin(static_cast<int>(count));
      while (clipper.Step()) {
        for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) drawRow(static_cast<std::size_t>(row));
      }
    }
  } // namespace end

  void ModelTree::renderMeshTree(anaf::BRIDGE::Gui_Calc_Bridge& bridge) {
    // Snapshots are immutable: copying the pointer under the lock is enough (no deep copy per frame).
    std::shared_ptr<const BRIDGE::MeshData> meshData;
    {
      std::lock_guard lock(bridge.dataMutex);
      meshData = bridge.activeMesh;
    }
    if (meshData != m_indexedMesh) {
      m_indexedMesh = meshData;
      m_supportedNodes.clear();
      m_overstressedBars.clear();
      if (meshData) {
        for (std::uint32_t i = 0; i < meshData->trussNodes.size(); ++i) {
          if (meshData->trussNodes[i].isSupported()) m_supportedNodes.push_back(i);
        }
        for (std::uint32_t e = 0; e < meshData->trussElements.size(); ++e) {
          if (meshData->trussElements[e].isStressExceeded) m_overstressedBars.push_back(e);
        }
      }
    }

    if (ImGui::TreeNode("Boundary Conditions (fix and forces)")) {
      ImGui::Text("Applied Fixity");
      if (m_supportedNodes.empty()) {
        ImGui::TextDisabled("No fixed DOFs yet...");
      } else {
        ImGui::BeginChild("Applied Fixity List", ImVec2(0, 110), true);
        clippedRows(m_supportedNodes.size(), [&](const std::size_t row) {
          const auto& node = meshData->trussNodes[m_supportedNodes[row]];
          if (node.hasInclinedSupport()) {
            const auto& allowed = node.getAllowedMotionDirections();
            ImGui::Text("Node %u: inclined, moves %s", node.getNodeID(),
                        allowed.empty() ? "nowhere" : (allowed.size() == 1 ? "along a line" : "on a plane"));
            return;
          }
          const auto& movable = node.getMovable();
          ImGui::Text("Node %u: X=%s, Y=%s, Z=%s", node.getNodeID(),
            movable[0] ? "free" : "fixed",
            movable[1] ? "free" : "fixed",
            movable[2] ? "free" : "fixed"
          );
        });
        ImGui::EndChild();
      }

      ImGui::Text("Applied Forces");
      if (!meshData || meshData->appliedForces.empty()) {
        ImGui::TextDisabled("No applied forces yet...");
      } else {
        ImGui::BeginChild("AppliedForceList", ImVec2(0, 110), true);
        clippedRows(meshData->appliedForces.size(), [&](const std::size_t row) {
          const auto& force = meshData->appliedForces[row];
          ImGui::Text("Node %u: Fx=%.3f, Fy=%.3f, Fz=%.3f",
            force.getAppliedNode(),
            force.getForce()[0],
            force.getForce()[1],
            force.getForce()[2]);
        });
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
        if (m_overstressedBars.empty()) {
          ImGui::TextDisabled("No elements exceeded max stress");
        } else {
          // |stress| above the material's yield strength, in red (sign: tension > 0)
          ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
          clippedRows(m_overstressedBars.size(), [&](const std::size_t row) {
            const std::uint32_t e = m_overstressedBars[row];
            ImGui::Text("%u: %.3f(MPa)", e, static_cast<double>(meshData->trussElements[e].stress) / 1e6);
          });
          ImGui::PopStyleColor();
        }
        ImGui::EndChild();
      }

      ImGui::Text("NodeNum / disp");
      if (!meshData || meshData->trussNodes.empty()) {
        ImGui::TextDisabled("No nodes yet...");
      } else {
        ImGui::BeginChild("NodeList", ImVec2(0, 200), true);
        clippedRows(meshData->trussNodes.size(), [&](const std::size_t row) {
          const auto& disp = meshData->trussNodes[row].getDisplacement();
          const double nodeDisp = std::sqrt(disp[0] * disp[0] + disp[1] * disp[1] + disp[2] * disp[2]);
          ImGui::Text("%u: %.3f(mm)", static_cast<unsigned>(row), nodeDisp * 1e3);
        });
        ImGui::EndChild();
      }

      ImGui::TreePop();
    }
  }

  void ModelTree::renderBeamTree(anaf::BRIDGE::Gui_Calc_Bridge& bridge) {
    std::shared_ptr<const BRIDGE::BeamMeshData> mesh;
    {
      std::lock_guard lock(bridge.dataMutex);
      mesh = bridge.activeBeamMesh;
    }
    if (mesh != m_indexedBeamMesh) {
      m_indexedBeamMesh = mesh;
      m_supportedBeamNodes.clear();
      m_overstressedBeams.clear();
      if (mesh) {
        for (std::uint32_t i = 0; i < mesh->nodes.size(); ++i) {
          if (mesh->nodes[i].isSupported()) m_supportedBeamNodes.push_back(i);
        }
        for (std::uint32_t e = 0; e < mesh->elements.size(); ++e) {
          if (mesh->elements[e].stress.isStressExceeded) m_overstressedBeams.push_back(e);
        }
      }
    }
    if (!mesh) {
      ImGui::TextDisabled("No beam model yet...");
      return;
    }

    if (ImGui::TreeNode("Supports and Loads")) {
      ImGui::Text("Supported nodes (fixed DOFs)");
      if (m_supportedBeamNodes.empty()) {
        ImGui::TextDisabled("No supports yet...");
      } else {
        ImGui::BeginChild("BeamSupportList", ImVec2(0, 110), true);
        clippedRows(m_supportedBeamNodes.size(), [&](const std::size_t row) {
          const auto& node = mesh->nodes[m_supportedBeamNodes[row]];
          ImGui::Text("Node %u: %zu of 3 translations, %zu of 3 rotations free", node.getNodeID(),
                      node.getAllowedMotionDirections().size(), node.getAllowedRotationAxes().size());
        });
        ImGui::EndChild();
      }
      ImGui::Text("Nodal loads");
      if (mesh->nodalLoads.empty()) {
        ImGui::TextDisabled("No nodal loads yet...");
      } else {
        ImGui::BeginChild("BeamNodalLoadList", ImVec2(0, 90), true);
        clippedRows(mesh->nodalLoads.size(), [&](const std::size_t row) {
          const auto& load = mesh->nodalLoads[row];
          ImGui::Text("Node %u: F=(%.3g, %.3g, %.3g) N, M=(%.3g, %.3g, %.3g) N m", load.node, load.force[0], load.force[1],
                      load.force[2], load.moment[0], load.moment[1], load.moment[2]);
        });
        ImGui::EndChild();
      }
      ImGui::Text("Distributed loads");
      if (mesh->distributedLoads.empty()) {
        ImGui::TextDisabled("No distributed loads yet...");
      } else {
        ImGui::BeginChild("BeamDistributedLoadList", ImVec2(0, 90), true);
        clippedRows(mesh->distributedLoads.size(), [&](const std::size_t row) {
          const auto& load = mesh->distributedLoads[row];
          ImGui::Text("Element %u: q=(%.3g, %.3g, %.3g) N/m %s", load.element, load.value[0], load.value[1], load.value[2],
                      load.frame == FEM::BEAM::LoadFrame::Local ? "local" : "global");
        });
        ImGui::EndChild();
      }
      ImGui::TextDisabled("Self weight: %s", mesh->gravity == std::array<double, 3>{} ? "off" : "on");
      ImGui::TreePop();
    }

    if (ImGui::TreeNode("Beam Elements")) {
      ImGui::Text("Element / max von Mises");
      if (mesh->elements.empty()) {
        ImGui::TextDisabled("No element yet...");
      } else if (!mesh->hasResults) {
        ImGui::TextDisabled("No results yet (run the solver)");
      } else {
        if (m_overstressedBeams.empty()) ImGui::TextDisabled("No element exceeds its yield strength");
        ImGui::BeginChild("BeamElementStressList", ImVec2(0, 160), true);
        clippedRows(mesh->elements.size(), [&](const std::size_t row) {
          const auto& stress = mesh->elements[row].stress;
          if (!stress.available) {
            ImGui::TextDisabled("%zu: general section, no stress", row);
            return;
          }
          if (stress.isStressExceeded) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
          ImGui::Text("%zu: %.3f (MPa)", row, stress.maxVonMises / 1e6);
          if (stress.isStressExceeded) ImGui::PopStyleColor();
        });
        ImGui::EndChild();
      }

      ImGui::Text("Node / displacement, rotation");
      ImGui::BeginChild("BeamNodeResultList", ImVec2(0, 160), true);
      clippedRows(mesh->nodes.size(), [&](const std::size_t row) {
        const auto& d = mesh->nodes[row].getDisplacement();
        const auto& r = mesh->nodes[row].getRotation();
        ImGui::Text("%zu: %.3f (mm), %.3f (mrad)", row, std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]) * 1e3,
                    std::sqrt(r[0] * r[0] + r[1] * r[1] + r[2] * r[2]) * 1e3);
      });
      ImGui::EndChild();
      ImGui::TreePop();
    }
  }

  void ModelTree::onImGuiRender() {
    auto& bridge = anaf::BRIDGE::buildBridge();

    ImGui::Begin("Model Tree", &isOpen);

    // Read every frame, so the tree follows a type change (and its reset) immediately.
    const anaf::BRIDGE::ObjectType latestType = bridge.m_objectType.load();
    std::string rootLabel = latestType == anaf::BRIDGE::ObjectType::no_type
      ? std::string("Root Assembly")
      : std::string(anaf::BRIDGE::getObjectTypeName(latestType));
    rootLabel += "###ModelTreeRoot"; // same ImGui ID (open state) for every type

    if (ImGui::TreeNode(rootLabel.c_str())) {
      if (latestType == anaf::BRIDGE::ObjectType::beam_frame) renderBeamTree(bridge);
      else if (latestType != anaf::BRIDGE::ObjectType::no_type) renderMeshTree(bridge);
      ImGui::TreePop();
    }
    ImGui::End();
  }

} // namespace anaf::GUI end
