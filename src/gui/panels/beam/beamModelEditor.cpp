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


#include "beamModelEditor.hpp"

#include <guiMaterials/iconAtlas.hpp>
#include <guiMaterials/theme.hpp>

#include <beam/beamEngine/beamSolver/deformationUnderConstForce.hpp> // localAxes()
#include <beam/beamProperties/meshEdit.hpp>
#include <bridge/generalStatus.hpp>
#include <log/anaf_info.hpp>
#include <common/supportBasis.hpp>
#include <panels/beam/beamWorker.hpp>
#include <panels/beam/sectionCombo.hpp>
#include <panels/editorLayout.hpp>
#include <panels/propertyGrid.hpp>
#include <panels/truss/materialCombo.hpp>

#include "beam/beamSection/sectionLibrary.hpp"
#include "imgui.h"

#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <cstddef>
#include <exception>
#include <format>
#include <memory>
#include <span>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "common/globalConstantsAndFunctions.hpp"

namespace anaf::GUI {

  namespace {

    constexpr std::array<const char*, 2> formulations{"Euler-Bernoulli", "Timoshenko"};
    constexpr std::array<const char*, 2> loadFrames{"Global axes", "Local axes"};
    constexpr auto standardGravity = CONSTANTS::g_gravity;

    // End release presets of the combo; Custom shows the 12 check boxes.
    struct ReleasePreset {
      const char* name;
      unsigned int bits;
    };
    constexpr std::array<ReleasePreset, 4> releasePresets{{{"None (rigid)", 0U},
                                                            {"Hinge at A", FEM::BEAM::RELEASE::hinge},
                                                            {"Hinge at B", FEM::BEAM::RELEASE::atNode2(FEM::BEAM::RELEASE::hinge)},
                                                            {"Pinned both ends", FEM::BEAM::RELEASE::hinge | FEM::BEAM::RELEASE::atNode2(FEM::BEAM::RELEASE::hinge)}}};
    constexpr const char* releaseRules =
      "A released force is zero at that element end; the end moves on its own in that direction (local axes).\n"
      "My + Mz = a bending hinge (pin), torsion still carried. Release only one side of a joint: when every\n"
      "element end at a node is free about an axis, the node rotation there is undefined (held at zero; a moment\n"
      "on it is a mechanism). N or T released at both ends of an element is a mechanism too.";

    // Runs edit on a copy of the active beam snapshot (an empty one if there is none) under
    // dataMutex and publishes the copy. edit returns false to publish nothing.
    template <typename Edit>
    bool editModel(BRIDGE::GuiCalcBridge& bridge, Edit&& edit) {
      {
        std::lock_guard lock(bridge.dataMutex);
        auto mesh = bridge.activeBeamMesh ? std::make_shared<FEM::BEAM::MeshData>(*bridge.activeBeamMesh) : std::make_shared<FEM::BEAM::MeshData>();
        if (!edit(*mesh)) return false;
        FEM::BEAM::dropResults(*mesh);
        bridge.activeBeamMesh = std::move(mesh);
        bridge.isValid = false;
      }
      bridge.dataVersion.fetch_add(1, std::memory_order_release);
      return true;
    }

    std::shared_ptr<const FEM::BEAM::MeshData> currentMesh(BRIDGE::GuiCalcBridge& bridge) {
      std::lock_guard lock(bridge.dataMutex);
      return bridge.activeBeamMesh;
    }

    // True when the global axis lies in the span of an orthonormal basis.
    bool axisFree(const FEM::SUPPORT::Basis& basis, const std::size_t axis) {
      std::array<double, 3> unit{};
      unit[axis] = 1.0;
      const auto rest = FEM::SUPPORT::componentOutside(unit, basis);
      return std::sqrt(rest[0] * rest[0] + rest[1] * rest[1] + rest[2] * rest[2]) <= 1e-12;
    }

    bool alongGlobalAxes(const FEM::SUPPORT::Basis& basis) {
      return std::ranges::all_of(basis, [](const std::array<double, 3>& v) { return std::ranges::count(v, 0.0) == 2; });
    }

    void select(BRIDGE::GuiCalcBridge& bridge, const std::uint32_t node, const std::uint32_t element) {
      {
        std::lock_guard lock(bridge.dataMutex);
        bridge.selectedNodeId = node;
        bridge.selectedElementId = element;
      }
      bridge.dataVersion.fetch_add(1, std::memory_order_release); // redraw the highlight
    }

    std::uint32_t findSectionByName(const BRIDGE::GuiCalcBridge& bridge, const char* name) {
      for (std::uint32_t i = 0; i < bridge.allSections.size(); ++i) {
        if (FEM::BEAM::sameSectionName(bridge.allSections[i].getName(), name)) return i;
      }
      return 0;
    }

    // Floating tool window opened from a command, near the top left of the viewport area.
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

  std::optional<FEM::SUPPORT::Basis> BeamModelEditor::SupportInput::allowedBasis(std::string& error) const {
    if (!inclined) {
      FEM::SUPPORT::Basis allowed;
      for (std::size_t axis = 0; axis < 3; ++axis) {
        if (fixed[axis]) continue;
        std::array<double, 3> unit{};
        unit[axis] = 1.0;
        allowed.push_back(unit);
      }
      return allowed;
    }
    try {
      const auto basis = FEM::SUPPORT::orthonormalize(FEM::SUPPORT::Basis(std::span<const std::array<double, 3>>(vectors.data(), static_cast<std::size_t>(vectorCount))));
      return vectorsRestrained ? FEM::SUPPORT::orthogonalComplement(basis) : basis;
    } catch (const std::exception&) {
      error = "The vectors must be non-zero and linearly independent.";
      return std::nullopt;
    }
  }

  void BeamModelEditor::SupportInput::load(const FEM::SUPPORT::Basis& allowed) {
    inclined = !alongGlobalAxes(allowed);
    for (std::size_t axis = 0; axis < 3; ++axis) fixed[axis] = !axisFree(allowed, axis);
    if (!inclined) return;
    const auto shown = vectorsRestrained ? FEM::SUPPORT::orthogonalComplement(allowed) : allowed;
    vectorCount = static_cast<int>(shown.size());
    for (std::size_t i = 0; i < shown.size(); ++i) vectors[i] = shown[i];
  }

  void BeamModelEditor::SupportInput::setGlobal(const bool fixAll) {
    inclined = false;
    fixed = {fixAll, fixAll, fixAll};
  }

  void BeamModelEditor::resetState() {
    m_focus = E_Focus::Model;
    m_nodePosition = {0.0, 0.0, 0.0};
    m_motion = SupportInput{};
    m_rotation = SupportInput{};
    m_force = {0.0, 0.0, 0.0};
    m_moment = {0.0, 0.0, 0.0};
    m_loadedNode = noSelection;
    m_loadedElement = noSelection;
    m_element = ElementInput{};
    m_distributed = {0.0, 0.0, 0.0};
    m_distributedFrame = 0;
    m_newNode = {0.0, 0.0, 0.0};
    m_newNodeA = 0;
    m_newNodeB = 1;
    m_newElement = ElementInput{};
    m_status.clear();
    m_statusIsError = false;
  }

  void BeamModelEditor::setStatus(std::string message, const bool error) {
    if (error) anaf::LOG::warn("Beam editor: {}", message);
    m_status = std::move(message);
    m_statusIsError = error;
  }

  void BeamModelEditor::openAddNode() { m_addNodeOpen = true; }
  void BeamModelEditor::openAddElement() { m_addElementOpen = true; }
  void BeamModelEditor::openAssignAll() { m_assignAllOpen = true; }

  void BeamModelEditor::removeAllReleases() {
    std::size_t removed = 0;
    editModel(BRIDGE::buildBridge(), [&](FEM::BEAM::MeshData& mesh) {
      for (auto& element : mesh.elements) {
        if (element.endReleases == 0) continue;
        element.endReleases = 0;
        ++removed;
      }
      return removed > 0;
    });
    m_loadedElement = noSelection; // reload the selected element's releases
    setStatus(removed > 0 ? std::format("End releases removed from {} elements", removed) : "No element has end releases", false);
  }

  bool BeamModelEditor::solve() {
    auto& bridge = BRIDGE::buildBridge();
    const auto mesh = currentMesh(bridge);
    if (bridge.isRunning.load() || !mesh || mesh->elements.empty()) return false;
    m_status.clear();
    BEAM_WORKER::startSolve(bridge, mesh);
    return true;
  }

  void BeamModelEditor::syncSelection(const std::uint32_t node, const std::uint32_t element) {
    auto& bridge = BRIDGE::buildBridge();
    // The latest valid pick decides what the panel shows; a lost pick falls back to the other one.
    if (node != m_loadedNode && node != noSelection) m_focus = E_Focus::Node;
    if (element != m_loadedElement && element != noSelection) m_focus = E_Focus::Element;
    if (m_focus == E_Focus::Node && node == noSelection) m_focus = element != noSelection ? E_Focus::Element : E_Focus::Model;
    if (m_focus == E_Focus::Element && element == noSelection) m_focus = node != noSelection ? E_Focus::Node : E_Focus::Model;

    if (node != m_loadedNode) {
      m_loadedNode = node;
      m_force = {0.0, 0.0, 0.0};
      m_moment = {0.0, 0.0, 0.0};
      std::lock_guard lock(bridge.dataMutex);
      if (node != noSelection && bridge.activeBeamMesh && node < bridge.activeBeamMesh->nodes.size()) {
        const auto& selected = bridge.activeBeamMesh->nodes[node];
        m_nodePosition = selected.getLocation();
        m_motion.load(selected.getAllowedMotionDirections());
        m_rotation.load(selected.getAllowedRotationAxes());
        for (const auto& load : bridge.activeBeamMesh->nodalLoads) {
          if (load.node != node) continue;
          for (std::size_t axis = 0; axis < 3; ++axis) {
            m_force[axis] += load.force[axis];
            m_moment[axis] += load.moment[axis];
          }
        }
      }
    }
    if (element != m_loadedElement) {
      m_loadedElement = element;
      std::lock_guard lock(bridge.dataMutex);
      if (element != noSelection && bridge.activeBeamMesh && element < bridge.activeBeamMesh->elements.size()) {
        const auto& selected = bridge.activeBeamMesh->elements[element];
        if (selected.materialID < bridge.allMaterials.size()) m_element.materialID = bridge.allMaterials[selected.materialID].getMaterialID();
        if (selected.sectionID < bridge.allSections.size()) m_element.sectionID = bridge.allSections[selected.sectionID].getSectionID();
        m_element.formulation = static_cast<int>(selected.formulation);
        m_element.orientation = selected.orientation;
        m_element.releases = selected.endReleases;
        m_element.customReleases = false;
        // The next new element starts like the last one looked at.
        m_newElement = m_element;
        m_newElement.releases = 0;
      }
    }
  }

  void BeamModelEditor::renderHeader(const std::uint32_t node, const std::uint32_t element) {
    std::string title = "Model";
    ICONS::E_IconId icon = ICONS::E_IconId::ModelFrame;
    if (m_focus == E_Focus::Node) {
      title = std::format("Node {}", node);
      icon = ICONS::E_IconId::ModelNode;
    } else if (m_focus == E_Focus::Element) {
      const auto mesh = currentMesh(BRIDGE::buildBridge());
      title = mesh && element < mesh->elements.size()
        ? std::format("Element {}  ({} - {})", element, mesh->elements[element].node1, mesh->elements[element].node2)
        : std::format("Element {}", element);
      icon = ICONS::E_IconId::ModelElementBeam;
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

  void BeamModelEditor::renderModel() {
    auto& bridge = BRIDGE::buildBridge();
    const auto mesh = currentMesh(bridge);

    if (!mesh || mesh->nodes.empty()) {
      ImGui::PushTextWrapPos(0.0f);
      ImGui::TextDisabled("No model yet. Start from a built-in model, the example frame, or add nodes and elements.");
      ImGui::PopTextWrapPos();
      if (ICONS::iconTextButton(ICONS::E_IconId::ModelLibrary, "Open Example...", ImVec2(-FLT_MIN, 0.0f)) && onOpenLibrary) onOpenLibrary();
      if (ICONS::iconTextButton(ICONS::E_IconId::ModelFrame, "Load Example Frame", ImVec2(-FLT_MIN, 0.0f))) loadExample();
      ImGui::SetItemTooltip("A 3D portal frame: HEB 200 columns, an IPE 300 girder with a uniform load, clamped bases.");
      if (ICONS::iconTextButton(ICONS::E_IconId::ModelNode, "Add Node...", ImVec2(-FLT_MIN, 0.0f))) openAddNode();
      return;
    }

    const auto supported = std::ranges::count_if(mesh->nodes, [](const FEM::BEAM::Node& n) { return n.isSupported(); });
    const bool selfWeightOn = mesh->gravity != std::array<double, 3>{};
    if (PROPERTY::beginFixed("##beam_model")) {
      if (PROPERTY::group("Model")) {
        PROPERTY::value("Nodes", std::to_string(mesh->nodes.size()));
        PROPERTY::value("Elements", std::to_string(mesh->elements.size()));
        PROPERTY::value("Supports", std::to_string(supported));
        PROPERTY::value("Nodal loads", std::to_string(mesh->nodalLoads.size()));
        PROPERTY::value("Line loads", std::to_string(mesh->distributedLoads.size()));
        PROPERTY::row("Self weight", "Density x area x g along -Y, on every element.");
        bool selfWeight = selfWeightOn;
        if (ImGui::Checkbox("##self_weight", &selfWeight)) {
          editModel(bridge, [&](FEM::BEAM::MeshData& edited) {
            edited.gravity = selfWeight ? standardGravity : std::array<double, 3>{};
            return true;
          });
        }
      }
      if (mesh->hasResults && PROPERTY::group("Results")) {
        double maxDisplacement = 0.0, maxVonMises = 0.0;
        std::size_t exceeded = 0, withoutStress = 0;
        std::uint32_t worst = noSelection, farthest = noSelection;
        for (std::uint32_t n = 0; n < mesh->nodes.size(); ++n) {
          const auto& d = mesh->nodes[n].getDisplacement();
          if (const double length = std::hypot(d[0], d[1], d[2]); length > maxDisplacement) {
            maxDisplacement = length;
            farthest = n;
          }
        }
        for (std::uint32_t e = 0; e < mesh->elements.size(); ++e) {
          const auto& stress = mesh->elements[e].stress;
          if (!stress.available) {
            ++withoutStress;
            continue;
          }
          if (stress.isStressExceeded) ++exceeded;
          if (stress.maxVonMises > maxVonMises) {
            maxVonMises = stress.maxVonMises;
            worst = e;
          }
        }
        PROPERTY::value("Max |u|", std::format("{:.4g}  (node {})", maxDisplacement * 1e3, farthest), "mm",
                        "Largest nodal displacement; Results in the Model Tree select it.");
        if (worst != noSelection) PROPERTY::value("Max von Mises", std::format("{:.4g}  (element {})", maxVonMises / 1e6, worst), "MPa");
        PROPERTY::value("Over yield", std::to_string(exceeded), nullptr, "Elements whose von Mises stress exceeds the yield strength.");
        if (withoutStress > 0) PROPERTY::value("No stresses", std::to_string(withoutStress), nullptr, "Elements with a general section.");
      }
      PROPERTY::end();
    }
    if (mesh->hasResults) {
      const bool valid = bridge.isValid.load();
      ICONS::iconText(valid ? ICONS::E_IconId::StatusSuccess : ICONS::E_IconId::StatusWarning,
                      valid ? "Solved, energy check passed" : "Results from a file, or energy check not passed");
    } else {
      ICONS::iconText(ICONS::E_IconId::StatusInfo, "No results yet: Run Solver (F5).");
    }
    ImGui::Spacing();
    if (ICONS::iconTextButton(ICONS::E_IconId::ModelElementBeam, "Assign to All Elements...", ImVec2(-FLT_MIN, 0.0f))) openAssignAll();
    ImGui::SetItemTooltip("Formulation, material and section of every element; end releases off");
    ImGui::Spacing();
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("Click a node or an element in the viewport or in the Model Tree to edit it.");
    ImGui::PopTextWrapPos();
  }

  void BeamModelEditor::renderNode(const std::uint32_t node, const bool withLoads) {
    auto& bridge = BRIDGE::buildBridge();
    const auto mesh = currentMesh(bridge);
    if (!mesh || node >= mesh->nodes.size()) return;

    if (PROPERTY::beginFixed("##beam_node_geometry")) {
      if (PROPERTY::group("Geometry")) PROPERTY::inputVector3("Position", m_nodePosition.data(), "m");
      PROPERTY::end();
    }
    if (ICONS::iconTextButton(ICONS::E_IconId::ModelNode, "Move", ImVec2(LAYOUT::splitWidth(2), 0.0f))) {
      if (editModel(bridge, [&](FEM::BEAM::MeshData& edited) {
            if (node >= edited.nodes.size()) return false;
            edited.nodes[node].setLocation(m_nodePosition);
            return true;
          })) {
        setStatus(std::format("Node {} moved", node), false);
      }
    }
    ImGui::SameLine();
    if (ICONS::iconTextButton(ICONS::E_IconId::ActionRemove, "Delete Node", ImVec2(-FLT_MIN, 0.0f))) {
      if (editModel(bridge, [&](FEM::BEAM::MeshData& edited) {
            if (node >= edited.nodes.size()) return false;
            FEM::BEAM::deleteNode(edited, node);
            return true;
          })) {
        select(bridge, noSelection, noSelection);
        m_loadedNode = noSelection;
        setStatus(std::format("Node {} deleted with its elements; later node ids moved down by one", node), false);
        return;
      }
    }
    ImGui::SetItemTooltip("Removes the node, its elements and their loads; later node ids move down by one");

    ImGui::Spacing();
    if (PROPERTY::beginFixed("##beam_node_preset")) {
      PROPERTY::row("Support preset", "Fixed: every DOF held. Pinned: translations held, rotations free. Free: no support.");
      const float third = (ImGui::GetContentRegionAvail().x - 2.0f * ImGui::GetStyle().ItemSpacing.x) / 3.0f;
      if (ImGui::Button("Fixed", ImVec2(third, 0.0f))) {
        m_motion.setGlobal(true);
        m_rotation.setGlobal(true);
      }
      ImGui::SameLine();
      if (ImGui::Button("Pinned", ImVec2(third, 0.0f))) {
        m_motion.setGlobal(true);
        m_rotation.setGlobal(false);
      }
      ImGui::SameLine();
      if (ImGui::Button("Free", ImVec2(-FLT_MIN, 0.0f))) {
        m_motion.setGlobal(false);
        m_rotation.setGlobal(false);
      }
      PROPERTY::end();
    }
    const auto motion = renderSupportGroup("motion", "Translation", "U", m_motion);
    const auto rotation = renderSupportGroup("rotation", "Rotation", "R", m_rotation);
    ImGui::BeginDisabled(!motion || !rotation);
    if (ICONS::iconTextButton(ICONS::E_IconId::SupportFixed, "Apply Support", ImVec2(-FLT_MIN, 0.0f))) {
      if (editModel(bridge, [&](FEM::BEAM::MeshData& edited) {
            if (node >= edited.nodes.size()) return false;
            edited.nodes[node].setAllowedMotionDirections(*motion);
            edited.nodes[node].setAllowedRotationAxes(*rotation);
            return true;
          })) {
        setStatus(std::format("Support of node {} updated", node), false);
      }
    }
    ImGui::EndDisabled();

    if (withLoads) {
      ImGui::Spacing();
      bool loadOpen = false;
      if (PROPERTY::beginFixed("##beam_nodal_load")) {
        loadOpen = PROPERTY::group("Nodal Load");
        if (loadOpen) {
          PROPERTY::inputVector3("Force", m_force.data(), "N", "%.4g", "Global axes.");
          PROPERTY::inputVector3("Moment", m_moment.data(), "N m", "%.4g", "About the global axes.");
        }
        PROPERTY::end();
      }
      if (loadOpen) {
        const auto setLoad = [&](const std::array<double, 3>& force, const std::array<double, 3>& moment) {
          return editModel(bridge, [&](FEM::BEAM::MeshData& edited) {
            if (node >= edited.nodes.size()) return false;
            std::erase_if(edited.nodalLoads, [&](const FEM::BEAM::NodalLoad& load) { return load.node == node; });
            if (force != std::array<double, 3>{} || moment != std::array<double, 3>{}) edited.nodalLoads.push_back({node, force, moment});
            return true;
          });
        };
        if (ICONS::iconTextButton(ICONS::E_IconId::LoadForce, "Apply Load", ImVec2(LAYOUT::splitWidth(2), 0.0f))) {
          if (setLoad(m_force, m_moment)) setStatus(std::format("Load on node {} updated", node), false);
        }
        ImGui::SameLine();
        if (ICONS::iconTextButton(ICONS::E_IconId::ActionRemove, "Remove Load", ImVec2(-FLT_MIN, 0.0f))) {
          m_force = {0.0, 0.0, 0.0};
          m_moment = {0.0, 0.0, 0.0};
          if (setLoad(m_force, m_moment)) setStatus(std::format("Load on node {} removed", node), false);
        }
      }
    }

    if (mesh->hasResults) {
      ImGui::Spacing();
      if (PROPERTY::beginFixed("##beam_node_results")) {
        if (PROPERTY::group("Results")) {
          const auto& d = mesh->nodes[node].getDisplacement();
          const auto& r = mesh->nodes[node].getRotation();
          PROPERTY::value("Displacement", std::format("{:.4g}, {:.4g}, {:.4g}", d[0] * 1e3, d[1] * 1e3, d[2] * 1e3), "mm");
          PROPERTY::value("Magnitude", std::format("{:.4g}", std::hypot(d[0], d[1], d[2]) * 1e3), "mm");
          PROPERTY::value("Rotation", std::format("{:.4g}, {:.4g}, {:.4g}", r[0] * 1e3, r[1] * 1e3, r[2] * 1e3), "mrad");
        }
        PROPERTY::end();
      }
    }
  }

  std::optional<FEM::SUPPORT::Basis> BeamModelEditor::renderSupportGroup(const char* id, const char* title, const char* axisNames,
                                                                        SupportInput& input) {
    ImGui::PushID(id);
    bool open = false;
    if (PROPERTY::beginFixed("##support_grid")) {
      open = PROPERTY::group(title);
      if (open) {
        int mode = input.inclined ? 1 : 0;
        PROPERTY::row("Directions", "Global axes: fix single global directions.\nInclined: give the directions as vectors (skewed support).");
        constexpr std::array<const char*, 2> directionModes{"Global axes", "Inclined vectors"};
        ImGui::Combo("##directions", &mode, directionModes.data(), static_cast<int>(directionModes.size()));
        if ((mode == 1) != input.inclined) {
          // Switching keeps the support: the current one is shown in the other mode.
          std::string ignored;
          const auto current = input.allowedBasis(ignored);
          input.inclined = mode == 1;
          if (current && input.inclined) {
            const auto shown = input.vectorsRestrained ? FEM::SUPPORT::orthogonalComplement(*current) : *current;
            if (!shown.empty()) {
              input.vectorCount = static_cast<int>(shown.size());
              for (std::size_t i = 0; i < shown.size(); ++i) input.vectors[i] = shown[i];
            }
          } else if (current) {
            for (std::size_t axis = 0; axis < 3; ++axis) input.fixed[axis] = !axisFree(*current, axis);
          }
        }

        if (!input.inclined) {
          PROPERTY::row("Fixed");
          for (std::size_t axis = 0; axis < 3; ++axis) {
            if (axis > 0) ImGui::SameLine();
            ImGui::Checkbox(std::format("{}{}", axisNames, "xyz"[axis]).c_str(), &input.fixed[axis]);
          }
        } else {
          bool restrained = input.vectorsRestrained;
          PROPERTY::row("Vectors give", "Restrained: the directions the support holds.\nAllowed: the directions left free.");
          if (ImGui::RadioButton("Restrained", restrained)) restrained = true;
          ImGui::SameLine();
          if (ImGui::RadioButton("Allowed", !restrained)) restrained = false;
          if (restrained != input.vectorsRestrained) {
            std::string ignored;
            if (const auto current = input.allowedBasis(ignored)) {
              const auto shown = restrained ? FEM::SUPPORT::orthogonalComplement(*current) : *current;
              if (!shown.empty()) {
                input.vectorCount = static_cast<int>(shown.size());
                for (std::size_t i = 0; i < shown.size(); ++i) input.vectors[i] = shown[i];
              }
            }
            input.vectorsRestrained = restrained;
          }
          PROPERTY::row("Vector count");
          for (int count = 1; count <= 3; ++count) {
            if (count > 1) ImGui::SameLine();
            ImGui::RadioButton(std::format("{}##count", count).c_str(), &input.vectorCount, count);
          }
          for (int i = 0; i < input.vectorCount; ++i) {
            PROPERTY::inputVector3(std::format("d{}", i + 1).c_str(), input.vectors[static_cast<std::size_t>(i)].data(), nullptr, "%.4g",
                                   "Direction in global axes (normalized by the solver).");
          }
        }
      }
      PROPERTY::end();
    }

    std::string error;
    const auto allowed = input.allowedBasis(error);
    if (open) {
      ImGui::PushTextWrapPos(0.0f);
      if (input.inclined && std::string_view(id) == "rotation") {
        ImGui::TextDisabled("Solved as given; files store rotation supports per global axis only (export warns).");
      }
      if (!allowed) {
        ImGui::TextColored(THEME::theme().bad, "%s", error.c_str());
      } else if (input.inclined) {
        if (allowed->empty()) ImGui::TextDisabled("Held in every direction.");
        else if (allowed->size() == 3) ImGui::TextDisabled("Free in every direction.");
        else if (allowed->size() == 1) ImGui::TextDisabled("Free along (%.3f, %.3f, %.3f) only.", (*allowed)[0][0], (*allowed)[0][1], (*allowed)[0][2]);
        else {
          const auto normal = FEM::SUPPORT::orthogonalComplement(*allowed)[0];
          ImGui::TextDisabled("Held along (%.3f, %.3f, %.3f) only.", normal[0], normal[1], normal[2]);
        }
      }
      ImGui::PopTextWrapPos();
    }
    ImGui::PopID();
    return allowed;
  }

  void BeamModelEditor::elementRows(ElementInput& input, const char* id) {
    auto& bridge = BRIDGE::buildBridge();
    ImGui::PushID(id);
    PROPERTY::row("Material");
    PROPERTY::reserveBrowse();
    materialCombo(bridge, "##material", input.materialID);
    if (PROPERTY::browseButton("##materials", "Open the Material Handler") && onOpenMaterialHandler) onOpenMaterialHandler();
    PROPERTY::row("Section");
    PROPERTY::reserveBrowse();
    sectionCombo(bridge, "##section", input.sectionID);
    if (PROPERTY::browseButton("##sections", "Open the Section Handler") && onOpenSectionHandler) onOpenSectionHandler();
    PROPERTY::row("Formulation", "Euler-Bernoulli: no shear deformation. Timoshenko: with shear deformation (deep members).");
    ImGui::Combo("##formulation", &input.formulation, formulations.data(), static_cast<int>(formulations.size()));
    PROPERTY::inputVector3("Orientation v", input.orientation.data(), nullptr, "%.3g",
                           "Vector in the local x-y plane (global axes). 0 0 0 = automatic: local y as close to +Y as\n"
                           "possible, +X for vertical members. A section's height lies along local y.");

    // End releases: a preset, or Custom with the 12 section-force check boxes.
    const auto preset = std::ranges::find_if(releasePresets, [&](const ReleasePreset& p) { return p.bits == input.releases; });
    const bool custom = preset == releasePresets.end();
    PROPERTY::row("End releases", releaseRules);
    if (ImGui::BeginCombo("##releases", custom ? "Custom" : preset->name)) {
      for (const ReleasePreset& p : releasePresets) {
        const ICONS::E_IconId icon = p.bits == 0 ? ICONS::E_IconId::ModelElementBeam : ICONS::E_IconId::ReleaseHinge;
        if (ICONS::iconSelectable(icon, p.name, !custom && !input.customReleases && p.bits == input.releases)) {
          input.releases = p.bits;
          input.customReleases = false;
        }
      }
      // Custom starts from the current bits.
      if (ICONS::iconSelectable(ICONS::E_IconId::ReleaseTorsion, "Custom...", custom || input.customReleases)) input.customReleases = true;
      ImGui::EndCombo();
    }
    if (custom || input.customReleases) {
      PROPERTY::row("", releaseRules);
      constexpr std::array<const char*, 6> sectionForceNames{"N", "Vy", "Vz", "T", "My", "Mz"};
      if (ImGui::BeginTable("##releases_table", 7, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_BordersInnerV)) {
        ImGui::TableSetupColumn("End");
        for (const char* force : sectionForceNames) ImGui::TableSetupColumn(force);
        ImGui::TableHeadersRow();
        for (int end = 0; end < 2; ++end) {
          ImGui::TableNextRow();
          ImGui::TableNextColumn();
          ImGui::TextUnformatted(end == 0 ? "A" : "B");
          for (int dof = 0; dof < 6; ++dof) {
            ImGui::TableNextColumn();
            ImGui::PushID(end * 6 + dof);
            ImGui::CheckboxFlags("##release", &input.releases, 1U << (6 * end + dof));
            ImGui::PopID();
          }
        }
        ImGui::EndTable();
      }
    }
    ImGui::PopID();
  }

  void BeamModelEditor::renderElement(const std::uint32_t element, const bool withLoads) {
    auto& bridge = BRIDGE::buildBridge();
    const auto mesh = currentMesh(bridge);
    if (!mesh || element >= mesh->elements.size()) return;
    const auto& selected = mesh->elements[element];
    const auto& a = mesh->nodes[selected.node1].getLocation();
    const auto& b = mesh->nodes[selected.node2].getLocation();

    if (PROPERTY::beginFixed("##beam_element")) {
      if (PROPERTY::group("Element")) {
        PROPERTY::value("Nodes", std::format("{} - {}", selected.node1, selected.node2));
        PROPERTY::value("Length", std::format("{:.4g}", std::hypot(b[0] - a[0], b[1] - a[1], b[2] - a[2])), "m");
        elementRows(m_element, "selected");
      }
      PROPERTY::end();
    }

    // Writes the inputs into an element; an error message if they are unusable. Caller holds dataMutex (editModel).
    const auto assign = [&](FEM::BEAM::BeamElement& target, const FEM::BEAM::MeshData& edited) -> std::string {
      const auto material = bridge.findMaterialIndex(m_element.materialID);
      const auto section = bridge.findSectionIndex(m_element.sectionID);
      if (!material || !section) return "choose a material and a section";
      try {
        (void)FEM::BEAM::localAxes(edited.nodes[target.node1].getLocation(), edited.nodes[target.node2].getLocation(), m_element.orientation);
      } catch (const std::exception& exception) {
        return exception.what();
      }
      target.materialID = *material;
      target.sectionID = *section;
      target.formulation = static_cast<FEM::BEAM::E_Formulation>(m_element.formulation);
      target.orientation = m_element.orientation;
      target.endReleases = static_cast<std::uint16_t>(m_element.releases & FEM::BEAM::RELEASE::allMask);
      return {};
    };
    if (ICONS::iconTextButton(ICONS::E_IconId::ModelElementBeam, "Apply", ImVec2(LAYOUT::splitWidth(2), 0.0f))) {
      std::string error = "element not found";
      const bool ok = editModel(bridge, [&](FEM::BEAM::MeshData& edited) {
        if (element >= edited.elements.size()) return false;
        error = assign(edited.elements[element], edited);
        return error.empty();
      });
      if (ok) setStatus(std::format("Element {} updated", element), false);
      else setStatus("Element not updated: " + error, true);
    }
    ImGui::SetItemTooltip("Writes material, section, formulation, orientation and end releases into the element");
    ImGui::SameLine();
    if (ICONS::iconTextButton(ICONS::E_IconId::ActionRemove, "Delete Element", ImVec2(-FLT_MIN, 0.0f))) {
      if (editModel(bridge, [&](FEM::BEAM::MeshData& edited) {
            if (element >= edited.elements.size()) return false;
            std::uint32_t index = 0;
            FEM::BEAM::removeElements(edited, [&](const FEM::BEAM::BeamElement&) { return index++ == element; });
            return true;
          })) {
        select(bridge, noSelection, noSelection);
        setStatus(std::format("Element {} deleted", element), false);
        return;
      }
    }

    if (withLoads) {
      ImGui::Spacing();
      bool loadOpen = false;
      std::size_t onElement = 0;
      if (PROPERTY::beginFixed("##beam_element_loads")) {
        loadOpen = PROPERTY::group("Distributed Loads");
        if (loadOpen) {
          for (const auto& load : mesh->distributedLoads) {
            if (load.element != element) continue;
            ++onElement;
            PROPERTY::value(std::format("q{}  ({})", onElement, load.frame == FEM::BEAM::E_LoadFrame::Local ? "local" : "global").c_str(),
                            std::format("{:.4g}, {:.4g}, {:.4g}", load.value[0], load.value[1], load.value[2]), "N/m");
          }
          if (onElement == 0) PROPERTY::value("On element", "none");
          PROPERTY::inputVector3("New load q", m_distributed.data(), "N/m");
          PROPERTY::row("Axes");
          ImGui::Combo("##q_frame", &m_distributedFrame, loadFrames.data(), static_cast<int>(loadFrames.size()));
        }
        PROPERTY::end();
      }
      if (loadOpen) {
        if (ICONS::iconTextButton(ICONS::E_IconId::LoadDistributed, "Add Load", ImVec2(LAYOUT::splitWidth(2), 0.0f))) {
          if (m_distributed == std::array<double, 3>{}) {
            setStatus("Enter a non-zero load", true);
          } else if (editModel(bridge, [&](FEM::BEAM::MeshData& edited) {
                       if (element >= edited.elements.size()) return false;
                       edited.distributedLoads.push_back({element, m_distributed, static_cast<FEM::BEAM::E_LoadFrame>(m_distributedFrame)});
                       return true;
                     })) {
            setStatus(std::format("Uniform load added to element {}", element), false);
          }
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(onElement == 0);
        if (ICONS::iconTextButton(ICONS::E_IconId::ActionRemove, "Remove Loads", ImVec2(-FLT_MIN, 0.0f))) {
          if (editModel(bridge, [&](FEM::BEAM::MeshData& edited) {
                std::erase_if(edited.distributedLoads, [&](const FEM::BEAM::DistributedLoad& load) { return load.element == element; });
                return true;
              })) {
            setStatus(std::format("Uniform loads of element {} removed", element), false);
          }
        }
        ImGui::EndDisabled();
      }
    }

    if (mesh->hasResults) {
      ImGui::Spacing();
      if (PROPERTY::beginFixed("##beam_element_results")) {
        if (PROPERTY::group("Results")) {
          const auto& stress = selected.stress;
          if (stress.available) {
            PROPERTY::value("Max von Mises", std::format("{:.4g}", stress.maxVonMises / 1e6), "MPa");
            PROPERTY::value("Yield check", stress.isStressExceeded ? "exceeded" : "ok");
          } else {
            PROPERTY::value("Stresses", "none (general section)");
          }
        }
        PROPERTY::end();
      }
      if (ICONS::iconTextButton(ICONS::E_IconId::ResultDiagram, "Show Diagrams", ImVec2(-FLT_MIN, 0.0f)) && onOpenDiagrams) onOpenDiagrams();
      ImGui::SetItemTooltip("Section forces, displacements and stresses along this element");
    }
  }

  void BeamModelEditor::renderAddNodeWindow() {
    if (!beginToolWindow("Add Node", m_addNodeOpen, 22.0f)) return;
    auto& bridge = BRIDGE::buildBridge();
    ImGui::BeginDisabled(bridge.isRunning.load());
    if (PROPERTY::beginFixed("##add_node", 5.0f)) {
      PROPERTY::inputVector3("Position", m_newNode.data(), "m");
      PROPERTY::end();
    }
    if (ICONS::iconTextButton(ICONS::E_IconId::ActionAdd, "Add Node", ImVec2(-FLT_MIN, 0.0f))) {
      std::uint32_t added = 0;
      editModel(bridge, [&](FEM::BEAM::MeshData& mesh) {
        added = static_cast<std::uint32_t>(mesh.nodes.size());
        mesh.nodes.emplace_back(added, m_newNode[0], m_newNode[1], m_newNode[2]);
        return true;
      });
      select(bridge, added, noSelection);
      setStatus(std::format("Node {} added", added), false);
    }
    ImGui::EndDisabled();
    ImGui::TextDisabled("The new node is selected; the window stays open.");
    ImGui::End();
  }

  void BeamModelEditor::renderAddElementWindow() {
    if (!beginToolWindow("Add Element", m_addElementOpen, 26.0f)) return;
    auto& bridge = BRIDGE::buildBridge();
    ImGui::BeginDisabled(bridge.isRunning.load());
    if (PROPERTY::beginFixed("##add_element")) {
      PROPERTY::row("Nodes A - B", "Ids of the two end nodes; after Add, A takes B for the next element of a chain.");
      ImGui::SetNextItemWidth(LAYOUT::splitWidth(2));
      ImGui::InputScalar("##node_a", ImGuiDataType_U32, &m_newNodeA);
      ImGui::SameLine();
      ImGui::SetNextItemWidth(-FLT_MIN);
      ImGui::InputScalar("##node_b", ImGuiDataType_U32, &m_newNodeB);
      elementRows(m_newElement, "new");
      PROPERTY::end();
    }
    if (ICONS::iconTextButton(ICONS::E_IconId::ActionAdd, "Add Element", ImVec2(-FLT_MIN, 0.0f))) {
      std::string error;
      std::uint32_t added = 0;
      const bool ok = editModel(bridge, [&](FEM::BEAM::MeshData& mesh) {
        const auto count = mesh.nodes.size();
        const auto a = m_newNodeA, b = m_newNodeB;
        if (a >= count || b >= count) {
          error = std::format("nodes {} and {} must both exist (the model has {} nodes)", a, b, count);
          return false;
        }
        if (a == b) {
          error = "an element needs two different nodes";
          return false;
        }
        const bool duplicate = std::ranges::any_of(mesh.elements, [&](const FEM::BEAM::BeamElement& e) {
          return (e.node1 == a && e.node2 == b) || (e.node1 == b && e.node2 == a);
        });
        if (duplicate) {
          error = std::format("an element between nodes {} and {} already exists", a, b);
          return false;
        }
        const auto material = bridge.findMaterialIndex(m_newElement.materialID);
        const auto section = bridge.findSectionIndex(m_newElement.sectionID);
        if (!material || !section) {
          error = "choose a material and a section";
          return false;
        }
        try {
          (void)FEM::BEAM::localAxes(mesh.nodes[a].getLocation(), mesh.nodes[b].getLocation(), m_newElement.orientation);
        } catch (const std::exception& exception) {
          error = exception.what();
          return false;
        }
        FEM::BEAM::BeamElement element;
        element.node1 = a;
        element.node2 = b;
        element.materialID = *material;
        element.sectionID = *section;
        element.formulation = static_cast<FEM::BEAM::E_Formulation>(m_newElement.formulation);
        element.orientation = m_newElement.orientation;
        element.endReleases = static_cast<std::uint16_t>(m_newElement.releases & FEM::BEAM::RELEASE::allMask);
        added = static_cast<std::uint32_t>(mesh.elements.size());
        mesh.elements.push_back(element);
        return true;
      });
      if (ok) {
        setStatus(std::format("Element {} ({} - {}) added", added, m_newNodeA, m_newNodeB), false);
        m_newNodeA = m_newNodeB; // ready for the next element of a chain
      } else {
        setStatus("Element not added: " + error, true);
      }
    }
    ImGui::EndDisabled();
    if (!m_status.empty()) LAYOUT::wrappedColored(m_statusIsError ? THEME::theme().bad : THEME::theme().note, m_status);
    ImGui::End();
  }

  void BeamModelEditor::renderAssignAllWindow() {
    if (!beginToolWindow("Assign to All Elements", m_assignAllOpen, 26.0f)) return;
    auto& bridge = BRIDGE::buildBridge();
    ImGui::BeginDisabled(bridge.isRunning.load());
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("Sets the formulation, or material and section, on every element; single elements can be changed afterwards.");
    ImGui::PopTextWrapPos();
    if (PROPERTY::beginFixed("##assign_formulation")) {
      PROPERTY::row("Formulation");
      ImGui::Combo("##whole_formulation", &m_wholeFormulation, formulations.data(), static_cast<int>(formulations.size()));
      PROPERTY::end();
    }
    if (ImGui::Button("Apply Formulation to All", ImVec2(-FLT_MIN, 0.0f))) {
      if (editModel(bridge, [&](FEM::BEAM::MeshData& mesh) {
            if (mesh.elements.empty()) return false;
            FEM::BEAM::setFormulationForAll(mesh, static_cast<FEM::BEAM::E_Formulation>(m_wholeFormulation));
            return true;
          })) {
        m_loadedElement = noSelection;
        setStatus(std::format("{} set on every element", formulations[static_cast<std::size_t>(m_wholeFormulation)]), false);
      }
    }
    ImGui::Spacing();
    if (PROPERTY::beginFixed("##assign_material")) {
      PROPERTY::row("Material");
      PROPERTY::reserveBrowse();
      materialCombo(bridge, "##whole_material", m_wholeMaterialID);
      if (PROPERTY::browseButton("##whole_materials", "Open the Material Handler") && onOpenMaterialHandler) onOpenMaterialHandler();
      PROPERTY::row("Section");
      PROPERTY::reserveBrowse();
      sectionCombo(bridge, "##whole_section", m_wholeSectionID);
      if (PROPERTY::browseButton("##whole_sections", "Open the Section Handler") && onOpenSectionHandler) onOpenSectionHandler();
      PROPERTY::end();
    }
    if (ImGui::Button("Apply Material & Section to All", ImVec2(-FLT_MIN, 0.0f))) {
      const bool ok = editModel(bridge, [&](FEM::BEAM::MeshData& mesh) {
        const auto material = bridge.findMaterialIndex(m_wholeMaterialID); // dataMutex held
        const auto section = bridge.findSectionIndex(m_wholeSectionID);
        if (!material || !section || mesh.elements.empty()) return false;
        for (auto& element : mesh.elements) {
          element.materialID = *material;
          element.sectionID = *section;
        }
        return true;
      });
      m_loadedElement = noSelection;
      if (ok) setStatus("Material and section set on every element", false);
      else setStatus("Nothing changed: the model has no elements", true);
    }
    ImGui::Spacing();
    if (ICONS::iconTextButton(ICONS::E_IconId::ReleaseHinge, "Remove All End Releases", ImVec2(-FLT_MIN, 0.0f))) removeAllReleases();
    ImGui::EndDisabled();
    if (!m_status.empty()) LAYOUT::wrappedColored(m_statusIsError ? THEME::theme().bad : THEME::theme().note, m_status);
    ImGui::End();
  }

  void BeamModelEditor::loadExample() {
    // A 3D portal frame: two HEB 200 columns, an IPE 300 beam with a uniform load, a lateral
    // and an out-of-plane load, clamped bases, self weight on.
    auto& bridge = BRIDGE::buildBridge();
    bridge.resetModel(BRIDGE::E_ObjectType::BeamFrame);
    resetState();
    editModel(bridge, [&](FEM::BEAM::MeshData& mesh) {
      mesh.nodes = {FEM::BEAM::Node{0, 0.0, 0.0, 0.0}, FEM::BEAM::Node{1, 0.0, 4.0, 0.0}, FEM::BEAM::Node{2, 6.0, 4.0, 0.0},
                    FEM::BEAM::Node{3, 6.0, 0.0, 0.0}};
      mesh.nodes[0].fixAll();
      mesh.nodes[3].fixAll();
      const std::uint32_t column = findSectionByName(bridge, "HEB 200");
      const std::uint32_t girder = findSectionByName(bridge, "IPE 300");
      const auto element = [](const std::uint32_t a, const std::uint32_t b, const std::uint32_t section) {
        FEM::BEAM::BeamElement e;
        e.node1 = a;
        e.node2 = b;
        e.sectionID = section;
        return e;
      };
      mesh.elements = {element(0, 1, column), element(1, 2, girder), element(3, 2, column)};
      mesh.distributedLoads = {{1, {0.0, -10e3, 0.0}, FEM::BEAM::E_LoadFrame::Global}};
      mesh.nodalLoads = {{1, {5e3, 0.0, 0.0}, {}}, {2, {0.0, 0.0, 2e3}, {}}};
      return true;
    });
    setStatus("Example frame loaded (material: the first in the list)", false);
  }

  void BeamModelEditor::onImGuiRender() {
    if (!isOpen) return;
    auto& bridge = BRIDGE::buildBridge();

    std::uint32_t node = noSelection, element = noSelection;
    {
      std::lock_guard lock(bridge.dataMutex);
      const auto& mesh = bridge.activeBeamMesh;
      if (mesh && bridge.selectedNodeId < mesh->nodes.size()) node = bridge.selectedNodeId;
      if (mesh && bridge.selectedElementId < mesh->elements.size()) element = bridge.selectedElementId;
    }
    syncSelection(node, element);
    const bool withLoads = bridge.loadKind.load() != BRIDGE::E_LoadKind::Dynamic;
    const bool running = bridge.isRunning.load();

    if (ImGui::Begin("Properties###beam_properties", &isOpen)) {
      renderHeader(node, element);
      // The status line keeps its place at the bottom; the content scrolls above it.
      const float statusHeight = m_status.empty() ? 0.0f : ImGui::GetTextLineHeightWithSpacing() * 2.0f + ImGui::GetStyle().ItemSpacing.y;
      ImGui::BeginChild("##beam_properties_body", ImVec2(0.0f, -statusHeight));
      // The solve works on a copy; edits made meanwhile would be overwritten by its result.
      ImGui::BeginDisabled(running);
      if (m_focus == E_Focus::Node) renderNode(node, withLoads);
      else if (m_focus == E_Focus::Element) renderElement(element, withLoads);
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
    renderAddElementWindow();
    renderAssignAllWindow();
  }

} // namespace anaf::GUI end
