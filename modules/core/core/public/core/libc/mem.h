#pragma once
#include "typedef.h"
#include "compilers.h"

namespace {
        extern "C" void* memcpy(void* __restrict dst, const void* __restrict src, usize n);
        extern "C" void* memset(void* dst, int value, usize n);
}

namespace core
{
    MLW_FORCE_INLINE void *mlwMemcpy(void* __restrict dst, const void* __restrict src, usize n)
    {
        // u8 *d = static_cast<u8 *>(dst);
        // const u8 *s = static_cast<const u8 *>(src);

        // constexpr usize POINTER_MASK = sizeof(usize) - 1;
        // constexpr usize POINTER_SIZE = sizeof(usize);

        // usize head = (POINTER_SIZE - (reinterpret_cast<usize>(d) & POINTER_MASK)) & POINTER_MASK;
        // head = head < n ? head : n; // clamp to n in case n is tiny

        // for (usize i = 0; i < head; ++i)
        //     d[i] = s[i];

        // d += head;
        // s += head;
        // n -= head;

        // usize words = n / POINTER_SIZE;

        // usize *dw = reinterpret_cast<usize *>(d);
        // const usize *sw = reinterpret_cast<const usize *>(s);

        // for (usize i = 0; i < words; ++i)
        //     dw[i] = sw[i];

        // d += words * POINTER_SIZE;
        // s += words * POINTER_SIZE;
        // n -= words * POINTER_SIZE;

        // for (usize i = 0; i < n; ++i)
        //     d[i] = s[i];

        // return dst;
        return ::memcpy(dst, src, n);
    }

    MLW_FORCE_INLINE void *mlwMemset(void* dst, u8 value, usize n)
    {
        return ::memset(dst, value, n);
    }
}