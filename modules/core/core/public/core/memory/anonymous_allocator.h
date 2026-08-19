#pragma once
#include "core/typedef.h"

namespace core
{
    /// \brief Type-erased allocator: a function pointer plus its context.
    ///
    /// The allocator is stored as a runtime value (fat pointer), not a template
    /// parameter, so every container is a single type regardless of where it
    /// allocates. One indirect call per allocation; negligible since containers
    /// only allocate on growth.
    ///
    /// All sizes are signed (\ref isize) � the unsigned OS boundary is crossed
    /// only inside a concrete \c realloc implementation, never here.
    struct AnonymousAllocator
    {
        /// \brief Single entry point for allocate, resize, and free.
        ///
        /// Exactly one of three operations, selected by the arguments:
        /// - **alloc**  : \p ptr == nullptr, \p new_size > 0.
        ///                Returns a fresh block of at least \p new_size bytes.
        /// - **free**   : \p new_size == 0 (any \p ptr; nullptr is a no-op).
        ///                Releases \p ptr and returns nullptr.
        /// - **resize** : \p ptr != nullptr, \p new_size > 0.
        ///                Returns a block of at least \p new_size bytes holding the
        ///                original contents (see guarantees below).
        ///
        /// \par Size guarantee
        /// On alloc/resize the returned block is usable for **at least** \p new_size
        /// bytes. It MAY be larger (e.g. size-class rounding); callers must not assume
        /// the block is exactly \p new_size.
        ///
        /// \par Alignment guarantee
        /// The returned block is aligned to **at least** \p align bytes (\p align must
        /// be a power of two; it MAY be over-aligned). For **resize**, the caller MUST
        /// pass the same \p align used at alloc; doing so guarantees the alignment is
        /// preserved even if the block relocates. (This holds automatically for a
        /// container of a fixed \c T, since \c alignof(T) never changes.)
        ///
        /// \par Resize behavior
        /// - A **grow** (new_size > old_size) may extend in place, returning the same
        ///   \p ptr, or **relocate** to a new address — in which case the old \p ptr is
        ///   freed and MUST NOT be used again; only the returned pointer is valid.
        /// - A **shrink** (0 < new_size < old_size) **never relocates**: it returns the
        ///   same \p ptr, either reclaiming the tail in place or leaving the block
        ///   unchanged. Reclamation is best-effort and never guaranteed, but the pointer
        ///   is guaranteed stable. (A shrink to new_size == 0 is a *free*, not a shrink,
        ///   and does invalidate \p ptr.)
        ///
        /// \par Content preservation
        /// On resize, the first min(old_size, new_size) bytes are preserved. If the
        /// block relocates, those bytes are moved with a **raw byte copy** (memcpy).
        /// Therefore resize is only safe for **trivially-relocatable** contents;
        /// callers holding objects that are not safe to move byte-wise (self-referential
        /// types, etc.) must NOT use resize to relocate them — allocate a new block and
        /// move-construct manually instead.
        ///
        /// \par Failure
        /// On failure the function returns nullptr and the **original block (if any)
        /// remains valid and owned by the caller** — no data is lost. (Callers that
        /// treat OOM as fatal may panic; a shrink that fails is simply ignored.)
        ///
        /// \param ctx       Implementation state; nullptr for stateless allocators.
        /// \param ptr       Block to resize/free; nullptr for alloc.
        /// \param old_size  True previously-requested size of \p ptr in bytes; 0 for
        ///                  alloc. Stateful allocators (arenas/pools) rely on this being
        ///                  correct; self-tracking allocators may ignore it.
        /// \param new_size  Requested size in bytes; 0 requests free.
        /// \param align     Required alignment in bytes (power of two). Must match the
        ///                  original \p align on every resize of the same block.
        /// \return Block of at least \p new_size bytes on alloc/resize; nullptr on free
        ///         or failure.
        void *(*realloc)(const AnonymousAllocator *self, void *ptr,
                         isize old_size, isize new_size, isize align);

        /// \brief Opaque per-allocator state, passed back as the first argument
        ///        to \ref realloc. nullptr for stateless allocators (e.g. the
        ///        global default).
        void *ctx;
    };

    /// \brief The process-wide default allocator, backed by the system allocator.
    ///
    /// Returned by reference and never mutated after startup � a default-
    /// constructed container captures this. Use a distinct \ref AnonymousAllocator
    /// (arena, pool, �) where per-subsystem control is wanted.
    ///
    /// \return Reference to the singleton default allocator.
    const AnonymousAllocator &default_allocator();

} // namespace core