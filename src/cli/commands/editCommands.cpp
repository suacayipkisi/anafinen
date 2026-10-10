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

#include "handlers.hpp"
#include "output.hpp"

#include "cli/parsing/arguments.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <format>
#include <limits>
#include <mutex>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

namespace anaf::CLI::HANDLERS {

  namespace {

    constexpr std::array<double, 3> defaultGravity{0.0, -9.80665, 0.0};

    std::vector<FEM::TRUSS::Node>& nodeList(BRIDGE::MeshData& mesh) { return mesh.trussNodes; }
    std::vector<FEM::BEAM::Node>& nodeList(BRIDGE::BeamMeshData& mesh) { return mesh.nodes; }
    const std::vector<FEM::TRUSS::Node>& nodeList(const BRIDGE::MeshData& mesh) { return mesh.trussNodes; }
    const std::vector<FEM::BEAM::Node>& nodeList(const BRIDGE::BeamMeshData& mesh) { return mesh.nodes; }
    std::size_t elementCount(const BRIDGE::MeshData& mesh) { return mesh.trussElements.size(); }
    std::size_t elementCount(const BRIDGE::BeamMeshData& mesh) { return mesh.elements.size(); }

    void removeNode(BRIDGE::MeshData& mesh, const std::uint32_t k) { FEM::TRUSS::deleteNode(mesh, k); }
    void removeNode(BRIDGE::BeamMeshData& mesh, const std::uint32_t k) { FEM::BEAM::deleteNode(mesh, k); }

    CommandResult noModel() {
      return std::unexpected("no model: start one with -new truss|beam, -generate, -library load or -import");
    }

    // Runs a generic edit (a lambda taking the truss or the beam MeshData) on the active model.
    template <typename Edit>
    CommandResult editActive(Session& session, Edit&& edit) {
      switch (modelKind(session)) {
        case E_ModelKind::Truss: return editTruss(session, [&](BRIDGE::MeshData& mesh) { return edit(mesh); });
        case E_ModelKind::Beam: return editBeam(session, [&](BRIDGE::BeamMeshData& mesh) { return edit(mesh); });
        default: return noModel();
      }
    }

    // Calls show with the active (const) snapshot; an empty model when there is none yet.
    template <typename Show>
    CommandResult showActive(Session& session, Show&& show) {
      switch (modelKind(session)) {
        case E_ModelKind::Truss: {
          const auto mesh = trussMesh(session);
          return show(mesh ? *mesh : BRIDGE::MeshData{});
        }
        case E_ModelKind::Beam: {
          const auto mesh = beamMesh(session);
          return show(mesh ? *mesh : BRIDGE::BeamMeshData{});
        }
        default: return noModel();
      }
    }

    template <typename Mesh>
    constexpr bool isBeam = std::is_same_v<std::remove_cvref_t<Mesh>, BRIDGE::BeamMeshData>;

    std::expected<std::vector<std::array<double, 3>>, std::string> parsePoints(const Arguments args) {
      if (args.empty() || args.size() % 3 != 0) return std::unexpected("give the coordinates as <x> <y> <z> (groups of three)");
      std::vector<std::array<double, 3>> points(args.size() / 3);
      for (std::size_t i = 0; i < args.size(); ++i) {
        const auto value = parseDouble(args[i], "coordinate");
        if (!value) return std::unexpected(value.error());
        points[i / 3][i % 3] = *value;
      }
      return points;
    }

    // Allowed directions "x,y,z/x,y,z" or "none".
    std::expected<std::vector<std::array<double, 3>>, std::string> parseBasis(const std::string_view text, const std::string_view what) {
      std::vector<std::array<double, 3>> basis;
      if (text == "none") return basis;
      for (const auto part : std::views::split(text, '/')) {
        const auto vector = parseVector3(std::string_view(part.begin(), part.end()), what);
        if (!vector) return std::unexpected(vector.error());
        basis.push_back(*vector);
      }
      if (basis.size() > 3) return std::unexpected(std::format("{}: at most three vectors", what));
      return basis;
    }

    std::vector<std::array<double, 3>> globalAxes(const std::array<bool, 3>& free) {
      std::vector<std::array<double, 3>> axes;
      for (std::size_t axis = 0; axis < 3; ++axis) {
        if (!free[axis]) continue;
        std::array<double, 3> unit{};
        unit[axis] = 1.0;
        axes.push_back(unit);
      }
      return axes;
    }

    // Element nodes: both must exist, differ and not coincide.
    template <typename Mesh>
    CommandResult checkElementNodes(const Mesh& mesh, const std::uint32_t a, const std::uint32_t b) {
      const auto& nodes = nodeList(mesh);
      if (a >= nodes.size() || b >= nodes.size()) {
        return std::unexpected(std::format("node {} does not exist ({} nodes)", std::max(a, b), nodes.size()));
      }
      if (a == b) return std::unexpected("an element needs two different nodes");
      if (OUTPUT::distance(nodes[a].getLocation(), nodes[b].getLocation()) <= 0.0) {
        return std::unexpected(std::format("nodes {} and {} are at the same point", a, b));
      }
      return {};
    }

    // The orientation vector must not be parallel to the element (zero = default rule).
    CommandResult checkOrientation(const BRIDGE::BeamMeshData& mesh, const FEM::BEAM::BeamElement& element) {
      const auto& v = element.orientation;
      if (OUTPUT::norm(v) == 0.0) return {};
      const auto& p1 = mesh.nodes[element.node1].getLocation();
      const auto& p2 = mesh.nodes[element.node2].getLocation();
      const std::array<double, 3> x{p2[0] - p1[0], p2[1] - p1[1], p2[2] - p1[2]};
      const std::array<double, 3> cross{x[1] * v[2] - x[2] * v[1], x[2] * v[0] - x[0] * v[2], x[0] * v[1] - x[1] * v[0]};
      if (OUTPUT::norm(cross) <= 1e-9 * OUTPUT::norm(x) * OUTPUT::norm(v)) {
        return std::unexpected(std::format("orientation {} is parallel to element {}-{}", OUTPUT::vector3(v), element.node1, element.node2));
      }
      return {};
    }

    struct ElementValues {
      std::optional<std::uint32_t> material;
      std::optional<double> area;
      std::optional<std::uint32_t> section;
      std::optional<FEM::BEAM::E_Formulation> formulation;
      std::optional<std::array<double, 3>> orientation;
    };

    // material= / area= (truss) / section= / formulation= / orient= (beam) of -element add / set.
    std::expected<ElementValues, std::string> parseElementValues(Session& session, const ParsedArgs& parsed, const bool beam) {
      ElementValues values;
      for (const auto& [key, value] : parsed.named) {
        if (beam && key == "area") return std::unexpected("area= is a truss value; beams take section=");
        if (!beam && (key == "section" || key == "formulation" || key == "orient")) {
          return std::unexpected(std::format("{}= is a beam value; truss bars take material= and area=", key));
        }
      }
      if (const auto value = parsed.get("material")) {
        const auto index = findMaterial(session, *value);
        if (!index) return std::unexpected(index.error());
        values.material = *index;
      }
      if (const auto value = parsed.get("area")) {
        const auto area = parseDouble(*value, "area");
        if (!area) return std::unexpected(area.error());
        if (*area <= 0.0) return std::unexpected("area must be greater than 0");
        values.area = *area;
      }
      if (const auto value = parsed.get("section")) {
        const auto index = findSection(session, *value);
        if (!index) return std::unexpected(index.error());
        values.section = *index;
      }
      if (const auto value = parsed.get("formulation")) {
        const auto formulation = OUTPUT::parseFormulation(*value);
        if (!formulation) return std::unexpected(formulation.error());
        values.formulation = *formulation;
      }
      if (const auto value = parsed.get("orient")) {
        const auto orientation = parseVector3(*value, "orient");
        if (!orientation) return std::unexpected(orientation.error());
        values.orientation = *orientation;
      }
      return values;
    }

    // Release bits of one element end: none, hinge or a list of N,Vy,Vz,T,My,Mz.
    std::expected<std::uint16_t, std::string> parseReleaseBits(const std::string_view text) {
      if (text == "none") return std::uint16_t{0};
      if (text == "hinge") return FEM::BEAM::RELEASE::hinge;
      constexpr std::array<std::string_view, 6> sectionForceNames{"N", "Vy", "Vz", "T", "My", "Mz"};
      std::uint16_t bits = 0;
      for (const auto part : std::views::split(text, ',')) {
        const std::string_view name(part.begin(), part.end());
        const auto* found = std::ranges::find(sectionForceNames, name);
        if (found == sectionForceNames.end()) return std::unexpected(std::format("unknown release '{}' (none, hinge or N,Vy,Vz,T,My,Mz)", name));
        bits = static_cast<std::uint16_t>(bits | (1U << (found - sectionForceNames.begin())));
      }
      return bits;
    }

  } // namespace end

  CommandResult node(Session& session, const Arguments args) {
    if (args.empty()) return std::unexpected("usage: -node add|move|remove ... (see -help node)");
    const std::string_view action = args[0];
    if (action == "add") {
      const auto points = parsePoints(args.subspan(1));
      if (!points) return std::unexpected(points.error());
      std::size_t first = 0;
      auto result = editActive(session, [&](auto& mesh) -> CommandResult {
        auto& nodes = nodeList(mesh);
        first = nodes.size();
        if (first + points->size() > std::numeric_limits<std::uint32_t>::max()) return std::unexpected("too many nodes");
        for (const auto& p : *points) nodes.emplace_back(static_cast<std::uint32_t>(nodes.size()), p[0], p[1], p[2]);
        return {};
      });
      if (!result) return result;
      if (points->size() == 1) session.out << std::format("node {} at {}\n", first, OUTPUT::vector3(points->front()));
      else session.out << std::format("nodes {}..{} added\n", first, first + points->size() - 1);
      return {};
    }
    if (action == "move") {
      if (args.size() != 5) return std::unexpected("usage: -node move <id> <x> <y> <z>");
      const auto id = parseIndex(args[1], "node");
      if (!id) return std::unexpected(id.error());
      const auto points = parsePoints(args.subspan(2));
      if (!points) return std::unexpected(points.error());
      return editActive(session, [&](auto& mesh) -> CommandResult {
        auto& nodes = nodeList(mesh);
        if (*id >= nodes.size()) return std::unexpected(std::format("node {} does not exist ({} nodes)", *id, nodes.size()));
        nodes[*id].setLocation(points->front());
        session.out << std::format("node {} moved to {}\n", *id, OUTPUT::vector3(points->front()));
        return {};
      });
    }
    if (action == "remove") {
      if (args.size() != 2) return std::unexpected("usage: -node remove <nodes>");
      return editActive(session, [&](auto& mesh) -> CommandResult {
        const auto ids = parseIdList(args[1], nodeList(mesh).size(), "node");
        if (!ids) return std::unexpected(ids.error());
        const std::size_t elementsBefore = elementCount(mesh);
        for (const auto id : std::views::reverse(*ids)) removeNode(mesh, id); // highest first: lower ids stay valid
        session.out << std::format("removed {} node{} and {} element{}; the later ids moved down\n", ids->size(),
                                   ids->size() == 1 ? "" : "s", elementsBefore - elementCount(mesh),
                                   elementsBefore - elementCount(mesh) == 1 ? "" : "s");
        return {};
      });
    }
    return std::unexpected("usage: -node add|move|remove ... (see -help node)");
  }

  CommandResult nodes(Session& session, const Arguments args) {
    if (args.size() > 1) return std::unexpected("usage: -nodes [<nodes>]");
    return showActive(session, [&](const auto& mesh) -> CommandResult {
      const auto& list = nodeList(mesh);
      if (list.empty() && args.empty()) {
        session.out << "no nodes (add them with -node add <x> <y> <z>)\n";
        return {};
      }
      const auto ids = parseIdList(args.empty() ? "all" : std::string_view(args[0]), list.size(), "node");
      if (!ids) return std::unexpected(ids.error());
      session.out << std::format("{:>6}  {:>12}  {:>12}  {:>12}  {}\n", "id", "x", "y", "z", "support");
      for (const auto id : *ids) {
        const auto& p = list[id].getLocation();
        session.out << std::format("{:>6}  {:>12.6g}  {:>12.6g}  {:>12.6g}  {}\n", id, p[0], p[1], p[2], OUTPUT::supportLabel(list[id]));
      }
      return {};
    });
  }

  CommandResult element(Session& session, const Arguments args) {
    if (args.empty()) return std::unexpected("usage: -element add|set|remove ... (see -help element)");
    const std::string_view action = args[0];
    const bool beam = modelKind(session) == E_ModelKind::Beam;
    if (modelKind(session) == E_ModelKind::None) return noModel();

    if (action == "add" || action == "set") {
      const auto parsed = parseArgs(args.subspan(1), {"material", "area", "section", "formulation", "orient"});
      if (!parsed) return std::unexpected(parsed.error());
      const auto values = parseElementValues(session, *parsed, beam);
      if (!values) return std::unexpected(values.error());

      if (action == "add") {
        if (parsed->positional.size() != 2) return std::unexpected("usage: -element add <node1> <node2> [options]");
        const auto a = parseIndex(parsed->positional[0], "node");
        if (!a) return std::unexpected(a.error());
        const auto b = parseIndex(parsed->positional[1], "node");
        if (!b) return std::unexpected(b.error());
        const std::uint32_t materialIndex = values->material.value_or(defaultMaterialIndex(session));
        const std::uint32_t sectionIndex = values->section.value_or(defaultSectionIndex(session));
        std::size_t id = 0;
        auto result = editActive(session, [&](auto& mesh) -> CommandResult {
          if (auto ok = checkElementNodes(mesh, *a, *b); !ok) return ok;
          if constexpr (isBeam<decltype(mesh)>) {
            FEM::BEAM::BeamElement created{.node1 = *a, .node2 = *b, .materialID = materialIndex, .sectionID = sectionIndex,
                                           .formulation = values->formulation.value_or(session.defaults.formulation),
                                           .orientation = values->orientation.value_or(std::array<double, 3>{})};
            if (auto ok = checkOrientation(mesh, created); !ok) return ok;
            id = mesh.elements.size();
            mesh.elements.push_back(created);
          } else {
            id = mesh.trussElements.size();
            mesh.trussElements.push_back({.node1 = *a, .node2 = *b, .materialID = materialIndex,
                                          .crossSectionArea = values->area.value_or(session.defaults.area)});
          }
          return {};
        });
        if (!result) return result;
        session.out << std::format("element {} ({}-{})\n", id, *a, *b);
        return {};
      }

      // set
      if (parsed->positional.size() != 1 || parsed->named.empty()) return std::unexpected("usage: -element set <elements> <key>=<value> ...");
      return editActive(session, [&](auto& mesh) -> CommandResult {
        const auto ids = parseIdList(parsed->positional[0], elementCount(mesh), "element");
        if (!ids) return std::unexpected(ids.error());
        for (const auto id : *ids) {
          if constexpr (isBeam<decltype(mesh)>) {
            auto& e = mesh.elements[id];
            if (values->material) e.materialID = *values->material;
            if (values->section) e.sectionID = *values->section;
            if (values->formulation) e.formulation = *values->formulation;
            if (values->orientation) e.orientation = *values->orientation;
            if (auto ok = checkOrientation(mesh, e); !ok) return ok;
          } else {
            auto& e = mesh.trussElements[id];
            if (values->material) e.materialID = *values->material;
            if (values->area) e.crossSectionArea = *values->area;
          }
        }
        session.out << std::format("changed {} element{}\n", ids->size(), ids->size() == 1 ? "" : "s");
        return {};
      });
    }
    if (action == "remove") {
      if (args.size() != 2) return std::unexpected("usage: -element remove <elements>");
      return editActive(session, [&](auto& mesh) -> CommandResult {
        const auto ids = parseIdList(args[1], elementCount(mesh), "element");
        if (!ids) return std::unexpected(ids.error());
        std::vector<bool> drop(elementCount(mesh), false);
        for (const auto id : *ids) drop[id] = true;
        std::size_t index = 0;
        if constexpr (isBeam<decltype(mesh)>) {
          FEM::BEAM::removeElements(mesh, [&](const FEM::BEAM::BeamElement&) { return drop[index++]; });
        } else {
          std::erase_if(mesh.trussElements, [&](const BRIDGE::RenderElement&) { return drop[index++]; });
        }
        session.out << std::format("removed {} element{}; the later ids moved down\n", ids->size(), ids->size() == 1 ? "" : "s");
        return {};
      });
    }
    return std::unexpected("usage: -element add|set|remove ... (see -help element)");
  }

  CommandResult elements(Session& session, const Arguments args) {
    if (args.size() > 1) return std::unexpected("usage: -elements [<elements>]");
    std::vector<MATERIAL::Material> materialList;
    std::vector<FEM::BEAM::BeamSection> sectionList;
    {
      std::lock_guard lock(session.bridge.dataMutex);
      materialList = session.bridge.allMaterials;
      sectionList = session.bridge.allSections;
    }
    const auto materialName = [&](const std::uint32_t i) {
      return i < materialList.size() ? std::string(materialList[i].getMaterialType()) : std::format("#{}?", i);
    };
    std::size_t mw = 8, sw = 7; // column widths: the longest material / section name
    for (const auto& m : materialList) mw = std::max(mw, m.getMaterialType().size());
    for (const auto& s : sectionList) sw = std::max(sw, s.getName().size());
    return showActive(session, [&](const auto& mesh) -> CommandResult {
      if (elementCount(mesh) == 0 && args.empty()) {
        session.out << "no elements (add them with -element add <node1> <node2>)\n";
        return {};
      }
      const auto ids = parseIdList(args.empty() ? "all" : std::string_view(args[0]), elementCount(mesh), "element");
      if (!ids) return std::unexpected(ids.error());
      const auto& nodes = nodeList(mesh);
      if constexpr (isBeam<decltype(mesh)>) {
        session.out << std::format("{:>6}  {:>6}  {:>6}  {:>10}  {:<{}}  {:<{}}  {:<4}  {}\n", "id", "node1", "node2", "L [m]", "material", mw,
                                   "section", sw, "form", "orientation / releases");
        for (const auto id : *ids) {
          const auto& e = mesh.elements[id];
          const auto section = e.sectionID < sectionList.size() ? sectionList[e.sectionID].getName() : std::format("#{}?", e.sectionID);
          std::string extra = OUTPUT::norm(e.orientation) == 0.0 ? std::string("default") : OUTPUT::vector3(e.orientation);
          if (e.endReleases != 0) extra += "; " + OUTPUT::releaseLabel(e.endReleases);
          session.out << std::format("{:>6}  {:>6}  {:>6}  {:>10.6g}  {:<{}}  {:<{}}  {:<4}  {}\n", id, e.node1, e.node2,
                                     OUTPUT::distance(nodes[e.node1].getLocation(), nodes[e.node2].getLocation()), materialName(e.materialID), mw,
                                     section, sw, e.formulation == FEM::BEAM::E_Formulation::Timoshenko ? "TI" : "EB", extra);
        }
      } else {
        session.out << std::format("{:>6}  {:>6}  {:>6}  {:>10}  {:<{}}  {:>10}\n", "id", "node1", "node2", "L [m]", "material", mw, "A [m^2]");
        for (const auto id : *ids) {
          const auto& e = mesh.trussElements[id];
          session.out << std::format("{:>6}  {:>6}  {:>6}  {:>10.6g}  {:<{}}  {:>10.6g}{}\n", id, e.node1, e.node2,
                                     OUTPUT::distance(nodes[e.node1].getLocation(), nodes[e.node2].getLocation()), materialName(e.materialID), mw,
                                     e.crossSectionArea, e.isWireframe ? "  (wireframe, not solved)" : "");
        }
      }
      return {};
    });
  }

  CommandResult support(Session& session, const Arguments args) {
    const auto parsed = parseArgs(args, {"fix", "motion", "rotation"});
    if (!parsed) return std::unexpected(parsed.error());
    const auto& positional = parsed->positional;
    if (positional.empty() || positional.size() > 2 || (positional.size() == 1 && parsed->named.empty())) {
      return std::unexpected("usage: -support <nodes> fixed|pinned|free | fix=<dofs> | motion=... rotation=... (see -help support)");
    }
    const bool beam = modelKind(session) == E_ModelKind::Beam;
    if (!beam && parsed->get("rotation")) return std::unexpected("truss nodes have no rotations");

    // nullopt = keep the node's current basis.
    std::optional<std::vector<std::array<double, 3>>> motion;
    std::optional<std::vector<std::array<double, 3>>> rotation;
    if (positional.size() == 2) {
      const std::string_view kind = positional[1];
      if (parsed->get("fix")) return std::unexpected("give either a support kind or fix=, not both");
      if (kind == "fixed") {
        motion.emplace();
        rotation.emplace();
      } else if (kind == "pinned") {
        motion.emplace();
        rotation = globalAxes({true, true, true});
      } else if (kind == "free") {
        motion = globalAxes({true, true, true});
        rotation = globalAxes({true, true, true});
      } else {
        return std::unexpected(std::format("unknown support '{}' (fixed, pinned, free)", kind));
      }
    }
    if (const auto dofs = parsed->get("fix")) {
      std::array<bool, 3> freeMotion{true, true, true};
      std::array<bool, 3> freeRotation{true, true, true};
      for (const auto part : std::views::split(*dofs, ',')) {
        const std::string_view dof(part.begin(), part.end());
        if (dof == "all" || dof == "translations") freeMotion = {false, false, false};
        if (dof == "all" || dof == "rotations") freeRotation = {false, false, false};
        if (dof == "all" || dof == "translations" || dof == "rotations") continue;
        if (dof.size() != 2 || (dof[0] != 'u' && dof[0] != 'r') || dof[1] < 'x' || dof[1] > 'z') {
          return std::unexpected(std::format("unknown DOF '{}' (ux, uy, uz, rx, ry, rz, translations, rotations, all)", dof));
        }
        if (dof[0] == 'r' && !beam) return std::unexpected("truss nodes have no rotations");
        (dof[0] == 'u' ? freeMotion : freeRotation)[static_cast<std::size_t>(dof[1] - 'x')] = false;
      }
      motion = globalAxes(freeMotion);
      rotation = globalAxes(freeRotation);
    }
    if (const auto text = parsed->get("motion")) {
      const auto basis = parseBasis(*text, "motion");
      if (!basis) return std::unexpected(basis.error());
      motion = *basis;
    }
    if (const auto text = parsed->get("rotation")) {
      const auto basis = parseBasis(*text, "rotation");
      if (!basis) return std::unexpected(basis.error());
      rotation = *basis;
    }

    return editActive(session, [&](auto& mesh) -> CommandResult {
      auto& list = nodeList(mesh);
      const auto ids = parseIdList(positional[0], list.size(), "node");
      if (!ids) return std::unexpected(ids.error());
      try {
        for (const auto id : *ids) {
          if (motion) list[id].setAllowedMotionDirections(*motion);
          if constexpr (isBeam<decltype(mesh)>) {
            if (rotation) list[id].setAllowedRotationAxes(*rotation);
          }
        }
      } catch (const std::invalid_argument&) {
        return std::unexpected("the direction vectors must be non-zero and linearly independent");
      }
      const auto& first = list[ids->front()];
      session.out << std::format("{} node{}: {}\n", ids->size(), ids->size() == 1 ? "" : "s", OUTPUT::supportLabel(first));
      return {};
    });
  }

  CommandResult load(Session& session, const Arguments args) {
    if (args.empty()) return std::unexpected("usage: -load node|element|clear ... (see -help load)");
    const std::string_view action = args[0];
    if (action == "clear") {
      const std::string_view what = args.size() > 1 ? std::string_view(args[1]) : "all";
      if (args.size() > 2 || (what != "all" && what != "nodes" && what != "elements")) return std::unexpected("usage: -load clear [nodes|elements|all]");
      return editActive(session, [&](auto& mesh) -> CommandResult {
        if constexpr (isBeam<decltype(mesh)>) {
          if (what != "elements") mesh.nodalLoads.clear();
          if (what != "nodes") mesh.distributedLoads.clear();
        } else {
          if (what == "elements") return std::unexpected("truss models have no element loads");
          mesh.appliedForces.clear();
        }
        session.out << "loads cleared\n";
        return {};
      });
    }
    if (action == "node") {
      if (args.size() != 5 && args.size() != 8) return std::unexpected("usage: -load node <nodes> <fx> <fy> <fz> [<mx> <my> <mz>]");
      std::array<double, 6> values{};
      for (std::size_t i = 2; i < args.size(); ++i) {
        const auto value = parseDouble(args[i], i < 5 ? "force" : "moment");
        if (!value) return std::unexpected(value.error());
        values[i - 2] = *value;
      }
      const std::array<double, 3> force{values[0], values[1], values[2]};
      const std::array<double, 3> moment{values[3], values[4], values[5]};
      return editActive(session, [&](auto& mesh) -> CommandResult {
        const auto ids = parseIdList(args[1], nodeList(mesh).size(), "node");
        if (!ids) return std::unexpected(ids.error());
        for (const auto id : *ids) {
          if constexpr (isBeam<decltype(mesh)>) {
            mesh.nodalLoads.push_back({.node = id, .force = force, .moment = moment});
          } else {
            if (args.size() == 8) return std::unexpected("truss nodes take no moments");
            mesh.appliedForces.emplace_back(id, force);
          }
        }
        session.out << std::format("load on {} node{}\n", ids->size(), ids->size() == 1 ? "" : "s");
        return {};
      });
    }
    if (action == "element") {
      if (args.size() != 5 && !(args.size() == 6 && (args[5] == "local" || args[5] == "global"))) {
        return std::unexpected("usage: -load element <elements> <wx> <wy> <wz> [local]");
      }
      if (modelKind(session) != E_ModelKind::Beam) return std::unexpected("line loads need a beam model (truss bars take nodal loads)");
      std::array<double, 3> value{};
      for (std::size_t i = 0; i < 3; ++i) {
        const auto number = parseDouble(args[i + 2], "line load");
        if (!number) return std::unexpected(number.error());
        value[i] = *number;
      }
      const auto frame = args.size() == 6 && args[5] == "local" ? FEM::BEAM::E_LoadFrame::Local : FEM::BEAM::E_LoadFrame::Global;
      return editBeam(session, [&](BRIDGE::BeamMeshData& mesh) -> CommandResult {
        const auto ids = parseIdList(args[1], mesh.elements.size(), "element");
        if (!ids) return std::unexpected(ids.error());
        for (const auto id : *ids) mesh.distributedLoads.push_back({.element = id, .value = value, .frame = frame});
        session.out << std::format("line load on {} element{}\n", ids->size(), ids->size() == 1 ? "" : "s");
        return {};
      });
    }
    return std::unexpected("usage: -load node|element|clear ... (see -help load)");
  }

  CommandResult loads(Session& session, const Arguments args) {
    if (!args.empty()) return std::unexpected("usage: -loads");
    return showActive(session, [&](const auto& mesh) -> CommandResult {
      std::size_t count = 0;
      if constexpr (isBeam<decltype(mesh)>) {
        for (const auto& l : mesh.nodalLoads) {
          session.out << std::format("node {:>6}  F {} N  M {} N m\n", l.node, OUTPUT::vector3(l.force), OUTPUT::vector3(l.moment));
        }
        for (const auto& l : mesh.distributedLoads) {
          session.out << std::format("element {:>3}  w {} N/m ({})\n", l.element, OUTPUT::vector3(l.value),
                                     l.frame == FEM::BEAM::E_LoadFrame::Local ? "local" : "global");
        }
        count = mesh.nodalLoads.size() + mesh.distributedLoads.size();
        session.out << std::format("self weight: gravity {} m/s^2\n", OUTPUT::vector3(mesh.gravity));
      } else {
        for (const auto& f : mesh.appliedForces) session.out << std::format("node {:>6}  F {} N\n", f.getAppliedNode(), OUTPUT::vector3(f.getForce()));
        count = mesh.appliedForces.size();
        session.out << "self weight: always on, gravity (0, -9.80665, 0) m/s^2\n";
      }
      if (count == 0) session.out << "no loads (add them with -load)\n";
      return {};
    });
  }

  CommandResult gravity(Session& session, const Arguments args) {
    if (modelKind(session) == E_ModelKind::Truss) return std::unexpected("truss self weight always acts along -Y (9.80665 m/s^2)");
    if (modelKind(session) != E_ModelKind::Beam) return noModel();
    if (args.empty()) {
      const auto mesh = beamMesh(session);
      session.out << std::format("gravity {} m/s^2\n", OUTPUT::vector3(mesh ? mesh->gravity : defaultGravity));
      return {};
    }
    std::array<double, 3> value{};
    if (args.size() == 1 && args[0] == "off") {
      value = {};
    } else if (args.size() == 3) {
      for (std::size_t i = 0; i < 3; ++i) {
        const auto number = parseDouble(args[i], "gravity");
        if (!number) return std::unexpected(number.error());
        value[i] = *number;
      }
    } else {
      return std::unexpected("usage: -gravity [<gx> <gy> <gz> | off]");
    }
    return editBeam(session, [&](BRIDGE::BeamMeshData& mesh) -> CommandResult {
      mesh.gravity = value;
      session.out << std::format("gravity {} m/s^2{}\n", OUTPUT::vector3(value), OUTPUT::norm(value) == 0.0 ? " (no self weight)" : "");
      return {};
    });
  }

  CommandResult release(Session& session, const Arguments args) {
    if (args.size() != 3 || (args[1] != "1" && args[1] != "2" && args[1] != "both")) {
      return std::unexpected("usage: -release <elements> 1|2|both none|hinge|<N,Vy,Vz,T,My,Mz>");
    }
    const auto bits = parseReleaseBits(args[2]);
    if (!bits) return std::unexpected(bits.error());
    const bool atNode1 = args[1] != "2";
    const bool atNode2 = args[1] != "1";
    return editBeam(session, [&](BRIDGE::BeamMeshData& mesh) -> CommandResult {
      const auto ids = parseIdList(args[0], mesh.elements.size(), "element");
      if (!ids) return std::unexpected(ids.error());
      for (const auto id : *ids) {
        auto& releases = mesh.elements[id].endReleases;
        if (atNode1) releases = static_cast<std::uint16_t>((releases & ~FEM::BEAM::RELEASE::endMask) | *bits);
        if (atNode2) {
          releases = static_cast<std::uint16_t>((releases & ~FEM::BEAM::RELEASE::atNode2(FEM::BEAM::RELEASE::endMask)) |
                                                FEM::BEAM::RELEASE::atNode2(*bits));
        }
      }
      const auto label = OUTPUT::releaseLabel(mesh.elements[ids->front()].endReleases);
      session.out << std::format("{} element{}: {}\n", ids->size(), ids->size() == 1 ? "" : "s", label.empty() ? "rigid ends" : label);
      return {};
    });
  }

  CommandResult formulation(Session& session, const Arguments args) {
    if (args.size() != 2) return std::unexpected("usage: -formulation <elements> eb|timoshenko");
    const auto chosen = OUTPUT::parseFormulation(args[1]);
    if (!chosen) return std::unexpected(chosen.error());
    return editBeam(session, [&](BRIDGE::BeamMeshData& mesh) -> CommandResult {
      const auto ids = parseIdList(args[0], mesh.elements.size(), "element");
      if (!ids) return std::unexpected(ids.error());
      for (const auto id : *ids) mesh.elements[id].formulation = *chosen;
      session.out << std::format("{} on {} element{}\n", OUTPUT::formulationName(*chosen), ids->size(), ids->size() == 1 ? "" : "s");
      return {};
    });
  }

} // namespace anaf::CLI::HANDLERS end
