#pragma once
#include "typedef.h"




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

namespace core
{

#if defined(MLW_WINDOWS)

    namespace detail
    {
        extern const struct PlatformMemoryInfo
        {
            usize page_size;
            usize allocation_granularity;

            static PlatformMemoryInfo query();
        } PLATFORM_MEMORY_INFO;
    }

    extern const usize PAGE_SIZE;
    extern const usize ALLOC_GRANULARITY;

    extern const usize GRAN_MASK;
    extern const usize GRAN_SHIFT;

#elif defined(MLW_MAC)
    extern const usize PAGE_SIZE;
#elif defined(MLW_LINUX)
    extern const usize PAGE_SIZE;
#else
#error "unknown platform - add page size and granularity definitions"
#endif

    extern const usize PAGE_MASK;
    extern const usize PAGE_SHIFT;


}