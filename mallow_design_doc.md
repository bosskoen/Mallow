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
- [ ] sleep
- [ ] look into printing argument count valedation
- [ ] add format option i.e. print to hex, print with presision
- [ ] add thread hangup function
- [ ] add type erased error type
- [ ] make libm constexpr
- [ ] add __chkstk() and remove the fully commited stak
- [ ] small mem* dont get optimised becous the compile doesnt see the mem* call
- [ ] leak detection


### entry
Runtime + program entry point; links `core` + `my_app` and produces the executable
(`OUTPUT_NAME` = `MALLOW_APP_NAME`).

### stl — *planned*
Container library; the STL replacement.
- [ ] make list sicrualr
- [ ] stress test stl
- [ ] refrence stable unordered map or maby a uniqpointer stile thing
- [ ] hashmap inserts doent move key


### io (file handling) — *planned*
File read/write on top of core's io / writer.
- [ ] clean up and ducoment stream and fs
test file, director, itoratrs
document, file, directory, path, itoratrs, acsii, free io functions

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

### reflection
ider a runtime or compile time or macro or a combination of


### custom sintax
- [ ] make [ ] become the list intioliser
- [ ] add the ? error operator
- [ ] see if adding ( dont know the name (int i = if(x) {1} else {2};)) to msvc is doeble
- [ ] add a way to force types to confine to a constept
- [ ] make a null type replace unit

## Test system  *(current focus)*

The runner discovers `bool test_*()` and generates the dispatch main.cpp; the root build compiles it into the tests exe.
- [ ] "run only these tests" — test selection / filtering

## overhale

### split fs more

### split windows and unix translation units
addes the abilty to add more system ezzer and maby even NDA system behind a seperate pull



---

## Systems / cross-cutting

- [ ] app exe name is hardcoded in `app/CMakeLists.txt` (the `Mallow` target) and the run
      scripts (`run.bat` / `run.sh`) — make renaming a single edit (config / arg / auto-detect).
- [ ] re-enable LTO once the MSVC issue clears
- [ ] per-module static vs. dynamic link switch
- [ ] scaffold-a-module script (module creation script) (folder + `CMakeLists.txt`)
- [ ] test Clang; test x86 / ARM32 paths or mark them unsupported

- [ ] real streach make real cargo like maniger ( maby with online sorage, maby with version, maby with binarys, maby with a custom cmake like tool)