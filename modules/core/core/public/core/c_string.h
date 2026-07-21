#pragma once

/// \file
/// \brief Defines core::CStr, a non-owning C-string view (pointer + length).

#include "compilers.h"

namespace core
{
    usize mlwStrlen(const char *str);
    
    /// \ingroup formattable
    /// \brief Non-owning view of a C-style string.
    ///
    /// `CStr` stores a pointer to a character buffer and an explicit length.
    /// It is intentionally lightweight and suitable for constexpr use with
    /// string literals. `CStr` never owns or null-terminates the buffer; the
    /// caller is responsible for ensuring the pointed data remains valid for
    /// the lifetime of the `CStr` instance. Convert to an owning string type
    /// only if the data must outlive the source buffer.
    struct CStr
    {
        /// \brief Pointer to the character data (not owned).
        const char *ptr;

        /// \brief Length of the string in bytes, excluding any terminating NUL.
        index_t len;

        /// \brief Construct from a string literal or character array.
        ///
        /// Deduces the array extent `N` and stores `N - 1` as the length,
        /// excluding the trailing NUL.
        ///
        /// \warning The length is the array extent minus one, not a scan for
        ///          NUL. This is correct for string literals, but for a
        ///          partially filled `char[N]` buffer it reports the whole
        ///          buffer (minus one), and for a literal with an embedded NUL
        ///          such as `"ab\0cd"` it counts through the embedded NUL. Use
        ///          the pointer+length constructor or \ref fromPtr for those
        ///          cases.
        template <index_t N>
        MLW_FORCE_INLINE constexpr CStr(const char (&str)[N]) : ptr(str), len(N - 1) {}

        /// \brief Construct from a pointer and explicit length.
        ///
        /// Use this when you already know the buffer length or when the
        /// string may contain embedded NULs.
        MLW_FORCE_INLINE constexpr CStr(const char *ptr, index_t len) : ptr(ptr), len(len) {}

        /// \brief Create a `CStr` by measuring a null-terminated string at runtime.
        ///
        /// Measures the length with mlwStrlen() and stores it. Not constexpr,
        /// because it calls a runtime function.
        ///
        /// \see mlwStrlen
        MLW_FORCE_INLINE static CStr fromPtr(const char *ptr)
        {
            return CStr(ptr, static_cast<index_t>(mlwStrlen(ptr)));
        }
    };
} // namespace core