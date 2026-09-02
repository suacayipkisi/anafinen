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
#include <vector>

#include "log/anaf_info.hpp"
#include "gui/gui.hpp"
#include "material/properties.hpp"
#include "test/status.hpp"
#include "gen/genNum.hpp"
#include "gui/panels/logTerminal.hpp"

#include "bridge/generalStatus.hpp"

#ifdef _WIN32
extern "C" {
    __declspec(dllexport) unsigned long NvOptimusEnablement = 0x00000001;
    __declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}
#endif

int main() {
    anaf::BRIDGE::Gui_Calc_Bridge& GUI_CALC_BRIDGE = anaf::BRIDGE::buildBridge();

    anaf::LOG::setCallback(
        [](anaf::LOG::Level level, std::string_view message) {
            anaf::GUI::anafUILogSink(level, std::string(message).c_str());
        }
    );
    if (!anaf::LOG::init("anafinen_run.log")) {
        anaf::LOG::error("Failed to open log file!");
        return 1;
    }
    anaf::LOG::core("Initializing ANAFINEN Workspace (C++23)...");

    anaf::TEST::AllStatus mainStatus{};

    // add two material for experimental reasons
    // if you see that code block below that means
    // not even phase 1 is finished
    // you are currently watching the born of an analysis program
    anafGen::IdGenerator materialIDs;
    std::vector<anaf::MATERIAL::Material> allMaterials;
    allMaterials.push_back({
        "Structural Steel (AISI 4130)",
        205.0e9,
        78.0e9,
        160.0e9,
        435.0e6,
        670.0e6,
        205.0e9,
        0.29f,
        0.25f,
        materialIDs.next_u32()
    });
    allMaterials.push_back({
        "Aluminum 6061-T6",
        68.9e9,
        26.0e9,
        67.5e9,
        276.0e9 / 1e3,
        310.0e6,
        68.9e9,
        0.33f,
        0.12f,
        materialIDs.next_u32()
    });

    anaf::GUI::initgui();

    anaf::LOG::core("Anafinen is closing.");
    anaf::LOG::close();
    anaf::LOG::core("Anafinen is closed.");
    return 0;
}
