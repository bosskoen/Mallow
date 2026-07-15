#include "libc/mem.h"
#include "libc/process.h"
#include "libc/str.h"

#if defined(MLW_WINDOWS)
#include <windows.h>
#elif defined(MLW_LINUX) || defined(MLW_MAC)
#include "posix/syscall_api.h"
#else
#error "Unsupported platform"
#endif

#include "libc/math.h"
#include "thread/atomic.h"

#include "crt_internals.h"
#include "memory/galloc.h"

static core::sync::Atomic<uint32> thread_id_counter{ 0 };
const thread_local uint32 core::thread_id = thread_id_counter.fetchAdd(1, core::sync::MemoryOrder::Relaxed);

//TODO implement a user at exit not realy needed but nice to have;

MLW_NO_RETURN void core::mlwExit(int32 status)
{
#if !defined(MLW_ABI_MSVC)
    crt::run_thread_local_dtors();
    crt::run_global_dtors();
    core::ThreadCache::mlw__crt_distroy_tc_storage();
    core::detail::mlw__crt_distroy_format_buffer();
#endif

#if defined(MLW_WINDOWS)
    ::ExitProcess(static_cast<UINT>(status));   // loader fires DLL_PROCESS_DETACH → teardown block runs there
#else
    // Linux: no callback — run teardown explicitly, THEN exit

    syscall(SYS_EXIT_GROUP, status);
#endif
    MLW_UNREACHABLE();
}

MLW_NO_RETURN void core::mlwTerminate(int32 status)
{
#if defined(MLW_WINDOWS)
    ::TerminateProcess(::GetCurrentProcess(), (UINT)status);
    for (;;) MLW_DEBUGBREAK();
#else
    syscall(SYS_EXIT_GROUP, status);   // or SYS_exit_group — immediate, no teardown
    for (;;) MLW_DEBUGBREAK();
#endif
}



constinit core::PlatformInfo core::PLATFORM_INFO {};

void core::detail::mlw__crt_init_platforminf()
{
	#if defined(MLW_WINDOWS)
			SYSTEM_INFO si;
			GetSystemInfo(&si);
			PLATFORM_INFO = { (usize)si.dwPageSize, (usize)si.dwPageSize - 1, MLW_CTZ((usize)si.dwPageSize), (usize)si.dwAllocationGranularity, (usize)si.dwAllocationGranularity - 1, MLW_CTZ((usize)si.dwAllocationGranularity) };
#elif defined(MLW_LINUX) || defined(MLW_MAC)
			usize page = (usize)mlw_pagesize;
			PLATFORM_INFO = { page, page - 1, MLW_CTZ(page), page, page - 1, MLW_CTZ(page) };
#else
#error "Unsupported platform"
#endif
}