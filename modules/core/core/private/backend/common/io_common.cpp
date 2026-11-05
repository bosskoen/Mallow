
#include "io/io.h"
#include "backend/common/io_common.h"

namespace io
{
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
    
} // namespace io
