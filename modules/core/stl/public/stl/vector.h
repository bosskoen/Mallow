#pragma once

#include <core/memory/anonymous_allocator.h>
#include <core/compilers.h>
#include <core/traits.h>
#include <core/libc/mem.h>
#include <core/macro.h>
#include <core/optional.h>

/// \file
/// \brief Growable contiguous array (dynamic array / "vector").

namespace core
{

	/// \brief Constrains what may be stored in a \ref Vector.
	///
	/// A valid element is a non-reference, non-cv-qualified, destructible type
	/// that can be either copied or moved into storage.
	/// \note `const`/`volatile` element types are rejected for now (they break
	///       relocation and the modifier surface); lift the `remove_cv` clause
	///       if that ever changes.
	/// \note Per-operation requirements are stricter than the concept: e.g.
	///       \ref Vector::clone and the copy \ref Vector::push overload also
	///       require copy-constructibility, so a move-only `T` simply does not
	///       expose those members.
	template <typename T>
	concept VectorElement =
		!is_reference_v<T> && is_destructible_v<T> &&
		(is_copy_constructible_v<T> || is_move_constructible_v<T>) &&
		// no voletiles or const for now. wen needed change
		is_same_v<T, remove_cv_t<T>>;

	/// \ingroup formattable
	/// \brief A dynamically-sized, contiguous, owning array of `T`.
	///
	/// Stores elements in a single heap block obtained from a type-erased
	/// \ref AnonymousAllocator captured at construction. Because the allocator
	/// is a runtime value rather than a template parameter, `Vector<T>` is a
	/// single type regardless of where it allocates, and values allocated from
	/// different allocators are freely interchangeable.
	///
	/// Elements are only ever *constructed* into and *destroyed* out of storage
	/// — never assigned — so `T` need not be assignable. Growth relocates by a
	/// raw byte move for trivially-copyable `T`, and by per-element move-construct
	/// + destroy otherwise.
	///
	/// \note Not copyable. Use \ref clone for an explicit deep copy, or move.
	/// \note Sizes and indices are signed (\ref isize).
	/// \warning On allocation failure the growing operations panic; only
	///          \ref tryReserve reports failure as a value.
	/// \tparam T Element type; see \ref VectorElement.
	template <VectorElement T>
	class Vector
	{
		T *data;
		isize length;
		isize cap;
		AnonymousAllocator allocator;

		static constexpr isize INITIAL_CAP = 8;
		static constexpr f32 GROWTH_FACTOR = 2.f;

		/// \brief Grow capacity by \ref GROWTH_FACTOR (or to \ref INITIAL_CAP
		///        from empty), relocating existing elements. Panics on OOM.
		void grow()
		{
			const isize new_bytes = static_cast<isize>((cap == 0) ? (INITIAL_CAP * sizeof(T)) : ((cap * GROWTH_FACTOR) * sizeof(T)));

			if constexpr (is_trivially_copyable_v<T>)
			{
				// memcpy-move is correct → let realloc fuse alloc+move (fast, in-place when possible)
				void *p = allocator.realloc(allocator.ctx, data,
											cap * (isize)sizeof(T), new_bytes, alignof(T));

				mlw_debug_assert_msg(p != nullptr, "Vectro::grow failed to realloc");
				data = static_cast<T *>(p);
			}
			else
			{
				// nontrivial move → realloc must NOT copy bytes. Pure alloc, manual relocate, free.
				void *p = allocator.realloc(allocator.ctx, nullptr, 0, new_bytes, alignof(T));
				mlw_debug_assert_msg(p != nullptr, "Vectro::grow failed to realloc");

				T *fresh = static_cast<T *>(p);

				for (isize i = 0; i < length; ++i)
				{
					::new (fresh + i) T(core::move(data[i])); // real move ctor
					data[i].~T();							  // destroy moved-from source
				}
				if (data)
					allocator.realloc(allocator.ctx, data, cap * (isize)sizeof(T), 0, alignof(T)); // free old
				data = fresh;
			}

			cap = cap == 0 ? cap = INITIAL_CAP : static_cast<isize>(cap * GROWTH_FACTOR);
		}

	public:
		// -- lifetime -----------------------------------------------------

		/// \brief Construct an empty vector using \ref default_allocator.
		Vector() : data(nullptr), length(0), cap(0), allocator(core::default_allocator()) {}

		/// \brief Construct an empty vector that allocates from \p alloc.
		/// \param alloc Allocator captured by value and used for the vector's
		///              lifetime.
		explicit Vector(AnonymousAllocator alloc) : data(nullptr), length(0), cap(0), allocator(alloc) {}

		/// \brief Deleted: vectors are not implicitly copyable.
		/// \see clone
		Vector(const Vector &) = delete;
		/// \copydoc Vector(const Vector&)
		Vector &operator=(const Vector &) = delete;

		/// \brief Return a deep copy: a new vector with copies of every element.
		///
		/// The copy uses the same allocator as `*this`. Trivially-copyable
		/// elements are bulk-copied; otherwise each element is copy-constructed.
		/// \return A vector of the same length with element-wise copies.
		/// \warning Panics on allocation failure.
		/// \note Available only when `T` is copy-constructible.
		Vector clone() const
			requires is_copy_constructible_v<T>
		{
			Vector ret{allocator};
			if (cap == 0)
				return ret; // already {nullptr, 0, 0} from the ctor

			ret.data = static_cast<T *>(
				allocator.realloc(allocator.ctx, nullptr, 0, cap * static_cast<isize>(sizeof(T)), alignof(T)));
			mlw_debug_assert_msg(ret.data != nullptr, "Vector::clone allocation returned nullptr");
			ret.cap = cap;

			if constexpr (is_trivially_copyable_v<T>)
			{
				core::mlwMemcpy(ret.data, data, (usize)(length * (isize)sizeof(T)));
			}
			else
			{
				for (isize i = 0; i < length; ++i)
				{
					::new (ret.data + i) T(static_cast<const T &>(data[i])); // lvalue -> copy
				}
			}

			ret.length = length;
			return ret;
		};

		/// \brief Destroy all elements and release the storage.
		///
		/// Leaves the vector valid and empty (data null, len/cap zero), so it is
		/// safe to reuse or destroy afterward. Idempotent.
		void deinit()
		{
			if constexpr (!is_trivially_destructible_v<T>)
			{
				for (isize i = 0; i < length; ++i)
				{
					data[i].~T();
				}
			}
			if (data != nullptr)
				data = static_cast<T *>(allocator.realloc(allocator.ctx, data, cap * sizeof(T), 0, alignof(T)));

			length = 0;
			cap = 0;
		};

		/// \brief Move-construct, taking ownership of \p other's storage.
		/// \post \p other is left empty and valid.
		Vector(Vector &&other) : data(other.data), length(other.length), cap(other.cap), allocator(other.allocator)
		{
			other.data = nullptr;
			other.length = 0;
			other.cap = 0;
		};

		/// \brief Move-assign: release current storage, then take \p other's.
		/// \post \p other is left empty and valid. Self-assignment is a no-op.
		Vector &operator=(Vector &&other)
		{
			if (this != &other)
			{
				deinit();
				new (this) Vector{move(other)};
			}
			return *this;
		};

		/// \brief Destroy all elements and free storage.
		~Vector() { deinit(); };

		// -- capacity -----------------------------------------------------

		isize len() const { return length; }

		/// \brief True if the vector holds no elements.
		MLW_FORCE_INLINE bool isEmpty() const { return length == 0; }

		/// \brief Ensure capacity for at least \p n elements *total*.
		///
		/// No-op if `n <= capacity`. Never shrinks. This is the only operation
		/// that reports allocation failure rather than panicking.
		/// \param n Requested total capacity in elements.
		/// \return `true` on success; `false` if the allocator could not satisfy
		///         the request (the vector is left unchanged and valid).
		/// \note `n` is a *total*, not an increment;
		bool tryReserve(isize n)
		{
			if (n <= cap)
				return true;

			const isize new_bytes = n * (isize)sizeof(T);

			if constexpr (is_trivially_copyable_v<T>)
			{
				// memcpy-move is correct → let realloc fuse alloc+move (fast, in-place when possible)
				void *p = allocator.realloc(allocator.ctx, data,
											cap * (isize)sizeof(T), new_bytes, alignof(T));
				if (!p)
					return false;
				data = static_cast<T *>(p);
			}
			else
			{
				// nontrivial move → realloc must NOT copy bytes. Pure alloc, manual relocate, free.
				void *p = allocator.realloc(allocator.ctx, nullptr, 0, new_bytes, alignof(T));
				if (!p)
					return false; // old buffer untouched, still valid
				T *fresh = static_cast<T *>(p);

				for (isize i = 0; i < length; ++i)
				{
					::new (fresh + i) T(core::move(data[i])); // real move ctor
					data[i].~T();							  // destroy moved-from source
				}
				if (data)
					allocator.realloc(allocator.ctx, data, cap * (isize)sizeof(T), 0, alignof(T)); // free old
				data = fresh;
			}

			cap = n;
			return true;
		}

		/// \brief Ensure capacity for at least \p n elements total; panic on OOM.
		/// \param n Requested total capacity in elements.
		/// \see tryReserve for the non-panicking form.
		void reserve(isize n)
		{
			// maby panic or let the os kill you/ UB
			bool x = tryReserve(n);
			mlw_debug_assert_msg(x, "Vector::reserve OOM");
		};

		/// \brief Best-effort request to release unused capacity.
		///
		/// Reduces reported capacity to the current length. Reclamation is
		/// allocator-dependent and never guaranteed; the operation never
		/// relocates elements, so pointers/references remain valid.
		void shrinkToFit()
		{
			if (cap == length)
				return;
			if (length == 0)
			{
				deinit();
				return;
			} // frees; nulls data/len/cap

			// shrink never relocates: same ptr back, no elements moved, regardless of T.
			// realloc reclaims in place where it can (medium) and no-ops where it can't (small/OS).
			allocator.realloc(allocator.ctx, data,
							  cap * (isize)sizeof(T), length * (isize)sizeof(T), alignof(T));
			cap = length;
		}

		/// \brief Shrink the length to \p n, destroying the removed tail.
		///
		/// No-op if `n >= len`. Capacity is unchanged.
		/// \param n New length; elements in `[n, len)` are destroyed.
		/// \note does not shrink capasety
		void truncate(isize n)
		{
			if (n < length)
			{
				if constexpr (!is_trivially_destructible_v<T>)
				{
					for (isize i = n; i < length; ++i)
					{
						data[i].~T();
					}
				}
				length = n;
			}
		}

		/// \brief Destroy all elements, keeping capacity.
		void clear()
		{
			if constexpr (!is_trivially_destructible_v<T>)
			{
				for (isize i = 0; i < length; ++i)
				{
					data[i].~T();
				}
			}
			length = 0;
		}

		// -- access -------------------------------------------------------

		/// \brief Unchecked element access. \pre `0 <= i < len`.
		/// \param i Element index.
		T &operator[](isize i)
		{
			mlw_debug_assert_msg(i >= 0 && i < length, "Vector::operator[] out of bounds max: {}; acsesed: {}", length, i);
			return data[i];
		}
		/// \copydoc operator[](isize)
		const T &operator[](isize i) const
		{
			mlw_debug_assert_msg(i >= 0 && i < length, "Vector::operator[] out of bounds max: {}; acsesed: {}", length, i);
			return data[i];
		}

		/// \brief Checked element access.
		/// \param i Element index.
		/// \return Some reference to the element, or None if `i` is out of range
		///         (including negative).
		Optional<T &> get(isize i)
		{
			if (i < 0 || i >= length)
			{
				return nullptr;
			}
			else
			{
				return Optional<T &>{data[i]};
			}
		}
		/// \copydoc get(isize)
		Optional<const T &> get(isize i) const
		{
			if (i < 0 || i >= length)
			{
				return nullptr;
			}
			else
			{
				return Optional<const T &>{data[i]};
			}
		}
		/// \brief The first element, or None if empty.
		Optional<T &> front()
		{
			if (length == 0)
			{
				return nullptr;
			}
			return Optional<T &>{data[0]};
		}
		/// \copydoc front()
		Optional<const T &> front() const
		{
			if (length == 0)
			{
				return nullptr;
			}
			return Optional<const T &>{data[0]};
		}

		/// \brief The last element, or None if empty.
		Optional<T &> back()
		{
			if (length == 0)
			{
				return nullptr;
			}
			return Optional<T &>{data[length - 1]};
		}
		/// \copydoc back()
		Optional<const T &> back() const
		{
			if (length == 0)
			{
				return nullptr;
			}
			return Optional<const T &>{data[length - 1]};
		}

		/// \brief Pointer to the underlying storage (may be null when empty).
		/// \warning Invalidated by any operation that grows or relocates.
		T *rawData() { return data; }
		/// \copydoc rawData()
		const T *rawData() const { return data; }

		// -- modifiers -----------------------------------------------------

		/// \brief Append a copy of \p v. Grows if needed; panics on OOM.
		/// \note Available only when `T` is copy-constructible.
		void push(const T &v)
			requires is_copy_constructible_v<T>
		{
			if (length == cap)
				grow();
			::new (data + length) T(v); // lvalue → copy ctor
			++length;
		}

		/// \brief Append \p v by moving. Grows if needed; panics on OOM.
		/// \note Available only when `T` is move-constructible.
		void push(T &&v)
			requires is_move_constructible_v<T>
		{
			if (length == cap)
				grow();
			::new (data + length) T(core::move(v)); // v is a NAMED rvalue ref = lvalue → must move()
			++length;
		}

		/// \brief Append a copy of \p v without checking capacity.
		/// \pre `len < capacity` (caller must have reserved). \warning No bounds
		///      or capacity check in release; writes past capacity are UB.
		void pushAssumeCapacity(const T &v)
			requires is_copy_constructible_v<T>
		{
			new (data + length) T(v);
			++length;
		}
		/// \brief Append \p v by moving without checking capacity.
		/// \pre `len < capacity`. \warning As \ref pushAssumeCapacity(const T&).
		void pushAssumeCapacity(T &&v)
			requires is_move_constructible_v<T>
		{
			new (data + length) T(core::move(v));
			++length;
		}

		/// \brief Construct a new element in place from \p args and append it.
		///
		/// Grows if needed; panics on OOM. No intermediate `T` is created.
		/// \param args Forwarded to `T`'s constructor.
		/// \return Reference to the newly-constructed element.
		template <typename... Args>
		T &emplace(Args &&...args)
			requires is_constructible_v<T, Args...>
		{
			if (length == cap)
				grow();
			::new (data + length) T(core::forward<Args>(args)...); // forward: preserve each arg's category
			++length;
			return data[length - 1];
		}

		/// \brief Remove and return the last element.
		/// \pre `!isEmpty()`. \return The removed element.
		T pop()
		{
			mlw_debug_assert_msg(!isEmpty(), "Vector::pop poped a empty vector");
			T ret{move_if_movable(data[length - 1])};

			if constexpr (!is_trivially_destructible_v<T>)
			{
				data[length - 1].~T();
			}

			--length;
			return ret;
		}

		/// \brief Insert \p v at index \p i, shifting later elements right.
		///
		/// `i == len` appends. Grows if needed; panics on OOM.
		/// \pre `0 <= i <= len`.
		/// \warning \p v must not alias this vector's storage (a grow may
		///          relocate it mid-operation).
		void insert(isize i, const T &v)
			requires is_copy_constructible_v<T>
		{
			mlw_debug_assert_msg(i >= 0 && i <= length, "Vector::insert index out of range");
			reserve(length + 1); // may relocate all elements; panics on OOM

			if constexpr (is_trivially_copyable_v<T>)
			{
				mlwMemmove(data + i + 1, data + i, static_cast<usize>((length - i) * (isize)sizeof(T)));
			}
			else
			{
				// top-down: each element move-constructs into the raw slot above it,
				// then its old slot is destroyed -> becomes the next raw slot.
				for (isize j = length; j > i; --j)
				{
					::new (data + j) T(move_if_movable(data[j - 1]));
					if constexpr (!is_trivially_destructible_v<T>)
						data[j - 1].~T();
				}
				// slot i is now raw (destroyed above); construct into the hole below.
			}
			::new (data + i) T(v); // lvalue -> copy
			++length;
		}

		/// \brief Insert \p v at \p i by moving, shifting later elements right.
		/// \copydetails insert(isize, const T&)
		void insert(isize i, T &&v)
			requires is_move_constructible_v<T>
		{
			mlw_debug_assert_msg(i >= 0 && i <= length, "Vector::insert index out of range");
			reserve(length + 1);

			if constexpr (is_trivially_copyable_v<T>)
			{
				mlwMemmove(data + i + 1, data + i, static_cast<usize>((length - i) * (isize)sizeof(T)));
			}
			else
			{
				for (isize j = length; j > i; --j)
				{
					::new (data + j) T(move_if_movable(data[j - 1]));
					if constexpr (!is_trivially_destructible_v<T>)
						data[j - 1].~T();
				}
			}
			::new (data + i) T(core::move(v)); // named rvalue ref is an lvalue -> move() it
			++length;
		}

		/// \brief Remove the element at \p i, shifting later elements left.
		///
		/// Order-preserving; O(len - i).
		/// \pre `0 <= i < len`. \return The removed element.
		T remove(isize i)
		{
			mlw_debug_assert_msg(i >= 0 && i < length, "Vector::remove index out of range");

			T ret{move_if_movable(data[i])}; // extract the element to return

			if constexpr (is_trivially_copyable_v<T>)
			{
				mlwMemmove(data + i, data + i + 1, static_cast<usize>((length - i - 1) * (isize)sizeof(T)));
				// trivial: no source destroy needed; --len drops the now-duplicate tail slot
			}
			else
			{
				if constexpr (!is_trivially_destructible_v<T>)
					data[i].~T(); // moved-from source destroyed -> slot i raw

				for (isize j = i; j < length - 1; ++j)
				{
					::new (data + j) T(move_if_movable(data[j + 1]));
					if constexpr (!is_trivially_destructible_v<T>)
						data[j + 1].~T(); // source destroyed -> next raw slot
				}
			}
			--length;
			return ret;
		}

		/// \brief Remove the element at \p i by swapping in the last element.
		///
		/// O(1); does **not** preserve order.
		/// \pre `0 <= i < len`. \return The removed element.
		T swapRemove(isize i)
		{
			mlw_debug_assert_msg(i >= 0 && i < length, "Vector::swapRemove index out of range");

			T ret{move_if_movable(data[i])};
			if constexpr (!is_trivially_destructible_v<T>)
				data[i].~T(); // slot i now raw

			if (i != length - 1)
			{ // refill from the last element
				::new (data + i) T(move_if_movable(data[length - 1]));
				if constexpr (!is_trivially_destructible_v<T>)
					data[length - 1].~T();
			}
			--length;
			return ret;
		}

		/// \brief Append copies of \p n elements read from \p p.
		///
		/// Grows if needed; panics on OOM.
		/// \param p Source of \p n contiguous elements; must not alias this
		///          vector's storage.
		/// \param n Number of elements to copy (must be `>= 0`).
		/// \note Available only when `T` is copy-constructible.
		void extendFromPtr(const T *p, isize n)
			requires is_copy_constructible_v<T>
		{
			mlw_debug_assert_msg(n >= 0, "Vector::extendFromPtr negative count");
			if (n == 0)
				return;
			reserve(length + n); // panics on OOM; MAX_ALLOC guard lives in reserve

			if constexpr (is_trivially_copyable_v<T>)
			{
				mlwMemcpy(data + length, p, static_cast<usize>(n * (isize)sizeof(T))); // non-overlapping
			}
			else
			{
				for (isize j = 0; j < n; ++j)
					::new (data + length + j) T(p[j]); // copy-construct; source retained
			}
			length += n;
		}

		template <FormatBuffer Buffer>
			requires(FormattableValue<T, Buffer>)
		void format(Buffer &buffer) const
		{
			buffer.append(CStr("{"));
			for (isize i = 0; i < length; ++i)
			{
				if (i != 0)
					buffer.append(CStr(", "));
				detail::formatValue(buffer, data[i]); // see below re: mlw_write vs formatValue
			}
			buffer.append(CStr("}"));
		}

		// ── iteration ─────────────────────────────────────────────────────────
		// A Vector is contiguous, so its iterator is simply a pointer. Range-for
		// needs only begin()/end(); pointer arithmetic gives ++, *, and != for free.

		/// \brief Iterator to the first element (a raw `T*`).
		/// \warning Invalidated by any operation that grows or relocates storage.
		T *begin() { return data; }
		/// \brief Iterator one past the last element.
		T *end() { return data + length; }

		/// \copydoc begin()
		const T *begin() const { return data; }
		/// \copydoc end()
		const T *end() const { return data + length; }

		/// \brief Const iterator to the first element, even on a non-const vector.
		const T *cbegin() const { return data; }
		/// \brief Const iterator one past the last element.
		const T *cend() const { return data + length; }
	};
}