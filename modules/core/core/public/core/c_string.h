#pragma once

#include "typedef.h"
#include "libc/str.h"

namespace core
{
    // namespace detail
    // {
    //     constexpr index_t c_strLen(const char *ptr)
    //     {
    //         index_t len = 0;
    //         while (ptr[len] != '\0')
    //             len++;
    //         return len;
    //     }
    // }
    struct CStr
    {
        const char *ptr;
        index_t len;

        template <index_t N>
        MLW_FORCE_INLINE constexpr CStr(const char (&str)[N]) : ptr(str), len(N - 1) {}

        MLW_FORCE_INLINE constexpr CStr(const char *ptr, index_t len) : ptr(ptr), len(len) {}

        MLW_FORCE_INLINE static constexpr CStr fromPtr(const char *ptr)
        {
            return CStr(ptr, mlwStrlen(ptr));
        }
    };
} // namespace core::
