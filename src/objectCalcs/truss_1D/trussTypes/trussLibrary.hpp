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

#pragma once

// Built-in truss library: common roof, bridge, stadium, tower and platform trusses as
// ready-to-solve models (geometry, supports, loads, material, cross-section).
//
// The files under assets/objects/truss/truss1D/ are generated from buildLibrary() by
// anaf_truss_library_tool (tools/) and must never be edited by hand; the test suite checks
// that they match the generator. The application only reads them: loading one gives an
// in-memory copy, and export refuses to write into the library folder.

#include <io/model/meshModel.hpp>

#include <expected>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace FEM::TRUSS::LIBRARY {

  // Relative to the assets folder (anaf::DIRECTORY::findAssetPath).
  inline constexpr std::string_view kLibrarySubdir = "objects/truss/truss1D";
  inline constexpr std::string_view kIndexFile = "index.json";

  struct Entry {
    std::string id;          // file name without ".msh": [a-z0-9_]+
    std::string name;        // shown in the GUI
    std::string category;    // Roof, Bridge, Stadium, Tower & Platform
    std::string description; // geometry, supports, loads, section
  };

  struct LibraryTruss {
    Entry entry;
    anaf::IO::MeshModel model; // bars (Line2), CrossSectionArea / MaterialId, Material:<name> set, supports, loads
  };

  // Every built-in truss, in index order. Deterministic: the same models on every platform.
  std::vector<LibraryTruss> buildLibrary();

  // Writes every model as <id>.msh (MSH 4.1 ASCII) plus index.json into dir; returns the entries.
  std::expected<std::vector<Entry>, std::string> writeLibrary(const std::filesystem::path& dir);

  // index.json text for entries (stable formatting, so the committed file can be compared).
  std::string indexJson(std::span<const Entry> entries);

  // Reads and checks index.json of a library folder.
  std::expected<std::vector<Entry>, std::string> loadIndex(const std::filesystem::path& indexFile);

  inline std::filesystem::path modelFile(const std::filesystem::path& libraryDir, const Entry& entry) {
    return libraryDir / (entry.id + ".msh");
  }

} // namespace FEM::TRUSS::LIBRARY end
