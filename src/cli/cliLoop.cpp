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
#include "cli/operations/sideCommands.hpp"
#include <iostream>
#include <string>
#include <string_view>

namespace anaf::CLI {

  void cliLoop() {
    while(true) {
      std::cout << ">>> ";
      std::string userInput{};
      std::cin >> userInput;
      std::string_view operation = userInput;
      if(operation.starts_with("-")) {
        operation.remove_prefix(1);

        // exits loop if input is: "-exit"
        if(!SIDE_COMMANDS::mainSideCommands(operation)) {
          break;
        }
      }
      //std::cout << userInput << '\n';
    }
  }

} // namespace anaf::CLI end
