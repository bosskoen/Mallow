#include "io/io.h"
#include "io/terminal.h"
#include "libc/mem.h"
#include "backend/common/io_common.h"

#include "syscall_api.h" // Linux x86-64 numbers; NOT valid for macOS as-is

static constexpr isize DIR_BUF = 8 * 1024;

namespace io
{

    namespace
    { // file-local error mappers

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
                return {IoError::Kind::BufferTooSmall, e};
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

       
    } // anonymous

    core::Result<Handle, IoError> openFile(core::CStr path, const OpenOptions &o)
    {

        if(!validateOpen(o)) return core::Err{invalidInput()};

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
    }

    // --- diagnostic writer: fire-and-forget, never returns a Result ---
    void writeStringToHandle(Handle handle, core::CStr str)
    {

        (void)retrySyscall(SYS_WRITE, (long)handle.fd, (long)str.ptr, (long)str.len);
    }



    core::Result<isize, IoError> writeFile(Handle handle, core::Span<const uint8> data)
    {

        long r = retrySyscall(SYS_WRITE, (long)handle.fd, (long)data.ptr, (long)data.len);
        if (r < 0)
            return core::Err{fromErrno(-r)};
        return core::Ok{(isize)r};
    }

    core::Result<isize, IoError> readFile(Handle handle, core::Span<uint8> buffer)
    {

        long r = retrySyscall(SYS_READ, (long)handle.fd, (long)buffer.ptr, (long)buffer.len);
        if (r < 0)
            return core::Err{fromErrno(-r)};
        return core::Ok{(isize)r}; // r == 0 → EOF
    }

    void closeHandle(Handle handle)
    {

        (void)syscall(SYS_CLOSE, (long)handle.fd);
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

#if defined(MLW_X64) || defined(MLW_ARM64)
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

        long r = retrySyscall(SYS_FSYNC, (long)handle.fd, 0, 0); // extra 0s ignored by kernel
        if (r < 0)
            return core::Err{fromErrno(-r)};
        return core::Ok{unit{}};
    }

    core::Result<unit, IoError> truncateFile(Handle handle, int64 len)
    {

        #if defined(MLW_X64) || defined(MLW_ARM64)
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

    core::Result<int64, IoError> fileSize(Handle handle)
    {

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
    }

    core::Result<Metadata, IoError> statHandle(Handle handle)
    {

        Statx st;
        // AT_EMPTY_PATH + "" makes statx operate on the fd itself (the fstat form).
        long r = syscall(SYS_STATX, (long)handle.fd, (long)"", AT_EMPTY_PATH,
                         (long)(STATX_BASIC_STATS | STATX_BTIME), (long)&st);
        if (r == -38 /*ENOSYS*/)
            return core::Err{IoError{IoError::Kind::Unsupported, 38}};
        if (r < 0)
            return core::Err{fromErrno(-r)};
        return core::Ok{fromStatx(st)};
    }

    core::Result<Metadata, IoError> statPath(core::CStr path, bool follow)
    {

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
    }

    core::Result<unit, IoError> removePath(core::CStr path)
    { // file

        char buf[4096];
        if (!toCPath(path, buf))
            return core::Err{invalidInput()};
        long r = retrySyscall(SYS_UNLINKAT, AT_FDCWD, (long)buf, 0);
        if (r < 0)
            return core::Err{fromErrno(-r)};
        return core::Ok{unit{}};
    }

    core::Result<unit, IoError> removeDirPath(core::CStr path)
    { // empty dir

        char buf[4096];
        if (!toCPath(path, buf))
            return core::Err{invalidInput()};
        long r = retrySyscall(SYS_UNLINKAT, AT_FDCWD, (long)buf, AT_REMOVEDIR);
        if (r < 0)
            return core::Err{fromErrno(-r)};

        return core::Ok{unit{}};
    }

    core::Result<unit, IoError> makeDir(core::CStr path, int32 mode)
    {

        char buf[4096];
        if (!toCPath(path, buf))
            return core::Err{invalidInput()};
        long r = retrySyscall(SYS_MKDIRAT, AT_FDCWD, (long)buf, (long)mode);
        if (r < 0)
            return core::Err{fromErrno(-r)};
        return core::Ok{unit{}};
    }

    core::Result<unit, IoError> renamePath(core::CStr from, core::CStr to)
    {
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
    }

    core::Result<unit, IoError> setPermissions(core::CStr path, int32 mode)
    {

        char buf[4096];
        if (!toCPath(path, buf))
            return core::Err{invalidInput()};
        long r = retrySyscall(SYS_FCHMODAT, AT_FDCWD, (long)buf, (long)mode, 0); // 4-arg (last is flags=0, see note)
        if (r < 0)
            return core::Err{fromErrno(-r)};
        return core::Ok{unit{}};
    }

    core::Result<DirReader, IoError> openDir(core::CStr path)
    {

        char buf[4096];
        if (!toCPath(path, buf))
            return core::Err{invalidInput()};
        long r = retrySyscall(SYS_OPENAT, AT_FDCWD, (long)buf, O_RDONLY | O_DIRECTORY, 0);
        if (r < 0)
            return core::Err{fromErrno(-r)};
        DirReader dr;
        dr.handle_ = Handle{(int32)r};
        dr.buf_ = (uint8 *)core::mlwMalloc(DIR_BUF);
        return core::Ok{core::move(dr)};
    }

    core::Result<core::Optional<DirEntryRaw>, IoError> DirReader::nextEntry()
    {
        using OptE = core::Optional<DirEntryRaw>;

        for (;;)
        {
            if (pos_ >= len_)
            {
                long n = retrySyscall(SYS_GETDENTS64, (long)handle_.fd, (long)buf_, (long)DIR_BUF);
                if (n < 0)
                    return core::Err{fromErrno(-n)};
                if (n == 0)
                    return core::Ok{OptE{}}; // end of directory
                len_ = (isize)n;
                pos_ = 0;
            }
            linux_dirent64 *d = (linux_dirent64 *)(buf_ + pos_);
            const char *name = d->d_name;
            uint8 dtype = d->d_type;
            pos_ += d->d_reclen; // advance by reclen, not name len
            if (isDotEntry(name))
                continue;
            isize len = 0;
            while (name[len])
                ++len; // d_name is padded; scan to NUL
            return core::Ok{OptE{core::optional_in_place, DirEntryRaw{core::CStr{name, len}, fromDType(dtype)}}};
        }
    }

    DirReader::~DirReader()
    {
        if (buf_)
            core::mlwFree(buf_);
        if (handle_.isValid())
            syscall(SYS_CLOSE, (long)handle_.fd);
    }
    DirReader::DirReader(DirReader &&o) noexcept
        : handle_(o.handle_), buf_(o.buf_), len_(o.len_), pos_(o.pos_), first_(o.first_)
    {
        o.handle_ = Handle::invalid();
        o.buf_ = nullptr;
        o.len_ = 0;
        o.pos_ = 0;
        o.first_ = false;
    }
    DirReader &DirReader::operator=(DirReader &&o) noexcept
    {
        if (this != &o)
        {
            if (buf_)
                core::mlwFree(buf_);
            if (handle_.isValid())

                syscall(SYS_CLOSE, (long)handle_.fd);
            handle_ = o.handle_;
            buf_ = o.buf_;
            len_ = o.len_;
            pos_ = o.pos_;
            first_ = o.first_;
            o.handle_ = Handle::invalid();
            o.buf_ = nullptr;
            o.len_ = 0;
            o.pos_ = 0;
            o.first_ = false;
        }
        return *this;
    }

    core::Result<isize, IoError> getCwdInto(core::CStr buf)
    {
        // getcwd(buf, size): returns buf on success, -ERANGE if too small.
        long r = syscall(SYS_GETCWD, (long)buf.ptr, (long)buf.len);
        if (r < 0)
            return core::Err{fromErrno(-r)};
        // The syscall writes a NUL-terminated string and returns the LENGTH INCLUDING the NUL.
        // (This differs from the libc wrapper, which returns the pointer — the raw syscall returns length.)
        return core::Ok{r > 0 ? r - 1 : 0}; // length excluding the NUL
    }

}