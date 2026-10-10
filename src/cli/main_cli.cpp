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

#include "bridge/generalStatus.hpp"
#include "directory/getExecutableDirectory.hpp"

#ifdef _WIN32
#include "io/core/pathUtf8.hpp"
#endif

#include "log/anaf_info.hpp"
#include <omp.h>

#include "cli.hpp"

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>

// force to use external GPU if exist, not iGPU
extern "C" {
  __declspec(dllexport) unsigned long NvOptimusEnablement = 0x00000001;
  __declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}
#endif

namespace {

  // The arguments as UTF-8, without the program name. On Windows argv is in the ANSI code
  // page, so the wide command line is converted instead (non-ASCII script paths).
  std::vector<std::string> commandLineArguments([[maybe_unused]] const int argc, [[maybe_unused]] char** argv) {
    std::vector<std::string> args;
#ifdef _WIN32
    int count = 0;
    if (LPWSTR* wide = CommandLineToArgvW(GetCommandLineW(), &count)) {
      for (int i = 1; i < count; ++i) args.push_back(anaf::IO::pathToUtf8(std::filesystem::path(wide[i])));
      LocalFree(wide);
    }
#else
    for (int i = 1; i < argc; ++i) args.emplace_back(argv[i]);
#endif
    return args;
  }

} // namespace end

int main(int argc, char** argv) {
#ifdef _WIN32
  // UTF-8 in and out of the console (names such as "Çelik", non-ASCII paths).
  SetConsoleOutputCP(CP_UTF8);
  SetConsoleCP(CP_UTF8);
#endif
  const std::vector<std::string> args = commandLineArguments(argc, argv);
  const bool quiet = std::ranges::any_of(args, [](const std::string& arg) { return arg == "--quiet" || arg == "-q"; });

  // --quiet keeps the log lines off the terminal, normally it's always written
  anaf::LOG::setConsoleOutput(!quiet);

  if (!anaf::LOG::init(anaf::LOG::getLogFileLoc())) {
    anaf::LOG::error("Failed to open log file!");
    return 1;
  }
  anaf::LOG::core("Initializing ANAFINEN Workspace (C++23)...");

  // After the log init, so a missing or broken material file is reported.
  anaf::BRIDGE::Gui_Calc_Bridge& CLI_CALC_BRIDGE = anaf::BRIDGE::buildBridge();
  CLI_CALC_BRIDGE.setStaticInfo();
  // Outside assets/ on purpose: materials added while testing a build never reach a package.
  CLI_CALC_BRIDGE.loadUserMaterials(anaf::DIRECTORY::getUserConfigDirectory() / "userMaterials.json");
  CLI_CALC_BRIDGE.loadSectionCatalog();
  CLI_CALC_BRIDGE.loadUserSections(anaf::DIRECTORY::getUserConfigDirectory() / "userSections.json");

  anaf::LOG::info("OpenMP thread limit set to {} of {} available threads", omp_get_max_threads(), omp_get_num_procs());

  const int cliStatus = anaf::CLI::initcli(args);

  anaf::LOG::core("Anafinen is closing.");
  anaf::LOG::close();
  return cliStatus == 0 ? 0 : 1;
}
