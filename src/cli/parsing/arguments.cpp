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

#include "arguments.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <format>
#include <ranges>
#include <system_error>

namespace anaf::CLI {

  std::expected<std::vector<std::string>, std::string> tokenize(const std::string_view line) {
    std::vector<std::string> tokens;
    std::string current;
    bool inToken = false;
    bool inQuotes = false;
    for (std::size_t i = 0; i < line.size(); ++i) {
      const char c = line[i];
      if (inQuotes) {
        if (c == '"') inQuotes = false;
        else current.push_back(c);
        continue;
      }
      if (c == '"') {
        inQuotes = true;
        inToken = true;
      } else if (c == '#' && !inToken && !(i + 1 < line.size() && std::isdigit(static_cast<unsigned char>(line[i + 1])))) {
        break; // a comment; "#3" is an index (material=#3, -section show #3)
      } else if (std::isspace(static_cast<unsigned char>(c))) {
        if (inToken) tokens.push_back(std::move(current));
        current.clear();
        inToken = false;
      } else {
        current.push_back(c);
        inToken = true;
      }
    }
    if (inQuotes) return std::unexpected("missing closing quote");
    if (inToken) tokens.push_back(std::move(current));
    return tokens;
  }

  std::expected<double, std::string> parseDouble(std::string_view token, const std::string_view what) {
    if (token.starts_with('+')) token.remove_prefix(1); // from_chars does not take a leading '+'
    double value = 0.0;
    const auto [end, ec] = std::from_chars(token.data(), token.data() + token.size(), value);
    if (token.empty() || ec != std::errc{} || end != token.data() + token.size() || !std::isfinite(value)) {
      return std::unexpected(std::format("{}: '{}' is not a number", what, token));
    }
    return value;
  }

  std::expected<std::uint32_t, std::string> parseIndex(const std::string_view token, const std::string_view what) {
    std::uint32_t value = 0;
    const auto [end, ec] = std::from_chars(token.data(), token.data() + token.size(), value);
    if (token.empty() || ec != std::errc{} || end != token.data() + token.size()) {
      return std::unexpected(std::format("{}: '{}' is not a non-negative integer", what, token));
    }
    return value;
  }

  std::expected<std::array<double, 3>, std::string> parseVector3(const std::string_view token, const std::string_view what) {
    std::array<double, 3> vector{};
    std::size_t count = 0;
    for (const auto part : std::views::split(token, ',')) {
      if (count == 3) return std::unexpected(std::format("{}: '{}' must be three numbers x,y,z", what, token));
      const auto value = parseDouble(std::string_view(part.begin(), part.end()), what);
      if (!value) return std::unexpected(value.error());
      vector[count++] = *value;
    }
    if (count != 3) return std::unexpected(std::format("{}: '{}' must be three numbers x,y,z", what, token));
    return vector;
  }

  std::expected<std::vector<std::uint32_t>, std::string> parseIdList(const std::string_view token, const std::size_t count,
                                                                     const std::string_view what) {
    std::vector<std::uint32_t> ids;
    if (token == "all") {
      if (count == 0) return std::unexpected(std::format("no {}s to choose from", what));
      ids.resize(count);
      for (std::uint32_t i = 0; i < ids.size(); ++i) ids[i] = i;
      return ids;
    }
    for (const auto part : std::views::split(token, ',')) {
      const std::string_view item(part.begin(), part.end());
      const auto dash = item.find('-', 1);
      const auto first = parseIndex(item.substr(0, dash), what);
      if (!first) return std::unexpected(first.error());
      auto last = first;
      if (dash != std::string_view::npos) {
        last = parseIndex(item.substr(dash + 1), what);
        if (!last) return std::unexpected(last.error());
        if (*last < *first) return std::unexpected(std::format("{}: range '{}' runs backwards", what, item));
      }
      if (*last >= count) {
        return std::unexpected(count == 0 ? std::format("{} {} does not exist (there are none)", what, *last)
                                          : std::format("{} {} does not exist (valid: 0..{})", what, *last, count - 1));
      }
      for (std::uint32_t id = *first; id <= *last; ++id) ids.push_back(id);
    }
    std::ranges::sort(ids);
    const auto [begin, end] = std::ranges::unique(ids);
    ids.erase(begin, end);
    if (ids.empty()) return std::unexpected(std::format("no {}s to choose from", what));
    return ids;
  }

  std::optional<std::string_view> ParsedArgs::get(const std::string_view key) const {
    for (const auto& [name, value] : named) {
      if (name == key) return value;
    }
    return std::nullopt;
  }

  std::expected<ParsedArgs, std::string> parseArgs(const std::span<const std::string> args,
                                                   const std::initializer_list<std::string_view> allowedKeys) {
    ParsedArgs parsed;
    for (const auto& arg : args) {
      const auto equals = arg.find('=');
      const bool isNamed = equals != std::string::npos && equals > 0 &&
                           std::ranges::all_of(arg.substr(0, equals), [](const char c) {
                             return std::isalpha(static_cast<unsigned char>(c)) != 0 || c == '_';
                           });
      if (!isNamed) {
        parsed.positional.push_back(arg);
        continue;
      }
      std::string key = arg.substr(0, equals);
      if (std::ranges::find(allowedKeys, std::string_view(key)) == allowedKeys.end()) {
        std::string allowed;
        for (const auto known : allowedKeys) allowed += std::format("{}{}=", allowed.empty() ? "" : ", ", known);
        return std::unexpected(allowedKeys.size() == 0 ? std::format("unknown option '{}=' (this command takes none)", key)
                                                       : std::format("unknown option '{}=' (allowed: {})", key, allowed));
      }
      if (parsed.get(key)) return std::unexpected(std::format("option '{}=' given twice", key));
      parsed.named.emplace_back(std::move(key), arg.substr(equals + 1));
    }
    return parsed;
  }

} // namespace anaf::CLI end
