#include "libc/mem.h"

void operator delete(void*, usize) noexcept{
	MLW_DEBUGBREAK();
}