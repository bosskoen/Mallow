#include "io/io.h"

#if defined(MLW_WINDOWS)
    #define WIN32_LEAN_AND_MEAN
    #define NOMINMAX
    #include <windows.h>
#elif defined(MLW_LINUX) || defined(MLW_MAC)
#include "syscall.h"
#endif

namespace io
{
    void writeHandle(Handle handle, core::CStr str){
#if defined(MLW_WINDOWS)
        DWORD written;
        WriteFile(handle.fd, str.ptr, str.len, &written, nullptr);
#else
        ::write(handle.fd, str.ptr, str.len);
#endif
    }
} // namespace core::io
