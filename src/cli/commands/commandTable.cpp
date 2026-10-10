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

#include "commands.hpp"
#include "handlers.hpp"

#include "cli/parsing/arguments.hpp"

#include <io/core/pathUtf8.hpp>

#include <array>
#include <exception>
#include <format>
#include <fstream>

namespace anaf::CLI {

  namespace {

    constexpr int maxScriptDepth = 16;

    // Ids are 0-based everywhere; <nodes> / <elements> take "all", "3", "0,4,7", "2-9" or a mix.
    // Units are SI: m, m^2, m^4, N, N m, N/m, Pa, kg/m^3.
    constexpr std::array commands{
      // General
      Command{"help", "-help\n-help <command>", "Lists the commands, or shows the forms of one command.", HANDLERS::help},
      Command{"operations", "-operations", "Shows what each analysis can do and which commands need which model.", HANDLERS::operations},
      Command{"license", "-license", "Prints the license (GPL-3.0-or-later).", HANDLERS::license},
      Command{"third-party-licenses", "-third-party-licenses", "Prints the licenses of the bundled libraries.", HANDLERS::thirdPartyLicenses},
      Command{"version", "-version", "Prints the version.", HANDLERS::version},
      Command{"exit", "-exit", "Leaves the CLI (also ends a script). -quit does the same.", HANDLERS::exit},
      Command{"quit", "-quit", "Same as -exit.", HANDLERS::exit},
      Command{"run", "-run <script file>", "Runs the commands of a file, one per line ('#' starts a comment); stops at the first error.", HANDLERS::run},
      Command{"status", "-status", "Shows the model, its size, the solve state and the defaults.", HANDLERS::status},
      Command{"set",
              "-set\n-set material <name|#index>\n-set area <m^2>\n-set section <name|#index>\n-set formulation eb|timoshenko",
              "Shows or changes the defaults of -element add (material, truss bar area, beam section, beam formulation).",
              HANDLERS::set},
      // Model
      Command{"new", "-new truss|beam", "Starts an empty truss or beam / frame model (the current model is dropped).", HANDLERS::newModel},
      Command{"generate", "-generate sqpt <nx> <ny> <nz> [edge=<m>] [area=<m^2>] [material=<name|#index>]",
              "Builds the simple quadrangle prism truss: nx * ny * nz cubes (edge 1 m by default), a bar on every edge and face "
              "diagonal. The result is an ordinary truss model: add supports and loads, then -solve.",
              HANDLERS::generate},
      Command{"library", "-library [list] [truss|beam]\n-library show <id>\n-library load <id> [solved]",
              "Lists, describes or loads the built-in models. 'solved' loads a beam model with its stored results.",
              HANDLERS::library},
      Command{"import", "-import <file> [dim=1|2|3] [size=<m>]",
              "Reads a model (MSH, VTK, VTU, PVD; STEP / IGES / BREP are meshed with dimension dim and element size size). "
              "Files with beam elements become a beam model, every other file a truss.",
              HANDLERS::importFile},
      Command{"export", "-export <file> [format=msh|msh22|vtu|pvd|vtk|vtk42|step] [binary] [compress]",
              "Writes the model (and its results). The format follows the extension unless format= is given; "
              "compress (zlib) needs binary and a VTU / PVD file.",
              HANDLERS::exportFile},
      // Materials and sections
      Command{"materials", "-materials [text]", "Lists the materials (those whose name contains text).", HANDLERS::materials},
      Command{"material",
              "-material show <name|#index>\n"
              "-material add <name> E=<Pa> nu=<-> density=<kg/m^3> yield=<Pa> [ultimate=<Pa>] [G=<Pa>] [K=<Pa>] [ductility=<->]\n"
              "-material remove <name|#index>",
              "Shows, adds or removes a material. G and K default to the isotropic values from E and nu, ultimate to yield. "
              "User materials are saved to the user config folder; built-ins cannot be removed.",
              HANDLERS::material},
      Command{"sections", "-sections [text]", "Lists the beam sections (those whose name contains text).", HANDLERS::sections},
      Command{"section",
              "-section show <name|#index>\n"
              "-section add <name> rectangle h=<m> b=<m>\n"
              "-section add <name> circle d=<m>\n"
              "-section add <name> pipe d=<m> t=<m>\n"
              "-section add <name> box h=<m> b=<m> t=<m> [ro=<m>] [ri=<m>]\n"
              "-section add <name> i h=<m> b=<m> tw=<m> tf=<m> [r=<m>]\n"
              "-section add <name> general A=<m^2> Iy=<m^4> Iz=<m^4> J=<m^4> [Asy=<m^2>] [Asz=<m^2>]\n"
              "-section remove <name|#index>",
              "Shows, adds or removes a beam section. h runs along local y (the I web), b along local z. "
              "User sections are saved to the user config folder; catalogue sections cannot be removed.",
              HANDLERS::section},
      // Editing
      Command{"node", "-node add <x> <y> <z> [<x> <y> <z> ...]\n-node move <id> <x> <y> <z>\n-node remove <nodes>",
              "Adds nodes (ids are given in order), moves one, or removes nodes with their elements and loads "
              "(the ids above a removed node move down).",
              HANDLERS::node},
      Command{"nodes", "-nodes [<nodes>]", "Lists nodes with their positions and supports.", HANDLERS::nodes},
      Command{"element",
              "-element add <node1> <node2> [material=<name|#index>] [area=<m^2>]                         (truss)\n"
              "-element add <node1> <node2> [material=] [section=] [formulation=eb|timoshenko] [orient=x,y,z] (beam)\n"
              "-element set <elements> [material=] [area=] [section=] [formulation=] [orient=x,y,z]\n"
              "-element remove <elements>",
              "Adds, changes or removes elements. Missing values come from -set. orient is the beam orientation vector "
              "(in the local x-y plane; 0,0,0 = default: global +Y, or +X for members parallel to Y).",
              HANDLERS::element},
      Command{"elements", "-elements [<elements>]", "Lists elements with their nodes, length, material and section.", HANDLERS::elements},
      Command{"support",
              "-support <nodes> fixed|pinned|free\n"
              "-support <nodes> fix=<dofs>                          dofs: ux,uy,uz,rx,ry,rz, translations, rotations, all\n"
              "-support <nodes> [motion=<v>/<v>...|none] [rotation=<v>/<v>...|none]   v = x,y,z (inclined supports)",
              "Sets the support of nodes. fixed holds every DOF, pinned the translations (a beam node keeps its rotations), "
              "free removes the support. fix= holds the listed global DOFs and frees the others. motion= / rotation= give "
              "the allowed directions / rotation axes directly (any direction; none = held).",
              HANDLERS::support},
      Command{"load",
              "-load node <nodes> <fx> <fy> <fz> [<mx> <my> <mz>]\n"
              "-load element <elements> <wx> <wy> <wz> [local]\n"
              "-load clear [nodes|elements|all]",
              "Adds a nodal force (and moment, beams), or a uniform line load in N/m on beam elements (global axes, or local "
              "with 'local'); clear removes loads. Self weight is separate (-gravity).",
              HANDLERS::load},
      Command{"loads", "-loads", "Lists the loads.", HANDLERS::loads},
      Command{"gravity", "-gravity\n-gravity <gx> <gy> <gz>\n-gravity off",
              "Shows or sets the gravity of the beam self weight (default 0 -9.80665 0). Truss self weight always uses -Y.",
              HANDLERS::gravity},
      Command{"release", "-release <elements> 1|2|both none|hinge|<forces>",
              "Sets the end releases (hinges) of beam elements at node 1, node 2 or both ends. forces: comma separated "
              "N,Vy,Vz,T,My,Mz (the section forces that become zero); hinge = My,Mz.",
              HANDLERS::release},
      Command{"formulation", "-formulation <elements> eb|timoshenko", "Sets Euler-Bernoulli or Timoshenko on beam elements.",
              HANDLERS::formulation},
      // Solve and results
      Command{"solve", "-solve", "Runs the linear static solve of the model and prints a summary.", HANDLERS::solve},
      Command{"results", "-results\n-results nodes [<nodes>]\n-results elements [<elements>]",
              "Shows the results: a summary, node displacements (and rotations), or element stresses and forces.",
              HANDLERS::results},
      Command{"diagram", "-diagram <element> [points=<n>]",
              "Prints N, Vy, Vz, T, My, Mz and the displacement along a solved beam element (n points, default 11).",
              HANDLERS::diagram},
    };

  } // namespace end

  std::span<const Command> commandTable() {
    return commands;
  }

  const Command* findCommand(const std::string_view name) {
    for (const auto& command : commands) {
      if (command.name == name) return &command;
    }
    return nullptr;
  }

  bool executeLine(Session& session, const std::string_view line) {
    const auto tokens = tokenize(line);
    if (!tokens) {
      session.err << "error: " << tokens.error() << '\n';
      return false;
    }
    if (tokens->empty()) return true;
    const std::string_view name = tokens->front();
    if (!name.starts_with('-') || name.size() < 2) {
      session.err << std::format("error: '{}' is not a command; commands start with '-' (see -help)\n", name);
      return false;
    }
    const Command* command = findCommand(name.substr(1));
    if (!command) {
      session.err << std::format("error: unknown command '{}' (see -help)\n", name);
      return false;
    }
    CommandResult result;
    try {
      result = command->run(session, std::span(*tokens).subspan(1));
    } catch (const std::exception& exception) {
      result = std::unexpected(exception.what());
    }
    if (!result) {
      session.err << std::format("error: {}\n", result.error());
      return false;
    }
    return true;
  }

  CommandResult runScript(Session& session, const std::filesystem::path& path) {
    if (session.scriptDepth >= maxScriptDepth) return std::unexpected("scripts nested too deeply (a script that runs itself?)");
    std::ifstream file(path);
    if (!file) return std::unexpected(std::format("cannot open '{}'", IO::pathToUtf8(path)));
    ++session.scriptDepth;
    std::string line;
    int lineNumber = 0;
    CommandResult result;
    while (!session.exitRequested && std::getline(file, line)) {
      ++lineNumber;
      if (!line.empty() && line.back() == '\r') line.pop_back(); // CRLF scripts
      if (const auto tokens = tokenize(line); tokens && tokens->empty()) continue;
      session.out << "> " << line << '\n';
      if (!executeLine(session, line)) {
        result = std::unexpected(std::format("{}:{}: script stopped", IO::pathToUtf8(path), lineNumber));
        break;
      }
    }
    --session.scriptDepth;
    return result;
  }

} // namespace anaf::CLI end
