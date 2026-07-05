#pragma once
#include "compilers.h"

namespace core
{
    template <typename To, typename From>
    MLW_FORCE_INLINE To mlwBitCast(From value)
    {
        static_assert(sizeof(To) == sizeof(From));

        union
        {
            From from;
            To to;
        } u{value};

        return u.to;
    }
} // namespace core
