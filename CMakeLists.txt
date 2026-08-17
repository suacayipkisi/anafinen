cmake_minimum_required(VERSION 3.20...3.31)
cmake_policy(SET CMP0169 OLD)
project(anafinen VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Eigen3 CONFIG REQUIRED)
find_package(OpenMP REQUIRED)
find_package(OpenGL REQUIRED)

include(FetchContent)

# 1. Spectra (Header-Only Eigen Solver)
FetchContent_Declare(
    spectra
    GIT_REPOSITORY https://github.com/yixuan/spectra.git
    GIT_TAG        v1.0.1
)
FetchContent_GetProperties(spectra)
if(NOT spectra_POPULATED)
    FetchContent_Populate(spectra)
endif()

# 2. GLFW (Windowing & Context)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG        3.4
)
FetchContent_MakeAvailable(glfw)

# 3. Dear ImGui (GUI & Docking)
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        docking
)
FetchContent_MakeAvailable(imgui)

# Source Files
set(SOURCES
    src/main.cpp
    src/glad.c
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)

add_executable(anafinen ${SOURCES})

target_compile_definitions(anafinen PRIVATE 
    MAIN_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
    GLFW_INCLUDE_NONE
)

# Compiler Optimizations
if(MSVC)
    target_compile_definitions(anafinen PRIVATE NOMINMAX _CRT_SECURE_NO_WARNINGS WIN32_LEAN_AND_MEAN)
    target_compile_options(anafinen PRIVATE /O2 /arch:AVX2)
else()
    target_compile_options(anafinen PRIVATE -O3 -march=native)
    target_link_options(anafinen PRIVATE -O3)
endif()

# Include Directories
target_include_directories(anafinen PRIVATE 
    include
    ${imgui_SOURCE_DIR} 
    ${imgui_SOURCE_DIR}/backends
    ${spectra_SOURCE_DIR}/include
)

# Link Libraries
target_link_libraries(anafinen PRIVATE 
    glfw 
    OpenGL::GL
    OpenMP::OpenMP_CXX 
    Eigen3::Eigen
)
