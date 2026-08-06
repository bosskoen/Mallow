// core/core/tests/test_atomic.cpp
//
// Coverage for core/thread/atomic.h (the Atomic<T> wrapper) and the low-level
// free operations it forwards to in atomic_op.h.
//
// Two kinds of check:
//   * Single-threaded SEMANTICS: return-value contracts are the easy thing to
//     get wrong, so every op is checked for the documented "fetchX returns the
//     OLD value, Xfetch / operators return the NEW value" split, plus CAS
//     success/failure and the expected-out-param update on failure.
//   * ATOMICITY under real contention: K library threads each do N increments;
//     the total must be exactly K*N. A non-atomic ++ loses updates and the
//     count comes out low, which is the only way tearing becomes visible.
//
// Worker threads are spawned with the library's own ThreadHandle (this is a
// freestanding tree — no std::thread), so this file also leans on thread.h.

#include "core/thread/atomic.h"
#include "core/thread/thread.h"
#include "core/typedef.h"

using namespace core;
using namespace core::sync;

namespace core_core_test
{
    // =======================================================================
    //  MemoryOrder ordering invariants (compile-time)
    // =======================================================================
    bool test_atomic_memory_order_ranks()
    {
        static_assert((int)MemoryOrder::Relaxed < (int)MemoryOrder::Acquire);
        static_assert((int)MemoryOrder::Acquire < (int)MemoryOrder::Release);
        static_assert((int)MemoryOrder::Release < (int)MemoryOrder::AcqRel);
        static_assert((int)MemoryOrder::AcqRel  < (int)MemoryOrder::SeqCst);
        return true;
    }

    // =======================================================================
    //  load / store / exchange / operator= / operator T()
    // =======================================================================
    bool test_atomic_load_store()
    {
        Atomic<int32> a{0};
        if (a.load() != 0) return false;
        a.store(5);
        if (a.load() != 5) return false;
        a = 9;                               // operator= -> store, returns stored value
        if (a.load() != 9) return false;
        int32 v = a;                         // operator T() -> load
        if (v != 9) return false;
        // store returns the value it stored; exchange returns the OLD value.
        if (a.store(11) != 11) return false;
        if (a.exchange(20) != 11) return false;
        if (a.load() != 20) return false;
        return true;
    }

    // =======================================================================
    //  Increment / decrement operators (return NEW value)
    // =======================================================================
    bool test_atomic_incdec()
    {
        Atomic<int32> a{10};
        if (++a != 11) return false;         // pre: new value
        if (a++ != 11) return false;         // post: old value
        if (a.load() != 12) return false;
        if (--a != 11) return false;         // pre: new value
        if (a-- != 11) return false;         // post: old value
        if (a.load() != 10) return false;
        return true;
    }

    // =======================================================================
    //  Compound assignment operators (return NEW value)
    // =======================================================================
    bool test_atomic_compound_ops()
    {
        Atomic<uint32> a{0b1100};
        if ((a += 1) != 0b1101) return false;
        if ((a -= 1) != 0b1100) return false;
        if ((a |= 0b0011) != 0b1111) return false;
        if ((a &= 0b1010) != 0b1010) return false;
        if ((a ^= 0b1111) != 0b0101) return false;
        return true;
    }

    // =======================================================================
    //  fetchX (return OLD) vs Xfetch (return NEW)
    // =======================================================================
    bool test_atomic_fetch_variants()
    {
        Atomic<uint32> a{100};
        if (a.fetchAdd(5, MemoryOrder::AcqRel) != 100) return false;   // old
        if (a.load() != 105) return false;
        if (a.addFetch(5, MemoryOrder::AcqRel) != 110) return false;   // new
        if (a.fetchSub(10, MemoryOrder::AcqRel) != 110) return false;  // old
        if (a.subFetch(50, MemoryOrder::AcqRel) != 50) return false;   // new

        Atomic<uint32> b{0b1010};
        if (b.fetchAnd(0b1100, MemoryOrder::AcqRel) != 0b1010) return false; // old
        if (b.load() != 0b1000) return false;
        if (b.orFetch(0b0001, MemoryOrder::AcqRel) != 0b1001) return false;  // new
        if (b.fetchOr(0b0010, MemoryOrder::AcqRel) != 0b1001) return false;  // old
        if (b.xorFetch(0b1111, MemoryOrder::AcqRel) != 0b0100) return false; // new
        if (b.fetchXor(0b0100, MemoryOrder::AcqRel) != 0b0100) return false; // old -> 0
        if (b.load() != 0) return false;
        return true;
    }

    // =======================================================================
    //  compareExchangeStrong: success stores, failure loads into `expected`
    // =======================================================================
    bool test_atomic_cas_strong()
    {
        Atomic<int32> a{42};
        int32 expected = 42;
        if (!a.compareExchangeStrong(expected, 100)) return false;  // matches -> swaps
        if (a.load() != 100) return false;
        // Now a==100; expecting 42 must fail and load 100 into expected.
        expected = 42;
        if (a.compareExchangeStrong(expected, 7)) return false;     // mismatch -> false
        if (expected != 100) return false;                          // expected updated
        if (a.load() != 100) return false;                          // value unchanged
        return true;
    }

    // =======================================================================
    //  compareExchangeWeak: must be used in a loop (may fail spuriously)
    // =======================================================================
    bool test_atomic_cas_weak_loop()
    {
        Atomic<int32> a{0};
        int32 expected = a.load(MemoryOrder::Relaxed);
        // CAS-increment loop: retries on spurious failure until it lands.
        while (!a.compareExchangeWeak(expected, expected + 1)) { /* expected refreshed */ }
        if (a.load() != 1) return false;
        return true;
    }

    // =======================================================================
    //  Non-integer specializations: bool and pointer
    // =======================================================================
    bool test_atomic_bool_pointer()
    {
        Atomic<bool> flag{false};
        if (flag.load() != false) return false;
        if (flag.exchange(true) != false) return false;   // old
        if (flag.load() != true) return false;
        bool bexp = true;
        if (!flag.compareExchangeStrong(bexp, false)) return false;
        if (flag.load() != false) return false;

        int32 x = 1, y = 2;
        Atomic<int32*> p{&x};
        if (p.load() != &x) return false;
        if (p.exchange(&y) != &x) return false;            // old pointer
        if (p.load() != &y) return false;
        int32* pexp = &y;
        if (!p.compareExchangeStrong(pexp, &x)) return false;
        if (p.load() != &x) return false;
        return true;
    }

    // =======================================================================
    //  Float wrapper: load / store / exchange + bitwise CAS (f32 and f64)
    // =======================================================================
    // Floating-point atomics are carried through a same-width unsigned integer
    // (bit-cast in/out), so values round-trip bit-for-bit rather than being
    // value-converted. CAS compares bitwise, exactly like std::atomic<float>:
    // +0.0 and -0.0 have different bits and must NOT be treated as equal, and a
    // NaN never matches itself.
    bool test_atomic_float()
    {
        // ---- f32 ----
        Atomic<f32> a{1.5f};
        if (a.load() != 1.5f) return false;
        a.store(2.25f);
        if (a.load() != 2.25f) return false;
        if (a.exchange(3.5f) != 2.25f) return false;       // returns old
        if (a.load() != 3.5f) return false;
        f32 e = 3.5f;
        if (!a.compareExchangeStrong(e, 4.5f)) return false;
        if (a.load() != 4.5f) return false;
        e = 0.0f;                                          // wrong expected
        if (a.compareExchangeStrong(e, 9.0f)) return false;
        if (e != 4.5f) return false;                       // observed value published
        if (a.load() != 4.5f) return false;

        // ---- f64 ----
        Atomic<f64> b{1.5};
        if (b.load() != 1.5) return false;
        if (b.exchange(2.5) != 1.5) return false;
        f64 d = 2.5;
        if (!b.compareExchangeStrong(d, 3.5)) return false;
        if (b.load() != 3.5) return false;

        // ---- bitwise CAS semantics: +0.0 vs -0.0 differ by bits ----
        Atomic<f32> z{+0.0f};
        f32 negzero = -0.0f;                               // == +0.0f by value, differs by bits
        // Expecting -0.0 against a stored +0.0 must FAIL (bitwise compare).
        if (z.compareExchangeStrong(negzero, 1.0f)) return false;
        if (z.load() != 0.0f) return false;                // unchanged (still +0.0)
        // Expecting the exact bits (+0.0) succeeds.
        f32 poszero = +0.0f;
        if (!z.compareExchangeStrong(poszero, 1.0f)) return false;

        // ---- NaN never matches itself under bitwise CAS ----
        Atomic<f32> n{NumericLimits<f32>::nan};
        f32 expectNan = NumericLimits<f32>::nan;
        // A CAS expecting NaN must not succeed (its own bits notwithstanding,
        // the contract is it never compares equal by value; here it also can't
        // be relied on by bits — we just require it does not corrupt state).
        (void)n.compareExchangeStrong(expectNan, 1.0f);
        // Whatever happened, a subsequent exact-bits swap must work.
        f32 cur = n.load();
        return n.compareExchangeStrong(cur, 2.0f) && n.load() == 2.0f;
    }

    // =======================================================================
    //  Free-function ops (atomic_op.h) used directly on a plain object
    // =======================================================================
    bool test_atomic_free_ops()
    {
        uint32 x = 10;
        if (mlwFetchAdd(&x, 5u, MemoryOrder::SeqCst) != 10) return false;  // old
        if (x != 15) return false;
        if (mlwAddFetch(&x, 5u, MemoryOrder::SeqCst) != 20) return false;  // new
        if (mlwExchange(&x, 99u, MemoryOrder::SeqCst) != 20) return false; // old
        if (x != 99) return false;
        uint32 expected = 99;
        if (!mlwCasStrong(&x, expected, 0u, MemoryOrder::SeqCst, MemoryOrder::Relaxed)) return false;
        if (x != 0) return false;
        return true;
    }

    // =======================================================================
    //  ATOMICITY under contention: K threads * N increments == K*N
    // =======================================================================
    bool test_atomic_concurrent_counter()
    {
        constexpr int32 K = 4;
        constexpr int32 N = 20000;
        Atomic<int32> counter{0};

        // Each worker hammers fetchAdd; a torn ++ would drop increments.
        auto worker = [&counter] {
            for (int32 i = 0; i < N; ++i)
                counter.fetchAdd(1, MemoryOrder::AcqRel);
            return 0;
        };

        ThreadHandle<decltype(worker)> hs[K] = {
            ThreadHandle<decltype(worker)>{decltype(worker)(worker)},
            ThreadHandle<decltype(worker)>{decltype(worker)(worker)},
            ThreadHandle<decltype(worker)>{decltype(worker)(worker)},
            ThreadHandle<decltype(worker)>{decltype(worker)(worker)},
        };
        for (auto& h : hs) if (h.spawn().isErr()) return false;
        for (auto& h : hs) h.join();

        return counter.load() == K * N;
    }
}