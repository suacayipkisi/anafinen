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
#include "log/anaf_info.hpp"
#include <omp.h>

#include "cli.hpp"

#ifdef _WIN32
extern "C" {
  __declspec(dllexport) unsigned long NvOptimusEnablement = 0x00000001;
  __declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}
#endif

int main() {

  anaf::LOG::setConsoleOutput(true);

  if (!anaf::LOG::init("anafinen_run.log")) {
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

  const int cliStatus = anaf::CLI::initcli();

  anaf::LOG::core("Anafinen is closing.");
  anaf::LOG::close();
  return cliStatus == 0 ? 0 : 1;
}
