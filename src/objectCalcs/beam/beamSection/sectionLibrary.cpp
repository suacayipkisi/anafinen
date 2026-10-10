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

#include "sectionLibrary.hpp"

#include <io/core/pathUtf8.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <format>
#include <fstream>
#include <set>
#include <stdexcept>

namespace FEM::BEAM {

  namespace {
    using nlohmann::json;
    constexpr int currentSchemaVersion = 1;

    template <class... Ts> struct Overloaded : Ts... { using Ts::operator()...; };

    double number(const json& entry, const char* key) {
      const auto it = entry.find(key);
      if (it == entry.end() || !it->is_number()) throw std::runtime_error(std::format("'{}' is missing or not a number", key));
      return it->get<double>();
    }

    // Optional values (corner and root radii) default to 0.
    double optionalNumber(const json& entry, const char* key) {
      const auto it = entry.find(key);
      if (it == entry.end()) return 0.0;
      if (!it->is_number()) throw std::runtime_error(std::format("'{}' is not a number", key));
      return it->get<double>();
    }

    SectionShape parseShape(const json& entry) {
      const auto it = entry.find("shape");
      if (it == entry.end() || !it->is_string()) throw std::runtime_error("'shape' is missing or not a string");
      const auto key = it->get<std::string>();
      if (key == "general") {
        return GeneralSection{{number(entry, "area"), number(entry, "secondMomentY"), number(entry, "secondMomentZ"),
                               number(entry, "torsionConstant"), optionalNumber(entry, "shearAreaY"), optionalNumber(entry, "shearAreaZ")}};
      }
      if (key == "rectangle") return RectangleSection{number(entry, "height"), number(entry, "width")};
      if (key == "circle") return CircleSection{number(entry, "diameter")};
      if (key == "pipe") return PipeSection{number(entry, "outerDiameter"), number(entry, "wallThickness")};
      if (key == "box") {
        return BoxSection{number(entry, "height"), number(entry, "width"), number(entry, "wallThickness"),
                          optionalNumber(entry, "outerCornerRadius"), optionalNumber(entry, "innerCornerRadius")};
      }
      if (key == "i") {
        return ISection{number(entry, "height"), number(entry, "flangeWidth"), number(entry, "webThickness"),
                        number(entry, "flangeThickness"), optionalNumber(entry, "rootRadius")};
      }
      throw std::runtime_error(std::format("unknown shape '{}'", key));
    }

    json shapeToJson(const SectionShape& shape) {
      json entry = {{"shape", shapeKey(shape)}};
      std::visit(Overloaded{
        [&](const GeneralSection& s) {
          entry["area"] = s.values.area;
          entry["secondMomentY"] = s.values.secondMomentY;
          entry["secondMomentZ"] = s.values.secondMomentZ;
          entry["torsionConstant"] = s.values.torsionConstant;
          entry["shearAreaY"] = s.values.shearAreaY;
          entry["shearAreaZ"] = s.values.shearAreaZ;
        },
        [&](const RectangleSection& s) { entry["height"] = s.height; entry["width"] = s.width; },
        [&](const CircleSection& s) { entry["diameter"] = s.diameter; },
        [&](const PipeSection& s) { entry["outerDiameter"] = s.outerDiameter; entry["wallThickness"] = s.wallThickness; },
        [&](const BoxSection& s) {
          entry["height"] = s.height;
          entry["width"] = s.width;
          entry["wallThickness"] = s.wallThickness;
          entry["outerCornerRadius"] = s.outerCornerRadius;
          entry["innerCornerRadius"] = s.innerCornerRadius;
        },
        [&](const ISection& s) {
          entry["height"] = s.height;
          entry["flangeWidth"] = s.flangeWidth;
          entry["webThickness"] = s.webThickness;
          entry["flangeThickness"] = s.flangeThickness;
          entry["rootRadius"] = s.rootRadius;
        },
      }, shape);
      return entry;
    }

    std::string lowered(std::string_view text) {
      std::string result(text);
      for (char& c : result) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
      return result;
    }

    std::expected<std::vector<BeamSection>, std::string> readSectionFile(const std::filesystem::path& path, const bool isBuiltin) {
      const auto where = anaf::IO::pathToUtf8(path);
      std::ifstream file(path);
      if (!file) return std::unexpected(std::format("cannot open '{}'", where));

      json root;
      try {
        root = json::parse(file);
      } catch (const json::exception& exception) {
        return std::unexpected(std::format("'{}': {}", where, exception.what()));
      }
      const auto list = root.find("sections");
      if (list == root.end() || !list->is_array()) return std::unexpected(std::format("'{}': 'sections' is missing or not an array", where));

      std::vector<BeamSection> sections;
      sections.reserve(list->size());
      std::set<std::string, std::less<>> names;
      for (std::size_t i = 0; i < list->size(); ++i) {
        const auto& entry = (*list)[i];
        try {
          if (!entry.is_object()) throw std::runtime_error("entry is not an object");
          std::uint32_t id = 0;
          if (isBuiltin) {
            const auto it = entry.find("id");
            if (it == entry.end() || !it->is_number_unsigned()) throw std::runtime_error("'id' is missing or not an unsigned integer");
            id = it->get<std::uint32_t>();
          }
          const auto name = entry.find("name");
          if (name == entry.end() || !name->is_string()) throw std::runtime_error("'name' is missing or not a string");
          sections.emplace_back(name->get<std::string>(), parseShape(entry), isBuiltin, id);
        } catch (const std::exception& exception) {
          return std::unexpected(std::format("'{}': section #{}: {}", where, i, exception.what()));
        }
        const auto& section = sections.back();
        if (const auto valid = validateSection(section); !valid) {
          return std::unexpected(std::format("'{}': '{}': {}", where, section.getName(), valid.error()));
        }
        if (!names.emplace(lowered(section.getName())).second) {
          return std::unexpected(std::format("'{}': duplicate section name '{}'", where, section.getName()));
        }
      }
      return sections;
    }
  } // namespace end

  bool sameSectionName(const std::string_view a, const std::string_view b) {
    return std::ranges::equal(a, b, [](const unsigned char x, const unsigned char y) { return std::tolower(x) == std::tolower(y); });
  }

  std::expected<void, std::string> validateSection(const BeamSection& section) {
    const std::string_view name = section.getName();
    if (name.empty()) return std::unexpected("name is empty");
    if (name.size() > maxSectionNameLength) return std::unexpected(std::format("name is longer than {} bytes", maxSectionNameLength));
    // Names are written into mesh files, like material names.
    const auto forbidden = [](const unsigned char c) { return c < 0x20 || c == 0x7f || c == '"'; };
    if (std::ranges::any_of(name, forbidden)) return std::unexpected("name must not contain quotes or control characters");
    return validateShape(section.getShape());
  }

  std::expected<std::vector<BeamSection>, std::string> loadSectionLibrary(const std::filesystem::path& path) {
    auto sections = readSectionFile(path, true);
    if (!sections) return sections;
    if (sections->empty()) return std::unexpected(std::format("'{}': 'sections' is empty", anaf::IO::pathToUtf8(path)));
    std::ranges::sort(*sections, {}, &BeamSection::getSectionID);
    for (std::uint32_t i = 0; i < sections->size(); ++i) {
      if ((*sections)[i].getSectionID() != i) {
        return std::unexpected(std::format("'{}': section IDs must be 0..{} without gaps or duplicates",
                                           anaf::IO::pathToUtf8(path), sections->size() - 1));
      }
    }
    return sections;
  }

  std::expected<std::vector<BeamSection>, std::string> loadUserSectionFile(const std::filesystem::path& path) {
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) return std::vector<BeamSection>{};
    return readSectionFile(path, false);
  }

  std::expected<void, std::string> saveUserSectionFile(const std::filesystem::path& path, const std::span<const BeamSection> sections) {
    json list = json::array();
    for (const auto& section : sections) {
      if (section.getIsBuiltin()) continue;
      json entry = shapeToJson(section.getShape());
      entry["name"] = section.getName();
      list.push_back(std::move(entry));
    }
    const json root = {{"schemaVersion", currentSchemaVersion}, {"units", {{"length", "m"}}}, {"sections", std::move(list)}};
    std::string text;
    try {
      text = root.dump(2);
    } catch (const json::exception& exception) { // invalid UTF-8 in a name
      return std::unexpected(std::format("cannot encode the sections: {}", exception.what()));
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
      if (copyError) return std::unexpected(std::format("cannot replace '{}': {}", anaf::IO::pathToUtf8(path), copyError.message()));
    }
    return {};
  }

} // namespace FEM::BEAM end
