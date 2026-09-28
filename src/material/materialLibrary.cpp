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

#include "materialLibrary.hpp"

#include <io/core/pathUtf8.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <format>
#include <fstream>
#include <set>
#include <string>

namespace anaf::MATERIAL {

  namespace {
    using nlohmann::json;

    double requireNumber(const json& entry, const char* key) {
      const auto it = entry.find(key);
      if (it == entry.end() || !it->is_number()) throw std::runtime_error(std::format("'{}' is missing or not a number", key));
      return it->get<double>();
    }

    constexpr int kSchemaVersion = 1;

    // Shortest decimal form of the float (0.3f is written as 0.3, not 0.30000001192092896).
    // std::from_chars, unlike std::stod, never depends on the C locale (decimal comma).
    double floatForJson(const float value) {
      const std::string text = std::format("{}", value);
      double result = value;
      std::from_chars(text.data(), text.data() + text.size(), result);
      return result;
    }

    Material parseMaterial(const json& entry, const bool isBuiltin) {
      if (!entry.is_object()) throw std::runtime_error("entry is not an object");

      std::uint32_t materialID = 0;
      if (isBuiltin) {
        const auto id = entry.find("id");
        if (id == entry.end() || !id->is_number_unsigned()) throw std::runtime_error("'id' is missing or not an unsigned integer");
        materialID = id->get<std::uint32_t>();
      }
      const auto name = entry.find("name");
      if (name == entry.end() || !name->is_string()) throw std::runtime_error("'name' is missing or not a string");

      const double elasticityModulus = requireNumber(entry, "elasticityModulus");
      // Optional: the solver only uses the elasticity modulus, both are E for a truss bar.
      const double youngModulus = entry.contains("youngModulus") ? requireNumber(entry, "youngModulus") : elasticityModulus;

      return Material{
        isBuiltin,
        name->get<std::string>(),
        elasticityModulus,
        requireNumber(entry, "shearModulus"),
        requireNumber(entry, "bulkModulus"),
        requireNumber(entry, "yieldTensileStrength"),
        requireNumber(entry, "ultimateTensileStrength"),
        youngModulus,
        requireNumber(entry, "density"),
        static_cast<float>(requireNumber(entry, "poissonsRatio")),
        static_cast<float>(requireNumber(entry, "ductility")),
        materialID
      };
    }

    // Parses and validates the "materials" array of a library or user file.
    std::expected<std::vector<Material>, std::string> readMaterialFile(const std::filesystem::path& path, const bool isBuiltin) {
      std::ifstream file(path);
      if (!file) return std::unexpected(std::format("cannot open '{}'", anaf::IO::pathToUtf8(path)));

      json root;
      try {
        root = json::parse(file);
      } catch (const json::exception& exception) {
        return std::unexpected(std::format("'{}': {}", anaf::IO::pathToUtf8(path), exception.what()));
      }

      const auto list = root.find("materials");
      if (list == root.end() || !list->is_array()) {
        return std::unexpected(std::format("'{}': 'materials' is missing or not an array", anaf::IO::pathToUtf8(path)));
      }

      std::vector<Material> materials;
      materials.reserve(list->size());
      std::set<std::string, std::less<>> names;
      for (std::size_t i = 0; i < list->size(); ++i) {
        try {
          materials.push_back(parseMaterial((*list)[i], isBuiltin));
        } catch (const std::exception& exception) {
          return std::unexpected(std::format("'{}': material #{}: {}", anaf::IO::pathToUtf8(path), i, exception.what()));
        }
        const auto& material = materials.back();
        if (const auto valid = validateMaterial(material); !valid) {
          return std::unexpected(std::format("'{}': '{}': {}", anaf::IO::pathToUtf8(path), material.getMaterialType(), valid.error()));
        }
        if (!names.emplace(material.getMaterialType()).second) {
          return std::unexpected(std::format("'{}': duplicate material name '{}'", anaf::IO::pathToUtf8(path), material.getMaterialType()));
        }
      }
      return materials;
    }
  } // namespace end

  bool sameMaterialName(const std::string_view a, const std::string_view b) {
    return std::ranges::equal(a, b, [](const unsigned char x, const unsigned char y) { return std::tolower(x) == std::tolower(y); });
  }

  std::expected<void, std::string> validateMaterial(const Material& material) {
    const auto positive = [](const double value) { return std::isfinite(value) && value > 0.0; };

    const std::string_view name = material.getMaterialType();
    if (name.empty()) return std::unexpected("name is empty");
    if (name.size() > kMaxMaterialNameLength) return std::unexpected(std::format("name is longer than {} bytes", kMaxMaterialNameLength));
    // Names are written into mesh files (MSH physical names are double-quoted, VTK / sidecar are line based).
    const auto forbidden = [](const unsigned char c) { return c < 0x20 || c == 0x7f || c == '"'; };
    if (std::ranges::any_of(name, forbidden)) return std::unexpected("name must not contain quotes or control characters");
    if (!positive(material.getElasticityModulues())) return std::unexpected("elasticity modulus must be > 0");
    if (!positive(material.getYoungModulus())) return std::unexpected("Young's modulus must be > 0");
    if (!positive(material.getShearModulues())) return std::unexpected("shear modulus must be > 0");
    if (!positive(material.getBulkModulus())) return std::unexpected("bulk modulus must be > 0");
    if (!positive(material.getYieldTensile())) return std::unexpected("yield strength must be > 0");
    if (!positive(material.getUltTensile())) return std::unexpected("ultimate strength must be > 0");
    if (material.getUltTensile() < material.getYieldTensile()) return std::unexpected("ultimate strength must be >= yield strength");
    if (!positive(material.getDensity())) return std::unexpected("density must be > 0");
    if (!(material.getPoisson() > -1.0f && material.getPoisson() < 0.5f)) return std::unexpected("Poisson's ratio must be in (-1, 0.5)");
    if (!(std::isfinite(material.getDuctility()) && material.getDuctility() >= 0.0f)) return std::unexpected("ductility must be >= 0");
    return {};
  }

  std::expected<std::vector<Material>, std::string> loadMaterialLibrary(const std::filesystem::path& path) {
    auto materials = readMaterialFile(path, true);
    if (!materials) return materials;
    if (materials->empty()) return std::unexpected(std::format("'{}': 'materials' is empty", anaf::IO::pathToUtf8(path)));

    std::ranges::sort(*materials, {}, &Material::getMaterialID);
    for (std::uint32_t i = 0; i < materials->size(); ++i) {
      if ((*materials)[i].getMaterialID() != i) {
        return std::unexpected(std::format("'{}': material IDs must be 0..{} without gaps or duplicates", anaf::IO::pathToUtf8(path), materials->size() - 1));
      }
    }
    return materials;
  }

  std::expected<std::vector<Material>, std::string> loadUserMaterialFile(const std::filesystem::path& path) {
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) return std::vector<Material>{};
    return readMaterialFile(path, false);
  }

  std::expected<void, std::string> saveUserMaterialFile(const std::filesystem::path& path, const std::span<const Material> materials) {
    json list = json::array();
    for (const auto& material : materials) {
      if (material.getIsBuiltin()) continue;
      list.push_back({
        {"name", std::string(material.getMaterialType())},
        {"elasticityModulus", material.getElasticityModulues()},
        {"youngModulus", material.getYoungModulus()},
        {"shearModulus", material.getShearModulues()},
        {"bulkModulus", material.getBulkModulus()},
        {"yieldTensileStrength", material.getYieldTensile()},
        {"ultimateTensileStrength", material.getUltTensile()},
        {"density", material.getDensity()},
        {"poissonsRatio", floatForJson(material.getPoisson())},
        {"ductility", floatForJson(material.getDuctility())}
      });
    }
    const json root = {{"schemaVersion", kSchemaVersion}, {"materials", std::move(list)}};
    std::string text;
    try {
      text = root.dump(2);
    } catch (const json::exception& exception) { // invalid UTF-8 in a name
      return std::unexpected(std::format("cannot encode the materials: {}", exception.what()));
    }

    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) return std::unexpected(std::format("cannot create '{}': {}", anaf::IO::pathToUtf8(path.parent_path()), ec.message()));

    auto temporary = path;
    temporary += ".tmp";
    {
      std::ofstream file(temporary, std::ios::trunc);
      file << text << '\n';
      if (!file.flush()) return std::unexpected(std::format("cannot write '{}'", anaf::IO::pathToUtf8(temporary)));
    }
    std::filesystem::rename(temporary, path, ec); // replaces the old file on Windows too (MoveFileExW)
    if (ec) {
      // Windows: a virus scanner or indexer may briefly hold the target; copying over it still works.
      std::error_code copyError;
      std::filesystem::copy_file(temporary, path, std::filesystem::copy_options::overwrite_existing, copyError);
      std::error_code ignored;
      std::filesystem::remove(temporary, ignored);
      if (copyError) {
        return std::unexpected(std::format("cannot replace '{}': {}", anaf::IO::pathToUtf8(path), copyError.message()));
      }
    }
    return {};
  }

} // namespace anaf::MATERIAL end
