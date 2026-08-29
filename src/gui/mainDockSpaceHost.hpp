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