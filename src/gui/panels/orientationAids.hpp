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

#pragma once

#include "imgui.h"

#include <glm/ext/matrix_float3x3.hpp>
#include <glm/ext/vector_float3.hpp>

#include <optional>

// Viewport orientation aids drawn with ImGui over the scene image: the ViewCube (top right)
// and the axis triad (bottom left). Both take the camera rotation viewRotation (world to view,
// view x right, y up, z toward the eye). The project is Y-up; the cube's Front face is +Z,
// Right +X, Top +Y.
namespace anaf::GUI::ORIENTATION {

  struct ViewCubeResult {
    // Unit direction from the target to the eye of the view to snap to (a face, an edge or a
    // corner was clicked without dragging).
    std::optional<glm::vec3> snapDirection;
    ImVec2 drag{0.0f, 0.0f}; // mouse delta while the cube is dragged (orbit like a right drag)
    ImVec2 rectMin{0.0f, 0.0f}; // area that takes the mouse (no picking / camera input through it)
    ImVec2 rectMax{0.0f, 0.0f};
  };

  // Cube with face labels around center; halfSize is half the edge in pixels. Each face is split
  // 3 x 3: the middle selects the face, the border cells the edge, the corner cells the corner;
  // the hovered region is highlighted on every face it touches. Submits one ImGui item.
  ViewCubeResult viewCube(const glm::mat3& viewRotation, ImVec2 center, float halfSize);

  // Global X / Y / Z axes from origin, length pixels long, semantic colors (X red, Y green, Z blue).
  void axisTriad(ImDrawList* drawList, const glm::mat3& viewRotation, ImVec2 origin, float length);

  // Yaw / pitch of the orbit camera (ViewportPanel::orbitDirection) that looks from direction
  // (target to eye, unit). A view straight up or down keeps referenceYaw rounded to a quarter
  // turn, so the screen stays aligned with the global axes.
  struct OrbitAngles {
    float yaw{0.0f};
    float pitch{0.0f};
  };
  OrbitAngles anglesForDirection(const glm::vec3& direction, float referenceYaw);

} // namespace anaf::GUI::ORIENTATION end
