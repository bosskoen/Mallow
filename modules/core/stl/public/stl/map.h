#pragma once

#include <core/compilers.h>
#include <core/traits.h>
#include <core/memory/anonymous_allocator.h>
#include <core/libc/mem.h>
#include <core/macro.h>
#include <core/optional.h>
#include <core/hash.h>

/// \file
/// \brief Open-addressing SwissTable hash map (Abseil flat_hash_map / hashbrown
///        design) whose control-byte metadata array is scanned a whole *group*
///        at a time with SIMD (or a SWAR emulation of it).
///
/// \par How it works
///   Alongside the slot array sits a parallel array of one control byte per slot:
///     - EMPTY    (0x80)  slot never used
///     - DELETED  (0xFE)  tombstone (was used, since erased)
///     - SENTINEL (0xFF)  one fixed byte at index `capacity`; see below
///     - FULL     (0..0x7F) slot in use, byte stores H2 (low 7 bits of the hash)
///   A lookup loads `Group::Width` control bytes at once, compares them against
///   the wanted H2 in parallel, and only touches the slots whose control byte
///   matched. A group with any EMPTY byte ends the probe chain.
///
/// \par Hash split
///   The 64-bit key hash is split into H1 (upper bits, picks the home group via
///   `H1 & capacity`) and H2 (low 7 bits, stored in the control byte as the
///   in-group filter). See \ref swiss::H1 / \ref swiss::H2.
///
/// \par Capacity convention
///   `capacity` is the probe bitmask, always `2^k - 1` (all ones), so
///   `pos & capacity` is the wrap-around modulo. There are exactly `capacity`
///   real slots (indices `0..capacity-1`); index `capacity` holds the SENTINEL
///   and has no backing slot. The control array is longer than the slot array —
///   see the layout note below.
///
/// \par Control-array layout (why it is longer than the slots)
///   Physical control bytes: `[ 0 .. capacity-1 ][ capacity ][ capacity+1 .. capacity+Width-1 ]`
///                             \___ real slots ___/ \_SENTINEL_/ \______ cloned tail ______/
///   The cloned tail mirrors control bytes `0 .. Width-2` so that a group load
///   starting near the end reads `Width` contiguous bytes that already contain
///   the wrapped-around front of the table — no branch on the wrap. Total control
///   bytes = `capacity + Width` (see \ref Map::ctrlCount). The SENTINEL is a byte
///   that is deliberately neither FULL, EMPTY, nor DELETED, so a group scan slides
///   over it (never a match, never ends a chain, never a valid insert target),
///   which both keeps a wrapping scan correct and guarantees index `capacity` is
///   never chosen for a real slot.
///
/// \par Backend selection (one is chosen at compile time)
///   - x86/x64 + GCC/Clang : SSE2 via vector extensions        (16-wide)
///   - x86/x64 + MSVC      : SSE2 via self-declared intrinsics  (16-wide)
///   - arm64  + GCC/Clang  : NEON via vector extensions         (8-wide)
///   - everything else     : portable SWAR                      (8-wide)
///   Define \c MLW_HASHMAP_PORTABLE to force SWAR everywhere, or
///   \c MLW_LINIAR_MAP_PROBE to fall back to the scalar byte-at-a-time map
///   (see the `#else` at the bottom of the file).
///
/// \par Probing
///   Linear group probing: the home group is at `H1 & capacity`, and on a miss
///   the offset advances by `Group::Width` (`off = (off + Width) & capacity`).
///   This visits slots in the same order as a scalar `+1` probe, just a group at
///   a time, which is why the simple erase heuristic below stays valid.
///
/// \note Little-endian is assumed for the group load (true on x86/x64/arm).
/// \note Not reference-stable: any \ref Map::put that grows relocates every entry,
///       invalidating references returned by \ref Map::get / \ref Map::put and any
///       stored `Entry*`. Copy the value out if you need it across an insert.
/// \note Not copyable (use \ref Map::clone); movable.

// ---------------------------------------------------------------------------
//  Backend selection
// ---------------------------------------------------------------------------
#if defined(MLW_HASHMAP_PORTABLE)
#define MLW_HASHMAP_SWAR 1
#define MLW_SIMD_MAP_EXELERATION
#elif defined(MLW_LINIAR_MAP_PROBE)
// nothing here: fall back to the scalar byte-scan map (the #else branch)
#elif (defined(MLW_X86) || defined(MLW_X64)) && (defined(MLW_GCC) || defined(MLW_CLANG))
#define MLW_HASHMAP_SSE2_GNU 1
#define MLW_SIMD_MAP_EXELERATION
#elif (defined(MLW_X86) || defined(MLW_X64)) && defined(MLW_MSVC)
#define MLW_HASHMAP_SSE2_MSVC 1
#define MLW_SIMD_MAP_EXELERATION
#elif defined(MLW_ARM64) && (defined(MLW_GCC) || defined(MLW_CLANG))
#define MLW_HASHMAP_NEON_GNU 1
#define MLW_SIMD_MAP_EXELERATION
#endif

// #undef MLW_SIMD_MAP_EXELERATION

namespace core
{

	namespace swiss
	{
		using ctrl_t = int8;
		// Signedness is load-bearing: EMPTY/DELETED/SENTINEL all have the top
		// (sign) bit set (negative as int8), FULL bytes store H2 in 0..0x7F
		// (non-negative). `isFull` is therefore just `c >= 0`, and the SIMD
		// maskEmptyOrDeleted uses a single signed compare against SENTINEL.
		static constexpr ctrl_t EMPTY = static_cast<ctrl_t>(0x80);	  // 1000`0000
		static constexpr ctrl_t DELETED = static_cast<ctrl_t>(0xFE);  // 1111`1110
		static constexpr ctrl_t SENTINEL = static_cast<ctrl_t>(0xFF); // 1111`1111
		// FULL = 0xxx`xxxx

		/// \brief Upper hash bits: selects the home group (`H1 & capacity`).
		MLW_FORCE_INLINE usize H1(usize hash) { return hash >> 7; }
		/// \brief Low 7 bits: the in-group filter byte stored in a FULL control byte.
		MLW_FORCE_INLINE ctrl_t H2(usize hash) { return static_cast<ctrl_t>(hash & 0x7F); }

		MLW_FORCE_INLINE bool isFull(ctrl_t c) { return c >= 0; } // top bit clear
		MLW_FORCE_INLINE bool isEmpty(ctrl_t c) { return c == EMPTY; }
		MLW_FORCE_INLINE bool isDeleted(ctrl_t c) { return c == DELETED; }

		template <typename T>
		MLW_FORCE_INLINE usize hashKey(const T &key)
		{
			return static_cast<usize>(Hash<T>{}(key));
		}

		// ---- match bitmask --------------------------------------------------
		/// \brief Result of a group scan: a set of matching lanes.
		/// \tparam Width Slots per group (16 SSE / 8 SWAR/NEON).
		/// \tparam Shift 0 when the backend produces 1 bit per slot (SSE
		///         movemask), 3 when it produces 1 set bit per *byte* lane (the
		///         MSB of each byte, as SWAR/NEON do) so lane = bitpos >> 3.
		template <usize Width, usize Shift>
		struct BitMask
		{
			uint64 mask;
			explicit BitMask(uint64 m) : mask(m) {}

			bool any() const { return mask != 0; }
			/// Consume the lowest set lane (use in a for-loop to iterate matches).
			void clearLowest() { mask &= (mask - 1); }
			/// Index of the lowest set lane.
			usize lowest() const { return static_cast<usize>(MLW_CTZ(mask)) >> Shift; }
			usize trailingZeros() const { return static_cast<usize>(MLW_CTZ(mask)) >> Shift; }
			usize leadingZeros() const
			{
				constexpr usize extra = 64 - (Width << Shift);
				return static_cast<usize>(MLW_CLZ(mask << extra)) >> Shift;
			}
		};

		// ---- group backends -------------------------------------------------
		// Each backend exposes the same interface: a ctor that loads Width control
		// bytes from a (possibly unaligned) pointer, and three scans returning a
		// BitMask -- match(h2), maskEmpty(), maskEmptyOrDeleted(). Only this struct
		// differs per platform; the map logic below is backend-agnostic.
#if defined(MLW_HASHMAP_SSE2_GNU)
		struct Group
		{
			static constexpr usize Width = 16;
			using Mask = BitMask<16, 0>; // pmovmskb gives 1 bit per lane

			typedef char v16 __attribute__((__vector_size__(16)));
			typedef signed char s16 __attribute__((__vector_size__(16)));
			typedef char v16u __attribute__((__vector_size__(16), __may_alias__, __aligned__(1)));

			v16 ctrl;
			// direct unaligned vector load -> single movdqu, no call
			explicit Group(const ctrl_t *pos) { ctrl = *reinterpret_cast<const v16u *>(pos); }

			static v16 splat(ctrl_t h)
			{
				return v16{h, h, h, h, h, h, h, h, h, h, h, h, h, h, h, h};
			}
			Mask match(ctrl_t h) const
			{
				const int m = __builtin_ia32_pmovmskb128((v16)(ctrl == splat(h)));
				return Mask(static_cast<uint64>(static_cast<unsigned>(m)));
			}
			Mask maskEmpty() const
			{
				const int m = __builtin_ia32_pmovmskb128((v16)(ctrl == splat(EMPTY)));
				return Mask(static_cast<uint64>(static_cast<unsigned>(m)));
			}
			Mask maskEmptyOrDeleted() const
			{
				// signed compare: SENTINEL(-1) > c  is true only for EMPTY(-128)
				// and DELETED(-2); false for FULL(>=0) and the SENTINEL itself.
				const s16 c = (s16)ctrl;
				const s16 sent = (s16)splat(SENTINEL);
				const int m = __builtin_ia32_pmovmskb128((v16)(sent > c));
				return Mask(static_cast<uint64>(static_cast<unsigned>(m)));
			}
		};
#elif defined(MLW_HASHMAP_SSE2_MSVC)
		// Self-declared SSE2 intrinsics so we never pull in <emmintrin.h> (which
		// drags in <malloc.h> and CRT symbols). Same approach as mlwSqrt.
		// VERIFY these signatures against your MSVC toolset on first build; if it
		// won't compile, build with -DMLW_HASHMAP_PORTABLE while you sort it out.
		namespace msvc_sse2
		{
			typedef union __declspec(intrin_type) __declspec(align(16)) __m128i
			{
				__int8 m128i_i8[16];
				__int16 m128i_i16[8];
				__int32 m128i_i32[4];
				__int64 m128i_i64[2];
				unsigned __int8 m128i_u8[16];
				unsigned __int16 m128i_u16[8];
				unsigned __int32 m128i_u32[4];
				unsigned __int64 m128i_u64[2];
			} __m128i;

			extern "C" __m128i _mm_loadu_si128(__m128i const *); // the unaligned load
			extern "C" __m128i _mm_set1_epi8(char);
			extern "C" __m128i _mm_cmpeq_epi8(__m128i, __m128i);
			extern "C" __m128i _mm_cmpgt_epi8(__m128i, __m128i);
			extern "C" int _mm_movemask_epi8(__m128i);
		} // namespace msvc_sse2

		struct Group
		{
			static constexpr usize Width = 16;
			using Mask = BitMask<16, 0>;

			msvc_sse2::__m128i ctrl;
			// movdqu via the intrinsic instead of a call to mlwMemcpy
			explicit Group(const ctrl_t *pos)
			{
				ctrl = msvc_sse2::_mm_loadu_si128(reinterpret_cast<const msvc_sse2::__m128i *>(pos));
			}

			Mask match(ctrl_t h) const
			{
				const int m = msvc_sse2::_mm_movemask_epi8(
					msvc_sse2::_mm_cmpeq_epi8(ctrl, msvc_sse2::_mm_set1_epi8(static_cast<char>(h))));
				return Mask(static_cast<uint64>(static_cast<unsigned>(m)));
			}
			Mask maskEmpty() const
			{
				const int m = msvc_sse2::_mm_movemask_epi8(
					msvc_sse2::_mm_cmpeq_epi8(ctrl, msvc_sse2::_mm_set1_epi8(static_cast<char>(EMPTY))));
				return Mask(static_cast<uint64>(static_cast<unsigned>(m)));
			}
			Mask maskEmptyOrDeleted() const
			{
				// cmpgt(SENTINEL, c): same signed trick as the GNU path.
				const int m = msvc_sse2::_mm_movemask_epi8(
					msvc_sse2::_mm_cmpgt_epi8(msvc_sse2::_mm_set1_epi8(static_cast<char>(SENTINEL)), ctrl));
				return Mask(static_cast<uint64>(static_cast<unsigned>(m)));
			}
		};
#elif defined(MLW_HASHMAP_NEON_GNU)
		// NEON via compiler vector extensions - no <arm_neon.h> needed. On aarch64
		// the compares lower to cmeq / cmgt. There is no pmovmskb on NEON, so the
		// compare result (0xFF/0x00 per lane) is reinterpreted as a u64 and the
		// per-byte MSB kept via & kMsbs -> an 8-lane BitMask<8,3>. That u64 packing
		// is why this path is 8-wide even though the q register is 128-bit.
		struct Group
		{
			static constexpr usize Width = 8;
			using Mask = BitMask<8, 3>;
			static constexpr uint64 kMsbs = 0x8080808080808080ull;

			typedef unsigned char u8x8 __attribute__((__vector_size__(8)));
			typedef signed char s8x8 __attribute__((__vector_size__(8)));
			typedef unsigned char u8x8u __attribute__((__vector_size__(8), __may_alias__, __aligned__(1)));
			typedef unsigned long long u64u __attribute__((__may_alias__, __aligned__(1)));

			u8x8 ctrl;
			// direct unaligned vector load, no call
			explicit Group(const ctrl_t *pos) { ctrl = *reinterpret_cast<const u8x8u *>(pos); }

			// reinterpret the 8-lane vector as a u64 via a direct load, no call
			static uint64 bits(u8x8 v) { return *reinterpret_cast<const u64u *>(&v); }
			static u8x8 usplat(unsigned char h) { return u8x8{h, h, h, h, h, h, h, h}; }
			static s8x8 ssplat(signed char h) { return s8x8{h, h, h, h, h, h, h, h}; }

			Mask match(ctrl_t h) const
			{
				return Mask(bits(ctrl == usplat(static_cast<unsigned char>(h))) & kMsbs);
			}
			Mask maskEmpty() const
			{
				return Mask(bits(ctrl == usplat(static_cast<unsigned char>(EMPTY))) & kMsbs);
			}
			Mask maskEmptyOrDeleted() const
			{
				const s8x8 c = (s8x8)ctrl;
				const u8x8 r = (u8x8)(ssplat(static_cast<signed char>(SENTINEL)) > c);
				return Mask(bits(r) & kMsbs);
			}
		};
#elif MLW_HASHMAP_SWAR
		// Portable SWAR: 8 control bytes packed in a uint64, pure bit tricks, no
		// intrinsics. Works on any compiler/arch. Byte i of the load is lane i
		// (little-endian assumption). The match/mask tricks are the standard
		// Abseil group_portable formulas, valid for the EMPTY/DELETED/SENTINEL
		// values above.
		struct Group
		{
			static constexpr usize Width = 8;
			using Mask = BitMask<8, 3>;
			static constexpr uint64 kMsbs = 0x8080808080808080ull;
			static constexpr uint64 kLsbs = 0x0101010101010101ull;

			uint64 ctrl;
			explicit Group(const ctrl_t *pos)
			{
#if defined(MLW_MSVC)
				// MSVC never actually selects SWAR (it takes the SSE2 branch), but
				// keep it correct: byte copy the compiler folds, no library call.
				uint64 v = 0;
				for (int i = 0; i < 8; ++i)
					reinterpret_cast<unsigned char *>(&v)[i] = static_cast<unsigned char>(pos[i]);
				ctrl = v;
#else
				uint64 v;
				__builtin_memcpy(&v, pos, 8); // compiler intrinsic -> single load, no call
				ctrl = v;
#endif
			}
			// Classic "byte equals" SWAR: zero byte in x marks a matching lane.
			Mask match(ctrl_t h) const
			{
				const uint64 x = ctrl ^ (kLsbs * static_cast<uint64>(static_cast<uint8>(h)));
				return Mask((x - kLsbs) & ~x & kMsbs);
			}
			Mask maskEmpty() const { return Mask((ctrl & (~ctrl << 6)) & kMsbs); }
			Mask maskEmptyOrDeleted() const { return Mask((ctrl & (~ctrl << 7)) & kMsbs); }
		};
#endif

	} // namespace swiss

	template <typename T>
	concept HashStorable =
		!is_reference_v<T> && is_destructible_v<T> &&
		(is_copy_constructible_v<T> || is_move_constructible_v<T>) &&
		is_same_v<T, remove_cv_t<T>>;

	template <typename T>
	concept HashKey = HashStorable<T> && Equatable<T>;

#ifdef MLW_SIMD_MAP_EXELERATION
	/// \ingroup formattable
	/// \brief A SwissTable hash map from `K` to `V`, group-scanned via \ref swiss::Group.
	///
	/// \tparam K Key type; needs `operator==` and a `core::Hash<K>`.
	/// \tparam V Mapped value type.
	///
	/// Iteration yields `Entry&` (public `.key` / `.value`) for full slots only.
	/// Construct/destroy slots via placement-new / explicit dtor, never assignment.
	/// See the file header for the layout, sentinel, and reference-stability notes.
	template <HashKey K, HashStorable V>
	class Map
	{
		using ctrl_t = swiss::ctrl_t;

	public:
		struct Entry
		{
			K key;
			MLW_NO_UNIQUE_ADDRESS V value;
		};

	private:
		Entry *data = nullptr;	// slot array (capacity entries); lives in the same block as ctrl
		ctrl_t *ctrl = nullptr; // control bytes; also the allocated block's base pointer
		isize capacity = 0;		// probe bitmask (2^k - 1), or 0 when unallocated
		isize size = 0;			// live elements
		isize growth_left = 0;	// inserts-into-empty remaining before we must grow
		const AnonymousAllocator *allocator;

		/// Control bytes for a table of `cap` slots: cap + sentinel + (Width-1) clones.
		static isize ctrlCount(isize cap) { return cap + swiss::Group::Width; }
		/// 7/8 load factor, counting tombstones as occupied.
		static isize loadLimit(isize slots) { return slots - slots / 8; }

		/// Single-block layout: `[ctrl bytes][pad to alignof(Entry)][slots]`.
		/// Returns total bytes and writes the slot region's byte offset. Control
		/// goes first so its cloned tail is backed by the slot array (never at the
		/// block edge), and so `ctrl` doubles as the free pointer.
		static usize blockLayout(isize cap, usize &slotOffsetOut)
		{
			const usize ctrlBytes = ctrlCount(cap);
			const usize a = alignof(Entry);
			const usize slotOffset = (ctrlBytes + a - 1) & ~(a - 1); // round up to Entry alignment
			slotOffsetOut = slotOffset;
			return slotOffset + static_cast<usize>(cap) * sizeof(Entry);
		}
		static usize blockAlign() { return alignof(Entry); } // whole block aligned for the slots

		/// Write control byte `i`, mirroring it into the cloned tail when
		/// `i < Width-1` so a wrapping group load stays consistent. For
		/// `i >= Width-1` the second write lands on `i` itself (a harmless
		/// double write). Every metadata write for a live slot must go through
		/// here, or the tail goes stale and group scans across the seam break.
		MLW_FORCE_INLINE void setCtrl(usize i, ctrl_t c)
		{
			ctrl[i] = c;
			ctrl[((i - (swiss::Group::Width - 1)) & capacity) + (swiss::Group::Width - 1)] = c;
		}

		/// Locate the slot holding `key`, or nullptr. Group-linear probe: scan the
		/// home group's H2 matches (verifying the key), stop when the group has any
		/// EMPTY (chain ended), else advance one group.
		Entry *findEntry(usize hash, const K &key) const
		{
			if (capacity == 0)
				return nullptr;
			const ctrl_t h2 = swiss::H2(hash);
			usize off = swiss::H1(hash) & static_cast<usize>(capacity);
			MLW_PREFETCH(data + off); // hide the slot cache-miss behind the group scan
			while (true)
			{
				swiss::Group g(ctrl + off);
				for (auto bm = g.match(h2); bm.any(); bm.clearLowest())
				{
					const usize i = (off + bm.lowest()) & static_cast<usize>(capacity);
					if (data[i].key == key)
						return &data[i]; // H2 lane hit and key verified
				}
				if (g.maskEmpty().any())
					return nullptr; // group had an empty -> chain ended, not present
				off = (off + swiss::Group::Width) & static_cast<usize>(capacity);
			}
		}

		/// First slot a new key may occupy: the first EMPTY or DELETED (reusable
		/// tombstone) in probe order. Guaranteed to terminate because loadLimit
		/// keeps at least one non-full slot; the SENTINEL is never returned.
		usize findFirstNonFull(usize hash) const
		{
			usize off = swiss::H1(hash) & static_cast<usize>(capacity);
			while (true)
			{
				swiss::Group g(ctrl + off);
				auto bm = g.maskEmptyOrDeleted();
				if (bm.any())
					return (off + bm.lowest()) & static_cast<usize>(capacity);
				off = (off + swiss::Group::Width) & static_cast<usize>(capacity);
			}
		}

		/// Allocate one block for `cap` slots, point ctrl/data into it, fill the
		/// control array with EMPTY and stamp the SENTINEL. Sets `capacity`.
		void allocateTables(isize cap)
		{
			usize slotOffset;
			const usize total = blockLayout(cap, slotOffset);
			void *p = allocator->realloc(allocator, nullptr, 0,
										 static_cast<isize>(total), static_cast<isize>(blockAlign()));
			mlw_debug_assert_msg(p != nullptr, "Map allocation returned nullptr");

			ctrl = static_cast<ctrl_t *>(p);
			data = reinterpret_cast<Entry *>(static_cast<uint8 *>(p) + slotOffset);
			capacity = cap;

			const isize ctrlLen = ctrlCount(cap);
			core::mlwMemset(ctrl, static_cast<int>(static_cast<uint8>(swiss::EMPTY)), static_cast<usize>(ctrlLen));
			ctrl[cap] = swiss::SENTINEL;
		}

		/// Free a block previously produced by allocateTables for `cap` slots.
		/// Must use the same `cap` the block was allocated with (sized allocator).
		void freeBlock(ctrl_t *block, isize cap)
		{
			usize slotOffset;
			const usize total = blockLayout(cap, slotOffset);
			allocator->realloc(allocator, block, static_cast<isize>(total), 0,
							   static_cast<isize>(blockAlign()));
		}

		/// Double capacity (15 -> 31 -> 63 ...) and re-insert every live entry.
		/// allocateTables already zeroed the new control array, so nothing to
		/// re-init here; move each old entry to its new home via findFirstNonFull.
		void rehashAndGrow()
		{
			const isize newCap = (capacity == 0) ? 15 : (capacity * 2 + 1);

			Entry *oldData = data;
			ctrl_t *oldCtrl = ctrl;
			const isize oldCap = capacity;

			allocateTables(newCap);

			size = 0;

			if (oldCtrl != nullptr)
			{
				for (isize i = 0; i < oldCap; ++i)
				{
					if (!swiss::isFull(oldCtrl[i]))
						continue;
					Entry &e = oldData[i];
					const usize hash = swiss::hashKey(e.key);
					const usize slot = findFirstNonFull(hash);
					::new (&data[slot].key) K(core::move_if_movable(e.key));
					::new (&data[slot].value) V(core::move_if_movable(e.value));
					e.key.~K();
					e.value.~V();
					setCtrl(slot, swiss::H2(hash));
					++size;
				}
				freeBlock(oldCtrl, oldCap);
			}

			// Fresh table has no tombstones, so the budget is simply limit - size.
			growth_left = loadLimit(capacity) - size;
		}

		/// Reserve a slot for a *new* key (caller has already verified it is
		/// absent). Grows if out of empty budget and we'd have to consume a fresh
		/// empty; reusing a tombstone doesn't raise the load so it doesn't grow.
		/// Returns the slot index; control byte and size/growth are updated here,
		/// but the key/value are constructed by the caller.
		usize prepareInsert(usize hash)
		{
			usize target;
			if (capacity == 0)
			{
				rehashAndGrow();
				target = findFirstNonFull(hash);
			}
			else
			{
				target = findFirstNonFull(hash);
				if (growth_left == 0 && !swiss::isDeleted(ctrl[target]))
				{
					rehashAndGrow();
					target = findFirstNonFull(hash);
				}
			}
			++size;
			if (swiss::isEmpty(ctrl[target]))
				--growth_left; // consumed an empty; tombstone reuse doesn't
			setCtrl(target, swiss::H2(hash));
			return target;
		}

		template <typename Kk, typename Vv>
		V &putImpl(Kk &&key, Vv &&value)
		{
			const usize hash = swiss::hashKey(key); // key is still a valid lvalue here
			if (Entry *e = findEntry(hash, key))
			{
				e->value.~V();
				::new (&e->value) V(core::forward<Vv>(value));
				return e->value;
			}
			const usize t = prepareInsert(hash);
			::new (&data[t].key) K(core::forward<Kk>(key)); // moves iff caller passed an rvalue
			::new (&data[t].value) V(core::forward<Vv>(value));
			return data[t].value;
		}
		template <typename Kk, typename Vv>
		bool tryInsertImpl(Kk &&key, Vv &&value)
		{
			const usize hash = swiss::hashKey(key);
			if (findEntry(hash, key))
				return false;
			const usize t = prepareInsert(hash);
			::new (&data[t].key) K(core::forward<Kk>(key));
			::new (&data[t].value) V(core::forward<Vv>(value));
			return true;
		}

	public:
		// ---- lifetime -------------------------------------------------------
		Map() : allocator(&core::default_allocator()) {}
		explicit Map(const AnonymousAllocator *alloc) : allocator(alloc) {}

		Map(const Map &) = delete;
		Map &operator=(const Map &) = delete;

		Map(Map &&o)
			: data(o.data), ctrl(o.ctrl), capacity(o.capacity), size(o.size),
			  growth_left(o.growth_left), allocator(o.allocator)
		{
			o.data = nullptr;
			o.ctrl = nullptr;
			o.capacity = 0;
			o.size = 0;
			o.growth_left = 0;
		}
		Map &operator=(Map &&o) noexcept
		{
			if (this != &o)
			{
				deinit();
				::new (this) Map(core::move(o));
			}
			return *this;
		}

		~Map() { deinit(); }

		/// \brief Deep copy. Available only when both K and V are copyable.
		///        Trivially-copyable slots are bulk-copied; otherwise each live
		///        slot is copy-constructed (dead slots stay uninitialized).
		Map clone() const
			requires(is_copy_constructible_v<K> && is_copy_constructible_v<V>)
		{
			Map ret{allocator};
			if (capacity == 0)
				return ret;

			usize slotOffset;
			const usize total = blockLayout(capacity, slotOffset);
			void *p = allocator->realloc(allocator, nullptr, 0,
										 static_cast<isize>(total), static_cast<isize>(blockAlign()));
			mlw_debug_assert_msg(p != nullptr, "Map::clone allocation returned nullptr");

			ret.ctrl = static_cast<ctrl_t *>(p);
			ret.data = reinterpret_cast<Entry *>(static_cast<uint8 *>(p) + slotOffset);
			ret.capacity = capacity;
			ret.size = size;
			ret.growth_left = growth_left;

			// control array (incl. sentinel + clones) is POD -> copy wholesale
			core::mlwMemcpy(ret.ctrl, ctrl, static_cast<usize>(ctrlCount(capacity)) * sizeof(ctrl_t));

			if constexpr (is_trivially_copyable_v<K> && is_trivially_copyable_v<V>)
			{
				core::mlwMemcpy(ret.data, data, static_cast<usize>(capacity) * sizeof(Entry));
			}
			else
			{
				for (isize i = 0; i < capacity; ++i)
					if (swiss::isFull(ctrl[i]))
					{
						::new (&ret.data[i].key) K(static_cast<const K &>(data[i].key));
						::new (&ret.data[i].value) V(static_cast<const V &>(data[i].value));
					}
			}
			return ret;
		}

		// ---- capacity -------------------------------------------------------
		isize len() const { return size; }
		bool isEmpty() const { return size == 0; }

		// ---- lookup ---------------------------------------------------------
		/// \brief Reference to the value for `key`, or empty. Invalidated by any
		///        subsequent insert that grows the table.
		Optional<V &> get(const K &key)
		{
			Entry *e = findEntry(swiss::hashKey(key), key);
			if (e == nullptr)
				return nullptr;
			return Optional<V &>{e->value};
		}
		Optional<const V &> get(const K &key) const
		{
			const Entry *e = findEntry(swiss::hashKey(key), key);
			if (e == nullptr)
				return nullptr;
			return Optional<const V &>{e->value};
		}
		bool contains(const K &key) const
		{
			return findEntry(swiss::hashKey(key), key) != nullptr;
		}

		// ---- insert ---------------------------------------------------------
		/// \brief Insert or overwrite `key` -> `value`. \return Reference to the value.
		// existing: copy key, copy value
		V &put(const K &key, const V &value) { return putImpl(key, value); }
		// existing: copy key, move value
		V &put(const K &key, V &&value) { return putImpl(key, core::move(value)); }
		// NEW: move key, copy value
		V &put(K &&key, const V &value) { return putImpl(core::move(key), value); }
		// NEW: move key, move value
		V &put(K &&key, V &&value) { return putImpl(core::move(key), core::move(value)); }

		/// \brief Insert `key` -> `value` **only if `key` is absent**. Unlike \ref put,
		///        an existing entry is left untouched (its value is not overwritten and
		///        `value` is dropped). \return true if a new entry was inserted, false
		///        if `key` was already present.
		///
		/// \note Same reference-stability contract as \ref put: an insert that grows
		///       the table relocates every entry, invalidating references previously
		///       returned by \ref get / \ref put and any stored `Entry*`. A false
		///       return performs no insert and never grows.
		bool tryInsert(const K &key, const V &value) { return tryInsertImpl(key, value); }
		/// \copydoc tryInsert(const K&, V&&)
		bool tryInsert(const K &key, V &&value) { return tryInsertImpl(key, core::move(value)); }
		/// \copydoc tryInsert(const K&, V&&)
		bool tryInsert(K &&key, const V &value) { return tryInsertImpl(core::move(key), value); }
		/// \copydoc tryInsert(const K&, V&&)
		bool tryInsert(K &&key, V &&value) { return tryInsertImpl(core::move(key), core::move(value)); }

		// ---- erase ----------------------------------------------------------
		/// \brief Remove `key` if present. \return true if something was removed.
		///        Downgrades the slot to EMPTY (and reclaims growth budget) when
		///        the next slot is already EMPTY -- no probe chain runs through it;
		///        otherwise leaves a DELETED tombstone, which a later resize clears.
		bool remove(const K &key)
		{
			if (capacity == 0)
				return false;
			const usize hash = swiss::hashKey(key);
			const ctrl_t h2 = swiss::H2(hash);
			usize off = swiss::H1(hash) & static_cast<usize>(capacity);
			MLW_PREFETCH(data + off);
			while (true)
			{
				swiss::Group g(ctrl + off);
				for (auto bm = g.match(h2); bm.any(); bm.clearLowest())
				{
					const usize i = (off + bm.lowest()) & static_cast<usize>(capacity);
					if (data[i].key == key)
					{
						data[i].key.~K();
						data[i].value.~V();
						const bool nextEmpty = ctrl[(i + 1) & static_cast<usize>(capacity)] == swiss::EMPTY;
						setCtrl(i, nextEmpty ? swiss::EMPTY : swiss::DELETED);
						if (nextEmpty)
							++growth_left;
						--size;
						return true;
					}
				}
				if (g.maskEmpty().any())
					return false;
				off = (off + swiss::Group::Width) & static_cast<usize>(capacity);
			}
		}

		/// \brief Destroy all entries but keep the allocated capacity.
		void clear()
		{
			if (capacity == 0)
				return;

			for (isize i = 0; i < capacity; ++i)
			{
				if (swiss::isFull(ctrl[i]))
				{
					data[i].key.~K();
					data[i].value.~V();
				}
			}
			core::mlwMemset(ctrl, static_cast<int>(static_cast<uint8>(swiss::EMPTY)), static_cast<usize>(ctrlCount(capacity)));
			ctrl[capacity] = swiss::SENTINEL;
			size = 0;
			growth_left = loadLimit(capacity);
		}

		/// \brief Destroy all entries and release storage. Idempotent.
		void deinit()
		{
			if (capacity != 0)
			{
				for (isize i = 0; i < capacity; ++i)
				{
					if (swiss::isFull(ctrl[i]))
					{
						data[i].key.~K();
						data[i].value.~V();
					}
				}
				freeBlock(ctrl, capacity);
			}
			data = nullptr;
			ctrl = nullptr;
			capacity = 0;
			size = 0;
			growth_left = 0;
		}

		// ---- iteration (yields Entry& for full slots only) ------------------
		template <typename MapPtr, typename EntryRef>
		struct IteratorT
		{
			MapPtr map;
			isize idx;
			/// Advance idx to the next full slot (or end). Byte-at-a-time; a
			/// group-aware version would speed up iterate at high load factors.
			void skip()
			{
				while (idx < map->capacity && !swiss::isFull(map->ctrl[idx]))
					++idx;
			}
			EntryRef operator*() const { return map->data[idx]; }
			bool operator!=(const IteratorT &o) const { return idx != o.idx; }
			IteratorT &operator++()
			{
				++idx;
				skip();
				return *this;
			}
		};
		using Iterator = IteratorT<Map *, Entry &>;
		using ConstIterator = IteratorT<const Map *, const Entry &>;

		Iterator begin()
		{
			Iterator it{this, 0};
			it.skip();
			return it;
		}
		Iterator end() { return Iterator{this, capacity}; }
		ConstIterator begin() const
		{
			ConstIterator it{this, 0};
			it.skip();
			return it;
		}
		ConstIterator end() const { return ConstIterator{this, capacity}; }

		template <FormatBuffer Buffer>
			requires(FormattableValue<K, Buffer> && FormattableValue<V, Buffer>)
		void format(Buffer &buffer) const
		{
			buffer.append('{');
			bool first = true;
			for (const auto &entry : *this) // entry is const Entry&
			{
				if (!first)
					buffer.append(", ");
				first = false;
				buffer.append('[');
				detail::formatValue(buffer, entry.key);
				buffer.append("; ");
				detail::formatValue(buffer, entry.value);
				buffer.append(']');
			}
			buffer.append('}');
		}
	};
#else
	// No group backend selected (MLW_LINIAR_MAP_PROBE, or an unsupported target):
	// fall back to the scalar byte-scan map, aliased to the same name.
#include "stl/scalar_map.inl"
	template <HashKey K, HashStorable V>
	using Map = ScalarMap<K, V>;
#endif
} // namespace core