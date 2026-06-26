#pragma once

// i8 / u8
using int8  = signed char;
using uint8  = unsigned char;

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

using uint = uint32;
using sint = int32;

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
    using int128 = __int128;
    using uint128 = unsigned __int128;
#else
    // MLW_NO_I128 lets user code check availability
    #define MLW_NO_I128
#endif


// f32 / f64
using f32 = float;
using f64 = double;

// isize / usize
#if defined(MLW_X64) || defined(MLW_ARM64)
    using isize = int64;
    using usize = uint64;
#else
    using isize = int32;
    using usize = uint32;
#endif


#if defined(MLW_X64) || defined(MLW_ARM64)
    using index_t = int32;
#else
    using index_t = int32;
#endif

using uptr = usize;
using iptr = isize;

using alignment_t = f64;

namespace core
{
    template<typename T>
    struct NumericLimits{ static_assert(true, "type does not impment numaric_limits");};

    template<> struct NumericLimits<uint8>  {
        static constexpr uint8  min = 0;
        static constexpr uint8  max = 0xFF;
    };
    template<> struct NumericLimits<int8>   {
        static constexpr int8   min = static_cast<int8>(0x80u);
        static constexpr int8   max = 0x7F;
    };
    template<> struct NumericLimits<uint16> {
        static constexpr uint16 min = 0;
        static constexpr uint16 max = 0xFFFF;
    };
    template<> struct NumericLimits<int16>  {
        static constexpr int16  min = static_cast<int16>(0x8000u);
        static constexpr int16  max = 0x7FFF;
    };
    template<> struct NumericLimits<uint32> {
        static constexpr uint32 min = 0;
        static constexpr uint32 max = 0xFFFFFFFFu;
    };
    template<> struct NumericLimits<int32>  {
        static constexpr int32  min = static_cast<int32>(0x80000000u);
        static constexpr int32  max = 0x7FFFFFFF;
    };
    template<> struct NumericLimits<uint64> {
        static constexpr uint64 min = 0;
        static constexpr uint64 max = 0xFFFFFFFFFFFFFFFFull;
    };
    template<> struct NumericLimits<int64>  {
        static constexpr int64  min = static_cast<int64>(0x8000000000000000ull);
        static constexpr int64  max = 0x7FFFFFFFFFFFFFFFll;
    };

    template<> struct NumericLimits<f32> {
        static constexpr f32 min      = -3.402823466e+38f;
        static constexpr f32 max      =  3.402823466e+38f;
        static constexpr f32 epsilon  =  1.192092896e-07f;
        static constexpr f32 infinity =  __builtin_huge_valf();
        static constexpr f32 nan      =  __builtin_nanf("");
    };
    template<> struct NumericLimits<f64> {
        static constexpr f64 min      = -1.7976931348623158e+308;
        static constexpr f64 max      =  1.7976931348623158e+308;
        static constexpr f64 epsilon  =  2.2204460492503131e-016;
        static constexpr f64 infinity =  __builtin_huge_val();
        static constexpr f64 nan      =  __builtin_nan("");
    };

#ifndef MLW_NO_I128
    template<> struct NumericLimits<uint128> {
        static constexpr uint128 min = 0;
        static constexpr uint128 max = ~static_cast<uint128>(0);
    };
    template<> struct NumericLimits<int128> {
        static constexpr int128 min = static_cast<int128>(static_cast<uint128>(1) << 127);
        static constexpr int128 max = ~static_cast<int128>(static_cast<uint128>(1) << 127);
    };
#endif
} // namespace core
