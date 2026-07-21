/// \file
/// \brief Implementations of Arena and Pool (see memory.h).
///
/// Both own a single OS reservation. The key platform split: on Windows we
/// MEM_RESERVE address space and MEM_COMMIT physical pages separately (the Arena
/// commits lazily as it grows); on Linux/mac a MAP_ANONYMOUS mmap reserves and
/// the kernel populates pages lazily on first touch, so there is nothing to
/// commit incrementally.

#include "memory/memory.h"
#include "macro.h"
#include "libc/mem.h"

#if defined(MLW_WINDOWS)
#include <windows.h>
#elif defined(MLW_LINUX) || defined(MLW_MAC)
#include "posix/syscall_api.h"
#endif

bool core::Arena::init(usize size)
{
	if (base)
	{
		MLW_DEBUG_PRINT("Arena already initiolised");
		return false;
	}

	size = (size + PLATFORM_INFO.page_mask) & ~PLATFORM_INFO.page_mask; // round up to page size

#if defined(MLW_WINDOWS)
	// Reserve the whole range but commit only the first page; alloc() commits
	// more on demand as the offset grows. This keeps physical memory proportional
	// to what is actually used, not to the reservation size.
	base = ::VirtualAlloc(nullptr, size, MEM_RESERVE, PAGE_READWRITE);
	if (!base)
		return false;

	void* commit = ::VirtualAlloc(base, PLATFORM_INFO.page_size, MEM_COMMIT, PAGE_READWRITE);
	if (!commit)
	{
		::VirtualFree(base, 0, MEM_RELEASE);
		base = nullptr;
		return false;
	}
	committed = PLATFORM_INFO.page_size;

#elif defined(MLW_LINUX) || defined(MLW_MAC)
	// One anonymous mapping for the whole range. Pages are demand-zero: the
	// kernel backs them on first write, so "committed = size" here means the
	// mapping exists, not that physical memory is populated.
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

void core::Arena::shutdown()
{
	// Releases the reservation only — no destructors (see the Arena warning).
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

void* core::Arena::alloc(usize size, usize alignment)
{
	if (!base)
	{
		MLW_DEBUG_PRINT("trying to alloc to a uninitiolised Arena");
		return nullptr;
	}

	usize new_offset = (offset + alignment - 1) & ~(alignment - 1); // align the bump pointer up

	if (new_offset + size > capacity)
	{
		MLW_DEBUG_PRINT("Arena full");
		return nullptr;
	}

#if defined(MLW_WINDOWS)
	// Grow the committed region to cover the new high-water mark, page-rounded.
	// Linux/mac need nothing here: the mapping already spans the range and pages
	// fault in on demand.
	if (new_offset + size > committed)
	{
		usize new_committed = (new_offset + size + PLATFORM_INFO.page_mask) & ~PLATFORM_INFO.page_mask;
		void* commit = VirtualAlloc(static_cast<uint8*>(base) + committed, new_committed - committed, MEM_COMMIT, PAGE_READWRITE);
		if (!commit)
			return nullptr;
		committed = new_committed;
	}
#endif

	void* ptr = reinterpret_cast<uint8*>(base) + new_offset;
	offset = new_offset + size;
	return ptr;
}

bool core::Pool::init(usize size, usize b_size)
{
	if (base)
	{
		MLW_DEBUG_PRINT("Pool already initiolised");
		return false;
	}

	// Block must hold at least the free-list link, and be pointer-aligned so the
	// link stores/loads cleanly.
	block_size = mlwMax(sizeof(void*), b_size);
	block_size = (block_size + alignof(void*) - 1) & ~(alignof(void*) - 1);

	size = (size + PLATFORM_INFO.page_mask) & ~PLATFORM_INFO.page_mask; // round up to page size
#if defined(MLW_WINDOWS)
	base = ::VirtualAlloc(nullptr, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
	if (base == nullptr) {
		block_size = 0;
		base = nullptr;
		return false;
	}
#elif defined(MLW_LINUX) || defined(MLW_MAC)
	base = ::mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
	if (base == MAP_FAILED) {
		block_size = 0;
		base = nullptr;
		return false;
	}
#endif
	capacity = size;

	// Thread the free list through every *whole* block. block_count comes from
	// integer division, so a final partial block (when capacity isn't a multiple
	// of block_size) is simply never linked — dead tail space rather than a
	// short block that would overrun on use. Loop stops one early and terminates
	// the last real block with null.
	first_free = base;
	usize block_count = capacity / block_size;   // runt at the end is excluded
	if (block_count == 0) { shutdown(); return false; }  // size smaller than one block
	uint8* current = static_cast<uint8*>(first_free);
	for (usize i = 0; i + 1 < block_count; ++i)
	{
		uint8* next = current + block_size;
		*reinterpret_cast<void**>(current) = next;
		current = next;
	}
	*reinterpret_cast<void**>(current) = nullptr;
	return true;
}

void core::Pool::shutdown()
{
	// Releases the reservation only — does not run destructors and does not know
	// which blocks are live (see the Pool warning).
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