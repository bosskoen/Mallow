#pragma once
#include "io/format.h"
#include "libc/process.h"
#include "io/terminal.h"
#include "io/io.h"


#define println(format_str, ...)                                                                             \
    do                                                                                                       \
    {                                                                                                        \
        static_assert(                                                                                       \
            [](auto &&...args) { return core::detail::checkFormattable<decltype(args)...>(); }(__VA_ARGS__), \
            "println has an unformattable argument");                                                        \
        core::FormatBufferType &buf = core::detail::getFormatBuffer();                                       \
        buf.len = 0;                                                                                         \
        core::detail::format(buf, core::CStr(format_str), ##__VA_ARGS__);                                    \
        buf.append('\n');                                                                                    \
        core::io::write(core::terminal::stdoutHandle(), core::CStr(buf.ptr, buf.len));                       \
    } while (0)

#define print(format_str, ...)                                                                                   \
    do                                                                                                           \
    {                                                                                                            \
        if constexpr ([](auto &&...args) { return sizeof...(args) == 0; }(__VA_ARGS__))                          \
        {                                                                                                        \
            core::io::write(core::terminal::stdoutHandle(), core::CStr(format_str));                             \
        }                                                                                                        \
        else                                                                                                     \
        {                                                                                                        \
            static_assert(                                                                                       \
                [](auto &&...args) { return core::detail::checkFormattable<decltype(args)...>(); }(__VA_ARGS__), \
                "print has an unformattable argument");                                                          \
            core::FormatBufferType &buf = core::detail::getFormatBuffer();                                       \
            buf.len = 0;                                                                                         \
            core::detail::format(buf, core::CStr(format_str), ##__VA_ARGS__);                                    \
            core::io::write(core::terminal::stdoutHandle(), core::CStr(buf.ptr, buf.len));                       \
        }                                                                                                        \
    } while (0)

#define eprintln(format_str, ...)                                                                            \
    do                                                                                                       \
    {                                                                                                        \
        static_assert(                                                                                       \
            [](auto &&...args) { return core::detail::checkFormattable<decltype(args)...>(); }(__VA_ARGS__), \
            "eprintln has an unformattable argument");                                                       \
        core::FormatBufferType &buf = core::detail::ge                                                       \
            core::FormatBufferType &buf = core::detail::getFormatBuffer();                                   \
        buf.len = 0;                                                                                         \
        core::detail::format(buf, core::CStr(format_str), ##__VA_ARGS__);                                    \
        buf.append('\n');                                                                                    \
        core::io::write(core::terminal::stderrHandle(), core::CStr(buf.ptr, buf.len));                       \
    } while (0)

#define eprint(format_str, ...)                                                                                  \
    do                                                                                                           \
    {                                                                                                            \
        if constexpr ([](auto &&...args) { return sizeof...(args) == 0; }(__VA_ARGS__))                          \
        {                                                                                                        \
            core::io::write(core::terminal::stderrHandle(), core::CStr(format_str));                             \
        }                                                                                                        \
        else                                                                                                     \
        {                                                                                                        \
            static_assert(                                                                                       \
                [](auto &&...args) { return core::detail::checkFormattable<decltype(args)...>(); }(__VA_ARGS__), \
                "eprint has an unformattable argument");                                                         \
            core::FormatBufferType &buf = core::detail::ge                                                       \
                core::FormatBufferType &buf = core::detail::getFormatBuffer();                                   \
            buf.len = 0;                                                                                         \
            core::detail::format(buf, core::CStr(format_str), ##__VA_ARGS__);                                    \
            core::io::write(core::terminal::stderrHandle(), core::CStr(buf.ptr, buf.len));                       \
        }                                                                                                        \
    } while (0)

#define panic(format_str, ...)                           \
    do                                                   \
    {                                                    \
        eprintln("panic at {}::{}", __FILE__, __LINE__); \
        eprint(format_str, ##__VA_ARGS__);               \
        core::terminate(1);                      \
    } while (0)

#define TODO()                                    \
    do                                            \
    {                                             \
        print("TODO {}::{}", __FILE__, __LINE__); \
        core::exit(1);                        \
    } while (0)

// TODO 
//      document how to use
//      pritify
//      make more test
//      make collors
//      make error {} != args...
//      float auto presision
//      make format options
//      make vscode snippets for formatt fucntion
//      make format macro as soon as alocator avalebel
//      if peaple what to make there own formatters does the macro function need to be declared becous of formateble concept? maybe not if the formateble concept only requires a format function with the right signature but is not required to be a member function. maby just a free function in the same namespace as the type that needs to be formated. then the formatValue function can find it with ADL. this way user code can implement formating for there own types without needing to modify core code or include core headers.