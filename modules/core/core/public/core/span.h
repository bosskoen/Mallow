#pragma once
#include "typedef.h"
#include "c_string.h"
#include "traits.h"

namespace core
{
    template <typename T>
    struct Span
    {
        T*    ptr = nullptr;
        isize len = 0;

        constexpr Span() = default;
        constexpr Span(T* p, isize l) : ptr(p), len(l) {}
        static constexpr Span fromRange(T* b, T* e) { return Span(b, static_cast<isize>(e - b)); }

        template <isize N>
        constexpr Span(T (&arr)[N]) requires (!same_as<T, const char>) : ptr(arr), len(N) {}     // note: char arrays include the NUL

        constexpr explicit Span(CStr s) requires same_as<T, const char> : ptr(s.ptr), len(s.len) {}

        constexpr T&       operator[](isize i)       { return ptr[i]; }   // + optional debug assert
        constexpr const T& operator[](isize i) const { return ptr[i]; }

        constexpr bool  empty()  const { return len == 0; }
        constexpr Span  subspan(isize off, isize count) const { return Span(ptr + off, count); }
        constexpr Span  first(isize n) const { return Span(ptr, n); }
        constexpr Span  last (isize n) const { return Span(ptr + (len - n), n); }

        constexpr operator CStr() const requires same_as<remove_cv_t<T>, char>
        { return CStr(ptr, static_cast<index_t>(len)); }

        constexpr T*       begin()       { return ptr; }
        constexpr T*       end()         { return ptr + len; }
        constexpr const T* begin() const { return ptr; }
        constexpr const T* end()   const { return ptr + len; }
    };

    inline Span<const uint8> asBytes(CStr s)
    { return { reinterpret_cast<const uint8*>(s.ptr), s.len }; }
}