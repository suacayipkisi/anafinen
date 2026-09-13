cmake_minimum_required(VERSION 3.20...3.31)

if(POLICY CMP0207)
    cmake_policy(SET CMP0207 NEW)
endif()

project(anafinen VERSION 0.1.1 LANGUAGES C CXX)

set(CMAKE_INTERPROCEDURAL_OPTIMIZATION FALSE)
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE FALSE)

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
    src/truss_1D/trussEngine/trussSolver/deformationUnderConstForce.cpp
    src/truss_1D/trussEngine/trussSolver.cpp
)

add_library(anaf_core STATIC ${ANAF_CORE_SOURCES})
target_include_directories(anaf_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/truss_1D
)

if(ANAFINEN_HAS_CHOLMOD)
    target_compile_definitions(anaf_core PRIVATE ANAFINEN_HAS_CHOLMOD)
    target_include_directories(anaf_core PRIVATE ${CHOLMOD_INCLUDE_DIR})
    target_link_libraries(anaf_core PRIVATE ${CHOLMOD_LIBRARIES})
endif()

if(SPECTRA_TARGET)
    target_link_libraries(anaf_core PUBLIC ${SPECTRA_TARGET})
endif()

target_link_libraries(anaf_core PUBLIC
    Eigen3::Eigen
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

if(MSVC)
    target_compile_options(anaf_core PUBLIC /openmp:llvm)
    target_compile_options(anafinen PUBLIC /openmp:llvm)
else()
    target_link_libraries(anaf_core PUBLIC OpenMP::OpenMP_CXX)
endif()

target_link_libraries(anafinen PRIVATE
    anaf_core
    ${GUI_DEPENDENCIES}
    ${GLAD_TARGET}
    ${GLFW_TARGET}
    OpenGL::GL
    glm::glm
    PNG::PNG
    project_warnings_and_optimizations
)

target_include_directories(anafinen PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/bridge
    ${CMAKE_CURRENT_SOURCE_DIR}/src/directory
    ${CMAKE_CURRENT_SOURCE_DIR}/src/gen
    ${CMAKE_CURRENT_SOURCE_DIR}/src/log
    ${CMAKE_CURRENT_SOURCE_DIR}/src/gui
    ${CMAKE_CURRENT_SOURCE_DIR}/src/gui/panels
    ${CMAKE_CURRENT_SOURCE_DIR}/src/gui/guiMaterials
)

add_custom_command(
    TARGET anafinen POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/assets
        $<TARGET_FILE_DIR:anafinen>/assets
)

# copy icon if it exists
if(EXISTS "${ANAFINEN_GENERATED_ASSETS_DIR}/icons/anafinen.png")
    add_custom_command(
        TARGET anafinen POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${ANAFINEN_GENERATED_ASSETS_DIR}/icons/anafinen.png"
            "$<TARGET_FILE_DIR:anafinen>/assets/icons/anafinen.png"
    )
endif()

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
install(TARGETS anafinen DESTINATION .)
install(DIRECTORY assets/ DESTINATION ./assets)

# Install runtime DLL dependencies into the package root
if(WIN32)
    install(TARGETS anafinen
        RUNTIME_DEPENDENCIES
            DIRECTORIES 
                "C:/vcpkg/installed/x64-windows/bin"
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

include(Packaging)
