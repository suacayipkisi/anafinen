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

#include <guiMaterials/iconAtlas.hpp>
#include <guiMaterials/theme.hpp>

#include "imgui.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <string>

namespace anaf::GUI {

  namespace {
    constexpr std::uint32_t noSelection = std::numeric_limits<std::uint32_t>::max();
    constexpr std::size_t visibleRows = 10; // list height before it scrolls

    void select(anaf::BRIDGE::GuiCalcBridge& bridge, const std::uint32_t node, const std::uint32_t element) {
      {
        std::lock_guard lock(bridge.dataMutex);
        bridge.selectedNodeId = node;
        bridge.selectedElementId = element;
      }
      bridge.dataVersion.fetch_add(1, std::memory_order_release); // redraw the highlight
    }

    // Scrolled list of count rows, only the visible ones drawn.
    template <typename DrawRow>
    void clippedList(const char* id, const std::size_t count, DrawRow&& drawRow) {
      if (count == 0) {
        ImGui::TextDisabled("none");
        return;
      }
      const float height = static_cast<float>(std::min(count, visibleRows)) * ImGui::GetTextLineHeightWithSpacing() +
                           ImGui::GetStyle().WindowPadding.y;
      ImGui::BeginChild(id, ImVec2(0.0f, height), ImGuiChildFlags_None);
      ImGuiListClipper clipper;
      clipper.Begin(static_cast<int>(count));
      while (clipper.Step()) {
        for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
          ImGui::PushID(row);
          drawRow(static_cast<std::size_t>(row));
          ImGui::PopID();
        }
      }
      ImGui::EndChild();
    }

    // Icon row that selects something; red text for members over yield.
    bool row(const ICONS::E_IconId icon, const std::string& text, const bool selected, const bool bad = false) {
      if (bad) ImGui::PushStyleColor(ImGuiCol_Text, THEME::theme().bad);
      const bool clicked = ICONS::iconSelectable(icon, text.c_str(), selected);
      if (bad) ImGui::PopStyleColor();
      return clicked;
    }

    struct SupportKind {
      ICONS::E_IconId icon;
      const char* name;
    };

    // Support symbol from the free translations / rotations (beam) or translations (truss).
    SupportKind supportKind(const std::size_t freeTranslations, const std::size_t freeRotations, const bool inclined, const bool beam) {
      if (inclined) return {ICONS::E_IconId::SupportInclined, "inclined"};
      if (freeTranslations == 0) {
        if (!beam || freeRotations == 3) return {ICONS::E_IconId::SupportPin, "pinned"};
        if (freeRotations == 0) return {ICONS::E_IconId::SupportFixed, "fixed"};
        return {ICONS::E_IconId::SupportPin, "pinned, partly held rotation"};
      }
      if (freeTranslations == 1) return {ICONS::E_IconId::SupportSlider, "guided (moves along a line)"};
      if (freeTranslations == 2) return {ICONS::E_IconId::SupportRoller, "roller (moves on a plane)"};
      return {ICONS::E_IconId::SupportFixed, "rotation held"};
    }

    bool alongGlobalAxes(const FEM::SUPPORT::Basis& basis) {
      return std::ranges::all_of(basis, [](const std::array<double, 3>& v) { return std::ranges::count(v, 0.0) == 2; });
    }

    ICONS::E_IconId sectionIcon(const int shape) {
      constexpr std::array<ICONS::E_IconId, 6> icons{ICONS::E_IconId::SectionGeneral, ICONS::E_IconId::SectionRectangle,
                                                     ICONS::E_IconId::SectionCircle,  ICONS::E_IconId::SectionPipe,
                                                     ICONS::E_IconId::SectionBox,     ICONS::E_IconId::SectionIShape};
      return shape >= 0 && static_cast<std::size_t>(shape) < icons.size() ? icons[static_cast<std::size_t>(shape)] : ICONS::E_IconId::ModelSection;
    }

    std::string countLabel(const char* name, const std::size_t count) { return std::format("{} ({})", name, count); }
  } // namespace end

  void ModelTree::indexBeam(const std::shared_ptr<const FEM::BEAM::MeshData>& mesh, anaf::BRIDGE::GuiCalcBridge& bridge) {
    if (mesh == m_indexedBeamMesh) return;
    m_indexedBeamMesh = mesh;
    m_indexedTrussMesh.reset();
    m_supportedNodes.clear();
    m_overstressed.clear();
    m_materials.clear();
    m_sections.clear();
    m_maxDisplacement = 0.0;
    m_maxStress = 0.0;
    if (!mesh) return;
    std::map<std::uint32_t, std::size_t> materialCount, sectionCount;
    for (std::uint32_t i = 0; i < mesh->nodes.size(); ++i) {
      if (mesh->nodes[i].isSupported()) m_supportedNodes.push_back(i);
      const auto& d = mesh->nodes[i].getDisplacement();
      if (const double length = std::hypot(d[0], d[1], d[2]); length > m_maxDisplacement) {
        m_maxDisplacement = length;
        m_farthestNode = i;
      }
    }
    for (std::uint32_t e = 0; e < mesh->elements.size(); ++e) {
      const auto& element = mesh->elements[e];
      ++materialCount[element.materialID];
      ++sectionCount[element.sectionID];
      if (element.stress.isStressExceeded) m_overstressed.push_back(e);
      if (element.stress.available && element.stress.maxVonMises > m_maxStress) {
        m_maxStress = element.stress.maxVonMises;
        m_worstElement = e;
      }
    }
    std::lock_guard lock(bridge.dataMutex);
    for (const auto& [index, count] : materialCount) {
      const std::string name = index < bridge.allMaterials.size() ? std::string(bridge.allMaterials[index].getMaterialType()) : "?";
      m_materials.push_back({name, count, -1});
    }
    for (const auto& [index, count] : sectionCount) {
      if (index < bridge.allSections.size()) {
        const auto& section = bridge.allSections[index];
        m_sections.push_back({section.getName(), count, static_cast<int>(section.getShape().index())});
      } else {
        m_sections.push_back({"?", count, -1});
      }
    }
  }

  void ModelTree::indexTruss(const std::shared_ptr<const FEM::TRUSS::MeshData>& mesh, anaf::BRIDGE::GuiCalcBridge& bridge) {
    if (mesh == m_indexedTrussMesh) return;
    m_indexedTrussMesh = mesh;
    m_indexedBeamMesh.reset();
    m_supportedNodes.clear();
    m_overstressed.clear();
    m_materials.clear();
    m_sections.clear();
    m_maxDisplacement = 0.0;
    m_maxStress = 0.0;
    if (!mesh) return;
    std::map<std::uint32_t, std::size_t> materialCount;
    for (std::uint32_t i = 0; i < mesh->trussNodes.size(); ++i) {
      if (mesh->trussNodes[i].isSupported()) m_supportedNodes.push_back(i);
      const auto& d = mesh->trussNodes[i].getDisplacement();
      if (const double length = std::hypot(d[0], d[1], d[2]); length > m_maxDisplacement) {
        m_maxDisplacement = length;
        m_farthestNode = i;
      }
    }
    for (std::uint32_t e = 0; e < mesh->trussElements.size(); ++e) {
      const auto& element = mesh->trussElements[e];
      if (element.isWireframe) continue;
      ++materialCount[element.materialID];
      if (element.isStressExceeded) m_overstressed.push_back(e);
      if (const double stress = std::abs(static_cast<double>(element.stress)); stress > m_maxStress) {
        m_maxStress = stress;
        m_worstElement = e;
      }
    }
    std::lock_guard lock(bridge.dataMutex);
    for (const auto& [index, count] : materialCount) {
      const std::string name = index < bridge.allMaterials.size() ? std::string(bridge.allMaterials[index].getMaterialType()) : "?";
      m_materials.push_back({name, count, -1});
    }
  }

  void ModelTree::renderBeamTree(anaf::BRIDGE::GuiCalcBridge& bridge) {
    std::shared_ptr<const FEM::BEAM::MeshData> mesh;
    std::uint32_t selectedNode = noSelection, selectedElement = noSelection;
    {
      std::lock_guard lock(bridge.dataMutex);
      mesh = bridge.activeBeamMesh;
      selectedNode = bridge.selectedNodeId;
      selectedElement = bridge.selectedElementId;
    }
    indexBeam(mesh, bridge);
    if (!mesh) {
      ImGui::TextDisabled("No beam model yet");
      return;
    }

    if (ICONS::iconTreeNode("##nodes", ICONS::E_IconId::ModelNode, countLabel("Nodes", mesh->nodes.size()).c_str())) {
      clippedList("##node_list", mesh->nodes.size(), [&](const std::size_t i) {
        const auto& p = mesh->nodes[i].getLocation();
        if (row(ICONS::E_IconId::ModelNode, std::format("{}   ({:.4g}, {:.4g}, {:.4g})", i, p[0], p[1], p[2]), selectedNode == i)) {
          select(bridge, static_cast<std::uint32_t>(i), noSelection);
        }
      });
      ImGui::TreePop();
    }

    const bool elementsOpen = ICONS::iconTreeNode("##elements", ICONS::E_IconId::ModelElementBeam, countLabel("Elements", mesh->elements.size()).c_str());
    if (ImGui::BeginPopupContextItem("##elements_context")) {
      if (ICONS::iconMenuItem(ICONS::E_IconId::ModelElementBeam, "Assign to All Elements...") && onAssignAll) onAssignAll();
      if (ICONS::iconMenuItem(ICONS::E_IconId::ReleaseHinge, "Remove All End Releases") && onRemoveAllReleases) onRemoveAllReleases();
      ImGui::EndPopup();
    }
    ImGui::SetItemTooltip("Right click: assign to all elements");
    if (elementsOpen) {
      std::lock_guard lock(bridge.dataMutex); // section names
      clippedList("##element_list", mesh->elements.size(), [&](const std::size_t i) {
        const auto& element = mesh->elements[i];
        const std::string section = element.sectionID < bridge.allSections.size() ? bridge.allSections[element.sectionID].getName() : "?";
        std::string text = std::format("{}   {} - {}   {}", i, element.node1, element.node2, section);
        if (element.formulation == FEM::BEAM::E_Formulation::Timoshenko) text += "  TI";
        if (element.stress.available) text += std::format("   {:.1f} MPa", element.stress.maxVonMises / 1e6);
        const ICONS::E_IconId icon = element.endReleases != 0 ? ICONS::E_IconId::ReleaseHinge : ICONS::E_IconId::ModelElementBeam;
        if (row(icon, text, selectedElement == i, element.stress.isStressExceeded)) {
          // Picked without a node, like a viewport click on the element.
          bridge.selectedNodeId = noSelection;
          bridge.selectedElementId = static_cast<std::uint32_t>(i);
          bridge.dataVersion.fetch_add(1, std::memory_order_release);
        }
        if (element.endReleases != 0) ImGui::SetItemTooltip("Has end releases (hinges)");
      });
      ImGui::TreePop();
    }

    if (ICONS::iconTreeNode("##materials", ICONS::E_IconId::ModelMaterial, countLabel("Materials", m_materials.size()).c_str())) {
      for (const Usage& usage : m_materials) {
        if (row(ICONS::E_IconId::ModelMaterial, std::format("{}   ({} elements)", usage.name, usage.count), false) && onOpenMaterials) onOpenMaterials();
      }
      ImGui::TreePop();
    }

    if (ICONS::iconTreeNode("##sections", ICONS::E_IconId::ModelSection, countLabel("Sections", m_sections.size()).c_str())) {
      for (const Usage& usage : m_sections) {
        if (row(sectionIcon(usage.shape), std::format("{}   ({} elements)", usage.name, usage.count), false) && onOpenSections) onOpenSections();
      }
      ImGui::TreePop();
    }

    if (ICONS::iconTreeNode("##supports", ICONS::E_IconId::SupportPin, countLabel("Supports", m_supportedNodes.size()).c_str())) {
      clippedList("##support_list", m_supportedNodes.size(), [&](const std::size_t i) {
        const std::uint32_t n = m_supportedNodes[i];
        const auto& node = mesh->nodes[n];
        const auto& motion = node.getAllowedMotionDirections();
        const auto& rotation = node.getAllowedRotationAxes();
        const SupportKind kind = supportKind(motion.size(), rotation.size(), !alongGlobalAxes(motion) || !alongGlobalAxes(rotation), true);
        if (row(kind.icon, std::format("Node {}   {}", n, kind.name), selectedNode == n)) select(bridge, n, noSelection);
      });
      ImGui::TreePop();
    }

    if (ICONS::iconTreeNode("##loads", ICONS::E_IconId::LoadForce, "Loads")) {
      const bool selfWeight = mesh->gravity != std::array<double, 3>{};
      ICONS::iconText(ICONS::E_IconId::LoadGravity, selfWeight ? "Self weight: on" : "Self weight: off");
      for (std::size_t i = 0; i < mesh->nodalLoads.size(); ++i) {
        const auto& load = mesh->nodalLoads[i];
        const bool moment = load.force == std::array<double, 3>{};
        ImGui::PushID(static_cast<int>(i));
        const std::string text = moment ? std::format("Node {}   M = ({:.3g}, {:.3g}, {:.3g}) N m", load.node, load.moment[0], load.moment[1], load.moment[2])
                                        : std::format("Node {}   F = ({:.3g}, {:.3g}, {:.3g}) N", load.node, load.force[0], load.force[1], load.force[2]);
        if (row(moment ? ICONS::E_IconId::LoadMoment : ICONS::E_IconId::LoadForce, text, selectedNode == load.node)) select(bridge, load.node, noSelection);
        ImGui::PopID();
      }
      for (std::size_t i = 0; i < mesh->distributedLoads.size(); ++i) {
        const auto& load = mesh->distributedLoads[i];
        ImGui::PushID(static_cast<int>(i + mesh->nodalLoads.size()));
        const std::string text = std::format("Element {}   q = ({:.3g}, {:.3g}, {:.3g}) N/m {}", load.element, load.value[0], load.value[1],
                                             load.value[2], load.frame == FEM::BEAM::E_LoadFrame::Local ? "local" : "global");
        if (row(ICONS::E_IconId::LoadDistributed, text, selectedElement == load.element)) select(bridge, noSelection, load.element);
        ImGui::PopID();
      }
      ImGui::TreePop();
    }

    if (mesh->hasResults && ICONS::iconTreeNode("##results", ICONS::E_IconId::ResultStress, "Results")) {
      if (row(ICONS::E_IconId::ResultDisplacement, std::format("Max displacement   {:.4g} mm  (node {})", m_maxDisplacement * 1e3, m_farthestNode),
              selectedNode == m_farthestNode)) {
        select(bridge, m_farthestNode, noSelection);
      }
      if (m_maxStress > 0.0 && row(ICONS::E_IconId::ResultStress, std::format("Max von Mises   {:.4g} MPa  (element {})", m_maxStress / 1e6, m_worstElement),
                                   selectedElement == m_worstElement)) {
        select(bridge, noSelection, m_worstElement);
      }
      if (m_overstressed.empty()) {
        ICONS::iconText(ICONS::E_IconId::StatusSuccess, "No element exceeds its yield strength");
      } else {
        ICONS::iconText(ICONS::E_IconId::StatusWarning, std::format("{} elements over yield", m_overstressed.size()).c_str());
        clippedList("##overstressed_list", m_overstressed.size(), [&](const std::size_t i) {
          const std::uint32_t e = m_overstressed[i];
          if (row(ICONS::E_IconId::StatusError, std::format("Element {}   {:.1f} MPa", e, mesh->elements[e].stress.maxVonMises / 1e6), selectedElement == e, true)) {
            select(bridge, noSelection, e);
          }
        });
      }
      if (row(ICONS::E_IconId::ResultDiagram, "Beam Diagrams...", false) && onOpenDiagrams) onOpenDiagrams();
      ImGui::TreePop();
    }
  }

  void ModelTree::renderTrussTree(anaf::BRIDGE::GuiCalcBridge& bridge, const bool editable) {
    std::shared_ptr<const FEM::TRUSS::MeshData> mesh;
    std::uint32_t selectedNode = noSelection, selectedBar = noSelection;
    {
      std::lock_guard lock(bridge.dataMutex);
      mesh = bridge.activeMesh;
      selectedNode = bridge.selectedNodeId;
      selectedBar = bridge.selectedElementId;
    }
    indexTruss(mesh, bridge);
    if (!mesh) {
      ImGui::TextDisabled("No truss model yet");
      return;
    }

    if (ICONS::iconTreeNode("##nodes", ICONS::E_IconId::ModelNode, countLabel("Nodes", mesh->trussNodes.size()).c_str())) {
      clippedList("##node_list", mesh->trussNodes.size(), [&](const std::size_t i) {
        const auto& p = mesh->trussNodes[i].getLocation();
        if (row(ICONS::E_IconId::ModelNode, std::format("{}   ({:.4g}, {:.4g}, {:.4g})", i, p[0], p[1], p[2]), selectedNode == i)) {
          select(bridge, static_cast<std::uint32_t>(i), noSelection);
        }
      });
      ImGui::TreePop();
    }

    const bool barsOpen = ICONS::iconTreeNode("##bars", ICONS::E_IconId::ModelElementBar, countLabel("Bars", mesh->trussElements.size()).c_str());
    if (editable && ImGui::BeginPopupContextItem("##bars_context")) {
      if (ICONS::iconMenuItem(ICONS::E_IconId::ModelElementBar, "Assign to All Bars...") && onAssignAll) onAssignAll();
      ImGui::EndPopup();
    }
    if (editable) ImGui::SetItemTooltip("Right click: assign to all bars");
    if (barsOpen) {
      std::lock_guard lock(bridge.dataMutex); // material names
      clippedList("##bar_list", mesh->trussElements.size(), [&](const std::size_t i) {
        const auto& element = mesh->trussElements[i];
        std::string text;
        if (element.isWireframe) {
          text = std::format("{}   {} - {}   wireframe edge", i, element.node1, element.node2);
        } else {
          const std::string material = element.materialID < bridge.allMaterials.size() ? std::string(bridge.allMaterials[element.materialID].getMaterialType()) : "?";
          text = std::format("{}   {} - {}   {}   {:.4g} cm²", i, element.node1, element.node2, material, element.crossSectionArea * 1e4);
          if (mesh->hasResults) text += std::format("   {:.1f} MPa", static_cast<double>(element.stress) / 1e6);
        }
        if (row(ICONS::E_IconId::ModelElementBar, text, selectedBar == i, element.isStressExceeded)) {
          bridge.selectedNodeId = noSelection;
          bridge.selectedElementId = static_cast<std::uint32_t>(i);
          bridge.dataVersion.fetch_add(1, std::memory_order_release);
        }
      });
      ImGui::TreePop();
    }

    if (ICONS::iconTreeNode("##materials", ICONS::E_IconId::ModelMaterial, countLabel("Materials", m_materials.size()).c_str())) {
      for (const Usage& usage : m_materials) {
        if (row(ICONS::E_IconId::ModelMaterial, std::format("{}   ({} bars)", usage.name, usage.count), false) && onOpenMaterials) onOpenMaterials();
      }
      ImGui::TreePop();
    }

    if (ICONS::iconTreeNode("##supports", ICONS::E_IconId::SupportPin, countLabel("Supports", m_supportedNodes.size()).c_str())) {
      clippedList("##support_list", m_supportedNodes.size(), [&](const std::size_t i) {
        const std::uint32_t n = m_supportedNodes[i];
        const auto& node = mesh->trussNodes[n];
        const auto& movable = node.getMovable();
        const std::size_t free = node.hasInclinedSupport() ? node.getAllowedMotionDirections().size()
                                                           : static_cast<std::size_t>(std::ranges::count(movable, true));
        const SupportKind kind = supportKind(free, 0, node.hasInclinedSupport() && free != 0, false);
        if (row(kind.icon, std::format("Node {}   {}", n, kind.name), selectedNode == n)) select(bridge, n, noSelection);
      });
      ImGui::TreePop();
    }

    if (ICONS::iconTreeNode("##loads", ICONS::E_IconId::LoadForce, "Loads")) {
      ICONS::iconText(ICONS::E_IconId::LoadGravity, "Self weight: always on");
      clippedList("##force_list", mesh->appliedForces.size(), [&](const std::size_t i) {
        const auto& force = mesh->appliedForces[i];
        const auto& f = force.getForce();
        const std::uint32_t n = force.getAppliedNode();
        if (row(ICONS::E_IconId::LoadForce, std::format("Node {}   F = ({:.3g}, {:.3g}, {:.3g}) N", n, f[0], f[1], f[2]), selectedNode == n)) {
          select(bridge, n, noSelection);
        }
      });
      ImGui::TreePop();
    }

    if (mesh->hasResults && ICONS::iconTreeNode("##results", ICONS::E_IconId::ResultStress, "Results")) {
      if (row(ICONS::E_IconId::ResultDisplacement, std::format("Max displacement   {:.4g} mm  (node {})", m_maxDisplacement * 1e3, m_farthestNode),
              selectedNode == m_farthestNode)) {
        select(bridge, m_farthestNode, noSelection);
      }
      if (m_maxStress > 0.0 && row(ICONS::E_IconId::ResultStress, std::format("Max |stress|   {:.4g} MPa  (bar {})", m_maxStress / 1e6, m_worstElement),
                                   selectedBar == m_worstElement)) {
        select(bridge, noSelection, m_worstElement);
      }
      if (m_overstressed.empty()) {
        ICONS::iconText(ICONS::E_IconId::StatusSuccess, "No bar exceeds its yield strength");
      } else {
        ICONS::iconText(ICONS::E_IconId::StatusWarning, std::format("{} bars over yield", m_overstressed.size()).c_str());
        clippedList("##overstressed_list", m_overstressed.size(), [&](const std::size_t i) {
          const std::uint32_t e = m_overstressed[i];
          if (row(ICONS::E_IconId::StatusError, std::format("Bar {}   {:.1f} MPa", e, static_cast<double>(mesh->trussElements[e].stress) / 1e6),
                  selectedBar == e, true)) {
            select(bridge, noSelection, e);
          }
        });
      }
      ImGui::TreePop();
    }
  }

  void ModelTree::onImGuiRender() {
    auto& bridge = anaf::BRIDGE::buildBridge();
    if (!ImGui::Begin("Model Tree", &isOpen)) {
      ImGui::End();
      return;
    }

    // Read every frame, so the tree follows a type change (and its reset) immediately.
    const anaf::BRIDGE::E_ObjectType latestType = bridge.objectType.load();
    const std::string rootLabel = latestType == anaf::BRIDGE::E_ObjectType::NoType
      ? std::string("No model (Analyze menu)")
      : std::string(anaf::BRIDGE::getObjectTypeName(latestType));
    const ICONS::E_IconId rootIcon = latestType == anaf::BRIDGE::E_ObjectType::BeamFrame ? ICONS::E_IconId::ModelFrame
                                   : latestType == anaf::BRIDGE::E_ObjectType::NoType   ? ICONS::E_IconId::ModelMesh
                                                                                         : ICONS::E_IconId::ModelTruss;

    // Same ImGui ID (open state) for every type; open by default so the browser shows its groups.
    if (ICONS::iconTreeNode("ModelTreeRoot", rootIcon, rootLabel.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
      if (latestType == anaf::BRIDGE::E_ObjectType::BeamFrame) renderBeamTree(bridge);
      else if (latestType != anaf::BRIDGE::E_ObjectType::NoType) renderTrussTree(bridge, latestType == anaf::BRIDGE::E_ObjectType::TrussImportedOrEntered);
      ImGui::TreePop();
    }
    ImGui::End();
  }

} // namespace anaf::GUI end
