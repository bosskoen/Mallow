// freestanding_mem.cpp
//
// Freestanding implementations of the C-named functions the compiler emits by
// name even under -ffreestanding: strlen, memset, memcpy, memmove, memcmp. Each
// exists as a real out-of-line symbol; the public mlw* names alias the same
// address (GCC/Clang) or tail-call forward to it (MSVC).
//
// BUILD REQUIREMENT - compile this file with idiom recognition and strict
// aliasing OFF, or the loops below get rewritten into calls to themselves
// (infinite recursion). In CMake:
//   set_source_files_properties(freestanding_mem.cpp PROPERTIES COMPILE_FLAGS
//     "$<IF:$<CXX_COMPILER_ID:MSVC>,/Oi-,-fno-builtin -fno-tree-loop-distribute-patterns -fno-strict-aliasing>")

#include "libc/mem.h"

// strlen: byte-scan to alignment, then word-at-a-time using the classic SWAR
// zero-byte test. himagic/lomagic detect a zero byte inside a word; on a hit the
// exact position is confirmed byte-by-byte (the test can false-positive).
extern "C" usize strlen(const char *str)
{
    const char *p = str;
    // byte-at-a-time until word-aligned, so the word loads below are aligned
    while (((uptr)p & (sizeof(usize) - 1)) != 0)
    {
        if (*p == '\0')
            return (usize)(p - str);
        ++p;
    }
    const usize himagic = (usize)0x8080808080808080ULL; // truncates to 0x80808080 on 32-bit
    const usize lomagic = (usize)0x0101010101010101ULL;
    const usize *w = (const usize *)p;
    for (;;)
    {
        usize word = *w++;
        if (((word - lomagic) & ~word & himagic) != 0)
        { // a zero byte is (probably) here
            const char *cp = (const char *)(w - 1);
            for (unsigned i = 0; i < sizeof(usize); ++i)
                if (cp[i] == 0)
                    return (usize)(cp - str) + i; // confirm, handle misfire
        }
    }
}

// Public alias / forwarder to the same code (see file header).
#ifdef MLW_MSVC
usize core::mlwStrlen(const char* s){ return strlen(s); }   // -> `jmp strlen`
#elif defined(MLW_GCC) || defined(MLW_CLANG)
namespace core { usize mlwStrlen(const char*) __attribute__((alias("strlen"))); }
#endif


// A word type allowed to alias any object, so the byte<->word pointer casts
// below are not strict-aliasing UB.
#if defined(MLW_GCC) || defined(MLW_CLANG)
    typedef usize __attribute__((may_alias)) mlw_word;
#else
    typedef usize mlw_word;
#endif

// All four routines share the same head/word/tail shape: a byte loop advances
// the destination to word alignment, a word loop does the bulk, and a byte loop
// finishes the remainder. Because the head loop aligns before the word loop
// runs, the word accesses are aligned.

extern "C" void* memset(void* dst, int value, usize n) {
    uint8* d = (uint8*)dst;
    uint8  v = (uint8)value;
    while (n && ((uptr)d & (sizeof(usize) - 1))) { *d++ = v; --n; }     // head: align d
    mlw_word  word = (mlw_word)((usize)v * (usize)0x0101010101010101ULL); // broadcast byte to all lanes
    mlw_word* dw   = (mlw_word*)d;
    while (n >= sizeof(usize)) { *dw++ = word; n -= sizeof(usize); }    // words
    d = (uint8*)dw;
    while (n--) *d++ = v;                                               // tail
    return dst;
}

extern "C" void* memcpy(void* __restrict dst, const void* __restrict src, usize n) {
    // __restrict: caller promises no overlap (that is memmove's job).
    uint8* d = (uint8*)dst; const uint8* s = (const uint8*)src;
    while (n && ((uptr)d & (sizeof(usize) - 1))) { *d++ = *s++; --n; }  // head: align d
    mlw_word* dw = (mlw_word*)d; const mlw_word* sw = (const mlw_word*)s;
    while (n >= sizeof(usize)) { *dw++ = *sw++; n -= sizeof(usize); }   // words
    d = (uint8*)dw; s = (const uint8*)sw;
    while (n--) *d++ = *s++;                                            // tail
    return dst;
}

extern "C" void* memmove(void* dst, const void* src, usize n) {
    // Overlap-safe: choose copy direction so a byte is never read after being
    // overwritten. d < s copies forward; d > s copies backward.
    uint8* d = (uint8*)dst; const uint8* s = (const uint8*)src;
    if (d == s || n == 0) return dst;
    if (d < s) {                                    // forward is safe (incl. overlap)
        while (n && ((uptr)d & (sizeof(usize) - 1))) { *d++ = *s++; --n; }
        mlw_word* dw = (mlw_word*)d; const mlw_word* sw = (const mlw_word*)s;
        while (n >= sizeof(usize)) { *dw++ = *sw++; n -= sizeof(usize); }
        d = (uint8*)dw; s = (const uint8*)sw;
        while (n--) *d++ = *s++;
    } else {                                        // overlap with d > s -> go backward
        d += n; s += n;
        while (n && ((uptr)d & (sizeof(usize) - 1))) { *--d = *--s; --n; }
        mlw_word* dw = (mlw_word*)d; const mlw_word* sw = (const mlw_word*)s;
        while (n >= sizeof(usize)) { *--dw = *--sw; n -= sizeof(usize); }
        d = (uint8*)dw; s = (const uint8*)sw;
        while (n--) *--d = *--s;
    }
    return dst;
}

extern "C" int memcmp(const void* a, const void* b, usize n) {
    const uint8* x = (const uint8*)a; const uint8* y = (const uint8*)b;
    while (n >= sizeof(usize)) {                     // fast word-equality scan
        if (*(const mlw_word*)x != *(const mlw_word*)y) break; // differ -> refine below
        x += sizeof(usize); y += sizeof(usize); n -= sizeof(usize);
    }
    while (n--) { if (*x != *y) return (int)*x - (int)*y; ++x; ++y; }  // byte refine (endian-safe)
    return 0;
}

// ---- public mlw* names: same address as the C symbols above ----
#if defined(MLW_GCC) || defined(MLW_CLANG)
namespace core {
    void* mlwMemcpy(void*, const void*, usize)  __attribute__((alias("memcpy")));
    void* mlwMemset(void*, int, usize)          __attribute__((alias("memset")));
    void* mlwMemmove(void*, const void*, usize) __attribute__((alias("memmove")));
    int   mlwMemcmp(const void*, const void*, usize) __attribute__((alias("memcmp")));
}
#else // MSVC: no alias attribute - tail-call forwarders compile to `jmp` at /O2
namespace core {
    void* mlwMemcpy(void* d, const void* s, usize n){ return memcpy(d, s, n); }
    void* mlwMemset(void* d, int v, usize n){ return memset(d, v, n); }
    void* mlwMemmove(void* d, const void* s, usize n){ return memmove(d, s, n); }
    int   mlwMemcmp(const void* a, const void* b, usize n){ return memcmp(a, b, n); }
}
#endif