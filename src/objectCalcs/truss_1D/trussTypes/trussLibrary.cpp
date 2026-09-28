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

#include "trussLibrary.hpp"

#include <io/core/pathUtf8.hpp>
#include <io/meshIo.hpp>
#include <truss_1D/trussIO/trussMeshAdapter.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <format>
#include <fstream>
#include <functional>
#include <map>
#include <numbers>
#include <set>
#include <utility>

namespace FEM::TRUSS::LIBRARY {

  namespace {

    using Point = std::array<double, 3>;
    using anaf::IO::MeshModel;

    // Must match the built-in names in assets/bridge/materialProperties.json (checked by the tests).
    struct MaterialRef {
      std::string_view name;
      std::uint32_t index; // built-in order, written as MaterialID for viewers
    };
    constexpr MaterialRef kSteel{"Structural Steel (AISI 4130)", 0};
    constexpr MaterialRef kAluminum{"Aluminum 6061-T6", 1};

    constexpr double kCm2 = 1e-4; // cm^2 -> m^2
    constexpr double kKN = 1e3;   // kN -> N
    constexpr double kPi = std::numbers::pi;

    struct Bar {
      std::uint32_t a{};
      std::uint32_t b{};
      double area{}; // m^2
    };

    // A 3D truss being built: nodes, bars with their own section, supports, nodal loads.
    struct Draft {
      std::vector<Point> nodes;
      std::vector<Bar> bars;
      std::map<std::uint32_t, std::array<bool, 3>> supports;
      std::map<std::uint32_t, Point> loads;

      std::uint32_t node(const double x, const double y, const double z) {
        nodes.push_back({x, y, z});
        return static_cast<std::uint32_t>(nodes.size() - 1);
      }
      void bar(const std::uint32_t a, const std::uint32_t b, const double areaCm2) {
        if (a != b) bars.push_back({std::min(a, b), std::max(a, b), areaCm2 * kCm2});
      }
      void fix(const std::uint32_t n, const std::array<bool, 3> dofs) {
        auto& fixed = supports[n];
        for (std::size_t axis = 0; axis < 3; ++axis) fixed[axis] = fixed[axis] || dofs[axis];
      }
      void pin(const std::uint32_t n) { fix(n, {true, true, true}); }
      void load(const std::uint32_t n, const Point force) {
        auto& total = loads[n];
        for (std::size_t axis = 0; axis < 3; ++axis) total[axis] += force[axis];
      }
    };

    MeshModel toModel(Draft draft, const MaterialRef material, std::string title) {
      // One bar per node pair; where two generators add the same bar the larger section wins.
      std::ranges::sort(draft.bars, [](const Bar& l, const Bar& r) {
        return std::tie(l.a, l.b, r.area) < std::tie(r.a, r.b, l.area);
      });
      const auto duplicates = std::ranges::unique(draft.bars, [](const Bar& l, const Bar& r) { return l.a == r.a && l.b == r.b; });
      draft.bars.erase(duplicates.begin(), duplicates.end());

      MeshModel model;
      model.title = std::move(title);
      model.nodes.reserve(draft.nodes.size());
      for (std::size_t i = 0; i < draft.nodes.size(); ++i) {
        model.nodes.push_back(anaf::IO::Node{static_cast<std::uint64_t>(i) + 1, draft.nodes[i]});
      }

      auto& block = model.blockFor(anaf::IO::ElementType::Line2);
      std::vector<double> area;
      anaf::IO::EntitySet set;
      set.name = std::string(ADAPTER::kMaterialSetPrefix) + std::string(material.name);
      set.kind = anaf::IO::SetKind::Element;
      set.dimension = 1;
      for (std::size_t e = 0; e < draft.bars.size(); ++e) {
        block.tags.push_back(e + 1);
        block.entityTags.push_back(0);
        block.connectivity.push_back(draft.bars[e].a);
        block.connectivity.push_back(draft.bars[e].b);
        area.push_back(draft.bars[e].area);
        set.members.push_back(static_cast<std::uint32_t>(e));
      }
      model.elementAttributes[anaf::IO::Attribute::CrossSectionArea] = std::move(area);
      model.elementAttributes[anaf::IO::Attribute::MaterialId] = std::vector<double>(draft.bars.size(), static_cast<double>(material.index));
      model.sets.push_back(std::move(set));

      for (const auto& [n, fixed] : draft.supports) model.constraints.push_back(anaf::IO::NodeConstraint{n, fixed, {}, {}, {}});
      for (const auto& [n, force] : draft.loads) {
        if (force[0] != 0.0 || force[1] != 0.0 || force[2] != 0.0) model.loads.push_back(anaf::IO::NodalLoad{n, force, {}});
      }
      return model;
    }

    // ---- planar trusses (x = span, y = up), turned into 3D by extrude() ----------------------

    struct Profile {
      std::vector<std::array<double, 2>> points;
      std::vector<Bar> members; // area in cm^2 until extrude()
      std::vector<std::uint32_t> pins;    // x, y, z fixed
      std::vector<std::uint32_t> rollers; // y fixed
      std::vector<std::uint32_t> loaded;  // panel points that carry the design load

      std::uint32_t add(const double x, const double y) {
        points.push_back({x, y});
        return static_cast<std::uint32_t>(points.size() - 1);
      }
      void member(const std::uint32_t a, const std::uint32_t b, const double areaCm2) { members.push_back({a, b, areaCm2}); }
    };

    // Copies of the planar truss at z = 0, spacing, 2 * spacing, ... Neighbouring copies are
    // tied by a strut at every node and a brace in every face swept by a member (purlins and
    // roof bracing; floor beams and wind bracing for a bridge), which makes a stable space
    // truss. nodeLoad [kN] acts downwards on each loaded node of an inner copy; the two outer
    // copies carry half of it (half the tributary width).
    Draft extrude(const Profile& profile, const int copies, const double spacing, const double nodeLoad, const double braceArea) {
      Draft draft;
      const auto count = static_cast<std::uint32_t>(profile.points.size());
      for (int c = 0; c < copies; ++c) {
        for (const auto& point : profile.points) draft.node(point[0], point[1], c * spacing);
      }
      const auto id = [count](const int copy, const std::uint32_t local) { return static_cast<std::uint32_t>(copy) * count + local; };

      for (int c = 0; c < copies; ++c) {
        for (const auto& member : profile.members) draft.bar(id(c, member.a), id(c, member.b), member.area);
        for (const auto n : profile.pins) draft.pin(id(c, n));
        for (const auto n : profile.rollers) draft.fix(id(c, n), {false, true, false});
        const double share = (c == 0 || c == copies - 1) ? 0.5 : 1.0;
        for (const auto n : profile.loaded) draft.load(id(c, n), {0.0, -nodeLoad * kKN * share, 0.0});
      }
      for (int c = 0; c + 1 < copies; ++c) {
        for (std::uint32_t n = 0; n < count; ++n) draft.bar(id(c, n), id(c + 1, n), braceArea);
        for (const auto& member : profile.members) draft.bar(id(c, member.a), id(c + 1, member.b), braceArea);
      }
      return draft;
    }

    enum class Web { Pratt, Howe };

    // Panel truss between supports at x = 0 (pin) and x = span (roller). Top and bottom chords
    // follow top(x) / bottom(x) and meet at the supports; a vertical at every inner panel point
    // and one diagonal per panel: Pratt diagonals fall towards mid-span, Howe diagonals rise.
    // panels must be even. loadTop: roof load on the top chord, else deck load on the bottom.
    Profile panelTruss(const double span, const int panels, const std::function<double(double)>& top,
                       const std::function<double(double)>& bottom, const Web web, const bool loadTop,
                       const double chordArea, const double webArea) {
      Profile p;
      const auto n = static_cast<std::uint32_t>(panels);
      std::vector<std::uint32_t> b(n + 1), t(n + 1);
      for (std::uint32_t i = 0; i <= n; ++i) {
        const double x = span * i / n;
        b[i] = p.add(x, bottom(x));
      }
      t[0] = b[0];
      t[n] = b[n];
      for (std::uint32_t i = 1; i < n; ++i) {
        const double x = span * i / n;
        t[i] = p.add(x, top(x));
      }
      for (std::uint32_t i = 0; i < n; ++i) {
        p.member(b[i], b[i + 1], chordArea);
        p.member(t[i], t[i + 1], chordArea);
      }
      for (std::uint32_t i = 1; i < n; ++i) p.member(b[i], t[i], webArea);
      const std::uint32_t mid = n / 2;
      for (std::uint32_t i = 1; i < mid; ++i) {
        if (web == Web::Pratt) p.member(t[i], b[i + 1], webArea);
        else p.member(b[i], t[i + 1], webArea);
      }
      for (std::uint32_t i = mid + 1; i < n; ++i) {
        if (web == Web::Pratt) p.member(t[i], b[i - 1], webArea);
        else p.member(b[i], t[i - 1], webArea);
      }
      p.pins = {b[0]};
      p.rollers = {b[n]};
      for (std::uint32_t i = 1; i < n; ++i) p.loaded.push_back(loadTop ? t[i] : b[i]);
      return p;
    }

    // Straight rafters rising to a ridge at mid-span.
    std::function<double(double)> gable(const double span, const double rise) {
      return [=](const double x) { return rise * (1.0 - std::abs(x - span / 2.0) / (span / 2.0)); };
    }
    double flat(double) { return 0.0; }

    // ---- roofs ---------------------------------------------------------------------------------

    LibraryTruss kingPostRoof() {
      constexpr double span = 8.0, rise = 2.0;
      Profile p;
      const auto left = p.add(0.0, 0.0), foot = p.add(span / 2, 0.0), right = p.add(span, 0.0);
      const auto ridge = p.add(span / 2, rise), l = p.add(span / 4, rise / 2), r = p.add(3 * span / 4, rise / 2);
      for (const auto& [a, c] : {std::pair{left, foot}, {foot, right}}) p.member(a, c, 12.0);
      for (const auto& [a, c] : {std::pair{left, l}, {l, ridge}, {ridge, r}, {r, right}}) p.member(a, c, 12.0);
      for (const auto& [a, c] : {std::pair{foot, ridge}, {foot, l}, {foot, r}}) p.member(a, c, 8.0);
      p.pins = {left};
      p.rollers = {right};
      p.loaded = {l, ridge, r};
      return {{"roof_king_post", "King post roof truss", "Roof",
               "Span 8 m, rise 2 m (26.6 deg), king post with two struts. 4 trusses at 4 m, tied by purlins and "
               "roof bracing. Pin / roller at the eaves. 6 kN per top node (roofing + snow). Steel, chords 12 cm^2, "
               "webs 8 cm^2."},
              toModel(extrude(p, 4, 4.0, 6.0, 6.0), kSteel, "King post roof truss")};
    }

    LibraryTruss queenPostRoof() {
      constexpr double span = 10.0, rise = 2.5;
      Profile p;
      std::array<std::uint32_t, 4> b{};
      for (std::uint32_t i = 0; i < 4; ++i) b[i] = p.add(span * i / 3.0, 0.0);
      const auto l = p.add(span / 3.0, rise * 2.0 / 3.0), ridge = p.add(span / 2.0, rise), r = p.add(2.0 * span / 3.0, rise * 2.0 / 3.0);
      for (std::uint32_t i = 0; i < 3; ++i) p.member(b[i], b[i + 1], 14.0);
      for (const auto& [a, c] : {std::pair{b[0], l}, {l, ridge}, {ridge, r}, {r, b[3]}}) p.member(a, c, 14.0);
      for (const auto& [a, c] : {std::pair{b[1], l}, {b[2], r}, {b[1], ridge}, {b[2], ridge}}) p.member(a, c, 9.0);
      p.pins = {b[0]};
      p.rollers = {b[3]};
      p.loaded = {l, ridge, r};
      return {{"roof_queen_post", "Queen post roof truss", "Roof",
               "Span 10 m, rise 2.5 m, two queen posts and ridge braces. 4 trusses at 4.5 m with purlins and "
               "bracing. Pin / roller at the eaves. 8 kN per top node. Steel, chords 14 cm^2, webs 9 cm^2."},
              toModel(extrude(p, 4, 4.5, 8.0, 6.0), kSteel, "Queen post roof truss")};
    }

    LibraryTruss finkRoof() {
      constexpr double span = 12.0, rise = 3.0;
      Profile p;
      std::array<std::uint32_t, 4> b{};
      for (std::uint32_t i = 0; i < 4; ++i) b[i] = p.add(span * i / 3.0, 0.0);
      const auto l = p.add(span / 4.0, rise / 2.0), ridge = p.add(span / 2.0, rise), r = p.add(3.0 * span / 4.0, rise / 2.0);
      for (std::uint32_t i = 0; i < 3; ++i) p.member(b[i], b[i + 1], 16.0);
      for (const auto& [a, c] : {std::pair{b[0], l}, {l, ridge}, {ridge, r}, {r, b[3]}}) p.member(a, c, 16.0);
      for (const auto& [a, c] : {std::pair{b[1], l}, {b[1], ridge}, {b[2], ridge}, {b[2], r}}) p.member(a, c, 10.0);
      p.pins = {b[0]};
      p.rollers = {b[3]};
      p.loaded = {l, ridge, r};
      return {{"roof_fink", "Fink (W) roof truss", "Roof",
               "Span 12 m, rise 3 m (26.6 deg), W-shaped web, the most common house / warehouse roof truss. "
               "5 trusses at 5 m. Pin / roller. 10 kN per top node. Steel, chords 16 cm^2, webs 10 cm^2."},
              toModel(extrude(p, 5, 5.0, 10.0, 6.0), kSteel, "Fink roof truss")};
    }

    LibraryTruss howeRoof() {
      constexpr double span = 16.0, rise = 4.0;
      const auto p = panelTruss(span, 8, gable(span, rise), flat, Web::Howe, true, 20.0, 12.0);
      return {{"roof_howe", "Howe roof truss", "Roof",
               "Span 16 m, rise 4 m, 8 panels, verticals in tension and diagonals rising to the ridge. "
               "5 trusses at 6 m. Pin / roller. 12 kN per top node. Steel, chords 20 cm^2, webs 12 cm^2."},
              toModel(extrude(p, 5, 6.0, 12.0, 8.0), kSteel, "Howe roof truss")};
    }

    LibraryTruss prattRoof() {
      constexpr double span = 20.0, rise = 4.0;
      const auto p = panelTruss(span, 10, gable(span, rise), flat, Web::Pratt, true, 24.0, 14.0);
      return {{"roof_pratt", "Pratt roof truss", "Roof",
               "Span 20 m, rise 4 m, 10 panels, diagonals falling to mid-span (tension under gravity). "
               "5 trusses at 6 m. Pin / roller. 12 kN per top node. Steel, chords 24 cm^2, webs 14 cm^2."},
              toModel(extrude(p, 5, 6.0, 12.0, 8.0), kSteel, "Pratt roof truss")};
    }

    LibraryTruss scissorsRoof() {
      constexpr double span = 10.0;
      const auto p = panelTruss(span, 6, gable(span, 3.5), gable(span, 1.5), Web::Pratt, true, 14.0, 9.0);
      return {{"roof_scissors", "Scissors roof truss", "Roof",
               "Span 10 m, roof rise 3.5 m, sloped bottom chord rising 1.5 m for a vaulted ceiling. "
               "4 trusses at 4 m. Pin / roller (the roller lets the truss spread). 6 kN per top node. Steel, "
               "chords 14 cm^2, webs 9 cm^2."},
              toModel(extrude(p, 4, 4.0, 6.0, 6.0), kSteel, "Scissors roof truss")};
    }

    LibraryTruss bowstringRoof() {
      constexpr double span = 24.0, rise = 4.5;
      const auto arch = [=](const double x) { return 4.0 * rise * x * (span - x) / (span * span); };
      const auto p = panelTruss(span, 8, arch, flat, Web::Pratt, true, 30.0, 14.0);
      return {{"roof_bowstring", "Bowstring roof truss (hangar)", "Roof",
               "Span 24 m, parabolic top chord rising 4.5 m, flat tie, 8 panels. 5 trusses at 6 m for a "
               "hangar / sports hall. Pin / roller. 14 kN per top node. Steel, chords 30 cm^2, webs 14 cm^2."},
              toModel(extrude(p, 5, 6.0, 14.0, 10.0), kSteel, "Bowstring roof truss")};
    }

    // ---- bridges (two main trusses 7 m apart, deck load on the bottom chord) -------------------

    LibraryTruss prattBridge() {
      const auto p = panelTruss(36.0, 6, [](double) { return 6.0; }, flat, Web::Pratt, false, 220.0, 120.0);
      return {{"bridge_pratt", "Pratt through-truss bridge", "Bridge",
               "Span 36 m, 6 panels of 6 m, height 6 m, inclined end posts. Two trusses 7 m apart with floor "
               "beams, top and bottom wind bracing. Pin / roller bearings. Deck: 300 kN per panel point "
               "(150 kN per truss). Steel, chords 220 cm^2, webs 120 cm^2."},
              toModel(extrude(p, 2, 7.0, 300.0, 60.0), kSteel, "Pratt truss bridge")};
    }

    LibraryTruss howeBridge() {
      const auto p = panelTruss(36.0, 6, [](double) { return 6.0; }, flat, Web::Howe, false, 220.0, 120.0);
      return {{"bridge_howe", "Howe through-truss bridge", "Bridge",
               "Span 36 m, 6 panels, height 6 m, diagonals rising to mid-span (compression) and verticals in "
               "tension, the classic timber-era layout. Two trusses 7 m apart, braced. Pin / roller. 300 kN per "
               "panel point. Steel, chords 220 cm^2, webs 120 cm^2."},
              toModel(extrude(p, 2, 7.0, 300.0, 60.0), kSteel, "Howe truss bridge")};
    }

    LibraryTruss warrenBridge() {
      constexpr double span = 30.0, height = 4.5;
      constexpr std::uint32_t panels = 6;
      Profile p;
      std::vector<std::uint32_t> b, t;
      for (std::uint32_t i = 0; i <= panels; ++i) b.push_back(p.add(span * i / panels, 0.0));
      for (std::uint32_t i = 0; i < panels; ++i) t.push_back(p.add(span * (i + 0.5) / panels, height));
      for (std::uint32_t i = 0; i < panels; ++i) {
        p.member(b[i], b[i + 1], 180.0);
        p.member(b[i], t[i], 100.0);
        p.member(t[i], b[i + 1], 100.0);
        if (i + 1 < panels) p.member(t[i], t[i + 1], 180.0);
      }
      p.pins = {b.front()};
      p.rollers = {b.back()};
      p.loaded.assign(b.begin() + 1, b.end() - 1);
      return {{"bridge_warren", "Warren truss bridge", "Bridge",
               "Span 30 m, 6 panels of 5 m, height 4.5 m, equilateral-style diagonals without verticals. Two "
               "trusses 6 m apart, braced (pedestrian / light road bridge). Pin / roller. 200 kN per panel "
               "point. Steel, chords 180 cm^2, diagonals 100 cm^2."},
              toModel(extrude(p, 2, 6.0, 200.0, 50.0), kSteel, "Warren truss bridge")};
    }

    LibraryTruss kTrussBridge() {
      constexpr double span = 48.0, height = 8.0;
      constexpr std::uint32_t n = 8;
      Profile p;
      std::vector<std::uint32_t> b(n + 1), t(n + 1), m(n + 1);
      for (std::uint32_t i = 0; i <= n; ++i) b[i] = p.add(span * i / n, 0.0);
      t[0] = b[0];
      t[n] = b[n];
      for (std::uint32_t i = 1; i < n; ++i) t[i] = p.add(span * i / n, height);
      for (std::uint32_t i = 0; i < n; ++i) {
        p.member(b[i], b[i + 1], 260.0);
        p.member(t[i], t[i + 1], 260.0);
      }
      // Verticals next to the end posts are plain; the others are split at mid-height, where the
      // two diagonals of the "K" meet (from the panel on the support side; both sides at mid-span).
      for (std::uint32_t i : {1u, n - 1}) p.member(b[i], t[i], 140.0);
      for (std::uint32_t i = 2; i <= n - 2; ++i) {
        m[i] = p.add(span * i / n, height / 2.0);
        p.member(b[i], m[i], 140.0);
        p.member(m[i], t[i], 140.0);
        if (i <= n / 2) {
          p.member(m[i], t[i - 1], 140.0);
          p.member(m[i], b[i - 1], 140.0);
        }
        if (i >= n / 2) {
          p.member(m[i], t[i + 1], 140.0);
          p.member(m[i], b[i + 1], 140.0);
        }
      }
      p.pins = {b[0]};
      p.rollers = {b[n]};
      for (std::uint32_t i = 1; i < n; ++i) p.loaded.push_back(b[i]);
      return {{"bridge_k_truss", "K-truss bridge", "Bridge",
               "Span 48 m, 8 panels of 6 m, height 8 m. Verticals split at mid-height by K-shaped diagonals, "
               "which keeps compression members short on deep, long spans. Two trusses 8 m apart, braced. "
               "Pin / roller. 400 kN per panel point. Steel, chords 260 cm^2, webs 140 cm^2."},
              toModel(extrude(p, 2, 8.0, 400.0, 70.0), kSteel, "K truss bridge")};
    }

    LibraryTruss parkerBridge() {
      constexpr double span = 48.0;
      const auto camel = [=](const double x) { return 6.0 + 3.0 * std::sin(kPi * x / span); };
      const auto p = panelTruss(span, 8, camel, flat, Web::Pratt, false, 260.0, 140.0);
      return {{"bridge_parker", "Parker (camelback) bridge", "Bridge",
               "Span 48 m, 8 panels, Pratt web under a polygonal top chord rising from 6 m at the ends to 9 m at "
               "mid-span, where the bending moment peaks. Two trusses 8 m apart, braced. Pin / roller. 400 kN "
               "per panel point. Steel, chords 260 cm^2, webs 140 cm^2."},
              toModel(extrude(p, 2, 8.0, 400.0, 70.0), kSteel, "Parker truss bridge")};
    }

    // ---- stadium / long-span roofs --------------------------------------------------------------

    LibraryTruss grandstandCantilever() {
      constexpr double length = 20.0, top = 16.0, backDepth = 3.5;
      constexpr std::uint32_t n = 5;
      Profile p;
      std::vector<std::uint32_t> t(n + 1), c(n + 1);
      for (std::uint32_t i = 0; i <= n; ++i) {
        const double x = length * i / n;
        t[i] = p.add(x, top + 0.08 * x); // slight upward slope for drainage
      }
      for (std::uint32_t i = 0; i < n; ++i) {
        const double x = length * i / n;
        c[i] = p.add(x, top - backDepth + (backDepth - 0.4) * x / length);
      }
      c[n] = t[n]; // chords meet at the tip
      for (std::uint32_t i = 0; i < n; ++i) {
        p.member(t[i], t[i + 1], 60.0);
        p.member(c[i], c[i + 1], 60.0);
        p.member(c[i], t[i], 30.0);
        if (i + 1 < n) p.member(t[i], c[i + 1], 30.0);
      }
      p.pins = {t[0], c[0]};
      p.loaded.assign(t.begin() + 1, t.end());
      return {{"stadium_grandstand_cantilever", "Grandstand cantilever roof", "Stadium",
               "20 m cantilever trusses over a stand, 3.5 m deep at the back tapering to the tip, anchored at "
               "two points on the back column (both pinned). 6 trusses at 8 m with purlins and bracing. 15 kN "
               "per top node. Steel, chords 60 cm^2, webs 30 cm^2."},
              toModel(extrude(p, 6, 8.0, 15.0, 12.0), kSteel, "Grandstand cantilever roof")};
    }

    LibraryTruss spaceFrameRoof() {
      constexpr std::uint32_t n = 8;
      constexpr double module = 3.0, depth = 2.1, elevation = 12.0;
      Draft d;
      std::vector<std::uint32_t> top((n + 1) * (n + 1)), bottom(n * n);
      const auto ti = [&](std::uint32_t i, std::uint32_t j) { return top[i * (n + 1) + j]; };
      const auto bi = [&](std::uint32_t i, std::uint32_t j) { return bottom[i * n + j]; };
      for (std::uint32_t i = 0; i <= n; ++i) {
        for (std::uint32_t j = 0; j <= n; ++j) top[i * (n + 1) + j] = d.node(i * module, elevation + depth, j * module);
      }
      for (std::uint32_t i = 0; i < n; ++i) {
        for (std::uint32_t j = 0; j < n; ++j) bottom[i * n + j] = d.node((i + 0.5) * module, elevation, (j + 0.5) * module);
      }
      for (std::uint32_t i = 0; i <= n; ++i) {
        for (std::uint32_t j = 0; j <= n; ++j) {
          if (i < n) d.bar(ti(i, j), ti(i + 1, j), 20.0);
          if (j < n) d.bar(ti(i, j), ti(i, j + 1), 20.0);
          const bool edgeI = i == 0 || i == n, edgeJ = j == 0 || j == n;
          if (edgeI || edgeJ) d.fix(ti(i, j), {edgeI && edgeJ, true, edgeI && edgeJ}); // perimeter columns, corners pinned
          const double share = (edgeI ? 0.5 : 1.0) * (edgeJ ? 0.5 : 1.0);
          d.load(ti(i, j), {0.0, -9.0 * kKN * share, 0.0});
        }
      }
      for (std::uint32_t i = 0; i < n; ++i) {
        for (std::uint32_t j = 0; j < n; ++j) {
          if (i + 1 < n) d.bar(bi(i, j), bi(i + 1, j), 20.0);
          if (j + 1 < n) d.bar(bi(i, j), bi(i, j + 1), 20.0);
          for (const auto& [di, dj] : {std::pair{0u, 0u}, {1u, 0u}, {0u, 1u}, {1u, 1u}}) d.bar(bi(i, j), ti(i + di, j + dj), 12.0);
        }
      }
      return {{"stadium_space_frame", "Double-layer space frame roof", "Stadium",
               "24 x 24 m square-on-square offset grid (3 m modules, 2.1 m deep) at 12 m, the typical "
               "exhibition hall / stadium concourse roof. Carried on every top perimeter node, corners pinned. "
               "9 kN per top node (1 kN/m^2). Steel, chords 20 cm^2, webs 12 cm^2."},
              toModel(std::move(d), kSteel, "Space frame roof")};
    }

    LibraryTruss schwedlerDome() {
      constexpr double sphere = 24.0, baseAngle = 50.0 * kPi / 180.0;
      constexpr std::uint32_t rings = 5, perRing = 16;
      Draft d;
      const double baseY = sphere * std::cos(baseAngle);
      std::vector<std::vector<std::uint32_t>> ring(rings);
      for (std::uint32_t k = 0; k < rings; ++k) {
        const double theta = baseAngle * (1.0 - static_cast<double>(k) / rings);
        for (std::uint32_t j = 0; j < perRing; ++j) {
          const double phi = 2.0 * kPi * j / perRing;
          ring[k].push_back(d.node(sphere * std::sin(theta) * std::cos(phi), sphere * std::cos(theta) - baseY,
                                   sphere * std::sin(theta) * std::sin(phi)));
        }
      }
      const auto crown = d.node(0.0, sphere - baseY, 0.0);
      for (std::uint32_t k = 0; k < rings; ++k) {
        for (std::uint32_t j = 0; j < perRing; ++j) {
          const auto next = (j + 1) % perRing;
          d.bar(ring[k][j], ring[k][next], k == 0 ? 40.0 : 25.0); // tension ring at the base
          if (k + 1 < rings) {
            d.bar(ring[k][j], ring[k + 1][j], 25.0);
            d.bar(ring[k][j], ring[k + 1][next], 15.0);
          } else {
            d.bar(ring[k][j], crown, 25.0);
          }
          if (k == 0) d.pin(ring[k][j]);
          else d.load(ring[k][j], {0.0, -12.0 * kKN, 0.0});
        }
      }
      d.load(crown, {0.0, -12.0 * kKN, 0.0});
      return {{"stadium_schwedler_dome", "Schwedler dome (sports hall)", "Stadium",
               "Spherical cap, 36.8 m base diameter, 8.6 m rise: 16 meridian ribs, 5 rings and one diagonal "
               "per panel, the classic arena / gasholder dome. Pinned on the base ring. 12 kN per node "
               "(snow + cladding). Steel, ribs and rings 25 cm^2, base ring 40 cm^2, diagonals 15 cm^2."},
              toModel(std::move(d), kSteel, "Schwedler dome")};
    }

    LibraryTruss geodesicDome() {
      constexpr double radius = 10.0;
      constexpr int frequency = 3;
      const double phi = std::numbers::phi;
      // Icosahedron with a vertex on +y (rotated about x), so the dome has a crown node.
      std::vector<Point> ico{{-1, phi, 0}, {1, phi, 0}, {-1, -phi, 0}, {1, -phi, 0}, {0, -1, phi}, {0, 1, phi},
                             {0, -1, -phi}, {0, 1, -phi}, {phi, 0, -1}, {phi, 0, 1}, {-phi, 0, -1}, {-phi, 0, 1}};
      const double tilt = std::atan2(1.0, phi); // brings (1, phi, 0) onto the y axis
      for (auto& v : ico) {
        const double x = v[0] * std::cos(tilt) - v[1] * std::sin(tilt);
        const double y = v[0] * std::sin(tilt) + v[1] * std::cos(tilt);
        v = {x, y, v[2]};
      }
      constexpr std::array<std::array<int, 3>, 20> faces{{
        {0, 11, 5}, {0, 5, 1}, {0, 1, 7}, {0, 7, 10}, {0, 10, 11}, {1, 5, 9}, {5, 11, 4}, {11, 10, 2}, {10, 7, 6}, {7, 1, 8},
        {3, 9, 4}, {3, 4, 2}, {3, 2, 6}, {3, 6, 8}, {3, 8, 9}, {4, 9, 5}, {2, 4, 11}, {6, 2, 10}, {8, 6, 7}, {9, 8, 1}}};

      // Subdivide every face, project onto the sphere, merge shared points.
      std::map<std::array<long long, 3>, std::uint32_t> indexOf;
      std::vector<Point> points;
      const auto vertex = [&](Point p) {
        const double norm = std::sqrt(p[0] * p[0] + p[1] * p[1] + p[2] * p[2]);
        for (auto& c : p) c = c / norm * radius;
        const std::array<long long, 3> key{std::llround(p[0] * 1e6), std::llround(p[1] * 1e6), std::llround(p[2] * 1e6)};
        const auto [it, inserted] = indexOf.try_emplace(key, static_cast<std::uint32_t>(points.size()));
        if (inserted) points.push_back(p);
        return it->second;
      };
      std::vector<std::array<std::uint32_t, 3>> triangles;
      for (const auto& f : faces) {
        const Point& a = ico[static_cast<std::size_t>(f[0])];
        const Point& b = ico[static_cast<std::size_t>(f[1])];
        const Point& c = ico[static_cast<std::size_t>(f[2])];
        const auto at = [&](const int i, const int j) {
          const int k = frequency - i - j;
          return vertex({(i * a[0] + j * b[0] + k * c[0]) / frequency, (i * a[1] + j * b[1] + k * c[1]) / frequency,
                         (i * a[2] + j * b[2] + k * c[2]) / frequency});
        };
        for (int i = 0; i < frequency; ++i) {
          for (int j = 0; i + j < frequency; ++j) {
            triangles.push_back({at(i, j), at(i + 1, j), at(i, j + 1)});
            if (i + j + 1 < frequency) triangles.push_back({at(i + 1, j), at(i + 1, j + 1), at(i, j + 1)});
          }
        }
      }

      // Upper part: triangles whose centroid is above the equator.
      std::vector<std::array<std::uint32_t, 3>> kept;
      for (const auto& t : triangles) {
        if (points[t[0]][1] + points[t[1]][1] + points[t[2]][1] > 0.0) kept.push_back(t);
      }
      std::map<std::pair<std::uint32_t, std::uint32_t>, int> edgeUse;
      for (const auto& t : kept) {
        for (int e = 0; e < 3; ++e) {
          const auto a = t[static_cast<std::size_t>(e)], b = t[static_cast<std::size_t>((e + 1) % 3)];
          ++edgeUse[{std::min(a, b), std::max(a, b)}];
        }
      }
      std::set<std::uint32_t> used, boundary;
      for (const auto& [edge, uses] : edgeUse) {
        used.insert(edge.first);
        used.insert(edge.second);
        if (uses == 1) {
          boundary.insert(edge.first);
          boundary.insert(edge.second);
        }
      }
      double lowest = radius;
      for (const auto n : used) lowest = std::min(lowest, points[n][1]);

      Draft d;
      std::map<std::uint32_t, std::uint32_t> local;
      for (const auto n : used) local[n] = d.node(points[n][0], points[n][1] - lowest, points[n][2]);
      for (const auto& [edge, uses] : edgeUse) d.bar(local[edge.first], local[edge.second], 12.0);
      for (const auto n : used) {
        if (boundary.contains(n)) d.pin(local[n]);
        else d.load(local[n], {0.0, -4.0 * kKN, 0.0});
      }
      return {{"stadium_geodesic_dome", "Geodesic dome (3V)", "Stadium",
               "Frequency-3 icosahedral geodesic dome, radius 10 m: triangulated, so every bar works in pure "
               "tension / compression. Pinned on its lower edge nodes. 4 kN per free node. Aluminum 6061-T6, "
               "12 cm^2 tubes."},
              toModel(std::move(d), kAluminum, "Geodesic dome")};
    }

    // ---- towers and platforms ------------------------------------------------------------------

    // Square lattice tower: levels of 4 nodes (corners at +-halfWidth, tapering linearly), legs,
    // level ring with one plan diagonal, X-bracing on the four faces. Base nodes pinned.
    struct Tower {
      Draft draft;
      std::vector<std::array<std::uint32_t, 4>> level;
    };
    Tower latticeTower(const int levels, const double height, const double baseHalf, const double topHalf,
                       const double legArea, const double braceArea) {
      Tower tower;
      auto& d = tower.draft;
      for (int k = 0; k <= levels; ++k) {
        const double y = height * k / levels;
        const double w = baseHalf + (topHalf - baseHalf) * k / levels;
        tower.level.push_back({d.node(-w, y, -w), d.node(w, y, -w), d.node(w, y, w), d.node(-w, y, w)});
      }
      for (std::size_t k = 0; k < tower.level.size(); ++k) {
        const auto& ring = tower.level[k];
        for (std::size_t c = 0; c < 4; ++c) {
          d.bar(ring[c], ring[(c + 1) % 4], braceArea);
          if (k == 0) d.pin(ring[c]);
          if (k + 1 < tower.level.size()) {
            const auto& up = tower.level[k + 1];
            d.bar(ring[c], up[c], legArea);
            d.bar(ring[c], up[(c + 1) % 4], braceArea);
            d.bar(ring[(c + 1) % 4], up[c], braceArea);
          }
        }
        if (k > 0) d.bar(ring[0], ring[2], braceArea); // plan bracing keeps the section square
      }
      return tower;
    }

    LibraryTruss transmissionTower() {
      auto tower = latticeTower(6, 30.0, 4.0, 1.0, 45.0, 18.0);
      auto& d = tower.draft;
      const auto& top = tower.level[6];
      const auto& below = tower.level[5];
      // Cross-arms: a tip node tied to the two top and two lower corners on its side (a pyramid).
      const auto armTip = [&](const double side, const std::array<std::size_t, 2> corners) {
        const auto tip = d.node(side * 7.0, 29.0, 0.0);
        for (const auto c : corners) {
          d.bar(tip, top[c], 18.0);
          d.bar(tip, below[c], 18.0);
        }
        d.load(tip, {0.0, -20.0 * kKN, 6.0 * kKN}); // conductor weight + wind on the wires
      };
      armTip(1.0, {1, 2});
      armTip(-1.0, {0, 3});
      for (std::size_t k = 1; k < tower.level.size(); ++k) {
        for (const auto n : tower.level[k]) d.load(n, {0.0, 0.0, 1.5 * kKN}); // wind on the tower
      }
      return {{"tower_transmission", "Transmission line tower", "Tower & Platform",
               "30 m square lattice tower tapering from 8 m to 2 m, X-braced faces, two 7 m cross-arms. Base "
               "legs pinned. Conductors: 20 kN down and 6 kN wind at each arm tip; 1.5 kN wind per tower node. "
               "Steel, legs 45 cm^2, bracing 18 cm^2."},
              toModel(std::move(d), kSteel, "Transmission tower")};
    }

    LibraryTruss offshoreJacket() {
      auto tower = latticeTower(4, 40.0, 12.0, 7.0, 400.0, 150.0);
      auto& d = tower.draft;
      for (const auto n : tower.level[4]) d.load(n, {0.0, -2500.0 * kKN, 0.0}); // topsides, 10 MN
      for (const std::size_t k : {1u, 2u}) {
        for (const auto n : tower.level[k]) d.load(n, {150.0 * kKN, 0.0, 0.0}); // waves and current
      }
      return {{"platform_offshore_jacket", "Offshore jacket platform", "Tower & Platform",
               "Four-leg steel jacket, 40 m high, battered from 24 x 24 m at the seabed to 14 x 14 m at the "
               "deck, X-braced faces and plan bracing at every level. Piles modelled as pins. 10 MN topsides "
               "(2.5 MN per leg) and 150 kN wave load per node at two levels. Steel, legs 400 cm^2, braces "
               "150 cm^2."},
              toModel(std::move(d), kSteel, "Offshore jacket")};
    }

    LibraryTruss craneJib() {
      constexpr std::uint32_t bays = 8;
      constexpr double length = 24.0, halfWidth = 0.8, depth = 2.2;
      Draft d;
      std::vector<std::array<std::uint32_t, 3>> station;
      for (std::uint32_t s = 0; s <= bays; ++s) {
        const double x = length * s / bays;
        station.push_back({d.node(x, 0.0, -halfWidth), d.node(x, 0.0, halfWidth), d.node(x, depth, 0.0)});
      }
      for (std::uint32_t s = 0; s <= bays; ++s) {
        const auto& [l, r, t] = station[s];
        d.bar(l, r, 12.0);
        d.bar(r, t, 12.0);
        d.bar(t, l, 12.0);
        if (s == 0) {
          for (const auto n : station[s]) d.pin(n);
        }
        if (s < bays) {
          const auto& [nl, nr, nt] = station[s + 1];
          d.bar(l, nl, 60.0);
          d.bar(r, nr, 60.0);
          d.bar(t, nt, 60.0);
          d.bar(l, nr, 12.0);
          d.bar(l, nt, 12.0);
          d.bar(r, nt, 12.0);
        }
      }
      d.load(station[bays][0], {0.0, -15.0 * kKN, 0.0}); // 3 t hook load at the tip
      d.load(station[bays][1], {0.0, -15.0 * kKN, 0.0});
      return {{"tower_crane_jib", "Crane jib (triangular lattice)", "Tower & Platform",
               "24 m cantilever jib with a triangular 1.6 m x 2.2 m section, 8 bays, laced on all three faces, "
               "fixed (pinned chords) at the mast. 30 kN hook load at the tip. Steel, chords 60 cm^2, lacing "
               "12 cm^2."},
              toModel(std::move(d), kSteel, "Crane jib")};
    }

  } // namespace end

  std::vector<LibraryTruss> buildLibrary() {
    std::vector<LibraryTruss> library;
    for (auto* build : {kingPostRoof, queenPostRoof, finkRoof, howeRoof, prattRoof, scissorsRoof, bowstringRoof,
                        prattBridge, howeBridge, warrenBridge, kTrussBridge, parkerBridge,
                        grandstandCantilever, spaceFrameRoof, schwedlerDome, geodesicDome,
                        transmissionTower, offshoreJacket, craneJib}) {
      library.push_back(build());
    }
    return library;
  }

  std::expected<std::vector<Entry>, std::string> writeLibrary(const std::filesystem::path& dir) {
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    if (ec) return std::unexpected(std::format("cannot create '{}': {}", anaf::IO::pathToUtf8(dir), ec.message()));

    std::vector<Entry> entries;
    for (const auto& truss : buildLibrary()) {
      anaf::IO::WriteOptions options;
      options.format = anaf::IO::FileFormat::Msh;
      options.mshVersion = anaf::IO::MshVersion::V4_1;
      options.encoding = anaf::IO::Encoding::Ascii;
      if (const auto written = anaf::IO::writeMesh(modelFile(dir, truss.entry), truss.model, options); !written) {
        return std::unexpected(std::format("{}: {}", truss.entry.id, written.error().message));
      }
      entries.push_back(truss.entry);
    }
    std::ofstream index(dir / std::filesystem::path(kIndexFile), std::ios::binary | std::ios::trunc);
    index << indexJson(entries);
    if (!index) return std::unexpected(std::format("cannot write '{}'", anaf::IO::pathToUtf8(dir / std::filesystem::path(kIndexFile))));
    return entries;
  }

  std::string indexJson(const std::span<const Entry> entries) {
    nlohmann::ordered_json models = nlohmann::ordered_json::array();
    for (const auto& entry : entries) {
      models.push_back({{"id", entry.id}, {"name", entry.name}, {"category", entry.category}, {"description", entry.description}});
    }
    nlohmann::ordered_json root;
    root["schemaVersion"] = 1;
    root["generator"] = "anaf_truss_library_tool (do not edit; regenerate instead)";
    root["models"] = std::move(models);
    return root.dump(2) + "\n";
  }

  std::expected<std::vector<Entry>, std::string> loadIndex(const std::filesystem::path& indexFile) {
    std::ifstream in(indexFile, std::ios::binary);
    if (!in) return std::unexpected(std::format("cannot open '{}'", anaf::IO::pathToUtf8(indexFile)));
    try {
      const auto root = nlohmann::json::parse(in);
      std::vector<Entry> entries;
      for (const auto& item : root.at("models")) {
        Entry entry{item.at("id").get<std::string>(), item.at("name").get<std::string>(), item.at("category").get<std::string>(),
                    item.at("description").get<std::string>()};
        const bool safeId = !entry.id.empty() && std::ranges::all_of(entry.id, [](const char c) {
          return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
        });
        if (!safeId) return std::unexpected(std::format("invalid model id '{}'", entry.id));
        entries.push_back(std::move(entry));
      }
      return entries;
    } catch (const std::exception& exception) {
      return std::unexpected(std::format("'{}': {}", anaf::IO::pathToUtf8(indexFile), exception.what()));
    }
  }

} // namespace FEM::TRUSS::LIBRARY end
