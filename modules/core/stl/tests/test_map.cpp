// core/core/tests/test_map.cpp
//
// Coverage for stl/map_swissTable.h (core::Map — the SIMD SwissTable and its
// scalar byte-scan fallback). Unlike test_bit, none of this is constexpr: the
// map does runtime allocation, so every check is a runtime bool — a test returns
// false on the first failed condition, true if all pass.
//
// Build the same source once per backend to cover all probe/scan paths:
//     (default)                SSE2 on x86 / NEON on arm64
//     -DMLW_HASHMAP_PORTABLE   forces the portable SWAR group backend
//     -DMLW_LINIAR_MAP_PROBE   forces the scalar byte-scan fallback
//
// The fuzz test mirrors the map against a flat array model, so this file needs
// no libc/STL. It has no main(): register the test_map_* functions with the
// harness the same way test_bit's cases are picked up.

#include "stl/map.h"

using namespace core;

namespace core_stl_test
{
    // small xorshift so the fuzz test needs no libc/STL
    struct MapRng
    {
        uint64 s;
        explicit MapRng(uint64 seed) : s(seed ? seed : 0x9e3779b97f4a7c15ull) {}
        uint64 next() { s ^= s >> 12; s ^= s << 25; s ^= s >> 27; return s * 0x2545f4914f6cdd1dull; }
    };

    // value type that counts live instances, to catch leaks / double-frees
    struct Tracked
    {
        static isize live;
        uint64 v;
        Tracked() : v(0) { ++live; }
        Tracked(uint64 x) : v(x) { ++live; }
        Tracked(const Tracked &o) : v(o.v) { ++live; }
        Tracked(Tracked &&o) noexcept : v(o.v) { ++live; }
        ~Tracked() { --live; }
    };
    isize Tracked::live = 0;

    // ---- empty map is safe and finds nothing ------------------------------
    bool test_map_empty()
    {
        Map<int32, uint64> m;
        if (m.len() != 0) return false;
        if (!m.isEmpty()) return false;
        if (m.contains(42)) return false;
        if (m.get(42)) return false;
        if (m.remove(42)) return false; // remove on empty is false, not a crash
        return true;
    }

    // ---- single put / get -------------------------------------------------
    bool test_map_put_get_single()
    {
        Map<int32, uint64> m;
        m.put(7, 100);
        if (m.len() != 1) return false;
        if (!m.contains(7)) return false;
        auto o = m.get(7);
        if (!o || o.unwrap() != 100) return false;
        if (m.contains(8)) return false;
        return true;
    }

    // ---- put on an existing key overwrites, does not grow len -------------
    bool test_map_overwrite()
    {
        Map<int32, uint64> m;
        m.put(7, 100);
        m.put(7, 200);
        if (m.len() != 1) return false;
        return m.get(7).unwrap() == 200;
    }

    // ---- the reference returned by put is live ----------------------------
    bool test_map_live_reference()
    {
        Map<int32, uint64> m;
        uint64 &r = m.put(1, 10);
        r = 999;
        return m.get(1).unwrap() == 999;
    }

    // ---- remove, then absent; double remove is false ----------------------
    bool test_map_remove()
    {
        Map<int32, uint64> m;
        m.put(5, 50);
        if (!m.remove(5)) return false;
        if (m.len() != 0) return false;
        if (m.contains(5)) return false;
        if (m.remove(5)) return false;
        return true;
    }

    // ---- remove everything then reinsert (tombstone reuse) ----------------
    bool test_map_remove_reinsert()
    {
        Map<int32, uint64> m;
        for (int32 i = 0; i < 100; ++i) m.put(i, (uint64)i);
        for (int32 i = 0; i < 100; ++i) if (!m.remove(i)) return false;
        if (m.len() != 0) return false;
        for (int32 i = 0; i < 100; ++i) m.put(i, (uint64)i * 2);
        for (int32 i = 0; i < 100; ++i) { auto o = m.get(i); if (!o || o.unwrap() != (uint64)i * 2) return false; }
        return true;
    }

    // ---- many inserts force resizes; all survive --------------------------
    bool test_map_grow_preserves()
    {
        Map<int32, uint64> m;
        constexpr int32 N = 10000;
        for (int32 i = 0; i < N; ++i) m.put(i, (uint64)i * 3 + 1);
        if (m.len() != N) return false;
        for (int32 i = 0; i < N; ++i) { auto o = m.get(i); if (!o || o.unwrap() != (uint64)i * 3 + 1) return false; }
        if (m.contains(N)) return false;
        return true;
    }

    // ---- clear empties but leaves the map usable --------------------------
    bool test_map_clear()
    {
        Map<int32, uint64> m;
        for (int32 i = 0; i < 500; ++i) m.put(i, (uint64)i);
        m.clear();
        if (m.len() != 0) return false;
        for (int32 i = 0; i < 500; ++i) if (m.contains(i)) return false;
        m.put(1, 1);
        return m.get(1).unwrap() == 1;
    }

    // ---- iteration visits every live entry exactly once -------------------
    bool test_map_iterate()
    {
        constexpr int32 N = 3000;
        Map<int32, uint64> m;
        for (int32 i = 0; i < N; ++i) m.put(i, (uint64)i);
        bool seen[N];
        for (int32 i = 0; i < N; ++i) seen[i] = false;
        isize count = 0;
        for (auto &e : m)
        {
            if (e.key < 0 || e.key >= N) return false;
            if (seen[e.key]) return false; // no slot twice
            seen[e.key] = true;
            if (e.value != (uint64)e.key) return false;
            ++count;
        }
        if (count != N) return false;
        for (int32 i = 0; i < N; ++i) if (!seen[i]) return false;
        return true;
    }

    // ---- iterating an empty (and an emptied) map yields nothing -----------
    bool test_map_iterate_empty()
    {
        Map<int32, uint64> m;
        isize count = 0;
        for (auto &e : m) { (void)e; ++count; }
        if (count != 0) return false;
        m.put(1, 1);
        m.remove(1);
        count = 0;
        for (auto &e : m) { (void)e; ++count; }
        return count == 0;
    }

    // ---- move ctor transfers ownership; moved-from stays valid ------------
    bool test_map_move()
    {
        Map<int32, uint64> a;
        for (int32 i = 0; i < 1000; ++i) a.put(i, (uint64)i);
        Map<int32, uint64> b = core::move(a);
        if (a.len() != 0) return false;
        if (a.contains(0)) return false;
        if (b.len() != 1000) return false;
        if (b.get(500).unwrap() != 500) return false;
        a.put(1, 1); // still usable
        return a.get(1).unwrap() == 1;
    }

    // ---- move assignment frees the lhs's old table ------------------------
    bool test_map_move_assign()
    {
        Map<int32, uint64> a;
        for (int32 i = 0; i < 100; ++i) a.put(i, (uint64)i);
        Map<int32, uint64> b;
        for (int32 i = 0; i < 50; ++i) b.put(i + 1000, (uint64)i);
        b = core::move(a);
        if (b.len() != 100) return false;
        if (!b.contains(0) || b.contains(1000)) return false;
        return true;
    }

    // ---- clone is an independent deep copy --------------------------------
    bool test_map_clone()
    {
        Map<int32, uint64> a;
        for (int32 i = 0; i < 2000; ++i) a.put(i, (uint64)i * 7);
        Map<int32, uint64> b = a.clone();
        if (b.len() != a.len()) return false;
        for (int32 i = 0; i < 2000; ++i) { auto o = b.get(i); if (!o || o.unwrap() != (uint64)i * 7) return false; }
        b.put(0, 12345);
        a.remove(1);
        if (a.get(0).unwrap() != 0) return false;
        if (b.get(0).unwrap() != 12345) return false;
        if (!b.contains(1) || a.contains(1)) return false;
        return true;
    }

    // ---- dense keys build long chains; mid-chain erase keeps neighbours ---
    bool test_map_collisions_mid_chain_erase()
    {
        Map<int32, uint64> m;
        for (int32 i = 0; i < 64; ++i) m.put(i, (uint64)i);
        for (int32 i = 0; i < 64; ++i) { auto o = m.get(i); if (!o || o.unwrap() != (uint64)i) return false; }
        for (int32 i = 1; i < 64; i += 2) if (!m.remove(i)) return false; // erase odds
        for (int32 i = 0; i < 64; i += 2) if (!m.contains(i)) return false;
        for (int32 i = 1; i < 64; i += 2) if (m.contains(i)) return false;
        return true;
    }

    // ---- every stored value has exactly one live object; no leaks ---------
    bool test_map_lifetime_balance()
    {
        if (Tracked::live != 0) return false;
        {
            Map<int32, Tracked> m;
            for (int32 i = 0; i < 1000; ++i) m.put(i, Tracked{(uint64)i});
            if (Tracked::live != m.len()) return false; // one live per entry
            for (int32 i = 0; i < 500; ++i) m.remove(i);
            if (Tracked::live != m.len()) return false;
            m.put(500, Tracked{7}); // overwrite must destroy the old value
            if (Tracked::live != m.len()) return false;
        } // deinit destroys the rest
        return Tracked::live == 0;
    }

    // ---- differential fuzz against a flat array model (no STL) ------------
    bool test_map_fuzz_vs_model()
    {
        constexpr int32 KS = 400;
        bool present[KS];
        uint64 mval[KS];
        for (int32 i = 0; i < KS; ++i) { present[i] = false; mval[i] = 0; }

        Map<int32, uint64> m;
        MapRng rng(0xBEEF);
        constexpr isize ITERS = 200000;
        isize liveCount = 0;

        for (isize it = 0; it < ITERS; ++it)
        {
            const uint64 r = rng.next();
            const int32 k = (int32)(r % (uint64)KS);
            const int op = (int)((r >> 20) % 10);
            if (op < 5) // put
            {
                const uint64 v = rng.next();
                m.put(k, v);
                if (!present[k]) ++liveCount;
                present[k] = true;
                mval[k] = v;
            }
            else if (op < 7) // remove
            {
                const bool had = present[k];
                if (m.remove(k) != had) return false;
                if (had) --liveCount;
                present[k] = false;
            }
            else // get / contains
            {
                auto o = m.get(k);
                if (present[k]) { if (!o || o.unwrap() != mval[k]) return false; }
                else           { if (o) return false; }
                if (m.contains(k) != present[k]) return false;
            }
            if (m.len() != liveCount) return false;
        }

        // iteration must yield exactly the model's live set
        bool seen[KS];
        for (int32 i = 0; i < KS; ++i) seen[i] = false;
        isize count = 0;
        for (auto &e : m)
        {
            if (e.key < 0 || e.key >= KS) return false;
            if (!present[e.key]) return false;
            if (seen[e.key]) return false;
            if (e.value != mval[e.key]) return false;
            seen[e.key] = true;
            ++count;
        }
        if (count != liveCount) return false;
        for (int32 i = 0; i < KS; ++i) if (present[i] != seen[i]) return false;
        return true;
    }
} // namespace core_core_test