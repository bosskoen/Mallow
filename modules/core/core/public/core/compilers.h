#pragma once
#include "typedef.h"

#if !defined(MLW_MSVC) && !defined(MLW_GCC) && !defined(MLW_CLANG)
    #error "unknown compiler - add compiler specific definitions"
#endif

#if defined(MLW_MSVC)
    #define MLW_ASSUME_ALIGNED(ptr, alignment) ((__assume((reinterpret_cast<usize>(ptr) & ((alignment) - 1)) == 0)), (ptr))
#elif defined(MLW_GCC) || defined(MLW_CLANG)
    #define MLW_ASSUME_ALIGNED(ptr, alignment) __builtin_assume_aligned(ptr, alignment)

#endif

#if defined(MLW_MSVC)
    static __forceinline usize mlw_ctz64(uint64 x)
    {
        unsigned long idx;
        _BitScanForward64(&idx, x);
        return static_cast<usize>(idx);
    }
    #define MLW_CTZ(x) mlw_ctz64(x)
#elif defined(MLW_GCC) || defined(MLW_CLANG)

    #define MLW_CTZ(x) __builtin_ctzll(x)
#endif

#if defined(MLW_MSVC)

    static __forceinline usize mlw_clz64(uint64 x)
    {
        unsigned long idx;
        _BitScanReverse64(&idx, x);
        return 63u - static_cast<usize>(idx);
    }

    #define MLW_CLZ(x) mlw_clz64(x)

#elif defined(MLW_GCC) || defined(MLW_CLANG)

    #define MLW_CLZ(x) __builtin_clzll(x)

#endif

#if defined(MLW_MSVC)
    #define MLW_FORCE_INLINE __forceinline
#elif defined(MLW_GCC) || defined(MLW_CLANG)
    #define MLW_FORCE_INLINE __attribute__((always_inline)) inline
#else
    #define MLW_FORCE_INLINE inline
#endif

#if defined(MLW_MSVC)
    #define MLW_NO_RETURN __declspec(noreturn)
#elif defined(MLW_GCC) || defined(MLW_CLANG)
    #define MLW_NO_RETURN __attribute__((noreturn))
#else
    #define MLW_NO_RETURN [[noreturn]]
#endif

#if defined(MLW_MSVC)

#elif defined(MLW_GCC) || defined(MLW_CLANG)

#endif
