#include "thread/thread.h"

#include "crt_internals.h"
#include "memory/galloc.h"
#include "macro.h"

#ifdef MLW_WINDOWS
#include <windows.h>

DWORD WINAPI threadEntry(LPVOID s) {
#ifndef MLW_ABI_MSVC
	core::ThreadCache::mlw__first_crt_ctor();
	crt::run_thread_local_ctors(); // no real definition just symitry
#endif
	core::detail::ThreadStart* start = static_cast<core::detail::ThreadStart*>(s);
	start->fn(start->args);

	core::mlwFree(start);


#ifndef MLW_ABI_MSVC
	crt::run_thread_local_dtors();
	core::ThreadCache::mlw__crt_distroy_tc_storage();
	core::detail::mlw__crt_distroy_format_buffer();
#endif
	return 0;
}

void core::detail::sys_join(io::Handle& handle) {
	WaitForSingleObject(handle.fd, INFINITE);
	CloseHandle(handle.fd);
}
uint32 core::detail::sys_spawn(io::Handle& h, ThreadStart* s) {
	h.fd = CreateThread(nullptr, 0, &threadEntry, s, 0, nullptr);
	return h.fd ? 0u : (uint32)GetLastError();     // capture immediately
}


#elif defined(MLW_LINUX)

#else

#endif