#include "core/traits.h"
using namespace core;
namespace core_core_test
{
    bool test_test(){
        return true;
    }

    bool test_is_same() {
    return is_same_v<int32, int32>
        && !is_same_v<int32, int64>
        && !is_same_v<bool, int32>;
}

bool test_is_bool() {
    return is_bool_v<bool>
        && !is_bool_v<int32>
        && !is_bool_v<f32>;
}

bool test_is_integer() {
    return is_integer_v<uint8>
        && is_integer_v<int64>
        && is_integer_v<isize>
        && is_integer_v<index_t>
        && !is_integer_v<bool>
        && !is_integer_v<f32>;
}

bool test_is_float() {
    return is_float_v<f32>
        && is_float_v<f64>
        && !is_float_v<int32>
        && !is_float_v<bool>;
}
} // namespace core_test


// GAlloc correctness tests and benchmarks
// Adjust the namespace to match your module path.
//
// Benchmarks compare against ::malloc/::free from libc. If you've
// overridden the global malloc symbol to point at galloc, the comparison
// will be galloc-vs-galloc — in that case, link against libc directly
// or skip the malloc benchmarks.

#include "core/memory/galloc.h"

using namespace core;

// adjust this namespace to match your module layout
namespace core_core_test
{

// ============================================================================
//  Helpers
// ============================================================================

static void fill(void* p, usize size, uint8 pattern) {
	uint8* b = static_cast<uint8*>(p);
	for (usize i = 0; i < size; ++i) b[i] = static_cast<uint8>(pattern + (i & 0xFF));
}

static bool verify(void* p, usize size, uint8 pattern) {
	uint8* b = static_cast<uint8*>(p);
	for (usize i = 0; i < size; ++i) {
		if (b[i] != static_cast<uint8>(pattern + (i & 0xFF))) return false;
	}
	return true;
}

static bool is_aligned(void* p, usize align) {
	return (reinterpret_cast<uptr>(p) & (align - 1)) == 0;
}


// prevent the compiler from optimizing away allocations
static volatile void* bench_sink;

// ============================================================================
//  Correctness — Small allocations
// ============================================================================

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
	void* p = mlw_g_alloc.alignAlloc(4096, 4096);
	if (!p) return false;
	if (!is_aligned(p, 4096)) {
		mlw_g_alloc.free(p);
		return false;
	}
	fill(p, 4096, 0x77);
	bool ok = verify(p, 4096, 0x77);
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

} // namespace core_core_test