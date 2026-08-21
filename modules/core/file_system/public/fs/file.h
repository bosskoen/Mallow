#include <core/io/handle.h>
#include <core/span.h>
#include <core/io/io.h>
#include <core/result.h>
#include <stl/vector.h>

namespace fs
{
    class File
    {
        io::Handle handle = io::Handle::invalid();

        explicit File(io::Handle h) : handle(h) {}

    public:
        static core::Result<File, io::IoError> open(core::CStr path, io::OpenMode mode)
        {
            core::Result<io::Handle, io::IoError> result = io::openFile(path, mode);
            if (result.isErr())
            {
                return core::Err(result.error());
            }
            return core::Ok(File(result.takeValue()));
        }
        File(const File &) = delete; // can't duplicate ownership
        File &operator=(const File &) = delete;

        File(File &&o) noexcept : handle(o.handle) { o.handle = io::Handle::invalid(); } // steal
        File &operator=(File &&o) noexcept
        {
            if (this != &o)
            {
                if (handle.isValid())
                    io::closeHandle(handle);      // release what we currently own
                handle = o.handle;                    // take theirs
                o.handle = io::Handle::invalid(); // leave them empty so their dtor no-ops
            }
            return *this;
        }
        ~File()
        {
            if (handle.isValid())
                io::closeHandle(handle);
        }

        core::Result<isize, io::IoError> write(core::Span<const uint8> data)
        {
            return io::writeFile(handle, data);
        }
        core::Result<isize, io::IoError> read(core::Span<uint8> buffer)
        {
            return io::readFile(handle, buffer);
        }
        core::Result<core::Vector<uint8>, io::IoError> readAll();
    };
} // namespace fs
