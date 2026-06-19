#pragma once
#include "compilers.h"

namespace
{
    extern "C" usize strlen(const char *str);
}

namespace core
{
    MLW_FORCE_INLINE usize mlwStrlen(const char *str){
        return strlen(str);
    }
} // namespace core
