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

#include "anafInfo.hpp"
#include "gui/gui.hpp"
#include "test/status.hpp"

#ifdef _WIN32
extern "C" {
    __declspec(dllexport) unsigned long NvOptimusEnablement = 0x00000001;
    __declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}
#endif

namespace Log = anafLog;

int main() {
    Log::core("Initializing ANAFINEN Workspace (C++23)...");

    AllStatus mainStatus{};


    if(initRunGUI(mainStatus) != -1){
        Log::info("Main window processed succesfully.");
    }
    else{
        Log::error("Main window initrun is not succesful!");
    }

    Log::core("Anafinen is closed.");
    return 0;
}
