#root\CMakeLists.txt

cmake_minimum_required(VERSION 3.21)
project(Mallow VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(${CMAKE_CURRENT_SOURCE_DIR}/tools/cmake/definitions.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/tools/cmake/warnings.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/tools/cmake/Docs.cmake)

add_compile_definitions(FREE_MLW_BUILD)

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

    if(MLW_ARCH_X64 OR MLW_ARCH_X86)
        add_compile_options(/arch:AVX2 /fp:contract)
    elseif(MLW_ARCH_ARM64)
        add_compile_options(/fp:contract)          # FMA baseline, just allow fusion
    endif()

    # add asmble for __chtstk
    ##enable_language(ASM_MASM)
    add_compile_options(/Gs2147483647)
    add_link_options(/STACK:0x100000,0x100000)

 #off becous msvc bugs
   #    add_compile_options($<$<NOT:$<CONFIG:Debug>>:/GL>)
   # add_link_options($<$<NOT:$<CONFIG:Debug>>:/LTCG>
   #                         $<$<NOT:$<CONFIG:Debug>>:/INCREMENTAL:NO>)
else()
    add_compile_options(-fno-rtti -fno-exceptions -ffreestanding -fno-stack-protector -fno-math-errno)
    add_link_options(-nostdlib -static -fno-pie)

    link_libraries(gcc)


   if(MLW_ARCH_X64 OR MLW_ARCH_X86)
        add_compile_options(-mfma -mavx2 -ffp-contract=fast)
    elseif(MLW_ARCH_ARM64)
        add_compile_options(-ffp-contract=fast)    # FMA baseline
    elseif(MLW_ARCH_ARM32)
        add_compile_options(-mfpu=neon-vfpv4 -ffp-contract=fast)
    endif()
    #off becous msvc bugs
  #     add_compile_options($<$<NOT:$<CONFIG:Debug>>:-flto>)
#add_link_options($<$<NOT:$<CONFIG:Debug>>:-flto>)
endif()


# if(MLW_NO_STDLIB AND MSVC)
#     add_compile_options(/Zc:threadSafeInit-)
# endif()


# Build the test harness (the `tests` target). Must be set BEFORE the modules are
# added, so each module can gate its self-registration on it.
option(MLW_BUILD_TESTS "Build the test harness" ON)


add_subdirectory(modules/core/core)

#add new moduals here

add_subdirectory(modules/core/entry)
add_subdirectory(app)


# ============================================================================
#  Test harness
#
#  Modules self-register — only when they actually have tests — into two global
#  lists (done in each module's tests/CMakeLists.txt):
#     MLW_MODULES       : the module lib(s) the test exe links
#     MLW_TEST_SOURCES  : the test .cpp files (ABSOLUTE paths) to compile
#  A module with no tests never registers, so it never lands in the harness.
#
#  The dispatch main.cpp (the extern decls + call table) can't come from CMake
#  — it's function-level, from scanning source — so it's produced by
#  tools/test_runner. The runner is built with an ISOLATED toolchain (a nested
#  cmake, so it does NOT inherit the freestanding flags above; it needs hosted
#  libc++/filesystem) and run at CONFIGURE time, so main.cpp exists before the
#  .sln/.vcxproj is written and VS can build `tests` with no external script.
#
#  NOTE: `tests` is added at root scope, so it INHERITS the freestanding flags
#  (/ENTRY:mlwStart, /NODEFAULTLIB, -nostdlib, -fno-exceptions). That means the
#  generated main must define `mallowMain` (the symbol entry's mlwStart calls)
#  and report through core's io — NOT `int main()` + printf. Update the runner's
#  write_main accordingly, or `tests` will not link under this config.
# ============================================================================
if(MLW_BUILD_TESTS)
    # 1. build the generator, isolated from the freestanding build.
    #    A fresh cmake process does not see the add_compile_options above, so the
    #    tool builds hosted. (Also correct when cross-compiling: the tool must run
    #    on the host, so we deliberately don't pass a cross toolchain here.)
    execute_process(
        COMMAND ${CMAKE_COMMAND} -S ${CMAKE_SOURCE_DIR}/tools/test_runner
                                 -B ${CMAKE_BINARY_DIR}/test_runner
        RESULT_VARIABLE _mlw_tr_cfg)
    execute_process(
        COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR}/test_runner --config Release
        RESULT_VARIABLE _mlw_tr_bld)
    if(_mlw_tr_cfg OR _mlw_tr_bld)
        message(FATAL_ERROR "test_runner failed to build")
    endif()

    find_program(MLW_TEST_RUNNER
        NAMES test_runner
        PATHS ${CMAKE_BINARY_DIR}/test_runner
              ${CMAKE_BINARY_DIR}/test_runner/Release
        NO_DEFAULT_PATH REQUIRED)

    # 2. generate the dispatch main.cpp.
    #    The runner scans ./modules and writes ./generated/tests/, so it runs with
    #    the source tree as its working directory.
    set(MLW_GEN_MAIN ${CMAKE_SOURCE_DIR}/generated/tests/main.cpp)
    execute_process(
        COMMAND ${MLW_TEST_RUNNER}
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        RESULT_VARIABLE _mlw_tr_run)
    if(_mlw_tr_run)
        message(FATAL_ERROR "test_runner failed to generate tests")
    endif()

    # 3. assemble the test exe: generated main + every registered test source,
    #    linking every registered module (plus entry for the startup + mlwStart).
    get_property(mlw_modules      GLOBAL PROPERTY MLW_MODULES)
    get_property(mlw_test_sources GLOBAL PROPERTY MLW_TEST_SOURCES)

    add_executable(tests EXCLUDE_FROM_ALL
        ${MLW_GEN_MAIN}
        ${mlw_test_sources})

    target_link_libraries(tests PRIVATE entry ${mlw_modules})
endif()