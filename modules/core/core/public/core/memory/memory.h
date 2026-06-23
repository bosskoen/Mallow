#pragma once
#include "../libc/mem.h"
#include "../thread/spinlock.h"

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
			else if (reinterpret_cast<uint8*>(ptr) - reinterpret_cast<uint8*>(base) < offset)
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

	class GAlloc {

	public:
		struct FreePointer;
		struct Header {
			uint64 pre_off : 24;
			uint64 next_off : 24;
			uint64 align : 16; // what the block was aligned to, 0 if empty
			MLW_FORCE_INLINE FreePointer* getFreePtr() { return reinterpret_cast<FreePointer*>(this + 1); };
		};
		struct FreePointer {
			Header* next_free_block = nullptr;
			Header* prev_free_block = nullptr;
		};
		struct Region {
			Region* previous = nullptr;
			Region* next = nullptr;
			Header* free_block = nullptr;
			sync::spin_lock::MCS lock{};
			static constexpr usize BLOCK_SIZE = 1 << 22;//4.194.304
		};
	private:
		Region* first_region = nullptr;
		sync::spin_lock::MCS region_list_lock{};

		static constexpr usize MIN_SIZE = 1 << 7; //128
		static constexpr usize MAX_SIZE = 1 << 18; //262.144

		bool alloc_new();
	public:

		explicit GAlloc();
		~GAlloc();
		GAlloc(GAlloc&&);
		GAlloc& operator = (GAlloc&&); //can this be with a lock in the struct;

		GAlloc(const GAlloc&) = delete;
		GAlloc& operator = (const GAlloc&) = delete;

		void* allign_alloc(usize size, usize align); /*
		{
		switch on block size
			to big to os
			to small find block alloc lock and call free
			just write:
			get first region and in that regeion get first free block
			if that blocks next block pointer 0 && the end of the region is far enoth || next block is > size
				lock region
				split block if nesesary but not if the left over is smaller than a constent
				return pointer
			else
			goto just write or if the next region is nullptr alloc a new region
		}*/
		void* alloc(usize size); /*
		{
			return allign_alloc(size, some allignment)
		}*/
		void free(void* ptr);/*
		{
			switch were does the pointer live
			if small lock call free
			if big free
			is medeum
				find region lock it free block if it can be combide combid with other blocks else make now block and link corectly in list
				if foll region is empty maby dealoc region
				check for first block if needs to be moved back
		}
		*/

		void* realloc(void* ptr, usize new_size);/*
		{
		 extent or realoc
		}*/
	};
}
