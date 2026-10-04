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

#include "beamLibrary.hpp"

#include <beam/beamEngine/beamSolver.hpp>
#include <beam/beamIO/beamMeshAdapter.hpp>
#include <io/core/pathUtf8.hpp>
#include <io/meshIo.hpp>

#include <Eigen/Core>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <format>
#include <fstream>
#include <numbers>
#include <stdexcept>
#include <utility>
#include <vector>

namespace FEM::BEAM::LIBRARY {

  namespace {

    using P = std::array<double, 3>;
    constexpr double kPi = std::numbers::pi;
    constexpr double kG = 9.80665;
    constexpr double kKN = 1e3;

    // Built-in material names (assets/bridge/materialProperties.json).
    constexpr std::string_view kSteel4130 = "Structural Steel (AISI 4130)";
    constexpr std::string_view kAl6061 = "Aluminum 6061-T6";
    constexpr std::string_view kS235 = "Structural Steel S235 (EN 10025)";
    constexpr std::string_view kS355 = "Structural Steel S355 (EN 10025)";
    constexpr std::string_view kAl2024 = "Aluminum 2024-T3";
    constexpr std::string_view kAl7075 = "Aluminum 7075-T6";
    constexpr std::string_view kTi64 = "Titanium Ti-6Al-4V (Grade 5, annealed)";
    constexpr std::string_view kDouglasFir = "Douglas Fir (along the grain)";

    constexpr Formulation kEB = Formulation::EulerBernoulli;
    constexpr Formulation kTI = Formulation::Timoshenko;

    // End releases: a pin about the local z axis (in-plane hinge of a member whose local y is
    // up), a bending hinge (My + Mz) and a member pinned at both ends.
    constexpr std::uint16_t kPinZAtA = RELEASE::momentZ;
    constexpr std::uint16_t kPinZAtB = RELEASE::atNode2(RELEASE::momentZ);
    constexpr std::uint16_t kPinnedBothEnds = RELEASE::hinge | RELEASE::atNode2(RELEASE::hinge);
    // A pin-ended strut that may also spin about its own axis (rod end): torsion freed at one end.
    constexpr std::uint16_t kStrut = kPinnedBothEnds | RELEASE::atNode2(RELEASE::torsion);

    // A model being built: nodes, elements by section / material name, supports, loads.
    class Draft {
    public:
      Draft(std::span<const anaf::MATERIAL::Material> materials, std::span<const BeamSection> sections)
        : m_materials(materials), m_sections(sections) {}

      std::uint32_t node(const double x, const double y, const double z) {
        const auto id = static_cast<std::uint32_t>(mesh.nodes.size());
        mesh.nodes.emplace_back(id, x, y, z);
        return id;
      }

      std::uint32_t beam(const std::uint32_t a, const std::uint32_t b, const std::string_view section, const std::string_view material,
                         const P orientation = {}, const Formulation formulation = kEB) {
        BeamElement element;
        element.node1 = a;
        element.node2 = b;
        element.sectionID = sectionIndex(section);
        element.materialID = materialIndex(material);
        element.orientation = orientation;
        element.formulation = formulation;
        mesh.elements.push_back(element);
        return static_cast<std::uint32_t>(mesh.elements.size() - 1);
      }

      // Elements through consecutive nodes; returns their indices.
      std::vector<std::uint32_t> chain(const std::vector<std::uint32_t>& nodes, const std::string_view section, const std::string_view material,
                                       const P orientation = {}, const Formulation formulation = kEB) {
        std::vector<std::uint32_t> elements;
        for (std::size_t i = 0; i + 1 < nodes.size(); ++i) elements.push_back(beam(nodes[i], nodes[i + 1], section, material, orientation, formulation));
        return elements;
      }

      // End releases (RELEASE bits) on an element; see hingeA() / hingeB() / pinned() below.
      void release(const std::uint32_t element, const std::uint16_t bits) { mesh.elements[element].endReleases |= bits; }
      void release(const std::vector<std::uint32_t>& elements, const std::uint16_t bits) {
        for (const auto e : elements) release(e, bits);
      }

      void clamp(const std::uint32_t n) { mesh.nodes[n].fixAll(); }
      // held: true = restrained, per global axis.
      void support(const std::uint32_t n, const std::array<bool, 3> heldTranslation, const std::array<bool, 3> heldRotation) {
        mesh.nodes[n].setMovable({!heldTranslation[0], !heldTranslation[1], !heldTranslation[2]});
        mesh.nodes[n].setRotatable({!heldRotation[0], !heldRotation[1], !heldRotation[2]});
      }
      void pin(const std::uint32_t n) { support(n, {true, true, true}, {false, false, false}); }
      void load(const std::uint32_t n, const P force, const P moment = {}) { mesh.nodalLoads.push_back({n, force, moment}); }
      void line(const std::uint32_t element, const P value, const LoadFrame frame = LoadFrame::Global) {
        mesh.distributedLoads.push_back({element, value, frame});
      }
      void line(const std::vector<std::uint32_t>& elements, const P value, const LoadFrame frame = LoadFrame::Global) {
        for (const auto e : elements) line(e, value, frame);
      }

      MeshData mesh;

    private:
      std::span<const anaf::MATERIAL::Material> m_materials;
      std::span<const BeamSection> m_sections;

      std::uint32_t sectionIndex(const std::string_view name) const {
        for (std::uint32_t i = 0; i < m_sections.size(); ++i) {
          if (m_sections[i].getName() == name) return i;
        }
        throw std::runtime_error(std::format("section '{}' is not in the catalogue", name));
      }
      std::uint32_t materialIndex(const std::string_view name) const {
        for (std::uint32_t i = 0; i < m_materials.size(); ++i) {
          if (m_materials[i].getMaterialType() == name) return i;
        }
        throw std::runtime_error(std::format("material '{}' is not built in", name));
      }
    };

    struct Lists {
      std::span<const anaf::MATERIAL::Material> materials;
      std::span<const BeamSection> sections;
    };

    LibraryBeam make(Draft&& draft, std::string id, std::string name, std::string category, std::string description) {
      return LibraryBeam{Entry{std::move(id), std::move(name), std::move(category), std::move(description)}, std::move(draft.mesh)};
    }

    std::vector<double> steps(const double spacing, const int count, const double start = 0.0) {
      std::vector<double> values;
      for (int i = 0; i <= count; ++i) values.push_back(start + spacing * i);
      return values;
    }

    // Columns at (xs[i], zs[j]) through every level of ys (bases clamped), beams along x and z at
    // every level above ground. Columns have their web along x (v = +X).
    struct Grid {
      std::vector<double> xs, zs, ys;
      std::vector<std::uint32_t> ids;
      std::vector<std::uint32_t> beamsX, beamsZ; // floor beams, all levels
      std::uint32_t at(const std::size_t i, const std::size_t j, const std::size_t k) const { return ids[(k * zs.size() + j) * xs.size() + i]; }
    };
    Grid buildGrid(Draft& d, std::vector<double> xs, std::vector<double> zs, std::vector<double> ys, const std::string_view column,
                   const std::string_view beamX, const std::string_view beamZ, const std::string_view material) {
      Grid g{std::move(xs), std::move(zs), std::move(ys), {}, {}, {}};
      for (std::size_t k = 0; k < g.ys.size(); ++k) {
        for (std::size_t j = 0; j < g.zs.size(); ++j) {
          for (std::size_t i = 0; i < g.xs.size(); ++i) g.ids.push_back(d.node(g.xs[i], g.ys[k], g.zs[j]));
        }
      }
      for (std::size_t j = 0; j < g.zs.size(); ++j) {
        for (std::size_t i = 0; i < g.xs.size(); ++i) {
          d.clamp(g.at(i, j, 0));
          for (std::size_t k = 0; k + 1 < g.ys.size(); ++k) d.beam(g.at(i, j, k), g.at(i, j, k + 1), column, material, {1, 0, 0});
        }
      }
      for (std::size_t k = 1; k < g.ys.size(); ++k) {
        for (std::size_t j = 0; j < g.zs.size(); ++j) {
          for (std::size_t i = 0; i + 1 < g.xs.size(); ++i) g.beamsX.push_back(d.beam(g.at(i, j, k), g.at(i + 1, j, k), beamX, material));
        }
        for (std::size_t i = 0; i < g.xs.size(); ++i) {
          for (std::size_t j = 0; j + 1 < g.zs.size(); ++j) g.beamsZ.push_back(d.beam(g.at(i, j, k), g.at(i, j + 1, k), beamZ, material));
        }
      }
      return g;
    }

    // Nodes along a straight line from a to b in n segments (both ends included).
    std::vector<std::uint32_t> line(Draft& d, const P& a, const P& b, const int n) {
      std::vector<std::uint32_t> nodes;
      for (int i = 0; i <= n; ++i) {
        const double t = static_cast<double>(i) / n;
        nodes.push_back(d.node(a[0] + t * (b[0] - a[0]), a[1] + t * (b[1] - a[1]), a[2] + t * (b[2] - a[2])));
      }
      return nodes;
    }

    // ---- Building ------------------------------------------------------------------------------

    LibraryBeam portalFrame(const Lists& l) {
      Draft d(l.materials, l.sections);
      const double span = 15.0, eaves = 6.0, ridge = 7.5, spacing = 6.0;
      std::vector<std::array<std::uint32_t, 5>> frames;
      for (int f = 0; f < 3; ++f) {
        const double z = f * spacing, share = f == 1 ? 1.0 : 0.5;
        const std::array<std::uint32_t, 5> n{d.node(0, 0, z), d.node(0, eaves, z), d.node(span / 2, ridge, z), d.node(span, eaves, z), d.node(span, 0, z)};
        d.clamp(n[0]);
        d.clamp(n[4]);
        d.line(d.beam(n[0], n[1], "HEB 300", kS355, {1, 0, 0}), {3.6 * kKN * share, 0, 0}); // wind on the windward column
        d.beam(n[4], n[3], "HEB 300", kS355, {1, 0, 0});
        d.line(d.beam(n[1], n[2], "IPE 400", kS355), {0, -7.2 * kKN * share, 0});
        d.line(d.beam(n[2], n[3], "IPE 400", kS355), {0, -7.2 * kKN * share, 0});
        frames.push_back(n);
      }
      for (std::size_t f = 0; f + 1 < frames.size(); ++f) {
        for (const std::size_t k : {1u, 2u, 3u}) d.beam(frames[f][k], frames[f + 1][k], "IPE 200", kS355);
      }
      return make(std::move(d), "building_portal_frame", "Portal frame (pitched roof)", "Building",
                  "Span 15 m, eaves 6 m, ridge 7.5 m; 3 frames at 6 m tied at the eaves and the ridge (IPE 200). Columns HEB 300 clamped, "
                  "rafters IPE 400. Roof 1.2 kN/m^2 (7.2 kN/m on the inner rafters), wind 0.6 kN/m^2 on the windward columns. S355, self weight.");
    }

    LibraryBeam twoStoreyFrame(const Lists& l) {
      Draft d(l.materials, l.sections);
      auto g = buildGrid(d, steps(5.0, 2), steps(5.0, 2), {0.0, 3.5, 7.0}, "HEB 200", "IPE 300", "IPE 300", kS235);
      d.line(g.beamsX, {0, -12 * kKN, 0});
      d.line(g.beamsZ, {0, -6 * kKN, 0});
      return make(std::move(d), "building_two_storey", "Two-storey frame (2 x 2 bays)", "Building",
                  "Bays 5 x 5 m, storeys 3.5 m, rigid joints. Columns HEB 200 clamped, beams IPE 300 both ways. Floor 12 kN/m on the x beams, "
                  "6 kN/m on the z beams. S235, self weight.");
    }

    LibraryBeam officeFrame(const Lists& l) {
      Draft d(l.materials, l.sections);
      auto g = buildGrid(d, steps(6.0, 3), steps(7.5, 2), steps(3.6, 5), "HEB 340", "IPE 450", "IPE 400", kS355);
      d.line(g.beamsX, {0, -24 * kKN, 0});
      d.line(g.beamsZ, {0, -8 * kKN, 0});
      for (std::size_t k = 1; k < g.ys.size(); ++k) {
        for (std::size_t i = 0; i < g.xs.size(); ++i) d.load(g.at(i, 0, k), {0, 0, 12 * kKN}); // wind on the z = 0 face
      }
      return make(std::move(d), "building_office_5_storey", "Office building frame (5 storeys)", "Building",
                  "3 bays of 6 m by 2 bays of 7.5 m, 5 storeys of 3.6 m, moment frame. Columns HEB 340 clamped, beams IPE 450 (x) and IPE 400 (z). "
                  "Floors 24 kN/m (x) and 8 kN/m (z), wind 12 kN per node of the z = 0 face. S355, self weight.");
    }

    LibraryBeam bracedFrame(const Lists& l) {
      Draft d(l.materials, l.sections);
      auto g = buildGrid(d, {0.0, 6.0}, {0.0, 6.0}, steps(4.0, 3), "HEA 240", "IPE 330", "IPE 330", kS355);
      d.line(g.beamsX, {0, -15 * kKN, 0});
      for (std::size_t j = 0; j < 2; ++j) {
        for (std::size_t k = 0; k < 3; ++k) {
          d.beam(g.at(0, j, k), g.at(1, j, k + 1), "CHS 114.3x5", kS355);
          d.beam(g.at(1, j, k), g.at(0, j, k + 1), "CHS 114.3x5", kS355);
        }
      }
      for (std::size_t k = 1; k < g.ys.size(); ++k) {
        for (std::size_t j = 0; j < 2; ++j) d.load(g.at(0, j, k), {40 * kKN, 0, 0});
      }
      return make(std::move(d), "building_braced_frame", "X-braced frame (3 storeys)", "Building",
                  "One 6 x 6 m bay, 3 storeys of 4 m, X bracing (CHS 114.3x5) in both x frames. Columns HEA 240 clamped, beams IPE 330. "
                  "Floors 15 kN/m, lateral 40 kN per floor node in x. S355, self weight.");
    }

    LibraryBeam cantileverCanopy(const Lists& l) {
      Draft d(l.materials, l.sections);
      std::vector<std::uint32_t> tips;
      for (int c = 0; c < 4; ++c) {
        const double z = c * 6.0, share = (c == 0 || c == 3) ? 0.5 : 1.0;
        const auto base = d.node(0, 0, z), top = d.node(0, 4.5, z), tip = d.node(5.0, 4.5, z), back = d.node(-1.5, 4.5, z);
        d.clamp(base);
        d.beam(base, top, "HEB 260", kS355, {1, 0, 0});
        d.line(d.beam(top, tip, "IPE 360", kS355), {0, -6 * kKN * share, 0});
        d.beam(back, top, "IPE 360", kS355);
        d.beam(back, base, "CHS 114.3x5", kS355); // backstay
        tips.push_back(tip);
      }
      for (std::size_t c = 0; c + 1 < tips.size(); ++c) d.beam(tips[c], tips[c + 1], "IPE 200", kS355);
      return make(std::move(d), "building_cantilever_canopy", "Cantilever canopy", "Building",
                  "4 columns HEB 260 at 6 m, 4.5 m high, clamped; IPE 360 cantilevers 5 m with a 1.5 m back span and a CHS backstay to the "
                  "column base; IPE 200 edge beam. Snow 1 kN/m^2 (6 kN/m per inner cantilever). S355, self weight.");
    }

    LibraryBeam mezzanine(const Lists& l) {
      Draft d(l.materials, l.sections);
      // Columns on a 4 m grid (3 x 2 bays); main beams along x split at mid bay, where joists
      // along z frame in.
      const int nx = 3, nz = 2;
      const double bay = 4.0, height = 3.0;
      std::vector<std::vector<std::uint32_t>> top(static_cast<std::size_t>(2 * nx + 1), std::vector<std::uint32_t>(static_cast<std::size_t>(nz + 1)));
      for (int j = 0; j <= nz; ++j) {
        for (int i = 0; i <= 2 * nx; ++i) top[static_cast<std::size_t>(i)][static_cast<std::size_t>(j)] = d.node(i * bay / 2, height, j * bay);
      }
      for (int j = 0; j <= nz; ++j) {
        for (int i = 0; i <= nx; ++i) {
          const auto base = d.node(i * bay, 0, j * bay);
          d.clamp(base);
          d.beam(base, top[static_cast<std::size_t>(2 * i)][static_cast<std::size_t>(j)], "HEB 160", kS235, {1, 0, 0});
        }
        for (int i = 0; i < 2 * nx; ++i) {
          d.line(d.beam(top[static_cast<std::size_t>(i)][static_cast<std::size_t>(j)], top[static_cast<std::size_t>(i + 1)][static_cast<std::size_t>(j)], "IPE 240", kS235),
                 {0, -4 * kKN, 0});
        }
      }
      for (int i = 0; i <= 2 * nx; ++i) {
        const bool onColumn = i % 2 == 0;
        for (int j = 0; j < nz; ++j) {
          const auto e = d.beam(top[static_cast<std::size_t>(i)][static_cast<std::size_t>(j)], top[static_cast<std::size_t>(i)][static_cast<std::size_t>(j + 1)],
                                onColumn ? "IPE 240" : "IPE 160", kS235);
          d.line(e, {0, -(onColumn ? 4.0 : 8.0) * kKN, 0});
        }
      }
      return make(std::move(d), "building_mezzanine", "Mezzanine floor", "Building",
                  "3 x 2 bays of 4 m on 12 columns HEB 160 (3 m, clamped); main beams IPE 240 both ways on the column lines, joists IPE 160 "
                  "at mid bay. Floor 4 kN/m on the main beams, 8 kN/m on the joists (2 kN/m^2). S235, self weight.");
    }

    LibraryBeam timberPergola(const Lists& l) {
      Draft d(l.materials, l.sections);
      // Two beams along x (z = 0 and 3 m) with a node at every rafter (every 5/6 m), posts at
      // x = 0, 2.5, 5 m, rafters along z with 0.4 m overhangs.
      constexpr int segments = 6;
      std::array<std::vector<std::uint32_t>, 2> beams;
      for (int j = 0; j < 2; ++j) {
        for (int r = 0; r <= segments; ++r) beams[static_cast<std::size_t>(j)].push_back(d.node(r * 5.0 / segments, 2.7, j * 3.0));
        d.chain(beams[static_cast<std::size_t>(j)], "Rect 300x120", kDouglasFir);
        for (const int r : {0, 3, 6}) {
          const auto base = d.node(r * 5.0 / segments, 0, j * 3.0);
          d.clamp(base);
          d.beam(base, beams[static_cast<std::size_t>(j)][static_cast<std::size_t>(r)], "Rect 200x100", kDouglasFir, {1, 0, 0});
        }
      }
      for (int r = 0; r <= segments; ++r) {
        const double x = r * 5.0 / segments;
        const auto front = d.node(x, 2.7, -0.4), back = d.node(x, 2.7, 3.4);
        const auto rafters = d.chain({front, beams[0][static_cast<std::size_t>(r)], beams[1][static_cast<std::size_t>(r)], back}, "Rect 140x60", kDouglasFir);
        d.line(rafters, {0, -0.8 * kKN, 0});
      }
      return make(std::move(d), "building_timber_pergola", "Timber pergola", "Building",
                  "5 x 3 m, 2.7 m high. Posts 200x100 (clamped footings), beams 300x120 along x, 7 rafters 140x60 with 0.4 m overhangs. "
                  "Climbing plants / shade cloth 0.8 kN/m on the rafters. Douglas fir along the grain, self weight.");
    }

    // ---- Bridge --------------------------------------------------------------------------------

    // Two girders along x (z = 0 and width) through stations xs, joined by cross beams at every
    // station. Supports at the stations in supported: vertical everywhere, longitudinal at the
    // first support of girder 0, lateral on girder 0, torsion (rotation about x) held (forks).
    struct TwinGirder {
      std::array<std::vector<std::uint32_t>, 2> nodes;
      std::array<std::vector<std::uint32_t>, 2> girders;
    };
    TwinGirder twinGirder(Draft& d, const std::vector<double>& xs, const double width, const std::string_view girder,
                          const std::string_view crossBeam, const std::vector<std::size_t>& supported, const std::string_view material,
                          const std::size_t crossEvery = 1) {
      TwinGirder t;
      for (std::size_t g = 0; g < 2; ++g) {
        for (const double x : xs) t.nodes[g].push_back(d.node(x, 0, static_cast<double>(g) * width));
        t.girders[g] = d.chain(t.nodes[g], girder, material);
      }
      for (std::size_t i = 0; i < xs.size(); ++i) {
        const bool isSupport = std::ranges::find(supported, i) != supported.end();
        if (i % crossEvery == 0 || isSupport) d.beam(t.nodes[0][i], t.nodes[1][i], crossBeam, material);
      }
      for (std::size_t k = 0; k < supported.size(); ++k) {
        const auto i = supported[k];
        d.support(t.nodes[0][i], {k == 0, true, true}, {true, false, false});
        d.support(t.nodes[1][i], {false, true, false}, {true, false, false});
      }
      return t;
    }

    LibraryBeam footbridge(const Lists& l) {
      Draft d(l.materials, l.sections);
      const auto t = twinGirder(d, steps(2.0, 9), 2.5, "IPE 600", "IPE 200", {0, 9}, kS355);
      d.line(t.girders[0], {0, -8 * kKN, 0});
      d.line(t.girders[1], {0, -8 * kKN, 0});
      return make(std::move(d), "bridge_footbridge_girder", "Footbridge (twin girder)", "Bridge",
                  "Span 18 m, width 2.5 m: two IPE 600 girders, IPE 200 cross beams every 2 m. Fork supports (torsion held), pin / rollers. "
                  "Crowd 5 kN/m^2 plus deck: 8 kN/m per girder. S355, self weight.");
    }

    LibraryBeam continuousGirder(const Lists& l) {
      Draft d(l.materials, l.sections);
      const auto t = twinGirder(d, steps(2.5, 20), 3.0, "HEB 500", "IPE 300", {0, 6, 14, 20}, kS355, 2);
      d.line(t.girders[0], {0, -20 * kKN, 0});
      d.line(t.girders[1], {0, -20 * kKN, 0});
      return make(std::move(d), "bridge_continuous_girder", "Continuous girder bridge (3 spans)", "Bridge",
                  "Spans 15 + 20 + 15 m, two HEB 500 girders 3 m apart, IPE 300 cross beams every 5 m and at the piers. Fork supports at the "
                  "abutments and piers. Lane load 20 kN/m per girder. S355, self weight.");
    }

    LibraryBeam vierendeel(const Lists& l) {
      Draft d(l.materials, l.sections);
      constexpr int panels = 8;
      const double panel = 2.0, depth = 2.5, width = 3.0;
      std::array<std::vector<std::uint32_t>, 2> bottom, top;
      for (std::size_t g = 0; g < 2; ++g) {
        for (int i = 0; i <= panels; ++i) {
          bottom[g].push_back(d.node(i * panel, 0, static_cast<double>(g) * width));
          top[g].push_back(d.node(i * panel, depth, static_cast<double>(g) * width));
        }
        d.line(d.chain(bottom[g], "HEB 280", kS355), {0, -8 * kKN, 0});
        d.chain(top[g], "HEB 280", kS355);
        for (int i = 0; i <= panels; ++i) d.beam(bottom[g][static_cast<std::size_t>(i)], top[g][static_cast<std::size_t>(i)], "HEB 240", kS355, {1, 0, 0});
      }
      for (int i = 0; i <= panels; ++i) {
        d.beam(bottom[0][static_cast<std::size_t>(i)], bottom[1][static_cast<std::size_t>(i)], "IPE 270", kS355);
        d.beam(top[0][static_cast<std::size_t>(i)], top[1][static_cast<std::size_t>(i)], "IPE 200", kS355);
      }
      d.support(bottom[0][0], {true, true, true}, {false, false, false});
      d.support(bottom[1][0], {false, true, true}, {false, false, false});
      d.support(bottom[0][panels], {false, true, true}, {false, false, false});
      d.support(bottom[1][panels], {false, true, false}, {false, false, false});
      return make(std::move(d), "bridge_vierendeel", "Vierendeel truss bridge", "Bridge",
                  "Span 16 m, 8 panels, depth 2.5 m, two planes 3 m apart: no diagonals, the rigid joints carry the shear by bending. "
                  "Chords HEB 280, verticals HEB 240, deck cross beams IPE 270, top struts IPE 200. Pins / rollers. Deck 8 kN/m per plane. S355.");
    }

    LibraryBeam tiedArch(const Lists& l) {
      Draft d(l.materials, l.sections);
      constexpr int segments = 12;
      const double span = 30.0, rise = 6.0, width = 3.0;
      std::array<std::vector<std::uint32_t>, 2> deck, arch;
      for (std::size_t g = 0; g < 2; ++g) {
        for (int i = 0; i <= segments; ++i) deck[g].push_back(d.node(i * span / segments, 0, static_cast<double>(g) * width));
        d.line(d.chain(deck[g], "HEB 400", kS355), {0, -8 * kKN, 0});
        arch[g].push_back(deck[g].front());
        for (int i = 1; i < segments; ++i) {
          const double x = i * span / segments, s = 2.0 * x / span - 1.0;
          arch[g].push_back(d.node(x, rise * (1.0 - s * s), static_cast<double>(g) * width));
        }
        arch[g].push_back(deck[g].back());
        d.chain(arch[g], "CHS 323.9x12.5", kS355);
        for (int i = 1; i < segments; ++i) d.beam(deck[g][static_cast<std::size_t>(i)], arch[g][static_cast<std::size_t>(i)], "Bar D40", kS355);
      }
      for (int i = 0; i <= segments; ++i) d.beam(deck[0][static_cast<std::size_t>(i)], deck[1][static_cast<std::size_t>(i)], "IPE 240", kS355);
      for (int i = 3; i <= segments - 3; ++i) d.beam(arch[0][static_cast<std::size_t>(i)], arch[1][static_cast<std::size_t>(i)], "CHS 168.3x8", kS355);
      d.support(deck[0][0], {true, true, true}, {true, false, false});
      d.support(deck[1][0], {false, true, false}, {true, false, false});
      d.support(deck[0][segments], {false, true, true}, {true, false, false});
      d.support(deck[1][segments], {false, true, false}, {true, false, false});
      return make(std::move(d), "bridge_tied_arch", "Tied-arch footbridge", "Bridge",
                  "Span 30 m, parabolic arches (CHS 323.9x12.5, rise 6 m) tied by the HEB 400 deck girders, 2 x 11 hangers (round bar D40), "
                  "IPE 240 cross beams, CHS 168.3x8 arch struts. Fork supports, pin / rollers. Deck 8 kN/m per girder. S355, self weight.");
    }

    LibraryBeam grillage(const Lists& l) {
      Draft d(l.materials, l.sections);
      constexpr int girders = 4, segments = 7;
      std::vector<std::vector<std::uint32_t>> nodes(girders);
      for (int g = 0; g < girders; ++g) {
        for (int i = 0; i <= segments; ++i) nodes[static_cast<std::size_t>(g)].push_back(d.node(i * 2.0, 0, g * 2.0));
        d.line(d.chain(nodes[static_cast<std::size_t>(g)], "HEB 600", kS355), {0, -10 * kKN, 0});
        d.support(nodes[static_cast<std::size_t>(g)].front(), {g == 0, true, g == 0}, {true, false, false});
        d.support(nodes[static_cast<std::size_t>(g)].back(), {false, true, g == 0}, {true, false, false});
      }
      for (int i = 0; i <= segments; ++i) {
        for (int g = 0; g + 1 < girders; ++g) d.beam(nodes[static_cast<std::size_t>(g)][static_cast<std::size_t>(i)], nodes[static_cast<std::size_t>(g + 1)][static_cast<std::size_t>(i)], "IPE 400", kS355);
      }
      for (const int i : {3, 4}) {
        for (const int g : {1, 2}) d.load(nodes[static_cast<std::size_t>(g)][static_cast<std::size_t>(i)], {0, -50 * kKN, 0});
      }
      return make(std::move(d), "bridge_grillage_deck", "Grillage road deck", "Bridge",
                  "Span 14 m: four HEB 600 girders 2 m apart, IPE 400 cross girders every 2 m. Fork supports. Surfacing 10 kN/m per girder "
                  "and a 200 kN truck (4 wheel loads of 50 kN) at mid-span on the inner girders. S355, self weight.");
    }

    // ---- Industrial ----------------------------------------------------------------------------

    LibraryBeam pipeRack(const Lists& l) {
      Draft d(l.materials, l.sections);
      constexpr int frames = 5;
      std::vector<std::array<std::uint32_t, 4>> tiers; // (z=0, y=4), (z=0, y=8), (z=6, y=4), (z=6, y=8)
      for (int f = 0; f < frames; ++f) {
        const double x = f * 6.0;
        std::array<std::uint32_t, 4> t{};
        for (int side = 0; side < 2; ++side) {
          const auto base = d.node(x, 0, side * 6.0), mid = d.node(x, 4, side * 6.0), top = d.node(x, 8, side * 6.0);
          d.clamp(base);
          d.chain({base, mid, top}, "HEB 260", kS355, {1, 0, 0});
          t[static_cast<std::size_t>(2 * side)] = mid;
          t[static_cast<std::size_t>(2 * side + 1)] = top;
        }
        d.line(d.beam(t[0], t[2], "IPE 300", kS355), {0, -15 * kKN, 0});
        d.line(d.beam(t[1], t[3], "IPE 300", kS355), {0, -12 * kKN, 0});
        tiers.push_back(t);
      }
      for (int f = 0; f + 1 < frames; ++f) {
        for (std::size_t k = 0; k < 4; ++k) d.beam(tiers[static_cast<std::size_t>(f)][k], tiers[static_cast<std::size_t>(f + 1)][k], "IPE 200", kS355);
      }
      return make(std::move(d), "industrial_pipe_rack", "Pipe rack (2 tiers)", "Industrial",
                  "5 frames at 6 m, width 6 m, tiers at 4 and 8 m. Columns HEB 260 clamped, tier beams IPE 300, longitudinal ties IPE 200. "
                  "Pipes 15 kN/m (lower tier) and 12 kN/m (upper tier). S355, self weight.");
    }

    LibraryBeam gantryCrane(const Lists& l) {
      Draft d(l.materials, l.sections);
      constexpr int segments = 8;
      std::array<std::vector<std::uint32_t>, 2> girders;
      for (std::size_t g = 0; g < 2; ++g) {
        for (int i = 0; i <= segments; ++i) girders[g].push_back(d.node(i * 1.5, 9.0, static_cast<double>(g) * 2.5));
        d.chain(girders[g], "HEB 600", kS355, {}, kTI);
        for (const auto end : {girders[g].front(), girders[g].back()}) {
          const auto& p = d.mesh.nodes[end].getLocation();
          const auto foot = d.node(p[0], 0, p[2]);
          d.clamp(foot);
          d.beam(foot, end, "CHS 323.9x12.5", kS355);
        }
        d.load(girders[g][4], {0, -60 * kKN, 0});
      }
      d.beam(girders[0].front(), girders[1].front(), "IPE 360", kS355);
      d.beam(girders[0].back(), girders[1].back(), "IPE 360", kS355);
      return make(std::move(d), "industrial_gantry_crane", "Gantry crane", "Industrial",
                  "Span 12 m, height 9 m: two HEB 600 girders 2.5 m apart (Timoshenko) on four CHS 323.9x12.5 legs (clamped on the rails), "
                  "IPE 360 end ties. Trolley at mid-span: 60 kN per girder. S355, self weight.");
    }

    LibraryBeam palletRack(const Lists& l) {
      Draft d(l.materials, l.sections);
      constexpr int bays = 3, levels = 4;
      const double bay = 2.7, depth = 1.1, level = 1.5;
      std::vector<std::array<std::vector<std::uint32_t>, 2>> uprights(bays + 1);
      for (int f = 0; f <= bays; ++f) {
        for (std::size_t side = 0; side < 2; ++side) {
          for (int k = 0; k <= levels; ++k) uprights[static_cast<std::size_t>(f)][side].push_back(d.node(f * bay, k * level, static_cast<double>(side) * depth));
          d.clamp(uprights[static_cast<std::size_t>(f)][side].front());
          d.chain(uprights[static_cast<std::size_t>(f)][side], "RHS 100x50x5", kS355, {0, 0, 1});
        }
        for (int k = 0; k <= levels; ++k) {
          if (k > 0) d.beam(uprights[static_cast<std::size_t>(f)][0][static_cast<std::size_t>(k)], uprights[static_cast<std::size_t>(f)][1][static_cast<std::size_t>(k)], "Box 40x40x2", kS355);
          if (k < levels) d.beam(uprights[static_cast<std::size_t>(f)][k % 2][static_cast<std::size_t>(k)], uprights[static_cast<std::size_t>(f)][(k + 1) % 2][static_cast<std::size_t>(k + 1)], "Box 40x40x2", kS355);
        }
      }
      for (int f = 0; f < bays; ++f) {
        for (int k = 1; k <= levels; ++k) {
          for (std::size_t side = 0; side < 2; ++side) {
            d.line(d.beam(uprights[static_cast<std::size_t>(f)][side][static_cast<std::size_t>(k)], uprights[static_cast<std::size_t>(f + 1)][side][static_cast<std::size_t>(k)], "Box 120x60x4", kS355),
                   {0, -3.6 * kKN, 0});
          }
        }
      }
      return make(std::move(d), "industrial_pallet_rack", "Pallet racking", "Industrial",
                  "3 bays of 2.7 m, depth 1.1 m, 4 beam levels at 1.5 m. Uprights RHS 100x50x5 (clamped base plates), frame bracing "
                  "Box 40x40x2, pallet beams Box 120x60x4. Two 1000 kg pallets per bay and level (3.6 kN/m per beam). S355, self weight.");
    }

    LibraryBeam signGantry(const Lists& l) {
      Draft d(l.materials, l.sections);
      const auto left = d.node(0, 0, 0), right = d.node(18, 0, 0);
      d.clamp(left);
      d.clamp(right);
      const auto girder = line(d, {0, 7, 0}, {18, 7, 0}, 9);
      d.beam(left, girder.front(), "CHS 323.9x12.5", kS355);
      d.beam(right, girder.back(), "CHS 323.9x12.5", kS355);
      d.line(d.chain(girder, "RHS 300x200x12.5", kS355), {0, -3 * kKN, 2 * kKN});
      return make(std::move(d), "industrial_sign_gantry", "Highway sign gantry", "Industrial",
                  "Span 18 m, clearance 7 m: CHS 323.9x12.5 columns clamped on foundations, RHS 300x200x12.5 girder. Sign weight 3 kN/m and "
                  "wind on the sign 2 kN/m (z). S355, self weight.");
    }

    LibraryBeam scaffoldTower(const Lists& l) {
      Draft d(l.materials, l.sections);
      constexpr int lifts = 4;
      const std::array<std::array<double, 2>, 4> corners{{{0, 0}, {2.5, 0}, {2.5, 1.3}, {0, 1.3}}};
      std::array<std::vector<std::uint32_t>, 4> standards;
      for (std::size_t c = 0; c < 4; ++c) {
        for (int k = 0; k <= lifts; ++k) standards[c].push_back(d.node(corners[c][0], k * 2.0, corners[c][1]));
        d.clamp(standards[c].front());
        d.chain(standards[c], "CHS 48.3x3.2", kS235);
      }
      for (int k = 1; k <= lifts; ++k) {
        for (std::size_t c = 0; c < 4; ++c) d.beam(standards[c][static_cast<std::size_t>(k)], standards[(c + 1) % 4][static_cast<std::size_t>(k)], "CHS 48.3x3.2", kS235);
      }
      for (int k = 0; k < lifts; ++k) {
        const std::size_t lower = static_cast<std::size_t>(k), upper = lower + 1;
        d.beam(standards[0][k % 2 ? upper : lower], standards[1][k % 2 ? lower : upper], "CHS 48.3x3.2", kS235);
        d.beam(standards[3][k % 2 ? upper : lower], standards[2][k % 2 ? lower : upper], "CHS 48.3x3.2", kS235);
        d.beam(standards[1][lower], standards[2][upper], "CHS 48.3x3.2", kS235);
        d.beam(standards[0][upper], standards[3][lower], "CHS 48.3x3.2", kS235);
      }
      for (std::size_t c = 0; c < 4; ++c) d.load(standards[c].back(), {0, -1.5 * kKN, 0});
      return make(std::move(d), "industrial_scaffold_tower", "Scaffold tower", "Industrial",
                  "2.5 x 1.3 m, 4 lifts of 2 m: standards, ledgers and face diagonals in CHS 48.3x3.2, base plates clamped. Working platform "
                  "2 kN/m^2 (1.5 kN per standard at the top). S235, self weight.");
    }

    LibraryBeam craneRunway(const Lists& l) {
      Draft d(l.materials, l.sections);
      const auto girder = line(d, {0, 7, 0}, {24, 7, 0}, 16);
      d.chain(girder, "HEB 450", kS355);
      for (int c = 0; c <= 4; ++c) {
        const auto foot = d.node(c * 6.0, 0, 0);
        d.clamp(foot);
        d.beam(foot, girder[static_cast<std::size_t>(c * 4)], "HEB 300", kS355, {1, 0, 0});
      }
      for (const std::size_t wheel : {7u, 9u}) d.load(girder[wheel], {0, -120 * kKN, 12 * kKN});
      return make(std::move(d), "industrial_crane_runway", "Crane runway girder", "Industrial",
                  "Continuous HEB 450 runway girder over 4 spans of 6 m on HEB 300 columns (7 m, clamped). Two crane wheels 3 m apart: "
                  "120 kN vertical and 12 kN lateral (10 %) each. S355, self weight.");
    }

    // ---- Energy & Tower ------------------------------------------------------------------------

    LibraryBeam windTurbineTower(const Lists& l) {
      Draft d(l.materials, l.sections);
      const auto nodes = line(d, {0, 0, 0}, {0, 90, 0}, 9);
      d.clamp(nodes.front());
      for (std::size_t i = 0; i + 1 < nodes.size(); ++i) {
        d.beam(nodes[i], nodes[i + 1], i < 3 ? "Tube 4200x30" : (i < 6 ? "Tube 3500x26" : "Tube 2500x22"), kS355);
      }
      d.load(nodes.back(), {400 * kKN, -2943 * kKN, 0}, {0, 0, 2943 * kKN * 4.0});
      return make(std::move(d), "tower_wind_turbine", "Wind turbine tower", "Energy & Tower",
                  "90 m tubular tower in three cans: 4200x30 (0-30 m), 3500x26 (30-60 m), 2500x22 (60-90 m), clamped on the foundation. "
                  "Rotor and nacelle 300 t (2943 kN) with 4 m overhang (11.8 MNm) and 400 kN rotor thrust at the top. S355, self weight.");
    }

    LibraryBeam telecomMonopole(const Lists& l) {
      Draft d(l.materials, l.sections);
      const auto pole = line(d, {0, 0, 0}, {0, 36, 0}, 6);
      d.clamp(pole.front());
      for (std::size_t i = 0; i + 1 < pole.size(); ++i) {
        d.line(d.beam(pole[i], pole[i + 1], i < 3 ? "Tube 1000x16" : "Tube 600x12", kS355), {1.5 * kKN, 0, 0});
      }
      for (int a = 0; a < 3; ++a) {
        const double angle = 2.0 * kPi * a / 3.0;
        const auto tip = d.node(1.5 * std::cos(angle), 36, 1.5 * std::sin(angle));
        d.beam(pole.back(), tip, "Box 120x60x4", kS355);
        d.load(tip, {3 * kKN, -0.6 * kKN, 0});
      }
      return make(std::move(d), "tower_telecom_monopole", "Telecom monopole", "Energy & Tower",
                  "36 m monopole: Tube 1000x16 (0-18 m), Tube 600x12 (18-36 m), clamped; three 1.5 m antenna arms (Box 120x60x4). "
                  "Wind 1.5 kN/m on the pole and 3 kN per antenna, antenna weight 0.6 kN. S355, self weight.");
    }

    LibraryBeam solarTracker(const Lists& l) {
      Draft d(l.materials, l.sections);
      const auto tube = line(d, {0, 1.5, 0}, {30, 1.5, 0}, 15); // a node every 2 m
      d.line(d.chain(tube, "CHS 139.7x6.3", kS355), {0, -0.6 * kKN, 0});
      for (int p = 0; p <= 5; ++p) {
        const auto foot = d.node(p * 6.0, 0, 0);
        d.clamp(foot);
        d.beam(foot, tube[static_cast<std::size_t>(p * 3)], "IPE 200", kS355, {1, 0, 0});
      }
      for (std::size_t i = 1; i + 1 < tube.size(); ++i) {
        if (i % 3 != 0) d.load(tube[i], {0, 1.2 * kKN * 2.0, 0}, {1.5 * kKN, 0, 0});
      }
      return make(std::move(d), "energy_solar_tracker", "Single-axis solar tracker", "Energy & Tower",
                  "30 m torque tube CHS 139.7x6.3 at 1.5 m on six IPE 200 posts every 6 m (clamped, rigid bearings). Panels 0.6 kN/m; wind "
                  "uplift (2.4 kN) and the wind torque of the tilted panels (1.5 kNm) at the tube nodes between the posts. S355, self weight.");
    }

    LibraryBeam offshoreJacket(const Lists& l) {
      Draft d(l.materials, l.sections);
      constexpr int levels = 4;
      const double height = 40.0;
      std::array<std::vector<std::uint32_t>, 4> legs;
      const std::array<std::array<double, 2>, 4> sign{{{-1, -1}, {1, -1}, {1, 1}, {-1, 1}}};
      for (std::size_t c = 0; c < 4; ++c) {
        for (int k = 0; k <= levels; ++k) {
          const double half = 10.0 - 4.0 * k / levels;
          legs[c].push_back(d.node(sign[c][0] * half, height * k / levels, sign[c][1] * half));
        }
        d.clamp(legs[c].front());
        d.chain(legs[c], "Tube 1000x16", kS355);
      }
      for (int k = 1; k <= levels; ++k) {
        for (std::size_t c = 0; c < 4; ++c) {
          const auto a = legs[c][static_cast<std::size_t>(k)], b = legs[(c + 1) % 4][static_cast<std::size_t>(k)];
          if (k == levels) d.line(d.beam(a, b, "HEB 600", kS355), {0, -50 * kKN, 0});
          else d.beam(a, b, "Tube 600x12", kS355);
        }
      }
      for (int k = 0; k < levels; ++k) {
        for (std::size_t c = 0; c < 4; ++c) {
          const auto nk = static_cast<std::size_t>(k);
          d.beam(legs[c][nk], legs[(c + 1) % 4][nk + 1], "Tube 600x12", kS355);
          d.beam(legs[(c + 1) % 4][nk], legs[c][nk + 1], "Tube 600x12", kS355);
        }
      }
      for (int k = 1; k < levels; ++k) {
        for (const std::size_t c : {0u, 3u}) d.load(legs[c][static_cast<std::size_t>(k)], {150 * kKN, 0, 0});
      }
      return make(std::move(d), "offshore_jacket", "Offshore jacket (4 legs)", "Energy & Tower",
                  "40 m four-legged jacket, battered from 20 x 20 m at the mudline to 12 x 12 m: legs Tube 1000x16 (piled, clamped), "
                  "horizontal frames and X braces Tube 600x12, HEB 600 deck girders with topsides 50 kN/m. Wave and current 150 kN per "
                  "upstream leg node. S355, self weight.");
    }

    // ---- Machine & Vehicle ---------------------------------------------------------------------

    LibraryBeam machineFrame(const Lists& l) {
      Draft d(l.materials, l.sections);
      constexpr int segments = 4;
      std::array<std::vector<std::uint32_t>, 2> rails;
      for (std::size_t s = 0; s < 2; ++s) {
        for (int i = 0; i <= segments; ++i) rails[s].push_back(d.node(i * 0.75, 0.8, static_cast<double>(s) * 1.5));
        d.chain(rails[s], "RHS 150x100x8", kS355, {}, kTI);
      }
      std::vector<std::uint32_t> mids;
      for (int i = 0; i <= segments; ++i) {
        const auto mid = d.node(i * 0.75, 0.8, 0.75);
        d.chain({rails[0][static_cast<std::size_t>(i)], mid, rails[1][static_cast<std::size_t>(i)]}, "RHS 120x60x6.3", kS355, {}, kTI);
        mids.push_back(mid);
      }
      for (std::size_t s = 0; s < 2; ++s) {
        for (const int i : {0, 2, 4}) {
          const auto foot = d.node(i * 0.75, 0, static_cast<double>(s) * 1.5);
          d.clamp(foot);
          d.beam(foot, rails[s][static_cast<std::size_t>(i)], "SHS 100x100x6.3", kS355, {1, 0, 0}, kTI);
        }
      }
      d.load(mids[2], {0, -15 * kKN, 0}, {0, 3 * kKN, 0});
      d.load(mids[1], {0, -5 * kKN, 0});
      d.load(mids[3], {0, -5 * kKN, 0});
      return make(std::move(d), "machine_welded_frame", "Welded machine base frame", "Machine & Vehicle",
                  "3 x 1.5 m base at 0.8 m: rails RHS 150x100x8, cross members RHS 120x60x6.3, six SHS 100x100x6.3 legs (clamped). Motor "
                  "15 kN with 3 kNm reaction torque at the centre, 5 kN gearbox and pump. Timoshenko elements (short, deep members). S355.");
    }

    LibraryBeam ladderChassis(const Lists& l) {
      Draft d(l.materials, l.sections);
      constexpr int segments = 10;
      std::array<std::vector<std::uint32_t>, 2> rails;
      for (std::size_t s = 0; s < 2; ++s) {
        for (int i = 0; i <= segments; ++i) rails[s].push_back(d.node(i * 0.7, 0.9, -0.45 + static_cast<double>(s) * 0.9));
        const auto elements = d.chain(rails[s], "RHS 200x100x10", kS355);
        for (std::size_t e = 3; e < elements.size(); ++e) d.line(elements[e], {0, -8 * kKN, 0});
        d.load(rails[s][2], {0, -6 * kKN, 0});
      }
      for (const int i : {0, 2, 4, 6, 8, 10}) d.beam(rails[0][static_cast<std::size_t>(i)], rails[1][static_cast<std::size_t>(i)], "Box 120x60x4", kS355);
      d.support(rails[0][1], {true, true, true}, {false, false, false});
      d.support(rails[1][1], {false, true, false}, {false, false, false});
      d.support(rails[0][8], {false, true, true}, {false, false, false});
      d.support(rails[1][8], {false, true, false}, {false, false, false});
      return make(std::move(d), "vehicle_ladder_chassis", "Truck ladder chassis", "Machine & Vehicle",
                  "7 m ladder frame: rails RHS 200x100x10 0.9 m apart, six Box 120x60x4 cross members. Supported at the front axle (0.7 m) and "
                  "rear axle (5.6 m). Payload 8 kN/m per rail behind the cab, engine 12 kN. S355, self weight.");
    }

    LibraryBeam bicycleFrame(const Lists& l) {
      Draft d(l.materials, l.sections);
      const auto bb = d.node(0.41, 0.27, 0), seat = d.node(0.33, 0.80, 0), headTop = d.node(0.98, 0.86, 0), headBottom = d.node(1.02, 0.71, 0);
      const auto dropL = d.node(0.0, 0.34, -0.065), dropR = d.node(0.0, 0.34, 0.065);
      d.beam(seat, headTop, "Tube 25.4x0.89", kSteel4130);
      d.beam(bb, headBottom, "Tube 31.75x1.24", kSteel4130);
      d.beam(bb, seat, "Tube 25.4x1.24", kSteel4130);
      d.beam(headBottom, headTop, "Tube 38.1x1.24", kSteel4130);
      for (const auto drop : {dropL, dropR}) {
        d.beam(seat, drop, "Tube 19.05x0.89", kSteel4130);
        d.beam(bb, drop, "Tube 22.2x0.89", kSteel4130);
      }
      d.pin(dropL);
      d.support(dropR, {false, true, true}, {false, false, false});
      d.support(headBottom, {false, true, true}, {false, false, false});
      d.load(seat, {0, -0.8 * kKN, 0});
      d.load(bb, {0, -1.2 * kKN, 0.15 * kKN});
      return make(std::move(d), "vehicle_bicycle_frame", "Bicycle frame (diamond)", "Machine & Vehicle",
                  "Diamond road frame in 4130 chromoly tubing: top tube 25.4x0.89, down tube 31.75x1.24, seat tube 25.4x1.24, head tube "
                  "38.1x1.24, seat stays 19.05x0.89, chain stays 22.2x0.89. Axle at the dropouts, fork at the head tube. Rider 0.8 kN on the "
                  "saddle, standing pedal force 1.2 kN with 0.15 kN side load at the bottom bracket.");
    }

    LibraryBeam rollCage(const Lists& l) {
      Draft d(l.materials, l.sections);
      std::array<std::uint32_t, 2> mainFoot{}, mainKnee{}, mainTop{}, frontFoot{}, frontKnee{}, frontTop{}, rearFoot{};
      for (std::size_t s = 0; s < 2; ++s) {
        const double z = s == 0 ? -0.7 : 0.7, zt = s == 0 ? -0.6 : 0.6;
        mainFoot[s] = d.node(1.3, 0, z);
        mainKnee[s] = d.node(1.3, 0.45, z);
        mainTop[s] = d.node(1.3, 1.15, zt);
        frontFoot[s] = d.node(0.0, 0, z);
        frontKnee[s] = d.node(0.25, 0.45, z);
        frontTop[s] = d.node(0.65, 1.1, zt);
        rearFoot[s] = d.node(2.3, 0.2, zt);
        d.clamp(mainFoot[s]);
        d.clamp(frontFoot[s]);
        d.clamp(rearFoot[s]);
        d.chain({mainFoot[s], mainKnee[s], mainTop[s]}, "Tube 50.8x1.65", kSteel4130);
        d.chain({frontFoot[s], frontKnee[s], frontTop[s]}, "Tube 44.45x1.65", kSteel4130);
        d.beam(frontTop[s], mainTop[s], "Tube 44.45x1.65", kSteel4130);
        d.beam(frontKnee[s], mainKnee[s], "Tube 44.45x1.65", kSteel4130); // door bar
        d.beam(mainTop[s], rearFoot[s], "Tube 44.45x1.65", kSteel4130);   // rear stay
      }
      d.beam(mainTop[0], mainTop[1], "Tube 50.8x1.65", kSteel4130);
      d.beam(frontTop[0], frontTop[1], "Tube 44.45x1.65", kSteel4130);
      d.beam(mainTop[0], mainKnee[1], "Tube 44.45x1.65", kSteel4130); // main hoop diagonal
      d.beam(frontTop[0], mainTop[1], "Tube 44.45x1.65", kSteel4130); // roof diagonal
      for (const auto top : mainTop) d.load(top, {0, -37 * kKN, 0});
      return make(std::move(d), "vehicle_roll_cage", "Motorsport roll cage", "Machine & Vehicle",
                  "Six-point cage in 4130 tubing: main hoop 50.8x1.65, front hoops, roof bars, door bars, rear stays and diagonals 44.45x1.65, "
                  "welded to floor plates (clamped). Static roof load after the FIA main hoop test: 74 kN "
                  "(7.5 t) vertical at the top of the main hoop.");
    }

    LibraryBeam cncGantry(const Lists& l) {
      Draft d(l.materials, l.sections);
      const auto beam = line(d, {0, 0.6, 0}, {2.0, 0.6, 0}, 8);
      d.chain(beam, "Box 200x100x6", kAl6061, {}, kTI);
      for (const auto end : {beam.front(), beam.back()}) {
        const auto& p = d.mesh.nodes[end].getLocation();
        const auto foot = d.node(p[0], 0, p[2]);
        d.clamp(foot);
        d.beam(foot, end, "Box 120x60x4", kAl6061, {0, 0, 1}, kTI);
      }
      d.load(beam[4], {0, -2 * kKN, 0.5 * kKN}, {0.3 * kKN, 0, 0});
      return make(std::move(d), "machine_cnc_gantry", "CNC router gantry", "Machine & Vehicle",
                  "2 m aluminium gantry beam Box 200x100x6 on two 0.6 m uprights Box 120x60x4 (clamped to the bed), Timoshenko. Spindle "
                  "carriage 2 kN at mid-span with 0.5 kN cutting force and 0.3 kNm torque. Aluminium 6061-T6, self weight.");
    }

    LibraryBeam robotArm(const Lists& l) {
      Draft d(l.materials, l.sections);
      const double ex = 1.0 * std::cos(kPi / 6), ey = 0.6 + 1.0 * std::sin(kPi / 6);       // elbow
      const double wx = ex + 0.8 * std::cos(-kPi / 9), wy = ey + 0.8 * std::sin(-kPi / 9); // wrist
      const auto base = d.node(0, 0, 0), shoulder = d.node(0, 0.6, 0), elbow = d.node(ex, ey, 0), wrist = d.node(wx, wy, 0);
      const auto tool = d.node(wx + 0.3, wy, 0);
      d.clamp(base);
      d.beam(base, shoulder, "Tube 101.6x2.11", kAl7075);
      d.beam(shoulder, elbow, "Tube 101.6x2.11", kAl7075);
      d.beam(elbow, wrist, "Tube 76.2x2.11", kAl7075);
      d.beam(wrist, tool, "Tube 63.5x1.65", kAl7075);
      d.load(elbow, {0, -150, 0});
      d.load(wrist, {0, -80, 0});
      d.load(tool, {0, -200, 50}, {0, 0, -20});
      return make(std::move(d), "machine_robot_arm", "Articulated robot arm", "Machine & Vehicle",
                  "Arm held in a reach pose: 0.6 m base column, 1.0 m upper arm at 30 deg (Tube 101.6x2.11), 0.8 m forearm (Tube 76.2x2.11), "
                  "0.3 m wrist (Tube 63.5x1.65), base clamped. Motors 150 N and 80 N at the joints, 20 kg payload with 50 N side force "
                  "and 20 Nm at the tool. Aluminium 7075-T6, self weight.");
    }

    // ---- Aerospace -----------------------------------------------------------------------------

    LibraryBeam wingSpar(const Lists& l) {
      Draft d(l.materials, l.sections);
      const auto spar = line(d, {0, 0, 0}, {0, 0, 6.0}, 12); // span along z, web vertical (v = +Y default)
      d.clamp(spar.front());
      for (std::size_t i = 0; i + 1 < spar.size(); ++i) {
        const auto section = i < 4 ? "Spar I 250x80x4x10" : (i < 8 ? "Spar I 180x70x3x8" : "Spar I 120x60x2.5x6");
        const double lift = i < 4 ? 5.5 : (i < 8 ? 4.5 : 2.8);
        d.line(d.beam(spar[i], spar[i + 1], section, kAl7075, {}, kTI), {0, lift * kKN, 0});
      }
      d.load(spar[4], {0, -8 * kKN, 0});
      return make(std::move(d), "aero_wing_spar", "Wing main spar (tapered)", "Aerospace",
                  "6 m semi-span cantilever spar in three extruded I steps (Spar I 250, 180, 120 mm), root clamped to the centre box, "
                  "Timoshenko. Lift in steps that follow an elliptical spanwise distribution (5.5 / 4.5 / 2.8 kN/m), engine 8 kN at 2 m. "
                  "Aluminium 7075-T6, self weight.");
    }

    LibraryBeam strutBracedWing(const Lists& l) {
      Draft d(l.materials, l.sections);
      const auto spar = line(d, {0, 1.2, 0.6}, {0, 1.2, 5.6}, 10);
      d.line(d.chain(spar, "Spar I 180x70x3x8", kAl2024), {0, 2.5 * kKN, 0});
      const auto strutBase = d.node(0, 0, 0.6);
      d.beam(strutBase, spar[5], "Tube 50.8x1.65", kSteel4130);
      d.support(spar.front(), {true, true, true}, {false, true, true}); // drag fitting: fore-aft and torsion held
      d.pin(strutBase);
      return make(std::move(d), "aero_strut_braced_wing", "Strut-braced high wing", "Aerospace",
                  "Light aircraft high wing: 5 m spar (Spar I 180, aluminium 2024-T3) hinged to the cabin roof for flapping (fore-aft and torsion held by the drag fitting), lift strut "
                  "Tube 50.8x1.65 (4130) from the lower fuselage to mid-span. Lift 2.5 kN/m (3.8 g at 650 kg). Self weight.");
    }

    LibraryBeam skidGear(const Lists& l) {
      Draft d(l.materials, l.sections);
      std::array<std::vector<std::uint32_t>, 2> skids;
      for (std::size_t s = 0; s < 2; ++s) {
        skids[s] = line(d, {-1.2, 0, s == 0 ? -1.1 : 1.1}, {1.3, 0, s == 0 ? -1.1 : 1.1}, 5);
        d.chain(skids[s], "Tube 76.2x2.11", kAl6061);
      }
      for (const std::size_t station : {1u, 4u}) {
        const double x = d.mesh.nodes[skids[0][station]].getLocation()[0];
        const auto a = d.node(x, 0.45, -0.85), b = d.node(x, 0.6, -0.45), c = d.node(x, 0.6, 0.45), e = d.node(x, 0.45, 0.85);
        d.chain({skids[0][station], a, b, c, e, skids[1][station]}, "CHS 114.3x5", kAl7075);
        d.load(b, {0, -7.85 * kKN, 0});
        d.load(c, {0, -7.85 * kKN, 0});
      }
      // Ground contact under the cross tubes.
      d.support(skids[0][1], {true, true, true}, {false, false, false});
      d.support(skids[1][1], {false, true, true}, {false, false, false});
      d.support(skids[0][4], {false, true, true}, {false, false, false});
      d.support(skids[1][4], {false, true, false}, {false, false, false});
      return make(std::move(d), "aero_helicopter_skid_gear", "Helicopter skid landing gear", "Aerospace",
                  "Skids Tube 76.2x2.11 (6061-T6) 2.5 m long, 2.2 m apart; two arched cross tubes 114.3x5 (7075-T6) bolted to the "
                  "fuselage at 0.6 m. Level landing at 2 g with 1600 kg: 7.85 kN per attachment, skids on the ground under the cross tubes.");
    }

    LibraryBeam enginePylon(const Lists& l) {
      Draft d(l.materials, l.sections);
      std::array<std::uint32_t, 4> upper{}, lower{};
      for (std::size_t s = 0; s < 2; ++s) {
        const double z = s == 0 ? -0.3 : 0.3;
        upper[s] = d.node(0, 0, z);
        upper[s + 2] = d.node(1.5, 0, z);
        lower[s] = d.node(-1.0, -0.8, z);
        lower[s + 2] = d.node(0.6, -0.8, z);
        d.clamp(upper[s]);
        d.clamp(upper[s + 2]);
        d.beam(upper[s], lower[s], "Box 120x60x4", kTi64);
        d.beam(upper[s], lower[s + 2], "Box 120x60x4", kTi64);
        d.beam(upper[s + 2], lower[s + 2], "Box 120x60x4", kTi64);
        d.beam(lower[s], lower[s + 2], "Box 120x60x4", kTi64);
      }
      d.beam(lower[0], lower[1], "Box 120x60x4", kTi64);
      d.beam(lower[2], lower[3], "Box 120x60x4", kTi64);
      d.beam(lower[0], lower[3], "Box 120x60x4", kTi64);
      for (const auto mount : lower) d.load(mount, {-30 * kKN, -9.4 * kKN, 0});
      return make(std::move(d), "aero_engine_pylon", "Engine pylon frame", "Aerospace",
                  "Under-wing pylon as a welded titanium frame (Box 120x60x4, Ti-6Al-4V), clamped to the wing front and rear spar fittings, "
                  "engine mounts 0.8 m below. Engine 25 kN at 1.5 g (37.5 kN) and 120 kN take-off thrust over the four mounts.");
    }

    LibraryBeam satelliteBus(const Lists& l) {
      Draft d(l.materials, l.sections);
      const std::array<std::array<double, 2>, 4> corner{{{-0.6, -0.6}, {0.6, -0.6}, {0.6, 0.6}, {-0.6, 0.6}}};
      std::array<std::array<std::uint32_t, 3>, 4> n{};
      for (std::size_t c = 0; c < 4; ++c) {
        for (std::size_t k = 0; k < 3; ++k) n[c][k] = d.node(corner[c][0], 0.7 * static_cast<double>(k), corner[c][1]);
        d.clamp(n[c][0]);
        d.chain({n[c][0], n[c][1], n[c][2]}, "Box 80x40x3", kAl7075);
      }
      for (std::size_t k = 0; k < 3; ++k) {
        for (std::size_t c = 0; c < 4; ++c) d.beam(n[c][k], n[(c + 1) % 4][k], "Box 60x40x3", kAl7075);
      }
      d.mesh.gravity = {2.0 * kG, -6.0 * kG, 0.0};
      const auto inertial = [](const double mass) { return P{mass * 2.0 * kG, -mass * 6.0 * kG, 0.0}; };
      for (std::size_t c = 0; c < 4; ++c) {
        d.load(n[c][1], inertial(40.0));
        d.load(n[c][2], inertial(15.0));
      }
      return make(std::move(d), "space_satellite_bus", "Small satellite bus frame", "Aerospace",
                  "1.2 x 1.2 x 1.4 m bus: corner posts Box 80x40x3, deck edges Box 60x40x3 (7075-T6), clamped at the launch adapter. "
                  "Quasi-static launch load 6 g axial + 2 g lateral (gravity vector) on the structure, 160 kg of equipment on the mid deck "
                  "and 60 kg on the top deck.");
    }

    LibraryBeam spaceTruss(const Lists& l) {
      Draft d(l.materials, l.sections);
      constexpr int bays = 4;
      const std::array<std::array<double, 2>, 4> corner{{{-1.5, -1.5}, {1.5, -1.5}, {1.5, 1.5}, {-1.5, 1.5}}};
      std::array<std::vector<std::uint32_t>, 4> longerons;
      for (std::size_t c = 0; c < 4; ++c) {
        for (int b = 0; b <= bays; ++b) longerons[c].push_back(d.node(b * 4.0, corner[c][0], corner[c][1]));
        d.clamp(longerons[c].front());
        d.chain(longerons[c], "Tube 101.6x2.11", kAl2024);
      }
      for (int b = 0; b <= bays; ++b) {
        for (std::size_t c = 0; c < 4; ++c) d.beam(longerons[c][static_cast<std::size_t>(b)], longerons[(c + 1) % 4][static_cast<std::size_t>(b)], "Tube 63.5x1.65", kAl2024);
      }
      for (int b = 0; b < bays; ++b) {
        for (std::size_t c = 0; c < 4; ++c) {
          d.beam(longerons[c][static_cast<std::size_t>(b)], longerons[(c + 1) % 4][static_cast<std::size_t>(b + 1)], "Tube 63.5x1.65", kAl2024);
        }
      }
      d.mesh.gravity = {0.0, 0.0, 0.0};
      for (std::size_t c = 0; c < 4; ++c) d.load(longerons[c].back(), {0, 2 * kKN, 0}, {0.75 * kKN, 0, 0});
      return make(std::move(d), "space_station_truss", "Space station truss segment", "Aerospace",
                  "16 m integrated truss segment, 3 x 3 m section, 4 bays: longerons Tube 101.6x2.11, frames and face diagonals "
                  "Tube 63.5x1.65 (2024-T3), clamped to the module. In orbit (no gravity): solar array interface loads at the free end, "
                  "8 kN lateral and 3 kNm torque from a reboost.");
    }

    LibraryBeam lunarLander(const Lists& l) {
      Draft d(l.materials, l.sections);
      const std::array<std::array<double, 2>, 4> corner{{{-1.5, -1.5}, {1.5, -1.5}, {1.5, 1.5}, {-1.5, 1.5}}};
      std::array<std::uint32_t, 4> top{}, bottom{}, mid{};
      for (std::size_t c = 0; c < 4; ++c) {
        top[c] = d.node(corner[c][0], 2.0, corner[c][1]);
        bottom[c] = d.node(corner[c][0], 1.2, corner[c][1]);
      }
      for (std::size_t c = 0; c < 4; ++c) {
        const auto& a = corner[c];
        const auto& b = corner[(c + 1) % 4];
        mid[c] = d.node(0.5 * (a[0] + b[0]), 1.2, 0.5 * (a[1] + b[1]));
      }
      for (std::size_t c = 0; c < 4; ++c) {
        d.beam(top[c], top[(c + 1) % 4], "Box 200x100x6", kTi64);
        d.beam(bottom[c], mid[c], "Box 200x100x6", kTi64);
        d.beam(mid[c], bottom[(c + 1) % 4], "Box 200x100x6", kTi64);
        d.beam(top[c], bottom[c], "Box 200x100x6", kTi64, {1, 0, 0});
      }
      for (std::size_t c = 0; c < 4; ++c) {
        const auto pad = d.node(corner[c][0] * 1.75, 0, corner[c][1] * 1.75);
        d.pin(pad);
        d.beam(top[c], pad, "Tube 101.6x2.11", kTi64);
        d.beam(mid[c], pad, "Tube 63.5x1.65", kTi64);
        d.beam(mid[(c + 3) % 4], pad, "Tube 63.5x1.65", kTi64);
        d.load(top[c], {0, -12.2 * kKN, 0});
      }
      d.mesh.gravity = {0.0, -3.0 * 1.62, 0.0};
      return make(std::move(d), "space_lunar_lander_legs", "Lunar lander legs", "Aerospace",
                  "3 x 3 m body frame (Box 200x100x6, Ti-6Al-4V) on four inverted-tripod legs: primary strut Tube 101.6x2.11 and two "
                  "secondary struts Tube 63.5x1.65 to each footpad (pinned on the regolith, 5.25 m apart). Touchdown at 3 lunar g with "
                  "10 t: 12.2 kN per upper corner, structure at 3 x 1.62 m/s^2.");
    }

    LibraryBeam thrustFrame(const Lists& l) {
      Draft d(l.materials, l.sections);
      constexpr int count = 8;
      const auto gimbal = d.node(0, 0, 0);
      std::vector<std::uint32_t> ring;
      for (int k = 0; k < count; ++k) {
        const double angle = 2.0 * kPi * k / count;
        ring.push_back(d.node(1.2 * std::cos(angle), 1.5, 1.2 * std::sin(angle)));
        d.clamp(ring.back());
      }
      for (int k = 0; k < count; ++k) {
        d.beam(ring[static_cast<std::size_t>(k)], ring[static_cast<std::size_t>((k + 1) % count)], "Box 200x100x6", kTi64);
        d.beam(gimbal, ring[static_cast<std::size_t>(k)], "Tube 101.6x2.11", kTi64);
      }
      d.load(gimbal, {9 * kKN, 900 * kKN, 0});
      return make(std::move(d), "space_rocket_thrust_frame", "Rocket engine thrust frame", "Aerospace",
                  "Eight struts Tube 101.6x2.11 from the engine gimbal to a 2.4 m thrust ring (Box 200x100x6), titanium Ti-6Al-4V, ring bolted "
                  "to the tank skirt (clamped). 900 kN engine thrust with 1 % side load from gimbal misalignment. Self weight.");
    }

    LibraryBeam quadcopter(const Lists& l) {
      Draft d(l.materials, l.sections);
      const auto centre = d.node(0, 0, 0);
      d.clamp(centre);
      for (int a = 0; a < 4; ++a) {
        const double angle = kPi / 4 + kPi / 2 * a;
        const auto mid = d.node(0.175 * std::cos(angle), 0, 0.175 * std::sin(angle));
        const auto tip = d.node(0.35 * std::cos(angle), 0, 0.35 * std::sin(angle));
        d.chain({centre, mid, tip}, "Tube 25.4x1.24", kAl7075);
        d.load(tip, {0, 30.0 - 2.5, 0}, {0, a % 2 == 0 ? 0.6 : -0.6, 0});
      }
      return make(std::move(d), "aero_quadcopter_frame", "Quadcopter frame", "Aerospace",
                  "Four 0.35 m arms Tube 25.4x1.24 (7075-T6) from the body (clamped), X layout. Full-throttle climb: 30 N thrust per motor "
                  "minus 2.5 N motor weight, rotor reaction torque 0.6 Nm alternating with the spin direction. Self weight.");
    }

    LibraryBeam fuselageSection(const Lists& l) {
      Draft d(l.materials, l.sections);
      constexpr int frames = 4, points = 16;
      const double radius = 1.0;
      std::vector<std::vector<std::uint32_t>> ring(frames);
      for (int f = 0; f < frames; ++f) {
        for (int k = 0; k < points; ++k) {
          const double angle = 2.0 * kPi * k / points;
          ring[static_cast<std::size_t>(f)].push_back(d.node(f * 0.5, radius * std::cos(angle), radius * std::sin(angle)));
        }
        for (int k = 0; k < points; ++k) {
          const double angle = 2.0 * kPi * (k + 0.5) / points;
          d.beam(ring[static_cast<std::size_t>(f)][static_cast<std::size_t>(k)], ring[static_cast<std::size_t>(f)][static_cast<std::size_t>((k + 1) % points)],
                 "Box 80x40x3", kAl2024, {0, std::cos(angle), std::sin(angle)}); // frame depth radial
        }
        const auto floor = d.beam(ring[static_cast<std::size_t>(f)][5], ring[static_cast<std::size_t>(f)][11], "Box 60x40x3", kAl2024);
        d.line(floor, {0, -3 * kKN, 0});
      }
      for (int f = 0; f + 1 < frames; ++f) {
        for (int k = 0; k < points; ++k) d.beam(ring[static_cast<std::size_t>(f)][static_cast<std::size_t>(k)], ring[static_cast<std::size_t>(f + 1)][static_cast<std::size_t>(k)], "Tube 25.4x1.24", kAl2024);
      }
      d.clamp(ring.front()[8]);
      d.clamp(ring.back()[8]);
      return make(std::move(d), "aero_fuselage_section", "Fuselage barrel section", "Aerospace",
                  "2 m diameter barrel, 4 frames at 0.5 m (Box 80x40x3, depth radial) joined by 16 stringers (Tube 25.4x1.24), floor beams "
                  "Box 60x40x3. Cabin floor 3 kN/m, keel fittings at the first and last frame clamped. Aluminium 2024-T3, self weight.");
    }

    LibraryBeam tailBoom(const Lists& l) {
      Draft d(l.materials, l.sections);
      const auto boom = line(d, {0, 0, 0}, {4.5, 0, 0}, 9);
      d.clamp(boom.front());
      for (std::size_t i = 0; i + 1 < boom.size(); ++i) {
        d.beam(boom[i], boom[i + 1], i < 3 ? "CHS 139.7x6.3" : (i < 6 ? "CHS 114.3x5" : "Tube 101.6x2.11"), kAl2024);
      }
      d.load(boom.back(), {0, -0.6 * kKN, 1.2 * kKN}, {0.4 * kKN, 0, 0});
      return make(std::move(d), "aero_tail_boom", "Helicopter tail boom", "Aerospace",
                  "4.5 m tapered boom in three tubes (139.7x6.3, 114.3x5, 101.6x2.11, 2024-T3), clamped to the fuselage. Tail rotor thrust 1.2 kN "
                  "sideways, stabiliser download 0.6 kN and fin torque 0.4 kNm at the tip. Self weight.");
    }

    LibraryBeam solarArrayBoom(const Lists& l) {
      Draft d(l.materials, l.sections);
      const auto boom = line(d, {0, 0, 0}, {10, 0, 0}, 10);
      d.clamp(boom.front());
      d.chain(boom, "Tube 76.2x2.11", kAl6061);
      d.mesh.gravity = {0.0, 0.0, 0.0};
      d.load(boom.back(), {0, 40, 20});
      return make(std::move(d), "space_solar_array_boom", "Solar array boom", "Aerospace",
                  "10 m deployed boom Tube 76.2x2.11 (6061-T6) clamped at the spacecraft. In orbit (no gravity): attitude manoeuvre inertia "
                  "of the array at the tip, 40 N and 20 N lateral.");
    }

    // ---- Hinges & Pins (end releases) -----------------------------------------------------------
    // A pin is an end release on one side of a joint: the other member keeps the node's rotation
    // defined. Pin-ended members that end at an airframe or foundation fitting go to a clamped
    // node, the release being the pin.

    LibraryBeam threeHingedFrame(const Lists& l) {
      Draft d(l.materials, l.sections);
      const double span = 20.0, eaves = 5.0, ridge = 6.5, spacing = 6.0;
      std::vector<std::array<std::uint32_t, 5>> frames;
      for (int f = 0; f < 4; ++f) {
        const double z = f * spacing, share = (f == 0 || f == 3) ? 0.5 : 1.0;
        const std::array<std::uint32_t, 5> n{d.node(0, 0, z), d.node(0, eaves, z), d.node(span / 2, ridge, z), d.node(span, eaves, z), d.node(span, 0, z)};
        for (const auto base : {n[0], n[4]}) d.support(base, {true, true, true}, {true, true, false}); // pinned in the frame plane
        d.line(d.beam(n[0], n[1], "HEB 400", kS355, {1, 0, 0}), {2.4 * kKN * share, 0, 0}); // wind on the windward column
        d.beam(n[4], n[3], "HEB 400", kS355, {1, 0, 0});
        const auto left = d.beam(n[1], n[2], "IPE 550", kS355);
        d.line(d.beam(n[2], n[3], "IPE 550", kS355), {0, -7.2 * kKN * share, 0});
        d.line(left, {0, -7.2 * kKN * share, 0});
        d.release(left, kPinZAtB); // ridge hinge
        frames.push_back(n);
      }
      for (std::size_t f = 0; f + 1 < frames.size(); ++f) {
        for (const std::size_t k : {1u, 2u, 3u}) d.release(d.beam(frames[f][k], frames[f + 1][k], "IPE 220", kS355), kPinnedBothEnds);
      }
      for (const std::size_t f : {0u, 2u}) { // wall bracing in two bays of both side walls
        for (const std::size_t side : {0u, 4u}) {
          const std::size_t top = side == 0 ? 1 : 3;
          d.release(d.beam(frames[f][side], frames[f + 1][top], "CHS 88.9x5", kS355), kPinnedBothEnds);
        }
      }
      return make(std::move(d), "hinge_three_hinged_frame", "Three-hinged portal frame", "Hinges & Pins",
                  "Statically determinate frame: span 20 m, eaves 5 m, ridge 6.5 m, bases pinned in the frame plane and a ridge hinge (left rafter "
                  "released about its local z). Horizontal thrust H = w L^2 / (8 f). 4 frames at 6 m, columns HEB 400, rafters IPE 550; eaves "
                  "and ridge ties IPE 220 and wall braces CHS 88.9x5 pinned at both ends. Roof 7.2 kN/m, wind 2.4 kN/m. S355, self weight.");
    }

    LibraryBeam gerberGirder(const Lists& l) {
      Draft d(l.materials, l.sections);
      const double width = 4.0;
      std::array<std::vector<std::uint32_t>, 2> girders;
      for (std::size_t g = 0; g < 2; ++g) {
        const double z = static_cast<double>(g) * width;
        girders[g] = line(d, {0, 0, z}, {70, 0, z}, 35); // 2 m segments
        const auto elements = d.chain(girders[g], "HEB 600", kS355);
        d.line(elements, {0, -18 * kKN, 0});
        d.release(elements[12], kPinZAtB); // hinges 6 m into the main span (x = 26 m and 44 m)
        d.release(elements[22], kPinZAtA);
        for (const std::size_t at : {0u, 10u, 25u, 35u}) { // abutments and piers; x held at the first pier
          d.support(girders[g][at], {at == 10, true, true}, {true, false, false});
        }
      }
      for (std::size_t i = 0; i < girders[0].size(); i += 5) d.beam(girders[0][i], girders[1][i], "IPE 400", kS355, {0, 1, 0});
      return make(std::move(d), "hinge_gerber_girder", "Gerber girder bridge", "Hinges & Pins",
                  "Cantilever-and-suspended-span bridge 20 + 30 + 20 m: two hinges 6 m into the main span (released about z) carry an 18 m "
                  "suspended span, so the girder is statically determinate in its plane. Twin HEB 600 girders 4 m apart, cross girders IPE 400 "
                  "every 10 m, deck 18 kN/m per girder. S355, self weight.");
    }

    LibraryBeam simpleConnectionFrame(const Lists& l) {
      Draft d(l.materials, l.sections);
      auto g = buildGrid(d, steps(6.0, 2), steps(6.0, 2), steps(3.5, 3), "HEB 240", "IPE 360", "IPE 300", kS355);
      for (std::size_t j = 0; j < g.zs.size(); ++j) {
        for (std::size_t i = 0; i < g.xs.size(); ++i) d.support(g.at(i, j, 0), {true, true, true}, {false, true, false}); // pinned base plates
      }
      d.release(g.beamsX, kPinnedBothEnds); // simple (shear) connections
      d.release(g.beamsZ, kPinnedBothEnds);
      d.line(g.beamsX, {0, -20 * kKN, 0});
      d.line(g.beamsZ, {0, -5 * kKN, 0});
      for (std::size_t k = 0; k + 1 < g.ys.size(); ++k) { // X bracing in one bay of every face
        for (const std::size_t j : {std::size_t{0}, g.zs.size() - 1}) {
          d.release(d.beam(g.at(0, j, k), g.at(1, j, k + 1), "CHS 114.3x5", kS355), kStrut);
          d.release(d.beam(g.at(1, j, k), g.at(0, j, k + 1), "CHS 114.3x5", kS355), kStrut);
        }
        for (const std::size_t i : {std::size_t{0}, g.xs.size() - 1}) {
          d.release(d.beam(g.at(i, 0, k), g.at(i, 1, k + 1), "CHS 114.3x5", kS355), kStrut);
          d.release(d.beam(g.at(i, 1, k), g.at(i, 0, k + 1), "CHS 114.3x5", kS355), kStrut);
        }
      }
      // Plan bracing in every bay of every floor stands in for the floor diaphragm: pinned beams
      // alone carry no shear, so the inner column lines would sway freely (a mechanism).
      for (std::size_t k = 1; k < g.ys.size(); ++k) {
        for (std::size_t j = 0; j + 1 < g.zs.size(); ++j) {
          for (std::size_t i = 0; i + 1 < g.xs.size(); ++i) {
            d.release(d.beam(g.at(i, j, k), g.at(i + 1, j + 1, k), "CHS 76.1x4", kS355), kStrut);
            d.release(d.beam(g.at(i + 1, j, k), g.at(i, j + 1, k), "CHS 76.1x4", kS355), kStrut);
          }
        }
        for (std::size_t j = 0; j < g.zs.size(); ++j) d.load(g.at(0, j, k), {15 * kKN, 0, 0}); // wind in x
      }
      return make(std::move(d), "hinge_simple_connection_frame", "Braced frame with simple connections", "Hinges & Pins",
                  "2 x 2 bays of 6 m, 3 storeys of 3.5 m. Beams IPE 360 (x) / IPE 300 (z) pinned at both ends (shear connections), so they "
                  "span simply; continuous HEB 240 columns on pinned base plates; stability from pin-ended X bracing CHS 114.3x5 in every face and plan "
                  "bracing CHS 76.1x4 in every bay (the floor diaphragm: without it the inner column lines are a mechanism). "
                  "Floors 20 / 5 kN/m, wind 15 kN per node of the x = 0 face. S355, self weight.");
    }

    LibraryBeam pinnedWebTruss(const Lists& l) {
      Draft d(l.materials, l.sections);
      const double span = 24.0, depth = 2.4, spacing = 6.0;
      const int panels = 8;
      std::array<std::vector<std::uint32_t>, 2> tops;
      for (std::size_t t = 0; t < 2; ++t) {
        const double z = static_cast<double>(t) * spacing;
        const auto bottom = line(d, {0, 0, z}, {span, 0, z}, panels);
        const auto top = line(d, {0, depth, z}, {span, depth, z}, panels);
        d.chain(bottom, "SHS 120x120x8", kS355); // continuous chords
        d.chain(top, "SHS 120x120x8", kS355);
        for (int p = 0; p <= panels; ++p) {
          const auto post = d.beam(bottom[p], top[p], "SHS 80x80x6.3", kS355, {1, 0, 0});
          if (p != 0 && p != panels) d.release(post, kPinnedBothEnds); // end posts rigid: portal action out of plane
        }
        for (int p = 0; p < panels; ++p) { // Pratt diagonals, in tension under gravity
          const bool left = p < panels / 2;
          d.release(d.beam(left ? top[p] : bottom[p], left ? bottom[p + 1] : top[p + 1], "CHS 88.9x5", kS355), kStrut);
        }
        d.support(bottom.front(), {true, true, true}, {true, true, false});
        d.support(bottom.back(), {false, true, true}, {true, true, false});
        for (int p = 0; p <= panels; ++p) d.load(top[p], {0, (p == 0 || p == panels ? -6.0 : -12.0) * kKN, 0});
        tops[t] = top;
      }
      for (int p = 0; p <= panels; ++p) d.release(d.beam(tops[0][p], tops[1][p], "IPE 160", kS355), kPinnedBothEnds); // purlins
      for (const int p : {0, panels - 1}) { // roof bracing in the end panels
        d.release(d.beam(tops[0][p], tops[1][p + 1], "CHS 60.3x4", kS355), kStrut);
        d.release(d.beam(tops[1][p], tops[0][p + 1], "CHS 60.3x4", kS355), kStrut);
      }
      return make(std::move(d), "hinge_pinned_web_truss", "Roof truss with pin-ended web", "Hinges & Pins",
                  "Two 24 m Pratt trusses 2.4 m deep, 6 m apart: continuous SHS 120x120x8 chords, posts SHS 80x80x6.3 and diagonals CHS 88.9x5 "
                  "pinned at both ends (axial force only), rigid end posts. Purlins IPE 160 pinned, roof bracing CHS 60.3x4 in the end panels. "
                  "Pin and roller bearings, 12 kN per top chord node. S355, self weight.");
    }

    LibraryBeam loaderCrane(const Lists& l) {
      Draft d(l.materials, l.sections);
      const double angle = kPi / 12, reach = 4.0;
      const auto at = [&](const double s) { return P{s * std::cos(angle), 2.0 + s * std::sin(angle), 0.0}; };
      const auto base = d.node(0, 0, 0), lug = d.node(0, 0.8, 0), top = d.node(0, 2.0, 0);
      const auto ram = d.node(at(1.2)[0], at(1.2)[1], 0), mid = d.node(at(2.6)[0], at(2.6)[1], 0), tip = d.node(at(reach)[0], at(reach)[1], 0);
      d.clamp(base);
      d.chain({base, lug, top}, "RHS 300x200x12.5", kS355, {1, 0, 0});
      const auto boom = d.chain({top, ram, mid, tip}, "RHS 250x150x10", kS355);
      d.release(boom.front(), kPinZAtA); // boom pivot at the column head
      d.release(d.beam(lug, ram, "CHS 114.3x5", kS355), kStrut); // luffing cylinder, clevis pins at both ends
      d.load(tip, {0, -10 * kKN, 0.5 * kKN});
      return make(std::move(d), "hinge_loader_crane", "Loader crane (pinned boom and cylinder)", "Hinges & Pins",
                  "2 m column RHS 300x200x12.5 clamped to the truck frame; 4 m boom RHS 250x150x10 at 15 deg, pinned to the column head "
                  "about z; luffing cylinder (CHS 114.3x5) pinned at both ends and free to spin, so it carries axial force only. 1 t at the "
                  "tip plus a 0.5 kN side pull. S355, self weight.");
    }

    LibraryBeam bracedLandingGear(const Lists& l) {
      Draft d(l.materials, l.sections);
      const auto trunnion = d.node(0, 0, 0), knee = d.node(0, -0.7, 0), axle = d.node(0, -1.6, 0), wheel = d.node(0, -1.6, 0.15);
      const auto sideFitting = d.node(0, 0, -0.9), dragFitting = d.node(0.8, 0, 0);
      d.support(trunnion, {true, true, true}, {false, true, true}); // trunnion bearing: free about x (retraction)
      d.clamp(sideFitting);
      d.clamp(dragFitting);
      d.chain({trunnion, knee, axle}, "CHS 168.3x8", kSteel4130, {1, 0, 0}, kTI);
      d.beam(axle, wheel, "CHS 114.3x5", kSteel4130, {}, kTI);
      d.release(d.beam(sideFitting, knee, "CHS 76.1x4", kSteel4130), kStrut); // side brace (locks the retraction)
      d.release(d.beam(dragFitting, knee, "CHS 76.1x4", kSteel4130), kStrut); // drag brace
      d.load(wheel, {-12 * kKN, 40 * kKN, -6 * kKN});
      return make(std::move(d), "hinge_braced_landing_gear", "Main landing gear (pinned braces)", "Hinges & Pins",
                  "Cantilever main gear leg CHS 168.3x8 hung from a trunnion that is free about the fore-aft axis (retraction); a side brace "
                  "and a drag brace (CHS 76.1x4) pinned at both ends to the knee lock it. Landing load at the wheel: 40 kN up, 12 kN drag, "
                  "6 kN side. AISI 4130, Timoshenko leg, self weight.");
    }

    // ---- Large Structures ------------------------------------------------------------------------
    // Thousands of elements: realistic sizes for the solver, the level of detail and the GUI.

    LibraryBeam stadium(const Lists& l) {
      Draft d(l.materials, l.sections);
      constexpr int frames = 48;
      const double aIn = 60.0, bIn = 40.0; // inner edge of the stands: an ellipse around the pitch
      const std::vector<double> standS{0.0, 10.0, 20.0, 30.0};
      const std::vector<double> roofS{30.0, 25.0, 20.0, 15.0, 10.0, 5.0, 0.0, -5.0, -10.0}; // the roof reaches 10 m over the pitch
      const auto topY = [](const double s) { return 40.0 - 0.05 * (30.0 - s); };
      const auto depth = [](const double s) { return 1.5 + 4.5 * (s + 10.0) / 40.0; };
      struct Frame { std::vector<std::uint32_t> stand, top, bottom; };
      std::vector<Frame> frame(frames);
      for (int f = 0; f < frames; ++f) {
        const double angle = 2.0 * kPi * f / frames, c = std::cos(angle), sn = std::sin(angle);
        const P radial{c, 0.0, sn};
        const auto at = [&](const double s, const double y) { return d.node((aIn + s) * c, y, (bIn + s) * sn); };
        auto& fr = frame[static_cast<std::size_t>(f)];
        for (const double s : standS) fr.stand.push_back(at(s, 2.0 + 0.6 * s));
        d.line(d.chain(fr.stand, "HEA 600", kS355), {0, -35 * kKN, 0}); // raking beam: crowd 4 kN/m^2 + seating
        for (std::size_t i = 0; i < 3; ++i) {
          const auto base = at(standS[i], 0.0);
          d.clamp(base);
          d.beam(base, fr.stand[i], "HEB 400", kS355, radial);
        }
        fr.top.push_back(at(36.0, topY(30.0)));
        for (const double s : roofS) {
          fr.top.push_back(at(s, topY(s)));
          fr.bottom.push_back(at(s, topY(s) - depth(s)));
        }
        // Back column: ground, stand top, roof bottom chord, roof top chord.
        const auto back = at(30.0, 0.0);
        d.clamp(back);
        d.chain({back, fr.stand.back(), fr.bottom.front(), fr.top[1]}, "HEB 600", kS355, radial);
        // Rear tie holding the cantilever down.
        const auto anchor = at(36.0, 0.0);
        d.clamp(anchor);
        d.beam(anchor, fr.top.front(), "CHS 323.9x12.5", kS355, radial);
        d.beam(fr.top.front(), fr.bottom.front(), "CHS 323.9x12.5", kS355); // back panel strut: carries the tie-down force
        // Cantilever roof truss: tapered from 6 m at the back column to 1.5 m at the tip.
        const auto topChord = d.chain(fr.top, "CHS 323.9x12.5", kS355);
        d.line(std::vector<std::uint32_t>(topChord.begin() + 1, topChord.end()), {0, -6 * kKN, 0}); // cladding and snow
        d.chain(fr.bottom, "CHS 273x10", kS355);
        for (std::size_t i = 1; i < fr.bottom.size(); ++i) {
          d.beam(fr.bottom[i], fr.top[i + 1], "CHS 168.3x8", kS355, radial);
          d.beam(fr.top[i], fr.bottom[i], "CHS 168.3x8", kS355);
        }
      }
      for (int f = 0; f < frames; ++f) {
        const auto& a = frame[static_cast<std::size_t>(f)];
        const auto& b = frame[static_cast<std::size_t>((f + 1) % frames)];
        for (std::size_t i = 0; i < a.stand.size(); ++i) d.beam(a.stand[i], b.stand[i], "IPE 400", kS355);
        for (std::size_t i = 0; i < a.top.size(); ++i) d.beam(a.top[i], b.top[i], i + 1 == a.top.size() ? "CHS 273x10" : "CHS 168.3x8", kS355);
        for (std::size_t i = 0; i < a.bottom.size(); ++i) d.beam(a.bottom[i], b.bottom[i], i + 1 == a.bottom.size() ? "CHS 273x10" : "CHS 168.3x8", kS355);
        if (f % 4 == 0) { // roof plane bracing
          for (std::size_t i = 1; i + 1 < a.top.size(); ++i) d.beam(a.top[i], b.top[i + 1], "CHS 114.3x5", kS355);
        }
      }
      return make(std::move(d), "large_stadium", "Football stadium (bowl and cantilever roof)", "Large Structures",
                  "Elliptical bowl around a 120 x 80 m inner edge, 48 radial frames: raking beams HEA 600 on HEB 400 columns carry the "
                  "stands (35 kN/m), HEB 600 back columns carry 40 m cantilever roof trusses (CHS 323.9 / 273 chords, CHS 168.3 web, 6 to "
                  "1.5 m deep) tied down at the back. Ring beams at every chord node and the stands, a compression ring at the roof tip, "
                  "roof bracing in every fourth bay. Roof 6 kN/m per truss. S355, self weight.");
    }

    LibraryBeam airportTerminal(const Lists& l) {
      Draft d(l.materials, l.sections);
      constexpr int nx = 24, nz = 12; // 6 m modules: 144 x 72 m
      const double module = 6.0, floorY = 6.0, bottomY = 15.0, topY = 18.0;
      using Grid2 = std::vector<std::vector<std::uint32_t>>;
      const auto grid = [&](const int countX, const int countZ, const double y, const double offset) {
        Grid2 g(static_cast<std::size_t>(countX), std::vector<std::uint32_t>(static_cast<std::size_t>(countZ)));
        for (int i = 0; i < countX; ++i) {
          for (int j = 0; j < countZ; ++j) g[static_cast<std::size_t>(i)][static_cast<std::size_t>(j)] = d.node(offset + module * i, y, offset + module * j);
        }
        return g;
      };
      const auto floorGrid = grid(nx + 1, nz + 1, floorY, 0.0);
      const auto bottom = grid(nx + 1, nz + 1, bottomY, 0.0);
      const auto top = grid(nx, nz, topY, module / 2.0);
      const auto at = [](const Grid2& g, const int i, const int j) { return g[static_cast<std::size_t>(i)][static_cast<std::size_t>(j)]; };

      // Departures floor: a grillage on the 12 m column grid, 4 kN/m^2 on the x beams.
      for (int j = 0; j <= nz; ++j) {
        for (int i = 0; i < nx; ++i) {
          const double share = (j == 0 || j == nz) ? 0.5 : 1.0;
          d.line(d.beam(at(floorGrid, i, j), at(floorGrid, i + 1, j), j % 2 == 0 ? "HEB 600" : "HEB 500", kS355), {0, -24 * kKN * share, 0});
        }
      }
      for (int i = 0; i <= nx; ++i) {
        for (int j = 0; j < nz; ++j) d.beam(at(floorGrid, i, j), at(floorGrid, i, j + 1), i % 2 == 0 ? "HEB 600" : "HEB 500", kS355);
      }
      // Roof: square-on-square-offset space frame, 3 m deep.
      for (int j = 0; j <= nz; ++j) {
        for (int i = 0; i < nx; ++i) d.beam(at(bottom, i, j), at(bottom, i + 1, j), "CHS 139.7x6.3", kS355);
      }
      for (int i = 0; i <= nx; ++i) {
        for (int j = 0; j < nz; ++j) d.beam(at(bottom, i, j), at(bottom, i, j + 1), "CHS 139.7x6.3", kS355);
      }
      for (int i = 0; i < nx; ++i) {
        for (int j = 0; j < nz; ++j) {
          if (i + 1 < nx) d.beam(at(top, i, j), at(top, i + 1, j), "CHS 139.7x6.3", kS355);
          if (j + 1 < nz) d.beam(at(top, i, j), at(top, i, j + 1), "CHS 139.7x6.3", kS355);
          for (const auto& [di, dj] : {std::pair{0, 0}, std::pair{1, 0}, std::pair{0, 1}, std::pair{1, 1}}) {
            d.beam(at(top, i, j), at(bottom, i + di, j + dj), "CHS 114.3x5", kS355);
          }
          d.load(at(top, i, j), {0, -54 * kKN, 0}); // roof 1.5 kN/m^2 over a 6 x 6 m module
        }
      }
      // Columns on the 12 m grid: HEB 400 below the floor, CHS 323.9x12.5 up to the roof.
      for (int i = 0; i <= nx; i += 2) {
        for (int j = 0; j <= nz; j += 2) {
          const auto base = d.node(module * i, 0.0, module * j);
          d.clamp(base);
          d.beam(base, at(floorGrid, i, j), "HEB 400", kS355, {1, 0, 0});
          d.beam(at(floorGrid, i, j), at(bottom, i, j), "CHS 323.9x12.5", kS355, {1, 0, 0});
        }
      }
      for (int i = 0; i <= nx; ++i) d.load(at(bottom, i, 0), {0, 0, 12 * kKN}); // wind on the airside facade
      return make(std::move(d), "large_airport_terminal", "Airport terminal hall", "Large Structures",
                  "144 x 72 m hall on a 12 m column grid (91 columns: HEB 400 to the departures floor at 6 m, CHS 323.9x12.5 to the roof). "
                  "Floor grillage HEB 600 on the column lines, HEB 500 between, 4 kN/m^2. Roof: 3 m deep square-on-square-offset space "
                  "frame on 6 m modules (CHS 139.7x6.3 chords, CHS 114.3x5 diagonals), 1.5 kN/m^2, wind 12 kN per facade node. S355, "
                  "self weight.");
    }

    LibraryBeam airliner(const Lists& l) {
      Draft d(l.materials, l.sections);
      // x aft from the nose, y up, z to the right wing; ground at y = 0.
      constexpr int ringPoints = 20, firstFrame = 1, lastFrame = 37;
      const auto radius = [](const double x) { return x < 6.0 ? 2.0 * std::sqrt(x / 6.0) : (x > 28.0 ? 2.0 * (1.0 - 0.65 * (x - 28.0) / 9.0) : 2.0); };
      const auto centre = [](const double x) { return 3.2 + (x > 28.0 ? 0.11 * (x - 28.0) : 0.0); };
      std::vector<std::vector<std::uint32_t>> rings;
      for (int f = firstFrame; f <= lastFrame; ++f) {
        const double x = f, r = radius(x), yc = centre(x);
        auto& ring = rings.emplace_back();
        for (int k = 0; k < ringPoints; ++k) {
          const double angle = 2.0 * kPi * k / ringPoints;
          ring.push_back(d.node(x, yc + r * std::sin(angle), r * std::cos(angle)));
        }
        const bool wingFrame = f == 15 || f == 17;
        for (int k = 0; k < ringPoints; ++k) {
          const double mid = 2.0 * kPi * (k + 0.5) / ringPoints;
          d.beam(ring[static_cast<std::size_t>(k)], ring[static_cast<std::size_t>((k + 1) % ringPoints)], wingFrame ? "Box 200x100x6" : "Box 80x40x3",
                 kAl2024, {0, std::sin(mid), std::cos(mid)}); // frame depth radial
        }
        const auto floor = d.beam(ring[11], ring[19], "Box 120x60x4", kAl2024);
        if (x >= 6.0 && x <= 30.0) d.line(floor, {0, -1.6 * kKN, 0}); // passengers, seats and cargo: 6 kN per metre of cabin
      }
      for (std::size_t f = 0; f + 1 < rings.size(); ++f) {
        for (std::size_t k = 0; k < ringPoints; ++k) {
          const std::size_t next = (k + 1) % ringPoints;
          d.beam(rings[f][k], rings[f + 1][k], "Box 60x40x3", kAl7075); // stringers
          // Skin shear panels as one diagonal each, alternating.
          if ((f + k) % 2 == 0) d.beam(rings[f][k], rings[f + 1][next], "Box 40x40x2", kAl2024);
          else d.beam(rings[f][next], rings[f + 1][k], "Box 40x40x2", kAl2024);
        }
      }
      const auto nearestFuselageNode = [&](const std::uint32_t node) {
        const auto& p = d.mesh.nodes[node].getLocation();
        std::uint32_t best = rings.front().front();
        double bestDistance = 1e300;
        for (const auto& ring : rings) {
          for (const auto candidate : ring) {
            const auto& q = d.mesh.nodes[candidate].getLocation();
            const double distance = std::hypot(p[0] - q[0], p[1] - q[1], p[2] - q[2]);
            if (distance < bestDistance) {
              bestDistance = distance;
              best = candidate;
            }
          }
        }
        return best;
      };

      // Torsion box of a lifting surface: four spar caps (front / rear, upper / lower), a rib at every
      // station, the spar webs and skins as diagonals. thick = the box depth direction.
      using Corners = std::array<std::uint32_t, 4>; // front upper, front lower, rear upper, rear lower
      // Stations below `heavy` get heavier spar webs (gear and engine loads enter there).
      const auto liftingBox = [&](const int stations, const auto& frontAt, const auto& chordAt, const auto& depthAt, const P thick,
                                  const auto& capSection, const int heavy = 0) {
        std::vector<Corners> box;
        const Eigen::Vector3d t(thick[0], thick[1], thick[2]);
        for (int i = 0; i <= stations; ++i) {
          const Eigen::Vector3d front = frontAt(i), rear = front + Eigen::Vector3d(chordAt(i), 0.0, 0.0);
          const Eigen::Vector3d half = 0.5 * depthAt(i) * t;
          const auto node = [&](const Eigen::Vector3d& v) { return d.node(v[0], v[1], v[2]); };
          box.push_back({node(front + half), node(front - half), node(rear + half), node(rear - half)});
          const auto& c = box.back();
          d.beam(c[0], c[2], "Box 60x40x3", kAl2024, thick); // rib caps
          d.beam(c[1], c[3], "Box 60x40x3", kAl2024, thick);
          const char* web = i < heavy ? "Box 120x60x4" : "Box 80x40x3";
          d.beam(c[0], c[1], web, kAl2024, {1, 0, 0}); // spar webs
          d.beam(c[2], c[3], web, kAl2024, {1, 0, 0});
          d.beam(c[0], c[3], "Box 40x40x2", kAl2024, thick); // rib web
        }
        for (int i = 0; i < stations; ++i) {
          const auto& a = box[static_cast<std::size_t>(i)];
          const auto& b = box[static_cast<std::size_t>(i + 1)];
          for (std::size_t corner = 0; corner < 4; ++corner) d.beam(a[corner], b[corner], capSection(i), kAl7075, thick);
          const char* web = i < heavy ? "Box 120x60x4" : "Box 60x40x3";
          d.beam(a[0], b[1], web, kAl2024, {1, 0, 0}); // spar webs
          d.beam(a[2], b[3], web, kAl2024, {1, 0, 0});
          d.beam(a[0], b[2], "Box 60x40x3", kAl2024, thick);     // skins
          d.beam(a[1], b[3], "Box 60x40x3", kAl2024, thick);
        }
        return box;
      };
      const auto linkRoot = [&](const Corners& root) {
        for (const auto corner : root) d.beam(corner, nearestFuselageNode(corner), "Box 200x100x6", kAl7075);
      };

      // Wings: 15.1 m semi-span, 25 deg sweep, taper 2.0 -> 1.0 m box chord, 0.8 -> 0.25 m depth, dihedral.
      constexpr int wingStations = 15;
      std::array<std::vector<Corners>, 2> wings;
      for (std::size_t side = 0; side < 2; ++side) {
        const double sign = side == 0 ? 1.0 : -1.0;
        const auto spanAt = [](const int i) { return 1.9 + 15.1 * i / wingStations; };
        wings[side] = liftingBox(
          wingStations,
          [&](const int i) { const double z = spanAt(i); return Eigen::Vector3d(15.0 + 0.45 * (z - 1.9), 1.75 + 0.08 * (z - 1.9), sign * z); },
          [](const int i) { return 2.0 - 1.0 * i / wingStations; },
          [](const int i) { return 0.8 - 0.55 * i / wingStations; },
          P{0, 1, 0},
          [](const int i) { return i < 6 ? "Box 200x100x6" : (i < 11 ? "Box 120x60x4" : "Box 80x40x3"); }, 5);
        linkRoot(wings[side].front());
        for (std::size_t i = 0; i <= 10; ++i) { // fuel in the inner tanks: 3 kN per station on each lower spar cap
          d.load(wings[side][i][1], {0, -3 * kKN, 0});
          d.load(wings[side][i][3], {0, -3 * kKN, 0});
        }
        // Main gear under the rear spar at station 2, engine on a pylon at station 4.
        const auto gearTop = wings[side][2][3];
        const auto& g = d.mesh.nodes[gearTop].getLocation();
        const auto ground = d.node(g[0], 0.0, g[2]);
        d.clamp(ground);
        d.beam(gearTop, ground, "CHS 168.3x8", kSteel4130, {1, 0, 0});
        const auto& front = d.mesh.nodes[wings[side][4][1]].getLocation();
        const auto engine = d.node(front[0] - 2.2, front[1] - 1.1, front[2]);
        d.beam(wings[side][4][1], engine, "CHS 114.3x5", kSteel4130);
        d.beam(wings[side][4][3], engine, "CHS 114.3x5", kSteel4130);
        d.load(engine, {0, -30 * kKN, 0}); // engine weight
      }
      for (std::size_t corner = 0; corner < 4; ++corner) d.beam(wings[0][0][corner], wings[1][0][corner], "Box 200x100x6", kAl7075); // centre box

      // Horizontal tail (two halves) and fin.
      for (const double sign : {1.0, -1.0}) {
        linkRoot(liftingBox(
          6, [&](const int i) { const double z = 1.0 + 5.0 * i / 6.0; return Eigen::Vector3d(34.0 + 0.5 * (z - 1.0), centre(34.0), sign * z); },
          [](const int i) { return 2.0 - 1.1 * i / 6.0; }, [](const int i) { return 0.3 - 0.18 * i / 6.0; }, P{0, 1, 0},
          [](const int) { return "Box 80x40x3"; }).front());
      }
      linkRoot(liftingBox(
        7, [&](const int i) { const double y = 5.0 + 6.0 * i / 7.0; return Eigen::Vector3d(32.0 + 0.7 * (y - 5.0), y, 0.0); },
        [](const int i) { return 3.0 - 1.8 * i / 7.0; }, [](const int i) { return 0.35 - 0.23 * i / 7.0; }, P{0, 0, 1},
        [](const int) { return "Box 80x40x3"; }).front());

      // Nose gear under frame 4.
      const auto noseTop = rings[3][15];
      const auto& n = d.mesh.nodes[noseTop].getLocation();
      const auto noseGround = d.node(n[0], 0.0, n[2]);
      d.clamp(noseGround);
      d.beam(noseTop, noseGround, "CHS 114.3x5", kSteel4130, {1, 0, 0});
      return make(std::move(d), "large_airliner_airframe", "Narrow-body airliner airframe", "Large Structures",
                  "Generic narrow-body with A320 / 737-class proportions, not a real aircraft's structure: the skin is represented by "
                  "diagonals, and only a 1 g ground case is applied (no pressurisation, no flight loads). Complete 38 m airframe on its landing gear: 37 fuselage frames (Box 80x40x3, heavy wing frames Box 200x100x6, 4 m "
                  "diameter, tapered nose and upswept tail), 20 stringers (Box 60x40x3, 7075-T6), skin panels as diagonals, floor beams. "
                  "Swept tapered wing boxes (4 spar caps, ribs, spar webs and skins as diagonals) with a centre box and fuel (3 kN per "
                  "station and spar), horizontal tail and fin boxes, engines (30 kN) on pylons, nose and main gear legs clamped to the ground. Cabin 6 kN/m. Aluminium 2024-T3 / "
                  "7075-T6, steel gear and pylons, self weight.");
    }

  } // namespace end

  std::vector<LibraryBeam> buildLibrary(const std::span<const anaf::MATERIAL::Material> materials, const std::span<const BeamSection> sections) {
    const Lists lists{materials, sections};
    std::vector<LibraryBeam> library;
    for (auto* build : {portalFrame, twoStoreyFrame, officeFrame, bracedFrame, cantileverCanopy, mezzanine, timberPergola,
                        footbridge, continuousGirder, vierendeel, tiedArch, grillage,
                        pipeRack, gantryCrane, palletRack, signGantry, scaffoldTower, craneRunway,
                        windTurbineTower, telecomMonopole, solarTracker, offshoreJacket,
                        machineFrame, ladderChassis, bicycleFrame, rollCage, cncGantry, robotArm,
                        wingSpar, strutBracedWing, skidGear, enginePylon, satelliteBus, spaceTruss, lunarLander, thrustFrame,
                        quadcopter, fuselageSection, tailBoom, solarArrayBoom,
                        threeHingedFrame, gerberGirder, simpleConnectionFrame, pinnedWebTruss, loaderCrane, bracedLandingGear,
                        stadium, airportTerminal, airliner}) {
      library.push_back(build(lists));
    }
    return library;
  }

  std::expected<std::vector<Entry>, std::string> writeLibrary(const std::filesystem::path& dir,
                                                              const std::span<const anaf::MATERIAL::Material> materials,
                                                              const std::span<const BeamSection> sections) {
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    if (ec) return std::unexpected(std::format("cannot create '{}': {}", anaf::IO::pathToUtf8(dir), ec.message()));

    std::vector<LibraryBeam> library;
    try {
      library = buildLibrary(materials, sections);
    } catch (const std::exception& exception) {
      return std::unexpected(exception.what());
    }
    anaf::IO::WriteOptions options;
    options.format = anaf::IO::FileFormat::Msh;
    options.mshVersion = anaf::IO::MshVersion::V4_1;
    options.encoding = anaf::IO::Encoding::Ascii;

    std::vector<Entry> entries;
    for (const auto& beam : library) {
      const auto model = ADAPTER::toMeshModel(beam.mesh, materials, sections);
      if (const auto written = anaf::IO::writeMesh(modelFile(dir, beam.entry), model, options); !written) {
        return std::unexpected(std::format("{}: {}", beam.entry.id, written.error().message));
      }
      const auto solved = solveStatic(beam.mesh, materials, sections);
      if (!solved) return std::unexpected(std::format("{}: {}", beam.entry.id, solved.error()));
      const auto results = ADAPTER::toMeshModel(*solved->mesh, materials, sections);
      if (const auto written = anaf::IO::writeMesh(solvedFile(dir, beam.entry), results, options); !written) {
        return std::unexpected(std::format("{} (solved): {}", beam.entry.id, written.error().message));
      }
      entries.push_back(beam.entry);
    }
    const auto indexPath = dir / std::filesystem::path(kIndexFile);
    std::ofstream index(indexPath, std::ios::binary | std::ios::trunc);
    index << indexJson(entries);
    if (!index) return std::unexpected(std::format("cannot write '{}'", anaf::IO::pathToUtf8(indexPath)));
    return entries;
  }

  std::string indexJson(const std::span<const Entry> entries) {
    nlohmann::ordered_json models = nlohmann::ordered_json::array();
    for (const auto& entry : entries) {
      models.push_back({{"id", entry.id}, {"name", entry.name}, {"category", entry.category}, {"description", entry.description}});
    }
    nlohmann::ordered_json root;
    root["schemaVersion"] = 1;
    root["generator"] = "anaf_beam_library_tool (do not edit; regenerate instead)";
    root["files"] = "<id>.msh: model; <id>_solved.msh: the same model with its results";
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

} // namespace FEM::BEAM::LIBRARY end
