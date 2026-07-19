#pragma once
#include "io/format.h"
#include "libc/process.h"
#include "io/terminal.h"
#include "io/io.h"

/// \file
/// \brief Lightweight printing, panic and assert macros used across the freestanding core.
///
/// This header documents the public-facing macros only. Implementation helpers
/// (names starting with `_MLW_`) are internal and intentionally left alone.

#define _MLW_CHECK_ARGS(...) \
    __VA_OPT__(core::remove_ref_t<decltype(__VA_ARGS__)>)

/// \brief Format into an existing buffer without writing to stdout/stderr.
///
/// Use `mlw_write(buffer, "fmt {}", arg)` to render formatted text into a
/// `core::FormatBufferType` or compatible buffer. Performs compile-time checks
/// that the arguments are formattable and that `buffer` meets the `FormatBuffer`
/// concept.
///
/// \code
/// core::FormatBufferType buf = core::detail::getFormatBuffer();
/// mlw_write(buf, "Hello {}", name);
/// \endcode
///
/// \note You may pass any number of arguments; each `{}` placeholder in the
///       format string is replaced in order by the corresponding argument.
///       Placeholders may appear anywhere in the string, for example:
/// \code
/// mlw_write(buf, "a={} b={} c={}", a, b, c);
/// mlw_write(buf, "User {} connected from {} at {}", user, host, time);
/// \endcode
#define mlw_write(buffer, format_str, ...)                                                       \
    do                                                                                           \
    {                                                                                            \
        static_assert(core::detail::checkFormattable<_MLW_CHECK_ARGS(__VA_ARGS__)>(),            \
                      "write has an unformattable argument");                                    \
        static_assert(core::FormatBuffer<decltype(buffer)>, "write has an incompatible buffer"); \
        core::detail::format(buffer, core::CStr(format_str), ##__VA_ARGS__);                     \
    } while (0)

// Internal helper used by the printing macros; not part of the public API.
#define _MLW_WRITE_IMPL(handle, newline, format_str, ...)                                                     \
    do                                                                                                        \
    {                                                                                                         \
        if constexpr ([](auto &&...args) consteval { return sizeof...(args); }(__VA_ARGS__) == 0 && !newline) \
        {                                                                                                     \
            io::writeHandle(handle, core::CStr(format_str));                                                  \
        }                                                                                                     \
        else                                                                                                  \
        {                                                                                                     \
            core::FormatBufferType &__format_write_buf = core::detail::getFormatBuffer();                                    \
            __format_write_buf.len = 0;                                                                                      \
            mlw_write(__format_write_buf, format_str, ##__VA_ARGS__);                                                        \
            if constexpr (newline)                                                                            \
                __format_write_buf.append('\n');                                                                             \
            io::writeHandle(handle, core::CStr(__format_write_buf.ptr, __format_write_buf.len));                                            \
        }                                                                                                     \
    } while (0)

/// \brief Print formatted text to the terminal's stdout without a newline.
///
/// \code
/// print("count={}", n);
/// print("x={} y={} z={}", x, y, z);
/// println("User {} logged in from {}", user, ip);
/// \endcode
#define print(format_str, ...) _MLW_WRITE_IMPL(core::terminal::stdoutHandle(), false, format_str, ##__VA_ARGS__)

/// \brief Print formatted text to stdout and append a newline.
///
/// \see print() for examples showing multiple placeholders and arguments.
#define println(format_str, ...) _MLW_WRITE_IMPL(core::terminal::stdoutHandle(), true, format_str, ##__VA_ARGS__)

/// \brief Print formatted text to stderr without a newline.
///
/// Works like \see print() but writes to stderr. Examples:
/// \code
/// eprint("error: {}", msg);
/// eprintln("failed: {} {}", code, reason);
/// \endcode
#define eprint(format_str, ...) _MLW_WRITE_IMPL(core::terminal::stderrHandle(), false, format_str, ##__VA_ARGS__)

/// \brief Print formatted text to stderr and append a newline.
///
/// \see eprint() and `print()` for placeholder rules and examples.
#define eprintln(format_str, ...) _MLW_WRITE_IMPL(core::terminal::stderrHandle(), true, format_str, ##__VA_ARGS__)

/// \brief Debug-only printing to stderr; compiled out in release builds.
#if defined(MLW_DEBUG)
#define MLW_DEBUG_PRINT(format_str, ...) eprintln(format_str, ##__VA_ARGS__)
#else
#define MLW_DEBUG_PRINT(format_str, ...)
#endif

#define MLW_STRINGIFY2(x) #x
#define MLW_STRINGIFY(x) MLW_STRINGIFY2(x)

/// \brief Panic without allocating memory. Prints file/line and `msg`, then terminates.
///
/// Use when handling low-level fatal errors where heap allocation is not allowed.
/// Example: `panic_mem("out of memory");`
#define panic_mem(msg)                                      \
    do                                                  \
    {                                                   \
        eprint("panic at " __FILE__ ":"                 \
               MLW_STRINGIFY(__LINE__) "\n");           \
        eprint(msg "\n");                               \
        MLW_DEBUGBREAK();                               \
        core::mlwTerminate(1);                          \
    } while (0)

/// \brief Panic with formatted message and terminate.
///
/// Example: `panic("unexpected state: {}", id);` prints the location then the message.
#define panic(format_str, ...)                          \
    do                                                  \
    {                                                   \
        eprintln("panic at {}:{}", __FILE__, __LINE__); \
        eprint(format_str, ##__VA_ARGS__);              \
        MLW_DEBUGBREAK();                               \
        core::mlwTerminate(1);                          \
    } while (0)

/// \brief Emit a TODO marker and abort. Intended as a development aid.
#define TODO()                                    \
    do                                            \
    {                                             \
        eprint("TODO {}:{}", __FILE__, __LINE__); \
        MLW_DEBUGBREAK();                         \
        core::mlwExit(1);                         \
    } while (0)

/// \brief Abort if `cond` is false, using `panic` to report the failure.
#define mlw_assert(cond)                       \
    do                                         \
    {                                          \
        if (!(cond))                           \
        {                                      \
            panic("assertion failed: " #cond); \
        }                                      \
    } while (0)

/// \brief Abort with a formatted message if `cond` is false.
///
/// Example: `mlw_assert_msg(x > 0, "x is {}", x);`
#define mlw_assert_msg(cond, format_str, ...)                                 \
    do                                                                        \
    {                                                                         \
        if (!(cond))                                                          \
        {                                                                     \
            panic("assertion failed: " #cond "\n" format_str, ##__VA_ARGS__); \
        }                                                                     \
    } while (0)

#ifdef MLW_DEBUG            
/// \brief Debug-only assert with message; no-op in release builds.
#define mlw_debug_assert_msg(cond, format_str, ...)                           \
    do                                                                        \
    {                                                                         \
        if (!(cond))                                                          \
        {                                                                     \
            panic("assertion failed: " #cond "\n" format_str, ##__VA_ARGS__); \
        }                                                                     \
    } while (0)
#else
#define mlw_debug_assert_msg(cond, format_str, ...)
#endif // MLW_DEBUG

// TODO
//      document how to use
//      make more test
//      make collors
//      make error {} != args...
//      make format options
//      make vscode snippets for formatt fucntion
//      make format macro as soon as alocator avalebel
//      if peaple what to make there own formatters does the macro function need to be declared becous of formateble concept? maybe not if the formateble concept only requires a format function with the right signature but is not required to be a member function. maby just a free function in the same namespace as the type that needs to be formated. then the formatValue function can find it with ADL. this way user code can implement formating for there own types without needing to modify core code or include core headers.