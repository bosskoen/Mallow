#include "io/terminal.h"

#if defined(MLW_WINDOWS)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace core::terminal
{
    io::Handle stdoutHandle()
    {
#if defined(MLW_WINDOWS)
        return GetStdHandle(STD_OUTPUT_HANDLE);
#else
        return {1};
#endif
    }

    io::Handle stderrHandle()
{
    #if defined(MLW_WINDOWS)
        return GetStdHandle(STD_ERROR_HANDLE);
#else
        return {2};
#endif
}

io::Handle stdinHandle()
{
#if defined(MLW_WINDOWS)
        return GetStdHandle(STD_INPUT_HANDLE);
#else
        return {0};
#endif
}
} // namespace core::terminal


