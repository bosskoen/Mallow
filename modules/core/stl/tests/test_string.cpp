// core/core/tests/test_string.cpp
//
// Coverage for stl/string.h (core::String — SSO byte string). Runtime bool
// checks like test_map/test_set: a test returns false on the first failed
// condition, true if all pass. No libc/STL; the fuzz test mirrors the string
// against a flat byte model.
//
// The interesting surface is the SSO boundary — the union's short<->long
// transition at len 23/24 — and that the maintained NUL and length bookkeeping
// stay correct across it. Backend build flags don't apply here (String pulls in
// no group backend of its own), but building this alongside the map/set suite
// under each -DMLW_* flag is harmless and confirms the shared headers compile.
//
// No main(): register the test_string_* functions with the harness the same way
// the other cases are picked up.

#include "stl/string.h"
#include "stl/set.h" // for the Hash<String> test

using namespace core;

namespace core_stl_test
{
    // xorshift, same as the other suites
    struct StrRng
    {
        uint64 s;
        explicit StrRng(uint64 seed) : s(seed ? seed : 0x9e3779b97f4a7c15ull) {}
        uint64 next() { s ^= s >> 12; s ^= s << 25; s ^= s >> 27; return s * 0x2545f4914f6cdd1dull; }
    };

    // byte-compare a String against a known buffer of length n
    static bool eqBytes(const String &s, const char *p, isize n)
    {
        if (s.len() != n) return false;
        for (isize i = 0; i < n; ++i)
            if (s[i] != p[i]) return false;
        return true;
    }

    // ---- default and empty --------------------------------------------------
    bool test_string_empty()
    {
        String s;
        if (s.len() != 0) return false;
        if (!s.isEmpty()) return false;
        if (!s.isInline()) return false;         // empty is short
        if (s.capacity() != 23) return false;    // SSO_CAP
        if (s.data()[0] != '\0') return false;   // maintained NUL
        return true;
    }

    // ---- construct from a literal stays inline when short -------------------
    bool test_string_from_literal_short()
    {
        String s("hello");
        if (s.len() != 5) return false;
        if (!s.isInline()) return false;
        if (!eqBytes(s, "hello", 5)) return false;
        if (s.data()[5] != '\0') return false;   // NUL convenience
        // CStr view round-trips pointer+length
        CStr v = static_cast<CStr>(s);
        if (v.len != 5 || v.ptr != s.data()) return false;
        return true;
    }

    // ---- the exact SSO boundary: 22 / 23 inline, 24 heap -------------------
    bool test_string_sso_boundary()
    {
        // 23 bytes: the largest inline string. space_left becomes 0, and that
        // zero byte IS the terminator at offset 23.
        {
            String s("12345678901234567890123");  // exactly 23 chars
            if (s.len() != 23) return false;
            if (!s.isInline()) return false;       // still short at the max
            if (!eqBytes(s, "12345678901234567890123", 23)) return false;
        }
        // 22 bytes: inline with one spare, NUL sits at [22]
        {
            String s("1234567890123456789012");    // 22
            if (s.len() != 22 || !s.isInline()) return false;
            if (s.data()[22] != '\0') return false;
        }
        // 24 bytes: one past inline capacity -> must be on the heap
        {
            String s("123456789012345678901234"); // 24
            if (s.len() != 24) return false;
            if (s.isInline()) return false;        // spilled to heap
            if (!eqBytes(s, "123456789012345678901234", 24)) return false;
            if (s.data()[24] != '\0') return false;
        }
        return true;
    }

    // ---- push across the boundary one byte at a time -----------------------
    bool test_string_push_crosses_boundary()
    {
        String s;
        char model[64];
        for (isize i = 0; i < 40; ++i)
        {
            const char c = (char)('a' + (i % 26));
            s.push(c);
            model[i] = c;
            if (s.len() != i + 1) return false;
            if (!eqBytes(s, model, i + 1)) return false;
            // inline exactly up to and including 23, heap from 24 on
            const bool shouldBeInline = (i + 1) <= 23;
            if (s.isInline() != shouldBeInline) return false;
            if (s.data()[i + 1] != '\0') return false; // NUL trails after every push
        }
        return true;
    }

    // ---- append: short+short stays short, short+long spills ----------------
    bool test_string_append()
    {
        String s("abc");
        s.append(CStr("def"));                 // 6, inline
        if (!eqBytes(s, "abcdef", 6) || !s.isInline()) return false;

        s += CStr("ghij");                     // 10, inline
        if (!eqBytes(s, "abcdefghij", 10) || !s.isInline()) return false;

        s.append(CStr("0123456789ABCDEF"));    // +16 = 26, spills to heap
        if (s.len() != 26 || s.isInline()) return false;
        if (!eqBytes(s, "abcdefghij0123456789ABCDEF", 26)) return false;

        s += 'Z';                              // char append on heap
        if (s.len() != 27 || s[26] != 'Z') return false;
        return true;
    }

    // ---- append empty is a no-op, appending to empty works -----------------
    bool test_string_append_edge()
    {
        String s;
        s.append(CStr("", 0));
        if (s.len() != 0 || !s.isInline()) return false;
        s.append(CStr("x"));
        if (!eqBytes(s, "x", 1)) return false;
        return true;
    }

    // ---- reserve forces heap even for short content, content preserved ------
    bool test_string_reserve()
    {
        String s("short");
        s.reserve(100);                        // must move to heap
        if (s.isInline()) return false;
        if (s.capacity() < 100) return false;
        if (!eqBytes(s, "short", 5)) return false;   // content survived the move
        if (s.data()[5] != '\0') return false;
        // reserve smaller-than-current never shrinks
        s.reserve(1);
        if (s.capacity() < 100) return false;
        return true;
    }

    // ---- clear keeps allocation, resets length + NUL -----------------------
    bool test_string_clear()
    {
        String s("something long enough to be on the heap for sure");
        if (s.isInline()) return false;
        const isize capBefore = s.capacity();
        s.clear();
        if (s.len() != 0) return false;
        if (s.data()[0] != '\0') return false;
        if (s.capacity() != capBefore) return false; // still on heap, cap kept
        s.append(CStr("reused"));
        if (!eqBytes(s, "reused", 6)) return false;
        return true;
    }

    // ---- move: short source ------------------------------------------------
    bool test_string_move_short()
    {
        String a("tiny");
        String b = core::move(a);
        if (!eqBytes(b, "tiny", 4)) return false;
        if (a.len() != 0) return false;        // moved-from is empty
        if (!a.isInline()) return false;       // and short
        a.append(CStr("ok"));                  // still usable
        if (!eqBytes(a, "ok", 2)) return false;
        return true;
    }

    // ---- move: long source (ownership transfers, no double free) -----------
    bool test_string_move_long()
    {
        String a("a string comfortably past the inline capacity threshold");
        if (a.isInline()) return false;
        const isize n = a.len();
        String b = core::move(a);
        if (b.len() != n || b.isInline()) return false;
        if (a.len() != 0 || !a.isInline()) return false;
        // move-assign path too
        String c("literal");
        c = core::move(b);
        if (c.len() != n || c.isInline()) return false;
        if (b.len() != 0) return false;
        return true;
    } // a,b,c destruct here: b is empty-short (no free), c frees once. No double free.

    // ---- clone is independent, across both arms ----------------------------
    bool test_string_clone()
    {
        // short
        {
            String a("dup");
            String b = a.clone();
            if (!eqBytes(b, "dup", 3)) return false;
            b.push('!');                       // mutating b must not touch a
            if (!eqBytes(a, "dup", 3)) return false;
            if (!eqBytes(b, "dup!", 4)) return false;
        }
        // long
        {
            String a("this one is definitely heap allocated, well past 23 bytes");
            String b = a.clone();
            if (b.len() != a.len()) return false;
            if (b.data() == a.data()) return false;   // distinct buffers
            b.clear();
            if (a.len() == 0) return false;           // a untouched
        }
        return true;
    }

    // ---- equality: both overloads, both arms -------------------------------
    bool test_string_equality()
    {
        String a("match");
        String b("match");
        String c("MATCH");
        if (!(a == b)) return false;
        if (a == c) return false;              // case-sensitive, byte compare
        if (!(a == CStr("match"))) return false;
        if (a == CStr("match longer")) return false;  // length differs
        // long vs long
        String d("the quick brown fox jumps over the lazy dog exactly");
        String e("the quick brown fox jumps over the lazy dog exactly");
        if (!(d == e)) return false;
        if (d.isInline()) return false;
        return true;
    }

    // ---- slice yields the right byte view ----------------------------------
    bool test_string_slice()
    {
        String s("0123456789");
        CStr mid = s.slice(3, 4);              // "3456"
        if (mid.len != 4) return false;
        for (isize i = 0; i < 4; ++i)
            if (mid.ptr[i] != s[3 + i]) return false;
        CStr empty = s.slice(10, 0);           // empty tail slice is legal
        if (empty.len != 0) return false;
        return true;
    }

    // ---- String works as a Map/Set key (Hash<String> + operator==) ---------
    bool test_string_as_key()
    {
        Map<String, int32> m;
        m.put(String("one"), 1);
        m.put(String("two"), 2);
        m.put(String("a key long enough to live on the heap, past 23"), 3);
        if (m.len() != 3) return false;
        auto o = m.get(String("two"));
        if (!o || o.unwrap() != 2) return false;
        if (!m.contains(String("one"))) return false;
        if (m.contains(String("three"))) return false;
        // overwrite by equal key (short vs short)
        m.put(String("one"), 11);
        if (m.len() != 3 || m.get(String("one")).unwrap() != 11) return false;

        Set<String> set;
        if (!set.insert(String("x"))) return false;
        if (set.insert(String("x"))) return false;   // duplicate rejected by ==
        if (set.len() != 1) return false;
        return true;
    }

    // ---- differential fuzz: append/clear/push vs a flat byte model ---------
    bool test_string_fuzz_vs_model()
    {
        char model[4096];
        isize mlen = 0;
        String s;
        StrRng rng(0xF00D);
        constexpr isize ITERS = 50000;

        for (isize it = 0; it < ITERS; ++it)
        {
            const uint64 r = rng.next();
            const int op = (int)(r % 10);
            if (op < 6) // push a byte
            {
                if (mlen < (isize)sizeof(model))
                {
                    const char c = (char)('!' + (int)((r >> 8) % 90)); // printable-ish, non-NUL
                    s.push(c);
                    model[mlen++] = c;
                }
            }
            else if (op < 8) // append a short run
            {
                const isize run = (isize)((r >> 8) % 20);
                for (isize k = 0; k < run && mlen < (isize)sizeof(model); ++k)
                {
                    const char c = (char)('A' + (int)((rng.next()) % 26));
                    s.push(c);                 // model via push keeps them in lockstep
                    model[mlen++] = c;
                }
            }
            else if (op < 9) // clear
            {
                s.clear();
                mlen = 0;
            }
            else // full content check
            {
                if (s.len() != mlen) return false;
                for (isize i = 0; i < mlen; ++i)
                    if (s[i] != model[i]) return false;
                if (s.data()[mlen] != '\0') return false;   // NUL invariant holds throughout
                // inline iff within SSO_CAP
                if (mlen > 23 && s.isInline()) return false;
            }
        }
        // final check
        if (s.len() != mlen) return false;
        for (isize i = 0; i < mlen; ++i)
            if (s[i] != model[i]) return false;
        return true;
    }
} // namespace core_stl_test