cmake_minimum_required(VERSION 3.20...3.31)
project(anafinen VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_library(anaf_logger STATIC
    src/log/anaf_info.c
)

target_include_directories(anaf_logger PUBLIC src/log)

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

set(EXTERNAL_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external")
set(IMGUI_DIR    "${EXTERNAL_DIR}/imgui")
set(IMGUIZMO_DIR "${EXTERNAL_DIR}/ImGuizmo")
set(IMPLOT_DIR   "${EXTERNAL_DIR}/implot")
set(SPECTRA_DIR  "${EXTERNAL_DIR}/spectra")
set(GLAD_DIR     "${EXTERNAL_DIR}/glad")

# Glad target
add_library(glad STATIC "${GLAD_DIR}/src/gl.c")
target_include_directories(glad PUBLIC "${GLAD_DIR}/include")

set(SOURCES
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp
    ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
    ${IMGUIZMO_DIR}/src/ImGuizmo.cpp
    ${IMPLOT_DIR}/implot.cpp
    ${IMPLOT_DIR}/implot_items.cpp

    src/main.cpp

    src/gui/gui.cpp
    src/gui/guiMaterials/imGuiLayer.cpp

    src/test/test.cpp
    
    src/truss_1D/element.cpp
    src/truss_1D/trussSolver/solveTrussStaticDeflection.cpp

    src/trussEngine/trussSolver.cpp

    src/log/anaf_info.c
)

add_executable(anafinen ${SOURCES})

target_compile_definitions(anafinen PRIVATE 
    MAIN_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
    GLFW_INCLUDE_NONE
)

if(MSVC)
    target_compile_definitions(anafinen PRIVATE NOMINMAX _CRT_SECURE_NO_WARNINGS WIN32_LEAN_AND_MEAN)
    target_compile_options(anafinen PRIVATE /O2 /arch:AVX2)
else()
    target_compile_options(anafinen PRIVATE -O3 -march=native)
    target_link_options(anafinen PRIVATE -O3)
endif()

target_include_directories(anafinen PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
    ${IMGUIZMO_DIR}/src
    ${IMPLOT_DIR}
    ${SPECTRA_DIR}/include
    ${GMSH_INCLUDE_DIRS}
)

target_link_libraries(anafinen PRIVATE 
    glad
    glfw 
    OpenGL::GL
    OpenMP::OpenMP_CXX 
    Eigen3::Eigen
    ${GMSH_LIBRARIES}
)
