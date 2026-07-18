#include "libc/mem.h"

void operator delete(void*, usize) noexcept{
	MLW_DEBUGBREAK();
}
void operator delete(void* p, usize) noexcept { operator delete(p); }