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
#include <beam/beamEngine/beamDiagrams.hpp>
#include <beam/beamEngine/beamSolver/deformationUnderConstForce.hpp>
#include <beam/beamSection/sectionStress.hpp>
#include <beam/beamSection/sectionTriangulation.hpp>
#include <bridge/generalStatus.hpp>
#include <objectCalcs/common/supportBasis.hpp>

#include "imgui.h"
#include "viewportRenderer.hpp"

#include <glm/ext/vector_float3.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <limits>
#include <memory>
#include <mutex>
#include <numbers>
#include <string>
#include <variant>
#include <vector>

namespace anaf::GUI {

  ViewportPanel::ViewportPanel(std::shared_ptr<Framebuffer> fbo, std::shared_ptr<ViewportDisplayOptions> display) :
    m_fbo_(std::move(fbo)),
    m_renderer_(std::make_unique<ViewportRenderer>()),
    m_beamRenderer_(std::make_unique<BeamSceneRenderer>()),
    m_display(std::move(display))
  {}

  namespace {
    constexpr float kFovY = std::numbers::pi_v<float> / 4.0f; // 45 deg
    constexpr float kMinCameraDistance = 1e-3f;
    // Level of detail only for large beam models: below this count every element keeps its real
    // section at any distance.
    constexpr std::size_t kLodElementThreshold = 4000;
    constexpr float kFullSectionPixels = 10.0f;  // section at least this tall on screen: real shape
    constexpr float kSimpleSectionPixels = 2.0f; // at least this: box / cylinder; below: a line
    constexpr int kSectionSegmentsPerQuarter = 4;

    const glm::vec4 kBeamColor(0.62f, 0.70f, 0.80f, 1.0f);
    const glm::vec4 kNoStressColor(0.55f, 0.55f, 0.55f, 1.0f);
    const glm::vec4 kSelectedColor(1.0f, 0.7f, 0.2f, 1.0f);
    const glm::vec4 kSupportColor(1.0f, 0.3f, 0.3f, 1.0f);
    const glm::vec4 kHingeColor(0.96f, 0.96f, 0.98f, 1.0f);

    glm::vec3 toVec(const std::array<double, 3>& v) {
      return glm::vec3(static_cast<float>(v[0]), static_cast<float>(v[1]), static_cast<float>(v[2]));
    }

    double magnitude(const std::array<double, 3>& v) { return std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]); }

    // Jet colormap, t in [0, 1] (the colorbars use the same formula).
    glm::vec4 jet(const double value) {
      const float t = static_cast<float>(std::clamp(value, 0.0, 1.0));
      return glm::vec4(std::clamp(1.5f - std::abs(4.0f * t - 3.0f), 0.0f, 1.0f), std::clamp(1.5f - std::abs(4.0f * t - 2.0f), 0.0f, 1.0f),
                       std::clamp(1.5f - std::abs(4.0f * t - 1.0f), 0.0f, 1.0f), 1.0f);
    }

    bool alongGlobalAxes(const std::vector<std::array<double, 3>>& basis) {
      return std::ranges::all_of(basis, [](const std::array<double, 3>& v) { return std::ranges::count(v, 0.0) == 2; });
    }

    // Line with a four-stroke head at tip, plus its glow copy.
    void addArrow(ViewportRenderer& renderer, const glm::vec3& base, const glm::vec3& tip, const glm::vec4& color,
                  const glm::vec4& glow, const float headLength, const float headRadius) {
      renderer.addLine(base, tip, color, -1);
      renderer.addGlowLine(base, tip, glow);
      const glm::vec3 dir = glm::normalize(tip - base);
      const glm::vec3 helper = std::abs(dir.y) > 0.9f ? glm::vec3(1.0f, 0.0f, 0.0f) : glm::vec3(0.0f, 1.0f, 0.0f);
      const glm::vec3 side1 = glm::normalize(glm::cross(dir, helper)) * headRadius;
      const glm::vec3 side2 = glm::normalize(glm::cross(dir, side1)) * headRadius;
      const glm::vec3 headBase = tip - dir * headLength;
      for (const glm::vec3& side : {side1, -side1, side2, -side2}) {
        renderer.addLine(tip, headBase + side, color, -1);
        renderer.addGlowLine(tip, headBase + side, glow);
      }
    }

    // Inclined / skewed support at pos (red): a plane the node slides on as a translucent square
    // with outline, or a line it moves along as a double arrow. symbol sets the size.
    void addInclinedSupport(ViewportRenderer& renderer, const glm::vec3& pos, const std::vector<std::array<double, 3>>& directions,
                            const float symbol) {
      const glm::vec4 supportColor(1.0f, 0.22f, 0.22f, 1.0f);
      const glm::vec4 supportFill(1.0f, 0.22f, 0.22f, 0.28f);
      const glm::vec4 supportGlow(1.0f, 0.3f, 0.3f, 0.35f);
      if (directions.size() == 1) {
        const glm::vec3 along = toVec(directions[0]);
        const glm::vec3 a = pos - along * (1.6f * symbol), b = pos + along * (1.6f * symbol);
        renderer.addLine(a, b, supportColor, -1);
        renderer.addGlowLine(a, b, supportGlow);
        const glm::vec3 helper = std::abs(along.y) > 0.9f ? glm::vec3(1.0f, 0.0f, 0.0f) : glm::vec3(0.0f, 1.0f, 0.0f);
        const glm::vec3 side = glm::normalize(glm::cross(along, helper)) * (0.25f * symbol);
        const glm::vec3 side2 = glm::cross(along, side);
        for (const auto& [tip, back] : {std::pair{a, along}, {b, -along}}) {
          const glm::vec3 headBase = tip + back * (0.4f * symbol);
          for (const glm::vec3& offset : {side, -side, side2, -side2}) renderer.addLine(tip, headBase + offset, supportColor, -1);
        }
      } else if (directions.size() == 2) {
        const glm::vec3 u = toVec(directions[0]) * symbol, v = toVec(directions[1]) * symbol;
        const std::array<glm::vec3, 4> corner{pos - u - v, pos + u - v, pos + u + v, pos - u + v};
        renderer.addTriangle(corner[0], corner[1], corner[2], supportFill);
        renderer.addTriangle(corner[0], corner[2], corner[3], supportFill);
        for (std::size_t c = 0; c < 4; ++c) renderer.addLine(corner[c], corner[(c + 1) % 4], supportColor, -1);
      }
    }

    // Support symbol by type at pos, from the allowed motion basis and the allowed rotation axes
    // (nullptr = rotation free, as for a truss node). The ground side g points away from the
    // free motion, toward -Y where there is a choice:
    //   no motion:   pyramid (rotation free or hinged) on a hatched plate; a clamp (rotation
    //                fixed) is a hatched wall through the node, behind the members
    //   along a line: the same on two rollers in the motion direction, double arrow on the track
    //   in a plane:   the same on four rollers, the plane drawn as a translucent square
    // memberDirection: sum of the unit directions of the members leaving the node (zero if none).
    //   free motion:  a wire cube around the node when a rotation is fixed
    // Allowed rotation axes of a hinge (one or two axes) are drawn as cyan axles through the node.
    void addSupportSymbol(ViewportRenderer& renderer, BeamSceneRenderer& spheres, const glm::vec3& pos,
                          const std::vector<std::array<double, 3>>& motion, const std::vector<std::array<double, 3>>* rotation,
                          const glm::vec3& memberDirection, const float symbol) {
      const std::size_t rotationCount = rotation ? rotation->size() : 3;
      if (motion.size() == 3 && rotationCount == 3) return;
      const glm::vec4 lineColor(1.0f, 0.22f, 0.22f, 1.0f);
      const glm::vec4 fillColor(1.0f, 0.22f, 0.22f, 0.28f);
      const glm::vec4 axleColor(0.3f, 0.85f, 1.0f, 1.0f);
      const glm::vec4 axleGlow(0.3f, 0.85f, 1.0f, 0.35f);
      const float h = symbol, w = 0.7f * symbol;

      const auto quad = [&](const glm::vec3& a, const glm::vec3& b, const glm::vec3& c, const glm::vec3& d) {
        renderer.addTriangle(a, b, c, fillColor);
        renderer.addTriangle(a, c, d, fillColor);
      };
      const auto perpendicular = [](const glm::vec3& v) {
        const glm::vec3 helper = std::abs(v.y) > 0.9f ? glm::vec3(1.0f, 0.0f, 0.0f) : glm::vec3(0.0f, 1.0f, 0.0f);
        return glm::normalize(glm::cross(v, helper));
      };

      if (rotation && rotationCount < 3) {
        for (const auto& axis : *rotation) {
          const glm::vec3 a = toVec(axis);
          renderer.addLine(pos - a * (0.9f * h), pos + a * (0.9f * h), axleColor, -1);
          renderer.addGlowLine(pos - a * (0.9f * h), pos + a * (0.9f * h), axleGlow);
          for (const float end : {-0.9f, 0.9f}) spheres.addSphere(pos + a * (end * h), 0.07f * h, axleColor, -1);
        }
      }

      if (motion.size() == 3) { // only rotations restrained
        const float c = 0.3f * h;
        for (int axis = 0; axis < 3; ++axis) {
          glm::vec3 along(0.0f), u(0.0f), v(0.0f);
          along[axis] = c;
          u[(axis + 1) % 3] = c;
          v[(axis + 2) % 3] = c;
          for (const float su : {-1.0f, 1.0f}) {
            for (const float sv : {-1.0f, 1.0f}) renderer.addLine(pos - along + su * u + sv * v, pos + along + su * u + sv * v, lineColor, -1);
          }
        }
        return;
      }

      // Ground side g and the plate axes e1, e2 (e1 along the motion line, e1 / e2 in the motion
      // plane). A pin or roller stands below the node (-Y). A clamp sits behind the node, opposite
      // the members leaving it, so a cantilever comes out of a wall instead of resting on a block.
      const glm::vec3 down(0.0f, -1.0f, 0.0f);
      const bool clamped = rotationCount == 0;
      const glm::vec3 preferred = clamped && glm::length(memberDirection) > 0.3f ? -glm::normalize(memberDirection) : down;
      glm::vec3 g = preferred, e1{}, e2{};
      if (motion.empty()) {
        e1 = perpendicular(g);
        e2 = glm::cross(g, e1);
      } else if (motion.size() == 1) {
        e1 = toVec(motion[0]);
        glm::vec3 projected = preferred - e1 * glm::dot(preferred, e1);
        if (glm::length(projected) < 1e-4f) projected = down - e1 * glm::dot(down, e1);
        g = glm::length(projected) > 1e-4f ? glm::normalize(projected) : perpendicular(e1);
        e2 = glm::cross(g, e1);
      } else {
        e1 = toVec(motion[0]);
        e2 = toVec(motion[1]);
        g = glm::normalize(glm::cross(e1, e2));
        float side = glm::dot(g, preferred);
        if (std::abs(side) < 1e-4f) side = glm::dot(g, down);
        if (std::abs(side) < 1e-4f) side = glm::dot(g, glm::vec3(-1.0f, 0.0f, 0.0f));
        if (std::abs(side) < 1e-4f) side = glm::dot(g, glm::vec3(0.0f, 0.0f, -1.0f));
        if (side < 0.0f) g = -g;
      }

      // Body: a pyramid with its tip at the node. A clamp has none: its plate (the wall, or the
      // shoe on the rollers) goes through the node itself.
      const glm::vec3 base = clamped ? pos : pos + g * h;
      if (clamped && !motion.empty()) {
        const float shoe = 0.9f * w;
        const std::array<glm::vec3, 4> corner{base - e1 * shoe - e2 * shoe, base + e1 * shoe - e2 * shoe, base + e1 * shoe + e2 * shoe,
                                              base - e1 * shoe + e2 * shoe};
        quad(corner[0], corner[1], corner[2], corner[3]);
        for (std::size_t k = 0; k < 4; ++k) renderer.addLine(corner[k], corner[(k + 1) % 4], lineColor, -1);
      } else if (!clamped) {
        const std::array<glm::vec3, 4> corner{base - e1 * w - e2 * w, base + e1 * w - e2 * w, base + e1 * w + e2 * w, base - e1 * w + e2 * w};
        for (std::size_t k = 0; k < 4; ++k) {
          const std::size_t next = (k + 1) % 4;
          renderer.addTriangle(pos, corner[k], corner[next], fillColor);
          renderer.addLine(pos, corner[k], lineColor, -1);
          renderer.addLine(corner[k], corner[next], lineColor, -1);
        }
      }

      // Rollers between the body and the ground plate when the node can move.
      glm::vec3 ground = base;
      if (!motion.empty()) {
        const float roller = 0.18f * h;
        ground = base + g * (2.0f * roller);
        std::vector<glm::vec3> at;
        if (motion.size() == 1) {
          at = {base + g * roller - e1 * (0.5f * w), base + g * roller + e1 * (0.5f * w)};
        } else {
          for (const float s1 : {-0.5f, 0.5f}) {
            for (const float s2 : {-0.5f, 0.5f}) at.push_back(base + g * roller + e1 * (s1 * w) + e2 * (s2 * w));
          }
        }
        for (const auto& center : at) spheres.addSphere(center, roller, lineColor, -1);
      }

      // Ground plate and hatching (two rows, so it reads from any side).
      const float plate = 1.35f * w;
      const std::array<glm::vec3, 4> plateCorner{ground - e1 * plate - e2 * plate, ground + e1 * plate - e2 * plate,
                                                 ground + e1 * plate + e2 * plate, ground - e1 * plate + e2 * plate};
      for (std::size_t k = 0; k < 4; ++k) renderer.addLine(plateCorner[k], plateCorner[(k + 1) % 4], lineColor, -1);
      if (motion.size() == 2 || (clamped && motion.empty())) quad(plateCorner[0], plateCorner[1], plateCorner[2], plateCorner[3]);
      constexpr int strokes = 5;
      for (int k = 0; k < strokes; ++k) {
        const float t = -1.0f + 2.0f * static_cast<float>(k) / static_cast<float>(strokes - 1);
        for (const glm::vec3& along : {e1, e2}) {
          const glm::vec3 from = ground + along * (t * plate);
          renderer.addLine(from, from + g * (0.35f * h) - along * (0.3f * w), lineColor, -1);
        }
      }
      if (motion.size() == 1) { // the track direction
        const glm::vec3 a = ground + g * (0.55f * h) - e1 * (1.6f * w), b = ground + g * (0.55f * h) + e1 * (1.6f * w);
        renderer.addLine(a, b, lineColor, -1);
        for (const auto& [tip, back] : {std::pair{a, e1}, {b, -e1}}) {
          for (const glm::vec3& side : {e2, -e2, g, -g}) renderer.addLine(tip, tip + back * (0.3f * w) + side * (0.15f * w), lineColor, -1);
        }
      }
    }

    // Cone with its apex at apex, opening against direction (unit): translucent sides and an outline.
    void addCone(ViewportRenderer& renderer, const glm::vec3& apex, const glm::vec3& direction, const float length, const float radius,
                 const glm::vec4& color) {
      constexpr int segments = 10;
      const glm::vec3 helper = std::abs(direction.y) > 0.9f ? glm::vec3(1.0f, 0.0f, 0.0f) : glm::vec3(0.0f, 1.0f, 0.0f);
      const glm::vec3 u = glm::normalize(glm::cross(direction, helper)) * radius;
      const glm::vec3 v = glm::cross(direction, u);
      const glm::vec3 center = apex - direction * length;
      const glm::vec4 fill(color.r, color.g, color.b, 0.45f);
      const auto ring = [&](const int k) {
        const float angle = 2.0f * std::numbers::pi_v<float> * static_cast<float>(k) / static_cast<float>(segments);
        return center + u * std::cos(angle) + v * std::sin(angle);
      };
      for (int k = 0; k < segments; ++k) {
        renderer.addTriangle(apex, ring(k), ring(k + 1), fill);
        renderer.addTriangle(center, ring(k + 1), ring(k), fill);
        renderer.addLine(ring(k), ring(k + 1), color, -1);
        if (k % 2 == 0) renderer.addLine(apex, ring(k), color, -1);
      }
    }

    // CAD style restraints (as in Abaqus / ANSYS): every restrained direction, the orthogonal
    // complement of the allowed basis, gets its own marker pointing at the node. A translation is
    // a red cone from the -c side, a rotation an amber double cone from the +c side, so both fit
    // on one axis. c is signed so its largest component is positive (global axes stay +X / +Y / +Z).
    void addDofRestraints(ViewportRenderer& renderer, const glm::vec3& pos, const std::vector<std::array<double, 3>>& motion,
                          const std::vector<std::array<double, 3>>* rotation, const float symbol) {
      const glm::vec4 translationColor(1.0f, 0.25f, 0.25f, 1.0f);
      const glm::vec4 rotationColor(1.0f, 0.75f, 0.2f, 1.0f);
      const float length = 0.8f * symbol, radius = 0.22f * symbol;
      const auto signedAxis = [](const std::array<double, 3>& direction) {
        glm::vec3 c = toVec(direction);
        const std::size_t largest = std::abs(c.x) >= std::abs(c.y) ? (std::abs(c.x) >= std::abs(c.z) ? 0 : 2) : (std::abs(c.y) >= std::abs(c.z) ? 1 : 2);
        return c[static_cast<glm::length_t>(largest)] < 0.0f ? -c : c;
      };
      for (const auto& restrained : FEM::SUPPORT::orthogonalComplement(motion)) {
        const glm::vec3 c = signedAxis(restrained);
        addCone(renderer, pos, c, length, radius, translationColor);
        renderer.addLine(pos - c * length, pos - c * (1.5f * length), translationColor, -1);
      }
      if (!rotation) return;
      for (const auto& restrained : FEM::SUPPORT::orthogonalComplement(*rotation)) {
        const glm::vec3 c = signedAxis(restrained);
        addCone(renderer, pos, -c, 0.5f * length, radius, rotationColor);
        addCone(renderer, pos + c * (0.5f * length), -c, 0.5f * length, radius, rotationColor);
        renderer.addLine(pos + c * length, pos + c * (1.5f * length), rotationColor, -1);
      }
    }

    // Where an end release is drawn: just inside the released end (so it marks that member, not
    // the joint), with the drawn local axes there (x pointing into the member).
    struct ReleaseFrame {
      glm::vec3 center;
      glm::vec3 axisX;
      glm::vec3 axisY;
      glm::vec3 axisZ;
    };
    template <typename Draw>
    ReleaseFrame releaseFrame(const Draw& draw, const int end) {
      const std::size_t last = draw.stations.size() - 1;
      const glm::vec3 from = end == 0 ? draw.stations.front() : draw.stations.back();
      const glm::vec3 next = end == 0 ? draw.stations[1] : draw.stations[last - 1];
      const float chord = glm::length(draw.stations.back() - draw.stations.front());
      const glm::vec3 inward = glm::length(next - from) > 0.0f ? glm::normalize(next - from) : (end == 0 ? draw.axisX : -draw.axisX);
      const float inset = std::min(1.4f * draw.halfSize, 0.25f * chord);
      return {from + inward * inset, inward, end == 0 ? draw.frameY.front() : draw.frameY.back(),
              end == 0 ? draw.frameZ.front() : draw.frameZ.back()};
    }

    // Shape drawn for a section: a general section (no shape) as the rectangle with the same A,
    // Iy and Iz (h = sqrt(12 Iz / A), b = sqrt(12 Iy / A)).
    FEM::BEAM::SectionShape drawnShape(const FEM::BEAM::SectionShape& shape) {
      if (const auto* general = std::get_if<FEM::BEAM::GeneralSection>(&shape)) {
        const auto& p = general->values;
        return FEM::BEAM::RectangleSection{std::sqrt(12.0 * p.secondMomentZ / p.area), std::sqrt(12.0 * p.secondMomentY / p.area)};
      }
      return shape;
    }

    // Largest |y| and |z| of the outline.
    std::array<double, 2> outlineExtent(const FEM::BEAM::SectionShape& shape) {
      std::array<double, 2> extent{0.0, 0.0};
      for (const auto& loop : FEM::BEAM::sectionOutline(shape, kSectionSegmentsPerQuarter)) {
        for (const auto& p : loop) {
          extent[0] = std::max(extent[0], std::abs(p[0]));
          extent[1] = std::max(extent[1], std::abs(p[1]));
        }
      }
      return extent;
    }

    // The section extruded over x = 0..1: side walls (normals smoothed across gentle corners,
    // so arcs look round and sharp corners stay sharp) and both end caps.
    std::vector<MeshVertex> extrudeSection(const FEM::BEAM::SectionShape& shape, const int segmentsPerQuarter) {
      std::vector<MeshVertex> vertices;
      const float smoothCos = std::cos(40.0f * std::numbers::pi_v<float> / 180.0f);
      for (const auto& loop : FEM::BEAM::sectionOutline(shape, segmentsPerQuarter)) {
        const std::size_t n = loop.size();
        if (n < 3) continue;
        // Outward normal of edge i (loop i -> i + 1) in (y, z); loops are counter-clockwise in
        // the (z, y) plane and holes clockwise, so the same formula points out of the material.
        std::vector<glm::vec2> edgeNormal(n);
        for (std::size_t i = 0; i < n; ++i) {
          const auto& a = loop[i];
          const auto& b = loop[(i + 1) % n];
          const glm::vec2 normal(static_cast<float>(-(b[1] - a[1])), static_cast<float>(b[0] - a[0]));
          const float length = glm::length(normal);
          edgeNormal[i] = length > 0.0f ? normal / length : glm::vec2(0.0f);
        }
        const auto vertexNormal = [&](const std::size_t vertex, const std::size_t edge) {
          const glm::vec2 before = edgeNormal[(vertex + n - 1) % n];
          const glm::vec2 after = edgeNormal[vertex % n];
          if (glm::dot(before, after) < smoothCos) return edgeNormal[edge];
          const glm::vec2 sum = before + after;
          return glm::length(sum) > 0.0f ? glm::normalize(sum) : edgeNormal[edge];
        };
        for (std::size_t i = 0; i < n; ++i) {
          const std::size_t j = (i + 1) % n;
          const glm::vec2 na = vertexNormal(i, i), nb = vertexNormal(j, i);
          const auto at = [&](const std::size_t k, const float x, const glm::vec2 normal) {
            return MeshVertex{glm::vec3(x, static_cast<float>(loop[k][0]), static_cast<float>(loop[k][1])), glm::vec3(0.0f, normal.x, normal.y)};
          };
          vertices.insert(vertices.end(), {at(i, 0.0f, na), at(j, 0.0f, nb), at(j, 1.0f, nb), at(i, 0.0f, na), at(j, 1.0f, nb), at(i, 1.0f, na)});
        }
      }
      const auto faces = FEM::BEAM::triangulateSection(shape, segmentsPerQuarter);
      for (const float x : {0.0f, 1.0f}) {
        const glm::vec3 normal(x == 0.0f ? -1.0f : 1.0f, 0.0f, 0.0f);
        for (const auto& t : faces.triangles) {
          for (const auto k : t) {
            vertices.push_back({glm::vec3(x, static_cast<float>(faces.points[k][0]), static_cast<float>(faces.points[k][1])), normal});
          }
        }
      }
      return vertices;
    }
  } // namespace end

  bool ViewportPanel::hasModel() const {
    return (m_currentBeamMesh && !m_currentBeamMesh->nodes.empty()) || (m_currentMesh && !m_currentMesh->trussNodes.empty());
  }

  void ViewportPanel::updateSceneBounds() {
    m_sceneCenter = glm::vec3(0.0f);
    m_sceneRadius = 10.0f;
    if (!hasModel()) return;

    glm::vec3 boundsMin(std::numeric_limits<float>::max());
    glm::vec3 boundsMax(std::numeric_limits<float>::lowest());
    const auto include = [&](const std::array<double, 3>& location) {
      boundsMin = glm::min(boundsMin, toVec(location));
      boundsMax = glm::max(boundsMax, toVec(location));
    };
    if (m_currentBeamMesh) {
      for (const auto& node : m_currentBeamMesh->nodes) include(node.getLocation());
    } else {
      for (const auto& node : m_currentMesh->trussNodes) include(node.getLocation());
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
    m_cameraDistance = hasModel() ? m_sceneRadius * 2.2f : 18.0f;
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
    m_beamRenderer_->clearSpheres();
    m_nodeLabels.clear();

    // Coordinate axes X, Y, Z (only with the axes toggle on), extended far past the camera's far clip plane so they appear infinite (EntityID = -1)
    // farPlane() stays below ~100 scene radii at the widest zoom, so 200 radii look infinite.
    if (m_display->showAxes) {
      const float axisReach = std::max(8000.0f, m_sceneRadius * 200.0f);
      m_renderer_->addLine(glm::vec3(-axisReach, 0.0f, 0.0f), glm::vec3(axisReach, 0.0f, 0.0f), glm::vec4(1.0f, 0.2f, 0.2f, 1.0f), -1);
      m_renderer_->addLine(glm::vec3(0.0f, -axisReach, 0.0f), glm::vec3(0.0f, axisReach, 0.0f), glm::vec4(0.2f, 1.0f, 0.2f, 1.0f), -1);
      m_renderer_->addLine(glm::vec3(0.0f, 0.0f, -axisReach), glm::vec3(0.0f, 0.0f, axisReach), glm::vec4(0.2f, 0.4f, 1.0f, 1.0f), -1);
    }

    if (m_currentBeamMesh) {
      buildBeamScene();
    } else {
      m_beamRenderer_->clearInstances();
      buildTrussScene();
    }
    m_renderer_->uploadCurrentBuffer();
    m_beamRenderer_->upload();
  }

  void ViewportPanel::buildTrussScene() {
    if (!m_currentMesh || m_currentMesh->trussNodes.empty()) {
      m_cachedMaxStress = 0.0;
      m_cachedMaxDisp = 0.0;
      return;
    }

    const auto& mesh = *m_currentMesh;
    const std::uint32_t selectedId = m_selectedNode;

    m_renderer_->reserve(3 + mesh.trussElements.size() + mesh.appliedForces.size() * 3, mesh.trussNodes.size());

    double maxStress = 0.0;
    for (const auto& element : mesh.trussElements) {
      maxStress = std::max(maxStress, std::abs(static_cast<double>(element.stress)));
    }
    m_cachedMaxStress = maxStress;

    const double deformScale = m_deformScale;

    uint32_t maxNodeId = 0;
    double maxDisp = 0.0;
    for (const auto& node : mesh.trussNodes) {
      maxNodeId = std::max(maxNodeId, node.getNodeID());
      maxDisp = std::max(maxDisp, magnitude(node.getDisplacement()));
    }
    m_cachedMaxDisp = maxDisp;

    // Without coloring (or results) the elements keep the color of an unsolved model.
    const auto coloring = m_display->coloring;
    const auto elementColor = [&](const BRIDGE::RenderElement& element) -> glm::vec4 {
      if (coloring == ElementColoring::Stress && maxStress > 1e-9) {
        return jet(std::sqrt(std::clamp(std::abs(static_cast<double>(element.stress)) / maxStress, 0.0, 1.0)));
      }
      if (coloring == ElementColoring::Displacement && maxDisp > 0.0) {
        const double mean = 0.5 * (magnitude(mesh.trussNodes[element.node1].getDisplacement()) + magnitude(mesh.trussNodes[element.node2].getDisplacement()));
        return jet(mean / maxDisp);
      }
      return glm::vec4(0.4f, 0.6f, 0.85f, 1.0f);
    };

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
        m_renderer_->addLine(nodeLookup[element.node1], nodeLookup[element.node2], elementColor(element), -1);
      }
    }

    // Interactive Nodes: squares (points in the FBO) or spheres
    if (m_display->showNodes()) {
      auto displacementColor = [&](double value) -> glm::vec4 {
        const double t = (maxDisp > 0.0) ? std::clamp(value / maxDisp, 0.0, 1.0) : 0.0;
        float r = static_cast<float>(t);
        float g = static_cast<float>(1.0 - std::abs(t - 0.5) * 2.0);
        float b = static_cast<float>(1.0 - t);
        return glm::vec4(r, g, b, 1.0f);
      };
      const float sphereRadius = std::max(m_sceneRadius * 0.012f, 1e-4f);

      for (const auto& node : mesh.trussNodes) {
        const uint32_t id = node.getNodeID();
        const glm::vec3& pos = nodeLookup[id];

        glm::vec4 pColor = displacementColor(magnitude(node.getDisplacement()));
        float pSize = 12.0f;

        if (id == selectedId) {
          pColor = kSelectedColor;
          pSize = 18.0f;
        } else if (node.isSupported()) {
          pColor = kSupportColor;
        }

        if (m_display->nodeStyle == NodeStyle::Sphere) {
          m_beamRenderer_->addSphere(pos, sphereRadius * (id == selectedId ? 1.4f : 1.0f), pColor, static_cast<int>(id));
        } else {
          m_renderer_->addPoint(pos, pColor, static_cast<int>(id), pSize);
        }
        m_nodeLabels.emplace_back(id, pos);
      }
    }

    // Inclined / skewed supports at the drawn node position. Sized from the model, so they stay
    // readable on a 1 m mount and on a 300 m stadium.
    // Symbols / DOF draw every support, the inclined ones included.
    const float symbol = std::max(m_sceneRadius * 0.04f, 1e-3f);
    for (const auto& node : mesh.trussNodes) {
      const glm::vec3& pos = nodeLookup[node.getNodeID()];
      if (m_display->supportStyle == SupportStyle::Symbols) {
        addSupportSymbol(*m_renderer_, *m_beamRenderer_, pos, node.getAllowedMotionDirections(), nullptr, glm::vec3(0.0f), symbol);
      } else if (m_display->supportStyle == SupportStyle::Dof) {
        if (node.isSupported()) addDofRestraints(*m_renderer_, pos, node.getAllowedMotionDirections(), nullptr, symbol);
      } else if (node.hasInclinedSupport()) {
        addInclinedSupport(*m_renderer_, pos, node.getAllowedMotionDirections(), symbol);
      }
    }

    // Force Arrows (Lines in FBO)
    if (!m_display->showForces) return;
    const glm::vec4 forceArrowColor(1.0f, 0.25f, 0.25f, 1.0f);
    const glm::vec4 forceGlowColor(1.0f, 0.3f, 0.3f, 0.35f);
    for (const auto& force : mesh.appliedForces) {
      const uint32_t targetId = force.getAppliedNode();
      if (targetId > maxNodeId) continue;
      const auto forceVec = force.getForce();
      if (magnitude(forceVec) < 1e-6) continue;
      const glm::vec3 basePos = nodeLookup[targetId];
      addArrow(*m_renderer_, basePos, basePos + glm::normalize(toVec(forceVec)) * 3.0f, forceArrowColor, forceGlowColor, 0.1f, 0.05f);
    }
  }

  void ViewportPanel::buildBeamStations() {
    m_beamRenderer_->clearMeshes();
    m_beamElements.clear();
    m_lineMesh = -1;
    m_stationsMesh = m_currentBeamMesh;
    m_stationsScale = -1.0; // applyBeamDeformation() follows
    m_cachedMaxStress = 0.0;
    m_cachedMaxDisp = 0.0;
    if (!m_currentBeamMesh) return;
    const auto& mesh = *m_currentBeamMesh;

    auto& bridge = BRIDGE::buildBridge();
    std::vector<MATERIAL::Material> materials;
    std::vector<FEM::BEAM::BeamSection> sections;
    {
      std::lock_guard lock(bridge.dataMutex);
      materials = bridge.allMaterials;
      sections = bridge.allSections;
    }
    m_lodActive = mesh.elements.size() > kLodElementThreshold;

    const std::array<MeshVertex, 2> line{MeshVertex{glm::vec3(0.0f), glm::vec3(0.0f)}, MeshVertex{glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(0.0f)}};
    m_lineMesh = m_beamRenderer_->addMesh(line, GL_LINES);

    // One full and one simple mesh per section in use.
    std::vector<int> fullOf(sections.size(), -1), simpleOf(sections.size(), -1), pinOf(sections.size(), -1), collarOf(sections.size(), -1);
    std::vector<float> halfOf(sections.size(), 0.0f);
    for (const auto& element : mesh.elements) {
      const auto id = element.sectionID;
      if (id >= sections.size() || fullOf[id] >= 0) continue;
      const auto shape = drawnShape(sections[id].getShape());
      if (!FEM::BEAM::validateShape(shape)) continue;
      fullOf[id] = m_beamRenderer_->addMesh(extrudeSection(shape, kSectionSegmentsPerQuarter), GL_TRIANGLES);
      const auto extent = outlineExtent(shape);
      const bool round = std::holds_alternative<FEM::BEAM::CircleSection>(shape) || std::holds_alternative<FEM::BEAM::PipeSection>(shape);
      const FEM::BEAM::SectionShape simple = round ? FEM::BEAM::SectionShape{FEM::BEAM::CircleSection{2.0 * std::max(extent[0], extent[1])}}
                                                   : FEM::BEAM::SectionShape{FEM::BEAM::RectangleSection{2.0 * extent[0], 2.0 * extent[1]}};
      simpleOf[id] = m_beamRenderer_->addMesh(extrudeSection(simple, 2), GL_TRIANGLES);
      halfOf[id] = static_cast<float>(std::max(extent[0], extent[1]));
      const double half = halfOf[id];
      pinOf[id] = m_beamRenderer_->addMesh(extrudeSection(FEM::BEAM::CircleSection{0.4 * half}, 3), GL_TRIANGLES);
      collarOf[id] = m_beamRenderer_->addMesh(extrudeSection(FEM::BEAM::PipeSection{2.5 * half, 0.2 * half}, 4), GL_TRIANGLES);
    }

    // The exact displacement field along each element (beamDiagrams), when there are results.
    bool sampled = mesh.hasResults;
    std::vector<Eigen::Vector3d> loads;
    if (sampled) {
      try {
        const auto properties = FEM::BEAM::elementSectionProperties(mesh.elements, sections, materials);
        loads = FEM::BEAM::elementLocalLoads(mesh.nodes, mesh.elements, properties, mesh.distributedLoads, mesh.gravity, materials);
      } catch (const std::exception&) {
        sampled = false; // the lists changed after the solve: straight elements
      }
    }
    const std::size_t samples = m_lodActive ? 5 : 9;

    m_beamElements.resize(mesh.elements.size());
    const auto count = static_cast<long long>(mesh.elements.size());
    #pragma omp parallel for schedule(dynamic, 64)
    for (long long index = 0; index < count; ++index) {
      auto& draw = m_beamElements[static_cast<std::size_t>(index)];
      const auto& element = mesh.elements[static_cast<std::size_t>(index)];
      if (element.sectionID >= sections.size() || fullOf[element.sectionID] < 0 || element.node1 >= mesh.nodes.size()
          || element.node2 >= mesh.nodes.size()) {
        continue;
      }
      try {
        const auto& a = mesh.nodes[element.node1];
        const auto& b = mesh.nodes[element.node2];
        const auto axes = FEM::BEAM::localAxes(a.getLocation(), b.getLocation(), element.orientation);
        draw.axisX = glm::vec3(static_cast<float>(axes(0, 0)), static_cast<float>(axes(0, 1)), static_cast<float>(axes(0, 2)));
        draw.axisY = glm::vec3(static_cast<float>(axes(1, 0)), static_cast<float>(axes(1, 1)), static_cast<float>(axes(1, 2)));
        draw.axisZ = glm::vec3(static_cast<float>(axes(2, 0)), static_cast<float>(axes(2, 1)), static_cast<float>(axes(2, 2)));
        const auto local = [&](const std::array<double, 3>& v) {
          const glm::vec3 g = toVec(v);
          return glm::vec3(glm::dot(draw.axisX, g), glm::dot(draw.axisY, g), glm::dot(draw.axisZ, g));
        };
        draw.rotation0 = local(a.getRotation());
        draw.rotation1 = local(b.getRotation());
        if (sampled && element.endReleases != 0) { // a hinged end turns on its own
          const auto ends = FEM::BEAM::elementEndDisplacements(mesh, static_cast<std::size_t>(index), loads[static_cast<std::size_t>(index)],
                                                               materials, sections);
          draw.rotation0 = glm::vec3(static_cast<float>(ends[3]), static_cast<float>(ends[4]), static_cast<float>(ends[5]));
          draw.rotation1 = glm::vec3(static_cast<float>(ends[9]), static_cast<float>(ends[10]), static_cast<float>(ends[11]));
        }
        draw.fullMesh = fullOf[element.sectionID];
        draw.simpleMesh = simpleOf[element.sectionID];
        draw.pinMesh = pinOf[element.sectionID];
        draw.collarMesh = collarOf[element.sectionID];
        draw.halfSize = halfOf[element.sectionID];
        draw.releases = element.endReleases;
        if (sampled) {
          const auto states = FEM::BEAM::sampleElement(mesh, static_cast<std::size_t>(index), samples, loads[static_cast<std::size_t>(index)],
                                                       materials, sections);
          const auto& shape = sections[element.sectionID].getShape();
          const double length = magnitude({b.getLocation()[0] - a.getLocation()[0], b.getLocation()[1] - a.getLocation()[1],
                                           b.getLocation()[2] - a.getLocation()[2]});
          for (const auto& state : states) {
            const auto& d = state.displacement;
            draw.base.push_back(toVec(state.location));
            draw.offset.push_back(toVec(d));
            draw.xi.push_back(static_cast<float>(state.position / length));
            draw.displacement.push_back(static_cast<float>(magnitude(d)));
            if (const auto stress = FEM::BEAM::sectionStress(shape, state.forces)) draw.stress.push_back(static_cast<float>(stress->vonMises));
          }
          if (draw.stress.size() != draw.base.size()) draw.stress.clear();
        } else {
          draw.base = {toVec(a.getLocation()), toVec(b.getLocation())};
          draw.xi = {0.0f, 1.0f};
          if (mesh.hasResults) draw.offset = {toVec(a.getDisplacement()), toVec(b.getDisplacement())};
        }
      } catch (const std::exception&) {
        draw = BeamDrawElement{}; // not drawable (e.g. an orientation parallel to the axis)
      }
    }

    for (const auto& draw : m_beamElements) {
      for (const float s : draw.stress) m_cachedMaxStress = std::max(m_cachedMaxStress, static_cast<double>(s));
      for (const float d : draw.displacement) m_cachedMaxDisp = std::max(m_cachedMaxDisp, static_cast<double>(d));
    }
    for (const auto& node : mesh.nodes) m_cachedMaxDisp = std::max(m_cachedMaxDisp, magnitude(node.getDisplacement()));
  }

  void ViewportPanel::applyBeamDeformation() {
    m_stationsScale = m_deformScale;
    const auto scale = static_cast<float>(m_deformScale);
    const auto count = static_cast<long long>(m_beamElements.size());
    #pragma omp parallel for schedule(static)
    for (long long index = 0; index < count; ++index) {
      auto& draw = m_beamElements[static_cast<std::size_t>(index)];
      const std::size_t n = draw.base.size();
      draw.stations.resize(n);
      draw.frameY.resize(n);
      draw.frameZ.resize(n);
      for (std::size_t i = 0; i < n; ++i) draw.stations[i] = draw.base[i] + (draw.offset.empty() ? glm::vec3(0.0f) : draw.offset[i] * scale);
      for (std::size_t i = 0; i < n; ++i) {
        // Twist: linear between the nodes (no distributed torque). Bending: the section stays
        // normal to the drawn axis; at the nodes the exact nodal rotation gives the tangent
        // (x' = x + theta x x in local axes), inside a central difference of the stations.
        const float xi = draw.xi.empty() ? 0.0f : draw.xi[i];
        const glm::vec3 rotation = (1.0f - xi) * draw.rotation0 + xi * draw.rotation1;
        glm::vec3 tangent = draw.axisX;
        if (draw.offset.empty()) {
          tangent = draw.axisX;
        } else if (i == 0 || i + 1 == n) {
          const glm::vec3 r = i == 0 ? draw.rotation0 : draw.rotation1;
          tangent = draw.axisX + scale * (r.z * draw.axisY - r.y * draw.axisZ);
        } else {
          tangent = draw.stations[i + 1] - draw.stations[i - 1];
        }
        tangent = glm::length(tangent) > 0.0f ? glm::normalize(tangent) : draw.axisX;
        const float twist = draw.offset.empty() ? 0.0f : rotation.x * scale;
        glm::vec3 y = std::cos(twist) * draw.axisY + std::sin(twist) * draw.axisZ;
        glm::vec3 z = -std::sin(twist) * draw.axisY + std::cos(twist) * draw.axisZ;
        // Smallest rotation that turns the undeformed axis into the tangent (Rodrigues).
        const glm::vec3 k = glm::cross(draw.axisX, tangent);
        const float sine = glm::length(k);
        if (sine > 1e-7f) {
          const glm::vec3 unit = k / sine;
          const float cosine = glm::dot(draw.axisX, tangent);
          const auto turn = [&](const glm::vec3& v) {
            return v * cosine + glm::cross(unit, v) * sine + unit * glm::dot(unit, v) * (1.0f - cosine);
          };
          y = turn(y);
          z = turn(z);
        }
        draw.frameY[i] = glm::normalize(y);
        draw.frameZ[i] = glm::normalize(z);
      }
    }
  }

  void ViewportPanel::buildBeamScene() {
    if (m_stationsMesh != m_currentBeamMesh) buildBeamStations();
    if (m_stationsScale != m_deformScale) applyBeamDeformation();
    const auto& mesh = *m_currentBeamMesh;
    const float scale = static_cast<float>(m_deformScale);

    std::vector<glm::vec3> position(mesh.nodes.size());
    for (std::size_t i = 0; i < mesh.nodes.size(); ++i) {
      position[i] = toVec(mesh.nodes[i].getLocation()) + toVec(mesh.nodes[i].getDisplacement()) * scale;
    }

    // Nodes: a sphere is 1.3 times the largest section half size at the node, so it shows
    // around the elements. A square is lifted toward the eye by 1.2 times that half size (the
    // outline's farthest point from the axis, corners included), so only other members hide it.
    if (m_display->showNodes()) {
      std::vector<float> radius(mesh.nodes.size(), std::max(m_sceneRadius * 0.01f, 1e-4f));
      std::vector<float> lift(mesh.nodes.size(), 0.0f);
      std::vector<bool> connected(mesh.nodes.size(), false);
      for (std::size_t e = 0; e < mesh.elements.size() && e < m_beamElements.size(); ++e) {
        const auto& element = mesh.elements[e];
        for (const auto node : {element.node1, element.node2}) {
          if (node >= radius.size()) continue;
          radius[node] = connected[node] ? std::max(radius[node], 1.3f * m_beamElements[e].halfSize) : 1.3f * m_beamElements[e].halfSize;
          lift[node] = std::max(lift[node], 1.2f * m_beamElements[e].halfSize);
          connected[node] = true;
        }
      }
      for (std::uint32_t id = 0; id < mesh.nodes.size(); ++id) {
        const auto& node = mesh.nodes[id];
        glm::vec4 color = m_cachedMaxDisp > 0.0 ? jet(magnitude(node.getDisplacement()) / m_cachedMaxDisp) : glm::vec4(0.85f, 0.87f, 0.9f, 1.0f);
        if (node.isSupported()) color = kSupportColor;
        const bool selected = id == m_selectedNode;
        if (selected) color = kSelectedColor;
        if (m_display->nodeStyle == NodeStyle::Sphere) {
          m_beamRenderer_->addSphere(position[id], std::max(radius[id], 1e-4f) * (selected ? 1.25f : 1.0f), color, static_cast<int>(id));
        } else {
          m_renderer_->addPoint(position[id], color, static_cast<int>(id), selected ? 18.0f : 12.0f, lift[id]);
        }
        m_nodeLabels.emplace_back(id, position[id]);
      }
    }

    // End releases of a translation (a slot or slider at the member end): a light double arrow
    // along the released local axis. Rotation releases are pins and collars (pushBeamInstances()).
    for (std::size_t e = 0; e < m_beamElements.size(); ++e) {
      const auto& draw = m_beamElements[e];
      if (draw.releases == 0 || draw.stations.size() < 2) continue;
      const glm::vec4 color = e == m_selectedElement ? kSelectedColor : kHingeColor;
      const glm::vec4 glow(color.r, color.g, color.b, 0.35f);
      for (int end = 0; end < 2; ++end) {
        const auto bits = FEM::BEAM::RELEASE::ofEnd(draw.releases, end);
        const auto at = releaseFrame(draw, end);
        const float reach = 1.6f * draw.halfSize;
        for (const auto& [bit, axis] : {std::pair{FEM::BEAM::RELEASE::axial, at.axisX}, {FEM::BEAM::RELEASE::shearY, at.axisY},
                                        {FEM::BEAM::RELEASE::shearZ, at.axisZ}}) {
          if ((bits & bit) == 0) continue;
          const float head = 0.35f * draw.halfSize;
          addArrow(*m_renderer_, at.center, at.center + axis * reach, color, glow, head, 0.4f * head);
          addArrow(*m_renderer_, at.center, at.center - axis * reach, color, glow, head, 0.4f * head);
        }
      }
    }

    const float symbol = std::max(m_sceneRadius * 0.04f, 1e-3f);
    if (m_display->supportStyle != SupportStyle::Off) {
      // At least three times the largest section half size at the node, so the symbol shows
      // around the members.
      std::vector<float> halfSize(mesh.nodes.size(), 0.0f);
      std::vector<glm::vec3> memberDirection(mesh.nodes.size(), glm::vec3(0.0f));
      for (std::size_t e = 0; e < mesh.elements.size() && e < m_beamElements.size(); ++e) {
        const auto& element = mesh.elements[e];
        if (element.node1 >= mesh.nodes.size() || element.node2 >= mesh.nodes.size()) continue;
        const glm::vec3 chord = toVec(mesh.nodes[element.node2].getLocation()) - toVec(mesh.nodes[element.node1].getLocation());
        if (glm::length(chord) <= 0.0f) continue;
        const glm::vec3 unit = glm::normalize(chord);
        memberDirection[element.node1] += unit;
        memberDirection[element.node2] -= unit;
        for (const auto node : {element.node1, element.node2}) halfSize[node] = std::max(halfSize[node], m_beamElements[e].halfSize);
      }
      for (std::uint32_t id = 0; id < mesh.nodes.size(); ++id) {
        const auto& node = mesh.nodes[id];
        if (!node.isSupported()) continue;
        const float size = std::max(symbol, 3.0f * halfSize[id]);
        if (m_display->supportStyle == SupportStyle::Dof) {
          addDofRestraints(*m_renderer_, position[id], node.getAllowedMotionDirections(), &node.getAllowedRotationAxes(), size);
        } else {
          addSupportSymbol(*m_renderer_, *m_beamRenderer_, position[id], node.getAllowedMotionDirections(), &node.getAllowedRotationAxes(),
                           memberDirection[id], size);
        }
      }
    } else {
      for (std::uint32_t id = 0; id < mesh.nodes.size(); ++id) {
        const auto& motion = mesh.nodes[id].getAllowedMotionDirections();
        if (!alongGlobalAxes(motion)) addInclinedSupport(*m_renderer_, position[id], motion, symbol);
      }
    }

    if (m_display->showForces) {
      const float arrow = std::max(m_sceneRadius * 0.15f, 1e-3f);
      const glm::vec4 forceColor(1.0f, 0.25f, 0.25f, 1.0f), forceGlow(1.0f, 0.3f, 0.3f, 0.35f);
      const glm::vec4 momentColor(1.0f, 0.45f, 0.85f, 1.0f), momentGlow(1.0f, 0.45f, 0.85f, 0.35f);
      const glm::vec4 lineLoadColor(1.0f, 0.62f, 0.2f, 1.0f), lineLoadGlow(1.0f, 0.62f, 0.2f, 0.3f);
      for (const auto& load : mesh.nodalLoads) {
        if (load.node >= position.size()) continue;
        const glm::vec3 base = position[load.node];
        if (magnitude(load.force) > 1e-9) {
          addArrow(*m_renderer_, base, base + glm::normalize(toVec(load.force)) * arrow, forceColor, forceGlow, arrow * 0.12f, arrow * 0.05f);
        }
        if (magnitude(load.moment) > 1e-9) { // moment vector: double head
          const glm::vec3 dir = glm::normalize(toVec(load.moment));
          const glm::vec3 tip = base + dir * arrow;
          addArrow(*m_renderer_, base, tip, momentColor, momentGlow, arrow * 0.12f, arrow * 0.05f);
          addArrow(*m_renderer_, base, tip - dir * (arrow * 0.12f), momentColor, momentGlow, arrow * 0.12f, arrow * 0.05f);
        }
      }
      // Uniform loads: arrows pointing at the element, tails joined.
      const float small = arrow * 0.45f;
      for (const auto& load : mesh.distributedLoads) {
        if (load.element >= m_beamElements.size() || m_beamElements[load.element].stations.size() < 2) continue;
        const auto& draw = m_beamElements[load.element];
        glm::vec3 direction = toVec(load.value);
        if (load.frame == FEM::BEAM::LoadFrame::Local) {
          const glm::vec3 axisX = glm::normalize(draw.stations.back() - draw.stations.front());
          direction = axisX * direction.x + draw.axisY * direction.y + draw.axisZ * direction.z;
        }
        if (glm::length(direction) < 1e-12f) continue;
        direction = glm::normalize(direction);
        glm::vec3 previousTail{};
        constexpr int arrows = 5;
        for (int k = 0; k < arrows; ++k) {
          const float t = static_cast<float>(k) / static_cast<float>(arrows - 1);
          const auto stationIndex = static_cast<std::size_t>(std::lround(t * static_cast<float>(draw.stations.size() - 1)));
          const glm::vec3 at = draw.stations[stationIndex];
          const glm::vec3 tail = at - direction * small;
          addArrow(*m_renderer_, tail, at, lineLoadColor, lineLoadGlow, small * 0.2f, small * 0.08f);
          if (k > 0) m_renderer_->addLine(previousTail, tail, lineLoadColor, -1);
          previousTail = tail;
        }
      }
    }

    pushBeamInstances(getViewProjectionMatrix());
  }

  void ViewportPanel::pushBeamInstances(const glm::mat4& mvp) {
    m_beamRenderer_->clearInstances();
    m_lodMatrix = mvp;
    m_lodViewport = m_viewportSize;
    const glm::vec3 eye = m_target + orbitDirection() * m_cameraDistance;
    const float focal = std::max(m_viewportSize.y, 1.0f) / (2.0f * std::tan(kFovY * 0.5f));
    const auto coloring = m_display->coloring;
    const double maxStress = m_cachedMaxStress, maxDisp = m_cachedMaxDisp;

    for (std::size_t e = 0; e < m_beamElements.size(); ++e) {
      const auto& draw = m_beamElements[e];
      if (draw.stations.size() < 2 || draw.fullMesh < 0) continue;
      const bool selected = e == m_selectedElement;
      const auto color = [&](const std::size_t i) -> glm::vec4 {
        if (selected) return kSelectedColor;
        if (coloring == ElementColoring::Stress) {
          if (draw.stress.empty() || maxStress <= 0.0) return draw.stress.empty() && !draw.displacement.empty() ? kNoStressColor : kBeamColor;
          return jet(draw.stress[i] / maxStress);
        }
        if (coloring == ElementColoring::Displacement && !draw.displacement.empty() && maxDisp > 0.0) return jet(draw.displacement[i] / maxDisp);
        return kBeamColor;
      };
      const int entity = -static_cast<int>(e) - 2;
      const std::size_t last = draw.stations.size() - 1;

      enum class Tier { Full, Simple, Line } tier = Tier::Full;
      if (m_lodActive && !selected) {
        const float distance = std::max(glm::length(0.5f * (draw.stations.front() + draw.stations.back()) - eye), 1e-6f);
        const float pixels = 2.0f * draw.halfSize * focal / distance;
        tier = pixels >= kFullSectionPixels ? Tier::Full : (pixels >= kSimpleSectionPixels ? Tier::Simple : Tier::Line);
      }
      if (tier == Tier::Full) {
        for (std::size_t i = 0; i < last; ++i) {
          m_beamRenderer_->addInstance(draw.fullMesh, {draw.stations[i], draw.stations[i + 1], draw.frameY[i], draw.frameZ[i], draw.frameY[i + 1],
                                                       draw.frameZ[i + 1], color(i), color(i + 1), entity});
        }
      } else {
        const int meshIndex = tier == Tier::Simple ? draw.simpleMesh : m_lineMesh;
        m_beamRenderer_->addInstance(meshIndex, {draw.stations.front(), draw.stations.back(), draw.frameY.front(), draw.frameZ.front(),
                                                 draw.frameY.back(), draw.frameZ.back(), color(0), color(last), entity});
      }

      // End releases of a rotation: a pin through the end along each released bending axis
      // (local y / z; both = crossed pins, a universal joint), a collar around the member when
      // the twist is released. Lit like the members, light grey, and they pick the element.
      if (draw.releases == 0 || tier == Tier::Line || draw.pinMesh < 0) continue;
      const glm::vec4 pinColor = selected ? kSelectedColor : kHingeColor;
      for (int end = 0; end < 2; ++end) {
        const auto bits = FEM::BEAM::RELEASE::ofEnd(draw.releases, end);
        if ((bits & (FEM::BEAM::RELEASE::torsion | FEM::BEAM::RELEASE::hinge)) == 0) continue;
        const auto at = releaseFrame(draw, end);
        const float pin = 1.35f * draw.halfSize;
        for (const auto& [bit, axis] : {std::pair{FEM::BEAM::RELEASE::momentY, at.axisY}, {FEM::BEAM::RELEASE::momentZ, at.axisZ}}) {
          if ((bits & bit) == 0) continue;
          const glm::vec3 side = glm::normalize(glm::cross(axis, at.axisX));
          m_beamRenderer_->addInstance(draw.pinMesh, {at.center - axis * pin, at.center + axis * pin, at.axisX, side, at.axisX, side,
                                                      pinColor, pinColor, entity});
        }
        if ((bits & FEM::BEAM::RELEASE::torsion) != 0) {
          const float band = 0.12f * draw.halfSize;
          m_beamRenderer_->addInstance(draw.collarMesh, {at.center - at.axisX * band, at.center + at.axisX * band, at.axisY, at.axisZ,
                                                         at.axisY, at.axisZ, pinColor, pinColor, entity});
        }
      }
    }
    m_beamRenderer_->upload();
  }

  void ViewportPanel::renderSceneOpenGL() {
    if (!m_fbo_) return;

    auto& bridge = BRIDGE::buildBridge();
    const uint64_t currentVersion = bridge.dataVersion.load(std::memory_order_acquire);

    // Toolbar requests from the previous ImGui frame.
    if (m_display->changed) {
      m_display->changed = false;
      truss_1d_gui_prop.m_meshNeedsUpdate = true;
    }
    if (m_display->resetCameraRequested) {
      m_display->resetCameraRequested = false;
      resetCamera();
    }
    // A selection made in a panel (or by picking) redraws the highlight.
    {
      std::lock_guard<std::mutex> lock(bridge.dataMutex);
      if (bridge.selectedNodeId != m_selectedNode || bridge.selectedElementId != m_selectedElement) {
        m_selectedNode = bridge.selectedNodeId;
        m_selectedElement = bridge.selectedElementId;
        truss_1d_gui_prop.m_meshNeedsUpdate = true;
      }
    }

    if (truss_1d_gui_prop.m_meshNeedsUpdate || currentVersion != truss_1d_gui_prop.m_lastRenderedVersion) {
      {
        std::lock_guard<std::mutex> lock(bridge.dataMutex);
        m_currentMesh = bridge.activeMesh;
        m_currentBeamMesh = bridge.activeBeamMesh;
      }
      m_deformScale = bridge.deformScale.load();

      truss_1d_gui_prop.m_meshNeedsUpdate = false;
      truss_1d_gui_prop.m_lastRenderedVersion = currentVersion;
      if (m_fitRequested_ && hasModel()) {
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
    // Level of detail follows the camera: only the instance lists are rebuilt.
    if (m_currentBeamMesh && m_lodActive && (mvp != m_lodMatrix || m_viewportSize.x != m_lodViewport.x || m_viewportSize.y != m_lodViewport.y)) {
      pushBeamInstances(mvp);
    }
    if (m_display->showGrid) {
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
    m_beamRenderer_->render(mvp, -orbitDirection());
    {
      const glm::vec3 eye = m_target + orbitDirection() * m_cameraDistance;
      const float fbHeight = static_cast<float>(std::max(m_fbo_->getHeight(), 1u));
      m_renderer_->render(mvp, eye, 2.0f * std::tan(kFovY * 0.5f) / fbHeight);
    }

    // Node number labels, rendered as OpenGL glyph quads (ImGui font atlas) instead of an ImGui 2D overlay.
    m_renderer_->clearTextBuffer();
    if (m_display->showNodes()) {
      const float fbWidth = static_cast<float>(m_fbo_->getWidth());
      const float fbHeight = static_cast<float>(m_fbo_->getHeight());
      for (const auto& [id, worldPos] : m_nodeLabels) {
        if (m_cameraDistance >= 15.0f && id != m_selectedNode) continue;
        const glm::vec4 clipPos = mvp * glm::vec4(worldPos, 1.0f);
        if (clipPos.w <= 0.1f) continue;
        const glm::vec3 ndc = glm::vec3(clipPos) / clipPos.w;
        const float screenX = (ndc.x * 0.5f + 0.5f) * fbWidth + 8.0f;
        const float screenY = (-ndc.y * 0.5f + 0.5f) * fbHeight - 8.0f;
        m_renderer_->addText(glm::vec2(screenX, screenY), std::to_string(id), glm::vec4(0.9f, 0.9f, 0.9f, 1.0f), fbWidth, fbHeight);
      }
    }
    m_renderer_->uploadTextBuffer();
    m_renderer_->renderText();

    m_fbo_->unbind();
  }

  void ViewportPanel::renderOverlay2D(const ImVec2& origin, const ImVec2& size) {
    ImDrawList* drawList = ImGui::GetWindowDrawList();

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

    // Empty workspace: where to start (the Welcome panel may have been closed).
    if (!hasModel()) {
      constexpr const char* lines[] = {
        "No model loaded",
        "Analyze: pick a truss or beam / frame analysis",
        "File > Import: open a mesh, result or CAD file",
        "Help > Welcome: open a solved example",
      };
      const float lineHeight = ImGui::GetTextLineHeightWithSpacing();
      float y = origin.y + (size.y - lineHeight * static_cast<float>(std::size(lines))) * 0.5f;
      for (std::size_t i = 0; i < std::size(lines); ++i) {
        const float width = ImGui::CalcTextSize(lines[i]).x;
        const ImU32 color = i == 0 ? IM_COL32(200, 200, 205, 220) : IM_COL32(140, 142, 150, 200);
        drawList->AddText(ImVec2(origin.x + (size.x - width) * 0.5f, y), color, lines[i]);
        y += lineHeight * (i == 0 ? 1.5f : 1.0f);
      }
    }

    // Colorbars
    if (hasModel()) {
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
      float nextTop = startY;
      using enum ElementColoring;
      if (m_display->coloring == Stress) {
        drawColorbar(startX, nextTop, m_currentBeamMesh ? "von Mises (MPa)" : "|Stress| (MPa)", m_cachedMaxStress / 1.0e6);
        nextTop -= 220.0f;
      }
      if (m_display->coloring == Displacement || m_display->showNodes()) {
        drawColorbar(startX, nextTop, "Disp (mm)", m_cachedMaxDisp * 1000.0);
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

    // GPU Pixel Picking Interaction
    if (m_viewportHovered_ && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
      const ImVec2 mousePos = ImGui::GetMousePos();
      const int mouseX = static_cast<int>(mousePos.x - origin.x);
      const int mouseY = static_cast<int>(m_viewportSize.y - (mousePos.y - origin.y)); // Invert Y for OpenGL

      const int pickedID = m_fbo_->readEntityID(mouseX, mouseY);

      // Entity IDs: nodes >= 0, beam elements -(index + 2), nothing -1.
      auto& bridge = BRIDGE::buildBridge();
      std::lock_guard<std::mutex> lock(bridge.dataMutex);
      if (pickedID >= 0) {
        bridge.selectedNodeId = static_cast<std::uint32_t>(pickedID);
      } else if (pickedID <= -2) {
        bridge.selectedElementId = static_cast<std::uint32_t>(-(pickedID + 2));
      } else {
        bridge.selectedNodeId = kNone;
        bridge.selectedElementId = kNone;
      }
      truss_1d_gui_prop.m_meshNeedsUpdate = true;
    }

    handleCameraInput();
    renderOverlay2D(origin, availSize);

    ImGui::End();
    ImGui::PopStyleVar();
  }

} // namespace anaf::GUI end
