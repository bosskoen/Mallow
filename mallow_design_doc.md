# Mallow — Progress & TODO

The systems that make up Mallow, each with its own to-do, plus the cross-cutting work.
Full per-module detail lives with the module; the *why* is in `DESIGN.md`, the *how to use*
in `README.md`.

Status: **active.**

---

## Modules

### core — `core::core`
Mallow's libc + runtime: types, traits, libm, allocation (galloc), printing / io,
threading, CRT bootstrap.
- [ ] finish filling out the surface over time when needed
- [ ] add a exit thread function
- [ ] fix printing a function (print("{}", core::mlwExp10(1.0)))
- [ ] look into printing argument count valedation
- [ ] add format option i.e. print to hex, print with presision
- [ ] add thread hangup function
- [ ] add type erased error type


### entry
Runtime + program entry point; links `core` + `my_app` and produces the executable
(`OUTPUT_NAME` = `MALLOW_APP_NAME`).

### stl — *planned*
Container library; the STL replacement.
- [ ] decide the initial container set + allocator story
- [ ] first container

### io (file handling) — *planned*
File read/write on top of core's io / writer.
- [ ] file handle + read/write
- [ ] path handling


### serde — *planned*
Serialization. A group folder (like `core`), one module per format so you link only the one
you need.
- **toml** — TOML read/write
- **json** — JSON read/write
- **binary** — fast binary (de)serialization
- [ ] start with `toml`
- [ ] decide if formats share a `Serialize` / `Deserialize` interface (its own small module)
      or stay independent
---

## Test system  *(current focus)*

The runner discovers `bool test_*()` and generates `all_tests`.
- [ ] rename `test/` → `tests/` so discovery finds it (and fix the `CMakeLits.txt` typo)
- [ ] decide the config: freestanding (matches what ships, report via core io) vs. hosted
      `<cstdio>` — leaning freestanding, since core defines libc symbols that clash with a
      hosted CRT
- [ ] move module registration into the generated `all_tests` CMake so a second tested
      module doesn't double-`add_subdirectory` `core`
- [ ] first real test under `core`, end to end

---

## Systems / cross-cutting

- [ ] `run` app name is duplicated in `./definitions.cmake` (`MALLOW_APP_NAME`) and the run
      script — de-duplicate (config file / arg / auto-detect).
- [ ] re-enable LTO once the MSVC issue clears
- [ ] per-module static vs. dynamic link switch
- [ ] scaffold-a-module script (module creation script) (folder + `CMakeLists.txt`)
- [ ] test Clang; test x86 / ARM32 paths or mark them unsupported

- [ ] real streach make real cargo like maniger ( maby with online sorage, maby with version, maby with binarys, maby with a custom cmake like tool)