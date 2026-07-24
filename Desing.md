# Mallow — Design

This is the *why* and the *how* of Mallow. Current state and TODOs live in
[`PROGRESS.md`](PROGRESS.md); how to build and use it lives in [`README.md`](README.md).
This document deliberately holds no status — so it can't go stale the way the old one did.

---

## Why

Adding a dependency in C++ is still painful. In Rust you add one line to `Cargo.toml`; in
C# you add a NuGet package; either way you're building a minute later. C++ has no
equivalent that is both easy and portable. You track down headers, wire up include paths,
find the right thing to link, and hope it survives the next machine. CMake is the closest
thing to a standard, but in practice it's inconsistent — especially on Windows, where
"just build it" is rarely one step.

Mallow started from a want: reusable, portable code that I would actually use — something
that gets as close to the Cargo experience as C++ allows, without shipping binaries or
requiring installs.

Running alongside that was a second itch: shed C++ standard-library bloat — the hidden
allocations, the implicit exceptions, the code size — and replace it with something
explicit. The question "how much can I replace?" spiralled. It went past "drop `libc++`"
and into replacing libc itself, so Mallow now carries its own minimal libc and runtime.
That was more than the original aim called for, and it wasn't the efficient path — but
it's where the project is, and the libc/runtime is real.

The gist is bringing C++ closer to Cargo — as far as I can get without binaries or
installs.

---

## How

### Monorepo, link what you use

The missing piece is a package manager. A real one — universal, easy, close to
one-button, usable by someone who isn't me (a teacher, say) — is hard, and I don't know
how to make it easy without leaning on binaries and installs, which is the exact thing I
wanted to avoid.

So Mallow sidesteps distribution instead of solving it. Everything lives in one
repository, and you link only the modules you use. The monorepo *is* the registry; CMake
targets *are* the packages. Want a module? Link its target — no fetching, no version
resolution, no install step. It isn't Cargo, but it's the part of Cargo that matters most
(add a thing, use it immediately) minus the part I can't build yet.

### Modules

Each module is a self-contained CMake target with a `public/` surface and a `private/`
interior. The rules that keep it honest:

- Modules form a dependency order and never cycle. `core` sits at the bottom and depends
  on nothing.
- Every other module links `core::core`; beyond that, a module links only what it uses.
- No god headers, no hidden global state, no module that quietly drags in half the tree.

The CMake mechanics — target naming, aliases, includes, linking — are in `README.md`.

### Transparent

Every module is source in the tree: you link it, so you have it. Any implementation you
depend on is a file you can open, not a binary you take on trust, and the APIs follow libc
conventions so they read the way you already expect. Nothing is fetched, nothing is hidden.

RTTI and exceptions are the one hard rule here: always off.

### Shedding the standard library

The firm decision is **remove `libc++`** — `std::vector`, `std::string`, `std::iostream`
and the rest are replaced by Mallow modules.

Removing *libc* was the scope creep, now baked in: `core` supplies Mallow's own libc —
libm, allocation, printing, basic types, threading — and `entry` supplies the runtime and
entry point. A build runs freestanding-style: its own entry, no default libraries, only
the OS essentials linked.

There's no "keep the system libc" switch, and won't be one: core's libc is intertwined
with everything above it, with no clean, portable way to wrap the platform libc back in.
It isn't a layer you can dial — it's just what Mallow is.

### Portability

libc runs everywhere, and the OS is the only thing under Mallow that differs by platform.
So platform-specific code stays in a module's `private/` layer or behind a thin
`os/`-style module; it never leaks into a public API. A consumer of a module's `public/`
headers should not be able to tell which OS it's on.

Intended toolchain coverage is MSVC, GCC, and Clang on C++20. What is actually tested is
tracked in `README.md` / `PROGRESS.md`.

---

## Scope and non-goals

Mallow is a template and a starting point — and if it does its job, a lifetime foundation:
the base every project I write after it stands on. That's exactly why the foundation has to
be finishable and genuinely useful. The deep stretch goals are the opposite — documented,
but deliberately not the base anything is built on.

- It is **not** a kernel or bare-metal project. An OS is assumed.
- RTTI and exceptions are always disabled.