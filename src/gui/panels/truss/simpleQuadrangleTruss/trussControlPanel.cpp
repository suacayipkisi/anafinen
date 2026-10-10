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
#include <expected>
#include <format>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <stop_token>
#include <string>
#include <thread>

#include <bridge/generalStatus.hpp>
#include <log/anaf_info.hpp>
#include <panels/editorLayout.hpp>
#include <panels/truss/materialCombo.hpp>
#include <panels/truss/trussWorker.hpp>
#include <truss_1D/trussTypes/simpleQuadranglePrismTrussCreate.hpp>

namespace anaf::GUI {

  namespace {
    // Elements and the solver take the material as an index into allMaterials.
    bool resolveMaterialIndex(BRIDGE::GuiCalcBridge& bridge, std::uint32_t materialID, std::uint32_t& index) {
      std::lock_guard lock(bridge.dataMutex);
      const auto found = bridge.findMaterialIndex(materialID);
      if (!found) {
        anaf::LOG::error("No material selected (material library empty or not loaded)");
        return false;
      }
      index = *found;
      return true;
    }

    using Supports = std::map<std::uint32_t, std::array<bool, 3>>;

    // The panel's supports on a generated grid (ids outside the grid are ignored).
    void applySupports(BRIDGE::MeshData& mesh, const Supports& supports) {
      for (const auto& [id, fixed] : supports) {
        if (id < mesh.trussNodes.size()) mesh.trussNodes[id].setMovable({!fixed[0], !fixed[1], !fixed[2]});
      }
    }

    // Republishes the active grid with the panel's current loads and supports.
    void publishInputs(BRIDGE::GuiCalcBridge& bridge, const std::vector<FEM::TRUSS::ForceApplied>& forces, const Supports& supports) {
      {
        std::lock_guard lock(bridge.dataMutex);
        if (!bridge.activeMesh) return;
        auto updated = std::make_shared<BRIDGE::MeshData>(*bridge.activeMesh);
        updated->appliedForces = forces;
        for (auto& node : updated->trussNodes) node.setMovable({true, true, true});
        applySupports(*updated, supports);
        bridge.activeMesh = std::move(updated);
      }
      bridge.dataVersion.fetch_add(1, std::memory_order_release);
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
    m_supports.clear();
    m_fixed = {false, false, false};
    m_lastFixNode = std::numeric_limits<std::uint32_t>::max();
  }

  void TrussControlPanel::renderSummary(BRIDGE::GuiCalcBridge& bridge) {
    std::shared_ptr<const BRIDGE::MeshData> mesh;
    {
      std::lock_guard lock(bridge.dataMutex);
      mesh = bridge.activeMesh;
    }
    LAYOUT::beginCard("##sqpt_summary");
    if (LAYOUT::beginStats("##sqpt_stats")) {
      LAYOUT::stat("Grid", std::format("{} x {} x {}", m_cubeNumX, m_cubeNumY, m_cubeNumZ));
      LAYOUT::stat("Bars", mesh ? std::to_string(mesh->trussElements.size()) : "-");
      LAYOUT::stat("Supports", std::to_string(m_supports.size()));
      LAYOUT::stat("Loads", std::to_string(m_appliedForces.size()));
      ImGui::EndTable();
    }
    ImGui::Separator();
    if (!mesh) {
      ImGui::TextDisabled("No preview yet: Generate Preview in the Grid tab.");
    } else if (mesh->hasResults) {
      const bool valid = bridge.isValid.load();
      ImGui::TextColored(valid ? THEME::theme().good : THEME::theme().warn, "%s", valid ? "Solved, energy check passed" : "Results shown (energy check not passed)");
    } else {
      ImGui::TextDisabled("No results yet: run the solver below.");
    }
    LAYOUT::endCard();
  }

  void TrussControlPanel::renderGridTab(BRIDGE::GuiCalcBridge& bridge) {
    ImGui::SeparatorText("Grid");
    LAYOUT::field("Cells X");
    ImGui::InputScalar("##cube_x", ImGuiDataType_U32, &m_cubeNumX);
    LAYOUT::field("Cells Y");
    ImGui::InputScalar("##cube_y", ImGuiDataType_U32, &m_cubeNumY);
    LAYOUT::field("Cells Z");
    ImGui::InputScalar("##cube_z", ImGuiDataType_U32, &m_cubeNumZ);
    LAYOUT::field("Edge [m]");
    ImGui::InputDouble("##edge_length", &m_cubeEdgeLength, 0.0, 0.0, "%.2f");
    ImGui::SetItemTooltip("Edge length of one cell (element length)");

    ImGui::SeparatorText("Material & Section");
    LAYOUT::field("Material");
    materialCombo(bridge, "##Material TypeCombo", m_materialID);
    if (ImGui::Button("Open Material Handler", ImVec2(-FLT_MIN, 0.0f)) && onOpenMaterialHandler) {
      onOpenMaterialHandler();
    }
    LAYOUT::field("Area [cm^2]");
    ImGui::InputDouble("##cross_area", &m_crossSectionalArea, 0.0, 0.0, "%.3f");

    ImGui::Spacing();
    std::uint32_t materialIndex{};
    if (bridge.isGeneratingPreview.load()) {
      ImGui::BeginDisabled();
      ImGui::Button("Generating Preview...", ImVec2(-FLT_MIN, 0.0f));
      ImGui::EndDisabled();
    } else if (ImGui::Button("Generate Preview", ImVec2(-FLT_MIN, 0.0f)) && resolveMaterialIndex(bridge, m_materialID, materialIndex)) {
      startPreview(bridge, materialIndex);
    }
    if (ImGui::Button("Load Demo (10x1x10 self weight)", ImVec2(-FLT_MIN, 0.0f))) {
      bridge.resetModel(BRIDGE::E_ObjectType::TrussSqpt);
      resetState(); // the demo grid, section and load node are the panel defaults
      m_forceVector = {0.0, 0.0, 0.0};

      {
        std::lock_guard lock(bridge.dataMutex);
        if (!bridge.allMaterials.empty()) {
          m_materialID = bridge.allMaterials[bridge.allMaterials.size() > 1 ? 1 : 0].getMaterialID();
        }
        bridge.selectedNodeId = m_forceNodeId;
      }
      // Corner supports of the 10x1x10 demo grid (self weight only); Generate Preview builds it.
      for (const std::uint32_t corner : {0u, 10u, 220u, 230u}) m_supports[corner] = {true, true, true};
      bridge.dataVersion.fetch_add(1, std::memory_order_release);
    }
  }

  void TrussControlPanel::startPreview(BRIDGE::GuiCalcBridge& bridge, const std::uint32_t materialIndex) {
    bridge.joinWorker();
    bridge.isGeneratingPreview = true;
    bridge.workerThread = std::jthread(
      [&bridge,
       generation = bridge.modelGeneration.load(),
       cubeNumX = m_cubeNumX,
       cubeNumY = m_cubeNumY,
       cubeNumZ = m_cubeNumZ,
       cubeEdgeLength = m_cubeEdgeLength,
       crossSectionalArea = m_crossSectionalArea,
       type = materialIndex,
       appliedForces = m_appliedForces,
       supports = m_supports](std::stop_token st) mutable {
        try {
          // cm^2 in the panel, m^2 in the model.
          auto built = FEM::TRUSS::buildSimpleTruss({cubeNumX, cubeNumY, cubeNumZ}, cubeEdgeLength, crossSectionalArea * 1e-4, type);
          if (!built) {
            anaf::LOG::error("Preview not generated: {}", built.error());
            bridge.isGeneratingPreview = false;
            return;
          }
          if (st.stop_requested()) {
            bridge.isGeneratingPreview = false;
            return;
          }
          auto newMesh = std::make_shared<BRIDGE::MeshData>(std::move(*built));
          newMesh->appliedForces = appliedForces;
          applySupports(*newMesh, supports);

          {
            std::lock_guard lock(bridge.dataMutex);
            if (bridge.modelGeneration.load() != generation) {
              bridge.isGeneratingPreview = false;
              return; // the model was reset while the preview was built
            }
            bridge.activeMesh = std::move(newMesh);
            bridge.selectedNodeId = 0u;
          }
          bridge.dataVersion.fetch_add(1, std::memory_order_release);
        } catch (const std::exception&) {
          std::lock_guard lock(bridge.dataMutex);
          bridge.activeMesh = nullptr;
          bridge.dataVersion.fetch_add(1, std::memory_order_release);
        }

        bridge.isGeneratingPreview = false;
    });
  }

  void TrussControlPanel::renderLoadsTab(BRIDGE::GuiCalcBridge& bridge, const std::uint32_t currentSelectedNode, const bool dynamic) {
    if (currentSelectedNode != std::numeric_limits<std::uint32_t>::max()) {
      m_forceNodeId = currentSelectedNode;
    }

    LAYOUT::field("Node");
    ImGui::InputScalar("##force_node", ImGuiDataType_U32, &m_forceNodeId);
    ImGui::SetItemTooltip("Click a node in the viewport or type its id");

    // Panel members, not statics: resetState() clears them when the object type changes.
    if (m_lastFixNode != currentSelectedNode) {
      const auto it = m_supports.find(m_forceNodeId);
      m_fixed = it != m_supports.end() ? it->second : std::array<bool, 3>{false, false, false};
      m_lastFixNode = currentSelectedNode;
    }
    ImGui::SeparatorText("Support");
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Fix");
    ImGui::SameLine(ImGui::GetFontSize() * 7.0f);
    ImGui::Checkbox("X##fix_x", &m_fixed[0]);
    ImGui::SameLine();
    ImGui::Checkbox("Y##fix_y", &m_fixed[1]);
    ImGui::SameLine();
    ImGui::Checkbox("Z##fix_z", &m_fixed[2]);
    if (ImGui::Button("Apply Fixity", ImVec2(-FLT_MIN, 0.0f))) {
      if (m_fixed[0] || m_fixed[1] || m_fixed[2]) m_supports[m_forceNodeId] = m_fixed;
      else m_supports.erase(m_forceNodeId);
      publishInputs(bridge, m_appliedForces, m_supports);
    }

    // Static loads only; a dynamic analysis keeps them in the inputs but does not show them.
    if (dynamic) return;
    ImGui::SeparatorText("Nodal Load");
    LAYOUT::field("Force [N]");
    ImGui::InputScalarN("##force", ImGuiDataType_Double, m_forceVector.data(), 3, nullptr, nullptr, "%.4g");
    if (ImGui::Button("Apply Load to Node", ImVec2(-FLT_MIN, 0.0f))) {
      m_appliedForces.erase(
        std::remove_if(m_appliedForces.begin(), m_appliedForces.end(),
          [&](const FEM::TRUSS::ForceApplied& f) { return f.getAppliedNode() == m_forceNodeId; }),
        m_appliedForces.end());
      m_appliedForces.emplace_back(m_forceNodeId, m_forceVector);
      publishInputs(bridge, m_appliedForces, m_supports);
    }
    ImGui::TextDisabled("Self weight is always applied.");
  }

  void TrussControlPanel::renderSolve(BRIDGE::GuiCalcBridge& bridge) {
    LAYOUT::field("Deformation");
    double currentScale = bridge.deformScale.load();
    if (ImGui::InputDouble("##deformation_scale", &currentScale, 0.0, 0.0, "%.3f")) {
      bridge.deformScale = currentScale;
      bridge.dataVersion.fetch_add(1, std::memory_order_release);
    }
    ImGui::SetItemTooltip("Deformation scale of the drawn shape (1 = true size)");

    std::uint32_t materialIndex{};
    if (bridge.isRunning) {
      ImGui::ProgressBar(bridge.progress.load(), ImVec2(-FLT_MIN, 0.0f));
      ImGui::BeginDisabled();
      LAYOUT::primaryButton("Calculating...");
      ImGui::EndDisabled();
    }
    else if (LAYOUT::primaryButton("Run Solver for Truss") && resolveMaterialIndex(bridge, m_materialID, materialIndex)) {
      // The grid is rebuilt from the current inputs on the worker, like the preview.
      TRUSS_WORKER::startSolve(bridge,
        [cubeNumX = m_cubeNumX, cubeNumY = m_cubeNumY, cubeNumZ = m_cubeNumZ, cubeEdgeLength = m_cubeEdgeLength,
         crossSectionalArea = m_crossSectionalArea, type = materialIndex,
         forcesToApply = m_appliedForces, supports = m_supports]() -> std::expected<std::shared_ptr<const BRIDGE::MeshData>, std::string> {
          auto built = FEM::TRUSS::buildSimpleTruss({cubeNumX, cubeNumY, cubeNumZ}, cubeEdgeLength, crossSectionalArea * 1e-4, type);
          if (!built) return std::unexpected(built.error());
          built->appliedForces = forcesToApply;
          applySupports(*built, supports);
          return std::make_shared<const BRIDGE::MeshData>(std::move(*built));
        });
    }
  }

  void TrussControlPanel::onImGuiRender() {
    BRIDGE::GuiCalcBridge& bridge = BRIDGE::buildBridge();

    std::uint32_t currentSelectedNode = std::numeric_limits<std::uint32_t>::max();
    {
      std::lock_guard lock(bridge.dataMutex);
      currentSelectedNode = bridge.selectedNodeId;
    }

    const auto loadKind = bridge.loadKind.load();
    const bool dynamic = loadKind == BRIDGE::E_LoadKind::Dynamic;
    // Footer rows: deformation scale (+ progress bar) and Clear All; one run button.
    const float footer = dynamic ? LAYOUT::footerHeight(1, 1) : LAYOUT::footerHeight(bridge.isRunning ? 3 : 2, 1);

    ImGui::Begin("Truss(1D) Analysis Set", &isOpen);
    renderSummary(bridge);

    if (ImGui::BeginTabBar("##sqpt_tabs")) {
      if (ImGui::BeginTabItem("Grid")) {
        LAYOUT::beginBody("##sqpt_grid_tab", footer);
        renderGridTab(bridge);
        LAYOUT::endBody();
        ImGui::EndTabItem();
      }
      // ### keeps one tab (and its selection) for both labels.
      if (ImGui::BeginTabItem(dynamic ? "Supports###sqpt_loads_tab" : "Supports & Loads###sqpt_loads_tab")) {
        LAYOUT::beginBody("##sqpt_loads_tab", footer);
        renderLoadsTab(bridge, currentSelectedNode, dynamic);
        LAYOUT::endBody();
        ImGui::EndTabItem();
      }
      if (ImGui::BeginTabItem("Analysis")) {
        LAYOUT::beginBody("##sqpt_analysis_tab", footer);
        renderAnalysisTab(loadKind, m_dynamic);
        LAYOUT::endBody();
        ImGui::EndTabItem();
      }
      ImGui::EndTabBar();
    }

    if (dynamic) renderDynamicRunButton();
    else renderSolve(bridge);

    if (ImGui::Button("Clear All", ImVec2(-FLT_MIN, 0.0f))) {
      bridge.resetModel(BRIDGE::E_ObjectType::TrussSqpt);
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
