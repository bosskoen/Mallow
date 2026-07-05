#pragma once
#include "../compilers.h"

#include <stdlib.h>


namespace core{
    const extern thread_local uint32 thread_id;

    MLW_NO_RETURN MLW_FORCE_INLINE void mlwExit(int32 status)
    {
        ::exit(status);
    }

    MLW_NO_RETURN MLW_FORCE_INLINE void mlwTerminate(int32 status)
    {
        ::_Exit(status);
    }
}