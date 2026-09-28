find_package(Eigen3 CONFIG REQUIRED)
find_package(OpenMP REQUIRED)
find_package(OpenGL REQUIRED)
find_package(ZLIB REQUIRED)
find_package(PNG REQUIRED)

# nlohmann/json (header-only, MIT): material library file. The system / vcpkg package is used
# when present; otherwise (e.g. the MinGW cross-build sysroot) the pinned release is fetched.
find_package(nlohmann_json 3.11 CONFIG QUIET)
if(NOT TARGET nlohmann_json::nlohmann_json)
    include(FetchContent)
    FetchContent_Declare(nlohmann_json
        URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
        URL_HASH SHA256=42f6e95cad6ec532fd372391373363b62a14af6d771056dbfc86160e6dfff7aa
    )
    FetchContent_MakeAvailable(nlohmann_json)
    message(STATUS "nlohmann_json not found, fetched v3.12.0")
endif()
# SuiteSparse & CHOLMOD detection (single source of truth for ANAFINEN_HAS_CHOLMOD)
# find_package(CHOLMOD CONFIG) is intentionally not used: Fedora's suitesparse-devel
# ships CAMD/CCOLAMD configs that include missing *Targets_static.cmake files, which
# is a hard configure error even with QUIET. The manual fallback below covers it.
find_package(SuiteSparse CONFIG QUIET)

if(TARGET SuiteSparse::CHOLMOD)
    set(ANAFINEN_HAS_CHOLMOD ON)
    set(CHOLMOD_LIBRARIES SuiteSparse::CHOLMOD)
    message(STATUS "CHOLMOD found via CMake Config: SuiteSparse::CHOLMOD")
else()
    find_path(CHOLMOD_INCLUDE_DIR NAMES cholmod.h 
        PATHS "C:/vcpkg/installed/x64-windows/include" 
        PATH_SUFFIXES suitesparse
    )
    find_library(CHOLMOD_LIB NAMES cholmod 
        PATHS "C:/vcpkg/installed/x64-windows/lib"
    )
    find_library(SUITESPARSE_CONFIG_LIB NAMES suitesparseconfig 
        PATHS "C:/vcpkg/installed/x64-windows/lib"
    )

    if(CHOLMOD_INCLUDE_DIR AND CHOLMOD_LIB)
        set(ANAFINEN_HAS_CHOLMOD ON)
        set(CHOLMOD_LIBRARIES ${CHOLMOD_LIB} ${SUITESPARSE_CONFIG_LIB})
        message(STATUS "CHOLMOD found: ${CHOLMOD_LIBRARIES}")
    else()
        set(ANAFINEN_HAS_CHOLMOD OFF)
        message(STATUS "CHOLMOD not found, falling back to built-in Eigen solvers")
    endif()
endif()

# Win32 OpenGL target check
if(WIN32 AND NOT TARGET OpenGL::GL)
    add_library(OpenGL::GL INTERFACE IMPORTED)
    set_target_properties(OpenGL::GL PROPERTIES
        INTERFACE_LINK_LIBRARIES "opengl32"
    )
endif()

# Icon conversion
# rsvg-convert (librsvg) is preferred: ImageMagick without an rsvg delegate (e.g. Debian) falls back to
# its internal MSVG renderer, which draws only the icon background and still exits with 0.
find_program(ANAFINEN_SVG_RENDERER NAMES rsvg-convert)
find_program(ANAFINEN_IMAGE_CONVERTER NAMES magick convert)
set(ANAFINEN_GENERATED_ASSETS_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated-assets")
file(MAKE_DIRECTORY "${ANAFINEN_GENERATED_ASSETS_DIR}/icons")

# ANAFINEN_ICON_PNG is empty when no PNG could be made; the install rules and the POST_BUILD
# copy skip the icon then (the window starts without an icon, the SVG is still installed).
set(ANAFINEN_ICON_PNG "")
set(_anafinen_icon_png "${ANAFINEN_GENERATED_ASSETS_DIR}/icons/anafinen.png")

if(ANAFINEN_SVG_RENDERER)
    execute_process(
        COMMAND "${ANAFINEN_SVG_RENDERER}" --width 128 --height 128 --keep-aspect-ratio
                --output "${_anafinen_icon_png}" "${CMAKE_CURRENT_SOURCE_DIR}/assets/icons/anafinen.svg"
        RESULT_VARIABLE ANAFINEN_ICON_CONVERSION_RESULT
    )
elseif(ANAFINEN_IMAGE_CONVERTER)
    execute_process(
        COMMAND "${ANAFINEN_IMAGE_CONVERTER}" "${CMAKE_CURRENT_SOURCE_DIR}/assets/icons/anafinen.svg"
                -resize 128x128 "${_anafinen_icon_png}"
        RESULT_VARIABLE ANAFINEN_ICON_CONVERSION_RESULT
    )
endif()
if(DEFINED ANAFINEN_ICON_CONVERSION_RESULT AND ANAFINEN_ICON_CONVERSION_RESULT EQUAL 0)
    set(ANAFINEN_ICON_PNG "${_anafinen_icon_png}")
endif()

if(NOT ANAFINEN_ICON_PNG)
    # Never ship a stale PNG from an earlier configure, nor the SVG under a .png name.
    file(REMOVE "${_anafinen_icon_png}")
    message(WARNING "Neither rsvg-convert nor ImageMagick (magick / convert) could convert assets/icons/anafinen.svg; "
                    "the build and packages have no PNG application icon. Install librsvg (rsvg-convert).")
endif()

# Gmsh SDK integration
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

# Spectra
set(SPECTRA_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/spectra")
set(SPECTRA_TARGET "")
if(EXISTS "${SPECTRA_DIR}/include/Spectra/SymEigsSolver.h")
    add_library(spectra_local INTERFACE)
    target_include_directories(spectra_local SYSTEM INTERFACE "${SPECTRA_DIR}/include")
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

# GLM
find_package(glm CONFIG QUIET)
if(NOT TARGET glm::glm)
    find_path(GLM_INCLUDE_DIR "glm/glm.hpp")
    if(GLM_INCLUDE_DIR)
        add_library(glm::glm INTERFACE IMPORTED)
        set_target_properties(glm::glm PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES "${GLM_INCLUDE_DIR}"
        )
    else()
        message(FATAL_ERROR "GLM headers not found!")
    endif()
endif()
