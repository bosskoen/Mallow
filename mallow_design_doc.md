# Mallow — Design Document

**Status:** Shelved (June 2026) — picked up a similar external project  
**Repo:** bosskoen/Mallow  
**Language:** C++20 | **Build:** CMake 3.21+

---

## What Mallow Is

Mallow is a portable, modular C++ ecosystem built on top of libc — and nothing else above that unless you choose it.

The standard library gives you a lot for free, but it also makes a lot of decisions for you: how memory is allocated, how strings are stored, how errors propagate. Mallow's answer is to drop libc++ entirely and rebuild only the pieces you actually need, in a way where every dependency is visible and every allocation is explicit. libc is the floor. Everything above it is Mallow's own modules, written to be taken individually and dropped into any project without dragging dependencies along.

The design principles are:

**Portable.** libc runs everywhere. Each module above it uses only libc and other Mallow modules. No platform-specific code leaks into the public API — it stays in the `private/` layer or behind a thin `os/` module.

**Modular.** Each module is a self-contained CMake target. You can take `memory` or `string` or `collections` and add it to a completely unrelated project with one `add_subdirectory`. There are no god headers, no global state, no module that secretly pulls in half the ecosystem.

**Explicit.** No hidden allocations. If something allocates, it takes an allocator argument. If something does I/O, it takes a writer. Nothing happens behind your back. This makes the codebase easy to reason about and easy to profile.

**Not a lifetime project.** Mallow is a template and a starting point. The stretch goals (removing libc, bare-metal entry) are documented but deliberately not the foundation. The foundation is useful, finishable, and extensible.

It is **not** a freestanding/kernel project. libc stays. The C++ standard library (libc++) goes.

---

## The One Core Decision

**Use libc. Remove libc++.**

- `malloc`, `free`, `memcpy`, `memmove`, `memset` — all allowed, use freely
- `std::vector`, `std::string`, `std::unordered_map`, `std::iostream` — not used; replaced by Mallow modules
- Rationale: removing libc++ gives real benefits (no hidden allocations, no silent exceptions, controlled data layout) without becoming a lifetime project. Removing libc would require writing allocators, string functions, and syscall wrappers for every platform — that kills portability and kills the template purpose.

There is a `MLW_NO_STDLIB` CMake flag that removes everything including libc. It exists as a dormant stretch goal. **Do not design any module against it.** It is there for future exploration only.

RTTI and exceptions are always disabled.

---

## CMake Layers (for reference)

| Flag | What is removed |
|---|---|
| default | nothing; libc + libc++ both present |
| `MLW_NO_STDLIB_CXX` | libc++ removed, libc kept — **this is the target layer** |
| `MLW_NO_DEFAULT_LIBS` | all default libs removed, libc explicitly re-linked |
| `MLW_NO_STDLIB` | everything removed — stretch goal, not actively supported |
| `MLW_NO_BUILTINS` | compiler won't silently emit `memcpy`/`memset` calls |

---

## Current State

### What exists and works
- Root `CMakeLists.txt` with all layer flags wired up
- Module/test conventions fully documented in `README.md`
- `tools/` — build, test, and run scripts for Windows and Linux
- `tools/test_runner/` — standalone test runner that scans modules and auto-generates test entry points
- `modules/core/core` — stubbed, intended for base types and platform macros
- `modules/core/entry` — stubbed, intended to own the program entry point

### What is stubbed / empty
- `core` module has the folder structure but no real headers yet
- `entry` module exists but does not yet do anything beyond forwarding to `main`
- No other modules exist yet

---

## Planned Modules

These are the modules that were planned before shelving. Pick these up in order.

### 1. `core` — base types and macros
No dependencies. Must compile at any layer.
- Fixed-width integer aliases (`u8`, `u16`, `u32`, `u64`, `i8`, `i32`, etc.)
- Platform/compiler/arch detection macros
- Assert macro (no `<cassert>`, no exceptions — just trap or log)
- Attribute helpers (`force_inline`, `no_inline`, `nodiscard`, `likely`/`unlikely`)

### 2. `memory` — allocator wrappers
Depends on: `core`
The point is not to replace `malloc` — it is to wrap it behind an interface so call sites are not hardcoded to the global heap, and so you can swap strategies (pool, linear, etc.) without changing module code.
- `allocator.hpp` — allocator concept/interface that all allocators satisfy
- `heap_allocator.hpp` — wraps `malloc`/`free`; the default
- `linear_allocator.hpp` — bump allocator into a fixed buffer; no free, fast reset; useful for per-frame or per-request scratch memory
- `pool_allocator.hpp` — fixed-size block pool for objects of one type

### 3. `math` — integer math utilities
Depends on: `core`  
Header-only. No libc needed.
- `min`, `max`, `clamp`, `abs`
- `popcount`, `clz`, `ctz`, `next_pow2`
- All `constexpr`

### 4. `string` — string types without `std::string`
Depends on: `core`
- `string_view` — non-owning slice into existing memory (like `std::string_view` but without the stdlib header)
- `fixed_string<N>` — stack-local string with compile-time max length
- `string_builder` — writes into a caller-supplied buffer; takes an allocator from `memory` if it needs to grow

### 5. `collections` — containers without STL
Depends on: `core`, `memory`
- `array_view<T>` — non-owning span
- `fixed_array<T, N>` — stack array, fixed capacity
- `dynamic_array<T, Allocator>` — growable array; takes an explicit allocator, never calls `new` directly

### 6. `io` — output without `<iostream>` or `printf`
Depends on: `core`, `string`
- `writer.hpp` — abstract write-sink interface
- `console_writer` — stdout/stderr via `WriteFile` (Windows) / `write` syscall (Linux)
- `file_writer` — file output
- `format` — minimal `format_to(writer, pattern, args...)` with no `<cstdio>` dependency

### 7. Future: domain modules
Once the above six are solid, the template becomes useful for:
- `modules/gpu/vulkan` — Vulkan integration
- `modules/gpu/d3d12` — D3D12 integration (Windows only)
- `modules/serde/json` — JSON read/write using `string` and `collections`
- `modules/serde/binary` — fast binary serialization

---

## Module Rules (summary)

- Every module lives under `modules/` with `public/`, `private/`, and optionally `tests/`
- CMake target name matches folder name; alias is `name::name`
- No module may depend on a module further down the list than itself (no cycles)
- `core` and `math` must have zero dependencies
- `memory` must not depend on `collections` or `io`

---

## What To Do When Picking This Up

1. Fill out `modules/core/core` — just the type aliases and platform macros, nothing else
2. Write a simple test in `modules/core/core/tests/` to confirm the test runner works end to end
3. Start `modules/memory` with just `heap_allocator` wrapping `malloc`
4. From there the order in the planned modules section above is the right sequence

Don't touch `MLW_NO_STDLIB` or the entry CRT bootstrap until everything else is working. That's the stretch goal, not the foundation.
