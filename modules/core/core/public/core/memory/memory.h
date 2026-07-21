#pragma once
#include "compilers.h"
#include "traits.h"

/// \file
/// \brief Specialized instance allocators: Arena, Pool, and ObjectPool<T>.
///
/// These are standalone, workload-specific tools, NOT general-purpose
/// allocators and NOT interchangeable with the global allocator. Each owns a
/// single OS reservation and hands out raw memory; object lifetime (running
/// constructors and destructors) is always the caller's responsibility.
///
/// - core::Arena � bump allocator; all allocations reclaimed at once by reset().
/// - core::Pool  � fixed-block pool; blocks returned to the free list individually.
/// - core::ObjectPool � typed wrapper over Pool that owns T objects (create/destroy).

namespace core
{
    /// \brief Bump allocator over a single reservation: fast alloc, bulk free.
    ///
    /// Allocation just advances an offset, so it is extremely cheap. There is no
    /// per-allocation free � memory is reclaimed in bulk by \ref reset, or back
    /// to a saved point by \ref freeTo. This is the same discipline as a call
    /// stack: allocate downward-cheap, unwind all at once.
    ///
    /// Backing: on Windows the address range is reserved up front and committed
    /// one page at a time as the offset grows; on Linux the whole range is
    /// mmap'd and populated lazily by the kernel on first touch. `committed`
    /// tracks how much is currently backed by physical pages (Windows); on Linux
    /// it equals `capacity`.
    ///
    /// \warning Data only. \ref shutdown (and the destructor and move-assignment)
    ///          release the backing memory WITHOUT running any destructors for
    ///          objects constructed in it. An Arena cannot run destructors � it
    ///          frees in bulk and does not know what it holds � so only place
    ///          trivially-destructible data here, or run destructors yourself
    ///          before reset/shutdown. For objects with destructors, use
    ///          \ref ObjectPool instead.
    /// \note Not copyable; movable (transfers the reservation).
    struct Arena
    {
        void* base = nullptr;    ///< Reservation base, or null if uninitialized.
        usize capacity = 0;      ///< Total reserved bytes (page-rounded).
        usize offset = 0;        ///< Bytes handed out so far (the bump pointer).
        usize committed = 0;     ///< Bytes backed by physical pages (Windows); == capacity on Linux.

        /// \brief Reserve `size` bytes (rounded up to a page). \return false on OOM
        ///        or if already initialized.
        bool init(usize size);

        /// \brief Release the reservation and reset to empty. Runs no destructors.
        void shutdown();

        MLW_FORCE_INLINE ~Arena() { shutdown(); };

        Arena(const Arena&) = delete;
        Arena& operator=(const Arena&) = delete;

        /// \brief Move-construct, taking `other`'s reservation and emptying it.
        Arena(Arena&& other) noexcept
        {
            base = other.base;
            capacity = other.capacity;
            offset = other.offset;
            committed = other.committed;
            other.base = nullptr;
            other.capacity = 0;
            other.offset = 0;
            other.committed = 0;
        };

        /// \brief Move-assign. Releases our current reservation first (see the
        ///        type warning: no destructors are run), then takes `other`'s.
        Arena& operator=(Arena&& other) noexcept
        {
            if (this != &other)
            {
                shutdown();
                base = other.base;
                capacity = other.capacity;
                offset = other.offset;
                committed = other.committed;
                other.base = nullptr;
                other.capacity = 0;
                other.offset = 0;
                other.committed = 0;
            }
            return *this;
        };

        /// \brief Allocate `size` bytes aligned to `alignment`.
        /// \return Uninitialized storage, or nullptr when the Arena is full or
        ///         uninitialized. Contents are indeterminate.
        void* alloc(usize size, usize alignment);

        /// \brief Allocate `size` bytes with a natural alignment for that size.
        ///
        /// Alignment is the next power of two >= `size`, capped at the strictest
        /// fundamental alignment (`alignof(alignment_t)`), with size <= 1 using
        /// alignment 1. Keeps small allocations tightly packed while never
        /// under-aligning a fundamental type.
        MLW_FORCE_INLINE void* alloc(usize size)
        {
            usize alignment = size >= sizeof(alignment_t) ? alignof(alignment_t) : size <= 1 ? 1
                : 1ull << (64 - MLW_CLZ(size - 1));
            return alloc(size, alignment);
        }

        /// \brief Save the current offset, to be restored later by \ref freeTo.
        MLW_FORCE_INLINE usize mark() const { return offset; };

        /// \brief Rewind to a mark from \ref mark. Never advances the offset.
        /// \warning Invalidates every allocation made after the mark.
        MLW_FORCE_INLINE void freeTo(usize mark) { offset = offset < mark ? offset : mark; };

        /// \brief Rewind so `ptr` (previously returned by \ref alloc) becomes the
        ///        next allocation point. Clamped to the current range.
        /// \warning Invalidates every allocation made at or after `ptr`.
        MLW_FORCE_INLINE void freeTo(void* ptr)
        {
            if (ptr <= base)
                offset = 0;
            else if (static_cast<usize>(reinterpret_cast<uint8*>(ptr) - reinterpret_cast<uint8*>(base)) < offset)
                offset = reinterpret_cast<uint8*>(ptr) - reinterpret_cast<uint8*>(base);
        };

        /// \brief Reclaim everything (rewind to the start). Runs no destructors.
        MLW_FORCE_INLINE void reset() { offset = 0; };
    };

    /// \brief Fixed-size block pool over a single reservation.
    ///
    /// All blocks are the same size and freed individually via a lock-free-free
    /// (single-threaded) embedded free list: each free block stores a pointer to
    /// the next free one, so there is no separate metadata. `alloc` pops the
    /// head; `free` pushes it back.
    ///
    /// Alignment: block_size is rounded up to size_of(void*), so every block is
    /// aligned to at least alignof(void*) (8). Larger natural alignment is only
    /// obtained incidentally when block_size is itself a larger power of two.
    /// The raw Pool takes no alignment parameter by design; use ObjectPool for
    /// per-type alignment.
    ///
    /// \warning Raw slots, manual lifetime. `alloc` returns uninitialized storage
    ///          (its first `sizeof(void*)` bytes previously held a free-list
    ///          link). You placement-new into it and call the destructor
    ///          yourself before \ref free. \ref shutdown releases the whole
    ///          reservation WITHOUT running destructors, and does not track which
    ///          blocks are live � destroy your objects before shutdown.
    /// \note Not copyable; movable.
    struct Pool
    {
        void* base = nullptr;        ///< Reservation base, or null if uninitialized.
        usize capacity = 0;          ///< Total reserved bytes (page-rounded).
        usize block_size = 0;        ///< Size of each block (>= sizeof(void*), aligned).
        void* first_free = nullptr;  ///< Head of the embedded free list.

        /// \brief Reserve `size` bytes and carve them into `b_size` blocks.
        ///
        /// `b_size` is raised to at least `sizeof(void*)` and rounded up to
        /// `alignof(void*)`. `size` is page-rounded; the block count is
        /// `capacity / block_size` (integer division).
        /// \return false on OOM, if already initialized, or if `size` is smaller
        ///         than one block.
        /// \note Up to `block_size - 1` bytes at the tail are unused when capacity
        ///       is not an exact multiple of the block size � intentional, so no
        ///       short block is ever handed out.
        bool init(usize size, usize b_size);

        /// \brief Release the reservation and reset to empty. Runs no destructors.
        void shutdown();

        ~Pool() { shutdown(); };

        Pool(const Pool&) = delete;
        Pool& operator=(const Pool&) = delete;

        /// \brief Move-construct, taking `other`'s reservation and emptying it.
        Pool(Pool&& other) noexcept
        {
            base = other.base;
            capacity = other.capacity;
            block_size = other.block_size;
            first_free = other.first_free;
            other.base = nullptr;
            other.capacity = 0;
            other.block_size = 0;
            other.first_free = nullptr;
        };

        /// \brief Move-assign. Releases our current reservation first (no
        ///        destructors run � see the type warning), then takes `other`'s.
        Pool& operator=(Pool&& other) noexcept
        {
            if (this != &other)
            {
                shutdown(); // release our reservation before overwriting, or it leaks
                base = other.base;
                capacity = other.capacity;
                block_size = other.block_size;
                first_free = other.first_free;
                other.base = nullptr;
                other.capacity = 0;
                other.block_size = 0;
                other.first_free = nullptr;
            }
            return *this;
        };

        /// \brief Take one block from the free list.
        /// \return Uninitialized block storage, or nullptr when the pool is empty.
        void* alloc()
        {
            if (first_free == nullptr)
            {
                return nullptr;
            }
            void* ret = first_free;
            first_free = *static_cast<void**>(ret);
            return ret;
        };

        /// \brief Return a block to the free list. nullptr is a no-op.
        /// \pre `ptr` was returned by this pool's \ref alloc and its object (if
        ///      any) has already been destroyed.
        void free(void* ptr)
        {
            if (ptr == nullptr)
                return;
            *static_cast<void**>(ptr) = first_free;
            first_free = ptr;
        };
    };

    /// \brief Typed object pool: a \ref Pool that owns `T` instances.
    ///
    /// Bakes `sizeof(T)`/`alignof(T)` into the underlying pool and pairs
    /// allocation with construction (\ref create) and freeing with destruction
    /// (\ref destroy), so callers deal in objects rather than raw slots. This is
    /// the object-owning counterpart to the raw \ref Pool � and note there is
    /// deliberately no "ObjectArena": an arena frees in bulk and cannot run
    /// destructors, so only a pool (which frees per block) can own objects.
    ///
    /// \warning destroy() runs the destructor, but teardown does not. Destroying an
    ///          ObjectPool, or move-assigning into an already-initialized one,
    ///          releases that pool's memory WITHOUT running destructors for blocks
    ///          still live in it � the pool can't tell which slots are occupied.
    ///          Destroy live objects before a pool is torn down or overwritten.
    ///
    /// \tparam T The object type this pool allocates.
    template<typename T>
    struct ObjectPool {
        Pool pool;

        /// \brief Reserve room for `count` objects of `T`.
        ///
        /// Block size is `sizeof(T)`
        /// for any complete type (the language guarantees `sizeof(T)` is a
        /// multiple of `alignof(T)`), so every block is `alignof(T)`-aligned
        /// given the pool's page-aligned base.
        bool init(usize count) {
            // bake sizeof(T)/alignof(T) into the raw pool's block size
            return pool.init(count * sizeof(T), sizeof(T));
        }

        /// \brief Allocate and construct a `T` from `args`.
        /// \return The constructed object, or nullptr if the pool is exhausted.
        template<typename... Args> T* create(Args&&... a) {
            void* m = pool.alloc();
            return m ? new (m) T(core::forward<Args>(a)...) : nullptr;
        }

        /// \brief Destroy `p` and return its block to the pool. nullptr is a no-op.
        void destroy(T* p) { if (p) { p->~T(); pool.free(p); } }

        // Rule-of-5 is inherited implicitly from the Pool member: copy is deleted
        // (Pool's copy is deleted), move is generated (moves the Pool). The
        // teardown warning above applies to that implicit move/destroy too.
    };
} // namespace core