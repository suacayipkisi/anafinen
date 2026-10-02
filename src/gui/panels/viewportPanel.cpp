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

#include "viewportPanel.hpp"
#include <bridge/generalStatus.hpp>

#include "imgui.h"
#include "viewportRenderer.hpp"

#include <glm/ext/vector_float3.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <memory>
#include <mutex>
#include <numbers>
#include <string>
#include <vector>

namespace anaf::GUI {

  ViewportPanel::ViewportPanel(std::shared_ptr<Framebuffer> fbo) :
    m_fbo_(std::move(fbo)),
    m_renderer_(std::make_unique<ViewportRenderer>())
  {}

  namespace {
    constexpr float kFovY = std::numbers::pi_v<float> / 4.0f; // 45 deg
    constexpr float kMinCameraDistance = 1e-3f;
  } // namespace end

  void ViewportPanel::updateSceneBounds() {
    m_sceneCenter = glm::vec3(0.0f);
    m_sceneRadius = 10.0f;
    if (!m_currentMesh || m_currentMesh->trussNodes.empty()) return;

    glm::vec3 boundsMin(std::numeric_limits<float>::max());
    glm::vec3 boundsMax(std::numeric_limits<float>::lowest());
    for (const auto& node : m_currentMesh->trussNodes) {
      const auto& location = node.getLocation();
      const glm::vec3 position(
        static_cast<float>(location[0]),
        static_cast<float>(location[1]),
        static_cast<float>(location[2])
      );
      boundsMin = glm::min(boundsMin, position);
      boundsMax = glm::max(boundsMax, position);
    }
    m_sceneCenter = (boundsMin + boundsMax) * 0.5f;
    m_sceneRadius = std::max(0.5f * glm::length(boundsMax - boundsMin), 0.01f);
  }

  void ViewportPanel::resetCamera() {
    m_rotationYaw = 0.9f;
    m_rotationPitch = -0.7f;
    m_draggingView = false;
    updateSceneBounds();
    m_target = m_sceneCenter;
    m_cameraDistance = (m_currentMesh && !m_currentMesh->trussNodes.empty()) ? m_sceneRadius * 2.2f : 18.0f;
  }

  glm::vec3 ViewportPanel::orbitDirection() const {
    return glm::vec3(
      std::sin(m_rotationYaw) * std::cos(m_rotationPitch),
      -std::sin(m_rotationPitch),
      std::cos(m_rotationYaw) * std::cos(m_rotationPitch)
    );
  }

  glm::vec3 ViewportPanel::orbitUp() const {
    // -d(orbitDirection)/d(pitch): perpendicular to the view direction, equal to +y at zero
    // pitch, and upside down (continuously) once the camera passes over a pole.
    return glm::vec3(
      std::sin(m_rotationYaw) * std::sin(m_rotationPitch),
      std::cos(m_rotationPitch),
      std::cos(m_rotationYaw) * std::sin(m_rotationPitch)
    );
  }

  float ViewportPanel::farPlane() const {
    // Far enough for the whole model wherever the target has been panned to.
    const float reach = m_cameraDistance + glm::length(m_target - m_sceneCenter) + m_sceneRadius;
    return std::max(reach * 2.0f, 10.0f);
  }

  void ViewportPanel::handleCameraInput() {
    ImGuiIO& io = ImGui::GetIO();

    if (m_viewportHovered_ && ImGui::IsKeyPressed(ImGuiKey_R)) {
      resetCamera();
    }

    if (m_viewportHovered_ && io.MouseWheel != 0.0f) {
      // Only a numeric guard: zooming out stops when the model is far below a pixel.
      const float maxDistance = std::max(2000.0f, m_sceneRadius * 50.0f);
      m_cameraDistance = std::clamp(m_cameraDistance * (1.0f - io.MouseWheel * 0.15f), kMinCameraDistance, maxDistance);
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
        const glm::vec3 forward = -orbitDirection();
        const glm::vec3 up = orbitUp();
        const glm::vec3 right = glm::cross(forward, up);

        const bool panMode = ImGui::IsMouseDown(ImGuiMouseButton_Middle) || io.KeyShift;
        if (panMode) {
          const float panScale = 0.0015f * m_cameraDistance;
          m_target += (-right * delta.x + up * delta.y) * panScale;
        } else {
          // Upside down, a horizontal drag still turns the view the way the mouse moves.
          const float yawSign = std::cos(m_rotationPitch) < 0.0f ? -1.0f : 1.0f;
          m_rotationYaw = std::remainder(m_rotationYaw - yawSign * delta.x * 0.005f, 2.0f * std::numbers::pi_v<float>);
          m_rotationPitch = std::remainder(m_rotationPitch - delta.y * 0.005f, 2.0f * std::numbers::pi_v<float>);
        }
      }
    }
  }

  glm::mat4 ViewportPanel::getViewProjectionMatrix() const {
    const glm::vec3 eye = m_target + orbitDirection() * m_cameraDistance;
    const glm::mat4 view = glm::lookAt(eye, m_target, orbitUp());
    const float aspect = (m_viewportSize.y > 0.0f) ? (m_viewportSize.x / m_viewportSize.y) : 16.0f / 9.0f;
    const float farClip = farPlane();
    const float nearClip = std::max(m_cameraDistance * 0.005f, farClip * 1e-6f);
    const glm::mat4 projection = glm::perspective(kFovY, aspect, nearClip, farClip);

    return projection * view;
  }

  void ViewportPanel::buildSceneBatches() {
    m_renderer_->clearBuffers();

    // Coordinate axes X, Y, Z, extended far past the camera's far clip plane so they appear infinite (EntityID = -1)
    // farPlane() stays below ~100 scene radii at the widest zoom, so 200 radii look infinite.
    const float axisReach = std::max(8000.0f, m_sceneRadius * 200.0f);
    m_renderer_->addLine(glm::vec3(-axisReach, 0.0f, 0.0f), glm::vec3(axisReach, 0.0f, 0.0f), glm::vec4(1.0f, 0.2f, 0.2f, 1.0f), -1);
    m_renderer_->addLine(glm::vec3(0.0f, -axisReach, 0.0f), glm::vec3(0.0f, axisReach, 0.0f), glm::vec4(0.2f, 1.0f, 0.2f, 1.0f), -1);
    m_renderer_->addLine(glm::vec3(0.0f, 0.0f, -axisReach), glm::vec3(0.0f, 0.0f, axisReach), glm::vec4(0.2f, 0.4f, 1.0f, 1.0f), -1);

    if (!m_currentMesh || m_currentMesh->trussNodes.empty()) {
      m_cachedMaxStress = 0.0;
      m_cachedMaxDisp = 0.0;
      m_renderer_->uploadCurrentBuffer();
      return;
    }

    const auto& mesh = *m_currentMesh;
    auto& bridge = BRIDGE::buildBridge();

    std::uint32_t selectedId = std::numeric_limits<std::uint32_t>::max();
    {
      std::lock_guard<std::mutex> lock(bridge.dataMutex);
      selectedId = bridge.selectedNodeId;
    }

    m_renderer_->reserve(3 + mesh.trussElements.size() + mesh.appliedForces.size() * 3, mesh.trussNodes.size());

    double maxStress = 0.0;
    for (const auto& element : mesh.trussElements) {
      maxStress = std::max(maxStress, std::abs(static_cast<double>(element.stress)));
    }
    m_cachedMaxStress = maxStress;

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
      const auto disp = node.getDisplacement();
      maxDisp = std::max(maxDisp, std::sqrt(disp[0] * disp[0] + disp[1] * disp[1] + disp[2] * disp[2]));
    }
    m_cachedMaxDisp = maxDisp;

    std::vector<glm::vec3> nodeLookup(maxNodeId + 1, glm::vec3(0.0f));
    for (const auto& node : mesh.trussNodes) {
      const auto& loc = node.getLocation();
      const auto disp = node.getDisplacement();

      nodeLookup[node.getNodeID()] = glm::vec3(
        loc[0] + disp[0] * deformScale,
        loc[1] + disp[1] * deformScale,
        loc[2] + disp[2] * deformScale
      );
    }

    // Truss Elements (Lines)
    for (const auto& element : mesh.trussElements) {
      if (element.node1 <= maxNodeId && element.node2 <= maxNodeId) {
        glm::vec4 color = stressColor(element.stress);
        m_renderer_->addLine(nodeLookup[element.node1], nodeLookup[element.node2], color, -1);
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

        const auto disp = node.getDisplacement();
        const double mag = std::sqrt(disp[0] * disp[0] + disp[1] * disp[1] + disp[2] * disp[2]);

        glm::vec4 pColor = displacementColor(mag);
        float pSize = 12.0f;

        if (id == selectedId) {
          pColor = glm::vec4(1.0f, 0.7f, 0.2f, 1.0f);
          pSize = 18.0f;
        } else if (node.isSupported()) {
          pColor = glm::vec4(1.0f, 0.3f, 0.3f, 1.0f);
        }

        m_renderer_->addPoint(pos, pColor, static_cast<int>(id), pSize);
      }
    }

    // Inclined / skewed supports (red, at the drawn node position): a plane the node slides on
    // as a translucent square with outline, or a line it moves along as a double arrow. Sized
    // from the model, so they stay readable on a 1 m mount and on a 300 m stadium.
    {
      const float symbol = std::max(m_sceneRadius * 0.04f, 1e-3f);
      const glm::vec4 supportColor(1.0f, 0.22f, 0.22f, 1.0f);
      const glm::vec4 supportFill(1.0f, 0.22f, 0.22f, 0.28f);
      const glm::vec4 supportGlow(1.0f, 0.3f, 0.3f, 0.35f);
      const auto toVec = [](const std::array<double, 3>& v) {
        return glm::vec3(static_cast<float>(v[0]), static_cast<float>(v[1]), static_cast<float>(v[2]));
      };
      for (const auto& node : mesh.trussNodes) {
        if (!node.hasInclinedSupport()) continue;
        const auto& directions = node.getAllowedMotionDirections();
        const glm::vec3& pos = nodeLookup[node.getNodeID()];
        if (directions.size() == 1) {
          const glm::vec3 along = toVec(directions[0]);
          const glm::vec3 a = pos - along * (1.6f * symbol), b = pos + along * (1.6f * symbol);
          m_renderer_->addLine(a, b, supportColor, -1);
          m_renderer_->addGlowLine(a, b, supportGlow);
          const glm::vec3 helper = std::abs(along.y) > 0.9f ? glm::vec3(1.0f, 0.0f, 0.0f) : glm::vec3(0.0f, 1.0f, 0.0f);
          const glm::vec3 side = glm::normalize(glm::cross(along, helper)) * (0.25f * symbol);
          const glm::vec3 side2 = glm::cross(along, side);
          for (const auto& [tip, back] : {std::pair{a, along}, {b, -along}}) {
            const glm::vec3 headBase = tip + back * (0.4f * symbol);
            for (const glm::vec3& offset : {side, -side, side2, -side2}) m_renderer_->addLine(tip, headBase + offset, supportColor, -1);
          }
        } else if (directions.size() == 2) {
          const glm::vec3 u = toVec(directions[0]) * symbol, v = toVec(directions[1]) * symbol;
          const std::array<glm::vec3, 4> corner{pos - u - v, pos + u - v, pos + u + v, pos - u + v};
          m_renderer_->addTriangle(corner[0], corner[1], corner[2], supportFill);
          m_renderer_->addTriangle(corner[0], corner[2], corner[3], supportFill);
          for (std::size_t c = 0; c < 4; ++c) m_renderer_->addLine(corner[c], corner[(c + 1) % 4], supportColor, -1);
        }
      }
    }

    // Force Arrows (Lines in FBO)
    constexpr float arrowWorldLength = 3.0f;
    constexpr float headLength = 0.1f;
    constexpr float headRadius = 0.05f;
    const glm::vec4 forceArrowColor(1.0f, 0.25f, 0.25f, 1.0f);
    const glm::vec4 forceGlowColor(1.0f, 0.3f, 0.3f, 0.35f);

    for (const auto& force : mesh.appliedForces) {
      const uint32_t targetId = force.getAppliedNode();
      if (targetId > maxNodeId) continue;

      const auto forceVec = force.getForce();
      const double fMag = std::sqrt(forceVec[0] * forceVec[0] + forceVec[1] * forceVec[1] + forceVec[2] * forceVec[2]);
      if (fMag < 1e-6) continue;

      const glm::vec3 dir = glm::normalize(glm::vec3(forceVec[0], forceVec[1], forceVec[2]));
      const glm::vec3 basePos = nodeLookup[targetId];
      const glm::vec3 tipPos = basePos + dir * arrowWorldLength;

      m_renderer_->addLine(basePos, tipPos, forceArrowColor, -1);
      m_renderer_->addGlowLine(basePos, tipPos, forceGlowColor);

      glm::vec3 arbitraryUp = (std::abs(dir.y) > 0.9f) ? glm::vec3(1.0f, 0.0f, 0.0f) : glm::vec3(0.0f, 1.0f, 0.0f);
      glm::vec3 side1 = glm::normalize(glm::cross(dir, arbitraryUp)) * headRadius;
      glm::vec3 side2 = glm::normalize(glm::cross(dir, side1)) * headRadius;
      glm::vec3 headBase = tipPos - dir * headLength;

      m_renderer_->addLine(tipPos, headBase + side1, forceArrowColor, -1);
      m_renderer_->addLine(tipPos, headBase - side1, forceArrowColor, -1);
      m_renderer_->addLine(tipPos, headBase + side2, forceArrowColor, -1);
      m_renderer_->addLine(tipPos, headBase - side2, forceArrowColor, -1);
      m_renderer_->addGlowLine(tipPos, headBase + side1, forceGlowColor);
      m_renderer_->addGlowLine(tipPos, headBase - side1, forceGlowColor);
      m_renderer_->addGlowLine(tipPos, headBase + side2, forceGlowColor);
      m_renderer_->addGlowLine(tipPos, headBase - side2, forceGlowColor);
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
      if (m_fitRequested_ && m_currentMesh) {
        resetCamera();
        m_fitRequested_ = false;
      } else {
        updateSceneBounds();
      }
      buildSceneBatches();
    }

    m_fbo_->bind();
    glEnable(GL_DEPTH_TEST);

    // Scene color, entity-ID buffer (-1 = nothing picked) and depth.
    m_fbo_->clear(0.08f, 0.09f, 0.11f, 1.0f, -1);

    const glm::mat4 mvp = getViewProjectionMatrix();
    {
      GridView grid;
      grid.spacing = std::pow(10.0f, std::floor(std::log10(m_cameraDistance / 12.0f)));
      // Local origin snapped to the major grid, computed in double so a far-panned camera keeps
      // its precision; the shader then only works with eye-relative coordinates.
      const glm::vec3 eye = m_target + orbitDirection() * m_cameraDistance;
      const double major = 10.0 * static_cast<double>(grid.spacing);
      const double originX = std::floor(static_cast<double>(eye.x) / major) * major;
      const double originZ = std::floor(static_cast<double>(eye.z) / major) * major;
      grid.origin = glm::vec2(static_cast<float>(originX), static_cast<float>(originZ));
      grid.eyeLocal = glm::vec3(static_cast<float>(eye.x - originX), eye.y, static_cast<float>(eye.z - originZ));
      const float tanHalfFov = std::tan(kFovY * 0.5f);
      const float aspect = (m_viewportSize.y > 0.0f) ? (m_viewportSize.x / m_viewportSize.y) : 16.0f / 9.0f;
      grid.forward = -orbitDirection();
      grid.up = orbitUp() * tanHalfFov;
      grid.right = glm::cross(grid.forward, orbitUp()) * (tanHalfFov * aspect);
      grid.fadeDistance = std::max(m_cameraDistance * 40.0f, m_sceneRadius * 6.0f);
      m_renderer_->renderGrid(grid);
    }
    m_renderer_->render(mvp);

    // Node number labels, rendered as OpenGL glyph quads (ImGui font atlas) instead of an ImGui 2D overlay.
    m_renderer_->clearTextBuffer();
    if (m_showNodes && m_currentMesh && !m_currentMesh->trussNodes.empty()) {
      std::uint32_t selectedId = std::numeric_limits<std::uint32_t>::max();
      {
        std::lock_guard<std::mutex> lock(bridge.dataMutex);
        selectedId = bridge.selectedNodeId;
      }

      const float fbWidth = static_cast<float>(m_fbo_->getWidth());
      const float fbHeight = static_cast<float>(m_fbo_->getHeight());
      const double deformScale = m_currentMesh->deformScale;

      for (const auto& node : m_currentMesh->trussNodes) {
        const uint32_t id = node.getNodeID();
        const bool isSelected = (selectedId == id);
        if (m_cameraDistance >= 15.0f && !isSelected) continue;

        const auto& loc = node.getLocation();
        const auto disp = node.getDisplacement();
        const glm::vec3 worldPos(
          loc[0] + disp[0] * deformScale,
          loc[1] + disp[1] * deformScale,
          loc[2] + disp[2] * deformScale
        );

        const glm::vec4 clipPos = mvp * glm::vec4(worldPos, 1.0f);
        if (clipPos.w <= 0.1f) continue;

        const glm::vec3 ndc = glm::vec3(clipPos) / clipPos.w;
        const float screenX = (ndc.x * 0.5f + 0.5f) * fbWidth + 8.0f;
        const float screenY = (-ndc.y * 0.5f + 0.5f) * fbHeight - 8.0f;

        m_renderer_->addText(glm::vec2(screenX, screenY), std::to_string(id),
                   glm::vec4(0.9f, 0.9f, 0.9f, 1.0f), fbWidth, fbHeight);
      }
    }
    m_renderer_->uploadTextBuffer();
    m_renderer_->renderText();

    m_fbo_->unbind();
  }

  void ViewportPanel::renderOverlay2D(const ImVec2& origin, const ImVec2& size) {
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const auto currentMesh = m_currentMesh;

    // View orientation gizmo (top-right corner): 3 axes crossing at a point, rotating in sync with the camera.
    {
      constexpr float margin = 16.0f;
      constexpr float topOffset = 48.0f; // sits below the FPS monitor box
      constexpr float gizmoRadius = 40.0f;
      constexpr float gizmoBoxSize = gizmoRadius * 2.0f + 16.0f;

      const ImVec2 gizmoCenter(
        origin.x + size.x - gizmoBoxSize * 0.5f - margin,
        origin.y + topOffset + gizmoBoxSize * 0.5f
      );

      drawList->AddCircleFilled(gizmoCenter, gizmoBoxSize * 0.5f, IM_COL32(20, 22, 27, 150));

      // Rotation-only camera basis; same lookAt formula as getViewProjectionMatrix, so pitch/yaw stay in sync.
      const glm::mat3 camRot(glm::lookAt(orbitDirection(), glm::vec3(0.0f), orbitUp()));

      struct AxisLine { glm::vec3 dir; ImU32 color; const char* label; };
      const AxisLine axes[3] = {
        {glm::vec3(1.0f, 0.0f, 0.0f), IM_COL32(255, 110, 110, 255), "X"},
        {glm::vec3(0.0f, 1.0f, 0.0f), IM_COL32(110, 255, 140, 255), "Y"},
        {glm::vec3(0.0f, 0.0f, 1.0f), IM_COL32(110, 160, 255, 255), "Z"},
      };

      for (const auto& axis : axes) {
        const glm::vec3 viewDir = camRot * axis.dir;
        const ImVec2 tip(gizmoCenter.x + viewDir.x * gizmoRadius, gizmoCenter.y - viewDir.y * gizmoRadius);
        const ImVec2 tail(gizmoCenter.x - viewDir.x * gizmoRadius, gizmoCenter.y + viewDir.y * gizmoRadius);
        drawList->AddLine(tail, tip, axis.color, 2.0f);
        drawList->AddCircleFilled(tip, 3.5f, axis.color);
        drawList->AddText(ImVec2(tip.x + 6.0f, tip.y - 7.0f), axis.color, axis.label);
      }
    }

    // Node ID labels are rendered directly in the OpenGL scene pass (see renderSceneOpenGL), not here.

    // show-hide nodes
    ImGui::SetCursorScreenPos(ImVec2(origin.x + 4.0f, origin.y + 36.0f));
    if (ImGui::Button(m_showNodes ? "Nodes: Visible" : "Nodes: Hidden")) {
      m_showNodes = !m_showNodes;
      truss_1d_gui_prop.m_meshNeedsUpdate = true;
    }

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
      constexpr float barWidth = 10.0f;
      constexpr float barHeight = 180.0f;
      constexpr int colorSteps = 30;

      auto getJetColor = [](float t) -> ImU32 {
        float r = std::clamp(1.5f - std::abs(4.0f * t - 3.0f), 0.0f, 1.0f);
        float g = std::clamp(1.5f - std::abs(4.0f * t - 2.0f), 0.0f, 1.0f);
        float b = std::clamp(1.5f - std::abs(4.0f * t - 1.0f), 0.0f, 1.0f);
        return IM_COL32(static_cast<int>(r * 255.0f), static_cast<int>(g * 255.0f), static_cast<int>(b * 255.0f), 255);
      };

      // Legend box, jet gradient (max at the top) and max / mid / 0 labels; top is the bar's top edge.
      auto drawColorbar = [&](const float startX, const float top, const char* title, const double maxValue) {
        drawList->AddRectFilled(
          ImVec2(startX - 8.0f, top - 24.0f),
          ImVec2(startX + barWidth + 80.0f, top + barHeight + 14.0f),
          IM_COL32(15, 17, 22, 220),
          4.0f
        );
        drawList->AddText(ImVec2(startX, top - 20.0f), IM_COL32(230, 230, 230, 255), title);

        const float stepHeight = barHeight / static_cast<float>(colorSteps);
        for (int i = 0; i < colorSteps; ++i) {
          const float tTop = 1.0f - static_cast<float>(i) / static_cast<float>(colorSteps);
          const float tBottom = 1.0f - static_cast<float>(i + 1) / static_cast<float>(colorSteps);
          drawList->AddRectFilledMultiColor(
            ImVec2(startX, top + static_cast<float>(i) * stepHeight),
            ImVec2(startX + barWidth, top + static_cast<float>(i + 1) * stepHeight),
            getJetColor(tTop), getJetColor(tTop), getJetColor(tBottom), getJetColor(tBottom)
          );
        }
        drawList->AddRect(ImVec2(startX, top), ImVec2(startX + barWidth, top + barHeight), IM_COL32(200, 200, 200, 180));

        char txtMax[32], txtMid[32], txtMin[32];
        std::snprintf(txtMax, sizeof(txtMax), "%.2e", maxValue);
        std::snprintf(txtMid, sizeof(txtMid), "%.2e", maxValue * 0.5);
        std::snprintf(txtMin, sizeof(txtMin), "%.2e", 0.0);

        drawList->AddText(ImVec2(startX + barWidth + 6.0f, top - 2.0f), IM_COL32(230, 230, 230, 255), txtMax);
        drawList->AddText(ImVec2(startX + barWidth + 6.0f, top + barHeight * 0.5f - 6.0f), IM_COL32(200, 200, 200, 255), txtMid);
        drawList->AddText(ImVec2(startX + barWidth + 6.0f, top + barHeight - 10.0f), IM_COL32(230, 230, 230, 255), txtMin);
      };

      const float startX = origin.x + 20.0f;
      const float startY = origin.y + size.y - barHeight - 25.0f;
      drawColorbar(startX, startY, "|Stress| (MPa)", m_cachedMaxStress / 1.0e6);
      if (m_showNodes) {
        drawColorbar(startX, startY - 220.0f, "Disp (mm)", m_cachedMaxDisp * 1000.0);
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

    // Keep the camera reset accessible without requiring keyboard focus.
    ImGui::SetCursorScreenPos(ImVec2(origin.x + 4.0f, origin.y + 4.0f));
    if (ImGui::Button("Reset Camera")) {
      resetCamera();
    }

    // GPU Pixel Picking Interaction
    if (m_viewportHovered_ && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
      const ImVec2 mousePos = ImGui::GetMousePos();
      const int mouseX = static_cast<int>(mousePos.x - origin.x);
      const int mouseY = static_cast<int>(m_viewportSize.y - (mousePos.y - origin.y)); // Invert Y for OpenGL

      const int pickedID = m_fbo_->readEntityID(mouseX, mouseY);

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
    renderOverlay2D(origin, availSize);

    ImGui::End();
    ImGui::PopStyleVar();
  }

} // namespace anaf::GUI end
