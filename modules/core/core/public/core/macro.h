#pragma once
#include "c_string.h"
#include "io.h"
#include "typedef.h"
#include "traits.h"
#include "terminal.h"

namespace core
{

    template <typename T>
    concept FormatBuffer = requires(T &buffer, const CStr &str, char c) {
        { buffer.append(str) } -> same_as<void>;
        { buffer.append(c) } -> same_as<void>;
    };

    template <typename T, typename Buffer>
    concept Formattable = FormatBuffer<Buffer> && requires(const T &value, Buffer &buffer) {
        { value.format(buffer) } -> same_as<void>;
    };

    struct FormatBufferType
    {
        char *ptr;
        index_t len;
        index_t capacity;
        inline void append(const CStr &str)
        {
            if (len + str.len > capacity)
            {
                // need to reallocate
                // maby double the capacity or so
            }
            // copy str to buffer
            for (index_t i = 0; i < str.len; ++i)
            {
                ptr[len + i] = str.ptr[i];
            }
            len += str.len;
        }
        inline void append(const u8 value)
        {
            if (len + 1 > capacity)
            {
                // need to reallocate
                // maby double the capacity or so
            }
            ptr[len] = value;
            len += 1;
        }
    };

    namespace detail
    {
        FormatBufferType &getFormatBuffer();

        template <FormatBuffer Buffer>
        inline void formatUInt(Buffer &buffer, u64 value)
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

            for (index_t i = len - 1; i >= 0; i--)
            {
                buffer.append(tmp[i]);
            }
        }

        template <FormatBuffer Buffer>
        inline void formatInt(Buffer &buffer, i64 value)
        {
            if (value == 0)
            {
                buffer.append('0');
                return;
            }

            bool negative = value < 0;
            if (negative)
                buffer.append('-');
            formatUInt(buffer, negative ? static_cast<u64>(-value) : static_cast<u64>(value));
        }

        template <FormatBuffer Buffer>
        inline void formatFloat(Buffer &buffer, f64 value, index_t precision = 6)
        {
            if (value < 0)
            {
                buffer.append('-');
                value = -value;
            }

            u64 integer_part = static_cast<u64>(value);
            f64 frac = value - static_cast<f64>(integer_part);

            formatUInt(buffer, integer_part);
            buffer.append('.');

            for (index_t i = 0; i < precision; i++)
            {
                frac *= 10;
                buffer.append('0' + (u8)frac);
                frac -= (u8)frac;
            }
        }

        template <FormatBuffer Buffer, typename T>
        void formatValue(Buffer &buffer, const T &value)
        {
            if constexpr (is_array_v<T>)
            {
                if constexpr (is_same_v<typename is_array<T>::Valuetype, char>)
                {
                    buffer.append(CStr::fromPtr(value));
                }
                else
                {
                    static_assert(!is_same_v<T, T>, "array type is not formattable");
                }
            }
            else if constexpr (is_same_v<T, CStr>)
            {
                buffer.append(value);
            }
            else if constexpr (is_same_v<T, const char *>)
            {
                buffer.append(CStr::fromPtr(value));
            }
            else if constexpr (is_same_v<T, char>)
            {
                buffer.append(value);
            }
            else if constexpr (is_bool_v<T>)
            {
                buffer.append(value ? CStr("true") : CStr("false"));
            }
            else if constexpr (is_integer_v<T>)
            {
                if constexpr (is_signed_v<T>)
                {
                    formatInt(buffer, static_cast<i64>(value));
                }
                else
                {
                    formatUInt(buffer, static_cast<u64>(value));
                }
            }
            else if constexpr (is_float_v<T>)
            {
                formatFloat(buffer, value);
            }
            else if constexpr (Formattable<T, Buffer>)
            {
                value.format(buffer);
            }
            else
            {
                static_assert(!is_same_v<T, T>, "type does not implement format");
            }
        }
        template <FormatBuffer Buffer>
        void format(Buffer &buffer, const CStr str)
        {
            buffer.append(str);
        }
        template <FormatBuffer Buffer, typename T, typename... Args>
        void format(Buffer &buffer, const CStr str, const T &value, const Args &...args)
        {
            for (int i = 0; i < str.len; ++i)
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

    } // namespace detail

} // namespace core
#define println(format_str, ...)                                                       \
    do                                                                                 \
    {                                                                                  \
        core::FormatBufferType &buf = core::detail::getFormatBuffer();                 \
        buf.len = 0;                                                                   \
        core::detail::format(buf, core::CStr(format_str), ##__VA_ARGS__);              \
        buf.append('\n');                                                              \
        core::io::write(core::terminal::stdoutHandle(), core::CStr(buf.ptr, buf.len)); \
    } while (0)


//TODO resizeble alocations
//     array formating 
//     make way better error
//     document how to use
//     pritify
//     make more test
//     make collors