// core/core/tests/test_compilers.cpp
//
// Coverage for the parts of core/compilers.h that have observable behaviour:
// MLW_CTZ / MLW_CLZ (count trailing / leading zeros on a 64-bit value) and a
// couple of the pass-through macros. The fences, debugbreak, and memory-order
// constants are platform intrinsics with no portable observable result, so
// they are only smoke-checked (that they expand and compile).
//
// IMPORTANT: MLW_CTZ(0) and MLW_CLZ(0) are undefined (the underlying builtins
// are), so every case below uses a non-zero operand.

#include "core/compilers.h"

namespace core_core_test
{
    // ---- MLW_CTZ: number of trailing zero bits ----------------------------
    bool test_ctz()
    {
        if (MLW_CTZ(1ull)              != 0)  return false;  // ...0001
        if (MLW_CTZ(2ull)              != 1)  return false;  // ...0010
        if (MLW_CTZ(8ull)              != 3)  return false;  // ...1000
        if (MLW_CTZ(0b101000ull)       != 3)  return false;  // lowest set bit at 3
        if (MLW_CTZ(1ull << 40)        != 40) return false;
        if (MLW_CTZ(1ull << 63)        != 63) return false;
        // low bit set: trailing zeros is always 0 regardless of the high bits
        if (MLW_CTZ(0xFFFFFFFFFFFFFFFFull) != 0) return false;
        return true;
    }

    // ---- MLW_CLZ: number of leading zero bits (64-bit width) --------------
    bool test_clz()
    {
        if (MLW_CLZ(1ull)              != 63) return false;  // one low bit
        if (MLW_CLZ(2ull)              != 62) return false;
        if (MLW_CLZ(1ull << 40)        != 23) return false;  // 63 - 40
        if (MLW_CLZ(1ull << 63)        != 0)  return false;  // top bit set
        if (MLW_CLZ(0x8000000000000000ull) != 0) return false;
        if (MLW_CLZ(0xFFFFFFFFFFFFFFFFull) != 0) return false;
        return true;
    }

    // ---- ctz/clz relationship for a single set bit ------------------------
    bool test_ctz_clz_single_bit()
    {
        // For a value with exactly one set bit at position k:
        //   CTZ == k   and   CLZ == 63 - k
        for (unsigned k = 0; k < 64; ++k)
        {
            uint64 v = 1ull << k;
            if (MLW_CTZ(v) != k) return false;
            if (MLW_CLZ(v) != 63u - k) return false;
        }
        return true;
    }

    // ---- pass-through macros still yield the operand value ----------------
    bool test_branch_hint_macros()
    {
        int x = 5;
        // MLW_LIKELY/UNLIKELY are hints only; they must not change the value.
        if (!MLW_LIKELY(x == 5)) return false;
        if (MLW_UNLIKELY(x == 6)) return false;
        return true;
    }

    // ---- MLW_LAUNDER is identity on the pointer value ---------------------
    bool test_launder_identity()
    {
        int x = 0;
        int* p = &x;
        return MLW_LAUNDER(p) == p;
    }

    // ---- fences / pause just need to compile and run without effect -------
    bool test_fences_smoke()
    {
        MLW_COMPILER_BARRIER();
        MLW_FENCE_FULL();
        MLW_FENCE_LOAD();
        MLW_FENCE_STORE();
        MLW_CPU_PAUSE();
        return true;
    }
} // namespace core_core_test
