#include "io/io.h"
#include "io/terminal.h"
#include "libc/mem.h"
#include "backend/common/io_common.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

namespace io
{


    namespace
    { // file-local error mappers


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

    } // anonymous

    core::Result<Handle, IoError> openFile(core::CStr path, const OpenOptions &o)
    {
        if(!validateOpen(o)) return core::Err{invalidInput()};

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

    }

    // --- diagnostic writer: fire-and-forget, never returns a Result ---
    void writeStringToHandle(Handle handle, core::CStr str)
    {

        DWORD written;
        WriteFile(handle.fd, str.ptr, (DWORD)str.len, &written, nullptr);
        (void)written;

    }


    core::Result<isize, IoError> writeFile(Handle handle, core::Span<const uint8> data)
    {

        DWORD n;
        if (!WriteFile(handle.fd, data.ptr, (DWORD)data.len, &n, nullptr))
            return core::Err{lastError()};
        return core::Ok{(isize)n};

    }

    core::Result<isize, IoError> readFile(Handle handle, core::Span<uint8> buffer)
    {

        DWORD n;
        if (!ReadFile(handle.fd, buffer.ptr, (DWORD)buffer.len, &n, nullptr))
            return core::Err{lastError()};
        return core::Ok{(isize)n}; // n == 0 → EOF

    }

    void closeHandle(Handle handle)
    {

        CloseHandle(handle.fd);

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

        LARGE_INTEGER dist;
        dist.QuadPart = offset; // QuadPart is 64-bit
        LARGE_INTEGER out;
        DWORD method = (w == 0) ? FILE_BEGIN : (w == 1) ? FILE_CURRENT
                                                        : FILE_END;
        if (!SetFilePointerEx(handle.fd, dist, &out, method))
            return core::Err{lastError()};
        return core::Ok{(int64)out.QuadPart};

    }

    core::Result<unit, IoError> flushFile(Handle handle)
    {

        if (!FlushFileBuffers(handle.fd))
            return core::Err{lastError()};
        return core::Ok{unit{}};

    }

    core::Result<unit, IoError> truncateFile(Handle handle, int64 len)
    {
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

    }


    core::Result<int64, IoError> fileSize(Handle handle)
    {
        LARGE_INTEGER sz;
        if (!GetFileSizeEx(handle.fd, &sz))
            return core::Err{lastError()};
        return core::Ok{(int64)sz.QuadPart};

    }

    core::Result<Metadata, IoError> statHandle(Handle handle)
    {

        BY_HANDLE_FILE_INFORMATION info;
        if (!GetFileInformationByHandle(handle.fd, &info))
            return core::Err{lastError()};
        return core::Ok{fromWinInfo(info.dwFileAttributes,
                                    info.nFileSizeHigh, info.nFileSizeLow,
                                    info.ftLastAccessTime, info.ftLastWriteTime,
                                    info.ftCreationTime)};

    }

    core::Result<Metadata, IoError> statPath(core::CStr path, bool follow)
    {

        wchar_t wpath[4096];
        int wl = MultiByteToWideChar(CP_UTF8, 0, path.ptr, (int)path.len, wpath, 4095);
        wpath[wl] = L'\0';
        (void)follow; // see gotcha #3 — Windows always follows here in v1
        WIN32_FILE_ATTRIBUTE_DATA d;
        if (!GetFileAttributesExW(wpath, GetFileExInfoStandard, &d))
            return core::Err{lastError()};
        return core::Ok{fromWinInfo(d.dwFileAttributes, d.nFileSizeHigh, d.nFileSizeLow,
                                    d.ftLastAccessTime, d.ftLastWriteTime, d.ftCreationTime)};

    }

    core::Result<unit, IoError> removePath(core::CStr path)
    { // file

        wchar_t w[4096];
        toWPath(path, w);
        if (!DeleteFileW(w))
            return core::Err{lastError()};

        return core::Ok{unit{}};
    }

    core::Result<unit, IoError> removeDirPath(core::CStr path)
    { // empty dir

        wchar_t w[4096];
        toWPath(path, w);
        if (!RemoveDirectoryW(w))
            return core::Err{lastError()};

        return core::Ok{unit{}};
    }

    core::Result<unit, IoError> makeDir(core::CStr path, int32 mode)
    {

        wchar_t w[4096];
        toWPath(path, w);
        (void)mode; // Windows dirs get default ACLs
        if (!CreateDirectoryW(w, nullptr))
            return core::Err{lastError()};

        return core::Ok{unit{}};
    }

    core::Result<unit, IoError> renamePath(core::CStr from, core::CStr to)
    {
        wchar_t wf[4096], wt[4096];
        toWPath(from, wf);
        toWPath(to, wt);
        // MOVEFILE_REPLACE_EXISTING matches POSIX (which silently replaces an existing file)
        if (!MoveFileExW(wf, wt, MOVEFILE_REPLACE_EXISTING))
            return core::Err{lastError()};
        return core::Ok{unit{}};
    }

    core::Result<unit, IoError> setPermissions(core::CStr path, int32 mode)
    {
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

    }


core::Result<DirReader, IoError> openDir(core::CStr path)
{
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

}


core::Result<core::Optional<DirEntryRaw>, IoError> DirReader::nextEntry()
{
    using OptE = core::Optional<DirEntryRaw>;
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

}


DirReader::~DirReader() {
    if (buf_) core::mlwFree(buf_);
    if (handle_.isValid())
        FindClose(handle_.fd);

}
DirReader::DirReader(DirReader&& o) noexcept
    : handle_(o.handle_), buf_(o.buf_), len_(o.len_), pos_(o.pos_), first_(o.first_) {
    o.handle_ = Handle::invalid(); o.buf_ = nullptr; o.len_=0; o.pos_=0; o.first_=false;
}
DirReader& DirReader::operator=(DirReader&& o) noexcept {
    if (this != &o) {
        if (buf_) core::mlwFree(buf_);
        if (handle_.isValid())
            FindClose(handle_.fd);

        handle_=o.handle_; buf_=o.buf_; len_=o.len_; pos_=o.pos_; first_=o.first_;
        o.handle_=Handle::invalid(); o.buf_=nullptr; o.len_=0; o.pos_=0; o.first_=false;
    }
    return *this;
}

core::Result<isize, IoError> getCwdInto(core::CStr buf)
{
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

}

}