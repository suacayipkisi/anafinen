set(EXTERNAL_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external")
set(IMGUI_DIR    "${EXTERNAL_DIR}/imgui")
set(IMGUIZMO_DIR "${EXTERNAL_DIR}/ImGuizmo")
set(IMPLOT_DIR   "${EXTERNAL_DIR}/implot")
set(GLAD_DIR     "${EXTERNAL_DIR}/glad")

# GLAD
add_library(glad_local STATIC "${GLAD_DIR}/src/gl.c")
target_include_directories(glad_local PUBLIC "${GLAD_DIR}/include")
set(GLAD_TARGET glad_local)

# GLFW
set(GLFW_TARGET "")

if(EXISTS "${EXTERNAL_DIR}/glfw/CMakeLists.txt")
    set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
    add_subdirectory("${EXTERNAL_DIR}/glfw" EXCLUDE_FROM_ALL)
    set(GLFW_TARGET glfw)
else()
    # try CMake Config mode
    find_package(glfw3 CONFIG QUIET)
    if(TARGET glfw)
        set(GLFW_TARGET glfw)
    elseif(TARGET glfw3::glfw3)
        set(GLFW_TARGET glfw3::glfw3)
    endif()

    # try PkgConfig mode
    if(NOT GLFW_TARGET)
        find_package(PkgConfig QUIET)
        if(PKG_CONFIG_FOUND)
            pkg_check_modules(GLFW QUIET IMPORTED_TARGET glfw3)
            if(TARGET PkgConfig::GLFW)
                set(GLFW_TARGET PkgConfig::GLFW)
            endif()
        endif()
    endif()

    # 3. fallback to fetchcontent if no local library or system package exists
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
