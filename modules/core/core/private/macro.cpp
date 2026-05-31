#include "macro.h"

#if defined(MLW_WINDOWS)
#include <windows.h>
#elif defined(MLW_LINUX) || defined(MLW_MAC)
#include <sys/mman.h>
#include <unistd.h>
#else
#error "Unsupported platform"
#endif

// Sources:
//   Intel x86/x86_64 page size : Intel SDM vol.3a ch.5 table 5.1
//                                 https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html
//   Windows ARM64 page size    : implied by Windows ARM64 ABI conventions, not explicitly stated
//                                 https://learn.microsoft.com/en-us/cpp/build/arm64-windows-abi-conventions
//   Windows granularity        : no official documentation, assumed 65536
//   Apple ARM page size        : no official documentation, use runtime lookup
//   AMD x86/x86_64 page size   : no official documentation found
//   Linux ARM page size        : runtime only, kernel configurable
//                                 https://github.com/torvalds/linux/blob/master/arch/arm64/Kconfig
//
// Runtime lookup:
//   Windows : SYSTEM_INFO i; GetSystemInfo(&i); i.dwPageSize; i.dwAllocationGranularity;
//   Unix    : sysconf(_SC_PAGESIZE);

#if defined(MLW_WINDOWS)
static constexpr usize PAGE_SIZE = 4096;
static constexpr usize ALLOC_GRANULARITY = 65536;

#elif defined(MLW_MAC) && (defined(MLW_ARM64) || defined(MLW_ARM32))
static constexpr usize PAGE_SIZE = 16384; // TODO verify, use runtime lookup

#elif defined(MLW_MAC) && (defined(MLW_X64) || defined(MLW_X86))
static constexpr usize PAGE_SIZE = 4096;

#elif defined(MLW_LINUX) && (defined(MLW_ARM64) || defined(MLW_ARM32))
static const usize PAGE_SIZE = sysconf(_SC_PAGESIZE);

#elif defined(MLW_LINUX) && (defined(MLW_X64) || defined(MLW_X86))
static constexpr usize PAGE_SIZE = 4096;

#else
#error "unknown platform - add page size and granularity definitions"
#endif
namespace core::detail
{
    [[noreturn]]
    void terminate(int exit_code)
    {
        #if defined(MLW_WINDOWS)
        ExitProcess(exit_code);
        #elif defined(MLW_LINUX) || defined(MLW_MAC)
        _exit(exit_code)
        #endif
    }

    FormatBufferType &getFormatBuffer()
    {
        thread_local FormatBufferType buf = []()
        {
            FormatBufferType b;
#if defined(MLW_WINDOWS)

            b.ptr = static_cast<char *>(VirtualAlloc(nullptr, ALLOC_GRANULARITY, MEM_RESERVE, PAGE_READWRITE));
            VirtualAlloc(b.ptr, PAGE_SIZE, MEM_COMMIT, PAGE_READWRITE);
#else
            b.ptr = (char *)mmap(nullptr, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
#endif
            b.len = 0;
            b.capacity = PAGE_SIZE;
            return b;
        }();
        return buf;
    }
}

    void core::FormatBufferType::realocate()
    {
#if defined(MLW_WINDOWS)
        if (capacity % ALLOC_GRANULARITY == 0)
        {
            // need to reserve more memory
            // see if can extand block
            char *new_ptr = static_cast<char *>(VirtualAlloc(nullptr, ((capacity / ALLOC_GRANULARITY) + 1) * ALLOC_GRANULARITY, MEM_RESERVE, PAGE_READWRITE));
            VirtualAlloc(new_ptr, capacity + PAGE_SIZE, MEM_COMMIT, PAGE_READWRITE);
            // memcpy

            u64 *src = reinterpret_cast<u64 *>(ptr);
            u64 *dst = reinterpret_cast<u64 *>(new_ptr);
            index_t num_qwords = len / 8; // number of 8-byte chunks
            for (index_t i = 0; i < num_qwords; ++i)
            {
                dst[i] = src[i];
            }

            VirtualFree(ptr, 0, MEM_RELEASE);
            ptr = new_ptr;
            capacity += PAGE_SIZE;
        }
        else
        {
            VirtualAlloc(ptr + capacity, PAGE_SIZE, MEM_COMMIT, PAGE_READWRITE);
            capacity += PAGE_SIZE;
        }
#else
        ptr = static_cast<char *>(mremap(ptr, capacity, capacity + PAGE_SIZE, MREMAP_MAYMOVE));
        capacity += PAGE_SIZE;
#endif
    }
