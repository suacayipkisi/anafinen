cmake_minimum_required(VERSION 3.20...3.31)
project(anafinen VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Eigen3 CONFIG REQUIRED)
find_package(OpenMP REQUIRED)
find_package(OpenGL REQUIRED)

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

set(SOURCES
    src/main.cpp
    src/glad.c
    
    # Dear ImGui Core & Backends
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp
    ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
    
    # ImGuizmo (Located under src/ in the repository)
    ${IMGUIZMO_DIR}/src/ImGuizmo.cpp
    
    # ImPlot
    ${IMPLOT_DIR}/implot.cpp
    ${IMPLOT_DIR}/implot_items.cpp
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
)

target_link_libraries(anafinen PRIVATE 
    glfw 
    OpenGL::GL
    OpenMP::OpenMP_CXX 
    Eigen3::Eigen
)
