#include "io/io.h"
#include "io/terminal.h"

#if defined(MLW_WINDOWS)
    #define WIN32_LEAN_AND_MEAN
    #define NOMINMAX
    #include <windows.h>
#else
    #include "posix/syscall_api.h"   // Linux x86-64 numbers; NOT valid for macOS as-is
#endif

namespace io
{
namespace {   // file-local error mappers

#if defined(MLW_WINDOWS)
    IoError lastError() {
        DWORD e = GetLastError();
        switch (e) {
            case ERROR_FILE_NOT_FOUND:
            case ERROR_PATH_NOT_FOUND:  return { IoError::Kind::NotFound,        (isize)e };
            case ERROR_ACCESS_DENIED:   return { IoError::Kind::PermissionDenied,(isize)e };
            case ERROR_FILE_EXISTS:
            case ERROR_ALREADY_EXISTS:  return { IoError::Kind::AlreadyExists,   (isize)e };
            case ERROR_BROKEN_PIPE:     return { IoError::Kind::BrokenPipe,      (isize)e };
            case ERROR_INVALID_HANDLE:  return { IoError::Kind::InvalidHandle,   (isize)e };
            default:                    return { IoError::Kind::Other,           (isize)e };
        }
    }
#else
    IoError fromErrno(long e) {   // syscalls return -errno
        switch (e) {
            case 2:  return { IoError::Kind::NotFound,         e }; // ENOENT
            case 13: return { IoError::Kind::PermissionDenied, e }; // EACCES
            case 17: return { IoError::Kind::AlreadyExists,    e }; // EEXIST
            case 21: return { IoError::Kind::NotAFile,         e }; // EISDIR
            case 4:  return { IoError::Kind::Interrupted,      e }; // EINTR
            case 11: return { IoError::Kind::WouldBlock,       e }; // EAGAIN
            case 32: return { IoError::Kind::BrokenPipe,       e }; // EPIPE
            case 9:  return { IoError::Kind::InvalidHandle,    e }; // EBADF
            default: return { IoError::Kind::Other,            e };
        }
    }
    // Linux x86-64 open flags (octal in the kernel ABI)
    enum : long { O_RDONLY=0, O_WRONLY=1, O_RDWR=2, O_CREAT=0100, O_TRUNC=01000, O_APPEND=02000 };
#endif

} // anonymous

    // --- diagnostic writer: fire-and-forget, never returns a Result ---
    void writeStringToHandle(Handle handle, core::CStr str) {
#if defined(MLW_WINDOWS)
        DWORD written;
        WriteFile(handle.fd, str.ptr, (DWORD)str.len, &written, nullptr);
        (void)written;
#else
        (void)syscall(SYS_WRITE, (long)handle.fd, (long)str.ptr, (long)str.len);
#endif
    }

    core::Result<Handle, IoError> openFile(core::CStr path, OpenMode mode) {
#if defined(MLW_WINDOWS)
        wchar_t wpath[4096];
        int wl = MultiByteToWideChar(CP_UTF8, 0, path.ptr, (int)path.len, wpath, 4095);
        wpath[wl] = L'\0';

        DWORD access = GENERIC_READ, disp = OPEN_EXISTING;
        switch (mode) {
            case OpenMode::Read:      access = GENERIC_READ;                 disp = OPEN_EXISTING; break;
            case OpenMode::Write:     access = GENERIC_WRITE;                disp = CREATE_ALWAYS; break;
            case OpenMode::Append:    access = FILE_APPEND_DATA;             disp = OPEN_ALWAYS;   break;
            case OpenMode::ReadWrite: access = GENERIC_READ|GENERIC_WRITE;   disp = OPEN_EXISTING; break;
        }
        HANDLE h = CreateFileW(wpath, access, FILE_SHARE_READ, nullptr, disp, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE) return core::Err{ lastError() };
        return core::Ok{ Handle{ h } };
#else
        // CStr is NOT guaranteed NUL-terminated — the kernel needs a C string.
        char buf[4096];
        if (path.len >= (index_t)sizeof(buf)) return core::Err{ IoError{ IoError::Kind::Other, 0 } };
        for (index_t i = 0; i < path.len; ++i) buf[i] = path.ptr[i];
        buf[path.len] = '\0';

        long flags;
        switch (mode) {
            case OpenMode::Read:      flags = O_RDONLY;                    break;
            case OpenMode::Write:     flags = O_WRONLY | O_CREAT | O_TRUNC;  break;
            case OpenMode::Append:    flags = O_WRONLY | O_CREAT | O_APPEND; break;
            case OpenMode::ReadWrite: flags = O_RDWR;                     break;
        }
        long r = syscall(SYS_OPENAT, AT_FDCWD, (long)buf, flags, 0644);
        if (r < 0) return core::Err{ fromErrno(-r) };
        return core::Ok{ Handle{ (int32)r } };
#endif
    }

    core::Result<isize, IoError> writeFile(Handle handle, core::Span<const uint8> data) {
#if defined(MLW_WINDOWS)
        DWORD n;
        if (!WriteFile(handle.fd, data.ptr, (DWORD)data.len, &n, nullptr))
            return core::Err{ lastError() };
        return core::Ok{ (isize)n };
#else
        long r = syscall(SYS_WRITE, (long)handle.fd, (long)data.ptr, (long)data.len);
        if (r < 0) return core::Err{ fromErrno(-r) };
        return core::Ok{ (isize)r };
#endif
    }

    core::Result<isize, IoError> readFile(Handle handle, core::Span<uint8> buffer) {
#if defined(MLW_WINDOWS)
        DWORD n;
        if (!ReadFile(handle.fd, buffer.ptr, (DWORD)buffer.len, &n, nullptr))
            return core::Err{ lastError() };
        return core::Ok{ (isize)n };     // n == 0 → EOF
#else
        long r = syscall(SYS_READ, (long)handle.fd, (long)buffer.ptr, (long)buffer.len);
        if (r < 0) return core::Err{ fromErrno(-r) };
        return core::Ok{ (isize)r };     // r == 0 → EOF
#endif
    }

    void closeHandle(Handle handle) {
#if defined(MLW_WINDOWS)
        CloseHandle(handle.fd);
#else
        (void)syscall(SYS_CLOSE, (long)handle.fd);
#endif
    }
}