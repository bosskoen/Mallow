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
        while (true){
            core::Span<uint8> span{tmp, chunk_size};
            TRY(n, read(span));
            if (n == 0)
                break; // EOF
            buffer.extendFromPtr(span.ptr, n);
        }
        //return core::Result<core::Vector<uint8>, io::IoError>(core::move(buffer));
        return core::Ok(core::move(buffer));
    }

} // namespace fs
