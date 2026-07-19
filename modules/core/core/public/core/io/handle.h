#pragma once

/// \file
/// \brief Public wrapper for platform-specific I/O handles.
///
/// `io::Handle` wraps the underlying native handle type used for file and
/// console I/O. On Windows it stores a raw pointer handle, while on POSIX-like
/// systems it stores an integer file descriptor.

#include "../typedef.h"

namespace io
{
    /// \brief Platform-agnostic handle for I/O operations.
///
/// Wraps the native handle type: a `void*` on Windows, an `int32` file
/// descriptor on POSIX. The active member and constructor are selected by
/// platform.
struct Handle
{
#if defined(MLW_WINDOWS)
    /// \brief Construct from a native Windows handle.
    Handle(void* h) : fd(h) {}
    /// \brief Native Windows handle.
    void* fd;
#else
    /// \brief Construct from a POSIX file descriptor.
    Handle(int32 h) : fd(h) {}
    /// \brief POSIX file descriptor.
    int32 fd;
#endif
};
} // namespace core::io
