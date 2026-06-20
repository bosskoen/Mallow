#pragma once
#include "../compilers.h"
#include "../traits.h"

namespace
{
    extern "C" double copysign(double x, double y);
    extern "C" float copysignf(float x, float y);

#if !defined(MLW_MSVC)
    extern "C" int isinf(double x);
    extern "C" int isinff(float x);
#endif

    extern "C" double pow(double base, double power);
    extern "C" float powf(float base, float power);

    extern "C" double log2(double x);
    extern "C" float log2f(float x);
    extern "C" double log10(double x);
    extern "C" float log10f(float x);

    extern "C" double floor(double x);
    extern "C" float floorf(float x);
}

namespace core
{
    MLW_FORCE_INLINE f64 mlwCopySign(f64 magnitude, f64 sign)
    {
        return copysign(magnitude, sign);
    }
    MLW_FORCE_INLINE f32 mlwCopySign(f32 magnitude, f32 sign)
    {
        return copysignf(magnitude, sign);
    }

    MLW_FORCE_INLINE bool mlwIsInf(f64 value)
    {
#if defined(MLW_MSVC)
        uint64 bits;
        mlwMemcpy(&bits, &value, sizeof(bits));
        return (bits & 0x7FFFFFFFFFFFFFFF) == 0x7FF0000000000000;
#else
        return isinf(value) != 0;
#endif
    }
    MLW_FORCE_INLINE bool mlwIsInf(f32 value)
    {
#if defined(MLW_MSVC)
        uint32 bits;
        mlwMemcpy(&bits, &value, sizeof(bits));
        return (bits & 0x7FFFFFFF) == 0x7F800000;
#else
        return isinff(value) != 0;
#endif
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

    MLW_FORCE_INLINE f64 mlwPow(f64 base, f64 power)
    {
        return pow(base, power);
    }

    MLW_FORCE_INLINE f32 mlwPow(f32 base, f32 power)
    {
        return powf(base, power);
    }

    MLW_FORCE_INLINE f64 mlwLog2(f64 x)
    {
        return log2(x);
    }
    MLW_FORCE_INLINE f32 mlwLog2(f32 x)
    {
        return log2f(x);
    }
    MLW_FORCE_INLINE f64 mlwLog10(f64 x)
    {
        return log10(x);
    }
    MLW_FORCE_INLINE f32 mlwLog10(f32 x)
    {
        return log10f(x);
    }

    MLW_FORCE_INLINE f64 mlwFloor(f64 x)
    {
        return floor(x);
    }
    MLW_FORCE_INLINE f32 mlwFloor(f32 x)
    {
        return floorf(x);
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