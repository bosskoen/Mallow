#pragma once
#include "../thread/rwlock.h"

/// \file
/// \brief GAlloc — thread-caching general-purpose allocator.
///
/// Public surface is the process-wide \ref core::GAlloc instance
/// \ref core::mlw_g_alloc and its four methods; everything else is internal
/// (see \ref core::detail). For the full design, see \ref galloc_design.

/// \page galloc_design GAlloc Design
///
/// \section galloc_arch Architecture
///
/// GAlloc is a three-tier allocator: small, medium, and OS-backed large.
/// Every thread gets its own ThreadCache, so the fast path (alloc + free on
/// the same thread) requires no locking at all.
///
/// \verbatim
///   request size              tier          backing
///   --------------------------------------------------
///   <= 128 bytes              small         64 KB slab, bump-pointer free list
///   129 .. 262144 bytes       medium        4 MB region, first-fit with coalescing
///   > 262144 bytes            OS            mmap / VirtualAlloc per allocation
///   alignment == PAGE_SIZE    OS            (regardless of size)
///   alignment > 256           rejected      returns nullptr
/// \endverbatim
///
/// \section galloc_small Small allocations
///
/// Five fixed size classes: 8, 16, 32, 64, 128 bytes. Each size class owns a
/// linked list of Regions (64 KB slabs). Inside a slab every cell is the same
/// size, and the free list is embedded: each free cell stores a pointer to the
/// next free cell.
///
/// \verbatim
///   Region (64 KB slab)
///   +----------+----+----+----+----+----+-------+
///   |  Region  | c0 | c1 | c2 | c3 | c4 |  ...  |
///   |  header  |    |    |    |    |    |       |
///   +----------+----+----+----+----+----+-------+
/// \endverbatim
///
/// The first cell is aligned to the size class. Free cells form a singly-linked
/// list through embedded pointers.
///
/// \section galloc_medium Medium allocations
///
/// A 4 MB Region is divided into variable-size blocks. Each block has a Header
/// (8 bytes) followed by payload. Free blocks additionally store a FreePointer
/// (two pointers) in the payload area, forming a doubly-linked free list per
/// Region.
///
/// \verbatim
///   Region (4 MB)
///   +----------+----------+-------------+----------+---------+
///   |  Region  | Header_0 |  payload_0  | Header_1 |  ...    |
///   |  header  |          |             |          |         |
///   +----------+----------+-------------+----------+---------+
///
///   Header layout (8 bytes, bitfield):
///     pre_off  : 24 bits - distance in bytes to the previous Header
///     next_off : 24 bits - distance in bytes to the next Header
///     align    : 16 bits - alignment this block was allocated with (0 = free)
/// \endverbatim
///
/// The `align` field doubles as the free/used flag: 0 means free, nonzero means
/// in-use (and records the original alignment request so realloc can preserve
/// it). Blocks form a physical chain via pre_off / next_off; free blocks
/// additionally sit in a doubly-linked logical free list via FreePointer.
///
/// Aligned allocation for medium blocks works by splitting:
///
/// \verbatim
///     +--------+---------+------------+----------+-----------+
///     | Header | padding | new_Header | payload  | remainder |
///     |(front) |         | (aligned)  |          |  (back)   |
///     +--------+---------+------------+----------+-----------+
/// \endverbatim
///
/// Front and back fragments are kept as free blocks if large enough
/// (> MIN_SIZE + sizeof(Header)), otherwise absorbed.
///
/// \section galloc_coalesce Coalescing (freeMedium)
///
/// On free, adjacent free blocks are merged in two passes.
///
/// Backward: if the predecessor is free (align == 0), extend it forward to
/// cover this block. The predecessor is already in the free list, so no list
/// surgery is needed. Special case: if pre_off == 0 and we are not at region+1,
/// there is dead alignment padding before us — reclaim it by moving the header
/// to region+1.
///
/// Forward: if the successor is free, absorb it. Free list update depends on
/// whether backward already linked us: if backward merged, just unlink the
/// successor from the free list; if not, replace the successor's free list slot
/// with us. If neither direction merged, insert at the free list head.
///
/// \section galloc_remote Cross-thread freeing
///
/// Each Region has an atomic `remote_free` pointer: a lock-free LIFO stack. When
/// thread B frees memory owned by thread A's Region, it pushes the pointer onto
/// `remote_free` via compare-exchange.
///
/// Thread A periodically calls drainRemoteList(), which atomically swaps out the
/// remote_free stack and processes it: small blocks splice the whole chain onto
/// the slab free list; medium blocks call freeMedium for each (full coalescing).
///
/// The `has_remove_free` flag on each SizeClass is a coarse "you have remote
/// frees to drain" signal, set with Release by the freeing thread. This avoids
/// touching the atomic on every alloc — we only drain when the flag is set.
///
/// \section galloc_lifecycle Region lifecycle and orphan pool
///
/// Regions are allocated via mmap/VirtualAlloc and tracked in a sorted
/// RegionTable (binary search by address, protected by region_list_lock).
///
/// When a Region becomes completely empty (used_count == 0), it increments
/// free_slabs for that size class. If free_slabs reaches 2, one empty Region is
/// returned to the OS. This keeps one warm spare without hoarding memory.
///
/// When a thread exits, its ~ThreadCache migrates all Regions to the global
/// orphan_pool, updating owning_cache under each Region's migration_lock. Future
/// threads can adopt orphaned Regions instead of allocating fresh ones. The
/// orphan pool drains remote_free lists and frees excess empty regions during
/// migration.
///
/// \section galloc_os OS allocations
///
/// Anything above MAX_SIZE (256 KB) or requesting PAGE_SIZE alignment goes
/// straight to the OS. These are tracked in an OSTable (sorted array, similar to
/// RegionTable). On Linux, realloc attempts mremap for in-place growth before
/// falling back to alloc+copy+free.
///
/// \section galloc_locking Locking summary
///
/// - region_list_lock (RWLock) — protects RegionTable insert/remove/find. Taken
///   as ReadLock on every free() to look up the region; as WriteLock when
///   allocating or freeing a region.
/// - orphan_lock (MCS spinlock) — protects the orphan_pool. Held during orphan
///   adoption, ~ThreadCache migration, and while draining the orphan pool's
///   remote free lists. Other paths use tryLock and skip the drain if held.
/// - os_lock (MCS spinlock) — protects the OSTable.
/// - migration_lock (RWLock, per Region) — synchronizes ownership transfer.
///   Writers: thread exit (changing owning_cache). Readers: cross-thread free
///   (reading owning_cache to set has_remove_free on the correct SizeClass).
/// - No lock is needed on the fast path (same-thread alloc + free).
///
/// \section galloc_safety Thread safety guarantees
///
/// - alloc(): safe to call concurrently from any thread.
/// - free(): safe to free any pointer from any thread; cross-thread frees are
///   deferred via the lock-free remote_free list.
/// - realloc(): safe from any thread. Falls back to alloc+copy+free for
///   cross-thread or cross-tier transitions.

namespace core
{
    namespace detail
    {
        struct Region;

        /// \brief Sorted (Region*, Type) array mapping any pointer to its Region.
        ///
        /// Binary search on the address range. Protected externally by
        /// GAlloc::region_list_lock. Grows one page at a time; on Windows reserves
        /// a large virtual range upfront and commits incrementally, on Linux uses
        /// mremap.
        struct RegionTable
        {
            struct Entry
            {
                Region *ptr;
                /// \brief Size class: Medium for variable-size blocks, S8..S128 for
                /// fixed slabs. The numeric value of S8..S128 equals the cell size.
                enum class Type : uint8
                {
                    Medium,
                    S8 = 8,
                    S16 = 16,
                    S32 = 32,
                    S64 = 64,
                    S128 = 128
                } type;
                uint8 pad[7]{}; ///< 16 bytes total for cache-friendly scanning.
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

            /// \brief Free the backing array. (Note: spelling is `distroy`.)
            void distroy();

            /// \brief Binary search for the Region containing `ptr`.
            /// \return The entry, or nullptr if `ptr` is in no tracked region.
            Entry *find(void *ptr) const;

            /// \brief Remove the entry for `ptr` (exact Region* match). Panics if absent.
            void remove(Region *);

            /// \brief Insert in sorted order. \return false on OOM (grow failed).
            bool insert(Entry &&);

            /// \brief Commit one more page of capacity. \return false on OOM.
            bool grow();
        };

        /// \brief Sorted (void*, size) array tracking OS-level allocations.
        ///
        /// Same structure as RegionTable but for large allocations that bypass the
        /// region system (mmap/VirtualAlloc). Protected externally by
        /// GAlloc::os_lock.
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

            /// \brief Free the backing array. (Note: spelling is `distroy`.)
            void distroy();

            /// \brief Find the entry containing `ptr` and remove it in one lookup.
            /// \pre Caller holds os_lock. \return None if `ptr` is not tracked.
            Optional<Entry> findAndRemove(void *ptr);

            /// \brief Insert in sorted order. \return false on OOM.
            bool insert(Entry &&);
            /// \brief Commit one more page of capacity. \return false on OOM.
            bool grow();
        };

        /// \brief Per-thread allocation cache (one per thread via thread_local).
        ///
        /// Holds a SizeClass for each small tier plus one for medium. On thread
        /// exit, ~ThreadCache migrates all owned Regions to the global orphan_pool
        /// so future threads can adopt them.
        struct ThreadCache
        {
            /// \brief Region list plus per-class bookkeeping for one size class.
            struct SizeClass
            {
                Region *active = nullptr;                 ///< Head of this class's region list.
                sync::Atomic<bool> has_remove_free{false};///< Set (Release) by a remote thread: remote_free needs draining.
                uint32 free_slabs = 0;                    ///< Count of fully-empty regions; at >= 2, one is returned to the OS.
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

            /// \brief Select the SizeClass member for a given tier.
            MLW_FORCE_INLINE SizeClass &getSizeClass(RegionTable::Entry::Type size)
            {
                switch (size)
                {
                case RegionTable::Entry::Type::S8:
                    return small_8;
                case RegionTable::Entry::Type::S16:
                    return small_16;
                case RegionTable::Entry::Type::S32:
                    return small_32;
                case RegionTable::Entry::Type::S64:
                    return small_64;
                case RegionTable::Entry::Type::S128:
                    return small_128;
                case RegionTable::Entry::Type::Medium:
                    return medium;
                }
                return medium;
            }

            ThreadCache(const ThreadCache &) = delete;
            ThreadCache &operator=(const ThreadCache &) = delete;

            /// \brief CRT hook: initialize this thread's cache. Called first thing
            ///        on every spawned thread (see thread.cpp).
            static void mlw__first_crt_ctor();
            /// \brief CRT hook: tear down this thread's cache storage on exit.
            static void mlw__crt_distroy_tc_storage();
        };

        struct FreePointer;

        /// \brief Medium-block header (8 bytes, bitfield).
        ///
        /// \verbatim
        ///   +----------------------------------------------------+
        ///   |  pre_off (24b)  |  next_off (24b)  | align (16b)    |
        ///   +----------------------------------------------------+
        /// \endverbatim
        ///
        /// pre_off / next_off are byte distances to the previous / next Header in
        /// the physical chain, so traversal needs no stored pointers and the header
        /// stays 8 bytes. `align == 0` marks the block free; nonzero stores the
        /// alignment it was allocated with (min 8, up to 256).
        ///
        /// \note The 24-bit offsets cap any two neighboring headers at 2^24 bytes
        ///       (16 MB) apart. Medium region size (currently 4 MB) must stay below
        ///       that limit — do not raise MEDIUM_BLOCK_SIZE past 16 MB without
        ///       widening these fields.
        struct Header
        {
            uint64 pre_off : 24;
            uint64 next_off : 24;
            uint64 align : 16;
            /// \brief The FreePointer overlaid on this block's payload (free blocks only).
            MLW_FORCE_INLINE FreePointer *getFreePtr() { return reinterpret_cast<FreePointer *>(this + 1); };
        };

        /// \brief Doubly-linked free-list node overlaid on a free medium block's
        /// payload, separate from the physical block chain.
        struct FreePointer
        {
            Header *next_free_block = nullptr;
            Header *prev_free_block = nullptr;
        };

        /// \brief A contiguous OS allocation backing a set of blocks.
        ///
        /// 64 KB for small tiers, 4 MB for medium. Regions are linked per SizeClass
        /// and owned by exactly one ThreadCache at a time; ownership can transfer to
        /// the orphan_pool on thread exit, protected by migration_lock. remote_free
        /// is a lock-free LIFO stack for cross-thread frees, drained by the owner on
        /// its next alloc or free.
        struct Region
        {
            Region *previous = nullptr;
            Region *next = nullptr;
            Header *free_block = nullptr;
            sync::Atomic<void *> remote_free;
            ThreadCache *owning_cache;
            sync::RWLock migration_lock{};
            uint32 used_count;   ///< Live allocations in this region; 0 = empty (return-to-OS eligible).
            uint32 padding;      ///< Explicit pad to keep the following constants/layout aligned.
            static constexpr usize MEDIUM_BLOCK_SIZE = 1 << 22; ///< 4 MB.
            static constexpr usize SMALL_BLOCK_SIZE = 1 << 16;  ///< 64 KB.
        };
    } // namespace detail

    /// \brief Process-wide general-purpose allocator; use \ref mlw_g_alloc.
    ///
    /// Thread-caching three-tier allocator. See \ref galloc_design for the full
    /// architecture, locking, and thread-safety model. Not copyable or movable —
    /// there is exactly one instance.
    ///
    /// \warning There must be only one initialized instance of this allocator
    ///          supplied by the C runtime. Initializing more than one instance
    ///          can corrupt the allocator's internal tables and lead to undefined
    ///          behavior.
    extern class GAlloc
    {
    public:
    private:
        detail::RegionTable region_table{};
        sync::RWLock region_list_lock{}; // protects region_table

        static constexpr usize MIN_SIZE = 1 << 7;  // 128 — minimum medium block payload
        static constexpr usize MAX_SIZE = 1 << 18; // 256 KB — above this goes to OS

        bool allocMediumRegion(detail::Region *last_region, detail::ThreadCache &cache);
        void freeMediumRegion(detail::Region *region);
        bool freeMedium(void *ptr, detail::Region *);
        void freeSmall(void *ptr, detail::Region *, detail::RegionTable::Entry::Type size);
        void freeSmallRegion(detail::Region *, detail::RegionTable::Entry::Type size);
        bool allocSmallRegion(detail::Region *last_region, detail::ThreadCache &cache, detail::RegionTable::Entry::Type);
        void *osAlloc(usize size);
        void osFree(void *ptr);

        /// \brief Drain all pending cross-thread frees for the given cache.
        /// Called at the start of alloc and free to amortize remote-free processing.
        void drainRemoteList(detail::ThreadCache &);
        friend struct detail::ThreadCache;

        detail::ThreadCache orphan_pool{}; // holds regions from exited threads
        sync::spin_lock::MCS orphan_lock{};

        detail::OSTable os_table{};
        sync::spin_lock::MCS os_lock{}; // protects os_table

    public:
        explicit GAlloc() = default;
        ~GAlloc() = default;
        GAlloc(GAlloc &&) = delete;
        GAlloc &operator=(GAlloc &&) = delete;

        GAlloc(const GAlloc &) = delete;
        GAlloc &operator=(const GAlloc &) = delete;

        /// \brief Allocate `size` bytes with the given alignment.
        ///
        /// `align` must be a power of two; it is clamped up to a minimum of 8.
        /// Alignments > 256 return nullptr, except PAGE_SIZE, which routes to a
        /// direct OS allocation.
        /// \return The allocation, or nullptr on OOM or invalid alignment.
        void *alignAlloc(usize size, usize align);

        /// \brief Allocate `size` bytes with default alignment (8).
        MLW_FORCE_INLINE void *alloc(usize size) { return alignAlloc(size, 8); }

        /// \brief Free a pointer from alloc/alignAlloc/realloc.
        /// nullptr is a no-op. Safe to call from any thread (cross-thread frees are
        /// deferred via the region's remote_free list).
        void free(void *ptr);

        /// \brief Grow an allocation, in place if possible, else alloc + copy + free.
        ///
        /// A nullptr `ptr` acts as alloc; a zero `new_size` acts as free. Preserves
        /// the original alignment of medium blocks and does not shrink. Tries
        /// in-place growth for medium blocks (same thread, next block free, enough
        /// space); on Linux, OS allocations attempt mremap first.
        void *realloc(void *ptr, usize new_size);
    } &mlw_g_alloc;

} // namespace core