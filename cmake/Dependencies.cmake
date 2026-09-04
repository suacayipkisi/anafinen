if(WIN32)
    # OpenMP for MinGW GCC
    if(NOT TARGET OpenMP::OpenMP_CXX)
        add_library(OpenMP::OpenMP_CXX INTERFACE IMPORTED)
        set_target_properties(OpenMP::OpenMP_CXX PROPERTIES
            INTERFACE_COMPILE_OPTIONS "-fopenmp"
            INTERFACE_LINK_LIBRARIES "-lgomp"
        )
    endif()

    # Eigen3 from MinGW sysroot directly
    if(NOT TARGET Eigen3::Eigen)
        add_library(Eigen3::Eigen INTERFACE IMPORTED)
        set_target_properties(Eigen3::Eigen PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES "/usr/x86_64-w64-mingw32/sys-root/mingw/include/eigen3"
        )
    endif()

    set(OPENGL_LIBRARIES opengl32)
    if(NOT TARGET OpenGL::GL)
        add_library(OpenGL::GL INTERFACE IMPORTED)
        set_target_properties(OpenGL::GL PROPERTIES
            INTERFACE_LINK_LIBRARIES "opengl32"
        )
    endif()
else()
    find_package(Eigen3 REQUIRED)
    find_package(OpenMP REQUIRED)
    find_package(OpenGL REQUIRED)
endif()

find_package(PNG REQUIRED)

# icon conversion
find_program(ANAFINEN_IMAGE_CONVERTER NAMES magick convert REQUIRED)
set(ANAFINEN_GENERATED_ASSETS_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated-assets")
file(MAKE_DIRECTORY "${ANAFINEN_GENERATED_ASSETS_DIR}/icons")
execute_process(
    COMMAND "${ANAFINEN_IMAGE_CONVERTER}" "${CMAKE_CURRENT_SOURCE_DIR}/assets/icons/anafinen.svg"
            -resize 128x128 "${ANAFINEN_GENERATED_ASSETS_DIR}/icons/anafinen.png"
    RESULT_VARIABLE ANAFINEN_ICON_CONVERSION_RESULT
)
if(NOT ANAFINEN_ICON_CONVERSION_RESULT EQUAL 0)
    message(FATAL_ERROR "Failed to convert the application SVG icon to PNG")
endif()

# gmsh 
if(WIN32)
    set(GMSH_SDK_DIR "C:/libs/gmsh-sdk" CACHE PATH "Path to Gmsh SDK on Windows")
    find_path(GMSH_INCLUDE_DIR NAMES "gmsh.h" HINTS "${GMSH_SDK_DIR}/include" NO_DEFAULT_PATH)

    find_library(GMSH_LIBRARY NAMES gmsh.dll gmsh HINTS "${GMSH_SDK_DIR}/lib" NO_DEFAULT_PATH)
    file(GLOB GMSH_DLL_FILES LIST_DIRECTORIES false
        "${GMSH_SDK_DIR}/bin/*.dll"
        "${GMSH_SDK_DIR}/lib/*.dll"
    )

    list(FILTER GMSH_DLL_FILES EXCLUDE REGEX "\\.lib$")

    if(GMSH_DLL_FILES)
        list(GET GMSH_DLL_FILES 0 _DETECTED_DLL)
        set(GMSH_DLL "${_DETECTED_DLL}" CACHE FILEPATH "Path to Gmsh runtime DLL" FORCE)
    endif()
else()
    find_path(GMSH_INCLUDE_DIR NAMES "gmsh.h")
    find_library(GMSH_LIBRARY NAMES gmsh)
endif()

if(NOT GMSH_INCLUDE_DIR OR NOT GMSH_LIBRARY OR (WIN32 AND NOT GMSH_DLL))
    message(STATUS "GMSH_INCLUDE_DIR: ${GMSH_INCLUDE_DIR}")
    message(STATUS "GMSH_LIBRARY: ${GMSH_LIBRARY}")
    message(STATUS "GMSH_DLL: ${GMSH_DLL}")
    message(FATAL_ERROR "Gmsh SDK or headers not found! Set GMSH_SDK_DIR properly")
endif()

add_library(Gmsh::Gmsh SHARED IMPORTED)
set_target_properties(Gmsh::Gmsh PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${GMSH_INCLUDE_DIR}"
)

if(WIN32)
    set_target_properties(Gmsh::Gmsh PROPERTIES
        IMPORTED_IMPLIB "${GMSH_LIBRARY}"
        IMPORTED_LOCATION "${GMSH_DLL}"
    )
else()
    set_target_properties(Gmsh::Gmsh PROPERTIES
        IMPORTED_LOCATION "${GMSH_LIBRARY}"
    )
endif()

# spectra
set(SPECTRA_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/spectra")
set(SPECTRA_TARGET "")
if(EXISTS "${SPECTRA_DIR}/include/Spectra/SymEigsSolver.h")
    add_library(spectra_local INTERFACE)
    target_include_directories(spectra_local INTERFACE "${SPECTRA_DIR}/include")
    set(SPECTRA_TARGET spectra_local)
else()
    find_package(Spectra CONFIG QUIET)
    find_package(spectra CONFIG QUIET)
    if(TARGET Spectra::Spectra)
        set(SPECTRA_TARGET Spectra::Spectra)
    elseif(TARGET spectra::spectra)
        set(SPECTRA_TARGET spectra::spectra)
    elseif(TARGET spectra)
        set(SPECTRA_TARGET spectra)
    endif()
endif()

find_package(glm CONFIG QUIET)
if(NOT TARGET glm::glm)
    find_path(GLM_INCLUDE_DIR "glm/glm.hpp")
    if(GLM_INCLUDE_DIR)
        add_library(glm::glm INTERFACE IMPORTED)
        set_target_properties(glm::glm PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES "${GLM_INCLUDE_DIR}"
        )
    else()
        message(FATAL_ERROR "GLM headers not found in MinGW sysroot!")
    endif()
endif()
