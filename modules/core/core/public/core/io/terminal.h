#pragma once

/// \file
/// \brief Terminal handle helpers for standard input/output streams.
///
/// This header exposes public accessor functions for the process's standard
/// input, output, and error handles.

#include "handle.h"

namespace core::terminal
{
    /// \brief Retrieve the handle for standard output.
    io::Handle stdoutHandle();

    /// \brief Retrieve the handle for standard error.
    io::Handle stderrHandle();

    /// \brief Retrieve the handle for standard input.
    io::Handle stdinHandle();
} // namespace core::terminal

