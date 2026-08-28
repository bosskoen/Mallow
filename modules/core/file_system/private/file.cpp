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
        return Ok( core::move(s) );
    };

} // namespace fs
