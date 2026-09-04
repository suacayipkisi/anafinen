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
#include "../../bridge/generalStatus.hpp"

#include "imgui.h"
#include "viewportRenderer.hpp"

#include <glm/ext/vector_float3.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <memory>
#include <unordered_map>

namespace anaf::GUI {

    ViewportPanel::ViewportPanel (
        std::shared_ptr<Framebuffer> fbo
    ) :
        m_fbo_(std::move(fbo)),
        m_renderer_(std::make_unique<ViewportRenderer>())
    {}

    void ViewportPanel::handleCameraInput() {
        ImGuiIO& io = ImGui::GetIO();

        // ctrl key press 
        if (ImGui::IsKeyPressed(ImGuiKey_LeftCtrl) || ImGui::IsKeyPressed(ImGuiKey_RightCtrl)) {
            m_showNodes = !m_showNodes;
        }

        // zoom
        if (m_viewportHovered_ && io.MouseWheel != 0.0f) {
            m_cameraDistance = std::clamp(m_cameraDistance * (1.0f - io.MouseWheel * 0.15f), 0.5f, 500.0f);
        }

        // drag start 
        if (m_viewportHovered_ && (ImGui::IsMouseClicked(ImGuiMouseButton_Right) || ImGui::IsMouseClicked(ImGuiMouseButton_Middle))) {
            m_draggingView = true;
        }

        // drag finish
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Right) && !ImGui::IsMouseDown(ImGuiMouseButton_Middle)) {
            m_draggingView = false;
        }

        // camera movement
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
        m_renderer_->clearLines();

        // coordinate axis x-y-z
        m_renderer_->addLine(glm::vec3(0.0f), glm::vec3(2.0f, 0.0f, 0.0f), glm::vec4(1.0f, 0.2f, 0.2f, 1.0f));
        m_renderer_->addLine(glm::vec3(0.0f), glm::vec3(0.0f, 2.0f, 0.0f), glm::vec4(0.2f, 1.0f, 0.2f, 1.0f));
        m_renderer_->addLine(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, 2.0f), glm::vec4(0.2f, 0.4f, 1.0f, 1.0f));

        auto& bridge = BRIDGE::buildBridge();
        std::lock_guard lock(bridge.dataMutex);

        if (!bridge.hasTrussPreview || bridge.trussNodes.empty()) return;

        double maxStress = 0.0;
        for (const auto& element : bridge.trussElements) {
            maxStress = std::max(maxStress, element.getEleStress());
        }

        auto stressColor = [&](double val) -> glm::vec4 {
            const double t = (maxStress > 0.0) ? std::clamp(val / maxStress, 0.0, 1.0) : 0.0;
            return glm::vec4(
                static_cast<float>(t),
                static_cast<float>(1.0 - std::abs(t - 0.5) * 2.0),
                static_cast<float>(1.0 - t),
                1.0f
            );
        };

        std::unordered_map<std::uint32_t, glm::vec3> nodePosMap;
        nodePosMap.reserve(bridge.trussNodes.size());
        for (const auto& node : bridge.trussNodes) {
            const auto& loc = node.getLocation();
            nodePosMap[node.getNodeID()] = glm::vec3(loc[0], loc[1], loc[2]);
        }

        // truss elements lines
        for (size_t i = 0; i < bridge.trussElements.size(); ++i) {
            const auto& element = bridge.trussElements[i];
            const auto& nodeIDs = element.getEleNodes();
            
            auto itA = nodePosMap.find(nodeIDs[0]);
            auto itB = nodePosMap.find(nodeIDs[1]);
            if (itA != nodePosMap.end() && itB != nodePosMap.end()) {
                glm::vec4 color = stressColor(element.getEleStress());
                m_renderer_->addLine(itA->second, itB->second, color);
            }
        }
    }

    void ViewportPanel::renderSceneOpenGL() {
        if (!m_fbo_) return;

        buildSceneBatches();

        m_fbo_->bind();
        glEnable(GL_DEPTH_TEST);
        glClearColor(0.08f, 0.09f, 0.11f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        const glm::mat4 mvp = getViewProjectionMatrix();
        m_renderer_->flush(mvp);

        m_fbo_->unbind();
    }

    void ViewportPanel::renderOverlay2D(const ImVec2& origin, const ImVec2& size, const glm::mat4& viewProj) {
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        auto& bridge = BRIDGE::buildBridge();

        // change 3D coordinates to 2D screen cooordinates
        auto projectWorldToScreen = [&](const glm::vec3& worldPos) -> std::pair<ImVec2, bool> {
            glm::vec4 clipPos = viewProj * glm::vec4(worldPos, 1.0f);
            if (clipPos.w <= 0.1f) {
                return {ImVec2(0.0f, 0.0f), false}; // dont draw point behind camera
            }

            // normalized device coordinates [-1, 1]
            glm::vec3 ndc = glm::vec3(clipPos) / clipPos.w;

            // screen pixels
            float screenX = origin.x + (ndc.x * 0.5f + 0.5f) * size.x;
            float screenY = origin.y + (-ndc.y * 0.5f + 0.5f) * size.y; // Y ekseni dikeyde ters
            return {ImVec2(screenX, screenY), true};
        };

        // axis names
        auto [axisX, visX] = projectWorldToScreen(glm::vec3(2.1f, 0.0f, 0.0f));
        auto [axisY, visY] = projectWorldToScreen(glm::vec3(0.0f, 2.1f, 0.0f));
        auto [axisZ, visZ] = projectWorldToScreen(glm::vec3(0.0f, 0.0f, 2.1f));

        if (visX) drawList->AddText(axisX, IM_COL32(255, 110, 110, 255), "X");
        if (visY) drawList->AddText(axisY, IM_COL32(110, 255, 140, 255), "Y");
        if (visZ) drawList->AddText(axisZ, IM_COL32(110, 160, 255, 255), "Z");

        std::lock_guard lock(bridge.dataMutex);
        if (!bridge.hasTrussPreview || bridge.trussNodes.empty()) {
            drawList->AddText(ImVec2(origin.x + 16.0f, origin.y + 16.0f), IM_COL32(180, 180, 180, 255), "No truss preview generated");
            return;
        }

        // max displacement
        double maxDisp = 0.0;
        for (const auto& node : bridge.trussNodes) {
            const auto disp = node.getDisplacmenet();
            maxDisp = std::max(maxDisp, std::sqrt(disp[0] * disp[0] + disp[1] * disp[1] + disp[2] * disp[2]));
        }

        auto displacementColor = [&](double magnitude) -> ImU32 {
            const double t = (maxDisp > 0.0) ? std::clamp(magnitude / maxDisp, 0.0, 1.0) : 0.0;
            const int r = static_cast<int>(255.0 * t);
            const int g = static_cast<int>(180.0 * (1.0 - std::abs(t - 0.5) * 2.0));
            const int b = static_cast<int>(255.0 * (1.0 - t));
            return IM_COL32(std::clamp(r, 0, 255), std::clamp(g, 0, 255), std::clamp(b, 0, 255), 255);
        };

        struct ProjectedNodeCache {
            ImVec2 screenPos;
            bool visible;
            ImU32 color;
        };

        std::unordered_map<std::uint32_t, ProjectedNodeCache> projectedNodes;
        projectedNodes.reserve(bridge.trussNodes.size());

        for (const auto& node : bridge.trussNodes) {
            const auto& loc = node.getLocation();
            const auto disp = node.getDisplacmenet();
            const double mag = std::sqrt(disp[0] * disp[0] + disp[1] * disp[1] + disp[2] * disp[2]);

            auto [sPos, visible] = projectWorldToScreen(glm::vec3(loc[0], loc[1], loc[2]));
            projectedNodes[node.getNodeID()] = {sPos, visible, displacementColor(mag)};
        }

        // draw nodes
        if (m_showNodes) {
            for (const auto& node : bridge.trussNodes) {
                const uint32_t id = node.getNodeID();
                const auto& proj = projectedNodes[id];
                if (!proj.visible) continue;

                const bool isSelected = (bridge.selectedNodeId == id);
                bool isFixed = false;
                auto fixIt = bridge.fixedDOFsByNode.find(id);
                if (fixIt != bridge.fixedDOFsByNode.end()) {
                    isFixed = fixIt->second[0] || fixIt->second[1] || fixIt->second[2];
                }

                const ImVec2 center = proj.screenPos;
                const float radius = 5.5f;

                // circle
                drawList->AddCircleFilled(center, radius, proj.color);
                drawList->AddCircle(center, radius + 1.5f, IM_COL32(20, 20, 20, 255), 0, 1.5f);

                // fixed nodes
                if (isFixed) {
                    drawList->AddRect(
                        ImVec2(center.x - radius - 4.0f, center.y - radius - 4.0f),
                        ImVec2(center.x + radius + 4.0f, center.y + radius + 4.0f),
                        IM_COL32(255, 100, 100, 255),
                        0.0f, 0, 2.0f
                    );
                }

                // choosen node
                if (isSelected) {
                    drawList->AddCircle(center, radius + 6.0f, IM_COL32(255, 180, 80, 255), 0, 2.0f);
                }

                // node ID names
                drawList->AddText(ImVec2(center.x + 8.0f, center.y - 8.0f), IM_COL32(230, 230, 230, 255), std::to_string(id).c_str());
            }
        }

        // applied forve arrow
        constexpr double arrowWorldLength = 1.5;
        constexpr float arrowHeadSize = 8.0f;
        const ImU32 forceArrowColor = IM_COL32(255, 60, 60, 255);

        for (const auto& force : bridge.appliedForces) {
            const auto nodeIt = projectedNodes.find(force.getApliedNode());
            if (nodeIt == projectedNodes.end() || !nodeIt->second.visible) continue;

            const auto forceVec = force.getForce();
            const double fMag = std::sqrt(forceVec[0] * forceVec[0] + forceVec[1] * forceVec[1] + forceVec[2] * forceVec[2]);
            if (fMag < 1e-6) continue;

            const double dirX = forceVec[0] / fMag;
            const double dirY = forceVec[1] / fMag;
            const double dirZ = forceVec[2] / fMag;

            const auto actualNodeIt = std::find_if(bridge.trussNodes.begin(), bridge.trussNodes.end(), [&](const auto& n) {
                return n.getNodeID() == force.getApliedNode();
            });
            if (actualNodeIt == bridge.trussNodes.end()) continue;

            const auto& loc = actualNodeIt->getLocation();
            const ImVec2 baseScreen = nodeIt->second.screenPos;
            auto [tipScreen, tipVis] = projectWorldToScreen(glm::vec3(
                loc[0] + dirX * arrowWorldLength,
                loc[1] + dirY * arrowWorldLength,
                loc[2] + dirZ * arrowWorldLength
            ));

            if (!tipVis) continue;

            drawList->AddLine(baseScreen, tipScreen, forceArrowColor, 3.0f);

            const float sDx = tipScreen.x - baseScreen.x;
            const float sDy = tipScreen.y - baseScreen.y;
            const float sLen = std::sqrt(sDx * sDx + sDy * sDy);

            if (sLen > 1.0f) {
                const float uX = sDx / sLen;
                const float uY = sDy / sLen;
                const float perpX = -uY;
                const float perpY = uX;

                const ImVec2 leftWing(
                    tipScreen.x - uX * arrowHeadSize + perpX * (arrowHeadSize * 0.5f),
                    tipScreen.y - uY * arrowHeadSize + perpY * (arrowHeadSize * 0.5f)
                );
                const ImVec2 rightWing(
                    tipScreen.x - uX * arrowHeadSize - perpX * (arrowHeadSize * 0.5f),
                    tipScreen.y - uY * arrowHeadSize - perpY * (arrowHeadSize * 0.5f)
                );

                drawList->AddLine(tipScreen, leftWing, forceArrowColor, 2.5f);
                drawList->AddLine(tipScreen, rightWing, forceArrowColor, 2.5f);
            }
        }

        // picking node
        if (m_viewportHovered_ && m_showNodes && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            const ImVec2 mousePos = ImGui::GetMousePos();
            std::uint32_t nearestNode = bridge.selectedNodeId;
            float nearestDistanceSq = 144.0f; // 12 pixel choose tolerence (12^2)

            for (const auto& [nodeId, projData] : projectedNodes) {
                if (!projData.visible) continue;
                const float dx = mousePos.x - projData.screenPos.x;
                const float dy = mousePos.y - projData.screenPos.y;
                const float distSq = dx * dx + dy * dy;

                if (distSq < nearestDistanceSq) {
                    nearestDistanceSq = distSq;
                    nearestNode = nodeId;
                }
            }

            bridge.selectedNodeId = nearestNode;
        }

        drawList->AddText(
            ImVec2(origin.x + 16.0f, origin.y + 16.0f),
            IM_COL32(180, 180, 180, 255),
            m_showNodes ? "Nodes: Visible \n(Press CTRL to hide)" : "Nodes: Hidden \n(Press CTRL to show)"
        );
    }

    void ViewportPanel::onImGuiRender() {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));

        ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse;
        if (m_draggingView) {
            flags |= ImGuiWindowFlags_NoMove;
        }

        ImGui::Begin("3D Simulation Viewport", &isOpen);

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

        

        // 64-bit cross-platform safe texture pointer cast
        const ImTextureID texId = static_cast<ImTextureID>(static_cast<uintptr_t>(m_fbo_->getTextureID()));
        
        // OpenGL texture coordinates (y axis is upside down: uv0=(0,1), uv1=(1,0))
        ImGui::Image(texId, availSize, ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f));

        m_viewportHovered_ = ImGui::IsWindowHovered();

        handleCameraInput();

        renderOverlay2D(origin, availSize, getViewProjectionMatrix());

        ImGui::End();
        ImGui::PopStyleVar();
    }

} // namespace anaf::GUI end
