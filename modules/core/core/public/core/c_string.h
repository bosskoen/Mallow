#pragma once

#include "typedef.h"

namespace core
{
    struct CStr {
        const char* ptr;
        index_t len;

        template<index_t N>
        constexpr CStr(const char (&str)[N]) : ptr(str), len(N - 1) {}
        
        constexpr CStr(const char* ptr, index_t len) : ptr(ptr), len(len) {}

        static constexpr CStr forPtr(const char* ptr);
    };
} // namespace core::
