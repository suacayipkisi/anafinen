find_package(Eigen3 CONFIG REQUIRED)
find_package(OpenMP REQUIRED)
find_package(OpenGL REQUIRED)
find_package(ZLIB REQUIRED)
find_package(PNG REQUIRED)
# SuiteSparse & CHOLMOD detection
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
find_program(ANAFINEN_IMAGE_CONVERTER NAMES magick convert)
set(ANAFINEN_GENERATED_ASSETS_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated-assets")
file(MAKE_DIRECTORY "${ANAFINEN_GENERATED_ASSETS_DIR}/icons")

set(CONVERSION_SUCCESS FALSE)

if(ANAFINEN_IMAGE_CONVERTER)
    execute_process(
        COMMAND "${ANAFINEN_IMAGE_CONVERTER}" "${CMAKE_CURRENT_SOURCE_DIR}/assets/icons/anafinen.svg"
                -resize 128x128 "${ANAFINEN_GENERATED_ASSETS_DIR}/icons/anafinen.png"
        RESULT_VARIABLE ANAFINEN_ICON_CONVERSION_RESULT
    )
    if(ANAFINEN_ICON_CONVERSION_RESULT EQUAL 0)
        set(CONVERSION_SUCCESS TRUE)
    endif()
endif()

# Icon creation fallback
if(NOT CONVERSION_SUCCESS)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/assets/icons/anafinen.svg")
        file(COPY_FILE
            "${CMAKE_CURRENT_SOURCE_DIR}/assets/icons/anafinen.svg"
            "${ANAFINEN_GENERATED_ASSETS_DIR}/icons/anafinen.png"
        )
    else()
        message(WARNING "Neither ImageMagick nor fallback anafinen.png was found")
    endif()
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

# CHOLMOD / SuiteSparse detection
find_package(SuiteSparse QUIET)

if(SuiteSparse_FOUND OR TARGET SuiteSparse::CHOLMOD)
    set(ANAFINEN_HAS_CHOLMOD ON)
    if(TARGET SuiteSparse::CHOLMOD)
        set(CHOLMOD_LIBRARIES SuiteSparse::CHOLMOD)
    endif()
else()
    # Fallback to manual find if Config mode is not used
    find_path(CHOLMOD_INCLUDE_DIR NAMES cholmod.h)
    find_library(CHOLMOD_LIBRARIES NAMES cholmod)
    if(CHOLMOD_INCLUDE_DIR AND CHOLMOD_LIBRARIES)
        set(ANAFINEN_HAS_CHOLMOD ON)
    else()
        set(ANAFINEN_HAS_CHOLMOD OFF)
    endif()
endif()
