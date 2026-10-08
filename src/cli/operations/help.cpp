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

#include <filesystem>
#include <iostream>
#include "directory/getExecutableDirectory.hpp"
#include "io/detail/textIo.hpp"
#include "log/anaf_info.hpp"

namespace anaf::CLI {

  namespace SIDE_COMMANDS {

    int help() {
      const std::filesystem::path cliHelpFilePath = anaf::DIRECTORY::findAssetPath(std::filesystem::path("global") / "cli-help.txt");
      if (cliHelpFilePath.empty()) {
        anaf::LOG::error("Help file not found: assets/global/cli-help.txt");
        return 0;
      }
      std::cout << IO::detail::readWholeFile(cliHelpFilePath) << '\n';
      return 1;
    }

  } // namespace SIDE_COMMANDS end

} // namespace anaf::CLI end
