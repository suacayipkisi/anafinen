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

#include "trussControlPanel.hpp"

#include "imgui.h"

#include <stop_token>
#include <thread>

#include "../../../../truss_1D/trussEngine/trussSolver.hpp"
#include "../../../../bridge/generalStatus.hpp"

namespace anaf::GUI {

    namespace {
        void ensureDemoTrussCase(BRIDGE::Gui_Calc_Bridge& bridge, std::uint32_t forceNodeId) {
            bridge.fixedDOFsByNode.clear();
            bridge.fixedDOFsByNode[0u] = {true, true, true};
            bridge.fixedDOFsByNode[10u] = {true, true, true};
            bridge.fixedDOFsByNode[220u] = {true, true, true};
            bridge.fixedDOFsByNode[230u] = {true, true, true};
            bridge.appliedForces.clear();
            bridge.appliedForces.emplace_back(forceNodeId, std::array<double, 3>{0.0, 10000.0, 0.0});
            bridge.selectedNodeId = forceNodeId;
        }
    }

    void TrussControlPanel::onImGuiRender() {
        BRIDGE::Gui_Calc_Bridge& bridge = BRIDGE::buildBridge();

        ImGui::Begin("Truss(1D) Analysis Set", &isOpen);

        ImGui::Text("Truss Parameters");
        ImGui::Separator();

        if (bridge.fixedDOFsByNode.empty() || bridge.appliedForces.empty()) {
            ensureDemoTrussCase(bridge, m_forceNodeId);
        }

        if (bridge.selectedNodeId == std::numeric_limits<std::uint32_t>::max()) {
            bridge.selectedNodeId = m_forceNodeId;
        }

        if (bridge.selectedNodeId != std::numeric_limits<std::uint32_t>::max()) {
            ImGui::Text("Selected node: %u", bridge.selectedNodeId);
        }
        else {
            ImGui::Text("Selected node: none");
        }

        if (m_type >= bridge.allMaterials.size()) {
            ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.7f, 1.0f), "Material Type is outside the available materials.");
        }

        if (bridge.m_isGeneratingPreview.load()) {
            ImGui::BeginDisabled();
            ImGui::Button("Generating Preview...");
            ImGui::EndDisabled();
        }
        else if (ImGui::Button("Generate Preview")) {
            anaf::LOG::core("Press 'ctrl' to toggle node visibility");

            bridge.m_isGeneratingPreview = true;
            bridge.workerThread = std::jthread(
                [&bridge,
                 cubeNumX = m_cubeNumX,
                 cubeNumY = m_cubeNumY,
                 cubeNumZ = m_cubeNumZ,
                 cubeEdgeLength = m_cubeEdgeLength,
                 crossSectionalArea = m_crossSectionalArea,
                 type = m_type](std::stop_token st) mutable {
                    try {
                        FEM::TRUSS::SimpleTruss preview{{cubeNumX, cubeNumY, cubeNumZ}, cubeEdgeLength, crossSectionalArea, type};
                        preview.setTruss();

                        {
                            std::lock_guard lock(bridge.dataMutex);
                            bridge.trussNodes.assign(preview.getNodes().begin(), preview.getNodes().end());
                            bridge.trussElements.assign(preview.getElements().begin(), preview.getElements().end());
                            for (auto& node : bridge.trussNodes) {
                                std::array<bool, 3> movable{true, true, true};
                                const auto it = bridge.fixedDOFsByNode.find(node.getNodeID());
                                if (it != bridge.fixedDOFsByNode.end()) {
                                    movable = { !it->second[0], !it->second[1], !it->second[2] };
                                }
                                node.setMovable(movable);
                            }
                            bridge.hasTrussPreview = true;
                            bridge.selectedNodeId = bridge.trussNodes.empty() ? std::numeric_limits<std::uint32_t>::max() : 0u;
                            bridge.dataVersion.fetch_add(1, std::memory_order_relaxed);
                        }
                    } catch (const std::exception&) {
                        std::lock_guard lock(bridge.dataMutex);
                        bridge.hasTrussPreview = false;
                        bridge.trussNodes.clear();
                        bridge.trussElements.clear();
                    }

                    bridge.m_isGeneratingPreview = false;
            });
        }

        if (ImGui::BeginTable("TrussParamsTable", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_BordersInnerH)) {
        
            ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthStretch, 0.6f);
            ImGui::TableSetupColumn("Control", ImGuiTableColumnFlags_WidthStretch, 0.4f);

            // cube num x
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::AlignTextToFramePadding(); // Metni input kutusuyla dikeyde hizalar
            ImGui::Text("Cube Number X (N)");
            ImGui::TableSetColumnIndex(1);
            ImGui::SetNextItemWidth(-FLT_MIN); // Kutuyu sutunun tamamina yayar
            ImGui::InputScalar("##cube_x", ImGuiDataType_U32, &m_cubeNumX);

            // cube num y
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::AlignTextToFramePadding();
            ImGui::Text("Cube Number Y (N)");
            ImGui::TableSetColumnIndex(1);
            ImGui::SetNextItemWidth(-FLT_MIN);
            ImGui::InputScalar("##cube_y", ImGuiDataType_U32, &m_cubeNumY);

            // cube num z
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::AlignTextToFramePadding();
            ImGui::Text("Cube Number Z (N)");
            ImGui::TableSetColumnIndex(1);
            ImGui::SetNextItemWidth(-FLT_MIN);
            ImGui::InputScalar("##cube_z", ImGuiDataType_U32, &m_cubeNumZ);

            // material type
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::AlignTextToFramePadding();
            ImGui::Text("Material Type");
            ImGui::TableSetColumnIndex(1);
            ImGui::SetNextItemWidth(-FLT_MIN);
            ImGui::InputScalar("##mat_type", ImGuiDataType_U32, &m_type);

            // element length
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::AlignTextToFramePadding();
            ImGui::Text("Element Length (m)");
            ImGui::TableSetColumnIndex(1);
            ImGui::SetNextItemWidth(-FLT_MIN);
            ImGui::InputDouble("##edge_length", &m_cubeEdgeLength, 0.0, 0.0, "%.2f");

            // cross-sectional area
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::AlignTextToFramePadding();
            ImGui::Text("Cross-Sectional Area");
            ImGui::TableSetColumnIndex(1);
            ImGui::SetNextItemWidth(-FLT_MIN);
            ImGui::InputDouble("##cross_area", &m_crossSectionalArea, 0.0, 0.0, "%.2f");

            ImGui::EndTable();
        }

        ImGui::Separator();
        ImGui::Text("Node Forces & Constraints");

        if (bridge.selectedNodeId != std::numeric_limits<std::uint32_t>::max()) {
            m_forceNodeId = bridge.selectedNodeId;
        }
        if (bridge.appliedForces.empty()) {
            bridge.appliedForces.emplace_back(m_forceNodeId, m_forceVector);
        }

        m_appliedForces = bridge.appliedForces;

        ImGui::InputScalar("Node ID##force_node", ImGuiDataType_U32, &m_forceNodeId);
        ImGui::InputDouble("Fx##force_fx", &m_forceVector[0], 0.0, 0.0, "%.3f");
        ImGui::InputDouble("Fy##force_fy", &m_forceVector[1], 0.0, 0.0, "%.3f");
        ImGui::InputDouble("Fz##force_fz", &m_forceVector[2], 0.0, 0.0, "%.3f");

        if (ImGui::Button("Apply Load to Selected Node")) {
            if (bridge.selectedNodeId != std::numeric_limits<std::uint32_t>::max()) {
                m_forceNodeId = bridge.selectedNodeId;
            }
            bridge.appliedForces.erase(
                std::remove_if(bridge.appliedForces.begin(), bridge.appliedForces.end(),
                    [&](const FEM::TRUSS::ForceApplied& f) { return f.getApliedNode() == m_forceNodeId; }),
                bridge.appliedForces.end());
            bridge.appliedForces.emplace_back(m_forceNodeId, m_forceVector);
            m_appliedForces = bridge.appliedForces;
        }

        std::array<bool, 3> fixedDOFs = {false, false, false};
        const auto it = bridge.fixedDOFsByNode.find(m_forceNodeId);
        if (it != bridge.fixedDOFsByNode.end()) {
            fixedDOFs = it->second;
        }

        bool fixedX = fixedDOFs[0];
        bool fixedY = fixedDOFs[1];
        bool fixedZ = fixedDOFs[2];
        if (ImGui::Checkbox("Fix X##fix_x", &fixedX)) {
            bridge.fixedDOFsByNode[m_forceNodeId] = {fixedX, fixedY, fixedZ};
            for (auto& node : bridge.trussNodes) {
                if (node.getNodeID() == m_forceNodeId) {
                    node.setMovable({ !fixedX, !fixedY, !fixedZ });
                    break;
                }
            }
        }
        if (ImGui::Checkbox("Fix Y##fix_y", &fixedY)) {
            bridge.fixedDOFsByNode[m_forceNodeId] = {fixedX, fixedY, fixedZ};
            for (auto& node : bridge.trussNodes) {
                if (node.getNodeID() == m_forceNodeId) {
                    node.setMovable({ !fixedX, !fixedY, !fixedZ });
                    break;
                }
            }
        }
        if (ImGui::Checkbox("Fix Z##fix_z", &fixedZ)) {
            bridge.fixedDOFsByNode[m_forceNodeId] = {fixedX, fixedY, fixedZ};
            for (auto& node : bridge.trussNodes) {
                if (node.getNodeID() == m_forceNodeId) {
                    node.setMovable({ !fixedX, !fixedY, !fixedZ });
                    break;
                }
            }
        }

        if (ImGui::Button("Apply Fixity")) {
            if (bridge.selectedNodeId != std::numeric_limits<std::uint32_t>::max()) {
                m_forceNodeId = bridge.selectedNodeId;
            }
            bridge.fixedDOFsByNode[m_forceNodeId] = {fixedX, fixedY, fixedZ};

            for (auto& node : bridge.trussNodes) {
                if (node.getNodeID() == m_forceNodeId) {
                    node.setMovable({ !fixedX, !fixedY, !fixedZ });
                    break;
                }
            }
        }

        ImGui::Separator();
        ImGui::Text("Fixity log");
        if (bridge.fixedDOFsByNode.empty()) {
            ImGui::TextDisabled("No fixed DOFs yet.");
        } else {
            for (const auto& [nodeId, dofs] : bridge.fixedDOFsByNode) {
                ImGui::Text("Node %u: X=%s, Y=%s, Z=%s",
                    nodeId,
                    dofs[0] ? "fixed" : "free",
                    dofs[1] ? "fixed" : "free",
                    dofs[2] ? "fixed" : "free");
            }
        }

        if (!bridge.appliedForces.empty()) {
            ImGui::BeginChild("AppliedForceList", ImVec2(0, 110), true);
            for (const auto& force : bridge.appliedForces) {
                ImGui::Text("Node %u: Fx=%.3f, Fy=%.3f, Fz=%.3f",
                    force.getApliedNode(),
                    force.getForce()[0],
                    force.getForce()[1],
                    force.getForce()[2]);
            }
            ImGui::EndChild();
        }
    
        ImGui::SetNextItemWidth(160.0f);
        ImGui::InputDouble("Deformation Scale", &(bridge.deformScale), 0.0, 0.0, "%.3f");

        if (bridge.m_isRunning) {
            ImGui::ProgressBar(bridge.m_progress.load(), ImVec2(0.0f, 0.0f));
            ImGui::BeginDisabled();
            ImGui::Button("Calculating");
            ImGui::EndDisabled();
        }
        else if (ImGui::Button("Run Solver for Truss", ImVec2(-1, 32))) {
            bridge.m_isRunning = true;
            bridge.m_progress = 0.0f;

            const auto appliedForces = bridge.appliedForces.empty() ? std::vector<FEM::TRUSS::ForceApplied>{m_appliedForces.empty() ? FEM::TRUSS::ForceApplied{m_forceNodeId, m_forceVector} : m_appliedForces.front()} : bridge.appliedForces;
            m_appliedForces = appliedForces;
            bridge.workerThread = std::jthread(
                [&bridge,
                 cubeNumX = m_cubeNumX,
                 cubeNumY = m_cubeNumY,
                 cubeNumZ = m_cubeNumZ,
                 cubeEdgeLength = m_cubeEdgeLength,
                 crossSectionalArea = m_crossSectionalArea,
                 type = m_type,
                 appliedForces](std::stop_token st) mutable {
                    auto& allMaterials = bridge.allMaterials;

                    FEM::TRUSS::Truss_SQPT solver{
                        bridge,
                        st,
                        cubeNumX,
                        cubeNumY,
                        cubeNumZ,
                        cubeEdgeLength,
                        crossSectionalArea,
                        type
                    };

                    anaf::LOG::info(
                        "Calculating:\n\tcube_x: {}\n\tcube_y: {}\n\tcube_z: {}\n\tele_length(m): {}\n\tarea(cm^2): {}\n\ttype: {} ",
                        cubeNumX,
                        cubeNumY,
                        cubeNumZ,
                        cubeEdgeLength,
                        crossSectionalArea,
                        allMaterials[type].getMaterialType()
                    );

                    solver.trussCalculator_SQPT(bridge, st);
                    solver.trussSetForce_SQRT(bridge, st, appliedForces);
                    solver.setContainer(bridge, st);
                    solver.calculate(bridge, st, allMaterials);

                    {
                        std::lock_guard lock(bridge.dataMutex);
                        bridge.trussNodes.assign(solver.getNodes().begin(), solver.getNodes().end());
                        bridge.trussElements.assign(solver.getElements().begin(), solver.getElements().end());
                        
                        bridge.hasTrussPreview = true;
                        bridge.selectedNodeId = bridge.trussNodes.empty() ? std::numeric_limits<std::uint32_t>::max() : 0u;
                        bridge.dataVersion.fetch_add(1, std::memory_order_relaxed);
                    }

                    bridge.m_progress = 1.0f;
                    bridge.m_isRunning = false;
            });
        }

        if (ImGui::Button("Clear All", ImVec2(-1, 32))) {
            // stop worker thread if running
            if (bridge.workerThread.joinable()) {
                bridge.workerThread.request_stop();
            }

            // reset flags and progress
            bridge.m_isRunning = false;
            bridge.m_isGeneratingPreview = false;
            bridge.m_progress = 0.0f;

            // reset panel variables
            m_cubeNumX = 10;
            m_cubeNumY = 1;
            m_cubeNumZ = 10;
            m_type = 1;
            m_cubeEdgeLength = 1.0;
            m_crossSectionalArea = 80.0;
            m_forceNodeId = 126;
            m_forceVector = {0.0, 0.0, 0.0};
            m_appliedForces.clear();
            ensureDemoTrussCase(bridge, m_forceNodeId);

            // reset bridge data
            {
                std::lock_guard lock(bridge.dataMutex);

                bridge.trussNodes.clear();
                bridge.trussElements.clear();
                bridge.appliedForces.clear();
                bridge.fixedDOFsByNode.clear();
                bridge.hasTrussPreview = false;
                bridge.selectedNodeId = std::numeric_limits<std::uint32_t>::max();
                bridge.dataVersion.fetch_add(1, std::memory_order_relaxed);
            }
        }
        
        ImGui::End();
    }

} // namespace anaf::GUI end
