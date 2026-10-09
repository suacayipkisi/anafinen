# Generators of the committed built-in libraries (enabled with -DANAFINEN_BUILD_TOOLS=ON).
# Run them after editing trussLibrary.cpp / beamLibrary.cpp and commit the regenerated files;
# never edit the files under assets/objects/ by hand.

# Regenerates assets/objects/beam/beam3D (built-in beam library, models and solved results) from beamLibrary.cpp.
add_executable(anaf_beam_library_tool beamLibraryTool.cpp)
target_compile_definitions(anaf_beam_library_tool PRIVATE MAIN_DIR="${PROJECT_SOURCE_DIR}")
target_link_libraries(anaf_beam_library_tool PRIVATE anaf_core project_warnings_and_optimizations)

# Regenerates assets/objects/truss/truss1D (built-in truss library) from trussLibrary.cpp.
add_executable(anaf_truss_library_tool trussLibraryTool.cpp)
target_compile_definitions(anaf_truss_library_tool PRIVATE MAIN_DIR="${PROJECT_SOURCE_DIR}")
target_link_libraries(anaf_truss_library_tool PRIVATE anaf_core project_warnings_and_optimizations)

# Gmsh SDK DLL next to the executables (vcpkg's applocal step copies only vcpkg DLLs).
if(WIN32)
    foreach(_target anaf_beam_library_tool anaf_truss_library_tool)
        add_custom_command(TARGET ${_target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${GMSH_DLL}" $<TARGET_FILE_DIR:${_target}>
        )
    endforeach()
endif()
