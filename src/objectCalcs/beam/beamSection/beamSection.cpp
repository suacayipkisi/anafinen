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

#include "beamSection.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <numbers>

namespace FEM::BEAM {

  namespace {
    constexpr double pi = std::numbers::pi;

    template <class... Ts> struct Overloaded : Ts... { using Ts::operator()...; };

    // A piece of a composite section: its own centroidal second moments and its centroid.
    // sign = -1 removes it (holes, cut corners).
    struct Part {
      double area{};
      double ownIy{}; // about the axis through its centroid parallel to local y
      double ownIz{};
      double y{};
      double z{};
      double sign{1.0};
    };

    Part rectangle(const double height, const double width, const double y, const double z, const double sign = 1.0) {
      return {height * width, height * width * width * width / 12.0, width * height * height * height / 12.0, y, z, sign};
    }

    // Quarter disc of radius r; (y, z) is its centroid.
    Part quarterDisc(const double r, const double y, const double z, const double sign = 1.0) {
      const double own = (pi / 16.0 - 4.0 / (9.0 * pi)) * r * r * r * r;
      return {pi * r * r / 4.0, own, own, y, z, sign};
    }

    // Distance of the quarter disc centroid from its center along each axis.
    double quarterDiscOffset(const double r) { return 4.0 * r / (3.0 * pi); }

    // Fillet (spandrel): an r x r square minus the quarter disc centered at its far corner.
    // Its centroid lies spandrelOffset(r) from both straight edges.
    double spandrelOffset(const double r) { return r * (10.0 - 3.0 * pi) / (12.0 - 3.0 * pi); }

    Part spandrel(const double r, const double y, const double z) {
      const double area = (1.0 - pi / 4.0) * r * r;
      const double discArm = r - quarterDiscOffset(r); // disc centroid from the edge
      const double edgeMoment = r * r * r * r / 3.0
        - ((pi / 16.0 - 4.0 / (9.0 * pi)) * r * r * r * r + pi * r * r / 4.0 * discArm * discArm);
      const double e = spandrelOffset(r);
      const double own = edgeMoment - area * e * e;
      return {area, own, own, y, z, 1.0};
    }

    // Rectangle with four convex corners of radius r (r = 0: plain rectangle).
    void roundedRectangle(std::vector<Part>& parts, const double height, const double width, const double r, const double sign) {
      parts.push_back(rectangle(height, width, 0.0, 0.0, sign));
      if (r <= 0.0) return;
      const double cornerY = height / 2.0 - r / 2.0;
      const double cornerZ = width / 2.0 - r / 2.0;
      const double discY = height / 2.0 - r + quarterDiscOffset(r);
      const double discZ = width / 2.0 - r + quarterDiscOffset(r);
      for (const double sy : {-1.0, 1.0}) {
        for (const double sz : {-1.0, 1.0}) {
          parts.push_back(rectangle(r, r, sy * cornerY, sz * cornerZ, -sign));
          parts.push_back(quarterDisc(r, sy * discY, sz * discZ, sign));
        }
      }
    }

    // A, Iy, Iz of a doubly symmetric composite (centroid at the origin).
    SectionProperties sum(const std::vector<Part>& parts) {
      SectionProperties p;
      for (const auto& part : parts) {
        p.area += part.sign * part.area;
        p.secondMomentY += part.sign * (part.ownIy + part.area * part.z * part.z);
        p.secondMomentZ += part.sign * (part.ownIz + part.area * part.y * part.y);
      }
      return p;
    }

    double kappaRectangle(const double v) { return 10.0 * (1.0 + v) / (12.0 + 11.0 * v); }
    double kappaCircle(const double v) { return 6.0 * (1.0 + v) / (7.0 + 6.0 * v); }

    // Cowper's hollow circle, m = inner / outer diameter.
    double kappaPipe(const double v, const double m) {
      const double q = (1.0 + m * m) * (1.0 + m * m);
      return 6.0 * (1.0 + v) * q / ((7.0 + 6.0 * v) * q + (20.0 + 12.0 * v) * m * m);
    }

    // Cowper's thin-walled box with shear parallel to the side of length depth (centre lines).
    double kappaBox(const double v, const double depth, const double width) {
      const double m = width / depth;
      const double n = width / depth;
      const double numerator = 10.0 * (1.0 + v) * (1.0 + 3.0 * m) * (1.0 + 3.0 * m);
      const double denominator = (12.0 + 72.0 * m + 150.0 * m * m + 90.0 * m * m * m)
        + v * (11.0 + 66.0 * m + 135.0 * m * m + 90.0 * m * m * m)
        + 10.0 * n * n * ((3.0 + v) * m + 3.0 * m * m);
      return numerator / denominator;
    }

    // Cowper's thin-walled I with shear along the web; depth between flange centre lines.
    double kappaI(const double v, const double depth, const double flangeWidth, const double webThickness, const double flangeThickness) {
      const double m = 2.0 * flangeWidth * flangeThickness / (depth * webThickness);
      const double n = flangeWidth / depth;
      const double numerator = 10.0 * (1.0 + v) * (1.0 + 3.0 * m) * (1.0 + 3.0 * m);
      const double denominator = (12.0 + 72.0 * m + 150.0 * m * m + 90.0 * m * m * m)
        + v * (11.0 + 66.0 * m + 135.0 * m * m + 90.0 * m * m * m)
        + 30.0 * n * n * (m + m * m) + 5.0 * v * n * n * (8.0 * m + 9.0 * m * m);
      return numerator / denominator;
    }

    bool positive(const double v) { return std::isfinite(v) && v > 0.0; }
    bool nonNegative(const double v) { return std::isfinite(v) && v >= 0.0; }

    using Loop = std::vector<std::array<double, 2>>;

    // Arc points {y, z} around (cy, cz) from angle a0 to a1 (radians, angle measured in the
    // (z, y) plane from +z towards +y), both ends included.
    void arc(Loop& loop, const double cy, const double cz, const double r, const double a0, const double a1, const int segmentsPerQuarter) {
      if (r <= 0.0) {
        loop.push_back({cy, cz});
        return;
      }
      const int segments = std::max(1, static_cast<int>(std::ceil(std::abs(a1 - a0) / (pi / 2.0) * segmentsPerQuarter)));
      for (int i = 0; i <= segments; ++i) {
        const double a = a0 + (a1 - a0) * static_cast<double>(i) / static_cast<double>(segments);
        loop.push_back({cy + r * std::sin(a), cz + r * std::cos(a)});
      }
    }

    // Counter-clockwise rounded rectangle in the (z, y) plane.
    Loop roundedRectangleLoop(const double height, const double width, const double r, const int segmentsPerQuarter) {
      const double y = height / 2.0 - r;
      const double z = width / 2.0 - r;
      Loop loop;
      arc(loop, -y, z, r, -pi / 2.0, 0.0, segmentsPerQuarter);       // bottom right
      arc(loop, y, z, r, 0.0, pi / 2.0, segmentsPerQuarter);         // top right
      arc(loop, y, -z, r, pi / 2.0, pi, segmentsPerQuarter);         // top left
      arc(loop, -y, -z, r, pi, 1.5 * pi, segmentsPerQuarter);        // bottom left
      return loop;
    }

    Loop circleLoop(const double radius, const int segmentsPerQuarter) {
      Loop loop;
      const int segments = 4 * segmentsPerQuarter;
      for (int i = 0; i < segments; ++i) {
        const double a = 2.0 * pi * static_cast<double>(i) / static_cast<double>(segments);
        loop.push_back({radius * std::sin(a), radius * std::cos(a)});
      }
      return loop;
    }

    Loop reversed(Loop loop) {
      std::ranges::reverse(loop);
      return loop;
    }
  } // namespace end

  const char* shapeKey(const SectionShape& shape) noexcept {
    return std::visit(Overloaded{
      [](const GeneralSection&) { return "general"; },
      [](const RectangleSection&) { return "rectangle"; },
      [](const CircleSection&) { return "circle"; },
      [](const PipeSection&) { return "pipe"; },
      [](const BoxSection&) { return "box"; },
      [](const ISection&) { return "i"; },
    }, shape);
  }

  std::expected<void, std::string> validateShape(const SectionShape& shape) {
    const auto fail = [](std::string message) { return std::expected<void, std::string>(std::unexpect, std::move(message)); };
    return std::visit(Overloaded{
      [&](const GeneralSection& s) -> std::expected<void, std::string> {
        const auto& v = s.values;
        if (!positive(v.area) || !positive(v.secondMomentY) || !positive(v.secondMomentZ) || !positive(v.torsionConstant)) {
          return fail("A, Iy, Iz and J must be positive");
        }
        if (!nonNegative(v.shearAreaY) || !nonNegative(v.shearAreaZ)) return fail("shear areas must be zero (Euler-Bernoulli only) or positive");
        return {};
      },
      [&](const RectangleSection& s) -> std::expected<void, std::string> {
        if (!positive(s.height) || !positive(s.width)) return fail("height and width must be positive");
        return {};
      },
      [&](const CircleSection& s) -> std::expected<void, std::string> {
        if (!positive(s.diameter)) return fail("the diameter must be positive");
        return {};
      },
      [&](const PipeSection& s) -> std::expected<void, std::string> {
        if (!positive(s.outerDiameter) || !positive(s.wallThickness)) return fail("diameter and wall thickness must be positive");
        if (!(2.0 * s.wallThickness < s.outerDiameter)) return fail("the wall thickness must be less than half the diameter");
        return {};
      },
      [&](const BoxSection& s) -> std::expected<void, std::string> {
        if (!positive(s.height) || !positive(s.width) || !positive(s.wallThickness)) {
          return fail("height, width and wall thickness must be positive");
        }
        const double side = std::min(s.height, s.width);
        if (!(2.0 * s.wallThickness < side)) return fail("the walls must be thinner than half the smaller side");
        if (!nonNegative(s.outerCornerRadius) || !nonNegative(s.innerCornerRadius)) return fail("corner radii must be zero or positive");
        if (s.outerCornerRadius > side / 2.0) return fail("the outer corner radius does not fit the section");
        if (s.innerCornerRadius > side / 2.0 - s.wallThickness) return fail("the inner corner radius does not fit the opening");
        return {};
      },
      [&](const ISection& s) -> std::expected<void, std::string> {
        if (!positive(s.height) || !positive(s.flangeWidth) || !positive(s.webThickness) || !positive(s.flangeThickness)) {
          return fail("height, flange width, web and flange thickness must be positive");
        }
        if (!nonNegative(s.rootRadius)) return fail("the root radius must be zero or positive");
        if (!(2.0 * s.flangeThickness < s.height)) return fail("the flanges must be thinner than half the height");
        if (!(s.webThickness < s.flangeWidth)) return fail("the web must be thinner than the flange width");
        if (s.webThickness + 2.0 * s.rootRadius > s.flangeWidth) return fail("the root radius does not fit the flange width");
        if (2.0 * (s.flangeThickness + s.rootRadius) > s.height) return fail("the root radius does not fit the web height");
        return {};
      },
    }, shape);
  }

  SectionProperties computeProperties(const SectionShape& shape, const double v) {
    return std::visit(Overloaded{
      [](const GeneralSection& s) { return s.values; },
      [&](const RectangleSection& s) {
        SectionProperties p = sum({rectangle(s.height, s.width, 0.0, 0.0)});
        const double a = std::max(s.height, s.width);
        const double c = std::min(s.height, s.width);
        const double ratio = c / a;
        p.torsionConstant = a * c * c * c * (1.0 / 3.0 - 0.21 * ratio * (1.0 - ratio * ratio * ratio * ratio / 12.0));
        p.shearAreaY = p.shearAreaZ = kappaRectangle(v) * p.area;
        return p;
      },
      [&](const CircleSection& s) {
        const double r = s.diameter / 2.0;
        SectionProperties p;
        p.area = pi * r * r;
        p.secondMomentY = p.secondMomentZ = pi * r * r * r * r / 4.0;
        p.torsionConstant = 2.0 * p.secondMomentY;
        p.shearAreaY = p.shearAreaZ = kappaCircle(v) * p.area;
        return p;
      },
      [&](const PipeSection& s) {
        const double ro = s.outerDiameter / 2.0;
        const double ri = ro - s.wallThickness;
        SectionProperties p;
        p.area = pi * (ro * ro - ri * ri);
        p.secondMomentY = p.secondMomentZ = pi * (ro * ro * ro * ro - ri * ri * ri * ri) / 4.0;
        p.torsionConstant = 2.0 * p.secondMomentY;
        p.shearAreaY = p.shearAreaZ = kappaPipe(v, ri / ro) * p.area;
        return p;
      },
      [&](const BoxSection& s) {
        const double t = s.wallThickness;
        std::vector<Part> parts;
        roundedRectangle(parts, s.height, s.width, s.outerCornerRadius, 1.0);
        roundedRectangle(parts, s.height - 2.0 * t, s.width - 2.0 * t, s.innerCornerRadius, -1.0);
        SectionProperties p = sum(parts);
        // EN 10219-2 / EN 10210-2: J = t^3 h / 3 + 2 K Ah over the centre line.
        const double rc = (s.outerCornerRadius + s.innerCornerRadius) / 2.0;
        const double perimeter = 2.0 * ((s.width - t) + (s.height - t)) - 2.0 * rc * (4.0 - pi);
        const double enclosed = (s.width - t) * (s.height - t) - rc * rc * (4.0 - pi);
        const double k = 2.0 * t * enclosed / perimeter;
        p.torsionConstant = t * t * t * perimeter / 3.0 + 2.0 * k * enclosed;
        p.shearAreaY = kappaBox(v, s.height - t, s.width - t) * p.area;
        p.shearAreaZ = kappaBox(v, s.width - t, s.height - t) * p.area;
        return p;
      },
      [&](const ISection& s) {
        const double h = s.height;
        const double b = s.flangeWidth;
        const double tw = s.webThickness;
        const double tf = s.flangeThickness;
        const double r = s.rootRadius;
        std::vector<Part> parts{
          rectangle(tf, b, h / 2.0 - tf / 2.0, 0.0),
          rectangle(tf, b, -(h / 2.0 - tf / 2.0), 0.0),
          rectangle(h - 2.0 * tf, tw, 0.0, 0.0),
        };
        if (r > 0.0) {
          const double e = spandrelOffset(r);
          for (const double sy : {-1.0, 1.0}) {
            for (const double sz : {-1.0, 1.0}) parts.push_back(spandrel(r, sy * (h / 2.0 - tf - e), sz * (tw / 2.0 + e)));
          }
        }
        SectionProperties p = sum(parts);
        // ArcelorMittal (sections catalogue, technical notes): flanges and web as thin plates
        // plus the web-flange junction with its fillets.
        const double alpha = tw / tf * (0.145 + 0.1 * r / tf);
        const double d = ((r + tw / 2.0) * (r + tw / 2.0) + (r + tf) * (r + tf) - r * r) / (2.0 * r + tf);
        p.torsionConstant = 2.0 / 3.0 * (b - 0.63 * tf) * tf * tf * tf
          + (h - 2.0 * tf) * tw * tw * tw / 3.0
          + 2.0 * alpha * d * d * d * d;
        p.shearAreaY = kappaI(v, h - tf, b, tw, tf) * p.area;
        p.shearAreaZ = kappaRectangle(v) * 2.0 * b * tf;
        return p;
      },
    }, shape);
  }

  std::vector<std::vector<std::array<double, 2>>> sectionOutline(const SectionShape& shape, const int segmentsPerQuarter) {
    const int n = std::max(1, segmentsPerQuarter);
    return std::visit(Overloaded{
      [](const GeneralSection&) { return std::vector<Loop>{}; },
      [&](const RectangleSection& s) { return std::vector<Loop>{roundedRectangleLoop(s.height, s.width, 0.0, n)}; },
      [&](const CircleSection& s) { return std::vector<Loop>{circleLoop(s.diameter / 2.0, n)}; },
      [&](const PipeSection& s) {
        return std::vector<Loop>{circleLoop(s.outerDiameter / 2.0, n), reversed(circleLoop(s.outerDiameter / 2.0 - s.wallThickness, n))};
      },
      [&](const BoxSection& s) {
        const double t = s.wallThickness;
        return std::vector<Loop>{
          roundedRectangleLoop(s.height, s.width, s.outerCornerRadius, n),
          reversed(roundedRectangleLoop(s.height - 2.0 * t, s.width - 2.0 * t, s.innerCornerRadius, n)),
        };
      },
      [&](const ISection& s) {
        const double h2 = s.height / 2.0;
        const double b2 = s.flangeWidth / 2.0;
        const double w2 = s.webThickness / 2.0;
        const double tf = s.flangeThickness;
        const double r = s.rootRadius;
        Loop loop{{-h2, -b2}, {-h2, b2}, {-h2 + tf, b2}};
        arc(loop, -h2 + tf + r, w2 + r, r, -pi / 2.0, -pi, n); // bottom right fillet (concave)
        arc(loop, h2 - tf - r, w2 + r, r, pi, pi / 2.0, n);    // top right
        loop.push_back({h2 - tf, b2});
        loop.push_back({h2, b2});
        loop.push_back({h2, -b2});
        loop.push_back({h2 - tf, -b2});
        arc(loop, h2 - tf - r, -w2 - r, r, pi / 2.0, 0.0, n);  // top left
        arc(loop, -h2 + tf + r, -w2 - r, r, 0.0, -pi / 2.0, n); // bottom left
        loop.push_back({-h2 + tf, -b2});
        return std::vector<Loop>{loop};
      },
    }, shape);
  }

} // namespace FEM::BEAM end
