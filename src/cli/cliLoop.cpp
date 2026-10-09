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

#include "cliLoop.hpp"

#include "cli/commands/commands.hpp"

#include <string>

namespace anaf::CLI {

  int cliLoop(Session& session, std::istream& in, const bool interactive) {
    std::string line;
    while (!session.exitRequested) {
      if (interactive) session.out << ">>> " << std::flush;
      if (!std::getline(in, line)) {
        if (interactive) session.out << '\n';
        break;
      }
      if (!line.empty() && line.back() == '\r') line.pop_back();
      if (!executeLine(session, line) && !interactive) return 1;
    }
    return 0;
  }

} // namespace anaf::CLI end
