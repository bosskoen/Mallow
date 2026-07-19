#pragma once

#include "handle.h"

namespace core::terminal
{
    io::Handle stdoutHandle();
    io::Handle stderrHandle();
    io::Handle stdinHandle();
} // namespace core::terminal

