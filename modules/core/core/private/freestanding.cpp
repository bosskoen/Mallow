
#include "libc./str.h"
#include "libc/mem.h"

#ifdef MLW_MSVC
#pragma function(memcpy, memset, strlen)   // file scope, before the definitions
#endif



extern "C" usize strlen(const char *str)
{
    const char *p = str;
    // byte-at-a-time until word-aligned
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

#ifdef MLW_MSVC
usize core::mlwStrlen(const char* s){ return strlen(s); }   // -> `jmp strlen`
#elif defined(MLW_GCC) || defined(MLW_CLANG)
namespace core { usize mlwStrlen(const char*) __attribute__((alias("strlen"))); }
#endif


// freestanding_mem.cpp — memcpy / memset / memmove / memcmp for the freestanding build.
//
// The compiler emits calls to these four by their C names even in -ffreestanding
// (struct copy -> memcpy, array zero-init -> memset), so they MUST exist as real
// out-of-line symbols. The mlw* public names are aliased to the same address.
//
// COMPILE THIS FILE WITH IDIOM RECOGNITION + STRICT ALIASING OFF, e.g. in CMake:
//   set_source_files_properties(freestanding_mem.cpp PROPERTIES COMPILE_FLAGS
//     "$<IF:$<CXX_COMPILER_ID:MSVC>,/Oi-,-fno-builtin -fno-tree-loop-distribute-patterns -fno-strict-aliasing>")
// Without it the loops below get rewritten into calls to themselves (infinite recursion).
 

 
// A word type that is allowed to alias any object (kills strict-aliasing UB on the casts).
#if defined(MLW_GCC) || defined(MLW_CLANG)
    typedef usize __attribute__((may_alias)) mlw_word;
#else
    typedef usize mlw_word;
#endif
// NOTE: the word loops assume the target tolerates unaligned loads for the SOURCE
// pointer (true on x86-64 and ARM64). On a strict-alignment arch, drop to the
// byte loops or align both pointers.
 
extern "C" void* memset(void* dst, int value, usize n) {
    uint8* d = (uint8*)dst;
    uint8  v = (uint8)value;
    while (n && ((uptr)d & (sizeof(usize) - 1))) { *d++ = v; --n; }     // head
    mlw_word  word = (mlw_word)((usize)v * (usize)0x0101010101010101ULL);
    mlw_word* dw   = (mlw_word*)d;
    while (n >= sizeof(usize)) { *dw++ = word; n -= sizeof(usize); }    // words
    d = (uint8*)dw;
    while (n--) *d++ = v;                                               // tail
    return dst;
}
 
extern "C" void* memcpy(void* __restrict dst, const void* __restrict src, usize n) {
    uint8* d = (uint8*)dst; const uint8* s = (const uint8*)src;
    while (n && ((uptr)d & (sizeof(usize) - 1))) { *d++ = *s++; --n; }
    mlw_word* dw = (mlw_word*)d; const mlw_word* sw = (const mlw_word*)s;
    while (n >= sizeof(usize)) { *dw++ = *sw++; n -= sizeof(usize); }
    d = (uint8*)dw; s = (const uint8*)sw;
    while (n--) *d++ = *s++;
    return dst;
}
 
extern "C" void* memmove(void* dst, const void* src, usize n) {
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
        if (*(const mlw_word*)x != *(const mlw_word*)y) break;
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
#else // MSVC: no alias attribute — tail-call forwarders compile to `jmp` at /O2
namespace core {
    void* mlwMemcpy(void* d, const void* s, usize n){ return memcpy(d, s, n); }
    void* mlwMemset(void* d, int v, usize n){ return memset(d, v, n); }
    void* mlwMemmove(void* d, const void* s, usize n){ return memmove(d, s, n); }
    int   mlwMemcmp(const void* a, const void* b, usize n){ return memcmp(a, b, n); }
}
#endif