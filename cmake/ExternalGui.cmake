set(EXTERNAL_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external")
set(IMGUI_DIR    "${EXTERNAL_DIR}/imgui")
set(IMGUIZMO_DIR "${EXTERNAL_DIR}/ImGuizmo")
set(IMPLOT_DIR   "${EXTERNAL_DIR}/implot")
set(GLAD_DIR     "${EXTERNAL_DIR}/glad")

# GLAD
add_library(glad_local STATIC "${GLAD_DIR}/src/gl.c")
target_include_directories(glad_local SYSTEM PUBLIC "${GLAD_DIR}/include")
set(GLAD_TARGET glad_local)

# GLFW
#
# external/glfw is the GLFW 3.4 submodule, kept as released; external/patches/glfw-3.4-anafinen.patch
# adds glfwDragWindow / glfwResizeWindowFromEdge / glfwShowWindowMenu (Wayland xdg_toplevel move /
# resize / window menu, X11 _NET_WM_MOVERESIZE, Win32) for the integrated title bar. The submodule is
# copied into the build tree and patched there, so its checkout stays clean; the copy is redone when
# the submodule commit or the patch changes.
option(ANAFINEN_SYSTEM_GLFW
    "Link the system GLFW instead of external/glfw + the anafinen patch (no integrated title bar on Linux)" OFF)

set(GLFW_TARGET "")
set(GLFW_PATCH_FILE "${EXTERNAL_DIR}/patches/glfw-3.4-anafinen.patch")

if(NOT ANAFINEN_SYSTEM_GLFW)
    if(NOT EXISTS "${EXTERNAL_DIR}/glfw/CMakeLists.txt")
        message(FATAL_ERROR "external/glfw is missing. Run: git submodule update --init external/glfw "
                            "(or configure with -DANAFINEN_SYSTEM_GLFW=ON to use the system GLFW).")
    endif()
    find_package(Git REQUIRED)

    set(GLFW_PATCHED_DIR "${CMAKE_BINARY_DIR}/_deps/glfw-patched")
    set(GLFW_PATCH_STAMP "${CMAKE_BINARY_DIR}/_deps/glfw-patched.stamp")
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" -C "${EXTERNAL_DIR}/glfw" rev-parse HEAD
        OUTPUT_VARIABLE _glfw_revision OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET RESULT_VARIABLE _glfw_rev_result)
    if(NOT _glfw_rev_result EQUAL 0)
        # Source tree without git metadata (an exported archive): hash the version header instead.
        file(SHA256 "${EXTERNAL_DIR}/glfw/include/GLFW/glfw3.h" _glfw_revision)
    endif()
    file(SHA256 "${GLFW_PATCH_FILE}" _glfw_patch_hash)
    set(_glfw_stamp_value "${_glfw_revision} ${_glfw_patch_hash}")
    set(_glfw_old_stamp "")
    if(EXISTS "${GLFW_PATCH_STAMP}")
        file(READ "${GLFW_PATCH_STAMP}" _glfw_old_stamp)
    endif()

    if(NOT _glfw_old_stamp STREQUAL _glfw_stamp_value OR NOT EXISTS "${GLFW_PATCHED_DIR}/CMakeLists.txt")
        message(STATUS "Patching GLFW (${GLFW_PATCH_FILE})")
        file(REMOVE_RECURSE "${GLFW_PATCHED_DIR}")
        file(COPY "${EXTERNAL_DIR}/glfw/" DESTINATION "${GLFW_PATCHED_DIR}"
            PATTERN ".git" EXCLUDE
            PATTERN "docs" EXCLUDE
            PATTERN "examples" EXCLUDE
            PATTERN "tests" EXCLUDE)
        # The build tree may sit inside the anafinen checkout: the ceiling keeps git from treating the
        # copy as part of that repository, so "git apply" patches the copy like plain patch(1).
        execute_process(
            COMMAND "${CMAKE_COMMAND}" -E env "GIT_CEILING_DIRECTORIES=${CMAKE_BINARY_DIR}/_deps"
                    "${GIT_EXECUTABLE}" apply --whitespace=nowarn "${GLFW_PATCH_FILE}"
            WORKING_DIRECTORY "${GLFW_PATCHED_DIR}"
            RESULT_VARIABLE _glfw_patch_result
            ERROR_VARIABLE _glfw_patch_error)
        if(NOT _glfw_patch_result EQUAL 0)
            file(REMOVE_RECURSE "${GLFW_PATCHED_DIR}")
            message(FATAL_ERROR "The GLFW patch does not apply to external/glfw (expected GLFW 3.4):\n${_glfw_patch_error}")
        endif()
        file(WRITE "${GLFW_PATCH_STAMP}" "${_glfw_stamp_value}")
    endif()
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${GLFW_PATCH_FILE}")

    set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
    # Always static: the patched GLFW is part of the executables and never installed. Fedora's %cmake
    # passes -DBUILD_SHARED_LIBS=ON, which made it a libglfw.so.3 that the dynamic loader then took
    # from the distribution's unpatched glfw ("undefined symbol: glfwResizeWindowFromEdge").
    # GLFW's option(BUILD_SHARED_LIBS) honours this normal variable (CMP0077); restored afterwards.
    set(_anafinen_build_shared_libs "${BUILD_SHARED_LIBS}")
    set(BUILD_SHARED_LIBS OFF)
    add_subdirectory("${GLFW_PATCHED_DIR}" "${CMAKE_BINARY_DIR}/_deps/glfw-build" EXCLUDE_FROM_ALL)
    set(BUILD_SHARED_LIBS "${_anafinen_build_shared_libs}")
    # Third-party headers are SYSTEM, like the other externals (add_subdirectory(... SYSTEM) needs CMake 3.25).
    get_target_property(_glfw_includes glfw INTERFACE_INCLUDE_DIRECTORIES)
    set_target_properties(glfw PROPERTIES INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${_glfw_includes}")
    set(GLFW_TARGET glfw)
else()
    find_package(glfw3 CONFIG QUIET)
    if(TARGET glfw)
        set(GLFW_TARGET glfw)
    elseif(TARGET glfw3::glfw3)
        set(GLFW_TARGET glfw3::glfw3)
    endif()

    if(NOT GLFW_TARGET)
        find_package(PkgConfig QUIET)
        if(PKG_CONFIG_FOUND)
            pkg_check_modules(GLFW QUIET IMPORTED_TARGET glfw3)
            if(TARGET PkgConfig::GLFW)
                set(GLFW_TARGET PkgConfig::GLFW)
            endif()
        endif()
    endif()
    message(WARNING "ANAFINEN_SYSTEM_GLFW=ON: the integrated title bar is not available on Linux.")
endif()

# ANAFINEN_SYSTEM_GLFW without a system GLFW: plain GLFW 3.4 from GitHub (also without the patch).
if(NOT GLFW_TARGET)
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

# ImGui 
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
    # SYSTEM: third-party headers do not trigger our warning flags.
    target_include_directories(imgui_suite SYSTEM PUBLIC
        ${IMGUI_DIR}
        ${IMGUI_DIR}/backends
        ${IMGUIZMO_DIR}
        ${IMGUIZMO_DIR}/src
        ${IMPLOT_DIR}
    )
    target_link_libraries(imgui_suite PUBLIC ${GLAD_TARGET} ${GLFW_TARGET} OpenGL::GL)
    target_compile_definitions(imgui_suite PUBLIC GLFW_INCLUDE_NONE)

    # FreeType rasterizes the UI fonts (hinted, sharper at small sizes than stb_truetype).
    # Without it ImGui falls back to stb_truetype and the GUI still builds.
    find_package(Freetype QUIET)
    if(TARGET Freetype::Freetype)
        target_sources(imgui_suite PRIVATE ${IMGUI_DIR}/misc/freetype/imgui_freetype.cpp)
        target_include_directories(imgui_suite SYSTEM PUBLIC ${IMGUI_DIR}/misc/freetype)
        target_link_libraries(imgui_suite PUBLIC Freetype::Freetype)
        # PUBLIC: every translation unit that includes imgui.h must see the same definition.
        target_compile_definitions(imgui_suite PUBLIC IMGUI_ENABLE_FREETYPE)
        message(STATUS "ImGui font rasterizer: FreeType ${FREETYPE_VERSION_STRING}")
    else()
        message(WARNING "FreeType not found: ImGui uses stb_truetype, UI text is softer. "
                        "Install freetype-devel / libfreetype-dev / freetype2 / vcpkg freetype.")
    endif()

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
