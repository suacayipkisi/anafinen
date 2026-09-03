cmake_minimum_required(VERSION 3.20...3.31)
project(anafinen VERSION 0.1.0 LANGUAGES C CXX)

include(CheckIPOSupported)
check_ipo_supported(RESULT IPO_SUPPORTED OUTPUT IPO_ERROR)
if(IPO_SUPPORTED)
    message(STATUS "IPO / LTO enabled for Release builds.")
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
else()
    message(WARNING "IPO / LTO not supported: ${IPO_ERROR}")
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
option(ANAFINEN_NATIVE_OPTIMIZATIONS "Enable CPU-specific native optimizations" OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(CompilerOptions)

include(Dependencies)

include(ExternalGui)

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

set(ANAFINEN_SOURCES
    src/main.cpp
    src/bridge/generalStatus.cpp
    src/directory/getExecutableDirectory.cpp
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
    PNG::PNG
    project_warnings_and_optimizations
)

add_custom_command(
    TARGET anafinen POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/assets
        $<TARGET_FILE_DIR:anafinen>/assets
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${ANAFINEN_GENERATED_ASSETS_DIR}/icons/anafinen.png"
        "$<TARGET_FILE_DIR:anafinen>/assets/icons/anafinen.png"
)

if(WIN32)
    add_custom_command(
        TARGET anafinen POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${GMSH_DLL}"
            $<TARGET_FILE_DIR:anafinen>
    )
endif()

include(Packaging)
