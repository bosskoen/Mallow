#pragma once
#include "c_string.h"
#include "io.h"
#include "typedef.h"
#include "traits.h"
#include "terminal.h"
#include "compilers.h"
#include "core/memory.h"

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

    // remove the buffer and just use formateble???
    template <typename T, typename Buffer>
    concept FormattableValue =
        is_array_v<T> ||
        is_same_v<T, CStr> ||
        is_same_v<T, const char *> ||
        is_same_v<T, char> ||
        is_bool_v<T> ||
        is_integer_v<T> ||
        is_float_v<T> ||
        is_pointer_v<T> ||
        Formattable<T, Buffer>;

    struct FormatBufferType
    {
    private:
        void realocate();

    public:
        char *ptr;
        index_t len;
        index_t capacity;

        inline void append(const CStr &str)
        {
            while (len + str.len > capacity)
            {
                realocate();
            }
            // copy str to buffer
            mlwMemcpy(ptr + len, str.ptr, str.len);
            len += str.len;
        }
        inline void append(const u8 value)
        {
            while (len + 1 > capacity)
            {
                realocate();
            }
            ptr[len] = value;
            len += 1;
        }
    };

    namespace detail
    {
        [[noreturn]] void terminate(int exit_code);

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

            for (index_t i = 0; i < len / 2; ++i)
            {
                char t = tmp[i];
                tmp[i] = tmp[len - 1 - i];
                tmp[len - 1 - i] = t;
            }

            buffer.append(CStr(tmp, len));
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

            char tmp[32];
            index_t len = 0;

            if (value < 0)
            {
                tmp[len++] = '-';
                value = -value;
            }

            u64 integer_part = static_cast<u64>(value);
            f64 frac = value - static_cast<f64>(integer_part);

            if (integer_part == 0)
            {
                tmp[len++] = '0';
            }
            else
            {
                index_t start = len;
                while (integer_part > 0)
                {
                    tmp[len++] = '0' + (integer_part % 10);
                    integer_part /= 10;
                }
                // reverse just the integer part
                for (index_t i = start; i < start + (len - start) / 2; ++i)
                {
                    char t = tmp[i];
                    tmp[i] = tmp[len - 1 - (i - start)];
                    tmp[len - 1 - (i - start)] = t;
                }
            }
            tmp[len++] = '.';

            for (index_t i = 0; i < precision; i++)
            {
                frac *= 10;
                tmp[len++] = '0' + (u8)frac;
                frac -= (u8)frac;
            }

            buffer.append(CStr(tmp, len));
        }

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
                u8 nibble = value & 0xF;
                tmp[--len] = (nibble < 10) ? ('0' + nibble) : ('A' + nibble - 10);
                value >>= 4;
            }
            for (index_t i = len - 1; i >= 0; --i)
            {
                tmp[i] = '0';
            }
            buffer.append(CStr(tmp, num_nibbles));
        }

        template <FormatBuffer Buffer, typename T>
            requires FormattableValue<T, Buffer>
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
                    buffer.append('{');
                    for (size_t i = 0; i < is_array<T>::size; ++i)
                    {
                        formatValue(buffer, value[i]);
                        if (i + 1 < is_array<T>::size)
                            buffer.append(CStr(", "));
                    }
                    buffer.append('}');
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
            else if constexpr (is_pointer_v<T>)
            {
                formatHex(buffer, reinterpret_cast<usize>(value));
            }
            else
            {
                static_assert(!is_same_v<T, T>, "type does not implement format");
            }
        }
        template <FormatBuffer Buffer>
        inline void format(Buffer &buffer, const CStr str)
        {
            buffer.append(str);
        }
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

        template <typename... Args>
        consteval bool checkFormattable()
        {
            return (FormattableValue<remove_ref_t<Args>, FormatBufferType> && ...);
        }

        template <typename... Args>
        consteval index_t ArgsCount()
        {
            return (sizeof...(Args));
        }

    } // namespace detail

} // namespace core

#define println(format_str, ...)                                                       \
    do                                                                                 \
    {                                                                                  \
        constexpr bool _check = [](auto... args) consteval                             \
        {                                                                              \
            return core::detail::checkFormattable<decltype(args)...>();                \
        }(__VA_ARGS__);                                                                \
        static_assert(_check,                                                          \
                      "println has an unformattable argument");                        \
        core::FormatBufferType &buf = core::detail::getFormatBuffer();                 \
        buf.len = 0;                                                                   \
        core::detail::format(buf, core::CStr(format_str), ##__VA_ARGS__);              \
        buf.append('\n');                                                              \
        core::io::write(core::terminal::stdoutHandle(), core::CStr(buf.ptr, buf.len)); \
    } while (0)

#define print(format_str, ...)                                                             \
    do                                                                                     \
    {                                                                                      \
        constexpr index_t _argc = [](auto... args) consteval                               \
        {                                                                                  \
            return static_cast<index_t>(sizeof...(args));                                  \
        }(__VA_ARGS__);                                                                    \
        if constexpr (_argc == 0)                                                          \
        {                                                                                  \
            core::io::write(core::terminal::stdoutHandle(), core::CStr(format_str));       \
        }                                                                                  \
        else                                                                               \
        {                                                                                  \
            constexpr bool _check = [](auto... args) consteval                             \
            {                                                                              \
                return core::detail::checkFormattable<decltype(args)...>();                \
            }(__VA_ARGS__);                                                                \
            static_assert(_check, "print has an unformattable argument");                  \
            core::FormatBufferType &buf = core::detail::getFormatBuffer();                 \
            buf.len = 0;                                                                   \
            core::detail::format(buf, core::CStr(format_str), ##__VA_ARGS__);              \
            core::io::write(core::terminal::stdoutHandle(), core::CStr(buf.ptr, buf.len)); \
        }                                                                                  \
    } while (0)

#define eprintln(format_str, ...)                                                      \
    do                                                                                 \
    {                                                                                  \
        constexpr bool _check = [](auto... args) consteval                             \
        {                                                                              \
            return core::detail::checkFormattable<decltype(args)...>();                \
        }(__VA_ARGS__);                                                                \
        static_assert(_check,                                                          \
                      "println has an unformattable argument");                        \
        core::FormatBufferType &buf = core::detail::getFormatBuffer();                 \
        buf.len = 0;                                                                   \
        core::detail::format(buf, core::CStr(format_str), ##__VA_ARGS__);              \
        buf.append('\n');                                                              \
        core::io::write(core::terminal::stderrHandle(), core::CStr(buf.ptr, buf.len)); \
    } while (0)

#define eprint(format_str, ...)                                                            \
    do                                                                                     \
    {                                                                                      \
        constexpr index_t _argc = [](auto... args) consteval                               \
        {                                                                                  \
            return static_cast<index_t>(sizeof...(args));                                  \
        }(__VA_ARGS__);                                                                    \
        if constexpr (_argc == 0)                                                          \
        {                                                                                  \
            core::io::write(core::terminal::stderrHandle(), core::CStr(format_str));       \
        }                                                                                  \
        else                                                                               \
        {                                                                                  \
            constexpr bool _check = [](auto... args) consteval                             \
            {                                                                              \
                return core::detail::checkFormattable<decltype(args)...>();                \
            }(__VA_ARGS__);                                                                \
            static_assert(_check, "print has an unformattable argument");                  \
            core::FormatBufferType &buf = core::detail::getFormatBuffer();                 \
            buf.len = 0;                                                                   \
            core::detail::format(buf, core::CStr(format_str), ##__VA_ARGS__);              \
            core::io::write(core::terminal::stderrHandle(), core::CStr(buf.ptr, buf.len)); \
        }                                                                                  \
    } while (0)

#define panic(format_str, ...)                           \
    do                                                   \
    {                                                    \
        eprintln("panic at {}::{}", __FILE__, __LINE__); \
        eprint(format_str, ##__VA_ARGS__);               \
        core::detail::terminate(1);                      \
    } while (0)

#define TODO()                                    \
    do                                            \
    {                                             \
        print("TODO {}::{}", __FILE__, __LINE__); \
        core::detail::terminate(1);               \
    } while (0)

// TODO resizeble alocations
//      document how to use
//      pritify
//      make more test
//      make collors
//      make error {} != args...
//      float auto presision
//      make format options
//      make vscode snippets for formatt fucntion
//      make format macro as soon as alocator avalebel
//      make patic and TODO macros
//      if peaple what to make there own formatters does the macro function need to be declared becous of formateble concept? maybe not if the formateble concept only requires a format function with the right signature but is not required to be a member function. maby just a free function in the same namespace as the type that needs to be formated. then the formatValue function can find it with ADL. this way user code can implement formating for there own types without needing to modify core code or include core headers.