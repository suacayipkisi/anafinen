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

// UTF-8 <-> std::filesystem::path. On Windows path::string() converts to the ANSI code page
// (MSVC throws for characters it cannot map, e.g. "Ş" under code page 1252) and
// path(std::string) reads the ANSI code page. Use these helpers for every path that is
// shown, logged, or passed to a library expecting UTF-8 (ImGui, Gmsh, portable-file-dialogs).
// On Linux both are plain byte copies.

#include <filesystem>
#include <string>
#include <string_view>

namespace anaf::IO {

  inline std::string pathToUtf8(const std::filesystem::path& path) {
    const std::u8string text = path.u8string();
    return {text.begin(), text.end()};
  }

  inline std::filesystem::path pathFromUtf8(const std::string_view text) {
    return std::filesystem::path(std::u8string(text.begin(), text.end()));
  }

} // namespace anaf::IO end
