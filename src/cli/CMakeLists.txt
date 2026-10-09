# Command-line front end: anaf_bridge (+ anaf_core, anaf_io through it) only, no GUI dependency. The log's
# terminal sink is chosen at run time in main() (anaf::LOG::setConsoleOutput), never by macro.
#
# Compiler flags, OpenMP (/openmp is PUBLIC on anaf_core for MSVC, OpenMP::OpenMP_CXX elsewhere),
# Eigen and the include directories all come from anaf_core (through anaf_bridge) and
# project_warnings_and_optimizations.

# The commands live in the static library anaf_cli, so the test suite can drive them
# (anaf_cli_tests) without starting the executable; the executable adds main() only.
set(ANAF_CLI_SOURCES
    cli.cpp
    cliLoop.cpp
    session.cpp
    parsing/arguments.cpp
    commands/commandTable.cpp
    commands/output.cpp
    commands/generalCommands.cpp
    commands/modelCommands.cpp
    commands/materialCommands.cpp
    commands/editCommands.cpp
    commands/resultCommands.cpp
    operations/help.cpp
    operations/operations.cpp
    operations/licenses.cpp
    operations/sideCommands.cpp
)

add_library(anaf_cli STATIC ${ANAF_CLI_SOURCES})
target_link_libraries(anaf_cli PUBLIC
    anaf_bridge
    project_warnings_and_optimizations
)
target_compile_definitions(anaf_cli PRIVATE
    ANAFINEN_VERSION="${PROJECT_VERSION}-alpha"
)

set(ANAFINEN_CLI_SOURCES
    main_cli.cpp
)

add_executable(anafinen_cli ${ANAFINEN_CLI_SOURCES})
# Next to the GUI executable (the anafinen target's binary directory; multi-config generators still
# add the <Config> subdirectory to both), so the two share the copied assets/ folder and the DLLs.
get_target_property(ANAFINEN_GUI_BINARY_DIR anafinen BINARY_DIR)
set_target_properties(anafinen_cli PROPERTIES
    OUTPUT_NAME "anafinen-cli"
    RUNTIME_OUTPUT_DIRECTORY "${ANAFINEN_GUI_BINARY_DIR}"
)

target_compile_definitions(anafinen_cli PRIVATE
    ANAFINEN_VERSION="${PROJECT_VERSION}-alpha"
)

target_link_libraries(anafinen_cli PRIVATE
    anaf_cli
    project_warnings_and_optimizations
)

# Runtime DLL copy for local build execution (vcpkg's applocal step copies only vcpkg DLLs).
if(WIN32)
    add_custom_command(
        TARGET anafinen_cli POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${GMSH_DLL}"
            $<TARGET_FILE_DIR:anafinen_cli>
    )
endif()
