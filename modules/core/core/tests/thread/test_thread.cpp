// core/core/tests/test_thread.cpp
//
// Coverage for core/thread/thread.h (ThreadHandle<Fn>). This is a port of the
// hand-run suite in app/main.cpp into the test_runner convention: every
// mlw_assert becomes an `if (!cond) return false`, so a failure is reported as
// FAIL instead of aborting the process.
//
// The point of these tests is NOT "does it print" — that passes even with
// corrupt memory. Each test asserts on COUNTS (copies, live heap blocks),
// because that is the only way the silent failure modes (leaks, double-frees,
// stray copies on the return path) become visible. Probe instruments every
// operation; MoveOnly turns "something tried to copy" into a compile error.
//
// Two paths are intentionally NOT executed here (see the notes inline):
//   * t10 — destroying a still-joinable handle: by contract this panics.
//   * void tryJoin success — BUG: unlike the value path, ThreadHandle<void>::
//     tryJoin() never nulls `handle` on success, so the handle is left dangling
//     and cannot be disposed of without a panic. Documented, not run.

#include "core/thread/thread.h"
#include "core/libc/mem.h"
#include "core/optional.h"
#include "core/typedef.h"

using namespace core;

namespace
{
    // Probe: instruments every operation and tracks net owned heap blocks.
    struct Probe
    {
        static inline int ctor = 0, copy = 0, move = 0, dtor = 0, live = 0;
        void* mem = nullptr;
        int   tag = 0;
        static void reset() { ctor = copy = move = dtor = 0; live = 0; }

        Probe(int t = 0) : tag(t) { mem = core::mlwMalloc(64); ++live; ++ctor; }
        Probe(const Probe& o) : tag(o.tag) { mem = core::mlwMalloc(64); ++live; ++copy; } // must never fire
        Probe(Probe&& o) noexcept : mem(o.mem), tag(o.tag) { o.mem = nullptr; ++move; }
        ~Probe() { if (mem) { core::mlwFree(mem); --live; } ++dtor; }
    };

    // MoveOnly: deleted copy. Anything that tries to copy it fails to COMPILE.
    struct MoveOnly
    {
        int val = 0;
        MoveOnly() = default;
        MoveOnly(const MoveOnly&) = delete;
        MoveOnly(MoveOnly&&) noexcept = default;
    };
}

namespace core_core_test
{
    // T1 — baseline: void return, no capture. Confirms ReturnSlot<void>
    // compiles and the void join path returns cleanly.
    bool test_thread_void_return()
    {
        ThreadHandle h{ [] { } };
        if (h.spawn().isErr()) return false;
        h.join();
        return true;
    }

    // T2 — trivial return value. Catches broken placement-new-into-slot or
    // broken move-out-of-slot for a trivial R (garbage int).
    bool test_thread_int_return()
    {
        ThreadHandle h{ [] { return 42; } };
        if (h.spawn().isErr()) return false;
        return h.join() == 42;
    }

    // T3 — non-trivial return: the leak / double-free test.
    // (a) freeing slot without ~R -> live != 0 (leak)
    // (b) running ~R twice        -> live goes negative
    // (c) any accidental copy     -> copy != 0
    bool test_thread_probe_return()
    {
        Probe::reset();
        {
            ThreadHandle h{ [] { return Probe{7}; } };
            if (h.spawn().isErr()) return false;
            Probe r = h.join();
            if (r.tag != 7) return false;
            if (Probe::copy != 0) return false;   // no copy anywhere on the return path
            if (Probe::live != 1) return false;   // only r holds a block right now
        }
        return Probe::live == 0 && Probe::copy == 0;  // r destroyed -> exactly one free
    }

    // T4 — move-only return. A COMPILE-TIME test: if join() copied R instead of
    // moving, this would not compile.
    bool test_thread_move_only_return()
    {
        ThreadHandle h{ []() -> MoveOnly { MoveOnly m; m.val = 99; return m; } };
        if (h.spawn().isErr()) return false;
        MoveOnly r = h.join();
        return r.val == 99;
    }

    // T5 — init-capture move: a big value moved INTO the closure, never copied.
    // Catches the closure being copied instead of moved into params.
    bool test_thread_capture_move()
    {
        Probe::reset();
        Probe big{ 5 };
        if (Probe::live != 1) return false;
        int tag = -1;
        {
            ThreadHandle h{ [p = core::move(big)] { return p.tag; } };
            if (h.spawn().isErr()) return false;
            tag = h.join();
        }                                        // closure (owning p) destroyed here
        if (tag != 5) return false;
        return Probe::copy == 0 && Probe::live == 0;
    }

    // T6 — destructor on a NEVER-SPAWNED handle. Catches a dtor that frees
    // params storage but forgets to run ~Fn (captured probe would leak).
    bool test_thread_never_spawned()
    {
        Probe::reset();
        {
            Probe p{ 2 };
            ThreadHandle h{ [x = core::move(p)] { return 0; } };
            // never spawn, never join — h dtor must destroy captured x
        }
        return Probe::copy == 0 && Probe::live == 0;
    }

    // T7 — spawned-and-joined completes cleanly. Catches has_return not reset
    // after join -> dtor re-runs ~R (double free).
    bool test_thread_spawn_join_clean()
    {
        Probe::reset();
        {
            ThreadHandle h{ [] { return Probe{8}; } };
            if (h.spawn().isErr()) return false;
            Probe r = h.join();
            if (r.tag != 8) return false;
        }
        return Probe::live == 0 && Probe::copy == 0;
    }

    // T8 — handle MOVE after spawn. Catches a move ctor that fails to null the
    // source's params/handle -> double free of params / double close of the OS
    // handle when both dtors run.
    bool test_thread_handle_move()
    {
        Probe::reset();
        {
            ThreadHandle h1{ [] { return Probe{3}; } };
            if (h1.spawn().isErr()) return false;
            ThreadHandle h2 = core::move(h1);    // h1 now inert (params == null)
            Probe r = h2.join();
            if (r.tag != 3) return false;
        }                                        // h1 dtor: early return; h2 dtor: frees once
        return Probe::live == 0 && Probe::copy == 0;
    }

    // T9 — two concurrent handles, no cross-talk. Catches shared/static params
    // or ThreadStart clobbering between threads.
    bool test_thread_two_threads()
    {
        Probe::reset();
        {
            ThreadHandle a{ [] { return Probe{10}; } };
            ThreadHandle b{ [] { return Probe{20}; } };
            if (a.spawn().isErr()) return false;
            if (b.spawn().isErr()) return false;
            Probe ra = a.join();
            Probe rb = b.join();
            if (ra.tag != 10 || rb.tag != 20) return false;
        }
        return Probe::live == 0 && Probe::copy == 0;
    }

    // =======================================================================
    //  Extensions beyond the app/main.cpp suite
    // =======================================================================

    // spawn() twice on one handle -> ThreadError::DoubleStart (no second thread).
    bool test_thread_double_start()
    {
        ThreadHandle h{ [] { return 1; } };
        if (h.spawn().isErr()) return false;         // first start OK
        auto second = h.spawn();                     // must be refused
        bool refused = second.isErr() && second.error().kind == ThreadError::DoubleStart;
        h.join();                                    // satisfy the must-join contract
        return refused;
    }

    // joinable(): false before spawn, true after spawn, false after join.
    bool test_thread_joinable()
    {
        ThreadHandle h{ [] { return 0; } };
        if (h.joinable()) return false;              // constructed, not spawned
        if (h.spawn().isErr()) return false;
        if (!h.joinable()) return false;             // running
        h.join();
        return !h.joinable();                        // consumed
    }

    // Convenience factory: construct+spawn+hand back a running handle.
    bool test_thread_factory_spawn()
    {
        auto fn = [] { return 5; };
        auto r = core::ThreadHandle<decltype(fn)>::spawn(core::move(fn));
        if (r.isErr()) return false;
        ThreadHandle<decltype(fn)> h = r.takeValue();
        return h.join() == 5;
    }

    // Value tryJoin: poll until the value arrives (success nulls the handle, so
    // the handle disposes cleanly). Bounded so a stuck thread can't hang the
    // runner — on the (not expected) cap path we fall back to join() to clean up.
    bool test_thread_try_join_value()
    {
        Probe::reset();
        bool ok = false;
        {
            ThreadHandle h{ [] { return Probe{55}; } };
            if (h.spawn().isErr()) return false;
            constexpr int CAP = 2000;
            for (int i = 0; i < CAP; ++i)
            {
                Optional<Probe> o = h.tryJoin(50);   // ms != 0 (0 == infinite on Linux)
                if (o.isSome())
                {
                    ok = (o.unwrap().tag == 55);
                    break;                           // handle nulled by the success path
                }
            }
            if (!ok)
            {
                // Extremely unlikely: never observed done. Clean up and fail.
                if (h.joinable()) h.join();
                return false;
            }
        }
        return ok && Probe::live == 0 && Probe::copy == 0;
    }

    // Void tryJoin: poll a void thread to completion. A successful tryJoin now
    // clears the handle (the fixed behavior), so the handle disposes cleanly and
    // is immediately reusable.
    bool test_thread_try_join_void()
    {
        ThreadHandle h{ [] { } };
        if (h.spawn().isErr()) return false;
        constexpr int CAP = 2000;
        bool done = false;
        for (int i = 0; i < CAP; ++i)
            if (h.tryJoin(50)) { done = true; break; }   // ms != 0 (0 == infinite on Linux)
        if (!done) { if (h.joinable()) h.join(); return false; }
        // Handle was cleared by the successful tryJoin: not joinable, disposes
        // without a panic, and can be spawned again.
        if (h.joinable()) return false;
        if (h.spawn().isErr()) return false;
        h.join();
        return true;
    }

    // Reuse: the closure is retained, so a handle can be spawned, joined, and
    // spawned again. Each run produces its own result.
    bool test_thread_reuse()
    {
        int base = 10;
        ThreadHandle h{ [base] { return base * 2; } };
        if (h.spawn().isErr()) return false;
        if (h.join() != 20) return false;
        if (h.joinable()) return false;              // cleared after join
        if (h.spawn().isErr()) return false;         // re-spawn the same closure
        if (h.join() != 20) return false;            // same inputs -> same result
        return true;
    }

    // Mutable closure across runs: captured state persists in the handle's stored
    // closure (threadCall invokes it as a non-const lvalue), so a mutable lambda
    // that bumps an internal counter yields a DIFFERENT value on each run.
    bool test_thread_mutable_reuse()
    {
        int calls = 0;
        auto counter = [calls]() mutable { return ++calls; };  // own captured `calls`
        ThreadHandle<decltype(counter)> h{ core::move(counter) };
        if (h.spawn().isErr()) return false;
        int first = h.join();
        if (h.spawn().isErr()) return false;
        int second = h.join();
        if (h.spawn().isErr()) return false;
        int third = h.join();
        // Distinct, increasing results prove the captured state carried over.
        return first == 1 && second == 2 && third == 3;
    }

    // NOTE — t10 (destroy a still-joinable handle) is a deliberate-panic test:
    // spawning without joining and letting the dtor run calls
    // panic("...discarded without join..."). It needs a death-test harness and
    // is intentionally absent here.
}