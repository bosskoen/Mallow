// core/core/tests/test_bit.cpp
//
// Coverage for core/bit.h (mlwBitCast). It is constexpr and just wraps
// __builtin_bit_cast, so the interesting checks are compile-time: known bit
// patterns and round-tripping. The size mismatch guard is a static_assert
// inside the template, so it can only be checked by *not* instantiating a bad
// call — noted below.

#include "core/bit.h"

using namespace core;

namespace core_core_test
{
    // ---- known IEEE-754 patterns ------------------------------------------
    bool test_bit_cast_float_pattern()
    {
        static_assert(mlwBitCast<uint32>(1.0f)  == 0x3F800000u);
        static_assert(mlwBitCast<uint32>(2.0f)  == 0x40000000u);
        static_assert(mlwBitCast<uint32>(0.0f)  == 0x00000000u);
        static_assert(mlwBitCast<uint64>(1.0)   == 0x3FF0000000000000ull);
        return true;
    }

    // ---- round-trip: bits -> T -> bits is identity ------------------------
    bool test_bit_cast_round_trip()
    {
        static_assert(mlwBitCast<f32>(mlwBitCast<uint32>(3.14159f)) == 3.14159f);
        static_assert(mlwBitCast<f64>(mlwBitCast<uint64>(2.71828))  == 2.71828);
        // signed<->unsigned reinterpret keeps the bits
        static_assert(mlwBitCast<uint32>(int32(-1)) == 0xFFFFFFFFu);
        static_assert(mlwBitCast<int32>(0xFFFFFFFFu) == int32(-1));
        return true;
    }

    // ---- pointer-sized reinterpret ----------------------------------------
    bool test_bit_cast_pointer_bits()
    {
        int x = 0;
        int* p = &x;
        uptr as_int = mlwBitCast<uptr>(p);
        int* back    = mlwBitCast<int*>(as_int);
        return back == p;
    }

    // NOTE: mlwBitCast<To,From> with sizeof(To) != sizeof(From) is a hard
    // static_assert failure by design, so it cannot be exercised at runtime
    // without breaking the build. If you use a compile-fail harness, add:
    //     mlwBitCast<uint64>(1.0f);   // sizeof mismatch -> should NOT compile
} // namespace core_core_test
