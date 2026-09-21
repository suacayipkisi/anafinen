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

#include "viewportPanel.hpp"
#include <atomic>
#include <bridge/generalStatus.hpp>

#include "imgui.h"
#include "viewportRenderer.hpp"

#include <glm/ext/vector_float3.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <memory>
#include <mutex>
#include <unordered_map>

namespace anaf::GUI {

    ViewportPanel::ViewportPanel(std::shared_ptr<Framebuffer> fbo) :
        m_fbo_(std::move(fbo)),
        m_renderer_(std::make_unique<ViewportRenderer>())
    {}

    void ViewportPanel::handleCameraInput() {
        ImGuiIO& io = ImGui::GetIO();

        if (ImGui::IsKeyPressed(ImGuiKey_LeftCtrl) || ImGui::IsKeyPressed(ImGuiKey_RightCtrl)) {
            m_showNodes = !m_showNodes;
            truss_1d_gui_prop.m_meshNeedsUpdate = true;
        }

        if (m_viewportHovered_ && io.MouseWheel != 0.0f) {
            m_cameraDistance = std::clamp(m_cameraDistance * (1.0f - io.MouseWheel * 0.15f), 0.5f, 500.0f);
        }

        if (m_viewportHovered_ && (ImGui::IsMouseClicked(ImGuiMouseButton_Right) || ImGui::IsMouseClicked(ImGuiMouseButton_Middle))) {
            m_draggingView = true;
        }

        if (!ImGui::IsMouseDown(ImGuiMouseButton_Right) && !ImGui::IsMouseDown(ImGuiMouseButton_Middle)) {
            m_draggingView = false;
        }

        if (m_draggingView) {
            const ImVec2 delta = io.MouseDelta;

            if (delta.x != 0.0f || delta.y != 0.0f) {
                constexpr float pitchLimit = 1.553343f;

                const glm::vec3 forward = glm::normalize(m_target - glm::vec3(
                    m_target.x + std::sin(m_rotationYaw) * std::cos(m_rotationPitch) * m_cameraDistance,
                    m_target.y - std::sin(m_rotationPitch) * m_cameraDistance,
                    m_target.z + std::cos(m_rotationYaw) * std::cos(m_rotationPitch) * m_cameraDistance
                ));

                glm::vec3 worldUp(0.0f, 1.0f, 0.0f);
                if (std::abs(forward.y) > 0.99f) worldUp = glm::vec3(1.0f, 0.0f, 0.0f);

                const glm::vec3 right = glm::normalize(glm::cross(worldUp, forward));
                const glm::vec3 up = glm::cross(forward, right);

                const bool panMode = ImGui::IsMouseDown(ImGuiMouseButton_Middle) || io.KeyShift;
                if (panMode) {
                    const float panScale = 0.0015f * m_cameraDistance;
                    m_target += (right * delta.x + up * delta.y) * panScale;
                } else {
                    m_rotationYaw -= delta.x * 0.005f;
                    m_rotationPitch -= delta.y * 0.005f;
                    m_rotationPitch = std::clamp(m_rotationPitch, -pitchLimit, pitchLimit);
                }
            }
        }
    }

    glm::mat4 ViewportPanel::getViewProjectionMatrix() const {
        const glm::vec3 eye = m_target + glm::vec3(
            std::sin(m_rotationYaw) * std::cos(m_rotationPitch) * m_cameraDistance,
            -std::sin(m_rotationPitch) * m_cameraDistance,
            std::cos(m_rotationYaw) * std::cos(m_rotationPitch) * m_cameraDistance
        );

        const glm::mat4 view = glm::lookAt(eye, m_target, glm::vec3(0.0f, 1.0f, 0.0f));
        const float aspect = (m_viewportSize.y > 0.0f) ? (m_viewportSize.x / m_viewportSize.y) : 16.0f / 9.0f;
        const glm::mat4 projection = glm::perspective(glm::radians(45.0f), aspect, 0.1f, 1000.0f);

        return projection * view;
    }

    void ViewportPanel::buildSceneBatches() {
        m_renderer_->clearBuffers();

        // Coordinate axes X, Y, Z (EntityID = -1)
        m_renderer_->addLine(glm::vec3(0.0f), glm::vec3(2.0f, 0.0f, 0.0f), glm::vec4(1.0f, 0.2f, 0.2f, 1.0f), -1);
        m_renderer_->addLine(glm::vec3(0.0f), glm::vec3(0.0f, 2.0f, 0.0f), glm::vec4(0.2f, 1.0f, 0.2f, 1.0f), -1);
        m_renderer_->addLine(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, 2.0f), glm::vec4(0.2f, 0.4f, 1.0f, 1.0f), -1);

        if (!m_currentMesh || m_currentMesh->trussNodes.empty()) {
            m_renderer_->uploadCurrentBuffer();
            return;
        }

        const auto& mesh = *m_currentMesh;
        auto& bridge = BRIDGE::buildBridge();

        std::unordered_map<std::uint32_t, std::array<bool, 3>> fixedDOFs;
        std::uint32_t selectedId = std::numeric_limits<std::uint32_t>::max();
        {
            std::lock_guard<std::mutex> lock(bridge.dataMutex);
            fixedDOFs = bridge.fixedDOFsByNode;
            selectedId = bridge.selectedNodeId;
        }

        m_renderer_->reserve(3 + mesh.trussElements.size() + mesh.appliedForces.size() * 3, mesh.trussNodes.size());

        double maxStress = 0.0;
        for (const auto& element : mesh.trussElements) {
            maxStress = std::max(maxStress, std::abs(element.getEleStress()));
        }

        const double deformScale = mesh.deformScale;

        auto stressColor = [&](double val) -> glm::vec4 {
            if (maxStress <= 1e-9) {
                return glm::vec4(0.4f, 0.6f, 0.85f, 1.0f);
            }
            const float t = static_cast<float>(std::sqrt(std::clamp(std::abs(val) / maxStress, 0.0, 1.0)));
            float r = std::clamp(1.5f - std::abs(4.0f * t - 3.0f), 0.0f, 1.0f);
            float g = std::clamp(1.5f - std::abs(4.0f * t - 2.0f), 0.0f, 1.0f);
            float b = std::clamp(1.5f - std::abs(4.0f * t - 1.0f), 0.0f, 1.0f);
            return glm::vec4(r, g, b, 1.0f);
        };

        uint32_t maxNodeId = 0;
        double maxDisp = 0.0;
        for (const auto& node : mesh.trussNodes) {
            maxNodeId = std::max(maxNodeId, node.getNodeID());
            const auto disp = node.getDisplacmenet();
            maxDisp = std::max(maxDisp, std::sqrt(disp[0] * disp[0] + disp[1] * disp[1] + disp[2] * disp[2]));
        }

        std::vector<glm::vec3> nodeLookup(maxNodeId + 1, glm::vec3(0.0f));
        for (const auto& node : mesh.trussNodes) {
            const auto& loc = node.getLocation();
            const auto disp = node.getDisplacmenet();

            nodeLookup[node.getNodeID()] = glm::vec3(
                loc[0] + disp[0] * deformScale,
                loc[1] + disp[1] * deformScale,
                loc[2] + disp[2] * deformScale
            );
        }

        // Truss Elements (Lines)
        for (const auto& element : mesh.trussElements) {
            const auto& nodeIDs = element.getEleNodes();
            if (nodeIDs[0] <= maxNodeId && nodeIDs[1] <= maxNodeId) {
                glm::vec4 color = stressColor(element.getEleStress());
                m_renderer_->addLine(nodeLookup[nodeIDs[0]], nodeLookup[nodeIDs[1]], color, -1);
            }
        }

        // Interactive Nodes (Points in FBO)
        if (m_showNodes) {
            auto displacementColor = [&](double magnitude) -> glm::vec4 {
                const double t = (maxDisp > 0.0) ? std::clamp(magnitude / maxDisp, 0.0, 1.0) : 0.0;
                float r = static_cast<float>(t);
                float g = static_cast<float>(1.0 - std::abs(t - 0.5) * 2.0);
                float b = static_cast<float>(1.0 - t);
                return glm::vec4(r, g, b, 1.0f);
            };

            for (const auto& node : mesh.trussNodes) {
                const uint32_t id = node.getNodeID();
                const glm::vec3& pos = nodeLookup[id];

                const auto disp = node.getDisplacmenet();
                const double mag = std::sqrt(disp[0] * disp[0] + disp[1] * disp[1] + disp[2] * disp[2]);

                glm::vec4 pColor = displacementColor(mag);
                float pSize = 12.0f;

                if (id == selectedId) {
                    pColor = glm::vec4(1.0f, 0.7f, 0.2f, 1.0f);
                    pSize = 18.0f;
                } else if (fixedDOFs.find(id) != fixedDOFs.end()) {
                    pColor = glm::vec4(1.0f, 0.3f, 0.3f, 1.0f);
                }

                m_renderer_->addPoint(pos, pColor, static_cast<int>(id), pSize);
            }
        }

        // Force Arrows (Lines in FBO)
        constexpr float arrowWorldLength = 1.5f;
        constexpr float headLength = 0.35f;
        constexpr float headRadius = 0.15f;
        const glm::vec4 forceArrowColor(1.0f, 0.25f, 0.25f, 1.0f);

        for (const auto& force : mesh.appliedForces) {
            const uint32_t targetId = force.getApliedNode();
            if (targetId > maxNodeId) continue;

            const auto forceVec = force.getForce();
            const double fMag = std::sqrt(forceVec[0] * forceVec[0] + forceVec[1] * forceVec[1] + forceVec[2] * forceVec[2]);
            if (fMag < 1e-6) continue;

            const glm::vec3 dir = glm::normalize(glm::vec3(forceVec[0], forceVec[1], forceVec[2]));
            const glm::vec3 basePos = nodeLookup[targetId];
            const glm::vec3 tipPos = basePos + dir * arrowWorldLength;

            m_renderer_->addLine(basePos, tipPos, forceArrowColor, -1);

            glm::vec3 arbitraryUp = (std::abs(dir.y) > 0.9f) ? glm::vec3(1.0f, 0.0f, 0.0f) : glm::vec3(0.0f, 1.0f, 0.0f);
            glm::vec3 side1 = glm::normalize(glm::cross(dir, arbitraryUp)) * headRadius;
            glm::vec3 side2 = glm::normalize(glm::cross(dir, side1)) * headRadius;
            glm::vec3 headBase = tipPos - dir * headLength;

            m_renderer_->addLine(tipPos, headBase + side1, forceArrowColor, -1);
            m_renderer_->addLine(tipPos, headBase - side1, forceArrowColor, -1);
            m_renderer_->addLine(tipPos, headBase + side2, forceArrowColor, -1);
            m_renderer_->addLine(tipPos, headBase - side2, forceArrowColor, -1);
        }

        m_renderer_->uploadCurrentBuffer();
    }

    void ViewportPanel::renderSceneOpenGL() {
        if (!m_fbo_) return;

        auto& bridge = BRIDGE::buildBridge();
        const uint64_t currentVersion = bridge.dataVersion.load(std::memory_order_acquire);

        if (truss_1d_gui_prop.m_meshNeedsUpdate || currentVersion != truss_1d_gui_prop.m_lastRenderedVersion) {
            {
                std::lock_guard<std::mutex> lock(bridge.dataMutex);
                m_currentMesh = bridge.activeMesh;
            }

            truss_1d_gui_prop.m_meshNeedsUpdate = false;
            truss_1d_gui_prop.m_lastRenderedVersion = currentVersion;
            buildSceneBatches();
        }

        m_fbo_->bind();
        glEnable(GL_DEPTH_TEST);

        // Clear Color Attachment 0 (Scene Image)
        glClearColor(0.08f, 0.09f, 0.11f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        // Clear Color Attachment 1 (Entity ID Buffer to -1)
        const int clearEntityID = -1;
        glClearBufferiv(GL_COLOR, 1, &clearEntityID);

        const glm::mat4 mvp = getViewProjectionMatrix();
        m_renderer_->render(mvp);

        m_fbo_->unbind();
    }

    void ViewportPanel::renderOverlay2D(const ImVec2& origin, const ImVec2& size, const glm::mat4& viewProj) {
        //auto& bridge = BRIDGE::buildBridge();

        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const auto currentMesh = m_currentMesh;

        auto projectWorldToScreen = [&](const glm::vec3& worldPos) -> std::pair<ImVec2, bool> {
            glm::vec4 clipPos = viewProj * glm::vec4(worldPos, 1.0f);
            if (clipPos.w <= 0.1f) return {ImVec2(0.0f, 0.0f), false};

            glm::vec3 ndc = glm::vec3(clipPos) / clipPos.w;
            float screenX = origin.x + (ndc.x * 0.5f + 0.5f) * size.x;
            float screenY = origin.y + (-ndc.y * 0.5f + 0.5f) * size.y;
            return {ImVec2(screenX, screenY), true};
        };

        // Coordinate Labels
        auto [axisX, visX] = projectWorldToScreen(glm::vec3(2.1f, 0.0f, 0.0f));
        auto [axisY, visY] = projectWorldToScreen(glm::vec3(0.0f, 2.1f, 0.0f));
        auto [axisZ, visZ] = projectWorldToScreen(glm::vec3(0.0f, 0.0f, 2.1f));

        if (visX) drawList->AddText(axisX, IM_COL32(255, 110, 110, 255), "X");
        if (visY) drawList->AddText(axisY, IM_COL32(110, 255, 140, 255), "Y");
        if (visZ) drawList->AddText(axisZ, IM_COL32(110, 160, 255, 255), "Z");

        // Node ID Text Rendering (Only when zoomed in or selected)
        if (m_showNodes && currentMesh && !currentMesh->trussNodes.empty()) {
            auto& bridge = BRIDGE::buildBridge();
            std::uint32_t selectedId = std::numeric_limits<std::uint32_t>::max();
            {
                std::lock_guard<std::mutex> lock(bridge.dataMutex);
                selectedId = bridge.selectedNodeId;
            }

            const double deformScale = currentMesh->deformScale;
            for (const auto& node : currentMesh->trussNodes) {
                const uint32_t id = node.getNodeID();
                const bool isSelected = (selectedId == id);

                if (m_cameraDistance < 15.0f || isSelected) {
                    const auto& loc = node.getLocation();
                    const auto disp = node.getDisplacmenet();
                    glm::vec3 deformedPos(
                        loc[0] + disp[0] * deformScale,
                        loc[1] + disp[1] * deformScale,
                        loc[2] + disp[2] * deformScale
                    );
                    auto [sPos, visible] = projectWorldToScreen(deformedPos);
                    if (visible) {
                        drawList->AddText(ImVec2(sPos.x + 8.0f, sPos.y - 8.0f), IM_COL32(230, 230, 230, 255), std::to_string(id).c_str());
                    }
                }
            }
        }

        // Status Text
        drawList->AddText(
            ImVec2(origin.x + 16.0f, origin.y + 16.0f),
            IM_COL32(180, 180, 180, 255),
            m_showNodes ? "Nodes: Visible (Press CTRL to hide)" : "Nodes: Hidden (Press CTRL to show)"
        );

        // FPS Monitor
        {
            const float fps = ImGui::GetIO().Framerate;
            const float ms = 1000.0f / (fps > 0.0f ? fps : 1.0f);

            char fpsBuffer[64];
            std::snprintf(fpsBuffer, sizeof(fpsBuffer), "%.1f FPS (%.2f ms)", fps, ms);

            const ImVec2 textSize = ImGui::CalcTextSize(fpsBuffer);
            const ImVec2 textPos(origin.x + size.x - textSize.x - 16.0f, origin.y + 16.0f);

            drawList->AddRectFilled(ImVec2(textPos.x - 6.0f, textPos.y - 4.0f), ImVec2(textPos.x + textSize.x + 6.0f, textPos.y + textSize.y + 4.0f), IM_COL32(15, 17, 22, 220), 4.0f);
            drawList->AddText(textPos, (fps < 30.0f) ? IM_COL32(255, 90, 90, 255) : IM_COL32(100, 255, 120, 255), fpsBuffer);
        }

        // Colorbars
        if (currentMesh && !currentMesh->trussNodes.empty()) {
            const auto& mesh = *currentMesh;
            constexpr float barWidth = 10.0f;
            constexpr float barHeight = 180.0f;
            constexpr int colorSteps = 30;

            auto getJetColor = [](float t) -> ImU32 {
                float r = std::clamp(1.5f - std::abs(4.0f * t - 3.0f), 0.0f, 1.0f);
                float g = std::clamp(1.5f - std::abs(4.0f * t - 2.0f), 0.0f, 1.0f);
                float b = std::clamp(1.5f - std::abs(4.0f * t - 1.0f), 0.0f, 1.0f);
                return IM_COL32(static_cast<int>(r * 255.0f), static_cast<int>(g * 255.0f), static_cast<int>(b * 255.0f), 255);
            };

            // Stress Bar
            double maxStress = 0.0;
            double maxDisp = 0.0;
            for (const auto& el : mesh.trussElements) {
                maxStress = std::max(maxStress, std::abs(el.getEleStress()));
            }
            for (const auto& nod : mesh.trussNodes) {
                const auto& disp = nod.getDisplacmenet();
                const double dispLength = std::sqrt(disp[0] * disp[0] + disp[1] * disp[1] + disp[2] * disp[2]);
                maxDisp = std::max(maxDisp, dispLength);
            }

            const float startX = origin.x + 20.0f;
            const float startY = origin.y + size.y - barHeight - 25.0f;
            const float startYDisp = startY - 220.0f;

            // draw stress legend
            drawList->AddRectFilled(
                ImVec2(startX - 8.0f, startY - 24.0f), 
                ImVec2(startX + barWidth + 80.0f, startY + barHeight + 14.0f), 
                IM_COL32(15, 17, 22, 220), 
                4.0f
            );
            drawList->AddText(ImVec2(startX, startY - 20.0f), IM_COL32(230, 230, 230, 255), "Stress (MPa)");

            const float stepHeight = barHeight / static_cast<float>(colorSteps);
            for (int i = 0; i < colorSteps; ++i) {
                const float tTop = 1.0f - static_cast<float>(i) / static_cast<float>(colorSteps);
                const float tBottom = 1.0f - static_cast<float>(i + 1) / static_cast<float>(colorSteps);
                drawList->AddRectFilledMultiColor(
                    ImVec2(startX, startY + i * stepHeight),
                    ImVec2(startX + barWidth, startY + (i + 1) * stepHeight),
                    getJetColor(tTop), getJetColor(tTop), getJetColor(tBottom), getJetColor(tBottom)
                );
            }
            drawList->AddRect(ImVec2(startX, startY), ImVec2(startX + barWidth, startY + barHeight), IM_COL32(200, 200, 200, 180));

            char txtMax[32], txtMid[32], txtMin[32];
            std::snprintf(txtMax, sizeof(txtMax), "%.2e", maxStress / 1000.0);
            std::snprintf(txtMid, sizeof(txtMid), "%.2e", maxStress * 0.5 / 1000.0);
            std::snprintf(txtMin, sizeof(txtMin), "%.2e", 0.0);

            drawList->AddText(ImVec2(startX + barWidth + 6.0f, startY - 2.0f), IM_COL32(230, 230, 230, 255), txtMax);
            drawList->AddText(ImVec2(startX + barWidth + 6.0f, startY + barHeight * 0.5f - 6.0f), IM_COL32(200, 200, 200, 255), txtMid);
            drawList->AddText(ImVec2(startX + barWidth + 6.0f, startY + barHeight - 10.0f), IM_COL32(230, 230, 230, 255), txtMin);

            // draw disp legend
            if(m_showNodes) {
                    drawList->AddRectFilled(
                    ImVec2(startX - 8.0f, startYDisp - 24.0f), 
                    ImVec2(startX + barWidth + 80.0f, startYDisp + barHeight + 14.0f), 
                    IM_COL32(15, 17, 22, 220),
                    4.0f
                );
                drawList->AddText(ImVec2(startX, startYDisp - 20.0f), IM_COL32(230, 230, 230, 255), "Disp (mm)");

                const float stepHeightDisp = barHeight / static_cast<float>(colorSteps);
                for (int i = 0; i < colorSteps; ++i) {
                    const float tTop = 1.0f - static_cast<float>(i) / static_cast<float>(colorSteps);
                    const float tBottom = 1.0f - static_cast<float>(i + 1) / static_cast<float>(colorSteps);
                    drawList->AddRectFilledMultiColor(
                        ImVec2(startX, startYDisp + i * stepHeightDisp),
                        ImVec2(startX + barWidth, startYDisp + (i + 1) * stepHeightDisp),
                        getJetColor(tTop), getJetColor(tTop), getJetColor(tBottom), getJetColor(tBottom)
                    );
                }
                drawList->AddRect(ImVec2(startX, startYDisp), ImVec2(startX + barWidth, startYDisp + barHeight), IM_COL32(200, 200, 200, 180));

                char txtMaxDisp[32], txtMidDisp[32], txtMinDisp[32];
                std::snprintf(txtMaxDisp, sizeof(txtMaxDisp), "%.2e", maxDisp * 1000.0);
                std::snprintf(txtMidDisp, sizeof(txtMidDisp), "%.2e", maxDisp * 0.5 * 1000.0);
                std::snprintf(txtMinDisp, sizeof(txtMinDisp), "%.2e", 0.0);

                drawList->AddText(ImVec2(startX + barWidth + 6.0f, startYDisp - 2.0f), IM_COL32(230, 230, 230, 255), txtMaxDisp);
                drawList->AddText(ImVec2(startX + barWidth + 6.0f, startYDisp + barHeight * 0.5f - 6.0f), IM_COL32(200, 200, 200, 255), txtMidDisp);
                drawList->AddText(ImVec2(startX + barWidth + 6.0f, startYDisp + barHeight - 10.0f), IM_COL32(230, 230, 230, 255), txtMinDisp);
            }
        }

    }

    void ViewportPanel::onImGuiRender() {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));

        ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse;
        if (m_draggingView) {
            flags |= ImGuiWindowFlags_NoMove;
        }

        ImGui::Begin("3D Simulation Viewport", &isOpen, flags);

        m_viewportFocused_ = ImGui::IsWindowFocused();

        const ImVec2 availSize = ImGui::GetContentRegionAvail();
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        m_viewportSize = availSize;

        if (availSize.x > 0.0f && availSize.y > 0.0f) {
            const auto w = static_cast<std::uint32_t>(availSize.x);
            const auto h = static_cast<std::uint32_t>(availSize.y);
            if (m_fbo_->getWidth() != w || m_fbo_->getHeight() != h) {
                m_fbo_->resize(w, h);
            }
        }

        const ImTextureID texId = static_cast<ImTextureID>(static_cast<uintptr_t>(m_fbo_->getTextureID()));
        ImGui::Image(texId, availSize, ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f));

        m_viewportHovered_ = ImGui::IsItemHovered();

        // GPU Pixel Picking Interaction
        if (m_viewportHovered_ && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            const ImVec2 mousePos = ImGui::GetMousePos();
            const int mouseX = static_cast<int>(mousePos.x - origin.x);
            const int mouseY = static_cast<int>(m_viewportSize.y - (mousePos.y - origin.y)); // Invert Y for OpenGL

            const int pickedID = m_fbo_->readPixel(1, mouseX, mouseY);

            auto& bridge = BRIDGE::buildBridge();
            std::lock_guard<std::mutex> lock(bridge.dataMutex);
            if (pickedID >= 0) {
                bridge.selectedNodeId = static_cast<std::uint32_t>(pickedID);
            } else {
                bridge.selectedNodeId = std::numeric_limits<std::uint32_t>::max();
            }
            truss_1d_gui_prop.m_meshNeedsUpdate = true;
        }

        handleCameraInput();
        renderOverlay2D(origin, availSize, getViewProjectionMatrix());

        ImGui::End();
        ImGui::PopStyleVar();
    }

} // namespace anaf::GUI end
