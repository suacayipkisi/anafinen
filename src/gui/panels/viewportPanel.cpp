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
#include <limits>
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

        if (m_viewportHovered_ && (ImGui::IsMouseClicked(ImGuiMouseButton_Right) || ImGui::IsMouseClicked(ImGuiMouseButton_Middle))) {
            m_draggingView = true;
            m_lastMousePos = ImGui::GetMousePos();
        }
        if (m_draggingView && !ImGui::IsMouseDown(ImGuiMouseButton_Right) && !ImGui::IsMouseDown(ImGuiMouseButton_Middle)) {
            m_draggingView = false;
        }

        const float wheel = ImGui::GetIO().MouseWheel;
        if (m_viewportHovered_ && wheel != 0.0f) {
            m_cameraDistance = std::clamp(m_cameraDistance * (1.0f - wheel * 0.42f), 2.0f, 120.0f);
        }

        if (m_draggingView && (ImGui::IsMouseDragging(ImGuiMouseButton_Right, 0.0f) || ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0.0f))) {
            const ImVec2 mousePos = ImGui::GetMousePos();
            const ImVec2 delta(
                mousePos.x - m_lastMousePos.x,
                mousePos.y - m_lastMousePos.y
            );

            const bool panMode = ImGui::IsMouseDown(ImGuiMouseButton_Middle) || ImGui::GetIO().KeyShift;
            if (panMode) {
                // Camera frame vectors
                const double cameraX = m_targetX + std::sin(m_rotationYaw) * std::cos(m_rotationPitch) * m_cameraDistance;
                const double cameraY = m_targetY - std::sin(m_rotationPitch) * m_cameraDistance;
                const double cameraZ = m_targetZ + std::cos(m_rotationYaw) * std::cos(m_rotationPitch) * m_cameraDistance;

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

                // Pan in camera frame
                const double panScale = 0.01f;
                m_targetX += (-rx * delta.x + upX * delta.y) * panScale;
                m_targetY += (-ry * delta.x + upY * delta.y) * panScale;
                m_targetZ += (-rz * delta.x + upZ * delta.y) * panScale;
            }
            else {
                m_rotationYaw += delta.x * 0.01f;
                m_rotationPitch -= delta.y * 0.01f;
            }
            m_lastMousePos = mousePos;
        }

        if (bridge.hasTrussPreview && !bridge.trussNodes.empty()) {
            const float padding = 24.0f;
            const float xMin = origin.x + padding;
            const float xMax = origin.x + panelSize.x - padding;
            const float yMin = origin.y + padding;
            const float yMax = origin.y + panelSize.y - padding;

            const float centerX = (xMin + xMax) * 0.5f;
            const float centerY = (yMin + yMax) * 0.5f;

            auto projectPoint = [&](double x, double y, double z) -> ImVec2 {
                const double cameraX = m_targetX + std::sin(m_rotationYaw) * std::cos(m_rotationPitch) * m_cameraDistance;
                const double cameraY = m_targetY - std::sin(m_rotationPitch) * m_cameraDistance;
                const double cameraZ = m_targetZ + std::cos(m_rotationYaw) * std::cos(m_rotationPitch) * m_cameraDistance;

                const double dx = x - cameraX;
                const double dy = y - cameraY;
                const double dz = z - cameraZ;

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

                const double xCam = dx * rx + dy * ry + dz * rz;
                const double yCam = dx * upX + dy * upY + dz * upZ;
                const double zCam = dx * fx + dy * fy + dz * fz;

                const double focal = 1200.0;
                if (zCam <= 0.1) {
                    return ImVec2(centerX, centerY);
                }

                const double scale = focal / zCam;
                return ImVec2(
                    centerX + static_cast<float>(xCam * scale),
                    centerY - static_cast<float>(yCam * scale)
                );
            };

            std::vector<std::pair<std::uint32_t, ImVec2>> projected;
            projected.reserve(bridge.trussNodes.size());

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

            for (const auto& node : bridge.trussNodes) {
                const auto& loc = node.getLocation();
                projected.emplace_back(node.getNodeID(), projectPoint(loc[0], loc[1], loc[2]));
            }

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

            for (const auto& element : bridge.trussElements) {
                const auto aIt = std::find_if(projected.begin(), projected.end(), [&](const auto& pair) {
                    return pair.first == element[0];
                });
                const auto bIt = std::find_if(projected.begin(), projected.end(), [&](const auto& pair) {
                    return pair.first == element[1];
                });

                if (aIt != projected.end() && bIt != projected.end()) {
                    const auto aNode = std::find_if(bridge.trussNodes.begin(), bridge.trussNodes.end(), [&](const auto& node) {
                        return node.getNodeID() == element[0];
                    });
                    const auto bNode = std::find_if(bridge.trussNodes.begin(), bridge.trussNodes.end(), [&](const auto& node) {
                        return node.getNodeID() == element[1];
                    });

                    double aMag = 0.0;
                    double bMag = 0.0;
                    if (aNode != bridge.trussNodes.end()) {
                        const auto disp = aNode->getDisplacmenet();
                        aMag = std::sqrt(disp[0] * disp[0] + disp[1] * disp[1] + disp[2] * disp[2]);
                    }
                    if (bNode != bridge.trussNodes.end()) {
                        const auto disp = bNode->getDisplacmenet();
                        bMag = std::sqrt(disp[0] * disp[0] + disp[1] * disp[1] + disp[2] * disp[2]);
                    }
                    const double averageMag = (aMag + bMag) * 0.5;
                    drawList->AddLine(aIt->second, bIt->second, displacementColor(averageMag), 2.5f);
                }
            }

            for (const auto& pair : projected) {
                const bool isSelected = bridge.selectedNodeId == pair.first;
                const bool isFixed = bridge.fixedDOFsByNode.find(pair.first) != bridge.fixedDOFsByNode.end() &&
                    (bridge.fixedDOFsByNode[pair.first][0] || bridge.fixedDOFsByNode[pair.first][1] || bridge.fixedDOFsByNode[pair.first][2]);
                const ImVec2 center = pair.second;
                const auto nodeIt = std::find_if(bridge.trussNodes.begin(), bridge.trussNodes.end(), [&](const auto& node) {
                    return node.getNodeID() == pair.first;
                });
                double nodeDisp = 0.0;
                if (nodeIt != bridge.trussNodes.end()) {
                    const auto disp = nodeIt->getDisplacmenet();
                    nodeDisp = std::sqrt(disp[0] * disp[0] + disp[1] * disp[1] + disp[2] * disp[2]);
                }
                const float radius = isSelected ? 7.5f : 5.5f;
                const ImU32 nodeColor = displacementColor(nodeDisp);
                drawList->AddCircleFilled(center, radius, nodeColor);
                drawList->AddCircle(center, radius + 2.0f, IM_COL32(20, 20, 20, 255), 0, 1.5f);

                if (isFixed) {
                    drawList->AddRect(
                        ImVec2(center.x - radius - 5.0f, center.y - radius - 5.0f),
                        ImVec2(center.x + radius + 5.0f, center.y + radius + 5.0f),
                        IM_COL32(255, 100, 100, 255),
                        0.0f,
                        0,
                        2.0f);
                }

                if (isSelected) {
                    drawList->AddCircle(
                        center,
                        radius + 7.0f,
                        IM_COL32(255, 180, 80, 255),
                        0,
                        2.0f);
                }

                if (!isSelected && isFixed) {
                    drawList->AddRect(
                        ImVec2(center.x - radius - 3.5f, center.y - radius - 3.5f),
                        ImVec2(center.x + radius + 3.5f, center.y + radius + 3.5f),
                        IM_COL32(255, 100, 100, 160),
                        0.0f,
                        0,
                        1.5f);
                }

                drawList->AddText(ImVec2(center.x + 8.0f, center.y - 8.0f), IM_COL32(230, 230, 230, 255), std::to_string(pair.first).c_str());
            }

            if (m_viewportHovered_ && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                const ImVec2 mousePos = ImGui::GetMousePos();
                std::uint32_t nearestNode = bridge.selectedNodeId;
                float nearestDistance = std::numeric_limits<float>::max();

                for (const auto& pair : projected) {
                    const float dx = mousePos.x - pair.second.x;
                    const float dy = mousePos.y - pair.second.y;
                    const float distSq = dx * dx + dy * dy;
                    if (distSq < nearestDistance && distSq < 100.0f) {
                        nearestDistance = distSq;
                        nearestNode = pair.first;
                    }
                }

                bridge.selectedNodeId = nearestNode;
            }
        }
        else {
            drawList->AddText(ImVec2(origin.x + 16.0f, origin.y + 16.0f), IM_COL32(180, 180, 180, 255), "No truss preview generated");
        }

        ImGui::End();
        ImGui::PopStyleVar();
    }

} // namespace anaf::GUI end
