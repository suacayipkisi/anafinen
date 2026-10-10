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

#include <beam/beamIO/beamMeshAdapter.hpp>
#include <beam/beamTypes/beamLibrary.hpp>
#include <directory/getExecutableDirectory.hpp>
#include <io/core/pathUtf8.hpp>
#include <io/meshIo.hpp>
#include <truss_1D/trussIO/trussMeshAdapter.hpp>
#include <truss_1D/trussTypes/simpleQuadranglePrismTrussCreate.hpp>
#include <truss_1D/trussTypes/trussLibrary.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <filesystem>
#include <format>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace anaf::CLI::HANDLERS {

  namespace {

    void printNotes(Session& session, const std::vector<std::string>& notes) {
      for (const auto& note : notes) session.out << "  note: " << note << '\n';
    }

    // Reads a model file into the bridge the way File > Import does: beam files (beam
    // ElementFormulation) become the beam model, every other file a truss. The previous model
    // is dropped; sections the file brings are added as user sections.
    CommandResult loadModelFile(Session& session, const std::filesystem::path& path, const IO::ReadOptions& options) {
      auto& bridge = session.bridge;
      if (bridge.isRunning) return std::unexpected("a solve is running");
      auto model = IO::readMesh(path, options);
      if (!model) return std::unexpected(std::format("cannot read '{}': {}", IO::pathToUtf8(path), model.error().message));

      std::vector<MATERIAL::Material> materials;
      std::vector<FEM::BEAM::BeamSection> sections;
      {
        std::lock_guard lock(bridge.dataMutex);
        materials = bridge.allMaterials;
        sections = bridge.allSections;
      }

      if (FEM::BEAM::ADAPTER::isBeamModel(*model)) {
        auto beam = FEM::BEAM::ADAPTER::toMeshData(*model, materials, sections);
        if (!beam) return std::unexpected(std::format("'{}' is not a usable beam model: {}", IO::pathToUtf8(path), beam.error()));
        bridge.resetModel(BRIDGE::E_ObjectType::BeamFrame);
        // Element section indices from sections.size() on refer to these, in order.
        for (const auto& section : beam->newSections) {
          if (const auto added = bridge.addUserSection(section); !added) {
            return std::unexpected(std::format("section '{}' of the file could not be added: {}", section.getName(), added.error()));
          }
          session.out << std::format("  added section '{}'\n", section.getName());
        }
        {
          std::lock_guard lock(bridge.dataMutex);
          bridge.activeBeamMesh = beam->mesh;
        }
        bridge.dataVersion.fetch_add(1, std::memory_order_release);
        for (const auto& warning : model->warnings) session.out << "  warning: " << warning << '\n';
        printNotes(session, beam->notes);
        session.out << std::format("beam model: {} nodes, {} elements{}\n", beam->mesh->nodes.size(), beam->mesh->elements.size(),
                                   beam->mesh->hasResults ? ", with results" : "");
        return {};
      }

      auto truss = FEM::TRUSS::ADAPTER::toMeshData(*model, materials);
      bridge.resetModel(BRIDGE::E_ObjectType::TrussImportedOrEntered);
      {
        std::lock_guard lock(bridge.dataMutex);
        bridge.activeMesh = truss.mesh;
      }
      bridge.dataVersion.fetch_add(1, std::memory_order_release);
      for (const auto& warning : model->warnings) session.out << "  warning: " << warning << '\n';
      printNotes(session, truss.notes);
      session.out << std::format("truss model: {} nodes, {} elements{}\n", truss.mesh->trussNodes.size(), truss.mesh->trussElements.size(),
                                 truss.mesh->hasResults ? ", with results" : "");
      return {};
    }

    struct LibraryDirs {
      std::filesystem::path truss;
      std::filesystem::path beam;
    };

    LibraryDirs libraryDirs() {
      return {anaf::DIRECTORY::findAssetPath(std::filesystem::path(FEM::TRUSS::LIBRARY::librarySubdir)),
              anaf::DIRECTORY::findAssetPath(std::filesystem::path(FEM::BEAM::LIBRARY::librarySubdir))};
    }

    // Built-in models are never overwritten (the GUI refuses the same).
    bool isInBuiltinLibrary(const std::filesystem::path& path) {
      std::error_code ec;
      const auto folder = std::filesystem::weakly_canonical(std::filesystem::absolute(path, ec).parent_path(), ec);
      if (ec) return false;
      const auto dirs = libraryDirs();
      for (const auto& library : {dirs.truss, dirs.beam}) {
        if (!library.empty() && std::filesystem::equivalent(folder, library, ec) && !ec) return true;
      }
      return false;
    }

    struct FoundEntry {
      bool isBeam{false};
      std::filesystem::path dir;
      std::string id, name, category, description;
    };

    template <typename Entry>
    void appendEntries(std::vector<FoundEntry>& found, const bool isBeam, const std::filesystem::path& dir,
                       const std::vector<Entry>& entries) {
      for (const auto& entry : entries) found.push_back({isBeam, dir, entry.id, entry.name, entry.category, entry.description});
    }

    // Both library indexes, truss first, in index order.
    std::expected<std::vector<FoundEntry>, std::string> libraryEntries(const bool truss, const bool beam) {
      const auto dirs = libraryDirs();
      std::vector<FoundEntry> found;
      if (truss) {
        if (dirs.truss.empty()) return std::unexpected(std::format("assets/{} not found", FEM::TRUSS::LIBRARY::librarySubdir));
        const auto index = FEM::TRUSS::LIBRARY::loadIndex(dirs.truss / std::filesystem::path(FEM::TRUSS::LIBRARY::indexFileName));
        if (!index) return std::unexpected(index.error());
        appendEntries(found, false, dirs.truss, *index);
      }
      if (beam) {
        if (dirs.beam.empty()) return std::unexpected(std::format("assets/{} not found", FEM::BEAM::LIBRARY::librarySubdir));
        const auto index = FEM::BEAM::LIBRARY::loadIndex(dirs.beam / std::filesystem::path(FEM::BEAM::LIBRARY::indexFileName));
        if (!index) return std::unexpected(index.error());
        appendEntries(found, true, dirs.beam, *index);
      }
      return found;
    }

    // "<id>", or "truss:<id>" / "beam:<id>" when both libraries have the id.
    std::expected<FoundEntry, std::string> findLibraryEntry(std::string_view id) {
      bool truss = true, beam = true;
      if (id.starts_with("truss:")) beam = false;
      if (id.starts_with("beam:")) truss = false;
      if (!truss || !beam) id.remove_prefix(id.find(':') + 1);
      auto entries = libraryEntries(truss, beam);
      if (!entries) return std::unexpected(entries.error());
      std::optional<FoundEntry> found;
      for (auto& entry : *entries) {
        if (entry.id != id) continue;
        if (found) return std::unexpected(std::format("'{}' is in both libraries: write truss:{} or beam:{}", id, id, id));
        found = std::move(entry);
      }
      if (!found) return std::unexpected(std::format("no built-in model '{}' (see -library list)", id));
      return std::move(*found);
    }

    struct ExportFormat {
      std::string_view key;
      IO::E_FileFormat format;
      IO::E_MshVersion mshVersion;
      IO::E_VtkLegacyVersion vtkVersion;
    };

    constexpr std::array<ExportFormat, 7> exportFormats{{
      {"msh", IO::E_FileFormat::Msh, IO::E_MshVersion::V4_1, IO::E_VtkLegacyVersion::V5_1},
      {"msh22", IO::E_FileFormat::Msh, IO::E_MshVersion::V2_2, IO::E_VtkLegacyVersion::V5_1},
      {"vtu", IO::E_FileFormat::Vtu, IO::E_MshVersion::V4_1, IO::E_VtkLegacyVersion::V5_1},
      {"pvd", IO::E_FileFormat::Pvd, IO::E_MshVersion::V4_1, IO::E_VtkLegacyVersion::V5_1},
      {"vtk", IO::E_FileFormat::VtkLegacy, IO::E_MshVersion::V4_1, IO::E_VtkLegacyVersion::V5_1},
      {"vtk42", IO::E_FileFormat::VtkLegacy, IO::E_MshVersion::V4_1, IO::E_VtkLegacyVersion::V4_2},
      {"step", IO::E_FileFormat::Step, IO::E_MshVersion::V4_1, IO::E_VtkLegacyVersion::V5_1},
    }};

  } // namespace end

  CommandResult newModel(Session& session, const Arguments args) {
    if (args.size() != 1 || (args[0] != "truss" && args[0] != "beam")) return std::unexpected("usage: -new truss|beam");
    const bool beam = args[0] == "beam";
    session.bridge.resetModel(beam ? BRIDGE::E_ObjectType::BeamFrame : BRIDGE::E_ObjectType::TrussImportedOrEntered);
    session.out << std::format("new empty {} model\n", beam ? "beam / frame" : "truss");
    return {};
  }

  CommandResult generate(Session& session, const Arguments args) {
    const auto parsed = parseArgs(args, {"edge", "area", "material"});
    if (!parsed) return std::unexpected(parsed.error());
    if (parsed->positional.size() != 4 || parsed->positional[0] != "sqpt") {
      return std::unexpected("usage: -generate sqpt <nx> <ny> <nz> [edge=<m>] [area=<m^2>] [material=<name|#index>]");
    }
    std::array<std::uint32_t, 3> cubes{};
    for (std::size_t axis = 0; axis < 3; ++axis) {
      const auto count = parseIndex(parsed->positional[axis + 1], "cube count");
      if (!count) return std::unexpected(count.error());
      cubes[axis] = *count;
    }
    double edge = 1.0;
    if (const auto value = parsed->get("edge")) {
      const auto number = parseDouble(*value, "edge");
      if (!number) return std::unexpected(number.error());
      edge = *number;
    }
    double area = session.defaults.area;
    if (const auto value = parsed->get("area")) {
      const auto number = parseDouble(*value, "area");
      if (!number) return std::unexpected(number.error());
      area = *number;
    }
    std::uint32_t materialIndex = defaultMaterialIndex(session);
    if (const auto value = parsed->get("material")) {
      const auto index = findMaterial(session, *value);
      if (!index) return std::unexpected(index.error());
      materialIndex = *index;
    }
    if (session.bridge.isRunning) return std::unexpected("a solve is running");
    auto built = FEM::TRUSS::buildSimpleTruss(cubes, edge, area, materialIndex);
    if (!built) return std::unexpected(built.error());
    session.bridge.resetModel(BRIDGE::E_ObjectType::TrussImportedOrEntered);
    const auto mesh = std::make_shared<BRIDGE::MeshData>(std::move(*built));
    {
      std::lock_guard lock(session.bridge.dataMutex);
      session.bridge.activeMesh = mesh;
    }
    session.bridge.dataVersion.fetch_add(1, std::memory_order_release);
    session.out << std::format("truss model: {} nodes, {} elements (no supports or loads yet)\n", mesh->trussNodes.size(),
                               mesh->trussElements.size());
    return {};
  }

  CommandResult library(Session& session, const Arguments args) {
    const std::string_view action = args.empty() ? "list" : std::string_view(args[0]);
    if (action == "list" || action == "truss" || action == "beam") {
      const std::string_view kind = action == "list" ? (args.size() > 1 ? std::string_view(args[1]) : "") : action;
      if (args.size() > (action == "list" ? 2U : 1U) || (!kind.empty() && kind != "truss" && kind != "beam")) {
        return std::unexpected("usage: -library [list] [truss|beam]");
      }
      const auto entries = libraryEntries(kind != "beam", kind != "truss");
      if (!entries) return std::unexpected(entries.error());
      // Grouped by kind and category, index order inside a category.
      std::map<std::pair<bool, std::string>, std::vector<const FoundEntry*>> groups;
      for (const auto& entry : *entries) groups[{entry.isBeam, entry.category}].push_back(&entry);
      std::size_t width = 0;
      for (const auto& entry : *entries) width = std::max(width, entry.id.size());
      for (const auto& [key, members] : groups) {
        session.out << std::format("{} / {}\n", key.first ? "beam" : "truss", key.second);
        for (const auto* entry : members) session.out << std::format("  {:<{}}  {}\n", entry->id, width, entry->name);
      }
      session.out << std::format("{} models; -library show <id> describes one, -library load <id> loads it\n", entries->size());
      return {};
    }
    if (action == "show") {
      if (args.size() != 2) return std::unexpected("usage: -library show <id>");
      const auto entry = findLibraryEntry(args[1]);
      if (!entry) return std::unexpected(entry.error());
      session.out << std::format("{} ({} / {})\n{}\n", entry->name, entry->isBeam ? "beam" : "truss", entry->category, entry->description);
      return {};
    }
    if (action == "load") {
      if (args.size() < 2 || args.size() > 3 || (args.size() == 3 && args[2] != "solved")) {
        return std::unexpected("usage: -library load <id> [solved]");
      }
      const auto entry = findLibraryEntry(args[1]);
      if (!entry) return std::unexpected(entry.error());
      const bool solved = args.size() == 3;
      if (solved && !entry->isBeam) return std::unexpected("only the beam library stores solved models; load it and -solve");
      const auto path = solved ? entry->dir / (entry->id + "_solved.msh") : entry->dir / (entry->id + ".msh");
      session.out << std::format("loading '{}'\n", entry->name);
      return loadModelFile(session, path, {});
    }
    return std::unexpected("usage: -library [list] [truss|beam] | show <id> | load <id> [solved]");
  }

  CommandResult importFile(Session& session, const Arguments args) {
    const auto parsed = parseArgs(args, {"dim", "size"});
    if (!parsed) return std::unexpected(parsed.error());
    if (parsed->positional.size() != 1) return std::unexpected("usage: -import <file> [dim=1|2|3] [size=<m>]");
    IO::ReadOptions options;
    if (const auto value = parsed->get("dim")) {
      const auto dim = parseIndex(*value, "dim");
      if (!dim) return std::unexpected(dim.error());
      if (*dim < 1 || *dim > 3) return std::unexpected("dim must be 1, 2 or 3");
      options.cadMeshDimension = static_cast<int>(*dim);
    }
    if (const auto value = parsed->get("size")) {
      const auto size = parseDouble(*value, "size");
      if (!size) return std::unexpected(size.error());
      options.cadMeshSize = *size;
    }
    return loadModelFile(session, IO::pathFromUtf8(parsed->positional.front()), options);
  }

  CommandResult exportFile(Session& session, const Arguments args) {
    const auto parsed = parseArgs(args, {"format"});
    if (!parsed) return std::unexpected(parsed.error());
    const auto& positional = parsed->positional;
    if (positional.empty()) return std::unexpected("usage: -export <file> [format=...] [binary] [compress]");
    bool binary = false, compress = false;
    for (std::size_t i = 1; i < positional.size(); ++i) {
      if (positional[i] == "binary") binary = true;
      else if (positional[i] == "compress") compress = true;
      else return std::unexpected(std::format("unexpected argument '{}' (binary or compress)", positional[i]));
    }
    const auto path = IO::pathFromUtf8(positional.front());
    if (isInBuiltinLibrary(path)) return std::unexpected("the built-in libraries are read-only: export to another folder");

    IO::WriteOptions options;
    if (const auto key = parsed->get("format")) {
      const auto* choice = std::ranges::find(exportFormats, *key, &ExportFormat::key);
      if (choice == exportFormats.end()) return std::unexpected(std::format("unknown format '{}' (msh, msh22, vtu, pvd, vtk, vtk42, step)", *key));
      options.format = choice->format;
      options.mshVersion = choice->mshVersion;
      options.vtkVersion = choice->vtkVersion;
    } else {
      options.format = IO::detectFormat(path);
      if (options.format == IO::E_FileFormat::Auto) return std::unexpected("unknown extension: give format= (msh, msh22, vtu, pvd, vtk, vtk42, step)");
    }
    const bool canBinary = options.format == IO::E_FileFormat::Msh || options.format == IO::E_FileFormat::Vtu ||
                           options.format == IO::E_FileFormat::Pvd || options.format == IO::E_FileFormat::VtkLegacy;
    const bool canCompress = options.format == IO::E_FileFormat::Vtu || options.format == IO::E_FileFormat::Pvd;
    if (options.format == IO::E_FileFormat::Iges || options.format == IO::E_FileFormat::Brep) return std::unexpected("IGES and BREP are read only");
    if (binary && !canBinary) return std::unexpected(std::format("{} has no binary encoding", IO::formatName(options.format)));
    if (compress && (!binary || !canCompress)) return std::unexpected("compress needs binary and a VTU / PVD file");
    options.encoding = binary ? IO::E_Encoding::Binary : IO::E_Encoding::Ascii;
    options.compress = compress;

    std::vector<MATERIAL::Material> materials;
    std::vector<FEM::BEAM::BeamSection> sections;
    {
      std::lock_guard lock(session.bridge.dataMutex);
      materials = session.bridge.allMaterials;
      sections = session.bridge.allSections;
    }
    std::optional<IO::MeshModel> model;
    if (modelKind(session) == E_ModelKind::Beam) {
      const auto mesh = beamMesh(session);
      if (mesh && !mesh->nodes.empty()) model = FEM::BEAM::ADAPTER::toMeshModel(*mesh, materials, sections);
    } else if (modelKind(session) == E_ModelKind::Truss) {
      const auto mesh = trussMesh(session);
      if (mesh && !mesh->trussNodes.empty()) model = FEM::TRUSS::ADAPTER::toMeshModel(*mesh, materials);
    }
    if (!model) return std::unexpected("nothing to export: create or import a model first");

    const auto report = IO::writeMesh(path, *model, options);
    if (!report) return std::unexpected(std::format("cannot write '{}': {}", IO::pathToUtf8(path), report.error().message));
    for (const auto& warning : model->warnings) session.out << "  warning: " << warning << '\n';
    for (const auto& warning : report->warnings) session.out << "  warning: " << warning << '\n';
    session.out << std::format("wrote {} ({})\n", report->path, IO::formatName(options.format));
    for (const auto& extra : report->extraFiles) session.out << "  and " << extra << '\n';
    return {};
  }

} // namespace anaf::CLI::HANDLERS end
