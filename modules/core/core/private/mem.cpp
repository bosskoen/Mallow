#include "libc/mem.h"
#if defined(MLW_WINDOWS)
#include <windows.h>
#elif defined(MLW_LINUX) || defined(MLW_MAC)
#include <unistd.h>
#else
#error "Unsupported platform"
#endif

#include "libc/math.h"

namespace {
    struct PlatformInfo {
        usize page_size;
        usize alloc_granularity;
    };

    PlatformInfo queryPlatform() {
#if defined(MLW_WINDOWS)
        SYSTEM_INFO si;
        GetSystemInfo(&si);
        return { (usize)si.dwPageSize, (usize)si.dwAllocationGranularity };
#elif defined(MLW_LINUX) || defined(MLW_MAC)
        usize page = (usize)sysconf(_SC_PAGESIZE);
        return { page, page };  // no separate granularity concept
#else
#error "Unsupported platform"
#endif
    }

    const PlatformInfo INFO = queryPlatform();
}

const usize core::PAGE_SIZE         = INFO.page_size;
const usize core::PAGE_MASK         = INFO.page_size - 1;
const usize core::PAGE_SHIFT        = MLW_CTZ(INFO.page_size);
const usize core::ALLOC_GRANULARITY = INFO.alloc_granularity;
const usize core::GRAN_MASK         = INFO.alloc_granularity - 1;
const usize core::GRAN_SHIFT        = MLW_CTZ(INFO.alloc_granularity);
