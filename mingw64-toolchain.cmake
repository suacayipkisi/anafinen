set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

set(CMAKE_C_COMPILER x86_64-w64-mingw32-gcc)
set(CMAKE_CXX_COMPILER x86_64-w64-mingw32-g++)
set(CMAKE_RC_COMPILER x86_64-w64-mingw32-windres)

set(CMAKE_FIND_ROOT_PATH 
    /usr/x86_64-w64-mingw32/sys-root/mingw
    /usr/x86_64-w64-mingw32
)

set(CMAKE_PREFIX_PATH 
    /usr/x86_64-w64-mingw32/sys-root/mingw
    /usr/x86_64-w64-mingw32
)

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

set(CMAKE_NO_SYSTEM_FROM_IMPORTED TRUE)

# Prevent MinGW from picking up Linux host include directories
set(CMAKE_CXX_IMPLICIT_INCLUDE_DIRECTORIES "/usr/x86_64-w64-mingw32/sys-root/mingw/include")
set(CMAKE_C_IMPLICIT_INCLUDE_DIRECTORIES "/usr/x86_64-w64-mingw32/sys-root/mingw/include")

set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static-libgcc -static-libstdc++")
