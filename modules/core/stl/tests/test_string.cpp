// core/stl/tests/test_string.cpp
//
// Coverage for stl/string.h (core::String — SSO byte string). Runtime bool
// checks like the other stl suites: a test returns false on the first failed
// condition. No libc/STL; the fuzz test mirrors the string against a flat byte
// model.
//
// The interesting surface is the SSO boundary — the union's short<->long
// transition at len 23/24 — and that the maintained NUL and length bookkeeping
// stay correct across it, through every mutating operation.

#include "stl/string.h"
#include "stl/set.h"    // Map/Set, for the Hash<String> test
#include "stl/vector.h" // Vector<String> relocation test
#include "test_types.h"

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
        CStr v = s;                            // implicit view
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
        CountingAlloc counter; // counts live heap blocks, so leaks show up without a sanitizer
        {
            const auto *al = counter.get();
            String a(CStr("a string comfortably past the inline capacity threshold"), al);
            if (a.isInline()) return false;
            const isize n = a.len();
            String b = core::move(a);
            if (b.len() != n || b.isInline()) return false;
            if (a.len() != 0 || !a.isInline()) return false;
            // move-assign path too
            String c(CStr("literal"), al);
            c = core::move(b);
            if (c.len() != n || c.isInline()) return false;
            if (b.len() != 0) return false;
            // move-assign INTO a long string: its old heap block must be freed
            String d(CStr("this destination is also long enough to own a heap block"), al);
            if (counter.live != 2) return false;
            d = core::move(c);
            if (counter.live != 1) return false;
            if (d.len() != n || c.len() != 0) return false;
            d = core::move(d);                 // self move-assign is a no-op
            if (d.len() != n) return false;
        }
        return counter.live == 0;              // no leak, and the harness catches double frees
    }

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

    // ---- differential fuzz: every mutator vs a flat byte model --------------
    // Exercises push/append/pop/truncate/resize/insert/removeRange/clear and
    // shrinkToFit in random order, so lengths wander back and forth across the
    // SSO boundary through every code path, checking content + NUL as it goes.
    bool test_string_fuzz_vs_model()
    {
        constexpr isize CAP = 4096;
        char model[CAP];
        isize mlen = 0;
        String s;
        StrRng rng(0xF00D);
        constexpr isize ITERS = 50000;

        for (isize it = 0; it < ITERS; ++it)
        {
            const uint64 r = rng.next();
            const isize arg = (isize)((r >> 16) % 40);
            const char c = (char)('!' + (int)((r >> 8) % 90)); // printable, non-NUL
            switch ((int)(r % 12))
            {
            case 0: case 1: case 2: // push
                if (mlen < CAP) { s.push(c); model[mlen++] = c; }
                break;
            case 3: case 4: // append a run of `arg` bytes
            {
                char run[40];
                for (isize k = 0; k < arg; ++k) run[k] = (char)(c + (k % 5));
                if (mlen + arg <= CAP)
                {
                    s.append(CStr(run, arg));
                    for (isize k = 0; k < arg; ++k) model[mlen++] = run[k];
                }
                break;
            }
            case 5: // pop
                if (mlen > 0) { if (s.pop() != model[mlen - 1]) return false; --mlen; }
                break;
            case 6: // truncate (sometimes a no-op)
                s.truncate(arg);
                if (arg < mlen) mlen = arg;
                break;
            case 7: // resize with a fill byte
            {
                const isize n = mlen + arg - 20 < 0 ? 0 : mlen + arg - 20;
                if (n <= CAP)
                {
                    s.resize(n, c);
                    for (isize k = mlen; k < n; ++k) model[k] = c;
                    mlen = n;
                }
                break;
            }
            case 8: // insert `arg % 8` bytes at a random position
            {
                const isize n = arg % 8;
                const isize at = mlen == 0 ? 0 : (isize)(rng.next() % (uint64)(mlen + 1));
                char ins[8];
                for (isize k = 0; k < n; ++k) ins[k] = (char)(c + k);
                if (mlen + n <= CAP)
                {
                    s.insert(at, CStr(ins, n));
                    for (isize k = mlen - 1; k >= at; --k) model[k + n] = model[k];
                    for (isize k = 0; k < n; ++k) model[at + k] = ins[k];
                    mlen += n;
                }
                break;
            }
            case 9: // removeRange
            {
                if (mlen == 0) break;
                const isize at = (isize)(rng.next() % (uint64)mlen);
                const isize n = arg % (mlen - at + 1);
                s.removeRange(at, n);
                for (isize k = at; k + n < mlen; ++k) model[k] = model[k + n];
                mlen -= n;
                break;
            }
            case 10: // clear or shrinkToFit (content unchanged)
                if (arg < 4) { s.clear(); mlen = 0; }
                else s.shrinkToFit();
                break;
            default: // full content check
                if (s.len() != mlen) return false;
                for (isize i = 0; i < mlen; ++i)
                    if (s[i] != model[i]) return false;
                if (s.data()[mlen] != '\0') return false;  // NUL invariant holds throughout
                if (mlen > 23 && s.isInline()) return false;
                break;
            }
        }
        if (s.len() != mlen) return false;
        for (isize i = 0; i < mlen; ++i)
            if (s[i] != model[i]) return false;
        return s.data()[mlen] == '\0';
    }

    // ---- resize fills with NUL by default (regression: used to write '0') -----
    bool test_string_resize()
    {
        String s("ab");
        s.resize(5);
        if (s.len() != 5 || s[0] != 'a' || s[1] != 'b') return false;
        for (isize i = 2; i < 5; ++i)
            if (s[i] != '\0') return false;
        s.resize(30, 'x');                     // spills to heap, custom fill
        if (s.len() != 30 || s.isInline()) return false;
        for (isize i = 5; i < 30; ++i)
            if (s[i] != 'x') return false;
        if (s.data()[30] != '\0') return false;
        s.resize(1);                           // shrinking == truncate
        if (!eqBytes(s, "a", 1)) return false;
        return true;
    }

    bool test_string_truncate_pop()
    {
        String s("hello world, this one is on the heap");
        s.truncate(5);
        if (!eqBytes(s, "hello", 5) || s.data()[5] != '\0') return false;
        s.truncate(50);                        // no-op when n >= len
        if (s.len() != 5) return false;
        if (s.pop() != 'o' || s.pop() != 'l') return false;
        if (!eqBytes(s, "hel", 3)) return false;
        return true;
    }

    // front/back/get return Optional<char&>, consistent with Vector and List.
    bool test_string_front_back_get()
    {
        String e;
        if (e.front().isSome() || e.back().isSome() || e.get(0).isSome()) return false;
        String s("abc");
        if (s.front().unwrap() != 'a' || s.back().unwrap() != 'c') return false;
        if (s.get(1).unwrap() != 'b' || s.get(3).isSome() || s.get(-1).isSome()) return false;
        s.back().unwrap() = 'z';               // references, not copies
        if (!eqBytes(s, "abz", 3)) return false;
        return true;
    }

    // s.append(s) and friends: the source may live in the buffer that grows.
    bool test_string_append_self()
    {
        String a("0123456789");                // 10 + 10 = 20: stays inline
        a.append(a);
        if (!eqBytes(a, "01234567890123456789", 20) || !a.isInline()) return false;
        a.append(a);                           // 40: short -> long while reading itself
        if (a.len() != 40 || a.isInline()) return false;
        for (isize i = 0; i < 40; ++i)
            if (a[i] != (char)('0' + i % 10)) return false;
        a.append(a.slice(2, 3));               // long -> realloc while reading a slice of itself
        if (a.len() != 43 || a[40] != '2' || a[42] != '4') return false;
        return true;
    }

    bool test_string_insert_remove_range()
    {
        String s("held");
        s.insert(3, CStr("lo wor"));           // "hello word"
        s.insert(0, CStr(">> "));
        s.insert(s.len(), CStr("!"));           // insert at end == append
        if (!eqBytes(s, ">> hello word!", 14)) return false;
        s.removeRange(0, 3);
        if (!eqBytes(s, "hello word!", 11)) return false;
        s.insert(0, s.slice(6, 4));            // self-insert, source AFTER the gap: shifts under us
        if (!eqBytes(s, "wordhello word!", 15)) return false;
        s.removeRange(0, 4);
        s.insert(5, s.slice(0, 5));            // self-insert, source before the gap
        if (!eqBytes(s, "hellohello word!", 16)) return false;
        s.insert(16, CStr(" and a long tail")); // crosses the SSO boundary mid-insert
        if (s.isInline() || s.len() != 32) return false;
        s.removeRange(5, 27);
        if (!eqBytes(s, "hello", 5) || s.data()[5] != '\0') return false;
        return true;
    }

    bool test_string_search()
    {
        String s("path/to/file.txt");
        if (!s.startsWith(CStr("path/")) || s.startsWith(CStr("to"))) return false;
        if (!s.endsWith(CStr(".txt")) || s.endsWith(CStr(".tx"))) return false;
        if (s.find(CStr("/")).unwrap() != 4) return false;
        if (s.find(CStr("file")).unwrap() != 8) return false;
        if (s.find(CStr("nope")).isSome()) return false;
        if (s.find(CStr("")).unwrap() != 0) return false;   // empty needle matches at 0
        if (!s.contains(CStr("to/f")) || s.contains(CStr("path/to/file.txt!"))) return false;
        if (!s.startsWith(CStr("")) || !s.endsWith(CStr(""))) return false;
        String e;
        if (e.find(CStr("a")).isSome() || !e.startsWith(CStr(""))) return false;
        return true;
    }

    // String is a FormatBuffer: you can format straight into it.
    bool test_string_as_format_buffer()
    {
        static_assert(core::FormatBuffer<String>, "String must satisfy FormatBuffer");
        String s;
        mlw_write(s, "x={} y={}", 42, CStr("ab"));
        if (!(s == CStr("x=42 y=ab"))) return false;
        String name("bob");
        mlw_write(s, ", name={}", name);       // a String formats into a String
        if (!(s == CStr("x=42 y=ab, name=bob"))) return false;
        return true;
    }

    bool test_string_shrink_to_fit()
    {
        CountingAlloc counter;
        String s(CStr("a long string that definitely lives on the heap"), counter.get());
        s.truncate(4);
        if (s.isInline() || counter.live != 1) return false;
        s.shrinkToFit();                       // fits inline again -> moves back, frees heap
        if (!s.isInline() || !eqBytes(s, "a lo", 4) || s.data()[4] != '\0') return false;
        if (counter.live != 0) return false;   // the heap block really was released

        String t("another heap string, comfortably over 23");
        t.reserve(200);
        t.shrinkToFit();                       // stays long, capacity == len
        if (t.isInline() || t.capacity() != t.len()) return false;
        if (!eqBytes(t, "another heap string, comfortably over 23", 40)) return false;
        return true;
    }

    // tryReserve reports OOM; and short strings never touch the allocator at all.
    bool test_string_try_reserve_failure()
    {
        String s(CStr("fits inline"), &FAILING_ALLOC);
        if (!eqBytes(s, "fits inline", 11)) return false;    // no allocation needed
        if (s.tryReserve(100)) return false;
        if (!eqBytes(s, "fits inline", 11) || !s.isInline()) return false; // unchanged
        if (!s.tryReserve(20)) return false;                 // within SSO_CAP: no-op
        return true;
    }

    bool test_string_uses_its_allocator()
    {
        CountingAlloc counter;
        {
            String s(CStr("short"), counter.get());
            if (s.allocator() != counter.get() || counter.requests != 0) return false;
            s.append(CStr(" and now long enough to need the heap"));
            if (counter.requests == 0) return false;
            String c = s.clone();
            if (c.allocator() != counter.get()) return false;
        }
        return counter.live == 0;
    }

    // Vector<String> must relocate element-wise (SSO strings point into themselves).
    bool test_string_in_vector()
    {
        core::Vector<String> v;
        for (int i = 0; i < 40; ++i)           // several grows
        {
            String s(CStr("item "));
            s.push((char)('a' + i % 26));
            if (i % 3 == 0) s.append(CStr(" -- padded out past the inline limit"));
            v.push(core::move(s));
        }
        for (int i = 0; i < 40; ++i)
        {
            if (v[i][5] != (char)('a' + i % 26)) return false;
            if (v[i].data()[v[i].len()] != '\0') return false;
            if ((i % 3 == 0) == v[i].isInline()) return false;
        }
        if (!(v.find(String(CStr("item b"))).unwrap() == 1)) return false;
        return true;
    }
} // namespace core_stl_test