# Copyright (c) 2026 Ufuk Deniz Konuk
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program. If not, see <https://www.gnu.org/licenses/>.
#
# SPDX-License-Identifier: GPL-3.0-or-later

cmake_minimum_required(VERSION 3.20...3.31)
project(anafinen VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
option(ANAFINEN_NATIVE_OPTIMIZATIONS "Enable CPU-specific native optimizations" OFF)

# compiler warnings and architecture-specific optimizations
add_library(project_warnings_and_optimizations INTERFACE)

if(MSVC)
    target_compile_definitions(project_warnings_and_optimizations INTERFACE 
        NOMINMAX _CRT_SECURE_NO_WARNINGS WIN32_LEAN_AND_MEAN
    )
    target_compile_options(project_warnings_and_optimizations INTERFACE
        $<$<CONFIG:Release>:/O2>
    )
    if(ANAFINEN_NATIVE_OPTIMIZATIONS)
        target_compile_options(project_warnings_and_optimizations INTERFACE /arch:AVX2)
    endif()
else()
    target_compile_options(project_warnings_and_optimizations INTERFACE
        $<$<CONFIG:Release>:-O3>
    )
    if(ANAFINEN_NATIVE_OPTIMIZATIONS)
        target_compile_options(project_warnings_and_optimizations INTERFACE -march=native)
    endif()
    target_link_options(project_warnings_and_optimizations INTERFACE
        $<$<CONFIG:Release>:-O3>
    )
endif()

# core system dependencies
find_package(Eigen3 REQUIRED)
find_package(OpenMP REQUIRED)
find_package(OpenGL REQUIRED)

# gmsh detection
if(WIN32)
    set(GMSH_SDK_DIR "C:/libs/gmsh-sdk" CACHE PATH "Path to Gmsh SDK on Windows")
    find_path(GMSH_INCLUDE_DIR NAMES "gmsh.h" HINTS "${GMSH_SDK_DIR}/include")
    find_library(GMSH_LIBRARY NAMES gmsh HINTS "${GMSH_SDK_DIR}/lib")
    find_file(GMSH_DLL NAMES gmsh.dll HINTS "${GMSH_SDK_DIR}/bin" "${GMSH_SDK_DIR}/lib")
else()
    find_path(GMSH_INCLUDE_DIR NAMES "gmsh.h")
    find_library(GMSH_LIBRARY NAMES gmsh)
endif()

if(NOT GMSH_INCLUDE_DIR OR NOT GMSH_LIBRARY OR (WIN32 AND NOT GMSH_DLL))
    message(FATAL_ERROR "Gmsh SDK or headers not found! Set GMSH_SDK_DIR to your Gmsh installation path.")
endif()

add_library(Gmsh::Gmsh UNKNOWN IMPORTED)
set_target_properties(Gmsh::Gmsh PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${GMSH_INCLUDE_DIR}"
)
if(WIN32)
    set_target_properties(Gmsh::Gmsh PROPERTIES
        IMPORTED_IMPLIB "${GMSH_LIBRARY}"
        IMPORTED_LOCATION "${GMSH_DLL}"
    )
else()
    set_target_properties(Gmsh::Gmsh PROPERTIES
        IMPORTED_LOCATION "${GMSH_LIBRARY}"
    )
endif()

# external submodules configuration
set(EXTERNAL_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external")
set(IMGUI_DIR    "${EXTERNAL_DIR}/imgui")
set(IMGUIZMO_DIR "${EXTERNAL_DIR}/ImGuizmo")
set(IMPLOT_DIR   "${EXTERNAL_DIR}/implot")
set(SPECTRA_DIR  "${EXTERNAL_DIR}/spectra")
set(GLAD_DIR     "${EXTERNAL_DIR}/glad")

# spectra setup
set(SPECTRA_TARGET "")
if(EXISTS "${SPECTRA_DIR}/include/Spectra/SymEigsSolver.h")
    add_library(spectra_local INTERFACE)
    target_include_directories(spectra_local INTERFACE "${SPECTRA_DIR}/include")
    set(SPECTRA_TARGET spectra_local)
else()
    find_package(Spectra CONFIG QUIET)
    find_package(spectra CONFIG QUIET)
    if(TARGET Spectra::Spectra)
        set(SPECTRA_TARGET Spectra::Spectra)
    elseif(TARGET spectra::spectra)
        set(SPECTRA_TARGET spectra::spectra)
    elseif(TARGET spectra)
        set(SPECTRA_TARGET spectra)
    endif()
endif()

# GLAD setup
add_library(glad_local STATIC "${GLAD_DIR}/src/gl.c")
target_include_directories(glad_local PUBLIC "${GLAD_DIR}/include")
set(GLAD_TARGET glad_local)

# GLFW setup
set(GLFW_TARGET "")

if(EXISTS "${EXTERNAL_DIR}/glfw/CMakeLists.txt")
    set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
    add_subdirectory("${EXTERNAL_DIR}/glfw" EXCLUDE_FROM_ALL)
    set(GLFW_TARGET glfw)
else()
    # 1. Try CMake Config mode
    find_package(glfw3 CONFIG QUIET)
    if(TARGET glfw)
        set(GLFW_TARGET glfw)
    elseif(TARGET glfw3::glfw3)
        set(GLFW_TARGET glfw3::glfw3)
    endif()

    # 2. Try PkgConfig mode
    if(NOT GLFW_TARGET)
        find_package(PkgConfig QUIET)
        if(PKG_CONFIG_FOUND)
            pkg_check_modules(GLFW QUIET IMPORTED_TARGET glfw3)
            if(TARGET PkgConfig::GLFW)
                set(GLFW_TARGET PkgConfig::GLFW)
            endif()
        endif()
    endif()

    # 3. Fallback to FetchContent if no local library or system package exists
    if(NOT GLFW_TARGET)
        message(STATUS "GLFW not found locally or via system package; fetching from GitHub...")
        include(FetchContent)
        set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
        set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
        set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
        set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)

        FetchContent_Declare(
            glfw
            GIT_REPOSITORY https://github.com/glfw/glfw.git
            GIT_TAG 3.4
        )
        FetchContent_MakeAvailable(glfw)
        set(GLFW_TARGET glfw)
    endif()
endif()

if(NOT GLFW_TARGET)
    message(FATAL_ERROR "Failed to resolve GLFW target.")
endif()

# ImGui Suite setup
set(GUI_DEPENDENCIES "")
if(EXISTS "${IMGUI_DIR}/imgui.cpp")
    set(IMGUIZMO_SRC "${IMGUIZMO_DIR}/ImGuizmo.cpp")
    if(NOT EXISTS "${IMGUIZMO_SRC}")
        set(IMGUIZMO_SRC "${IMGUIZMO_DIR}/src/ImGuizmo.cpp")
    endif()

    add_library(imgui_suite STATIC
        ${IMGUI_DIR}/imgui.cpp
        ${IMGUI_DIR}/imgui_draw.cpp
        ${IMGUI_DIR}/imgui_tables.cpp
        ${IMGUI_DIR}/imgui_widgets.cpp
        ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp
        ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
        ${IMGUIZMO_SRC}
        ${IMPLOT_DIR}/implot.cpp
        ${IMPLOT_DIR}/implot_items.cpp
    )
    target_include_directories(imgui_suite PUBLIC
        ${IMGUI_DIR}
        ${IMGUI_DIR}/backends
        ${IMGUIZMO_DIR}
        ${IMGUIZMO_DIR}/src
        ${IMPLOT_DIR}
    )
    target_link_libraries(imgui_suite PUBLIC ${GLAD_TARGET} ${GLFW_TARGET} OpenGL::GL)
    target_compile_definitions(imgui_suite PUBLIC GLFW_INCLUDE_NONE)
    list(APPEND GUI_DEPENDENCIES imgui_suite)
else()
    find_package(imgui CONFIG REQUIRED)
    find_package(implot CONFIG REQUIRED)
    find_package(imguizmo CONFIG QUIET)
    if(TARGET imgui::imgui)
        list(APPEND GUI_DEPENDENCIES imgui::imgui)
    endif()
    if(TARGET imgui::imgui_impl_glfw)
        list(APPEND GUI_DEPENDENCIES imgui::imgui_impl_glfw)
    endif()
    if(TARGET imgui::imgui_impl_opengl3)
        list(APPEND GUI_DEPENDENCIES imgui::imgui_impl_opengl3)
    endif()
    if(TARGET implot::implot)
        list(APPEND GUI_DEPENDENCIES implot::implot)
    endif()
    if(TARGET imguizmo::imguizmo)
        list(APPEND GUI_DEPENDENCIES imguizmo::imguizmo)
    elseif(TARGET ImGuizmo::ImGuizmo)
        list(APPEND GUI_DEPENDENCIES ImGuizmo::ImGuizmo)
    elseif(TARGET imguizmo)
        list(APPEND GUI_DEPENDENCIES imguizmo)
    endif()
endif()

# core FEM & engine library
set(ANAF_CORE_SOURCES
    src/truss_1D/trussProperties/element.cpp
    src/truss_1D/trussTypes/simpleQuadranglePrismTrussCreate.cpp
    src/truss_1D/trussEngine/trussSolver/solveTrussStaticDeflection.cpp
    src/truss_1D/trussEngine/trussSolver.cpp
)

add_library(anaf_core STATIC ${ANAF_CORE_SOURCES})
target_include_directories(anaf_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

if(SPECTRA_TARGET)
    target_link_libraries(anaf_core PUBLIC ${SPECTRA_TARGET})
endif()

target_link_libraries(anaf_core PUBLIC
    Eigen3::Eigen
    OpenMP::OpenMP_CXX
    Gmsh::Gmsh
    project_warnings_and_optimizations
)

# main exec
set(ANAFINEN_SOURCES
    src/main.cpp
    src/bridge/generalStatus.cpp
    src/log/anaf_info.cpp
    src/gui/gui.cpp
    src/gui/guiMaterials/imGuiLayer.cpp
    src/gui/guiMaterials/framebuffer.cpp
    src/gui/panels/logTerminal.cpp
    src/gui/panels/mainDockSpaceHost.cpp
    src/gui/panels/modelTree.cpp
    src/gui/panels/truss/trussTypePanel.cpp
    src/gui/panels/truss/simpleQuadrangleTruss/trussControlPanel.cpp
    src/gui/panels/viewportPanel.cpp
    src/test/test.cpp
)

add_executable(anafinen ${ANAFINEN_SOURCES})

target_compile_definitions(anafinen PRIVATE 
    MAIN_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
    GLFW_INCLUDE_NONE
)


target_link_libraries(anafinen PRIVATE 
    anaf_core
    ${GUI_DEPENDENCIES}
    ${GLAD_TARGET}
    ${GLFW_TARGET}
    OpenGL::GL
    project_warnings_and_optimizations
)

# post build

# copy assets
add_custom_command(
    TARGET anafinen POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/assets
        $<TARGET_FILE_DIR:anafinen>/assets
)

# Copy Gmsh DLL for local Windows runs.
if(WIN32)
    add_custom_command(
        TARGET anafinen POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${GMSH_DLL}"
            $<TARGET_FILE_DIR:anafinen>
    )
endif()

# ### Installation & Packaging ####################################################################
include(GNUInstallDirs)

install(TARGETS anafinen
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

if(WIN32)
    install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/assets/"
        DESTINATION "${CMAKE_INSTALL_BINDIR}/assets"
    )
    install(FILES "${GMSH_DLL}"
        DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
else()
    install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/assets/"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/anafinen/assets"
    )
endif()

if(UNIX AND NOT APPLE)
    install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/assets/anafinen.desktop"
        DESTINATION ${CMAKE_INSTALL_DATADIR}/applications
    )
    install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/assets/icons/anafinen.svg"
        DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps
    )
endif()

# --- CPack packaging ---
set(CPACK_PACKAGE_NAME "anafinen")
set(CPACK_PACKAGE_VENDOR "Ufuk Deniz Konuk")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_RELEASE "1")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "3D FEM Dynamic and Modal Analysis Engine")
set(CPACK_PACKAGE_LICENSE "GPL-3.0-or-later")
set(CPACK_PACKAGE_FILE_NAME "${CPACK_PACKAGE_NAME}-${CPACK_PACKAGE_VERSION}-alpha")

if(WIN32)
    set(CPACK_GENERATOR "ZIP")
    set(CPACK_PACKAGE_FILE_NAME "${CPACK_PACKAGE_NAME}-${CPACK_PACKAGE_VERSION}-windows-${CMAKE_SYSTEM_PROCESSOR}-alpha")
elseif(UNIX AND NOT APPLE)
    # Default to RPM on RedHat/Fedora systems
    set(CPACK_GENERATOR "RPM;TGZ")

    # Fedora RPM specific settings
    set(CPACK_RPM_PACKAGE_AUTOREQPROV ON)
    set(CPACK_RPM_PACKAGE_RELEASE "1.alpha")
    set(CPACK_RPM_PACKAGE_LICENSE "GPLv3+")
    set(CPACK_RPM_PACKAGE_GROUP "Applications/Engineering")
    set(CPACK_DEBIAN_PACKAGE_VERSION "${PROJECT_VERSION}~alpha1")
    set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
endif()

include(CPack)
