// core/core/tests/test_c_string.cpp
//
// Coverage for core/c_string.h: the CStr view and mlwStrlen. The literal and
// pointer+length constructors are constexpr and checked at compile time; the
// documented quirks (extent-minus-one length, counting through an embedded
// NUL) are pinned so a future "smarter" ctor can't silently change them.
// fromPtr()/mlwStrlen() are runtime (they call into the strlen alias).

#include "core/c_string.h"

using namespace core;

namespace core_core_test
{
    // ---- literal ctor: len is array extent - 1 ----------------------------
    bool test_cstr_from_literal()
    {
        constexpr CStr s("hello");
        static_assert(s.len == 5);
        static_assert(s.ptr[0] == 'h' && s.ptr[4] == 'o');
        constexpr CStr empty("");
        static_assert(empty.len == 0);
        return true;
    }

    // ---- documented quirk: embedded NUL is counted, not a terminator ------
    bool test_cstr_embedded_nul_quirk()
    {
        // "ab\0cd" is a char[6]; the header stores extent-1 == 5, walking
        // straight through the embedded NUL. This is the behaviour the header
        // explicitly warns about; lock it so it doesn't drift.
        constexpr CStr s("ab\0cd");
        static_assert(s.len == 5);
        return true;
    }

    // ---- pointer + length ctor --------------------------------------------
    bool test_cstr_from_ptr_len()
    {
        const char raw[] = {'a', 'b', 'c', 'd'};
        CStr s(raw, 3);
        return s.ptr == raw && s.len == 3;
    }

    // ---- fromPtr measures at runtime via mlwStrlen ------------------------
    bool test_cstr_from_ptr()
    {
        const char* lit = "measured";
        CStr s = CStr::fromPtr(lit);
        return s.ptr == lit && s.len == 8;
    }

    // ---- mlwStrlen directly -----------------------------------------------
    bool test_strlen()
    {
        if (mlwStrlen("") != 0) return false;
        if (mlwStrlen("a") != 1) return false;
        if (mlwStrlen("hello") != 5) return false;
        // stops at the first NUL, unlike the literal CStr ctor above
        if (mlwStrlen("ab\0cd") != 2) return false;
        return true;
    }
} // namespace core_core_test
