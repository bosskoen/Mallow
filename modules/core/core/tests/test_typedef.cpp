// core/core/tests/test_typedef.cpp
//
// Locks down the fundamental type aliases and NumericLimits. These are ABI
// promises, so they are asserted at compile time; a regression here is a build
// break rather than a silent runtime surprise.

#include "core/typedef.h"
#include "core/traits.h"   // is_signed_v / is_same_v for the checks below

using namespace core;

namespace core_core_test
{
    // ---- exact widths ------------------------------------------------------
    bool test_typedef_sizes()
    {
        static_assert(sizeof(int8)  == 1 && sizeof(uint8)  == 1);
        static_assert(sizeof(int16) == 2 && sizeof(uint16) == 2);
        static_assert(sizeof(int32) == 4 && sizeof(uint32) == 4);
        static_assert(sizeof(int64) == 8 && sizeof(uint64) == 8);
        static_assert(sizeof(f32) == 4 && sizeof(f64) == 8);
        static_assert(sizeof(uptr) == sizeof(void*));
        static_assert(sizeof(usize) == sizeof(void*));
        return true;
    }

    // ---- signedness --------------------------------------------------------
    bool test_typedef_signedness()
    {
        static_assert(int8(-1)  < int8(0));
        static_assert(uint8(-1) > uint8(0));       // wraps to 0xFF
        static_assert(int64(-1) < int64(0));
        static_assert(uint64(0) - uint64(1) > 0);  // unsigned wrap
        static_assert(is_same_v<sint, int32> && is_same_v<uint, uint32>);
        return true;
    }

    // ---- NumericLimits integers -------------------------------------------
    bool test_typedef_numeric_limits_int()
    {
        static_assert(NumericLimits<uint8>::max  == 0xFF);
        static_assert(NumericLimits<uint8>::min  == 0);
        static_assert(NumericLimits<int8>::max   == 127);
        static_assert(NumericLimits<int8>::min   == -128);
        static_assert(NumericLimits<uint16>::max == 0xFFFF);
        static_assert(NumericLimits<int32>::max  == 2147483647);
        static_assert(NumericLimits<int32>::min  == -2147483647 - 1);
        static_assert(NumericLimits<uint64>::max == 0xFFFFFFFFFFFFFFFFull);
        static_assert(NumericLimits<int64>::min  < 0);
        static_assert(NumericLimits<int64>::max  > 0);
        // min/max are actually the extremes: nothing smaller/larger wraps past
        static_assert(uint8(NumericLimits<uint8>::max + 1) == NumericLimits<uint8>::min);
        return true;
    }

    // ---- NumericLimits floats ---------------------------------------------
    bool test_typedef_numeric_limits_float()
    {
        // epsilon is positive and small; infinity beats max; nan != nan.
        static_assert(NumericLimits<f32>::epsilon > 0.0f);
        static_assert(NumericLimits<f64>::epsilon > 0.0);
        // These are runtime because the builtins aren't usable in static_assert
        // on every toolchain; keep them as ordinary checks.
        const f64 inf = NumericLimits<f64>::infinity;
        const f64 nan = NumericLimits<f64>::nan;
        if (!(inf > NumericLimits<f64>::max)) return false;
        if (nan == nan) return false;                 // the defining NaN property
        if (!(NumericLimits<f32>::max > 0.0f))  return false;
        if (!(NumericLimits<f32>::min < 0.0f))  return false;   // note: this "min" is -max, not the smallest positive
        return true;
    }

    // 128-bit types only exist on GCC/Clang + x64/arm64. The function is always
    // defined (the runner discovers names textually, so it cannot be compiled
    // out) and simply skips — returns true — where i128 is unavailable.
    bool test_typedef_i128_present()
    {
#ifndef MLW_NO_I128
        static_assert(sizeof(int128) == 16 && sizeof(uint128) == 16);
        static_assert(NumericLimits<uint128>::min == 0);
        static_assert(NumericLimits<uint128>::max == ~static_cast<uint128>(0));
        static_assert(NumericLimits<int128>::max > 0);
        static_assert(NumericLimits<int128>::min < 0);
#endif
        return true;
    }
} // namespace core_core_test
