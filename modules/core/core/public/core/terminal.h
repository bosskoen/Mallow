#pragma once

#include "handle.h"

namespace core::terminal
{
    core::io::Handle stdoutHandle();
    core::io::Handle stderrHandle();
    core::io::Handle stdinHandle();

    extern struct Args{
        int count;
        const char** values;
    } args;
} // namespace core::terminal

