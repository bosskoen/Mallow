#include "libc/mem.h"        // declares the mlw* alloc wrappers + PlatformInfo defined below
#include "libc/process.h"    // declares thread_id, mlwExit, mlwTerminate defined below

#if defined(MLW_WINDOWS)
#include <windows.h>         // ExitProcess / TerminateProcess
#elif defined(MLW_LINUX) || defined(MLW_MAC)
#include "posix/syscall_api.h" // syscall(), SYS_EXIT_GROUP
#else
#error "Unsupported platform"
#endif


#include "thread/atomic.h"      // Atomic<uint32> for the thread-id counter

#include "crt_internals.h"      // crt::run_*_dtors CRT teardown hooks
#include "proc_context.h"       // PROC_CONTEXT / SysInfo for platform-info init

#include "memory/galloc.h"      // mlw_g_alloc — backing for the alloc wrappers

// Monotonic thread-id source. Each thread's `thread_id` is initialized on first
// access (thread_local init) by atomically incrementing this counter. Relaxed is
// sufficient: ids only need to be unique, not ordered relative to other memory.
static core::sync::Atomic<uint32> thread_id_counter{ 0 };
const thread_local uint32 core::thread_id = thread_id_counter.fetchAdd(1, core::sync::MemoryOrder::Relaxed);

//TODO implement a user at exit not realy needed but nice to have;

// Graceful process exit: run CRT teardown, then hand off to the OS.
MLW_NO_RETURN void core::mlwExit(int32 status)
{
    // Teardown is gated on !MLW_ABI_MSVC because under the MSVC ABI the loader
    // runs global/thread-local destructors itself via DLL_PROCESS_DETACH — doing
    // it here too would run them twice. Under any other ABI (and on Linux) there
    // is no such callback, so we must run it explicitly.
    // Order matters: user/global destructors first (they may still allocate),
    // then the per-thread cache and format buffer they depend on.
#if !defined(MLW_ABI_MSVC)
    crt::run_thread_local_dtors();
    crt::run_global_dtors();
    core::ThreadCache::mlw__crt_distroy_tc_storage();
    core::detail::mlw__crt_distroy_format_buffer();
#endif

#if defined(MLW_WINDOWS)
    ::ExitProcess(static_cast<UINT>(status));   // MSVC-ABI teardown happens in the loader's DLL_PROCESS_DETACH
#else
    // Linux: no loader callback — teardown above already ran, now exit the group.
    syscall(SYS_EXIT_GROUP, status);
#endif
    MLW_UNREACHABLE();
}

// Immediate process kill: no destructors, no cache teardown. The trailing
// DEBUGBREAK loop is unreachable belt-and-suspenders in case the OS call
// unexpectedly returns (it should not).
MLW_NO_RETURN void core::mlwTerminate(int32 status)
{
#if defined(MLW_WINDOWS)
    ::TerminateProcess(::GetCurrentProcess(), (UINT)status);
    for (;;) MLW_DEBUGBREAK();
#else
    syscall(SYS_EXIT_GROUP, status);   // immediate, no teardown
    for (;;) MLW_DEBUGBREAK();
#endif
}


// Zero-initialized at load; the real values are written by the CRT init hook
// below, which must run before any allocator use (galloc reads page_size etc.).
constinit core::PlatformInfo core::PLATFORM_INFO {};

// CRT bring-up hook: populate PLATFORM_INFO from the OS. Called once during
// startup, before the allocator is used. (Name typo: `platforminf`.)
void core::detail::mlw__crt_init_platforminf()
{
    core::SysInfo si = core::PROC_CONTEXT.getSysInfo();

	PLATFORM_INFO = { .page_size = si.page_size,
        .page_mask   = si.page_size - 1,
        .page_shift= MLW_CTZ(si.page_size),          // shift = log2(page_size)
        .alloc_granularity  = si.alloc_gran,
        .gran_mask= si.alloc_gran - 1,
        .gran_shift= MLW_CTZ(si.alloc_gran) };
}


// Global-allocator wrappers, defined out-of-line here rather than inline in
// mem.h. That is deliberate: keeping the bodies out of the header means mem.h
// does not need to include galloc.h, which is what breaks the
// macro/format/c_string -> mem -> galloc include cycle. The one-call overhead is
// negligible on an allocation path.
void *core::mlwAlignedAlloc(usize size, usize alignment)
{
    return mlw_g_alloc.alignAlloc(size, alignment);
}

void *core::mlwMalloc(usize size)
{
    return mlw_g_alloc.alloc(size);
}

void core::mlwFree(void *ptr)
{
    mlw_g_alloc.free(ptr);
}

void *core::mlwRealloc(void *ptr, usize newSize)
{
    return mlw_g_alloc.realloc(ptr, newSize);
}