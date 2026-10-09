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

#include "cli/parsing/arguments.hpp"

#include <beam/beamSection/beamSection.hpp>
#include <material/properties.hpp>

#include <algorithm>
#include <cctype>
#include <format>
#include <initializer_list>
#include <mutex>
#include <optional>
#include <string>
#include <type_traits>
#include <variant>
#include <vector>

namespace anaf::CLI::HANDLERS {

  namespace {

    bool containsIgnoringCase(const std::string_view text, const std::string_view part) {
      const auto lower = [](const char c) { return std::tolower(static_cast<unsigned char>(c)); };
      return std::ranges::search(text, part, {}, lower, lower).begin() != text.end() || part.empty();
    }

    // Reads key=value as a number; missing keys give fallback (or an error when required).
    std::expected<double, std::string> numberOption(const ParsedArgs& parsed, const std::string_view key,
                                                    const std::optional<double> fallback = std::nullopt) {
      const auto value = parsed.get(key);
      if (!value) {
        if (fallback) return *fallback;
        return std::unexpected(std::format("{}= is required", key));
      }
      return parseDouble(*value, key);
    }

    void printMaterial(Session& session, const MATERIAL::Material& material, const std::uint32_t index) {
      const auto& p = material.getProperties();
      session.out << std::format("#{} {} ({}, id {})\n", index, p.name, material.getIsBuiltin() ? "built-in" : "user", material.getMaterialID());
      session.out << std::format("  E           {:.6g} Pa\n  G           {:.6g} Pa\n  K           {:.6g} Pa\n"
                                 "  nu          {:.6g}\n  density     {:.6g} kg/m^3\n  yield       {:.6g} Pa\n"
                                 "  ultimate    {:.6g} Pa\n  ductility   {:.6g}\n",
                                 p.elasticityModulus, p.shearModulus, p.bulkModulus, p.poissonsRatio, p.density,
                                 p.yieldTensileStrength, p.ultimateTensileStrength, p.ductility);
    }

    // Dimensions of a shape as "h=0.3 b=0.15 ..." (the keys of -section add).
    std::string shapeDimensions(const FEM::BEAM::SectionShape& shape) {
      return std::visit([](const auto& s) -> std::string {
        using Shape = std::decay_t<decltype(s)>;
        if constexpr (std::is_same_v<Shape, FEM::BEAM::GeneralSection>) {
          return std::format("A={:.6g} Iy={:.6g} Iz={:.6g} J={:.6g} Asy={:.6g} Asz={:.6g}", s.values.area, s.values.secondMomentY,
                             s.values.secondMomentZ, s.values.torsionConstant, s.values.shearAreaY, s.values.shearAreaZ);
        } else if constexpr (std::is_same_v<Shape, FEM::BEAM::RectangleSection>) {
          return std::format("h={:.6g} b={:.6g}", s.height, s.width);
        } else if constexpr (std::is_same_v<Shape, FEM::BEAM::CircleSection>) {
          return std::format("d={:.6g}", s.diameter);
        } else if constexpr (std::is_same_v<Shape, FEM::BEAM::PipeSection>) {
          return std::format("d={:.6g} t={:.6g}", s.outerDiameter, s.wallThickness);
        } else if constexpr (std::is_same_v<Shape, FEM::BEAM::BoxSection>) {
          return std::format("h={:.6g} b={:.6g} t={:.6g} ro={:.6g} ri={:.6g}", s.height, s.width, s.wallThickness, s.outerCornerRadius,
                             s.innerCornerRadius);
        } else {
          return std::format("h={:.6g} b={:.6g} tw={:.6g} tf={:.6g} r={:.6g}", s.height, s.flangeWidth, s.webThickness, s.flangeThickness,
                             s.rootRadius);
        }
      }, shape);
    }

    // Values of the required keys, then of the optional ones (0 when missing). Fails on a
    // missing required key and on a key that belongs to another shape.
    std::expected<std::vector<double>, std::string> shapeValues(const ParsedArgs& parsed, const std::string_view kind,
                                                                const std::initializer_list<std::string_view> required,
                                                                const std::initializer_list<std::string_view> optional = {}) {
      for (const auto& [key, value] : parsed.named) {
        if (std::ranges::find(required, std::string_view(key)) == required.end() &&
            std::ranges::find(optional, std::string_view(key)) == optional.end()) {
          return std::unexpected(std::format("{}= does not belong to a {} section", key, kind));
        }
      }
      std::vector<double> values;
      for (const auto key : required) {
        const auto value = numberOption(parsed, key);
        if (!value) return std::unexpected(value.error());
        values.push_back(*value);
      }
      for (const auto key : optional) {
        const auto value = numberOption(parsed, key, 0.0);
        if (!value) return std::unexpected(value.error());
        values.push_back(*value);
      }
      return values;
    }

    std::expected<FEM::BEAM::SectionShape, std::string> parseShape(const std::string_view kind, const ParsedArgs& parsed) {
      if (kind == "rectangle") {
        return shapeValues(parsed, kind, {"h", "b"}).transform([](const std::vector<double>& v) -> FEM::BEAM::SectionShape {
          return FEM::BEAM::RectangleSection{v[0], v[1]};
        });
      }
      if (kind == "circle") {
        return shapeValues(parsed, kind, {"d"}).transform([](const std::vector<double>& v) -> FEM::BEAM::SectionShape {
          return FEM::BEAM::CircleSection{v[0]};
        });
      }
      if (kind == "pipe") {
        return shapeValues(parsed, kind, {"d", "t"}).transform([](const std::vector<double>& v) -> FEM::BEAM::SectionShape {
          return FEM::BEAM::PipeSection{v[0], v[1]};
        });
      }
      if (kind == "box") {
        return shapeValues(parsed, kind, {"h", "b", "t"}, {"ro", "ri"}).transform([](const std::vector<double>& v) -> FEM::BEAM::SectionShape {
          return FEM::BEAM::BoxSection{v[0], v[1], v[2], v[3], v[4]};
        });
      }
      if (kind == "i") {
        return shapeValues(parsed, kind, {"h", "b", "tw", "tf"}, {"r"}).transform([](const std::vector<double>& v) -> FEM::BEAM::SectionShape {
          return FEM::BEAM::ISection{v[0], v[1], v[2], v[3], v[4]};
        });
      }
      if (kind == "general") {
        return shapeValues(parsed, kind, {"A", "Iy", "Iz", "J"}, {"Asy", "Asz"}).transform([](const std::vector<double>& v) -> FEM::BEAM::SectionShape {
          return FEM::BEAM::GeneralSection{{v[0], v[1], v[2], v[3], v[4], v[5]}};
        });
      }
      return std::unexpected(std::format("unknown shape '{}' (rectangle, circle, pipe, box, i, general)", kind));
    }

  } // namespace end

  CommandResult materials(Session& session, const Arguments args) {
    if (args.size() > 1) return std::unexpected("usage: -materials [text]");
    const std::string_view filter = args.empty() ? "" : std::string_view(args[0]);
    std::lock_guard lock(session.bridge.dataMutex);
    const auto& list = session.bridge.allMaterials;
    std::size_t width = 4;
    for (const auto& material : list) width = std::max(width, material.getMaterialType().size());
    session.out << std::format("{:>4}  {:<{}}  {:>9}  {:>6}  {:>9}  {:>10}  {}\n", "#", "name", width, "E [GPa]", "nu", "rho",
                               "fy [MPa]", "kind");
    std::size_t shown = 0;
    for (std::uint32_t i = 0; i < list.size(); ++i) {
      const auto& m = list[i];
      if (!containsIgnoringCase(m.getMaterialType(), filter)) continue;
      ++shown;
      session.out << std::format("{:>4}  {:<{}}  {:>9.4g}  {:>6.3g}  {:>9.5g}  {:>10.5g}  {}\n", i, m.getMaterialType(), width,
                                 m.getElasticityModulus() / 1e9, m.getPoisson(), m.getDensity(), m.getYieldTensile() / 1e6,
                                 m.getIsBuiltin() ? "built-in" : "user");
    }
    if (shown == 0) session.out << "no material matches\n";
    return {};
  }

  CommandResult material(Session& session, const Arguments args) {
    if (args.empty()) return std::unexpected("usage: -material show|add|remove ... (see -help material)");
    const std::string_view action = args[0];
    if (action == "show" || action == "remove") {
      if (args.size() != 2) return std::unexpected(std::format("usage: -material {} <name|#index>", action));
      const auto index = findMaterial(session, args[1]);
      if (!index) return std::unexpected(index.error());
      std::uint32_t materialID = 0;
      {
        std::lock_guard lock(session.bridge.dataMutex);
        if (action == "show") {
          printMaterial(session, session.bridge.allMaterials[*index], *index);
          return {};
        }
        materialID = session.bridge.allMaterials[*index].getMaterialID();
      }
      if (const auto removed = session.bridge.removeUserMaterial(materialID); !removed) return std::unexpected(removed.error());
      session.out << std::format("removed material '{}'\n", args[1]);
      return {};
    }
    if (action != "add") return std::unexpected("usage: -material show|add|remove ... (see -help material)");

    const auto parsed = parseArgs(args.subspan(1), {"E", "nu", "density", "yield", "ultimate", "G", "K", "ductility"});
    if (!parsed) return std::unexpected(parsed.error());
    if (parsed->positional.size() != 1) return std::unexpected("usage: -material add <name> E=<Pa> nu=<-> density=<kg/m^3> yield=<Pa> ...");
    const auto e = numberOption(*parsed, "E");
    if (!e) return std::unexpected(e.error());
    const auto nu = numberOption(*parsed, "nu");
    if (!nu) return std::unexpected(nu.error());
    const auto density = numberOption(*parsed, "density");
    if (!density) return std::unexpected(density.error());
    const auto yield = numberOption(*parsed, "yield");
    if (!yield) return std::unexpected(yield.error());
    // Isotropic relations unless given (the wood entries of the library are orthotropic).
    const auto ultimate = numberOption(*parsed, "ultimate", *yield);
    const auto g = numberOption(*parsed, "G", *e / (2.0 * (1.0 + *nu)));
    const auto k = numberOption(*parsed, "K", *nu < 0.5 ? *e / (3.0 * (1.0 - 2.0 * *nu)) : 0.0);
    const auto ductility = numberOption(*parsed, "ductility", 0.0);
    for (const auto* value : {&ultimate, &g, &k, &ductility}) {
      if (!*value) return std::unexpected(value->error());
    }
    const MATERIAL::Material created(MATERIAL::MaterialProperties{
      .name = parsed->positional.front(),
      .elasticityModulus = *e,
      .shearModulus = *g,
      .bulkModulus = *k,
      .yieldTensileStrength = *yield,
      .ultimateTensileStrength = *ultimate,
      .density = *density,
      .poissonsRatio = *nu,
      .ductility = *ductility,
    });
    const auto id = session.bridge.addUserMaterial(created);
    if (!id) return std::unexpected(id.error());
    std::lock_guard lock(session.bridge.dataMutex);
    const auto index = session.bridge.findMaterialIndex(*id).value_or(0);
    session.out << std::format("added material #{} '{}'\n", index, created.getMaterialType());
    return {};
  }

  CommandResult sections(Session& session, const Arguments args) {
    if (args.size() > 1) return std::unexpected("usage: -sections [text]");
    const std::string_view filter = args.empty() ? "" : std::string_view(args[0]);
    std::lock_guard lock(session.bridge.dataMutex);
    const auto& list = session.bridge.allSections;
    std::size_t width = 4;
    for (const auto& section : list) width = std::max(width, section.getName().size());
    session.out << std::format("{:>4}  {:<{}}  {:<9}  {:>10}  {:>11}  {:>11}  {}\n", "#", "name", width, "shape", "A [cm^2]", "Iy [cm^4]",
                               "Iz [cm^4]", "kind");
    std::size_t shown = 0;
    for (std::uint32_t i = 0; i < list.size(); ++i) {
      const auto& s = list[i];
      if (!containsIgnoringCase(s.getName(), filter)) continue;
      ++shown;
      // A and I do not depend on Poisson's ratio (only the shear areas do).
      const auto p = FEM::BEAM::computeProperties(s.getShape(), 0.3);
      session.out << std::format("{:>4}  {:<{}}  {:<9}  {:>10.5g}  {:>11.6g}  {:>11.6g}  {}\n", i, s.getName(), width,
                                 FEM::BEAM::shapeKey(s.getShape()), p.area * 1e4, p.secondMomentY * 1e8, p.secondMomentZ * 1e8,
                                 s.getIsBuiltin() ? "catalogue" : "user");
    }
    if (shown == 0) session.out << "no section matches\n";
    return {};
  }

  CommandResult section(Session& session, const Arguments args) {
    if (args.empty()) return std::unexpected("usage: -section show|add|remove ... (see -help section)");
    const std::string_view action = args[0];
    if (action == "show" || action == "remove") {
      if (args.size() != 2) return std::unexpected(std::format("usage: -section {} <name|#index>", action));
      const auto index = findSection(session, args[1]);
      if (!index) return std::unexpected(index.error());
      if (action == "show") {
        const double nu = [&] {
          const auto material = defaultMaterialIndex(session);
          std::lock_guard lock(session.bridge.dataMutex);
          return session.bridge.allMaterials.empty() ? 0.3 : session.bridge.allMaterials[material].getPoisson();
        }();
        std::lock_guard lock(session.bridge.dataMutex);
        const auto& s = session.bridge.allSections[*index];
        const auto p = FEM::BEAM::computeProperties(s.getShape(), nu);
        session.out << std::format("#{} {} ({}, id {})\n  shape       {} {}\n", *index, s.getName(), s.getIsBuiltin() ? "catalogue" : "user",
                                   s.getSectionID(), FEM::BEAM::shapeKey(s.getShape()), shapeDimensions(s.getShape()));
        session.out << std::format("  A           {:.6g} m^2\n  Iy          {:.6g} m^4\n  Iz          {:.6g} m^4\n  J           {:.6g} m^4\n"
                                   "  Asy         {:.6g} m^2\n  Asz         {:.6g} m^2   (shear areas for nu = {:.3g}, the default material)\n",
                                   p.area, p.secondMomentY, p.secondMomentZ, p.torsionConstant, p.shearAreaY, p.shearAreaZ, nu);
        return {};
      }
      std::uint32_t sectionID = 0;
      {
        std::lock_guard lock(session.bridge.dataMutex);
        sectionID = session.bridge.allSections[*index].getSectionID();
      }
      if (const auto removed = session.bridge.removeUserSection(sectionID); !removed) return std::unexpected(removed.error());
      session.out << std::format("removed section '{}'\n", args[1]);
      return {};
    }
    if (action != "add") return std::unexpected("usage: -section show|add|remove ... (see -help section)");

    const auto parsed = parseArgs(args.subspan(1), {"h", "b", "d", "t", "ro", "ri", "tw", "tf", "r", "A", "Iy", "Iz", "J", "Asy", "Asz"});
    if (!parsed) return std::unexpected(parsed.error());
    if (parsed->positional.size() != 2) return std::unexpected("usage: -section add <name> <shape> <dimensions> (see -help section)");
    const auto shape = parseShape(parsed->positional[1], *parsed);
    if (!shape) return std::unexpected(shape.error());
    const auto id = session.bridge.addUserSection(FEM::BEAM::BeamSection(parsed->positional[0], *shape));
    if (!id) return std::unexpected(id.error());
    std::lock_guard lock(session.bridge.dataMutex);
    const auto index = session.bridge.findSectionIndex(*id).value_or(0);
    session.out << std::format("added section #{} '{}'\n", index, parsed->positional[0]);
    return {};
  }

} // namespace anaf::CLI::HANDLERS end
