#pragma once
#include "io/format.h"
#include "libc/process.h"
#include "io/terminal.h"
#include "io/io.h"

#define _MLW_CHECK_ARGS(...) \
    __VA_OPT__(core::remove_ref_t<decltype(__VA_ARGS__)>)

#define mlw_write(buffer, format_str, ...)                                                       \
    do                                                                                           \
    {                                                                                            \
        static_assert(core::detail::checkFormattable<_MLW_CHECK_ARGS(__VA_ARGS__)>(),            \
                      "write has an unformattable argument");                                    \
        static_assert(core::FormatBuffer<decltype(buffer)>, "write has an incompatible buffer"); \
        core::detail::format(buffer, core::CStr(format_str), ##__VA_ARGS__);                     \
    } while (0)

#define _MLW_WRITE_IMPL(handle, newline, format_str, ...)                                                     \
    do                                                                                                        \
    {                                                                                                         \
        if constexpr ([](auto &&...args) consteval { return sizeof...(args); }(__VA_ARGS__) == 0 && !newline) \
        {                                                                                                     \
            io::writeHandle(handle, core::CStr(format_str));                                                  \
        }                                                                                                     \
        else                                                                                                  \
        {                                                                                                     \
            core::FormatBufferType &buf = core::detail::getFormatBuffer();                                    \
            buf.len = 0;                                                                                      \
            mlw_write(buf, format_str, ##__VA_ARGS__);                                                        \
            if constexpr (newline)                                                                            \
                buf.append('\n');                                                                             \
            io::writeHandle(handle, core::CStr(buf.ptr, buf.len));                                            \
        }                                                                                                     \
    } while (0)

#define print(format_str, ...) _MLW_WRITE_IMPL(core::terminal::stdoutHandle(), false, format_str, ##__VA_ARGS__)
#define println(format_str, ...) _MLW_WRITE_IMPL(core::terminal::stdoutHandle(), true, format_str, ##__VA_ARGS__)
#define eprint(format_str, ...) _MLW_WRITE_IMPL(core::terminal::stderrHandle(), false, format_str, ##__VA_ARGS__)
#define eprintln(format_str, ...) _MLW_WRITE_IMPL(core::terminal::stderrHandle(), true, format_str, ##__VA_ARGS__)

#if defined(MLW_DEBUG)
#define MLW_DEBUG_PRINT(format_str, ...) eprintln(format_str, ##__VA_ARGS__)
#else
#define MLW_DEBUG_PRINT(format_str, ...)
#endif

// make path were you can call write so no format needs to hapen
#define panic(format_str, ...)                          \
    do                                                  \
    {                                                   \
        eprintln("panic at {}:{}", __FILE__, __LINE__); \
        eprint(format_str, ##__VA_ARGS__);              \
        MLW_DEBUGBREAK();                               \
        core::mlwTerminate(1);                          \
    } while (0)

#define TODO()                                    \
    do                                            \
    {                                             \
        eprint("TODO {}:{}", __FILE__, __LINE__); \
        MLW_DEBUGBREAK();                         \
        core::mlwExit(1);                         \
    } while (0)

#define mlw_assert(cond)                       \
    do                                         \
    {                                          \
        if (!(cond))                           \
        {                                      \
            panic("assertion failed: " #cond); \
        }                                      \
    } while (0)

#define mlw_assert_msg(cond, format_str, ...)                                 \
    do                                                                        \
    {                                                                         \
        if (!(cond))                                                          \
        {                                                                     \
            panic("assertion failed: " #cond "\n" format_str, ##__VA_ARGS__); \
        }                                                                     \
    } while (0)

#ifdef MLW_DEBUG            
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