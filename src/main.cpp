// Copyright (c) 2026 Ufuk Deniz Konuk
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

#include <glad/gl.h>
#include <GLFW/glfw3.h>

#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include <Eigen/Dense>
#include <Eigen/Sparse>
#include <Spectra/SymGEigsShiftSolver.h>

#include <gmsh.h>
#include <omp.h>

#include "log/anaf_info.hpp"
#include "gui/gui.hpp"
#include "test/status.hpp"

#include "gui/panels/logTerminal.hpp"

#include "bridge/generalStatus.hpp"

#ifdef _WIN32
extern "C" {
  __declspec(dllexport) unsigned long NvOptimusEnablement = 0x00000001;
  __declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}
#endif

int main(int argc, char* argv[]) {
  anaf::BRIDGE::Gui_Calc_Bridge& GUI_CALC_BRIDGE = anaf::BRIDGE::buildBridge();
  GUI_CALC_BRIDGE.setStaticInfo();

  anaf::LOG::setCallback(
    [](anaf::LOG::Level level, std::string_view message) {
      anaf::GUI::anafUILogSink(level, std::string(message).c_str());
    }
  );
  if (!anaf::LOG::init("anafinen_run.log")) {
    anaf::LOG::error("Failed to open log file!");
    return 1;
  }
  anaf::LOG::setFloatPrecision(6); // decimal digits shown for all logged floating-point values
  anaf::LOG::core("Initializing ANAFINEN Workspace (C++23)...");

  const int availableThreads = omp_get_num_procs();
  const int threadCount = availableThreads > 4 ? availableThreads - 2 : availableThreads;
  omp_set_dynamic(0);
  omp_set_num_threads(threadCount);
  Eigen::setNbThreads(threadCount);
  anaf::LOG::info("OpenMP thread limit set to {} of {} available threads", threadCount, availableThreads);

  anaf::TEST::AllStatus mainStatus{};

  anaf::GUI::initgui();

  anaf::LOG::core("Anafinen is closing.");
  anaf::LOG::close();
  anaf::LOG::core("Anafinen is closed.");
  return 0;
}
