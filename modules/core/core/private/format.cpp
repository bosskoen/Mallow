#include "io/format.h"
#include "libc/mem.h"
#include "macro.h"

#if defined(MLW_WINDOWS)
#include <windows.h>
#elif defined(MLW_LINUX) || defined(MLW_MAC)
#include <sys/mman.h>
#else
#error "Unsupported platform"
#endif

namespace core::detail
{
	FormatBufferType &getFormatBuffer()
	{
		thread_local FormatBufferType buf = []()
		{
			FormatBufferType b;

#if defined(MLW_WINDOWS)

			b.ptr = static_cast<char *>(VirtualAlloc(nullptr, mlwAllocGranularity(), MEM_RESERVE, PAGE_READWRITE));

			if (!b.ptr)
				panic_mem("VirtualAlloc(MEM_RESERVE) failed.");

			if (!VirtualAlloc(b.ptr, mlwPageSize(), MEM_COMMIT, PAGE_READWRITE))
			{
				panic_mem("VirtualAlloc(MEM_COMMIT) failed.");
			}

#else

			b.ptr = static_cast<char *>(
				mmap(nullptr, mlwPageSize(), PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));

			if (b.ptr == MAP_FAILED)
				panic_mem("mmap() failed.");

#endif

			b.ptr = MLW_ASSUME_ALIGNED(b.ptr, 4096);
			b.len = 0;
			b.capacity = static_cast<index_t>(mlwPageSize());

			return b;
		}();

		return buf;
	}
}

void core::FormatBufferType::realocate()
{
#if defined(MLW_WINDOWS)

	if ((capacity & mlwGranMask()) == 0)
	{
		char *new_ptr = static_cast<char *>(VirtualAlloc(nullptr, ((capacity >> mlwGranShift()) + 1) * mlwAllocGranularity(), MEM_RESERVE, PAGE_READWRITE));

		if (!new_ptr)
			panic_mem("VirtualAlloc(MEM_RESERVE) failed while growing format buffer.");

		if (!VirtualAlloc(new_ptr, capacity + mlwPageSize(), MEM_COMMIT, PAGE_READWRITE))
		{
			VirtualFree(new_ptr, 0, MEM_RELEASE);
			panic_mem("VirtualAlloc(MEM_COMMIT) failed while growing format buffer.");
		}

		mlwMemcpy(MLW_ASSUME_ALIGNED(new_ptr, 4096), MLW_ASSUME_ALIGNED(ptr, 4096), len);

		if (!VirtualFree(ptr, 0, MEM_RELEASE))
			panic_mem("VirtualFree() failed.");

		ptr = new_ptr;
		capacity += static_cast<index_t>(mlwPageSize());
	}
	else
	{
		if (!VirtualAlloc(ptr + capacity, mlwPageSize(), MEM_COMMIT, PAGE_READWRITE))
		{
			panic_mem("VirtualAlloc(MEM_COMMIT) failed while extending format buffer.");
		}

		capacity += static_cast<index_t>(mlwPageSize());
	}

#else

	void *new_ptr = mremap(ptr, capacity, capacity + mlwPageSize(), MREMAP_MAYMOVE);

	if (new_ptr == MAP_FAILED)
		panic_mem("mremap() failed.");

	ptr = static_cast<char *>(new_ptr);
	ptr = MLW_ASSUME_ALIGNED(ptr, 4096);

	capacity += static_cast<index_t>(mlwPageSize());

#endif
}

void core::FormatBufferType::append(const CStr &str)
{
	while (len + str.len > capacity)
	{
		realocate();
	}
	// copy str to buffer
	mlwMemcpy(ptr + len, str.ptr, str.len);
	len += str.len;
}

void core::FormatBufferType::append(const uint8 value)
{
	while (len + 1 > capacity)
	{
		realocate();
	}
	ptr[len] = value;
	len += 1;
}
