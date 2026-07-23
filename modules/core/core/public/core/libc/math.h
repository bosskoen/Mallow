#pragma once

/// \file math.h
/// \brief Public scalar math API: float classification, rounding, logs/exponentials,
///        power, and single-instruction hardware sqrt / fma.
/// \note Inline bit helpers are defined here; rounding/decomposition live in
///       implementation/float_manip.inl (included at the bottom, constexpr); transcendentals
///       are defined out-of-line in the math TU.
/// \note float_manip.inl is a textual include fragment of this header, not a standalone
///       header — do not \c #include it directly.

#include "../traits.h"
#include "../bit.h"

namespace core
{
    // ========================================================================
    //  Sign manipulation & classification
    // ========================================================================

    /// \brief Return \p magnitude carrying the sign bit of \p sign.
    /// \param magnitude Value supplying the magnitude (its own sign is discarded).
    /// \param sign      Value supplying the sign bit.
    /// \return |\p magnitude| with the sign of \p sign.
    /// \{
    MLW_FORCE_INLINE constexpr f64 mlwCopySign(f64 magnitude, f64 sign) noexcept
    {
        uint64 valueBits = mlwBitCast<uint64>(magnitude);
        uint64 signBits  = mlwBitCast<uint64>(sign);

        valueBits &= 0x7FFFFFFFFFFFFFFFULL;             // clear sign bit
        valueBits |= signBits & 0x8000000000000000ULL;  // copy sign bit

        return mlwBitCast<f64>(valueBits);
    }
    MLW_FORCE_INLINE constexpr f32 mlwCopySign(f32 magnitude, f32 sign) noexcept
    {
        uint32 valueBits = mlwBitCast<uint32>(magnitude);
        uint32 signBits  = mlwBitCast<uint32>(sign);

        valueBits &= 0x7FFFFFFFU;
        valueBits |= signBits & 0x80000000U;

        return mlwBitCast<f32>(valueBits);
    }
    /// \}

    /// \brief Test whether \p value is ±infinity.
    /// \param value Value to classify.
    /// \return \c true iff \p value is positive or negative infinity.
    /// \{
    MLW_FORCE_INLINE constexpr bool mlwIsInf(f64 value) noexcept
    {
        uint64 bits = mlwBitCast<uint64>(value);
        return (bits & 0x7FFFFFFFFFFFFFFFULL) == 0x7FF0000000000000ULL;
    }
    MLW_FORCE_INLINE constexpr bool mlwIsInf(f32 value) noexcept
    {
        uint32 bits = mlwBitCast<uint32>(value);
        return (bits & 0x7FFFFFFF) == 0x7F800000;
    }
    /// \}

    /// \brief Test whether \p value is NaN.
    /// \param value Value to classify.
    /// \return \c true iff \p value is Not-a-Number.
    /// \{
    MLW_FORCE_INLINE constexpr bool mlwIsNaN(f64 value) noexcept
    {
        uint64 b = mlwBitCast<uint64>(value);
        return (b & 0x7FFFFFFFFFFFFFFFULL) > 0x7FF0000000000000ULL;
    }
    MLW_FORCE_INLINE constexpr bool mlwIsNaN(f32 value) noexcept
    {
        uint32 bits = mlwBitCast<uint32>(value);
        return (bits & 0x7FFFFFFF) > 0x7F800000;
    }
    /// \}

    // ========================================================================
    //  Clamp / min / max
    // ========================================================================

    /// \brief Clamp \p value to [\p min, \p max].
    /// \param value Value to clamp.
    /// \param min   Lower bound.
    /// \param max   Upper bound.
    /// \return \p min if below, \p max if above, otherwise \p value.
    /// \pre \p min <= \p max (not checked).
    template <typename T>
        requires is_float_v<T> || is_integer_v<T>
    MLW_FORCE_INLINE constexpr T mlwClamp(T value, T min, T max) noexcept
    {
        return value < min ? min : (value > max ? max : value);
    }

    /// \brief Smaller of \p a and \p b.
    template <typename T>
        requires is_float_v<T> || is_integer_v<T>
    MLW_FORCE_INLINE constexpr T mlwMin(T a, T b) noexcept
    {
        return a < b ? a : b;
    }

    /// \brief Larger of \p a and \p b.
    template <typename T>
        requires is_float_v<T> || is_integer_v<T>
    MLW_FORCE_INLINE constexpr T mlwMax(T a, T b) noexcept
    {
        return a > b ? a : b;
    }

    // ========================================================================
    //  Absolute value
    // ========================================================================

    /// \brief Absolute value of a signed integer.
    /// \warning \c mlwAbs(T_MIN) overflows — its magnitude is not representable.
    template <typename T>
        requires is_integer_v<T> && is_signed_v<T>
    MLW_FORCE_INLINE constexpr T mlwAbs(T x) noexcept
    {
        return x < 0 ? -x : x;
    }

    /// \brief Absolute value of a float (NaN/inf preserved).
    /// \{
    MLW_FORCE_INLINE constexpr f32 mlwAbs(f32 x) noexcept
    {
        uint32 bits = mlwBitCast<uint32>(x);
        bits &= 0x7FFFFFFFU;
        return mlwBitCast<f32>(bits);
    }
    MLW_FORCE_INLINE constexpr f64 mlwAbs(f64 x) noexcept
    {
        uint64 bits = mlwBitCast<uint64>(x);
        bits &= 0x7FFFFFFFFFFFFFFFULL;
        return mlwBitCast<f64>(bits);
    }
    /// \}

    // ========================================================================
    //  Logarithms  (defined out-of-line in the math TU)
    // ========================================================================

    /// \brief Natural logarithm (base e).
    /// \param x Input.
    /// \return ln(\p x).
    /// \note Max error < 1 ulp.
    /// \{
    f64 mlwLog(f64 x);
    f32 mlwLog(f32 x);
    /// \}

    /// \brief Base-2 logarithm.
    /// \param x Input.
    /// \return log2(\p x).
    /// \note Max error < 1 ulp.
    /// \{
    f64 mlwLog2(f64 x);
    f32 mlwLog2(f32 x);
    /// \}

    /// \brief Base-10 logarithm.
    /// \param x Input.
    /// \return log10(\p x).
    /// \note Max error < 1 ulp.
    /// \{
    f64 mlwLog10(f64 x);
    f32 mlwLog10(f32 x);
    /// \}

    // ========================================================================
    //  Exponentials  (defined out-of-line in the math TU)
    // ========================================================================

    /// \brief Base-e exponential.
    /// \param x Exponent.
    /// \return e^\p x.
    /// \note Max error < 1 ulp.
    /// \{
    f32 mlwExp(f32 x);
    f64 mlwExp(f64 x);
    /// \}

    /// \brief Base-2 exponential.
    /// \param x Exponent.
    /// \return 2^\p x.
    /// \note Max error < 1 ulp.
    /// \{
    f32 mlwExp2(f32 x);
    f64 mlwExp2(f64 x);
    /// \}

    /// \brief Base-10 exponential.
    /// \param x Exponent.
    /// \return 10^\p x.
    /// \warning Max error up to 4 ulp. For a 1-ulp result use mlwPow(10, x) instead,
    ///          at ~2.2x the cost (f64) / ~1.15x the cost (f32, release).
    /// \{
    f64 mlwExp10(f64 x);
    f32 mlwExp10(f32 x);
    /// \}

    // ========================================================================
    //  Power  (defined out-of-line in the math TU)
    // ========================================================================

    /// \brief Raise \p base to \p power.
    /// \param base  Base.
    /// \param power Exponent.
    /// \return \p base ^ \p power.
    /// \note Max error < 1 ulp.
    /// \{
    f64 mlwPow(f64 base, f64 power);
    f32 mlwPow(f32 base, f32 power);
    /// \}

    // ========================================================================
    //  Rounding & decomposition  (constexpr; defined in float_manip.inl)
    // ========================================================================

    /// \brief Round toward negative infinity.
    /// \param x Input.
    /// \return Largest integral value <= \p x. NaN/inf/±0 preserved.
    /// \{
    MLW_FORCE_INLINE constexpr f64 mlwFloor(f64 x) noexcept;
    MLW_FORCE_INLINE constexpr f32 mlwFloor(f32 x) noexcept;
    /// \}

    /// \brief Round toward positive infinity.
    /// \param x Input.
    /// \return Smallest integral value >= \p x. NaN/inf/±0 preserved.
    /// \{
    MLW_FORCE_INLINE constexpr f64 mlwCeil(f64 x) noexcept;
    MLW_FORCE_INLINE constexpr f32 mlwCeil(f32 x) noexcept;
    /// \}

    /// \brief Round toward zero (drop the fractional part).
    /// \param x Input.
    /// \return \p x with its fractional part removed. NaN/inf/±0 preserved.
    /// \{
    MLW_FORCE_INLINE constexpr f64 mlwTrunc(f64 x) noexcept;
    MLW_FORCE_INLINE constexpr f32 mlwTrunc(f32 x) noexcept;
    /// \}

    /// \brief Round to nearest integral value, ties away from zero.
    /// \param x Input.
    /// \return \p x rounded to nearest. NaN/inf/±0 preserved.
    /// \{
    MLW_FORCE_INLINE constexpr f64 mlwRound(f64 x) noexcept;
    MLW_FORCE_INLINE constexpr f32 mlwRound(f32 x) noexcept;
    /// \}

    /// \brief Integral + fractional parts of a single-precision value (see mlwSplit).
    struct FloatParts
    {
        f32 integral   = 0.0f;
        f32 fractional = 0.0f;
    };

    /// \brief Integral + fractional parts of a double-precision value (see mlwSplit).
    struct DoubleParts
    {
        f64 integral   = 0.0;
        f64 fractional = 0.0;
    };

    /// \brief Split \p x into integral and fractional parts.
    /// \param x Input.
    /// \return Both parts carry the sign of \p x; their sum equals \p x.
    /// \{
    MLW_FORCE_INLINE constexpr FloatParts  mlwSplit(f32 x) noexcept;
    MLW_FORCE_INLINE constexpr DoubleParts mlwSplit(f64 x) noexcept;
    /// \}

    /// \brief Floating-point remainder of \p x / \p y.
    /// \param x Dividend.
    /// \param y Divisor.
    /// \return \p x - n*\p y (n = \p x/\p y truncated toward zero); result has the sign of \p x.
    /// \{
    MLW_FORCE_INLINE constexpr f64 mlwMod(f64 x, f64 y) noexcept;
    MLW_FORCE_INLINE constexpr f32 mlwMod(f32 x, f32 y) noexcept;
    /// \}

    // ========================================================================
    //  Square root  (single hardware instruction per target)
    // ========================================================================
    //
    // This block branches ARCH-first, then compiler, and hard-errors on an unknown
    // architecture (no software fallback is shipped). The fma block below is deliberately
    // shaped the other way around — see the comment there before "normalising" either.

    /// \cond INTERNAL
#if defined(MLW_MSVC) && (defined(MLW_X86) || defined(MLW_X64))
    // MSVC has no __builtin_sqrt. Declare the SSE2 scalar intrinsics ourselves so we never
    // pull in <emmintrin.h> (which drags in <malloc.h> and CRT symbols we don't want).
    // These layouts are MSVC-specific, and only MSVC compiles this block.
    namespace sqrt_internals
    {
        typedef struct __declspec(intrin_type) __declspec(align(16)) __m128d
        {
            double m128d_f64[2];
        } __m128d;

        typedef union __declspec(intrin_type) __declspec(align(16)) __m128
        {
            float            m128_f32[4];
            unsigned __int64 m128_u64[2];
            __int8           m128_i8[16];
            __int16          m128_i16[8];
            __int32          m128_i32[4];
            __int64          m128_i64[2];
            unsigned __int8  m128_u8[16];
            unsigned __int16 m128_u16[8];
            unsigned __int32 m128_u32[4];
        } __m128;

        extern "C" __m128d _mm_set_sd(double);
        extern "C" __m128d _mm_sqrt_sd(__m128d, __m128d);
        extern "C" double  _mm_cvtsd_f64(__m128d);

        extern "C" __m128  _mm_fmadd_ss(__m128, __m128, __m128);
        extern "C" __m128d _mm_fmadd_sd(__m128d, __m128d, __m128d);

        extern "C" __m128  _mm_set_ss(float);
        extern "C" __m128  _mm_sqrt_ss(__m128);
        extern "C" float   _mm_cvtss_f32(__m128);
    } // namespace sqrt_internals
#endif
    /// \endcond

    /// \brief Square root, one hardware instruction on every supported target.
    /// \param x Input (>= 0).
    /// \return sqrt(\p x).
    /// \note Correctly rounded (hardware). Unknown architecture: build error.
    /// \{
#if defined(MLW_X86) || defined(MLW_X64)
    // ---------- x86 / x64 : SSE2 ----------
#   if defined(MLW_MSVC)
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
#   else
    // GCC / Clang on x86: single sqrtsd/sqrtss under -fno-math-errno.
    MLW_FORCE_INLINE f64 mlwSqrt(f64 x) { return __builtin_sqrt(x); }
    MLW_FORCE_INLINE f32 mlwSqrt(f32 x) { return __builtin_sqrtf(x); }
#   endif
#elif defined(MLW_ARM64) || defined(MLW_ARM32)
    // ---------- ARM : always has FP sqrt ----------
#   if defined(MLW_MSVC)
    // MSVC ARM intrinsic: guaranteed single inline VSQRT.
    // (verify __sqrt/__sqrtf availability for your MSVC ARM target)
    MLW_FORCE_INLINE f64 mlwSqrt(f64 x) { return __sqrt(x); }
    MLW_FORCE_INLINE f32 mlwSqrt(f32 x) { return __sqrtf(x); }
#   else
    // GCC/Clang: single instruction under -fno-math-errno.
    MLW_FORCE_INLINE f64 mlwSqrt(f64 x) { return __builtin_sqrt(x); }
    MLW_FORCE_INLINE f32 mlwSqrt(f32 x) { return __builtin_sqrtf(x); }
#   endif
#else
    // ---------- unknown target : leave the door open ----------
#   error "mlwSqrt: no hardware sqrt path for this target; add one here"
#endif
    /// \}

    // ========================================================================
    //  Cube root  (defined out-of-line in the math TU)
    // ========================================================================

    /// \brief Cube root (sign preserved).
    /// \param x Input.
    /// \return the real cube root of \p x.
    /// \note Max error < 1 ulp.
    /// \{
    f32 mlwCbrt(f32 x);
    f64 mlwCbrt(f64 x);
    /// \}

    // ========================================================================
    //  Fused multiply-add  (single-rounding, one hardware instruction)
    // ========================================================================

    /// \def MLW_HAS_FAST_FMA
    /// \brief 1 when this TU has a true single-rounding hardware FMA, 0 otherwise.
    /// \note Lets callers select an FMA-based kernel only when it is actually cheap.
#if defined(__FMA__)                                                      /* gcc/clang x86/x64 with -mfma      */ \
    || defined(__ARM_FEATURE_FMA)                                         /* gcc/clang armv7 vfpv4 / arm64     */ \
    || defined(__FP_FAST_FMA) || (defined(MLW_MSVC) && defined(__AVX2__)) /* MSVC x86/x64 /arch:AVX2           */ \
    || (defined(MLW_MSVC) && (defined(MLW_ARM64) || defined(MLW_ARM32)))  /* MSVC ARM                          */
#   define MLW_HAS_FAST_FMA 1
#else
#   define MLW_HAS_FAST_FMA 0
#endif

    // Structure note: unlike mlwSqrt, this block branches COMPILER-first. GCC/Clang expose a
    // portable __builtin_fma on every arch, so only MSVC needs per-arch handling; flipping it
    // to arch-first would force redundant fallbacks.

    /// \brief Single-rounding fused multiply-add: \p a * \p b + \p c with one rounding step.
    /// \param a First multiplicand.
    /// \param b Second multiplicand.
    /// \param c Addend.
    /// \return round(\p a * \p b + \p c).
    /// \{
#if defined(MLW_MSVC)
#   if defined(MLW_ARM64) || defined(MLW_ARM32)
    // MSVC ARM: ACLE intrinsics -> single vfma. Declared here to avoid <arm_neon.h>.
    extern "C" double __fma(double, double, double);
    extern "C" float  __fmaf(float, float, float);
    MLW_FORCE_INLINE f64 mlwFma(f64 a, f64 b, f64 c) { return __fma(a, b, c); }
    MLW_FORCE_INLINE f32 mlwFma(f32 a, f32 b, f32 c) { return __fmaf(a, b, c); }
#   else
    // MSVC x86/x64: FMA3 intrinsic -> single vfmadd under /arch:AVX2.
    MLW_FORCE_INLINE f64 mlwFma(f64 a, f64 b, f64 c)
    {
        sqrt_internals::__m128d va = sqrt_internals::_mm_set_sd(a);
        sqrt_internals::__m128d vb = sqrt_internals::_mm_set_sd(b);
        sqrt_internals::__m128d vc = sqrt_internals::_mm_set_sd(c);
        return sqrt_internals::_mm_cvtsd_f64(sqrt_internals::_mm_fmadd_sd(va, vb, vc));
    }
    MLW_FORCE_INLINE f32 mlwFma(f32 a, f32 b, f32 c)
    {
        sqrt_internals::__m128 va = sqrt_internals::_mm_set_ss(a);
        sqrt_internals::__m128 vb = sqrt_internals::_mm_set_ss(b);
        sqrt_internals::__m128 vc = sqrt_internals::_mm_set_ss(c);
        return sqrt_internals::_mm_cvtss_f32(sqrt_internals::_mm_fmadd_ss(va, vb, vc));
    }
#   endif
#else
    // GCC / Clang, any arch: portable single-rounding builtin.
    MLW_FORCE_INLINE f64 mlwFma(f64 a, f64 b, f64 c) { return __builtin_fma(a, b, c); }
    MLW_FORCE_INLINE f32 mlwFma(f32 a, f32 b, f32 c) { return __builtin_fmaf(a, b, c); }
#endif
    /// \}
}

#include "implementation/float_manip.inl"