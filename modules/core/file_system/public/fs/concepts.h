#pragma once

#include <core/span.h>
#include <core/result.h>

namespace fs
{
    /// \brief A type that can read bytes into a buffer.
    ///
    /// The `read` method reads up to `dst.len` bytes into the provided buffer
    /// and returns the number of bytes read, or an error. A return value of 0
    /// indicates end-of-file (EOF).
template <typename R>
concept Reader = requires(R r, core::Span<uint8> dst){
    { r.read(dst) } -> core::same_as<core::Result<isize, io::IoError>>;
};

template <typename W>
concept Writer = requires(W w, core::Span<const uint8> src) {
    { w.write(src) } -> core::same_as<core::Result<isize, io::IoError>>;
};
} // namespace fs