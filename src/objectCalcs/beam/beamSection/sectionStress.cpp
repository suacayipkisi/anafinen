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

#include "sectionStress.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace FEM::BEAM {

  namespace {
    constexpr double pi = std::numbers::pi;

    template <class... Ts>
    struct Overloaded : Ts... {
      using Ts::operator()...;
    };

    // Value of max (a y + b z) over the shape and the point where it is reached.
    struct Support {
      double value{};
      std::array<double, 2> point{};
    };

    double signOf(const double v) { return v < 0.0 ? -1.0 : 1.0; }

    // Rectangle of half sizes (hy, hz) with corners rounded by r: the support of a rounded
    // rectangle is that of the inner rectangle plus a disc of radius r.
    Support roundedRectangleSupport(const double hy, const double hz, const double r, const double a, const double b) {
      const double norm = std::hypot(a, b);
      const double ny = norm > 0.0 ? a / norm : 1.0;
      const double nz = norm > 0.0 ? b / norm : 0.0;
      const double cy = signOf(a) * (hy - r);
      const double cz = signOf(b) * (hz - r);
      return {std::abs(a) * (hy - r) + std::abs(b) * (hz - r) + r * norm, {cy + r * ny, cz + r * nz}};
    }

    // Jourawski at the neutral axis: V Q / (I t).
    double jourawski(const double shear, const double firstMoment, const double secondMoment, const double width) {
      return std::abs(shear) * firstMoment / (secondMoment * width);
    }

    // First moment about the centroidal axis of the half (y > 0) of a rounded rectangle of
    // height h (along the axis normal to the cut), width w and corner radius r.
    double halfRoundedRectangleMoment(const double h, const double w, const double r) {
      double q = w * h * h / 8.0;
      if (r > 0.0) {
        q -= 2.0 * r * r * (h / 2.0 - r / 2.0);
        q += 2.0 * (pi * r * r / 4.0) * (h / 2.0 - r + 4.0 * r / (3.0 * pi));
      }
      return q;
    }
  } // namespace end

  std::optional<SectionStress> sectionStress(const SectionShape& shape, const std::array<double, 6>& forces) {
    if (std::holds_alternative<GeneralSection>(shape)) return std::nullopt;
    const auto p = computeProperties(shape, 0.0); // A, I, J do not depend on Poisson's ratio
    const double N = forces[0], Vy = forces[1], Vz = forces[2], T = forces[3], My = forces[4], Mz = forces[5];
    const double a = -Mz / p.secondMomentZ; // sigma = N / A + a y + b z
    const double b = My / p.secondMomentY;

    struct Parts { Support support; double shear; double torsion; };
    const Parts parts = std::visit(Overloaded{
      [](const GeneralSection&) { return Parts{}; },
      [&](const RectangleSection& s) {
        const double longSide = std::max(s.height, s.width);
        const double shortSide = std::min(s.height, s.width);
        return Parts{
          roundedRectangleSupport(s.height / 2.0, s.width / 2.0, 0.0, a, b),
          1.5 * (std::abs(Vy) + std::abs(Vz)) / p.area,
          std::abs(T) * (3.0 * longSide + 1.8 * shortSide) / (longSide * longSide * shortSide * shortSide),
        };
      },
      [&](const CircleSection& s) {
        const double r = s.diameter / 2.0;
        return Parts{roundedRectangleSupport(r, r, r, a, b), 4.0 * (std::abs(Vy) + std::abs(Vz)) / (3.0 * p.area),
                     std::abs(T) * r / p.torsionConstant};
      },
      [&](const PipeSection& s) {
        const double ro = s.outerDiameter / 2.0;
        const double ri = ro - s.wallThickness;
        const double q = 2.0 * (ro * ro * ro - ri * ri * ri) / 3.0;
        const double width = 2.0 * s.wallThickness;
        return Parts{roundedRectangleSupport(ro, ro, ro, a, b),
                     jourawski(Vy, q, p.secondMomentZ, width) + jourawski(Vz, q, p.secondMomentY, width),
                     std::abs(T) * ro / p.torsionConstant};
      },
      [&](const BoxSection& s) {
        const double t = s.wallThickness;
        const auto firstMoment = [&](const double h, const double w) {
          return halfRoundedRectangleMoment(h, w, s.outerCornerRadius) - halfRoundedRectangleMoment(h - 2.0 * t, w - 2.0 * t, s.innerCornerRadius);
        };
        const double rc = (s.outerCornerRadius + s.innerCornerRadius) / 2.0;
        const double enclosed = (s.width - t) * (s.height - t) - rc * rc * (4.0 - pi);
        return Parts{roundedRectangleSupport(s.height / 2.0, s.width / 2.0, s.outerCornerRadius, a, b),
                     jourawski(Vy, firstMoment(s.height, s.width), p.secondMomentZ, 2.0 * t)
                       + jourawski(Vz, firstMoment(s.width, s.height), p.secondMomentY, 2.0 * t),
                     std::abs(T) / (2.0 * enclosed * t)};
      },
      [&](const ISection& s) {
        const double h = s.height, w = s.flangeWidth, tw = s.webThickness, tf = s.flangeThickness, r = s.rootRadius;
        // Strong axis (Vy along the web): flange + half web + two fillets above the neutral axis.
        double q = w * tf * (h / 2.0 - tf / 2.0) + tw * (h / 2.0 - tf) * (h / 2.0 - tf) / 2.0;
        if (r > 0.0) {
          const double area = (1.0 - pi / 4.0) * r * r;
          const double e = r * (10.0 - 3.0 * pi) / (12.0 - 3.0 * pi);
          q += 2.0 * area * (h / 2.0 - tf - e);
        }
        return Parts{roundedRectangleSupport(h / 2.0, w / 2.0, 0.0, a, b),
                     jourawski(Vy, q, p.secondMomentZ, tw) + 1.5 * std::abs(Vz) / (2.0 * w * tf),
                     std::abs(T) * std::max(tw, tf) / p.torsionConstant};
      },
    }, shape);

    SectionStress stress;
    const double axial = N / p.area;
    stress.maxNormal = axial + parts.support.value;
    stress.minNormal = axial - parts.support.value;
    stress.maxNormalPoint = parts.support.point;
    stress.minNormalPoint = {-parts.support.point[0], -parts.support.point[1]};
    stress.shearFromForce = parts.shear;
    stress.shearFromTorsion = parts.torsion;
    const double normal = std::max(std::abs(stress.maxNormal), std::abs(stress.minNormal));
    const double shear = parts.shear + parts.torsion;
    stress.vonMises = std::sqrt(normal * normal + 3.0 * shear * shear);
    return stress;
  }

} // namespace FEM::BEAM end
