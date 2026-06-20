#pragma once
#include "../compilers.h"

namespace {
    extern "C" MLW_NO_RETURN void exit(int status);
    extern "C" MLW_NO_RETURN void _Exit(int status);
}

namespace core{
    MLW_NO_RETURN MLW_FORCE_INLINE void mlwExit(int32 status)
    {
        ::exit(status);
    }

    MLW_NO_RETURN MLW_FORCE_INLINE void mlwTerminate(int32 status)
    {
        ::_Exit(status);
    }
}