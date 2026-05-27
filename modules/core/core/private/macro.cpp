#include "macro.h"
#if defined(_WIN32)
#include <windows.h>
#else
#include <sys/mman.h>
#endif

namespace core::detail {
    FormatBufferType& getFormatBuffer() {
        thread_local FormatBufferType buf = []() {
            FormatBufferType b;
            #if defined(_WIN32)
                b.ptr = (char*)VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
            #else
                b.ptr = (char*)mmap(nullptr, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
            #endif
            b.len = 0;
            b.capacity = 4096;
            return b;
        }();
        return buf;
    }
}