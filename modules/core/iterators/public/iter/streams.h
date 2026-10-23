#pragma once

namespace itorator
{
    template <typename R>
    concept Reader = requires(R r, core::Span<uint8> dst) {
        { r.read(dst) } -> core::same_as<core::Result<isize, io::IoError>>;
        { r.readAll() } -> core::same_as<core::Result<core::Vector<uint8>, io::IoError>>;
        { r.readExact(dst) } -> core::same_as<core::Result<unit, io::IoError>>;
    };

    template <typename R>
    concept BufRead = Reader<R> && requires(R r, isize n) {
        { r.fillBuf() } -> core::same_as<core::Result<core::Span<const uint8>, io::IoError>>; // peek buffered bytes (refills if empty)
        { r.consume(n) };                                                                     // mark n bytes used
    };

    template <Reader R>
        requires !core::is_same_v<BufferedReader, R>
                 class BufferedReader
    {
        R inner;
        core::Vector<uint8> buffer; // storage; [pos, filled) are pending unread bytes
        isize pos = 0;
        isize filled = 0;
        static constexpr isize READ_CHUNK = 16 * 1024;

    public:
        explicit BufferedReader(R &&r) : inner(core::move(r)) { buffer.resize(READ_CHUNK); }

        BufferedReader(const BufferedReader &) = delete;
        BufferedReader &operator=(const BufferedReader &) = delete;
        BufferedReader(BufferedReader &&) noexcept = default;
        BufferedReader &operator=(BufferedReader &&) noexcept = default;

        core::Result<isize, io::IoError> read(core::Span<uint8> dst)
        {
            // A. Serve from buffer if we have pending bytes — no syscall.
            if (pos < filled)
            {
                isize n = filled - pos;
                if (n > dst.len)
                    n = dst.len;
                core::mlwMemcpy(dst.ptr, buffer.rawData() + pos, n);
                pos += n;
                return core::Ok{n};
            }
            // B. Buffer empty + request >= a chunk: bypass buffer, read straight through.
            if (dst.len >= READ_CHUNK)
                return inner.read(dst);
            // C. Buffer empty + small request: refill one chunk, then serve from it.
            core::Result<isize, io::IoError> r =
                inner.read(core::Span<uint8>{buffer.rawData(), READ_CHUNK});
            if (r.isErr())
                return core::Err{r.takeError()};
            filled = r.value();
            pos = 0;
            if (filled == 0)
                return core::Ok{isize(0)}; // EOF
            isize n = filled;
            if (n > dst.len)
                n = dst.len;
            core::mlwMemcpy(dst.ptr, buffer.rawData(), n);
            pos = n;
            return core::Ok{n};
        }
        core::Result<core::Vector<uint8>, io::IoError> readAll()
        {
            // 1. Drain the leftover buffered bytes (if any) into the result first.

            // 2. Let inner do the rest its own optimal way (File stats+sizes, Slice one-copies...).
            core::Result<core::Vector<uint8>, io::IoError> rest = inner.readAll();
            if (rest.isErr())
                return core::Err{rest.takeError()};

            core::Vector<uint8> out;
            if (pos < filled)
            {
                out.extendFromPtr(buffer.rawData() + pos, filled - pos);
                pos = filled; // buffer now consumed
            }

            // 3. If there was no leftover, inner's vector IS the answer — move it, zero copies.
            if (out.len() == 0)
                return core::Ok{core::move(rest.takeValue())};

            // 4. Otherwise append inner's bytes after our buffered prefix.
            core::Vector<uint8> tail = rest.takeValue();
            out.extendFromPtr(tail.rawData(), tail.len());
            return core::Ok{core::move(out)};
        }
        core::Result<unit, io::IoError> readExact(core::Span<uint8> dst)
        {
            while (dst.len > 0)
            {
                core::Result<isize, io::IoError> r = read(dst); // our buffered read
                if (r.isErr())
                    return core::Err{r.takeError()};
                if (r.value() == 0)
                    return core::Err{io::IoError{io::IoError::Kind::UnexpectedEof, 0}};
                dst.ptr += r.value();
                dst.len -= r.value();
            }
            return core::Ok{unit{}};
        }
        core::Result<core::Span<const uint8>, io::IoError> fillBuf()
        {
            if (pos < filled)
            {
                return core::Span{buffer.data() + pos, filled - pos};
            }
            core::Result<isize, io::IoError> r =
                inner.read(core::Span<uint8>{buffer.rawData(), READ_CHUNK});
            if (r.isErr())
                return core::Err{r.takeError()};
            filled = r.value();
            pos = 0;
            return core::Span{buffer.data(), filled};
        }
        void consume(isize n)
        {
            if (pos + n > filled)
            {
                pos = filled;
            }
            else
            {
                pos += n;
            }
        }
    };

    struct SliceReader
    {
        core::Span<const uint8> data;
        isize pos = 0;

        explicit SliceReader(core::Span<const uint8> d) : data(d) {}

        core::Result<isize, io::IoError> read(core::Span<uint8> dst)
        {
            isize n = data.len - pos;
            if (n > dst.len)
                n = dst.len;
            for (isize i = 0; i < n; ++i)
                dst.ptr[i] = data.ptr[pos + i];
            pos += n;
            return core::Ok{n}; // n==0 means exhausted (EOF)
        }

        core::Result<core::Vector<uint8>, io::IoError> readAll()
        {
            core::Vector<uint8> out;
            isize n = data.len - pos;
            if (n > 0)
            {
                out.reserve(n);
                for (isize i = 0; i < n; ++i)
                    out.rawData()[i] = data.ptr[pos + i];
                pos += n;
            }
            return core::Ok{core::move(out)};
        }

        core::Result<unit, io::IoError> readExact(core::Span<uint8> dst)
        {
            if (data.len - pos < dst.len) // not enough bytes left
                return core::Err{io::IoError{io::IoError::Kind::UnexpectedEof, 0}};
            for (isize i = 0; i < dst.len; ++i)
                dst.ptr[i] = data.ptr[pos + i];
            pos += dst.len;
            return core::Ok{unit{}};
        }

        core::Result<core::Span<const uint8>, io::IoError> fillBuf()
        {
            return core::Ok{core::Span<const uint8>{data.ptr + pos, data.len - pos}}; // never refills, never errs
        }
        void consume(isize n)
        {
            pos += n;
            if (pos > data.len)
                pos = data.len;
        }
    };

    MLW_FORCE_INLINE SliceReader readerOver(const core::String &s)
    {
        return SliceReader{core::Span<const uint8>{(const uint8 *)s.data(), s.len()}};
    }

    template <typename W>
    concept Writer = requires(W w, core::Span<const uint8> src) {
        { w.write(src) } -> core::same_as<core::Result<isize, io::IoError>>;   // may be short
        { w.writeAll(src) } -> core::same_as<core::Result<unit, io::IoError>>; // loops till done
        { w.flush() } -> core::same_as<core::Result<unit, io::IoError>>;       // force bytes out
    };

    template <Writer W>
    class BufferedWriter
    {
        W inner;
        core::Vector<uint8> buffer;
        isize len = 0; // bytes currently buffered
        static constexpr isize WRITE_CHUNK = 16 * 1024;

    public:
        explicit BufferedWriter(W &&w) : inner(core::move(w)) { buffer.resize(WRITE_CHUNK); }

        BufferedWriter(const BufferedWriter &) = delete;
        BufferedWriter &operator=(const BufferedWriter &) = delete;
        BufferedWriter(BufferedWriter &&) noexcept = default;
        BufferedWriter &operator=(BufferedWriter &&) noexcept = default;

        core::Result<isize, io::IoError> write(core::Span<const uint8> src)
        {
            // big write: flush what we have, then write straight through (don't double-copy)
            if (src.len >= WRITE_CHUNK)
            {
                core::Result<unit, io::IoError> f = flushBuffer();
                if (f.isErr())
                    return core::Err{f.takeError()};
                return inner.write(src);
            }
            // won't fit: flush first
            if (len + src.len > WRITE_CHUNK)
            {
                core::Result<unit, io::IoError> f = flushBuffer();
                if (f.isErr())
                    return core::Err{f.takeError()};
            }
            core::mlwMemcpy(buffer.rawData() + len, src.ptr, src.len); 
            len += src.len;
            return core::Ok{src.len};
        }

        core::Result<unit, io::IoError> writeAll(core::Span<const uint8> src)
        {
            // write() already takes everything (buffered or passed through), so loop defensively
            while (src.len > 0)
            {
                core::Result<isize, io::IoError> r = write(src);
                if (r.isErr())
                    return core::Err{r.takeError()};
                src.ptr += r.value();
                src.len -= r.value();
            }
            return core::Ok{unit{}};
        }

        // flush = push OUR buffer to inner, then flush inner too (chain the durability)
        core::Result<unit, io::IoError> flush()
        {
            core::Result<unit, io::IoError> f = flushBuffer();
            if (f.isErr())
                return core::Err{f.takeError()};
            return inner.flush();
        }

        ~BufferedWriter() { (void)flushBuffer(); } // best-effort — SEE BELOW

    private:
        core::Result<unit, io::IoError> flushBuffer()
        {
            if (len == 0)
                return core::Ok{unit{}};
            core::Result<unit, io::IoError> r = inner.writeAll(core::Span<const uint8>{buffer.rawData(), len});
            len = 0;
            return r;
        }
    };

class MemWriter
    {
        core::Vector<uint8> buf;          // OWNS its storage — standalone value

    public:
        MemWriter() = default;
        explicit MemWriter(isize reserve) { buf.reserve(reserve); }   // optional size hint

        MemWriter(const MemWriter&) = delete;           // move-only, like the other streams
        MemWriter& operator=(const MemWriter&) = delete;
        MemWriter(MemWriter&&) noexcept = default;
        MemWriter& operator=(MemWriter&&) noexcept = default;

        // --- Writer concept ---
        core::Result<isize, io::IoError> write(core::Span<const uint8> src) {
            buf.extendFromPtr(src.ptr, src.len);
            return core::Ok{ src.len };            // memory never short-writes, never errs
        }
        core::Result<unit, io::IoError> writeAll(core::Span<const uint8> src) {
            buf.extendFromPtr(src.ptr, src.len);
            return core::Ok{ unit{} };
        }
        core::Result<unit, io::IoError> flush() { return core::Ok{ unit{} }; }

        // --- extraction: get your data back OUT, consuming the writer ---
        core::Vector<uint8> intoBytes() && { return core::move(buf); }   // move the bytes out
        core::String        intoString() && {                            // bytes -> text
            core::String s;
            s.append(core::CStr{ (const char*)buf.rawData(), buf.len() });
            return s;
        }

        // --- non-consuming peek, if you want to look without giving up ownership ---
        core::Span<const uint8> bytes() const { return core::Span<const uint8>{ buf.rawData(), buf.len() }; }
        isize len() const { return buf.len(); }
    };

} // namespace itorator
