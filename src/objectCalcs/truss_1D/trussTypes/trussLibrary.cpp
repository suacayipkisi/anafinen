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
    constexpr MaterialRef kSteelS355{"Structural Steel S355 (EN 10025)", 3};
    constexpr MaterialRef kMildSteel{"Carbon Steel AISI 1020 (hot rolled)", 6};
    constexpr MaterialRef kDuralumin{"Aluminum 2024-T3", 10};
    constexpr MaterialRef kAluminum7075{"Aluminum 7075-T6", 13};

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

    LibraryTruss continuousBridge() {
      // Three 120 m spans, 10 m panels; the top chord is haunched from 14 m to 24 m over the two
      // piers, where the hogging moment of a continuous girder peaks.
      constexpr double panel = 10.0;
      constexpr std::uint32_t n = 36;
      const auto depth = [](const double x) {
        const double toPier = std::min(std::abs(x - 120.0), std::abs(x - 240.0));
        return 14.0 + 10.0 * std::max(0.0, 1.0 - toPier / 40.0);
      };
      Profile p;
      std::vector<std::uint32_t> b(n + 1), t(n + 1);
      for (std::uint32_t i = 0; i <= n; ++i) {
        const double x = panel * i;
        b[i] = p.add(x, 0.0);
        t[i] = p.add(x, depth(x));
      }
      for (std::uint32_t i = 0; i < n; ++i) {
        p.member(b[i], b[i + 1], 650.0);
        p.member(t[i], t[i + 1], 650.0);
        if (i % 2 == 0) p.member(b[i], t[i + 1], 350.0);
        else p.member(t[i], b[i + 1], 350.0);
      }
      for (std::uint32_t i = 0; i <= n; ++i) p.member(b[i], t[i], 350.0);
      p.pins = {b[12]};
      p.rollers = {b[0], b[24], b[36]};
      for (std::uint32_t i = 1; i < n; ++i) p.loaded.push_back(b[i]);
      return {{"bridge_continuous_three_span", "Three-span continuous truss bridge", "Bridge",
               "360 m railway bridge, three continuous 120 m spans, 10 m panels, Warren web with verticals. "
               "Depth 14 m in the spans, haunched to 24 m over the two piers. Two trusses 12 m apart, braced. "
               "Pinned on the first pier, rollers on the abutments and the second pier. 1800 kN per panel point "
               "(double track + deck). Steel S355, chords 650 cm^2, webs 350 cm^2."},
              toModel(extrude(p, 2, 12.0, 1800.0, 150.0), kSteelS355, "Continuous truss bridge")};
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

    LibraryTruss ovalStadiumRoof() {
      // A full ring of radial cantilever trusses over an oval bowl. Truss c stands on the back
      // ellipse (semi-axes 120 m x 95 m) and reaches 35 m towards the pitch. Neighbouring trusses
      // are tied like extrude(): a purlin at every node and a brace in every face swept by a member,
      // so the ring closes into one space truss with an inner compression / tension ring.
      constexpr std::uint32_t trusses = 48, panels = 7;
      constexpr double semiX = 120.0, semiZ = 95.0, reach = 35.0, eaves = 30.0, backDepth = 6.0;
      Draft d;
      std::vector<std::vector<std::uint32_t>> top(trusses), bottom(trusses);
      for (std::uint32_t c = 0; c < trusses; ++c) {
        const double theta = 2.0 * kPi * c / trusses;
        const double bx = semiX * std::cos(theta), bz = semiZ * std::sin(theta);
        const double back = std::hypot(bx, bz);
        for (std::uint32_t i = 0; i <= panels; ++i) {
          const double s = reach * i / panels;
          const double x = bx * (1.0 - s / back), z = bz * (1.0 - s / back);
          const double y = eaves + 0.05 * s; // rises towards the pitch, drains to the back gutter
          top[c].push_back(d.node(x, y, z));
          bottom[c].push_back(i < panels ? d.node(x, y - backDepth * (1.0 - s / reach), z) : top[c].back());
        }
      }
      struct Member {
        bool aTop;
        std::uint32_t a;
        bool bTop;
        std::uint32_t b;
        double area;
      };
      std::vector<Member> members;
      for (std::uint32_t i = 0; i < panels; ++i) {
        members.push_back({true, i, true, i + 1, 140.0});
        members.push_back({false, i, false, i + 1, 140.0});
        members.push_back({false, i, true, i, 60.0});
        if (i + 1 < panels) members.push_back({true, i, false, i + 1, 60.0});
      }
      const auto id = [&](const std::uint32_t c, const bool onTop, const std::uint32_t i) {
        return onTop ? top[c % trusses][i] : bottom[c % trusses][i];
      };
      for (std::uint32_t c = 0; c < trusses; ++c) {
        for (const auto& m : members) d.bar(id(c, m.aTop, m.a), id(c, m.bTop, m.b), m.area);
        for (std::uint32_t i = 0; i <= panels; ++i) {
          d.bar(id(c, true, i), id(c + 1, true, i), 35.0);
          d.bar(id(c, false, i), id(c + 1, false, i), 35.0);
        }
        for (const auto& m : members) d.bar(id(c, m.aTop, m.a), id(c + 1, m.bTop, m.b), 35.0);
        d.pin(top[c][0]);
        d.pin(bottom[c][0]);
      }
      // 1 kN/m^2 (cladding, snow, services) on the top chord, plus 20 kN of floodlights and
      // catwalk at every tip.
      const auto distance = [&](const std::uint32_t a, const std::uint32_t b) {
        const auto& p = d.nodes[a];
        const auto& q = d.nodes[b];
        return std::hypot(p[0] - q[0], p[1] - q[1], p[2] - q[2]);
      };
      for (std::uint32_t c = 0; c < trusses; ++c) {
        for (std::uint32_t i = 1; i <= panels; ++i) {
          const double width = 0.5 * (distance(id(c, true, i), id(c + 1, true, i)) + distance(id(c, true, i), id(c + trusses - 1, true, i)));
          const double length = reach / panels * (i == panels ? 0.5 : 1.0);
          d.load(top[c][i], {0.0, -1.0 * kKN * width * length - (i == panels ? 20.0 * kKN : 0.0), 0.0});
        }
      }
      return {{"stadium_oval_ring_roof", "Oval stadium roof (full ring)", "Stadium",
               "Complete roof of a 240 x 190 m oval stadium: 48 radial cantilever trusses reaching 35 m over "
               "the stands, 6 m deep at the back columns, tied by purlins and bracing into one closed ring with "
               "an open 170 x 120 m centre. Every truss pinned at its two back nodes. 1 kN/m^2 roof load plus "
               "20 kN floodlights per tip. Steel S355, chords 140 cm^2, webs 60 cm^2, ring bracing 35 cm^2."},
              toModel(std::move(d), kSteelS355, "Oval stadium roof")};
    }

    LibraryTruss archStadiumRoof() {
      // Wembley-style arch: parabolic axis, 315 m span, 133 m high, in a plane leaning 22 deg
      // from the vertical towards -z. Triangular lattice section (7.4 m sides), one triangulated
      // frame per station and Warren lacing in the three faces.
      constexpr double span = 315.0, rise = 133.0, side = 7.4;
      constexpr std::uint32_t bays = 60;
      const double lean = 22.0 * kPi / 180.0;
      const Point up{0.0, std::cos(lean), -std::sin(lean)};
      const Point out{0.0, std::sin(lean), std::cos(lean)};
      const double r = side / std::sqrt(3.0);
      Draft d;
      std::vector<std::array<std::uint32_t, 3>> st;
      for (std::uint32_t s = 0; s <= bays; ++s) {
        const double u = span * s / bays;
        const double xi = 2.0 * u / span - 1.0;
        const double v = rise * (1.0 - xi * xi);
        const double slope = -4.0 * rise * xi / span; // dv/du
        const double norm = std::hypot(1.0, slope);
        const double nx = -slope / norm, nv = 1.0 / norm; // in-plane normal (x, along up)
        std::array<std::uint32_t, 3> station{};
        for (std::size_t c = 0; c < 3; ++c) {
          const double alpha = 2.0 * kPi * static_cast<double>(c) / 3.0;
          const double a = r * std::cos(alpha), b = r * std::sin(alpha);
          const double inPlane = v + a * nv;
          station[c] = d.node(u - span / 2.0 + a * nx, inPlane * up[1] + b * out[1], inPlane * up[2] + b * out[2]);
        }
        st.push_back(station);
      }
      for (std::uint32_t s = 0; s <= bays; ++s) {
        for (std::size_t c = 0; c < 3; ++c) {
          const std::size_t next = (c + 1) % 3;
          d.bar(st[s][c], st[s][next], 150.0);
          if (s == 0 || s == bays) d.pin(st[s][c]);
          if (s == bays) continue;
          d.bar(st[s][c], st[s + 1][c], 450.0);
          if ((s + c) % 2 == 0) d.bar(st[s][c], st[s + 1][next], 120.0);
          else d.bar(st[s][next], st[s + 1][c], 120.0);
        }
      }
      // Roof cables hang from the two lower chords and pull down and back towards the stands
      // (32 deg from the vertical), which also balances the lean of the arch's own weight.
      const double cable = 32.0 * kPi / 180.0;
      for (std::uint32_t s = 2; s + 2 <= bays; ++s) {
        for (const std::size_t c : {1u, 2u}) d.load(st[s][c], {0.0, -125.0 * kKN * std::cos(cable), 125.0 * kKN * std::sin(cable)});
      }
      return {{"stadium_wembley_arch", "Leaning stadium arch (Wembley style)", "Stadium",
               "315 m span, 133 m high parabolic arch leaning 22 deg from the vertical, triangular lattice "
               "section with 7.4 m sides, 60 bays. Both feet pinned. The roof hangs from the lower chords: 250 kN "
               "of cable force per station, 32 deg from the vertical towards the stands. Steel S355, chords "
               "450 cm^2, frames 150 cm^2, lacing 120 cm^2."},
              toModel(std::move(d), kSteelS355, "Leaning stadium arch")};
    }

    LibraryTruss kiewittDome() {
      // Kiewitt (lamella) dome: six sectors, ring k carries 6 k nodes, every face a triangle.
      // 210 m span, 36 m rise on a sphere, the class of the large covered stadium domes.
      constexpr std::uint32_t rings = 12, sectors = 6;
      constexpr double halfSpan = 105.0, rise = 36.0;
      const double sphere = (halfSpan * halfSpan + rise * rise) / (2.0 * rise);
      const double baseAngle = std::asin(halfSpan / sphere);
      const double baseY = sphere * std::cos(baseAngle);
      Draft d;
      std::vector<std::vector<std::uint32_t>> ring(rings + 1);
      ring[0] = {d.node(0.0, sphere - baseY, 0.0)};
      for (std::uint32_t k = 1; k <= rings; ++k) {
        const double theta = baseAngle * k / rings;
        const std::uint32_t count = sectors * k;
        for (std::uint32_t i = 0; i < count; ++i) {
          const double phi = 2.0 * kPi * i / count;
          ring[k].push_back(d.node(sphere * std::sin(theta) * std::cos(phi), sphere * std::cos(theta) - baseY,
                                   sphere * std::sin(theta) * std::sin(phi)));
        }
      }
      const auto at = [&](const std::uint32_t k, const std::uint32_t i) { return ring[k][i % ring[k].size()]; };
      for (std::uint32_t k = 0; k < rings; ++k) {
        const bool base = k + 1 == rings;
        for (std::uint32_t s = 0; s < sectors; ++s) {
          for (std::uint32_t i = 0; i <= k; ++i) {
            d.bar(at(k, s * k + i), at(k + 1, s * (k + 1) + i), i == 0 ? 260.0 : 180.0); // i == 0: main rib
            d.bar(at(k, s * k + i), at(k + 1, s * (k + 1) + i + 1), 180.0);
            d.bar(at(k + 1, s * (k + 1) + i), at(k + 1, s * (k + 1) + i + 1), base ? 600.0 : 180.0);
          }
        }
      }
      for (const auto n : ring[rings]) d.pin(n);
      // 1.2 kN/m^2 on plan (roofing, snow, catwalks), lumped by the plan area around each node.
      const auto planRadius = [&](const double k) { return sphere * std::sin(baseAngle * k / rings); };
      d.load(ring[0][0], {0.0, -1.2 * kKN * kPi * std::pow(planRadius(0.5), 2), 0.0});
      for (std::uint32_t k = 1; k < rings; ++k) {
        const double area = kPi * (std::pow(planRadius(k + 0.5), 2) - std::pow(planRadius(k - 0.5), 2)) / (sectors * k);
        for (const auto n : ring[k]) d.load(n, {0.0, -1.2 * kKN * area, 0.0});
      }
      return {{"stadium_kiewitt_dome", "Kiewitt dome (210 m stadium)", "Stadium",
               "Single-layer Kiewitt (lamella) dome over a 210 m circle, 36 m rise: 6 main ribs, 12 rings, "
               "every panel triangulated, 469 nodes. Pinned on the base tension ring. 1.2 kN/m^2 on plan "
               "(roofing, snow, catwalks). Steel S355, ribs 260 cm^2, other members 180 cm^2, base ring "
               "600 cm^2."},
              toModel(std::move(d), kSteelS355, "Kiewitt dome")};
    }

    // ---- towers and platforms ------------------------------------------------------------------

    // Square lattice tower: levels of 4 nodes (corners at +-halfWidth, tapering linearly), legs,
    // level ring with one plan diagonal, X-bracing on the four faces. Base nodes pinned.
    struct Tower {
      Draft draft;
      std::vector<std::array<std::uint32_t, 4>> level;
    };
    // halfWidth(k), legArea(k) and braceArea(k) per level k = 0..levels; the legs and face
    // braces between level k and k + 1 use the areas of level k.
    Tower latticeTower(const int levels, const double height, const std::function<double(int)>& halfWidth,
                       const std::function<double(int)>& legArea, const std::function<double(int)>& braceArea) {
      Tower tower;
      auto& d = tower.draft;
      for (int k = 0; k <= levels; ++k) {
        const double y = height * k / levels;
        const double w = halfWidth(k);
        tower.level.push_back({d.node(-w, y, -w), d.node(w, y, -w), d.node(w, y, w), d.node(-w, y, w)});
      }
      for (std::size_t k = 0; k < tower.level.size(); ++k) {
        const auto& ring = tower.level[k];
        const double brace = braceArea(static_cast<int>(k));
        for (std::size_t c = 0; c < 4; ++c) {
          d.bar(ring[c], ring[(c + 1) % 4], brace);
          if (k == 0) d.pin(ring[c]);
          if (k + 1 < tower.level.size()) {
            const auto& up = tower.level[k + 1];
            d.bar(ring[c], up[c], legArea(static_cast<int>(k)));
            d.bar(ring[c], up[(c + 1) % 4], brace);
            d.bar(ring[(c + 1) % 4], up[c], brace);
          }
        }
        if (k > 0) d.bar(ring[0], ring[2], brace); // plan bracing keeps the section square
      }
      return tower;
    }
    Tower latticeTower(const int levels, const double height, const double baseHalf, const double topHalf,
                       const double legArea, const double braceArea) {
      return latticeTower(levels, height, [=](const int k) { return baseHalf + (topHalf - baseHalf) * k / levels; },
                          [=](int) { return legArea; }, [=](int) { return braceArea; });
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

    LibraryTruss eiffelTower() {
      // 300 m wrought-iron style lattice tower; the plan width follows the exponential curve
      // that makes the Eiffel Tower's legs (125 m square base, about 10 m at the top).
      constexpr int levels = 24;
      constexpr double height = 300.0;
      const auto half = [](const int k) { return 3.0 + 59.5 * std::exp(-height * k / levels / 75.0); };
      auto tower = latticeTower(levels, height, half, [](const int k) { return 1600.0 * (1.0 - 0.9 * k / levels); },
                                [](const int k) { return 260.0 * (1.0 - 0.8 * k / levels); });
      auto& d = tower.draft;
      for (const auto& [k, total] : {std::pair{5, 8000.0}, {9, 4000.0}, {22, 1200.0}}) { // platforms
        for (const auto n : tower.level[static_cast<std::size_t>(k)]) d.load(n, {0.0, -total / 4.0 * kKN, 0.0});
      }
      // Wind: 1.2 kN/m^2 on the face, 30 % solid, along +x.
      for (int k = 1; k <= levels; ++k) {
        const double force = 1.2 * 2.0 * half(k) * (height / levels) * 0.3;
        for (const auto n : tower.level[static_cast<std::size_t>(k)]) d.load(n, {force / 4.0 * kKN, 0.0, 0.0});
      }
      return {{"tower_eiffel_style", "300 m lattice tower (Eiffel style)", "Tower & Platform",
               "300 m square lattice tower, 24 levels, the plan narrowing exponentially from 125 m at the base "
               "to 10 m at the top; X-braced faces, plan bracing at every level. Base legs pinned. Platforms: "
               "8 MN at 62.5 m, 4 MN at 112.5 m, 1.2 MN at 275 m; wind 1.2 kN/m^2 on a 30 % solid face. Mild "
               "steel, legs 1600 cm^2 tapering to 160 cm^2, bracing 260 to 52 cm^2."},
              toModel(std::move(d), kMildSteel, "Lattice tower (Eiffel style)")};
    }

    // ---- aircraft --------------------------------------------------------------------------------

    // Face k of a box girder joins corner k and corner k + 1 of every station. Rising: corner k
    // of station s to corner k + 1 of station s + 1; Falling: the other diagonal; Warren*:
    // alternating, starting with that direction; Cross: both.
    enum class Brace { Rising, Falling, WarrenRising, WarrenFalling, Cross };

    struct BoxGirder {
      std::vector<std::array<Point, 4>> stations;
      std::array<Brace, 4> faces{};
      double chordArea{};  // cm^2, corner to corner along the girder
      double frameArea{};  // cm^2, station frames (bulkheads, ribs, interplane struts)
      double braceArea{};  // cm^2, face diagonals (lacing, wires)
      std::function<double(std::size_t)> chordAreaOf; // cm^2 of the chords in bay s; empty: chordArea
      bool rootFrame{true}; // false: station 0 is fully supported, its frame carries nothing
    };

    // Stations of four corners, chords along the corners, a triangulated frame at every station
    // and one diagonal (or two) in every face of every bay: each bay is a closed triangulated
    // polyhedron, so the girder is a stable space truss. Returns the node ids per station.
    std::vector<std::array<std::uint32_t, 4>> boxGirder(Draft& d, const BoxGirder& g) {
      std::vector<std::array<std::uint32_t, 4>> id;
      for (const auto& corners : g.stations) {
        std::array<std::uint32_t, 4> station{};
        for (std::size_t c = 0; c < 4; ++c) station[c] = d.node(corners[c][0], corners[c][1], corners[c][2]);
        id.push_back(station);
      }
      for (std::size_t s = 0; s < id.size(); ++s) {
        if (s > 0 || g.rootFrame) {
          for (std::size_t c = 0; c < 4; ++c) d.bar(id[s][c], id[s][(c + 1) % 4], g.frameArea);
          d.bar(id[s][0], id[s][2], g.frameArea);
        }
        if (s + 1 == id.size()) continue;
        for (std::size_t c = 0; c < 4; ++c) {
          const std::size_t next = (c + 1) % 4;
          d.bar(id[s][c], id[s + 1][c], g.chordAreaOf ? g.chordAreaOf(s) : g.chordArea);
          const bool even = s % 2 == 0;
          const auto brace = g.faces[c];
          const bool rising = brace == Brace::Rising || brace == Brace::Cross || (brace == Brace::WarrenRising && even)
                              || (brace == Brace::WarrenFalling && !even);
          const bool falling = brace == Brace::Falling || brace == Brace::Cross || (brace == Brace::WarrenFalling && even)
                               || (brace == Brace::WarrenRising && !even);
          if (rising) d.bar(id[s][c], id[s + 1][next], g.braceArea);
          if (falling) d.bar(id[s][next], id[s + 1][c], g.braceArea);
        }
      }
      return id;
    }

    LibraryTruss tubeFuselage() {
      // x aft from the firewall, y up, z to the right. Corners: 0 lower left, 1 upper left,
      // 2 upper right, 3 lower right. Constant cabin section up to x = 1.8 m, then the bottom
      // longerons rise and the top longerons drop towards the tail post.
      constexpr std::array<double, 10> xs{0.0, 0.6, 1.2, 1.8, 2.4, 3.1, 3.8, 4.5, 5.2, 5.9};
      BoxGirder g;
      for (const double x : xs) {
        const double t = std::max(0.0, (x - 1.8) / (5.9 - 1.8));
        const double half = 0.375 + (0.075 - 0.375) * t;
        const double bottom = 0.55 * t, top = 1.15 - 0.2 * t;
        g.stations.push_back({Point{x, bottom, -half}, Point{x, top, -half}, Point{x, top, half}, Point{x, bottom, half}});
      }
      g.faces = {Brace::WarrenRising, Brace::WarrenRising, Brace::WarrenFalling, Brace::WarrenRising}; // sides mirrored
      g.chordArea = 0.94; // 1" x 0.049" tube
      g.frameArea = 0.72; // 3/4" x 0.049"
      g.braceArea = 0.72;
      Draft d;
      const auto st = boxGirder(d, g);
      for (const std::size_t s : {1u, 2u}) { // wing spar fittings on the upper longerons
        d.pin(st[s][1]);
        d.pin(st[s][2]);
      }
      // 3.8 g pull-up (normal category limit): inertia loads act downwards.
      for (const auto n : st[0]) d.load(n, {0.0, -1.03 * kKN, 0.0});   // engine + mount, 110 kg
      for (const std::size_t s : {1u, 2u, 3u}) {                        // two occupants + seats, 180 kg
        d.load(st[s][0], {0.0, -1.12 * kKN, 0.0});
        d.load(st[s][3], {0.0, -1.12 * kKN, 0.0});
      }
      d.load(st[4][0], {0.0, -0.37 * kKN, 0.0}); // baggage, 20 kg
      d.load(st[4][3], {0.0, -0.37 * kKN, 0.0});
      d.load(st[9][1], {0.0, -0.9 * kKN, 0.0});  // tail group weight + balancing tail download
      d.load(st[9][2], {0.0, -0.9 * kKN, 0.0});
      return {{"aircraft_tube_fuselage", "Welded steel-tube fuselage", "Aircraft",
               "Light two-seat fuselage (Piper Cub style), 5.9 m from firewall to tail post, 0.75 x 1.15 m "
               "cabin tapering to the tail, Warren-braced sides, top and bottom. Held at the four wing spar "
               "fittings on the upper longerons. 3.8 g pull-up: engine 4.1 kN, occupants 6.7 kN, baggage, 1.8 kN "
               "tail load. 4130 tube, longerons 0.94 cm^2 (1\" x 0.049\"), lacing 0.72 cm^2 (3/4\" x 0.049\")."},
              toModel(std::move(d), kSteel, "Steel tube fuselage")};
    }

    LibraryTruss engineMount() {
      // Firewall at x = 0 (pinned), engine bed ring 0.5 m forward (x < 0), engine CG 0.35 m ahead
      // of the bed, tied to the four bed points by stiff links standing in for the crankcase.
      Draft d;
      std::array<std::uint32_t, 4> wall{}, bed{};
      constexpr std::array<std::array<double, 2>, 4> wallYZ{{{0.3, -0.3}, {0.9, -0.3}, {0.9, 0.3}, {0.3, 0.3}}};
      constexpr std::array<std::array<double, 2>, 4> bedYZ{{{0.45, -0.2}, {0.75, -0.2}, {0.75, 0.2}, {0.45, 0.2}}};
      for (std::size_t c = 0; c < 4; ++c) {
        wall[c] = d.node(0.0, wallYZ[c][0], wallYZ[c][1]);
        bed[c] = d.node(-0.5, bedYZ[c][0], bedYZ[c][1]);
        d.pin(wall[c]);
      }
      constexpr double tube = 0.72; // 3/4" x 0.049"
      for (std::size_t c = 0; c < 4; ++c) {
        const std::size_t next = (c + 1) % 4;
        d.bar(wall[c], bed[c], tube);    // straight legs
        d.bar(wall[next], bed[c], tube); // V with the neighbouring leg
        d.bar(bed[c], bed[next], tube);  // bed ring
      }
      d.bar(bed[0], bed[2], tube);
      const auto cg = d.node(-0.85, 0.6, 0.0);
      for (const auto n : bed) d.bar(cg, n, 20.0);
      // 110 kg engine at 3.8 g plus 2.2 kN take-off thrust, forward along -x.
      d.load(cg, {-2.2 * kKN, -4.1 * kKN, 0.0});
      return {{"aircraft_engine_mount", "Engine mount (welded tube)", "Aircraft",
               "Four-point welded 4130 engine mount, 0.6 x 0.6 m firewall pattern to a 0.4 x 0.3 m engine bed "
               "0.5 m forward: straight legs and V tubes, braced bed ring. Firewall fittings pinned. The engine "
               "CG sits 0.35 m ahead of the bed on four stiff links (crankcase). 110 kg at 3.8 g (4.1 kN) plus "
               "2.2 kN thrust. Tubes 0.72 cm^2 (3/4\" x 0.049\")."},
              toModel(std::move(d), kSteel, "Engine mount")};
    }

    LibraryTruss strutBracedWing() {
      // Half wing: x chordwise (aft), y up, z spanwise from the fuselage side. Corners: 0 front
      // spar bottom, 1 front spar top, 2 rear spar top, 3 rear spar bottom. Truss spars (faces 0
      // and 2), drag trusses in the upper and lower surfaces (faces 1 and 3), a rib at every station.
      constexpr double semiSpan = 5.0;
      constexpr std::uint32_t bays = 20;
      BoxGirder g;
      for (std::uint32_t s = 0; s <= bays; ++s) {
        const double z = semiSpan * s / bays;
        g.stations.push_back({Point{0.4, 0.0, z}, Point{0.4, 0.19, z}, Point{1.2, 0.15, z}, Point{1.2, 0.02, z}});
      }
      g.faces = {Brace::WarrenRising, Brace::Cross, Brace::WarrenFalling, Brace::Cross};
      g.chordArea = 4.0;
      g.frameArea = 1.2;
      g.braceArea = 1.2;
      g.rootFrame = false;
      Draft d;
      const auto st = boxGirder(d, g);
      for (const auto n : st[0]) d.pin(n); // spar root fittings

      // Lift strut pair (V strut) from one fuselage fitting to both spars at 60 % semi-span.
      constexpr std::uint32_t strutStation = 12;
      const auto fitting = d.node(0.8, -1.1, 0.0);
      d.pin(fitting);
      d.bar(fitting, st[strutStation][0], 3.0);
      d.bar(fitting, st[strutStation][3], 3.0);

      // Schrenk lift distribution (mean of planform and ellipse), 10 kN per half wing on the
      // ribs, 70 % on the front spar and 30 % on the rear spar.
      constexpr double lift = 10.0;
      std::vector<double> weight(bays + 1, 0.0);
      double sum = 0.0;
      for (std::uint32_t s = 1; s <= bays; ++s) {
        const double eta = static_cast<double>(s) / bays;
        weight[s] = (s == bays ? 0.5 : 1.0) * 0.5 * (1.0 + 4.0 / kPi * std::sqrt(1.0 - eta * eta));
        sum += weight[s];
      }
      for (std::uint32_t s = 1; s <= bays; ++s) {
        const double station = lift * kKN * weight[s] / sum;
        d.load(st[s][1], {0.0, 0.7 * station, 0.0});
        d.load(st[s][2], {0.0, 0.3 * station, 0.0});
      }
      return {{"aircraft_strut_braced_wing", "Strut-braced high wing", "Aircraft",
               "Half wing of a light high-wing aircraft, 5 m semi-span, 1.6 m chord, truss spars 19 cm (front) "
               "and 13 cm (rear) deep, ribs every 25 cm with drag / anti-drag bracing in both skins. Root "
               "fittings pinned; a V lift strut runs from a pinned fuselage fitting to both spars at 3 m. 3.8 g: "
               "10 kN lift in a Schrenk distribution, 70 / 30 % front / rear spar. Aluminum 6061-T6, spar caps "
               "4 cm^2, webs and ribs 1.2 cm^2, struts 3 cm^2."},
              toModel(std::move(d), kAluminum, "Strut-braced wing")};
    }

    LibraryTruss biplaneWingCell() {
      // Two-bay biplane cell, half span: x chordwise (aft), y up, z spanwise. Corners: 0 lower
      // front spar, 1 upper front spar, 2 upper rear spar, 3 lower rear spar. The front and rear
      // planes are the classic wire-braced truss: interplane struts (frames) and flying wires
      // running from the lower wing up and outboard; landing wires are slack under positive g
      // and left out. Drag and anti-drag wires cross in both wing planes.
      constexpr std::array<double, 3> zs{0.0, 2.3, 4.6};
      constexpr double chord = 1.5, gap = 1.5, stagger = 0.3;
      BoxGirder g;
      for (const double z : zs) {
        g.stations.push_back({Point{0.3 + stagger, 0.0, z}, Point{0.3, gap, z}, Point{0.3 + 0.6 * chord, gap, z},
                              Point{0.3 + stagger + 0.6 * chord, 0.0, z}});
      }
      g.faces = {Brace::Rising, Brace::Cross, Brace::Falling, Brace::Cross};
      g.chordArea = 6.0; // spars
      g.frameArea = 4.0; // interplane struts and compression ribs
      g.braceArea = 0.8; // doubled streamline wires
      g.rootFrame = false;
      Draft d;
      const auto st = boxGirder(d, g);
      for (const auto n : st[0]) d.pin(n); // cabane (upper) and fuselage (lower) spar roots

      // 660 kg at 4.5 g: 14.6 kN per half cell; inner struts take half, outer struts a quarter.
      // Upper wing 55 %, lower 45 %; front spar 65 %, rear 35 %.
      for (const auto& [s, share] : {std::pair{std::size_t{1}, 7.3}, {std::size_t{2}, 3.65}}) {
        d.load(st[s][1], {0.0, share * 0.55 * 0.65 * kKN, 0.0});
        d.load(st[s][2], {0.0, share * 0.55 * 0.35 * kKN, 0.0});
        d.load(st[s][0], {0.0, share * 0.45 * 0.65 * kKN, 0.0});
        d.load(st[s][3], {0.0, share * 0.45 * 0.35 * kKN, 0.0});
      }
      return {{"aircraft_biplane_wing_cell", "Biplane wing cell (two-bay)", "Aircraft",
               "Half wing cell of a WWI-era two-bay biplane: 4.6 m, 1.5 m chord and gap, 0.3 m stagger. "
               "Interplane struts and flying wires make a Pratt-like truss in the front and rear spar planes; "
               "drag wires cross in both wings. Spar roots pinned at the cabane and fuselage. 660 kg at 4.5 g: "
               "14.6 kN lift, 55 / 45 % upper / lower wing. Steel: spars 6 cm^2, struts and ribs 4 cm^2, "
               "wires 0.8 cm^2."},
              toModel(std::move(d), kSteel, "Biplane wing cell")};
    }

    // Weight [N] of the bars built so far (the solver adds the same self weight).
    double structureWeight(const Draft& d, const double density) {
      double weight = 0.0;
      for (const auto& bar : d.bars) {
        const auto& a = d.nodes[bar.a];
        const auto& b = d.nodes[bar.b];
        weight += density * 9.80665 * bar.area * std::hypot(b[0] - a[0], b[1] - a[1], b[2] - a[2]);
      }
      return weight;
    }

    LibraryTruss rigidAirship() {
      // Zeppelin-type hull: x aft from the nose, y up, z to starboard. 19 polygonal main rings
      // (16 sides, node 0 on the keel), each wire-braced to a hub on the axis; longitudinal
      // girders between the rings, crossed shear wires in every hull panel, an axial wire from
      // the nose through every hub to the tail cone.
      constexpr double length = 200.0, maxRadius = 15.0;
      constexpr std::uint32_t rings = 19, sides = 16;
      Draft d;
      const auto nose = d.node(0.0, 0.0, 0.0);
      std::vector<std::array<std::uint32_t, sides>> ring(rings);
      std::vector<std::uint32_t> hub(rings);
      std::vector<double> xs(rings), radius(rings);
      for (std::uint32_t k = 0; k < rings; ++k) {
        const double xi = (k + 1.0) / (rings + 1.0);
        xs[k] = length * xi;
        radius[k] = maxRadius * std::sqrt(1.0 - (2.0 * xi - 1.0) * (2.0 * xi - 1.0));
        hub[k] = d.node(xs[k], 0.0, 0.0);
        for (std::uint32_t j = 0; j < sides; ++j) {
          const double phi = 2.0 * kPi * j / sides;
          ring[k][j] = d.node(xs[k], -radius[k] * std::cos(phi), radius[k] * std::sin(phi));
        }
      }
      const auto tail = d.node(length, 0.0, 0.0);
      d.bar(nose, hub[0], 6.0);
      d.bar(hub[rings - 1], tail, 6.0);
      for (std::uint32_t k = 0; k < rings; ++k) {
        if (k + 1 < rings) d.bar(hub[k], hub[k + 1], 6.0);
        for (std::uint32_t j = 0; j < sides; ++j) {
          const std::uint32_t next = (j + 1) % sides;
          d.bar(ring[k][j], ring[k][next], 12.0); // main ring girder
          d.bar(hub[k], ring[k][j], 2.0);         // radial wire bracing
          if (k == 0) d.bar(nose, ring[k][j], 12.0);
          if (k + 1 == rings) d.bar(ring[k][j], tail, 12.0);
          if (k + 1 < rings) {
            d.bar(ring[k][j], ring[k + 1][j], 12.0); // longitudinal girder
            d.bar(ring[k][j], ring[k + 1][next], 1.5);
            d.bar(ring[k][next], ring[k + 1][j], 1.5);
          }
        }
      }
      // Moored: nose cone on the mast (pinned), tail cone held vertically and sideways by the
      // stern handling party, one keel node held sideways against roll.
      d.pin(nose);
      d.fix(tail, {false, true, true});
      d.fix(ring[rings / 2][0], {false, false, true});

      // Hydrogen lift (11 N/m^3) of every gas cell, half to each bounding ring, on the upper
      // ring nodes. Five engine cars of 2.5 t and a 4 t control car; the rest of the useful load
      // (fuel, ballast, crew, cargo) on the keel nodes, so the ship floats in equilibrium.
      constexpr double liftPerM3 = 11.0;
      std::vector<double> ringLift(rings, 0.0);
      const auto cone = [](const double h, const double r1, const double r2) { return kPi * h / 3.0 * (r1 * r1 + r1 * r2 + r2 * r2); };
      ringLift[0] += liftPerM3 * cone(xs[0], 0.0, radius[0]);
      ringLift[rings - 1] += liftPerM3 * cone(length - xs[rings - 1], radius[rings - 1], 0.0);
      for (std::uint32_t k = 0; k + 1 < rings; ++k) {
        const double cell = liftPerM3 * cone(xs[k + 1] - xs[k], radius[k], radius[k + 1]);
        ringLift[k] += 0.5 * cell;
        ringLift[k + 1] += 0.5 * cell;
      }
      double totalLift = 0.0;
      for (std::uint32_t k = 0; k < rings; ++k) {
        totalLift += ringLift[k];
        std::vector<std::uint32_t> upper;
        for (const auto n : ring[k]) {
          if (d.nodes[n][1] > 1e-6 * radius[k]) upper.push_back(n);
        }
        for (const auto n : upper) d.load(n, {0.0, ringLift[k] / static_cast<double>(upper.size()), 0.0});
      }
      constexpr double engineCar = 2.5 * 9.80665 * kKN, controlCar = 4.0 * 9.80665 * kKN;
      for (const auto& [k, j] : {std::pair{5u, 3u}, {5u, 13u}, {10u, 3u}, {10u, 13u}, {15u, 0u}}) d.load(ring[k][j], {0.0, -engineCar, 0.0});
      d.load(ring[2][0], {0.0, -controlCar / 2.0, 0.0});
      d.load(ring[3][0], {0.0, -controlCar / 2.0, 0.0});
      const double usefulLoad = totalLift - structureWeight(d, 2780.0) - 5.0 * engineCar - controlCar;
      std::vector<std::uint32_t> keel;
      for (std::uint32_t k = 1; k + 1 < rings; ++k) {
        for (const std::uint32_t j : {sides - 1, 0u, 1u}) keel.push_back(ring[k][j]);
      }
      for (const auto n : keel) d.load(n, {0.0, -usefulLoad / static_cast<double>(keel.size()), 0.0});
      return {{"aircraft_rigid_airship", "Rigid airship hull (Zeppelin type)", "Aircraft",
               "200 m hull, 30 m diameter: 19 sixteen-sided main rings wire-braced to an axial wire, "
               "longitudinal girders, crossed shear wires in every panel, nose and tail cones. Moored: nose "
               "pinned on the mast, tail held vertically and sideways. Hydrogen lift of each gas cell on the "
               "upper ring nodes; five 2.5 t engine cars, a 4 t control car and the remaining useful load on "
               "the keel, so lift and weight balance. Duralumin (2024-T3): girders 12 cm^2, axial wire 6 cm^2, "
               "radial wires 2 cm^2, shear wires 1.5 cm^2."},
              toModel(std::move(d), kDuralumin, "Rigid airship hull")};
    }

    LibraryTruss geodeticFuselage() {
      // Vickers Wellington style geodetic (diagrid) fuselage: x aft from the nose, y up, z to
      // starboard. 31 stations, 0.6 m apart, of 20 nodes on an oval; odd stations are turned by
      // half a pitch, so the members between stations run as two crossing helices. Circumferential
      // members close every triangle; six triangulated bulkheads keep the section in shape.
      constexpr std::uint32_t stations = 31, around = 20;
      constexpr double pitch = 0.6;
      Draft d;
      std::vector<std::array<std::uint32_t, around>> st(stations);
      for (std::uint32_t k = 0; k < stations; ++k) {
        const double x = pitch * k;
        const double s = x < 2.4 ? 0.55 + 0.45 * std::sin(x / 2.4 * kPi / 2.0)
                                 : (x <= 9.0 ? 1.0 : 1.0 - 0.65 * (x - 9.0) / 9.0);
        const double halfHeight = 1.6 * s, halfWidth = 1.2 * s, centre = 0.6 * std::max(0.0, (x - 9.0) / 9.0);
        for (std::uint32_t j = 0; j < around; ++j) {
          const double phi = 2.0 * kPi * (j + 0.5 * (k % 2)) / around;
          st[k][j] = d.node(x, centre - halfHeight * std::cos(phi), halfWidth * std::sin(phi));
        }
      }
      for (std::uint32_t k = 0; k < stations; ++k) {
        for (std::uint32_t j = 0; j < around; ++j) {
          d.bar(st[k][j], st[k][(j + 1) % around], 3.0);
          if (k + 1 == stations) continue;
          // Even station node j sits between odd nodes j - 1 and j; odd node j between even j and j + 1.
          const std::uint32_t left = k % 2 == 0 ? (j + around - 1) % around : j;
          const std::uint32_t right = k % 2 == 0 ? j : (j + 1) % around;
          d.bar(st[k][j], st[k + 1][left], 5.0);
          d.bar(st[k][j], st[k + 1][right], 5.0);
        }
        if (k % 6 == 0) { // bulkhead: zig-zag triangulation of the polygon
          std::vector<std::uint32_t> order{0};
          for (std::uint32_t a = 1, b = around - 1; a <= b; ++a, --b) {
            order.push_back(a);
            if (a != b) order.push_back(b);
          }
          for (std::size_t i = 0; i + 1 < order.size(); ++i) d.bar(st[k][order[i]], st[k][order[i + 1]], 3.0);
        }
      }
      // Wing centre-section spar fittings: the side nodes of stations 12 and 14.
      for (const std::uint32_t k : {12u, 14u}) {
        d.pin(st[k][around / 4]);
        d.pin(st[k][3 * around / 4]);
      }
      // 3 g pull-up: nose turret 400 kg, tail turret 500 kg, five crew of 100 kg in the cockpit
      // and fuselage, 2000 kg of bombs in the bay (stations 10..18), 10 kN tail download.
      constexpr double g3 = 3.0 * 9.80665 / 1000.0 * kKN; // N per kg at 3 g
      for (const auto n : st[0]) d.load(n, {0.0, -400.0 * g3 / around, 0.0});
      for (const auto n : st[stations - 1]) d.load(n, {0.0, -500.0 * g3 / around, 0.0});
      const auto bottom = [&](const std::uint32_t k, const double limit) {
        std::vector<std::uint32_t> nodes;
        for (std::uint32_t j = 0; j < around; ++j) {
          if (std::cos(2.0 * kPi * (j + 0.5 * (k % 2)) / around) > limit) nodes.push_back(st[k][j]);
        }
        return nodes;
      };
      for (std::uint32_t k = 3; k <= 7; ++k) {
        const auto floor = bottom(k, 0.8);
        for (const auto n : floor) d.load(n, {0.0, -500.0 * g3 / 5.0 / static_cast<double>(floor.size()), 0.0});
      }
      for (std::uint32_t k = 10; k <= 18; ++k) {
        const auto bay = bottom(k, 0.8);
        for (const auto n : bay) d.load(n, {0.0, -2000.0 * g3 / 9.0 / static_cast<double>(bay.size()), 0.0});
      }
      for (const std::uint32_t k : {stations - 2, stations - 1}) {
        std::vector<std::uint32_t> topNodes;
        for (std::uint32_t j = 0; j < around; ++j) {
          if (std::cos(2.0 * kPi * (j + 0.5 * (k % 2)) / around) < -0.8) topNodes.push_back(st[k][j]);
        }
        for (const auto n : topNodes) d.load(n, {0.0, -5.0 * kKN / static_cast<double>(topNodes.size()), 0.0});
      }
      return {{"aircraft_geodetic_fuselage", "Geodetic bomber fuselage (Wellington style)", "Aircraft",
               "18 m geodetic fuselage of a twin-engine bomber: a diagrid of crossing helical members on an "
               "oval section (2.4 x 3.2 m) that tapers and rises towards the tail, 31 stations, six triangulated "
               "bulkheads, 620 nodes. Pinned at the four wing centre-section fittings. 3 g pull-up: turrets "
               "400 / 500 kg, five crew, 2000 kg bomb load, 10 kN tail download. Duralumin (2024-T3): geodetic "
               "members 5 cm^2, circumferential members and bulkheads 3 cm^2."},
              toModel(std::move(d), kDuralumin, "Geodetic fuselage")};
    }

    LibraryTruss airlinerWingBox() {
      // Full-span wing box of a narrow-body airliner: x aft, y up, z spanwise (to starboard).
      // A 4 m centre box inside the fuselage and two 15 m outer boxes with 25 deg sweep, 5 deg
      // dihedral and linear taper; ribs every 0.6 m. Corners: 0 front spar bottom, 1 front spar
      // top, 2 rear spar top, 3 rear spar bottom. Spar webs are Warren-laced, the skins cross-braced.
      std::vector<double> zs;
      for (int j = 0; j < 25; ++j) zs.push_back(-17.0 + 0.6 * j);
      for (int j = -2; j <= 2; ++j) zs.push_back(j);
      for (int j = 1; j <= 25; ++j) zs.push_back(2.0 + 0.6 * j);
      const auto eta = [](const double z) { return std::max(0.0, (std::abs(z) - 2.0) / 15.0); };
      const double sweep = std::tan(25.0 * kPi / 180.0), dihedral = std::tan(5.0 * kPi / 180.0);
      const auto frontSpar = [&](const double z) {
        const double out = std::max(0.0, std::abs(z) - 2.0);
        return std::array<double, 2>{out * sweep, out * dihedral};
      };
      BoxGirder g;
      for (const double z : zs) {
        const double e = eta(z);
        const double width = 3.2 - 2.4 * e, front = 0.85 - 0.6 * e, rear = 0.7 * front;
        const auto [x, y] = frontSpar(z);
        g.stations.push_back({Point{x, y - front / 2, z}, Point{x, y + front / 2, z}, Point{x + width, y + rear / 2, z},
                              Point{x + width, y - rear / 2, z}});
      }
      g.faces = {Brace::WarrenRising, Brace::Cross, Brace::WarrenRising, Brace::Cross};
      g.chordAreaOf = [&](const std::size_t s) { return 400.0 * (1.0 - 0.6 * eta(0.5 * (zs[s] + zs[s + 1]))); };
      g.frameArea = 25.0;
      g.braceArea = 60.0;
      Draft d;
      const auto st = boxGirder(d, g);
      constexpr std::size_t leftBody = 25, rightBody = 29; // z = -2 and z = +2
      for (const auto s : {leftBody, rightBody}) {
        for (const auto n : st[s]) d.pin(n); // wing-to-body side ribs
      }
      // Engines on pylons at z = +-5.6 m: 3.5 t (engine, nacelle, pylon) at 1 g and 25 kN cruise thrust.
      for (const auto s : {leftBody - 6, rightBody + 6}) {
        const auto [x, y] = frontSpar(zs[s]);
        const auto engine = d.node(x - 2.8, y - 1.8, zs[s]);
        const std::size_t outboard = s < leftBody ? s - 1 : s + 1;
        for (const auto n : st[s]) d.bar(engine, n, 40.0);
        for (const auto n : st[outboard]) d.bar(engine, n, 40.0);
        d.load(engine, {-25.0 * kKN, -3.5 * 9.80665 * kKN, 0.0});
      }
      // 1 g cruise: 370 kN lift per outer wing in a Schrenk distribution (taper 0.3), 60 / 40 %
      // on the front / rear spar top; 6 t of fuel per wing on the lower corners out to 10 m.
      std::vector<double> share(zs.size(), 0.0);
      double sum = 0.0;
      for (std::size_t s = 0; s < zs.size(); ++s) {
        const double e = eta(zs[s]);
        if (e <= 0.0) continue;
        const double planform = (1.0 - 0.7 * e) / 0.65;
        share[s] = (e >= 1.0 ? 0.5 : 1.0) * 0.5 * (planform + 4.0 / kPi * std::sqrt(std::max(0.0, 1.0 - e * e)));
        sum += share[s];
      }
      sum /= 2.0; // both wings
      std::vector<std::size_t> tanks;
      for (std::size_t s = 0; s < zs.size(); ++s) {
        const double lift = 370.0 * kKN * share[s] / sum;
        d.load(st[s][1], {0.0, 0.6 * lift, 0.0});
        d.load(st[s][2], {0.0, 0.4 * lift, 0.0});
        if (eta(zs[s]) > 0.0 && std::abs(zs[s]) <= 10.0) tanks.push_back(s);
      }
      const double fuel = 2.0 * 6.0 * 9.80665 * kKN / (2.0 * static_cast<double>(tanks.size()));
      for (const auto s : tanks) {
        d.load(st[s][0], {0.0, -fuel, 0.0});
        d.load(st[s][3], {0.0, -fuel, 0.0});
      }
      return {{"aircraft_airliner_wing_box", "Airliner wing box (full span)", "Aircraft",
               "34 m full-span wing box of a narrow-body airliner: 4 m centre box, two 15 m outer boxes with "
               "25 deg sweep, 5 deg dihedral, taper from 3.2 x 0.85 m to 0.8 x 0.25 m, ribs every 0.6 m, Warren "
               "spar webs, cross-braced skins, two engines on pylons. Pinned at the wing-to-body side ribs. 1 g "
               "cruise: 370 kN Schrenk lift per wing, 6 t fuel per wing, 3.5 t engines with 25 kN thrust. "
               "Aluminum 7075-T6: spar caps 400 cm^2 at the root tapering to 160 cm^2, webs and skins 60 cm^2, "
               "ribs 25 cm^2."},
              toModel(std::move(d), kAluminum7075, "Airliner wing box")};
    }

  } // namespace end

  std::vector<LibraryTruss> buildLibrary() {
    std::vector<LibraryTruss> library;
    for (auto* build : {kingPostRoof, queenPostRoof, finkRoof, howeRoof, prattRoof, scissorsRoof, bowstringRoof,
                        prattBridge, howeBridge, warrenBridge, kTrussBridge, parkerBridge, continuousBridge,
                        grandstandCantilever, spaceFrameRoof, schwedlerDome, geodesicDome, ovalStadiumRoof,
                        archStadiumRoof, kiewittDome,
                        transmissionTower, offshoreJacket, craneJib, eiffelTower,
                        tubeFuselage, engineMount, strutBracedWing, biplaneWingCell, rigidAirship, geodeticFuselage,
                        airlinerWingBox}) {
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
