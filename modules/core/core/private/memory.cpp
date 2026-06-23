#include "memory/memory.h"
#include "macro.h"

#if defined(MLW_WINDOWS)
#include <windows.h>
#elif defined(MLW_LINUX) || defined(MLW_MAC)
#include <sys/mman.h>
#include "memory.h"
#endif

bool core::StackAllocator::init(usize size)
{
	if (base)
	{
		MLW_DEBUG_PRINT("StackAllocator already initiolised");
		return false;
	}

	size = (size + PAGE_MASK) & ~PAGE_MASK; // round up to page size

#if defined(MLW_WINDOWS)
	base = ::VirtualAlloc(nullptr, size, MEM_RESERVE, PAGE_READWRITE);
	if (!base)
		return false;

	void* commit = ::VirtualAlloc(base, PAGE_SIZE, MEM_COMMIT, PAGE_READWRITE);
	if (!commit)
	{
		::VirtualFree(base, 0, MEM_RELEASE);
		base = nullptr;
		return false;
	}
	committed = PAGE_SIZE;

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

void* core::StackAllocator::alloc(usize size, usize alignment)
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
		usize new_committed = (new_offset + size + PAGE_MASK) & ~PAGE_MASK;
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

bool core::BlockAllocator::init(usize size, usize b_size)
{
	if (base)
	{
		MLW_DEBUG_PRINT("BlockAllocator already initiolised");
		return false;
	}

	block_size = mlwMax(sizeof(void*), b_size);
	block_size = (block_size + alignof(void*) - 1) & ~(alignof(void*) - 1);

	size = (size + PAGE_MASK) & ~PAGE_MASK; // round up to page size
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
		void* current = first_free;
		for (usize i = 0; i < block_count; ++i)
		{
			void* next = static_cast<uint8*>(current) + block_size;
			*static_cast<void**>(current) = next;
			current = next;
		}
		*static_cast<void**>(current) = nullptr;
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

static char* fit_aligned(core::GAlloc::Header* block, usize size, usize align) {
	char* payload = reinterpret_cast<char*>(block + 1);
	char* aligned = reinterpret_cast<char*>(
		(reinterpret_cast<uintptr_t>(payload) + align - 1) & ~(align - 1)
		);
	// if aligned == payload we still need room for nothing extra, that is fine
	// but if aligned is inside the header space we need to bump
	if (aligned < payload) aligned += align; // should never happen but safety

	usize total_needed = (aligned - reinterpret_cast<char*>(block)) + size;
	if (total_needed > block->next_off - sizeof(core::GAlloc::Header)) return nullptr;
	return aligned;
}



void* core::GAlloc::allign_alloc(usize size, usize align)
{
	if (align & (align - 1)) return nullptr; //maby assert
	if (align < 8) align = 8;

	if (align == PAGE_SIZE) {
		// force OS path
		// mmap/VirtualAlloc always page aligned
		TODO();
	}
	if (align > 256) return nullptr; //maby assert


	if (size <= MIN_SIZE) {
		//add aling > size path
		TODO();
	}
	else if (size < MAX_SIZE) {
		Region* curent = first_region;
		sync::spin_lock::MCS::Node lock_node{};
		while (true) {
			curent->lock.lock(lock_node);
			if (curent->free_block != nullptr) {
				break; // found one, still locked, exit
			}
			curent->lock.unlock(lock_node); // unlock before moving
			if (curent->next == nullptr) {
				if (!alloc_new()) return nullptr;
			}
			curent = curent->next;
		}

		Header* block = curent->free_block;
		char* aligned_payload = nullptr;
		while ((aligned_payload = fit_aligned(block, size, align)) == nullptr) {
			if (block->getFreePtr()->next_free_block != nullptr) {
				block = block->getFreePtr()->next_free_block;
				continue;
			}
			// exhausted this region, move to next

			curent->lock.unlock(lock_node);
			do {
				if (curent->next == nullptr) {
					if (!alloc_new()) return nullptr;
				}
				curent = curent->next;
				curent->lock.lock(lock_node);
				block = curent->free_block;
				if (block != nullptr) break;      // this region has blocks, check size
				curent->lock.unlock(lock_node);   // empty, keep going
			} while (true);
		}

		//][header][padding][new header][alinged payload][potentional header][after pading][

		Header* new_header = reinterpret_cast<Header*>(aligned_payload) - 1;

		usize padding = reinterpret_cast<char*>(new_header) - reinterpret_cast<char*>(block + 1);
		usize after_pading = (reinterpret_cast<char*>(block) + block->next_off) - aligned_payload - size - sizeof(Header);

		if (padding <= MIN_SIZE) {
			Header* next_h = reinterpret_cast<Header*>(reinterpret_cast<char*>(block) + block->next_off);
			Header* prev_h = nullptr;
			if (after_pading <= MIN_SIZE) {
				if (block->getFreePtr()->prev_free_block == nullptr) {
					curent->free_block = block->getFreePtr()->next_free_block;
				}
				else {
					block->getFreePtr()->prev_free_block->getFreePtr()->next_free_block = block->getFreePtr()->next_free_block;
				}
				if (block->getFreePtr()->next_free_block != nullptr) {
					block->getFreePtr()->next_free_block->getFreePtr()->prev_free_block = block->getFreePtr()->prev_free_block;
				}
				if (block->pre_off != 0)
					prev_h = reinterpret_cast<Header*>(reinterpret_cast<char*>(block) - block->pre_off);
				mlwMemset(block, 0, sizeof(Header));
				block = new_header;
			}
			else {
				prev_h = block;
				block = new_header;
			}

			if (prev_h) {
				block->pre_off = prev_h->next_off = reinterpret_cast<char*>(block) - reinterpret_cast<char*>(prev_h);
			}
			else {
				block->pre_off = 0;
			}

			block->next_off = reinterpret_cast<char*>(next_h) - reinterpret_cast<char*>(block);
			if (reinterpret_cast<char*>(next_h) - reinterpret_cast<char*>(curent) < Region::BLOCK_SIZE) {
				next_h->pre_off = block->next_off;
			}

			block->align = align;

			curent->lock.unlock(lock_node);
			return reinterpret_cast<void*>(block + 1);
		}
		else {

			//unlock
		}



	}
	else {
		TODO();
	}
}

bool core::GAlloc::alloc_new()
{
	Region* new_reg;
#if defined(MLW_WINDOWS)
	new_reg = static_cast<Region*>(::VirtualAlloc(nullptr, Region::BLOCK_SIZE, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
	if (!new_reg)
	{
		MLW_DEBUG_PRINT("failed to virtual alloc a new region in GAlloc");
		return false;
	}
#elif defined(MLW_LINUX) || defined(MLW_MAC)
	new_reg = static_cast<Region*>(::mmap(nullptr, Region::BLOCK_SIZE, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0));
	if (new_reg == MAP_FAILED)
	{
		MLW_DEBUG_PRINT("failed to virtual alloc a new region in GAlloc");
		return false;
	}
#endif
	new_reg->next = nullptr;
	(new_reg->lock) = sync::spin_lock::MCS{};
	new_reg->free_block = reinterpret_cast<Header*>(new_reg + 1);

	new_reg->free_block->align = 0;
	new_reg->free_block->pre_off = 0;
	new_reg->free_block->next_off = Region::BLOCK_SIZE - sizeof(Region) - sizeof(Header);
	new_reg->free_block->getFreePtr()->next_free_block = nullptr;
	new_reg->free_block->getFreePtr()->prev_free_block = nullptr;



	sync::Lock lock{ region_list_lock };
	Region* reg = first_region;
	if (!reg)
	{
		first_region = new_reg;

		new_reg->previous = nullptr;
	}
	else
	{
		while (reg->next)
		{
			reg = reg->next;
		}
		// reg is now the tail
		reg->next = new_reg;
		new_reg->previous = reg;
	}

	return true;
}

core::GAlloc::GAlloc()
{
	alloc_new();
}
