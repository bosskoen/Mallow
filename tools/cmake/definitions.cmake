# tools/cmake/definitions.cmake

if(WIN32)
    add_compile_definitions(MLW_WINDOWS)
elseif(UNIX AND NOT APPLE)
    add_compile_definitions(MLW_LINUX)
elseif(APPLE)
    add_compile_definitions(MLW_MAC)
else()
    message(WARNING "MLW: unknown platform")
endif()

add_compile_definitions(
    $<$<CONFIG:Debug>:MLW_DEBUG>
    $<$<CONFIG:Release>:MLW_RELEASE>
    $<$<CONFIG:RelWithDebInfo>:MLW_RELEASE>
    $<$<CONFIG:MinSizeRel>:MLW_RELEASE>
)

# ── compiler identity (for intrinsics, __builtin_*, pragmas) ──
if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    add_compile_definitions(MLW_MSVC)
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    add_compile_definitions(MLW_CLANG)
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    add_compile_definitions(MLW_GCC)
else()
    message(WARNING "MLW: unknown compiler: ${CMAKE_CXX_COMPILER_ID}")
endif()

# ── C++ ABI (for the crt backend + teardown model) ──
# MSVC (CMake var) is true for cl.exe AND clang-cl → MSVC ABI.
if(MSVC)
    set(MLW_ABI_MSVC TRUE)
    add_compile_definitions(MLW_ABI_MSVC)
else()
    set(MLW_ABI_ITANIUM TRUE)
    add_compile_definitions(MLW_ABI_ITANIUM)
endif()


if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
    add_compile_definitions(MLW_X64)
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64")
    add_compile_definitions(MLW_ARM64)
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "arm")
    add_compile_definitions(MLW_ARM32)
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "i386|i686|x86")
    add_compile_definitions(MLW_X86)
else()
    message(WARNING "MLW: unknown architecture: ${CMAKE_SYSTEM_PROCESSOR}")
endif()