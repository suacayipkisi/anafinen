cmake_minimum_required(VERSION 3.20...3.31)

if(POLICY CMP0207)
    cmake_policy(SET CMP0207 NEW)
endif()

# Windows: locate the vcpkg toolchain (and define the Gmsh SDK lookup) on any drive.
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/LocateWindowsSdks.cmake")

project(anafinen VERSION 0.3.0 LANGUAGES C CXX)

# A 32-bit compiler (e.g. the VS Code kit "amd64_x86", Hostx64/x86/cl.exe) cannot link the 64-bit
# vcpkg packages; find_package would only report an unrelated "version not compatible" error.
if(WIN32 AND CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR "anafinen builds 64-bit only, but the compiler targets 32-bit x86:\n"
        "  ${CMAKE_CXX_COMPILER}\n"
        "Select the x64 compiler (VS Code: \"CMake: Select a Kit\" -> \"... - amd64\", or run vcvars64.bat), "
        "then delete the CMake cache of this build directory.")
endif()

set(CMAKE_INTERPROCEDURAL_OPTIMIZATION FALSE)
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE FALSE)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
option(ANAFINEN_NATIVE_OPTIMIZATIONS "Enable CPU-specific native optimizations" OFF)
option(ANAFINEN_BUILD_TESTS "Build the anaf_io test suite (ctest)" OFF)
option(ANAFINEN_BUILD_CLI "Build the command-line front end (anafinen-cli)" ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(CompilerOptions)

include(Dependencies)

include(ExternalGui)

# Format-neutral mesh I/O and the HDF5 array store: no solver or GUI dependency, shared by every
# front end (GUI / CLI).
set(ANAF_IO_SOURCES
    src/io/model/elementType.cpp
    src/io/model/meshModel.cpp
    src/io/detail/textIo.cpp
    src/io/detail/modelCodec.cpp
    src/io/detail/gmshSession.cpp
    src/io/detail/vtkCommon.cpp
    src/io/formats/mshFormat.cpp
    src/io/formats/vtkLegacyFormat.cpp
    src/io/formats/vtuFormat.cpp
    src/io/formats/cadFormat.cpp
    src/io/meshIo.cpp
    src/io/service/ioService.cpp
    src/io/array/arrayFile.cpp
)

add_library(anaf_io STATIC ${ANAF_IO_SOURCES})
target_include_directories(anaf_io PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(anaf_io
    PRIVATE
        Gmsh::Gmsh
        ZLIB::ZLIB
        ${ANAFINEN_HDF5_TARGET}
        project_warnings_and_optimizations
)

set(ANAF_CORE_SOURCES
    src/log/anaf_info.cpp
    src/directory/getExecutableDirectory.cpp
    src/platform/systemInfo.cpp
    src/material/materialLibrary.cpp
    src/objectCalcs/common/supportBasis.cpp
    src/objectCalcs/truss_1D/trussProperties/node.cpp
    src/objectCalcs/truss_1D/trussIO/trussMeshAdapter.cpp
    src/objectCalcs/truss_1D/trussTypes/simpleQuadranglePrismTrussCreate.cpp
    src/objectCalcs/truss_1D/trussTypes/trussLibrary.cpp
    src/objectCalcs/truss_1D/trussEngine/trussSolver/deformationUnderConstForce.cpp
    src/solvers/direct/solver_cholmod.cpp
    src/solvers/direct/solver_simplicial.cpp
    src/solvers/iterative/solver_iterative.cpp
    src/solvers/solver_referee.cpp
    src/objectCalcs/truss_1D/trussEngine/trussSolver.cpp
    src/objectCalcs/beam/beamProperties/node.cpp
    src/objectCalcs/beam/beamSection/beamSection.cpp
    src/objectCalcs/beam/beamSection/sectionLibrary.cpp
    src/objectCalcs/beam/beamSection/sectionStress.cpp
    src/objectCalcs/beam/beamSection/sectionTriangulation.cpp
    src/objectCalcs/beam/beamEngine/beamSolver/deformationUnderConstForce.cpp
    src/objectCalcs/beam/beamEngine/beamSolver.cpp
    src/objectCalcs/beam/beamEngine/beamDiagrams.cpp
    src/objectCalcs/beam/beamEngine/beamStress.cpp
    src/objectCalcs/beam/beamIO/beamMeshAdapter.cpp
    src/objectCalcs/beam/beamTypes/beamLibrary.cpp
)

add_library(anaf_core STATIC ${ANAF_CORE_SOURCES})
target_include_directories(anaf_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/objectCalcs
    ${CMAKE_CURRENT_SOURCE_DIR}/src/objectCalcs/truss_1D
)

if(ANAFINEN_HAS_CHOLMOD)
    target_compile_definitions(anaf_core PRIVATE ANAFINEN_HAS_CHOLMOD)
    target_include_directories(anaf_core PRIVATE ${CHOLMOD_INCLUDE_DIR})
    target_link_libraries(anaf_core PRIVATE ${CHOLMOD_LIBRARIES})
endif()

if(SPECTRA_TARGET)
    target_link_libraries(anaf_core PUBLIC ${SPECTRA_TARGET})
endif()

target_link_libraries(anaf_core PRIVATE nlohmann_json::nlohmann_json)

# findAssetPath() falls back to the source tree (development builds, tests).
target_compile_definitions(anaf_core PRIVATE MAIN_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
if(WIN32)
    target_link_libraries(anaf_core PUBLIC shell32 ole32 uuid dxgi advapi32) # getUserConfigDirectory(), systemInfo
endif()

target_link_libraries(anaf_core PUBLIC
    anaf_io
    Eigen3::Eigen
    project_warnings_and_optimizations
)

# Front-end state shared by every front end (GUI / CLI): model, materials, sections, results.
# Kept out of anaf_core, which stays a stateless FEM layer; no GUI dependency.
set(ANAF_BRIDGE_SOURCES
    src/bridge/generalStatus.cpp
)

add_library(anaf_bridge STATIC ${ANAF_BRIDGE_SOURCES})
target_link_libraries(anaf_bridge
    PUBLIC
        anaf_core
    PRIVATE
        project_warnings_and_optimizations
)

set(ANAFINEN_SOURCES
    src/main.cpp
    src/gui/gui.cpp
    src/gui/guiMaterials/imGuiLayer.cpp
    src/gui/guiMaterials/theme.cpp
    src/gui/guiMaterials/framebuffer.cpp
    src/gui/guiMaterials/shaderProgram.cpp
    src/gui/panels/logTerminal.cpp
    src/gui/panels/mainDockSpaceHost.cpp
    src/gui/panels/materialHandler.cpp
    src/gui/panels/modelTree.cpp
    src/gui/panels/analysisSelector.cpp
    src/gui/panels/truss/trussWorker.cpp
    src/gui/panels/truss/simpleQuadrangleTruss/trussControlPanel.cpp
    src/gui/panels/truss/importedTruss/trussModelEditor.cpp
    src/gui/panels/beam/beamWorker.cpp
    src/gui/panels/beam/beamModelEditor.cpp
    src/gui/panels/beam/sectionHandler.cpp
    src/gui/panels/beam/beamDiagramPanel.cpp
    src/gui/panels/viewportPanel.cpp
    src/gui/panels/viewportToolbar.cpp
    src/gui/panels/viewportRenderer.cpp
    src/gui/panels/beamSceneRenderer.cpp
    src/gui/panels/fileIoPanel.cpp
    src/gui/panels/aboutPanel.cpp
    src/gui/panels/welcomePanel.cpp
    src/gui/panels/statusBar.cpp
    src/platform/resourceMonitor.cpp
    src/gui/fileDialogs/nativeFileDialog.cpp
)

add_executable(anafinen ${ANAFINEN_SOURCES})

# Embed the application icon into the .exe (Explorer, taskbar, shortcuts); the window icon
# itself is set at run time from assets/icons/anafinen.png.
if(WIN32)
    target_sources(anafinen PRIVATE src/anafinen.rc)
endif()

# GUI subsystem on Windows: no console window opens with the application. Debug keeps the console
# for the anaf_info log (std::cout). main() stays the entry point, so no WinMain is needed.
if(WIN32)
    if(MSVC)
        target_link_options(anafinen PRIVATE
            "/SUBSYSTEM:$<IF:$<CONFIG:Debug>,CONSOLE,WINDOWS>"
            "/ENTRY:mainCRTStartup"
        )
    else()
        target_link_options(anafinen PRIVATE "$<$<NOT:$<CONFIG:Debug>>:-mwindows>")
    endif()
endif()

target_compile_definitions(anafinen PRIVATE
    MAIN_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
    ANAFINEN_VERSION="${PROJECT_VERSION}-alpha"
    GLFW_INCLUDE_NONE
)

if(MSVC)
    target_compile_options(anaf_core PUBLIC /openmp)
    target_compile_options(anafinen PUBLIC /openmp)
else()
    target_link_libraries(anaf_core PUBLIC OpenMP::OpenMP_CXX)
endif()

target_link_libraries(anafinen PRIVATE
    anaf_bridge
    ${GUI_DEPENDENCIES}
    ${GLAD_TARGET}
    ${GLFW_TARGET}
    OpenGL::GL
    glm::glm
    PNG::PNG
    project_warnings_and_optimizations
)

target_include_directories(anafinen PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/gui
)

# Native file chooser (header-only, WTFPL): Windows common dialogs, zenity / kdialog on Linux.
target_include_directories(anafinen SYSTEM PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/external/portable-file-dialogs)
if(WIN32)
    target_link_libraries(anafinen PRIVATE ole32 comdlg32 shell32 uuid psapi) # psapi: resourceMonitor
endif()

add_custom_command(
    TARGET anafinen POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/assets
        $<TARGET_FILE_DIR:anafinen>/assets
)

# The POST_BUILD copy above runs only when anafinen relinks; keep the editable
# material library and section catalogue current on every build.
add_custom_target(anafinen_material_library ALL
    COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:anafinen>/assets/bridge
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_SOURCE_DIR}/assets/bridge/materialProperties.json
        ${CMAKE_CURRENT_SOURCE_DIR}/assets/bridge/sectionCatalog.json
        $<TARGET_FILE_DIR:anafinen>/assets/bridge/
    COMMENT "Syncing assets/bridge/materialProperties.json and sectionCatalog.json"
)

# Window icons rendered at configure time (or the committed fallbacks, see cmake/Dependencies.cmake)
add_custom_command(
    TARGET anafinen POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${ANAFINEN_ICON_PNG}"
        "$<TARGET_FILE_DIR:anafinen>/assets/icons/anafinen.png"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${ANAFINEN_ICON_PNG_32}"
        "$<TARGET_FILE_DIR:anafinen>/assets/icons/anafinen-32.png"
)

# Runtime DLL copy for local build execution
if(WIN32)
    add_custom_command(
        TARGET anafinen POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${GMSH_DLL}"
            $<TARGET_FILE_DIR:anafinen>
    )
endif()

# Installation rules for executable and assets
# Install layout per platform (binary, assets, licenses) lives in cmake/Packaging.cmake.

# Install runtime DLL dependencies (vcpkg: libpng, zlib, HDF5, SuiteSparse, ...) into the package root.
if(WIN32)
    install(TARGETS anafinen
        RUNTIME_DEPENDENCIES
            DIRECTORIES
                "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin"
            PRE_EXCLUDE_REGEXES
                "api-ms-win-.*"
                "ext-ms-win-.*"
            POST_EXCLUDE_REGEXES
                ".*[Ss][Yy][Ss][Tt][Ee][Mm]32.*"
                ".*[Ww][Ii][Nn][Dd][Oo][Ww][Ss].*"
        DESTINATION .
    )
    if(EXISTS "${GMSH_DLL}")
        install(FILES "${GMSH_DLL}" DESTINATION .)
    endif()
endif()

if(ANAFINEN_BUILD_CLI)
    add_subdirectory(src/cli)
endif()

if(ANAFINEN_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

include(Packaging)
