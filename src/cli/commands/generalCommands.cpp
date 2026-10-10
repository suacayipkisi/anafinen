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

#include "cli/operations/sideCommands.hpp"
#include "cli/parsing/arguments.hpp"

#include <io/core/pathUtf8.hpp>

#include <algorithm>
#include <format>
#include <mutex>
#include <ranges>
#include <string>

namespace anaf::CLI::HANDLERS {

  namespace {

    CommandResult noArguments(const Arguments args) {
      if (!args.empty()) return std::unexpected(std::format("unexpected argument '{}'", args.front()));
      return {};
    }

    CommandResult printed(const bool found, const std::string_view fileName) {
      if (!found) return std::unexpected(std::format("assets/global/{} not found", fileName));
      return {};
    }

    void printUsage(Session& session, const Command& command) {
      for (const auto line : std::views::split(command.usage, '\n')) session.out << "  " << std::string_view(line) << '\n';
    }

  } // namespace end

  CommandResult help(Session& session, const Arguments args) {
    if (args.size() > 1) return std::unexpected("usage: -help [command]");
    if (args.size() == 1) {
      std::string_view name = args.front();
      if (name.starts_with('-')) name.remove_prefix(1);
      const Command* command = findCommand(name);
      if (!command) return std::unexpected(std::format("unknown command '-{}'", name));
      printUsage(session, *command);
      session.out << '\n' << command->summary << '\n';
      return {};
    }
    // The introduction (syntax rules) comes from the asset, the list from the table, so it
    // always matches the commands that exist.
    if (!SIDE_COMMANDS::help(session.out)) return printed(false, "cli-help.txt");
    std::size_t width = 0;
    for (const auto& command : commandTable()) width = std::max(width, command.name.size());
    session.out << "\nCommands (-help <command> shows the forms of one):\n";
    for (const auto& command : commandTable()) {
      const auto end = command.summary.find(". ");
      const auto summary = end == std::string_view::npos ? command.summary : command.summary.substr(0, end + 1); // first sentence
      session.out << std::format("  -{:<{}}  {}\n", command.name, width, summary);
    }
    return {};
  }

  CommandResult operations(Session& session, const Arguments args) {
    if (auto ok = noArguments(args); !ok) return ok;
    return printed(SIDE_COMMANDS::operations(session.out), "operations.txt");
  }

  CommandResult license(Session& session, const Arguments args) {
    if (auto ok = noArguments(args); !ok) return ok;
    return printed(SIDE_COMMANDS::license(session.out), "license.txt");
  }

  CommandResult thirdPartyLicenses(Session& session, const Arguments args) {
    if (auto ok = noArguments(args); !ok) return ok;
    return printed(SIDE_COMMANDS::thirdPartyLicenses(session.out), "thirdPartyLicenses.txt");
  }

  CommandResult version(Session& session, const Arguments args) {
    if (auto ok = noArguments(args); !ok) return ok;
    session.out << "anafinen-cli " << ANAFINEN_VERSION << '\n';
    return {};
  }

  CommandResult exit(Session& session, const Arguments args) {
    if (auto ok = noArguments(args); !ok) return ok;
    session.exitRequested = true;
    return {};
  }

  CommandResult run(Session& session, const Arguments args) {
    if (args.size() != 1) return std::unexpected("usage: -run <script file>");
    return runScript(session, IO::pathFromUtf8(args.front()));
  }

  CommandResult status(Session& session, const Arguments args) {
    if (auto ok = noArguments(args); !ok) return ok;
    auto& bridge = session.bridge;
    const auto kind = modelKind(session);
    bool solved = false;
    if (kind == E_ModelKind::None) {
      session.out << "Model:     none (start one with -new, -generate, -library load or -import)\n";
    } else if (kind == E_ModelKind::Truss) {
      const auto mesh = trussMesh(session);
      const std::size_t nodeCount = mesh ? mesh->trussNodes.size() : 0;
      const auto supported = mesh ? std::ranges::count_if(mesh->trussNodes, [](const auto& n) { return n.isSupported(); }) : 0;
      solved = mesh && mesh->hasResults;
      session.out << std::format("Model:     truss (3 DOFs per node; self weight along -Y)\n"
                                 "Nodes:     {} ({} supported)\nElements:  {}\nLoads:     {} nodal\n",
                                 nodeCount, supported, mesh ? mesh->trussElements.size() : 0,
                                 mesh ? mesh->appliedForces.size() : 0);
    } else {
      const auto mesh = beamMesh(session);
      const auto supported = mesh ? std::ranges::count_if(mesh->nodes, [](const auto& n) { return n.isSupported(); }) : 0;
      solved = mesh && mesh->hasResults;
      session.out << std::format("Model:     beam / frame (6 DOFs per node)\n"
                                 "Nodes:     {} ({} supported)\nElements:  {}\nLoads:     {} nodal, {} distributed\nGravity:   {}\n",
                                 mesh ? mesh->nodes.size() : 0, supported, mesh ? mesh->elements.size() : 0,
                                 mesh ? mesh->nodalLoads.size() : 0, mesh ? mesh->distributedLoads.size() : 0,
                                 mesh ? OUTPUT::vector3(mesh->gravity) : std::string("(0, -9.80665, 0)"));
    }
    if (kind != E_ModelKind::None) {
      if (!solved) session.out << "Results:   none (-solve)\n";
      else if (session.solvedVersion != bridge.dataVersion.load()) session.out << "Results:   loaded from a file (no energy check)\n";
      else session.out << std::format("Results:   solved, energy check {} (|U - W/2| = {:.3g} J)\n",
                                      bridge.isValid ? "passed" : "FAILED", bridge.energyDiff.load());
    }

    std::size_t userMaterials = 0, userSections = 0;
    std::string materialName, sectionName;
    {
      std::lock_guard lock(bridge.dataMutex);
      userMaterials = static_cast<std::size_t>(std::ranges::count_if(bridge.allMaterials, [](const auto& m) { return !m.getIsBuiltin(); }));
      userSections = static_cast<std::size_t>(std::ranges::count_if(bridge.allSections, [](const auto& s) { return !s.getIsBuiltin(); }));
      if (const auto i = bridge.findMaterialIndex(session.defaults.materialID)) materialName = bridge.allMaterials[*i].getMaterialType();
      if (const auto i = bridge.findSectionIndex(session.defaults.sectionID)) sectionName = bridge.allSections[*i].getName();
      session.out << std::format("Materials: {} ({} user)\nSections:  {} ({} user)\n", bridge.allMaterials.size(), userMaterials,
                                 bridge.allSections.size(), userSections);
    }
    session.out << std::format("Defaults:  material '{}', area {:.6g} m^2, section '{}', {}\n", materialName, session.defaults.area,
                               sectionName, OUTPUT::formulationName(session.defaults.formulation));
    return {};
  }

  CommandResult set(Session& session, const Arguments args) {
    if (args.empty()) return status(session, args);
    if (args.size() != 2) return std::unexpected("usage: -set material|area|section|formulation <value>");
    const std::string_view what = args[0];
    const std::string_view value = args[1];
    if (what == "material") {
      const auto index = findMaterial(session, value);
      if (!index) return std::unexpected(index.error());
      std::lock_guard lock(session.bridge.dataMutex);
      session.defaults.materialID = session.bridge.allMaterials[*index].getMaterialID();
      session.out << std::format("default material: {}\n", session.bridge.allMaterials[*index].getMaterialType());
    } else if (what == "area") {
      const auto area = parseDouble(value, "area");
      if (!area) return std::unexpected(area.error());
      if (*area <= 0.0) return std::unexpected("area must be greater than 0");
      session.defaults.area = *area;
      session.out << std::format("default area: {:.6g} m^2\n", *area);
    } else if (what == "section") {
      const auto index = findSection(session, value);
      if (!index) return std::unexpected(index.error());
      std::lock_guard lock(session.bridge.dataMutex);
      session.defaults.sectionID = session.bridge.allSections[*index].getSectionID();
      session.out << std::format("default section: {}\n", session.bridge.allSections[*index].getName());
    } else if (what == "formulation") {
      const auto formulation = OUTPUT::parseFormulation(value);
      if (!formulation) return std::unexpected(formulation.error());
      session.defaults.formulation = *formulation;
      session.out << std::format("default formulation: {}\n", OUTPUT::formulationName(*formulation));
    } else {
      return std::unexpected(std::format("cannot set '{}' (material, area, section or formulation)", what));
    }
    return {};
  }

} // namespace anaf::CLI::HANDLERS end
