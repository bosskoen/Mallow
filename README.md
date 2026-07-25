# Mallow

A modular C++20 template. It drops the C++ standard library (`libc++`) and builds small,
self-contained modules on top of a minimal C runtime of its own. Every module is a
self-contained CMake target reusable across Mallow-compatible projects — they build on
`core`, so they are not drop-in for an arbitrary unrelated project. Nothing is hidden behind
a binary: you link a module, so you have its source.

This README is the how-to-use-it manual. For *why* it is built this way, see
[`DESIGN.md`](DESIGN.md). For current state and TODOs, see [`PROGRESS.md`](PROGRESS.md).
Third-party code and its licenses are indexed in
[`THIRD_PARTY_LICENSES.md`](THIRD_PARTY_LICENSES.md).

Status: active, single-author learning/template project.

---

## Requirements

- CMake 3.21 or newer
- A C++20 compiler

Compiler/target support is *intended* to cover MSVC, GCC, and Clang on C++20.
What is actually tested today:

| Toolchain | Arch | Tested |
|---|---|---|
| MSVC | x64 | yes |
| GCC | x64, ARM64 | yes |
| Clang | any | not yet — paths exist but are unverified |
| any | x86, ARM32 | not yet — code paths exist, untested |

The build is freestanding-style: it configures with `-ffreestanding` / `-nostdlib`
(GCC/Clang) or `/NODEFAULTLIB` with a custom `/ENTRY:mlwStart` (MSVC), links only
`kernel32` (Windows) or `libgcc` (Linux), and disables RTTI and exceptions everywhere.
The program entry point (`mlwStart`) is provided by the `entry` module, not the CRT.

---

## Building

The wrapper scripts in `tools/` are the normal entry points:

```
tools/build.bat      # Windows
tools/build.sh       # Linux
```

They configure and build the main project. The equivalent by hand:

```
cmake -B build -S .
cmake --build build
```

(Check the `tools/build` script for the exact generator and any flags it passes before
relying on the manual form.)

## Running

```
tools/run.bat        # Windows
tools/run.sh         # Linux
```

The executable name comes from `MALLOW_APP_NAME` in `./definitions.cmake` (it's `my_app`'s
`OUTPUT_NAME`). The same name is also hardcoded in the run script, so to rename the app you
change it in **both** places. (Tracked in `PROGRESS.md`.)

---

## Repository layout

```
Mallow/
    app/                    your program, built as `Mallow` — the executable
    modules/
        core/               group folder (no CMakeLists.txt of its own)
            core/           module: minimal libc + runtime (types, mem, math, io, threading)
            entry/          module: the CRT/startup slab (a library) — provides mlwStart,
                            linked into the executable
    tools/
        build.*  test.*  run.*        wrapper scripts
        cmake/
            definitions.cmake         arch/compiler/OS detection macros
            warnings.cmake            warning-level helpers (see below)
            Docs.cmake                docs generation
        test_runner/                  the test-dispatcher generator (built isolated)
    definitions.cmake       root-level definitions include (MALLOW_APP_NAME lives here)
    CmakeLists.txt          root build: toolchain flags, module wiring, test harness
```

A **group folder** (like `modules/core`) has no `CMakeLists.txt` and just nests other
modules. A **module** is the first folder down any branch that *does* have a
`CMakeLists.txt`; tooling stops descending there.

---

## Modules

### Anatomy

```
my_module/
    public/      headers exposed to consumers        (required, even if empty)
    private/     internal headers + source           (required, even if empty)
    tests/       optional; a module with no tests/CMakeLists.txt is skipped
        CMakeLists.txt
        test_*.cpp
    CMakeLists.txt
```

### Module CMakeLists.txt

```cmake
add_library(memory STATIC)
add_library(memory::memory ALIAS memory)

target_sources(memory
    PRIVATE
        private/allocator.cpp
)

target_include_directories(memory
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/public
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/private
)

# every module links core::core (except core itself)
target_link_libraries(memory PUBLIC core::core)

if(MLW_BUILD_TESTS)
    add_subdirectory(tests)
endif()
```

Rules:
- Target name matches the folder name.
- Give the target an alias for consumers to link against. The alias name is yours to
  choose — `core` uses `core::core`.
- `PUBLIC` include is `public/`, `PRIVATE` include is `private/`.
- **Every module links `core::core` (except `core` itself), `PUBLIC`.** Your public
  headers use core's typedefs/traits, so consumers need them transitively.
- To use another module's headers, link that module. Linking the same module through
  several paths is fine — it's built once and its symbols resolve once, no duplication.
- A module may only depend on modules earlier in the dependency order (no cycles);
  `core` depends on nothing.

### Adding a new module

1. Create the folder under `modules/` (inside a group folder if you want nesting),
   with `public/`, `private/`, a `CMakeLists.txt`, and optionally `tests/`.
2. Write the module `CMakeLists.txt` following the pattern above.
3. Wire it into the root `CmakeLists.txt` with `add_subdirectory(modules/<path>)`
   under the "add new modules here" comment.
4. To make it optional, gate it behind an `option()`:

```cmake
option(MLW_ENABLE_PHYSICS "Build physics module" OFF)
if(MLW_ENABLE_PHYSICS)
    add_subdirectory(modules/physics)
endif()
```

### Linking modules into the app

`app/CMakeLists.txt` builds `Mallow` — **the executable**, and the file you edit per
project. List the modules `Mallow` uses, plus `entry` (the startup slab that carries
`mlwStart`). You don't touch `entry`.

```cmake
target_link_libraries(Mallow
    PRIVATE
        entry
        core::core
)
```

No include paths needed: a linked module forwards its `public/` headers automatically.

---

## Warning levels

Warning flags are managed by `tools/cmake/warnings.cmake`, which maps a level name to
the right flags per compiler:

| Level | MSVC | GCC / Clang |
|---|---|---|
| W0 | `/W0` | `-w` |
| W1 | `/W1` | `-Wall` |
| W2 | `/W2` | `-Wall` |
| W3 | `/W3` | `-Wall -Wextra` |
| W4 | `/W4` | `-Wall -Wextra -Wpedantic` |
| WALL | `/Wall` | `-Wall -Wextra -Wpedantic -Wconversion -Wshadow` |

**Project default** is set once in the root `CmakeLists.txt`:

```cmake
mlw_set_default_warnings(W3)
```

Change that line to raise or lower the default for the whole build.

**Per-target override** — call this in a module's `CMakeLists.txt` after the target
exists:

```cmake
mlw_set_warnings(memory LEVEL W4)
```

On MSVC the helper strips any conflicting `/W*` flag CMake injected before applying the
new one, so you won't get duplicate-level warnings.

---

## Testing

Tests are plain `bool test_*()` functions the runner discovers — you never register a test,
you follow the naming rules and it finds them. `tests` is an executable **in the main
build** (excluded from the default build), compiled under the same freestanding config as
everything else; pass/fail is reported with core's print macros. There is no separate test
project.

### Writing a test

```cpp
// modules/core/core/tests/test_something.cpp
#include "core/whatever.h"

namespace core_core_test {

bool test_it_works() {
    return 1 + 1 == 2;
}

} // namespace core_core_test
```

Rules the runner depends on:
- The namespace is the module path relative to `modules/`, with `/` replaced by `_`,
  plus `_test`. So `modules/core/core/` → `core_core_test`.
- Every test function is exactly `bool test_name()` — no parameters, no overloads.
- Return `true` for pass, `false` for fail.
- Keep one namespace per file, and don't open a nested scope inside the namespace before
  the first test function (the runner tracks `{`/`}` depth to find where the namespace
  ends).
- Tests live in a `tests/` folder; a module needs `tests/CMakeLists.txt` to be picked up.

### Adding tests to a module

The module's own `CMakeLists.txt` pulls its tests in (gated on the switch):

```cmake
if(MLW_BUILD_TESTS)
    add_subdirectory(tests)
endif()
```

`tests/CMakeLists.txt` builds no target — it just registers the module and its test
sources into the two global lists the root build reads:

```cmake
# modules/.../<name>/tests/CMakeLists.txt
set_property(GLOBAL APPEND PROPERTY MLW_TEST_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/test_something.cpp)   # absolute paths
set_property(GLOBAL APPEND PROPERTY MLW_MODULES <module_name>)
```

### Running the tests

`tests` is excluded from the default build, so build it explicitly:

- **Visual Studio:** build the `tests` project directly (it won't build as part of
  ALL_BUILD).
- **Non-VS / command line:** `tools/test.bat`, or `cmake --build build --target tests`
  and run the resulting exe.

Discovery runs at **configure** time, so a newly added test isn't seen until CMake
reconfigures. Adding a new `bool test_*()` **function** to an existing file needs this — run
`tools/test.bat` (it reconfigures) or trigger a reconfigure in your IDE, or the new function
won't be in the generated dispatcher.