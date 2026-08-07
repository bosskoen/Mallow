// core/core/tests/test_galloc.cpp
//
// Coverage for core/memory/galloc.h — the process-wide thread-caching allocator
// core::mlw_g_alloc. Two halves:
//
//   1. Single-threaded CORRECTNESS across all three tiers (small <=128, medium
//      <=256KB, OS-backed large), plus alignment, realloc, coalescing, and
//      fragmentation. Every allocation is written with a per-block pattern and
//      read back so overlap/corruption is caught, not just "did it return a
//      pointer".
//   2. Concurrency: cross-thread free (the remote_free path), concurrent
//      alloc/free, producer/consumer, and thread-exit orphan migration. These
//      spawn workers with the library's own ThreadHandle (this is a freestanding
//      tree — no std::thread), so they also lean on the thread module.
//
// The malloc-comparison benchmarks from the original reference are intentionally
// dropped: there is no libc malloc to compare against in this tree.

#include "core/memory/galloc.h"
#include "core/libc/mem.h"
#include "core/thread/thread.h"
#include "core/thread/atomic.h"
#include "core/optional.h"
#include "core/typedef.h"

using namespace core;

namespace
{
    // Shared instrumentation. Not named test_* so the runner's scan ignores them.
    void fill(void* p, usize size, uint8 pattern) {
        uint8* b = static_cast<uint8*>(p);
        for (usize i = 0; i < size; ++i) b[i] = static_cast<uint8>(pattern + (i & 0xFF));
    }
    bool verify(void* p, usize size, uint8 pattern) {
        uint8* b = static_cast<uint8*>(p);
        for (usize i = 0; i < size; ++i)
            if (b[i] != static_cast<uint8>(pattern + (i & 0xFF))) return false;
        return true;
    }
    bool is_aligned(void* p, usize align) {
        return (reinterpret_cast<uptr>(p) & (align - 1)) == 0;
    }
}

namespace core_core_test
{
// Allocate one block from each small size class, write a pattern, verify,
// then free. Tests the basic slab fast path.
bool test_small_alloc_free() {
	usize sizes[] = { 1, 8, 16, 32, 64, 128 };
	for (usize s : sizes) {
		void* p = mlw_g_alloc.alloc(s);
		if (!p) return false;
		fill(p, s, static_cast<uint8>(s));
		if (!verify(p, s, static_cast<uint8>(s))) return false;
		mlw_g_alloc.free(p);
	}
	return true;
}

// Allocate 10000 small blocks, write unique patterns to each, verify all
// patterns are intact (no overlap), then free all.
bool test_small_no_corruption() {
	constexpr int COUNT = 10000;
	constexpr usize SIZE = 32;
	void* ptrs[COUNT];

	for (int i = 0; i < COUNT; ++i) {
		ptrs[i] = mlw_g_alloc.alloc(SIZE);
		if (!ptrs[i]) return false;
		fill(ptrs[i], SIZE, static_cast<uint8>(i));
	}
	// verify all patterns survived
	for (int i = 0; i < COUNT; ++i) {
		if (!verify(ptrs[i], SIZE, static_cast<uint8>(i))) return false;
	}
	for (int i = 0; i < COUNT; ++i) {
		mlw_g_alloc.free(ptrs[i]);
	}
	return true;
}

// Free a small block, allocate again — should get the same address back
// (LIFO free list).
bool test_small_reuse() {
	void* a = mlw_g_alloc.alloc(16);
	if (!a) return false;
	mlw_g_alloc.free(a);

	void* b = mlw_g_alloc.alloc(16);
	if (!b) return false;
	bool reused = (a == b);
	mlw_g_alloc.free(b);
	return reused;
}

// Allocate enough small blocks to force at least two regions (64 KB each).
// 64 KB / 8 bytes = 8192 cells per region, minus the Region header.
bool test_small_region_pressure() {
	constexpr int COUNT = 20000; // well over one 64KB region of 8-byte cells
	void* ptrs[COUNT];
	for (int i = 0; i < COUNT; ++i) {
		ptrs[i] = mlw_g_alloc.alloc(8);
		if (!ptrs[i]) return false;
	}
	// verify we can write to all of them
	for (int i = 0; i < COUNT; ++i) {
		*static_cast<uint8*>(ptrs[i]) = static_cast<uint8>(i);
	}
	for (int i = 0; i < COUNT; ++i) {
		mlw_g_alloc.free(ptrs[i]);
	}
	return true;
}

// ============================================================================
//  Correctness — Medium allocations
// ============================================================================

// Basic medium alloc/write/verify/free for various sizes.
bool test_medium_alloc_free() {
	usize sizes[] = { 256, 512, 1024, 4096, 16384, 100000 };
	for (usize s : sizes) {
		void* p = mlw_g_alloc.alloc(s);
		if (!p) return false;
		fill(p, s, static_cast<uint8>(s >> 3));
		if (!verify(p, s, static_cast<uint8>(s >> 3))) return false;
		mlw_g_alloc.free(p);
	}
	return true;
}

// Allocate many medium blocks, verify no overlap.
bool test_medium_no_corruption() {
	constexpr int COUNT = 500;
	constexpr usize SIZE = 1024;
	void* ptrs[COUNT];

	for (int i = 0; i < COUNT; ++i) {
		ptrs[i] = mlw_g_alloc.alloc(SIZE);
		if (!ptrs[i]) return false;
		fill(ptrs[i], SIZE, static_cast<uint8>(i));
	}
	for (int i = 0; i < COUNT; ++i) {
		if (!verify(ptrs[i], SIZE, static_cast<uint8>(i))) return false;
	}
	for (int i = 0; i < COUNT; ++i) {
		mlw_g_alloc.free(ptrs[i]);
	}
	return true;
}

// Test forward coalescing: alloc A B, free A, free B, then alloc combined size.
// If coalescing works, the combined block can satisfy a larger request.
bool test_medium_coalesce_forward() {
	constexpr usize S = 256;
	void* a = mlw_g_alloc.alloc(S);
	void* b = mlw_g_alloc.alloc(S);
	void* c = mlw_g_alloc.alloc(S); // keep c alive to prevent region release
	if (!a || !b || !c) return false;

	mlw_g_alloc.free(a);
	mlw_g_alloc.free(b);
	// a and b should merge. Allocate something bigger than S but <= 2*S.
	// The merged block includes headers so usable space is slightly less
	// than 2*S + 2*sizeof(Header), but S + S/2 should fit.
	void* big = mlw_g_alloc.alloc(S + S / 2);
	if (!big) { mlw_g_alloc.free(c); return false; }

	fill(big, S + S / 2, 0xAA);
	bool ok = verify(big, S + S / 2, 0xAA);
	mlw_g_alloc.free(big);
	mlw_g_alloc.free(c);
	return ok;
}

// Test backward coalescing: alloc A B, free B, free A, then alloc combined size.
bool test_medium_coalesce_backward() {
	constexpr usize S = 256;
	void* a = mlw_g_alloc.alloc(S);
	void* b = mlw_g_alloc.alloc(S);
	void* c = mlw_g_alloc.alloc(S);
	if (!a || !b || !c) return false;

	mlw_g_alloc.free(b);
	mlw_g_alloc.free(a);
	// a freed after b — backward merge should combine them
	void* big = mlw_g_alloc.alloc(S + S / 2);
	if (!big) { mlw_g_alloc.free(c); return false; }

	fill(big, S + S / 2, 0xBB);
	bool ok = verify(big, S + S / 2, 0xBB);
	mlw_g_alloc.free(big);
	mlw_g_alloc.free(c);
	return ok;
}

// Test three-way coalescing: alloc A B C, free A, free C, free B.
// Freeing B should merge all three.
bool test_medium_coalesce_three_way() {
	constexpr usize S = 256;
	void* a = mlw_g_alloc.alloc(S);
	void* b = mlw_g_alloc.alloc(S);
	void* c = mlw_g_alloc.alloc(S);
	void* d = mlw_g_alloc.alloc(S); // keep alive to prevent region release
	if (!a || !b || !c || !d) return false;

	mlw_g_alloc.free(a);
	mlw_g_alloc.free(c);
	mlw_g_alloc.free(b); // should merge with both a and c
	// merged block should fit ~3 * S
	void* big = mlw_g_alloc.alloc(S * 2 + S / 2);
	if (!big) { mlw_g_alloc.free(d); return false; }

	fill(big, S * 2 + S / 2, 0xCC);
	bool ok = verify(big, S * 2 + S / 2, 0xCC);
	mlw_g_alloc.free(big);
	mlw_g_alloc.free(d);
	return ok;
}

// Allocate enough medium blocks to force multiple 4 MB regions.
bool test_medium_region_pressure() {
	// 4 MB region. 4096-byte blocks: ~1000 per region.
	// Allocate 2500 to guarantee at least 2 regions.
	constexpr int COUNT = 2500;
	constexpr usize SIZE = 4096;
	void* ptrs[COUNT];

	for (int i = 0; i < COUNT; ++i) {
		ptrs[i] = mlw_g_alloc.alloc(SIZE);
		if (!ptrs[i]) return false;
	}
	// spot-check a few
	for (int i = 0; i < COUNT; i += 100) {
		fill(ptrs[i], SIZE, static_cast<uint8>(i));
	}
	for (int i = 0; i < COUNT; i += 100) {
		if (!verify(ptrs[i], SIZE, static_cast<uint8>(i))) return false;
	}
	for (int i = 0; i < COUNT; ++i) {
		mlw_g_alloc.free(ptrs[i]);
	}
	return true;
}

// ============================================================================
//  Correctness — Large (OS) allocations
// ============================================================================

bool test_large_alloc_free() {
	usize sizes[] = { 300000, 1 << 20, 4 << 20 }; // 300KB, 1MB, 4MB
	for (usize s : sizes) {
		void* p = mlw_g_alloc.alloc(s);
		if (!p) return false;
		fill(p, s, 0x55);
		if (!verify(p, s, 0x55)) return false;
		mlw_g_alloc.free(p);
	}
	return true;
}

// ============================================================================
//  Correctness — Alignment
// ============================================================================

bool test_alignment() {
	usize aligns[] = { 8, 16, 32, 64, 128, 256 };
	usize sizes[]  = { 1, 64, 256, 1024, 8192 };

	for (usize a : aligns) {
		for (usize s : sizes) {
			void* p = mlw_g_alloc.alignAlloc(s, a);
			if (!p) return false;
			if (!is_aligned(p, a)) {
				println("    alignment fail: size={} align={} ptr={}\n",
					s, a, p);
				mlw_g_alloc.free(p);
				return false;
			}
			// write and verify to catch overlapping headers
			fill(p, s, static_cast<uint8>(a));
			if (!verify(p, s, static_cast<uint8>(a))) {
				mlw_g_alloc.free(p);
				return false;
			}
			mlw_g_alloc.free(p);
		}
	}
	return true;
}

// Page-aligned allocation should go through the OS path.
bool test_alignment_page() {
	usize ps = core::PLATFORM_INFO.page_size;
	void* p = mlw_g_alloc.alignAlloc(ps, ps);
	if (!p) return false;
	if (!is_aligned(p, ps)) {
		mlw_g_alloc.free(p);
		return false;
	}
	fill(p, ps, 0x77);
	bool ok = verify(p, ps, 0x77);
	mlw_g_alloc.free(p);
	return ok;
}

// Invalid alignments should return nullptr.
bool test_alignment_invalid() {
	// not a power of two
	if (mlw_g_alloc.alignAlloc(64, 3) != nullptr) return false;
	if (mlw_g_alloc.alignAlloc(64, 7) != nullptr) return false;
	// above 256 (and not PAGE_SIZE)
	if (mlw_g_alloc.alignAlloc(64, 512) != nullptr) return false;
	return true;
}

// ============================================================================
//  Correctness — Realloc
// ============================================================================

bool test_realloc_grow() {
	constexpr usize INITIAL = 256;
	constexpr usize GROWN = 2048;

	void* p = mlw_g_alloc.alloc(INITIAL);
	if (!p) return false;
	fill(p, INITIAL, 0xDD);

	void* q = mlw_g_alloc.realloc(p, GROWN);
	if (!q) return false;
	// original data must survive
	if (!verify(q, INITIAL, 0xDD)) {
		mlw_g_alloc.free(q);
		return false;
	}
	// can write into the grown region
	fill(q, GROWN, 0xEE);
	bool ok = verify(q, GROWN, 0xEE);
	mlw_g_alloc.free(q);
	return ok;
}

// realloc(nullptr, n) should act as alloc.
bool test_realloc_null() {
	void* p = mlw_g_alloc.realloc(nullptr, 64);
	if (!p) return false;
	fill(p, 64, 0x11);
	bool ok = verify(p, 64, 0x11);
	mlw_g_alloc.free(p);
	return ok;
}

// realloc(ptr, 0) should act as free.
bool test_realloc_zero() {
	void* p = mlw_g_alloc.alloc(128);
	if (!p) return false;
	void* q = mlw_g_alloc.realloc(p, 0);
	// should return nullptr and not crash
	return q == nullptr;
}

// realloc with same or smaller size should return the same pointer.
bool test_realloc_same_size() {
	void* p = mlw_g_alloc.alloc(512);
	if (!p) return false;
	fill(p, 512, 0x22);

	void* q = mlw_g_alloc.realloc(p, 512);
	if (q != p) {
		mlw_g_alloc.free(q);
		return false;
	}
	// smaller request should also keep the same pointer
	void* r = mlw_g_alloc.realloc(p, 256);
	bool ok = (r == p) && verify(r, 256, 0x22);
	mlw_g_alloc.free(r);
	return ok;
}

// realloc should try in-place growth for medium blocks when the next
// block is free. Alloc A B, free B, realloc A to cover A+B space.
bool test_realloc_inplace_grow() {
	constexpr usize S = 256;
	void* a = mlw_g_alloc.alloc(S);
	void* b = mlw_g_alloc.alloc(S);
	void* c = mlw_g_alloc.alloc(S); // prevent region release
	if (!a || !b || !c) return false;

	fill(a, S, 0x44);
	mlw_g_alloc.free(b);

	// realloc a — should extend into b's space without moving
	void* grown = mlw_g_alloc.realloc(a, S + S / 2);
	if (!grown) { mlw_g_alloc.free(c); return false; }

	// original data must survive
	bool ok = verify(grown, S, 0x44);
	// ideally same pointer (in-place), but not guaranteed by the API
	mlw_g_alloc.free(grown);
	mlw_g_alloc.free(c);
	return ok;
}

// ============================================================================
//  Correctness — Mixed workload
// ============================================================================

// Interleave allocations of different sizes, free in random-ish order.
bool test_mixed_sizes() {
	constexpr int COUNT = 300;
	void* ptrs[COUNT];
	usize sizes[COUNT];

	// allocate a mix of small, medium, large
	for (int i = 0; i < COUNT; ++i) {
		if (i % 5 == 0)      sizes[i] = 16;       // small
		else if (i % 5 == 1) sizes[i] = 64;       // small
		else if (i % 5 == 2) sizes[i] = 512;      // medium
		else if (i % 5 == 3) sizes[i] = 8192;     // medium
		else                  sizes[i] = 300000;   // large (OS)

		ptrs[i] = mlw_g_alloc.alloc(sizes[i]);
		if (!ptrs[i]) return false;
		fill(ptrs[i], sizes[i], static_cast<uint8>(i));
	}

	// verify all
	for (int i = 0; i < COUNT; ++i) {
		if (!verify(ptrs[i], sizes[i], static_cast<uint8>(i))) return false;
	}

	// free in a stride pattern (not sequential, not fully random)
	for (int i = 0; i < COUNT; i += 3) mlw_g_alloc.free(ptrs[i]);
	for (int i = 1; i < COUNT; i += 3) mlw_g_alloc.free(ptrs[i]);
	for (int i = 2; i < COUNT; i += 3) mlw_g_alloc.free(ptrs[i]);

	return true;
}

// Free in the middle, re-alloc, verify the allocator handles fragmentation.
bool test_fragmentation_resilience() {
	constexpr int COUNT = 200;
	constexpr usize SIZE = 512;
	void* ptrs[COUNT];

	for (int i = 0; i < COUNT; ++i) {
		ptrs[i] = mlw_g_alloc.alloc(SIZE);
		if (!ptrs[i]) return false;
	}
	// free every other block — creates a checkerboard
	for (int i = 0; i < COUNT; i += 2) {
		mlw_g_alloc.free(ptrs[i]);
		ptrs[i] = nullptr;
	}
	// allocate the same size again — should reuse freed blocks
	for (int i = 0; i < COUNT; i += 2) {
		ptrs[i] = mlw_g_alloc.alloc(SIZE);
		if (!ptrs[i]) return false;
		fill(ptrs[i], SIZE, static_cast<uint8>(i));
	}
	for (int i = 0; i < COUNT; i += 2) {
		if (!verify(ptrs[i], SIZE, static_cast<uint8>(i))) return false;
	}
	for (int i = 0; i < COUNT; ++i) {
		mlw_g_alloc.free(ptrs[i]);
	}
	return true;
}

// ============================================================================
//  Correctness — Edge cases
// ============================================================================

bool test_free_null() {
	// should be a no-op, not crash
	mlw_g_alloc.free(nullptr);
	return true;
}

bool test_alloc_zero() {
	// allocating 0 bytes — implementation-defined but shouldn't crash
	void* p = mlw_g_alloc.alloc(0);
	// either nullptr or a valid pointer is acceptable
	if (p) mlw_g_alloc.free(p);
	return true;
}

// ============================================================================
//  Concurrency (ported to ThreadHandle; benchmarks dropped)
// ============================================================================

using namespace core::sync;

// Cross-thread free (small): allocate on the main thread, free on another.
// Exercises the remote_free push + owner-side drain path.
bool test_cross_thread_free_small() {
    constexpr int COUNT = 10000;
    constexpr usize SIZE = 32;
    struct Ctx { void* ptrs[COUNT]; } ctx;
    for (int i = 0; i < COUNT; ++i) {
        ctx.ptrs[i] = mlw_g_alloc.alloc(SIZE);
        if (!ctx.ptrs[i]) return false;
        fill(ctx.ptrs[i], SIZE, static_cast<uint8>(i));
    }
    for (int i = 0; i < COUNT; ++i)
        if (!verify(ctx.ptrs[i], SIZE, static_cast<uint8>(i))) return false;

    ThreadHandle h{ [&ctx] { for (int i = 0; i < COUNT; ++i) mlw_g_alloc.free(ctx.ptrs[i]); return 0; } };
    if (h.spawn().isErr()) return false;
    h.join();

    void* probe = mlw_g_alloc.alloc(SIZE);
    if (!probe) return false;
    mlw_g_alloc.free(probe);
    return true;
}

// Cross-thread free (medium): same, in the coalescing tier.
bool test_cross_thread_free_medium() {
    constexpr int COUNT = 1000;
    constexpr usize SIZE = 512;
    struct Ctx { void* ptrs[COUNT]; } ctx;
    for (int i = 0; i < COUNT; ++i) {
        ctx.ptrs[i] = mlw_g_alloc.alloc(SIZE);
        if (!ctx.ptrs[i]) return false;
        fill(ctx.ptrs[i], SIZE, static_cast<uint8>(i));
    }
    for (int i = 0; i < COUNT; ++i)
        if (!verify(ctx.ptrs[i], SIZE, static_cast<uint8>(i))) return false;

    ThreadHandle h{ [&ctx] { for (int i = 0; i < COUNT; ++i) mlw_g_alloc.free(ctx.ptrs[i]); return 0; } };
    if (h.spawn().isErr()) return false;
    h.join();

    void* probe = mlw_g_alloc.alloc(SIZE);
    if (!probe) return false;
    mlw_g_alloc.free(probe);
    return true;
}

// Two threads each allocate/write/verify/free the same size class concurrently.
bool test_concurrent_alloc_free() {
    constexpr int COUNT = 20000;
    constexpr usize SIZE = 64;
    struct Ctx { Atomic<bool> failure{false}; uint8 pattern; };
    Ctx a; a.pattern = 0xAA;
    Ctx b; b.pattern = 0xBB;

    auto body = [](Ctx* c) {
        for (int i = 0; i < COUNT; ++i) {
            void* p = mlw_g_alloc.alloc(SIZE);
            if (!p) { c->failure.store(true, MemoryOrder::Relaxed); return; }
            fill(p, SIZE, c->pattern);
            if (!verify(p, SIZE, c->pattern)) { c->failure.store(true, MemoryOrder::Relaxed); mlw_g_alloc.free(p); return; }
            mlw_g_alloc.free(p);
        }
    };
    ThreadHandle h1{ [&a, body] { body(&a); return 0; } };
    ThreadHandle h2{ [&b, body] { body(&b); return 0; } };
    if (h1.spawn().isErr()) return false;
    if (h2.spawn().isErr()) { h1.join(); return false; }
    h1.join(); h2.join();
    return !a.failure.load(MemoryOrder::Relaxed) && !b.failure.load(MemoryOrder::Relaxed);
}

// Four threads, four different size classes, concurrently.
bool test_concurrent_mixed_sizes() {
    constexpr int COUNT = 10000;
    struct Ctx { Atomic<bool> failure{false}; usize size; uint8 pattern; };
    Ctx c[4];
    c[0].size = 8;    c[0].pattern = 0x11;
    c[1].size = 64;   c[1].pattern = 0x22;
    c[2].size = 256;  c[2].pattern = 0x33;
    c[3].size = 4096; c[3].pattern = 0x44;

    auto make = [&](int i) {
        return [ci = &c[i]] {
            for (int k = 0; k < COUNT; ++k) {
                void* p = mlw_g_alloc.alloc(ci->size);
                if (!p) { ci->failure.store(true, MemoryOrder::Relaxed); return 0; }
                fill(p, ci->size, ci->pattern);
                if (!verify(p, ci->size, ci->pattern)) { ci->failure.store(true, MemoryOrder::Relaxed); mlw_g_alloc.free(p); return 0; }
                mlw_g_alloc.free(p);
            }
            return 0;
        };
    };
    using Fn = decltype(make(0));
    Optional<ThreadHandle<Fn>> hs[4];
    for (int i = 0; i < 4; ++i) hs[i].emplace(make(i));
    for (int i = 0; i < 4; ++i) if (hs[i].unwrap().spawn().isErr()) return false;
    for (int i = 0; i < 4; ++i) hs[i].unwrap().join();
    for (int i = 0; i < 4; ++i) if (c[i].failure.load(MemoryOrder::Relaxed)) return false;
    return true;
}

// Producer allocates + fills; consumer verifies + frees across a ring buffer.
bool test_producer_consumer() {
    constexpr int COUNT = 20000;
    constexpr usize SIZE = 128;
    struct Ctx {
        void* ring[1024];
        Atomic<int> write_idx{0};
        Atomic<int> read_idx{0};
        Atomic<bool> producer_done{false};
        Atomic<bool> failure{false};
    } ctx;

    ThreadHandle prod{ [&ctx] {
        for (int i = 0; i < COUNT; ++i) {
            while (ctx.write_idx.load(MemoryOrder::Acquire) - ctx.read_idx.load(MemoryOrder::Acquire) >= 1023) {}
            void* p = mlw_g_alloc.alloc(SIZE);
            if (!p) { ctx.failure.store(true, MemoryOrder::Relaxed); return 0; }
            fill(p, SIZE, static_cast<uint8>(i));
            int w = ctx.write_idx.load(MemoryOrder::Relaxed);
            ctx.ring[w % 1024] = p;
            ctx.write_idx.store(w + 1, MemoryOrder::Release);
        }
        ctx.producer_done.store(true, MemoryOrder::Release);
        return 0;
    } };
    ThreadHandle cons{ [&ctx] {
        int i = 0;
        while (true) {
            int w = ctx.write_idx.load(MemoryOrder::Acquire);
            int r = ctx.read_idx.load(MemoryOrder::Relaxed);
            if (r >= w) {
                if (ctx.producer_done.load(MemoryOrder::Acquire) && r >= ctx.write_idx.load(MemoryOrder::Acquire)) break;
                continue;
            }
            void* p = ctx.ring[r % 1024];
            if (!verify(p, SIZE, static_cast<uint8>(i))) { ctx.failure.store(true, MemoryOrder::Relaxed); mlw_g_alloc.free(p); return 0; }
            mlw_g_alloc.free(p);
            ctx.read_idx.store(r + 1, MemoryOrder::Release);
            ++i;
        }
        return 0;
    } };
    if (prod.spawn().isErr()) return false;
    if (cons.spawn().isErr()) { prod.join(); return false; }
    prod.join(); cons.join();
    return !ctx.failure.load(MemoryOrder::Relaxed);
}

// A worker thread allocates a batch, then exits; the MAIN thread verifies and
// frees them afterwards — the region must survive the owner's exit (orphan
// migration) and stay usable cross-thread.
bool test_thread_exit_orphan_migration() {
    constexpr int COUNT = 500;
    constexpr usize SIZE = 256;
    struct Ctx { void* ptrs[COUNT]; } ctx;

    ThreadHandle h{ [&ctx] {
        for (int i = 0; i < COUNT; ++i) {
            ctx.ptrs[i] = mlw_g_alloc.alloc(SIZE);
            if (ctx.ptrs[i]) fill(ctx.ptrs[i], SIZE, static_cast<uint8>(i));
        }
        return 0;
    } };
    if (h.spawn().isErr()) return false;
    h.join();  // worker has now exited

    for (int i = 0; i < COUNT; ++i) {
        if (!ctx.ptrs[i]) return false;
        if (!verify(ctx.ptrs[i], SIZE, static_cast<uint8>(i))) return false;
    }
    for (int i = 0; i < COUNT; ++i) mlw_g_alloc.free(ctx.ptrs[i]);

    void* probe = mlw_g_alloc.alloc(SIZE);
    if (!probe) return false;
    fill(probe, SIZE, 0xFF);
    bool ok = verify(probe, SIZE, 0xFF);
    mlw_g_alloc.free(probe);
    return ok;
}

// Same, mixing small size classes on the exiting thread.
bool test_thread_exit_orphan_small() {
    constexpr int COUNT = 2000;
    struct Ctx { void* ptrs[COUNT]; usize sizes[COUNT]; } ctx;

    ThreadHandle h{ [&ctx] {
        for (int i = 0; i < COUNT; ++i) {
            usize s = (i % 4) == 0 ? 8 : (i % 4) == 1 ? 32 : (i % 4) == 2 ? 64 : 128;
            ctx.sizes[i] = s;
            ctx.ptrs[i] = mlw_g_alloc.alloc(s);
            if (ctx.ptrs[i]) fill(ctx.ptrs[i], s, static_cast<uint8>(i));
        }
        return 0;
    } };
    if (h.spawn().isErr()) return false;
    h.join();

    for (int i = 0; i < COUNT; ++i) {
        if (!ctx.ptrs[i]) return false;
        if (!verify(ctx.ptrs[i], ctx.sizes[i], static_cast<uint8>(i))) return false;
    }
    for (int i = 0; i < COUNT; ++i) mlw_g_alloc.free(ctx.ptrs[i]);
    return true;
}

// Many threads spawn, allocate, and exit in sequence; each batch is verified and
// freed by the main thread, repeatedly exercising orphan adoption.
bool test_many_threads_exit() {
    constexpr int THREADS = 16;
    constexpr int PER = 200;
    constexpr usize SIZE = 64;
    struct Ctx { void* ptrs[PER]; int id; } ctxs[THREADS];

    for (int t = 0; t < THREADS; ++t) {
        ctxs[t].id = t;
        ThreadHandle h{ [c = &ctxs[t]] {
            for (int i = 0; i < PER; ++i) {
                c->ptrs[i] = mlw_g_alloc.alloc(SIZE);
                if (c->ptrs[i]) fill(c->ptrs[i], SIZE, static_cast<uint8>(c->id + i));
            }
            return 0;
        } };
        if (h.spawn().isErr()) return false;
        h.join();
    }
    for (int t = 0; t < THREADS; ++t)
        for (int i = 0; i < PER; ++i) {
            if (!ctxs[t].ptrs[i]) return false;
            if (!verify(ctxs[t].ptrs[i], SIZE, static_cast<uint8>(t + i))) return false;
        }
    for (int t = 0; t < THREADS; ++t)
        for (int i = 0; i < PER; ++i) mlw_g_alloc.free(ctxs[t].ptrs[i]);
    return true;
}

// Stress: 4 threads allocate their own blocks, then in a barrier-synchronized
// second phase each frees ANOTHER thread's blocks (cross-thread), then frees the
// rest of its own. Phases are ordered with an atomic counter.
bool test_stress_cross_thread() {
    constexpr int THREADS = 4;
    constexpr int COUNT = 5000;
    constexpr usize SIZE = 128;
    struct Shared {
        void* ptrs[THREADS][COUNT];
        Atomic<int> phase{0};
        Atomic<bool> failure{false};
    } shared;

    auto make = [&](int id) {
        return [s = &shared, id] {
            for (int i = 0; i < COUNT; ++i) {
                s->ptrs[id][i] = mlw_g_alloc.alloc(SIZE);
                if (!s->ptrs[id][i]) { s->failure.store(true, MemoryOrder::Relaxed); return 0; }
                fill(s->ptrs[id][i], SIZE, static_cast<uint8>(id * 37 + i));
            }
            s->phase.fetchAdd(1, MemoryOrder::AcqRel);
            while (s->phase.load(MemoryOrder::Acquire) < THREADS) {}

            for (int i = 0; i < COUNT; ++i)
                if (!verify(s->ptrs[id][i], SIZE, static_cast<uint8>(id * 37 + i))) { s->failure.store(true, MemoryOrder::Relaxed); return 0; }

            int target = (id + 1) % THREADS;
            for (int i = 0; i < COUNT / 2; ++i) mlw_g_alloc.free(s->ptrs[target][i]);

            s->phase.fetchAdd(1, MemoryOrder::AcqRel);
            while (s->phase.load(MemoryOrder::Acquire) < THREADS * 2) {}

            for (int i = COUNT / 2; i < COUNT; ++i) mlw_g_alloc.free(s->ptrs[id][i]);
            return 0;
        };
    };
    using Fn = decltype(make(0));
    Optional<ThreadHandle<Fn>> hs[THREADS];
    for (int i = 0; i < THREADS; ++i) hs[i].emplace(make(i));
    for (int i = 0; i < THREADS; ++i) if (hs[i].unwrap().spawn().isErr()) return false;
    for (int i = 0; i < THREADS; ++i) hs[i].unwrap().join();

    void* probe = mlw_g_alloc.alloc(SIZE);
    if (probe) mlw_g_alloc.free(probe);
    return !shared.failure.load(MemoryOrder::Relaxed);
}

} // namespace core_core_test
