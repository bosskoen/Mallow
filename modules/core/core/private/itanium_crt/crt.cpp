// crt_itanium.cpp — Itanium C++ ABI runtime support.
//
// Used by GCC and Clang, on BOTH Windows (MinGW) and Linux. The C++ ABI is
// decided by the COMPILER, not the OS: GCC-on-Windows uses these Itanium
// symbols (__cxa_*, .init_array), NOT the MSVC .CRT$XD/_Init_thread_* symbols.
//
// The OS-specific bits (mmap vs VirtualAlloc, exit_group vs ExitProcess) live
// behind MLW_WINDOWS / MLW_LINUX and are the SAME regardless of which C++ ABI
// the compiler uses.

#include "../crt_internals.h"
#include "core/thread/mutex.h"
#include "core/thread/lock.h"
#include "core/thread/candvar.h"
#include "core/thread/atomic.h"
#include "../posix/syscall_api.h"

// ════════════════════════════════════════════════════════════════════════
// 1. Global constructors — walk .init_array (Itanium/ELF & MinGW)
// ════════════════════════════════════════════════════════════════════════
// The linker synthesizes these bracketing the .init_array section, which the
// compiler fills with one pointer per non-trivial global constructor.
// This is the Itanium analog of MSVC's .CRT$XC walk.
extern "C" {
    extern void (*__init_array_start[])(void);
    extern void (*__init_array_end[])(void);
}

namespace crt {
void run_global_ctors() {
    for (auto p = __init_array_start; p != __init_array_end; ++p)
        if (*p) (*p)();
}
}

const unsigned long* mlw_crt_auxv = nullptr;

// ════════════════════════════════════════════════════════════════════════
// 2. Global destructors — __cxa_atexit / __dso_handle
// ════════════════════════════════════════════════════════════════════════
// The compiler calls __cxa_atexit(dtor, arg, dso) from each global's ctor to
// register its destructor. This replaces the plain atexit() the MSVC ABI uses.
// __dso_handle identifies "this module"; for a single executable we ignore it.
extern "C"{ void* __dso_handle = nullptr;}

extern "C" unsigned long __getauxval(unsigned long type)
{
    for (const unsigned long* p = mlw_crt_auxv; p && p[0]; p += 2)
        if (p[0] == type)          // p[0] = a_type, p[1] = a_val
            return p[1];
    return 0;                      // not found (safe: libgcc reads this as "no LSE")
}

namespace {
    struct DtorEntry { void (*fn)(void*); void* arg; };
    constexpr uint32 MLW_ATEXIT_MAX = 64;
    core::sync::Atomic<uint32> g_dtor_count{0};
    DtorEntry g_dtors[MLW_ATEXIT_MAX];
}

extern "C" int __cxa_atexit(void (*fn)(void*), void* arg, void* /*dso*/) {
    uint32 i = g_dtor_count.fetchAdd(1, core::sync::MemoryOrder::AcqRel);
    if (i >= MLW_ATEXIT_MAX) {
        g_dtor_count.fetchSub(1, core::sync::MemoryOrder::Relaxed);
        panic_mem("__cxa_atexit table full");
    }
    g_dtors[i] = { fn, arg };
    return 0;
}

// Some toolchains also reference plain atexit() (signature differs — no arg).
extern "C" int atexit(void (*fn)(void)) {
    // adapt the no-arg form into the arg form
    return __cxa_atexit(reinterpret_cast<void(*)(void*)>(fn), nullptr, __dso_handle);
}

// __cxa_finalize(dso): run registered dtors. Called at exit; we drive it from
// run_global_dtors. A null dso means "all".
extern "C" void __cxa_finalize(void* /*dso*/) {
    uint32 n = g_dtor_count.load(core::sync::MemoryOrder::Acquire);
    if (n > MLW_ATEXIT_MAX) n = MLW_ATEXIT_MAX;
    for (uint32 i = n; i-- > 0; )                 // LIFO
        if (g_dtors[i].fn) { g_dtors[i].fn(g_dtors[i].arg); g_dtors[i].fn = nullptr; }
}

namespace crt {
void run_global_dtors() { __cxa_finalize(nullptr); }
}

// ════════════════════════════════════════════════════════════════════════
// 3. Function-static guards — __cxa_guard_acquire / release / abort
// ════════════════════════════════════════════════════════════════════════
// Itanium's replacement for MSVC's _Init_thread_header/footer. The guard is a
// 64-bit word: byte 0 = "initialized", byte 1 = "in progress" (our convention).
// Same global-lock + CV pattern as the MSVC version — but NO epoch fast path;
// the compiler's own fast path is the plain byte-0 check before it ever calls
// acquire, so acquire is only reached when byte 0 is still 0.
//
// Contract:
//   __cxa_guard_acquire(g): returns 1 -> caller constructs, then calls release.
//                           returns 0 -> already constructed, caller skips.
//                           blocks if another thread is mid-construction.
//   __cxa_guard_release(g): mark initialized, wake waiters.
//   __cxa_guard_abort(g):   ctor threw -> clear in-progress, let next try.
namespace {
    core::sync::Mutex g_guard_lock{};
    core::sync::CondVar g_guard_cv{};

    inline unsigned char& guard_done(uint64* g)    { return reinterpret_cast<unsigned char*>(g)[0]; }
    inline unsigned char& guard_pending(uint64* g) { return reinterpret_cast<unsigned char*>(g)[1]; }
}

extern "C" int __cxa_guard_acquire(uint64* g) {
    core::sync::Lock l{g_guard_lock};
    // wait out any other thread currently constructing THIS guard
    g_guard_cv.wait(l, [g]() -> bool { return guard_pending(g) == 0; });
    if (guard_done(g)) return 0;              // someone finished it -> skip
    guard_pending(g) = 1;                     // claim it
    return 1;                                 // caller constructs
}

extern "C" void __cxa_guard_release(uint64* g) {
    core::sync::Lock l{g_guard_lock};
    // publish with release so the compiler's lock-free byte-0 read sees it
    // (byte store under lock + wake; on weak archs the wake path carries the
    //  barrier, but mark done via an atomic release to be correct)
    
    guard_done(g) = 1;
    // reinterpret_cast<core::sync::Atomic<unsigned char>*>(&guard_done(g))
    //     ->store(1, core::sync::MemoryOrder::Release);

    guard_pending(g) = 0;
    l.unlock();
    g_guard_cv.wakeAll();
}

extern "C" void __cxa_guard_abort(uint64* g) {
    core::sync::Lock l{g_guard_lock};
    guard_pending(g) = 0;                     // reset so another thread retries
    l.unlock();
    g_guard_cv.wakeAll();
}

// ════════════════════════════════════════════════════════════════════════
// 4. Thread-local destructors — __cxa_thread_atexit
// ════════════════════════════════════════════════════════════════════════
// Itanium's replacement for __tlregdtor. The compiler calls this when a
// function-local thread_local with a non-trivial dtor is first constructed on
// a thread. Normally libc/pthread backs this and runs the dtors at thread exit
// via a pthread key. We have NO pthread, so:
//   - we store into a per-thread list here,
//   - and WE drain it: in the Thread wrapper (spawned threads) and in mlwExit
//     (the main thread). See run_thread_local_dtors below.
namespace {
    struct ThreadDtor { void (*fn)(void*); void* obj; };
    constexpr int MLW_TLS_DTOR_MAX = 32;
    thread_local ThreadDtor tl_dtors[MLW_TLS_DTOR_MAX];
    thread_local int tl_dtor_count = 0;
}

extern "C" int __cxa_thread_atexit(void (*fn)(void*), void* obj, void* /*dso*/) {
    if (tl_dtor_count >= MLW_TLS_DTOR_MAX) panic_mem("__cxa_thread_atexit OOM");
    tl_dtors[tl_dtor_count++] = { fn, obj };
    return 0;
}
// Some toolchains reference __cxa_thread_atexit_impl (the libc-provided one).
extern "C" int __cxa_thread_atexit_impl(void (*fn)(void*), void* obj, void* dso) {
    return __cxa_thread_atexit(fn, obj, dso);
}

namespace crt {
void run_thread_local_dtors() {
    for (int i = tl_dtor_count; i-- > 0; )        // LIFO, reverse of construction
        if (tl_dtors[i].fn) tl_dtors[i].fn(tl_dtors[i].obj);
    tl_dtor_count = 0;
}
}

// ════════════════════════════════════════════════════════════════════════
// 5. Misc symbols the compiler may reference
// ════════════════════════════════════════════════════════════════════════
extern "C" void __cxa_pure_virtual() {
    panic_mem("pure virtual call");               // a pure-virtual was invoked
}

// _fltused equivalent isn't needed on Itanium; floating point just works.

// ════════════════════════════════════════════════════════════════════════
// NOTE on thread-local CONSTRUCTION under Itanium:
//   Unlike MSVC (.CRT$XD walk + __dyn_tls_init), Itanium constructs
//   thread-locals lazily on first access via compiler-emitted TLS-init
//   wrappers (__tls_get_addr / per-module __tls_init). You do NOT hand-build
//   an init array here. thread_cache is constructed by mlw__first_crt_ctor,
//   called from your Thread wrapper's entry and from mlwStartCommon for the
//   main thread — same as the MSVC side. So construction is portable; only
//   the OTHER thread-locals (if any) rely on the compiler's lazy path.
// ════════════════════════════════════════════════════════════════════════


void crt::run_thread_local_ctors(){};

using uptr = unsigned long;

// Program-header struct differs between 32- and 64-bit ELF (field order!).
#if defined(MLW_X64) || defined(MLW_ARM64)
struct Elf_Phdr { unsigned type, flags; uptr offset, vaddr, paddr, filesz, memsz, align; };
#else
struct Elf_Phdr { unsigned type, offset, vaddr, paddr, filesz, memsz, flags, align; };
#endif

enum { AT_PHDR = 3, AT_PHNUM = 5, PT_TLS = 7 };

static uptr mlw_align_up(uptr x, uptr a) { return (x + a - 1) & ~(a - 1); }

void* mlw_setup_main_tls(usize& leng, usize pagesize)
{
    // --- common: locate the program's TLS template via the aux vector ------
    auto phdr = (const Elf_Phdr*)__getauxval(AT_PHDR);
    unsigned long n = __getauxval(AT_PHNUM);
    const Elf_Phdr* t = nullptr;
    for (unsigned long i = 0; i < n; i++)
        if (phdr[i].type == PT_TLS) { t = &phdr[i]; break; }
    if (!t) return nullptr;                                   // no thread_local in program

    const uptr WORD = sizeof(void*);                  // 4 on 32-bit, 8 on 64-bit
    const uptr TCB  = 2 * WORD;                        // reserved control block
    uptr align = t->align < TCB ? TCB : t->align;      // honor PT_TLS p_align
    const char* img = (const char*)t->vaddr;

#if defined(MLW_ARM64) || defined(MLW_ARM32)
    // ---- Variant I: TP -> TCB, data above ---------------------------------
    uptr reserve = mlw_align_up(TCB, align);
    leng = mlw_align_up(reserve + t->memsz + align, pagesize);
    char* raw = (char*)mmap(nullptr, leng,
                            PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (raw == (char*)MAP_FAILED) return nullptr;
    char* base = (char*)mlw_align_up((uptr)raw, align);
    char* data = base + reserve;
    for (unsigned long i = 0; i < t->filesz; i++) data[i] = img[i];   // .tbss stays 0
    uptr tp = (uptr)base;

  #if defined(MLW_ARM64)
    asm volatile("msr tpidr_el0, %0" :: "r"(tp) : "memory");
  #else // MLW_ARM32 — set via the ARM-private set_tls syscall (0x0f0005)
    syscall1(0x0f0005, (long)tp);
    // NOTE: compile ARM with -mtp=cp15 (default on ARMv6K+/v7) so thread_local
    // reads TPIDRURO directly; otherwise provide __aeabi_read_tp.
  #endif

#else
    // ---- Variant II: TP -> TCB at top, data below -------------------------
    uptr tsize = mlw_align_up(t->memsz, align);        // size of the data region
    leng = mlw_align_up(tsize + TCB + align, pagesize);
    char* raw = (char*)mmap(nullptr, leng,
                            PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (raw == (char*)MAP_FAILED) return nullptr;
    uptr  tp   = mlw_align_up((uptr)raw + tsize, align);  // TP aligned, tsize below it
    char* data = (char*)(tp - tsize);
    for (unsigned long i = 0; i < t->filesz; i++) data[i] = img[i];
    *(void**)tp = (void*)tp;                            // self-pointer at %fs/%gs:0

  #if defined(MLW_X64)
    syscall(158 /*arch_prctl*/, 0x1002 /*ARCH_SET_FS*/, (long)tp, 0);
  #else // MLW_X86 — set_thread_area, then load the returned selector into %gs
    unsigned desc[4] = { 0xffffffffu, (unsigned)tp, 0x000fffffu, 0x51u };
    // entry=-1(pick), base=tp, limit=4GB pages, flags: 32bit|limit_in_pages|useable
    syscall(243 /*set_thread_area*/, (long)desc);
    unsigned short sel = (unsigned short)((desc[0] << 3) | 3);  // GDT, RPL 3
    asm volatile("movw %0, %%gs" :: "r"(sel));
  #endif
#endif

return raw;
}