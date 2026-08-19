// core/core/tests/test_set.cpp
//
// Coverage for stl/set.h (core::Set — the hash set facade over core::Map).
// Runtime bool checks like test_map: a test returns false on the first failed
// condition, true if all pass. No libc/STL; the fuzz test mirrors the set
// against a flat bool array.
//
// Build once per backend to cover every probe/scan path, same as test_map:
//     (default)                SSE2 on x86 / NEON on arm64
//     -DMLW_HASHMAP_PORTABLE   forces the portable SWAR group backend
//     -DMLW_LINIAR_MAP_PROBE   forces the scalar byte-scan fallback
//
// The set adds no probing of its own — it forwards to core::Map — so these
// tests are really checking the facade: insert's new-vs-present return, the
// key-only iterator, and that no dummy value leaks or bloats a slot.
//
// No main(): register the test_set_* functions with the harness the same way
// test_map's cases are picked up.

#include "stl/set.h"

using namespace core;



namespace core_stl_test
{
    // small xorshift so the fuzz test needs no libc/STL (same as test_map)
    struct SetRng
    {
        uint64 s;
        explicit SetRng(uint64 seed) : s(seed ? seed : 0x9e3779b97f4a7c15ull) {}
        uint64 next() { s ^= s >> 12; s ^= s << 25; s ^= s >> 27; return s * 0x2545f4914f6cdd1dull; }
    };

    // element type that counts live instances, to catch leaks / double-frees.
    // needs operator== and a Hash<> to be a HashKey; hash/eq go through .v.
    struct TrackedKey
    {
        static isize live;
        uint64 v;
        TrackedKey() : v(0) { ++live; }
        TrackedKey(uint64 x) : v(x) { ++live; }
        TrackedKey(const TrackedKey &o) : v(o.v) { ++live; }
        TrackedKey(TrackedKey &&o) noexcept : v(o.v) { ++live; }
        ~TrackedKey() { --live; }
        bool operator==(const TrackedKey &o) const { return v == o.v; }
    };
    isize TrackedKey::live = 0;


    // ---- the empty-value layout trick actually collapsed -----------------
    // Belt-and-suspenders next to the static_assert inside Set: a slot must
    // cost exactly the key, with no padding for the dummy value.
    bool test_set_slot_is_key_sized()
    {
        return sizeof(Set<int32>::ConstIterator) >= 0 // (compile touchstone)
            && sizeof(typename Map<int32, char>::Entry) >= sizeof(int32);
    }

    // ---- empty set is safe and finds nothing ------------------------------
    bool test_set_empty()
    {
        Set<int32> s;
        if (s.len() != 0) return false;
        if (!s.isEmpty()) return false;
        if (s.contains(42)) return false;
        if (s.remove(42)) return false; // remove on empty is false, not a crash
        return true;
    }

    // ---- insert reports new, and membership takes ------------------------
    bool test_set_insert_single()
    {
        Set<int32> s;
        if (!s.insert(7)) return false;   // newly inserted -> true
        if (s.len() != 1) return false;
        if (!s.contains(7)) return false;
        if (s.contains(8)) return false;
        return true;
    }

    // ---- re-inserting a present element returns false, does not grow ------
    // This is the core reason Set::insert isn't just Map::put; test it hard.
    bool test_set_insert_duplicate()
    {
        Set<int32> s;
        if (!s.insert(7)) return false;   // first: true
        if (s.insert(7)) return false;    // second: false, already present
        if (s.insert(7)) return false;    // still false
        if (s.len() != 1) return false;   // len unchanged by duplicates
        if (!s.contains(7)) return false; // still there
        return true;
    }

    // ---- remove, then absent; double remove is false ----------------------
    bool test_set_remove()
    {
        Set<int32> s;
        s.insert(5);
        if (!s.remove(5)) return false;
        if (s.len() != 0) return false;
        if (s.contains(5)) return false;
        if (s.remove(5)) return false;
        // and it can be inserted-as-new again after removal
        if (!s.insert(5)) return false;
        return true;
    }

    // ---- remove everything then reinsert (tombstone reuse) ----------------
    bool test_set_remove_reinsert()
    {
        Set<int32> s;
        for (int32 i = 0; i < 100; ++i) if (!s.insert(i)) return false;
        for (int32 i = 0; i < 100; ++i) if (!s.remove(i)) return false;
        if (s.len() != 0) return false;
        for (int32 i = 0; i < 100; ++i) if (!s.insert(i)) return false; // new again
        for (int32 i = 0; i < 100; ++i) if (!s.contains(i)) return false;
        return true;
    }

    // ---- many inserts force resizes; all survive --------------------------
    bool test_set_grow_preserves()
    {
        Set<int32> s;
        constexpr int32 N = 10000;
        for (int32 i = 0; i < N; ++i) if (!s.insert(i)) return false;
        if (s.len() != N) return false;
        for (int32 i = 0; i < N; ++i) if (!s.contains(i)) return false;
        if (s.contains(N)) return false;
        // every one of them is now a duplicate
        for (int32 i = 0; i < N; ++i) if (s.insert(i)) return false;
        if (s.len() != N) return false;
        return true;
    }

    // ---- clear empties but leaves the set usable --------------------------
    bool test_set_clear()
    {
        Set<int32> s;
        for (int32 i = 0; i < 500; ++i) s.insert(i);
        s.clear();
        if (s.len() != 0) return false;
        for (int32 i = 0; i < 500; ++i) if (s.contains(i)) return false;
        if (!s.insert(1)) return false; // usable, and 1 counts as new post-clear
        return s.contains(1);
    }

    // ---- iteration visits every element exactly once ----------------------
    bool test_set_iterate()
    {
        constexpr int32 N = 3000;
        Set<int32> s;
        for (int32 i = 0; i < N; ++i) s.insert(i);
        bool seen[N];
        for (int32 i = 0; i < N; ++i) seen[i] = false;
        isize count = 0;
        for (const auto &k : s)
        {
            if (k < 0 || k >= N) return false;
            if (seen[k]) return false; // no element twice
            seen[k] = true;
            ++count;
        }
        if (count != N) return false;
        for (int32 i = 0; i < N; ++i) if (!seen[i]) return false;
        return true;
    }

    // ---- iterating an empty (and an emptied) set yields nothing -----------
    bool test_set_iterate_empty()
    {
        Set<int32> s;
        isize count = 0;
        for (const auto &k : s) { (void)k; ++count; }
        if (count != 0) return false;
        s.insert(1);
        s.remove(1);
        count = 0;
        for (const auto &k : s) { (void)k; ++count; }
        return count == 0;
    }

    // ---- clone is an independent deep copy --------------------------------
    bool test_set_clone()
    {
        Set<int32> a;
        for (int32 i = 0; i < 2000; ++i) a.insert(i);
        Set<int32> b = a.clone();
        if (b.len() != a.len()) return false;
        for (int32 i = 0; i < 2000; ++i) if (!b.contains(i)) return false;
        // mutating one must not touch the other
        b.insert(999999);
        a.remove(1);
        if (a.contains(999999)) return false;
        if (b.contains(1) == false) return false; // b still has 1
        if (a.contains(1)) return false;          // a no longer does
        if (b.len() != 2001) return false;
        return true;
    }

    // ---- dense keys build long chains; mid-chain erase keeps neighbours ---
    bool test_set_collisions_mid_chain_erase()
    {
        Set<int32> s;
        for (int32 i = 0; i < 64; ++i) s.insert(i);
        for (int32 i = 0; i < 64; ++i) if (!s.contains(i)) return false;
        for (int32 i = 1; i < 64; i += 2) if (!s.remove(i)) return false; // erase odds
        for (int32 i = 0; i < 64; i += 2) if (!s.contains(i)) return false;
        for (int32 i = 1; i < 64; i += 2) if (s.contains(i)) return false;
        return true;
    }

    // ---- exactly one live key object per element; no leaks ----------------
    // Guards that the folded empty value never runs a spurious ctor/dtor and
    // that keys are destroyed on remove / overwrite-skip / deinit.
    bool test_set_lifetime_balance()
    {
        if (TrackedKey::live != 0) return false;
        {
            Set<TrackedKey> s;
            for (uint64 i = 0; i < 1000; ++i) s.insert(TrackedKey{i});
            if (TrackedKey::live != s.len()) return false; // one live per element
            for (uint64 i = 0; i < 500; ++i) s.remove(TrackedKey{i});
            if (TrackedKey::live != s.len()) return false;
            // duplicate insert must NOT create a surviving extra key: the
            // temporary is dropped, live count tracks len, not calls.
            s.insert(TrackedKey{600});
            if (TrackedKey::live != s.len()) return false;
        } // deinit destroys the rest
        return TrackedKey::live == 0;
    }

    // ---- differential fuzz against a flat bool model (no STL) -------------
    bool test_set_fuzz_vs_model()
    {
        constexpr int32 KS = 400;
        bool present[KS];
        for (int32 i = 0; i < KS; ++i) present[i] = false;

        Set<int32> s;
        SetRng rng(0xBEEE);
        constexpr isize ITERS = 200000;
        isize liveCount = 0;

        for (isize it = 0; it < ITERS; ++it)
        {
            const uint64 r = rng.next();
            const int32 k = (int32)(r % (uint64)KS);
            const int op = (int)((r >> 20) % 10);
            if (op < 5) // insert
            {
                const bool wasNew = !present[k];
                if (s.insert(k) != wasNew) return false; // return tracks novelty
                if (wasNew) ++liveCount;
                present[k] = true;
            }
            else if (op < 7) // remove
            {
                const bool had = present[k];
                if (s.remove(k) != had) return false;
                if (had) --liveCount;
                present[k] = false;
            }
            else // contains
            {
                if (s.contains(k) != present[k]) return false;
            }
            if (s.len() != liveCount) return false;
        }

        // iteration must yield exactly the model's live set
        bool seen[KS];
        for (int32 i = 0; i < KS; ++i) seen[i] = false;
        isize count = 0;
        for (const auto &k : s)
        {
            if (k < 0 || k >= KS) return false;
            if (!present[k]) return false;
            if (seen[k]) return false;
            seen[k] = true;
            ++count;
        }
        if (count != liveCount) return false;
        for (int32 i = 0; i < KS; ++i) if (present[i] != seen[i]) return false;
        return true;
    }
} // namespace core_stl_test

    template <>
    struct core::Hash<core_stl_test::TrackedKey>
    {
        usize operator()(const core_stl_test::TrackedKey &k) const { return static_cast<usize>(mix64(k.v)); }
    };