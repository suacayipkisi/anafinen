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

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <unordered_map>
#include <vector>
#include <string>

namespace anaf::GUI {

    void ViewportPanel::onImGuiRender() {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::Begin("3D Simulation Viewport", &isOpen);

        m_viewportFocused_ = ImGui::IsWindowFocused();
        m_viewportHovered_ = ImGui::IsWindowHovered();

        const ImVec2 panelSize = ImGui::GetContentRegionAvail();
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        const ImVec2 rectMax = ImVec2(origin.x + panelSize.x, origin.y + panelSize.y);

        auto& bridge = BRIDGE::buildBridge();
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        drawList->AddRectFilled(origin, rectMax, IM_COL32(20, 23, 30, 255));

        if (ImGui::IsKeyPressed(ImGuiKey_LeftCtrl) || ImGui::IsKeyPressed(ImGuiKey_RightCtrl)) {
            m_showNodes = !m_showNodes;
        }

        if (m_viewportHovered_ && (ImGui::IsMouseClicked(ImGuiMouseButton_Right) || ImGui::IsMouseClicked(ImGuiMouseButton_Middle))) {
            m_draggingView = true;
            m_lastMousePos = ImGui::GetMousePos();
        }
        if (m_draggingView && !ImGui::IsMouseDown(ImGuiMouseButton_Right) && !ImGui::IsMouseDown(ImGuiMouseButton_Middle)) {
            m_draggingView = false;
        }

        const float wheel = ImGui::GetIO().MouseWheel;
        if (m_viewportHovered_ && wheel != 0.0f) {
            m_cameraDistance = std::clamp(m_cameraDistance * (1.0f - wheel * 0.15f), 1.0f, 250.0f);
        }

        // ~89 degree in radius scale
        constexpr float pitchLimit = 1.553343f;
        m_rotationPitch = std::clamp(m_rotationPitch, -pitchLimit, pitchLimit);

        const double sinYaw = std::sin(m_rotationYaw);
        const double cosYaw = std::cos(m_rotationYaw);
        const double sinPitch = std::sin(m_rotationPitch);
        const double cosPitch = std::cos(m_rotationPitch);

        const double cameraX = m_targetX + sinYaw * cosPitch * m_cameraDistance;
        const double cameraY = m_targetY - sinPitch * m_cameraDistance;
        const double cameraZ = m_targetZ + cosYaw * cosPitch * m_cameraDistance;

        const double forwardX = m_targetX - cameraX;
        const double forwardY = m_targetY - cameraY;
        const double forwardZ = m_targetZ - cameraZ;
        const double forwardLen = std::sqrt(forwardX * forwardX + forwardY * forwardY + forwardZ * forwardZ);
        const double fx = forwardX / forwardLen;
        const double fy = forwardY / forwardLen;
        const double fz = forwardZ / forwardLen;

        double worldUpX = 0.0;
        double worldUpY = 1.0;
        double worldUpZ = 0.0;
        if (std::abs(fy) > 0.99) {
            worldUpX = 1.0;
            worldUpY = 0.0;
            worldUpZ = 0.0;
        }

        const double rightX = worldUpY * fz - worldUpZ * fy;
        const double rightY = worldUpZ * fx - worldUpX * fz;
        const double rightZ = worldUpX * fy - worldUpY * fx;
        const double rightLen = std::sqrt(rightX * rightX + rightY * rightY + rightZ * rightZ);
        const double rx = rightX / rightLen;
        const double ry = rightY / rightLen;
        const double rz = rightZ / rightLen;

        const double upX = fy * rz - fz * ry;
        const double upY = fz * rx - fx * rz;
        const double upZ = fx * ry - fy * rx;

        if (m_draggingView && (ImGui::IsMouseDragging(ImGuiMouseButton_Right, 0.0f) || ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0.0f))) {
            const ImVec2 mousePos = ImGui::GetMousePos();
            const ImVec2 delta(mousePos.x - m_lastMousePos.x, mousePos.y - m_lastMousePos.y);

            const bool panMode = ImGui::IsMouseDown(ImGuiMouseButton_Middle) || ImGui::GetIO().KeyShift;
            if (panMode) {
                const double panScale = 0.0012f;
                m_targetX += (-rx * delta.x + upX * delta.y) * panScale;
                m_targetY += (-ry * delta.x + upY * delta.y) * panScale;
                m_targetZ += (-rz * delta.x + upZ * delta.y) * panScale;
            }
            else {
                m_rotationYaw += delta.x * 0.005f;
                m_rotationPitch -= delta.y * 0.005f;
                m_rotationPitch = std::clamp(m_rotationPitch, -pitchLimit, pitchLimit);
            }
            m_lastMousePos = mousePos;
        }

        if (bridge.hasTrussPreview && !bridge.trussNodes.empty()) {
            const float centerX = origin.x + panelSize.x * 0.5f;
            const float centerY = origin.y + panelSize.y * 0.5f;
            constexpr double focal = 1200.0;

            auto projectPoint = [&](double x, double y, double z) -> ImVec2 {
                const double dx = x - cameraX;
                const double dy = y - cameraY;
                const double dz = z - cameraZ;

                const double zCam = dx * fx + dy * fy + dz * fz;
                if (zCam <= 0.1) {
                    return ImVec2(centerX, centerY);
                }

                const double xCam = dx * rx + dy * ry + dz * rz;
                const double yCam = dx * upX + dy * upY + dz * upZ;
                const double invZ = focal / zCam;

                return ImVec2(
                    centerX + static_cast<float>(xCam * invZ),
                    centerY - static_cast<float>(yCam * invZ)
                );
            };

            // define max displacements for nodes and max stress for elements and
            // and color them with comparing these max values

            double maxDisp = 0.0;
            for (const auto& node : bridge.trussNodes) {
                const auto disp = node.getDisplacmenet();
                const double mag = std::sqrt(disp[0] * disp[0] + disp[1] * disp[1] + disp[2] * disp[2]);
                maxDisp = std::max(maxDisp, mag);
            }

            auto displacementColor = [&](double magnitude) -> ImU32 {
                const double normalized = maxDisp > 0.0 ? std::clamp(magnitude / maxDisp, 0.0, 1.0) : 0.0;
                const double t = std::clamp(normalized, 0.0, 1.0);
                const int r = static_cast<int>(255.0 * t);
                const int g = static_cast<int>(180.0 * (1.0 - std::abs(t - 0.5) * 2.0));
                const int b = static_cast<int>(255.0 * (1.0 - t));
                return IM_COL32(std::clamp(r, 0, 255), std::clamp(g, 0, 255), std::clamp(b, 0, 255), 255);
            };

            double maxStress = 0.0;
            for (const auto& element : bridge.trussElements) {
                const auto stress = element.getEleStress();
                maxStress = std::max(maxStress, stress);
            }

            auto stressColor = [&](double magnitude) -> ImU32 {
                const double normalized = maxStress > 0.0 ? std::clamp(magnitude/maxStress, 0.0, 1.0) : 0.0;
                const double t = std::clamp(normalized, 0.0, 1.0);
                const int r = static_cast<int>(255.0 * t);
                const int g = static_cast<int>(180.0 * (1.0 - std::abs(t - 0.5) * 2.0));
                const int b = static_cast<int>(255.0 * (1.0 - t));
                return IM_COL32(std::clamp(r, 0, 255), std::clamp(g, 0, 255), std::clamp(b, 0, 255), 255);
            };

            struct ProjectedNode {
                ImVec2 screenPos;
                ImU32 color;
                double dispMagnitude;
            };

            struct ProjectedElement {
                ImU32 color;
            };

            std::unordered_map<std::uint32_t, ProjectedNode> projectedNodeMap;
            projectedNodeMap.reserve(bridge.trussNodes.size());

            std::unordered_map<std::uint32_t, ProjectedElement> projectedElementMap;
            projectedElementMap.reserve(bridge.trussElements.size());

            for (const auto& node : bridge.trussNodes) {
                const auto& loc = node.getLocation();
                const auto disp = node.getDisplacmenet();
                const auto mag = std::sqrt(disp[0] * disp[0] + disp[1] * disp[1] + disp[2] * disp[2]);
                projectedNodeMap[node.getNodeID()] = {
                    projectPoint(loc[0], loc[1], loc[2]),
                    displacementColor(mag),
                    mag
                };
            }

            for (long long i{0}; i < bridge.trussElements.size(); ++i) {
                const auto& element = bridge.trussElements[i];
                const auto stress = element.getEleStress();
                projectedElementMap[i] = {stressColor(stress)};
            }
            
            // draw coordinat axis 
            const ImVec2 axisOrigin = projectPoint(0.0, 0.0, 0.0);
            const ImVec2 axisX = projectPoint(2.0, 0.0, 0.0);
            const ImVec2 axisY = projectPoint(0.0, 2.0, 0.0);
            const ImVec2 axisZ = projectPoint(0.0, 0.0, 2.0);

            drawList->AddLine(axisOrigin, axisX, IM_COL32(255, 110, 110, 255), 2.0f);
            drawList->AddLine(axisOrigin, axisY, IM_COL32(110, 255, 140, 255), 2.0f);
            drawList->AddLine(axisOrigin, axisZ, IM_COL32(110, 160, 255, 255), 2.0f);
            drawList->AddText(axisX, IM_COL32(255, 110, 110, 255), "X");
            drawList->AddText(axisY, IM_COL32(110, 255, 140, 255), "Y");
            drawList->AddText(axisZ, IM_COL32(110, 160, 255, 255), "Z");

            // render truss elements
            for (long long i{0}; i < bridge.trussElements.size(); ++i) {
                const auto& element = bridge.trussElements[i];
                const auto& nodeID = element.getEleNodes();
                const auto itA = projectedNodeMap.find(nodeID[0]);
                const auto itB = projectedNodeMap.find(nodeID[1]);

                const auto itElement = projectedElementMap.find(i);
                if (itA != projectedNodeMap.end() && itB != projectedNodeMap.end() &&
                    itElement != projectedElementMap.end()) {
                    drawList->AddLine(itA->second.screenPos, itB->second.screenPos, itElement->second.color);
                }
            }

            // render truss nodes
            for (const auto& [nodeID, projData] : projectedNodeMap) {
                if (!m_showNodes) {
                    continue;
                }

                const bool isSelected = (bridge.selectedNodeId == nodeID);
                bool isFixed = false;
                auto fixIt = bridge.fixedDOFsByNode.find(nodeID);
                if (fixIt != bridge.fixedDOFsByNode.end()) {
                    isFixed = fixIt->second[0] || fixIt->second[1] || fixIt->second[2];
                }

                const ImVec2 center = projData.screenPos;
                const float radius = isSelected ? 5.5f : 5.5f;

                drawList->AddCircleFilled(center, radius, projData.color);
                drawList->AddCircle(center, radius + 2.0f, IM_COL32(20, 20, 20, 255), 0, 1.5f);

                if (isFixed) {
                    drawList->AddRect(
                        ImVec2(center.x - radius - 4.0f, center.y - radius - 4.0f),
                        ImVec2(center.x + radius + 4.0f, center.y + radius + 4.0f),
                        IM_COL32(255, 100, 100, 255),
                        0.0f,
                        0,
                        2.0f
                    );
                }

                if (isSelected) {
                    drawList->AddCircle(center, radius + 7.0f, IM_COL32(255, 180, 80, 255), 0, 2.0f);
                }

                drawList->AddText(ImVec2(center.x + 8.0f, center.y - 8.0f), IM_COL32(230, 230, 230, 255), std::to_string(nodeID).c_str());
            }

            constexpr double arrowWorldLength = 1.5;
            constexpr float arrowHeadSize = 8.0f;
            const ImU32 forceArrowColor = IM_COL32(255, 60, 60, 255);

            // render applied force
            for (const auto& force : bridge.appliedForces) {
                const auto nodeIt = projectedNodeMap.find(force.getApliedNode());
                if (nodeIt == projectedNodeMap.end()) {
                    continue;
                }

                const auto forceVec = force.getForce();
                const double fMag = std::sqrt(forceVec[0] * forceVec[0] + forceVec[1] * forceVec[1] + forceVec[2] * forceVec[2]);
                if (fMag < 1e-6) {
                    continue;
                }

                const double dirX = forceVec[0] / fMag;
                const double dirY = forceVec[1] / fMag;
                const double dirZ = forceVec[2] / fMag;

                const auto actualNodeIt = std::find_if(bridge.trussNodes.begin(), bridge.trussNodes.end(), [&](const auto& n) {
                    return n.getNodeID() == force.getApliedNode();
                });
                if (actualNodeIt == bridge.trussNodes.end()) {
                    continue;
                }

                const auto& loc = actualNodeIt->getLocation();
                const ImVec2 baseScreen = nodeIt->second.screenPos;
                const ImVec2 tipScreen = projectPoint(
                    loc[0] + dirX * arrowWorldLength,
                    loc[1] + dirY * arrowWorldLength,
                    loc[2] + dirZ * arrowWorldLength
                );

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

            if (m_viewportHovered_ && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                const ImVec2 mousePos = ImGui::GetMousePos();
                std::uint32_t nearestNode = bridge.selectedNodeId;
                float nearestDistanceSq = 144.0f; // 12px threshold squared

                for (const auto& [nodeId, projData] : projectedNodeMap) {
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

            drawList->AddText(ImVec2(origin.x + 16.0f, origin.y + 16.0f), IM_COL32(180, 180, 180, 255), "Press CTRL to toggle nodes");
        }
        else {
            drawList->AddText(ImVec2(origin.x + 16.0f, origin.y + 16.0f), IM_COL32(180, 180, 180, 255), "No truss preview generated");
        }

        ImGui::End();
        ImGui::PopStyleVar();
    }

} // namespace anaf::GUI end
