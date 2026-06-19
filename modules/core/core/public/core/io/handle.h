#pragma once
#include "../typedef.h"

namespace io
{
    struct Handle{
#if defined(MLW_WINDOWS)
        Handle(void* h) : fd(h) {}; // no explised
        void* fd;
#else
        Handle(int32 h) : fd(h) {};
        int32 fd;
#endif
    };
} // namespace core::io
