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

#include "orientationAids.hpp"

#include <guiMaterials/theme.hpp>

#include <glm/geometric.hpp>

#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <numbers>

namespace anaf::GUI::ORIENTATION {

  namespace {
    // A cube face: outward normal, in-plane axes u (label right) and v (label up), u x v = normal.
    struct Face {
      glm::vec3 normal;
      glm::vec3 u;
      glm::vec3 v;
      const char* label;
    };

    // Border cells take this much of each half edge: edges and corners stay easy to hit.
    constexpr float cellSplit = 0.6f;
    // Faces turned further away than this are not labelled (the text would be a sliver).
    constexpr float labelMinFacing = 0.2f;

    ImVec2 project(const glm::mat3& viewRotation, const glm::vec3& point, const ImVec2 center, const float halfSize) {
      const glm::vec3 view = viewRotation * point;
      return ImVec2(center.x + view.x * halfSize, center.y - view.y * halfSize);
    }

    float cross2(const ImVec2 a, const ImVec2 b, const ImVec2 p) { return (b.x - a.x) * (p.y - a.y) - (b.y - a.y) * (p.x - a.x); }

    // p inside the convex quad (either winding).
    bool insideQuad(const std::array<ImVec2, 4>& quad, const ImVec2 p) {
      bool anyPositive = false;
      bool anyNegative = false;
      for (std::size_t i = 0; i < 4; ++i) {
        const float side = cross2(quad[i], quad[(i + 1) % 4], p);
        anyPositive = anyPositive || side > 0.0f;
        anyNegative = anyNegative || side < 0.0f;
      }
      return !(anyPositive && anyNegative);
    }

    ImU32 shaded(const ImVec4& color, const float factor, const float alpha) {
      return THEME::toU32(ImVec4(color.x * factor, color.y * factor, color.z * factor, 1.0f), alpha);
    }

    // Draws label flat on the face: the text is laid out at the face center and every vertex
    // it added is mapped through the face's projected u / v axes.
    void drawFaceLabel(ImDrawList* drawList, const glm::mat3& viewRotation, const Face& face, const ImVec2 center,
                       const float halfSize, const ImU32 color) {
      ImFont* font = ImGui::GetFont();
      float fontSize = halfSize * 0.46f;
      ImVec2 textSize = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, face.label);
      const float maxWidth = halfSize * 1.7f;
      if (textSize.x > maxWidth) {
        fontSize *= maxWidth / textSize.x;
        textSize = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, face.label);
      }

      // Inside the cube, so the glyphs are never culled by the window's clip rectangle.
      const ImVec2 anchor = project(viewRotation, face.normal, center, halfSize);
      const ImVec2 textMin(anchor.x - textSize.x * 0.5f, anchor.y - textSize.y * 0.5f);
      const int first = drawList->VtxBuffer.Size;
      drawList->AddText(font, fontSize, textMin, color, face.label);
      const int last = drawList->VtxBuffer.Size;

      for (int i = first; i < last; ++i) {
        ImDrawVert& vertex = drawList->VtxBuffer[i];
        const float a = (vertex.pos.x - anchor.x) / halfSize;
        const float b = -(vertex.pos.y - anchor.y) / halfSize;
        vertex.pos = project(viewRotation, face.normal + face.u * a + face.v * b, center, halfSize);
      }
    }
  } // namespace end

  ViewCubeResult viewCube(const glm::mat3& viewRotation, const ImVec2 center, const float halfSize) {
    const std::array<Face, 6> faces{{
      {glm::vec3(0.0f, 0.0f, 1.0f), glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f), "FRONT"},
      {glm::vec3(0.0f, 0.0f, -1.0f), glm::vec3(-1.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f), "BACK"},
      {glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, -1.0f), glm::vec3(0.0f, 1.0f, 0.0f), "RIGHT"},
      {glm::vec3(-1.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f), glm::vec3(0.0f, 1.0f, 0.0f), "LEFT"},
      {glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, -1.0f), "TOP"},
      {glm::vec3(0.0f, -1.0f, 0.0f), glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f), "BOTTOM"},
    }};
    const std::array<std::array<float, 2>, 3> cellRange{{{-1.0f, -cellSplit}, {-cellSplit, cellSplit}, {cellSplit, 1.0f}}};

    ViewCubeResult result;
    // The projected cube never leaves the circle of radius sqrt(3) * halfSize.
    const float reach = halfSize * 1.75f;
    result.rectMin = ImVec2(center.x - reach, center.y - reach);
    result.rectMax = ImVec2(center.x + reach, center.y + reach);

    // One item for the whole cube: drag orbits, a click (no drag) snaps to the region under the mouse.
    static bool s_dragged = false;
    ImGui::SetCursorScreenPos(result.rectMin);
    ImGui::InvisibleButton("##viewCube", ImVec2(2.0f * reach, 2.0f * reach));
    const bool hovered = ImGui::IsItemHovered();
    const bool active = ImGui::IsItemActive();
    if (active && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 3.0f)) {
      s_dragged = true;
      result.drag = ImGui::GetIO().MouseDelta;
    }
    const bool released = ImGui::IsItemDeactivated();
    if (!s_dragged) ImGui::SetItemTooltip("Click a face, edge or corner to look from there; drag to orbit.");

    // Region under the mouse: n + i u + j v of the hit cell (i, j in -1, 0, 1), unique for a face
    // center, an edge or a corner, so it also marks the cells of the same region on other faces.
    struct Cell {
      std::array<ImVec2, 4> quad;
      glm::vec3 region;
      float facing;
    };
    std::array<Cell, 6 * 9> cells{};
    std::size_t cellCount = 0;
    std::optional<glm::vec3> hoveredRegion;
    const ImVec2 mouse = ImGui::GetMousePos();
    for (const Face& face : faces) {
      const float facing = (viewRotation * face.normal).z;
      if (facing <= 0.02f) continue;
      for (int i = -1; i <= 1; ++i) {
        for (int j = -1; j <= 1; ++j) {
          const auto& rangeU = cellRange[static_cast<std::size_t>(i + 1)];
          const auto& rangeV = cellRange[static_cast<std::size_t>(j + 1)];
          Cell& cell = cells[cellCount++];
          cell.region = face.normal + face.u * static_cast<float>(i) + face.v * static_cast<float>(j);
          cell.facing = facing;
          cell.quad = {project(viewRotation, face.normal + face.u * rangeU[0] + face.v * rangeV[0], center, halfSize),
                       project(viewRotation, face.normal + face.u * rangeU[1] + face.v * rangeV[0], center, halfSize),
                       project(viewRotation, face.normal + face.u * rangeU[1] + face.v * rangeV[1], center, halfSize),
                       project(viewRotation, face.normal + face.u * rangeU[0] + face.v * rangeV[1], center, halfSize)};
          if ((hovered || active) && !s_dragged && insideQuad(cell.quad, mouse)) hoveredRegion = cell.region;
        }
      }
    }
    if (released) {
      if (!s_dragged && hoveredRegion) result.snapDirection = glm::normalize(*hoveredRegion);
      s_dragged = false;
    }

    // Faces lit by how directly they face the eye; outline and labels on top.
    const THEME::ThemePalette& palette = THEME::theme();
    const ImVec4 faceBase = THEME::mix(palette.raised, palette.text, palette.light ? 0.06f : 0.22f);
    const ImU32 highlight = THEME::toU32(palette.accent, 0.62f);
    const ImU32 outline = THEME::toU32(palette.textDim, 0.85f);
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    for (std::size_t c = 0; c < cellCount; ++c) {
      const Cell& cell = cells[c];
      const bool lit = hoveredRegion && *hoveredRegion == cell.region;
      drawList->AddQuadFilled(cell.quad[0], cell.quad[1], cell.quad[2], cell.quad[3],
                              lit ? highlight : shaded(faceBase, 0.72f + 0.28f * cell.facing, 0.94f));
    }
    for (const Face& face : faces) {
      const float facing = (viewRotation * face.normal).z;
      if (facing <= 0.02f) continue;
      const auto corner = [&](const float a, const float b) { return project(viewRotation, face.normal + face.u * a + face.v * b, center, halfSize); };
      const std::array<ImVec2, 4> quad{corner(-1.0f, -1.0f), corner(1.0f, -1.0f), corner(1.0f, 1.0f), corner(-1.0f, 1.0f)};
      drawList->AddPolyline(quad.data(), 4, outline, ImDrawFlags_Closed, 1.0f);
      if (facing >= labelMinFacing) {
        const float alpha = std::clamp((facing - labelMinFacing) / 0.25f, 0.0f, 1.0f);
        drawFaceLabel(drawList, viewRotation, face, center, halfSize, THEME::toU32(palette.text, alpha));
      }
    }
    return result;
  }

  void axisTriad(ImDrawList* drawList, const glm::mat3& viewRotation, const ImVec2 origin, const float length) {
    struct Axis {
      glm::vec3 view;
      ImU32 color;
      const char* label;
    };
    // Axis colors are semantic (X red, Y green, Z blue), the same in every theme.
    std::array<Axis, 3> axes{{
      {viewRotation * glm::vec3(1.0f, 0.0f, 0.0f), IM_COL32(236, 88, 88, 255), "X"},
      {viewRotation * glm::vec3(0.0f, 1.0f, 0.0f), IM_COL32(96, 196, 80, 255), "Y"},
      {viewRotation * glm::vec3(0.0f, 0.0f, 1.0f), IM_COL32(84, 140, 240, 255), "Z"},
    }};
    // Back to front (view z points out of the screen), so the axes facing the eye are drawn last.
    std::ranges::sort(axes, {}, [](const Axis& axis) { return axis.view.z; });

    const float headLength = std::max(6.0f, length * 0.2f);
    for (const Axis& axis : axes) {
      const ImVec2 direction(axis.view.x, -axis.view.y);
      const float screenLength = std::sqrt(direction.x * direction.x + direction.y * direction.y);
      const ImVec2 tip(origin.x + direction.x * length, origin.y + direction.y * length);
      drawList->AddLine(origin, tip, axis.color, 2.0f);
      // Arrow head only while the axis shows a usable length on screen (not pointing at the eye).
      if (screenLength > 0.25f) {
        const ImVec2 along(direction.x / screenLength, direction.y / screenLength);
        const ImVec2 side(-along.y, along.x);
        const ImVec2 base(tip.x - along.x * headLength, tip.y - along.y * headLength);
        const float halfWidth = headLength * 0.4f;
        drawList->AddTriangleFilled(ImVec2(tip.x + along.x * 2.0f, tip.y + along.y * 2.0f),
                                    ImVec2(base.x + side.x * halfWidth, base.y + side.y * halfWidth),
                                    ImVec2(base.x - side.x * halfWidth, base.y - side.y * halfWidth), axis.color);
      }
      // Label past the tip; an axis pointing at the eye keeps it next to the origin.
      const ImVec2 labelSize = ImGui::CalcTextSize(axis.label);
      const float labelDistance = length + std::max(labelSize.x, labelSize.y) * 0.75f + 2.0f;
      const ImVec2 labelCenter(origin.x + direction.x * labelDistance, origin.y + direction.y * labelDistance);
      drawList->AddText(ImVec2(labelCenter.x - labelSize.x * 0.5f, labelCenter.y - labelSize.y * 0.5f), axis.color, axis.label);
    }
    drawList->AddCircleFilled(origin, 2.5f, THEME::toU32(THEME::theme().overlayTextDim));
  }

  OrbitAngles anglesForDirection(const glm::vec3& direction, const float referenceYaw) {
    constexpr float quarterTurn = 0.5f * std::numbers::pi_v<float>;
    const glm::vec3 d = glm::normalize(direction);
    OrbitAngles angles;
    angles.pitch = -std::asin(std::clamp(d.y, -1.0f, 1.0f));
    if (std::abs(d.y) > 0.9999f) {
      angles.yaw = std::round(referenceYaw / quarterTurn) * quarterTurn;
    } else {
      angles.yaw = std::atan2(d.x, d.z);
    }
    return angles;
  }

} // namespace anaf::GUI::ORIENTATION end
