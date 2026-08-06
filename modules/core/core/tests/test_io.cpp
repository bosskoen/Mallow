// core/core/tests/test_io.cpp
//
// Coverage for core/io/format.h — the {} format engine that powers println /
// mlw_write / panic and the format() members on Optional/Result/Variant/Atomic.
//
// The engine (detail::formatValue / detail::format and the numeric helpers) is
// templated on the output buffer, so these tests render into a small in-memory
// TestBuf that satisfies the FormatBuffer concept and then compare the exact
// bytes produced. That keeps everything pure: no process-wide FormatBufferType,
// no terminal handles, no syscalls — just "given this input, are these the
// right characters". The OS-glue pieces (getFormatBuffer, terminal handles,
// io::writeHandle) are runtime/CRT wrappers exercised transitively by println
// in the real tree and are not unit-tested here; handle.h's trivial value type
// is checked directly.
//
// Float cases use only exactly-representable values with clean decimal
// expansions, so there are no fragile floating comparisons. Two substitution
// footguns are pinned deliberately (see the two _args tests): the engine
// consumes exactly one {} per argument, drops the trailing literal when there
// are more args than placeholders, and leaves surplus {} literal when there are
// fewer.

#include "core/io/format.h"
#include "core/io/handle.h"
#include "core/c_string.h"
#include "core/typedef.h"

using namespace core;

namespace
{
    // In-memory FormatBuffer for byte-exact assertions. append(CStr)/append(char)
    // are the two operations the FormatBuffer concept requires.
    struct TestBuf
    {
        char data[8192];
        index_t len = 0;
        void append(const CStr& s) { for (index_t i = 0; i < s.len; ++i) data[len++] = s.ptr[i]; }
        void append(char c) { data[len++] = c; }
        // Not part of the concept — test helper.
        bool eq(const char* expect) const
        {
            index_t i = 0;
            for (; expect[i] != '\0'; ++i)
                if (i >= len || data[i] != expect[i]) return false;
            return i == len;   // exact length match, no trailing bytes
        }
    };

    // User type that formats into any concrete buffer (the direct path).
    struct BufferFormattable
    {
        template <core::FormatBuffer Buf>
        void format(Buf& b) const { b.append(CStr("USER")); }
    };

    // User type that can only format into a type-erased Sink (the wrapped path).
    struct SinkFormattable
    {
        void format(core::Sink& s) const { s.append(CStr("SINK")); }
    };
}

namespace core_core_test
{
    // =======================================================================
    //  Integers (signed via formatInt, unsigned via formatUInt)
    // =======================================================================
    bool test_io_format_int()
    {
        TestBuf b0; detail::formatValue(b0, 0);        if (!b0.eq("0")) return false;
        TestBuf b1; detail::formatValue(b1, 42);       if (!b1.eq("42")) return false;
        TestBuf b2; detail::formatValue(b2, -7);       if (!b2.eq("-7")) return false;
        TestBuf b3; detail::formatValue(b3, 1000000);  if (!b3.eq("1000000")) return false;
        TestBuf b4; detail::formatValue(b4, (uint32)4000000000u); if (!b4.eq("4000000000")) return false;
        TestBuf b5; detail::formatValue(b5, (uint64)18446744073709551615ull);
        if (!b5.eq("18446744073709551615")) return false;   // UINT64_MAX
        return true;
    }

    // =======================================================================
    //  bool / char
    // =======================================================================
    bool test_io_format_bool_char()
    {
        TestBuf t; detail::formatValue(t, true);  if (!t.eq("true")) return false;
        TestBuf f; detail::formatValue(f, false); if (!f.eq("false")) return false;
        TestBuf c; detail::formatValue(c, 'A');   if (!c.eq("A")) return false;
        return true;
    }

    // =======================================================================
    //  Strings: CStr, const char*, char array
    // =======================================================================
    bool test_io_format_strings()
    {
        TestBuf a; detail::formatValue(a, CStr("hello")); if (!a.eq("hello")) return false;
        const char* p = "world";
        TestBuf b; detail::formatValue(b, p);             if (!b.eq("world")) return false;
        TestBuf c; detail::formatValue(c, "array");       if (!c.eq("array")) return false; // char[6]
        return true;
    }

    // =======================================================================
    //  Non-char array renders as {a, b, c}
    // =======================================================================
    bool test_io_format_array()
    {
        int arr[3] = { 1, 2, 3 };
        TestBuf b; detail::formatValue(b, arr);
        return b.eq("{1, 2, 3}");
    }

    // =======================================================================
    //  Floats — exact-decimal values, trailing zeros trimmed
    // =======================================================================
    bool test_io_format_float()
    {
        TestBuf z;  detail::formatValue(z, 0.0);    if (!z.eq("0.0")) return false;
        TestBuf a;  detail::formatValue(a, 1.5);    if (!a.eq("1.5")) return false;
        TestBuf b;  detail::formatValue(b, 3.125);  if (!b.eq("3.125")) return false;
        TestBuf c;  detail::formatValue(c, -2.25);  if (!c.eq("-2.25")) return false;
        TestBuf d;  detail::formatValue(d, 0.5);    if (!d.eq("0.5")) return false;
        return true;
    }

    // =======================================================================
    //  Float special values
    // =======================================================================
    bool test_io_format_float_special()
    {
        const f64 nan = NumericLimits<f64>::nan;
        const f64 inf = NumericLimits<f64>::infinity;
        TestBuf n; detail::formatValue(n, nan);  if (!n.eq("NaN")) return false;
        TestBuf p; detail::formatValue(p, inf);  if (!p.eq("Inf")) return false;
        TestBuf m; detail::formatValue(m, -inf); if (!m.eq("-Inf")) return false;
        return true;
    }

    // =======================================================================
    //  Hex / pointer formatting
    // =======================================================================
    bool test_io_format_hex()
    {
        TestBuf z; detail::formatHex(z, 0);       if (!z.eq("nullptr")) return false;
        TestBuf v; detail::formatHex(v, 0xABCD);
#if defined(MLW_X64) || defined(MLW_ARM64)
        if (!v.eq("0x000000000000ABCD")) return false;
#else
        if (!v.eq("0x0000ABCD")) return false;
#endif
        // pointer through formatValue: null renders as the hex path's "nullptr"
        TestBuf p; int* np = nullptr; detail::formatValue(p, np);
        return p.eq("nullptr");
    }

    // =======================================================================
    //  User Formattable — direct-into-buffer path
    // =======================================================================
    bool test_io_format_user_buffer()
    {
        BufferFormattable u;
        TestBuf b; detail::formatValue(b, u);
        return b.eq("USER");
    }

    // =======================================================================
    //  User Formattable — Sink-wrapped path
    // =======================================================================
    bool test_io_format_user_sink()
    {
        SinkFormattable u;
        TestBuf b; detail::formatValue(b, u);
        return b.eq("SINK");
    }

    // =======================================================================
    //  {} substitution: basic single + multiple, with surrounding literals
    // =======================================================================
    bool test_io_substitution_basic()
    {
        TestBuf a; detail::format(a, CStr("x={}"), 5);
        if (!a.eq("x=5")) return false;
        TestBuf b; detail::format(b, CStr("{}+{}={}"), 1, 2, 3);
        if (!b.eq("1+2=3")) return false;
        TestBuf c; detail::format(c, CStr("[{}] and [{}]"), CStr("a"), CStr("b"));
        if (!c.eq("[a] and [b]")) return false;
        return true;
    }

    // =======================================================================
    //  {} substitution: no args -> whole string is literal (incl. any {})
    // =======================================================================
    bool test_io_substitution_no_args()
    {
        TestBuf b; detail::format(b, CStr("literal {} stays"));
        return b.eq("literal {} stays");
    }

    // =======================================================================
    //  Footgun 1: fewer args than {} -> leftover {} stays literal
    // =======================================================================
    bool test_io_substitution_too_few_args()
    {
        TestBuf b; detail::format(b, CStr("a={} b={}"), 1);
        // first {} filled; recursion hits the no-arg terminal which appends the
        // remaining " b={}" verbatim.
        return b.eq("a=1 b={}");
    }

    // =======================================================================
    //  Footgun 2: more args than {} -> trailing literal AND extra args dropped
    // =======================================================================
    bool test_io_substitution_too_many_args()
    {
        TestBuf b; detail::format(b, CStr("a={}z"), 1, 2);
        // {} -> "a=1", then recursion formats the tail "z" with a leftover arg;
        // that overload scans for a {} it never finds and returns WITHOUT
        // appending "z". So the trailing 'z' is silently lost. Pinning the
        // current (surprising) behavior — caller must match arity.
        return b.eq("a=1");
    }

    // =======================================================================
    //  Sink round-trip via makeSink
    // =======================================================================
    bool test_io_sink_roundtrip()
    {
        TestBuf b;
        Sink s = makeSink(b);
        s.append(CStr("hi"));
        s.append('!');
        return b.eq("hi!");
    }

    // =======================================================================
    //  Numeric helpers used directly
    // =======================================================================
    bool test_io_numeric_helpers()
    {
        TestBuf u; detail::formatUInt(u, 0);          if (!u.eq("0")) return false;
        TestBuf u2; detail::formatUInt(u2, 250);      if (!u2.eq("250")) return false;
        TestBuf i; detail::formatInt(i, 0);           if (!i.eq("0")) return false;
        TestBuf i2; detail::formatInt(i2, -123);      if (!i2.eq("-123")) return false;
        return true;
    }

    // =======================================================================
    //  Concepts (compile-time)
    // =======================================================================
    bool test_io_concepts()
    {
        static_assert(FormatBuffer<TestBuf>);
        static_assert(FormatBuffer<Sink>);
        static_assert(Formattable<BufferFormattable, TestBuf>);
        static_assert(Formattable<SinkFormattable, Sink>);
        static_assert(!Formattable<SinkFormattable, TestBuf>);   // Sink-only, not buffer
        static_assert(FormattableValue<int, TestBuf>);
        static_assert(FormattableValue<bool, TestBuf>);
        static_assert(FormattableValue<CStr, TestBuf>);
        static_assert(FormattableValue<f64, TestBuf>);
        static_assert(FormattableValue<int*, TestBuf>);
        static_assert(FormattableValue<BufferFormattable, TestBuf>);
        static_assert(detail::checkFormattable<int, bool, CStr, f64>());
        static_assert(detail::ArgsCount<int, bool, CStr>() == 3);
        return true;
    }

    // =======================================================================
    //  io::Handle value type (trivial, no OS interaction)
    // =======================================================================
    bool test_io_handle()
    {
#if defined(MLW_WINDOWS)
        void* native = reinterpret_cast<void*>(static_cast<uptr>(7));
        io::Handle h(native);
        return h.fd == native;
#else
        io::Handle h(7);
        return h.fd == 7;
#endif
    }
}
