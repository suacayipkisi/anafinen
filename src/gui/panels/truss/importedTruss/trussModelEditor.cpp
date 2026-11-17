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


#include "trussModelEditor.hpp"

#include <guiMaterials/iconAtlas.hpp>
#include <guiMaterials/theme.hpp>

#include <bridge/generalStatus.hpp>
#include <log/anaf_info.hpp>
#include <truss_1D/trussProperties/meshEdit.hpp>
#include <common/supportBasis.hpp>
#include <panels/editorLayout.hpp>
#include <panels/propertyGrid.hpp>
#include <panels/truss/materialCombo.hpp>
#include <panels/truss/trussWorker.hpp>

#include "imgui.h"

#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <cstddef>
#include <expected>
#include <format>
#include <memory>
#include <span>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace anaf::GUI {

  namespace {

    constexpr double cm2ToM2 = 1e-4;

    // Runs edit on a copy of the active snapshot (an empty one if there is none) under
    // dataMutex and publishes the copy. edit returns false to publish nothing.
    template <typename Edit>
    bool editModel(BRIDGE::GuiCalcBridge& bridge, Edit&& edit) {
      {
        std::lock_guard lock(bridge.dataMutex);
        auto mesh = bridge.activeMesh ? std::make_shared<FEM::TRUSS::MeshData>(*bridge.activeMesh) : std::make_shared<FEM::TRUSS::MeshData>();
        if (!edit(*mesh)) return false;
        FEM::TRUSS::dropResults(*mesh);
        bridge.activeMesh = std::move(mesh);
        bridge.isValid = false;
      }
      bridge.dataVersion.fetch_add(1, std::memory_order_release);
      return true;
    }

    std::shared_ptr<const FEM::TRUSS::MeshData> currentMesh(BRIDGE::GuiCalcBridge& bridge) {
      std::lock_guard lock(bridge.dataMutex);
      return bridge.activeMesh;
    }

    void select(BRIDGE::GuiCalcBridge& bridge, const std::uint32_t node, const std::uint32_t bar) {
      {
        std::lock_guard lock(bridge.dataMutex);
        bridge.selectedNodeId = node;
        bridge.selectedElementId = bar;
      }
      bridge.dataVersion.fetch_add(1, std::memory_order_release); // redraw the highlight
    }

    bool beginToolWindow(const char* name, bool& open, const float widthEm) {
      if (!open) return false;
      const ImGuiViewport* viewport = ImGui::GetMainViewport();
      const float fontSize = ImGui::GetFontSize();
      ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + viewport->WorkSize.x * 0.25f, viewport->WorkPos.y + fontSize * 6.0f),
                              ImGuiCond_FirstUseEver);
      ImGui::SetNextWindowSize(ImVec2(fontSize * widthEm, 0.0f), ImGuiCond_Always);
      if (!ImGui::Begin(name, &open, ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::End();
        return false;
      }
      return true;
    }

  } // namespace end

  void TrussModelEditor::resetState() {
    m_focus = E_Focus::Model;
    m_nodePosition = {0.0, 0.0, 0.0};
    m_newNode = {0.0, 0.0, 0.0};
    m_barNodeA = 0;
    m_barNodeB = 1;
    m_areaCm2 = 80.0;
    m_newAreaCm2 = 80.0;
    m_force = {0.0, 0.0, 0.0};
    m_fixed = {false, false, false};
    m_supportInclined = false;
    m_vectorsRestrained = true;
    setSupportVectors({{0.0, 1.0, 0.0}});
    m_loadedNode = noSelection;
    m_loadedBar = noSelection;
    m_status.clear();
    m_statusIsError = false;
  }

  void TrussModelEditor::setStatus(std::string message, const bool error) {
    if (error) anaf::LOG::warn("Model editor: {}", message);
    m_status = std::move(message);
    m_statusIsError = error;
  }

  void TrussModelEditor::openAddNode() { m_addNodeOpen = true; }
  void TrussModelEditor::openAddBar() { m_addBarOpen = true; }
  void TrussModelEditor::openAssignAll() { m_assignAllOpen = true; }

  bool TrussModelEditor::solve() {
    auto& bridge = BRIDGE::buildBridge();
    const auto mesh = currentMesh(bridge);
    if (bridge.isRunning.load() || !mesh || mesh->trussElements.empty()) return false;
    m_status.clear();
    TRUSS_WORKER::startSolve(bridge, [mesh]() -> std::expected<std::shared_ptr<const FEM::TRUSS::MeshData>, std::string> { return mesh; });
    return true;
  }

  void TrussModelEditor::syncSelection(const std::uint32_t node, const std::uint32_t bar) {
    if (node != m_loadedNode && node != noSelection) m_focus = E_Focus::Node;
    if (bar != m_loadedBar && bar != noSelection) m_focus = E_Focus::Bar;
    if (m_focus == E_Focus::Node && node == noSelection) m_focus = bar != noSelection ? E_Focus::Bar : E_Focus::Model;
    if (m_focus == E_Focus::Bar && bar == noSelection) m_focus = node != noSelection ? E_Focus::Node : E_Focus::Model;

    auto& bridge = BRIDGE::buildBridge();
    if (bar != m_loadedBar) {
      m_loadedBar = bar;
      std::lock_guard lock(bridge.dataMutex);
      if (bar != noSelection && bridge.activeMesh && bar < bridge.activeMesh->trussElements.size()) {
        const auto& element = bridge.activeMesh->trussElements[bar];
        if (element.materialID < bridge.allMaterials.size()) m_materialID = bridge.allMaterials[element.materialID].getMaterialID();
        if (!element.isWireframe) m_areaCm2 = element.crossSectionArea / cm2ToM2;
        m_newMaterialID = m_materialID;
        m_newAreaCm2 = m_areaCm2;
      }
    }

    if (node == m_loadedNode) return;
    m_loadedNode = node;
    m_force = {0.0, 0.0, 0.0};
    m_fixed = {false, false, false};
    if (node == noSelection) return;
    std::lock_guard lock(bridge.dataMutex);
    if (!bridge.activeMesh || node >= bridge.activeMesh->trussNodes.size()) return;
    m_nodePosition = bridge.activeMesh->trussNodes[node].getLocation();
    for (const auto& force : bridge.activeMesh->appliedForces) {
      if (force.getAppliedNode() == node) m_force = force.getForce();
    }
    const auto& selected = bridge.activeMesh->trussNodes[node];
    const auto& movable = selected.getMovable();
    m_fixed = {!movable[0], !movable[1], !movable[2]};
    m_supportInclined = selected.hasInclinedSupport();
    if (m_supportInclined) {
      const auto& allowed = selected.getAllowedMotionDirections();
      setSupportVectors(m_vectorsRestrained ? FEM::SUPPORT::orthogonalComplement(allowed) : allowed);
    }
  }

  void TrussModelEditor::setSupportVectors(const FEM::SUPPORT::Basis& vectors) {
    if (vectors.empty() || vectors.size() > 3) return;
    m_supportVectorCount = static_cast<int>(vectors.size());
    for (std::size_t i = 0; i < vectors.size(); ++i) m_supportVectors[i] = vectors[i];
  }

  void TrussModelEditor::renderHeader(const std::uint32_t node, const std::uint32_t bar) {
    std::string title = "Model";
    ICONS::E_IconId icon = ICONS::E_IconId::ModelTruss;
    if (m_focus == E_Focus::Node) {
      title = std::format("Node {}", node);
      icon = ICONS::E_IconId::ModelNode;
    } else if (m_focus == E_Focus::Bar) {
      const auto mesh = currentMesh(BRIDGE::buildBridge());
      title = mesh && bar < mesh->trussElements.size()
        ? std::format("Bar {}  ({} - {})", bar, mesh->trussElements[bar].node1, mesh->trussElements[bar].node2)
        : std::format("Bar {}", bar);
      icon = ICONS::E_IconId::ModelElementBar;
    }
    const float right = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;
    ImGui::AlignTextToFramePadding();
    ICONS::iconText(icon, title.c_str());
    if (m_focus != E_Focus::Model) {
      const float width = ImGui::CalcTextSize("Deselect").x + ImGui::GetStyle().FramePadding.x * 2.0f;
      ImGui::SameLine(std::max(right - width, ImGui::GetCursorPosX()));
      if (ImGui::Button("Deselect")) select(BRIDGE::buildBridge(), noSelection, noSelection);
      ImGui::SetItemTooltip("Back to the model (a click on empty viewport space does the same)");
    }
    ImGui::Separator();
  }

  void TrussModelEditor::renderModel() {
    auto& bridge = BRIDGE::buildBridge();
    const auto mesh = currentMesh(bridge);
    if (!mesh || mesh->trussNodes.empty()) {
      ImGui::PushTextWrapPos(0.0f);
      ImGui::TextDisabled("No model yet. Import a file, start from a built-in model, or add nodes and bars.");
      ImGui::PopTextWrapPos();
      if (ICONS::iconTextButton(ICONS::E_IconId::ActionImport, "Import File...", ImVec2(-FLT_MIN, 0.0f)) && onRequestImport) onRequestImport();
      ImGui::SetItemTooltip("File > Import Mesh / CAD (Ctrl+O)");
      if (ICONS::iconTextButton(ICONS::E_IconId::ModelLibrary, "Open Example...", ImVec2(-FLT_MIN, 0.0f)) && onOpenLibrary) onOpenLibrary();
      if (ICONS::iconTextButton(ICONS::E_IconId::ModelNode, "Add Node...", ImVec2(-FLT_MIN, 0.0f))) openAddNode();
      return;
    }

    std::size_t bars = 0, wireframe = 0;
    for (const auto& element : mesh->trussElements) {
      if (element.isWireframe) ++wireframe;
      else ++bars;
    }
    const auto supports = std::ranges::count_if(mesh->trussNodes, [](const FEM::TRUSS::Node& n) { return n.isSupported(); });
    if (PROPERTY::beginFixed("##truss_model")) {
      if (PROPERTY::group("Model")) {
        PROPERTY::value("Nodes", std::to_string(mesh->trussNodes.size()));
        PROPERTY::value("Bars", std::to_string(bars));
        if (wireframe > 0) PROPERTY::value("Wireframe edges", std::to_string(wireframe), nullptr, "Edges of an imported surface mesh; not solved.");
        PROPERTY::value("Supports", std::to_string(supports));
        PROPERTY::value("Loads", std::to_string(mesh->appliedForces.size()));
        PROPERTY::value("Self weight", "always on");
      }
      if (mesh->hasResults && PROPERTY::group("Results")) {
        double maxDisplacement = 0.0, maxStress = 0.0;
        std::size_t exceeded = 0;
        for (const auto& n : mesh->trussNodes) {
          const auto& d = n.getDisplacement();
          maxDisplacement = std::max(maxDisplacement, std::hypot(d[0], d[1], d[2]));
        }
        for (const auto& element : mesh->trussElements) {
          if (element.isWireframe) continue;
          maxStress = std::max(maxStress, std::abs(static_cast<double>(element.stress)));
          if (element.isStressExceeded) ++exceeded;
        }
        PROPERTY::value("Max |u|", std::format("{:.4g}", maxDisplacement * 1e3), "mm", "Largest nodal displacement.");
        PROPERTY::value("Max |stress|", std::format("{:.4g}", maxStress / 1e6), "MPa");
        PROPERTY::value("Over yield", std::to_string(exceeded), nullptr, "Bars whose |stress| exceeds the yield strength.");
      }
      PROPERTY::end();
    }
    if (mesh->hasResults) {
      const bool valid = bridge.isValid.load();
      ICONS::iconText(valid ? ICONS::E_IconId::StatusSuccess : ICONS::E_IconId::StatusWarning,
                      valid ? "Solved, energy check passed" : "Results shown (energy check not passed)");
    } else {
      ICONS::iconText(ICONS::E_IconId::StatusInfo, "No results yet: Run Solver (F5).");
    }
    ImGui::Spacing();
    if (ICONS::iconTextButton(ICONS::E_IconId::ModelElementBar, "Assign to All Bars...", ImVec2(-FLT_MIN, 0.0f))) openAssignAll();
    ImGui::SetItemTooltip("One material and cross-section on every bar (e.g. after importing a file without sections)");
    ImGui::Spacing();
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("Click a node in the viewport, or a node or a bar in the Model Tree, to edit it.");
    ImGui::PopTextWrapPos();
  }

  void TrussModelEditor::renderNode(const std::uint32_t node, const bool withLoads) {
    auto& bridge = BRIDGE::buildBridge();
    const auto mesh = currentMesh(bridge);
    if (!mesh || node >= mesh->trussNodes.size()) return;

    if (PROPERTY::beginFixed("##truss_node_geometry")) {
      if (PROPERTY::group("Geometry")) PROPERTY::inputVector3("Position", m_nodePosition.data(), "m");
      PROPERTY::end();
    }
    if (ICONS::iconTextButton(ICONS::E_IconId::ModelNode, "Move", ImVec2(LAYOUT::splitWidth(2), 0.0f))) {
      if (editModel(bridge, [&](FEM::TRUSS::MeshData& edited) {
            if (node >= edited.trussNodes.size()) return false;
            edited.trussNodes[node].setLocation(m_nodePosition);
            return true;
          })) {
        setStatus(std::format("Node {} moved", node), false);
      }
    }
    ImGui::SameLine();
    if (ICONS::iconTextButton(ICONS::E_IconId::ActionRemove, "Delete Node", ImVec2(-FLT_MIN, 0.0f))) {
      if (editModel(bridge, [&](FEM::TRUSS::MeshData& edited) {
            if (node >= edited.trussNodes.size()) return false;
            FEM::TRUSS::deleteNode(edited, node);
            return true;
          })) {
        select(bridge, noSelection, noSelection);
        m_loadedNode = noSelection;
        setStatus(std::format("Node {} deleted with its bars; later node ids moved down by one", node), false);
        return;
      }
    }

    ImGui::Spacing();
    bool supportOpen = false;
    if (PROPERTY::beginFixed("##truss_support")) {
      supportOpen = PROPERTY::group("Support");
      if (supportOpen) {
        int supportType = m_supportInclined ? 1 : 0;
        PROPERTY::row("Directions", "Global axes: fix single global directions.\nInclined: give the directions as vectors (skewed support).");
        constexpr std::array<const char*, 2> directionModes{"Global axes", "Inclined vectors"};
        ImGui::Combo("##support_mode", &supportType, directionModes.data(), static_cast<int>(directionModes.size()));
        m_supportInclined = supportType == 1;
        if (m_supportInclined) {
          renderInclinedSupportInputs();
        } else {
          PROPERTY::row("Fixed");
          ImGui::Checkbox("X##fix_x", &m_fixed[0]);
          ImGui::SameLine();
          ImGui::Checkbox("Y##fix_y", &m_fixed[1]);
          ImGui::SameLine();
          ImGui::Checkbox("Z##fix_z", &m_fixed[2]);
        }
      }
      PROPERTY::end();
    }

    // Allowed motion of the inclined support, or the error that keeps Apply disabled (also while
    // the group is folded).
    FEM::SUPPORT::Basis allowed;
    std::string inclinedError;
    if (m_supportInclined) {
      try {
        const auto basis = FEM::SUPPORT::orthonormalize(FEM::SUPPORT::Basis(
          std::span<const std::array<double, 3>>(m_supportVectors.data(), static_cast<std::size_t>(m_supportVectorCount))));
        allowed = m_vectorsRestrained ? FEM::SUPPORT::orthogonalComplement(basis) : basis;
      } catch (const std::invalid_argument&) {
        inclinedError = "The vectors must be non-zero and linearly independent.";
      }
      const auto show = [](const char* what, const std::array<double, 3>& v) {
        ImGui::TextDisabled("%s (%.3f, %.3f, %.3f)", what, v[0], v[1], v[2]);
      };
      if (!inclinedError.empty()) {
        ImGui::TextColored(THEME::theme().bad, "%s", inclinedError.c_str());
      } else if (!supportOpen) {
        // Folded: the description of a valid support stays hidden with its inputs.
      } else if (allowed.empty()) {
        ImGui::TextDisabled("Node is held in every direction (pin).");
      } else if (allowed.size() == 1) {
        show("Moves along the line", allowed[0]);
      } else if (allowed.size() == 2) {
        show("Slides on the plane with normal", FEM::SUPPORT::orthogonalComplement(allowed)[0]);
      } else {
        ImGui::TextDisabled("Nothing is restrained (free node).");
      }
    }
    ImGui::BeginDisabled(!inclinedError.empty());
    if (ICONS::iconTextButton(ICONS::E_IconId::SupportPin, "Apply Support", ImVec2(-FLT_MIN, 0.0f))) {
      const bool applied = editModel(bridge, [&](FEM::TRUSS::MeshData& edited) {
        if (node >= edited.trussNodes.size()) return false;
        auto& target = edited.trussNodes[node];
        if (!m_supportInclined) {
          target.setMovable({!m_fixed[0], !m_fixed[1], !m_fixed[2]});
        } else if (allowed.size() == 3 || allowed.empty()) {
          const bool free = !allowed.empty();
          target.setMovable({free, free, free});
        } else {
          target.setAllowedMotionDirections(allowed);
        }
        // The checkboxes show the global axes outside the allowed subspace.
        const auto& movable = target.getMovable();
        m_fixed = {!movable[0], !movable[1], !movable[2]};
        return true;
      });
      if (applied) setStatus(std::format("Support of node {} updated", node), false);
    }
    ImGui::EndDisabled();

    if (withLoads) {
      ImGui::Spacing();
      bool loadOpen = false;
      if (PROPERTY::beginFixed("##truss_nodal_load")) {
        loadOpen = PROPERTY::group("Nodal Load");
        if (loadOpen) PROPERTY::inputVector3("Force", m_force.data(), "N", "%.4g", "Global axes.");
        PROPERTY::end();
      }
      if (loadOpen) {
        const auto setLoad = [&](const std::array<double, 3> force) {
          return editModel(bridge, [&](FEM::TRUSS::MeshData& edited) {
            if (node >= edited.trussNodes.size()) return false;
            std::erase_if(edited.appliedForces, [&](const FEM::TRUSS::ForceApplied& applied) { return applied.getAppliedNode() == node; });
            if (force[0] != 0.0 || force[1] != 0.0 || force[2] != 0.0) edited.appliedForces.emplace_back(node, force);
            return true;
          });
        };
        if (ICONS::iconTextButton(ICONS::E_IconId::LoadForce, "Apply Load", ImVec2(LAYOUT::splitWidth(2), 0.0f))) {
          if (setLoad(m_force)) setStatus(std::format("Load on node {} updated", node), false);
        }
        ImGui::SameLine();
        if (ICONS::iconTextButton(ICONS::E_IconId::ActionRemove, "Remove Load", ImVec2(-FLT_MIN, 0.0f))) {
          m_force = {0.0, 0.0, 0.0};
          if (setLoad(m_force)) setStatus(std::format("Load on node {} removed", node), false);
        }
      }
    }

    if (mesh->hasResults) {
      ImGui::Spacing();
      if (PROPERTY::beginFixed("##truss_node_results")) {
        if (PROPERTY::group("Results")) {
          const auto& d = mesh->trussNodes[node].getDisplacement();
          PROPERTY::value("Displacement", std::format("{:.4g}, {:.4g}, {:.4g}", d[0] * 1e3, d[1] * 1e3, d[2] * 1e3), "mm");
          PROPERTY::value("Magnitude", std::format("{:.4g}", std::hypot(d[0], d[1], d[2]) * 1e3), "mm");
        }
        PROPERTY::end();
      }
    }
  }

  void TrussModelEditor::renderInclinedSupportInputs() {
    bool restrained = m_vectorsRestrained;
    PROPERTY::row("Vectors give", "Restrained: the support reaction directions (1 = roller on a plane, 2 = guide along a line, "
                                  "3 = pin).\nAllowed: the line (1) or plane (2) the node may move in.");
    if (ImGui::RadioButton("Restrained##support_restrained", restrained)) restrained = true;
    ImGui::SameLine();
    if (ImGui::RadioButton("Allowed##support_allowed", !restrained)) restrained = false;
    if (restrained != m_vectorsRestrained) {
      // Switch the meaning without changing the support: the vectors become their complement.
      try {
        const auto basis = FEM::SUPPORT::orthonormalize(FEM::SUPPORT::Basis(
          std::span<const std::array<double, 3>>(m_supportVectors.data(), static_cast<std::size_t>(m_supportVectorCount))));
        if (basis.size() < 3) setSupportVectors(FEM::SUPPORT::orthogonalComplement(basis));
      } catch (const std::invalid_argument&) {
        // Invalid input: keep the vectors as typed.
      }
      m_vectorsRestrained = restrained;
    }
    PROPERTY::row("Vector count");
    for (int count = 1; count <= 3; ++count) {
      if (count > 1) ImGui::SameLine();
      ImGui::RadioButton(std::format("{}##support_count", count).c_str(), &m_supportVectorCount, count);
    }
    for (int i = 0; i < m_supportVectorCount; ++i) {
      PROPERTY::inputVector3(std::format("d{}", i + 1).c_str(), m_supportVectors[static_cast<std::size_t>(i)].data(), nullptr, "%.4g",
                             "Direction in global axes (normalized by the solver).");
    }
  }

  void TrussModelEditor::renderBar(const std::uint32_t bar) {
    auto& bridge = BRIDGE::buildBridge();
    const auto mesh = currentMesh(bridge);
    if (!mesh || bar >= mesh->trussElements.size()) return;
    const auto& element = mesh->trussElements[bar];
    const auto& a = mesh->trussNodes[element.node1].getLocation();
    const auto& b = mesh->trussNodes[element.node2].getLocation();

    if (PROPERTY::beginFixed("##truss_bar")) {
      if (PROPERTY::group(element.isWireframe ? "Wireframe Edge" : "Bar")) {
        PROPERTY::value("Nodes", std::format("{} - {}", element.node1, element.node2));
        PROPERTY::value("Length", std::format("{:.4g}", std::hypot(b[0] - a[0], b[1] - a[1], b[2] - a[2])), "m");
        PROPERTY::row("Material");
        PROPERTY::reserveBrowse();
        materialCombo(bridge, "##bar_material", m_materialID);
        if (PROPERTY::browseButton("##bar_materials", "Open the Material Handler") && onOpenMaterialHandler) onOpenMaterialHandler();
        PROPERTY::inputDouble("Area A", &m_areaCm2, "cm²", "%.4g", "Cross-section area.");
        if (m_areaCm2 < 0.0) m_areaCm2 = 0.0;
      }
      PROPERTY::end();
    }
    if (element.isWireframe) {
      ImGui::PushTextWrapPos(0.0f);
      ImGui::TextDisabled("An edge of an imported surface mesh: Apply turns it into a bar.");
      ImGui::PopTextWrapPos();
    }
    if (ICONS::iconTextButton(ICONS::E_IconId::ModelElementBar, "Apply", ImVec2(LAYOUT::splitWidth(2), 0.0f))) {
      const bool applied = editModel(bridge, [&](FEM::TRUSS::MeshData& edited) {
        const auto index = bridge.findMaterialIndex(m_materialID); // editModel holds dataMutex
        if (bar >= edited.trussElements.size() || !index || !(m_areaCm2 > 0.0)) return false;
        auto& target = edited.trussElements[bar];
        target.materialID = *index;
        target.crossSectionArea = m_areaCm2 * cm2ToM2;
        target.isWireframe = false;
        return true;
      });
      if (applied) setStatus(std::format("Bar {} updated", bar), false);
      else setStatus("Choose a material and an area greater than 0", true);
    }
    ImGui::SameLine();
    if (ICONS::iconTextButton(ICONS::E_IconId::ActionRemove, "Delete Bar", ImVec2(-FLT_MIN, 0.0f))) {
      if (editModel(bridge, [&](FEM::TRUSS::MeshData& edited) {
            if (bar >= edited.trussElements.size()) return false;
            edited.trussElements.erase(edited.trussElements.begin() + static_cast<std::ptrdiff_t>(bar));
            return true;
          })) {
        select(bridge, noSelection, noSelection);
        setStatus(std::format("Bar {} deleted", bar), false);
        return;
      }
    }

    if (mesh->hasResults && !element.isWireframe) {
      ImGui::Spacing();
      if (PROPERTY::beginFixed("##truss_bar_results")) {
        if (PROPERTY::group("Results")) {
          const double stress = static_cast<double>(element.stress);
          PROPERTY::value("Stress", std::format("{:.4g}", stress / 1e6), "MPa", "Axial stress; tension > 0.");
          PROPERTY::value("Axial force", std::format("{:.4g}", stress * element.crossSectionArea / 1e3), "kN");
          PROPERTY::value("Yield check", element.isStressExceeded ? "exceeded" : "ok");
        }
        PROPERTY::end();
      }
    }
  }

  void TrussModelEditor::renderAddNodeWindow() {
    if (!beginToolWindow("Add Node##truss", m_addNodeOpen, 22.0f)) return;
    auto& bridge = BRIDGE::buildBridge();
    ImGui::BeginDisabled(bridge.isRunning.load());
    if (PROPERTY::beginFixed("##add_node", 5.0f)) {
      PROPERTY::inputVector3("Position", m_newNode.data(), "m");
      PROPERTY::end();
    }
    if (ICONS::iconTextButton(ICONS::E_IconId::ActionAdd, "Add Node", ImVec2(-FLT_MIN, 0.0f))) {
      std::uint32_t added = 0;
      editModel(bridge, [&](FEM::TRUSS::MeshData& mesh) {
        added = static_cast<std::uint32_t>(mesh.trussNodes.size());
        mesh.trussNodes.emplace_back(added, m_newNode[0], m_newNode[1], m_newNode[2]);
        return true;
      });
      select(bridge, added, noSelection);
      setStatus(std::format("Node {} added", added), false);
    }
    ImGui::EndDisabled();
    ImGui::TextDisabled("The new node is selected; the window stays open.");
    ImGui::End();
  }

  void TrussModelEditor::renderAddBarWindow() {
    if (!beginToolWindow("Add Bar", m_addBarOpen, 24.0f)) return;
    auto& bridge = BRIDGE::buildBridge();
    ImGui::BeginDisabled(bridge.isRunning.load());
    if (PROPERTY::beginFixed("##add_bar")) {
      PROPERTY::row("Nodes A - B", "Ids of the two end nodes; after Add, A takes B for the next bar of a chain.");
      ImGui::SetNextItemWidth(LAYOUT::splitWidth(2));
      ImGui::InputScalar("##bar_a", ImGuiDataType_U32, &m_barNodeA);
      ImGui::SameLine();
      ImGui::SetNextItemWidth(-FLT_MIN);
      ImGui::InputScalar("##bar_b", ImGuiDataType_U32, &m_barNodeB);
      PROPERTY::row("Material");
      PROPERTY::reserveBrowse();
      materialCombo(bridge, "##new_bar_material", m_newMaterialID);
      if (PROPERTY::browseButton("##new_bar_materials", "Open the Material Handler") && onOpenMaterialHandler) onOpenMaterialHandler();
      PROPERTY::inputDouble("Area A", &m_newAreaCm2, "cm²", "%.4g");
      if (m_newAreaCm2 < 0.0) m_newAreaCm2 = 0.0;
      PROPERTY::end();
    }
    if (ICONS::iconTextButton(ICONS::E_IconId::ActionAdd, "Add Bar", ImVec2(-FLT_MIN, 0.0f))) {
      std::string error;
      const bool added = editModel(bridge, [&](FEM::TRUSS::MeshData& mesh) {
        const auto count = mesh.trussNodes.size();
        const auto a = m_barNodeA, b = m_barNodeB;
        if (a >= count || b >= count) {
          error = std::format("nodes {} and {} must both exist (the model has {} nodes)", a, b, count);
          return false;
        }
        if (a == b) {
          error = "a bar needs two different nodes";
          return false;
        }
        const auto& pa = mesh.trussNodes[a].getLocation();
        const auto& pb = mesh.trussNodes[b].getLocation();
        if (std::hypot(pb[0] - pa[0], pb[1] - pa[1], pb[2] - pa[2]) <= 0.0) {
          error = std::format("nodes {} and {} are at the same position", a, b);
          return false;
        }
        const bool duplicate = std::ranges::any_of(mesh.trussElements, [&](const FEM::TRUSS::RenderElement& element) {
          return !element.isWireframe && ((element.node1 == a && element.node2 == b) || (element.node1 == b && element.node2 == a));
        });
        if (duplicate) {
          error = std::format("a bar between nodes {} and {} already exists", a, b);
          return false;
        }
        const auto index = bridge.findMaterialIndex(m_newMaterialID); // editModel holds dataMutex
        if (!index || !(m_newAreaCm2 > 0.0)) {
          error = "choose a material and an area greater than 0";
          return false;
        }
        FEM::TRUSS::RenderElement element{a, b, 0.0f, false, 0u, 0.0, false};
        element.materialID = *index;
        element.crossSectionArea = m_newAreaCm2 * cm2ToM2;
        mesh.trussElements.push_back(element);
        return true;
      });
      if (added) {
        setStatus(std::format("Bar {} - {} added", m_barNodeA, m_barNodeB), false);
        m_barNodeA = m_barNodeB; // ready for the next bar of a chain
      } else {
        setStatus("Bar not added: " + error, true);
      }
    }
    ImGui::EndDisabled();
    if (!m_status.empty()) LAYOUT::wrappedColored(m_statusIsError ? THEME::theme().bad : THEME::theme().note, m_status);
    ImGui::End();
  }

  void TrussModelEditor::renderAssignAllWindow() {
    if (!beginToolWindow("Assign to All Bars", m_assignAllOpen, 26.0f)) return;
    auto& bridge = BRIDGE::buildBridge();
    const auto mesh = currentMesh(bridge);
    std::size_t bars = 0, wireframe = 0;
    if (mesh) {
      for (const auto& element : mesh->trussElements) {
        if (element.isWireframe) ++wireframe;
        else ++bars;
      }
    }
    ImGui::BeginDisabled(bridge.isRunning.load());
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("One material and cross-section on every bar at once (e.g. after importing a file without sections).");
    ImGui::PopTextWrapPos();
    if (PROPERTY::beginFixed("##truss_whole_model")) {
      PROPERTY::row("Material");
      PROPERTY::reserveBrowse();
      materialCombo(bridge, "##whole_material", m_wholeMaterialID);
      if (PROPERTY::browseButton("##whole_materials", "Open the Material Handler") && onOpenMaterialHandler) onOpenMaterialHandler();
      PROPERTY::inputDouble("Area A", &m_wholeAreaCm2, "cm²", "%.4g");
      if (m_wholeAreaCm2 < 0.0) m_wholeAreaCm2 = 0.0;
      if (wireframe > 0) {
        PROPERTY::row("Wireframe edges", "Also turn the wireframe edges of an imported surface mesh into bars.");
        ImGui::Checkbox(std::format("Make {} into bars##include_wireframe", wireframe).c_str(), &m_includeWireframe);
      }
      PROPERTY::end();
    }
    ImGui::BeginDisabled(bars == 0 && (wireframe == 0 || !m_includeWireframe));
    if (ImGui::Button("Apply to All Bars", ImVec2(-FLT_MIN, 0.0f))) {
      std::size_t changed = 0;
      const bool applied = editModel(bridge, [&](FEM::TRUSS::MeshData& edited) {
        const auto index = bridge.findMaterialIndex(m_wholeMaterialID); // editModel holds dataMutex
        if (!index || !(m_wholeAreaCm2 > 0.0)) return false;
        for (auto& element : edited.trussElements) {
          if (element.isWireframe && !m_includeWireframe) continue;
          element.materialID = *index;
          element.crossSectionArea = m_wholeAreaCm2 * cm2ToM2;
          element.isWireframe = false;
          ++changed;
        }
        return changed > 0;
      });
      m_loadedBar = noSelection;
      if (applied) setStatus(std::format("Material and {:.4g} cm² set on {} bars", m_wholeAreaCm2, changed), false);
      else setStatus("Nothing changed: choose a material and an area greater than 0", true);
    }
    ImGui::EndDisabled();
    ImGui::EndDisabled();
    if (!m_status.empty()) LAYOUT::wrappedColored(m_statusIsError ? THEME::theme().bad : THEME::theme().note, m_status);
    ImGui::End();
  }

  void TrussModelEditor::onImGuiRender() {
    if (!isOpen) return;
    auto& bridge = BRIDGE::buildBridge();

    std::uint32_t node = noSelection, bar = noSelection;
    {
      std::lock_guard lock(bridge.dataMutex);
      if (bridge.activeMesh && bridge.selectedNodeId < bridge.activeMesh->trussNodes.size()) node = bridge.selectedNodeId;
      if (bridge.activeMesh && bridge.selectedElementId < bridge.activeMesh->trussElements.size()) bar = bridge.selectedElementId;
    }
    syncSelection(node, bar);
    const bool withLoads = bridge.loadKind.load() != BRIDGE::E_LoadKind::Dynamic;
    // The solve works on a copy; edits made meanwhile would be overwritten by its result.
    const bool busy = bridge.isRunning.load() || bridge.isGeneratingPreview.load();

    if (ImGui::Begin("Properties###truss_properties", &isOpen)) {
      renderHeader(node, bar);
      const float statusHeight = m_status.empty() ? 0.0f : ImGui::GetTextLineHeightWithSpacing() * 2.0f + ImGui::GetStyle().ItemSpacing.y;
      ImGui::BeginChild("##truss_properties_body", ImVec2(0.0f, -statusHeight));
      ImGui::BeginDisabled(busy);
      if (m_focus == E_Focus::Node) renderNode(node, withLoads);
      else if (m_focus == E_Focus::Bar) renderBar(bar);
      else renderModel();
      ImGui::EndDisabled();
      ImGui::EndChild();
      if (!m_status.empty()) {
        ImGui::Separator();
        LAYOUT::wrappedColored(m_statusIsError ? THEME::theme().bad : THEME::theme().note, m_status);
      }
    }
    ImGui::End();

    renderAddNodeWindow();
    renderAddBarWindow();
    renderAssignAllWindow();
  }

} // namespace anaf::GUI end
