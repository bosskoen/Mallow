#pragma once
#include "../c_string.h"
#include "../libc/math.h"

/// \file
/// \brief The formatting core: concepts, sinks, and the `{}` format engine.
///
/// Public surface is the \ref FormatBuffer / \ref Formattable / \ref
/// FormattableValue concepts, \ref core::Sink, and \ref core::FormatBufferType.
/// The actual rendering (integers, floats, hex, the placeholder parser) lives in
/// `core::detail` and is excluded from these docs; see it with a maintainer
/// build (`INTERNAL_DOCS = YES`).

/// \defgroup formattable Formattable Types
/// \brief Types printable through the format API.
///
/// Each member implements a `format()` customization point that the format core
/// calls; you don't call it directly. A type joins this group by satisfying
/// \ref core::Formattable and tagging itself with `\ingroup formattable`.

namespace core
{

    /// \brief Requirements for a type usable as a formatting output buffer.
    ///
    /// A `FormatBuffer` can append a \ref CStr and append a single `char`.
    /// \ref FormatBufferType and \ref Sink both satisfy this.
    template <typename T>
    concept FormatBuffer = requires(T &buffer, const CStr &str, char c) {
        { buffer.append(str) } -> same_as<void>;
        { buffer.append(c) } -> same_as<void>;
    };

    /// \brief A type `T` that can format itself into a `Buffer`.
    ///
    /// Satisfied when `T` has a `format(Buffer&) const` member. This is the
    /// customization point user types implement to become printable.
    template <typename T, typename Buffer>
    concept Formattable = FormatBuffer<Buffer> && requires(const T &value, Buffer &buffer) {
        { value.format(buffer) } -> same_as<void>;
    };

    /// \brief Type-erased \ref FormatBuffer.
    ///
    /// Wraps any concrete buffer behind function pointers so non-template code
    /// (and types whose `format()` only accepts a `Sink`) can write output
    /// without being templated on the buffer type. Build one with \ref makeSink.
    struct Sink
    {
        void *ctx;                                ///< Opaque pointer to the wrapped buffer.
        void (*append_str)(void *, const CStr &); ///< Thunk: append a string to \c ctx.
        void (*append_chr)(void *, char);         ///< Thunk: append a char to \c ctx.

        /// \brief Append a string.
        inline void append(const CStr &str) { append_str(ctx, str); }
        /// \brief Append a single character.
        inline void append(char c) { append_chr(ctx, c); }
    };

    /// \brief Wrap a concrete \ref FormatBuffer into a type-erased \ref Sink.
    /// \param b  The buffer to wrap; must outlive the returned Sink.
    template <FormatBuffer Buffer>
    MLW_FORCE_INLINE Sink makeSink(Buffer &b)
    {
        return Sink{
            &b,
            [](void *p, const CStr &s)
            { static_cast<Buffer *>(p)->append(s); },
            [](void *p, char c)
            { static_cast<Buffer *>(p)->append(c); },
        };
    }

    /// \brief Everything the format core knows how to render.
    ///
    /// True for the built-in cases handled directly by \ref detail::formatValue
    /// (arrays, \ref CStr, `const char*`, `char`, `bool`, integers, floats,
    /// raw pointers) or for any user type satisfying \ref Formattable into
    /// `Buffer` or into a \ref Sink.
    ///
    /// \note The `Buffer` parameter is required: it lets a user type format
    ///       directly into the concrete buffer (\ref Formattable "Formattable<T, Buffer>")
    ///       and only fall back to the type-erased \ref Sink path when it can't.
    template <typename T, typename Buffer>
    concept FormattableValue =
        is_array_v<remove_const_t<T>> ||
        is_same_v<remove_const_t<T>, CStr> ||
        is_same_v<remove_const_t<T>, const char *> ||
        is_same_v<remove_const_t<T>, char> ||
        is_bool_v<remove_const_t<T>> ||
        is_integer_v<remove_const_t<T>> ||
        is_float_v<remove_const_t<T>> ||
        is_pointer_v<remove_const_t<T>> ||
        Formattable<T, Buffer> ||
        Formattable<T, Sink>;

    namespace detail
    {
        /// \brief A growable character buffer that satisfies \ref FormatBuffer.
        ///
        /// The concrete output target used by the format API. Owns a heap buffer
        /// that grows on demand.
        ///
        /// This type is intended for a process-wide scratch buffer managed by the
        /// runtime/CRT. The backing storage is lazily allocated when first used
        /// and may be freed during program shutdown.
        struct FormatBufferType
        {
        private:
            /// \brief Grow the backing storage when capacity is exhausted.
            void realocate();

        public:
            char *ptr;        ///< Backing storage (owned).
            index_t len;      ///< Number of characters currently written.
            index_t capacity; ///< Allocated capacity in characters.

            /// \brief Append a string, growing if needed.
            void append(const CStr &str);

            /// \brief Append a single byte, growing if needed.
            void append(const uint8 value);
        };

        /// \brief Access the process-wide scratch \ref FormatBufferType.
        ///
        /// The scratch buffer is owned by the runtime and lazily initialized on
        /// first use. Its lifetime is tied to process shutdown so callers do not
        /// need to explicitly destroy it.
        FormatBufferType &getFormatBuffer();

        /// \brief Append the decimal digits of an unsigned value.
        /// Builds digits reversed into a small stack buffer, then flips them.
        template <FormatBuffer Buffer>
        inline void formatUInt(Buffer &buffer, uint64 value)
        {
            if (value == 0)
            {
                buffer.append('0');
                return;
            }

            char tmp[20];
            index_t len = 0;

            while (value > 0)
            {
                tmp[len++] = '0' + (value % 10);
                value /= 10;
            }

            for (index_t i = 0; i < len / 2; ++i)
            {
                char t = tmp[i];
                tmp[i] = tmp[len - 1 - i];
                tmp[len - 1 - i] = t;
            }

            buffer.append(CStr(tmp, len));
        }

        /// \brief Append the decimal digits of a signed value, with sign.
        /// Emits `-` then delegates the magnitude to \ref formatUInt.
        template <FormatBuffer Buffer>
        inline void formatInt(Buffer &buffer, int64 value)
        {
            if (value == 0)
            {
                buffer.append('0');
                return;
            }

            bool negative = value < 0;
            if (negative)
                buffer.append('-');
            formatUInt(buffer, negative ? static_cast<uint64>(-value) : static_cast<uint64>(value));
        }

        /// \brief Append a floating-point value in decimal or scientific form.
        /// \param precision       Fractional digits, clamped to [1, 16].
        /// \param fixedPrecision  If true, keep trailing zeros; otherwise trim.
        /// Very large (>1e14) or very small (<1e-4) magnitudes switch to
        /// `<mantissa>e<exponent>` notation. NaN and infinities are spelled out.
        template <FormatBuffer Buffer>
        inline void formatFloat(Buffer &buffer, f64 value, index_t precision = 6, bool fixedPrecision = false)
        {
            precision = mlwClamp(precision, 1, 16); // make mlw call
            if (mlwIsNaN(value))                    // NaN
            {
                buffer.append("NaN");
                return;
            }
            if (mlwIsInf(value))
            {
                buffer.append(value < 0.0 ? ::core::CStr("-Inf") : ::core::CStr("Inf"));
                return;
            }

            if (value == 0.0)
            {
                if (fixedPrecision)
                {
                    char tmp[20] = {};
                    index_t len = 0;
                    tmp[len++] = '0';
                    tmp[len++] = '.';
                    for (index_t i = 0; i < precision; ++i)
                    {
                        tmp[len++] = '0';
                    }
                    buffer.append(CStr(tmp, len));
                }
                else
                {
                    buffer.append("0.0");
                }
                return;
            }

            // if bigger than somthing
            if (value < 0.0)
            {
                buffer.append('-');
                value = mlwCopySign(value, 1.0);
            }

            if (value > 1e14 || value < 1e-4)
            {
                // use scientific notation
                int64 exponent = static_cast<int64>(mlwFloor(mlwLog10(value)));
                value /= mlwPow(10.0, static_cast<f64>(exponent));

                if (value >= 10.0)
                {
                    value /= 10.0;
                    ++exponent;
                }
                else if (value < 1.0)
                {
                    value *= 10.0;
                    --exponent;
                }
                formatFloat(buffer, value, precision, fixedPrecision);
                buffer.append('e');
                formatInt(buffer, exponent);
                return;
            }

            int64 integer_part = static_cast<int64>(value);
            formatUInt(buffer, static_cast<uint64>(integer_part));
            f64 frac = value - static_cast<f64>(integer_part);

            char tmp[20];
            index_t len = 0;

            tmp[len++] = '.';

            f64 remaining = frac;
            for (index_t i = 0; i < precision; ++i)
            {
                remaining *= 10;
                tmp[len++] = '0' + static_cast<uint8>(remaining);
                remaining -= static_cast<uint8>(remaining);
            }

            if (!fixedPrecision)
            {
                // trim trailing zeros
                while (len > 2 && tmp[len - 1] == '0')
                    --len;
            }

            buffer.append(CStr(tmp, len));
        }

        /// \brief Append a value as fixed-width, zero-padded hexadecimal.
        /// Width is the pointer width of the target (16 or 8 nibbles) and the
        /// output is prefixed with `0x`.
        /// \note A value of 0 is rendered as `nullptr`, since this is the
        ///       pointer-formatting path.
        template <FormatBuffer Buffer>
        inline void formatHex(Buffer &buffer, usize value)
        {
            if (value == 0)
            {
                buffer.append("nullptr");
                return;
            }
            buffer.append("0x");

#if defined(MLW_X64) || defined(MLW_ARM64)
            const index_t num_nibbles = 16;
#elif defined(MLW_X86) || defined(MLW_ARM32)
            const index_t num_nibbles = 8;
#else
#error "Unknow ararchitecture"
#endif
            char tmp[num_nibbles];
            index_t len = num_nibbles;
            while (value > 0)
            {
                uint8 nibble = value & 0xF;
                tmp[--len] = (nibble < 10) ? ('0' + nibble) : ('A' + nibble - 10);
                value >>= 4;
            }
            for (index_t i = len - 1; i >= 0; --i)
            {
                tmp[i] = '0';
            }
            buffer.append(CStr(tmp, num_nibbles));
        }

        /// \brief Render a single value of any \ref FormattableValue type.
        ///
        /// Compile-time dispatch over the supported categories: char arrays
        /// become strings, other arrays render as `{a, b, ...}`, built-ins go
        /// to the numeric/bool/hex helpers, and user types call their own
        /// `format()` (into the buffer directly, or wrapped in a \ref Sink).
        template <FormatBuffer Buffer, typename T>
            requires FormattableValue<T, Buffer>
        void formatValue(Buffer &buffer, const T &value)
        {
            using U = remove_const_t<T>;
            if constexpr (is_array_v<U>)
            {
                if constexpr (is_same_v<typename is_array<U>::Valuetype, char>)
                {
                    buffer.append(CStr::fromPtr(value));
                }
                else
                {
                    buffer.append('{');
                    for (usize i = 0; i < is_array<T>::size; ++i)
                    {
                        formatValue(buffer, value[i]);
                        if (i + 1 < is_array<T>::size)
                            buffer.append(CStr(", "));
                    }
                    buffer.append('}');
                }
            }
            else if constexpr (is_same_v<U, CStr>)
            {
                buffer.append(value);
            }
            else if constexpr (is_same_v<U, const char *>)
            {
                buffer.append(CStr::fromPtr(value));
            }
            else if constexpr (is_same_v<U, char>)
            {
                buffer.append(value);
            }
            else if constexpr (is_bool_v<U>)
            {
                buffer.append(value ? CStr("true") : CStr("false"));
            }
            else if constexpr (is_integer_v<U>)
            {
                if constexpr (is_signed_v<U>)
                {
                    formatInt(buffer, static_cast<int64>(value));
                }
                else
                {
                    formatUInt(buffer, static_cast<uint64>(value));
                }
            }
            else if constexpr (is_float_v<U>)
            {
                formatFloat(buffer, value);
            }
            else if constexpr (Formattable<T, Buffer>)
            {
                value.format(buffer);
            }
            else if constexpr (Formattable<T, Sink>)
            {
                Sink s = makeSink(buffer); // can only format into a Sink -> wrap once
                value.format(s);
            }
            else if constexpr (is_pointer_v<U>)
            {
                formatHex(buffer, reinterpret_cast<usize>(value));
            }
            else
            {
                static_assert(!is_same_v<U, U>, "type does not implement format");
            }
        }

        /// \brief Terminal overload: append the remaining literal text.
        template <FormatBuffer Buffer>
        inline void format(Buffer &buffer, const CStr str)
        {
            buffer.append(str);
        }

        /// \brief Substitute the next `{}` in `str` with `value`, then recurse.
        ///
        /// Appends the text before the first `{}`, formats one argument in its
        /// place, and continues with the rest of the string and arguments.
        ///
        /// \warning Consumes exactly one `{}` per argument. If the format string
        ///          runs out of `{}` while arguments remain, this returns
        ///          without appending the trailing literal or the leftover
        ///          arguments. Correct argument count is entirely the caller's
        ///          responsibility; the call site must verify arity first
        ///          (see \ref checkFormattable).
        template <FormatBuffer Buffer, typename T, typename... Args>
        void format(Buffer &buffer, const CStr str, const T &value, const Args &...args)
        {
            for (index_t i = 0; i < str.len; ++i)
            {
                if (str.ptr[i] == '{' && i + 1 < str.len && str.ptr[i + 1] == '}')
                {
                    buffer.append(CStr(str.ptr, i)); // append the part before the placeholder
                    formatValue(buffer, value);

                    format(buffer, CStr(str.ptr + i + 2, str.len - i - 2), args...);

                    return;
                }
            }
        }

        /// \brief Compile-time check that every argument type is formattable.
        template <typename... Args>
        consteval bool checkFormattable()
        {
            return (FormattableValue<remove_cv_t<remove_ref_t<Args>>, FormatBufferType> && ...);
        }

        /// \brief Compile-time argument count, used by the format macro for sizing/optimization.
        /// \note Not a correctness check; it does not validate count against placeholders.
        template <typename... Args>
        consteval index_t ArgsCount()
        {
            return (sizeof...(Args));
        }

    } // namespace detail

} // namespace core