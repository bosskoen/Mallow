#pragma once

/// \file
/// \brief Terminal handle helpers for standard input/output streams.
///
/// This header exposes public accessor functions for the process's standard
/// input, output, and error handles.

#include "handle.h"
#include "../c_string.h"

namespace io
{
    /// \brief Write a string view to the given handle.
    void writeStringToHandle(Handle handle, core::CStr str);
}

namespace core::terminal
{
    /// \brief Retrieve the handle for standard output.
    io::Handle stdoutHandle();

    /// \brief Retrieve the handle for standard error.
    io::Handle stderrHandle();

    /// \brief Retrieve the handle for standard input.
    io::Handle stdinHandle();
} // namespace core::terminal
