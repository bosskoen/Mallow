# Third-Party License Index — Mallow

An index to every third-party license used in Mallow. This file only points to where each
license lives; it holds no license text itself.

A license lives in one of two places:

- **File header:** code adapted into an existing source file. The notice sits at the top of
  that file. (e.g. the musl functions in `float_manip.inl`.)
- **Module root:** a whole component vendored into a module. The full license ships with the
  component, and the module records it. *(exact convention TBD — likely a
  `private/third_party/<component>/LICENSE` per module.)*

Columns: **Local** where it lives here · **Source** who I took it from · **Origin** where they
got it (or "—" if they wrote it) · **License** · **Notice** where the text lives.

## Index

| Local | Source | Origin | License | Notice |
|-------|--------|--------|---------|--------|
| `core/core/private/core/libc/implementation/float_manip.inl` ; `mlwMod` | musl libc (`src/math/fmod.c`, `fmodf.c`) | - | MIT | file header |
| `core/core/private/core/libc/implementation/float_manip.inl` ; `mlwSplit` | musl libc (`src/math/modf.c`, `modff.c`) | - | MIT | file header |
| `core/core/private/math/cbrt.cpp` ; `mlwCbrt` | musl libc (`src/math/cbrt.c`, `cbrf.c`) | Sun Microsystems (SunPro) | SunPro notice (permissive) | file header |
| `core/core/private/math/helpers.hpp` — `__math_*`, `fp_barrier*`, `eval_as_*` | musl libc (`src/math/`, `libm.h`) | — | MIT | file header |
| `core/core/private/math/log10.cpp` — `mlwLog10` (f64/f32) | musl libc (`e_log10.c`, `e_log10f.c`) | FreeBSD/fdlibm → Sun Microsystems (SunSoft/SunPro) | SunPro notice (permissive) | file header |
| `core/core/private/math/{log,logf,log2,log2f}.cpp` — `mlwLog`, `mlwLog2` | musl libc (Arm `math/log*.c` + `*_data.c`) | Arm optimized-routines | MIT (SPDX) | file header |

<!--
Add rows as you pull things in. When Origin names a third party (fdlibm / Arm / Sun),
that party's notice must be kept in addition to the Source's.
Local paths are relative to modules/core/core/public/core.
-->