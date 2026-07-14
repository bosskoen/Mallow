#pragma once
#include "compilers.h"

namespace core
{
    template <typename To, typename From>
    constexpr MLW_FORCE_INLINE To mlwBitCast(From value)
    {
        static_assert(sizeof(To) == sizeof(From));

        return __builtin_bit_cast(To, value);
    }
} // namespace core
