#pragma once
#include "../traits.h"
#include "../bit.h"

namespace core
{
    MLW_FORCE_INLINE f64 mlwCopySign(f64 magnitude, f64 sign)
    {
        uint64 valueBits = mlwBitCast<uint64>(magnitude);
        uint64 signBits = mlwBitCast<uint64>(sign);

        valueBits &= 0x7FFFFFFFFFFFFFFFULL;            // Clear sign bit.
        valueBits |= signBits & 0x8000000000000000ULL; // Copy sign bit.

        return mlwBitCast<f64>(valueBits);
    }
    MLW_FORCE_INLINE f32 mlwCopySign(f32 magnitude, f32 sign)
    {
        uint32 valueBits = mlwBitCast<uint32>(magnitude);
        uint32 signBits = mlwBitCast<uint32>(sign);

        valueBits &= 0x7FFFFFFFU;
        valueBits |= signBits & 0x80000000U;

        return mlwBitCast<f32>(valueBits);
    }

    MLW_FORCE_INLINE bool mlwIsInf(f64 value)
    {

        uint64 bits = mlwBitCast<uint64>(value);
        return (bits & 0x7FFFFFFFFFFFFFFFULL) == 0x7FF0000000000000ULL;
    }
    MLW_FORCE_INLINE bool mlwIsInf(f32 value)
    {
        uint32 bits = mlwBitCast<uint32>(value);
        return (bits & 0x7FFFFFFF) == 0x7F800000;
    }
    MLW_FORCE_INLINE bool mlwIsNaN(f64 value)
    {
        uint64 b = mlwBitCast<uint64>(value);
        return (b & 0x7FFFFFFFFFFFFFFFULL) > 0x7FF0000000000000ULL;
    }
    MLW_FORCE_INLINE bool mlwIsNaN(f32 value)
    {
        uint32 bits = mlwBitCast<uint32>(value);
        return (bits & 0x7FFFFFFF) > 0x7F800000;
    }

    template <typename T>
        requires is_float_v<T> || is_integer_v<T>
    MLW_FORCE_INLINE T mlwClamp(T value, T min, T max)
    {
        return value < min ? min : (value > max ? max : value);
    }

    inline f64 mlwPow(f64 base, f64 power) { return 0; };

    f32 mlwPow(f32 base, f32 power);

    f64 mlwLog2(f64 x);
    f32 mlwLog2(f32 x);

    f32 mlwExp(f32 x);
    f64 mlwExp(f64 x);

    inline f64 mlwLog10(f64 x) {
        return 0;
    };


    MLW_FORCE_INLINE f64 mlwFloor(f64 x)
    {
        uint64 bits = mlwBitCast<uint64>(x);

        bool sign = bits >> 63;
        uint32 exp = (bits >> 52) & 0x7FF;

        // NaN or ±Inf
        if (exp == 0x7FF)
            return x;

        int32 e = static_cast<int32>(exp) - 1023;

        // |x| < 1
        if (e < 0)
        {
            // ±0
            if ((bits << 1) == 0)
                return x;

            return sign ? -1.0 : 0.0;
        }

        // Already integral
        if (e >= 52)
            return x;

        uint64 mask = (1ULL << (52 - e)) - 1;

        // Already an integer
        if ((bits & mask) == 0)
            return x;

        bits &= ~mask;

        f64 result = mlwBitCast<f64>(bits);

        if (sign)
            result -= 1.0;

        return result;
    }

    MLW_FORCE_INLINE f32 mlwFloor(f32 x)
    {
        uint32 bits = mlwBitCast<uint32>(x);

        bool sign = bits >> 31;
        uint32 exp = (bits >> 23) & 0xFF;

        // NaN or ±Inf
        if (exp == 0xFF)
            return x;

        int32 e = static_cast<int32>(exp) - 127;

        // |x| < 1
        if (e < 0)
        {
            // ±0
            if ((bits << 1) == 0)
                return x;

            return sign ? -1.0f : 0.0f;
        }

        // Already integral
        if (e >= 23)
            return x;

        uint32 mask = (1u << (23 - e)) - 1;

        // Already an integer
        if ((bits & mask) == 0)
            return x;

        bits &= ~mask;

        f32 result = mlwBitCast<f32>(bits);

        if (sign)
            result -= 1.0f;

        return result;
    }

    MLW_FORCE_INLINE f64 mlwCeil(f64 x)
    {
        uint64 bits = mlwBitCast<uint64>(x);

        bool sign = bits >> 63;
        uint32 exp = (bits >> 52) & 0x7FF;

        // NaN or ±Inf
        if (exp == 0x7FF)
            return x;

        int32 e = static_cast<int32>(exp) - 1023;

        // |x| < 1
        if (e < 0)
        {
            // ±0
            if ((bits << 1) == 0)
                return x;

            return sign ? -0.0 : 1.0;
        }

        // Already integral (no fractional bits exist at this magnitude)
        if (e >= 52)
            return x;

        uint64 mask = (1ULL << (52 - e)) - 1;

        // Already an integer
        if ((bits & mask) == 0)
            return x;

        bits &= ~mask; // truncate toward zero
        f64 result = mlwBitCast<f64>(bits);

        if (!sign) // positive & had a fraction -> round up
            result += 1.0;

        return result;
    }

    MLW_FORCE_INLINE f32 mlwCeil(f32 x)
    {
        uint32 bits = mlwBitCast<uint32>(x);

        bool sign = bits >> 31;
        uint32 exp = (bits >> 23) & 0xFF;

        // NaN or ±Inf
        if (exp == 0xFF)
            return x;

        int32 e = static_cast<int32>(exp) - 127;

        // |x| < 1
        if (e < 0)
        {
            // ±0
            if ((bits << 1) == 0)
                return x;

            return sign ? -1.0f : 0.0f;
        }

        // Already integral
        if (e >= 23)
            return x;

        uint32 mask = (1u << (23 - e)) - 1;

        // Already an integer
        if ((bits & mask) == 0)
            return x;

        bits &= ~mask;

        f32 result = mlwBitCast<f32>(bits);

        if (!sign)
            result += 1.0f;

        return result;
    }

    MLW_FORCE_INLINE f64 mlwTrunc(f64 x)
    {
        uint64 bits = mlwBitCast<uint64>(x);

        uint32 exp = (bits >> 52) & 0x7FF;

        // NaN or ±Inf
        if (exp == 0x7FF)
            return x;

        int32 e = static_cast<int32>(exp) - 1023;

        // |x| < 1  ->  chop to ±0 (preserve sign)
        if (e < 0)
        {
            bits &= 0x8000000000000000ULL; // keep only sign bit
            return mlwBitCast<f64>(bits);
        }

        // Already integral
        if (e >= 52)
            return x;

        uint64 mask = (1ULL << (52 - e)) - 1;

        bits &= ~mask; // chop toward zero — no fixup
        return mlwBitCast<f64>(bits);
    }

    MLW_FORCE_INLINE f32 mlwTrunc(f32 x)
    {
        uint32 bits = mlwBitCast<uint32>(x);

        uint32 exp = (bits >> 23) & 0xFF;

        // NaN or ±Inf
        if (exp == 0xFF)
            return x;

        int32 e = static_cast<int32>(exp) - 127;

        // |x| < 1  ->  chop to ±0 (preserve sign)
        if (e < 0)
        {
            bits &= 0x80000000U; // keep only sign bit
            return mlwBitCast<f32>(bits);
        }

        // Already integral
        if (e >= 23)
            return x;

        uint32 mask = (1u << (23 - e)) - 1;

        // Already an integer
        if ((bits & mask) == 0)
            return x;

        bits &= ~mask;

        f32 result = mlwBitCast<f32>(bits);
        return result;
    }

    MLW_FORCE_INLINE f64 mlwRound(f64 x)
    {
        uint64 bits = mlwBitCast<uint64>(x);

        bool sign = bits >> 63;
        uint32 exp = (bits >> 52) & 0x7FF;

        // NaN or ±Inf
        if (exp == 0x7FF)
            return x;

        int32 e = static_cast<int32>(exp) - 1023;

        // |x| < 1
        if (e < 0)
        {
            // e == -1 means 0.5 <= |x| < 1  -> rounds away to ±1
            if (e == -1)
                return sign ? -1.0 : 1.0;

            // |x| < 0.5  -> ±0 (keep sign)
            bits &= 0x8000000000000000ULL;
            return mlwBitCast<f64>(bits);
        }

        // Already integral (no fractional bits at this magnitude)
        if (e >= 52)
            return x;

        uint64 mask = (1ULL << (52 - e)) - 1; // fractional bits
        uint64 frac = bits & mask;            // capture BEFORE clearing

        // No fractional part
        if (frac == 0)
            return x;

        uint64 half = 1ULL << (51 - e); // the 0.5 threshold bit

        bits &= ~mask; // truncate toward zero
        f64 result = mlwBitCast<f64>(bits);

        if (frac >= half) // >= 0.5 -> step away from zero
            result = sign ? result - 1.0 : result + 1.0;

        return result;
    }

    MLW_FORCE_INLINE f32 mlwRound(f32 x)
    {
        uint32 bits = mlwBitCast<uint32>(x);

        bool sign = bits >> 31;
        uint32 exp = (bits >> 23) & 0xFF;

        // NaN or ±Inf
        if (exp == 0xFF)
            return x;

        int32 e = static_cast<int32>(exp) - 127;

        // |x| < 1
        if (e < 0)
        {
            // e == -1 means 0.5 <= |x| < 1  -> rounds away to ±1
            if (e == -1)
                return sign ? -1.0f : 1.0f;

            // |x| < 0.5  -> ±0 (keep sign)
            bits &= 0x80000000U;
            return mlwBitCast<f32>(bits);
        }

        // Already integral
        if (e >= 23)
            return x;

        uint32 mask = (1u << (23 - e)) - 1; // fractional bits
        uint32 frac = bits & mask;          // capture BEFORE clearing

        // No fractional part
        if (frac == 0)
            return x;

        uint32 half = 1u << (22 - e); // the 0.5 threshold bit

        bits &= ~mask; // truncate toward zero
        f32 result = mlwBitCast<f32>(bits);

        if (frac >= half) // >= 0.5 -> step away from zero
            result = sign ? result - 1.0f : result + 1.0f;

        return result;
    }

    template <typename T>
        requires is_float_v<T> || is_integer_v<T>
    MLW_FORCE_INLINE T mlwMin(T a, T b)
    {
        return a < b ? a : b;
    }

    template <typename T>
        requires is_float_v<T> || is_integer_v<T>
    MLW_FORCE_INLINE T mlwMax(T a, T b)
    {
        return a > b ? a : b;
    }

    template <typename T>
        requires is_integer_v<T> && is_signed_v<T>
    MLW_FORCE_INLINE T mlwAbs(T x)
    {
        return x < 0 ? -x : x;
    };

    MLW_FORCE_INLINE f32 mlwAbs(f32 x)
    {
        uint32 bits = mlwBitCast<uint32>(x);
        bits &= 0x7FFFFFFFU;
        return mlwBitCast<f32>(bits);
    }

    MLW_FORCE_INLINE f64 mlwAbs(f64 x)
    {
        uint64 bits = mlwBitCast<uint64>(x);
        bits &= 0x7FFFFFFFFFFFFFFFULL; // clear sign bit
        return mlwBitCast<f64>(bits);
    }

#if defined(MLW_X84) || defined(MLW_X64)
    // ---------- x86 / x64 : SSE2 ----------
#if defined(MLW_MSVC)

    // MSVC has no __builtin_sqrt. Use the SSE2 scalar intrinsic, but
    // declare the pieces ourselves so we never include <emmintrin.h>
    // (which drags in <malloc.h> and CRT symbols we don't want).
    //
    // These type layouts are MSVC-specific — but only MSVC ever compiles
    // this block, so portability across compilers is not a concern here.
    namespace sqrt_internals
    {
        typedef struct __declspec(intrin_type) __declspec(align(16)) __m128d
        {
            double m128d_f64[2];
        } __m128d;

        typedef union __declspec(intrin_type) __declspec(align(16)) __m128
        {
            float m128_f32[4];
            unsigned __int64 m128_u64[2];
            __int8 m128_i8[16];
            __int16 m128_i16[8];
            __int32 m128_i32[4];
            __int64 m128_i64[2];
            unsigned __int8 m128_u8[16];
            unsigned __int16 m128_u16[8];
            unsigned __int32 m128_u32[4];
        } __m128;

        extern "C" __m128d _mm_set_sd(double);
        extern "C" __m128d _mm_sqrt_sd(__m128d, __m128d);
        extern "C" double _mm_cvtsd_f64(__m128d);

        extern "C" __m128 _mm_set_ss(float);
        extern "C" __m128 _mm_sqrt_ss(__m128);
        extern "C" float _mm_cvtss_f32(__m128);
    } // namespace sqrt_internals;

    MLW_FORCE_INLINE f64 mlwSqrt(f64 x)
    {
        sqrt_internals::__m128d v = sqrt_internals::_mm_set_sd(x);
        v = sqrt_internals::_mm_sqrt_sd(v, v);
        return sqrt_internals::_mm_cvtsd_f64(v);
    }
    MLW_FORCE_INLINE f32 mlwSqrt(f32 x)
    {
        sqrt_internals::__m128 v = sqrt_internals::_mm_set_ss(x);
        v = sqrt_internals::_mm_sqrt_ss(v);
        return sqrt_internals::_mm_cvtss_f32(v);
    }

#else
    // GCC / Clang on x86: single sqrtsd/sqrtss under -fno-math-errno.
    // No header, no types, no malloc.h.
    MLW_FORCE_INLINE f64 mlwSqrt(f64 x) { return __builtin_sqrt(x); }
    MLW_FORCE_INLINE f32 mlwSqrt(f32 x) { return __builtin_sqrtf(x); }
#endif
#elif defined(MLW_ARM64) || defined(MLW_ARM32)
    // ---------- ARM64 : always has FP sqrt ----------
#if defined(MLW_MSVC)
    // MSVC ARM intrinsic: guaranteed single inline VSQRT.
    // (verify __sqrt/__sqrtf availability for your MSVC ARM target)
    MLW_FORCE_INLINE f64 mlwSqrt(f64 x) { return __sqrt(x); }
    MLW_FORCE_INLINE f32 mlwSqrt(f32 x) { return __sqrtf(x); }
#else
    // GCC/Clang: single instruction under -fno-math-errno
    MLW_FORCE_INLINE f64 mlwSqrt(f64 x) { return __builtin_sqrt(x); }
    MLW_FORCE_INLINE f32 mlwSqrt(f32 x) { return __builtin_sqrtf(x); }
#endif
#else
    // ---------- unknown target : leave the door open ----------
#error "mlwSqrt: no hardware sqrt path for this target; add one here"
#endif
    // =================== end mlwSqrt ===================

    MLW_FORCE_INLINE f64 mlwMod(f64 x, f64 y)
    {
        union
        {
            f64 f;
            uint64 i;
        } ux = {x}, uy = {y};
        int ex = ux.i >> 52 & 0x7ff;
        int ey = uy.i >> 52 & 0x7ff;
        int sx = ux.i >> 63;
        uint64 i;

        /* in the followings uxi should be ux.i, but then gcc wrongly adds */
        /* float load/store to inner loops ruining performance and code size */
        uint64 uxi = ux.i;

        if (uy.i << 1 == 0 || mlwIsNaN(y) || ex == 0x7ff)
            return (x * y) / (x * y);
        if (uxi << 1 <= uy.i << 1)
        {
            if (uxi << 1 == uy.i << 1)
                return 0 * x;
            return x;
        }

        /* normalize x and y */
        if (!ex)
        {
            for (i = uxi << 12; i >> 63 == 0; ex--, i <<= 1)
                ;
            uxi <<= -ex + 1;
        }
        else
        {
            uxi &= -1ULL >> 12;
            uxi |= 1ULL << 52;
        }
        if (!ey)
        {
            for (i = uy.i << 12; i >> 63 == 0; ey--, i <<= 1)
                ;
            uy.i <<= -ey + 1;
        }
        else
        {
            uy.i &= -1ULL >> 12;
            uy.i |= 1ULL << 52;
        }

        /* x mod y */
        for (; ex > ey; ex--)
        {
            i = uxi - uy.i;
            if (i >> 63 == 0)
            {
                if (i == 0)
                    return 0 * x;
                uxi = i;
            }
            uxi <<= 1;
        }
        i = uxi - uy.i;
        if (i >> 63 == 0)
        {
            if (i == 0)
                return 0 * x;
            uxi = i;
        }
        for (; uxi >> 52 == 0; uxi <<= 1, ex--)
            ;

        /* scale result */
        if (ex > 0)
        {
            uxi -= 1ULL << 52;
            uxi |= (uint64)ex << 52;
        }
        else
        {
            uxi >>= -ex + 1;
        }
        uxi |= (uint64)sx << 63;
        ux.i = uxi;
        return ux.f;
    }

    MLW_FORCE_INLINE f32 mlwMod(f32 x, f32 y)
    {
        union
        {
            f32 f;
            uint32 i;
        } ux = {x}, uy = {y};
        int ex = ux.i >> 23 & 0xff;
        int ey = uy.i >> 23 & 0xff;
        uint32 sx = ux.i & 0x80000000;
        uint32 i;
        uint32 uxi = ux.i;

        if (uy.i << 1 == 0 || mlwIsNaN(y) || ex == 0xff)
            return (x * y) / (x * y);
        if (uxi << 1 <= uy.i << 1)
        {
            if (uxi << 1 == uy.i << 1)
                return 0 * x;
            return x;
        }

        /* normalize x and y */
        if (!ex)
        {
            for (i = uxi << 9; i >> 31 == 0; ex--, i <<= 1)
                ;
            uxi <<= -ex + 1;
        }
        else
        {
            uxi &= -1U >> 9;
            uxi |= 1U << 23;
        }
        if (!ey)
        {
            for (i = uy.i << 9; i >> 31 == 0; ey--, i <<= 1)
                ;
            uy.i <<= -ey + 1;
        }
        else
        {
            uy.i &= -1U >> 9;
            uy.i |= 1U << 23;
        }

        /* x mod y */
        for (; ex > ey; ex--)
        {
            i = uxi - uy.i;
            if (i >> 31 == 0)
            {
                if (i == 0)
                    return 0 * x;
                uxi = i;
            }
            uxi <<= 1;
        }
        i = uxi - uy.i;
        if (i >> 31 == 0)
        {
            if (i == 0)
                return 0 * x;
            uxi = i;
        }
        for (; uxi >> 23 == 0; uxi <<= 1, ex--)
            ;

        /* scale result up */
        if (ex > 0)
        {
            uxi -= 1U << 23;
            uxi |= (uint32)ex << 23;
        }
        else
        {
            uxi >>= -ex + 1;
        }
        uxi |= sx;
        ux.i = uxi;
        return ux.f;
    }

    f32 mlwCbrt(f32);
    f64 mlwCbrt(f64);
}
