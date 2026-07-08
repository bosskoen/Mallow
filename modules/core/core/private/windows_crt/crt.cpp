#include <windows.h>
#include "macro.h"
#include "memory/galloc.h"

namespace core::detail
{
    void mlw__crt_distorty_format_buffer();
}

extern "C" {

// ── 1. .tls section bounds (from tlssup.c) ──────────────────────────────
// The compiler places every thread_local between these two symbols.
// _tls_used references them so the loader knows the TLS template size.
#pragma data_seg(".tls")
    char _tls_start = 0;
#pragma data_seg(".tls$ZZZ")
    char _tls_end = 0;
#pragma data_seg()

// ── 2. _tls_index — the module's TLS slot (compiler references it) ───────
ULONG _tls_index = 0;

// ── 3. dynamic-init callback array anchors (.CRT$XD[A..Z]) ──────────────
// Compiler injects each thread_local's initializer into .CRT$XDC/XDL/XDU.
using PVFV = void (__cdecl*)(void);
#pragma section(".CRT$XDA", long, read)
#pragma section(".CRT$XDZ", long, read)
__declspec(allocate(".CRT$XDA")) PVFV __xd_a = nullptr;
__declspec(allocate(".CRT$XDZ")) PVFV __xd_z = nullptr;

// ── 4. TLS start-callback array anchors (.CRT$XL[A..Z]) ─────────────────
#pragma section(".CRT$XLA", long, read)
#pragma section(".CRT$XLZ", long, read)
__declspec(allocate(".CRT$XLA")) PIMAGE_TLS_CALLBACK __xl_a = nullptr;
__declspec(allocate(".CRT$XLZ")) PIMAGE_TLS_CALLBACK __xl_z = nullptr;


#pragma section(".CRT$XCA", long, read)
#pragma section(".CRT$XCZ", long, read)
__declspec(allocate(".CRT$XCA")) PVFV __xc_a[] = { nullptr };
__declspec(allocate(".CRT$XCZ")) PVFV __xc_z[] = { nullptr };

void run_cpp_ctors() {
    for (PVFV* p = __xc_a; p != __xc_z; ++p)
        if (*p) (*p)();
}

// on-demand guard the compiler references at each thread_local access
[[msvc::no_tls_guard]] __declspec(thread) bool __tls_guard = false;

// ── 5. run all dynamic initializers for this thread ─────────────────────
void WINAPI __dyn_tls_init(PVOID, DWORD reason, LPVOID) {
    if (reason != DLL_THREAD_ATTACH || __tls_guard == true) return;

    core::ThreadCache::mlw__first_crt_ctor();

    __tls_guard = true;

    for (PVFV* p = &__xd_a + 1; p != &__xd_z; ++p)
        if (*p) (*p)();
}

void WINAPI __dyn_tls_on_demand_init() noexcept{
    __dyn_tls_init(nullptr, DLL_THREAD_ATTACH, nullptr);

}

// ── 6. thread-exit destructor registration (from tlsdtor.cpp) ───────────
static thread_local PVFV g_tls_dtors[32];
static thread_local int  g_tls_dtor_count = 0;
void __cdecl __tlregdtor(PVFV f) {
    if (g_tls_dtor_count < 32) g_tls_dtors[g_tls_dtor_count++] = f;
    else panic_mem("__tlregdtor OOM");
}
static void run_tls_dtors() {
    for (int i = g_tls_dtor_count; i-- > 0; )
        if (g_tls_dtors[i]) g_tls_dtors[i]();
    g_tls_dtor_count = 0;
}

static constexpr uint32 MLW_ATEXIT_MAX = 64;
static core::sync::Atomic<uint32> g_atexit_count{ 0 };
static void(__cdecl* g_atexit_fns[MLW_ATEXIT_MAX])(void) = {};

extern "C" int32 __cdecl atexit(void(__cdecl* fn)(void)) {
    uint32 i = g_atexit_count.fetchAdd(1, core::sync::MemoryOrder::AcqRel);
    if (i >= MLW_ATEXIT_MAX) {
        g_atexit_count.fetchSub(1, core::sync::MemoryOrder::Relaxed);
        panic_mem("atexit table full");
    }
    g_atexit_fns[i] = fn;
    return 0;
}

void runAtExit() {
    uint32 n = g_atexit_count.load(core::sync::MemoryOrder::Acquire);
    if (n > MLW_ATEXIT_MAX) n = MLW_ATEXIT_MAX;
    for (uint32 i = n; i-- > 0; )       // LIFO
        if (g_atexit_fns[i]) g_atexit_fns[i]();
}

// ── 7. the actual callback the loader invokes ───────────────────────────
static void WINAPI mlw_tls_callback(PVOID h, DWORD reason, LPVOID r) {
    if (reason == DLL_THREAD_ATTACH)
        __dyn_tls_init(h, DLL_THREAD_ATTACH, r);
    else if (reason == DLL_THREAD_DETACH) {
        run_tls_dtors();
        core::ThreadCache::mlw__crt_distroy_tc_storage();
        core::detail::mlw__crt_distorty_format_buffer();
    }
    else if (reason == DLL_PROCESS_DETACH) {
        run_tls_dtors();
        runAtExit();
        core::ThreadCache::mlw__crt_distroy_tc_storage();
        //run memory check maby
        core::detail::mlw__crt_distorty_format_buffer();
    }
}
#pragma section(".CRT$XLC", long, read)
__declspec(allocate(".CRT$XLC")) PIMAGE_TLS_CALLBACK __xl_c = mlw_tls_callback;

// ── 8. the TLS directory the PE loader reads (x64 layout) ───────────────
#pragma const_seg(".rdata$T")
extern const IMAGE_TLS_DIRECTORY64 _tls_used = {
    (ULONGLONG)&_tls_start,      // StartAddressOfRawData
    (ULONGLONG)&_tls_end,        // EndAddressOfRawData
    (ULONGLONG)&_tls_index,      // AddressOfIndex
    (ULONGLONG)(&__xl_a + 1),    // AddressOfCallBacks — array starts AFTER __xl_a
    (ULONG)0,                    // SizeOfZeroFill
    (ULONG)0                     // Characteristics
};
#pragma const_seg()

} // extern "C"