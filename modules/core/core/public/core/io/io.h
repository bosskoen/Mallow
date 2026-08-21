#pragma once

/// \file
/// \brief Basic I/O helpers for the public API.
///
/// This header exposes low-level I/O operations that work with the project's
/// cross-platform handle type and string views.

#include "handle.h"
#include "../span.h"
#include "../result.h"

namespace io
{

    enum class OpenMode
    {
        Read,      // must exist; read-only
        Write,     // create or truncate to empty; write-only
        Append,    // create if missing; writes go to end
        ReadWrite, // must exist; read + write
    };

    struct IoError
    {
        enum class Kind
        {
            NotFound, // no such file
            PermissionDenied,
            AlreadyExists,
            NotAFile,    // e.g. tried to open a directory
            Interrupted, // EINTR — usually retried internally, rarely surfaces
            WouldBlock,  // non-blocking handle had no data (future)
            BrokenPipe,  // wrote to a closed reader
            InvalidHandle,
            Other, // catch-all; carries the raw errno/GetLastError below
        } kind;
        isize os_code; ///< errno or GetLastError() captured at the failure site

        template <core::FormatBuffer Buf>
        void format(Buf &b) const
        {
            switch (kind)
            {
            case Kind::NotFound:
                b.append("File not found");
                break;
            case Kind::PermissionDenied:
                b.append("Permission denied");
                break;
            case Kind::AlreadyExists:
                b.append("File already exists");
                break;
            case Kind::NotAFile:
                b.append("Not a file");
                break;
            case Kind::Interrupted:
                b.append("Operation interrupted");
                break;
            case Kind::WouldBlock:
                b.append("Operation would block");
                break;
            case Kind::BrokenPipe:
                b.append("Broken pipe");
                break;
            case Kind::InvalidHandle:
                b.append("Invalid handle");
                break;
            case Kind::Other:
                mlw_write(b, "I/O error (os={})", os_code);
                break;
            }
        }

    };

    core::Result<Handle, IoError> openFile(core::CStr path, OpenMode mode);

    core::Result<isize, IoError> writeFile(Handle handle, core::Span<const uint8> data);

    core::Result<isize, IoError> readFile(Handle handle, core::Span<uint8> buffer);
    
    void closeHandle(Handle handle);

} // namespace io
