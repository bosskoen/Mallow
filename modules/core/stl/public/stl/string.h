#pragma once

#include <core/typedef.h>
#include <core/c_string.h>
#include <core/compilers.h>
#include <core/macro.h>
#include <core/optional.h>
#include <core/libc/mem.h>
#include <core/memory/anonymous_allocator.h>
#include <core/hash.h>

/// \file
/// \brief Owning UTF-8-agnostic byte string with small-string optimization (SSO).

namespace core
{
	/// \ingroup formattable
	/// \brief A growable, owning string of bytes with small-string optimization.
	///
	/// Uses the same member names and meanings as \ref Vector (see "Shared
	/// container vocabulary" there); its view type is \ref CStr. A `String` is
	/// also a \ref FormatBuffer, so you can format straight into it:
	/// `mlw_write(s, "{}: {}", key, value);`.
	///
	/// \par Layout (32 bytes on a 64-bit target)
	///   `[ allocator* (8) ][ union { long | short } (24) ]`. A *long* string is
	///   `{ char* ptr; usize len; usize cap; }` pointing at a heap block; a *short*
	///   string stores up to \ref SSO_CAP bytes inline with no allocation.
	///
	/// \par The last-byte tag (little-endian)
	///   The short arm's trailing byte holds `space_left = SSO_CAP - len`, always
	///   in `0..SSO_CAP` (high bit clear). The long arm stores `cap` with its top
	///   bit set (\ref LONG_MASK); on little-endian that bit lands in the same
	///   trailing byte. So "is long?" is the high bit of that byte, read through
	///   an `unsigned char` (legal aliasing, not union punning).
	///
	/// \par NUL termination
	///   A NUL is kept at `data()[len()]` as a convenience (at max short length
	///   `space_left == 0` *is* that NUL; the heap block reserves one extra byte).
	///   The real interface is the \ref CStr view (pointer + length).
	///
	/// \par Growth & aliasing
	///   Same rules as \ref Vector: implicit growth is geometric, only
	///   \ref reserve / \ref tryReserve are exact, and arguments may point into
	///   this string (`s.append(s)`, `s.insert(0, s.slice(2, 3))` are fine).
	///
	/// \note Not copyable (use \ref clone); movable.
	/// \warning Views (`data()`, \ref slice, the \ref CStr conversion) are
	///          invalidated by anything that grows. SSO also means a `String` is
	///          not trivially relocatable; it has a real move constructor, so
	///          containers move it element-wise automatically.
	class String
	{
		/// \brief Inline capacity in bytes: the 24-byte union minus the 1-byte tag.
		static constexpr isize SSO_CAP = 23;
		/// \brief Top-bit marker OR-ed into the long arm's `cap` to flag "is long".
		static constexpr usize LONG_MASK = static_cast<usize>(1) << 63;

		struct Long
		{
			char *ptr;
			usize len;
			usize cap; // real_cap | LONG_MASK
		};
		struct Short
		{
			char buf[SSO_CAP];
			uint8 space_left; // SSO_CAP - len; high bit 0 => short. Doubles as NUL at len == SSO_CAP.
		};
		union Repr
		{
			Long l;
			Short s;
		};

		const AnonymousAllocator *alloc;
		Repr u;

		// -- tag helpers ------------------------------------------------------

		MLW_FORCE_INLINE unsigned char lastByte() const
		{
			return reinterpret_cast<const unsigned char *>(&u)[SSO_CAP];
		}
		MLW_FORCE_INLINE bool isLong() const { return (lastByte() & 0x80u) != 0; }
		MLW_FORCE_INLINE isize longCap() const { return static_cast<isize>(u.l.cap & ~LONG_MASK); }

		/// Switch to the short arm holding `n <= SSO_CAP` bytes already in `u.s.buf`.
		MLW_FORCE_INLINE void setShortLen(isize n)
		{
			u.s.space_left = static_cast<uint8>(SSO_CAP - n);
			if (n < SSO_CAP)
				u.s.buf[n] = '\0';
		}

		/// Set length on the live arm and rewrite the trailing NUL.
		MLW_FORCE_INLINE void setLen(isize n)
		{
			if (isLong())
			{
				u.l.len = static_cast<usize>(n);
				u.l.ptr[n] = '\0';
			}
			else
			{
				setShortLen(n);
			}
		}

		// -- storage ----------------------------------------------------------

		/// Move to a heap block with room for `new_cap` bytes (+1 for the NUL),
		/// keeping contents. Returns false on OOM with the string unchanged.
		bool tryGrowTo(isize new_cap)
		{
			const isize n = len();
			if (isLong())
			{
				void *p = alloc->realloc(alloc, u.l.ptr, longCap() + 1, new_cap + 1, alignof(char));
				if (!p)
					return false;
				u.l.ptr = static_cast<char *>(p);
			}
			else
			{
				char *p = static_cast<char *>(alloc->realloc(alloc, nullptr, 0, new_cap + 1, alignof(char)));
				if (!p)
					return false;
				mlwMemcpy(p, u.s.buf, static_cast<usize>(n) + 1); // + the NUL; read before u.l overwrites buf
				u.l.ptr = p;
				u.l.len = static_cast<usize>(n);
			}
			u.l.cap = static_cast<usize>(new_cap) | LONG_MASK;
			return true;
		}

		/// Ensure room for `need` bytes, growing geometrically. Panics on OOM.
		void growFor(isize need)
		{
			const isize c = capacity();
			if (need > c)
				reserve(need > 2 * c ? need : 2 * c);
		}

		void freeHeap()
		{
			if (isLong())
				alloc->realloc(alloc, u.l.ptr, longCap() + 1, 0, alignof(char));
		}

		void setEmptyShort() { setShortLen(0); }

		/// True if `p` points into this string's bytes (so a grow would invalidate it).
		bool ownsPtr(const char *p) const
		{
			const char *d = data();
			return reinterpret_cast<uptr>(p) >= reinterpret_cast<uptr>(d) &&
				   reinterpret_cast<uptr>(p) < reinterpret_cast<uptr>(d + len());
		}

	public:
		// -- lifetime ---------------------------------------------------------

		/// \brief Empty string on the default allocator. Does not allocate.
		String() : String(&core::default_allocator()) {}

		/// \brief Empty string on \p allocator. Does not allocate.
		explicit String(const AnonymousAllocator *allocator) : alloc(allocator), u{} { setEmptyShort(); }

		/// \brief Copy the bytes of \p sv (a literal works: `String s("hi")`).
		///        Allocates only if `sv.len > 23`, and then exactly.
		String(CStr sv, const AnonymousAllocator *allocator = &core::default_allocator()) : String(allocator)
		{
			if (sv.len > SSO_CAP)
				reserve(sv.len);
			append(sv);
		}

		String(const String &) = delete;
		String &operator=(const String &) = delete;

		/// \brief Take \p o's storage. \post \p o is empty and valid.
		String(String &&o) : alloc(o.alloc), u(o.u) { o.setEmptyShort(); }

		/// \brief Free ours, take \p o's. Self-assignment is a no-op.
		String &operator=(String &&o)
		{
			if (this != &o)
			{
				freeHeap();
				alloc = o.alloc;
				u = o.u;
				o.setEmptyShort();
			}
			return *this;
		}

		~String() { freeHeap(); }

		/// \brief Free heap storage (if any) and reset to empty. Idempotent.
		void deinit()
		{
			freeHeap();
			setEmptyShort();
		}

		/// \brief Independent deep copy on the same allocator.
		String clone() const { return String(*this, alloc); }

		/// \brief The allocator this string was built with.
		const AnonymousAllocator *allocator() const { return alloc; }

		// -- size & capacity --------------------------------------------------

		/// \brief Length in bytes (excluding the maintained NUL).
		MLW_FORCE_INLINE isize len() const
		{
			return isLong() ? static_cast<isize>(u.l.len) : SSO_CAP - static_cast<isize>(lastByte());
		}
		bool isEmpty() const { return len() == 0; }

		/// \brief Bytes that fit before the next reallocation.
		isize capacity() const { return isLong() ? longCap() : SSO_CAP; }

		/// \brief Whether the bytes currently live inline (no heap block).
		bool isInline() const { return !isLong(); }

		/// \brief Ensure capacity for at least \p n bytes (exact). Never shrinks.
		/// \return `false` on OOM, leaving the string unchanged.
		bool tryReserve(isize n)
		{
			mlw_debug_assert_msg(n >= 0, "String::reserve negative size");
			return n <= capacity() || tryGrowTo(n);
		}

		/// \brief As \ref tryReserve, but panics on OOM.
		void reserve(isize n)
		{
			const bool ok = tryReserve(n);
			mlw_debug_assert_msg(ok, "String: out of memory");
		}

		/// \brief Drop unused capacity: moves back inline if it fits, otherwise
		///        shrinks the heap block in place (never relocates).
		void shrinkToFit()
		{
			if (!isLong())
				return;
			const isize n = len();
			char *p = u.l.ptr;
			const isize c = longCap();
			if (n <= SSO_CAP)
			{
				mlwMemcpy(u.s.buf, p, static_cast<usize>(n));
				setShortLen(n);
				alloc->realloc(alloc, p, c + 1, 0, alignof(char));
			}
			else if (n < c)
			{
				alloc->realloc(alloc, p, c + 1, n + 1, alignof(char));
				u.l.cap = static_cast<usize>(n) | LONG_MASK;
			}
		}

		/// \brief Set length to 0, keeping the allocation.
		void clear() { setLen(0); }

		/// \brief Shrink to \p n bytes. No-op if `n >= len`.
		void truncate(isize n)
		{
			mlw_debug_assert_msg(n >= 0, "String::truncate negative length");
			if (n < len())
				setLen(n);
		}

		/// \brief Resize to \p n bytes; new bytes are \p fill.
		void resize(isize n, char fill = '\0')
		{
			const isize L = len();
			if (n <= L)
				return truncate(n);
			growFor(n);
			mlwMemset(data() + L, fill, static_cast<usize>(n - L));
			setLen(n);
		}

		// -- access -----------------------------------------------------------

		/// \brief Pointer to the bytes (NUL-terminated as a convenience; prefer
		///        the \ref CStr view). \warning Invalidated by growth.
		char *data() { return isLong() ? u.l.ptr : u.s.buf; }
		const char *data() const { return isLong() ? u.l.ptr : u.s.buf; }

		/// \brief Non-owning view (pointer + length). The canonical way to pass a string.
		operator CStr() const { return CStr(data(), len()); }

		/// \brief Byte access, bounds-checked in debug builds only.
		char &operator[](isize i)
		{
			mlw_debug_assert_msg(i >= 0 && i < len(), "String index {} out of range (len {})", i, len());
			return data()[i];
		}
		const char &operator[](isize i) const
		{
			mlw_debug_assert_msg(i >= 0 && i < len(), "String index {} out of range (len {})", i, len());
			return data()[i];
		}

		/// \brief Checked access: None if \p i is out of range (including negative).
		Optional<char &> get(isize i)
		{
			if (i < 0 || i >= len())
				return nullptr;
			return Optional<char &>{data()[i]};
		}
		Optional<const char &> get(isize i) const
		{
			if (i < 0 || i >= len())
				return nullptr;
			return Optional<const char &>{data()[i]};
		}

		/// \brief First / last byte, or None when empty.
		Optional<char &> front() { return get(0); }
		Optional<const char &> front() const { return get(0); }
		Optional<char &> back() { return get(len() - 1); }
		Optional<const char &> back() const { return get(len() - 1); }

		/// \brief Non-owning view of `[start, start + count)`.
		CStr slice(isize start, isize count) const
		{
			mlw_debug_assert_msg(start >= 0 && count >= 0 && start + count <= len(), "String::slice out of range");
			return CStr(data() + start, count);
		}

		// -- modifiers --------------------------------------------------------

		/// \brief Append one byte.
		void push(char c)
		{
			const isize L = len();
			growFor(L + 1);
			data()[L] = c;
			setLen(L + 1);
		}

		/// \brief Append \p sv (may be a view of this string).
		void append(CStr sv)
		{
			const isize n = sv.len;
			if (n <= 0)
				return;
			const isize L = len();
			if (ownsPtr(sv.ptr)) // self-append: re-find the source after a possible move
			{
				const isize off = sv.ptr - data();
				growFor(L + n);
				sv.ptr = data() + off;
			}
			else
			{
				growFor(L + n);
			}
			mlwMemcpy(data() + L, sv.ptr, static_cast<usize>(n));
			setLen(L + n);
		}

		/// \brief Same as \ref push; with \ref append(CStr) it makes String a \ref FormatBuffer.
		void append(char c) { push(c); }

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

		/// \brief Remove and return the last byte. \pre `!isEmpty()`.
		char pop()
		{
			const isize L = len();
			mlw_debug_assert_msg(L > 0, "String::pop on empty string");
			const char c = data()[L - 1];
			setLen(L - 1);
			return c;
		}

		/// \brief Insert \p sv at byte \p i (may be a view of this string).
		/// \pre `0 <= i <= len()`.
		void insert(isize i, CStr sv)
		{
			const isize L = len();
			mlw_debug_assert_msg(i >= 0 && i <= L, "String::insert index {} out of range (len {})", i, L);
			if (sv.len <= 0)
				return;
			if (ownsPtr(sv.ptr)) // the shift below would move the source under us
			{
				const String tmp(sv, alloc);
				return insert(i, tmp);
			}
			growFor(L + sv.len);
			char *d = data();
			mlwMemmove(d + i + sv.len, d + i, static_cast<usize>(L - i));
			mlwMemcpy(d + i, sv.ptr, static_cast<usize>(sv.len));
			setLen(L + sv.len);
		}

		/// \brief Remove `[start, start + count)`, shifting the rest left.
		void removeRange(isize start, isize count)
		{
			const isize L = len();
			mlw_debug_assert_msg(start >= 0 && count >= 0 && start + count <= L, "String::removeRange out of range");
			char *d = data();
			mlwMemmove(d + start, d + start + count, static_cast<usize>(L - start - count));
			setLen(L - count);
		}

		// -- search & compare -------------------------------------------------

		/// \brief Byte offset of the first occurrence of \p needle, or None.
		///        An empty needle matches at 0.
		Optional<isize> find(CStr needle) const
		{
			const isize n = needle.len;
			const isize L = len();
			if (n == 0)
			{
				return Optional<isize>{static_cast<isize>(0)};
			}
			if (n > L)
				return nullptr;
			const char *d = data();
			const char first = needle.ptr[0];
			for (isize i = 0; i <= L - n; ++i)
			{
				if (d[i] != first) // cheap filter: most positions stop here
					continue;
				if (mlwMemcmp(d + i + 1, needle.ptr + 1, static_cast<usize>(n - 1)) == 0)
					return Optional<isize>{i};
			}
			return nullptr;
		}

		bool contains(CStr needle) const { return find(needle).isSome(); }

		bool startsWith(CStr prefix) const
		{
			return static_cast<CStr>(*this).startsWith(prefix);
		}

		bool endsWith(CStr suffix) const
		{
			return static_cast<CStr>(*this).endsWith(suffix);
		}

		/// \brief Byte-wise equality.
		bool operator==(CStr sv) const { return CStr(*this) == sv; }
		bool operator==(const String &o) const { return CStr(*this) == CStr(o); }

		// -- format & iteration -----------------------------------------------

		template <FormatBuffer Buffer>
		void format(Buffer &buffer) const { buffer.append(CStr(*this)); }

		char *begin() { return data(); }
		char *end() { return data() + len(); }
		const char *begin() const { return data(); }
		const char *end() const { return data() + len(); }
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
