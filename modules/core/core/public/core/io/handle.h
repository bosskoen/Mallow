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
        constexpr Handle(void *h) : fd(h) {}
        /// \brief Native Windows handle.
        void *fd;
        constexpr static Handle invalid() { return Handle((void *)(int64)-1); }
#else
        /// \brief Construct from a POSIX file descriptor.
        constexpr Handle(int32 h) : fd(h) {}
        /// \brief POSIX file descriptor.
        int32 fd;
        constexpr static Handle invalid() { return Handle(-1); }
#endif
        bool isValid() const
        {
            return fd != invalid().fd;
        }
    };
} // namespace io
