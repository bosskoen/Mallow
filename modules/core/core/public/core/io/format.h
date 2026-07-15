#pragma once
#include "../c_string.h"
#include "../libc/math.h"

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

    struct Sink
    {
        void *ctx;
        void (*append_str)(void *, const CStr &);
        void (*append_chr)(void *, char);

        inline void append(const CStr &str) { append_str(ctx, str); }
        inline void append(char c) { append_chr(ctx, c); }
    };

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

    // remove the buffer and just use formateble???
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

    struct FormatBufferType
    {
    private:
        void realocate();

    public:
        char *ptr;
        index_t len;
        index_t capacity;

        void append(const CStr &str);

        void append(const uint8 value);
    };

    namespace detail
    {
        //void mlw__crt_distorty_format_buffer();
        FormatBufferType &getFormatBuffer();

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
            return (FormattableValue<remove_cv_t<remove_ref_t<Args>>, FormatBufferType> && ...);
        }

        template <typename... Args>
        consteval index_t ArgsCount()
        {
            return (sizeof...(Args));
        }

    } // namespace detail

} // namespace core