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

#include "log/anaf_info.h"
#include "gui/gui.hpp"
#include "test/status.hpp"

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


    if(initRunGUI(mainStatus) != -1){
        anaf_info("Main window processed succesfully.");
    }
    else{
        anaf_error("Main window initrun is not succesful!");
    }

    anaf_core("Anafinen is closed.");
    return 0;
}
