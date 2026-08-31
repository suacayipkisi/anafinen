# Copyright (c) 2026 Ufuk Deniz Konuk

# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.

# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
# GNU General Public License for more details.

# You should have received a copy of the GNU General Public License
# along with this program. If not, see <https://www.gnu.org/licenses/>.

# SPDX-License-Identifier: GPL-3.0-or-later

cmake_minimum_required(VERSION 3.20...3.31)
project(anafinen VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_library(project_warnings_and_optimizations INTERFACE)
if(MSVC)
    target_compile_definitions(project_warnings_and_optimizations INTERFACE 
        NOMINMAX _CRT_SECURE_NO_WARNINGS WIN32_LEAN_AND_MEAN
    )
    target_compile_options(project_warnings_and_optimizations INTERFACE /O2 /arch:AVX2)
else()
    target_compile_options(project_warnings_and_optimizations INTERFACE -O3 -march=native)
    target_link_options(project_warnings_and_optimizations INTERFACE -O3)
endif()

set(EXTERNAL_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external")
set(IMGUI_DIR    "${EXTERNAL_DIR}/imgui")
set(IMGUIZMO_DIR "${EXTERNAL_DIR}/ImGuizmo")
set(IMPLOT_DIR   "${EXTERNAL_DIR}/implot")
set(SPECTRA_DIR  "${EXTERNAL_DIR}/spectra")
set(GLAD_DIR     "${EXTERNAL_DIR}/glad")

find_package(Eigen3 CONFIG REQUIRED)
find_package(OpenMP REQUIRED)
find_package(OpenGL REQUIRED)

find_path(GMSH_INCLUDE_DIRS NAMES "gmsh.h" HINTS "${GMSH_SDK_DIR}/include")
find_library(GMSH_LIBRARIES NAMES gmsh HINTS "${GMSH_SDK_DIR}/lib")

if(NOT GMSH_INCLUDE_DIRS OR NOT GMSH_LIBRARIES)
    message(FATAL_ERROR "Gmsh SDK not found!")
endif()

include(FetchContent)
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

# Core SOURCES ####################################################################################
set(ANAF_CORE_SOURCES
    src/truss_1D/element.cpp
    src/truss_1D/trussSolver/solveTrussStaticDeflection.cpp
    src/trussEngine/trussSolver.cpp
)

add_library(anaf_core STATIC ${ANAF_CORE_SOURCES})

target_include_directories(anaf_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${SPECTRA_DIR}/include
    ${GMSH_INCLUDE_DIRS}
)

target_link_libraries(anaf_core PUBLIC
    Eigen3::Eigen
    OpenMP::OpenMP_CXX
    ${GMSH_LIBRARIES}
    project_warnings_and_optimizations
)

add_library(glad STATIC "${GLAD_DIR}/src/gl.c")
target_include_directories(glad PUBLIC "${GLAD_DIR}/include")

add_library(imgui_suite STATIC
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp
    ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
    ${IMGUIZMO_DIR}/src/ImGuizmo.cpp
    ${IMPLOT_DIR}/implot.cpp
    ${IMPLOT_DIR}/implot_items.cpp
)

target_include_directories(imgui_suite PUBLIC
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
    ${IMGUIZMO_DIR}/src
    ${IMPLOT_DIR}
)
target_link_libraries(imgui_suite PUBLIC glad glfw OpenGL::GL)
target_compile_definitions(imgui_suite PUBLIC GLFW_INCLUDE_NONE)

# SOURCES #########################################################################################
set(ANAFINEN_SOURCES
    src/main.cpp

    src/log/anaf_info.cpp

    src/gui/gui.cpp
    src/gui/guiMaterials/imGuiLayer.cpp
    src/gui/guiMaterials/framebuffer.cpp
    src/gui/panels/logTerminal.cpp
    src/gui/panels/mainDockSpaceHost.cpp
    src/gui/panels/modelTree.cpp
    src/gui/panels/trussControlPanel.cpp
    src/gui/panels/viewportPanel.cpp

    src/test/test.cpp
)

add_executable(anafinen ${ANAFINEN_SOURCES})

target_compile_definitions(anafinen PRIVATE 
    MAIN_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)

target_link_libraries(anafinen PRIVATE 
    anaf_core
    imgui_suite
    glad
    glfw
    OpenGL::GL
    project_warnings_and_optimizations
)

add_custom_command(
    TARGET anafinen POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/assets
        $<TARGET_FILE_DIR:anafinen>/assets
)
