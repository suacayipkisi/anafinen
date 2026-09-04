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

#include "mainDockSpaceHost.hpp"

#include <GLFW/glfw3.h>

#include "imgui.h"
#include "imgui_internal.h"

#include <functional>

namespace anaf::GUI {

    void MainDockSpaceHost::onImGuiRender() {
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->WorkPos);
        ImGui::SetNextWindowSize(viewport->WorkSize);
        ImGui::SetNextWindowViewport(viewport->ID);

        ImGuiWindowFlags host_flags = ImGuiWindowFlags_NoTitleBar | 
                                      ImGuiWindowFlags_NoCollapse | 
                                      ImGuiWindowFlags_NoResize | 
                                      ImGuiWindowFlags_NoMove | 
                                      ImGuiWindowFlags_NoBringToFrontOnFocus | 
                                      ImGuiWindowFlags_NoNavFocus | 
                                      ImGuiWindowFlags_NoBackground |
                                      ImGuiWindowFlags_MenuBar;

        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));

        ImGui::Begin("MainDockSpaceHostWindow", nullptr, host_flags);
        ImGui::PopStyleVar(3);

        if (ImGui::BeginMenuBar()) {
            if (ImGui::BeginMenu("File")) {
                if (ImGui::MenuItem("Import Mesh (.vtk / .obj)...")) {
                    if (on_import_mesh) on_import_mesh();
                }
                if (ImGui::MenuItem("Export Results (.vtk)...")) {
                    if (on_export_results) on_export_results();
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Exit", "Alt+F4")) {
                    glfwSetWindowShouldClose(m_window_, GLFW_TRUE);
                }
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Analyze")) {
                if (ImGui::MenuItem("Truss (1D Element)")) {
                    if (on_select_analyze_structure) {
                        on_select_analyze_structure(Truss_1D);
                    }
                }
                if (ImGui::MenuItem("Truss (3D Element)")) {
                    if (on_select_analyze_structure) {
                        on_select_analyze_structure(Truss_3D);
                    }
                }
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Solver")) {
                if (ImGui::MenuItem("Run Modal Analysis (Spectra)...")) {
                    if (on_run_solver) on_run_solver();
                }
                ImGui::EndMenu();
            }
            ImGui::EndMenuBar();
        }

        ImGuiID dockspace_id = ImGui::GetID("AppMainDockSpace");
        ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_PassthruCentralNode);

        // Layout initialization: only run when dimensions are valid AND it hasn't run yet
        static bool s_layout_built = false;
        if (!s_layout_built && viewport->WorkSize.x > 100.0f && viewport->WorkSize.y > 100.0f) {
            s_layout_built = true;

            ImGui::DockBuilderRemoveNode(dockspace_id);
            ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_DockSpace);
            ImGui::DockBuilderSetNodeSize(dockspace_id, viewport->WorkSize);

            ImGuiID dock_main_id = dockspace_id;

            // 1. Split Left (Full height)
            ImGuiID dock_left_id = ImGui::DockBuilderSplitNode(
                dock_main_id, ImGuiDir_Left, 0.22f, nullptr, &dock_main_id);

            // 2. Split Right (Full height)
            ImGuiID dock_right_id = ImGui::DockBuilderSplitNode(
                dock_main_id, ImGuiDir_Right, 0.24f, nullptr, &dock_main_id);

            // 3. Split Bottom from remaining center
            ImGuiID dock_bottom_id = ImGui::DockBuilderSplitNode(
                dock_main_id, ImGuiDir_Down, 0.25f, nullptr, &dock_main_id);

            // Dock windows into respective nodes
            ImGui::DockBuilderDockWindow("Truss(1D) Analysis Set", dock_left_id);
            ImGui::DockBuilderDockWindow("Model Tree", dock_right_id);
            ImGui::DockBuilderDockWindow("Console", dock_bottom_id);
            ImGui::DockBuilderDockWindow("3D Simulation Viewport", dock_main_id);

            ImGui::DockBuilderFinish(dockspace_id);
        }

        ImGui::End();
    }

} // namespace anaf::GUI end
