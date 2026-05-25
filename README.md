# Module & Test Conventions

This document describes the required folder structure, CMake format, and naming conventions for modules and tests. Follow this before building any tooling that auto-generates modules.

---

## Folder Structure

Every module lives under `modules/`. A module is any directory directly under `modules/` (or nested inside a group folder) that contains a `CMakeLists.txt`.

```
modules/
    core/                   <- group folder, no CMakeLists.txt
        memory/             <- module,       has CMakeLists.txt
        math/               <- module,       has CMakeLists.txt
    physics/                <- module,       has CMakeLists.txt
```

The rule the tooling uses: walk `modules/` recursively. The first directory in any branch that contains a `CMakeLists.txt` is a module. Do not go deeper from there.

A module folder must contain:

```
my_module/
    public/         <- headers exposed to consumers
    private/        <- headers and source only visible internally
    tests/
        CMakeLists.txt
        test_something.cpp
    CMakeLists.txt
```

`public/` and `private/` are required even if empty. `tests/` is optional — the test runner skips modules without `tests/CMakeLists.txt`.

---

## Module Naming

The module name is the folder name. Keep it lowercase with underscores.

```
modules/physics/            -> physics
modules/core/memory/        -> memory   (leaf name)
modules/core/math/          -> math
```

The test namespace is derived from the full path relative to `modules/`, with `/` replaced by `_` and `_test` appended:

```
modules/physics/            -> physics_test
modules/core/memory/        -> core_memory_test
modules/core/math/          -> core_math_test
```

This namespace must match exactly in your test files or the test runner will not find your functions.

---

## Module CMakeLists.txt

```cmake
add_library(memory STATIC)
add_library(memory::memory ALIAS memory)

target_sources(memory
    PRIVATE
        private/allocator.cpp
)

target_include_directories(memory
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/public
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/private
)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
```

Rules:
- target name must match the folder name exactly
- alias must follow the pattern `name::name`
- `PUBLIC` include points to `public/`
- `PRIVATE` include points to `private/`
- tests are gated behind `BUILD_TESTING`

---

## Test CMakeLists.txt

The test `CMakeLists.txt` is present so the test runner knows this module has tests. It does not need to do anything for now — the test runner generates its own build. Keep it as a minimal placeholder:

```cmake
# tests for memory module
# sources are picked up by the test runner
```

This file must exist for the test runner to include the module. If it is missing the module is skipped entirely.

---

## Writing Tests

Test files live in `tests/` or any subdirectory of `tests/`. All test functions must:

- return `bool`
- be named with a `test_` prefix
- live inside the correct namespace for the module

```cpp
// modules/core/memory/tests/test_allocator.cpp
#include "memory/allocator.hpp"

namespace core_memory_test {

bool test_alloc_returns_non_null() {
    return alloc(64) != nullptr;
}

bool test_free_does_not_crash() {
    void* p = alloc(64);
    free_mem(p);
    return true;
}

} // namespace core_memory_test
```

Rules:
- namespace must match exactly — `core_memory_test` for `modules/core/memory/`
- function signature must be exactly `bool test_name()`
- no parameters, no overloads
- return `true` for pass, `false` for fail
- one namespace per file, do not split the namespace across files in unexpected ways [soudnt mater]
- do not nest scopes inside the namespace before a function definition — the parser tracks `{` and `}` depth and will lose track of where the namespace ends [doesnt mater]

---

## Root CMakeLists.txt

The root wires in modules and the app. Add new modules here with `add_subdirectory`. Optional modules are gated behind an `option()`.

```cmake
cmake_minimum_required(VERSION 3.21)
project(MyProject VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# always-on modules
add_subdirectory(modules/core/memory)
add_subdirectory(modules/core/math)

# optional modules
option(MY_ENABLE_PHYSICS "Build physics module" OFF)
if(MY_ENABLE_PHYSICS)
    add_subdirectory(modules/physics)
endif()

add_subdirectory(app)
```

---

## App CMakeLists.txt

The only file you edit per project. List which modules the app links against.

```cmake
add_executable(my_app
    src/main.cpp
)

target_link_libraries(my_app
    PRIVATE
        memory::memory
        math::math
        # physics::physics
)
```

No include paths needed — `PUBLIC` headers from linked modules are forwarded automatically.

---

## Tools

All tools are scripts in `tools/` callable from the repo root.

| Script | What it does |
|---|---|
| `tools/build.bat` / `build.sh` | configures and builds the main project |
| `tools/test.bat` / `test.sh` | builds test_runner, generates tests, builds and runs them |

The test runner binary lives at `tools/test_runner/` and is a standalone CMake project. It does not connect to the main project build.

---

## Things Not Yet Done

- dynamic and static linking switch per module
- auto-generate module folder and CMakeLists from a script
- auto-add new `.cpp` files to a module's `target_sources`
- package manager integration
- handling of modules that depend on other modules


## TODO

### `run` script — app name
The `tools/run.bat` and `tools/run.sh` scripts have the app name hardcoded as `my_app`.
When cloning this template, change the app name in both scripts to match your executable.

Options to improve this later:
- read from a config file like `.vscode/project.env` with `APP_NAME=my_app`
- pass as argument `tools\run.bat my_app`
- auto-detect by scanning `app/CMakeLists.txt` for `add_executable`