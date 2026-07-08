#include "libc/mem.h"
#include "libc/process.h"
#include "libc/str.h"

#if defined(MLW_WINDOWS)
#include <windows.h>
#elif defined(MLW_LINUX) || defined(MLW_MAC)
#include <unistd.h>
#else
#error "Unsupported platform"
#endif

#include "libc/math.h"
#include "thread/atomic.h"

#include "crt_internals.h"

static core::sync::Atomic<uint32> thread_id_counter{ 0 };
const thread_local uint32 core::thread_id = thread_id_counter.fetchAdd(1, core::sync::MemoryOrder::Relaxed);

//TODO implement a user at exit not realy needed but nice to have;

MLW_NO_RETURN void core::mlwExit(int32 status)
{
	::ExitProcess(static_cast<UINT>(status));
	MLW_UNREACHABLE();
}

MLW_NO_RETURN void core::mlwTerminate(int32 status)
{
	::TerminateProcess(::GetCurrentProcess(), static_cast<UINT>(status));
	for (;;) MLW_DEBUGBREAK();
}


constinit core::PlatformInfo core::PLATFORM_INFO {};

void core::detail::mlw__crt_init_platforminf()
{
	#if defined(MLW_WINDOWS)
			SYSTEM_INFO si;
			GetSystemInfo(&si);
			PLATFORM_INFO = { (usize)si.dwPageSize, (usize)si.dwPageSize - 1, MLW_CTZ((usize)si.dwPageSize), (usize)si.dwAllocationGranularity, (usize)si.dwAllocationGranularity - 1, MLW_CTZ((usize)si.dwAllocationGranularity) };
#elif defined(MLW_LINUX) || defined(MLW_MAC)
			usize page = (usize)sysconf(_SC_PAGESIZE);
			PLATFORM_INFO = { page, page - 1, MLW_CTZ(page), page, page - 1, MLW_CTZ(page) };
#else
#error "Unsupported platform"
#endif
}