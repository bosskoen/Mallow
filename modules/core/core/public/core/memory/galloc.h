/**
 * ============================================================================
 *  GAlloc — Thread-caching general-purpose allocator
 * ============================================================================
 *
 * Architecture
 * ------------
 * GAlloc is a three-tier allocator: small, medium, and OS-backed large.
 * Every thread gets its own ThreadCache, so the fast path (alloc + free on
 * the same thread) requires no locking at all.
 *
 *   request size              tier          backing
 *   ──────────────────────────────────────────────────
 *   <= 128 bytes              small         64 KB slab, bump-pointer free list
 *   129 .. 262144 bytes       medium        4 MB region, first-fit with coalescing
 *   > 262144 bytes            OS            mmap / VirtualAlloc per allocation
 *   alignment == PAGE_SIZE    OS            (regardless of size)
 *   alignment > 256           rejected      returns nullptr
 *
 *
 * Small allocations
 * -----------------
 * Five fixed size classes: 8, 16, 32, 64, 128 bytes.
 * Each size class owns a linked list of Regions (64 KB slabs). Inside a
 * slab every cell is the same size, and the free list is embedded:
 * each free cell stores a pointer to the next free cell.
 *
 *   Region (64 KB slab)
 *   ┌──────────┬────┬────┬────┬────┬────┬───────┐
 *   │  Region  │ c0 │ c1 │ c2 │ c3 │ c4 │  ...  │
 *   │  header  │    │    │    │    │    │       │
 *   └──────────┴────┴────┴────┴────┴────┴───────┘
 *   The first cell is aligned to the size class. Free cells form a
 *   singly-linked list through embedded pointers.
 *
 *
 * Medium allocations
 * ------------------
 * A 4 MB Region is divided into variable-size blocks. Each block has a
 * Header (8 bytes) followed by payload. Free blocks additionally store
 * a FreePointer (two pointers) in the payload area, forming a doubly-
 * linked free list per Region.
 *
 *   Region (4 MB)
 *   ┌──────────┬──────────┬─────────────┬──────────┬─────────┐
 *   │  Region  │ Header_0 │  payload_0  │ Header_1 │  ...    │
 *   │  header  │          │             │          │         │
 *   └──────────┴──────────┴─────────────┴──────────┴─────────┘
 *
 *   Header layout (8 bytes, bitfield):
 *     pre_off  : 24 bits — distance in bytes to the previous Header
 *     next_off : 24 bits — distance in bytes to the next Header
 *     align    : 16 bits — alignment this block was allocated with (0 = free)
 *
 *   The `align` field doubles as the free/used flag: 0 means free,
 *   nonzero means in-use (and records the original alignment request
 *   so realloc can preserve it).
 *
 *   Blocks form a physical chain via pre_off / next_off (every block
 *   knows its neighbors). Free blocks additionally sit in a doubly-
 *   linked logical free list via FreePointer embedded in the payload.
 *
 *   Aligned allocation for medium blocks works by splitting:
 *
 *     ┌────────┬─────────┬────────────┬──────────┬───────────┐
 *     │ Header │ padding │ new_Header │ payload  │ remainder │
 *     │(front) │         │ (aligned)  │          │  (back)   │
 *     └────────┴─────────┴────────────┴──────────┴───────────┘
 *
 *   Front and back fragments are kept as free blocks if large enough
 *   (> MIN_SIZE + sizeof(Header)), otherwise absorbed.
 *
 *
 * Coalescing (freeMedium)
 * -----------------------
 * On free, adjacent free blocks are merged in two passes:
 *
 *   Backward: if the predecessor is free (align == 0), extend it
 *   forward to cover this block. The predecessor is already in the
 *   free list, so no list surgery is needed.
 *   Special case: if pre_off == 0 and we are not at region+1, there
 *   is dead alignment padding before us — reclaim it by moving the
 *   header to region+1.
 *
 *   Forward: if the successor is free, absorb it. Free list update
 *   depends on whether backward already linked us:
 *     - backward merged:  just unlink successor from the free list
 *     - no backward:      replace successor's free list slot with us
 *
 *   If neither direction merged, insert at the free list head.
 *
 *
 * Cross-thread freeing
 * --------------------
 * Each Region has an atomic `remote_free` pointer: a lock-free LIFO
 * stack. When thread B frees memory owned by thread A's Region, it
 * pushes the pointer onto `remote_free` via compare-exchange.
 *
 * Thread A periodically calls drainRemoteList(), which atomically
 * swaps out the remote_free stack and processes it:
 *   - Small blocks: splice the whole chain onto the slab free list
 *   - Medium blocks: call freeMedium for each (full coalescing)
 *
 * The `has_remove_free` flag on each SizeClass is a coarse "you have
 * remote frees to drain" signal, set with Release by the freeing
 * thread. This avoids touching the atomic on every alloc — we only
 * drain when the flag is set.
 *
 *
 * Region lifecycle & orphan pool
 * ------------------------------
 * Regions are allocated via mmap/VirtualAlloc and tracked in a sorted
 * RegionTable (binary search by address, protected by region_list_lock).
 *
 * When a Region becomes completely empty (used_count == 0), it increments
 * free_slabs for that size class. If free_slabs reaches 2, one empty
 * Region is returned to the OS. This keeps one warm spare without
 * hoarding memory.
 *
 * When a thread exits, its ~ThreadCache migrates all Regions to the
 * global orphan_pool, updating owning_cache under each Region's
 * migration_lock. Future threads can adopt orphaned Regions instead
 * of allocating fresh ones. The orphan pool drains remote_free lists
 * and frees excess empty regions during migration.
 *
 *
 * OS allocations
 * --------------
 * Anything above MAX_SIZE (256 KB) or requesting PAGE_SIZE alignment
 * goes straight to the OS. These are tracked in an OSTable (sorted
 * array, similar to RegionTable). On Linux, realloc attempts mremap
 * for in-place growth before falling back to alloc+copy+free.
 *
 *
 * Locking summary
 * ---------------
 *   region_list_lock (RWLock) — protects RegionTable insert/remove/find.
 *     Taken as ReadLock on every free() to look up the region.
 *     Taken as WriteLock when allocating or freeing a region.
 *
 *   orphan_lock (MCS spinlock) — protects the orphan_pool.
 *     Held during orphan adoption, ~ThreadCache migration, and while
 *     draining the orphan pool's remote free lists. Other paths use
 *     tryLock and skip the drain if the lock is already held.
 *
 *   os_lock (MCS spinlock) — protects the OSTable.
 *
 *   migration_lock (RWLock, per Region) — synchronizes ownership
 *     transfer. Writers: thread exit (changing owning_cache).
 *     Readers: cross-thread free (reading owning_cache to set the
 *     has_remove_free flag on the correct SizeClass).
 *
 *   No lock is needed on the fast path (same-thread alloc + free).
 *
 *
 * Thread safety guarantees
 * ------------------------
 *   - alloc(): safe to call concurrently from any thread.
 *   - free():  safe to free any pointer from any thread; cross-thread
 *              frees are deferred via the lock-free remote_free list.
 *   - realloc(): safe from any thread. Falls back to alloc+copy+free
 *                for cross-thread or cross-tier transitions.
 *
 * ============================================================================
 */

#pragma once
#include "../thread/rwlock.h"

namespace core
{
	struct Region;

	/**
	 * Sorted array of (Region*, Type) entries, used to map any pointer
	 * back to its owning Region via binary search on the address range.
	 * Protected externally by GAlloc::region_list_lock.
	 *
	 * Grows one page at a time. On Windows, reserves a large virtual
	 * range upfront and commits incrementally. On Linux, uses mremap.
	 */
	struct RegionTable
	{
		struct Entry
		{
			Region *ptr;
			/// Size class: Medium for variable-size blocks, S8..S128 for fixed slabs.
			/// The numeric value of S8..S128 equals the cell size in bytes.
			enum class Type : uint8
			{
				Medium,
				S8 = 8,
				S16 = 16,
				S32 = 32,
				S64 = 64,
				S128 = 128
			} type;
			uint8 pad[7]{}; // 16 bytes total for cache-friendly scanning
			MLW_FORCE_INLINE Entry(Region *p, Type t) : ptr(p), type(t) {};
			MLW_FORCE_INLINE Entry() = default;
		} *base = nullptr;
		index_t capacity = 0;
		index_t size = 0;
		RegionTable();
		~RegionTable();

		RegionTable(const RegionTable &) = delete;
		RegionTable &operator=(const RegionTable &) = delete;

		RegionTable(RegionTable &&o) noexcept
			: base(o.base), capacity(o.capacity), size(o.size)
		{
			o.base = nullptr;
			o.capacity = 0;
			o.size = 0;
		}
		RegionTable &operator=(RegionTable &&o) noexcept
		{
			if (this != &o)
			{
				distroy();
				base = o.base;
				capacity = o.capacity;
				size = o.size;
				o.base = nullptr;
				o.capacity = 0;
				o.size = 0;
			}
			return *this;
		}

		void distroy();

		/// Binary search for the Region containing `ptr`. Returns nullptr if
		/// the pointer doesn't fall within any tracked region.
		Entry *find(void *ptr) const;

		/// Remove the entry for `ptr` (exact Region* match). Panics if not found.
		void remove(Region *);

		/// Insert a new entry in sorted order. Returns false on OOM (grow failed).
		bool insert(Entry &&);

		/// Commit one more page of capacity. Returns false on OOM.
		bool grow();
	};

	/**
	 * Sorted array of (void*, size) entries tracking OS-level allocations
	 * (mmap/VirtualAlloc). Same structure as RegionTable but for large
	 * allocations that bypass the region system.
	 * Protected externally by GAlloc::os_lock.
	 */
	struct OSTable
	{
		struct Entry
		{
			void *ptr;
			usize size;
			MLW_FORCE_INLINE Entry(void *p, usize t) : ptr(p), size(t) {};
			MLW_FORCE_INLINE Entry() = default;
		} *base = nullptr;
		index_t capacity = 0;
		index_t size = 0;
		OSTable();
		~OSTable();

		OSTable(const OSTable &) = delete;
		OSTable &operator=(const OSTable &) = delete;

		OSTable(OSTable &&o) noexcept
			: base(o.base), capacity(o.capacity), size(o.size)
		{
			o.base = nullptr;
			o.capacity = 0;
			o.size = 0;
		}
		OSTable &operator=(OSTable &&o) noexcept
		{
			if (this != &o)
			{
				distroy();
				base = o.base;
				capacity = o.capacity;
				size = o.size;
				o.base = nullptr;
				o.capacity = 0;
				o.size = 0;
			}
			return *this;
		}

		void distroy();

		/// Find the entry containing `ptr` and remove it in one lookup.
		/// Caller must hold os_lock. Returns None if not tracked.
		Optional<Entry> findAndRemove(void *ptr);

		bool insert(Entry &&);
		bool grow();
	};

	/**
	 * Per-thread allocation cache. Each thread gets one via thread_local.
	 * Contains a SizeClass (region list + metadata) for each small tier
	 * and one for medium.
	 *
	 * On thread exit, ~ThreadCache migrates all owned Regions to the
	 * global orphan_pool so they can be adopted by future threads.
	 */
	struct ThreadCache
	{
		/**
		 * Tracks the linked list of Regions for one size class, plus:
		 *   has_remove_free — set (Release) by a remote thread to signal
		 *     that remote_free lists need draining.
		 *   free_slabs — count of completely empty regions in this class.
		 *     When >= 2, one is returned to the OS.
		 */
		struct SizeClass
		{
			Region *active = nullptr;
			sync::Atomic<bool> has_remove_free{false};
			uint32 free_slabs = 0;
		};
		SizeClass small_8{};
		SizeClass small_16{};
		SizeClass small_32{};
		SizeClass small_64{};
		SizeClass small_128{};
		SizeClass medium{};
		explicit ThreadCache() = default;
		~ThreadCache();
		ThreadCache(ThreadCache &&) = delete;
		ThreadCache &operator=(ThreadCache &&) = delete;

		MLW_FORCE_INLINE SizeClass &getSizeClass(RegionTable::Entry::Type size)
		{
			switch (size)
			{
			case core::RegionTable::Entry::Type::S8:
				return small_8;
			case core::RegionTable::Entry::Type::S16:
				return small_16;
			case core::RegionTable::Entry::Type::S32:
				return small_32;
			case core::RegionTable::Entry::Type::S64:
				return small_64;
			case core::RegionTable::Entry::Type::S128:
				return small_128;
			case core::RegionTable::Entry::Type::Medium:
				return medium;
			}
			return medium;
		}

		ThreadCache(const ThreadCache &) = delete;
		ThreadCache &operator=(const ThreadCache &) = delete;

		static void mlw__first_crt_ctor();
		static void mlw__crt_distroy_tc_storage();
	};

	struct FreePointer;

	/**
	 * Block header for medium allocations (8 bytes).
	 *
	 *   ┌──────────────────────────────────────────────────┐
	 *   │  pre_off (24b)  │  next_off (24b)  │ align (16b)│
	 *   └──────────────────────────────────────────────────┘
	 *
	 * pre_off / next_off are byte distances to the previous / next Header
	 * in the physical chain. This allows traversal without pointers, keeping
	 * the header at 8 bytes.
	 *
	 * align == 0 means the block is free. Nonzero stores the alignment the
	 * block was allocated with (minimum 8, up to 256).
	 */
	struct Header
	{
		uint64 pre_off : 24;
		uint64 next_off : 24;
		uint64 align : 16;
		MLW_FORCE_INLINE FreePointer *getFreePtr() { return reinterpret_cast<FreePointer *>(this + 1); };
	};

	/// Embedded in the payload of free medium blocks. Forms a doubly-linked
	/// free list per Region, separate from the physical block chain.
	struct FreePointer
	{
		Header *next_free_block = nullptr;
		Header *prev_free_block = nullptr;
	};

	/**
	 * A Region is a contiguous OS allocation (64 KB for small, 4 MB for medium)
	 * that serves as the backing store for a set of blocks.
	 *
	 * Regions are linked per SizeClass (previous/next) and owned by exactly
	 * one ThreadCache at a time. Ownership can transfer to the orphan_pool
	 * on thread exit, protected by migration_lock.
	 *
	 * remote_free is a lock-free LIFO stack. Cross-thread frees push here;
	 * the owning thread drains it during its next alloc or free.
	 */
	struct Region
	{
		Region *previous = nullptr;
		Region *next = nullptr;
		Header *free_block = nullptr;
		sync::Atomic<void *> remote_free;
		ThreadCache *owning_cache;
		sync::RWLock migration_lock{};
		uint32 used_count;
		uint32 padding;
		static constexpr usize MEDIUM_BLOCK_SIZE = 1 << 22; // 4 MB
		static constexpr usize SMALL_BLOCK_SIZE = 1 << 16;	// 64 KB
	};

	/**
	 * Global allocator instance. One per process.
	 *
	 * Public API:
	 *   alloc(size)             — allocate with default alignment (8).
	 *   alignAlloc(size, align) — allocate with explicit alignment.
	 *                             align must be a power of two, <= 256
	 *                             (or PAGE_SIZE for a direct OS alloc).
	 *                             Returns nullptr on invalid alignment or OOM.
	 *   free(ptr)               — free a pointer from any thread. nullptr is a no-op.
	 *   realloc(ptr, new_size)  — grow an allocation. nullptr ptr acts as alloc,
	 *                             zero size acts as free. Does not shrink.
	 *                             Tries in-place growth for medium blocks
	 *                             (same thread, next block free, enough space).
	 *                             Falls back to alloc + copy + free.
	 *                             On Linux, OS allocations attempt mremap.
	 *                             Preserves original alignment on reallocation.
	 */
	extern class GAlloc
	{
	public:
	private:
		RegionTable region_table{};
		sync::RWLock region_list_lock{}; // protects region_table

		static constexpr usize MIN_SIZE = 1 << 7;  // 128 — minimum medium block payload
		static constexpr usize MAX_SIZE = 1 << 18; // 256 KB — above this goes to OS

		bool allocMediumRegion(core::Region *last_region, core::ThreadCache &cache);
		void freeMediumRegion(core::Region *region);
		bool freeMedium(void *ptr, core::Region *);
		void freeSmall(void *ptr, core::Region *, RegionTable::Entry::Type size);
		void freeSmallRegion(core::Region *, RegionTable::Entry::Type size);
		bool allocSmallRegion(core::Region *last_region, core::ThreadCache &cache, RegionTable::Entry::Type);
		void *osAlloc(usize size);
		void osFree(void *ptr);

		/// Drain all pending cross-thread frees for the given cache.
		/// Called at the start of alloc and free to amortize remote free processing.
		void drainRemoteList(ThreadCache &);
		friend struct ThreadCache;

		ThreadCache orphan_pool{}; // holds regions from exited threads
		sync::spin_lock::MCS orphan_lock{};

		OSTable os_table{};
		sync::spin_lock::MCS os_lock{}; // protects os_table

	public:
		explicit GAlloc() = default;
		~GAlloc() = default;
		GAlloc(GAlloc &&) = delete;
		GAlloc &operator=(GAlloc &&) = delete;

		GAlloc(const GAlloc &) = delete;
		GAlloc &operator=(const GAlloc &) = delete;

		/// Allocate `size` bytes with the given alignment.
		/// `align` must be a power of two. Clamped to minimum 8.
		/// Alignments > 256 return nullptr (except PAGE_SIZE, which routes to OS).
		/// Returns nullptr on OOM or invalid alignment.
		void *alignAlloc(usize size, usize align);

		/// Allocate `size` bytes with default alignment (8).
		MLW_FORCE_INLINE void *alloc(usize size) { return alignAlloc(size, 8); }

		/// Free a pointer returned by alloc/alignAlloc/realloc.
		/// nullptr is a no-op. Safe to call from any thread.
		void free(void *ptr);

		/// Grow an allocation in place if possible, otherwise alloc + copy + free.
		/// nullptr ptr acts as alloc. Zero new_size acts as free.
		/// Preserves the original alignment of medium blocks. Does not shrink.
		void *realloc(void *ptr, usize new_size);
	} &mlw_g_alloc;

} // namespace core