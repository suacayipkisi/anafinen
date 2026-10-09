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

#include "cli/session.hpp"

#include <filesystem>
#include <span>
#include <string>
#include <string_view>

namespace anaf::CLI {

  using Arguments = std::span<const std::string>; // the tokens after the command name

  struct Command {
    std::string_view name;    // typed with a leading '-', stored without it
    std::string_view usage;   // forms of the command, one per line
    std::string_view summary; // what it does, shown by -help <command>
    CommandResult (*run)(Session&, Arguments);
  };

  // Every command, in the order -help lists them.
  std::span<const Command> commandTable();
  // nullptr for an unknown name (given without the leading '-').
  const Command* findCommand(std::string_view name);

  // Tokenizes and runs one line ("-node add 0 0 0"). Blank and comment-only lines succeed.
  // A failure is printed to session.err as "error: ..."; returns false then.
  bool executeLine(Session& session, std::string_view line);

  // Runs the lines of a script file (the syntax of the prompt, '#' comments) and stops at the
  // first failing line. Each line is echoed with a "> " prefix.
  CommandResult runScript(Session& session, const std::filesystem::path& path);

} // namespace anaf::CLI end
