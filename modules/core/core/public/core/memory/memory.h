#pragma once
#include "libc/mem.h"

namespace core
{

    struct StackAllocator
    {
        void *base = nullptr;
        usize capacity = 0;
        usize offset = 0;

        bool init(usize size);
        void shutdown();

        MLW_FORCE_INLINE ~StackAllocator() { shutdown(); };

        StackAllocator(const StackAllocator &) = delete;
        StackAllocator &operator=(const StackAllocator &) = delete;

        StackAllocator(StackAllocator &&other) noexcept
        {
            base = other.base;
            capacity = other.capacity;
            offset = other.offset;

            other.base = nullptr;
            other.capacity = 0;
            other.offset = 0;
        };
        StackAllocator &operator=(StackAllocator &&other) noexcept
        {
            if (this != &other)
            {
                base = other.base;
                capacity = other.capacity;
                offset = other.offset;

                other.base = nullptr;
                other.capacity = 0;
                other.offset = 0;
            }
            return *this;
        };

        void *alloc(usize size, usize alignment);
        void *alloc(usize size); // just calls alloc(size, alignof(max_align_t))

        MLW_FORCE_INLINE usize mark() const { return offset; };                               // save current position
        MLW_FORCE_INLINE void freeTo(usize mark) { offset = offset < mark ? offset : mark; }; // rewind to saved position
        MLW_FORCE_INLINE void freeTo(void *ptr)
        {
            if (ptr >= base)
                offset = 0;
            else if (reinterpret_cast<uint8 *>(ptr) - base < offset)
                offset = reinterpret_cast<uint8 *>(ptr) - base;
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
        void *base = nullptr;
        usize capacity = 0;
        usize block_size = 0;
        void *first_free = nullptr;

        bool init(usize size, usize b_size);
        void shutdown();

        BlockAllocator(const BlockAllocator &) = delete;
        BlockAllocator &operator=(const BlockAllocator &) = delete;

        BlockAllocator(BlockAllocator &&other)
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
        BlockAllocator &operator=(BlockAllocator &&other)
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

        void *alloc(){
            if(first_free == nullptr){
                return nullptr;
            }
            void* ret = first_free;
            first_free = *static_cast<void**>(ret);
            return ret;
        };
        void free(void *ptr){
            *static_cast<void**>(ptr) = first_free;
            first_free = ptr;
        };
    };
}
