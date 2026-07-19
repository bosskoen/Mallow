#pragma once

/// \file
/// \brief Basic I/O helpers for the public API.
///
/// This header exposes low-level I/O operations that work with the project's
/// cross-platform handle type and string views.

#include "handle.h"
#include "../c_string.h"

namespace io
{
    /// \brief Write a string view to the given handle.
    void writeHandle(Handle handle, core::CStr str);

} // namespace io
