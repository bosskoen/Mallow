#include "file.h"
#include <core/libc/mem.h>
#include <core/error.h>

using namespace core;
namespace fs
{

    core::Result<core::Vector<uint8>, io::IoError> File::readAll()
    {
        core::Vector<uint8> out;

        // --- size hint: how many bytes remain from the current cursor? ---
        // size() is the statx call; tell() is seek(Current,0). Both can fail
        // (pipe, special file) — if so, fall back to the plain growing loop.
        core::Result<int64, io::IoError> szR = size();
        core::Result<int64, io::IoError> posR = tell();

        if (szR.isOk() && posR.isOk() && szR.value() > posR.value())
        {
            int64 remaining = szR.value() - posR.value();
            out.reserve((isize)remaining); // one allocation, exact size

            core::Span<uint8> dst{out.rawData(), (isize)remaining};
            while (dst.len > 0)
            {
                core::Result<isize, io::IoError> r = read(dst); // raw single read
                if (r.isErr())
                    return core::Err{r.takeError()};
                if (r.value() == 0)
                {                                             // file shrank, or hint was stale
                    out.reserve((isize)(remaining - dst.len)); // trim to what we got
                    return core::Ok{core::move(out)};
                }
                dst.ptr += r.value();
                dst.len -= r.value();
            }
            // Loop filled exactly `remaining`. But the file may have GROWN since the
            // stat (another writer), so there could be more — do ONE more read to check.
            for (;;)
            {
                uint8 tmp[16 * 1024];
                core::Result<isize, io::IoError> r = read(core::Span<uint8>{tmp, (isize)sizeof tmp});
                if (r.isErr())
                    return core::Err{r.takeError()};
                if (r.value() == 0)
                    break;                         // confirmed EOF
                out.extendFromPtr(tmp, r.value()); // rare: file grew, append the extra
            }
            return core::Ok{core::move(out)};
        }

        // --- fallback: no usable size hint (pipe/terminal/special) -> plain growing loop ---
        uint8 tmp[16 * 1024];
        for (;;)
        {
            core::Result<isize, io::IoError> r = read(core::Span<uint8>{tmp, (isize)sizeof tmp});
            if (r.isErr())
                return core::Err{r.takeError()};
            if (r.value() == 0)
                break;
            out.extendFromPtr(tmp, r.value());
        }
        return core::Ok{core::move(out)};
    }

    core::Result<unit, io::IoError> File::writeAll(core::Span<const uint8> data)
    {
        while (!data.empty())
        {
            TRY(n, write(data));
            if (n == 0)
                return core::Err{io::IoError{io::IoError::Kind::WriteZero, 0}};
            data.ptr += n;
            data.len -= n;
        }

        return core::Ok{unit{}};
    }

    core::Result<unit, io::IoError> File::readExact(core::Span<uint8> data)
    {
        while (!data.empty())
        {
            TRY(n, read(data));
            if (n == 0)
                return core::Err{io::IoError{io::IoError::Kind::UnexpectedEof, 0}};
            data.ptr += n;
            data.len -= n;
        }
        return core::Ok{unit{}};
    }

    core::Result<core::String, io::IoError> File::readText()
    {
        core::Result<core::Vector<uint8>, io::IoError> r = readAll();
        if (r.isErr())
        {
            return Err(r.takeError());
        }
        core::String s{};
        s.append({reinterpret_cast<char *>(r.value().rawData()), r.value().len()});
        return Ok(core::move(s));
    }

    core::Result<unit, io::IoError> createDirAll(core::CStr path)
    {
        char buf[4096];
        if (path.len >= (index_t)sizeof(buf))
            return core::Err{io::IoError{io::IoError::Kind::InvalidInput, 22}};
        for (index_t i = 0; i < path.len; ++i)
            buf[i] = path.ptr[i];

        for (index_t i = 1; i <= path.len; ++i)
        {
            if (i == path.len || buf[i] == '/'
#ifdef MLW_WINDOWS
                || buf[i] == '\\'
#endif
            )
            { // '/' only — see gotcha
                core::Result<unit, io::IoError> r = io::makeDir(core::CStr{buf, i}, 0755);
                if (r.isErr() && r.error().kind != io::IoError::Kind::AlreadyExists)
                    return r;
            }
        }
        return core::Ok{unit{}};
    }

    core::Result<int64, io::IoError> copy(core::CStr from, core::CStr to)
    {
        core::Result<File, io::IoError> rs = File::open(from, io::OpenMode::Read);
        if (rs.isErr())
            return core::Err{rs.takeError()};
        File src = rs.takeValue();

        TRY(prems, src.metadata());

        core::Result<File, io::IoError> rd = File::open(to, io::OpenMode::Write); // create+trunc
        if (rd.isErr())
            return core::Err{rd.takeError()};
        File dst = rd.takeValue();

        uint8 buf[16 * 1024];
        int64 total = 0;
        for (;;)
        {
            core::Result<isize, io::IoError> rn = src.read(core::Span<uint8>{buf, (isize)sizeof buf});
            if (rn.isErr())
                return core::Err{rn.takeError()};
            isize n = rn.value();
            if (n == 0)
                break;
            core::Result<unit, io::IoError> w = dst.writeAll(core::Span<const uint8>{buf, n});
            if (w.isErr())
                return core::Err{w.takeError()};
            total += n;
        }
        TRY(_, setPermissions(to, prems.permissions()));
        return core::Ok{total};
    }

    core::Result<unit, io::IoError> removeDirAll(core::CStr path)
    {
        TRY(entries, readDirAll(path)); // dir fd closed once this returns

        char child[4096];
        isize base = path.len;
        if (base + 1 >= (isize)sizeof(child))
            return core::Err{io::IoError{io::IoError::Kind::InvalidInput, 22}};
        for (isize i = 0; i < base; ++i)
            child[i] = path.ptr[i];
        if (base == 0 || child[base - 1] != '/')
            child[base++] = '/'; // v1: '/' — Path lands in M6

        DirEntry *es = entries.rawData();
        for (isize k = 0; k < (isize)entries.len(); ++k)
        {
            core::CStr nm = es[k].name();
            if (base + nm.len + 1 >= (isize)sizeof(child))
                return core::Err{io::IoError{io::IoError::Kind::InvalidInput, 22}};
            for (isize i = 0; i < nm.len; ++i)
                child[base + i] = nm.ptr[i];
            child[base + nm.len] = '\0';
            core::CStr cp{child, base + nm.len};

            if (es[k].isDir())
            {
                TRY(_, removeDirAll(cp));
            } // recurse into real dirs only
            else
            {
                TRY(_, removeFile(cp));
            } // symlink-to-dir hits this: unlinked, not followed
        }
        return removeDir(path);
    }

} // namespace fs
