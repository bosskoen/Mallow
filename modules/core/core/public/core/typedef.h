#pragma once

// i8 / u8
using i8  = signed char;
using u8  = unsigned char;

// i16 / u16
using i16 = short;
using u16 = unsigned short;

// i32 / u32
#if defined(MLW_MSVC)
    using i32 = __int32;
    using u32 = unsigned __int32;
#else
    using i32 = __INT32_TYPE__;
    using u32 = __UINT32_TYPE__;
#endif

// i64 / u64
#if defined(MLW_MSVC)
    using i64 = __int64;
    using u64 = unsigned __int64;
#else
    using i64 = __INT64_TYPE__;
    using u64 = __UINT64_TYPE__;
#endif

// i128 / u128 — only where natively supported
#if (defined(MLW_GCC) || defined(MLW_CLANG)) && (defined(MLW_X64) || defined(MLW_ARM64))
    using i128 = __int128;
    using u128 = unsigned __int128;
#else
    // MLW_NO_I128 lets user code check availability
    #define MLW_NO_I128
#endif


// f32 / f64
using f32 = float;
using f64 = double;

// isize / usize
#if defined(MLW_X64) || defined(MLW_ARM64)
    using isize = i64;
    using usize = u64;
#else
    using isize = i32;
    using usize = u32;
#endif


#if defined(MLW_X64) || defined(MLW_ARM64)
    using index_t = i32;
#else
    using index_t = i32;
#endif