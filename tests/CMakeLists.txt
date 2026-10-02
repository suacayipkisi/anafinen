# anaf_io test suite (enabled with -DANAFINEN_BUILD_TESTS=ON).

add_executable(anaf_io_tests ioTests.cpp)
# Gmsh is linked directly to generate fixtures (meshes, CAD files) written by Gmsh itself.
target_link_libraries(anaf_io_tests PRIVATE anaf_io Gmsh::Gmsh project_warnings_and_optimizations)
add_test(NAME anaf_io_tests COMMAND anaf_io_tests)

add_executable(anaf_io_tool ioTool.cpp)
target_link_libraries(anaf_io_tool PRIVATE anaf_io project_warnings_and_optimizations)

# Cross-check against the reference VTK implementation when its Python bindings are available.
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
    execute_process(
        COMMAND ${Python3_EXECUTABLE} -c "import vtk"
        RESULT_VARIABLE ANAFINEN_PYTHON_VTK_MISSING
        OUTPUT_QUIET ERROR_QUIET
    )
    if(ANAFINEN_PYTHON_VTK_MISSING EQUAL 0)
        add_test(NAME vtk_reference_check
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/vtkReferenceCheck.py
                    $<TARGET_FILE:anaf_io_tool> ${CMAKE_CURRENT_BINARY_DIR}/vtk_reference)
        message(STATUS "anaf_io tests: VTK reference check enabled")
    else()
        message(STATUS "anaf_io tests: Python 'vtk' module not found, VTK reference check skipped")
    endif()
endif()

# GUI data path without the GUI: solve -> snapshot -> adapter -> file -> adapter -> snapshot.
# anaf_core is self-contained (solver, adapter, materials, log, asset lookup). The GUI bridge
# is compiled in as well because its model reset and user-material handling are tested here.
# FEM core unit tests: linked against anaf_core only, which also keeps the core self-contained.
add_executable(anaf_core_tests coreTests.cpp)
target_link_libraries(anaf_core_tests PRIVATE anaf_core project_warnings_and_optimizations)
add_test(NAME anaf_core_tests COMMAND anaf_core_tests)

add_executable(anaf_truss_io_tests
    trussIoTests.cpp
    ${PROJECT_SOURCE_DIR}/src/bridge/generalStatus.cpp # the bridge itself is under test (resetModel, user materials)
)
# The tests read the committed library files and fixtures from the source tree.
target_compile_definitions(anaf_truss_io_tests PRIVATE MAIN_DIR="${PROJECT_SOURCE_DIR}")
target_link_libraries(anaf_truss_io_tests PRIVATE anaf_core project_warnings_and_optimizations)
if(ANAFINEN_HAS_CHOLMOD)
    target_compile_definitions(anaf_truss_io_tests PRIVATE ANAFINEN_HAS_CHOLMOD)
endif()
add_test(NAME anaf_truss_io_tests COMMAND anaf_truss_io_tests)

# Regenerates assets/objects/truss/truss1D (built-in truss library) from trussLibrary.cpp.
# anaf_truss_io_tests fails when the committed files differ from the generator.
add_executable(anaf_truss_library_tool trussLibraryTool.cpp)
target_compile_definitions(anaf_truss_library_tool PRIVATE MAIN_DIR="${PROJECT_SOURCE_DIR}")
target_link_libraries(anaf_truss_library_tool PRIVATE anaf_core project_warnings_and_optimizations)
