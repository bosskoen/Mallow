#pragma once

#include <core/typedef.h>
#include <core/c_string.h>
#include <core/compilers.h>
#include <core/macro.h>
#include <core/libc/mem.h>                 // mlwMemcpy
#include <core/memory/anonymous_allocator.h>
#include <core/hash.h>                     // core::Hash primary + core::mix64 for Hash<String>
                                           // (consider factoring those into a light core/hash.h)

/// \file
/// \brief Owning UTF-8-agnostic byte string with small-string optimization (SSO).

namespace core
{
	/// \ingroup formattable
	/// \brief A growable, owning string of bytes with small-string optimization.
	///
	/// \par Layout (32 bytes on a 64-bit target)
	///   `[ allocator* (8) ][ union { long | short } (24) ]`. A *long* string is
	///   `{ char* ptr; usize len; usize cap; }` pointing at a heap block; a *short*
	///   string stores up to \ref SSO_CAP bytes **inline** in the union with no
	///   allocation. Which arm is live is read from the union's last byte — see
	///   the tag note — so short strings (the common case) never touch the heap.
	///
	/// \par The last-byte tag (little-endian)
	///   The short arm's trailing byte holds `space_left = SSO_CAP - len`, always
	///   in `0..SSO_CAP` (high bit clear). The long arm stores `cap` with its top
	///   bit set (\ref LONG_MASK); on a little-endian target that top bit lands in
	///   the very same trailing byte. So "is long?" is just the high bit of that
	///   byte, read through an `unsigned char` (legal aliasing, not union punning).
	///   \note Little-endian is assumed, consistent with \ref core::Map.
	///
	/// \par NUL termination
	///   A NUL is maintained at `data()[len()]` as a **convenience** (cheap: the
	///   short arm's `space_left == 0` doubles as the terminator at max length; the
	///   heap arm reserves one extra byte). It is *not* the interface contract —
	///   the canonical view is \ref operator CStr (pointer + length), which is how
	///   you should pass a string anywhere. Treat `data()` as C-string-usable only
	///   at the rare boundary that truly demands it.
	///
	/// \note Not copyable (use \ref clone); movable.
	/// \note Not reference/pointer stable: any mutation that grows may relocate to
	///       the heap (or move short->long), invalidating `data()`, \ref operator[]
	///       results, and any \ref slice / \ref operator CStr taken earlier. Take a
	///       fresh view after mutating.
	/// \warning Because SSO makes `data()` point *into* the object, an SSO string is
	///       **not trivially relocatable**. `String` has a real move ctor and is not
	///       trivially copyable, so a `Vector<String>` automatically takes the
	///       per-element move path — do not force a byte-move over strings.
	class String
	{
		/// \brief Inline capacity in bytes: the union is 24 wide, minus the 1-byte tag.
		static constexpr isize SSO_CAP = 23;
		/// \brief Top-bit marker OR-ed into the long arm's `cap` to flag "is long".
		static constexpr usize LONG_MASK = static_cast<usize>(1) << 63;

		struct Long
		{
			char *ptr;
			usize len;
			usize cap; // stored as (real_cap | LONG_MASK)
		};
		struct Short
		{
			char buf[SSO_CAP];
			uint8 space_left; // SSO_CAP - len; high bit 0 => short. NUL when len==SSO_CAP.
		};
		union Repr
		{
			Long l;
			Short s;
		};

		const AnonymousAllocator *allocator;
		Repr u;

		// -- tag / arm helpers ------------------------------------------------

		/// Last union byte via unsigned-char access (well-defined; no inactive read).
		MLW_FORCE_INLINE unsigned char lastByte() const
		{
			return reinterpret_cast<const unsigned char *>(&u)[SSO_CAP];
		}
		MLW_FORCE_INLINE bool isLong() const { return (lastByte() & 0x80u) != 0; }

		/// Reset to the empty short state (drops long ownership without freeing).
		MLW_FORCE_INLINE void setEmptyShort()
		{
			u.s.space_left = static_cast<uint8>(SSO_CAP);
			u.s.buf[0] = '\0';
		}

		/// Set length and (re)write the maintained NUL. Branches on the live arm.
		MLW_FORCE_INLINE void setLen(isize n)
		{
			if (isLong())
			{
				u.l.len = static_cast<usize>(n);
				u.l.ptr[n] = '\0'; // heap reserves cap+1, always room
			}
			else
			{
				u.s.space_left = static_cast<uint8>(SSO_CAP - n);
				if (n < SSO_CAP)
					u.s.buf[n] = '\0'; // in-bounds
				// n == SSO_CAP: space_left == 0 IS the terminator at offset SSO_CAP
			}
		}

		/// Move storage to a larger `new_cap` usable bytes, preserving contents+len.
		/// Long->long reallocs (bytes relocate freely); short->long allocs+copies+switches.
		void growTo(isize new_cap)
		{
			mlw_debug_assert_msg(new_cap > capacity(), "String::growTo not growing");
			const isize L = len();
			if (isLong())
			{
				const isize old_cap = static_cast<isize>(u.l.cap & ~LONG_MASK);
				void *p = allocator->realloc(allocator, u.l.ptr,
											 old_cap + 1, new_cap + 1, alignof(char));
				mlw_debug_assert_msg(p != nullptr, "String::growTo OOM");
				u.l.ptr = static_cast<char *>(p);
				u.l.cap = static_cast<usize>(new_cap) | LONG_MASK;
				// u.l.len unchanged; NUL at ptr[L] carried by the byte-copy realloc
			}
			else
			{
				void *p = allocator->realloc(allocator, nullptr, 0, new_cap + 1, alignof(char));
				mlw_debug_assert_msg(p != nullptr, "String::growTo OOM");
				char *np = static_cast<char *>(p);
				core::mlwMemcpy(np, u.s.buf, static_cast<usize>(L)); // buf still active
				np[L] = '\0';
				u.l.ptr = np; // activates long arm
				u.l.len = static_cast<usize>(L);
				u.l.cap = static_cast<usize>(new_cap) | LONG_MASK;
			}
		}

		/// Ensure capacity for `need` bytes, growing ~1.5x (or straight to need).
		void ensureCap(isize need)
		{
			if (need <= capacity())
				return;
			isize nc = capacity() + capacity() / 2;
			if (nc < need)
				nc = need;
			growTo(nc);
		}

		/// Populate a freshly-constructed String from a byte view.
		void initFrom(CStr sv)
		{
			const isize n = static_cast<isize>(sv.len);
			if (n <= SSO_CAP)
			{
				if (n > 0)
					core::mlwMemcpy(u.s.buf, sv.ptr, static_cast<usize>(n)); // activates short
				u.s.space_left = static_cast<uint8>(SSO_CAP - n);
				if (n < SSO_CAP)
					u.s.buf[n] = '\0';
			}
			else
			{
				void *p = allocator->realloc(allocator, nullptr, 0, n + 1, alignof(char));
				mlw_debug_assert_msg(p != nullptr, "String ctor OOM");
				char *np = static_cast<char *>(p);
				core::mlwMemcpy(np, sv.ptr, static_cast<usize>(n));
				np[n] = '\0';
				u.l.ptr = np;
				u.l.len = static_cast<usize>(n);
				u.l.cap = static_cast<usize>(n) | LONG_MASK; // exact fit
			}
		}

	public:
		// -- lifetime ---------------------------------------------------------

		/// \brief Empty string on the default allocator (no allocation).
		String() : allocator(&core::default_allocator()) { setEmptyShort(); }

		/// \brief Empty string on \p alloc (no allocation).
		explicit String(const AnonymousAllocator *alloc) : allocator(alloc) { setEmptyShort(); }

		/// \brief Copy the bytes of \p sv (default allocator). The canonical inward path.
		String(CStr sv) : allocator(&core::default_allocator()) { initFrom(sv); }

		/// \brief Copy the bytes of \p sv onto \p alloc.
		String(CStr sv, const AnonymousAllocator *alloc) : allocator(alloc) { initFrom(sv); }

		/// \brief Copy a string literal / char array (default allocator).
		/// \note Length is the array extent minus one — same rule (and caveats) as
		///       \ref CStr's array constructor; use the \ref CStr overload for
		///       partially-filled buffers or embedded NULs.
		template <index_t N>
		explicit String(const char (&lit)[N]) : allocator(&core::default_allocator())
		{
			initFrom(CStr(lit));
		}

		String(const String &) = delete;
		String &operator=(const String &) = delete;

		/// \brief Move: steal \p o's storage; \p o is left empty and valid.
		String(String &&o) : allocator(o.allocator), u(o.u) { o.setEmptyShort(); }

		/// \brief Move-assign: free ours, take \p o's. Self-assign is a no-op.
		String &operator=(String &&o)
		{
			if (this != &o)
			{
				deinit();
				allocator = o.allocator;
				u = o.u;
				o.setEmptyShort();
			}
			return *this;
		}

		~String() { deinit(); }

		/// \brief Free heap storage (if any) and reset to empty. Idempotent.
		void deinit()
		{
			if (isLong() && u.l.ptr != nullptr)
			{
				const isize cap = static_cast<isize>(u.l.cap & ~LONG_MASK);
				allocator->realloc(allocator, u.l.ptr, cap + 1, 0, alignof(char));
			}
			setEmptyShort();
		}

		/// \brief Independent deep copy on the same allocator.
		String clone() const { return String(static_cast<CStr>(*this), allocator); }

		// -- capacity ---------------------------------------------------------

		/// \brief Length in bytes (excludes the maintained NUL).
		isize len() const
		{
			return isLong() ? static_cast<isize>(u.l.len)
							 : (SSO_CAP - static_cast<isize>(lastByte()));
		}
		bool isEmpty() const { return len() == 0; }

		/// \brief Usable byte capacity before the next reallocation.
		isize capacity() const
		{
			return isLong() ? static_cast<isize>(u.l.cap & ~LONG_MASK) : SSO_CAP;
		}

		/// \brief Whether the bytes currently live inline (no heap allocation).
		bool isInline() const { return !isLong(); }

		/// \brief Ensure capacity for at least \p n bytes. Never shrinks. Panics on OOM.
		void reserve(isize n)
		{
			mlw_debug_assert_msg(n >= 0, "String::reserve negative");
			if (n > capacity())
				growTo(n);
		}

		// -- access -----------------------------------------------------------

		/// \brief Pointer to the bytes. NUL-terminated as a convenience; prefer
		///        \ref operator CStr. \warning Invalidated by any growth.
		char *data() { return isLong() ? u.l.ptr : u.s.buf; }
		/// \copydoc data()
		const char *data() const { return isLong() ? u.l.ptr : u.s.buf; }

		/// \brief Non-owning view (pointer + length). The canonical way to pass a string.
		operator CStr() const { return CStr(data(), static_cast<index_t>(len())); }

		/// \brief Unchecked byte access. \pre `0 <= i < len`.
		char &operator[](isize i)
		{
			mlw_debug_assert_msg(i >= 0 && i < len(), "String::operator[] out of bounds");
			return data()[i];
		}
		/// \copydoc operator[](isize)
		const char &operator[](isize i) const
		{
			mlw_debug_assert_msg(i >= 0 && i < len(), "String::operator[] out of bounds");
			return data()[i];
		}

		/// \brief Non-owning view of `[start, start+count)`.
		/// \pre `0 <= start`, `0 <= count`, `start + count <= len`.
		/// \warning Invalidated by any growth, like every other view into the string.
		CStr slice(isize start, isize count) const
		{
			mlw_debug_assert_msg(start >= 0 && count >= 0 && start + count <= len(),
								 "String::slice out of range");
			return CStr(data() + start, static_cast<index_t>(count));
		}

		// -- modifiers --------------------------------------------------------

		/// \brief Append one byte. Grows if needed; panics on OOM.
		void push(char c)
		{
			const isize L = len();
			ensureCap(L + 1);
			data()[L] = c;
			setLen(L + 1);
		}

		/// \brief Append the bytes of \p sv. Grows if needed; panics on OOM.
		/// \warning \p sv must not alias this string's storage (a grow may relocate).
		void append(CStr sv)
		{
			const isize n = static_cast<isize>(sv.len);
			if (n == 0)
				return;
			const isize L = len();
			ensureCap(L + n);
			core::mlwMemcpy(data() + L, sv.ptr, static_cast<usize>(n));
			setLen(L + n);
		}

		String &operator+=(CStr sv)
		{
			append(sv);
			return *this;
		}
		String &operator+=(char c)
		{
			push(c);
			return *this;
		}

		/// \brief Truncate to length 0, keeping the current allocation.
		void clear() { setLen(0); }

		// -- compare ----------------------------------------------------------

		bool operator==(const String &o) const { return bytesEqual(o.data(), o.len()); }
		bool operator==(CStr sv) const { return bytesEqual(sv.ptr, static_cast<isize>(sv.len)); }

	private:
		bool bytesEqual(const char *p, isize n) const
		{
			const isize L = len();
			if (L != n)
				return false;
			const char *a = data();
			for (isize i = 0; i < L; ++i)
				if (a[i] != p[i])
					return false;
			return true;
		}

	public:
		// -- format (mirrors Vector::format; depends on your FormatBuffer concept) --
		template <FormatBuffer Buffer>
		void format(Buffer &buffer) const { buffer.append(static_cast<CStr>(*this)); }
	};

	static_assert(sizeof(void *) == 8, "String's last-byte tag assumes a 64-bit target.");
	static_assert(sizeof(String) == 32, "String must stay 32 bytes: 8 (allocator) + 24 (union).");

	/// \brief `Hash<String>` so `Map<String, V>` and `Set<String>` work out of the box.
	///        FNV-1a over the bytes, finished through \ref mix64 for a strong H1/H2 split.
	template <>
	struct Hash<String>
	{
		usize operator()(const String &str) const
		{
			const char *p = str.data();
			const isize n = str.len();
			uint64 h = 0xcbf29ce484222325ull; // FNV-1a offset basis
			for (isize i = 0; i < n; ++i)
			{
				h ^= static_cast<uint8>(p[i]);
				h *= 0x100000001b3ull; // FNV-1a prime
			}
			return static_cast<usize>(mix64(h ^ static_cast<uint64>(n)));
		}
	};
} // namespace core