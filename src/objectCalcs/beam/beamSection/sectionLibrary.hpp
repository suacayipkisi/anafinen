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

// Section catalogue (assets/bridge/sectionCatalog.json, read-only) and user section file,
// the same scheme as the material library (src/material/materialLibrary.hpp). Lengths in m.
//
// Entry: {"id": 0 (catalogue only), "name": "IPE 300", "shape": "i", <dimensions>}
//   general    area, secondMomentY, secondMomentZ, torsionConstant, shearAreaY, shearAreaZ
//   rectangle  height, width
//   circle     diameter
//   pipe       outerDiameter, wallThickness
//   box        height, width, wallThickness, outerCornerRadius, innerCornerRadius
//   i          height, flangeWidth, webThickness, flangeThickness, rootRadius
// The catalogue stores dimensions only; A, I, J and the shear areas are always computed.

#include "beamSection.hpp"

#include <cstddef>
#include <expected>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace FEM::BEAM {

  inline constexpr std::size_t kMaxSectionNameLength = 120; // bytes (UTF-8)
  inline constexpr const char* kSectionCatalogAsset = "bridge/sectionCatalog.json"; // for anaf::DIRECTORY::findAssetPath

  // Section names identify a section in the library and in saved mesh files; comparison
  // ignores ASCII case ("ipe 300" == "IPE 300").
  bool sameSectionName(std::string_view a, std::string_view b);

  // Name rules (non-empty, at most kMaxSectionNameLength bytes, no quotes or control
  // characters) and validateShape().
  std::expected<void, std::string> validateSection(const BeamSection& section);

  // Reads the built-in catalogue. IDs must be 0..n-1: elements store a section as an index.
  std::expected<std::vector<BeamSection>, std::string> loadSectionLibrary(const std::filesystem::path& path);

  // Reads user sections (same schema, no "id"). A missing file yields an empty list.
  std::expected<std::vector<BeamSection>, std::string> loadUserSectionFile(const std::filesystem::path& path);

  // Writes the non-built-in entries (parent directories are created; written next to path,
  // then renamed over it).
  std::expected<void, std::string> saveUserSectionFile(const std::filesystem::path& path, std::span<const BeamSection> sections);

} // namespace FEM::BEAM end
