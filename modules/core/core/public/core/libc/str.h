#pragma once
#include "compilers.h"

namespace
{
    extern "C" size_t strlen(const char *str);
}

namespace core
{
    MLW_FORCE_INLINE size_t mlwStrlen(const char *str){
        return strlen(str);
    }
} // namespace core
