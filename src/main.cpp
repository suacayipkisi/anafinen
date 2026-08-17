#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <Eigen/Dense>
#include <Eigen/Sparse>
#include <Spectra/SymGEigsShiftSolver.h>

#include "info.h"

namespace Log = anafinen::logger;

int main() {
    Log::core("Initializing ANAFINEN Core Engine (C++23)...");

    // 1. Initialize GLFW
    if (!glfwInit()) {
        Log::error("GLFW initialization failed.");
        return -1;
    }
    Log::success("GLFW initialized successfully.");

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    GLFWwindow* window = glfwCreateWindow(800, 600, "ANAFINEN - Modal Solver", nullptr, nullptr);
    if (!window) {
        Log::error("Failed to create GLFW window context.");
        glfwTerminate();
        return -1;
    }

    glfwMakeContextCurrent(window);

    // 2. Initialize GLAD (GLAD 2.x API)
    if (!gladLoadGL((GLADloadfunc)glfwGetProcAddress)) {
        Log::error("GLAD loader failed to bind OpenGL context.");
        glfwDestroyWindow(window);
        glfwTerminate();
        return -1;
    }
    Log::success("GLAD function pointers bound successfully.");

    const char* gl_version = reinterpret_cast<const char*>(glGetString(GL_VERSION));
    const char* gl_renderer = reinterpret_cast<const char*>(glGetString(GL_RENDERER));

    Log::info("OpenGL Version: {}", gl_version ? gl_version : "Unknown");
    Log::info("GPU Renderer  : {}", gl_renderer ? gl_renderer : "Unknown");

    // 3. Eigen Linear Algebra Test
    Eigen::Matrix2d K;
    K <<  2.0, -1.0,
         -1.0,  2.0;

    double det = K.determinant();
    if (det == 3.0) {
        Log::success("Eigen math backend verified. Det(K) = {}", det);
    } else {
        Log::warn("Eigen determinant mismatch. Result: {}", det);
    }

    Log::core("All engine subsystems operational. Ready for Phase 1 assembly.");

    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
