#include "io/format.h"

#if defined(MLW_WINDOWS)
#include <windows.h>
#elif defined(MLW_LINUX) || defined(MLW_MAC)
#include <sys/mman.h>
#else
#error "Unsupported platform"
#endif

namespace core::detail
{
	FormatBufferType& getFormatBuffer()
	{
		thread_local FormatBufferType buf = []()
			{
				FormatBufferType b;
#if defined(MLW_WINDOWS)

				b.ptr = static_cast<char*>(VirtualAlloc(nullptr, ALLOC_GRANULARITY, MEM_RESERVE, PAGE_READWRITE));
				VirtualAlloc(b.ptr, PAGE_SIZE, MEM_COMMIT, PAGE_READWRITE);
#else
				b.ptr = (char*)mmap(nullptr, PAGE_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
#endif

				b.ptr = MLW_ASSUME_ALIGNED(b.ptr, 4096);
				b.len = 0;
				b.capacity = static_cast<index_t>(PAGE_SIZE);
				return b;
			}();
		return buf;
	}
}

void core::FormatBufferType::realocate()
{
#if defined(MLW_WINDOWS)
	if ((capacity & GRAN_MASK) == 0)
	{
		// need to reserve more memory
		// see if can extand block
		char* new_ptr = static_cast<char*>(VirtualAlloc(nullptr, ((capacity >> GRAN_SHIFT) + 1) * ALLOC_GRANULARITY, MEM_RESERVE, PAGE_READWRITE));
		VirtualAlloc(new_ptr, capacity + PAGE_SIZE, MEM_COMMIT, PAGE_READWRITE);
		// memcpy


		mlwMemcpy(MLW_ASSUME_ALIGNED(new_ptr, 4096), MLW_ASSUME_ALIGNED(ptr, 4096), len);

		VirtualFree(ptr, 0, MEM_RELEASE);
		ptr = new_ptr;
		capacity += static_cast<index_t>(PAGE_SIZE);
	}
	else
	{
		VirtualAlloc(ptr + capacity, PAGE_SIZE, MEM_COMMIT, PAGE_READWRITE);
		capacity += static_cast<index_t>(PAGE_SIZE);
	}
#else
	ptr = static_cast<char*>(mremap(ptr, capacity, capacity + PAGE_SIZE, MREMAP_MAYMOVE));
	ptr = MLW_ASSUME_ALIGNED(ptr, 4096);
	capacity += static_cast<index_t>(PAGE_SIZE);
#endif
}
