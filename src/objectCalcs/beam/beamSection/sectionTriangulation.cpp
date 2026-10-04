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

#include "sectionTriangulation.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>

namespace FEM::BEAM {

  namespace {
    using Point = std::array<double, 2>; // {y, z}; the plane used below is (u, v) = (z, y)

    double u(const Point& p) { return p[1]; }
    double v(const Point& p) { return p[0]; }

    // Twice the signed area of (a, b, c) in the (z, y) plane; > 0 counter-clockwise.
    double cross(const Point& a, const Point& b, const Point& c) {
      return (u(b) - u(a)) * (v(c) - v(a)) - (v(b) - v(a)) * (u(c) - u(a));
    }

    bool samePoint(const Point& a, const Point& b) { return a[0] == b[0] && a[1] == b[1]; }

    // Proper intersection of segments ab and cd (shared end points do not count).
    bool segmentsCross(const Point& a, const Point& b, const Point& c, const Point& d) {
      if (samePoint(a, c) || samePoint(a, d) || samePoint(b, c) || samePoint(b, d)) return false;
      const double d1 = cross(a, b, c), d2 = cross(a, b, d), d3 = cross(c, d, a), d4 = cross(c, d, b);
      return ((d1 > 0.0) != (d2 > 0.0)) && ((d3 > 0.0) != (d4 > 0.0)) && d1 != 0.0 && d2 != 0.0 && d3 != 0.0 && d4 != 0.0;
    }

    bool insideTriangle(const Point& p, const Point& a, const Point& b, const Point& c) {
      return cross(a, b, p) >= 0.0 && cross(b, c, p) >= 0.0 && cross(c, a, p) >= 0.0;
    }

    // Joins hole (clockwise) into polygon (counter-clockwise) through the polygon vertex closest
    // to the hole's rightmost vertex whose connecting segment crosses no edge of the polygon,
    // of this hole or of the holes still to come.
    void bridgeHole(std::vector<Point>& polygon, const std::vector<Point>& hole, const std::vector<std::vector<Point>>& others) {
      std::size_t m = 0;
      for (std::size_t i = 1; i < hole.size(); ++i) {
        if (u(hole[i]) > u(hole[m])) m = i;
      }
      const Point& from = hole[m];
      const auto crossesAny = [&](const Point& to, const std::vector<Point>& loop) {
        for (std::size_t i = 0; i < loop.size(); ++i) {
          if (segmentsCross(from, to, loop[i], loop[(i + 1) % loop.size()])) return true;
        }
        return false;
      };
      std::size_t best = polygon.size();
      double bestDistance = std::numeric_limits<double>::infinity();
      for (std::size_t i = 0; i < polygon.size(); ++i) {
        const double du = u(polygon[i]) - u(from), dv = v(polygon[i]) - v(from);
        const double distance = du * du + dv * dv;
        if (distance >= bestDistance) continue;
        bool blocked = crossesAny(polygon[i], polygon) || crossesAny(polygon[i], hole);
        for (const auto& other : others) blocked = blocked || crossesAny(polygon[i], other);
        if (!blocked) {
          best = i;
          bestDistance = distance;
        }
      }
      if (best == polygon.size()) return; // no visible vertex (cannot happen for valid shapes)
      std::vector<Point> merged(polygon.begin(), polygon.begin() + static_cast<std::ptrdiff_t>(best) + 1);
      for (std::size_t k = 0; k <= hole.size(); ++k) merged.push_back(hole[(m + k) % hole.size()]);
      merged.insert(merged.end(), polygon.begin() + static_cast<std::ptrdiff_t>(best), polygon.end());
      polygon = std::move(merged);
    }
  } // namespace end

  SectionTriangulation triangulateSection(const SectionShape& shape, const int segmentsPerQuarter) {
    SectionTriangulation result;
    auto loops = sectionOutline(shape, segmentsPerQuarter);
    if (loops.empty()) return result;

    // Drop repeated consecutive points (zero-radius arcs repeat corners).
    for (auto& loop : loops) {
      std::vector<Point> clean;
      for (const auto& p : loop) {
        if (clean.empty() || !samePoint(clean.back(), p)) clean.push_back(p);
      }
      while (clean.size() > 1 && samePoint(clean.front(), clean.back())) clean.pop_back();
      loop = std::move(clean);
    }

    std::vector<Point> polygon = loops.front();
    std::vector<std::vector<Point>> holes(loops.begin() + 1, loops.end());
    std::ranges::sort(holes, [](const auto& a, const auto& b) {
      const auto maxU = [](const std::vector<Point>& loop) {
        double best = -std::numeric_limits<double>::infinity();
        for (const auto& p : loop) best = std::max(best, u(p));
        return best;
      };
      return maxU(a) > maxU(b);
    });
    for (std::size_t h = 0; h < holes.size(); ++h) {
      const std::vector<std::vector<Point>> rest(holes.begin() + static_cast<std::ptrdiff_t>(h) + 1, holes.end());
      bridgeHole(polygon, holes[h], rest);
    }

    result.points = polygon;
    std::vector<std::uint32_t> remaining(polygon.size());
    for (std::uint32_t i = 0; i < remaining.size(); ++i) remaining[i] = i;

    // Ear clipping. Vertices at the same position as the ear's corners (bridge copies) do not
    // block it.
    std::size_t guard = 0;
    while (remaining.size() > 3 && guard < polygon.size() * polygon.size()) {
      ++guard;
      bool clipped = false;
      const std::size_t n = remaining.size();
      for (std::size_t i = 0; i < n; ++i) {
        const auto ia = remaining[(i + n - 1) % n], ib = remaining[i], ic = remaining[(i + 1) % n];
        const Point &a = polygon[ia], &b = polygon[ib], &c = polygon[ic];
        const double turn = cross(a, b, c);
        if (turn <= 0.0) {
          if (turn == 0.0 && (samePoint(a, b) || samePoint(b, c))) { // degenerate: drop the vertex
            remaining.erase(remaining.begin() + static_cast<std::ptrdiff_t>(i));
            clipped = true;
            break;
          }
          continue; // reflex
        }
        bool ear = true;
        for (const auto k : remaining) {
          const Point& p = polygon[k];
          if (k == ia || k == ib || k == ic || samePoint(p, a) || samePoint(p, b) || samePoint(p, c)) continue;
          if (insideTriangle(p, a, b, c)) {
            ear = false;
            break;
          }
        }
        if (!ear) continue;
        result.triangles.push_back({ia, ib, ic});
        remaining.erase(remaining.begin() + static_cast<std::ptrdiff_t>(i));
        clipped = true;
        break;
      }
      if (!clipped) break; // numerically stuck: leave the rest open
    }
    if (remaining.size() == 3 && cross(polygon[remaining[0]], polygon[remaining[1]], polygon[remaining[2]]) > 0.0) {
      result.triangles.push_back({remaining[0], remaining[1], remaining[2]});
    }
    return result;
  }

} // namespace FEM::BEAM end
