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

