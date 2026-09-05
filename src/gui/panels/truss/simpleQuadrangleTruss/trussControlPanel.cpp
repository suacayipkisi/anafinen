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

#include <atomic>
#include <memory>
#include <mutex>
#include <stop_token>
#include <thread>

#include <truss_1D/trussEngine/trussSolver.hpp>
#include <bridge/generalStatus.hpp>

namespace anaf::GUI {

    namespace {
        void ensureDemoTrussCase(BRIDGE::Gui_Calc_Bridge& bridge, std::uint32_t forceNodeId) {
            bridge.fixedDOFsByNode.clear();
            bridge.fixedDOFsByNode[0u] = {true, true, true};
            bridge.fixedDOFsByNode[10u] = {true, true, true};
            bridge.fixedDOFsByNode[220u] = {true, true, true};
            bridge.fixedDOFsByNode[230u] = {true, true, true};

            bridge.selectedNodeId = forceNodeId;
            if (bridge.activeMesh) {
                auto updatedMesh = std::make_shared<BRIDGE::MeshData>(*bridge.activeMesh);
                updatedMesh->appliedForces.clear();
                updatedMesh->appliedForces.emplace_back(forceNodeId, std::array<double, 3>{0.0, 10000.0, 0.0});
                bridge.activeMesh = std::move(updatedMesh);
            }
        }
    }

    void TrussControlPanel::onImGuiRender() {
        BRIDGE::Gui_Calc_Bridge& bridge = BRIDGE::buildBridge();

        ImGui::Begin("Truss(1D) Analysis Set", &isOpen);

        ImGui::Text("Truss Parameters");
        ImGui::Separator();

        {
            std::lock_guard lock(bridge.dataMutex);
            if (bridge.fixedDOFsByNode.empty()) {
                ensureDemoTrussCase(bridge, m_forceNodeId);
            }
            if (bridge.selectedNodeId == std::numeric_limits<std::uint32_t>::max()) {
                bridge.selectedNodeId = m_forceNodeId;
            }
        }

        // if there is no force, push one force example
        if (m_appliedForces.empty()) {
            m_appliedForces.emplace_back(m_forceNodeId, std::array<double, 3>{0.0, 10000.0, 0.0});
        }

        std::uint32_t currentSelectedNode = std::numeric_limits<std::uint32_t>::max();
        {
            std::lock_guard lock(bridge.dataMutex);
            currentSelectedNode = bridge.selectedNodeId;
        }

        if (currentSelectedNode != std::numeric_limits<std::uint32_t>::max()) {
            ImGui::Text("Selected node: %u", currentSelectedNode);
        } else {
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
                 type = m_type,
                 appliedForces = m_appliedForces](std::stop_token st) mutable {
                    try {
                        FEM::TRUSS::SimpleTruss preview{{cubeNumX, cubeNumY, cubeNumZ}, cubeEdgeLength, crossSectionalArea, type};
                        preview.setTruss();

                        auto newMesh = std::make_shared<BRIDGE::MeshData>();
                        newMesh->trussNodes.assign(preview.getNodes().begin(), preview.getNodes().end());
                        newMesh->trussElements.assign(preview.getElements().begin(), preview.getElements().end());
                        newMesh->appliedForces = appliedForces;

                        {
                            std::lock_guard lock(bridge.dataMutex);
                            for (auto& node : newMesh->trussNodes) {
                                std::array<bool, 3> movable{true, true, true};
                                const auto it = bridge.fixedDOFsByNode.find(node.getNodeID());
                                if (it != bridge.fixedDOFsByNode.end()) {
                                    movable = { !it->second[0], !it->second[1], !it->second[2] };
                                }
                                node.setMovable(movable);
                            }
                            if (bridge.activeMesh) {
                                newMesh->deformScale = bridge.activeMesh->deformScale;
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

        if (ImGui::BeginTable("TrussParamsTable", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_BordersInnerH)) {
            ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthStretch, 0.6f);
            ImGui::TableSetupColumn("Control", ImGuiTableColumnFlags_WidthStretch, 0.4f);

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

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::AlignTextToFramePadding();
            ImGui::Text("Material Type");
            ImGui::TableSetColumnIndex(1);
            ImGui::SetNextItemWidth(-FLT_MIN);
            ImGui::InputScalar("##mat_type", ImGuiDataType_U32, &m_type);

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
            ImGui::InputDouble("##cross_area", &m_crossSectionalArea, 0.0, 0.0, "%.2f");

            ImGui::EndTable();
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
                    [&](const FEM::TRUSS::ForceApplied& f) { return f.getApliedNode() == m_forceNodeId; }),
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

        bool fixedX = fixedDOFs[0];
        bool fixedY = fixedDOFs[1];
        bool fixedZ = fixedDOFs[2];
        bool fixChanged = false;

        if (ImGui::Checkbox("Fix X##fix_x", &fixedX)) fixChanged = true;
        if (ImGui::Checkbox("Fix Y##fix_y", &fixedY)) fixChanged = true;
        if (ImGui::Checkbox("Fix Z##fix_z", &fixedZ)) fixChanged = true;

        if (fixChanged || ImGui::Button("Apply Fixity")) {
            std::lock_guard lock(bridge.dataMutex);
            bridge.fixedDOFsByNode[m_forceNodeId] = {fixedX, fixedY, fixedZ};
        }

        ImGui::Separator();
        ImGui::Text("Fixity log");
        {
            std::lock_guard lock(bridge.dataMutex);
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
        }

        if (!m_appliedForces.empty()) {
            ImGui::BeginChild("AppliedForceList", ImVec2(0, 110), true);
            for (const auto& force : m_appliedForces) {
                ImGui::Text("Node %u: Fx=%.3f, Fy=%.3f, Fz=%.3f",
                    force.getApliedNode(),
                    force.getForce()[0],
                    force.getForce()[1],
                    force.getForce()[2]);
            }
            ImGui::EndChild();
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
        else if (ImGui::Button("Run Solver for Truss", ImVec2(-1, 32))) {
            bridge.m_isRunning = true;
            bridge.m_progress = 0.0f;

            if (m_appliedForces.empty()) {
                m_appliedForces.emplace_back(m_forceNodeId, m_forceVector);
            }

            std::vector<FEM::TRUSS::ForceApplied> forcesToApply = m_appliedForces;
            double deformScale = currentScale;

            bridge.workerThread = std::jthread(
                [&bridge,
                 cubeNumX = m_cubeNumX,
                 cubeNumY = m_cubeNumY,
                 cubeNumZ = m_cubeNumZ,
                 cubeEdgeLength = m_cubeEdgeLength,
                 crossSectionalArea = m_crossSectionalArea,
                 type = m_type,
                 appliedForces = m_appliedForces,
                 deformScale = currentScale, forcesToApply](std::stop_token st) mutable {
                    try {
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
                        
                        solver.trussCalculator_SQPT(bridge, st);
                        solver.trussSetForce_SQRT(bridge, st, forcesToApply);
                        solver.setContainer(bridge, st);
                        
                        solver.calculate(bridge, st, allMaterials);

                        // send solved nodes and elements into new snapshot
                        auto newMesh = std::make_shared<BRIDGE::MeshData>();
                        newMesh->trussNodes.assign(solver.getNodes().begin(), solver.getNodes().end());
                        newMesh->trussElements.assign(solver.getElements().begin(), solver.getElements().end());
                        newMesh->appliedForces = forcesToApply;
                        newMesh->deformScale = deformScale;

                        {
                            std::lock_guard lock(bridge.dataMutex);

                            // apply boundary conditions into nodes again for overlay draw
                            for (auto& node : newMesh->trussNodes) {
                                std::array<bool, 3> movable{true, true, true};
                                const auto it = bridge.fixedDOFsByNode.find(node.getNodeID());
                                if (it != bridge.fixedDOFsByNode.end()) {
                                    movable = { !it->second[0], !it->second[1], !it->second[2] };
                                }
                                node.setMovable(movable);
                            }

                            bridge.activeMesh = std::move(newMesh);
                            bridge.hasTrussPreview = true;
                        }

                        // send signal to gui to draw scene
                        bridge.dataVersion.fetch_add(1, std::memory_order_release);

                    } catch (const std::exception& e) {
                        anaf::LOG::error("Solver failed: {}", e.what());
                    }

                    bridge.m_progress = 1.0f;
                    bridge.m_isRunning = false;
            });
        }

        if (ImGui::Button("Clear All", ImVec2(-1, 32))) {
            if (bridge.workerThread.joinable()) {
                bridge.workerThread.request_stop();
            }

            bridge.m_isRunning = false;
            bridge.m_isGeneratingPreview = false;
            bridge.m_progress = 0.0f;

            m_cubeNumX = 10;
            m_cubeNumY = 1;
            m_cubeNumZ = 10;
            m_type = 1;
            m_cubeEdgeLength = 1.0;
            m_crossSectionalArea = 80.0;
            m_forceNodeId = 126;
            m_forceVector = {0.0, 0.0, 0.0};
            m_appliedForces.clear();

            {
                std::lock_guard lock(bridge.dataMutex);
                bridge.activeMesh = nullptr;
                bridge.fixedDOFsByNode.clear();
                bridge.hasTrussPreview = false;
                bridge.selectedNodeId = std::numeric_limits<std::uint32_t>::max();
                ensureDemoTrussCase(bridge, m_forceNodeId);
            }
            bridge.dataVersion.fetch_add(1, std::memory_order_release);
        }
        
        ImGui::End();
    }

} // namespace anaf::GUI end
