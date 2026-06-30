#pragma once
#include "../thread/rwlock.h"

namespace core
{
	struct Region;
	struct RegionTable {
		struct Entry {
			Region* ptr;
			// > 128 ,8 ,16, 64, 128 bytes
			enum class Type : uint8 { Medium, S8 = 8, S16 = 16, S32 = 32, S64 = 64, S128 = 128 } type;
			uint8 pad[7]{}; //16 bytes total
			MLW_FORCE_INLINE Entry(Region* p, Type t) : ptr(p), type(t) {};
			MLW_FORCE_INLINE Entry() = default;
		}*base = nullptr;
		index_t capacity = 0;
		index_t size = 0;
		RegionTable();
		~RegionTable();


		RegionTable(const RegionTable&) = delete;
		RegionTable& operator=(const RegionTable&) = delete;

		RegionTable(RegionTable&& o) noexcept
			: base(o.base), capacity(o.capacity), size(o.size)
		{
			o.base = nullptr;
			o.capacity = 0;
			o.size = 0;
		}
		RegionTable& operator=(RegionTable&& o) noexcept
		{
			if (this != &o) {
				// free our current allocation before taking o's
				distroy();
				base = o.base;
				capacity = o.capacity;
				size = o.size;
				o.base = nullptr;
				o.capacity = 0;
				o.size = 0;
			}
			return *this;
		}

		void distroy();
		Entry* find(void* ptr) const;
		void remove(Region*);
		bool insert(Entry&&);
		bool grow();

	};

	struct OSTable {
		struct Entry {
			void* ptr;
			usize size;
			MLW_FORCE_INLINE Entry(void* p, usize t) : ptr(p), size(t) {};
			MLW_FORCE_INLINE Entry() = default;
		}*base = nullptr;
		index_t capacity = 0;
		index_t size = 0;
		OSTable();
		~OSTable();


		OSTable(const OSTable&) = delete;
		OSTable& operator=(const OSTable&) = delete;

		OSTable(OSTable&& o) noexcept
			: base(o.base), capacity(o.capacity), size(o.size)
		{
			o.base = nullptr;
			o.capacity = 0;
			o.size = 0;
		}
		OSTable& operator=(OSTable&& o) noexcept
		{
			if (this != &o) {
				// free our current allocation before taking o's
				distroy();
				base = o.base;
				capacity = o.capacity;
				size = o.size;
				o.base = nullptr;
				o.capacity = 0;
				o.size = 0;
			}
			return *this;
		}

		void distroy();
		Optional<Entry> findAndRemove(void* ptr);
		//void remove(void*);
		bool insert(Entry&&);
		bool grow();

	};

	extern thread_local struct ThreadCache {
		struct SizeClass {
			Region* active = nullptr;
			sync::Atomic<bool> has_remove_free{ false };
			uint32 free_slabs = 0;

		};
		SizeClass small_8{};
		SizeClass small_16{};
		SizeClass small_32{};
		SizeClass small_64{};
		SizeClass small_128{};
		SizeClass medium{};
		explicit ThreadCache() = default;
		~ThreadCache(); //TODO deconstruction order/responsebilety
		ThreadCache(ThreadCache&&) = delete;
		ThreadCache& operator = (ThreadCache&&) = delete;

		MLW_FORCE_INLINE SizeClass& getSizeClass(RegionTable::Entry::Type size) {
			switch (size)
			{
			case core::RegionTable::Entry::Type::S8:
				return small_8;
				break;
			case core::RegionTable::Entry::Type::S16:
				return small_16;
				break;
			case core::RegionTable::Entry::Type::S32:
				return small_32;
				break;
			case core::RegionTable::Entry::Type::S64:
				return small_64;
				break;
			case core::RegionTable::Entry::Type::S128:
				return small_128;
				break;
			case core::RegionTable::Entry::Type::Medium:
				return medium;
				break;
			}
			return medium;
		}

		ThreadCache(const ThreadCache&) = delete;
		ThreadCache& operator = (const ThreadCache&) = delete;
	} thread_cache;

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
		sync::Atomic<void*> remote_free; //pointer to the next block that need to be freed
		ThreadCache* owning_cache;
        sync::RWLock migration_lock{};
		uint32 used_count;
        uint32 padding;
        static constexpr usize MEDIUM_BLOCK_SIZE = 1 << 22;//4.194.304
        static constexpr usize SMALL_BLOCK_SIZE = 1 << 16;//65.536
    };

    extern class GAlloc {
	public:
	private:
		RegionTable region_table{} ;

		sync::RWLock region_list_lock{};

		static constexpr usize MIN_SIZE = 1 << 7; //128
		static constexpr usize MAX_SIZE = 1 << 18; //262.144


		bool allocMediumRegion(core::Region* last_region, core::ThreadCache& cache);
		void freeMediumRegion(core::Region* region);
		bool freeMedium(void* ptr, core::Region*);
        void freeSmall(void* ptr, core::Region*, RegionTable::Entry::Type size);
        void freeSmallRegion(core::Region*, RegionTable::Entry::Type size);
		bool allocSmallRegion(core::Region* last_region, core::ThreadCache& cache, RegionTable::Entry::Type);
		void* osAlloc(usize size);
		void osFree(void* ptr);

		void drainRemoteList(ThreadCache&);
		friend struct ThreadCache;

        ThreadCache orphan_pool{};
        sync::spin_lock::MCS orphan_lock{};
		sync::Atomic<bool> orphan_is_draining{ false };

		OSTable os_table{};
		sync::spin_lock::MCS os_lock{};

	public:

		explicit GAlloc() = default;
		~GAlloc() = default;
		GAlloc(GAlloc&&) =delete;
		GAlloc& operator = (GAlloc&&) =delete;

		GAlloc(const GAlloc&) = delete;
		GAlloc& operator = (const GAlloc&) = delete;

		void* alignAlloc(usize size, usize align);
		void* alloc(usize size); 
		void free(void* ptr);

		void* realloc(void* ptr, usize new_size);
	}mlw_g_alloc;

  
} // namespace core


