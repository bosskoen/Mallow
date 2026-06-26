#pragma once
#include "../thread/rwlock.h"

namespace core
{
    struct Region;

    extern thread_local struct ThreadCash{
        struct SizeClass{
            Region* active = nullptr;
			sync::Atomic<bool> has_remove_free{ false };
			uint32 free_slabes = 0;
			
        };
        SizeClass small_8{};
        SizeClass small_16{};
        SizeClass small_32{};
        SizeClass small_64{};
        SizeClass small_128{};
        SizeClass medium{};

        explicit ThreadCash();
		~ThreadCash();
		ThreadCash(ThreadCash&&) =delete;
		ThreadCash& operator = (ThreadCash&&) =delete;

		ThreadCash(const ThreadCash&) = delete;
		ThreadCash& operator = (const ThreadCash&) = delete;
    } thread_cash;

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
		uint32 used_count;
		sync::Atomic<void*> remote_free; //pointer to the next block that need to be freed
		ThreadCash* owning_cash;
		//maby lock
        static constexpr usize MEDIUM_BLOCK_SIZE = 1 << 22;//4.194.304
        static constexpr usize SMALL_BLOCK_SIZE = 1 << 16;//65.536
    };

    extern class GAlloc {
	public:
	private:
		struct RegionTable {
			struct Entry {
				Region* ptr;
				// > 128 ,8 ,16, 64, 128 bytes
				enum class Type: uint8 { Medium, S8, S16,S32,S64,S128} type;
				uint8 pad[7]{}; //16 bytes total
				MLW_FORCE_INLINE Entry(Region* p, Type t) : ptr(p), type(t) {};
				MLW_FORCE_INLINE Entry() = default;
			}*base = nullptr;
			index_t capacity = 0;
			index_t size = 0;
			RegionTable();
			~RegionTable();

			Entry* find(void* ptr) const;
			void remove(Region*);
			bool insert(Entry&&);
			bool grow();
			//what move/copy semantics

		} region_table{} ;

		sync::RWLock region_list_lock{};

		static constexpr usize MIN_SIZE = 1 << 7; //128
		static constexpr usize MAX_SIZE = 1 << 18; //262.144


		bool allocNewMedium(core::Region* last_region, core::ThreadCash& cash);
		void deallocMedium(core::Region* region);
		bool freeMediumBlock(void* ptr, core::Region*);
		bool alloc_new_small(core::Region* last_region, core::ThreadCash& cash);

		void emptyRemoteList(ThreadCash&);
		friend struct ThreadCash;
	public:

		explicit GAlloc();
		~GAlloc();
		GAlloc(GAlloc&&) =delete;
		GAlloc& operator = (GAlloc&&) =delete; //can this be with a lock in the struct;

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
	}mlw_galloc;
} // namespace core


