#pragma once

/// \file
/// \brief Basic I/O helpers for the public API.
///
/// This header exposes low-level I/O operations that work with the project's
/// cross-platform handle type and string views.

#include "handle.h"
#include "../span.h"
#include "../result.h"
#include "../optional.h"

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
            UnexpectedEof,
            WriteZero,
            InvalidInput,
            Unsupported,
            NotADirectory,     // a path component isn't a directory (ENOTDIR)
            DirectoryNotEmpty, // rmdir on a non-empty dir (ENOTEMPTY)
            Other,             // catch-all; carries the raw errno/GetLastError below
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
            case Kind::UnexpectedEof:
                b.append("Unexpected EOF");
                break;
            case Kind::WriteZero:
                b.append("Write zero bytes");
                break;
            case Kind::InvalidInput:
                b.append("Invalid input");
                break;
            case Kind::Unsupported:
                b.append("Operation not supported");
                break;
            case Kind::NotADirectory:
                b.append("Not a directory");
                break;
            case Kind::DirectoryNotEmpty:
                b.append("Directory not empty");
                break;
            case Kind::Other:
                mlw_write(b, "I/O error (os={})", os_code);
                break;
            }
        }
    };
    struct OpenOptions
    {
        bool read_ = false;
        bool write_ = false;
        bool append_ = false;
        bool truncate_ = false;
        bool create_ = false;
        bool createNew_ = false;
        int32 mode_ = 0644; // POSIX permission bits, applied only when a file is created

        OpenOptions &read(bool v)
        {
            read_ = v;
            return *this;
        }
        OpenOptions &write(bool v)
        {
            write_ = v;
            return *this;
        }
        OpenOptions &append(bool v)
        {
            append_ = v;
            return *this;
        }
        OpenOptions &truncate(bool v)
        {
            truncate_ = v;
            return *this;
        }
        OpenOptions &create(bool v)
        {
            create_ = v;
            return *this;
        }
        OpenOptions &createNew(bool v)
        {
            createNew_ = v;
            return *this;
        }
        OpenOptions &mode(int32 m)
        {
            mode_ = m;
            return *this;
        }
    };

    enum class FileType
    {
        File,
        Dir,
        Symlink,
        Other
    };

    /// Wall-clock time as seconds + nanoseconds since the Unix epoch.
    /// One representation for both platforms — Windows FILETIME is converted
    /// at the boundary so callers never see the 1601 epoch.
    struct SystemTime
    {
        int64 sec;
        uint32 nsec;
    };

    struct Metadata
    {
        int64 size; // bytes
        FileType type;
        uint16 mode; // POSIX type+perm bits; synthesized on Windows
        SystemTime accessed;
        SystemTime modified;
        SystemTime created; // valid only if hasCreated
        bool hasCreated;    // Linux btime is filesystem-dependent; Windows always true

        bool isFile() const { return type == FileType::File; }
        bool isDir() const { return type == FileType::Dir; }
        bool isSymlink() const { return type == FileType::Symlink; }
        uint16 permissions() const { return mode & 0777; }     // rwx bits
        bool isReadonly() const { return (mode & 0222) == 0; } // no write bit set
    };

    core::Result<Metadata, IoError> statHandle(Handle handle);              // by open fd
    core::Result<Metadata, IoError> statPath(core::CStr path, bool follow); // by path

    core::Result<Handle, IoError> openFile(core::CStr path, const OpenOptions &opts);
    core::Result<Handle, IoError> openFile(core::CStr path, OpenMode mode); // preset, see below

    core::Result<isize, IoError> writeFile(Handle handle, core::Span<const uint8> data);

    core::Result<isize, IoError> readFile(Handle handle, core::Span<uint8> buffer);

    void closeHandle(Handle handle);

    enum class SeekFrom
    {
        Start = 0,
        Current = 1,
        End = 2
    };
    core::Result<int64, IoError> seekFile(Handle handle, SeekFrom whence, int64 offset);

    core::Result<unit, IoError> truncateFile(Handle handle, int64 len);
    core::Result<unit, IoError> flushFile(Handle handle);
    core::Result<int64, IoError> fileSize(Handle handle);
    core::Result<unit, IoError> setPermissions(core::CStr path, int32 mode);
    core::Result<unit, IoError> removePath(core::CStr path);
    core::Result<unit, IoError> removeDirPath(core::CStr path);
    core::Result<unit, IoError> makeDir(core::CStr path, int32 mode);
    core::Result<unit, IoError> renamePath(core::CStr from, core::CStr to);

    struct DirEntryRaw
    {
        core::CStr name; // TRANSIENT: valid only until the next nextEntry() call
        FileType type;
    };

    class DirReader
    {
        Handle handle_ = Handle::invalid();
        uint8 *buf_ = nullptr;
        isize len_ = 0;      // POSIX: valid bytes from last getdents
        isize pos_ = 0;      // POSIX: parse cursor
        bool first_ = false; // Windows: FindFirst result not yet consumed

        DirReader() = default; // only openDir builds one
        friend core::Result<DirReader, IoError> openDir(core::CStr);

    public:
        ~DirReader();
        DirReader(const DirReader &) = delete;
        DirReader &operator=(const DirReader &) = delete;
        DirReader(DirReader &&) noexcept;
        DirReader &operator=(DirReader &&) noexcept;

        core::Result<core::Optional<DirEntryRaw>, IoError> nextEntry();
        //  Ok(Some) = entry, Ok(None) = end of directory, Err = read error
    };

    core::Result<DirReader, IoError> openDir(core::CStr path);

} // namespace io
