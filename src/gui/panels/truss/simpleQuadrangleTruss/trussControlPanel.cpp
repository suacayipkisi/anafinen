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

#include "trussControlPanel.hpp"

#include "imgui.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <exception>
#include <limits>
#include <memory>
#include <mutex>
#include <stop_token>
#include <thread>

#include <bridge/generalStatus.hpp>
#include <log/anaf_info.hpp>
#include <panels/truss/materialCombo.hpp>
#include <panels/truss/trussWorker.hpp>
#include <truss_1D/trussEngine/trussSolver.hpp>

namespace anaf::GUI {

  namespace {
    using TRUSS_WORKER::configureOpenMPForWorker;

    // Elements and the solver take the material as an index into allMaterials.
    bool resolveMaterialIndex(BRIDGE::Gui_Calc_Bridge& bridge, std::uint32_t materialID, std::uint32_t& index) {
      std::lock_guard lock(bridge.dataMutex);
      const auto found = bridge.findMaterialIndex(materialID);
      if (!found) {
        anaf::LOG::error("No material selected (material library empty or not loaded)");
        return false;
      }
      index = *found;
      return true;
    }

    // Corner supports of the 10x1x10 demo grid (self weight only). Called right after
    // resetModel(), so there is no snapshot yet: Generate Preview builds it. Caller holds dataMutex.
    void ensureDemoTrussCase(BRIDGE::Gui_Calc_Bridge& bridge, std::uint32_t selectedNode) {
      bridge.fixedDOFsByNode.clear();
      bridge.fixedDOFsByNode[0u] = {true, true, true};
      bridge.fixedDOFsByNode[10u] = {true, true, true};
      bridge.fixedDOFsByNode[220u] = {true, true, true};
      bridge.fixedDOFsByNode[230u] = {true, true, true};
      bridge.selectedNodeId = selectedNode;
    }
  }

  void TrussControlPanel::resetState() {
    m_cubeNumX = 10;
    m_cubeNumY = 1;
    m_cubeNumZ = 10;
    m_materialID = 1;
    m_cubeEdgeLength = 1.0;
    m_crossSectionalArea = 80.0;
    m_forceNodeId = 126;
    m_forceVector = {0.0, 10000.0, 0.0};
    m_appliedForces.clear();
    m_fixed = {false, false, false};
    m_fixChanged = false;
    m_lastFixNode = std::numeric_limits<std::uint32_t>::max();
  }

  void TrussControlPanel::onImGuiRender() {
    BRIDGE::Gui_Calc_Bridge& bridge = BRIDGE::buildBridge();
    std::uint32_t materialIndex{};

    ImGui::Begin("Truss(1D) Analysis Set", &isOpen);

    ImGui::Text("Truss Parameters");
    ImGui::Separator();

    std::uint32_t currentSelectedNode = std::numeric_limits<std::uint32_t>::max();
    {
      std::lock_guard lock(bridge.dataMutex);
      currentSelectedNode = bridge.selectedNodeId;
    }

    if (ImGui::BeginTable("TrussParamsTable", 2, ImGuiTableFlags_SizingStretchProp)) {
      ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthStretch, 0.6f);
      ImGui::TableSetupColumn("Control", ImGuiTableColumnFlags_WidthStretch, 0.3f);

      ImGui::TableNextRow();
      ImGui::TableSetColumnIndex(0);
      ImGui::AlignTextToFramePadding();
      ImGui::Text("Cube Number X (N)");
      ImGui::TableSetColumnIndex(1);
      ImGui::SetNextItemWidth(-FLT_MIN);
      ImGui::InputScalar("##cube_x", ImGuiDataType_U32, &m_cubeNumX);

      ImGui::TableNextRow();
      ImGui::TableSetColumnIndex(0);
      ImGui::AlignTextToFramePadding();
      ImGui::Text("Cube Number Y (N)");
      ImGui::TableSetColumnIndex(1);
      ImGui::SetNextItemWidth(-FLT_MIN);
      ImGui::InputScalar("##cube_y", ImGuiDataType_U32, &m_cubeNumY);

      ImGui::TableNextRow();
      ImGui::TableSetColumnIndex(0);
      ImGui::AlignTextToFramePadding();
      ImGui::Text("Cube Number Z (N)");
      ImGui::TableSetColumnIndex(1);
      ImGui::SetNextItemWidth(-FLT_MIN);
      ImGui::InputScalar("##cube_z", ImGuiDataType_U32, &m_cubeNumZ);

      ImGui::EndTable();
    }

    ImGui::Dummy(ImVec2(0.0f, 0.0f));

    if (ImGui::BeginTable("Material Type Table", 2, ImGuiTableFlags_SizingStretchProp)) {
      ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthStretch, 0.3f);
      ImGui::TableSetupColumn("Control", ImGuiTableColumnFlags_WidthStretch, 0.6f);

      ImGui::TableNextRow();
      ImGui::TableSetColumnIndex(0);
      ImGui::AlignTextToFramePadding();
      ImGui::Text("Material Type");
      ImGui::TableSetColumnIndex(1);
      ImGui::SetNextItemWidth(-FLT_MIN);
      materialCombo(bridge, "##Material TypeCombo", m_materialID);

      ImGui::EndTable();
    }
    
    if (ImGui::Button("Open Material Handler", ImVec2(-1.0f, 0.0f)) && onOpenMaterialHandler) {
      onOpenMaterialHandler();
    }

    ImGui::Dummy(ImVec2(0.0f, 0.0f));

    if (ImGui::BeginTable("Material Table", 2, ImGuiTableFlags_SizingStretchProp)) {
      ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthStretch, 0.6f);
      ImGui::TableSetupColumn("Control", ImGuiTableColumnFlags_WidthStretch, 0.3f);

      ImGui::TableNextRow();
      ImGui::TableSetColumnIndex(0);
      ImGui::AlignTextToFramePadding();
      ImGui::Text("Element Length (m)");
      ImGui::TableSetColumnIndex(1);
      ImGui::SetNextItemWidth(-FLT_MIN);
      ImGui::InputDouble("##edge_length", &m_cubeEdgeLength, 0.0, 0.0, "%.2f");

      ImGui::TableNextRow();
      ImGui::TableSetColumnIndex(0);
      ImGui::AlignTextToFramePadding();
      ImGui::Text("Cross-Sectional Area");
      ImGui::TableSetColumnIndex(1);
      ImGui::SetNextItemWidth(-FLT_MIN);
      ImGui::InputDouble("##cross_area", &m_crossSectionalArea, 0.0, 0.0, "%.3f");

      ImGui::EndTable();
    }

    ImGui::Separator();

    if (ImGui::Button("Load Demo (10x1x10 self weight)")) {
      bridge.resetModel(BRIDGE::ObjectType::truss_SQPT);
      resetState(); // the demo grid, section and load node are the panel defaults
      m_forceVector = {0.0, 0.0, 0.0};

      {
        std::lock_guard lock(bridge.dataMutex);
        if (!bridge.allMaterials.empty()) {
          m_materialID = bridge.allMaterials[bridge.allMaterials.size() > 1 ? 1 : 0].getMaterialID();
        }
        ensureDemoTrussCase(bridge, m_forceNodeId);
      }
      bridge.dataVersion.fetch_add(1, std::memory_order_release);
    }

    if (bridge.m_isGeneratingPreview.load()) {
      ImGui::BeginDisabled();
      ImGui::Button("Generating Preview...", ImVec2(-1, 32));
      ImGui::EndDisabled();
    }
    else if (ImGui::Button("Generate Preview", ImVec2(-1, 32)) && resolveMaterialIndex(bridge, m_materialID, materialIndex)) {
      bridge.joinWorker();
      bridge.m_isGeneratingPreview = true;
      bridge.workerThread = std::jthread(
        [&bridge,
         generation = bridge.modelGeneration.load(),
         cubeNumX = m_cubeNumX,
         cubeNumY = m_cubeNumY,
         cubeNumZ = m_cubeNumZ,
         cubeEdgeLength = m_cubeEdgeLength,
         crossSectionalArea = m_crossSectionalArea,
         type = materialIndex,
         appliedForces = m_appliedForces](std::stop_token st) mutable {
          try {
            configureOpenMPForWorker();
            // Same units as the solver (cm^2 in the panel, m^2 in the model).
            FEM::TRUSS::SimpleTruss preview{{cubeNumX, cubeNumY, cubeNumZ}, cubeEdgeLength, crossSectionalArea * 1e-4, type};
            if (const auto built = preview.setTruss(); !built) {
              anaf::LOG::error("Preview not generated: {}", built.error());
              bridge.m_isGeneratingPreview = false;
              return;
            }
            if (st.stop_requested()) {
              bridge.m_isGeneratingPreview = false;
              return;
            }

            auto newMesh = std::make_shared<BRIDGE::MeshData>();
            newMesh->trussNodes.assign(preview.getNodes().begin(), preview.getNodes().end());
            newMesh->trussElements.reserve(preview.getElements().size());
            for (const auto& element : preview.getElements()) {
              const auto& nodes = element.getEleNodes();
              newMesh->trussElements.push_back({nodes[0], nodes[1], 0.0f, false, element.getEleProperties(), element.getEleCrossSection(), false});
            }
            newMesh->appliedForces = appliedForces;

            {
              std::lock_guard lock(bridge.dataMutex);
              if (bridge.modelGeneration.load() != generation) {
                bridge.m_isGeneratingPreview = false;
                return; // the model was reset while the preview was built
              }
              for (auto& node : newMesh->trussNodes) {
                std::array<bool, 3> movable{true, true, true};
                const auto it = bridge.fixedDOFsByNode.find(node.getNodeID());
                if (it != bridge.fixedDOFsByNode.end()) {
                  movable = { !it->second[0], !it->second[1], !it->second[2] };
                }
                node.setMovable(movable);
              }
              if (bridge.activeMesh) {
                newMesh->deformScale.store(
                  bridge.activeMesh->deformScale.load(std::memory_order_relaxed),
                  std::memory_order_relaxed);
              }
              bridge.activeMesh = std::move(newMesh);
              bridge.hasTrussPreview = true;
              bridge.selectedNodeId = 0u;
            }
            bridge.dataVersion.fetch_add(1, std::memory_order_release);
          } catch (const std::exception&) {
            std::lock_guard lock(bridge.dataMutex);
            bridge.activeMesh = nullptr;
            bridge.hasTrussPreview = false;
            bridge.dataVersion.fetch_add(1, std::memory_order_release);
          }

          bridge.m_isGeneratingPreview = false;
      });
    }

    ImGui::Separator();
    ImGui::Text("Node Forces & Constraints");

    if (currentSelectedNode != std::numeric_limits<std::uint32_t>::max()) {
      m_forceNodeId = currentSelectedNode;
    }

    ImGui::InputScalar("Node ID##force_node", ImGuiDataType_U32, &m_forceNodeId);
    ImGui::InputDouble("Fx##force_fx", &m_forceVector[0], 0.0, 0.0, "%.3f");
    ImGui::InputDouble("Fy##force_fy", &m_forceVector[1], 0.0, 0.0, "%.3f");
    ImGui::InputDouble("Fz##force_fz", &m_forceVector[2], 0.0, 0.0, "%.3f");

    if (ImGui::Button("Apply Load to Selected Node")) {
      m_appliedForces.erase(
        std::remove_if(m_appliedForces.begin(), m_appliedForces.end(),
          [&](const FEM::TRUSS::ForceApplied& f) { return f.getAppliedNode() == m_forceNodeId; }),
        m_appliedForces.end());
      m_appliedForces.emplace_back(m_forceNodeId, m_forceVector);

      {
        std::lock_guard lock(bridge.dataMutex);
        if (bridge.activeMesh) {
          auto updatedMesh = std::make_shared<BRIDGE::MeshData>(*bridge.activeMesh);
          updatedMesh->appliedForces = m_appliedForces;
          bridge.activeMesh = std::move(updatedMesh);
        }
      }
      bridge.dataVersion.fetch_add(1, std::memory_order_release);
    }

    std::array<bool, 3> fixedDOFs = {false, false, false};
    {
      std::lock_guard lock(bridge.dataMutex);
      const auto it = bridge.fixedDOFsByNode.find(m_forceNodeId);
      if (it != bridge.fixedDOFsByNode.end()) {
        fixedDOFs = it->second;
      }
    }

    // Panel members, not statics: resetState() clears them when the object type changes.
    {
      std::lock_guard lock(bridge.dataMutex);
      if (m_lastFixNode != currentSelectedNode) {
        m_fixed = fixedDOFs;
      }

      if (ImGui::Checkbox("Fix X##fix_x", &m_fixed[0])) m_fixChanged = true;
      if (ImGui::Checkbox("Fix Y##fix_y", &m_fixed[1])) m_fixChanged = true;
      if (ImGui::Checkbox("Fix Z##fix_z", &m_fixed[2])) m_fixChanged = true;

      if (ImGui::Button("Apply Fixity")) {
        if (m_fixChanged) {
          bridge.fixedDOFsByNode[m_forceNodeId] = m_fixed;
        }
      }
      m_lastFixNode = currentSelectedNode;
    }

    ImGui::SetNextItemWidth(160.0f);
    double currentScale = 1.0;
    {
      std::lock_guard lock(bridge.dataMutex);
      if (bridge.activeMesh) {
        currentScale = bridge.activeMesh->deformScale;
      }
    }

    if (ImGui::InputDouble("Deformation Scale", &currentScale, 0.0, 0.0, "%.3f")) {
      {
        std::lock_guard<std::mutex> lock(bridge.dataMutex);
        if (bridge.activeMesh) {
          auto updatedMesh = std::make_shared<BRIDGE::MeshData>(*bridge.activeMesh);
          updatedMesh->deformScale = currentScale;
          bridge.activeMesh = std::move(updatedMesh);
        }
      }
      bridge.dataVersion.fetch_add(1, std::memory_order_release);
    }

    if (bridge.m_isRunning) {
      ImGui::ProgressBar(bridge.m_progress.load(), ImVec2(0.0f, 0.0f));
      ImGui::BeginDisabled();
      ImGui::Button("Calculating");
      ImGui::EndDisabled();
    }
    else if (ImGui::Button("Run Solver for Truss", ImVec2(-1, 32)) && resolveMaterialIndex(bridge, m_materialID, materialIndex)) {
      bridge.joinWorker();
      bridge.m_isRunning = true;
      bridge.m_progress = 0.0f;

      // The worker gets its own copies: the GUI thread keeps editing the bridge
      // (Apply Fixity, materials) while the solve runs.
      BRIDGE::FixedDOFMap fixedDOFsSnapshot;
      std::vector<anaf::MATERIAL::Material> materialsSnapshot;
      {
        std::lock_guard lock(bridge.dataMutex);
        fixedDOFsSnapshot = bridge.fixedDOFsByNode;
        materialsSnapshot = bridge.allMaterials;
      }

      bridge.workerThread = std::jthread(
        [&bridge,
         generation = bridge.modelGeneration.load(),
         fixedDOFs = std::move(fixedDOFsSnapshot),
         allMaterials = std::move(materialsSnapshot),
         cubeNumX = m_cubeNumX,
         cubeNumY = m_cubeNumY,
         cubeNumZ = m_cubeNumZ,
         cubeEdgeLength = m_cubeEdgeLength,
         crossSectionalArea = m_crossSectionalArea,
         type = materialIndex,
         deformScale = currentScale,
         forcesToApply = m_appliedForces
        ](std::stop_token st) mutable {
          try {
            configureOpenMPForWorker();

            FEM::TRUSS::Truss_SQPT solver{
              cubeNumX,
              cubeNumY,
              cubeNumZ,
              cubeEdgeLength,
              crossSectionalArea,
              type
            };

            if (const auto built = solver.trussSetAndSetFix_SQPT(bridge, st, fixedDOFs); !built) {
              anaf::LOG::error("Solver not started: {}", built.error());
              bridge.m_progress = 0.0f;
              bridge.m_isRunning = false;
              return;
            }
            solver.trussSetForce_SQPT(bridge, st, forcesToApply);
            solver.setContainer(bridge, st);
            solver.calculate(bridge, st, allMaterials);
            if (st.stop_requested()) {
              bridge.m_progress = 0.0f;
              bridge.m_isRunning = false;
              return;
            }

            // send solved nodes and elements into new snapshot
            auto newMesh = std::make_shared<BRIDGE::MeshData>();
            newMesh->trussNodes.assign(solver.getNodes().begin(), solver.getNodes().end());
            newMesh->trussElements.reserve(solver.getElements().size());
            for (const auto& element : solver.getElements()) {
              const auto& nodes = element.getEleNodes();
              const auto& material = allMaterials[element.getEleProperties()];
              const bool isStressExceeded =
                std::abs(element.getEleStress()) > material.getYieldTensile();
              newMesh->trussElements.push_back({
                nodes[0],
                nodes[1],
                static_cast<float>(element.getEleStress()),
                isStressExceeded,
                element.getEleProperties(),
                element.getEleCrossSection(),
                false
              });
            }
            newMesh->appliedForces = forcesToApply;
            newMesh->deformScale = deformScale;
            newMesh->hasResults = true;

            // apply the boundary conditions this solve used into nodes for overlay draw
            for (auto& node : newMesh->trussNodes) {
              std::array<bool, 3> movable{true, true, true};
              const auto it = fixedDOFs.find(node.getNodeID());
              if (it != fixedDOFs.end()) {
                movable = { !it->second[0], !it->second[1], !it->second[2] };
              }
              node.setMovable(movable);
            }

            bool published = false;
            {
              std::lock_guard lock(bridge.dataMutex);
              if (bridge.modelGeneration.load() == generation) { // not reset while solving
                bridge.activeMesh = std::move(newMesh);
                bridge.hasTrussPreview = true;
                published = true;
              }
            }

            // send signal to gui to draw scene
            if (published) bridge.dataVersion.fetch_add(1, std::memory_order_release);

          } catch (const std::exception& exception) {
            anaf::LOG::error("Solver failed: {}", exception.what());
          }

          bridge.m_progress = 1.0f;
          bridge.m_isRunning = false;
      });
    }

    if (ImGui::Button("Clear All", ImVec2(-1, 32))) {
      bridge.resetModel(BRIDGE::ObjectType::truss_SQPT);
      resetState();
      m_cubeNumX = 1;
      m_cubeNumY = 1;
      m_cubeNumZ = 1;
      m_materialID = 0;
      m_forceNodeId = 0;
      m_forceVector = {0.0, 0.0, 0.0};
    }

    ImGui::End();
  }

} // namespace anaf::GUI end
