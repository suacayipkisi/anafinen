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

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <initializer_list>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace anaf::CLI {

  // Splits a command line into tokens at white space. "..." keeps spaces inside one token
  // (names such as "IPE 300"); # at the start of a token starts a comment,
  // except before a digit ("#3" is an index). Fails on an open quote.
  std::expected<std::vector<std::string>, std::string> tokenize(std::string_view line);

  // A finite number ("2.1e11", "-9.81"). what names the value in the message.
  std::expected<double, std::string> parseDouble(std::string_view token, std::string_view what);
  // A non-negative integer that fits in 32 bits.
  std::expected<std::uint32_t, std::string> parseIndex(std::string_view token, std::string_view what);
  // Three comma separated numbers: "0,1,0".
  std::expected<std::array<double, 3>, std::string> parseVector3(std::string_view token, std::string_view what);
  // Ids below count: "all", "4", "0,3,7", "2-9" or a mix ("0,4-6"). Sorted, without duplicates,
  // never empty (fails when there is nothing to choose).
  std::expected<std::vector<std::uint32_t>, std::string> parseIdList(std::string_view token, std::size_t count, std::string_view what);

  // Arguments of one command: key=value tokens are named, every other token is positional.
  struct ParsedArgs {
    std::vector<std::string> positional;
    std::vector<std::pair<std::string, std::string>> named;

    std::optional<std::string_view> get(std::string_view key) const;
  };

  // Fails on a key that is not in allowedKeys or that is given twice.
  std::expected<ParsedArgs, std::string> parseArgs(std::span<const std::string> args, std::initializer_list<std::string_view> allowedKeys);

} // namespace anaf::CLI end
