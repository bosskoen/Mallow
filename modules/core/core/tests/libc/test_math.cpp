// core/core/tests/test_math.cpp
//
// Coverage for core/libc/math.h and its constexpr companion
// implementation/float_manip.inl.
//
// Strategy:
//   * Everything constexpr (classification, sign, clamp/min/max, abs, and the
//     whole float_manip.inl set: floor/ceil/trunc/round/split/mod) is pinned
//     with static_assert. That validates the real implementation at COMPILE
//     time, so these checks hold the instant the file compiles — the runtime
//     wrappers below just re-observe them so the runner reports PASS/FAIL.
//   * sqrt / fma use hardware builtins and are not constexpr, so they are
//     runtime-only, checked against exact-representable results.
//   * The transcendentals (log/exp/pow/cbrt) are defined out-of-line in the
//     math TU and are runtime-only; they are checked within a tolerance since
//     the library documents "< 1 ulp" (exp10: up to 4 ulp).
//
// NaN/inf come from NumericLimits<T> (typedef.h), matching test_typedef.cpp.
// Only exact-representable operands/results are used in equality checks, so
// there are no fragile floating-point == comparisons anywhere below.

#include "core/libc/math.h"
#include "core/typedef.h"
#include "core/bit.h"

using namespace core;

namespace
{
    // Absolute-difference tolerance compare for the runtime transcendentals.
    // Not named test_* so the runner's textual scan ignores it.
    constexpr bool close(f64 a, f64 b, f64 tol) noexcept
    {
        f64 d = a - b;
        if (d < 0) d = -d;
        return d <= tol;
    }
    constexpr bool closef(f32 a, f32 b, f32 tol) noexcept
    {
        f32 d = a - b;
        if (d < 0) d = -d;
        return d <= tol;
    }

    // Shorthand for the special values.
    constexpr f64 INF64 = NumericLimits<f64>::infinity;
    constexpr f64 NAN64 = NumericLimits<f64>::nan;
    constexpr f32 INF32 = NumericLimits<f32>::infinity;
    constexpr f32 NAN32 = NumericLimits<f32>::nan;
}

namespace core_core_test
{
    // =======================================================================
    //  Classification: mlwIsInf / mlwIsNaN
    // =======================================================================
    bool test_math_isinf()
    {
        static_assert(mlwIsInf(INF64));
        static_assert(mlwIsInf(-INF64));
        static_assert(!mlwIsInf(0.0));
        static_assert(!mlwIsInf(1.0e308));       // large but finite
        static_assert(!mlwIsInf(NAN64));
        static_assert(mlwIsInf(INF32));
        static_assert(mlwIsInf(-INF32));
        static_assert(!mlwIsInf(3.4e38f));
        static_assert(!mlwIsInf(NAN32));
        return true;
    }

    bool test_math_isnan()
    {
        static_assert(mlwIsNaN(NAN64));
        static_assert(!mlwIsNaN(INF64));
        static_assert(!mlwIsNaN(-INF64));
        static_assert(!mlwIsNaN(0.0));
        static_assert(!mlwIsNaN(-0.0));
        static_assert(!mlwIsNaN(1.0));
        static_assert(mlwIsNaN(NAN32));
        static_assert(!mlwIsNaN(INF32));
        static_assert(!mlwIsNaN(0.0f));
        return true;
    }

    // =======================================================================
    //  Sign: mlwCopySign  (bit-exact, including signed zero)
    // =======================================================================
    bool test_math_copysign()
    {
        static_assert(mlwCopySign(1.0, -2.0) == -1.0);
        static_assert(mlwCopySign(-1.0, 2.0) == 1.0);
        static_assert(mlwCopySign(42.5, 0.0) == 42.5);
        static_assert(mlwCopySign(42.5, -0.0) == -42.5);
        // signed-zero result must be bit-exact
        static_assert(mlwBitCast<uint64>(mlwCopySign(0.0, -1.0)) == mlwBitCast<uint64>(-0.0));
        static_assert(mlwBitCast<uint64>(mlwCopySign(-0.0, 1.0)) == mlwBitCast<uint64>(0.0));
        // f32 overload
        static_assert(mlwCopySign(1.0f, -2.0f) == -1.0f);
        static_assert(mlwBitCast<uint32>(mlwCopySign(0.0f, -1.0f)) == mlwBitCast<uint32>(-0.0f));
        // sign carried onto infinity
        static_assert(mlwCopySign(INF64, -1.0) == -INF64);
        return true;
    }

    // =======================================================================
    //  Clamp / min / max  (float + integer)
    // =======================================================================
    bool test_math_clamp()
    {
        static_assert(mlwClamp(5, 0, 10) == 5);
        static_assert(mlwClamp(-3, 0, 10) == 0);
        static_assert(mlwClamp(42, 0, 10) == 10);
        static_assert(mlwClamp(0, 0, 10) == 0);   // on the boundary
        static_assert(mlwClamp(10, 0, 10) == 10);
        static_assert(mlwClamp(2.5, 0.0, 1.0) == 1.0);
        static_assert(mlwClamp(-2.5, 0.0, 1.0) == 0.0);
        static_assert(mlwClamp(0.5, 0.0, 1.0) == 0.5);
        return true;
    }

    bool test_math_min_max()
    {
        static_assert(mlwMin(3, 7) == 3);
        static_assert(mlwMin(7, 3) == 3);
        static_assert(mlwMax(3, 7) == 7);
        static_assert(mlwMax(7, 3) == 7);
        static_assert(mlwMin(-5, -2) == -5);
        static_assert(mlwMax(-5, -2) == -2);
        static_assert(mlwMin(1.5, 1.5) == 1.5);   // equal operands
        static_assert(mlwMax(2.25, 2.5) == 2.5);
        return true;
    }

    // =======================================================================
    //  Absolute value  (signed int + float; float keeps NaN/inf)
    // =======================================================================
    bool test_math_abs()
    {
        static_assert(mlwAbs(-5) == 5);
        static_assert(mlwAbs(5) == 5);
        static_assert(mlwAbs(0) == 0);
        static_assert(mlwAbs(-2.5) == 2.5);
        static_assert(mlwAbs(2.5) == 2.5);
        static_assert(mlwAbs(-2.5f) == 2.5f);
        // abs(-0.0) is +0.0, bit-exact
        static_assert(mlwBitCast<uint64>(mlwAbs(-0.0)) == mlwBitCast<uint64>(0.0));
        // NaN/inf preserved (magnitude)
        static_assert(mlwAbs(-INF64) == INF64);
        static_assert(mlwIsNaN(mlwAbs(NAN64)));
        return true;
    }

    // =======================================================================
    //  Floor  (float_manip.inl)
    // =======================================================================
    bool test_math_floor()
    {
        static_assert(mlwFloor(1.0) == 1.0);
        static_assert(mlwFloor(1.5) == 1.0);
        static_assert(mlwFloor(1.999) == 1.0);
        static_assert(mlwFloor(42.75) == 42.0);
        static_assert(mlwFloor(-1.0) == -1.0);
        static_assert(mlwFloor(-1.5) == -2.0);
        static_assert(mlwFloor(-0.001) == -1.0);
        static_assert(mlwFloor(0.5) == 0.0);
        static_assert(mlwFloor(123456789.75) == 123456789.0);
        static_assert(mlwFloor(-123456789.75) == -123456790.0);
        // ±0 preserved bit-exact
        static_assert(mlwBitCast<uint64>(mlwFloor(-0.0)) == mlwBitCast<uint64>(-0.0));
        // NaN/inf pass through
        static_assert(mlwFloor(INF64) == INF64 && mlwFloor(-INF64) == -INF64);
        static_assert(mlwIsNaN(mlwFloor(NAN64)));
        // f32 overload
        static_assert(mlwFloor(-1.5f) == -2.0f && mlwFloor(2.9f) == 2.0f);
        return true;
    }

    // =======================================================================
    //  Ceil
    // =======================================================================
    bool test_math_ceil()
    {
        static_assert(mlwCeil(1.0) == 1.0);
        static_assert(mlwCeil(1.001) == 2.0);
        static_assert(mlwCeil(1.5) == 2.0);
        static_assert(mlwCeil(-1.5) == -1.0);
        static_assert(mlwCeil(-1.999) == -1.0);
        static_assert(mlwCeil(0.5) == 1.0);
        // |x|<1 negative rounds up to -0.0 (bit-exact)
        static_assert(mlwBitCast<uint64>(mlwCeil(-0.5)) == mlwBitCast<uint64>(-0.0));
        static_assert(mlwCeil(INF64) == INF64 && mlwCeil(-INF64) == -INF64);
        static_assert(mlwIsNaN(mlwCeil(NAN64)));
        static_assert(mlwCeil(1.001f) == 2.0f && mlwCeil(-1.5f) == -1.0f);
        return true;
    }

    // =======================================================================
    //  Trunc  (toward zero, sign preserved)
    // =======================================================================
    bool test_math_trunc()
    {
        static_assert(mlwTrunc(1.9) == 1.0);
        static_assert(mlwTrunc(-1.9) == -1.0);
        static_assert(mlwTrunc(0.9) == 0.0);
        static_assert(mlwTrunc(42.0) == 42.0);
        // |x|<1 chops to signed zero, bit-exact
        static_assert(mlwBitCast<uint64>(mlwTrunc(-0.5)) == mlwBitCast<uint64>(-0.0));
        static_assert(mlwBitCast<uint64>(mlwTrunc(0.5)) == mlwBitCast<uint64>(0.0));
        static_assert(mlwTrunc(INF64) == INF64);
        static_assert(mlwIsNaN(mlwTrunc(NAN64)));
        static_assert(mlwTrunc(-1.9f) == -1.0f && mlwTrunc(3.999f) == 3.0f);
        return true;
    }

    // =======================================================================
    //  Round  (ties away from zero)
    // =======================================================================
    bool test_math_round()
    {
        static_assert(mlwRound(0.5) == 1.0);
        static_assert(mlwRound(-0.5) == -1.0);
        static_assert(mlwRound(2.5) == 3.0);      // tie away, not to-even
        static_assert(mlwRound(-2.5) == -3.0);
        static_assert(mlwRound(1.4) == 1.0);
        static_assert(mlwRound(1.6) == 2.0);
        static_assert(mlwRound(-1.6) == -2.0);
        static_assert(mlwRound(0.4) == 0.0);
        // |x|<0.5 -> signed zero, bit-exact
        static_assert(mlwBitCast<uint64>(mlwRound(-0.25)) == mlwBitCast<uint64>(-0.0));
        static_assert(mlwRound(INF64) == INF64);
        static_assert(mlwIsNaN(mlwRound(NAN64)));
        static_assert(mlwRound(2.5f) == 3.0f && mlwRound(-0.5f) == -1.0f);
        return true;
    }

    // =======================================================================
    //  Split  (integral + fractional; both carry sign, sum == x)
    // =======================================================================
    bool test_math_split()
    {
        constexpr DoubleParts a = mlwSplit(3.5);
        static_assert(a.integral == 3.0 && a.fractional == 0.5);
        constexpr DoubleParts b = mlwSplit(-3.25);
        static_assert(b.integral == -3.0 && b.fractional == -0.25);
        constexpr DoubleParts c = mlwSplit(42.0);        // no fractional part
        static_assert(c.integral == 42.0 && c.fractional == 0.0);
        // fractional of a whole number keeps the sign (musl semantics)
        static_assert(mlwBitCast<uint64>(mlwSplit(-42.0).fractional) == mlwBitCast<uint64>(-0.0));
        // |x|<1: integral is signed zero, fractional == x
        constexpr DoubleParts d = mlwSplit(-0.75);
        static_assert(d.fractional == -0.75);
        static_assert(mlwBitCast<uint64>(d.integral) == mlwBitCast<uint64>(-0.0));
        // f32 overload
        constexpr FloatParts e = mlwSplit(2.25f);
        static_assert(e.integral == 2.0f && e.fractional == 0.25f);
        return true;
    }

    // =======================================================================
    //  Mod  (fmod; result has sign of x)
    // =======================================================================
    bool test_math_mod()
    {
        static_assert(mlwMod(5.0, 3.0) == 2.0);
        static_assert(mlwMod(7.5, 2.5) == 0.0);
        static_assert(mlwMod(-7.0, 4.0) == -3.0);   // sign of x
        static_assert(mlwMod(7.0, -4.0) == 3.0);
        static_assert(mlwMod(4.0, 4.0) == 0.0);
        static_assert(mlwMod(1.0, 8.0) == 1.0);     // |x| < |y| -> x
        // x mod inf == x
        static_assert(mlwMod(3.0, INF64) == 3.0);
        // f32 overload
        static_assert(mlwMod(5.0f, 3.0f) == 2.0f && mlwMod(-7.0f, 4.0f) == -3.0f);
        // x mod 0 -> NaN. The implementation computes (x*y)/(x*y) == 0.0/0.0,
        // which is UB in a constant expression, so this one case is runtime.
        if (!mlwIsNaN(mlwMod(1.0, 0.0))) return false;
        if (!mlwIsNaN(mlwMod(1.0f, 0.0f))) return false;
        return true;
    }

    // =======================================================================
    //  Sqrt  (hardware; runtime-only — not constexpr)
    // =======================================================================
    bool test_math_sqrt()
    {
        if (mlwSqrt(0.0) != 0.0) return false;
        if (mlwSqrt(1.0) != 1.0) return false;
        if (mlwSqrt(4.0) != 2.0) return false;
        if (mlwSqrt(144.0) != 12.0) return false;    // exact
        if (mlwSqrt(2.0) * mlwSqrt(2.0) < 1.999999999 ||
            mlwSqrt(2.0) * mlwSqrt(2.0) > 2.000000001) return false;
        if (mlwSqrt(9.0f) != 3.0f) return false;
        if (!mlwIsInf(mlwSqrt(INF64))) return false;
        return true;
    }

    // =======================================================================
    //  Fma  (single-rounding; runtime-only)
    // =======================================================================
    bool test_math_fma()
    {
        if (mlwFma(2.0, 3.0, 4.0) != 10.0) return false;
        if (mlwFma(0.0, 123.0, 5.0) != 5.0) return false;
        if (mlwFma(-2.0, 3.0, 1.0) != -5.0) return false;
        if (mlwFma(2.0f, 3.0f, 4.0f) != 10.0f) return false;
        return true;
    }

    // =======================================================================
    //  Logarithms  (out-of-line; runtime; tolerance-checked)
    // =======================================================================
    bool test_math_log()
    {
        constexpr f64 E  = 2.718281828459045;
        constexpr f64 tol = 1.0e-12;
        if (!close(mlwLog(1.0), 0.0, tol)) return false;
        if (!close(mlwLog(E), 1.0, 1.0e-9)) return false;
        if (!close(mlwLog2(1.0), 0.0, tol)) return false;
        if (!close(mlwLog2(1024.0), 10.0, 1.0e-9)) return false;
        if (!close(mlwLog2(0.5), -1.0, 1.0e-9)) return false;
        if (!close(mlwLog10(1.0), 0.0, tol)) return false;
        if (!close(mlwLog10(1000.0), 3.0, 1.0e-9)) return false;
        if (!closef(mlwLog2(8.0f), 3.0f, 1.0e-5f)) return false;
        return true;
    }

    // =======================================================================
    //  Exponentials  (out-of-line; runtime; tolerance-checked)
    // =======================================================================
    bool test_math_exp()
    {
        constexpr f64 E = 2.718281828459045;
        if (!close(mlwExp(0.0), 1.0, 1.0e-12)) return false;
        if (!close(mlwExp(1.0), E, 1.0e-9)) return false;
        if (!close(mlwExp2(0.0), 1.0, 1.0e-12)) return false;
        if (!close(mlwExp2(10.0), 1024.0, 1.0e-6)) return false;
        // exp10 is documented as up to 4 ulp — keep the tolerance generous
        if (!close(mlwExp10(0.0), 1.0, 1.0e-12)) return false;
        if (!close(mlwExp10(3.0), 1000.0, 1.0e-6)) return false;
        if (!closef(mlwExp2(4.0f), 16.0f, 1.0e-3f)) return false;
        return true;
    }

    // =======================================================================
    //  Power / cube root  (out-of-line; runtime; tolerance-checked)
    // =======================================================================
    bool test_math_pow()
    {
        if (!close(mlwPow(2.0, 10.0), 1024.0, 1.0e-9)) return false;
        if (!close(mlwPow(5.0, 0.0), 1.0, 1.0e-12)) return false;
        if (!close(mlwPow(9.0, 0.5), 3.0, 1.0e-9)) return false;
        if (!close(mlwPow(2.0, -1.0), 0.5, 1.0e-12)) return false;
        if (!closef(mlwPow(3.0f, 3.0f), 27.0f, 1.0e-3f)) return false;
        return true;
    }

    bool test_math_cbrt()
    {
        if (!close(mlwCbrt(27.0), 3.0, 1.0e-9)) return false;
        if (!close(mlwCbrt(0.0), 0.0, 1.0e-12)) return false;
        if (!close(mlwCbrt(-8.0), -2.0, 1.0e-9)) return false;   // sign preserved
        if (!closef(mlwCbrt(64.0f), 4.0f, 1.0e-4f)) return false;
        return true;
    }
}
