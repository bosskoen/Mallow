#include "file.h"
#include <core/libc/mem.h>
#include <core/error.h>

using namespace core;
namespace fs
{

    core::Result<Vector<uint8>, io::IoError> File::readAll()
    {
        Vector<uint8> buffer{};
        constexpr isize chunk_size = 16 * 1024;
        uint8 tmp[chunk_size];
        while (true)
        {
            core::Span<uint8> span{tmp, chunk_size};
            TRY(n, read(span));
            if (n == 0)
                break; // EOF
            buffer.extendFromPtr(span.ptr, n);
        }
        // return core::Result<core::Vector<uint8>, io::IoError>(core::move(buffer));
        return core::Ok(core::move(buffer));
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
        TRY(_, setPermissions(to,prems.permissions()));
        return core::Ok{total};
    }

} // namespace fs
