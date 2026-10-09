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

#include "sideCommands.hpp"

#include "directory/getExecutableDirectory.hpp"
#include "io/detail/textIo.hpp"
#include "log/anaf_info.hpp"

#include <filesystem>
#include <sstream>
#include <string>

namespace anaf::CLI {

  namespace SIDE_COMMANDS {

    bool printAssetText(const std::string_view fileName, std::ostream& out) {
      const std::filesystem::path path = anaf::DIRECTORY::findAssetPath(std::filesystem::path("global") / fileName);
      if (path.empty()) {
        anaf::LOG::error("Text file not found: assets/global/{}", fileName);
        return false;
      }
      std::istringstream text(IO::detail::readWholeFile(path));
      std::string line;
      bool inHeader = true;
      while (std::getline(text, line)) {
        if (inHeader && (line.starts_with("//") || line.empty())) continue;
        inHeader = false;
        out << line << '\n';
      }
      return true;
    }

  } // namespace SIDE_COMMANDS end

} // namespace anaf::CLI end
