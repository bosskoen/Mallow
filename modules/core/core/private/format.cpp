#include "io/format.h"
#include "libc/mem.h"
#include "macro.h"

#if defined(MLW_WINDOWS)
#include <windows.h>
#elif defined(MLW_LINUX) || defined(MLW_MAC)
#include "syscall.h"
#else
#error "Unsupported platform"
#endif


#include "crt_internals.h"
namespace core::detail
{



	alignas(FormatBufferType) static thread_local uint8 fb_storage[sizeof(FormatBufferType)]{ 0 };
	static thread_local bool fb_constructed = false;

	void mlw__crt_distroy_format_buffer()
	{
		if (fb_constructed) {
			FormatBufferType& buf = *reinterpret_cast<FormatBufferType*>(fb_storage);
			char* p = buf.ptr;
			if (p) {
#if defined(MLW_WINDOWS)
				::VirtualFree(p, 0, MEM_RELEASE);
#elif defined(MLW_LINUX) || defined(MLW_MAC)
				::munmap(p, buf.capacity);
#endif
			}
			buf.ptr = nullptr;
			buf.len = 0;
			buf.capacity = 0;
			fb_constructed = false;
		} 
	}

	FormatBufferType &getFormatBuffer()
	{
		//io::writeHandle(core::terminal::stdoutHandle(), core::CStr(reinterpret_cast<char*>(&fb_constructed), 1));
		if (!fb_constructed) {
			FormatBufferType& b = *reinterpret_cast<FormatBufferType*>(fb_storage);

#if defined(MLW_WINDOWS)

			b.ptr = static_cast<char*>(VirtualAlloc(nullptr, PLATFORM_INFO.alloc_granularity, MEM_RESERVE, PAGE_READWRITE));

			if (!b.ptr)
				panic_mem("VirtualAlloc(MEM_RESERVE) failed.");

			if (!VirtualAlloc(b.ptr, PLATFORM_INFO.page_size, MEM_COMMIT, PAGE_READWRITE))
			{
				panic_mem("VirtualAlloc(MEM_COMMIT) failed.");
			}

#else

			b.ptr = static_cast<char*>(
				mmap(nullptr, PLATFORM_INFO.page_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));

			if (b.ptr == MAP_FAILED)
				panic_mem("mmap() failed.");

#endif

			b.ptr = MLW_ASSUME_ALIGNED(b.ptr, 4096);
			b.len = 0;
			b.capacity = static_cast<index_t>(PLATFORM_INFO.page_size);
			fb_constructed = true;
		}
		return *reinterpret_cast<FormatBufferType*>(fb_storage);
	}
}

void core::FormatBufferType::realocate()
{
#if defined(MLW_WINDOWS)

	if ((capacity & PLATFORM_INFO.gran_mask) == 0)
	{
		char *new_ptr = static_cast<char *>(VirtualAlloc(nullptr, ((capacity >> PLATFORM_INFO.gran_shift) + 1) * PLATFORM_INFO.alloc_granularity, MEM_RESERVE, PAGE_READWRITE));

		if (!new_ptr)
			panic_mem("VirtualAlloc(MEM_RESERVE) failed while growing format buffer.");

		if (!VirtualAlloc(new_ptr, capacity + PLATFORM_INFO.page_size, MEM_COMMIT, PAGE_READWRITE))
		{
			VirtualFree(new_ptr, 0, MEM_RELEASE);
			panic_mem("VirtualAlloc(MEM_COMMIT) failed while growing format buffer.");
		}

		mlwMemcpy(MLW_ASSUME_ALIGNED(new_ptr, 4096), MLW_ASSUME_ALIGNED(ptr, 4096), len);

		if (!VirtualFree(ptr, 0, MEM_RELEASE))
			panic_mem("VirtualFree() failed.");

		ptr = new_ptr;
		capacity += static_cast<index_t>(PLATFORM_INFO.page_size);
	}
	else
	{
		if (!VirtualAlloc(ptr + capacity, PLATFORM_INFO.page_size, MEM_COMMIT, PAGE_READWRITE))
		{
			panic_mem("VirtualAlloc(MEM_COMMIT) failed while extending format buffer.");
		}

		capacity += static_cast<index_t>(PLATFORM_INFO.page_size);
	}

#else

	void *new_ptr = mremap(ptr, capacity, capacity + PLATFORM_INFO.page_size, MREMAP_MAYMOVE);

	if (new_ptr == MAP_FAILED)
		panic_mem("mremap() failed.");

	ptr = static_cast<char *>(new_ptr);
	ptr = MLW_ASSUME_ALIGNED(ptr, 4096);

	capacity += static_cast<index_t>(PLATFORM_INFO.page_size);

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
