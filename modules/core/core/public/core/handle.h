#pragma once
#include "typedef.h"

namespace core::io
{
    struct Handle{
#if defined(MLW_WINDOWS)
        Handle(void* h) : fd(h) {}; // no explised
        void* fd;
#else
        Handle(i32 h) : fd(h) {};
        i32 fd;
#endif
    };
} // namespace core::io
