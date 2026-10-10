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

#include "cli.hpp"
#include "cliLoop.hpp"

#include "cli/commands/commands.hpp"
#include "cli/session.hpp"

#include <bridge/generalStatus.hpp>
#include <io/core/pathUtf8.hpp>

#include <iostream>
#include <string_view>
#include <vector>

#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

namespace anaf::CLI {

  namespace {

    bool stdinIsTerminal() {
#ifdef _WIN32
      return _isatty(_fileno(stdin)) != 0;
#else
      return isatty(STDIN_FILENO) != 0;
#endif
    }

    constexpr std::string_view usageText =
      "usage: anafinen-cli [--quiet] [<script> ... | -e \"<command>\" ...]\n"
      "  (no arguments)   interactive prompt; type -help for the commands\n"
      "  <script>         runs the commands of a file, stops at the first error\n"
      "  -e \"<command>\"   runs one command line (repeatable)\n"
      "  --quiet, -q      no log lines on the terminal (anafinen_run.log is still written)\n"
      "  --help, --version\n";

  } // namespace end

  int initcli(const std::span<const std::string> args) {
    struct Step {
      bool isScript;
      std::string text;
    };
    std::vector<Step> steps;
    for (std::size_t i = 0; i < args.size(); ++i) {
      const std::string_view arg = args[i];
      if (arg == "--help" || arg == "-h") {
        std::cout << usageText;
        return 0;
      }
      if (arg == "--version") {
        std::cout << "anafinen-cli " << ANAFINEN_VERSION << '\n';
        return 0;
      }
      if (arg == "--quiet" || arg == "-q") {
        continue; // applied in main() before the log starts
      }
      if (arg == "-e") {
        if (i + 1 == args.size()) {
          std::cerr << "error: -e needs a command line\n" << usageText;
          return 1;
        }
        steps.push_back({false, args[++i]});
      } else if (arg.starts_with('-')) {
        std::cerr << "error: unknown option '" << arg << "'\n" << usageText;
        return 1;
      } else {
        steps.push_back({true, std::string(arg)});
      }
    }

    Session session(BRIDGE::buildBridge(), std::cout, std::cerr);
    if (steps.empty()) {
      const bool interactive = stdinIsTerminal();
      if (interactive) std::cout << "ANAFINEN " << ANAFINEN_VERSION << " command line. Type -help for the commands, -exit to leave.\n";
      return cliLoop(session, std::cin, interactive);
    }
    for (const auto& step : steps) {
      if (session.exitRequested) break;
      if (step.isScript) {
        if (const auto ran = runScript(session, IO::pathFromUtf8(step.text)); !ran) {
          std::cerr << "error: " << ran.error() << '\n';
          return 1;
        }
      } else if (!executeLine(session, step.text)) {
        return 1;
      }
    }
    return 0;
  }

} // namespace anaf::CLI end
