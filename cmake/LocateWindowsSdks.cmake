# Finds the Windows SDKs that sit outside any package manager (the vcpkg checkout, the Gmsh SDK)
# when the developer has not pointed CMake at them. Every drive's usual roots and the user profile
# are scanned, so a checkout on D: or E: is found as well as one on C:.
# Included before project(): only host-side commands are used here.

# Collects every directory matching one of the glob patterns (relative to the user profile or to a
# drive root) that contains <marker>. Order: user profile, then drives C: .. Z:.
function(anafinen_scan_drives out_var marker)
    set(_roots)
    if(DEFINED ENV{USERPROFILE})
        file(TO_CMAKE_PATH "$ENV{USERPROFILE}" _home)
        list(APPEND _roots "${_home}")
    endif()
    foreach(_drive C D E F G H I J K L M N O P Q R S T U V W X Y Z)
        if(IS_DIRECTORY "${_drive}:/")
            list(APPEND _roots "${_drive}:")
        endif()
    endforeach()

    set(_found)
    foreach(_root IN LISTS _roots)
        foreach(_pattern IN LISTS ARGN)
            file(GLOB _candidates LIST_DIRECTORIES true "${_root}/${_pattern}")
            foreach(_dir IN LISTS _candidates)
                if(EXISTS "${_dir}/${marker}")
                    list(APPEND _found "${_dir}")
                endif()
            endforeach()
        endforeach()
    endforeach()
    list(REMOVE_DUPLICATES _found)
    set(${out_var} "${_found}" PARENT_SCOPE)
endfunction()

# A vcpkg root is usable in classic mode only with an installed/ tree; this skips the copy bundled
# with Visual Studio, whose VCPKG_ROOT a Developer PowerShell exports but which has nothing installed.
function(_anafinen_is_vcpkg_root out_var dir)
    if(EXISTS "${dir}/scripts/buildsystems/vcpkg.cmake" AND IS_DIRECTORY "${dir}/installed")
        set(${out_var} TRUE PARENT_SCOPE)
    else()
        set(${out_var} FALSE PARENT_SCOPE)
    endif()
endfunction()

# vcpkg toolchain: only when none was given (command line, VS Code configureSettings, preset) and
# not in an MSYS2 / MinGW shell, whose compilers cannot use the x64-windows (MSVC) triplet.
option(ANAFINEN_AUTO_VCPKG "On Windows, locate the vcpkg toolchain when CMAKE_TOOLCHAIN_FILE is not set" ON)
if(CMAKE_HOST_WIN32 AND ANAFINEN_AUTO_VCPKG AND NOT CMAKE_TOOLCHAIN_FILE AND NOT DEFINED ENV{MSYSTEM})
    set(_vcpkg_candidates)
    if(DEFINED ENV{VCPKG_ROOT})
        file(TO_CMAKE_PATH "$ENV{VCPKG_ROOT}" _env_root)
        list(APPEND _vcpkg_candidates "${_env_root}")
    endif()
    find_program(_anafinen_vcpkg_exe NAMES vcpkg NO_CACHE)
    if(_anafinen_vcpkg_exe)
        get_filename_component(_exe_root "${_anafinen_vcpkg_exe}" DIRECTORY)
        list(APPEND _vcpkg_candidates "${_exe_root}")
    endif()
    anafinen_scan_drives(_scanned "scripts/buildsystems/vcpkg.cmake"
        vcpkg dev/vcpkg tools/vcpkg src/vcpkg libs/vcpkg)
    list(APPEND _vcpkg_candidates ${_scanned})

    foreach(_root IN LISTS _vcpkg_candidates)
        _anafinen_is_vcpkg_root(_ok "${_root}")
        if(_ok)
            set(CMAKE_TOOLCHAIN_FILE "${_root}/scripts/buildsystems/vcpkg.cmake"
                CACHE FILEPATH "vcpkg toolchain (auto-detected)")
            message(STATUS "vcpkg toolchain auto-detected: ${CMAKE_TOOLCHAIN_FILE}")
            break()
        endif()
    endforeach()
    if(NOT CMAKE_TOOLCHAIN_FILE)
        message(STATUS "No vcpkg checkout found (VCPKG_ROOT, PATH, <drive>:/vcpkg ...); "
                       "pass -DCMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/buildsystems/vcpkg.cmake")
    endif()
endif()

# Gmsh SDK: an explicit, valid GMSH_SDK_DIR wins; otherwise the GMSH_SDK_DIR environment variable,
# external/gmsh-sdk in the source tree and the scanned drives are tried. Called from Dependencies.cmake.
function(anafinen_locate_gmsh_sdk)
    if(EXISTS "${GMSH_SDK_DIR}/include/gmsh.h")
        return()
    endif()
    set(_candidates)
    if(DEFINED ENV{GMSH_SDK_DIR})
        file(TO_CMAKE_PATH "$ENV{GMSH_SDK_DIR}" _env_dir)
        list(APPEND _candidates "${_env_dir}")
    endif()
    list(APPEND _candidates "${CMAKE_SOURCE_DIR}/external/gmsh-sdk")
    anafinen_scan_drives(_scanned "include/gmsh.h"
        libs/gmsh-sdk "libs/gmsh*sdk*" "gmsh*sdk*" "dev/gmsh*sdk*" "tools/gmsh*sdk*" "sdk/gmsh*sdk*")
    list(APPEND _candidates ${_scanned})

    foreach(_dir IN LISTS _candidates)
        if(EXISTS "${_dir}/include/gmsh.h")
            if(GMSH_SDK_DIR)
                message(STATUS "GMSH_SDK_DIR '${GMSH_SDK_DIR}' has no include/gmsh.h")
            endif()
            set(GMSH_SDK_DIR "${_dir}" CACHE PATH "Path to Gmsh SDK on Windows" FORCE)
            message(STATUS "Gmsh SDK auto-detected: ${_dir}")
            list(REMOVE_ITEM _scanned "${_dir}")
            if(_scanned)
                message(STATUS "Other Gmsh SDKs found (set GMSH_SDK_DIR to pick one): ${_scanned}")
            endif()
            return()
        endif()
    endforeach()
endfunction()
