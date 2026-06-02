#pragma once

#if defined(MLW_MSVC)
    #define MLW_ASSUME_ALIGNED(ptr, alignment) ((__assume((reinterpret_cast<usize>(ptr) & ((alignment) - 1)) == 0)), (ptr))
        static inline usize mlw_ctz64(u64 x)
    {
        unsigned long idx;
        _BitScanForward64(&idx, x);
        return static_cast<usize>(idx);
    }
    #define MLW_CTZ(x) mlw_ctz64(x)

#elif defined(MLW_GCC) || defined(MLW_CLANG)
    #define MLW_ASSUME_ALIGNED(ptr, alignment) __builtin_assume_aligned(ptr, alignment)
    #define MLW_CTZ(x) __builtin_ctzll(x)

#else
#error "unknown compiler - add compiler specific definitions"
#endif