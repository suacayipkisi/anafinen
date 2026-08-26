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

#include "log/anaf_info.h"
#include "gui/gui.hpp"
#include "material/properties.hpp"
#include "test/status.hpp"
#include "gen/genNum.hpp"

#ifdef _WIN32
extern "C" {
    __declspec(dllexport) unsigned long NvOptimusEnablement = 0x00000001;
    __declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}
#endif

int main() {
    if (!anaf_log_init("anafinen_run.log")) {
        anaf_error("Failed to open log file!");
        return 1;
    }
    anaf_core("Initializing ANAFINEN Workspace (C++23)...");

    AllStatus mainStatus{};

    anafGen::IdGenerator materialIDs;
    std::vector<Material> allMaterials;
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

    if(initRunGUI(mainStatus) != -1){
        anaf_info("Main window processed succesfully.");
    }
    else{
        anaf_error("Main window initrun is not succesful!");
    }

    anaf_core("Anafinen is closed.");
    return 0;
}
