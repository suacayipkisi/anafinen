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

// Command handlers of the table in commandTable.cpp (internal to the CLI).

#include "commands.hpp"

namespace anaf::CLI::HANDLERS {

  // generalCommands.cpp
  CommandResult help(Session& session, Arguments args);
  CommandResult operations(Session& session, Arguments args);
  CommandResult license(Session& session, Arguments args);
  CommandResult thirdPartyLicenses(Session& session, Arguments args);
  CommandResult version(Session& session, Arguments args);
  CommandResult exit(Session& session, Arguments args);
  CommandResult run(Session& session, Arguments args);
  CommandResult status(Session& session, Arguments args);
  CommandResult set(Session& session, Arguments args);

  // modelCommands.cpp
  CommandResult newModel(Session& session, Arguments args);
  CommandResult generate(Session& session, Arguments args);
  CommandResult library(Session& session, Arguments args);
  CommandResult importFile(Session& session, Arguments args);
  CommandResult exportFile(Session& session, Arguments args);

  // materialCommands.cpp
  CommandResult materials(Session& session, Arguments args);
  CommandResult material(Session& session, Arguments args);
  CommandResult sections(Session& session, Arguments args);
  CommandResult section(Session& session, Arguments args);

  // editCommands.cpp
  CommandResult node(Session& session, Arguments args);
  CommandResult nodes(Session& session, Arguments args);
  CommandResult element(Session& session, Arguments args);
  CommandResult elements(Session& session, Arguments args);
  CommandResult support(Session& session, Arguments args);
  CommandResult load(Session& session, Arguments args);
  CommandResult loads(Session& session, Arguments args);
  CommandResult gravity(Session& session, Arguments args);
  CommandResult release(Session& session, Arguments args);
  CommandResult formulation(Session& session, Arguments args);

  // resultCommands.cpp
  CommandResult solve(Session& session, Arguments args);
  CommandResult results(Session& session, Arguments args);
  CommandResult diagram(Session& session, Arguments args);

} // namespace anaf::CLI::HANDLERS end
