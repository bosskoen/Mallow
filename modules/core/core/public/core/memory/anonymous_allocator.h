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
    /// All sizes are signed (\ref isize) — the unsigned OS boundary is crossed
    /// only inside a concrete \c realloc implementation, never here.
    struct AnonymousAllocator
    {
        /// \brief Single entry point for allocate, resize, and free.
        ///
        /// The operation is selected by the arguments:
        /// - **alloc**  : \p ptr is nullptr, \p old_size is 0, \p new_size > 0.
        /// - **free**   : \p new_size is 0 (returns nullptr).
        /// - **resize** : \p ptr is non-null, \p old_size > 0, \p new_size > 0.
        ///
        /// On resize the returned block is guaranteed to hold **at least**
        /// \p new_size bytes; an implementation may return a larger block and is
        /// **not required to shrink** (a shrink request may return the same
        /// pointer unchanged). Callers must treat capacity reclamation as
        /// best-effort.
        ///
        /// On alloc/resize the returned block is aligned to **at least**
        /// \p align bytes. \p align must be a power of two.
        ///
        /// \param ctx       Implementation state (\ref ctx); nullptr for stateless allocators.
        /// \param ptr       Existing block for resize/free; nullptr for alloc.
        /// \param old_size  Current size of \p ptr in bytes; 0 for alloc. May be
        ///                  used by arenas/pools; stateful allocators that track
        ///                  their own sizes may ignore it.
        /// \param new_size  Requested size in bytes; 0 requests free.
        /// \param align     Required alignment in bytes (power of two).
        /// \return Pointer to a block of at least \p new_size bytes on
        ///         alloc/resize; nullptr on failure or on free. On failure the
        ///         original block (if any) remains valid and owned by the caller.
        void* (*realloc)(void* ctx, void* ptr,
            isize old_size, isize new_size, isize align);

        /// \brief Opaque per-allocator state, passed back as the first argument
        ///        to \ref realloc. nullptr for stateless allocators (e.g. the
        ///        global default).
        void* ctx;
    };

    /// \brief The process-wide default allocator, backed by the system allocator.
    ///
    /// Returned by reference and never mutated after startup — a default-
    /// constructed container captures this. Use a distinct \ref AnonymousAllocator
    /// (arena, pool, …) where per-subsystem control is wanted.
    ///
    /// \return Reference to the singleton default allocator.
    const AnonymousAllocator& default_allocator();

} // namespace core