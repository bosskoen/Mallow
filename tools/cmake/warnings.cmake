# tools/cmake/warnings.cmake
# ─────────────────────────────────────────────────────────────────────────────
# mlw_set_warnings(target LEVEL <W0|W1|W2|W3|W4|WALL>)
#
# Applies compiler warning flags to <target> at the requested level.
# Uses PRIVATE scope — callers can still tack on extra flags themselves.
# ─────────────────────────────────────────────────────────────────────────────

# Project-wide default: targets that never call mlw_set_warnings still get
# this level applied via mlw_apply_default_warnings().
set(MLW_DEFAULT_WARNING_LEVEL "W2" CACHE STRING
    "Default warning level for all targets (W0 W1 W2 W3 W4 WALL)")
set_property(CACHE MLW_DEFAULT_WARNING_LEVEL PROPERTY STRINGS W0 W1 W2 W3 W4 WALL)

# ── internal helper ──────────────────────────────────────────────────────────
function(_mlw_warning_flags out_var level)
    if(MSVC)
        if   (level STREQUAL "W0")   
            set(flags /W0)
        elseif(level STREQUAL "W1")
              set(flags /W1)
        elseif(level STREQUAL "W2")  
            set(flags /W2)
        elseif(level STREQUAL "W3")  
            set(flags /W3)
        elseif(level STREQUAL "W4")  
            set(flags /W4)
        elseif(level STREQUAL "WALL") 
            set(flags /Wall)
        else()
            message(FATAL_ERROR "mlw_set_warnings: unknown level '${level}'")
        endif()
    else()  # GCC / Clang
        if   (level STREQUAL "W0")   
            set(flags -w)
        elseif(level STREQUAL "W1") 
            set(flags -Wall)
        elseif(level STREQUAL "W2")  
            set(flags -Wall)
        elseif(level STREQUAL "W3")  
            set(flags -Wall -Wextra)
        elseif(level STREQUAL "W4")  
            set(flags -Wall -Wextra -Wpedantic)
        elseif(level STREQUAL "WALL") 
            set(flags -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
        else()
            message(FATAL_ERROR "mlw_set_warnings: unknown level '${level}'")
        endif()
    endif()
    set(${out_var} "${flags}" PARENT_SCOPE)
endfunction()

# ── public API ───────────────────────────────────────────────────────────────
function(mlw_set_warnings target)
    cmake_parse_arguments(PARSE_ARGV 1 ARG "" "LEVEL" "")
    if(NOT ARG_LEVEL)
        message(FATAL_ERROR "mlw_set_warnings: LEVEL is required")
    endif()

    # Strip whatever CMake (or a toolchain) injected — /W0.../W4/Wall on MSVC,
    # nothing needed on GCC/Clang since -w / -Wall don't accumulate weirdly.
    if(MSVC)
        get_target_property(opts ${target} COMPILE_OPTIONS)
        if(opts)
            list(FILTER opts EXCLUDE REGEX "^/W[0-4]$|^/Wall$")
            set_target_properties(${target} PROPERTIES COMPILE_OPTIONS "${opts}")
        endif()
    endif()

    _mlw_warning_flags(flags "${ARG_LEVEL}")
    target_compile_options(${target} PRIVATE ${flags})
    message(STATUS "warnings: ${target} -> ${ARG_LEVEL}")
endfunction()

function(mlw_set_default_warnings level)
    _mlw_warning_flags(flags "${level}")

    # On MSVC strip any /Wn that CMake already put in CMAKE_CXX_FLAGS
    # so we don't end up with two conflicting /Wn flags globally.
    if(MSVC)
        string(REGEX REPLACE "/W[0-4]" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}" PARENT_SCOPE)
        string(REPLACE "/Wall" ""       CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}" PARENT_SCOPE)
    endif()

    add_compile_options(${flags})
    message(STATUS "warnings: global default -> ${level}")
endfunction()