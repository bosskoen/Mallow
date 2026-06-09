#include "memory/memory.h"
#if defined(MLW_WINDOWS)
#include <windows.h>
#elif defined(MLW_LINUX) || defined(MLW_MAC)
#include <unistd.h>
#else
#error "Unsupported platform"
#endif

#include "compilers.h"

#if defined(MLW_WINDOWS)
core::detail::PlatformMemoryInfo core::detail::PlatformMemoryInfo::query()
{
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    return {
        static_cast<usize>(si.dwPageSize),
        static_cast<usize>(si.dwAllocationGranularity)};
}
const core::detail::PlatformMemoryInfo core::detail::PLATFORM_MEMORY_INFO = core::detail::PlatformMemoryInfo::query();

const usize core::PAGE_SIZE = core::detail::PLATFORM_MEMORY_INFO.page_size;
const usize core::ALLOC_GRANULARITY = core::detail::PLATFORM_MEMORY_INFO.allocation_granularity;
    
const usize core::GRAN_MASK = core::ALLOC_GRANULARITY - 1;
const usize core::GRAN_SHIFT = MLW_CTZ(core::ALLOC_GRANULARITY);

#elif defined(MLW_LINUX)
const usize core::PAGE_SIZE = sysconf(_SC_PAGESIZE);
#elif defined(MLW_MAC)
const usize core::PAGE_SIZE = sysconf(_SC_PAGESIZE);
#else
#error "Unsupported platform"
#endif
const usize core::PAGE_MASK = core::PAGE_SIZE - 1;
const usize core::PAGE_SHIFT = MLW_CTZ(core::PAGE_SIZE);