#pragma once
#include "compilers.h"

namespace core
{
	struct StackAllocator
	{
		void* base = nullptr;
		usize capacity = 0;
		usize offset = 0;

		usize committed = 0;


		bool init(usize size);
		void shutdown();

		MLW_FORCE_INLINE ~StackAllocator() { shutdown(); };

		StackAllocator(const StackAllocator&) = delete;
		StackAllocator& operator=(const StackAllocator&) = delete;

		StackAllocator(StackAllocator&& other) noexcept
		{
			base = other.base;
			capacity = other.capacity;
			offset = other.offset;

			committed = other.committed;


			other.base = nullptr;
			other.capacity = 0;
			other.offset = 0;

			other.committed = 0;

		};
		StackAllocator& operator=(StackAllocator&& other) noexcept
		{
			if (this != &other)
			{
				base = other.base;
				capacity = other.capacity;
				offset = other.offset;
				committed = other.committed;


				other.base = nullptr;
				other.capacity = 0;
				other.offset = 0;
				other.committed = 0;

			}
			return *this;
		};

		void* alloc(usize size, usize alignment);
		MLW_FORCE_INLINE void* alloc(usize size)
		{
			usize alignment = size >= sizeof(alignment_t) ? alignof(alignment_t) : size <= 1 ? 1
				: 1ull << (64 - MLW_CLZ(size - 1));
			return alloc(size, alignment);
		}

		MLW_FORCE_INLINE usize mark() const { return offset; };                               // save current position
		MLW_FORCE_INLINE void freeTo(usize mark) { offset = offset < mark ? offset : mark; }; // rewind to saved position
		MLW_FORCE_INLINE void freeTo(void* ptr)
		{
			if (ptr <= base)
				offset = 0;
			else if (static_cast<usize>(reinterpret_cast<uint8*>(ptr) - reinterpret_cast<uint8*>(base)) < offset)
				offset = reinterpret_cast<uint8*>(ptr) - reinterpret_cast<uint8*>(base);
		}; // rewind to pointer (must be previously returned by alloc)
		MLW_FORCE_INLINE void reset() { offset = 0; }; // rewind to 0
	};

	// template <typename T>
	// T *stackAlloc(StackAllocator &a)
	// {
	//     return static_cast<T *>(a.alloc(sizeof(T), alignof(T)));
	// }

	// template <typename T>
	// T *stackAllocArray(StackAllocator &a, usize count)
	// {
	//     return static_cast<T *>(a.alloc(sizeof(T) * count, alignof(T)));
	// }

	// template <typename T>
	// void stackFreeTo(StackAllocator &a, T *ptr)
	// {
	//     a.freeTo(static_cast<void *>(ptr));
	// }

	struct BlockAllocator
	{
		void* base = nullptr;
		usize capacity = 0;
		usize block_size = 0;
		void* first_free = nullptr;

		bool init(usize size, usize b_size);
		void shutdown();

		~BlockAllocator() { shutdown(); };

		BlockAllocator(const BlockAllocator&) = delete;
		BlockAllocator& operator=(const BlockAllocator&) = delete;

		BlockAllocator(BlockAllocator&& other)
		{
			base = other.base;
			capacity = other.capacity;
			block_size = other.block_size;
			first_free = other.first_free;

			other.base = nullptr;
			other.capacity = 0;
			other.block_size = 0;
			other.first_free = nullptr;
		};
		BlockAllocator& operator=(BlockAllocator&& other)
		{
			if (this != &other)
			{
				base = other.base;
				capacity = other.capacity;
				block_size = other.block_size;
				first_free = other.first_free;

				other.base = nullptr;
				other.capacity = 0;
				other.block_size = 0;
				other.first_free = nullptr;
			}
			return *this;
		};

		void* alloc()
		{
			if (first_free == nullptr)
			{
				return nullptr;
			}
			void* ret = first_free;
			first_free = *static_cast<void**>(ret);
			return ret;
		};
		void free(void* ptr)
		{
			*static_cast<void**>(ptr) = first_free;
			first_free = ptr;
		};
	};

} // namespace core