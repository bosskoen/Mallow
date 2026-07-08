#root\CMakeLists.txt

cmake_minimum_required(VERSION 3.21)
project(Mallow VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(${CMAKE_CURRENT_SOURCE_DIR}/tools/cmake/definitions.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/definitions.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/tools/cmake/warnings.cmake)

mlw_set_default_warnings(W3)

# (removed the stray `MLW_NO_STDLIB` line at the top — that's a CMake parse error)
string(REPLACE "/EHsc" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")

option(MLW_NO_STDLIB_CXX   "Remove C++ stdlib (keep CRT + libc)"        OFF)
option(MLW_NO_DEFAULT_LIBS "Remove default libs (keep CRT startup)"     OFF)
option(MLW_NO_STDLIB       "Remove everything including CRT"            OFF)
option(MLW_NO_BUILTINS     "Stop treating memcpy/strlen/etc as builtins" OFF)

# always strip rtti + exceptions (these ARE compile flags)
if(MSVC)
    add_compile_options(/GR- /EHs-c- /Zc:preprocessor)
else()
    add_compile_options(-fno-rtti -fno-exceptions)
endif()

if(MLW_NO_BUILTINS AND NOT MSVC)
    add_compile_options(-fno-builtin)
endif()

if(MLW_NO_STDLIB)
    if(MSVC)
        # strip /RTC (Debug-only, needs _RTC_* CRT symbols we don't have)
        string(REGEX REPLACE "/RTC[1csu]*" "" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
        # stop CMake injecting /MD or /MDd
        set(CMAKE_MSVC_RUNTIME_LIBRARY "")

        add_link_options(/NODEFAULTLIB /ENTRY:mlwStart /SUBSYSTEM:CONSOLE)
        link_libraries(kernel32)
        add_compile_options(/GS-)
    else()
        add_compile_options(-ffreestanding -fno-stack-protector)
        add_link_options(-nostdlib -static -no-pie)
        link_libraries(gcc)
    endif()
    message(STATUS "stdlib: none (you provide _start)")
elseif(MLW_NO_DEFAULT_LIBS)
    if(MSVC)
        add_link_options(/NODEFAULTLIB:msvcprt.lib /NODEFAULTLIB:libcpmt.lib
                         /NODEFAULTLIB:msvcrt.lib)
    else()
        add_link_options(-nodefaultlibs)           # LINK option
        link_libraries(c)                          # crt1.o needs __libc_start_main
    endif()
    message(STATUS "stdlib: no default libs, libc kept")

elseif(MLW_NO_STDLIB_CXX)
    if(MSVC)
        add_link_options(/NODEFAULTLIB:msvcprt.lib /NODEFAULTLIB:libcpmt.lib)
    else()
        add_link_options(-nostdlib++)              # LINK option
    endif()
    message(STATUS "stdlib: no C++ stdlib, CRT intact")

else()
    message(STATUS "stdlib: full (default)")
endif()

# static CRT — but ONLY when a CRT is actually present, else it fights /NODEFAULTLIB
if(MSVC AND NOT MLW_NO_STDLIB)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# if(MLW_NO_STDLIB AND MSVC)
#     add_compile_options(/Zc:threadSafeInit-)
# endif()


add_subdirectory(modules/core/core)



add_subdirectory(app)
add_subdirectory(modules/core/entry)