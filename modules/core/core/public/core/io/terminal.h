#pragma once

#include "handle.h"

namespace core::terminal
{
    io::Handle stdoutHandle();
    io::Handle stderrHandle();
    io::Handle stdinHandle();

    extern struct Args{
        int count;
        const char** values;
    } args;
} // namespace core::terminal

