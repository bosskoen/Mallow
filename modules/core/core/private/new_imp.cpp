#include "libc/mem.h"
#include "compilers.h"

void operator delete(void*) noexcept { MLW_DEBUGBREAK(); }

void operator delete(void* p, usize) noexcept{
	operator delete (p);
}