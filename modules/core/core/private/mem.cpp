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

namespace core
{
    struct AlignedHeader {
        void* raw;
        usize size;
        u16 alignment;
    };
void *mlwAlignedAlloc(usize size, u16 alignment)
{
    usize total_size = size + sizeof(AlignedHeader) + alignment - 1;
    void* raw = mlwMalloc(total_size);
    if (!raw) return nullptr;

    usize start = reinterpret_cast<usize>(raw) + sizeof(AlignedHeader);
    usize aligned_start = (start + alignment - 1) & ~(static_cast<usize>(alignment) - 1);
    void* user_ptr = reinterpret_cast<void*>(aligned_start);

    AlignedHeader* header = reinterpret_cast<AlignedHeader*>(aligned_start) -1;
    header->raw = raw;
    header->alignment = alignment;
    header->size = size;

    return user_ptr;
}

void mlwAlignedFree(void *ptr)
{
    if (!ptr) return;
    AlignedHeader* header = reinterpret_cast<AlignedHeader*>(ptr) - 1;
    mlwFree(header->raw);
}

void *mlwAlignedRealloc(void *ptr, usize newSize)
{
    AlignedHeader* hdr = reinterpret_cast<AlignedHeader*>(ptr) - 1;
    usize old_size      = hdr->size;

    void* newPtr = mlwAlignedAlloc(newSize, hdr->alignment);
    if (!newPtr) return nullptr;
    mlwMemcpy(newPtr, ptr, mlwMin(old_size, newSize));
    mlwAlignedFree(ptr);
    return newPtr;
}
}