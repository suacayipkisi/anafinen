add_library(project_warnings_and_optimizations INTERFACE)

target_compile_definitions(project_warnings_and_optimizations INTERFACE
    $<$<CONFIG:Release>:NDEBUG>
    $<$<CONFIG:Release>:EIGEN_NO_DEBUG>

    EIGEN_DONT_PARALLELIZE
)

if(MSVC)
    target_compile_definitions(project_warnings_and_optimizations INTERFACE 
        NOMINMAX _CRT_SECURE_NO_WARNINGS WIN32_LEAN_AND_MEAN
    )
    target_compile_options(project_warnings_and_optimizations INTERFACE
        $<$<CONFIG:Release>:/O2>
        /openmp:llvm
    )
    if(ANAFINEN_NATIVE_OPTIMIZATIONS)
        target_compile_options(project_warnings_and_optimizations INTERFACE /arch:AVX2)
    endif()
else()
    target_compile_options(project_warnings_and_optimizations INTERFACE
        $<$<CONFIG:Release>:-O3>
        $<$<CONFIG:Release>:-ffast-math> 
        $<$<CONFIG:Release>:-fno-finite-math-only> 
    )
    if(ANAFINEN_NATIVE_OPTIMIZATIONS)
        target_compile_options(project_warnings_and_optimizations INTERFACE -march=x86*64)
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

if(UNIX AND NOT APPLE AND NOT MSVC)
    find_program(MOLD_PATH mold)
    find_program(LLD_PATH ld.lld)
    if(MOLD_PATH)
        target_link_options(project_warnings_and_optimizations INTERFACE -fuse-ld=mold)
    elseif(LLD_PATH)
        target_link_options(project_warnings_and_optimizations INTERFACE -fuse-ld=lld)
    endif()
endif()
