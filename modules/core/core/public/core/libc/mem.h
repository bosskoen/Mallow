#pragma once

#include "../memory/galloc.h"

/// \file
/// \brief Freestanding libc runtime (mem*/strlen) and global-allocator wrappers.

namespace core
{
    /// \brief OS memory geometry, filled in once at CRT init.
    ///
    /// Populated by the CRT before any allocator use; reading it before that
    /// point yields zeroes.
    /// \note Not initialized as a constant — it is written at startup from the OS.
    extern struct PlatformInfo {
        usize page_size;          ///< Page size in bytes.
        usize page_mask;          ///< page_size - 1 (for round-to-page via AND).
        usize page_shift;         ///< log2(page_size).
        usize alloc_granularity;  ///< OS allocation granularity (Windows VirtualAlloc unit).
        usize gran_mask;          ///< alloc_granularity - 1.
        usize gran_shift;         ///< log2(alloc_granularity).
    } PLATFORM_INFO;

    /// \defgroup freestanding_libc Freestanding libc runtime
    /// \brief C-named functions the compiler emits implicitly, provided as real
    ///        out-of-line symbols with `mlw*` aliases.
    ///
    /// Even under `-ffreestanding` the compiler lowers certain idioms to calls by
    /// their C names (struct copy -> `memcpy`, array zero-init -> `memset`,
    /// scanning loops -> `strlen`), so these MUST exist as real symbols. Each has
    /// an `mlw`-prefixed alias at the same address for explicit use.
    ///
    /// \warning The translation units defining these must be compiled with
    ///          compiler idiom-recognition OFF (`-fno-builtin` etc.), or the
    ///          loops get rewritten into calls to themselves — infinite
    ///          recursion. See the build flags in the implementation files.
    /// @{

    /// \brief Copy `n` bytes from `s` to `d` (regions must not overlap).
    void* mlwMemcpy(void* d, const void* s, usize n);
    /// \brief Fill `n` bytes at `d` with byte `v`.
    void* mlwMemset(void* d, int v, usize n);
    /// \brief Copy `n` bytes from `s` to `d`, handling overlap.
    void* mlwMemmove(void* d, const void* s, usize n);
    /// \brief Compare `n` bytes; <0, 0, or >0 like C `memcmp`.
    int   mlwMemcmp(const void* a, const void* b, usize n);

    /// \brief Length of a NUL-terminated string, excluding the terminator.
    /// \note Declared here (grouped with the mem* runtime), but c_string.h
    ///       *forward-declares* it rather than including this header — that is
    ///       what breaks the macro/format/c_string -> mem include cycle. Do not
    ///       replace that forward declaration with an include.
    usize mlwStrlen(const char *str);

    /// @}

    /// \name Global allocator wrappers
    /// Thin forwards to the process-wide \ref core::GAlloc instance
    /// \ref core::mlw_g_alloc; they carry its contract exactly.
    /// @{

    /// \brief Allocate `size` bytes aligned to `alignment` from the global allocator.
    void *mlwAlignedAlloc(usize size, usize alignment);

    /// \brief Allocate `size` bytes with default alignment from the global allocator.
    void *mlwMalloc(usize size);

    /// \brief Free a global-allocator pointer. nullptr is a no-op.
    /// \note Frees a pointer from either \ref mlwMalloc or \ref mlwAlignedAlloc —
    ///       the allocator recovers the size and alignment from the block, so the
    ///       aligned and non-aligned paths share one free.
    void mlwFree(void *ptr);
    
    /// \brief Grow a global-allocator allocation. See \ref core::GAlloc::realloc.
    void *mlwRealloc(void *ptr, usize newSize);

    /// @}
}