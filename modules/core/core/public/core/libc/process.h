#pragma once
#include "compilers.h"

namespace {
    extern "C" void exit(int status);
    extern "C" void _Exit(int status);
}

namespace core{
    MLW_NO_RETURN MLW_FORCE_INLINE void mlwExit(int status)
    {
        ::exit(status);
    }

    MLW_NO_RETURN MLW_FORCE_INLINE void mlwTerminate(int status)
    {
        ::_Exit(status);
    }
}