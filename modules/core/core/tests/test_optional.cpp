// core/core/tests/test_optional.cpp
//
// Behavioural coverage for core/optional.h — both Optional<T> and the
// Optional<T&> reference specialization.
//
// Two hard rules shaped these tests:
//  1. unwrap()/expect()/take() on a None optional call panic() -> mlwTerminate,
//     which cannot be observed from a returning test. Those paths are only ever
//     exercised on an *engaged* optional here. (A death/compile-fail harness is
//     the right home for the failing cases.)
//  2. This module documents non-standard consuming behaviour: moving an
//     Optional<T> leaves the source None, and unwrapOr/unwrapOrElse/take consume
//     the value. Every test that relies on that is commented at the point it
//     matters, so the behaviour is pinned rather than assumed.
//
// Leak-checking uses Probe::live: it must return to 0 by the end of each test.

#include "core/optional.h"

using namespace core;

namespace
{
    struct Probe
    {
        int v = 0;
        static inline int live = 0;
        static inline int copies = 0;
        static inline int moves  = 0;

        Probe() { ++live; }
        explicit Probe(int x) : v(x) { ++live; }
        Probe(const Probe& o) : v(o.v) { ++live; ++copies; }
        Probe(Probe&& o) noexcept : v(o.v) { o.v = -1; ++live; ++moves; }
        Probe& operator=(const Probe& o) { v = o.v; ++copies; return *this; }
        Probe& operator=(Probe&& o) noexcept { v = o.v; o.v = -1; ++moves; return *this; }
        ~Probe() { --live; }
    };

    // A type that is neither copyable nor movable — only reachable via in-place
    // construction. Proves optional_in_place / emplace don't require a move.
    struct Immovable
    {
        int v;
        static inline int live = 0;
        explicit Immovable(int a, int b) : v(a + b) { ++live; }
        Immovable(const Immovable&) = delete;
        Immovable(Immovable&&) = delete;
        ~Immovable() { --live; }
    };

    void reset_counters() { Probe::live = Probe::copies = Probe::moves = 0; Immovable::live = 0; }
}

namespace core_core_test
{
    // ---- empty construction ------------------------------------------------
    bool test_opt_empty()
    {
        Optional<int> a;
        Optional<int> b(nullptr);
        return a.isNone() && !a.isSome() && !static_cast<bool>(a)
            && b.isNone();
    }

    // ---- value construction (copy) ----------------------------------------
    bool test_opt_from_value_copy()
    {
        reset_counters();
        {
            Probe p{42};
            Optional<Probe> o(p);                 // copy-construct into storage
            if (!o.isSome()) return false;
            if (o->v != 42) return false;
            if ((*o).v != 42) return false;
            if (Probe::copies != 1) return false;
        }
        return Probe::live == 0;                  // both p and the stored copy gone
    }

    // ---- value construction (move) ----------------------------------------
    bool test_opt_from_value_move()
    {
        reset_counters();
        {
            Optional<Probe> o(Probe{7});          // move a temporary in
            if (!o.isSome() || o->v != 7) return false;
            if (Probe::moves < 1) return false;
        }
        return Probe::live == 0;
    }

    // ---- in-place / emplace for a non-movable type ------------------------
    bool test_opt_in_place_and_emplace()
    {
        Immovable::live = 0;
        {
            Optional<Immovable> o(optional_in_place, 2, 3);  // builds 2+3
            if (!o.isSome() || o->v != 5) return false;
            Immovable& r = o.emplace(10, 20);                // replaces in place
            if (r.v != 30 || o->v != 30) return false;
            if (Immovable::live != 1) return false;          // old one destroyed
        }
        return Immovable::live == 0;
    }

    // ---- reset destroys the contained value -------------------------------
    bool test_opt_reset()
    {
        reset_counters();
        Optional<Probe> o(Probe{1});
        if (Probe::live != 1) return false;
        o.reset();
        if (!o.isNone()) return false;
        if (Probe::live != 0) return false;       // destroyed on reset
        o.reset();                                // idempotent, no crash
        return o.isNone();
    }

    // ---- unwrap on an ENGAGED optional (safe) -----------------------------
    bool test_opt_unwrap_engaged()
    {
        Optional<int> o(99);
        if (o.unwrap() != 99) return false;
        o.unwrap() = 5;                           // returns a mutable ref
        return o.unwrap() == 5;
        // NOTE: unwrap() on None panics; not exercised here.
    }

    // ---- expect(): NOT TESTABLE YET — known library bug -------------------
    // expect(const char* msg) forwards msg to panic(msg), which expands to
    // core::CStr(msg). CStr has no const char* constructor (only the char[N]
    // array-reference template, the (ptr,len) ctor, and fromPtr()), so
    // expect() fails to *compile* the instant it is instantiated. unwrap()
    // escapes this only because it panics with a string literal, which binds
    // to the array ctor. No test function is defined here on purpose: this
    // harness discovers test names textually, so a disabled stub would be
    // called and fail to link. Add a test once the panic path accepts a
    // runtime pointer (e.g. via CStr::fromPtr).

    // ---- unwrapOr consumes on Some, falls back on None --------------------
    bool test_opt_unwrap_or()
    {
        Optional<int> some(3);
        if (some.unwrapOr(100) != 3) return false;
        if (!some.isNone()) return false;         // documented: Some is consumed

        Optional<int> none;
        if (none.unwrapOr(100) != 100) return false;
        return none.isNone();
    }

    // ---- unwrapOrElse: fn only runs on None -------------------------------
    bool test_opt_unwrap_or_else()
    {
        bool called = false;
        Optional<int> some(8);
        int r1 = some.unwrapOrElse([&]{ called = true; return 0; });
        if (r1 != 8 || called) return false;      // fn must NOT run on Some
        if (!some.isNone()) return false;         // Some consumed

        Optional<int> none;
        int r2 = none.unwrapOrElse([&]{ called = true; return 55; });
        return r2 == 55 && called;
    }

    // ---- take moves the value out and empties -----------------------------
    bool test_opt_take_engaged()
    {
        reset_counters();
        Optional<Probe> o(Probe{21});
        Probe taken = o.take();                   // safe: engaged
        if (taken.v != 21) return false;
        if (!o.isNone()) return false;
        return true;
        // NOTE: take() on None panics; not exercised here.
    }

    // ---- copy leaves the source intact ------------------------------------
    bool test_opt_copy_ctor()
    {
        Optional<int> a(5);
        Optional<int> b(a);
        if (!(a.isSome() && b.isSome())) return false;
        return *a == 5 && *b == 5;                // source unchanged by copy
    }

    // ---- move EMPTIES the source (documented divergence from std) ---------
    bool test_opt_move_empties_source()
    {
        reset_counters();
        Optional<Probe> a(Probe{6});
        Optional<Probe> b(core::move(a));
        if (!b.isSome() || b->v != 6) return false;
        if (!a.isNone()) return false;            // <-- the documented behaviour
        return true;
    }

    // ---- assignment: copy, move (empties src), nullptr --------------------
    bool test_opt_assignment()
    {
        Optional<int> a(1), b(2);
        b = a;                                    // copy-assign
        if (*b != 1 || *a != 1) return false;

        Optional<int> c(3), d;
        d = core::move(c);
        if (*d != 3 || !c.isNone()) return false; // move-assign empties source

        d = nullptr;                              // assign nullptr resets
        return d.isNone();
    }

    // ---- Optional<T&>: non-owning reference view --------------------------
    bool test_opt_ref_basic()
    {
        int x = 10;
        Optional<int&> o(x);
        if (!o.isSome()) return false;
        *o = 20;                                  // writes through to x
        if (x != 20) return false;
        if (o.unwrap() != 20) return false;       // engaged, safe

        int y = 99;
        o.emplace(y);                             // rebind, x untouched
        if (&*o != &y) return false;
        if (x != 20) return false;

        o.reset();
        return o.isNone();
    }

    // ---- Optional<T&>::unwrapOr is NON-consuming and returns a ref --------
    bool test_opt_ref_unwrap_or()
    {
        int fallback = 7;
        Optional<int&> none;
        int& r = none.unwrapOr(fallback);
        if (&r != &fallback) return false;        // returns the fallback ref
        if (!none.isNone()) return false;         // unchanged (non-consuming)

        int val = 3;
        Optional<int&> some(val);
        int& r2 = some.unwrapOr(fallback);
        return &r2 == &val && some.isSome();
    }

    // ---- FORMAT: requires the real IO/format stack ------------------------
    // Renders into the process scratch buffer and compares bytes. Meaningful
    // only in the freestanding build where core::detail::getFormatBuffer and
    // the format engine are linked.
    bool test_opt_format()
    {
        auto eq = [](const char* got, index_t n, const char* exp) {
            for (index_t i = 0; i < n; ++i) { if (!exp[i] || got[i] != exp[i]) return false; }
            return exp[n] == '\0';
        };
        {
            auto& buf = core::detail::getFormatBuffer(); buf.len = 0;
            Optional<int> some(42);
            mlw_write(buf, "{}", some);
            if (!eq(buf.ptr, buf.len, "42")) return false;
        }
        {
            auto& buf = core::detail::getFormatBuffer(); buf.len = 0;
            Optional<int> none;
            mlw_write(buf, "{}", none);
            if (!eq(buf.ptr, buf.len, "None")) return false;
        }
        return true;
    }
} // namespace core_core_test
