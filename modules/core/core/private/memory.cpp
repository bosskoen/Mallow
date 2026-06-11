#include "memory/memory.h"
#include "macro.h"

#if defined(MLW_WINDOWS)
#include <windows.h>
#elif defined(MLW_LINUX) || defined(MLW_MAC)
#include <sys/mman.h>
#endif

bool core::StackAllocator::init(usize size)
{
    if (base){
        MLW_DEBUG_PRINT("StackAllocator already initiolised");
        return false;
    }

    size = (size + PAGE_MASK) & ~PAGE_MASK; // round up to page size
#if defined(MLW_WINDOWS)
    base = ::VirtualAlloc(nullptr, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
#elif defined(MLW_LINUX) || defined(MLW_MAC)
    base = ::mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
#endif
    if (base){
        capacity = size;
        offset = 0;
        return true;
    }
    
    return false;
}
void core::StackAllocator::shutdown()
{
    if(base){
#if defined(MLW_WINDOWS)
        ::VirtualFree(base, 0, MEM_RELEASE);
#elif defined(MLW_LINUX) || defined(MLW_MAC)
        ::munmap(base, capacity);
#endif
    }
    base = nullptr;
    capacity = 0;
    offset = 0;
}

void *core::StackAllocator::alloc(usize size, usize alignment)
{
    if(!base){
        MLW_DEBUG_PRINT("trying to alloc to a uninitiolised StackAllocator");
        return nullptr;
    }

    usize new_offset = (offset + alignment - 1) & ~(alignment -1);

    if (new_offset + size > capacity){
        MLW_DEBUG_PRINT("StackAllocator full");
         return nullptr;
        }

    void* ptr = reinterpret_cast<u8*>(base) + new_offset;
    offset = new_offset + size;
    return ptr;
}

void *core::StackAllocator::alloc(usize size)
{
    usize alignment = size >= sizeof(alignment_t) ? alignof(alignment_t) : 
                        size <=1 ? 1 : 1ull << (64 - MLW_CLZ(size - 1));
    return alloc(size, alignment);
}
