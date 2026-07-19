#pragma once

/// \file
/// \brief Minimal bit-level utilities for public code.
///
/// This header exposes a single helper for safely reinterpreting the bits of
/// one object type as another object type of the same size.

#include "compilers.h"

namespace core
{
    /// \brief Reinterpret the bit pattern of `value` as type `To`.
    ///
    /// `mlwBitCast` is a constexpr-friendly, type-safe replacement for raw
    /// pointer casts when the source and destination types have identical size.
    /// It does not perform conversions; only the underlying bit pattern is
    /// copied.
    template <typename To, typename From>
    constexpr MLW_FORCE_INLINE To mlwBitCast(From value)
    {
        static_assert(sizeof(To) == sizeof(From));

        return __builtin_bit_cast(To, value);
    }
} // namespace core
