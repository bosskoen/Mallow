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

static core::sync::Atomic<uint32> thread_id_counter{ 0 };
const thread_local uint32 core::thread_id = thread_id_counter.fetchAdd(1, core::sync::MemoryOrder::Relaxed);

namespace core::detail{


	const PlatformInfo& queryPlatform() {
		const static PlatformInfo INFO = []() -> const PlatformInfo {
#if defined(MLW_WINDOWS)
			SYSTEM_INFO si;
			GetSystemInfo(&si);
			return { (usize)si.dwPageSize, (usize)si.dwPageSize - 1, MLW_CTZ((usize)si.dwPageSize), (usize)si.dwAllocationGranularity, (usize)si.dwAllocationGranularity - 1, MLW_CTZ((usize)si.dwAllocationGranularity) };
#elif defined(MLW_LINUX) || defined(MLW_MAC)
			usize page = (usize)sysconf(_SC_PAGESIZE);
			return { page, page - 1, MLW_CTZ(page), page, page - 1, MLW_CTZ(page) };
#else
#error "Unsupported platform"
#endif
			}();

		return INFO;
	}
}