// core/core/tests/test_variant.cpp
//
// Behavioural coverage for core/variant.h. A Variant always holds exactly one
// of its alternatives (no empty/default state), so the risks are: storing into
// the right slot, the discriminant/index bookkeeping, visit() dispatching to
// the live alternative with correct const-ness, and destroying the active
// alternative exactly once on destruction / arm switch.
//
// get<T>() is unchecked, so it is only called after is<T>() confirms the arm.
// Leak-checking uses Probe::live (must return to 0).

#include "core/variant.h"

using namespace core;

namespace
{
    struct Probe
    {
        int v = 0;
        static inline int live = 0;
        Probe() { ++live; }
        explicit Probe(int x) : v(x) { ++live; }
        Probe(const Probe& o) : v(o.v) { ++live; }
        Probe(Probe&& o) noexcept : v(o.v) { o.v = -1; ++live; }
        Probe& operator=(const Probe& o) { v = o.v; return *this; }
        Probe& operator=(Probe&& o) noexcept { v = o.v; o.v = -1; return *this; }
        ~Probe() { --live; }
    };

    struct Immovable
    {
        int v;
        static inline int live = 0;
        explicit Immovable(int a, int b) : v(a + b) { ++live; }
        Immovable(const Immovable&) = delete;
        Immovable(Immovable&&) = delete;
        ~Immovable() { --live; }
    };
}

namespace core_core_test
{
    // ---- storage layout (compile-time) ------------------------------------
    bool test_variant_layout()
    {
        using V = Variant<int8, int64>;
        // buffer sized to the largest alternative + a uint8 tag
        static_assert(sizeof(V) >= sizeof(int64) + 1);
        static_assert(alignof(V) == alignof(int64));
        return true;
    }

    // ---- construct + index/is/get for each alternative --------------------
    bool test_variant_construct_index()
    {
        Variant<int, f64, char> a(5);           // first alternative
        if (a.index() != 0) return false;
        if (!a.is<int>() || a.is<f64>() || a.is<char>()) return false;
        if (a.get<int>() != 5) return false;

        Variant<int, f64, char> b(2.5);         // second alternative
        if (b.index() != 1 || !b.is<f64>()) return false;
        if (b.get<f64>() != 2.5) return false;

        Variant<int, f64, char> c('z');         // third alternative
        if (c.index() != 2 || !c.is<char>()) return false;
        return c.get<char>() == 'z';
    }

    // ---- lvalue is copied in, rvalue is moved in --------------------------
    bool test_variant_value_vs_move()
    {
        Probe::live = 0;
        {
            Probe p{7};
            Variant<int, Probe> a(p);            // copy an lvalue
            if (!a.is<Probe>() || a.get<Probe>().v != 7) return false;
            if (p.v != 7) return false;          // lvalue source untouched

            Variant<int, Probe> b(Probe{9});     // move a temporary
            if (!b.is<Probe>() || b.get<Probe>().v != 9) return false;
        }
        return Probe::live == 0;
    }

    // ---- in-place construction of a non-copyable, non-movable type --------
    bool test_variant_in_place()
    {
        Immovable::live = 0;
        {
            Variant<int, Immovable> v(variant_in_place<Immovable>, 4, 5);
            if (!v.is<Immovable>()) return false;
            if (v.get<Immovable>().v != 9) return false;
            if (Immovable::live != 1) return false;
        }
        return Immovable::live == 0;
    }

    // ---- visit() dispatches to the live alternative -----------------------
    bool test_variant_visit_dispatch()
    {
        int seen_i = -1;
        Variant<int, f64> v(10);
        v.visit([&](const auto& x) { seen_i = static_cast<int>(x); });
        if (seen_i != 10) return false;

        int seen_from_double = -1;
        Variant<int, f64> w(2.5);
        w.visit([&](const auto& x) { seen_from_double = static_cast<int>(x); });
        return seen_from_double == 2;      // 2.5 truncated by the cast
    }

    // ---- visit() on a non-const Variant can MUTATE the live alternative ----
    // A non-const visit hands the visitor `T&`, so a mutating generic lambda
    // works. (This previously failed to compile: the old const visit() overload
    // was constrained with is_invocable_v<F, const Ts&>, and evaluating that
    // constraint for a non-const call instantiated the lambda body against
    // `const T&` — a hard error, not a clean rejection. The check now lives in
    // the body of the selected overload, so the const path is never probed for
    // a non-const call.)
    bool test_variant_visit_mutate()
    {
        Variant<int, f64> v(10);
        v.visit([](auto& x) { x = x + 1; });      // int& -> 11
        if (v.get<int>() != 11) return false;

        Variant<int, f64> w(1.5);
        w.visit([](auto& x) { x = x * 2; });      // f64& -> 3.0
        return w.get<f64>() == 3.0;
    }

    // ---- mutate the active alternative through get<T>() -------------------
    bool test_variant_get_mutate()
    {
        Variant<int, f64> v(10);
        v.get<int>() += 1;
        if (v.get<int>() != 11) return false;

        Variant<int, f64> w(1.5);
        w.get<f64>() *= 2.0;
        return w.get<f64>() == 3.0;
    }

    // ---- const visit() sees the live alternative as const& ----------------
    bool test_variant_visit_const()
    {
        const Variant<int, f64> v(42);
        int seen = -1;
        v.visit([&](const auto& x) { seen = static_cast<int>(x); });
        return seen == 42;
    }

    // ---- copy duplicates the active alternative ---------------------------
    bool test_variant_copy()
    {
        Probe::live = 0;
        {
            Variant<int, Probe> a(Probe{3});
            Variant<int, Probe> b(a);            // copy the live Probe arm
            if (!b.is<Probe>() || b.get<Probe>().v != 3) return false;
            if (!a.is<Probe>() || a.get<Probe>().v != 3) return false;
            if (Probe::live != 2) return false;
        }
        return Probe::live == 0;
    }

    // ---- move transfers the active alternative ----------------------------
    bool test_variant_move()
    {
        Probe::live = 0;
        {
            Variant<int, Probe> a(Probe{4});
            Variant<int, Probe> b(core::move(a));
            if (!b.is<Probe>() || b.get<Probe>().v != 4) return false;
        }
        return Probe::live == 0;
    }

    // ---- assignment can switch arms and destroys the previous one ---------
    bool test_variant_assign_switch_arm()
    {
        Probe::live = 0;
        {
            Variant<int, Probe> v(Probe{1});
            if (Probe::live != 1) return false;
            v = Variant<int, Probe>(99);         // switch to the int arm
            if (!v.is<int>() || v.get<int>() != 99) return false;
            if (Probe::live != 0) return false;  // the Probe arm was destroyed
        }
        return Probe::live == 0;
    }

    // ---- FORMAT: needs the real IO/format stack ---------------------------
    // Formats whichever alternative is active. Meaningful only in the
    // freestanding build.
    bool test_variant_format()
    {
        auto eq = [](const char* got, index_t n, const char* exp) {
            for (index_t i = 0; i < n; ++i) { if (!exp[i] || got[i] != exp[i]) return false; }
            return exp[n] == '\0';
        };
        {
            auto& buf = core::detail::getFormatBuffer(); buf.len = 0;
            Variant<int, char> v(7);
            detail::formatValue(buf, v);
            if (!eq(buf.ptr, buf.len, "7")) return false;
        }
        {
            auto& buf = core::detail::getFormatBuffer(); buf.len = 0;
            Variant<int, char> v('Q');
            detail::formatValue(buf, v);
            if (!eq(buf.ptr, buf.len, "Q")) return false;
        }
        return true;
    }
} // namespace core_core_test