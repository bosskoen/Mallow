#include "io/io.h"
#include "io/terminal.h"
#include "libc/mem.h"

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

static constexpr isize DIR_BUF = 8 * 1024;
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

        SystemTime fromFiletime(FILETIME ft)
        {
            uint64 ticks = ((uint64)ft.dwHighDateTime << 32) | ft.dwLowDateTime; // 100ns since 1601
            const uint64 EPOCH_DIFF = 116444736000000000ull;                     // 100ns units between 1601 and 1970
            if (ticks < EPOCH_DIFF)
                return {0, 0}; // clamp pre-1970 (rare)
            uint64 rel = ticks - EPOCH_DIFF;
            return {(int64)(rel / 10000000ull), (uint32)((rel % 10000000ull) * 100)};
        }
        FileType winType(DWORD a)
        {
            if (a & FILE_ATTRIBUTE_REPARSE_POINT)
                return FileType::Symlink; // approx, see gotcha #3
            if (a & FILE_ATTRIBUTE_DIRECTORY)
                return FileType::Dir;
            return FileType::File;
        }
        Metadata fromWinInfo(DWORD attrs, DWORD hi, DWORD lo,
                             FILETIME at, FILETIME wt, FILETIME ct)
        {
            Metadata m;
            m.size = ((int64)hi << 32) | lo;
            m.type = winType(attrs);
            uint16 mode = (attrs & FILE_ATTRIBUTE_READONLY) ? 0444 : 0666; // synthesized
            if (m.type == FileType::Dir)
                mode |= 0111;
            m.mode = mode;
            m.accessed = fromFiletime(at);
            m.modified = fromFiletime(wt);
            m.created = fromFiletime(ct);
            m.hasCreated = true; // Windows always records creation time
            return m;
        }

        int toWPath(core::CStr p, wchar_t (&w)[4096])
        {
            int wl = MultiByteToWideChar(CP_UTF8, 0, p.ptr, (int)p.len, w, 4095);
            w[wl] = L'\0';
            return wl;
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
            case 22:
                return {IoError::Kind::InvalidInput, e};
            case 38:
                return {IoError::Kind::Unsupported, e};
            case 20:
                return {IoError::Kind::NotADirectory, e};
            case 39:
                return {IoError::Kind::DirectoryNotEmpty, e};
            case 34: 
                return {IoError:Kind::BufferTooSmall, e};
            default:
                return {IoError::Kind::Other, e};
            }
        }

        FileType typeFromMode(uint16 mode)
        {
            switch (mode & S_IFMT)
            {
            case S_IFREG:
                return FileType::File;
            case S_IFDIR:
                return FileType::Dir;
            case S_IFLNK:
                return FileType::Symlink;
            default:
                return FileType::Other; // device/fifo/socket
            }
        }

        Metadata fromStatx(const Statx &st)
        {
            Metadata m;
            m.size = (int64)st.size;
            m.mode = st.mode;
            m.type = typeFromMode(st.mode);
            m.accessed = {st.atime.sec, st.atime.nsec};
            m.modified = {st.mtime.sec, st.mtime.nsec};
            m.hasCreated = (st.mask & STATX_BTIME) != 0; // trust the returned mask, not the request
            m.created = m.hasCreated ? SystemTime{st.btime.sec, st.btime.nsec}
                                     : SystemTime{0, 0};
            return m;
        }

        bool toCPath(core::CStr path, char (&buf)[4096])
        {
            if (path.len >= (index_t)sizeof(buf))
                return false;
            for (index_t i = 0; i < path.len; ++i)
                buf[i] = path.ptr[i];
            buf[path.len] = '\0';
            return true;
        }

        FileType fromDType(uint8 t)
        {
            switch (t)
            {
            case DT_DIR:
                return FileType::Dir;
            case DT_REG:
                return FileType::File;
            case DT_LNK:
                return FileType::Symlink;
            default:
                return FileType::Other; 
            }
        }
        bool isDotEntry(const char *n)
        {
            return n[0] == '.' && (n[1] == '\0' || (n[1] == '.' && n[2] == '\0'));
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
        switch (mode)
        {
        case OpenMode::Read:
            o.read(true);
            break;
        case OpenMode::Write:
            o.write(true).create(true).truncate(true);
            break;
        case OpenMode::Append:
            o.append(true).create(true);
            break;
        case OpenMode::ReadWrite:
            o.read(true).write(true);
            break;
        default:
            return core::Err{invalidInput()};
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
#elif defined(MLW_X64) || defined(MLW_ARM64)
        // native 64-bit lseek; `long` is 64-bit here. seek never blocks → no retry.
        long r = syscall(SYS_LSEEK, (long)handle.fd, (long)offset, w);
        if (r < 0)
            return core::Err{fromErrno(-r)};
        return core::Ok{(int64)r};
#else // MLW_X86 / MLW_ARM32: _llseek(fd, off_high, off_low, loff_t* result, whence)
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
#elif defined(MLW_X64) || defined(MLW_ARM64)
        long r = retrySyscall(SYS_FTRUNCATE, (long)handle.fd, (long)len, 0);
        if (r < 0)
            return core::Err{fromErrno(-r)};
        return core::Ok{unit{}};
#elif defined(MLW_X86)
        // ftruncate64(fd, low, high) — LE split, no padding on i386
        long r = retrySyscall(SYS_FTRUNCATE64, (long)handle.fd,
                              (long)(uint32)(uint64)len,
                              (long)(uint32)((uint64)len >> 32));
        if (r < 0)
            return core::Err{fromErrno(-r)};
        return core::Ok{unit{}};
#else // MLW_ARM32 : 64-bit arg needs an even-aligned register pair → r1 is padding
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

    core::Result<Metadata, IoError> statHandle(Handle handle)
    {
#if defined(MLW_WINDOWS)
        BY_HANDLE_FILE_INFORMATION info;
        if (!GetFileInformationByHandle(handle.fd, &info))
            return core::Err{lastError()};
        return core::Ok{fromWinInfo(info.dwFileAttributes,
                                    info.nFileSizeHigh, info.nFileSizeLow,
                                    info.ftLastAccessTime, info.ftLastWriteTime,
                                    info.ftCreationTime)};
#else
        Statx st;
        // AT_EMPTY_PATH + "" makes statx operate on the fd itself (the fstat form).
        long r = syscall(SYS_STATX, (long)handle.fd, (long)"", AT_EMPTY_PATH,
                         (long)(STATX_BASIC_STATS | STATX_BTIME), (long)&st);
        if (r == -38 /*ENOSYS*/)
            return core::Err{IoError{IoError::Kind::Unsupported, 38}};
        if (r < 0)
            return core::Err{fromErrno(-r)};
        return core::Ok{fromStatx(st)};
#endif
    }

    core::Result<Metadata, IoError> statPath(core::CStr path, bool follow)
    {
#if defined(MLW_WINDOWS)
        wchar_t wpath[4096];
        int wl = MultiByteToWideChar(CP_UTF8, 0, path.ptr, (int)path.len, wpath, 4095);
        wpath[wl] = L'\0';
        (void)follow; // see gotcha #3 — Windows always follows here in v1
        WIN32_FILE_ATTRIBUTE_DATA d;
        if (!GetFileAttributesExW(wpath, GetFileExInfoStandard, &d))
            return core::Err{lastError()};
        return core::Ok{fromWinInfo(d.dwFileAttributes, d.nFileSizeHigh, d.nFileSizeLow,
                                    d.ftLastAccessTime, d.ftLastWriteTime, d.ftCreationTime)};
#else
        char buf[4096];
        if (!toCPath(path, buf))
            return core::Err{invalidInput()};
        long flags = follow ? 0 : AT_SYMLINK_NOFOLLOW;
        Statx st;
        long r = syscall(SYS_STATX, AT_FDCWD, (long)buf, flags,
                         (long)(STATX_BASIC_STATS | STATX_BTIME), (long)&st);
        if (r == -38)
            return core::Err{IoError{IoError::Kind::Unsupported, 38}};
        if (r < 0)
            return core::Err{fromErrno(-r)};
        return core::Ok{fromStatx(st)};
#endif
    }

    core::Result<unit, IoError> removePath(core::CStr path)
    { // file
#if defined(MLW_WINDOWS)
        wchar_t w[4096];
        toWPath(path, w);
        if (!DeleteFileW(w))
            return core::Err{lastError()};
#else
        char buf[4096];
        if (!toCPath(path, buf))
            return core::Err{invalidInput()};
        long r = retrySyscall(SYS_UNLINKAT, AT_FDCWD, (long)buf, 0);
        if (r < 0)
            return core::Err{fromErrno(-r)};
#endif
        return core::Ok{unit{}};
    }

    core::Result<unit, IoError> removeDirPath(core::CStr path)
    { // empty dir
#if defined(MLW_WINDOWS)
        wchar_t w[4096];
        toWPath(path, w);
        if (!RemoveDirectoryW(w))
            return core::Err{lastError()};
#else
        char buf[4096];
        if (!toCPath(path, buf))
            return core::Err{invalidInput()};
        long r = retrySyscall(SYS_UNLINKAT, AT_FDCWD, (long)buf, AT_REMOVEDIR);
        if (r < 0)
            return core::Err{fromErrno(-r)};
#endif
        return core::Ok{unit{}};
    }

    core::Result<unit, IoError> makeDir(core::CStr path, int32 mode)
    {
#if defined(MLW_WINDOWS)
        wchar_t w[4096];
        toWPath(path, w);
        (void)mode; // Windows dirs get default ACLs
        if (!CreateDirectoryW(w, nullptr))
            return core::Err{lastError()};
#else
        char buf[4096];
        if (!toCPath(path, buf))
            return core::Err{invalidInput()};
        long r = retrySyscall(SYS_MKDIRAT, AT_FDCWD, (long)buf, (long)mode);
        if (r < 0)
            return core::Err{fromErrno(-r)};
#endif
        return core::Ok{unit{}};
    }

    core::Result<unit, IoError> renamePath(core::CStr from, core::CStr to)
    {
#if defined(MLW_WINDOWS)
        wchar_t wf[4096], wt[4096];
        toWPath(from, wf);
        toWPath(to, wt);
        // MOVEFILE_REPLACE_EXISTING matches POSIX (which silently replaces an existing file)
        if (!MoveFileExW(wf, wt, MOVEFILE_REPLACE_EXISTING))
            return core::Err{lastError()};
        return core::Ok{unit{}};
#else
        char bf[4096], bt[4096];
        if (!toCPath(from, bf) || !toCPath(to, bt))
            return core::Err{invalidInput()};
#if defined(MLW_ARM64)
        long r = syscall(SYS_RENAMEAT2, AT_FDCWD, (long)bf, AT_FDCWD, (long)bt, 0); // flags=0
#else
        long r = syscall(SYS_RENAMEAT, AT_FDCWD, (long)bf, AT_FDCWD, (long)bt);
#endif
        if (r < 0)
            return core::Err{fromErrno(-r)};
        return core::Ok{unit{}};
#endif
    }

    core::Result<unit, IoError> setPermissions(core::CStr path, int32 mode)
    {
#if defined(MLW_WINDOWS)
        wchar_t w[4096];
        toWPath(path, w);
        DWORD attrs = GetFileAttributesW(w);
        if (attrs == INVALID_FILE_ATTRIBUTES)
            return core::Err{lastError()};
        if (mode & 0222)
            attrs &= ~FILE_ATTRIBUTE_READONLY;
        else
            attrs |= FILE_ATTRIBUTE_READONLY;
        if (!SetFileAttributesW(w, attrs))
            return core::Err{lastError()};
        return core::Ok{unit{}};
#else
        char buf[4096];
        if (!toCPath(path, buf))
            return core::Err{invalidInput()};
        long r = retrySyscall(SYS_FCHMODAT, AT_FDCWD, (long)buf, (long)mode, 0); // 4-arg (last is flags=0, see note)
        if (r < 0)
            return core::Err{fromErrno(-r)};
        return core::Ok{unit{}};
#endif
    }


core::Result<DirReader, IoError> openDir(core::CStr path)
{
#if defined(MLW_WINDOWS)
    wchar_t w[4096];
    int wl = MultiByteToWideChar(CP_UTF8, 0, path.ptr, (int)path.len, w, 4090);
    if (wl <= 0) return core::Err{ invalidInput() };
    if (w[wl-1] != L'\\' && w[wl-1] != L'/') w[wl++] = L'\\';
    w[wl++] = L'*'; w[wl] = L'\0';                       // search pattern "dir\*"

    DirReader dr;
    dr.buf_ = (uint8*)core::mlwMalloc(sizeof(WIN32_FIND_DATAW) + 1024); // + utf8 name scratch
    HANDLE h = FindFirstFileW(w, (WIN32_FIND_DATAW*)dr.buf_);
    if (h == INVALID_HANDLE_VALUE) {
        core::mlwFree(dr.buf_); dr.buf_ = nullptr;
        if (GetLastError() == ERROR_FILE_NOT_FOUND) return core::Ok{ core::move(dr) }; // empty
        return core::Err{ lastError() };
    }
    dr.handle_ = Handle{ h };
    dr.first_  = true;
    return core::Ok{ core::move(dr) };
#else
    char buf[4096];
    if (!toCPath(path, buf)) return core::Err{ invalidInput() };
    long r = retrySyscall(SYS_OPENAT, AT_FDCWD, (long)buf, O_RDONLY | O_DIRECTORY, 0);
    if (r < 0) return core::Err{ fromErrno(-r) };
    DirReader dr;
    dr.handle_ = Handle{ (int32)r };
    dr.buf_    = (uint8*)core::mlwMalloc(DIR_BUF);
    return core::Ok{ core::move(dr) };
#endif
}


core::Result<core::Optional<DirEntryRaw>, IoError> DirReader::nextEntry()
{
    using OptE = core::Optional<DirEntryRaw>;
#if defined(MLW_WINDOWS)
    if (!handle_.isValid()) return core::Ok{ OptE{} };          // empty dir
    WIN32_FIND_DATAW* fd = (WIN32_FIND_DATAW*)buf_;
    for (;;) {
        if (first_) first_ = false;
        else if (!FindNextFileW(handle_.fd, fd)) {
            if (GetLastError() == ERROR_NO_MORE_FILES) return core::Ok{ OptE{} };
            return core::Err{ lastError() };
        }
        const wchar_t* wn = fd->cFileName;
        if (wn[0]==L'.' && (wn[1]==0 || (wn[1]==L'.' && wn[2]==0))) continue; // . / ..
        char* name = (char*)(buf_ + sizeof(WIN32_FIND_DATAW));
        int nl = WideCharToMultiByte(CP_UTF8, 0, wn, -1, name, 1024, nullptr, nullptr);
        isize len = nl > 0 ? (isize)(nl - 1) : 0;            // drop the NUL
        FileType t = (fd->dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) ? FileType::Symlink
                   : (fd->dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)     ? FileType::Dir
                   :                                                          FileType::File;
        return core::Ok{ OptE{ core::optional_in_place, DirEntryRaw{ core::CStr{name, len}, t } } };
    }
#else
    for (;;) {
        if (pos_ >= len_) {
            long n = retrySyscall(SYS_GETDENTS64, (long)handle_.fd, (long)buf_, (long)DIR_BUF);
            if (n < 0) return core::Err{ fromErrno(-n) };
            if (n == 0) return core::Ok{ OptE{} };           // end of directory
            len_ = (isize)n; pos_ = 0;
        }
        linux_dirent64* d = (linux_dirent64*)(buf_ + pos_);
        const char* name = d->d_name;
        uint8 dtype = d->d_type;
        pos_ += d->d_reclen;                                  // advance by reclen, not name len
        if (isDotEntry(name)) continue;
        isize len = 0; while (name[len]) ++len;               // d_name is padded; scan to NUL
        return core::Ok{ OptE{ core::optional_in_place, DirEntryRaw{ core::CStr{name, len}, fromDType(dtype) } } };
    }
#endif
}


DirReader::~DirReader() {
    if (buf_) core::mlwFree(buf_);
    if (handle_.isValid())
#if defined(MLW_WINDOWS)
        FindClose(handle_.fd);
#else
        syscall(SYS_CLOSE, (long)handle_.fd);
#endif
}
DirReader::DirReader(DirReader&& o) noexcept
    : handle_(o.handle_), buf_(o.buf_), len_(o.len_), pos_(o.pos_), first_(o.first_) {
    o.handle_ = Handle::invalid(); o.buf_ = nullptr; o.len_=0; o.pos_=0; o.first_=false;
}
DirReader& DirReader::operator=(DirReader&& o) noexcept {
    if (this != &o) {
        if (buf_) core::mlwFree(buf_);
        if (handle_.isValid())
#if defined(MLW_WINDOWS)
            FindClose(handle_.fd);
#else
            syscall(SYS_CLOSE, (long)handle_.fd);
#endif
        handle_=o.handle_; buf_=o.buf_; len_=o.len_; pos_=o.pos_; first_=o.first_;
        o.handle_=Handle::invalid(); o.buf_=nullptr; o.len_=0; o.pos_=0; o.first_=false;
    }
    return *this;
}

core::Result<isize, IoError> getCwdInto(core::CStr buf)
{
#if defined(MLW_WINDOWS)
    // GetCurrentDirectoryW returns required size (incl NUL) if buf too small,
    // or chars written (excl NUL) on success. Wide -> UTF-8 conversion needed.
    wchar_t wbuf[4096];
    DWORD n = GetCurrentDirectoryW(4096, wbuf);
    if (n == 0) return core::Err{ lastError() };
    if (n >= 4096) return core::Err{ IoError{ IoError::Kind::Other, 0 } }; // path > 4096 wide chars, rare
    int need = WideCharToMultiByte(CP_UTF8, 0, wbuf, (int)n, nullptr, 0, nullptr, nullptr);
    if ((isize)need > buf.len) return core::Err{ IoError{ IoError::Kind::BufferTooSmall, 34 } }; // ERANGE-equiv
    WideCharToMultiByte(CP_UTF8, 0, wbuf, (int)n, (char*)buf.ptr, (int)buf.len, nullptr, nullptr);
    return core::Ok{ (isize)need };
#else
    // getcwd(buf, size): returns buf on success, -ERANGE if too small.
    long r = syscall(SYS_GETCWD, (long)buf.ptr, (long)buf.len);
    if (r < 0) return core::Err{ fromErrno(-r) };
    // The syscall writes a NUL-terminated string and returns the LENGTH INCLUDING the NUL.
    // (This differs from the libc wrapper, which returns the pointer — the raw syscall returns length.)
    return core::Ok{ r > 0 ? r - 1 : 0 };   // length excluding the NUL
#endif
}

}