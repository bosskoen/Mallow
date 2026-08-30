#include <core/io/handle.h>
#include <core/span.h>
#include <core/io/io.h>
#include <core/result.h>
#include <stl/vector.h>
#include <core/error.h>
#include <stl/string.h>
#include <core/libc/mem.h>

namespace fs
{

    class FileDrain;
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
        static core::Result<File, io::IoError> open(core::CStr path, const io::OpenOptions &opts)
        {
            core::Result<io::Handle, io::IoError> r = io::openFile(path, opts);
            if (r.isErr())
                return core::Err(r.takeError());
            return core::Ok(File(r.takeValue()));
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
                handle = o.handle;                // take theirs
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
        core::Result<core::String, io::IoError> readText();
        core::Result<unit, io::IoError> writeAll(core::Span<const uint8> data);
        core::Result<unit, io::IoError> readExact(core::Span<uint8> data);

        core::Result<int64, io::IoError> seek(io::SeekFrom whence, int64 offset)
        {
            return io::seekFile(handle, whence, offset);
        }

        core::Result<int64, io::IoError> tell()
        {
            return seek(io::SeekFrom::Current, 0);
        }

        core::Result<unit, io::IoError> flush()
        {
            return io::flushFile(handle);
        }

        core::Result<unit, io::IoError> setLen(int64 len)
        {
            return io::truncateFile(handle, len);
        }
        core::Result<int64, io::IoError> size() { return io::fileSize(handle); }

        core::Result<io::Metadata, io::IoError> metadata() { return io::statHandle(handle); }

        // size() as above
        // sync
        // ofsets /trucation
        // free function
        // some form of itoration/ buferd optations

        class FileDrain drain() &&;
    };

    // fs free functions
    MLW_FORCE_INLINE core::Result<io::Metadata, io::IoError> metadata(core::CStr p) { return io::statPath(p, true); }
    MLW_FORCE_INLINE core::Result<io::Metadata, io::IoError> symlinkMetadata(core::CStr p) { return io::statPath(p, false); }

    MLW_FORCE_INLINE core::Result<bool, io::IoError> exists(core::CStr path)
    {
        core::Result<io::Metadata, io::IoError> r = io::statPath(path, true);
        if (r.isErr())
        {
            if (r.error().kind == io::IoError::Kind::NotFound)
                return core::Ok{false};
            return core::Err{r.takeError()};
        }
        return core::Ok{true};
    }

    MLW_FORCE_INLINE core::Result<bool, io::IoError> isFile(core::CStr path)
    {
        core::Result<io::Metadata, io::IoError> r = io::statPath(path, true);
        if (r.isErr())
        {
            if (r.error().kind == io::IoError::Kind::NotFound)
                return core::Ok{false};
            return core::Err{r.takeError()};
        }
        return core::Ok{r.value().isFile()};
    }

    class FileDrain
    {
        File file;
        uint8 *buffer;
        static constexpr usize size = 16 * 1024; // 16k
    public:
        using Item = core::Result<core::Span<uint8>, io::IoError>;
        FileDrain(File &&f) : file(core::move(f)) { buffer = static_cast<uint8 *>(core::mlwMalloc(size)); }
        ~FileDrain()
        {
            core::mlwFree(buffer);
        }
        FileDrain(const FileDrain &) = delete;
        FileDrain &operator=(const FileDrain &) = delete;
        FileDrain(FileDrain &&f) : file(core::move(f.file)), buffer(f.buffer)
        {
            f.buffer = nullptr;
        }
        core::Optional<Item> next()
        {
            core::Span<uint8> span(buffer, size);
            core::Result<isize, io::IoError> result = file.read(span);
            if (result.isErr())
            {
                // return core::None;
                return core::Optional<Item>{core::optional_in_place, core::Err{result.error()}};
            }
            if (result.value() == 0)
            {
                return nullptr;
            }
            span.len = result.value();
            return core::Optional<Item>{core::optional_in_place, core::Ok(span)};
        }
    };

    MLW_FORCE_INLINE FileDrain File::drain() && { return FileDrain{core::move(*this)}; }

    MLW_FORCE_INLINE core::Result<unit, io::IoError> removeFile(core::CStr p) { return io::removePath(p); }
    MLW_FORCE_INLINE core::Result<unit, io::IoError> removeDir(core::CStr p) { return io::removeDirPath(p); }
    MLW_FORCE_INLINE core::Result<unit, io::IoError> createDir(core::CStr p) { return io::makeDir(p, 0755); }
    MLW_FORCE_INLINE core::Result<unit, io::IoError> rename(core::CStr a, core::CStr b) { return io::renamePath(a, b); }

    core::Result<unit, io::IoError> createDirAll(core::CStr path);

    core::Result<int64, io::IoError> copy(core::CStr from, core::CStr to);

    MLW_FORCE_INLINE core::Result<core::Vector<uint8>, io::IoError> readToBytes(core::CStr path)
    {
        core::Result<File, io::IoError> r = File::open(path, io::OpenMode::Read);
        if (r.isErr())
            return core::Err{r.takeError()};
        File f = r.takeValue();
        return f.readAll();
    }
    MLW_FORCE_INLINE core::Result<core::String, io::IoError> readToString(core::CStr path)
    {
        core::Result<File, io::IoError> r = File::open(path, io::OpenMode::Read);
        if (r.isErr())
            return core::Err{r.takeError()};
        File f = r.takeValue();
        return f.readText();
    }
    MLW_FORCE_INLINE core::Result<unit, io::IoError> writeBytes(core::CStr path, core::Span<const uint8> data)
    {
        core::Result<File, io::IoError> r = File::open(path, io::OpenMode::Write);
        if (r.isErr())
            return core::Err{r.takeError()};
        File f = r.takeValue();
        return f.writeAll(data);
    }
    MLW_FORCE_INLINE core::Result<unit, io::IoError> appendBytes(core::CStr path, core::Span<const uint8> data)
    {
        core::Result<File, io::IoError> r = File::open(path, io::OpenMode::Append);
        if (r.isErr())
            return core::Err{r.takeError()};
        File f = r.takeValue();
        return f.writeAll(data);
    }
    MLW_FORCE_INLINE core::Result<unit, io::IoError>
    setPermissions(core::CStr p, int32 mode) { return io::setPermissions(p, mode); }

} // namespace fs
