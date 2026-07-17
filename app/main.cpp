#include <core/thread/thread.h>



// thread_tests.cpp verification suite for core::ThreadHandle.
//
// The point of these tests is NOT "does it print" (that passes even when the
// memory is corrupt). Each test asserts on COUNTS — copies, live heap blocks —
// because that is the only way the silent failure modes become visible.
//
// Adapt names to your project if they differ:
//   core::ThreadHandle, core::move, core::mlwMalloc/mlwFree, println, mlw_assert
//
// Counts were verified against a synchronous mock: with copy elision on AND off,
// every path gives copy==0 and net heap balanced. If your run shows a copy > 0
// or a nonzero live count at end of scope, the annotated failure mode fired.



//  -Probe: instruments every operation and tracks net owned heap blocks 
struct Probe {
    static inline int ctor = 0, copy = 0, move = 0, dtor = 0, live = 0;
    void* mem = nullptr;
    int   tag = 0;
    static void reset() { ctor = copy = move = dtor = 0; live = 0; }

    Probe(int t = 0) : tag(t) { mem = core::mlwMalloc(64); ++live; ++ctor; }
    Probe(const Probe& o) : tag(o.tag) { mem = core::mlwMalloc(64); ++live; ++copy; }  // must never fire
    Probe(Probe&& o) noexcept : mem(o.mem), tag(o.tag) { o.mem = nullptr; ++move; }
    ~Probe() { if (mem) { core::mlwFree(mem); --live; } ++dtor; }
};

//  MoveOnly: deleted copy. Anything that tries to copy it fails to COMPILE 
struct MoveOnly {
    int val = 0;
    MoveOnly() = default;
    MoveOnly(const MoveOnly&) = delete;
    MoveOnly(MoveOnly&&) noexcept = default;
};

// 
// T1 — baseline: void return, no capture.
// Catches: nothing subtle. Confirms ResultSlot<void> compiles (the C3646 fix)
//          and the void join path returns cleanly.
// Expect:  prints "t1". No probe activity.
// 
static void t1_void() {
    core::ThreadHandle h{ [] { println("t1: hello from child"); } };
    h.spawn();
    h.join();
}

// 
// T2 — trivial return value.
// Catches: broken placement-new-into-slot or broken move-out-of-slot for a
//          trivial R (you'd get a garbage int).
// Expect:  r == 42.
// 
static void t2_int() {
    core::ThreadHandle h{ [] { return 42; } };
    h.spawn();
    int r = h.join();
    mlw_assert(r == 42);
}

// 
// T3 — non-trivial return, THE leak/double-free test.
// Catches: (a) freeing slot storage without running ~R   live != 0 (leak)
//          (b) running ~R twice on a live object        -> live goes negative / crash
//          (c) any accidental copy                      -> copy != 0
// Expect:  r.tag==7; copy==0; live==1 while r alive; live==0 after; move small.
// 
static void t3_probe_return() {
    Probe::reset();
    {
        core::ThreadHandle h{ [] { return Probe{7}; } };
        h.spawn();
        Probe r = h.join();
        mlw_assert(r.tag == 7);
        mlw_assert(Probe::copy == 0);   // no copy anywhere on the return path
        mlw_assert(Probe::live == 1);   // only r holds a block right now
    }
    mlw_assert(Probe::live == 0);       // r destroyed -> exactly one free
    mlw_assert(Probe::copy == 0);
}

// 
// T4 — move-only return. This is a COMPILE-TIME test.
// Catches: any code path that requires copying R (threadCall or join). If your
//          join used copy instead of move, this file would not compile.
// Expect:  compiles, r.val == 99.
// 
static void t4_move_only_return() {
    core::ThreadHandle h{ []() -> MoveOnly { MoveOnly m; m.val = 99; return m; } };
    h.spawn();
    MoveOnly r = h.join();
    mlw_assert(r.val == 99);
}

// 
// T5 — init-capture move: big value moved INTO the closure, never copied.
// Catches: the closure being copied instead of moved into `params` (forward<Fn>
//          wrong), or [=] semantics leaking in. Also proves move-only closures
//          survive the whole pipeline.
// Expect:  copy==0; live==0 at end. (big is moved-from after the capture.)
// 
static void t5_capture_move() {
    Probe::reset();
    Probe big{ 5 };
    mlw_assert(Probe::live == 1);
    {
        core::ThreadHandle h{ [p = core::move(big)] { mlw_assert(p.tag == 5); } };
        h.spawn();
        h.join();
    }                                   // closure (owning p) destroyed here
    mlw_assert(Probe::copy == 0);
    mlw_assert(Probe::live == 0);       // big husk (mem null) + p both accounted
}

// 
// T6 — destructor on a NEVER-SPAWNED handle.
// Catches: dtor that frees `params` storage but forgets ~ThreadParameters
//          (i.e. ~Fn). The captured probe would leak -> live != 0.
// Expect:  no panic (handle.fd==0); copy==0; live==0.
// 
static void t6_never_spawned() {
    Probe::reset();
    {
        Probe p{ 2 };
        core::ThreadHandle h{ [x = core::move(p)] { return 0; } };
        // never spawn, never join
    }                                   // h dtor must destroy captured x
    mlw_assert(Probe::copy == 0);
    mlw_assert(Probe::live == 0);
}

// 
// T7 — spawned-and-joined completes cleanly (state-machine happy path).
// Catches: has_return not reset after join -> dtor re-runs ~R (double). With a
//          Probe, a stale has_return would double-free -> crash / live<0.
// Expect:  live==0; copy==0.
// 
static void t7_spawn_join_clean() {
    Probe::reset();
    {
        core::ThreadHandle h{ [] { return Probe{8}; } };
        h.spawn();
        Probe r = h.join();
        mlw_assert(r.tag == 8);
    }
    mlw_assert(Probe::live == 0);
    mlw_assert(Probe::copy == 0);
}

// 
// T8 — handle MOVE after spawn. The critical ownership test.
// Catches: move ctor not nulling source's params/handle -> double-free of
//          params or double-close of the OS handle when both dtors run. Also:
//          moved-from source must not panic even though it "was spawned".
// Expect:  r.tag==3; live==0; copy==0; no crash from h1's dtor.
// 
static void t8_handle_move() {
    Probe::reset();
    {
        core::ThreadHandle h1{ [] { return Probe{3}; } };
        h1.spawn();
        core::ThreadHandle h2 = move(h1);   // h1 now inert (params == null)
        Probe r = h2.join();
        mlw_assert(r.tag == 3);
    }                                             // h1 dtor: early return; h2 dtor: frees once
    mlw_assert(Probe::live == 0);
    mlw_assert(Probe::copy == 0);
}

// 
// T9 — two concurrent handles, no cross-talk.
// Catches: shared/static params or ThreadStart clobbering between threads.
// Expect:  ra==10, rb==20; live==0.
// 
static void t9_two_threads() {
    Probe::reset();
    {
        core::ThreadHandle a{ [] { return Probe{10}; } };
        core::ThreadHandle b{ [] { return Probe{20}; } };
        a.spawn();
        b.spawn();
        Probe ra = a.join();
        Probe rb = b.join();
        mlw_assert(ra.tag == 10);
        mlw_assert(rb.tag == 20);
    }
    mlw_assert(Probe::live == 0);
    mlw_assert(Probe::copy == 0);
}

// 
// T10 — destroy a STILL-JOINABLE handle. This test intentionally PANICS.
// Run it ALONE, expect your panic_mem("destroyed while joinable"). Note the
// panic fires regardless of whether the child already finished — it keys on
// handle.fd != 0 (join is what nulls it), not on thread state.
// Guarded so the normal run doesn't abort.
// 
#ifdef MLW_TEST_EXPECT_PANIC
static void t10_destroy_joinable_panics() {
    core::ThreadHandle h{ [] { println("t10 child"); } };
    h.spawn();
    // no join  scope exit dtor sees handle.fd != 0  panic_mem
}
#endif

int32 mallowMain() {
    t1_void();               println("T1  void .................. ok");
    t2_int();                println("T2  int return ............ ok");
    t3_probe_return();       println("T3  probe return .......... ok (copy=0, no leak)");
    t4_move_only_return();   println("T4  move-only return ...... ok (compiled)");
    t5_capture_move();       println("T5  capture move .......... ok (copy=0)");
    t6_never_spawned();      println("T6  never-spawned dtor .... ok (no leak)");
    t7_spawn_join_clean();   println("T7  spawn+join clean ...... ok (no double ~R)");
    t8_handle_move();        println("T8  handle move ........... ok (no double free)");
    t9_two_threads();        println("T9  two threads ........... ok (no cross-talk)");
#ifdef MLW_TEST_EXPECT_PANIC
    t10_destroy_joinable_panics();   // should abort before this line prints
    println("T10 FAILED: expected panic did not fire");
    return 1;
#endif
    println("ALL THREAD TESTS PASSED");
    return 0;
}
