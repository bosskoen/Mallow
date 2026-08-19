#include "memory/anonymous_allocator.h"
#include "memory/galloc.h"

core::AnonymousAllocator glabal_alloc = { .realloc = [](const core::AnonymousAllocator*, void* ptr,
					 isize , isize new_size, isize align)-> void* { if (ptr == nullptr) { return core::mlw_g_alloc.alignAlloc(new_size, align); } return core::mlw_g_alloc.realloc(ptr, new_size); },
					 .ctx = nullptr };

const core::AnonymousAllocator& core::default_allocator()
{
	return glabal_alloc;
}