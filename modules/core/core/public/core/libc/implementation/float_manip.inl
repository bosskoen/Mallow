#pragma once

// float_manip.inl  —  Mallow float manipulation
//
// ---------------------------------------------------------------------------
// mlwMod (f32/f64) and mlwSplit (f32/f64) are derived from musl libc:
//   src/math/fmod.c, fmodf.c, modf.c, modff.c
// musl is MIT-licensed. The required notice is reproduced below and applies to
// those functions only; the remaining functions in this file are original and
// covered by the project license above.
//
//   Copyright (c) 2005-2020 Rich Felker, et al.
//
//   Permission is hereby granted, free of charge, to any person obtaining
//   a copy of this software and associated documentation files (the
//   "Software"), to deal in the Software without restriction, including
//   without limitation the rights to use, copy, modify, merge, publish,
//   distribute, sublicense, and/or sell copies of the Software, and to
//   permit persons to whom the Software is furnished to do so, subject to
//   the following conditions:
//
//   The above copyright notice and this permission notice shall be
//   included in all copies or substantial portions of the Software.
//
//   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
//   OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
//   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
//   THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
//   OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
//   ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
//   OTHER DEALINGS IN THE SOFTWARE.
// ---------------------------------------------------------------------------

namespace core
{

    MLW_FORCE_INLINE constexpr f64 mlwFloor(f64 x) noexcept
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

    MLW_FORCE_INLINE constexpr f32 mlwFloor(f32 x) noexcept
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

    MLW_FORCE_INLINE constexpr f64 mlwCeil(f64 x) noexcept
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

    MLW_FORCE_INLINE constexpr f32 mlwCeil(f32 x) noexcept
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

    MLW_FORCE_INLINE constexpr f64 mlwTrunc(f64 x) noexcept
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

    MLW_FORCE_INLINE constexpr f32 mlwTrunc(f32 x) noexcept
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

    // Derived from musl libc (src/math/modff.c). MIT — see file header.
    MLW_FORCE_INLINE constexpr FloatParts mlwSplit(f32 x) noexcept
    {
        uint32 bits = mlwBitCast<uint32>(x);
        uint32 mask;
        int e = (int)(bits >> 23 & 0xff) - 0x7f;

        FloatParts parts;

        /* no fractional part */
        if (e >= 23)
        {
            parts.integral = x;
            if (e == 0x80 && bits << 9 != 0)
            { /* nan */
                parts.fractional = x;
                return parts;
            }
            bits &= 0x80000000;
            parts.fractional = mlwBitCast<f32>(bits);
            return parts;
        }
        /* no integral part */
        if (e < 0)
        {
            bits &= 0x80000000;
            parts.integral = mlwBitCast<f32>(bits);;
            parts.fractional = x;
            return parts;
        }

        mask = 0x007fffff >> e;
        if ((bits & mask) == 0)
        {
            parts.integral = x;
            bits &= 0x80000000;
            parts.fractional = mlwBitCast<f32>(bits);;
            return parts;
        }
        bits &= ~mask;
        parts.integral = mlwBitCast<f32>(bits);;
        parts.fractional = x - parts.integral;
        return parts;
    }

    // Derived from musl libc (src/math/modf.c). MIT — see file header.
    MLW_FORCE_INLINE constexpr DoubleParts mlwSplit(f64 x) noexcept
    {
		uint64 bits = mlwBitCast<uint64>(x);
        uint64 mask;
        int e = (int)(bits >> 52 & 0x7ff) - 0x3ff;

        DoubleParts parts;

        /* no fractional part */
        if (e >= 52)
        {
            parts.integral = x;
            if (e == 0x400 && bits << 12 != 0)
            { /* nan */
                parts.fractional = x;
                return parts;
            }
            bits &= 1ULL << 63;
            parts.fractional = mlwBitCast<f64>(bits);
            return parts;
        }

        /* no integral part*/
        if (e < 0)
        {
            bits &= 1ULL << 63;
            parts.integral = mlwBitCast<f64>(bits);
            parts.fractional = x;
            return parts;
        }

        mask = (~0ULL) >> 12 >> e;
        if ((bits & mask) == 0)
        {
            parts.integral = x;
            bits &= 1ULL << 63;
            parts.fractional = mlwBitCast<f64>(bits);
            return parts;
        }
        bits &= ~mask;
        parts.integral = mlwBitCast<f64>(bits);
        parts.fractional = x - parts.integral;
        return parts;
    }

    MLW_FORCE_INLINE constexpr f64 mlwRound(f64 x) noexcept
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

    MLW_FORCE_INLINE constexpr f32 mlwRound(f32 x) noexcept
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

    // Derived from musl libc (src/math/fmod.c). MIT — see file header.
    MLW_FORCE_INLINE constexpr f64 mlwMod(f64 x, f64 y) noexcept
    {
        uint64 uxi = mlwBitCast<uint64>(x);
        uint64 uyi = mlwBitCast<uint64>(y);
        int ex = uxi >> 52 & 0x7ff;
        int ey = uyi >> 52 & 0x7ff;
        int sx = uxi >> 63;
        uint64 i;

        if (uyi << 1 == 0 || mlwIsNaN(y) || ex == 0x7ff)
            return (x * y) / (x * y);
        if (uxi << 1 <= uyi << 1)
        {
            if (uxi << 1 == uyi << 1)
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
            uxi &= (~0ULL) >> 12;
            uxi |= 1ULL << 52;
        }
        if (!ey)
        {
            for (i = uyi << 12; i >> 63 == 0; ey--, i <<= 1)
                ;
            uyi <<= -ey + 1;
        }
        else
        {
            uyi &= (~0ULL) >> 12;
            uyi |= 1ULL << 52;
        }
        /* x mod y */
        for (; ex > ey; ex--)
        {
            i = uxi - uyi;
            if (i >> 63 == 0)
            {
                if (i == 0)
                    return 0 * x;
                uxi = i;
            }
            uxi <<= 1;
        }
        i = uxi - uyi;
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
        return mlwBitCast<f64>(uxi);
    }

    // Derived from musl libc (src/math/fmodf.c). MIT — see file header.
    MLW_FORCE_INLINE constexpr f32 mlwMod(f32 x, f32 y) noexcept
    {
        uint32 uxi = mlwBitCast<uint32>(x);
        uint32 uyi = mlwBitCast<uint32>(y);
        int ex = uxi >> 23 & 0xff;
        int ey = uyi >> 23 & 0xff;
        uint32 sx = uxi & 0x80000000;
        uint32 i;

        if (uyi << 1 == 0 || mlwIsNaN(y) || ex == 0xff)
            return (x * y) / (x * y);
        if (uxi << 1 <= uyi << 1)
        {
            if (uxi << 1 == uyi << 1)
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
            uxi &= (~0U) >> 9;
            uxi |= 1U << 23;
        }
        if (!ey)
        {
            for (i = uyi << 9; i >> 31 == 0; ey--, i <<= 1)
                ;
            uyi <<= -ey + 1;
        }
        else
        {
            uyi &= (~0U) >> 9;
            uyi |= 1U << 23;
        }
        /* x mod y */
        for (; ex > ey; ex--)
        {
            i = uxi - uyi;
            if (i >> 31 == 0)
            {
                if (i == 0)
                    return 0 * x;
                uxi = i;
            }
            uxi <<= 1;
        }
        i = uxi - uyi;
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
        return mlwBitCast<f32>(uxi);
    }

}