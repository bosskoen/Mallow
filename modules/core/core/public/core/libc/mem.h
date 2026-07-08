#pragma once

#include "../memory/galloc.h"


namespace core
{
    //no initiolisasion
    extern struct PlatformInfo {
        usize page_size;
        usize page_mask;
        usize page_shift;
        usize alloc_granularity;
        usize gran_mask;
        usize gran_shift;
    } PLATFORM_INFO;


    void* mlwMemcpy(void* d, const void* s, usize n);
    void* mlwMemset(void* d, int v, usize n);
    void* mlwMemmove(void* d, const void* s, usize n);
    int   mlwMemcmp(const void* a, const void* b, usize n);

    MLW_FORCE_INLINE void *mlwAlignedAlloc(usize size, usize alignment)
    {
        return mlw_g_alloc.alignAlloc(size, alignment);
    }

    MLW_FORCE_INLINE void *mlwMalloc(usize size)
    {
        return mlw_g_alloc.alloc(size);
    }
    MLW_FORCE_INLINE void mlwFree(void *ptr)
    {
        mlw_g_alloc.free(ptr);
    }
    MLW_FORCE_INLINE void *mlwRealloc(void *ptr, usize newSize)
    {
        return mlw_g_alloc.realloc(ptr, newSize);
    }
}

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
