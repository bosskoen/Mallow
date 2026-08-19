#pragma once
namespace core{
	/// \brief splitmix64 finalizer: strong avalanche, cheap. Used to scramble
	///        raw key bits so H1/H2 are well distributed even for sequential keys.
	MLW_FORCE_INLINE uint64 mix64(uint64 x)
	{
		x ^= x >> 30;
		x *= 0xbf58476d1ce4e5b9ull;
		x ^= x >> 27;
		x *= 0x94d049bb133111ebull;
		x ^= x >> 31;
		return x;
	}

	/// \brief Customization point. Specialize `Hash<T>` to use `T` as a key.
	///        Integers, bools and pointers work out of the box; the unspecialized
	///        template is a hard error so an unhashable key fails at compile time.
	template <typename T>
	struct Hash
	{
		static_assert(sizeof(T) == 0,
					  "No Hash<T> for this key type: add a core::Hash<T> specialization.");
	};

	template <typename T>
		requires(is_integer_v<T> || is_bool_v<T>)
	struct Hash<T>
	{
		usize operator()(T v) const
		{
			if constexpr (sizeof(T) > 8)
			{
				const uint64 lo = static_cast<uint64>(v);
				const uint64 hi = static_cast<uint64>(v >> 64);
				return static_cast<usize>(mix64(lo ^ mix64(hi)));
			}
			else
			{
				return static_cast<usize>(mix64(static_cast<uint64>(v)));
			}
		}
	};

	template <typename T>
		requires is_pointer_v<T>
	struct Hash<T>
	{
		usize operator()(T v) const
		{
			return static_cast<usize>(mix64(static_cast<uint64>(reinterpret_cast<uptr>(v))));
		}
	};
}