// core/core/tests/test_memory.cpp
//
// Coverage for core/memory/memory.h: the three instance allocators Arena, Pool,
// and ObjectPool<T>. These are standalone (own one OS reservation each) and do
// NOT run destructors on teardown — object lifetime is the caller's job — so the
// tests here drive raw storage and, for ObjectPool, an instrumented type that
// counts ctor/dtor calls to prove create()/destroy() pair up exactly.
//
// Data-integrity checks write a per-allocation pattern and read it back to catch
// overlap; alignment checks verify the returned pointer's low bits. All three
// allocators are move-only (copy deleted), and move must transfer the
// reservation and empty the source — that invariant is checked directly.

#include "core/memory/memory.h"
#include "core/typedef.h"

using namespace core;

namespace
{
    void fill(void* p, usize size, uint8 pattern)
    {
        uint8* b = static_cast<uint8*>(p);
        for (usize i = 0; i < size; ++i) b[i] = static_cast<uint8>(pattern + (i & 0xFF));
    }
    bool verify(void* p, usize size, uint8 pattern)
    {
        const uint8* b = static_cast<const uint8*>(p);
        for (usize i = 0; i < size; ++i)
            if (b[i] != static_cast<uint8>(pattern + (i & 0xFF))) return false;
        return true;
    }
    bool isAligned(void* p, usize align)
    {
        return (reinterpret_cast<uptr>(p) & (align - 1)) == 0;
    }

    // Instrumented object for ObjectPool: tracks construction/destruction so a
    // leaked or double-run destructor is visible. Not named test_*.
    struct Counter
    {
        static inline int live = 0, ctor = 0, dtor = 0;
        static void reset() { live = ctor = dtor = 0; }
        int a, b;
        Counter(int x, int y) : a(x), b(y) { ++live; ++ctor; }
        ~Counter() { --live; ++dtor; }
    };

    // Over-aligned type to check ObjectPool honors alignof(T).
    struct alignas(64) Over64 { uint8 bytes[64]; };
}

namespace core_core_test
{
    // =======================================================================
    //  Arena — init / shutdown lifecycle
    // =======================================================================
    bool test_arena_init_shutdown()
    {
        Arena a;
        if (a.alloc(16) != nullptr) return false;      // uninitialized -> null
        if (!a.init(64 * 1024)) return false;          // reserve 64 KB
        if (a.init(64 * 1024)) return false;           // double init -> false
        if (a.base == nullptr || a.capacity < 64 * 1024) return false;
        a.shutdown();
        if (a.base != nullptr || a.offset != 0) return false;
        return a.alloc(16) == nullptr;                 // allocation after shutdown -> null
    }

    // =======================================================================
    //  Arena — bump behavior, alignment, no overlap
    // =======================================================================
    bool test_arena_bump_alloc()
    {
        Arena a;
        if (!a.init(1 << 20)) return false;            // 1 MB
        void* p0 = a.alloc(100, 8);
        void* p1 = a.alloc(200, 16);
        void* p2 = a.alloc(64, 64);
        if (!p0 || !p1 || !p2) return false;
        if (!isAligned(p0, 8) || !isAligned(p1, 16) || !isAligned(p2, 64)) return false;
        // Strictly increasing (bump pointer only advances).
        if (!(p0 < p1 && p1 < p2)) return false;
        // Distinct regions are writable without corrupting each other.
        fill(p0, 100, 0x10); fill(p1, 200, 0x20); fill(p2, 64, 0x30);
        return verify(p0, 100, 0x10) && verify(p1, 200, 0x20) && verify(p2, 64, 0x30);
    }

    // =======================================================================
    //  Arena — exhaustion returns null (and doesn't corrupt)
    // =======================================================================
    bool test_arena_exhaustion()
    {
        Arena a;
        if (!a.init(64 * 1024)) return false;
        usize cap = a.capacity;
        void* big = a.alloc(cap, 8);                   // consume ~everything
        if (!big) return false;
        // A further allocation that can't fit must fail cleanly.
        if (a.alloc(cap, 8) != nullptr) return false;
        return true;
    }

    // =======================================================================
    //  Arena — mark / freeTo(mark) / reset rewind
    // =======================================================================
    bool test_arena_mark_reset()
    {
        Arena a;
        if (!a.init(1 << 20)) return false;
        void* first = a.alloc(128, 8);
        usize m = a.mark();
        void* second = a.alloc(128, 8);
        if (!first || !second) return false;
        a.freeTo(m);                                   // rewind to the mark
        void* reused = a.alloc(128, 8);
        if (reused != second) return false;            // same slot handed out again
        // freeTo(ptr) rewinds so `first` is the next allocation point.
        a.freeTo(first);
        if (a.alloc(128, 8) != first) return false;
        // reset drops everything back to base.
        a.reset();
        if (a.offset != 0) return false;
        if (a.alloc(1, 1) != a.base) return false;     // first alloc lands at base (align 1)
        return true;
    }

    // =======================================================================
    //  Arena — move transfers the reservation and empties the source
    // =======================================================================
    bool test_arena_move()
    {
        Arena a;
        if (!a.init(1 << 20)) return false;
        void* p = a.alloc(256, 8);
        fill(p, 256, 0x5A);
        void* base_before = a.base;

        Arena b = core::move(a);                       // move-construct
        if (a.base != nullptr) return false;           // source emptied
        if (b.base != base_before) return false;       // reservation transferred
        if (!verify(p, 256, 0x5A)) return false;       // data still valid through b

        Arena c;
        if (!c.init(4096)) return false;
        c = core::move(b);                             // move-assign (frees c's own first)
        if (b.base != nullptr) return false;
        return c.base == base_before;
    }

    // =======================================================================
    //  Pool — init, block handout, LIFO reuse
    // =======================================================================
    bool test_pool_alloc_free()
    {
        Pool p;
        if (!p.init(64 * 1024, 32)) return false;
        void* a = p.alloc();
        void* b = p.alloc();
        if (!a || !b || a == b) return false;
        // free then alloc: LIFO free list hands the last-freed block back first.
        p.free(a);
        void* c = p.alloc();
        if (c != a) return false;
        p.free(b);
        p.free(c);
        p.free(nullptr);                               // no-op, must not crash
        return true;
    }

    // =======================================================================
    //  Pool — exhaust every block, then recover after frees
    // =======================================================================
    bool test_pool_exhaustion()
    {
        Pool p;
        if (!p.init(16 * 1024, 64)) return false;
        // block_size is raised to >= sizeof(void*) and rounded to alignof(void*);
        // 64 already satisfies that, so count == capacity / 64.
        usize count = p.capacity / p.block_size;
        if (count == 0) return false;
        // Drain the pool: keep the last block to free it and prove recovery.
        void* last = nullptr;
        usize got = 0;
        for (;;)
        {
            void* q = p.alloc();
            if (!q) break;
            last = q;
            ++got;
        }
        if (got != count) return false;                // handed out exactly count blocks
        if (p.alloc() != nullptr) return false;        // empty now
        p.free(last);
        if (p.alloc() != last) return false;           // one back in circulation
        return true;
    }

    // =======================================================================
    //  Pool — small block_size is raised to at least sizeof(void*)
    // =======================================================================
    bool test_pool_min_block_size()
    {
        Pool p;
        if (!p.init(4096, 1)) return false;            // 1 byte requested
        if (p.block_size < sizeof(void*)) return false;// must be raised
        // Two consecutive blocks must be at least block_size apart (no overlap of
        // the embedded free-list link).
        void* a = p.alloc();
        void* b = p.alloc();
        if (!a || !b) return false;
        usize gap = static_cast<usize>(
            reinterpret_cast<uint8*>(b) > reinterpret_cast<uint8*>(a)
                ? reinterpret_cast<uint8*>(b) - reinterpret_cast<uint8*>(a)
                : reinterpret_cast<uint8*>(a) - reinterpret_cast<uint8*>(b));
        return gap >= sizeof(void*);
    }

    // =======================================================================
    //  Pool — move transfers reservation and empties source
    // =======================================================================
    bool test_pool_move()
    {
        Pool p;
        if (!p.init(8192, 32)) return false;
        void* base_before = p.base;
        void* a = p.alloc();
        if (!a) return false;

        Pool q = core::move(p);
        if (p.base != nullptr || p.first_free != nullptr) return false;  // source emptied
        if (q.base != base_before) return false;
        q.free(a);
        return q.alloc() == a;                         // still functional after move
    }

    // =======================================================================
    //  ObjectPool<T> — create runs the ctor, destroy runs the dtor, exactly once
    // =======================================================================
    bool test_object_pool_create_destroy()
    {
        Counter::reset();
        {
            ObjectPool<Counter> op;
            if (!op.init(16)) return false;
            Counter* c = op.create(3, 4);
            if (!c) return false;
            if (c->a != 3 || c->b != 4) return false;  // ctor args forwarded
            if (Counter::ctor != 1 || Counter::live != 1) return false;
            op.destroy(c);                             // runs ~Counter
            if (Counter::dtor != 1 || Counter::live != 0) return false;
            op.destroy(nullptr);                       // no-op
            if (Counter::dtor != 1) return false;      // still one
        }
        // ObjectPool teardown does NOT run destructors, so a live object would
        // leak; we destroyed it above, so live must be balanced.
        return Counter::live == 0;
    }

    // =======================================================================
    //  ObjectPool<T> — exhaustion returns nullptr, recovery after destroy
    // =======================================================================
    // init(count) provisions AT LEAST count objects, but Pool::init page-rounds
    // the reservation, so the real capacity is usually larger. Compute it from
    // the pool rather than assuming it equals the requested count.
    bool test_object_pool_exhaustion()
    {
        Counter::reset();
        ObjectPool<Counter> op;
        constexpr usize N = 8;
        if (!op.init(N)) return false;
        usize cap = op.pool.capacity / op.pool.block_size;
        if (cap < N) return false;                         // must honor the minimum

        // Drain fully with O(1) storage: keep only the last block to prove
        // recovery. (The remaining objects are intentionally left live — this
        // test targets exhaustion/recovery; the ctor/dtor balance is covered by
        // test_object_pool_create_destroy.)
        Counter* last = nullptr;
        usize got = 0;
        for (;;)
        {
            Counter* c = op.create((int)(got & 0x7fffffff), 0);
            if (!c) break;
            last = c;
            ++got;
        }
        if (got != cap) return false;                      // handed out exactly capacity
        if (op.create(1, 1) != nullptr) return false;      // exhausted
        op.destroy(last);                                  // free one slot
        Counter* again = op.create(7, 7);                  // reuse it
        return again != nullptr;
    }

    // =======================================================================
    //  ObjectPool<T> — honors alignof(T) for over-aligned types
    // =======================================================================
    bool test_object_pool_alignment()
    {
        ObjectPool<Over64> op;
        if (!op.init(4)) return false;
        Over64* a = op.create();
        Over64* b = op.create();
        if (!a || !b) return false;
        bool ok = isAligned(a, alignof(Over64)) && isAligned(b, alignof(Over64));
        op.destroy(a);
        op.destroy(b);
        return ok;
    }
}
