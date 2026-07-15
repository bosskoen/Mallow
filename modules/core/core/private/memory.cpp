#include "memory/memory.h"
#include "macro.h"
#include "libc/mem.h"

#if defined(MLW_WINDOWS)
#include <windows.h>
#elif defined(MLW_LINUX) || defined(MLW_MAC)
#include "posix/syscall_api.h"
#endif

bool core::StackAllocator::init(usize size)
{
	if (base)
	{
		MLW_DEBUG_PRINT("StackAllocator already initiolised");
		return false;
	}

	size = (size + PLATFORM_INFO.page_mask) & ~PLATFORM_INFO.page_mask; // round up to page size

#if defined(MLW_WINDOWS)
	base = ::VirtualAlloc(nullptr, size, MEM_RESERVE, PAGE_READWRITE);
	if (!base)
		return false;

	void *commit = ::VirtualAlloc(base, PLATFORM_INFO.page_size, MEM_COMMIT, PAGE_READWRITE);
	if (!commit)
	{
		::VirtualFree(base, 0, MEM_RELEASE);
		base = nullptr;
		return false;
	}
	committed = PLATFORM_INFO.page_size;

#elif defined(MLW_LINUX) || defined(MLW_MAC)
	base = ::mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
	if (base == MAP_FAILED)
	{
		base = nullptr;
		return false;
	}
	committed = size;
#endif

	capacity = size;
	offset = 0;
	return true;
}
void core::StackAllocator::shutdown()
{
	if (base)
	{
#if defined(MLW_WINDOWS)
		::VirtualFree(base, 0, MEM_RELEASE);
#elif defined(MLW_LINUX) || defined(MLW_MAC)
		::munmap(base, capacity);
#endif
	}
	base = nullptr;
	capacity = 0;
	offset = 0;
	committed = 0;
}

void *core::StackAllocator::alloc(usize size, usize alignment)
{
	if (!base)
	{
		MLW_DEBUG_PRINT("trying to alloc to a uninitiolised StackAllocator");
		return nullptr;
	}

	usize new_offset = (offset + alignment - 1) & ~(alignment - 1);

	if (new_offset + size > capacity)
	{
		MLW_DEBUG_PRINT("StackAllocator full");
		return nullptr;
	}

#if defined(MLW_WINDOWS)
	if (new_offset + size > committed)
	{
		usize new_committed = (new_offset + size + PLATFORM_INFO.page_mask) & ~PLATFORM_INFO.page_mask;
		void *commit = VirtualAlloc(static_cast<uint8 *>(base) + committed, new_committed - committed, MEM_COMMIT, PAGE_READWRITE);
		if (!commit)
			return nullptr;
		committed = new_committed;
	}
#endif

	void *ptr = reinterpret_cast<uint8 *>(base) + new_offset;
	offset = new_offset + size;
	return ptr;
}

bool core::BlockAllocator::init(usize size, usize b_size)
{
	if (base)
	{
		MLW_DEBUG_PRINT("BlockAllocator already initiolised");
		return false;
	}

	block_size = mlwMax(sizeof(void *), b_size);
	block_size = (block_size + alignof(void *) - 1) & ~(alignof(void *) - 1);

	size = (size + PLATFORM_INFO.page_mask) & ~PLATFORM_INFO.page_mask; // round up to page size
#if defined(MLW_WINDOWS)
	base = ::VirtualAlloc(nullptr, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
#elif defined(MLW_LINUX) || defined(MLW_MAC)
	base = ::mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
#endif
	if (base)
	{
		capacity = size;
		usize block_count = (size / block_size);
		first_free = base;
		void *current = first_free;
		for (usize i = 0; i < block_count; ++i)
		{
			void *next = static_cast<uint8 *>(current) + block_size;
			*static_cast<void **>(current) = next;
			current = next;
		}
		*static_cast<void **>(current) = nullptr;
		return true;
	}

	return false;
}

void core::BlockAllocator::shutdown()
{
	if (base)
	{
#if defined(MLW_WINDOWS)
		::VirtualFree(base, 0, MEM_RELEASE);
#elif defined(MLW_LINUX) || defined(MLW_MAC)
		::munmap(base, capacity);
#endif
	}
	base = nullptr;
	capacity = 0;
	block_size = 0;
	first_free = nullptr;
}
//--------------------------------


