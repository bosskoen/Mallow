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



if(MSVC)
    # strip /RTC (Debug-only, needs _RTC_* CRT symbols we don't have)
    string(REGEX REPLACE "/RTC[1csu]*" "" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
    # stop CMake injecting /MD or /MDd
    set(CMAKE_MSVC_RUNTIME_LIBRARY "")

    add_link_options(/NODEFAULTLIB /ENTRY:mlwStart /SUBSYSTEM:CONSOLE)
    link_libraries(kernel32)
    add_compile_options(/GS- /GR- /EHs-c- /Zc:preprocessor)
else()
    add_compile_options(-fno-rtti -fno-exceptions -ffreestanding -fno-stack-protector)
    add_link_options(-nostdlib -static -no-pie)

    link_libraries(gcc)
endif()


# if(MLW_NO_STDLIB AND MSVC)
#     add_compile_options(/Zc:threadSafeInit-)
# endif()


add_subdirectory(modules/core/core)



add_subdirectory(app)
add_subdirectory(modules/core/entry)