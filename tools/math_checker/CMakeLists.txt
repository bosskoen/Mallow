#tools/math_checker/CMakeLists.txt

# Do NOT inherit the freestanding flags (/NODEFAULTLIB, -nostdlib, custom entry).
cmake_minimum_required(VERSION 3.20)
project(mlw_math_tests CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(${CMAKE_CURRENT_SOURCE_DIR}/../cmake/definitions.cmake)

# --- find MPFR + GMP (MPFR depends on GMP) ---
# No standard CMake FindMPFR, so locate by hand.
find_path(MPFR_INCLUDE_DIR mpfr.h)
find_library(MPFR_LIB mpfr)
find_library(GMP_LIB gmp)

find_package(Threads REQUIRED)


if(NOT MPFR_INCLUDE_DIR OR NOT MPFR_LIB OR NOT GMP_LIB)
    message(FATAL_ERROR
        "MPFR/GMP not found. Install with:  sudo apt install libmpfr-dev libgmp-dev")
endif()

# --- the math sources under test ---
# Point these at your actual freestanding math .cpp files. They must be
# compilable in a hosted context — see the note below about your headers.
set(MLW_MATH_SOURCES
    ${CMAKE_SOURCE_DIR}/../../modules/core/core/private/math/exp.cpp
    ${CMAKE_SOURCE_DIR}/../../modules/core/core/private/math/cbrt.cpp
    ${CMAKE_SOURCE_DIR}/../../modules/core/core/private/math/log.cpp
    # add trivial.cpp / others as you test them
)

add_executable(math_tests
    test_main.cpp
    ${MLW_MATH_SOURCES}
)

target_include_directories(math_tests PRIVATE
    ${MPFR_INCLUDE_DIR}
    ${CMAKE_SOURCE_DIR}/../../modules/core/core/public   # your headers
    ${CMAKE_SOURCE_DIR}/../../modules/core/core/public/core 
    ${CMAKE_SOURCE_DIR}/../../modules/core/core/private
)

# FMA must match your library build, or the harness tests different codegen
# than you ship. Mirror the exact FP/arch flags.
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" _proc)
    if(_proc MATCHES "x86_64|amd64")
        target_compile_options(math_tests PRIVATE -mfma -mavx2 -ffp-contract=fast)
    else() # aarch64 (Pi 5): FMA baseline
        target_compile_options(math_tests PRIVATE -ffp-contract=fast)
    endif()
endif()

target_link_libraries(math_tests PRIVATE ${MPFR_LIB} ${GMP_LIB} Threads::Threads)