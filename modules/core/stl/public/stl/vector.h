#pragma once

#include <core/memory/anonymous_allocator.h>
#include <core/compilers.h>
#include <core/traits.h>
#include <core/libc/mem.h>
#include <core/macro.h>
#include <core/optional.h>
#include <core/span.h>

/// \file
/// \brief Growable contiguous array (dynamic array / "vector").

namespace core
{
	/// \brief Constrains what may be stored in a \ref Vector.
	///
	/// A non-reference, non-cv-qualified, destructible type that can be copied
	/// or moved into storage. Individual members add their own requirements
	/// (e.g. \ref Vector::clone needs copy-construction), so a move-only `T`
	/// simply doesn't expose the copying members.
	template <typename T>
	concept VectorElement =
		!is_reference_v<T> && is_destructible_v<T> &&
		(is_copy_constructible_v<T> || is_move_constructible_v<T>) &&
		is_same_v<T, remove_cv_t<T>>;

	/// \ingroup formattable
	/// \brief A dynamically-sized, contiguous, owning array of `T`.
	///
	/// Storage comes from a type-erased \ref AnonymousAllocator captured at
	/// construction, so `Vector<T>` is one type regardless of where it allocates.
	///
	/// \par Shared container vocabulary
	///   `Vector` and \ref String expose the same names with the same meaning:
	///   `len / isEmpty / capacity / reserve / tryReserve / shrinkToFit / clear /
	///   truncate / resize`, `operator[] / get / front / back / data / slice`,
	///   `push / pop / append / insert / remove / removeRange`, `find / contains`,
	///   and an implicit view conversion (\ref Span here, \ref CStr for String).
	///
	/// \par Growth
	///   Operations that grow implicitly (`push`, `append`, `insert`, `resize`)
	///   use geometric growth: `max(needed, 2 * capacity, INITIAL_CAP)`. Only
	///   \ref reserve and \ref tryReserve allocate an exact amount.
	///
	/// \par Aliasing
	///   Arguments may refer to this vector's own elements: `v.push(v[0])`,
	///   `v.insert(0, v.back().unwrap())`, `v.append(v)` and
	///   `v.resize(n, v[0])` are all safe, even when they trigger a reallocation.
	///
	/// Elements are only ever constructed and destroyed in storage, never
	/// assigned, so `T` need not be assignable.
	///
	/// \note Not copyable; use \ref clone, or move.
	/// \note Sizes and indices are signed (\ref isize).
	/// \warning Growing operations panic on OOM; only \ref tryReserve reports it.
	template <VectorElement T>
	class Vector
	{
		static constexpr isize INITIAL_CAP = 8;
		static constexpr bool TRIVIAL = is_trivially_copyable_v<T>;

		T *ptr;
		isize length;
		isize cap;
		const AnonymousAllocator *alloc;

		// -- storage primitives -----------------------------------------------
		// Everything below the public line is built from these five, so the
		// trivial/non-trivial split lives in exactly one place each.

		static constexpr isize bytes(isize n) { return n * static_cast<isize>(sizeof(T)); }

		T *allocate(isize n) const
		{
			return static_cast<T *>(alloc->realloc(alloc, nullptr, 0, bytes(n), alignof(T)));
		}

		void release(T *p, isize n) const
		{
			if (p)
				alloc->realloc(alloc, p, bytes(n), 0, alignof(T));
		}

		/// Destroy `[p, p + n)`.
		static void destroy(T *p, isize n)
		{
			if constexpr (!is_trivially_destructible_v<T>)
				for (isize i = 0; i < n; ++i)
					p[i].~T();
		}

		/// Move `n` elements from `src` into raw, non-overlapping `dst`; sources are destroyed.
		static void relocate(T *dst, T *src, isize n)
		{
			if constexpr (TRIVIAL)
			{
				if (n > 0)
					mlwMemcpy(dst, src, static_cast<usize>(bytes(n)));
			}
			else
			{
				for (isize i = 0; i < n; ++i)
				{
					::new (dst + i) T(move_if_movable(src[i]));
					src[i].~T();
				}
			}
		}

		/// Shift `[from, length)` by `by` slots. The destination slots must be raw
		/// (beyond `length` when `by > 0`, already destroyed when `by < 0`). Does
		/// not change `length`.
		void shiftTail(isize from, isize by)
		{
			const isize n = length - from;
			if (n <= 0 || by == 0)
				return;
			if constexpr (TRIVIAL)
			{
				mlwMemmove(ptr + from + by, ptr + from, static_cast<usize>(bytes(n)));
			}
			else if (by > 0) // top-down so each source is vacated before it's a destination
			{
				for (isize j = length - 1; j >= from; --j)
				{
					::new (ptr + j + by) T(move_if_movable(ptr[j]));
					ptr[j].~T();
				}
			}
			else // bottom-up, same reason
			{
				for (isize j = from; j < length; ++j)
				{
					::new (ptr + j + by) T(move_if_movable(ptr[j]));
					ptr[j].~T();
				}
			}
		}

		/// Capacity to grow to so that `need` elements fit.
		isize nextCapacity(isize need) const
		{
			isize c = cap * 2;
			if (c < INITIAL_CAP)
				c = INITIAL_CAP;
			return c < need ? need : c;
		}

		/// Ensure room for `need` elements, growing geometrically. Panics on OOM.
		void growFor(isize need)
		{
			if (need > cap)
				reserve(nextCapacity(need));
		}

		/// True if `p` points into the live elements (so a grow would invalidate it).
		bool ownsPtr(const T *p) const
		{
			return ptr != nullptr && reinterpret_cast<uptr>(p) >= reinterpret_cast<uptr>(ptr) &&
				   reinterpret_cast<uptr>(p) < reinterpret_cast<uptr>(ptr + length);
		}

		/// Slow path of \ref emplace: the new element is built *before* the old
		/// block is released, so `args` may refer to existing elements.
		template <typename... Args>
		T &emplaceGrow(Args &&...args)
		{
			const isize nc = nextCapacity(length + 1);
			if constexpr (TRIVIAL)
			{
				T tmp(core::forward<Args>(args)...); // free for trivial T; lets realloc grow in place
				reserve(nc);
				::new (ptr + length) T(move_if_movable(tmp)); // trivial T may still be move-only
			}
			else
			{
				T *fresh = allocate(nc);
				mlw_debug_assert_msg(fresh != nullptr, "Vector: out of memory");
				::new (fresh + length) T(core::forward<Args>(args)...);
				relocate(fresh, ptr, length);
				release(ptr, cap);
				ptr = fresh;
				cap = nc;
			}
			return ptr[length++];
		}

		void takeFrom(Vector &o)
		{
			ptr = o.ptr;
			length = o.length;
			cap = o.cap;
			alloc = o.alloc;
			o.ptr = nullptr;
			o.length = 0;
			o.cap = 0;
		}

	public:
		// -- lifetime ---------------------------------------------------------

		/// \brief Empty vector on \ref default_allocator. Does not allocate.
		Vector() : Vector(&core::default_allocator()) {}

		/// \brief Empty vector that allocates from \p allocator. Does not allocate.
		explicit Vector(const AnonymousAllocator *allocator) : ptr(nullptr), length(0), cap(0), alloc(allocator) {}

		Vector(const Vector &) = delete;
		Vector &operator=(const Vector &) = delete;

		/// \brief Take \p other's storage. \post \p other is empty and valid.
		Vector(Vector &&other) { takeFrom(other); }

		/// \brief Free ours, take \p other's. Self-assignment is a no-op.
		Vector &operator=(Vector &&other)
		{
			if (this != &other)
			{
				deinit();
				takeFrom(other);
			}
			return *this;
		}

		~Vector() { deinit(); }

		/// \brief Destroy all elements and free storage. Idempotent; the vector
		///        stays valid and reusable on the same allocator.
		void deinit()
		{
			clear();
			release(ptr, cap);
			ptr = nullptr;
			cap = 0;
		}

		/// \brief Deep copy (element-wise copy) on the same allocator.
		Vector clone() const
			requires is_copy_constructible_v<T>
		{
			Vector out{alloc};
			out.append(*this);
			return out;
		}

		/// \brief The allocator this vector was built with.
		const AnonymousAllocator *allocator() const { return alloc; }

		// -- size & capacity --------------------------------------------------

		isize len() const { return length; }
		MLW_FORCE_INLINE bool isEmpty() const { return length == 0; }
		/// \brief Elements that fit before the next reallocation.
		isize capacity() const { return cap; }

		/// \brief Ensure capacity for at least \p n elements in total (exact, no
		///        growth factor). Never shrinks.
		/// \return `false` on OOM, leaving the vector unchanged.
		bool tryReserve(isize n)
		{
			if (n <= cap)
				return true;
			if constexpr (TRIVIAL)
			{
				// byte-moving is valid: let realloc grow in place when it can
				void *p = alloc->realloc(alloc, ptr, bytes(cap), bytes(n), alignof(T));
				if (!p)
					return false;
				ptr = static_cast<T *>(p);
			}
			else
			{
				T *fresh = allocate(n);
				if (!fresh)
					return false;
				relocate(fresh, ptr, length);
				release(ptr, cap);
				ptr = fresh;
			}
			cap = n;
			return true;
		}

		/// \brief As \ref tryReserve, but panics on OOM.
		void reserve(isize n)
		{
			const bool ok = tryReserve(n);
			mlw_debug_assert_msg(ok, "Vector: out of memory");
		}

		/// \brief Drop unused capacity. Never relocates (pointers stay valid);
		///        actual reclamation is up to the allocator.
		void shrinkToFit()
		{
			if (cap == length)
				return;
			if (length == 0)
				return deinit();
			alloc->realloc(alloc, ptr, bytes(cap), bytes(length), alignof(T));
			cap = length;
		}

		/// \brief Destroy every element; capacity is kept.
		void clear() { truncate(0); }

		/// \brief Shrink to \p n elements, destroying the tail. No-op if `n >= len`.
		void truncate(isize n)
		{
			mlw_debug_assert_msg(n >= 0, "Vector::truncate negative length");
			if (n >= length)
				return;
			destroy(ptr + n, length - n);
			length = n;
		}

		/// \brief Resize to \p n; new elements are default-constructed.
		void resize(isize n)
			requires is_default_constructible_v<T>
		{
			if (n <= length)
				return truncate(n);
			growFor(n);
			while (length < n)
				::new (ptr + length++) T();
		}

		/// \brief Resize to \p n; new elements are copies of \p value.
		void resize(isize n, const T &value)
			requires is_copy_constructible_v<T>
		{
			if (n <= length)
				return truncate(n);
			if (n > cap && ownsPtr(&value)) // value is ours and about to move
			{
				const T fill(value);
				return resize(n, fill); // fill isn't ours, so this takes the fast path
			}
			growFor(n);
			while (length < n)
				::new (ptr + length++) T(value);
		}

		// -- access -----------------------------------------------------------

		/// \brief Element access, bounds-checked in debug builds only.
		T &operator[](isize i)
		{
			mlw_debug_assert_msg(i >= 0 && i < length, "Vector index {} out of range (len {})", i, length);
			return ptr[i];
		}
		const T &operator[](isize i) const
		{
			mlw_debug_assert_msg(i >= 0 && i < length, "Vector index {} out of range (len {})", i, length);
			return ptr[i];
		}

		/// \brief Checked access: None if \p i is out of range (including negative).
		Optional<T &> get(isize i)
		{
			if (i < 0 || i >= length)
				return nullptr;
			return Optional<T &>{ptr[i]};
		}
		Optional<const T &> get(isize i) const
		{
			if (i < 0 || i >= length)
				return nullptr;
			return Optional<const T &>{ptr[i]};
		}

		/// \brief First / last element, or None when empty.
		Optional<T &> front() { return get(0); }
		Optional<const T &> front() const { return get(0); }
		Optional<T &> back() { return get(length - 1); }
		Optional<const T &> back() const { return get(length - 1); }

		/// \brief Pointer to the elements (null while nothing is allocated).
		/// \warning Invalidated by anything that grows.
		T *data() { return ptr; }
		const T *data() const { return ptr; }

		/// \brief Non-owning view of all elements. \warning Invalidated by growth.
		operator Span<T>() { return Span<T>(ptr, length); }
		operator Span<const T>() const { return Span<const T>(ptr, length); }

		/// \brief Non-owning view of `[start, start + count)`.
		Span<T> slice(isize start, isize count)
		{
			mlw_debug_assert_msg(start >= 0 && count >= 0 && start + count <= length, "Vector::slice out of range");
			return Span<T>(ptr + start, count);
		}
		Span<const T> slice(isize start, isize count) const
		{
			mlw_debug_assert_msg(start >= 0 && count >= 0 && start + count <= length, "Vector::slice out of range");
			return Span<const T>(ptr + start, count);
		}

		// -- modifiers --------------------------------------------------------

		/// \brief Construct an element in place at the back. Returns a reference to it.
		template <typename... Args>
		T &emplace(Args &&...args)
			requires is_constructible_v<T, Args...>
		{
			if (length == cap) [[unlikely]]
				return emplaceGrow(core::forward<Args>(args)...);
			::new (ptr + length) T(core::forward<Args>(args)...);
			return ptr[length++];
		}

		void push(const T &v)
			requires is_copy_constructible_v<T>
		{
			emplace(v);
		}
		void push(T &&v)
			requires is_move_constructible_v<T>
		{
			emplace(core::move(v));
		}

		/// \brief \ref push without the capacity check. \pre `len() < capacity()`.
		void pushAssumeCapacity(const T &v)
			requires is_copy_constructible_v<T>
		{
			mlw_debug_assert_msg(length < cap, "Vector::pushAssumeCapacity without capacity");
			::new (ptr + length++) T(v);
		}
		void pushAssumeCapacity(T &&v)
			requires is_move_constructible_v<T>
		{
			mlw_debug_assert_msg(length < cap, "Vector::pushAssumeCapacity without capacity");
			::new (ptr + length++) T(core::move(v));
		}

		/// \brief Append copies of every element of \p items (may be a view of this vector).
		void append(Span<const T> items)
			requires is_copy_constructible_v<T>
		{
			const isize n = items.len;
			if (n <= 0)
				return;
			if (ownsPtr(items.ptr)) // self-append: re-find the source after a possible move
			{
				const isize off = items.ptr - ptr;
				growFor(length + n);
				items.ptr = ptr + off;
			}
			else
			{
				growFor(length + n);
			}
			if constexpr (TRIVIAL)
				mlwMemcpy(ptr + length, items.ptr, static_cast<usize>(bytes(n)));
			else
				for (isize i = 0; i < n; ++i)
					::new (ptr + length + i) T(items.ptr[i]);
			length += n;
		}

		/// \brief Construct an element in place at index \p i, shifting later ones right.
		/// \pre `0 <= i <= len()`. `i == len()` appends.
		template <typename... Args>
		T &emplaceAt(isize i, Args &&...args)
			requires is_constructible_v<T, Args...>
		{
			mlw_debug_assert_msg(i >= 0 && i <= length, "Vector::insert index {} out of range (len {})", i, length);
			if (i == length)
				return emplace(core::forward<Args>(args)...);
			T tmp(core::forward<Args>(args)...); // built first: args may refer to elements we shift
			growFor(length + 1);
			shiftTail(i, 1);
			::new (ptr + i) T(move_if_movable(tmp));
			++length;
			return ptr[i];
		}

		void insert(isize i, const T &v)
			requires is_copy_constructible_v<T>
		{
			emplaceAt(i, v);
		}
		void insert(isize i, T &&v)
			requires is_move_constructible_v<T>
		{
			emplaceAt(i, core::move(v));
		}

		/// \brief Remove and return the last element. \pre `!isEmpty()`.
		T pop()
		{
			mlw_debug_assert_msg(length > 0, "Vector::pop on empty vector");
			T out(move_if_movable(ptr[length - 1]));
			destroy(ptr + --length, 1);
			return out;
		}

		/// \brief Remove and return element \p i, keeping order. O(len - i).
		T remove(isize i)
		{
			mlw_debug_assert_msg(i >= 0 && i < length, "Vector::remove index {} out of range (len {})", i, length);
			T out(move_if_movable(ptr[i]));
			destroy(ptr + i, 1);
			shiftTail(i + 1, -1);
			--length;
			return out;
		}

		/// \brief Remove and return element \p i by moving the last one into its
		///        place. O(1); does **not** keep order.
		T swapRemove(isize i)
		{
			mlw_debug_assert_msg(i >= 0 && i < length, "Vector::swapRemove index {} out of range (len {})", i, length);
			T out(move_if_movable(ptr[i]));
			destroy(ptr + i, 1);
			if (i != --length)
			{
				::new (ptr + i) T(move_if_movable(ptr[length]));
				destroy(ptr + length, 1);
			}
			return out;
		}

		/// \brief Destroy `[start, start + count)`, shifting later elements left.
		void removeRange(isize start, isize count)
		{
			mlw_debug_assert_msg(start >= 0 && count >= 0 && start + count <= length, "Vector::removeRange out of range");
			destroy(ptr + start, count);
			shiftTail(start + count, -count);
			length -= count;
		}

		// -- search & compare -------------------------------------------------

		/// \brief Index of the first element equal to \p value, or None.
		Optional<isize> find(const T &value) const
			requires requires(const T &a) { a == a; }
		{
			for (isize i = 0; i < length; ++i)
				if (ptr[i] == value)
					return Optional<isize>{i};
			return nullptr;
		}

		bool contains(const T &value) const
			requires requires(const T &a) { a == a; }
		{
			return find(value).isSome();
		}

		/// \brief Same length and pairwise-equal elements.
		bool operator==(const Vector &o) const
			requires requires(const T &a) { a == a; }
		{
			if (length != o.length)
				return false;
			for (isize i = 0; i < length; ++i)
				if (!(ptr[i] == o.ptr[i]))
					return false;
			return true;
		}

		// -- format -----------------------------------------------------------

		/// \brief Formats as `{a, b, c}`.
		template <FormatBuffer Buffer>
			requires(FormattableValue<T, Buffer>)
		void format(Buffer &buffer) const
		{
			buffer.append('{');
			for (isize i = 0; i < length; ++i)
			{
				if (i != 0)
					buffer.append(CStr(", "));
				detail::formatValue(buffer, ptr[i]);
			}
			buffer.append('}');
		}

		// -- iteration --------------------------------------------------------
		// Contiguous, so the iterator is a plain pointer. Invalidated by growth.

		T *begin() { return ptr; }
		T *end() { return ptr + length; }
		const T *begin() const { return ptr; }
		const T *end() const { return ptr + length; }
		const T *cbegin() const { return ptr; }
		const T *cend() const { return ptr + length; }

		/// \brief Consuming iterator: owns the vector and yields each element by move.
		struct Drainer
		{
			Vector v;
			isize index = 0;
			using Item = T;

			explicit Drainer(Vector &&src) : v(core::move(src)) {}
			Drainer(Drainer &&) = default;
			Drainer(const Drainer &) = delete;

			Optional<T> next()
			{
				if (index >= v.len())
					return {};
				return Optional<T>{optional_in_place, move_if_movable(v.ptr[index++])};
			}
			// ~Vector destroys the (moved-from) elements and frees the block.
		};

		/// \brief Turn the vector into a \ref Drainer (`core::move(v).drain()`).
		Drainer drain() && { return Drainer(core::move(*this)); }
	};
} // namespace core
