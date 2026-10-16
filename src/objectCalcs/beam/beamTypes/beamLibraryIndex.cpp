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

// Runtime part of the built-in library: reading index.json. The generators (buildLibrary(),
// writeLibrary(), indexJson()) are in beamLibrary.cpp, built into anaf_library_generators for
// the tools and the tests only, so the GUI and the CLI do not carry them.

#include "beamLibrary.hpp"

#include <io/core/pathUtf8.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <exception>
#include <format>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

namespace FEM::BEAM::LIBRARY {

  std::expected<std::vector<Entry>, std::string> loadIndex(const std::filesystem::path& indexFile) {
    std::ifstream in(indexFile, std::ios::binary);
    if (!in) return std::unexpected(std::format("cannot open '{}'", anaf::IO::pathToUtf8(indexFile)));
    try {
      const auto root = nlohmann::json::parse(in);
      std::vector<Entry> entries;
      for (const auto& item : root.at("models")) {
        Entry entry{item.at("id").get<std::string>(), item.at("name").get<std::string>(), item.at("category").get<std::string>(),
                    item.at("description").get<std::string>()};
        const bool safeId = !entry.id.empty() && std::ranges::all_of(entry.id, [](const char c) {
          return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
        });
        if (!safeId) return std::unexpected(std::format("invalid model id '{}'", entry.id));
        entries.push_back(std::move(entry));
      }
      return entries;
    } catch (const std::exception& exception) {
      return std::unexpected(std::format("'{}': {}", anaf::IO::pathToUtf8(indexFile), exception.what()));
    }
  }

} // namespace FEM::BEAM::LIBRARY end
