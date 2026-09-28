add_library(project_warnings_and_optimizations INTERFACE)

# Linked by every first-party target (anaf_io, anaf_core, anafinen, tests); third-party code is
# built by its own targets and included as SYSTEM, so these flags only cover our sources.
option(ANAFINEN_WARNINGS_AS_ERRORS "Treat compiler warnings as errors (CI)" OFF)

target_compile_definitions(project_warnings_and_optimizations INTERFACE
    $<$<CONFIG:Release>:NDEBUG>
    $<$<CONFIG:Release>:EIGEN_NO_DEBUG>

)

if(MSVC)
    target_compile_definitions(project_warnings_and_optimizations INTERFACE 
        NOMINMAX _CRT_SECURE_NO_WARNINGS WIN32_LEAN_AND_MEAN
    )
    target_compile_options(project_warnings_and_optimizations INTERFACE
        $<$<CONFIG:Release>:/O2>
        /utf-8 # sources and string literals are UTF-8 (default is the ANSI code page)
        /W4 /permissive-
        /external:anglebrackets /external:W0 # no warnings from <...> third-party headers
        $<$<BOOL:${ANAFINEN_WARNINGS_AS_ERRORS}>:/WX>
    )
    if(ANAFINEN_NATIVE_OPTIMIZATIONS)
        target_compile_options(project_warnings_and_optimizations INTERFACE /arch:AVX2)
    endif()
else()
    target_compile_options(project_warnings_and_optimizations INTERFACE
        $<$<CONFIG:Release>:-O3>
        $<$<CONFIG:Release>:-ffast-math>
        $<$<CONFIG:Release>:-fno-finite-math-only>
        -Wall -Wextra -Wpedantic -Wshadow -Wconversion
        # Clang's -Wconversion also enables -Wsign-conversion (GCC's does not in C++). Signed
        # OpenMP loop indices (MSVC compatibility) and int/size_t mixing make it pure noise.
        -Wno-sign-conversion
        $<$<BOOL:${ANAFINEN_WARNINGS_AS_ERRORS}>:-Werror>
    )
    if(ANAFINEN_NATIVE_OPTIMIZATIONS)
        target_compile_options(project_warnings_and_optimizations INTERFACE -march=native) # local builds only: binaries then need this CPU
    endif()
    target_link_options(project_warnings_and_optimizations INTERFACE
        $<$<CONFIG:Release>:-O3>
    )
endif()

find_program(CCACHE_PROGRAM ccache)
if(CCACHE_PROGRAM)
    set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
    set(CMAKE_C_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
endif()

if(UNIX AND NOT APPLE AND NOT MSVC AND NOT WIN32) 
    find_program(MOLD_PATH mold)
    find_program(LLD_PATH ld.lld)
    if(MOLD_PATH)
        target_link_options(project_warnings_and_optimizations INTERFACE -fuse-ld=mold)
    elseif(LLD_PATH)
        target_link_options(project_warnings_and_optimizations INTERFACE -fuse-ld=lld)
    endif()
endif()
