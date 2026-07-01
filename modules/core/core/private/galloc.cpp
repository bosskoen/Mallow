#include "memory/galloc.h"

#if defined(MLW_WINDOWS)
#include <windows.h>
#elif defined(MLW_LINUX) || defined(MLW_MAC)
#include <sys/mman.h>
#include "memory.h"
#endif

thread_local core::ThreadCache thread_cache{};
core::GAlloc mlw_g_alloc{};

static char* fit_aligned(core::Header* block, usize size, usize align)
{
	char* payload = reinterpret_cast<char*>(block + 1);
	char* aligned = reinterpret_cast<char*>(
		(reinterpret_cast<uptr>(payload) + align - 1) & ~(align - 1));

	// if aligned is inside the header space we need to bump
	if (aligned < payload)
		aligned += align; // should never happen but safety

	usize total_needed = (aligned - reinterpret_cast<char*>(block)) + size;
	if (total_needed > block->next_off - sizeof(core::Header))
		return nullptr;
	return aligned;
}


void* core::GAlloc::alignAlloc(usize size, usize align)
{
	if (align & (align - 1)) return nullptr;
	if (align < 8) align = 8;
	size = (size + alignof(Header) - 1) & ~(alignof(Header) - 1);

	if (align == PAGE_SIZE) { return osAlloc(size); }// os alloc
	if (align > 256)        return nullptr;

	core::ThreadCache& cache = thread_cache;
	drainRemoteList(cache);
	if (size < align) { size = align; }

	if (size <= MIN_SIZE) {
		RegionTable::Entry::Type block_type;
		if (size <= 8) { block_type = RegionTable::Entry::Type::S8; }
		else if (size <= 16) { block_type = RegionTable::Entry::Type::S16; }
		else if (size <= 32) { block_type = RegionTable::Entry::Type::S32; }
		else if (size <= 64) { block_type = RegionTable::Entry::Type::S64; }
		else { block_type = RegionTable::Entry::Type::S128; }


		Region* current;
		ThreadCache::SizeClass& sc = cache.getSizeClass(block_type);
		current = sc.active;
		if (current == nullptr) {
			if (!allocSmallRegion(nullptr, cache, block_type)) return nullptr;
			current = sc.active;
		}

		while (current->free_block == nullptr) {
			if (current->next == nullptr)
				if (!allocSmallRegion(current, cache, block_type)) return nullptr;
			current = current->next;
		}

		void* block = reinterpret_cast<void*>(current->free_block);
		current->free_block = *reinterpret_cast<Header**>(block);
		++current->used_count;
		return block;
	}
	else if (size >= MAX_SIZE) { return osAlloc(size); }
	else
	{
		// --- find a region with a free block ---

		Region* current = cache.medium.active;
		if (current == nullptr) {
			if (!allocMediumRegion(current, cache)) return nullptr;
			current = cache.medium.active;
		}
		while (true)
		{
			if (current->free_block != nullptr) break;

			if (current->next == nullptr)
				if (!allocMediumRegion(current, cache)) return nullptr;
			current = current->next;
		}

		// --- find a block that fits ---
		Header* block = current->free_block;
		char* aligned_payload = nullptr;
		while ((aligned_payload = fit_aligned(block, size, align)) == nullptr)
		{
			if (block->getFreePtr()->next_free_block != nullptr)
			{
				block = block->getFreePtr()->next_free_block;
				continue;
			}

			do
			{
				if (current->next == nullptr)
					if (!allocMediumRegion(current, cache)) return nullptr;
				current = current->next;
				block = current->free_block;
				if (block != nullptr) break;
			} while (true);
		}

		// --- compute all pointers and sizes once ---
		//][header][padding][new_header][aligned_payload][middle_h][after_padding][next_h]

		Header* new_header = reinterpret_cast<Header*>(aligned_payload) - 1;
		Header* middle_h = reinterpret_cast<Header*>(aligned_payload + size);
		Header* next_h = reinterpret_cast<Header*>(reinterpret_cast<char*>(block) + block->next_off);
		Header* prev_h = block->pre_off != 0
			? reinterpret_cast<Header*>(reinterpret_cast<char*>(block) - block->pre_off)
			: nullptr;

		Header* prev_f = block->getFreePtr()->prev_free_block;
		Header* next_f = block->getFreePtr()->next_free_block;

		usize padding = reinterpret_cast<char*>(new_header) - reinterpret_cast<char*>(block + 1);
		usize after_padding = reinterpret_cast<char*>(next_h) - reinterpret_cast<char*>(middle_h);

		bool keep_front = padding > MIN_SIZE + sizeof(Header);
		bool keep_back = after_padding > MIN_SIZE + sizeof(Header);
		bool last_block = static_cast<usize>(reinterpret_cast<char*>(next_h) - reinterpret_cast<char*>(current)) >= Region::MEDIUM_BLOCK_SIZE;

		// --- case 1: keep back as free block, no front split ---
		if (!keep_front && keep_back)
		{
			// replace block with middle_h in free list
			if (prev_f) prev_f->getFreePtr()->next_free_block = middle_h;
			else        current->free_block = middle_h;
			if (next_f) next_f->getFreePtr()->prev_free_block = middle_h;
			middle_h->getFreePtr()->prev_free_block = prev_f;
			middle_h->getFreePtr()->next_free_block = next_f;

			// middle_h offsets
			usize mid_to_next = reinterpret_cast<char*>(next_h) - reinterpret_cast<char*>(middle_h);
			middle_h->pre_off = reinterpret_cast<char*>(middle_h) - reinterpret_cast<char*>(new_header);
			middle_h->next_off = mid_to_next;
			if (!last_block) next_h->pre_off = mid_to_next;
			middle_h->align = 0;

			// move header to aligned position
			mlwMemset(block, 0, sizeof(Header));
			new_header->pre_off = prev_h ? reinterpret_cast<char*>(new_header) - reinterpret_cast<char*>(prev_h) : 0;
			if (prev_h) prev_h->next_off = new_header->pre_off;
			new_header->next_off = middle_h->pre_off;
			new_header->align = align;
		}

		// --- case 2: give whole block, no split ---
		else if (!keep_front && !keep_back)
		{
			// unlink block from free list
			if (prev_f) prev_f->getFreePtr()->next_free_block = next_f;
			else        current->free_block = next_f;
			if (next_f) next_f->getFreePtr()->prev_free_block = prev_f;

			// move header to aligned position
			usize new_next_off = reinterpret_cast<char*>(next_h) - reinterpret_cast<char*>(new_header);
			mlwMemset(block, 0, sizeof(Header));
			new_header->pre_off = prev_h ? reinterpret_cast<char*>(new_header) - reinterpret_cast<char*>(prev_h) : 0;
			if (prev_h) prev_h->next_off = new_header->pre_off;
			new_header->next_off = new_next_off;
			if (!last_block) next_h->pre_off = new_next_off;

			new_header->align = align;
		}
		// --- case 3: keep both front and back as free blocks ---
		else if (keep_front && keep_back)
		{
			// block stays in free list as front fragment, middle_h inserted after it
			usize block_to_new = reinterpret_cast<char*>(new_header) - reinterpret_cast<char*>(block);
			block->next_off = block_to_new;
			block->getFreePtr()->next_free_block = middle_h;

			middle_h->getFreePtr()->prev_free_block = block;
			middle_h->getFreePtr()->next_free_block = next_f;
			if (next_f) next_f->getFreePtr()->prev_free_block = middle_h;

			usize mid_to_next = reinterpret_cast<char*>(next_h) - reinterpret_cast<char*>(middle_h);
			middle_h->pre_off = reinterpret_cast<char*>(middle_h) - reinterpret_cast<char*>(new_header);
			middle_h->next_off = mid_to_next;
			if (!last_block) next_h->pre_off = mid_to_next;
			middle_h->align = 0;

			new_header->pre_off = block_to_new;
			new_header->next_off = middle_h->pre_off;
			new_header->align = align;
		}
		// --- case 4: keep front as free block, no back split ---
		else   /*(keep_front && !keep_back) */
		{
			// block stays in free list as the front fragment, shrink it
			usize block_to_new = reinterpret_cast<char*>(new_header) - reinterpret_cast<char*>(block);
			block->next_off = block_to_new;

			// new_header offsets
			usize new_to_next = reinterpret_cast<char*>(next_h) - reinterpret_cast<char*>(new_header);
			new_header->pre_off = block_to_new;
			new_header->next_off = new_to_next;
			if (!last_block) next_h->pre_off = new_to_next;
			new_header->align = align;
		}
		++current->used_count;
		return reinterpret_cast<void*>(new_header + 1);
	}
}

bool core::GAlloc::allocMediumRegion(core::Region* last_region, core::ThreadCache& cache)
{

	{
		sync::Lock lock{ mlw_g_alloc.orphan_lock };
		drainRemoteList(orphan_pool);
		ThreadCache::SizeClass& sc = orphan_pool.getSizeClass(RegionTable::Entry::Type::Medium);
		if (sc.active != nullptr) {
			Region* r = sc.active;
			sc.active = r->next;
			if (r->next) r->next->previous = nullptr;
			r->next = nullptr;
			r->previous = nullptr;
			sync::WriteLock l{ r->migration_lock };
			r->owning_cache = &cache;
			l.unlock();
			// adopt into calling cache
			// append to last_region or set as active
			if (!last_region) {
				cache.getSizeClass(RegionTable::Entry::Type::Medium).active = r;
			}
			else {
				last_region->next = r;
				r->previous = last_region;
			}
			return true; // no mmap needed
		}

	}

	Region* new_reg;
#if defined(MLW_WINDOWS)
	new_reg = static_cast<Region*>(::VirtualAlloc(nullptr, Region::MEDIUM_BLOCK_SIZE, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
	if (!new_reg)
	{
		MLW_DEBUG_PRINT("failed to virtual alloc a new region in GAlloc");
		return false;
	}
#elif defined(MLW_LINUX) || defined(MLW_MAC)
	new_reg = static_cast<Region*>(::mmap(nullptr, Region::MEDIUM_BLOCK_SIZE, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0));
	if (new_reg == MAP_FAILED)
	{
		MLW_DEBUG_PRINT("failed to virtual alloc a new region in GAlloc");
		return false;
	}
#endif
	new_reg->next = nullptr;
	new (&new_reg->remote_free) sync::Atomic<void*>{ nullptr };
	new (&new_reg->migration_lock) sync::RWLock{};
	new_reg->used_count = 0;
	new_reg->owning_cache = &cache;
	new_reg->free_block = reinterpret_cast<Header*>(new_reg + 1);

	new_reg->free_block->align = 0;
	new_reg->free_block->pre_off = 0;
	new_reg->free_block->next_off = Region::MEDIUM_BLOCK_SIZE - sizeof(Region) - sizeof(Header);
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

	sync::WriteLock lock{ region_list_lock };
	region_table.insert(RegionTable::Entry{ new_reg, RegionTable::Entry::Type::Medium });

	return true;
}

void core::GAlloc::freeMediumRegion(core::Region* region)
{
	Optional<sync::Lock<sync::spin_lock::MCS>> l = nullptr;
	sync::Lock<sync::spin_lock::MCS>::tryLock(orphan_lock, l);

	if (l.isSome()) {
		drainRemoteList(orphan_pool);
		l->unlock();
	}

	Region* next = region->next;
	Region* prev = region->previous;
	ThreadCache* cache = region->owning_cache;
	if (next) {
		next->previous = prev;
	}
	if (prev) {
		prev->next = next;
	}
	else {
		cache->medium.active = next;
	}
	{
		sync::WriteLock lock{ region_list_lock };
		region_table.remove(region);
	}
#if defined(MLW_WINDOWS)
	::VirtualFree(region, 0, MEM_RELEASE);
#elif defined(MLW_LINUX) || defined(MLW_MAC)
	::munmap(region, Region::MEDIUM_BLOCK_SIZE);
#endif
}

bool core::GAlloc::freeMedium(void* ptr, core::Region* region)
{
	Header* header = static_cast<Header*>(ptr) - 1;
	mlw_debug_assert_msg(header->align != 0, "freed a free block");

	Header* next_header = reinterpret_cast<Header*>(
		reinterpret_cast<char*>(header) + header->next_off);
	bool last_block = static_cast<usize>(
		reinterpret_cast<char*>(next_header) - reinterpret_cast<char*>(region))
		>= Region::MEDIUM_BLOCK_SIZE;

	Header* prev_header = header->pre_off != 0
		? reinterpret_cast<Header*>(reinterpret_cast<char*>(header) - header->pre_off)
		: nullptr;

	bool merged_backward = false;

	// === BACKWARD ===
	if (prev_header != nullptr && prev_header->align == 0) {
		// prev is free and already in the free list — just extend it
		merged_backward = true;
		prev_header->next_off =
			reinterpret_cast<char*>(next_header) - reinterpret_cast<char*>(prev_header);
		if (!last_block) next_header->pre_off = prev_header->next_off;
		header = prev_header;
	}
	else if (header->pre_off == 0
		&& reinterpret_cast<Header*>(region + 1) != header) {
		// dead padding before us — reclaim by moving header to region start
		Header* reclaimed = reinterpret_cast<Header*>(region + 1);
		reclaimed->pre_off = 0;
		reclaimed->next_off =
			reinterpret_cast<char*>(next_header) - reinterpret_cast<char*>(reclaimed);
		if (!last_block) next_header->pre_off = reclaimed->next_off;
		header = reclaimed;
	}

	// === FORWARD ===
	if (!last_block && next_header->align == 0) {
		// next is free — absorb it
		Header* next_next = reinterpret_cast<Header*>(
			reinterpret_cast<char*>(next_header) + next_header->next_off);
		bool next_last = static_cast<usize>(
			reinterpret_cast<char*>(next_next) - reinterpret_cast<char*>(region))
			>= Region::MEDIUM_BLOCK_SIZE;

		header->next_off =
			reinterpret_cast<char*>(next_next) - reinterpret_cast<char*>(header);
		if (!next_last) next_next->pre_off = header->next_off;

		// free list: next_header is currently linked
		Header* next_f = next_header->getFreePtr()->next_free_block;
		Header* prev_f = next_header->getFreePtr()->prev_free_block;

		if (merged_backward) {
			// header (=prev) is already in the list — just unlink next_header
			if (prev_f) prev_f->getFreePtr()->next_free_block = next_f;
			else        region->free_block = next_f;
			if (next_f) next_f->getFreePtr()->prev_free_block = prev_f;
		}
		else {
			// header is NOT in the list — take next_header's slot
			header->getFreePtr()->prev_free_block = prev_f;
			header->getFreePtr()->next_free_block = next_f;
			if (prev_f) prev_f->getFreePtr()->next_free_block = header;
			else        region->free_block = header;
			if (next_f) next_f->getFreePtr()->prev_free_block = header;
		}
	}
	else if (!merged_backward) {
		// no forward merge, no backward merge — insert at head
		header->getFreePtr()->next_free_block = region->free_block;
		header->getFreePtr()->prev_free_block = nullptr;
		if (region->free_block)
			region->free_block->getFreePtr()->prev_free_block = header;
		region->free_block = header;
	}
	// else: merged_backward + no forward = prev already in list, nothing to do

	header->align = 0;

	// --- bookkeeping ---
	ThreadCache* cache = region->owning_cache;
	--region->used_count;
	if (region->used_count == 0) ++cache->medium.free_slabs;
	if (cache->medium.free_slabs >= 2) {
		freeMediumRegion(region);
		--cache->medium.free_slabs;
		return true;
	}
	return false;
}

void core::GAlloc::freeSmall(void* ptr, core::Region* region, RegionTable::Entry::Type size)
{
	*reinterpret_cast<void**>(ptr) = reinterpret_cast<void*>(region->free_block);
	region->free_block = reinterpret_cast<Header*>(ptr);

	ThreadCache* cache = region->owning_cache;
	--region->used_count;
	ThreadCache::SizeClass& sc = cache->getSizeClass(size);
	if (region->used_count == 0) ++sc.free_slabs;
	if (sc.free_slabs >= 2) {
		freeSmallRegion(region, size);
		--sc.free_slabs;
	}

}

void core::GAlloc::freeSmallRegion(core::Region* region, RegionTable::Entry::Type size)
{
	mlw_debug_assert_msg(size != RegionTable::Entry::Type::Medium, "tryed to free a medium region in the small path");

	Optional<sync::Lock<sync::spin_lock::MCS>> l = nullptr;
	sync::Lock<sync::spin_lock::MCS>::tryLock(orphan_lock, l);

	if (l.isSome()) {
		drainRemoteList(orphan_pool);
		l->unlock();
	}

	Region* next = region->next;
	Region* prev = region->previous;
	ThreadCache* cache = region->owning_cache;
	if (next) {
		next->previous = prev;
	}
	if (prev) {
		prev->next = next;
	}
	else {
		cache->getSizeClass(size).active = next;
	}
	{
		sync::WriteLock lock{ region_list_lock };
		region_table.remove(region);
	}
#if defined(MLW_WINDOWS)
	::VirtualFree(region, 0, MEM_RELEASE);
#elif defined(MLW_LINUX) || defined(MLW_MAC)
	::munmap(region, Region::SMALL_BLOCK_SIZE);
#endif
}

bool core::GAlloc::allocSmallRegion(core::Region* last_region, core::ThreadCache& cache, RegionTable::Entry::Type size)
{
	mlw_debug_assert_msg(size != RegionTable::Entry::Type::Medium, "core::GAlloc::allocSmallRegion tryed to alloc a medium region");

	{
		sync::Lock lock{ mlw_g_alloc.orphan_lock };
		drainRemoteList(orphan_pool);
		ThreadCache::SizeClass& sc = orphan_pool.getSizeClass(size);
		if (sc.active != nullptr) {
			Region* r = sc.active;
			sc.active = r->next;
			if (r->next) r->next->previous = nullptr;
			r->next = nullptr;
			r->previous = nullptr;
			sync::WriteLock l{ r->migration_lock };
			r->owning_cache = &cache;
			l.unlock();
			// adopt into calling cache
			// append to last_region or set as active
			if (!last_region) {
				cache.getSizeClass(size).active = r;
			}
			else {
				last_region->next = r;
				r->previous = last_region;
			}
			return true; // no mmap needed
		}

	}

	Region* new_reg;
#if defined(MLW_WINDOWS)
	new_reg = static_cast<Region*>(::VirtualAlloc(nullptr, Region::SMALL_BLOCK_SIZE, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
	if (!new_reg)
	{
		MLW_DEBUG_PRINT("failed to virtual alloc a new region in GAlloc");
		return false;
	}
#elif defined(MLW_LINUX) || defined(MLW_MAC)
	new_reg = static_cast<Region*>(::mmap(nullptr, Region::SMALL_BLOCK_SIZE, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0));
	if (new_reg == MAP_FAILED)
	{
		MLW_DEBUG_PRINT("failed to virtual alloc a new region in GAlloc");
		return false;
	}
#endif

	usize block_size = static_cast<usize>(size);

	new_reg->next = nullptr;
	new (&new_reg->remote_free) sync::Atomic<void*>{ nullptr };
	new (&new_reg->migration_lock) sync::spin_lock::MCS{};
	new_reg->used_count = 0;
	new_reg->owning_cache = &cache;

	new_reg->free_block = reinterpret_cast<Header*>((reinterpret_cast<uptr>(new_reg + 1) + block_size - 1) & ~(block_size - 1)); // this is not a header pointer bit a void pointer becous of reuse of the reagion struct

	char* current = reinterpret_cast<char*>(new_reg->free_block);
	char* end = reinterpret_cast<char*>(new_reg) + Region::SMALL_BLOCK_SIZE;
	while (current + block_size < end) {
		char* next = current + block_size;
		*reinterpret_cast<void**>(current) = next;
		current = next;
	}
	*reinterpret_cast<void**>(current) = nullptr;

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

	sync::WriteLock lock{ region_list_lock };
	region_table.insert(RegionTable::Entry{ new_reg, size });

	return true;
}

void* core::GAlloc::osAlloc(usize size)
{
	size = (size + PAGE_MASK) & ~(PAGE_MASK);
#if defined(MLW_WINDOWS)
	void* ptr = ::VirtualAlloc(nullptr, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
	if (!ptr) return nullptr;
#elif defined(MLW_LINUX) || defined(MLW_MAC)
	void* ptr = ::mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
	if (ptr == MAP_FAILED) return nullptr;
#endif

	sync::Lock l{ os_lock };
	if (!os_table.insert(OSTable::Entry{ ptr, size })) {
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

void core::GAlloc::osFree(void* ptr)
{

	sync::Lock l{ os_lock };

	Optional<OSTable::Entry> e = os_table.findAndRemove(ptr);
	l.unlock();
	if (e.isNone()) {
		MLW_DEBUG_PRINT("tryed freeing a os poiter taht doesnt exsist");
		return;
	}


#if defined(MLW_WINDOWS)
	::VirtualFree(e->ptr, 0, MEM_RELEASE);
#elif defined(MLW_LINUX) || defined(MLW_MAC)
	::munmap(e->ptr, e->size);
#endif

}

void core::GAlloc::drainRemoteList(ThreadCache& cache)
{
	auto drainSmall = [&](ThreadCache::SizeClass& sc, RegionTable::Entry::Type type) {
		if (!sc.has_remove_free) return;
		sc.has_remove_free.store(false, sync::MemoryOrder::Relaxed);
		Region* current = sc.active;
		while (current) {
			Region* next = current->next;
			void* head = current->remote_free.exchange(nullptr, sync::MemoryOrder::AcqRel);
			if (head) {
				usize count = 1;
				void* tail = head;
				while (*reinterpret_cast<void**>(tail)) {
					tail = *reinterpret_cast<void**>(tail);
					++count;
				}
				*reinterpret_cast<void**>(tail) = reinterpret_cast<void*>(current->free_block);
				current->free_block = reinterpret_cast<Header*>(head);
				current->used_count -= static_cast<uint32>(count);
				if (current->used_count == 0) ++sc.free_slabs;
				if (sc.free_slabs >= 2) {
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
	if (cache.medium.has_remove_free) {
		cache.medium.has_remove_free.store(false, sync::MemoryOrder::Relaxed);
		Region* current = cache.medium.active;
		while (current) {
			Region* next = current->next; //region can be deleted
			void* block = current->remote_free.exchange(nullptr, sync::MemoryOrder::AcqRel);
			while (block) {
				void* next_block = *static_cast<void**>(block);
				bool region_deleted = freeMedium(block, current);
				if (region_deleted) break;
				block = next_block;
			}
			current = next;
		}
	}
}

void core::GAlloc::free(void* ptr) {
	if (ptr == nullptr) return;
	ThreadCache& cache = thread_cache;

	drainRemoteList(cache);

	RegionTable::Entry entry;
	bool os_region;
	{
		sync::ReadLock l{ region_list_lock };
		RegionTable::Entry* e = region_table.find(ptr);
		if (e == nullptr) {
			os_region = true;
		}
		else {
			os_region = false;
			entry = *e;
		}
	}
	if (os_region) {
		osFree(ptr);
		return;
	}

	if (entry.ptr->owning_cache == &cache) {
		if (entry.type == RegionTable::Entry::Type::Medium) {
			freeMedium(ptr, entry.ptr);
		}
		else {
			freeSmall(ptr, entry.ptr, entry.type);
		}
	}
	else {

		{
			sync::ReadLock lock{ entry.ptr->migration_lock };
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

		void** next_slot = static_cast<void**>(ptr);

		void* expected = entry.ptr->remote_free.load(sync::MemoryOrder::Acquire);
		do {
			*next_slot = expected;
		} while (!entry.ptr->remote_free.compareExchangeWeak(
			expected,           // updated on failure with current value
			ptr,                // new head = this block
			sync::MemoryOrder::Release,
			sync::MemoryOrder::Acquire
		));
	}
}

void* core::GAlloc::realloc(void* ptr, usize new_size)
{
    if (ptr == nullptr) return alignAlloc(new_size, 8);
    if (new_size == 0) { free(ptr); return nullptr; }

    new_size = (new_size + alignof(Header) - 1) & ~(alignof(Header) - 1);

    ThreadCache& cache = thread_cache;

    RegionTable::Entry entry;
    bool os_region;
    {
        sync::ReadLock l{ region_list_lock };
        RegionTable::Entry* e = region_table.find(ptr);
        os_region = (e == nullptr);
        if (!os_region) entry = *e;
    }

    // --- OS allocs ---
    if (os_region) {
        usize rounded = (new_size + PAGE_MASK) & ~PAGE_MASK;

        sync::Lock l{ os_lock };
        Optional<OSTable::Entry> old = os_table.findAndRemove(ptr);
        if (old.isNone()) return nullptr;

        if (rounded <= old->size) {
            os_table.insert(OSTable::Entry{ old->ptr, old->size });
            return ptr;
        }

#if defined(MLW_LINUX) || defined(MLW_MAC)
        void* remapped = ::mremap(old->ptr, old->size, rounded, MREMAP_MAYMOVE);
        if (remapped != MAP_FAILED) {
            os_table.insert(OSTable::Entry{ remapped, rounded });
            return remapped;
        }
#endif
        // fallback: new alloc, copy, free old
        l.unlock();
        void* fresh = osAlloc(new_size);
        if (!fresh) {
            // put the old entry back
            sync::Lock l2{ os_lock };
            os_table.insert(OSTable::Entry{ old->ptr, old->size });
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

    // --- small allocs: always alloc + copy + free ---
    if (entry.type != RegionTable::Entry::Type::Medium) {
        usize old_size = static_cast<usize>(entry.type);
        if (new_size <= old_size) return ptr;

        void* fresh = alignAlloc(new_size, 8);
        if (!fresh) return nullptr;
        mlwMemcpy(fresh, ptr, old_size);
        free(ptr);
        return fresh;
    }

    // --- medium allocs ---
    Header* header = static_cast<Header*>(ptr) - 1;
    usize old_usable = header->next_off - sizeof(Header);

    if (new_size <= old_usable) return ptr;

    // don't try in-place extend if cross-thread or growing beyond medium
    if (new_size >= MAX_SIZE || entry.ptr->owning_cache != &cache) {
        void* fresh = alignAlloc(new_size, header->align);
        if (!fresh) return nullptr;
        mlwMemcpy(fresh, ptr, old_usable);
        free(ptr);
        return fresh;
    }

    // try extending into next physical block if it's free
    Region* region = entry.ptr;
    Header* next_h = reinterpret_cast<Header*>(reinterpret_cast<char*>(header) + header->next_off);
    bool last_block = static_cast<usize>(reinterpret_cast<char*>(next_h) - reinterpret_cast<char*>(region)) >= Region::MEDIUM_BLOCK_SIZE;

    if (!last_block && next_h->align == 0) {
        usize combined = header->next_off + next_h->next_off;
        usize combined_usable = combined - sizeof(Header);

        if (combined_usable >= new_size) {
            // unlink next_h from free list
            Header* next_f = next_h->getFreePtr()->next_free_block;
            Header* prev_f = next_h->getFreePtr()->prev_free_block;
            if (prev_f) prev_f->getFreePtr()->next_free_block = next_f;
            else        region->free_block = next_f;
            if (next_f) next_f->getFreePtr()->prev_free_block = prev_f;

            Header* next_next = reinterpret_cast<Header*>(reinterpret_cast<char*>(next_h) + next_h->next_off);
            bool next_last = static_cast<usize>(reinterpret_cast<char*>(next_next) - reinterpret_cast<char*>(region)) >= Region::MEDIUM_BLOCK_SIZE;

            usize leftover = combined_usable - new_size;
            if (leftover > MIN_SIZE + sizeof(Header)) {
                // split: take what we need, return the rest
                header->next_off = new_size + sizeof(Header);
                Header* split = reinterpret_cast<Header*>(reinterpret_cast<char*>(header) + header->next_off);
                split->pre_off = header->next_off;
                split->next_off = reinterpret_cast<char*>(next_next) - reinterpret_cast<char*>(split);
                split->align = 0;
                if (!next_last) next_next->pre_off = split->next_off;

                split->getFreePtr()->prev_free_block = nullptr;
                split->getFreePtr()->next_free_block = region->free_block;
                if (region->free_block) region->free_block->getFreePtr()->prev_free_block = split;
                region->free_block = split;
            }
            else {
                // absorb the whole next block
                header->next_off = combined;
                if (!next_last) next_next->pre_off = combined;
            }
            return ptr;
        }
    }

    // fallback: alloc + copy + free
    void* fresh = alignAlloc(new_size, header->align);
    if (!fresh) return nullptr;
    mlwMemcpy(fresh, ptr, old_usable);
    free(ptr);
    return fresh;
}


core::RegionTable::RegionTable()
{
#if defined(MLW_WINDOWS)
	base = static_cast<Entry*>(::VirtualAlloc(nullptr, ALLOC_GRANULARITY, MEM_RESERVE, PAGE_READWRITE));
	if (!base) {
		panic("failed to reserve region table memory");
	}
	void* committed = ::VirtualAlloc(base, PAGE_SIZE, MEM_COMMIT, PAGE_READWRITE);
	if (!committed) {
		::VirtualFree(base, 0, MEM_RELEASE);
		base = nullptr;
		panic("failed to reserve region table memory");

	}
#elif defined(MLW_LINUX) || defined(MLW_MAC)
	base = static_cast<Entry*>(::mmap(nullptr, PAGE_SIZE, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0));
	if (base == MAP_FAILED) { panic("failed to mmap region table memory"); }
#endif
	capacity = static_cast<index_t>(PAGE_SIZE) / sizeof(Entry);
}

core::RegionTable::~RegionTable()
{
	distroy();
}

core::OSTable::OSTable()
{
#if defined(MLW_WINDOWS)
	base = static_cast<Entry*>(::VirtualAlloc(nullptr, ALLOC_GRANULARITY, MEM_RESERVE, PAGE_READWRITE));
	if (!base) {
		panic("failed to reserve region table memory");
	}
	void* committed = ::VirtualAlloc(base, PAGE_SIZE, MEM_COMMIT, PAGE_READWRITE);
	if (!committed) {
		::VirtualFree(base, 0, MEM_RELEASE);
		base = nullptr;
		panic("failed to reserve region table memory");

	}
#elif defined(MLW_LINUX) || defined(MLW_MAC)
	base = static_cast<Entry*>(::mmap(nullptr, PAGE_SIZE, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0));
	if (base == MAP_FAILED) { panic("failed to mmap region table memory"); }
#endif
	capacity = static_cast<index_t>(PAGE_SIZE) / sizeof(Entry);
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


core::RegionTable::Entry* core::RegionTable::find(void* ptr) const
{
	Entry* aligned_base = reinterpret_cast<Entry*>(MLW_ASSUME_ALIGNED(base, 16));

	index_t lo = 0, hi = size - 1;
	while (lo <= hi) {
		index_t mid = lo + (hi - lo) / 2;
		char* entry_base = reinterpret_cast<char*>(aligned_base[mid].ptr);
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

core::Optional<core::OSTable::Entry> core::OSTable::findAndRemove(void* ptr)
{
	Entry* aligned_base = reinterpret_cast<Entry*>(MLW_ASSUME_ALIGNED(base, 16));

	index_t lo = 0, hi = size - 1;
	while (lo <= hi) {
		index_t mid = lo + (hi - lo) / 2;
		char* base_ptr = static_cast<char*>(aligned_base[mid].ptr);
		if (ptr < base_ptr)
			hi = mid - 1;
		else if (ptr >= base_ptr + aligned_base[mid].size)
			lo = mid + 1;
		else {
			Entry ent = aligned_base[mid];
			for (index_t i = mid; i < size - 1; i++) {
				aligned_base[i] = aligned_base[i + 1];
			}
			size--;
			return core::Optional<Entry>{ ent };
		}
	}
	return nullptr;
}

void core::RegionTable::remove(Region* ptr)
{
	Entry* a_base = MLW_ASSUME_ALIGNED(base, 16);
	// binary search for exact match
	// no need to check for size checking for exact match
	index_t lo = 0, hi = size - 1;
	while (lo <= hi) {
		index_t mid = lo + (hi - lo) / 2;
		if (a_base[mid].ptr == ptr) {
			// shift everything from mid+1 onwards left by one
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
	// not found should never happen, could assert here
	panic("GAlloc::RegionTable::remove: removed a pointer that was not in the list");
}


bool core::RegionTable::insert(Entry&& e)
{
	if (size == capacity)
		if (!grow()) return false;

	// binary search for insertion point
	// no need to check for size no way they over lap
	index_t lo = 0, hi = size;
	while (lo < hi) {
		index_t mid = lo + (hi - lo) / 2;
		if (base[mid].ptr < e.ptr)
			lo = mid + 1;
		else
			hi = mid;
	}
	// lo is now the insertion index
	// shift everything from lo onwards right by one
	for (index_t i = size; i > lo; i--)
		base[i] = base[i - 1];
	base[lo] = e;
	size++;
	return true;
}

bool core::OSTable::insert(Entry&& e)
{
	if (size == capacity)
		if (!grow()) return false;

	// binary search for insertion point
	// no need to check for size no way they over lap
	index_t lo = 0, hi = size;
	while (lo < hi) {
		index_t mid = lo + (hi - lo) / 2;
		if (base[mid].ptr < e.ptr)
			lo = mid + 1;
		else
			hi = mid;
	}
	// lo is now the insertion index
	// shift everything from lo onwards right by one
	for (index_t i = size; i > lo; i--)
		base[i] = base[i - 1];
	base[lo] = e;
	size++;
	return true;
}

bool core::RegionTable::grow()
{
#if defined(MLW_WINDOWS)
	if ((capacity * sizeof(Entry) & GRAN_MASK) == 0)
	{
		// at granularity boundary, need new reservation
		usize new_reserve = ((capacity * sizeof(Entry) >> GRAN_SHIFT) + 1) * ALLOC_GRANULARITY;
		Entry* new_ptr = static_cast<Entry*>(::VirtualAlloc(nullptr, new_reserve, MEM_RESERVE, PAGE_READWRITE));
		if (!new_ptr) return false;
		if (!::VirtualAlloc(new_ptr, capacity * sizeof(Entry) + PAGE_SIZE, MEM_COMMIT, PAGE_READWRITE)) {
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
		// within reservation, just commit another page
		if (!::VirtualAlloc(reinterpret_cast<char*>(base) + capacity * sizeof(Entry), PAGE_SIZE, MEM_COMMIT, PAGE_READWRITE))
			return false;
	}
	capacity += static_cast<index_t>(PAGE_SIZE) / sizeof(Entry);
	return true;
#elif defined(MLW_LINUX) || defined(MLW_MAC)
	Entry* new_ptr = static_cast<Entry*>(::mremap(base, capacity * sizeof(Entry), capacity * sizeof(Entry) + PAGE_SIZE, MREMAP_MAYMOVE));
	if (new_ptr == MAP_FAILED) return false;
	new_ptr = MLW_ASSUME_ALIGNED(new_ptr, 4096);
	base = new_ptr;
	capacity += static_cast<index_t>(PAGE_SIZE) / sizeof(Entry);
	return true;
#endif
}

bool core::OSTable::grow()
{
#if defined(MLW_WINDOWS)
	if ((capacity * sizeof(Entry) & GRAN_MASK) == 0)
	{
		// at granularity boundary, need new reservation
		usize new_reserve = ((capacity * sizeof(Entry) >> GRAN_SHIFT) + 1) * ALLOC_GRANULARITY;
		Entry* new_ptr = static_cast<Entry*>(::VirtualAlloc(nullptr, new_reserve, MEM_RESERVE, PAGE_READWRITE));
		if (!new_ptr) return false;
		if (!::VirtualAlloc(new_ptr, capacity * sizeof(Entry) + PAGE_SIZE, MEM_COMMIT, PAGE_READWRITE)) {
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
		// within reservation, just commit another page
		if (!::VirtualAlloc(reinterpret_cast<char*>(base) + capacity * sizeof(Entry), PAGE_SIZE, MEM_COMMIT, PAGE_READWRITE))
			return false;
	}
	capacity += static_cast<index_t>(PAGE_SIZE) / sizeof(Entry);
	return true;
#elif defined(MLW_LINUX) || defined(MLW_MAC)
	Entry* new_ptr = static_cast<Entry*>(::mremap(base, capacity * sizeof(Entry), capacity * sizeof(Entry) + PAGE_SIZE, MREMAP_MAYMOVE));
	if (new_ptr == MAP_FAILED) return false;
	new_ptr = MLW_ASSUME_ALIGNED(new_ptr, 4096);
	base = new_ptr;
	capacity += static_cast<index_t>(PAGE_SIZE) / sizeof(Entry);
	return true;
#endif
}


core::ThreadCache::~ThreadCache()
{
	if (this == &mlw_g_alloc.orphan_pool) return;
	mlw_g_alloc.drainRemoteList(*this);

	sync::Lock lock{ mlw_g_alloc.orphan_lock };

	auto migrate = [&](SizeClass& sc, SizeClass& orphan_sc, RegionTable::Entry::Type size) {
		if (sc.active == nullptr) return;
		// find tail of our list
		Region* tail = sc.active;
		orphan_sc.free_slabs += sc.free_slabs;
		while (tail->next)
		{
			sync::WriteLock l{ tail->migration_lock };
			tail->owning_cache = &mlw_g_alloc.orphan_pool;
			MLW_COMPILER_BARRIER();
			//if (tail->remote_free != nullptr) orphan_sc.has_remove_free.store(true, sync::MemoryOrder::Relaxed);
			l.unlock();
			tail = tail->next;
		}
		sync::WriteLock l{ tail->migration_lock };
		tail->owning_cache = &mlw_g_alloc.orphan_pool;
		MLW_COMPILER_BARRIER();
		l.unlock();
		// append to orphan list
		tail->next = orphan_sc.active;
		if (orphan_sc.active) orphan_sc.active->previous = tail;
		orphan_sc.active = sc.active;
		//if (sc.has_remove_free.load(sync::MemoryOrder::Relaxed)) orphan_sc.has_remove_free.store(true, sync::MemoryOrder::Relaxed);

		tail = orphan_sc.active;
		while (orphan_sc.free_slabs >= 2 && tail) {
			if (tail->used_count == 0) {
				Region* n = tail->next;
				if (size == RegionTable::Entry::Type::Medium) {
					mlw_g_alloc.freeMediumRegion(tail);
				}
				else {
					mlw_g_alloc.freeSmallRegion(tail, size);
				}
				--orphan_sc.free_slabs;
				tail = n;
			}
			else {

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
	mlw_g_alloc.orphan_pool.small_8.has_remove_free.store(true, sync::MemoryOrder::Relaxed);
	mlw_g_alloc.orphan_pool.small_16.has_remove_free.store(true, sync::MemoryOrder::Relaxed);
	mlw_g_alloc.orphan_pool.small_32.has_remove_free.store(true, sync::MemoryOrder::Relaxed);
	mlw_g_alloc.orphan_pool.small_64.has_remove_free.store(true, sync::MemoryOrder::Relaxed);
	mlw_g_alloc.orphan_pool.small_128.has_remove_free.store(true, sync::MemoryOrder::Relaxed);
	mlw_g_alloc.orphan_pool.medium.has_remove_free.store(true, sync::MemoryOrder::Relaxed);
	mlw_g_alloc.drainRemoteList(mlw_g_alloc.orphan_pool);

}
