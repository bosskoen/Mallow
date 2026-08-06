// core/core/tests/test_mem.cpp
//
// Coverage for core/libc/mem.h.
//
// The mem* / strlen functions are defined out-of-line in the freestanding TU
// (compiled -fno-builtin so the loops are not rewritten into calls to
// themselves), so every check here is runtime. The allocator wrappers
// (mlwMalloc/mlwAlignedAlloc/mlwFree/mlwRealloc) are thin forwards to the
// process-wide GAlloc, which test.cpp already exercises hard — so they get a
// light smoke test here (allocate, write, alignment, free round-trips) rather
// than a second full allocator suite.
//
// All buffers are on-stack fixed arrays; nothing here depends on libc.

#include "core/libc/mem.h"
#include "core/typedef.h"

using namespace core;

namespace
{
    // Byte-wise equality over a range. Not named test_* so the runner ignores it.
    bool bytesEqual(const void* a, const void* b, usize n) noexcept
    {
        const auto* pa = static_cast<const unsigned char*>(a);
        const auto* pb = static_cast<const unsigned char*>(b);
        for (usize i = 0; i < n; ++i)
            if (pa[i] != pb[i]) return false;
        return true;
    }
}

namespace core_core_test
{
    // =======================================================================
    //  mlwMemset
    // =======================================================================
    bool test_mem_memset()
    {
        unsigned char buf[16];
        // Poison, then set a middle span; check the edges are untouched.
        for (auto& b : buf) b = 0x11;
        void* r = mlwMemset(buf + 4, 0xAB, 8);
        if (r != buf + 4) return false;                 // returns destination
        for (int i = 0; i < 4; ++i)  if (buf[i] != 0x11) return false;
        for (int i = 4; i < 12; ++i) if (buf[i] != 0xAB) return false;
        for (int i = 12; i < 16; ++i) if (buf[i] != 0x11) return false;
        // Only the low byte of the fill value is used.
        mlwMemset(buf, 0x1FF & 0xFF, 16);
        for (auto b : buf) if (b != 0xFF) return false;
        // Zero length is a no-op.
        buf[0] = 0x42;
        mlwMemset(buf, 0x00, 0);
        return buf[0] == 0x42;
    }

    // =======================================================================
    //  mlwMemcpy  (non-overlapping)
    // =======================================================================
    bool test_mem_memcpy()
    {
        unsigned char src[10];
        unsigned char dst[10];
        for (int i = 0; i < 10; ++i) { src[i] = static_cast<unsigned char>(i + 1); dst[i] = 0; }
        void* r = mlwMemcpy(dst, src, 10);
        if (r != dst) return false;                     // returns destination
        if (!bytesEqual(dst, src, 10)) return false;
        // Partial copy leaves the tail untouched.
        unsigned char dst2[6] = {9, 9, 9, 9, 9, 9};
        mlwMemcpy(dst2, src, 3);
        if (dst2[0] != 1 || dst2[1] != 2 || dst2[2] != 3) return false;
        if (dst2[3] != 9 || dst2[4] != 9 || dst2[5] != 9) return false;
        // Zero length is a no-op.
        unsigned char one = 0x55;
        mlwMemcpy(&one, src, 0);
        return one == 0x55;
    }

    // =======================================================================
    //  mlwMemmove  (overlap in BOTH directions — the reason memmove exists)
    // =======================================================================
    bool test_mem_memmove()
    {
        // Forward overlap: dst below src (copy up).
        unsigned char a[8] = {1, 2, 3, 4, 5, 6, 7, 8};
        mlwMemmove(a, a + 2, 6);                         // pull [3..8] down by 2
        const unsigned char expectFwd[8] = {3, 4, 5, 6, 7, 8, 7, 8};
        if (!bytesEqual(a, expectFwd, 8)) return false;

        // Backward overlap: dst above src (copy down) — the case a naive
        // forward memcpy would corrupt.
        unsigned char b[8] = {1, 2, 3, 4, 5, 6, 7, 8};
        void* r = mlwMemmove(b + 2, b, 6);               // push [1..6] up by 2
        if (r != b + 2) return false;                    // returns destination
        const unsigned char expectBwd[8] = {1, 2, 1, 2, 3, 4, 5, 6};
        if (!bytesEqual(b, expectBwd, 8)) return false;

        // Fully non-overlapping still works.
        unsigned char s[4] = {10, 20, 30, 40};
        unsigned char d[4] = {0, 0, 0, 0};
        mlwMemmove(d, s, 4);
        return bytesEqual(d, s, 4);
    }

    // =======================================================================
    //  mlwMemcmp  (sign of first differing byte; equality; zero length)
    // =======================================================================
    bool test_mem_memcmp()
    {
        const unsigned char a[5] = {1, 2, 3, 4, 5};
        const unsigned char b[5] = {1, 2, 3, 4, 5};
        const unsigned char c[5] = {1, 2, 9, 4, 5};      // differs at index 2
        if (mlwMemcmp(a, b, 5) != 0) return false;       // equal
        if (mlwMemcmp(a, c, 5) >= 0) return false;       // a < c  (3 < 9)
        if (mlwMemcmp(c, a, 5) <= 0) return false;       // c > a
        // Comparison stops at n; the trailing difference is invisible.
        if (mlwMemcmp(a, c, 2) != 0) return false;
        // Zero length compares equal regardless of contents.
        if (mlwMemcmp(a, c, 0) != 0) return false;
        // Bytes compared as unsigned: 0x80 > 0x7F.
        const unsigned char hi[1] = {0x80};
        const unsigned char lo[1] = {0x7F};
        if (mlwMemcmp(hi, lo, 1) <= 0) return false;
        return true;
    }

    // =======================================================================
    //  mlwStrlen
    // =======================================================================
    bool test_mem_strlen()
    {
        if (mlwStrlen("") != 0) return false;
        if (mlwStrlen("a") != 1) return false;
        if (mlwStrlen("hello") != 5) return false;
        if (mlwStrlen("hello world!") != 12) return false;
        // Stops at the first NUL (does not count past it).
        const char embedded[] = {'a', 'b', '\0', 'c', 'd', '\0'};
        if (mlwStrlen(embedded) != 2) return false;
        return true;
    }

    // =======================================================================
    //  Allocator forwarders  (light smoke — GAlloc itself is tested in test.cpp)
    // =======================================================================
    bool test_mem_malloc_free()
    {
        void* p = mlwMalloc(64);
        if (p == nullptr) return false;
        // Writable across the whole request.
        mlwMemset(p, 0xCD, 64);
        const auto* bytes = static_cast<const unsigned char*>(p);
        for (int i = 0; i < 64; ++i) if (bytes[i] != 0xCD) return false;
        mlwFree(p);
        // Free of nullptr is an explicit no-op (must not crash).
        mlwFree(nullptr);
        return true;
    }

    bool test_mem_aligned_alloc()
    {
        // Request a few power-of-two alignments and verify the pointer honors them.
        for (usize align = 8; align <= 256; align <<= 1)
        {
            void* p = mlwAlignedAlloc(128, align);
            if (p == nullptr) return false;
            if (reinterpret_cast<uptr>(p) % align != 0) { mlwFree(p); return false; }
            mlwFree(p);
        }
        return true;
    }

    bool test_mem_realloc()
    {
        // Grow, preserving existing contents.
        auto* p = static_cast<unsigned char*>(mlwMalloc(16));
        if (p == nullptr) return false;
        for (int i = 0; i < 16; ++i) p[i] = static_cast<unsigned char>(i);
        auto* q = static_cast<unsigned char*>(mlwRealloc(p, 64));
        if (q == nullptr) return false;
        for (int i = 0; i < 16; ++i) if (q[i] != static_cast<unsigned char>(i)) { mlwFree(q); return false; }
        mlwFree(q);
        return true;
    }

    // =======================================================================
    //  PLATFORM_INFO invariants
    // =======================================================================
    // PLATFORM_INFO is written by the CRT at startup. In a running test binary
    // the CRT has already initialized, so the geometry should be self-consistent:
    // page_size is a power of two, and the *_mask / *_shift fields agree with it.
    bool test_mem_platform_info()
    {
        const usize ps = PLATFORM_INFO.page_size;
        // A CRT-initialized page size is a non-zero power of two.
        if (ps == 0 || (ps & (ps - 1)) != 0) return false;
        if (PLATFORM_INFO.page_mask != ps - 1) return false;
        if ((usize{1} << PLATFORM_INFO.page_shift) != ps) return false;
        const usize gr = PLATFORM_INFO.alloc_granularity;
        if (gr == 0 || (gr & (gr - 1)) != 0) return false;
        if (PLATFORM_INFO.gran_mask != gr - 1) return false;
        if ((usize{1} << PLATFORM_INFO.gran_shift) != gr) return false;
        return true;
    }
}
