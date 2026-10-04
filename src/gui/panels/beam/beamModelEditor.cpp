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

#include <beam/beamEngine/beamSolver/deformationUnderConstForce.hpp> // localAxes()
#include <bridge/generalStatus.hpp>
#include <log/anaf_info.hpp>
#include <objectCalcs/common/supportBasis.hpp>
#include <panels/beam/beamWorker.hpp>
#include <panels/beam/sectionCombo.hpp>
#include <panels/truss/materialCombo.hpp>

#include "beam/beamSection/sectionLibrary.hpp"
#include "imgui.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <exception>
#include <format>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace anaf::GUI {

  namespace {

    using BRIDGE::BeamMeshData;
    using BRIDGE::Gui_Calc_Bridge;
    using FEM::BEAM::BeamElement;
    using FEM::BEAM::Formulation;
    using FEM::BEAM::LoadFrame;
    using Vec3 = std::array<double, 3>;

    constexpr std::uint32_t kNone = std::numeric_limits<std::uint32_t>::max();
    constexpr std::array<const char*, 2> kFormulations{"Euler-Bernoulli", "Timoshenko"};
    constexpr std::array<const char*, 2> kFrames{"Global axes", "Local axes"};
    constexpr Vec3 kGravity{0.0, -9.80665, 0.0};

    const char* formulationShort(const Formulation formulation) {
      return formulation == Formulation::Timoshenko ? "TI" : "EB";
    }

    // Results no longer match an edited model.
    void dropResults(BeamMeshData& mesh) {
      if (!mesh.hasResults) return;
      for (auto& node : mesh.nodes) {
        node.setDisplacement({0.0, 0.0, 0.0});
        node.setRotation({0.0, 0.0, 0.0});
      }
      for (auto& element : mesh.elements) {
        element.sectionForces = {};
        element.stress = {};
      }
      mesh.hasResults = false;
    }

    // Runs edit on a copy of the active beam snapshot (an empty one if there is none) under
    // dataMutex and publishes the copy. edit returns false to publish nothing.
    template <typename Edit>
    bool editModel(Gui_Calc_Bridge& bridge, Edit&& edit) {
      {
        std::lock_guard lock(bridge.dataMutex);
        auto mesh = bridge.activeBeamMesh ? std::make_shared<BeamMeshData>(*bridge.activeBeamMesh) : std::make_shared<BeamMeshData>();
        if (!edit(*mesh)) return false;
        dropResults(*mesh);
        bridge.activeBeamMesh = std::move(mesh);
        bridge.m_isValid = false;
      }
      bridge.dataVersion.fetch_add(1, std::memory_order_release);
      return true;
    }

    std::shared_ptr<const BeamMeshData> currentMesh(Gui_Calc_Bridge& bridge) {
      std::lock_guard lock(bridge.dataMutex);
      return bridge.activeBeamMesh;
    }

    // True when the global axis lies in the span of an orthonormal basis.
    bool axisFree(const std::vector<Vec3>& basis, const std::size_t axis) {
      Vec3 unit{};
      unit[axis] = 1.0;
      const auto rest = FEM::SUPPORT::componentOutside(unit, basis);
      return std::sqrt(rest[0] * rest[0] + rest[1] * rest[1] + rest[2] * rest[2]) <= 1e-12;
    }

    bool alongGlobalAxes(const std::vector<Vec3>& basis) {
      return std::ranges::all_of(basis, [](const Vec3& v) { return std::ranges::count(v, 0.0) == 2; });
    }

    // Removes the elements for which drop(element) is true; distributed loads follow their
    // element (and disappear with it).
    template <typename Predicate>
    void removeElements(BeamMeshData& mesh, Predicate&& drop) {
      std::vector<std::uint32_t> newIndex(mesh.elements.size(), kNone);
      std::vector<BeamElement> kept;
      for (std::size_t i = 0; i < mesh.elements.size(); ++i) {
        if (drop(mesh.elements[i])) continue;
        newIndex[i] = static_cast<std::uint32_t>(kept.size());
        kept.push_back(mesh.elements[i]);
      }
      mesh.elements = std::move(kept);
      std::erase_if(mesh.distributedLoads, [&](const FEM::BEAM::DistributedLoad& load) {
        return load.element >= newIndex.size() || newIndex[load.element] == kNone;
      });
      for (auto& load : mesh.distributedLoads) load.element = newIndex[load.element];
    }

    // Removes node k with its elements and loads; later ids move down by one (ids = positions).
    void deleteNode(BeamMeshData& mesh, const std::uint32_t k) {
      const auto shift = [k](const std::uint32_t id) { return id > k ? id - 1 : id; };
      std::vector<FEM::BEAM::Node> nodes;
      nodes.reserve(mesh.nodes.size());
      for (const auto& node : mesh.nodes) {
        if (node.getNodeID() == k) continue;
        const auto& p = node.getLocation();
        FEM::BEAM::Node renumbered(shift(node.getNodeID()), p[0], p[1], p[2]);
        renumbered.setAllowedMotionDirections(node.getAllowedMotionDirections());
        renumbered.setAllowedRotationAxes(node.getAllowedRotationAxes());
        nodes.push_back(std::move(renumbered));
      }
      mesh.nodes = std::move(nodes);
      removeElements(mesh, [k](const BeamElement& element) { return element.node1 == k || element.node2 == k; });
      for (auto& element : mesh.elements) {
        element.node1 = shift(element.node1);
        element.node2 = shift(element.node2);
      }
      std::erase_if(mesh.nodalLoads, [k](const FEM::BEAM::NodalLoad& load) { return load.node == k; });
      for (auto& load : mesh.nodalLoads) load.node = shift(load.node);
    }

    void setSelectedNode(Gui_Calc_Bridge& bridge, const std::uint32_t node) {
      std::lock_guard lock(bridge.dataMutex);
      bridge.selectedNodeId = node;
    }

    void setSelectedElement(Gui_Calc_Bridge& bridge, const std::uint32_t element) {
      std::lock_guard lock(bridge.dataMutex);
      bridge.selectedElementId = element;
    }

    std::uint32_t findSectionByName(const Gui_Calc_Bridge& bridge, const char* name) {
      for (std::uint32_t i = 0; i < bridge.allSections.size(); ++i) {
        if (FEM::BEAM::sameSectionName(bridge.allSections[i].getName(), name)) return i;
      }
      return 0;
    }

  } // namespace end

  std::optional<std::vector<Vec3>> BeamModelEditor::SupportInput::allowedBasis(std::string& error) const {
    if (!inclined) {
      std::vector<Vec3> allowed;
      for (std::size_t axis = 0; axis < 3; ++axis) {
        if (fixed[axis]) continue;
        Vec3 unit{};
        unit[axis] = 1.0;
        allowed.push_back(unit);
      }
      return allowed;
    }
    try {
      const auto basis = FEM::SUPPORT::orthonormalize({vectors.begin(), vectors.begin() + vectorCount});
      return vectorsRestrained ? FEM::SUPPORT::orthogonalComplement(basis) : basis;
    } catch (const std::exception&) {
      error = "The vectors must be non-zero and linearly independent.";
      return std::nullopt;
    }
  }

  void BeamModelEditor::SupportInput::load(const std::vector<Vec3>& allowed) {
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
    m_newNode = {0.0, 0.0, 0.0};
    m_nodePosition = {0.0, 0.0, 0.0};
    m_motion = SupportInput{};
    m_rotation = SupportInput{};
    m_force = {0.0, 0.0, 0.0};
    m_moment = {0.0, 0.0, 0.0};
    m_loadedNode = kNone;
    m_elementNodeA = 0;
    m_elementNodeB = 1;
    m_formulation = 0;
    m_orientation = {0.0, 0.0, 0.0};
    m_distributed = {0.0, 0.0, 0.0};
    m_distributedFrame = 0;
    m_loadedElement = kNone;
    m_status.clear();
    m_statusIsError = false;
  }

  void BeamModelEditor::setStatus(std::string message, const bool error) {
    if (error) anaf::LOG::warn("Beam editor: {}", message);
    m_status = std::move(message);
    m_statusIsError = error;
  }

  void BeamModelEditor::syncSelection(const std::uint32_t node, const std::uint32_t element) {
    auto& bridge = BRIDGE::buildBridge();
    if (node != m_loadedNode) {
      m_loadedNode = node;
      m_force = {0.0, 0.0, 0.0};
      m_moment = {0.0, 0.0, 0.0};
      std::lock_guard lock(bridge.dataMutex);
      if (node != kNone && bridge.activeBeamMesh && node < bridge.activeBeamMesh->nodes.size()) {
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
      if (element != kNone && bridge.activeBeamMesh && element < bridge.activeBeamMesh->elements.size()) {
        const auto& selected = bridge.activeBeamMesh->elements[element];
        m_elementNodeA = selected.node1;
        m_elementNodeB = selected.node2;
        if (selected.materialID < bridge.allMaterials.size()) m_materialID = bridge.allMaterials[selected.materialID].getMaterialID();
        if (selected.sectionID < bridge.allSections.size()) m_sectionID = bridge.allSections[selected.sectionID].getSectionID();
        m_formulation = static_cast<int>(selected.formulation);
        m_orientation = selected.orientation;
      }
    }
  }

  void BeamModelEditor::renderSummary() {
    auto& bridge = BRIDGE::buildBridge();
    const auto mesh = currentMesh(bridge);
    if (!mesh || mesh->nodes.empty()) {
      ImGui::TextWrapped("No model yet. Add nodes and elements below, or load the example frame.");
    } else {
      const auto supported = std::ranges::count_if(mesh->nodes, [](const FEM::BEAM::Node& node) { return node.isSupported(); });
      ImGui::Text("Nodes: %zu   Elements: %zu   Supported: %td", mesh->nodes.size(), mesh->elements.size(), supported);
      ImGui::Text("Nodal loads: %zu   Distributed: %zu   Self weight: %s", mesh->nodalLoads.size(),
                  mesh->distributedLoads.size(), mesh->gravity == Vec3{} ? "off" : "on");
      if (mesh->hasResults) {
        double maxDisplacement = 0.0, maxVonMises = 0.0;
        std::size_t exceeded = 0, withoutStress = 0;
        std::uint32_t worst = kNone;
        for (const auto& node : mesh->nodes) {
          const auto& d = node.getDisplacement();
          maxDisplacement = std::max(maxDisplacement, std::hypot(d[0], d[1], d[2]));
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
        const bool valid = bridge.m_isValid.load();
        ImGui::TextColored(valid ? ImVec4(0.55f, 0.95f, 0.6f, 1.0f) : ImVec4(1.0f, 0.75f, 0.35f, 1.0f), "%s",
                           valid ? "Results: solved, energy check passed" : "Results: shown (read from a file, or energy check not passed)");
        ImGui::Text("Max displacement: %.4g mm", maxDisplacement * 1e3);
        if (worst != kNone) ImGui::Text("Max von Mises: %.4g MPa (element %u)", maxVonMises / 1e6, worst);
        if (exceeded > 0) ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%zu elements exceed the yield strength", exceeded);
        if (withoutStress > 0) ImGui::TextDisabled("%zu elements with a general section (no stresses)", withoutStress);
      } else {
        ImGui::TextDisabled("Results: none (run the solver)");
      }
    }
    if (!m_status.empty()) {
      const ImVec4 color = m_statusIsError ? ImVec4(1.0f, 0.45f, 0.45f, 1.0f) : ImVec4(0.7f, 0.7f, 0.7f, 1.0f);
      ImGui::PushTextWrapPos(0.0f);
      ImGui::TextColored(color, "%s", m_status.c_str());
      ImGui::PopTextWrapPos();
    }
  }

  void BeamModelEditor::renderNodes(const std::uint32_t node) {
    if (!ImGui::CollapsingHeader("Nodes", ImGuiTreeNodeFlags_DefaultOpen)) return;
    auto& bridge = BRIDGE::buildBridge();

    ImGui::InputScalarN("Position [m]##beam_new_node", ImGuiDataType_Double, m_newNode.data(), 3, nullptr, nullptr, "%.4g");
    if (ImGui::Button("Add Node##beam", ImVec2(-1.0f, 0.0f))) {
      std::uint32_t added = 0;
      editModel(bridge, [&](BeamMeshData& mesh) {
        added = static_cast<std::uint32_t>(mesh.nodes.size());
        mesh.nodes.emplace_back(added, m_newNode[0], m_newNode[1], m_newNode[2]);
        return true;
      });
      setSelectedNode(bridge, added);
      setStatus(std::format("Node {} added", added), false);
    }

    ImGui::Separator();
    std::uint32_t typed = node;
    ImGui::SetNextItemWidth(120.0f);
    if (ImGui::InputScalar("Selected node##beam", ImGuiDataType_U32, &typed, nullptr, nullptr, nullptr, ImGuiInputTextFlags_EnterReturnsTrue)) {
      const auto mesh = currentMesh(bridge);
      setSelectedNode(bridge, mesh && typed < mesh->nodes.size() ? typed : kNone);
    }
    if (node == kNone) {
      ImGui::TextDisabled("Type a node id to edit its position, support and load.");
      return;
    }
    ImGui::InputScalarN("Position [m]##beam_selected_node", ImGuiDataType_Double, m_nodePosition.data(), 3, nullptr, nullptr, "%.4g");
    if (ImGui::Button("Move Node##beam")) {
      if (editModel(bridge, [&](BeamMeshData& mesh) {
            if (node >= mesh.nodes.size()) return false;
            mesh.nodes[node].setLocation(m_nodePosition);
            return true;
          })) {
        setStatus(std::format("Node {} moved", node), false);
      }
    }
    ImGui::SameLine();
    if (ImGui::Button("Delete Node##beam")) {
      if (editModel(bridge, [&](BeamMeshData& mesh) {
            if (node >= mesh.nodes.size()) return false;
            deleteNode(mesh, node);
            return true;
          })) {
        setSelectedNode(bridge, kNone);
        setSelectedElement(bridge, kNone);
        m_loadedNode = kNone;
        setStatus(std::format("Node {} deleted with its elements; later node ids moved down by one", node), false);
      }
    }
  }

  void BeamModelEditor::renderSupportsAndLoads(const std::uint32_t node) {
    if (!ImGui::CollapsingHeader("Supports & Nodal Loads", ImGuiTreeNodeFlags_DefaultOpen)) return;
    if (node == kNone) {
      ImGui::TextDisabled("Select a node first.");
      return;
    }
    auto& bridge = BRIDGE::buildBridge();
    ImGui::Text("Node %u", node);
    if (ImGui::Button("Fixed##beam_preset")) {
      m_motion.setGlobal(true);
      m_rotation.setGlobal(true);
    }
    ImGui::SameLine();
    if (ImGui::Button("Pinned##beam_preset")) {
      m_motion.setGlobal(true);
      m_rotation.setGlobal(false);
    }
    ImGui::SameLine();
    if (ImGui::Button("Free##beam_preset")) {
      m_motion.setGlobal(false);
      m_rotation.setGlobal(false);
    }
    ImGui::SeparatorText("Translation");
    const auto motion = renderSupportGroup("motion", "U", m_motion);
    ImGui::SeparatorText("Rotation");
    const auto rotation = renderSupportGroup("rotation", "R", m_rotation);

    ImGui::BeginDisabled(!motion || !rotation);
    if (ImGui::Button("Apply Support##beam", ImVec2(-1.0f, 0.0f))) {
      if (editModel(bridge, [&](BeamMeshData& mesh) {
            if (node >= mesh.nodes.size()) return false;
            mesh.nodes[node].setAllowedMotionDirections(*motion);
            mesh.nodes[node].setAllowedRotationAxes(*rotation);
            return true;
          })) {
        setStatus(std::format("Support of node {} updated", node), false);
      }
    }
    ImGui::EndDisabled();

    ImGui::Spacing();
    ImGui::InputScalarN("Force [N]##beam", ImGuiDataType_Double, m_force.data(), 3, nullptr, nullptr, "%.4g");
    ImGui::InputScalarN("Moment [N m]##beam", ImGuiDataType_Double, m_moment.data(), 3, nullptr, nullptr, "%.4g");
    const auto setLoad = [&](const Vec3& force, const Vec3& moment) {
      return editModel(bridge, [&](BeamMeshData& mesh) {
        if (node >= mesh.nodes.size()) return false;
        std::erase_if(mesh.nodalLoads, [&](const FEM::BEAM::NodalLoad& load) { return load.node == node; });
        if (force != Vec3{} || moment != Vec3{}) mesh.nodalLoads.push_back({node, force, moment});
        return true;
      });
    };
    if (ImGui::Button("Apply Load##beam")) {
      if (setLoad(m_force, m_moment)) setStatus(std::format("Load on node {} updated", node), false);
    }
    ImGui::SameLine();
    if (ImGui::Button("Remove Load##beam")) {
      m_force = {0.0, 0.0, 0.0};
      m_moment = {0.0, 0.0, 0.0};
      if (setLoad(m_force, m_moment)) setStatus(std::format("Load on node {} removed", node), false);
    }
  }

  std::optional<std::vector<Vec3>> BeamModelEditor::renderSupportGroup(const char* id, const char* axisNames, SupportInput& input) {
    ImGui::PushID(id);
    int mode = input.inclined ? 1 : 0;
    ImGui::RadioButton("Global axes", &mode, 0);
    ImGui::SameLine();
    ImGui::RadioButton("Inclined / skewed", &mode, 1);
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
      ImGui::TextUnformatted("Fix");
      for (std::size_t axis = 0; axis < 3; ++axis) {
        ImGui::SameLine();
        ImGui::Checkbox(std::format("{}{}", axisNames, "xyz"[axis]).c_str(), &input.fixed[axis]);
      }
    } else {
      bool restrained = input.vectorsRestrained;
      if (ImGui::RadioButton("Restrained", restrained)) restrained = true;
      ImGui::SameLine();
      if (ImGui::RadioButton("Allowed", !restrained)) restrained = false;
      ImGui::SetItemTooltip("Restrained: the directions the support holds.\nAllowed: the directions left free.");
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
      ImGui::TextUnformatted("Vectors:");
      for (int count = 1; count <= 3; ++count) {
        ImGui::SameLine();
        ImGui::RadioButton(std::format("{}##count", count).c_str(), &input.vectorCount, count);
      }
      for (int i = 0; i < input.vectorCount; ++i) {
        ImGui::InputScalarN(std::format("d{}", i + 1).c_str(), ImGuiDataType_Double, input.vectors[static_cast<std::size_t>(i)].data(), 3,
                            nullptr, nullptr, "%.4g");
      }
    }

    if (input.inclined && std::string_view(id) == "rotation") {
      ImGui::PushTextWrapPos(0.0f);
      ImGui::TextDisabled("Solved as given; files store rotation supports per global axis only (export warns).");
      ImGui::PopTextWrapPos();
    }
    std::string error;
    const auto allowed = input.allowedBasis(error);
    if (!allowed) {
      ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.4f, 1.0f), "%s", error.c_str());
    } else if (input.inclined) {
      if (allowed->empty()) ImGui::TextDisabled("Held in every direction.");
      else if (allowed->size() == 3) ImGui::TextDisabled("Free in every direction.");
      else if (allowed->size() == 1) ImGui::TextDisabled("Free along (%.3f, %.3f, %.3f) only.", (*allowed)[0][0], (*allowed)[0][1], (*allowed)[0][2]);
      else {
        const auto normal = FEM::SUPPORT::orthogonalComplement(*allowed)[0];
        ImGui::TextDisabled("Held along (%.3f, %.3f, %.3f) only.", normal[0], normal[1], normal[2]);
      }
    }
    ImGui::PopID();
    return allowed;
  }

  void BeamModelEditor::renderElements(const std::uint32_t selected) {
    if (!ImGui::CollapsingHeader("Elements", ImGuiTreeNodeFlags_DefaultOpen)) return;
    auto& bridge = BRIDGE::buildBridge();

    materialCombo(bridge, "Material##beam_element", m_materialID);
    sectionCombo(bridge, "Section##beam_element", m_sectionID);
    if (ImGui::Button("Materials...##beam") && onOpenMaterialHandler) onOpenMaterialHandler();
    ImGui::SameLine();
    if (ImGui::Button("Sections...##beam") && onOpenSectionHandler) onOpenSectionHandler();
    ImGui::Combo("Formulation##beam_element", &m_formulation, kFormulations.data(), static_cast<int>(kFormulations.size()));
    ImGui::InputScalarN("Orientation v##beam", ImGuiDataType_Double, m_orientation.data(), 3, nullptr, nullptr, "%.3g");
    ImGui::SetItemTooltip("Vector in the local x-y plane (global axes). 0 0 0 = automatic: local y as close to +Y as\n"
                          "possible, +X for vertical members. A section's height lies along local y.");

    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.3f);
    ImGui::InputScalar("##beam_node_a", ImGuiDataType_U32, &m_elementNodeA);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.45f);
    ImGui::InputScalar("Node A - B##beam_node_b", ImGuiDataType_U32, &m_elementNodeB);

    // Writes the chosen properties into element; an error message if they are unusable.
    // Caller holds dataMutex (editModel).
    const auto assign = [&](BeamElement& element, const BeamMeshData& mesh) -> std::string {
      const auto material = bridge.findMaterialIndex(m_materialID);
      const auto section = bridge.findSectionIndex(m_sectionID);
      if (!material || !section) return "choose a material and a section";
      try {
        (void)FEM::BEAM::localAxes(mesh.nodes[element.node1].getLocation(), mesh.nodes[element.node2].getLocation(), m_orientation);
      } catch (const std::exception& exception) {
        return exception.what();
      }
      element.materialID = *material;
      element.sectionID = *section;
      element.formulation = static_cast<Formulation>(m_formulation);
      element.orientation = m_orientation;
      return {};
    };

    if (ImGui::Button("Add Element##beam", ImVec2(-1.0f, 0.0f))) {
      std::string error;
      std::uint32_t added = 0;
      const bool ok = editModel(bridge, [&](BeamMeshData& mesh) {
        const auto count = mesh.nodes.size();
        const auto a = m_elementNodeA, b = m_elementNodeB;
        if (a >= count || b >= count) {
          error = std::format("nodes {} and {} must both exist (the model has {} nodes)", a, b, count);
          return false;
        }
        if (a == b) {
          error = "an element needs two different nodes";
          return false;
        }
        const bool duplicate = std::ranges::any_of(mesh.elements, [&](const BeamElement& element) {
          return (element.node1 == a && element.node2 == b) || (element.node1 == b && element.node2 == a);
        });
        if (duplicate) {
          error = std::format("an element between nodes {} and {} already exists", a, b);
          return false;
        }
        BeamElement element;
        element.node1 = a;
        element.node2 = b;
        error = assign(element, mesh);
        if (!error.empty()) return false;
        added = static_cast<std::uint32_t>(mesh.elements.size());
        mesh.elements.push_back(element);
        return true;
      });
      if (ok) {
        setStatus(std::format("Element {} ({} - {}) added", added, m_elementNodeA, m_elementNodeB), false);
        m_elementNodeA = m_elementNodeB; // ready for the next element of a chain
      } else {
        setStatus("Element not added: " + error, true);
      }
    }

    const auto mesh = currentMesh(bridge);
    const std::size_t count = mesh ? mesh->elements.size() : 0;
    ImGui::BeginChild("BeamElementList", ImVec2(0.0f, 150.0f), true);
    if (mesh) {
      std::lock_guard lock(bridge.dataMutex); // section names
      ImGuiListClipper clipper;
      clipper.Begin(static_cast<int>(count));
      while (clipper.Step()) {
        for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
          const auto index = static_cast<std::uint32_t>(row);
          const auto& element = mesh->elements[index];
          const std::string section = element.sectionID < bridge.allSections.size() ? bridge.allSections[element.sectionID].getName() : "?";
          std::string label = std::format("{}: {} - {}  {}  {}", index, element.node1, element.node2, section, formulationShort(element.formulation));
          if (element.stress.available) label += std::format("  {:.1f} MPa", element.stress.maxVonMises / 1e6);
          ImGui::PushID(row);
          if (element.stress.isStressExceeded) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.4f, 0.4f, 1.0f));
          if (ImGui::Selectable(label.c_str(), selected == index)) {
            bridge.selectedElementId = selected == index ? kNone : index; // dataMutex held
          }
          if (element.stress.isStressExceeded) ImGui::PopStyleColor();
          ImGui::PopID();
        }
      }
    }
    ImGui::EndChild();

    ImGui::BeginDisabled(selected == kNone);
    if (ImGui::Button("Apply to Selected##beam")) {
      std::string error = "no element selected";
      const bool ok = editModel(bridge, [&](BeamMeshData& edited) {
        if (selected >= edited.elements.size()) return false;
        error = assign(edited.elements[selected], edited);
        return error.empty();
      });
      if (ok) setStatus(std::format("Element {} updated", selected), false);
      else setStatus("Element not updated: " + error, true);
    }
    ImGui::SameLine();
    if (ImGui::Button("Delete Element##beam")) {
      if (editModel(bridge, [&](BeamMeshData& edited) {
            if (selected >= edited.elements.size()) return false;
            std::uint32_t index = 0;
            removeElements(edited, [&](const BeamElement&) { return index++ == selected; });
            return true;
          })) {
        setSelectedElement(bridge, kNone);
        setStatus(std::format("Element {} deleted", selected), false);
      }
    }
    ImGui::EndDisabled();
  }

  void BeamModelEditor::renderElementLoads(const std::uint32_t element) {
    if (!ImGui::CollapsingHeader("Distributed Loads & Self Weight", ImGuiTreeNodeFlags_DefaultOpen)) return;
    auto& bridge = BRIDGE::buildBridge();
    const auto mesh = currentMesh(bridge);

    bool selfWeight = !mesh || mesh->gravity != Vec3{};
    if (ImGui::Checkbox("Self weight (density x area x g, -Y)##beam", &selfWeight)) {
      editModel(bridge, [&](BeamMeshData& edited) {
        edited.gravity = selfWeight ? kGravity : Vec3{};
        return true;
      });
    }

    if (element == kNone || !mesh || element >= mesh->elements.size()) {
      ImGui::TextDisabled("Select an element for uniform loads.");
      return;
    }
    ImGui::Text("Element %u", element);
    std::size_t onElement = 0;
    for (const auto& load : mesh->distributedLoads) {
      if (load.element != element) continue;
      ++onElement;
      ImGui::BulletText("q = (%.4g, %.4g, %.4g) N/m, %s", load.value[0], load.value[1], load.value[2],
                        load.frame == LoadFrame::Local ? "local" : "global");
    }
    if (onElement == 0) ImGui::TextDisabled("No uniform load on this element.");

    ImGui::InputScalarN("q [N/m]##beam", ImGuiDataType_Double, m_distributed.data(), 3, nullptr, nullptr, "%.4g");
    ImGui::Combo("Axes##beam_q_frame", &m_distributedFrame, kFrames.data(), static_cast<int>(kFrames.size()));
    if (ImGui::Button("Add Load##beam_q")) {
      if (m_distributed == Vec3{}) {
        setStatus("Enter a non-zero load", true);
      } else if (editModel(bridge, [&](BeamMeshData& edited) {
                   if (element >= edited.elements.size()) return false;
                   edited.distributedLoads.push_back({element, m_distributed, static_cast<LoadFrame>(m_distributedFrame)});
                   return true;
                 })) {
        setStatus(std::format("Uniform load added to element {}", element), false);
      }
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(onElement == 0);
    if (ImGui::Button("Remove Loads##beam_q")) {
      if (editModel(bridge, [&](BeamMeshData& edited) {
            std::erase_if(edited.distributedLoads, [&](const FEM::BEAM::DistributedLoad& load) { return load.element == element; });
            return true;
          })) {
        setStatus(std::format("Uniform loads of element {} removed", element), false);
      }
    }
    ImGui::EndDisabled();
  }

  void BeamModelEditor::renderWholeModel() {
    if (!ImGui::CollapsingHeader("Whole Model")) return;
    auto& bridge = BRIDGE::buildBridge();
    ImGui::TextWrapped("Sets the formulation, or material and section, on every element; single elements can be "
                       "changed afterwards.");
    ImGui::Combo("Formulation##beam_whole", &m_wholeFormulation, kFormulations.data(), static_cast<int>(kFormulations.size()));
    if (ImGui::Button("Apply Formulation to All##beam", ImVec2(-1.0f, 0.0f))) {
      if (editModel(bridge, [&](BeamMeshData& mesh) {
            if (mesh.elements.empty()) return false;
            FEM::BEAM::setFormulationForAll(mesh, static_cast<Formulation>(m_wholeFormulation));
            return true;
          })) {
        setStatus(std::format("{} set on every element", kFormulations[static_cast<std::size_t>(m_wholeFormulation)]), false);
      }
    }
    materialCombo(bridge, "Material##beam_whole", m_wholeMaterialID);
    sectionCombo(bridge, "Section##beam_whole", m_wholeSectionID);
    if (ImGui::Button("Apply Material & Section to All##beam", ImVec2(-1.0f, 0.0f))) {
      const bool ok = editModel(bridge, [&](BeamMeshData& mesh) {
        const auto material = bridge.findMaterialIndex(m_wholeMaterialID); // dataMutex held
        const auto section = bridge.findSectionIndex(m_wholeSectionID);
        if (!material || !section || mesh.elements.empty()) return false;
        for (auto& element : mesh.elements) {
          element.materialID = *material;
          element.sectionID = *section;
        }
        return true;
      });
      if (ok) setStatus("Material and section set on every element", false);
      else setStatus("Nothing changed: the model has no elements", true);
    }
  }

  void BeamModelEditor::loadExample() {
    // A 3D portal frame: two HEB 200 columns, an IPE 300 beam with a uniform load, a lateral
    // and an out-of-plane load, clamped bases, self weight on.
    auto& bridge = BRIDGE::buildBridge();
    bridge.resetModel(BRIDGE::ObjectType::beam_frame);
    resetState();
    editModel(bridge, [&](BeamMeshData& mesh) {
      mesh.nodes = {FEM::BEAM::Node{0, 0.0, 0.0, 0.0}, FEM::BEAM::Node{1, 0.0, 4.0, 0.0}, FEM::BEAM::Node{2, 6.0, 4.0, 0.0},
                    FEM::BEAM::Node{3, 6.0, 0.0, 0.0}};
      mesh.nodes[0].fixAll();
      mesh.nodes[3].fixAll();
      const std::uint32_t column = findSectionByName(bridge, "HEB 200");
      const std::uint32_t girder = findSectionByName(bridge, "IPE 300");
      const auto element = [](const std::uint32_t a, const std::uint32_t b, const std::uint32_t section) {
        BeamElement e;
        e.node1 = a;
        e.node2 = b;
        e.sectionID = section;
        return e;
      };
      mesh.elements = {element(0, 1, column), element(1, 2, girder), element(3, 2, column)};
      mesh.distributedLoads = {{1, {0.0, -10e3, 0.0}, LoadFrame::Global}};
      mesh.nodalLoads = {{1, {5e3, 0.0, 0.0}, {}}, {2, {0.0, 0.0, 2e3}, {}}};
      return true;
    });
    setStatus("Example frame loaded (material: the first in the list)", false);
  }

  void BeamModelEditor::renderSolve() {
    auto& bridge = BRIDGE::buildBridge();
    const auto mesh = currentMesh(bridge);
    if (bridge.m_isRunning.load()) {
      ImGui::ProgressBar(bridge.m_progress.load(), ImVec2(-1.0f, 0.0f));
      ImGui::BeginDisabled();
      ImGui::Button("Calculating...##beam", ImVec2(-1.0f, 32.0f));
      ImGui::EndDisabled();
    } else {
      ImGui::BeginDisabled(!mesh || mesh->elements.empty());
      if (ImGui::Button("Run Solver for Beam##beam", ImVec2(-1.0f, 32.0f))) {
        m_status.clear();
        BEAM_WORKER::startSolve(bridge, mesh);
      }
      ImGui::EndDisabled();
    }
    const bool busy = bridge.m_isRunning.load();
    ImGui::BeginDisabled(busy);
    if (ImGui::Button("Load Example Frame##beam")) loadExample();
    ImGui::SameLine();
    if (ImGui::Button("Clear Model##beam")) {
      bridge.resetModel(BRIDGE::ObjectType::beam_frame);
      resetState();
    }
    ImGui::EndDisabled();
  }

  void BeamModelEditor::onImGuiRender() {
    if (!isOpen) return;
    auto& bridge = BRIDGE::buildBridge();

    std::uint32_t node = kNone, element = kNone;
    {
      std::lock_guard lock(bridge.dataMutex);
      const auto& mesh = bridge.activeBeamMesh;
      if (mesh && bridge.selectedNodeId < mesh->nodes.size()) node = bridge.selectedNodeId;
      if (mesh && bridge.selectedElementId < mesh->elements.size()) element = bridge.selectedElementId;
    }
    syncSelection(node, element);

    ImGui::Begin("Beam(3D) Frame Editor", &isOpen);
    renderSummary();
    ImGui::Separator();
    // The solve works on a copy; edits made meanwhile would be overwritten by its result.
    ImGui::BeginDisabled(bridge.m_isRunning.load());
    renderNodes(node);
    renderSupportsAndLoads(node);
    renderElements(element);
    renderElementLoads(element);
    renderWholeModel();
    ImGui::EndDisabled();
    ImGui::Separator();
    renderSolve();
    ImGui::End();
  }

} // namespace anaf::GUI end
