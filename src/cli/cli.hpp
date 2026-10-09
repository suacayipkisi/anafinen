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

#include <span>
#include <string>

namespace anaf::CLI {

  // Runs the CLI with the command-line arguments (UTF-8, without the program name):
  //   (none)              interactive prompt (or the commands piped into stdin)
  //   <script> ...        runs the script files in order
  //   -e "<command>" ...  runs the given command lines in order
  //   --quiet / -q        no log lines on the terminal (the log file is still written)
  //   --help, --version
  // Returns the process exit code: 0, or 1 when a command failed.
  int initcli(std::span<const std::string> args);

} // namespace anaf::CLI end
