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

#include <guiMaterials/theme.hpp>

#include <bridge/generalStatus.hpp>
#include <directory/getExecutableDirectory.hpp>
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

  } // namespace end

  void TrussModelEditor::resetState() {
    m_newNode = {0.0, 0.0, 0.0};
    m_nodePosition = {0.0, 0.0, 0.0};
    m_barNodeA = 0;
    m_barNodeB = 1;
    m_areaCm2 = 80.0;
    m_selectedBar = noSelection;
    m_force = {0.0, 0.0, 0.0};
    m_fixed = {false, false, false};
    m_supportInclined = false;
    m_vectorsRestrained = true;
    setSupportVectors({{0.0, 1.0, 0.0}});
    m_loadedNode = noSelection;
    m_status.clear();
    m_statusIsError = false;
  }

  void TrussModelEditor::setStatus(std::string message, const bool error) {
    if (error) anaf::LOG::warn("Model editor: {}", message);
    m_status = std::move(message);
    m_statusIsError = error;
  }

  void TrussModelEditor::syncSelection(const std::uint32_t selectedNode) {
    if (selectedNode == m_loadedNode) return;
    m_loadedNode = selectedNode;
    m_force = {0.0, 0.0, 0.0};
    m_fixed = {false, false, false};
    if (selectedNode == noSelection) return;

    auto& bridge = BRIDGE::buildBridge();
    std::lock_guard lock(bridge.dataMutex);
    if (!bridge.activeMesh || selectedNode >= bridge.activeMesh->trussNodes.size()) return;
    m_nodePosition = bridge.activeMesh->trussNodes[selectedNode].getLocation();
    for (const auto& force : bridge.activeMesh->appliedForces) {
      if (force.getAppliedNode() == selectedNode) m_force = force.getForce();
    }
    const auto& node = bridge.activeMesh->trussNodes[selectedNode];
    const auto& movable = node.getMovable();
    m_fixed = {!movable[0], !movable[1], !movable[2]};
    m_supportInclined = node.hasInclinedSupport();
    if (m_supportInclined) {
      const auto& allowed = node.getAllowedMotionDirections();
      setSupportVectors(m_vectorsRestrained ? FEM::SUPPORT::orthogonalComplement(allowed) : allowed);
    }
  }

  void TrussModelEditor::setSupportVectors(const FEM::SUPPORT::Basis& vectors) {
    if (vectors.empty() || vectors.size() > 3) return;
    m_supportVectorCount = static_cast<int>(vectors.size());
    for (std::size_t i = 0; i < vectors.size(); ++i) m_supportVectors[i] = vectors[i];
  }

  void TrussModelEditor::renderSummary() {
    auto& bridge = BRIDGE::buildBridge();
    const auto mesh = currentMesh(bridge);

    std::size_t bars = 0, wireframe = 0, supports = 0;
    if (mesh) {
      for (const auto& element : mesh->trussElements) {
        if (element.isWireframe) ++wireframe;
        else ++bars;
      }
      supports = static_cast<std::size_t>(std::ranges::count_if(mesh->trussNodes, [](const FEM::TRUSS::Node& node) { return node.isSupported(); }));
    }

    LAYOUT::beginCard("##truss_editor_summary");
    if (!mesh) {
      ImGui::TextWrapped("No model yet. Import a file (Ctrl+O), pick a built-in model or add nodes and bars in the Model tab.");
    } else {
      if (LAYOUT::beginStats("##truss_editor_stats")) {
        LAYOUT::stat("Nodes", std::to_string(mesh->trussNodes.size()));
        LAYOUT::stat("Bars", std::to_string(bars));
        LAYOUT::stat("Supports", std::to_string(supports));
        LAYOUT::stat("Loads", std::to_string(mesh->appliedForces.size()));
        ImGui::EndTable();
      }
      if (wireframe > 0) ImGui::TextDisabled("Wireframe edges (not solved): %zu", wireframe);
      ImGui::Separator();
      if (mesh->hasResults) {
        const bool valid = bridge.isValid.load();
        LAYOUT::wrappedColored(valid ? THEME::theme().good : THEME::theme().warn,
                               valid ? "Solved, energy check passed" : "Results shown (energy check not passed)");
      } else {
        ImGui::TextDisabled("No results yet: run the solver below.");
      }
    }
    if (!m_status.empty()) LAYOUT::wrappedColored(m_statusIsError ? THEME::theme().bad : THEME::theme().note, m_status);
    LAYOUT::endCard();
  }

  void TrussModelEditor::renderNodes(const std::uint32_t selectedNode) {
    if (!PROPERTY::header("Nodes")) return;
    auto& bridge = BRIDGE::buildBridge();

    if (PROPERTY::beginFixed("##truss_new_node")) {
      PROPERTY::inputVector3("New node", m_newNode.data(), "m");
      PROPERTY::end();
    }
    if (ImGui::Button("Add Node", ImVec2(-FLT_MIN, 0.0f))) {
      std::uint32_t added = 0;
      editModel(bridge, [&](FEM::TRUSS::MeshData& mesh) {
        added = static_cast<std::uint32_t>(mesh.trussNodes.size());
        mesh.trussNodes.emplace_back(added, m_newNode[0], m_newNode[1], m_newNode[2]);
        return true;
      });
      {
        std::lock_guard lock(bridge.dataMutex);
        bridge.selectedNodeId = added;
      }
      setStatus(std::format("Node {} added", added), false);
    }

    ImGui::Spacing();
    std::uint32_t typed = selectedNode;
    if (PROPERTY::beginFixed("##truss_selected_node")) {
      if (PROPERTY::inputId("Selected node", &typed, noSelection, "Type an id and press Enter, or click a node in the viewport.")) {
        {
          std::lock_guard lock(bridge.dataMutex);
          const bool exists = bridge.activeMesh && typed < bridge.activeMesh->trussNodes.size();
          bridge.selectedNodeId = exists ? typed : noSelection;
        }
        bridge.dataVersion.fetch_add(1, std::memory_order_release); // redraw the highlight
      }
      if (selectedNode != noSelection) PROPERTY::inputVector3("Position", m_nodePosition.data(), "m");
      PROPERTY::end();
    }
    if (selectedNode == noSelection) {
      ImGui::TextDisabled("Click a node in the viewport (Nodes toolbar toggle on) or type its id.");
      return;
    }
    if (ImGui::Button("Move Node", ImVec2(LAYOUT::splitWidth(2), 0.0f))) {
      const bool moved = editModel(bridge, [&](FEM::TRUSS::MeshData& mesh) {
        if (selectedNode >= mesh.trussNodes.size()) return false;
        mesh.trussNodes[selectedNode].setLocation(m_nodePosition);
        return true;
      });
      if (moved) setStatus(std::format("Node {} moved", selectedNode), false);
    }
    ImGui::SameLine();
    if (ImGui::Button("Delete Node", ImVec2(-FLT_MIN, 0.0f))) {
      const bool deleted = editModel(bridge, [&](FEM::TRUSS::MeshData& mesh) {
        if (selectedNode >= mesh.trussNodes.size()) return false;
        FEM::TRUSS::deleteNode(mesh, selectedNode);
        return true;
      });
      if (deleted) {
        {
          std::lock_guard lock(bridge.dataMutex);
          bridge.selectedNodeId = noSelection;
        }
        m_selectedBar = noSelection;
        m_loadedNode = noSelection;
        setStatus(std::format("Node {} deleted with its bars; later node ids moved down by one", selectedNode), false);
      }
    }
  }

  void TrussModelEditor::renderBars() {
    if (!PROPERTY::header("Bars")) return;
    auto& bridge = BRIDGE::buildBridge();

    if (PROPERTY::beginFixed("##truss_bar_properties")) {
      if (PROPERTY::group("Properties")) {
        PROPERTY::row("Material", "Used by Add Bar and Apply to Selected.");
        PROPERTY::reserveBrowse();
        materialCombo(bridge, "##editor_bar_material", m_materialID);
        if (PROPERTY::browseButton("##truss_materials", "Open the Material Handler") && onOpenMaterialHandler) onOpenMaterialHandler();
        PROPERTY::inputDouble("Area A", &m_areaCm2, "cm\u00b2", "%.4g", "Cross-section area; used by Add Bar and Apply to Selected.");
        if (m_areaCm2 < 0.0) m_areaCm2 = 0.0;
      }
      PROPERTY::end();
    }

    ImGui::SeparatorText("Add / Edit");
    if (PROPERTY::beginFixed("##truss_bar_nodes")) {
      PROPERTY::row("Nodes A - B", "Ids of the two end nodes; after Add, A takes B for the next bar of a chain.");
      ImGui::SetNextItemWidth(LAYOUT::splitWidth(2));
      ImGui::InputScalar("##bar_a", ImGuiDataType_U32, &m_barNodeA);
      ImGui::SameLine();
      ImGui::SetNextItemWidth(-FLT_MIN);
      ImGui::InputScalar("##bar_b", ImGuiDataType_U32, &m_barNodeB);
      PROPERTY::end();
    }

    // Assigns the chosen material and area; false (with a status) if either is unusable.
    const auto assign = [&](FEM::TRUSS::RenderElement& element) {
      const auto index = bridge.findMaterialIndex(m_materialID); // caller holds dataMutex
      if (!index || !(m_areaCm2 > 0.0)) return false;
      element.materialID = *index;
      element.crossSectionArea = m_areaCm2 * cm2ToM2;
      element.isWireframe = false;
      return true;
    };

    if (ImGui::Button("Add Bar", ImVec2(-FLT_MIN, 0.0f))) {
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
        FEM::TRUSS::RenderElement element{a, b, 0.0f, false, 0u, 0.0, false};
        if (!assign(element)) {
          error = "choose a material and an area greater than 0";
          return false;
        }
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

    const auto mesh = currentMesh(bridge);
    const std::size_t count = mesh ? mesh->trussElements.size() : 0;
    if (m_selectedBar != noSelection && m_selectedBar >= count) m_selectedBar = noSelection;

    ImGui::BeginChild("EditorBarList", ImVec2(0.0f, 160.0f), true);
    if (mesh) {
      std::lock_guard lock(bridge.dataMutex); // material names
      ImGuiListClipper clipper;
      clipper.Begin(static_cast<int>(count));
      while (clipper.Step()) {
        for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
          const auto index = static_cast<std::uint32_t>(row);
          const auto& element = mesh->trussElements[index];
          std::string label;
          if (element.isWireframe) {
            label = std::format("{}: {} - {}  (wireframe edge)", index, element.node1, element.node2);
          } else {
            const auto material = element.materialID < bridge.allMaterials.size()
              ? std::string(bridge.allMaterials[element.materialID].getMaterialType()) : std::string("?");
            label = std::format("{}: {} - {}  {}  {:.4g} cm\u00b2", index, element.node1, element.node2, material,
                                element.crossSectionArea / cm2ToM2);
          }
          ImGui::PushID(row);
          if (ImGui::Selectable(label.c_str(), m_selectedBar == index)) {
            m_selectedBar = m_selectedBar == index ? noSelection : index;
          }
          ImGui::PopID();
        }
      }
    }
    ImGui::EndChild();

    ImGui::BeginDisabled(m_selectedBar == noSelection);
    if (ImGui::Button("Apply to Selected", ImVec2(LAYOUT::splitWidth(2), 0.0f))) {
      const auto bar = m_selectedBar;
      const bool applied = editModel(bridge, [&](FEM::TRUSS::MeshData& edited) {
        return bar < edited.trussElements.size() && assign(edited.trussElements[bar]);
      });
      if (applied) setStatus(std::format("Bar {} updated", bar), false);
      else setStatus("Choose a material and an area greater than 0", true);
    }
    ImGui::SameLine();
    if (ImGui::Button("Delete Bar", ImVec2(-FLT_MIN, 0.0f))) {
      const auto bar = m_selectedBar;
      const bool deleted = editModel(bridge, [&](FEM::TRUSS::MeshData& edited) {
        if (bar >= edited.trussElements.size()) return false;
        edited.trussElements.erase(edited.trussElements.begin() + static_cast<std::ptrdiff_t>(bar));
        return true;
      });
      if (deleted) {
        m_selectedBar = noSelection;
        setStatus(std::format("Bar {} deleted", bar), false);
      }
    }
    ImGui::EndDisabled();
  }

  void TrussModelEditor::renderWholeModel() {
    if (!PROPERTY::header("Whole Model", false)) return;
    auto& bridge = BRIDGE::buildBridge();
    const auto mesh = currentMesh(bridge);

    std::size_t bars = 0, wireframe = 0;
    if (mesh) {
      for (const auto& element : mesh->trussElements) {
        if (element.isWireframe) ++wireframe;
        else ++bars;
      }
    }
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("One material and cross-section on every bar at once (e.g. after importing a file without "
                        "sections). Single bars can still be changed under Bars.");
    ImGui::PopTextWrapPos();
    if (PROPERTY::beginFixed("##truss_whole_model")) {
      PROPERTY::row("Material");
      materialCombo(bridge, "##editor_whole_material", m_wholeMaterialID);
      PROPERTY::inputDouble("Area A", &m_wholeAreaCm2, "cm\u00b2", "%.4g");
      if (m_wholeAreaCm2 < 0.0) m_wholeAreaCm2 = 0.0;
      if (wireframe > 0) {
        PROPERTY::row("Wireframe edges", "Also turn the wireframe edges of an imported surface mesh into bars.");
        ImGui::Checkbox(std::format("Make {} into bars##editor_include_wireframe", wireframe).c_str(), &m_includeWireframe);
      }
      PROPERTY::end();
    }

    ImGui::BeginDisabled(bars == 0 && (wireframe == 0 || !m_includeWireframe));
    if (ImGui::Button("Apply to Whole Model", ImVec2(-FLT_MIN, 0.0f))) {
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
      if (applied) setStatus(std::format("Material and {:.4g} cm^2 set on {} bars", m_wholeAreaCm2, changed), false);
      else setStatus("Nothing changed: choose a material and an area greater than 0", true);
    }
    ImGui::EndDisabled();
  }

  void TrussModelEditor::readLibrary() {
    m_libraryRead = true;
    m_library.clear();
    m_libraryDir = anaf::DIRECTORY::findAssetPath(std::filesystem::path(FEM::TRUSS::LIBRARY::librarySubdir));
    if (m_libraryDir.empty()) {
      m_libraryError = std::format("assets/{} not found", FEM::TRUSS::LIBRARY::librarySubdir);
      anaf::LOG::warn("Built-in truss library: {}", m_libraryError);
      return;
    }
    auto index = FEM::TRUSS::LIBRARY::loadIndex(m_libraryDir / std::filesystem::path(FEM::TRUSS::LIBRARY::indexFileName));
    if (!index) {
      m_libraryError = index.error();
      anaf::LOG::warn("Built-in truss library: {}", m_libraryError);
      return;
    }
    // Grouped by category in the combo.
    std::ranges::stable_sort(*index, {}, &FEM::TRUSS::LIBRARY::Entry::category);
    m_library = std::move(*index);
    m_libraryError.clear();
  }

  void TrussModelEditor::renderLibrary() {
    if (!PROPERTY::header("Built-in Models", false)) return;
    if (!m_libraryRead) readLibrary();
    if (m_library.empty()) {
      ImGui::TextDisabled("%s", m_libraryError.empty() ? "No built-in models" : m_libraryError.c_str());
      return;
    }
    m_librarySelected = std::clamp(m_librarySelected, 0, static_cast<int>(m_library.size()) - 1);
    const auto& selected = m_library[static_cast<std::size_t>(m_librarySelected)];

    const std::string preview = selected.category + ": " + selected.name;
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::BeginCombo("##builtin_truss", preview.c_str())) {
      std::string_view category;
      for (int i = 0; i < static_cast<int>(m_library.size()); ++i) {
        const auto& entry = m_library[static_cast<std::size_t>(i)];
        if (entry.category != category) {
          category = entry.category;
          ImGui::SeparatorText(entry.category.c_str());
        }
        ImGui::PushID(i);
        if (ImGui::Selectable(entry.name.c_str(), i == m_librarySelected)) m_librarySelected = i;
        ImGui::PopID();
      }
      ImGui::EndCombo();
    }
    // Long descriptions scroll inside a box of at most five lines.
    ImGui::SetNextWindowSizeConstraints(ImVec2(0.0f, 0.0f), ImVec2(FLT_MAX, ImGui::GetTextLineHeightWithSpacing() * 5.0f + 8.0f));
    ImGui::BeginChild("##builtin_truss_description", ImVec2(0.0f, 0.0f), ImGuiChildFlags_FrameStyle | ImGuiChildFlags_AutoResizeY);
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("%s", selected.description.c_str());
    ImGui::PopTextWrapPos();
    ImGui::EndChild();
    if (ImGui::Button("Load Built-in Model", ImVec2(-FLT_MIN, 0.0f)) && onLoadBuiltin) {
      onLoadBuiltin(FEM::TRUSS::LIBRARY::modelFile(m_libraryDir, selected));
    }
    ImGui::SetItemTooltip("Read-only: loading makes a copy; save changes with File > Export.");
  }

  void TrussModelEditor::renderSupportsAndLoads(const std::uint32_t selectedNode, const bool withLoads) {
    // ### keeps one ID (open state) for both labels.
    if (!PROPERTY::header(withLoads ? "Supports & Nodal Loads###truss_supports" : "Supports###truss_supports")) return;
    if (selectedNode == noSelection) {
      ImGui::TextDisabled("Click a node in the viewport (or type its id in the Model tab).");
      return;
    }
    auto& bridge = BRIDGE::buildBridge();
    ImGui::Text("Node %u", selectedNode);

    bool supportOpen = false;
    if (PROPERTY::beginFixed("##truss_support")) {
      supportOpen = PROPERTY::group("Support");
      if (supportOpen) {
        int supportType = m_supportInclined ? 1 : 0;
        PROPERTY::row("Directions", "Global axes: fix single global directions.\nInclined: give the directions as vectors (skewed support).");
        constexpr std::array<const char*, 2> directionModes{"Global axes", "Inclined vectors"};
        ImGui::Combo("##editor_support_mode", &supportType, directionModes.data(), static_cast<int>(directionModes.size()));
        m_supportInclined = supportType == 1;
        if (m_supportInclined) {
          renderInclinedSupportInputs();
        } else {
          PROPERTY::row("Fixed");
          ImGui::Checkbox("X##editor_fix_x", &m_fixed[0]);
          ImGui::SameLine();
          ImGui::Checkbox("Y##editor_fix_y", &m_fixed[1]);
          ImGui::SameLine();
          ImGui::Checkbox("Z##editor_fix_z", &m_fixed[2]);
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
    if (ImGui::Button("Apply Support", ImVec2(-FLT_MIN, 0.0f))) {
      const bool applied = editModel(bridge, [&](FEM::TRUSS::MeshData& mesh) {
        if (selectedNode >= mesh.trussNodes.size()) return false;
        auto& node = mesh.trussNodes[selectedNode];
        if (!m_supportInclined) {
          node.setMovable({!m_fixed[0], !m_fixed[1], !m_fixed[2]});
        } else if (allowed.size() == 3 || allowed.empty()) {
          const bool free = !allowed.empty();
          node.setMovable({free, free, free});
        } else {
          node.setAllowedMotionDirections(allowed);
        }
        // The checkboxes show the global axes outside the allowed subspace.
        const auto& movable = node.getMovable();
        m_fixed = {!movable[0], !movable[1], !movable[2]};
        return true;
      });
      if (applied) setStatus(std::format("Support of node {} updated", selectedNode), false);
    }
    ImGui::EndDisabled();
    if (!withLoads) return;

    ImGui::Spacing();
    bool loadOpen = false;
    if (PROPERTY::beginFixed("##truss_nodal_load")) {
      loadOpen = PROPERTY::group("Nodal Load");
      if (loadOpen) PROPERTY::inputVector3("Force", m_force.data(), "N", "%.4g", "Global axes.");
      PROPERTY::end();
    }
    if (!loadOpen) return;
    const auto setLoad = [&](const std::array<double, 3> force) {
      return editModel(bridge, [&](FEM::TRUSS::MeshData& mesh) {
        if (selectedNode >= mesh.trussNodes.size()) return false;
        std::erase_if(mesh.appliedForces, [&](const FEM::TRUSS::ForceApplied& applied) {
          return applied.getAppliedNode() == selectedNode;
        });
        if (force[0] != 0.0 || force[1] != 0.0 || force[2] != 0.0) mesh.appliedForces.emplace_back(selectedNode, force);
        return true;
      });
    };
    if (ImGui::Button("Apply Load", ImVec2(LAYOUT::splitWidth(2), 0.0f))) {
      if (setLoad(m_force)) setStatus(std::format("Load on node {} updated", selectedNode), false);
    }
    ImGui::SameLine();
    if (ImGui::Button("Remove Load", ImVec2(-FLT_MIN, 0.0f))) {
      m_force = {0.0, 0.0, 0.0};
      if (setLoad(m_force)) setStatus(std::format("Load on node {} removed", selectedNode), false);
    }
  }

  void TrussModelEditor::renderInclinedSupportInputs() {
    bool restrained = m_vectorsRestrained;
    PROPERTY::row("Vectors give", "Restrained: the support reaction directions (1 = roller on a plane, 2 = guide along a line, "
                                  "3 = pin).\nAllowed: the line (1) or plane (2) the node may move in.");
    if (ImGui::RadioButton("Restrained##editor_support_restrained", restrained)) restrained = true;
    ImGui::SameLine();
    if (ImGui::RadioButton("Allowed##editor_support_allowed", !restrained)) restrained = false;
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
      ImGui::RadioButton(std::format("{}##editor_support_count", count).c_str(), &m_supportVectorCount, count);
    }
    for (int i = 0; i < m_supportVectorCount; ++i) {
      PROPERTY::inputVector3(std::format("d{}", i + 1).c_str(), m_supportVectors[static_cast<std::size_t>(i)].data(), nullptr, "%.4g",
                             "Direction in global axes (normalized by the solver).");
    }
  }

  void TrussModelEditor::renderSolve() {
    auto& bridge = BRIDGE::buildBridge();
    const auto mesh = currentMesh(bridge);

    double scale = bridge.deformScale.load();
    LAYOUT::field("Deformation");
    if (ImGui::InputDouble("##editor_deformation_scale", &scale, 0.0, 0.0, "%.3f")) {
      bridge.deformScale = scale;
      bridge.dataVersion.fetch_add(1, std::memory_order_release);
    }
    ImGui::SetItemTooltip("Deformation scale of the drawn shape (1 = true size)");

    if (bridge.isRunning.load()) {
      ImGui::ProgressBar(bridge.progress.load(), ImVec2(-FLT_MIN, 0.0f));
      ImGui::BeginDisabled();
      LAYOUT::primaryButton("Calculating...");
      ImGui::EndDisabled();
    } else {
      ImGui::BeginDisabled(!mesh);
      if (LAYOUT::primaryButton("Run Solver for Truss##editor")) {
        m_status.clear();
        TRUSS_WORKER::startSolve(bridge, [mesh]() -> std::expected<std::shared_ptr<const FEM::TRUSS::MeshData>, std::string> {
          return mesh;
        });
      }
      ImGui::EndDisabled();
    }
  }

  void TrussModelEditor::renderModelButtons() {
    auto& bridge = BRIDGE::buildBridge();
    ImGui::BeginDisabled(bridge.isRunning.load() || bridge.isGeneratingPreview.load());
    if (ImGui::Button("Import File...", ImVec2(LAYOUT::splitWidth(2), 0.0f)) && onRequestImport) onRequestImport();
    ImGui::SetItemTooltip("File > Import Mesh / CAD (Ctrl+O)");
    ImGui::SameLine();
    if (ImGui::Button("Clear Model", ImVec2(-FLT_MIN, 0.0f))) {
      bridge.resetModel(BRIDGE::E_ObjectType::TrussImportedOrEntered);
      resetState();
    }
    ImGui::EndDisabled();
  }

  void TrussModelEditor::onImGuiRender() {
    if (!isOpen) return;
    auto& bridge = BRIDGE::buildBridge();

    std::uint32_t selectedNode = noSelection;
    {
      std::lock_guard lock(bridge.dataMutex);
      selectedNode = bridge.selectedNodeId;
      if (!bridge.activeMesh || selectedNode >= bridge.activeMesh->trussNodes.size()) selectedNode = noSelection;
    }
    syncSelection(selectedNode);

    const auto loadKind = bridge.loadKind.load();
    const bool dynamic = loadKind == BRIDGE::E_LoadKind::Dynamic;
    const bool running = bridge.isRunning.load();
    // Footer rows: deformation scale (+ progress bar) and the model buttons; one run button.
    const float footer = dynamic ? LAYOUT::footerHeight(1, 1) : LAYOUT::footerHeight(running ? 3 : 2, 1);

    ImGui::Begin("Truss(1D) Model Editor", &isOpen);
    renderSummary();

    // The solve works on a copy; edits made meanwhile would be overwritten by its result.
    const bool busy = running || bridge.isGeneratingPreview.load();
    if (ImGui::BeginTabBar("##truss_editor_tabs")) {
      if (ImGui::BeginTabItem("Model")) {
        LAYOUT::beginBody("##truss_editor_model_tab", footer);
        ImGui::BeginDisabled(busy);
        renderLibrary();
        renderNodes(selectedNode);
        renderBars();
        renderWholeModel();
        ImGui::EndDisabled();
        LAYOUT::endBody();
        ImGui::EndTabItem();
      }
      // ### keeps one tab (and its selection) for both labels.
      if (ImGui::BeginTabItem(dynamic ? "Supports###truss_editor_loads_tab" : "Supports & Loads###truss_editor_loads_tab")) {
        LAYOUT::beginBody("##truss_editor_loads_tab", footer);
        ImGui::BeginDisabled(busy);
        renderSupportsAndLoads(selectedNode, !dynamic);
        ImGui::EndDisabled();
        LAYOUT::endBody();
        ImGui::EndTabItem();
      }
      if (ImGui::BeginTabItem("Analysis")) {
        LAYOUT::beginBody("##truss_editor_analysis_tab", footer);
        ImGui::BeginDisabled(busy);
        renderAnalysisTab(loadKind, m_dynamic);
        ImGui::EndDisabled();
        LAYOUT::endBody();
        ImGui::EndTabItem();
      }
      ImGui::EndTabBar();
    }

    if (dynamic) renderDynamicRunButton();
    else renderSolve();
    renderModelButtons();
    ImGui::End();
  }

} // namespace anaf::GUI end
