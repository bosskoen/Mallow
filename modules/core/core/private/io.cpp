#include "io/io.h"
#include "io/terminal.h"

#if defined(MLW_WINDOWS)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include "posix/syscall_api.h" // Linux x86-64 numbers; NOT valid for macOS as-is

static long retrySyscall(long num, long a, long b, long c)
{
    for (;;)
    {
        long r = syscall(num, a, b, c);
        if (r == -4 /*EINTR*/)
            continue; // signal interrupted; try again
        return r;
    }
}

static long retrySyscall(long num, long a, long b, long c, long d)
{
    for (;;)
    {
        long r = syscall(num, a, b, c, d);
        if (r == -4 /*EINTR*/)
            continue; // signal interrupted; try again
        return r;
    }
}
#endif

namespace io
{
    namespace
    { // file-local error mappers

#if defined(MLW_WINDOWS)
        IoError lastError()
        {
            DWORD e = GetLastError();
            switch (e)
            {
            case ERROR_FILE_NOT_FOUND:
            case ERROR_PATH_NOT_FOUND:
                return {IoError::Kind::NotFound, (isize)e};
            case ERROR_ACCESS_DENIED:
                return {IoError::Kind::PermissionDenied, (isize)e};
            case ERROR_FILE_EXISTS:
            case ERROR_ALREADY_EXISTS:
                return {IoError::Kind::AlreadyExists, (isize)e};
            case ERROR_BROKEN_PIPE:
                return {IoError::Kind::BrokenPipe, (isize)e};
            case ERROR_INVALID_HANDLE:
                return {IoError::Kind::InvalidHandle, (isize)e};
            default:
                return {IoError::Kind::Other, (isize)e};
            }
        }
#else
        IoError fromErrno(long e)
        { // syscalls return -errno
            switch (e)
            {
            case 2:
                return {IoError::Kind::NotFound, e}; // ENOENT
            case 13:
                return {IoError::Kind::PermissionDenied, e}; // EACCES
            case 17:
                return {IoError::Kind::AlreadyExists, e}; // EEXIST
            case 21:
                return {IoError::Kind::NotAFile, e}; // EISDIR
            case 4:
                return {IoError::Kind::Interrupted, e}; // EINTR
            case 11:
                return {IoError::Kind::WouldBlock, e}; // EAGAIN
            case 32:
                return {IoError::Kind::BrokenPipe, e}; // EPIPE
            case 9:
                return {IoError::Kind::InvalidHandle, e}; // EBADF
            case 22: return { IoError::Kind::InvalidInput, e };
            default:
                return {IoError::Kind::Other, e};
            }
        }

#endif
        IoError invalidInput() { return {IoError::Kind::InvalidInput, 22}; } // EINVAL
    } // anonymous

    core::Result<Handle, IoError> openFile(core::CStr path, const OpenOptions &o)
    {
        // ---- shared validation ----
        if (!o.read_ && !o.write_ && !o.append_)
            return core::Err{invalidInput()}; // no access mode requested at all
        if (!o.write_ && !o.append_ && (o.truncate_ || o.create_ || o.createNew_))
            return core::Err{invalidInput()}; // can't create/truncate a read-only open
        if (o.append_ && o.truncate_ && !o.createNew_)
            return core::Err{invalidInput()}; // truncate is meaningless in append mode

#if defined(MLW_WINDOWS)
        wchar_t wpath[4096];
        int wl = MultiByteToWideChar(CP_UTF8, 0, path.ptr, (int)path.len, wpath, 4095);
        wpath[wl] = L'\0';

        DWORD access;
        if (o.append_)
        { // append-only *right*: enforced by the OS
            access = FILE_APPEND_DATA;
            if (o.read_)
                access |= GENERIC_READ;
        }
        else
        {
            access = 0;
            if (o.read_)
                access |= GENERIC_READ;
            if (o.write_)
                access |= GENERIC_WRITE;
        }

        DWORD disp;
        if (o.createNew_)
            disp = CREATE_NEW; // fail if exists
        else if (o.create_ && o.truncate_)
            disp = CREATE_ALWAYS; // create or truncate
        else if (o.create_)
            disp = OPEN_ALWAYS; // create or open
        else if (o.truncate_)
            disp = TRUNCATE_EXISTING; // must exist, truncate
        else
            disp = OPEN_EXISTING; // must exist

        HANDLE h = CreateFileW(wpath, access, FILE_SHARE_READ, nullptr, disp,
                               FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE)
            return core::Err{lastError()};
        return core::Ok{Handle{h}};
#else
        char buf[4096];
        if (path.len >= (index_t)sizeof(buf))
            return core::Err{invalidInput()};
        for (index_t i = 0; i < path.len; ++i)
            buf[i] = path.ptr[i];
        buf[path.len] = '\0';

        long access;
        if (o.append_)
            access = (o.read_ ? O_RDWR : O_WRONLY) | O_APPEND;
        else if (o.read_ && o.write_)
            access = O_RDWR;
        else if (o.write_)
            access = O_WRONLY;
        else
            access = O_RDONLY; // only remaining valid case

        long creat = 0;
        if (o.createNew_)
            creat = O_CREAT | O_EXCL; // overrides create/truncate
        else
        {
            if (o.create_)
                creat |= O_CREAT;
            if (o.truncate_)
                creat |= O_TRUNC;
        }

        long r = retrySyscall(SYS_OPENAT, AT_FDCWD, (long)buf, access | creat, (long)o.mode_);
        if (r < 0)
            return core::Err{fromErrno(-r)};
        return core::Ok{Handle{(int32)r}};
#endif
    }

    // --- diagnostic writer: fire-and-forget, never returns a Result ---
    void writeStringToHandle(Handle handle, core::CStr str)
    {
#if defined(MLW_WINDOWS)
        DWORD written;
        WriteFile(handle.fd, str.ptr, (DWORD)str.len, &written, nullptr);
        (void)written;
#else
        (void)retrySyscall(SYS_WRITE, (long)handle.fd, (long)str.ptr, (long)str.len);
#endif
    }

    core::Result<Handle, IoError> openFile(core::CStr path, OpenMode mode)
    {
    OpenOptions o;
    switch (mode) {
        case OpenMode::Read:      o.read(true);                               break;
        case OpenMode::Write:     o.write(true).create(true).truncate(true);  break;
        case OpenMode::Append:    o.append(true).create(true);                break;
        case OpenMode::ReadWrite: o.read(true).write(true);                   break;
        default:                  return core::Err{ invalidInput() };
    }
    return openFile(path, o);
    }

    core::Result<isize, IoError> writeFile(Handle handle, core::Span<const uint8> data)
    {
#if defined(MLW_WINDOWS)
        DWORD n;
        if (!WriteFile(handle.fd, data.ptr, (DWORD)data.len, &n, nullptr))
            return core::Err{lastError()};
        return core::Ok{(isize)n};
#else
        long r = retrySyscall(SYS_WRITE, (long)handle.fd, (long)data.ptr, (long)data.len);
        if (r < 0)
            return core::Err{fromErrno(-r)};
        return core::Ok{(isize)r};
#endif
    }

    core::Result<isize, IoError> readFile(Handle handle, core::Span<uint8> buffer)
    {
#if defined(MLW_WINDOWS)
        DWORD n;
        if (!ReadFile(handle.fd, buffer.ptr, (DWORD)buffer.len, &n, nullptr))
            return core::Err{lastError()};
        return core::Ok{(isize)n}; // n == 0 → EOF
#else
        long r = retrySyscall(SYS_READ, (long)handle.fd, (long)buffer.ptr, (long)buffer.len);
        if (r < 0)
            return core::Err{fromErrno(-r)};
        return core::Ok{(isize)r}; // r == 0 → EOF
#endif
    }

    void closeHandle(Handle handle)
    {
#if defined(MLW_WINDOWS)
        CloseHandle(handle.fd);
#else
        (void)syscall(SYS_CLOSE, (long)handle.fd);
#endif
    }

    core::Result<int64, IoError> seekFile(Handle handle, SeekFrom whence, int64 offset)
    {
        long w = 0; // SEEK_SET/CUR/END — see note above, kept explicit on purpose
        switch (whence)
        {
        case SeekFrom::Start:
            w = 0;
            break;
        case SeekFrom::Current:
            w = 1;
            break;
        case SeekFrom::End:
            w = 2;
            break;
        }
#if defined(MLW_WINDOWS)
        LARGE_INTEGER dist;
        dist.QuadPart = offset; // QuadPart is 64-bit
        LARGE_INTEGER out;
        DWORD method = (w == 0) ? FILE_BEGIN : (w == 1) ? FILE_CURRENT
                                                        : FILE_END;
        if (!SetFilePointerEx(handle.fd, dist, &out, method))
            return core::Err{lastError()};
        return core::Ok{(int64)out.QuadPart};
#elif defined(__x86_64__) || defined(__aarch64__)
        // native 64-bit lseek; `long` is 64-bit here. seek never blocks → no retry.
        long r = syscall(SYS_LSEEK, (long)handle.fd, (long)offset, w);
        if (r < 0)
            return core::Err{fromErrno(-r)};
        return core::Ok{(int64)r};
#else // i386 / arm: _llseek(fd, off_high, off_low, loff_t* result, whence)
        int64 result = 0;
        long r = syscall(SYS_LLSEEK, (long)handle.fd,
                         (long)(uint32)((uint64)offset >> 32), // high 32 bits
                         (long)(uint32)((uint64)offset),       // low 32 bits
                         (long)&result,                        // kernel writes 64-bit pos here
                         w);
        if (r < 0)
            return core::Err{fromErrno(-r)};
        return core::Ok{result};
#endif
    }

    core::Result<unit, IoError> flushFile(Handle handle)
    {
#if defined(MLW_WINDOWS)
        if (!FlushFileBuffers(handle.fd))
            return core::Err{lastError()};
        return core::Ok{unit{}};
#else
        long r = retrySyscall(SYS_FSYNC, (long)handle.fd, 0, 0); // extra 0s ignored by kernel
        if (r < 0)
            return core::Err{fromErrno(-r)};
        return core::Ok{unit{}};
#endif
    }

    core::Result<unit, IoError> truncateFile(Handle handle, int64 len)
    {
#if defined(MLW_WINDOWS)
        // SetEndOfFile cuts at the cursor, so save/seek/restore to stay cursor-neutral
        // (POSIX ftruncate does not move the cursor — match that).
        core::Result<int64, IoError> cur = seekFile(handle, SeekFrom::Current, 0);
        if (cur.isErr())
            return core::Err{cur.takeError()};
        core::Result<int64, IoError> s = seekFile(handle, SeekFrom::Start, len);
        if (s.isErr())
            return core::Err{s.takeError()};
        BOOL ok = SetEndOfFile(handle.fd);
        (void)seekFile(handle, SeekFrom::Start, cur.value()); // restore
        if (!ok)
            return core::Err{lastError()};
        return core::Ok{unit{}};
#elif defined(__x86_64__) || defined(__aarch64__)
        long r = retrySyscall(SYS_FTRUNCATE, (long)handle.fd, (long)len, 0);
        if (r < 0)
            return core::Err{fromErrno(-r)};
        return core::Ok{unit{}};
#elif defined(__i386__)
        // ftruncate64(fd, low, high) — LE split, no padding on i386
        long r = retrySyscall(SYS_FTRUNCATE64, (long)handle.fd,
                              (long)(uint32)(uint64)len,
                              (long)(uint32)((uint64)len >> 32));
        if (r < 0)
            return core::Err{fromErrno(-r)};
        return core::Ok{unit{}};
#else // __arm__ : 64-bit arg needs an even-aligned register pair → r1 is padding
        long r = retrySyscall(SYS_FTRUNCATE64, (long)handle.fd,
                              0,                                  // pad (r1 unused)
                              (long)(uint32)(uint64)len,          // low  (r2)
                              (long)(uint32)((uint64)len >> 32)); // high (r3)
        if (r < 0)
            return core::Err{fromErrno(-r)};
        return core::Ok{unit{}};
#endif
    }

#if !defined(MLW_WINDOWS)
    static core::Result<int64, IoError> seekSize(Handle h)
    {
        core::Result<int64, IoError> cur = seekFile(h, SeekFrom::Current, 0);
        if (cur.isErr())
            return cur;
        core::Result<int64, IoError> end = seekFile(h, SeekFrom::End, 0);
        if (end.isErr())
            return end;
        core::Result<int64, IoError> back = seekFile(h, SeekFrom::Start, cur.value());
        if (back.isErr())
            return core::Err{back.takeError()};
        return end;
    }
#endif

    core::Result<int64, IoError> fileSize(Handle handle)
    {
#if defined(MLW_WINDOWS)
        LARGE_INTEGER sz;
        if (!GetFileSizeEx(handle.fd, &sz))
            return core::Err{lastError()};
        return core::Ok{(int64)sz.QuadPart};
#else
        Statx st;
        // statx(fd, "", AT_EMPTY_PATH, STATX_SIZE, &st) — the fstat-equivalent form.
        // 5-arg syscall (same arity _llseek needed). statx doesn't block on a local
        // fs, so no retry wrapper.
        long r = syscall(SYS_STATX, (long)handle.fd, (long)"", AT_EMPTY_PATH,
                         (long)STATX_SIZE, (long)&st);
        if (r == -38 /*ENOSYS*/) // kernel < 4.11: fall back
            return seekSize(handle);
        if (r < 0)
            return core::Err{fromErrno(-r)};
        return core::Ok{(int64)st.size};
#endif
    }

}