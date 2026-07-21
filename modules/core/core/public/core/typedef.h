#pragma once
/// 
/// \file 
/// \brief Common fundamental type aliases used across the core library.
/// 
/// This header provides portable aliases for integer, floating-point,
/// pointer-sized, and indexing types used throughout the project.
/// 

// i8 / u8
using int8 = signed char;
using uint8 = unsigned char;

// i16 / u16
using int16 = short;
using uint16 = unsigned short;

// i32 / u32
#if defined(MLW_MSVC)
using int32 = __int32;
using uint32 = unsigned __int32;
#else
using int32 = __INT32_TYPE__;
using uint32 = __UINT32_TYPE__;
#endif

using uint = uint32; ///< default unsigned integer (32-bit)
using sint = int32;  ///< default signed integer (32-bit). Named sint, not int, to avoid the keyword

// i64 / u64
#if defined(MLW_MSVC)
using int64 = __int64;
using uint64 = unsigned __int64;
#else
using int64 = __INT64_TYPE__;
using uint64 = __UINT64_TYPE__;
#endif

// i128 / u128 — only where natively supported
#if (defined(MLW_GCC) || defined(MLW_CLANG)) && (defined(MLW_X64) || defined(MLW_ARM64))
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
using int128 = __int128;
using uint128 = unsigned __int128;
#pragma GCC diagnostic pop
#else
// MLW_NO_I128 lets user code check availability
#define MLW_NO_I128
#endif

// f32 / f64
using f32 = float;
using f64 = double;

// isize / usize
#if defined(MLW_X64) || defined(MLW_ARM64)
using isize = int64;  ///< signed poiter-wide integer
using usize = uint64; ///< unsigned poiter-wide integer
#else
using isize = int32;  ///< signed poiter-wide integer
using usize = uint32; ///< unsigned poiter-wide integer
#endif

#if defined(MLW_X64) || defined(MLW_ARM64)
using index_t = int32; ///< signed 32-bit integer used for index variables
#else
using index_t = int32; ///< signed 32-bit integer used for index variables
#endif

using uptr = usize; ///< unsigned poiter-wide integer
using iptr = isize; ///< signed poiter-wide integer

using alignment_t = f64; ///< type with the strictest fundamental alignment; used as max-aligned storage

namespace core
{
    /// \brief Compile-time numeric properties of an arithmetic type.
    ///
    /// Freestanding stand-in for std::numeric_limits with only the members the
    /// core needs. Integer types expose \c min and \c max; floating-point types
    /// also expose \c epsilon, \c infinity and \c nan.
    ///
    /// \tparam T  An arithmetic type. Unsupported types fail to compile.
    template <typename T>
    struct NumericLimits
    {
        static_assert(false, "type does not impment numaric_limits");
    };

    template <>
    struct NumericLimits<uint8>
    {
        static constexpr uint8 min = 0;
        static constexpr uint8 max = 0xFF;
    };
    template <>
    struct NumericLimits<int8>
    {
        static constexpr int8 min = static_cast<int8>(0x80u);
        static constexpr int8 max = 0x7F;
    };
    template <>
    struct NumericLimits<uint16>
    {
        static constexpr uint16 min = 0;
        static constexpr uint16 max = 0xFFFF;
    };
    template <>
    struct NumericLimits<int16>
    {
        static constexpr int16 min = static_cast<int16>(0x8000u);
        static constexpr int16 max = 0x7FFF;
    };
    template <>
    struct NumericLimits<uint32>
    {
        static constexpr uint32 min = 0;
        static constexpr uint32 max = 0xFFFFFFFFu;
    };
    template <>
    struct NumericLimits<int32>
    {
        static constexpr int32 min = static_cast<int32>(0x80000000u);
        static constexpr int32 max = 0x7FFFFFFF;
    };
    template <>
    struct NumericLimits<uint64>
    {
        static constexpr uint64 min = 0;
        static constexpr uint64 max = 0xFFFFFFFFFFFFFFFFull;
    };
    template <>
    struct NumericLimits<int64>
    {
        static constexpr int64 min = static_cast<int64>(0x8000000000000000ull);
        static constexpr int64 max = 0x7FFFFFFFFFFFFFFFll;
    };

    template <>
    struct NumericLimits<f32>
    {
        static constexpr f32 min = -3.402823466e+38f;
        static constexpr f32 max = 3.402823466e+38f;
        static constexpr f32 epsilon = 1.192092896e-07f; ///< gap between 1 and the next representable value
        static constexpr f32 infinity = __builtin_huge_valf();
        static constexpr f32 nan = __builtin_nanf("");
    };
    template <>
    struct NumericLimits<f64>
    {
        static constexpr f64 min = -1.7976931348623158e+308;
        static constexpr f64 max = 1.7976931348623158e+308;
        static constexpr f64 epsilon = 2.2204460492503131e-016; ///< gap between 1 and the next representable value
        static constexpr f64 infinity = __builtin_huge_val();
        static constexpr f64 nan = __builtin_nan("");
    };

#ifndef MLW_NO_I128
    template <>
    struct NumericLimits<uint128>
    {
        static constexpr uint128 min = 0;
        static constexpr uint128 max = ~static_cast<uint128>(0);
    };
    template <>
    struct NumericLimits<int128>
    {
        static constexpr int128 min = static_cast<int128>(static_cast<uint128>(1) << 127);
        static constexpr int128 max = ~static_cast<int128>(static_cast<uint128>(1) << 127);
    };
#endif
} // namespace core
