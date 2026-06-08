#pragma once

#include "typedef.h"

extern "C" usize strlen(const char *str);

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
        inline constexpr CStr(const char (&str)[N]) : ptr(str), len(N - 1) {}

        inline constexpr CStr(const char *ptr, index_t len) : ptr(ptr), len(len) {}

        static constexpr CStr fromPtr(const char *ptr)
        {

            return CStr(ptr, strlen(ptr));
        }
    };
} // namespace core::
