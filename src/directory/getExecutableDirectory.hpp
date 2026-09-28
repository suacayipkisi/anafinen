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

#include <filesystem>

namespace anaf::DIRECTORY {

  std::filesystem::path getExecutableDirectory();

  // Finds a shipped asset (subpath relative to "assets/"). Search order: next to the
  // executable (build tree, Windows ZIP), the Linux package location, the working
  // directory, the source tree. Returns an empty path when the asset is not found.
  std::filesystem::path findAssetPath(const std::filesystem::path& subpath);

  // Per-user, writable settings directory (not created here): $XDG_CONFIG_HOME/anafinen or
  // ~/.config/anafinen on Linux, the roaming AppData folder + \anafinen on Windows (wide API,
  // so non-ASCII user names work). Empty if it cannot be determined.
  // User data goes here, never into assets/: that tree is shipped read-only in every package.
  std::filesystem::path getUserConfigDirectory();

} // namespace anaf::DIRECTORY end


