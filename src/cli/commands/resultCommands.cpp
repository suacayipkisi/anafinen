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

#include <beam/beamEngine/beamDiagrams.hpp>
#include <beam/beamEngine/beamSolver.hpp>
#include <beam/beamEngine/beamSolver/deformationUnderConstForce.hpp> // elementSectionProperties(), elementLocalLoads()
#include <truss_1D/trussEngine/trussSolver.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <format>
#include <mutex>
#include <string>
#include <vector>

namespace anaf::CLI::HANDLERS {

  namespace {

    // isRunning for the duration of a solve, also when it throws.
    struct RunningFlag {
      std::atomic<bool>& flag;
      explicit RunningFlag(std::atomic<bool>& f) : flag(f) { flag = true; }
      ~RunningFlag() { flag = false; }
      RunningFlag(const RunningFlag&) = delete;
      RunningFlag& operator=(const RunningFlag&) = delete;
    };

    struct Lists {
      std::vector<MATERIAL::Material> materials;
      std::vector<FEM::BEAM::BeamSection> sections;
    };

    Lists copyLists(Session& session) {
      std::lock_guard lock(session.bridge.dataMutex);
      return {session.bridge.allMaterials, session.bridge.allSections};
    }

    // Index of the largest value(i) over [0, count); count > 0.
    template <typename Value>
    std::size_t argMax(const std::size_t count, Value&& value) {
      std::size_t best = 0;
      for (std::size_t i = 1; i < count; ++i) {
        if (value(i) > value(best)) best = i;
      }
      return best;
    }

    void trussSummary(Session& session, const BRIDGE::MeshData& mesh) {
      const auto& nodes = mesh.trussNodes;
      const auto& elements = mesh.trussElements;
      if (!nodes.empty()) {
        const auto n = argMax(nodes.size(), [&](const std::size_t i) { return OUTPUT::norm(nodes[i].getDisplacement()); });
        session.out << std::format("max displacement   {:.6g} m at node {} {}\n", OUTPUT::norm(nodes[n].getDisplacement()), n,
                                   OUTPUT::vector3(nodes[n].getDisplacement()));
      }
      if (!elements.empty()) {
        const auto t = argMax(elements.size(), [&](const std::size_t i) { return static_cast<double>(elements[i].stress); });
        const auto c = argMax(elements.size(), [&](const std::size_t i) { return -static_cast<double>(elements[i].stress); });
        session.out << std::format("max tension        {:.6g} Pa in element {}\n", elements[t].stress, t);
        session.out << std::format("max compression    {:.6g} Pa in element {}\n", elements[c].stress, c);
        const auto exceeded = std::ranges::count_if(elements, [](const auto& e) { return e.isStressExceeded; });
        session.out << std::format("above yield        {} element{}\n", exceeded, exceeded == 1 ? "" : "s");
      }
    }

    void beamSummary(Session& session, const BRIDGE::BeamMeshData& mesh) {
      const auto& nodes = mesh.nodes;
      const auto& elements = mesh.elements;
      if (!nodes.empty()) {
        const auto n = argMax(nodes.size(), [&](const std::size_t i) { return OUTPUT::norm(nodes[i].getDisplacement()); });
        const auto r = argMax(nodes.size(), [&](const std::size_t i) { return OUTPUT::norm(nodes[i].getRotation()); });
        session.out << std::format("max displacement   {:.6g} m at node {} {}\n", OUTPUT::norm(nodes[n].getDisplacement()), n,
                                   OUTPUT::vector3(nodes[n].getDisplacement()));
        session.out << std::format("max rotation       {:.6g} rad at node {}\n", OUTPUT::norm(nodes[r].getRotation()), r);
      }
      if (!elements.empty()) {
        const auto endValue = [&](const std::size_t i, const std::size_t k) {
          return std::max(std::abs(elements[i].sectionForces[k]), std::abs(elements[i].sectionForces[k + 6]));
        };
        const auto endMoment = [&](const std::size_t i) {
          const auto& f = elements[i].sectionForces;
          return std::max(std::hypot(f[4], f[5]), std::hypot(f[10], f[11]));
        };
        const auto a = argMax(elements.size(), [&](const std::size_t i) { return endValue(i, 0); });
        const auto m = argMax(elements.size(), endMoment);
        session.out << std::format("max |N| at an end  {:.6g} N in element {}\n", endValue(a, 0), a);
        session.out << std::format("max |M| at an end  {:.6g} N m in element {} (-diagram shows the values along it)\n", endMoment(m), m);
        const auto v = argMax(elements.size(), [&](const std::size_t i) { return elements[i].stress.available ? elements[i].stress.maxVonMises : -1.0; });
        if (elements[v].stress.available) {
          session.out << std::format("max von Mises      {:.6g} Pa in element {} at x = {:.4g} m\n", elements[v].stress.maxVonMises, v,
                                     elements[v].stress.vonMisesPosition);
        }
        const auto exceeded = std::ranges::count_if(elements, [](const auto& e) { return e.stress.isStressExceeded; });
        session.out << std::format("above yield        {} element{}\n", exceeded, exceeded == 1 ? "" : "s");
      }
    }

    CommandResult noResults() {
      return std::unexpected("the model has no results: run -solve");
    }

  } // namespace end

  CommandResult solve(Session& session, const Arguments args) {
    if (!args.empty()) return std::unexpected("usage: -solve");
    auto& bridge = session.bridge;
    if (bridge.isRunning) return std::unexpected("a solve is already running");
    bridge.joinWorker();
    const auto lists = copyLists(session);
    const auto start = std::chrono::steady_clock::now();
    bool passed = false;
    double diff = 0.0, relative = 0.0;

    if (modelKind(session) == E_ModelKind::Truss) {
      const auto mesh = trussMesh(session);
      if (!mesh) return std::unexpected("the model is empty");
      std::expected<FEM::TRUSS::StaticResult, std::string> solved;
      {
        RunningFlag running(bridge.isRunning);
        solved = FEM::TRUSS::solveStatic(*mesh, lists.materials);
      }
      if (!solved) return std::unexpected(std::format("solve failed: {}", solved.error()));
      passed = solved->energyCheckPassed;
      diff = solved->energyDiff;
      relative = solved->energyRelativeDiff;
      std::lock_guard lock(bridge.dataMutex);
      bridge.activeMesh = std::move(solved->mesh);
    } else if (modelKind(session) == E_ModelKind::Beam) {
      const auto mesh = beamMesh(session);
      if (!mesh) return std::unexpected("the model is empty");
      std::expected<FEM::BEAM::StaticResult, std::string> solved;
      {
        RunningFlag running(bridge.isRunning);
        solved = FEM::BEAM::solveStatic(*mesh, lists.materials, lists.sections);
      }
      if (!solved) return std::unexpected(std::format("solve failed: {}", solved.error()));
      passed = solved->energyCheckPassed;
      diff = solved->energyDiff;
      relative = solved->energyRelativeDiff;
      std::lock_guard lock(bridge.dataMutex);
      bridge.activeBeamMesh = std::move(solved->mesh);
    } else {
      return std::unexpected("no model to solve");
    }
    bridge.isValid = passed;
    bridge.energyDiff = diff;
    session.solvedVersion = bridge.dataVersion.fetch_add(1, std::memory_order_acq_rel) + 1;

    const auto elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    session.out << std::format("solved in {:.1f} ms; energy check {} (|U - W/2| = {:.3g} J, relative {:.3g})\n", elapsed,
                               passed ? "passed" : "FAILED", diff, relative);
    return results(session, {});
  }

  CommandResult results(Session& session, const Arguments args) {
    const std::string_view what = args.empty() ? "summary" : std::string_view(args[0]);
    if (args.size() > 2 || (what != "summary" && what != "nodes" && what != "elements") || (what == "summary" && args.size() > 1)) {
      return std::unexpected("usage: -results [nodes|elements] [<ids>]");
    }
    const std::string_view selection = args.size() == 2 ? std::string_view(args[1]) : "all";
    const auto lists = copyLists(session);

    if (modelKind(session) == E_ModelKind::Truss) {
      const auto mesh = trussMesh(session);
      if (!mesh || !mesh->hasResults) return noResults();
      if (what == "summary") {
        trussSummary(session, *mesh);
        return {};
      }
      if (what == "nodes") {
        const auto ids = parseIdList(selection, mesh->trussNodes.size(), "node");
        if (!ids) return std::unexpected(ids.error());
        session.out << std::format("{:>6}  {:>13}  {:>13}  {:>13}  {:>13}\n", "node", "ux [m]", "uy [m]", "uz [m]", "|u| [m]");
        for (const auto id : *ids) {
          const auto& u = mesh->trussNodes[id].getDisplacement();
          session.out << std::format("{:>6}  {:>13.6g}  {:>13.6g}  {:>13.6g}  {:>13.6g}\n", id, OUTPUT::tidy(u[0]), OUTPUT::tidy(u[1]),
                                     OUTPUT::tidy(u[2]), OUTPUT::norm(u));
        }
        return {};
      }
      const auto ids = parseIdList(selection, mesh->trussElements.size(), "element");
      if (!ids) return std::unexpected(ids.error());
      session.out << std::format("{:>6}  {:>13}  {:>13}  {:>13}  {}\n", "elem", "stress [Pa]", "N [N]", "dL [m]", "");
      for (const auto id : *ids) {
        const auto& e = mesh->trussElements[id];
        const double length = OUTPUT::distance(mesh->trussNodes[e.node1].getLocation(), mesh->trussNodes[e.node2].getLocation());
        const double modulus = e.materialID < lists.materials.size() ? lists.materials[e.materialID].getElasticityModulus() : 0.0;
        const double elongation = modulus > 0.0 ? static_cast<double>(e.stress) / modulus * length : 0.0;
        session.out << std::format("{:>6}  {:>13.6g}  {:>13.6g}  {:>13.6g}  {}\n", id, e.stress, static_cast<double>(e.stress) * e.crossSectionArea,
                                   elongation, e.isWireframe ? "wireframe" : (e.isStressExceeded ? "ABOVE YIELD" : ""));
      }
      return {};
    }

    if (modelKind(session) != E_ModelKind::Beam) return std::unexpected("no model");
    const auto mesh = beamMesh(session);
    if (!mesh || !mesh->hasResults) return noResults();
    if (what == "summary") {
      beamSummary(session, *mesh);
      return {};
    }
    if (what == "nodes") {
      const auto ids = parseIdList(selection, mesh->nodes.size(), "node");
      if (!ids) return std::unexpected(ids.error());
      session.out << std::format("{:>6}  {:>12}  {:>12}  {:>12}  {:>12}  {:>12}  {:>12}\n", "node", "ux [m]", "uy [m]", "uz [m]", "rx [rad]",
                                 "ry [rad]", "rz [rad]");
      for (const auto id : *ids) {
        const auto& u = mesh->nodes[id].getDisplacement();
        const auto& r = mesh->nodes[id].getRotation();
        session.out << std::format("{:>6}  {:>12.5g}  {:>12.5g}  {:>12.5g}  {:>12.5g}  {:>12.5g}  {:>12.5g}\n", id, OUTPUT::tidy(u[0]),
                                   OUTPUT::tidy(u[1]), OUTPUT::tidy(u[2]), OUTPUT::tidy(r[0]), OUTPUT::tidy(r[1]), OUTPUT::tidy(r[2]));
      }
      return {};
    }
    const auto ids = parseIdList(selection, mesh->elements.size(), "element");
    if (!ids) return std::unexpected(ids.error());
    session.out << "section forces, local axes, section sign convention (N > 0 tension)\n";
    session.out << std::format("{:>6} {:>4}  {:>11}  {:>11}  {:>11}  {:>11}  {:>11}  {:>11}  {}\n", "elem", "end", "N [N]", "Vy [N]", "Vz [N]",
                               "T [N m]", "My [N m]", "Mz [N m]", "max von Mises [Pa]");
    for (const auto id : *ids) {
      const auto& e = mesh->elements[id];
      const auto& f = e.sectionForces;
      std::string stress = "-";
      if (e.stress.available) {
        stress = std::format("{:.5g} at x = {:.4g} m{}", e.stress.maxVonMises, e.stress.vonMisesPosition,
                             e.stress.isStressExceeded ? "  ABOVE YIELD" : "");
      }
      const auto t = [&](const std::size_t k) { return OUTPUT::tidy(f[k]); };
      session.out << std::format("{:>6} {:>4}  {:>11.5g}  {:>11.5g}  {:>11.5g}  {:>11.5g}  {:>11.5g}  {:>11.5g}  {}\n", id, 1, t(0), t(1), t(2),
                                 t(3), t(4), t(5), stress);
      session.out << std::format("{:>6} {:>4}  {:>11.5g}  {:>11.5g}  {:>11.5g}  {:>11.5g}  {:>11.5g}  {:>11.5g}\n", "", 2, t(6), t(7), t(8),
                                 t(9), t(10), t(11));
    }
    return {};
  }

  CommandResult diagram(Session& session, const Arguments args) {
    const auto parsed = parseArgs(args, {"points"});
    if (!parsed) return std::unexpected(parsed.error());
    if (parsed->positional.size() != 1) return std::unexpected("usage: -diagram <element> [points=<n>]");
    if (modelKind(session) != E_ModelKind::Beam) return std::unexpected("diagrams need a solved beam model");
    const auto mesh = beamMesh(session);
    if (!mesh || !mesh->hasResults) return noResults();
    const auto element = parseIndex(parsed->positional.front(), "element");
    if (!element) return std::unexpected(element.error());
    if (*element >= mesh->elements.size()) return std::unexpected(std::format("element {} does not exist ({} elements)", *element, mesh->elements.size()));
    std::uint32_t points = 11;
    if (const auto value = parsed->get("points")) {
      const auto count = parseIndex(*value, "points");
      if (!count) return std::unexpected(count.error());
      if (*count < 2 || *count > 10000) return std::unexpected("points must be 2..10000");
      points = *count;
    }

    const auto lists = copyLists(session);
    const auto properties = FEM::BEAM::elementSectionProperties(mesh->elements, lists.sections, lists.materials);
    const auto loads = FEM::BEAM::elementLocalLoads(mesh->nodes, mesh->elements, properties, mesh->distributedLoads, mesh->gravity, lists.materials);
    const auto states = FEM::BEAM::sampleElement(*mesh, *element, points, loads[*element], lists.materials, lists.sections);
    session.out << std::format("element {} ({}-{}), local axes, section sign convention\n", *element, mesh->elements[*element].node1,
                               mesh->elements[*element].node2);
    session.out << std::format("{:>9}  {:>11}  {:>11}  {:>11}  {:>11}  {:>11}  {:>11}  {:>11}\n", "x [m]", "N [N]", "Vy [N]", "Vz [N]", "T [N m]",
                               "My [N m]", "Mz [N m]", "|d| [m]");
    for (const auto& s : states) {
      const auto t = [&](const std::size_t k) { return OUTPUT::tidy(s.forces[k]); };
      session.out << std::format("{:>9.4g}  {:>11.5g}  {:>11.5g}  {:>11.5g}  {:>11.5g}  {:>11.5g}  {:>11.5g}  {:>11.5g}\n", s.position, t(0), t(1),
                                 t(2), t(3), t(4), t(5), OUTPUT::norm(s.displacement));
    }
    return {};
  }

} // namespace anaf::CLI::HANDLERS end
