#include <core/io/handle.h>
#include <core/span.h>
#include <core/io/io.h>
#include <core/result.h>
#include <stl/vector.h>
#include <core/error.h>
#include <stl/string.h>
#include <core/libc/mem.h>
#include <iter/iterator.h>
#include "fs/path.h"
namespace fs
{

    class FileDrain;
    class File
    {
        io::Handle handle = io::Handle::invalid();

        explicit File(io::Handle h) : handle(h) {}

    public:
        static core::Result<File, io::IoError> open(Path path, io::OpenMode mode)
        {
            core::Result<io::Handle, io::IoError> result = io::openFile(path.toCStr(), mode);
            if (result.isErr())
            {
                return core::Err(result.error());
            }
            return core::Ok(File(result.takeValue()));
        }
        static core::Result<File, io::IoError> open(Path path, const io::OpenOptions &opts)
        {
            core::Result<io::Handle, io::IoError> r = io::openFile(path.toCStr(), opts);
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
    MLW_FORCE_INLINE core::Result<io::Metadata, io::IoError> metadata(Path p) { return io::statPath(p.toCStr(), true); }
    MLW_FORCE_INLINE core::Result<io::Metadata, io::IoError> symlinkMetadata(Path p) { return io::statPath(p.toCStr(), false); }

    MLW_FORCE_INLINE core::Result<bool, io::IoError> exists(Path path)
    {
        core::Result<io::Metadata, io::IoError> r = io::statPath(path.toCStr(), true);
        if (r.isErr())
        {
            if (r.error().kind == io::IoError::Kind::NotFound)
                return core::Ok{false};
            return core::Err{r.takeError()};
        }
        return core::Ok{true};
    }

    MLW_FORCE_INLINE core::Result<bool, io::IoError> isFile(Path path)
    {
        core::Result<io::Metadata, io::IoError> r = io::statPath(path.toCStr(), true);
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

    MLW_FORCE_INLINE core::Result<unit, io::IoError> removeFile(Path p) { return io::removePath(p.toCStr()); }
    MLW_FORCE_INLINE core::Result<unit, io::IoError> removeDir(Path p) { return io::removeDirPath(p.toCStr()); }
    MLW_FORCE_INLINE core::Result<unit, io::IoError> createDir(Path p) { return io::makeDir(p.toCStr(), 0755); }
    MLW_FORCE_INLINE core::Result<unit, io::IoError> rename(Path a, Path b) { return io::renamePath(a.toCStr(), b.toCStr()); }

    core::Result<unit, io::IoError> createDirAll(Path path);

    core::Result<int64, io::IoError> copy(Path from, Path to);

    MLW_FORCE_INLINE core::Result<core::Vector<uint8>, io::IoError> readToBytes(Path path)
    {
        core::Result<File, io::IoError> r = File::open(path, io::OpenMode::Read);
        if (r.isErr())
            return core::Err{r.takeError()};
        File f = r.takeValue();
        return f.readAll();
    }
    MLW_FORCE_INLINE core::Result<core::String, io::IoError> readToString(Path path)
    {
        core::Result<File, io::IoError> r = File::open(path, io::OpenMode::Read);
        if (r.isErr())
            return core::Err{r.takeError()};
        File f = r.takeValue();
        return f.readText();
    }
    MLW_FORCE_INLINE core::Result<unit, io::IoError> writeBytes(Path path, core::Span<const uint8> data)
    {
        core::Result<File, io::IoError> r = File::open(path, io::OpenMode::Write);
        if (r.isErr())
            return core::Err{r.takeError()};
        File f = r.takeValue();
        return f.writeAll(data);
    }
    MLW_FORCE_INLINE core::Result<unit, io::IoError> appendBytes(Path path, core::Span<const uint8> data)
    {
        core::Result<File, io::IoError> r = File::open(path, io::OpenMode::Append);
        if (r.isErr())
            return core::Err{r.takeError()};
        File f = r.takeValue();
        return f.writeAll(data);
    }
    MLW_FORCE_INLINE core::Result<unit, io::IoError>
    setPermissions(Path p, int32 mode) { return io::setPermissions(p.toCStr(), mode); }

    core::Result<unit, io::IoError> removeDirAll(Path path);

    class DirEntry
    {
        core::String name_{};
        io::FileType type_;

    public:
        DirEntry(Path n, io::FileType t) : type_(t) { name_.append(n.toCStr()); }
        core::CStr name() const { return name_.slice(0, name_.len()); }
        io::FileType fileType() const { return type_; }
        bool isDir() const { return type_ == io::FileType::Dir; }
        bool isFile() const { return type_ == io::FileType::File; }
        bool isSymlink() const { return type_ == io::FileType::Symlink; }
    };

    struct ReadDir : itorator::IterBase<ReadDir>
    {
        io::DirReader reader;
        explicit ReadDir(io::DirReader &&r) : reader(core::move(r)) {}

        using Item = core::Result<DirEntry, io::IoError>;
        core::Optional<Item> next()
        {
            core::Result<core::Optional<io::DirEntryRaw>, io::IoError> r = reader.nextEntry();
            if (r.isErr())
                return core::Optional<Item>{core::optional_in_place, core::Err{r.takeError()}};
            core::Optional<io::DirEntryRaw> &e = r.value();
            if (e.isNone())
                return {}; // end of directory
            return core::Optional<Item>{core::optional_in_place,
                                        core::Ok{DirEntry{e->name, e->type}}}; // copy happens in DirEntry ctor
        }
    };

    MLW_FORCE_INLINE core::Result<ReadDir, io::IoError> readDir(Path path)
    {
        core::Result<io::DirReader, io::IoError> r = io::openDir(path.toCStr());
        if (r.isErr())
            return core::Err{r.takeError()};
        return core::Ok{ReadDir{r.takeValue()}};
    }

    MLW_FORCE_INLINE core::Result<core::Vector<DirEntry>, io::IoError> readDirAll(Path path)
    {
        TRY(rd, readDir(path));
        core::Vector<DirEntry> out;
        for (core::Result<fs::DirEntry, io::IoError>& it: core::move(rd).range())
        {
            if (it.isErr())
                return core::Err{it.takeError()};
            out.push(core::move(it.value()));
        }
        return core::Ok{core::move(out)};
    }

} // namespace fs
