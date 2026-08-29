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

#pragma once

#include "iPanel.hpp"
#include "imgui.h"
#include <GLFW/glfw3.h>
#include <functional>

class MainDockSpaceHost : public IPanel {
private:
    GLFWwindow* m_window_;

public:
    std::function<void()> on_import_mesh;
    std::function<void()> on_export_results;
    std::function<void()> on_run_solver;

    MainDockSpaceHost(GLFWwindow* window) : m_window_(window) {}

    void onImGuiRender() override {
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

        ImGui::End();
    }
};