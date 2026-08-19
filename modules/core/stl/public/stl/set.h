#pragma once
#include "stl/map.h"

/// \file
/// \brief Open-addressing hash set — a thin facade over \ref core::Map with an
///        empty value type. Inherits the map's SIMD/SWAR/scalar backend
///        selection wholesale; it adds no probing logic of its own.
///
/// \par Why a facade and not a second table
///   A set is a map whose values carry no information. Rather than duplicate the
///   SwissTable machinery, \ref core::Set stores `Map<T, EmptySetType>` and
///   forwards to it. There is exactly one table implementation to maintain (two
///   counting the scalar fallback); the set can never drift from the map because
///   it *is* the map.
///
/// \par Zero value overhead
///   `EmptySetType` is a genuinely empty type, and \ref core::Map::Entry marks
///   its `value` member with `[[no_unique_address]]`, so the empty value folds
///   into the key's storage: `sizeof(Entry) == sizeof(T)`. The set therefore
///   pays only for the keys, with no per-slot padding for a dummy value. The
///   \c static_assert below turns a toolchain that fails to honor the attribute
///   into a build error instead of silent 2x memory bloat.
///
/// \note Same contracts as \ref core::Map: not reference-stable (any \ref insert
///       that grows relocates every element), not copyable (use \ref clone),
///       movable. Iteration yields keys by `const&` only — a set never exposes a
///       mutable key, since mutating it would corrupt the slot it hashes to.

namespace core
{
	/// \brief A SwissTable hash set of `T`, built on \ref Map<T, EmptySetType>.
	///
	/// \tparam T Element type; needs `operator==` and a `core::Hash<T>` (i.e. it
	///           must satisfy \ref HashKey), exactly as a map key does.
	///
	/// Construct/lookup/erase all forward to the underlying map. See the file
	/// header for the facade rationale, the empty-value layout trick, and the
	/// reference-stability / copyability notes.
	template <HashKey T>
	class Set
	{
		/// Empty stand-in for the map's value type. Folds to zero size via the
		/// `[[no_unique_address]]` on \ref Map::Entry::value.
		struct EmptySetType {};

		/// Resolves to the SIMD map or the scalar fallback, whichever \ref map.h
		/// selected at compile time. The set is agnostic to which.
		using M = Map<T, EmptySetType>;
		M map;

		// Guards the empty-value layout trick: if any toolchain fails to collapse
		// the empty value into the key's storage, fail loudly at compile time
		// rather than silently doubling per-slot memory.
		static_assert(sizeof(typename M::Entry) == sizeof(T),
					  "Set slot bloated: no_unique_address not honored on this toolchain.");

	public:
		// -- lifetime ---------------------------------------------------------
		/// \brief Empty set backed by the default allocator.
		Set() = default;
		/// \brief Empty set backed by \p a.
		explicit Set(AnonymousAllocator a) : map(a) {}

		// -- insert -----------------------------------------------------------
		/// \brief Insert \p v if absent. \return true if newly inserted, false if
		///        \p v was already present. Single-probe (forwards to the map's
		///        insert-if-absent primitive); a false return performs no work and
		///        never grows the table.
		bool insert(const T &v) { return map.tryInsert(v, EmptySetType{}); }

		// -- lookup -----------------------------------------------------------
		/// \brief \return true if \p v is in the set.
		bool contains(const T &v) const { return map.contains(v); }
		/// \brief \return number of elements.
		isize len() const { return map.len(); }
		/// \brief \return true if the set has no elements.
		bool isEmpty() const { return map.isEmpty(); }

		// -- erase ------------------------------------------------------------
		/// \brief Remove \p v if present. \return true if something was removed.
		bool remove(const T &v) { return map.remove(v); }
		/// \brief Destroy all elements but keep the allocated capacity.
		void clear() { map.clear(); }

		// -- copy -------------------------------------------------------------
		/// \brief Deep copy. Available only when `T` is copyable.
		Set clone() const
			requires is_copy_constructible_v<T>
		{
			Set s;
			s.map = map.clone();
			return s;
		}

		// -- iteration (yields keys by const& for full slots only) ------------
		/// \brief Forward const iterator over set elements. Wraps the map's entry
		///        iterator and projects to the key; never yields a mutable key.
		struct ConstIterator
		{
			typename M::ConstIterator it;
			const T &operator*() const { return (*it).key; } // const& — never hand out mutable keys
			bool operator!=(const ConstIterator &o) const { return it != o.it; }
			ConstIterator &operator++()
			{
				++it;
				return *this;
			}
		};
		ConstIterator begin() const { return {map.begin()}; }
		ConstIterator end() const { return {map.end()}; }
	};
} // namespace core