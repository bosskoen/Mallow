#include "memory/galloc.h"

#include "libc/mem.h"

#if defined(MLW_WINDOWS)
#include <windows.h>
#elif defined(MLW_LINUX) || defined(MLW_MAC)
#include "posix/syscall_api.h"
#endif


namespace core {
	alignas(ThreadCache) static thread_local uint8 tc_storage[sizeof(ThreadCache)]{0};
	static thread_local bool tc_constructed  = false; 

	static ThreadCache& getThreadCache() { return *reinterpret_cast<ThreadCache*>(tc_storage); }


	alignas(GAlloc) static uint8 g_alloc_storage[sizeof(GAlloc)];
	GAlloc& mlw_g_alloc = *reinterpret_cast<GAlloc*>(g_alloc_storage);



}
void core::ThreadCache::mlw__crt_distroy_tc_storage(){
	if (core::tc_constructed) {
	core::getThreadCache().~ThreadCache();
	core::tc_constructed = false;
	}
}
void core::ThreadCache::mlw__first_crt_ctor()
{
	if (!tc_constructed){
		new (tc_storage) ThreadCache{};
		tc_constructed = true;
	}
}

// Check whether `block` has enough room for `size` bytes at the requested
// alignment. Returns the aligned payload pointer, or nullptr if it won't fit.
// The caller uses this to walk the free list looking for a suitable block.
static char *fit_aligned(core::Header *block, usize size, usize align)
{
	char *payload = reinterpret_cast<char *>(block + 1);
	char *aligned = reinterpret_cast<char *>(
		(reinterpret_cast<uptr>(payload) + align - 1) & ~(align - 1));

	if (aligned < payload)
		aligned += align; // should never happen, but guards against overflow

	usize total_needed = (aligned - reinterpret_cast<char *>(block)) + size;
	if (total_needed > block->next_off - sizeof(core::Header))
		return nullptr;
	return aligned;
}

void *core::GAlloc::alignAlloc(usize size, usize align)
{
	if (align & (align - 1))
		return nullptr; // not a power of two
	if (align < 8)
		align = 8;
	// round size up to Header alignment so block boundaries stay aligned
	size = (size + alignof(Header) - 1) & ~(alignof(Header) - 1);

	if (align == PLATFORM_INFO.page_size)
	{
		return osAlloc(size);
	}
	if (align > 256)
		return nullptr;

	core::ThreadCache &cache = getThreadCache();
	// amortize remote-free processing: drain before every alloc
	drainRemoteList(cache);
	if (size < align)
	{
		size = align;
	}

	// ---- SMALL PATH ----
	// Fixed-size slab allocation. Each cell is the same size, free list
	// is a singly-linked embedded pointer chain. No headers needed.
	if (size <= MIN_SIZE)
	{
		RegionTable::Entry::Type block_type;
		if (size <= 8)
		{
			block_type = RegionTable::Entry::Type::S8;
		}
		else if (size <= 16)
		{
			block_type = RegionTable::Entry::Type::S16;
		}
		else if (size <= 32)
		{
			block_type = RegionTable::Entry::Type::S32;
		}
		else if (size <= 64)
		{
			block_type = RegionTable::Entry::Type::S64;
		}
		else
		{
			block_type = RegionTable::Entry::Type::S128;
		}

		Region *current;
		ThreadCache::SizeClass &sc = cache.getSizeClass(block_type);
		current = sc.active;
		if (current == nullptr)
		{
			if (!allocSmallRegion(nullptr, cache, block_type))
				return nullptr;
			current = sc.active;
		}

		while (current->free_block == nullptr)
		{
			if (current->next == nullptr)
				if (!allocSmallRegion(current, cache, block_type))
					return nullptr;
			current = current->next;
		}

		// pop the first free cell
		void *block = reinterpret_cast<void *>(current->free_block);
		current->free_block = *reinterpret_cast<Header **>(block);

		if (current->used_count == 0) --sc.free_slabs;
		++current->used_count;
		return block;
	}
	// ---- OS PATH ----
	else if (size >= MAX_SIZE)
	{
		return osAlloc(size);
	}
	// ---- MEDIUM PATH ----
	// Variable-size blocks with Header. First-fit search across regions,
	// then within the region's doubly-linked free list.
	else
	{
		// --- find a region with a free block ---

		Region *current = cache.medium.active;
		if (current == nullptr)
		{
			if (!allocMediumRegion(current, cache))
				return nullptr;
			current = cache.medium.active;
		}
		while (true)
		{
			if (current->free_block != nullptr)
				break;

			if (current->next == nullptr)
				if (!allocMediumRegion(current, cache))
					return nullptr;
			current = current->next;
		}

		// --- find a block that fits the requested alignment ---
		Header *block = current->free_block;
		char *aligned_payload = nullptr;
		while ((aligned_payload = fit_aligned(block, size, align)) == nullptr)
		{
			// try next free block in this region
			if (block->getFreePtr()->next_free_block != nullptr)
			{
				block = block->getFreePtr()->next_free_block;
				continue;
			}

			// exhausted this region, move to next (or allocate one)
			do
			{
				if (current->next == nullptr)
					if (!allocMediumRegion(current, cache))
						return nullptr;
				current = current->next;
				block = current->free_block;
				if (block != nullptr)
					break;
			} while (true);
		}

		// --- split the free block around the aligned payload ---
		//
		// Layout after splitting:
		//   [block/front][padding][new_header][aligned_payload][middle_h][remainder][next_h]
		//
		// front  = space before new_header (may become a free fragment or be absorbed)
		// back   = space after payload end (may become a free fragment or be absorbed)
		// Fragments smaller than MIN_SIZE + sizeof(Header) can't hold a free block,
		// so they get absorbed into the allocated block.

		Header *new_header = reinterpret_cast<Header *>(aligned_payload) - 1;
		Header *middle_h = reinterpret_cast<Header *>(aligned_payload + size);
		Header *next_h = reinterpret_cast<Header *>(reinterpret_cast<char *>(block) + block->next_off);
		Header *prev_h = block->pre_off != 0
							 ? reinterpret_cast<Header *>(reinterpret_cast<char *>(block) - block->pre_off)
							 : nullptr;

		Header *prev_f = block->getFreePtr()->prev_free_block;
		Header *next_f = block->getFreePtr()->next_free_block;

		usize padding = (new_header > block)
			? static_cast<usize>(reinterpret_cast<char*>(new_header) - reinterpret_cast<char*>(block + 1))
			: 0;
		usize after_padding = reinterpret_cast<char *>(next_h) - reinterpret_cast<char *>(middle_h);

		bool keep_front = padding > MIN_SIZE + sizeof(Header);
		bool keep_back = after_padding > MIN_SIZE + sizeof(Header);
		bool last_block = static_cast<usize>(reinterpret_cast<char *>(next_h) - reinterpret_cast<char *>(current)) >= Region::MEDIUM_BLOCK_SIZE;

		// The four cases handle every combination of keeping/absorbing the
		// front and back fragments. In each case:
		//   - physical chain (pre_off / next_off) is updated for all affected headers
		//   - free list (prev_free_block / next_free_block) is updated to reflect
		//     which fragments remain free
		//   - block's old header is zeroed (absorbed into padding)
		//   - new_header gets align set to mark it as in-use

		// --- case 1: keep back as free block, absorb front ---
		if (!keep_front && keep_back)
		{
			// replace block with middle_h in free list
			if (prev_f)
				prev_f->getFreePtr()->next_free_block = middle_h;
			else
				current->free_block = middle_h;
			if (next_f)
				next_f->getFreePtr()->prev_free_block = middle_h;
			middle_h->getFreePtr()->prev_free_block = prev_f;
			middle_h->getFreePtr()->next_free_block = next_f;

			usize mid_to_next = reinterpret_cast<char *>(next_h) - reinterpret_cast<char *>(middle_h);
			middle_h->pre_off = reinterpret_cast<char *>(middle_h) - reinterpret_cast<char *>(new_header);
			middle_h->next_off = mid_to_next;
			if (!last_block)
				next_h->pre_off = mid_to_next;
			middle_h->align = 0;

			mlwMemset(block, 0, sizeof(Header));
			new_header->pre_off = prev_h ? reinterpret_cast<char *>(new_header) - reinterpret_cast<char *>(prev_h) : 0;
			if (prev_h)
				prev_h->next_off = new_header->pre_off;
			new_header->next_off = middle_h->pre_off;
			new_header->align = align;
		}

		// --- case 2: absorb both front and back ---
		else if (!keep_front && !keep_back)
		{
			// unlink block from free list entirely
			if (prev_f)
				prev_f->getFreePtr()->next_free_block = next_f;
			else
				current->free_block = next_f;
			if (next_f)
				next_f->getFreePtr()->prev_free_block = prev_f;

			usize new_next_off = reinterpret_cast<char *>(next_h) - reinterpret_cast<char *>(new_header);
			mlwMemset(block, 0, sizeof(Header));
			new_header->pre_off = prev_h ? reinterpret_cast<char *>(new_header) - reinterpret_cast<char *>(prev_h) : 0;
			if (prev_h)
				prev_h->next_off = new_header->pre_off;
			new_header->next_off = new_next_off;
			if (!last_block)
				next_h->pre_off = new_next_off;

			new_header->align = align;
		}
		// --- case 3: keep both front and back as free blocks ---
		else if (keep_front && keep_back)
		{
			// block stays as front fragment, middle_h inserted after it in the free list
			usize block_to_new = reinterpret_cast<char *>(new_header) - reinterpret_cast<char *>(block);
			block->next_off = block_to_new;
			block->getFreePtr()->next_free_block = middle_h;

			middle_h->getFreePtr()->prev_free_block = block;
			middle_h->getFreePtr()->next_free_block = next_f;
			if (next_f)
				next_f->getFreePtr()->prev_free_block = middle_h;

			usize mid_to_next = reinterpret_cast<char *>(next_h) - reinterpret_cast<char *>(middle_h);
			middle_h->pre_off = reinterpret_cast<char *>(middle_h) - reinterpret_cast<char *>(new_header);
			middle_h->next_off = mid_to_next;
			if (!last_block)
				next_h->pre_off = mid_to_next;
			middle_h->align = 0;

			new_header->pre_off = block_to_new;
			new_header->next_off = middle_h->pre_off;
			new_header->align = align;
		}
		// --- case 4: keep front, absorb back ---
		else /*(keep_front && !keep_back) */
		{
			// block stays as front fragment (already in free list), just shrink it
			usize block_to_new = reinterpret_cast<char *>(new_header) - reinterpret_cast<char *>(block);
			block->next_off = block_to_new;

			usize new_to_next = reinterpret_cast<char *>(next_h) - reinterpret_cast<char *>(new_header);
			new_header->pre_off = block_to_new;
			new_header->next_off = new_to_next;
			if (!last_block)
				next_h->pre_off = new_to_next;
			new_header->align = align;
		}
		if (current->used_count == 0) --cache.medium.free_slabs;
		++current->used_count;
		return reinterpret_cast<void *>(new_header + 1);
	}
}

// Try to adopt an orphaned medium region before falling back to mmap.
// If last_region is null, the new region becomes the active head;
// otherwise it's appended after last_region.
bool core::GAlloc::allocMediumRegion(core::Region *last_region, core::ThreadCache &cache)
{
	// check orphan pool first — reusing an existing region avoids a syscall
	{
		sync::Lock lock{mlw_g_alloc.orphan_lock};
		drainRemoteList(orphan_pool);
		ThreadCache::SizeClass &sc = orphan_pool.getSizeClass(RegionTable::Entry::Type::Medium);
		if (sc.active != nullptr)
		{
			Region *r = sc.active;
			sc.active = r->next;
			if (r->next)
				r->next->previous = nullptr;
			r->next = nullptr;
			r->previous = nullptr;
			// transfer ownership under the migration lock so cross-thread
			// frees see the correct owning_cache
			sync::WriteLock l{r->migration_lock};
			r->owning_cache = &cache;
			l.unlock();
			if (!last_region)
			{
				cache.getSizeClass(RegionTable::Entry::Type::Medium).active = r;
			}
			else
			{
				last_region->next = r;
				r->previous = last_region;
			}
			if (r->used_count == 0) {
				--sc.free_slabs;
				++cache.getSizeClass(RegionTable::Entry::Type::Medium).free_slabs;
			}
			return true;
		}
	}

	// no orphans available — allocate a fresh region from the OS
	Region *new_reg;
#if defined(MLW_WINDOWS)
	new_reg = static_cast<Region *>(::VirtualAlloc(nullptr, Region::MEDIUM_BLOCK_SIZE, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
	if (!new_reg)
	{
		MLW_DEBUG_PRINT("failed to virtual alloc a new region in GAlloc");
		return false;
	}
#elif defined(MLW_LINUX) || defined(MLW_MAC)
	new_reg = static_cast<Region *>(::mmap(nullptr, Region::MEDIUM_BLOCK_SIZE, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0));
	if (new_reg == MAP_FAILED)
	{
		MLW_DEBUG_PRINT("failed to virtual alloc a new region in GAlloc");
		return false;
	}
#endif
	// placement-new for non-trivial members (Atomic, RWLock)
	new_reg->next = nullptr;
	new (&new_reg->remote_free) sync::Atomic<void *>{nullptr};
	new (&new_reg->migration_lock) sync::RWLock{};
	new_reg->used_count = 0;
	++cache.medium.free_slabs;
	new_reg->owning_cache = &cache;

	// the entire region (minus the Region header and one Header) is a single free block
	new_reg->free_block = reinterpret_cast<Header *>(new_reg + 1);
	new_reg->free_block->align = 0;
	new_reg->free_block->pre_off = 0;
	new_reg->free_block->next_off = Region::MEDIUM_BLOCK_SIZE - sizeof(Region);
	new_reg->free_block->getFreePtr()->next_free_block = nullptr;
	new_reg->free_block->getFreePtr()->prev_free_block = nullptr;

	if (!last_region)
	{
		cache.medium.active = new_reg;
		new_reg->previous = nullptr;
	}
	else
	{
		last_region->next = new_reg;
		new_reg->previous = last_region;
	}

	sync::WriteLock lock{region_list_lock};
	region_table.insert(RegionTable::Entry{new_reg, RegionTable::Entry::Type::Medium});

	return true;
}

// Unlink a medium region from its owning cache and return it to the OS.
// Opportunistically drains orphan remote frees if the lock is available.
void core::GAlloc::freeMediumRegion(core::Region *region)
{
	// tryLock: don't block the free path just to drain orphans
	Optional<sync::Lock<sync::spin_lock::MCS>> l = nullptr;
	sync::Lock<sync::spin_lock::MCS>::tryLock(orphan_lock, l);

	if (l.isSome())
	{
		drainRemoteList(orphan_pool);
		l->unlock();
	}

	Region *next = region->next;
	Region *prev = region->previous;
	ThreadCache *cache = region->owning_cache;
	if (next)
	{
		next->previous = prev;
	}
	if (prev)
	{
		prev->next = next;
	}
	else
	{
		cache->medium.active = next;
	}
	{
		sync::WriteLock lock{region_list_lock};
		region_table.remove(region);
	}
#if defined(MLW_WINDOWS)
	::VirtualFree(region, 0, MEM_RELEASE);
#elif defined(MLW_LINUX) || defined(MLW_MAC)
	::munmap(region, Region::MEDIUM_BLOCK_SIZE);
#endif
}

// Free a medium block and coalesce with adjacent free blocks.
// Returns true if the region was completely empty and got returned to the OS.
//
// Coalescing is two-pass:
//   1. Backward: merge with predecessor if free, or reclaim dead padding
//   2. Forward:  merge with successor if free
// Free list updates depend on the combination — see the architecture
// overview in galloc.h for the full decision table.
bool core::GAlloc::freeMedium(void *ptr, core::Region *region)
{
	Header *header = static_cast<Header *>(ptr) - 1;
	mlw_debug_assert_msg(header->align != 0, "freed a free block");

	Header *next_header = reinterpret_cast<Header *>(
		reinterpret_cast<char *>(header) + header->next_off);
	bool last_block = static_cast<usize>(
						  reinterpret_cast<char *>(next_header) - reinterpret_cast<char *>(region)) >= Region::MEDIUM_BLOCK_SIZE;

	Header *prev_header = header->pre_off != 0
							  ? reinterpret_cast<Header *>(reinterpret_cast<char *>(header) - header->pre_off)
							  : nullptr;

	bool merged_backward = false;

	// === BACKWARD ===
	if (prev_header != nullptr && prev_header->align == 0)
	{
		// prev is free and already in the free list — extend it to cover us
		merged_backward = true;
		prev_header->next_off =
			reinterpret_cast<char *>(next_header) - reinterpret_cast<char *>(prev_header);
		if (!last_block)
			next_header->pre_off = prev_header->next_off;
		header = prev_header;
	}
	else if (header->pre_off == 0 && reinterpret_cast<Header *>(region + 1) != header)
	{
		// pre_off == 0 means nothing precedes us in the block chain, but we're
		// not at region+1 — the gap is dead alignment padding from a prior alloc.
		// Reclaim it by moving the header back to region start.
		Header *reclaimed = reinterpret_cast<Header *>(region + 1);
		reclaimed->pre_off = 0;
		reclaimed->next_off =
			reinterpret_cast<char *>(next_header) - reinterpret_cast<char *>(reclaimed);
		if (!last_block)
			next_header->pre_off = reclaimed->next_off;
		header = reclaimed;
	}

	// === FORWARD ===
	if (!last_block && next_header->align == 0)
	{
		// successor is free — absorb it into our block
		Header *next_next = reinterpret_cast<Header *>(
			reinterpret_cast<char *>(next_header) + next_header->next_off);
		bool next_last = static_cast<usize>(
							 reinterpret_cast<char *>(next_next) - reinterpret_cast<char *>(region)) >= Region::MEDIUM_BLOCK_SIZE;

		header->next_off =
			reinterpret_cast<char *>(next_next) - reinterpret_cast<char *>(header);
		if (!next_last)
			next_next->pre_off = header->next_off;

		// next_header is currently in the free list and needs to be dealt with
		Header *next_f = next_header->getFreePtr()->next_free_block;
		Header *prev_f = next_header->getFreePtr()->prev_free_block;

		if (merged_backward)
		{
			// header (= prev) is already linked — just remove next_header
			if (prev_f)
				prev_f->getFreePtr()->next_free_block = next_f;
			else
				region->free_block = next_f;
			if (next_f)
				next_f->getFreePtr()->prev_free_block = prev_f;
		}
		else
		{
			// header is new to the free list — steal next_header's position
			header->getFreePtr()->prev_free_block = prev_f;
			header->getFreePtr()->next_free_block = next_f;
			if (prev_f)
				prev_f->getFreePtr()->next_free_block = header;
			else
				region->free_block = header;
			if (next_f)
				next_f->getFreePtr()->prev_free_block = header;
		}
	}
	else if (!merged_backward)
	{
		// no merge in either direction — insert as new free block at list head
		header->getFreePtr()->next_free_block = region->free_block;
		header->getFreePtr()->prev_free_block = nullptr;
		if (region->free_block)
			region->free_block->getFreePtr()->prev_free_block = header;
		region->free_block = header;
	}
	// else: merged backward, no forward merge — prev is already in the list

	header->align = 0;

	// --- slab bookkeeping ---
	// free_slabs counts completely empty regions. When we hit 2, one gets
	// returned to the OS. This keeps one warm spare without hoarding.
	ThreadCache *cache = region->owning_cache;
	--region->used_count;
	if (region->used_count == 0)
		++cache->medium.free_slabs;
	if (cache->medium.free_slabs >= 2)
	{
		freeMediumRegion(region);
		--cache->medium.free_slabs;
		return true;
	}
	return false;
}

// Small free: push the cell back onto the slab's embedded free list.
// No headers involved — the cell itself stores the next pointer.
void core::GAlloc::freeSmall(void *ptr, core::Region *region, RegionTable::Entry::Type size)
{
	*reinterpret_cast<void **>(ptr) = reinterpret_cast<void *>(region->free_block);
	region->free_block = reinterpret_cast<Header *>(ptr);

	ThreadCache *cache = region->owning_cache;
	--region->used_count;
	ThreadCache::SizeClass &sc = cache->getSizeClass(size);
	if (region->used_count == 0)
		++sc.free_slabs;
	if (sc.free_slabs >= 2)
	{
		freeSmallRegion(region, size);
		--sc.free_slabs;
	}
}

void core::GAlloc::freeSmallRegion(core::Region *region, RegionTable::Entry::Type size)
{
	mlw_debug_assert_msg(size != RegionTable::Entry::Type::Medium, "tryed to free a medium region in the small path");

	Optional<sync::Lock<sync::spin_lock::MCS>> l = nullptr;
	sync::Lock<sync::spin_lock::MCS>::tryLock(orphan_lock, l);

	if (l.isSome())
	{
		drainRemoteList(orphan_pool);
		l->unlock();
	}

	Region *next = region->next;
	Region *prev = region->previous;
	ThreadCache *cache = region->owning_cache;
	if (next)
	{
		next->previous = prev;
	}
	if (prev)
	{
		prev->next = next;
	}
	else
	{
		cache->getSizeClass(size).active = next;
	}
	{
		sync::WriteLock lock{region_list_lock};
		region_table.remove(region);
	}
#if defined(MLW_WINDOWS)
	::VirtualFree(region, 0, MEM_RELEASE);
#elif defined(MLW_LINUX) || defined(MLW_MAC)
	::munmap(region, Region::SMALL_BLOCK_SIZE);
#endif
}

// Same structure as allocMediumRegion: try orphan pool first, then mmap.
// Small regions use a bump-pointer free list: every cell is linked during
// init, aligned to the cell size.
bool core::GAlloc::allocSmallRegion(core::Region *last_region, core::ThreadCache &cache, RegionTable::Entry::Type size)
{
	mlw_debug_assert_msg(size != RegionTable::Entry::Type::Medium, "core::GAlloc::allocSmallRegion tryed to alloc a medium region");

	{
		sync::Lock lock{mlw_g_alloc.orphan_lock};
		drainRemoteList(orphan_pool);
		ThreadCache::SizeClass &sc = orphan_pool.getSizeClass(size);
		if (sc.active != nullptr)
		{
			Region *r = sc.active;
			sc.active = r->next;
			if (r->next)
				r->next->previous = nullptr;
			r->next = nullptr;
			r->previous = nullptr;
			sync::WriteLock l{r->migration_lock};
			r->owning_cache = &cache;
			l.unlock();
			if (!last_region)
			{
				cache.getSizeClass(size).active = r;
			}
			else
			{
				last_region->next = r;
				r->previous = last_region;
			}
			if (r->used_count == 0) {
				--sc.free_slabs;
				++cache.getSizeClass(size).free_slabs;
			}
			return true;
		}
	}

	Region *new_reg;
#if defined(MLW_WINDOWS)
	new_reg = static_cast<Region *>(::VirtualAlloc(nullptr, Region::SMALL_BLOCK_SIZE, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
	if (!new_reg)
	{
		MLW_DEBUG_PRINT("failed to virtual alloc a new region in GAlloc");
		return false;
	}
#elif defined(MLW_LINUX) || defined(MLW_MAC)
	new_reg = static_cast<Region *>(::mmap(nullptr, Region::SMALL_BLOCK_SIZE, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0));
	if (new_reg == MAP_FAILED)
	{
		MLW_DEBUG_PRINT("failed to virtual alloc a new region in GAlloc");
		return false;
	}
#endif

	usize block_size = static_cast<usize>(size);

	new_reg->next = nullptr;
	new (&new_reg->remote_free) sync::Atomic<void *>{nullptr};
	new (&new_reg->migration_lock) sync::spin_lock::MCS{};
	new_reg->used_count = 0;
	++cache.getSizeClass(size).free_slabs;
	new_reg->owning_cache = &cache;

	// free_block is not actually a Header* here — it's the first cell pointer.
	// We reuse the Region struct's free_block field for both small and medium.
	// First cell is aligned to the cell size so every cell in the slab is aligned.
	new_reg->free_block = reinterpret_cast<Header *>((reinterpret_cast<uptr>(new_reg + 1) + block_size - 1) & ~(block_size - 1));

	// build the embedded free list: each cell points to the next
	char *current = reinterpret_cast<char *>(new_reg->free_block);
	char *end = reinterpret_cast<char *>(new_reg) + Region::SMALL_BLOCK_SIZE;
	while (current + block_size < end)
	{
		char *next = current + block_size;
		*reinterpret_cast<void **>(current) = next;
		current = next;
	}
	*reinterpret_cast<void **>(current) = nullptr; // terminate

	if (!last_region)
	{
		cache.getSizeClass(size).active = new_reg;
		new_reg->previous = nullptr;
	}
	else
	{
		last_region->next = new_reg;
		new_reg->previous = last_region;
	}

	sync::WriteLock lock{region_list_lock};
	region_table.insert(RegionTable::Entry{new_reg, size});

	return true;
}

// Allocate directly from the OS for large requests (> MAX_SIZE or page-aligned).
// Tracks the allocation in os_table so free/realloc can find the size.
void *core::GAlloc::osAlloc(usize size)
{
	size = (size + PLATFORM_INFO.page_mask) & ~(PLATFORM_INFO.page_mask);
#if defined(MLW_WINDOWS)
	void *ptr = ::VirtualAlloc(nullptr, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
	if (!ptr)
		return nullptr;
#elif defined(MLW_LINUX) || defined(MLW_MAC)
	void *ptr = ::mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
	if (ptr == MAP_FAILED)
		return nullptr;
#endif

	sync::Lock l{os_lock};
	if (!os_table.insert(OSTable::Entry{ptr, size}))
	{
		l.unlock();
#if defined(MLW_WINDOWS)
		::VirtualFree(ptr, 0, MEM_RELEASE);
#elif defined(MLW_LINUX) || defined(MLW_MAC)
		::munmap(ptr, size);
#endif
		return nullptr;
	}
	return ptr;
}

void core::GAlloc::osFree(void *ptr)
{

	sync::Lock l{os_lock};

	Optional<OSTable::Entry> e = os_table.findAndRemove(ptr);
	l.unlock();
	if (e.isNone())
	{
		MLW_DEBUG_PRINT("tryed freeing a os poiter taht doesnt exsist");
		return;
	}

#if defined(MLW_WINDOWS)
	::VirtualFree(e->ptr, 0, MEM_RELEASE);
#elif defined(MLW_LINUX) || defined(MLW_MAC)
	::munmap(e->ptr, e->size);
#endif
}

// Process all pending cross-thread frees for a given cache.
// Called at the start of alloc() and free() to amortize the cost.
//
// Small blocks: atomically swap out the remote_free stack, walk it to find
// the tail and count, then splice the whole chain onto the local free list
// in one shot. This avoids per-block overhead.
//
// Medium blocks: each block goes through full freeMedium() for coalescing,
// since medium blocks need header/free-list surgery that can't be batched.
// If freeMedium returns true (region was freed), we stop processing that
// region's remaining remote frees since the memory is gone.
void core::GAlloc::drainRemoteList(ThreadCache &cache)
{
	auto drainSmall = [&](ThreadCache::SizeClass &sc, RegionTable::Entry::Type type)
	{
		if (!sc.has_remove_free)
			return;
		sc.has_remove_free.store(false, sync::MemoryOrder::Relaxed);
		Region *current = sc.active;
		while (current)
		{
			Region *next = current->next; // save — region may be freed below
			void *head = current->remote_free.exchange(nullptr, sync::MemoryOrder::AcqRel);
			if (head)
			{
				// walk the chain to find tail and count
				usize count = 1;
				void *tail = head;
				while (*reinterpret_cast<void **>(tail))
				{
					tail = *reinterpret_cast<void **>(tail);
					++count;
				}
				// splice onto local free list
				*reinterpret_cast<void **>(tail) = reinterpret_cast<void *>(current->free_block);
				current->free_block = reinterpret_cast<Header *>(head);
				current->used_count -= static_cast<uint32>(count);
				if (current->used_count == 0)
					++sc.free_slabs;
				if (sc.free_slabs >= 2)
				{
					freeSmallRegion(current, type);
					--sc.free_slabs;
				}
			}
			current = next;
		}
	};

	drainSmall(cache.small_8, RegionTable::Entry::Type::S8);
	drainSmall(cache.small_16, RegionTable::Entry::Type::S16);
	drainSmall(cache.small_32, RegionTable::Entry::Type::S32);
	drainSmall(cache.small_64, RegionTable::Entry::Type::S64);
	drainSmall(cache.small_128, RegionTable::Entry::Type::S128);
	if (cache.medium.has_remove_free)
	{
		cache.medium.has_remove_free.store(false, sync::MemoryOrder::Relaxed);
		Region *current = cache.medium.active;
		while (current)
		{
			Region *next = current->next; // region can be deleted by freeMedium
			void *block = current->remote_free.exchange(nullptr, sync::MemoryOrder::AcqRel);
			while (block)
			{
				void *next_block = *static_cast<void **>(block);
				bool region_deleted = freeMedium(block, current);
				if (region_deleted)
					break; // region is unmapped, stop touching it
				block = next_block;
			}
			current = next;
		}
	}
}

void core::GAlloc::free(void *ptr)
{
	if (ptr == nullptr)
		return;
	ThreadCache &cache = getThreadCache();

	drainRemoteList(cache);

	// look up which region owns this pointer
	RegionTable::Entry entry;
	bool os_region;
	{
		sync::ReadLock l{region_list_lock};
		RegionTable::Entry *e = region_table.find(ptr);
		if (e == nullptr)
		{
			os_region = true;
		}
		else
		{
			os_region = false;
			entry = *e;
		}
	}
	if (os_region)
	{
		osFree(ptr);
		return;
	}

	// fast path: same-thread free — no locking needed
	if (entry.ptr->owning_cache == &cache)
	{
		if (entry.type == RegionTable::Entry::Type::Medium)
		{
			freeMedium(ptr, entry.ptr);
		}
		else
		{
			freeSmall(ptr, entry.ptr, entry.type);
		}
	}
	// slow path: cross-thread free — push onto the region's lock-free remote_free stack
	else
	{

		// Signal the owning thread that it has remote frees to drain.
		// migration_lock (ReadLock) ensures owning_cache doesn't change
		// underneath us during a thread exit migration.
		//
		// The has_remove_free store MUST happen BEFORE the CAS push onto
		// remote_free. If we stored it after:
		//   1. CAS-push our block onto remote_free
		//   2. Owning thread drains, frees our block — last block in region
		//   3. Region gets unmapped (freeMediumRegion / freeSmallRegion)
		//   4. We dereference entry.ptr->owning_cache to store the flag
		//      — use-after-free, the region is gone
		//
		// By storing the flag first (under ReadLock, so the region is alive),
		// the CAS is the last thing we do with the region pointer.
		{
			sync::ReadLock lock{entry.ptr->migration_lock};
			MLW_COMPILER_BARRIER();
			switch (entry.type)
			{
			case RegionTable::Entry::Type::Medium:
				entry.ptr->owning_cache->medium.has_remove_free.store(true, sync::MemoryOrder::Release);
				break;
			case RegionTable::Entry::Type::S8:
				entry.ptr->owning_cache->small_8.has_remove_free.store(true, sync::MemoryOrder::Release);
				break;
			case RegionTable::Entry::Type::S16:
				entry.ptr->owning_cache->small_16.has_remove_free.store(true, sync::MemoryOrder::Release);
				break;
			case RegionTable::Entry::Type::S32:
				entry.ptr->owning_cache->small_32.has_remove_free.store(true, sync::MemoryOrder::Release);
				break;
			case RegionTable::Entry::Type::S64:
				entry.ptr->owning_cache->small_64.has_remove_free.store(true, sync::MemoryOrder::Release);
				break;
			case RegionTable::Entry::Type::S128:
				entry.ptr->owning_cache->small_128.has_remove_free.store(true, sync::MemoryOrder::Release);
				break;
			default:
				break;
			}
		}

		// Lock-free push onto the LIFO remote_free stack.
		// We store the old head inside the freed block itself (reusing the
		// first pointer-sized bytes), then CAS the region's remote_free to
		// point to this block.
		void **next_slot = static_cast<void **>(ptr);

		void *expected = entry.ptr->remote_free.load(sync::MemoryOrder::Acquire);
		do
		{
			*next_slot = expected;
		} while (!entry.ptr->remote_free.compareExchangeWeak(
			expected,
			ptr,
			sync::MemoryOrder::Release,
			sync::MemoryOrder::Acquire));
	}
}

void *core::GAlloc::realloc(void *ptr, usize new_size)
{
	if (ptr == nullptr)
		return alignAlloc(new_size, 8);
	if (new_size == 0)
	{
		free(ptr);
		return nullptr;
	}

	new_size = (new_size + alignof(Header) - 1) & ~(alignof(Header) - 1);

	ThreadCache &cache = getThreadCache();

	RegionTable::Entry entry;
	bool os_region;
	{
		sync::ReadLock l{region_list_lock};
		RegionTable::Entry *e = region_table.find(ptr);
		os_region = (e == nullptr);
		if (!os_region)
			entry = *e;
	}

	// --- OS allocs: try mremap on Linux, else alloc+copy+free ---
	if (os_region)
	{
		usize rounded = (new_size + PLATFORM_INFO.page_mask) & ~PLATFORM_INFO.page_mask;

		// TODO maby not remove and keep index??
		sync::Lock l{os_lock};
		Optional<OSTable::Entry> old = os_table.findAndRemove(ptr);
		if (old.isNone())
			return nullptr;

		// already big enough — no-op
		if (rounded <= old->size)
		{
			os_table.insert(OSTable::Entry{old->ptr, old->size});
			return ptr;
		}

#if defined(MLW_LINUX) || defined(MLW_MAC)
		// mremap can often extend in-place or relocate without copying
		void *remapped = ::mremap(old->ptr, old->size, rounded, MREMAP_MAYMOVE);
		if (remapped != MAP_FAILED)
		{
			os_table.insert(OSTable::Entry{remapped, rounded});
			return remapped;
		}
#endif
		// fallback: new alloc, copy, free old
		l.unlock();
		void *fresh = osAlloc(new_size);
		if (!fresh)
		{
			// OOM — restore the old entry so the allocation isn't leaked
			sync::Lock l2{os_lock};
			os_table.insert(OSTable::Entry{old->ptr, old->size});
			return nullptr;
		}
		mlwMemcpy(fresh, old->ptr, old->size);
#if defined(MLW_WINDOWS)
		::VirtualFree(old->ptr, 0, MEM_RELEASE);
#elif defined(MLW_LINUX) || defined(MLW_MAC)
		::munmap(old->ptr, old->size);
#endif
		return fresh;
	}

	// --- small allocs: always alloc + copy + free (no in-place growth) ---
	if (entry.type != RegionTable::Entry::Type::Medium)
	{
		usize old_size = static_cast<usize>(entry.type); // cell size == type value
		if (new_size <= old_size)
			return ptr;

		void *fresh = alignAlloc(new_size, 8);
		if (!fresh)
			return nullptr;
		mlwMemcpy(fresh, ptr, old_size);
		free(ptr);
		return fresh;
	}

	// --- medium allocs: try in-place growth ---
	Header *header = static_cast<Header *>(ptr) - 1;
	usize old_usable = header->next_off - sizeof(Header);

	if (new_size <= old_usable)
		return ptr;

	// skip in-place attempt if cross-thread or result would exceed medium range
	if (new_size >= MAX_SIZE || entry.ptr->owning_cache != &cache)
	{
		void *fresh = alignAlloc(new_size, header->align);
		if (!fresh)
			return nullptr;
		mlwMemcpy(fresh, ptr, old_usable);
		free(ptr);
		return fresh;
	}

	// try extending into the next physical block if it's free
	Region *region = entry.ptr;
	Header *next_h = reinterpret_cast<Header *>(reinterpret_cast<char *>(header) + header->next_off);
	bool last_block = static_cast<usize>(reinterpret_cast<char *>(next_h) - reinterpret_cast<char *>(region)) >= Region::MEDIUM_BLOCK_SIZE;

	if (!last_block && next_h->align == 0)
	{
		usize combined = header->next_off + next_h->next_off;
		usize combined_usable = combined - sizeof(Header);

		if (combined_usable >= new_size)
		{
			// unlink next_h from free list
			Header *next_f = next_h->getFreePtr()->next_free_block;
			Header *prev_f = next_h->getFreePtr()->prev_free_block;
			if (prev_f)
				prev_f->getFreePtr()->next_free_block = next_f;
			else
				region->free_block = next_f;
			if (next_f)
				next_f->getFreePtr()->prev_free_block = prev_f;

			Header *next_next = reinterpret_cast<Header *>(reinterpret_cast<char *>(next_h) + next_h->next_off);
			bool next_last = static_cast<usize>(reinterpret_cast<char *>(next_next) - reinterpret_cast<char *>(region)) >= Region::MEDIUM_BLOCK_SIZE;

			usize leftover = combined_usable - new_size;
			if (leftover > MIN_SIZE + sizeof(Header))
			{
				// split: take what we need, return the rest as a new free block
				header->next_off = new_size + sizeof(Header);
				Header *split = reinterpret_cast<Header *>(reinterpret_cast<char *>(header) + header->next_off);
				split->pre_off = header->next_off;
				split->next_off = reinterpret_cast<char *>(next_next) - reinterpret_cast<char *>(split);
				split->align = 0;
				if (!next_last)
					next_next->pre_off = split->next_off;

				split->getFreePtr()->prev_free_block = nullptr;
				split->getFreePtr()->next_free_block = region->free_block;
				if (region->free_block)
					region->free_block->getFreePtr()->prev_free_block = split;
				region->free_block = split;
			}
			else
			{
				// leftover too small for a free block — absorb the whole next block
				header->next_off = combined;
				if (!next_last)
					next_next->pre_off = combined;
			}
			return ptr;
		}
	}

	// can't grow in place — alloc + copy + free
	void *fresh = alignAlloc(new_size, header->align);
	if (!fresh)
		return nullptr;
	mlwMemcpy(fresh, ptr, old_usable);
	free(ptr);
	return fresh;
}

// === RegionTable / OSTable implementation ===
// Both are sorted arrays with binary search. They grow one page at a time.
// On Windows: reserve a large virtual range upfront, commit pages incrementally.
// On Linux: mremap to extend (may relocate the array).

core::RegionTable::RegionTable()
{
#if defined(MLW_WINDOWS)
	base = static_cast<Entry *>(::VirtualAlloc(nullptr, PLATFORM_INFO.alloc_granularity, MEM_RESERVE, PAGE_READWRITE));
	if (!base)
	{
		panic_mem("failed to reserve region table memory");
	}
	void *committed = ::VirtualAlloc(base, PLATFORM_INFO.page_size, MEM_COMMIT, PAGE_READWRITE);
	if (!committed)
	{
		::VirtualFree(base, 0, MEM_RELEASE);
		base = nullptr;
		panic_mem("failed to commit region table memory");
	}
#elif defined(MLW_LINUX) || defined(MLW_MAC)
	base = static_cast<Entry *>(::mmap(nullptr, PLATFORM_INFO.page_size, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0));
	if (base == MAP_FAILED)
	{
		panic_mem("failed to mmap region table memory");
	}
#endif
	capacity = static_cast<index_t>(PLATFORM_INFO.page_size) / sizeof(Entry);
}

core::RegionTable::~RegionTable()
{
	distroy();
}

core::OSTable::OSTable()
{
#if defined(MLW_WINDOWS)
	base = static_cast<Entry *>(::VirtualAlloc(nullptr, PLATFORM_INFO.alloc_granularity, MEM_RESERVE, PAGE_READWRITE));
	if (!base)
	{
		panic_mem("failed to reserve region table memory");
	}
	void *committed = ::VirtualAlloc(base, PLATFORM_INFO.page_size, MEM_COMMIT, PAGE_READWRITE);
	if (!committed)
	{
		::VirtualFree(base, 0, MEM_RELEASE);
		base = nullptr;
		panic_mem("failed to commit region table memory");
	}
#elif defined(MLW_LINUX) || defined(MLW_MAC)
	base = static_cast<Entry *>(::mmap(nullptr, PLATFORM_INFO.page_size, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0));
	if (base == MAP_FAILED)
	{
		panic_mem("failed to mmap region table memory");
	}
#endif
	capacity = static_cast<index_t>(PLATFORM_INFO.page_size) / sizeof(Entry);
}

core::OSTable::~OSTable()
{
	distroy();
}

void core::RegionTable::distroy()
{
#if defined(MLW_WINDOWS)
	::VirtualFree(base, 0, MEM_RELEASE);
#elif defined(MLW_LINUX) || defined(MLW_MAC)
	::munmap(base, capacity * sizeof(Entry));
#endif
}

void core::OSTable::distroy()
{
#if defined(MLW_WINDOWS)
	::VirtualFree(base, 0, MEM_RELEASE);
#elif defined(MLW_LINUX) || defined(MLW_MAC)
	::munmap(base, capacity * sizeof(Entry));
#endif
}

// Binary search for the region containing ptr (range check, not exact match).
core::RegionTable::Entry *core::RegionTable::find(void *ptr) const
{
	Entry *aligned_base = reinterpret_cast<Entry *>(MLW_ASSUME_ALIGNED(base, 16));

	index_t lo = 0, hi = size - 1;
	while (lo <= hi)
	{
		index_t mid = lo + (hi - lo) / 2;
		char *entry_base = reinterpret_cast<char *>(aligned_base[mid].ptr);
		usize block_size = aligned_base[mid].type == Entry::Type::Medium
							   ? Region::MEDIUM_BLOCK_SIZE
							   : Region::SMALL_BLOCK_SIZE;

		if (ptr < entry_base)
			hi = mid - 1;
		else if (ptr >= entry_base + block_size)
			lo = mid + 1;
		else
			return &aligned_base[mid];
	}
	return nullptr;
}

core::Optional<core::OSTable::Entry> core::OSTable::findAndRemove(void *ptr)
{
	Entry *aligned_base = reinterpret_cast<Entry *>(MLW_ASSUME_ALIGNED(base, 16));

	index_t lo = 0, hi = size - 1;
	while (lo <= hi)
	{
		index_t mid = lo + (hi - lo) / 2;
		char *base_ptr = static_cast<char *>(aligned_base[mid].ptr);
		if (ptr < base_ptr)
			hi = mid - 1;
		else if (ptr >= base_ptr + aligned_base[mid].size)
			lo = mid + 1;
		else
		{
			Entry ent = aligned_base[mid];
			for (index_t i = mid; i < size - 1; i++)
			{
				aligned_base[i] = aligned_base[i + 1];
			}
			size--;
			return core::Optional<Entry>{ent};
		}
	}
	return nullptr;
}

void core::RegionTable::remove(Region *ptr)
{
	Entry *a_base = MLW_ASSUME_ALIGNED(base, 16);
	// exact match on Region* (not range check) — the Region base address is unique
	index_t lo = 0, hi = size - 1;
	while (lo <= hi)
	{
		index_t mid = lo + (hi - lo) / 2;
		if (a_base[mid].ptr == ptr)
		{
			for (index_t i = mid; i < size - 1; i++)
				a_base[i] = a_base[i + 1];
			size--;
			return;
		}
		if (a_base[mid].ptr < ptr)
			lo = mid + 1;
		else
			hi = mid - 1;
	}
	panic("GAlloc::RegionTable::remove: removed a pointer that was not in the list");
}

bool core::RegionTable::insert(Entry &&e)
{
	if (size == capacity)
		if (!grow())
			return false;

	// binary search for sorted insertion point
	index_t lo = 0, hi = size;
	while (lo < hi)
	{
		index_t mid = lo + (hi - lo) / 2;
		if (base[mid].ptr < e.ptr)
			lo = mid + 1;
		else
			hi = mid;
	}
	for (index_t i = size; i > lo; i--)
		base[i] = base[i - 1];
	base[lo] = e;
	size++;
	return true;
}

bool core::OSTable::insert(Entry &&e)
{
	if (size == capacity)
		if (!grow())
			return false;

	index_t lo = 0, hi = size;
	while (lo < hi)
	{
		index_t mid = lo + (hi - lo) / 2;
		if (base[mid].ptr < e.ptr)
			lo = mid + 1;
		else
			hi = mid;
	}
	for (index_t i = size; i > lo; i--)
		base[i] = base[i - 1];
	base[lo] = e;
	size++;
	return true;
}

bool core::RegionTable::grow()
{
#if defined(MLW_WINDOWS)
	if ((capacity * sizeof(Entry) & PLATFORM_INFO.gran_mask) == 0)
	{
		// hit a 64 KB granularity boundary — need a new reservation
		usize new_reserve = ((capacity * sizeof(Entry) >> PLATFORM_INFO.gran_shift) + 1) * PLATFORM_INFO.alloc_granularity;
		Entry *new_ptr = static_cast<Entry *>(::VirtualAlloc(nullptr, new_reserve, MEM_RESERVE, PAGE_READWRITE));
		if (!new_ptr)
			return false;
		if (!::VirtualAlloc(new_ptr, capacity * sizeof(Entry) + PLATFORM_INFO.page_size, MEM_COMMIT, PAGE_READWRITE))
		{
			::VirtualFree(new_ptr, 0, MEM_RELEASE);
			return false;
		}
		base = MLW_ASSUME_ALIGNED(base, 4096);
		new_ptr = MLW_ASSUME_ALIGNED(new_ptr, 4096);
		mlwMemcpy(new_ptr, base, size * sizeof(Entry));
		::VirtualFree(base, 0, MEM_RELEASE);
		base = new_ptr;
	}
	else
	{
		// still within the current reservation — just commit another page
		if (!::VirtualAlloc(reinterpret_cast<char *>(base) + capacity * sizeof(Entry), PLATFORM_INFO.page_size, MEM_COMMIT, PAGE_READWRITE))
			return false;
	}
	capacity += static_cast<index_t>(PLATFORM_INFO.page_size) / sizeof(Entry);
	return true;
#elif defined(MLW_LINUX) || defined(MLW_MAC)
	Entry *new_ptr = static_cast<Entry *>(::mremap(base, capacity * sizeof(Entry), capacity * sizeof(Entry) + PLATFORM_INFO.page_size, MREMAP_MAYMOVE));
	if (new_ptr == MAP_FAILED)
		return false;
	new_ptr = MLW_ASSUME_ALIGNED(new_ptr, 4096);
	base = new_ptr;
	capacity += static_cast<index_t>(PLATFORM_INFO.page_size) / sizeof(Entry);
	return true;
#endif
}

bool core::OSTable::grow()
{
#if defined(MLW_WINDOWS)
	if ((capacity * sizeof(Entry) & PLATFORM_INFO.gran_mask) == 0)
	{
		usize new_reserve = ((capacity * sizeof(Entry) >> PLATFORM_INFO.gran_shift) + 1) * PLATFORM_INFO.alloc_granularity;
		Entry *new_ptr = static_cast<Entry *>(::VirtualAlloc(nullptr, new_reserve, MEM_RESERVE, PAGE_READWRITE));
		if (!new_ptr)
			return false;
		if (!::VirtualAlloc(new_ptr, capacity * sizeof(Entry) + PLATFORM_INFO.page_size, MEM_COMMIT, PAGE_READWRITE))
		{
			::VirtualFree(new_ptr, 0, MEM_RELEASE);
			return false;
		}
		base = MLW_ASSUME_ALIGNED(base, 4096);
		new_ptr = MLW_ASSUME_ALIGNED(new_ptr, 4096);
		mlwMemcpy(new_ptr, base, size * sizeof(Entry));
		::VirtualFree(base, 0, MEM_RELEASE);
		base = new_ptr;
	}
	else
	{
		if (!::VirtualAlloc(reinterpret_cast<char *>(base) + capacity * sizeof(Entry), PLATFORM_INFO.page_size, MEM_COMMIT, PAGE_READWRITE))
			return false;
	}
	capacity += static_cast<index_t>(PLATFORM_INFO.page_size) / sizeof(Entry);
	return true;
#elif defined(MLW_LINUX) || defined(MLW_MAC)
	Entry *new_ptr = static_cast<Entry *>(::mremap(base, capacity * sizeof(Entry), capacity * sizeof(Entry) + PLATFORM_INFO.page_size, MREMAP_MAYMOVE));
	if (new_ptr == MAP_FAILED)
		return false;
	new_ptr = MLW_ASSUME_ALIGNED(new_ptr, 4096);
	base = new_ptr;
	capacity += static_cast<index_t>(PLATFORM_INFO.page_size) / sizeof(Entry);
	return true;
#endif
}

// On thread exit: drain our own remote frees, then migrate all our regions
// to the orphan pool so other threads can adopt them.
// The orphan_lock is held for the entire migration to prevent races with
// concurrent adopt attempts.
core::ThreadCache::~ThreadCache()
{
	if (this == &mlw_g_alloc.orphan_pool)
		return;
	mlw_g_alloc.drainRemoteList(*this);

	sync::Lock lock{mlw_g_alloc.orphan_lock};

	auto migrate = [&](SizeClass &sc, SizeClass &orphan_sc, RegionTable::Entry::Type size)
	{
		if (sc.active == nullptr)
			return;
		Region *tail = sc.active;
		orphan_sc.free_slabs += sc.free_slabs;
		// walk the list, transferring ownership of each region
		while (tail->next)
		{
			sync::WriteLock l{tail->migration_lock};
			tail->owning_cache = &mlw_g_alloc.orphan_pool;
			MLW_COMPILER_BARRIER();
			l.unlock();
			tail = tail->next;
		}
		sync::WriteLock l{tail->migration_lock};
		tail->owning_cache = &mlw_g_alloc.orphan_pool;
		MLW_COMPILER_BARRIER();
		l.unlock();
		// splice our list onto the front of the orphan list
		tail->next = orphan_sc.active;
		if (orphan_sc.active)
			orphan_sc.active->previous = tail;
		orphan_sc.active = sc.active;

		// free excess empty regions (keep at most 1 spare)
		tail = orphan_sc.active;
		while (orphan_sc.free_slabs >= 2 && tail)
		{
			if (tail->used_count == 0)
			{
				Region *n = tail->next;
				if (size == RegionTable::Entry::Type::Medium)
				{
					mlw_g_alloc.freeMediumRegion(tail);
				}
				else
				{
					mlw_g_alloc.freeSmallRegion(tail, size);
				}
				--orphan_sc.free_slabs;
				tail = n;
			}
			else
			{

				tail = tail->next;
			}
		}
	};

	migrate(small_8, mlw_g_alloc.orphan_pool.small_8, RegionTable::Entry::Type::S8);
	migrate(small_16, mlw_g_alloc.orphan_pool.small_16, RegionTable::Entry::Type::S16);
	migrate(small_32, mlw_g_alloc.orphan_pool.small_32, RegionTable::Entry::Type::S32);
	migrate(small_64, mlw_g_alloc.orphan_pool.small_64, RegionTable::Entry::Type::S64);
	migrate(small_128, mlw_g_alloc.orphan_pool.small_128, RegionTable::Entry::Type::S128);
	migrate(medium, mlw_g_alloc.orphan_pool.medium, RegionTable::Entry::Type::Medium);
	// any cross-thread frees that landed after our drain need to be picked up, it is slower but it is acurate
	mlw_g_alloc.orphan_pool.small_8.has_remove_free.store(true, sync::MemoryOrder::Relaxed);
	mlw_g_alloc.orphan_pool.small_16.has_remove_free.store(true, sync::MemoryOrder::Relaxed);
	mlw_g_alloc.orphan_pool.small_32.has_remove_free.store(true, sync::MemoryOrder::Relaxed);
	mlw_g_alloc.orphan_pool.small_64.has_remove_free.store(true, sync::MemoryOrder::Relaxed);
	mlw_g_alloc.orphan_pool.small_128.has_remove_free.store(true, sync::MemoryOrder::Relaxed);
	mlw_g_alloc.orphan_pool.medium.has_remove_free.store(true, sync::MemoryOrder::Relaxed);
	mlw_g_alloc.drainRemoteList(mlw_g_alloc.orphan_pool);
}
