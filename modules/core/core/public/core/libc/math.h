#pragma once
#include "../traits.h"
#include "bit.h"

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
        return (bits & 0x7FFFFFFFFFFFFFFF) == 0x7FF0000000000000;
    }
    MLW_FORCE_INLINE bool mlwIsInf(f32 value)
    {
        uint32 bits = mlwBitCast<uint32>(value);
        return (bits & 0x7FFFFFFF) == 0x7F800000;
    }
    MLW_FORCE_INLINE bool mlwIsNaN(f64 value)
    {
        return value != value;
    }
    MLW_FORCE_INLINE bool mlwIsNaN(f32 value)
    {
        return value != value;
    }

    template <typename T>
        requires is_float_v<T> || is_integer_v<T>
    MLW_FORCE_INLINE T mlwClamp(T value, T min, T max)
    {
        return value < min ? min : (value > max ? max : value);
    }

    f64 mlwPow(f64 base, f64 power);

    f32 mlwPow(f32 base, f32 power);

    f64 mlwLog2(f64 x);
    f32 mlwLog2(f32 x);

    MLW_FORCE_INLINE f64 mlwLoge(f64 x) { return mlwLog2(x) * 0x1.62e42fefa39efp-1; } // * ln2
    MLW_FORCE_INLINE f32 mlwLoge(f32 x) { return (f32)(mlwLog2((f64)x) * 0x1.62e42fefa39efp-1); }

    MLW_FORCE_INLINE f64 mlwLog10(f64 x) { return mlwLog2(x) * 0x1.34413509f79ffp-2; } // * log10(2)
    MLW_FORCE_INLINE f32 mlwLog10(f32 x) { return (f32)(mlwLog2((f64)x) * 0x1.34413509f79ffp-2); }
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
}