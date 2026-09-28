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

#include "properties.hpp"

#include <cstddef>
#include <expected>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace anaf::MATERIAL {

  inline constexpr std::size_t kMaxMaterialNameLength = 120; // bytes (UTF-8)

  // Material names identify a material in the library and in saved mesh files; comparison
  // ignores ASCII case ("steel" == "Steel").
  bool sameMaterialName(std::string_view a, std::string_view b);

  // Checks the physical limits of one material: positive moduli, strengths and density,
  // ultimate >= yield strength, -1 < Poisson's ratio < 0.5, ductility >= 0; and the name:
  // non-empty, at most kMaxMaterialNameLength bytes, no quotes or control characters.
  std::expected<void, std::string> validateMaterial(const Material& material);

  // Reads the built-in material library (assets/bridge/materialProperties.json).
  // IDs must be 0..n-1: elements store a material as an index into the list. Mesh files
  // carry material names, only files from anafinen <= 0.1.2 rely on IDs 0 and 1.
  std::expected<std::vector<Material>, std::string> loadMaterialLibrary(const std::filesystem::path& path);

  // Reads the user materials saved by the Material Handler (same schema, no "id": IDs are
  // assigned by the bridge). A missing file is not an error and yields an empty list.
  std::expected<std::vector<Material>, std::string> loadUserMaterialFile(const std::filesystem::path& path);

  // Writes the non-built-in entries of materials to path (parent directories are created).
  // The file is written next to path first and then renamed over it, so a crash never
  // leaves a half-written file behind.
  std::expected<void, std::string> saveUserMaterialFile(const std::filesystem::path& path, std::span<const Material> materials);

} // namespace anaf::MATERIAL end
