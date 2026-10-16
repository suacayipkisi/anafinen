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

#include "beamDiagramPanel.hpp"

#include <guiMaterials/theme.hpp>

#include <beam/beamEngine/beamSolver/deformationUnderConstForce.hpp>

#include "imgui.h"
#include "imgui_internal.h" // DockBuilder API, FindWindowByName
#include "implot.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <exception>
#include <mutex>
#include <stdexcept>

namespace anaf::GUI {

  namespace {
    struct Quantity {
      const char* label;
      const char* unit;
    };
    constexpr std::array<Quantity, 9> quantities{{
      {"Axial force N", "kN"}, {"Shear force Vy", "kN"}, {"Shear force Vz", "kN"}, {"Torque T", "kN m"},
      {"Bending moment My", "kN m"}, {"Bending moment Mz", "kN m"}, {"Displacement (local u, v, w)", "mm"},
      {"Normal stress (max / min)", "MPa"}, {"von Mises stress", "MPa"},
    }};
  } // namespace end

  void BeamDiagramPanel::dockUnderModelTree() {
    ImGuiWindow* tree = ImGui::FindWindowByName("Model Tree");
    if (!tree || !tree->DockNode) return; // try again next frame
    ImGuiWindow* self = ImGui::FindWindowByName(windowName);
    if (self && self->DockNode) {
      m_dockPending = false; // already docked somewhere: keep the user's layout
      return;
    }
    ImGuiID top = tree->DockNode->ID;
    const ImGuiID bottom = ImGui::DockBuilderSplitNode(top, ImGuiDir_Down, 0.5f, nullptr, &top);
    ImGui::DockBuilderDockWindow(windowName, bottom);
    ImGui::DockBuilderFinish(ImGui::DockNodeGetRootNode(tree->DockNode)->ID);
    m_dockPending = false;
  }

  void BeamDiagramPanel::rebuild(const std::shared_ptr<const FEM::BEAM::MeshData>& mesh, const std::uint32_t element) {
    m_mesh = mesh;
    m_element = element;
    m_states.clear();
    m_stresses.clear();
    m_sectionName.clear();
    m_error.clear();
    if (!mesh || !mesh->hasResults || element >= mesh->elements.size()) return;

    auto& bridge = BRIDGE::buildBridge();
    std::vector<MATERIAL::Material> materials;
    std::vector<FEM::BEAM::BeamSection> sections;
    {
      std::lock_guard lock(bridge.dataMutex);
      materials = bridge.allMaterials;
      sections = bridge.allSections;
    }
    m_plot = PlotSeries{};
    try {
      // Only this element: its section properties and loads, not those of the whole model.
      const auto& beam = mesh->elements[element];
      if (beam.materialID >= materials.size() || beam.sectionID >= sections.size()) {
        throw std::invalid_argument("material or section index out of range");
      }
      const auto& shape = sections[beam.sectionID].getShape();
      const auto properties = FEM::BEAM::computeProperties(shape, materials[beam.materialID].getPoisson());
      const auto load = FEM::BEAM::elementLocalLoad(mesh->nodes, mesh->elements, element, properties, mesh->distributedLoads, mesh->gravity, materials);
      m_states = FEM::BEAM::sampleElement(*mesh, element, sampleCount, load, materials, sections);
      for (const auto& state : m_states) m_stresses.push_back(FEM::BEAM::sectionStress(shape, properties, state.forces));
      m_sectionName = sections[beam.sectionID].getName();
    } catch (const std::exception& exception) {
      // The lists changed after the solve (e.g. a section was removed): run the solver again.
      m_states.clear();
      m_stresses.clear();
      m_error = exception.what();
      return;
    }

    for (const auto& state : m_states) {
      m_plot.x.push_back(state.position);
      for (std::size_t k = 0; k < 6; ++k) m_plot.forces[k].push_back(state.forces[k] / 1e3);
      for (std::size_t axis = 0; axis < 3; ++axis) m_plot.displacement[axis].push_back(state.localDisplacement[axis] * 1e3);
    }
    if (!m_stresses.empty() && m_stresses.front()) {
      for (const auto& stress : m_stresses) {
        m_plot.maxNormal.push_back(stress->maxNormal / 1e6);
        m_plot.minNormal.push_back(stress->minNormal / 1e6);
        m_plot.vonMises.push_back(stress->vonMises / 1e6);
      }
    }
  }

  void BeamDiagramPanel::renderPlot() {
    const auto& quantity = quantities[static_cast<std::size_t>(m_quantity)];
    const auto& x = m_plot.x;
    const int count = static_cast<int>(x.size());
    const bool hasStresses = !m_plot.vonMises.empty();

    if (!ImPlot::BeginPlot("##beam_diagram", ImVec2(-1.0f, ImGui::GetContentRegionAvail().y * 0.6f))) return;
    ImPlot::SetupAxes("x from node 1 [m]", quantity.unit, ImPlotAxisFlags_AutoFit, ImPlotAxisFlags_AutoFit);
    if (m_quantity <= 5) {
      const auto& y = m_plot.forces[static_cast<std::size_t>(m_quantity)];
      ImPlotSpec shade;
      shade.FillAlpha = 0.25f;
      ImPlot::PlotShaded(quantity.label, x.data(), y.data(), count, 0.0, shade);
      ImPlot::PlotLine(quantity.label, x.data(), y.data(), count);
    } else if (m_quantity == 6) {
      constexpr std::array<const char*, 3> names{"u (axial)", "v (local y)", "w (local z)"};
      for (std::size_t axis = 0; axis < 3; ++axis) ImPlot::PlotLine(names[axis], x.data(), m_plot.displacement[axis].data(), count);
    } else if (hasStresses) {
      if (m_quantity == 7) {
        ImPlot::PlotLine("max", x.data(), m_plot.maxNormal.data(), count);
        ImPlot::PlotLine("min", x.data(), m_plot.minNormal.data(), count);
      } else {
        ImPlotSpec shade;
        shade.FillAlpha = 0.25f;
        ImPlot::PlotShaded("von Mises", x.data(), m_plot.vonMises.data(), count, 0.0, shade);
        ImPlot::PlotLine("von Mises", x.data(), m_plot.vonMises.data(), count);
      }
    }
    ImPlot::EndPlot();
    if (m_quantity >= 7 && !hasStresses) {
      ImGui::TextDisabled("General section (no shape): no stresses.");
    }
  }

  void BeamDiagramPanel::renderTable() {
    const auto& element = m_mesh->elements[m_element];
    const auto& s = element.sectionForces;
    ImGui::TextWrapped("Element %u: nodes %u - %u, %s, %s", m_element, element.node1, element.node2, m_sectionName.c_str(),
                element.formulation == FEM::BEAM::E_Formulation::Timoshenko ? "Timoshenko" : "Euler-Bernoulli");
    // One row per force component, so the table fits a narrow dock column.
    if (ImGui::BeginTable("BeamEndForces", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchSame)) {
      ImGui::TableSetupColumn("End forces");
      ImGui::TableSetupColumn("Node 1");
      ImGui::TableSetupColumn("Node 2");
      ImGui::TableHeadersRow();
      constexpr std::array<const char*, 6> names{"N [kN]", "Vy [kN]", "Vz [kN]", "T [kN m]", "My [kN m]", "Mz [kN m]"};
      for (std::size_t k = 0; k < 6; ++k) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(names[k]);
        ImGui::TableNextColumn();
        ImGui::Text("%.4g", s[k] / 1e3);
        ImGui::TableNextColumn();
        ImGui::Text("%.4g", s[6 + k] / 1e3);
      }
      ImGui::EndTable();
    }
    const auto& stress = element.stress;
    if (stress.available) {
      ImGui::TextWrapped("sigma max / min: %.4g / %.4g MPa, tau max: %.4g MPa", stress.maxNormal / 1e6, stress.minNormal / 1e6, stress.maxShear / 1e6);
      ImGui::PushTextWrapPos(0.0f);
      ImGui::TextColored(stress.isStressExceeded ? THEME::theme().bad : THEME::theme().good,
                         "von Mises max: %.4g MPa at x = %.3g m%s", stress.maxVonMises / 1e6, stress.vonMisesPosition,
                         stress.isStressExceeded ? " (exceeds yield)" : "");
      ImGui::TextDisabled("von Mises is an upper bound (largest sigma and tau combined).");
      ImGui::PopTextWrapPos();
    }
  }

  void BeamDiagramPanel::onImGuiRender() {
    if (ImGui::GetFrameCount() - m_lastFrame > 1) m_dockPending = true; // reopened
    m_lastFrame = ImGui::GetFrameCount();
    if (m_dockPending) dockUnderModelTree();

    auto& bridge = BRIDGE::buildBridge();
    std::shared_ptr<const FEM::BEAM::MeshData> mesh;
    std::uint32_t selected = noSelection;
    {
      std::lock_guard lock(bridge.dataMutex);
      mesh = bridge.activeBeamMesh;
      selected = bridge.selectedElementId;
    }

    ImGui::Begin(windowName, &isOpen);
    if (!mesh || mesh->elements.empty()) {
      ImGui::TextDisabled("No beam model.");
      ImGui::End();
      return;
    }
    const auto count = static_cast<std::uint32_t>(mesh->elements.size());
    if (selected >= count) selected = 0;

    // Element choice, shared with the editor through bridge.selectedElementId.
    std::uint32_t chosen = selected;
    ImGui::SetNextItemWidth(110.0f);
    ImGui::InputScalar("Element##diagram", ImGuiDataType_U32, &chosen);
    ImGui::SameLine();
    if (ImGui::ArrowButton("##diagram_previous", ImGuiDir_Left)) chosen = chosen == 0 ? count - 1 : chosen - 1;
    ImGui::SameLine();
    if (ImGui::ArrowButton("##diagram_next", ImGuiDir_Right)) chosen = (chosen + 1) % count;
    ImGui::SameLine();
    ImGui::BeginDisabled(!mesh->hasResults);
    if (ImGui::Button("Highest stress")) {
      double worst = -1.0;
      for (std::uint32_t e = 0; e < count; ++e) {
        if (mesh->elements[e].stress.available && mesh->elements[e].stress.maxVonMises > worst) {
          worst = mesh->elements[e].stress.maxVonMises;
          chosen = e;
        }
      }
    }
    ImGui::EndDisabled();
    chosen = std::min(chosen, count - 1);
    if (chosen != selected) {
      std::lock_guard lock(bridge.dataMutex);
      bridge.selectedElementId = chosen;
    }

    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::BeginCombo("##diagram_quantity", quantities[static_cast<std::size_t>(m_quantity)].label)) {
      for (int q = 0; q < static_cast<int>(quantities.size()); ++q) {
        if (ImGui::Selectable(quantities[static_cast<std::size_t>(q)].label, q == m_quantity)) m_quantity = q;
      }
      ImGui::EndCombo();
    }

    if (!mesh->hasResults) {
      ImGui::TextDisabled("Run the solver to see the diagrams.");
      ImGui::End();
      return;
    }
    if (mesh != m_mesh || chosen != m_element) rebuild(mesh, chosen);
    if (!m_error.empty()) {
      ImGui::TextColored(THEME::theme().bad, "Diagram not available: %s (run the solver again)", m_error.c_str());
    } else if (!m_states.empty()) {
      renderPlot();
      renderTable();
    }
    ImGui::End();
  }

} // namespace anaf::GUI end
