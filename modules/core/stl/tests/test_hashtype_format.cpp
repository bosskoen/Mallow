// A minimal FormatBuffer for tests: fixed stack buffer, no allocation, no IO.
// Satisfies core::FormatBuffer (append(CStr)->void, append(char)->void).

#include "stl/set.h"

using namespace core;

struct TestFmtBuf
{
    char    data[1024];
    index_t len = 0;
    void append(const core::CStr &s) { for (index_t i = 0; i < s.len; ++i) data[len++] = s.ptr[i]; }
    void append(char c)        { data[len++] = c; }
};

static bool bufEq(const TestFmtBuf &b, const char *p)   // exact match vs a C literal
{
    index_t i = 0;
    while (p[i]) { if (i >= b.len || b.data[i] != p[i]) return false; ++i; }
    return i == b.len;
}
static bool bufHas(const TestFmtBuf &b, const char *p)  // substring present anywhere
{
    index_t n = 0; while (p[n]) ++n;
    if (n == 0) return true;
    for (index_t s = 0; s + n <= b.len; ++s)
    {
        bool ok = true;
        for (index_t j = 0; j < n; ++j) if (b.data[s + j] != p[j]) { ok = false; break; }
        if (ok) return true;
    }
    return false;
}
static index_t bufSeps(const TestFmtBuf &b)             // count ", " separators
{
    index_t c = 0;
    for (index_t i = 0; i + 1 < b.len; ++i) if (b.data[i] == ',' && b.data[i + 1] == ' ') ++c;
    return c;
}

// NOTE: the exact-match literals below assume the entry format `[key; value]`
// with `, ` between entries (Map) and bare values with `, ` (Set). If you chose
// different separators, update the bufEq strings — the structural checks hold
// regardless.

namespace core_stl_test
{
bool test_map_format()
{
    // empty -> "{}"
    {
        Map<int32, int32> m;
        TestFmtBuf b; m.format(b);
        if (!bufEq(b, "{}")) return false;
    }
    // single entry -> exact (order irrelevant with one element)
    {
        Map<int32, int32> m; m.put(7, 100);
        TestFmtBuf b; m.format(b);
        if (!bufEq(b, "{[7; 100]}")) return false;
    }
    // several entries -> order undefined, so check structure not sequence
    {
        Map<int32, int32> m; m.put(1, 10); m.put(2, 20); m.put(3, 30);
        TestFmtBuf b; m.format(b);
        if (b.len < 2 || b.data[0] != '{' || b.data[b.len - 1] != '}') return false;
        if (!bufHas(b, "[1; 10]") || !bufHas(b, "[2; 20]") || !bufHas(b, "[3; 30]")) return false;
        if (bufSeps(b) != 2) return false;   // 3 entries -> 2 separators
    }
    return true;
}

bool test_set_format()
{
    // empty -> "{}"
    {
        Set<int32> s;
        TestFmtBuf b; s.format(b);
        if (!bufEq(b, "{}")) return false;
    }
    // single element -> exact
    {
        Set<int32> s; s.insert(5);
        TestFmtBuf b; s.format(b);
        if (!bufEq(b, "{5}")) return false;
    }
    // several -> structural (values chosen so none is a substring of another)
    {
        Set<int32> s; s.insert(11); s.insert(22); s.insert(33);
        TestFmtBuf b; s.format(b);
        if (b.len < 2 || b.data[0] != '{' || b.data[b.len - 1] != '}') return false;
        if (!bufHas(b, "11") || !bufHas(b, "22") || !bufHas(b, "33")) return false;
        if (bufSeps(b) != 2) return false;
    }
    return true;
}
}