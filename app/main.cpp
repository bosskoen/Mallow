// Microbenchmark for core::Map / core::HashMap.
//
// Build it once per map you want to measure and compare the numbers:
//   cc ...                 hash_map_bench.cpp   # scalar core::Map (default)
//   cc ... -DBENCH_SIMD    hash_map_bench.cpp   # SIMD core::HashMap
// To A/B the remove change, just rebuild the scalar one after editing remove().
//
// Reports reference cycles per op (x86 rdtsc / arm64 cntvct). Absolute numbers
// are only meaningful relative to each other on the same machine; turbo and a
// non-invariant TSC add noise, so trust deltas, not absolutes. `println` is
// assumed in scope as in your other files.

 //#define BENCH_SIMD
// #define MLW_HASHMAP_PORTABLE

#if defined(BENCH_SIMD)
#	include <stl/map_swissTable.h>
template <typename K, typename V> using BenchMap = core::HashMap<K, V>;
static const char *BENCH_NAME = "SIMD HashMap";
#else
#	include <stl/map.h> // adjust to wherever you put the scalar map
template <typename K, typename V> using BenchMap = core::Map<K, V>;
static const char *BENCH_NAME = "scalar Map";
#endif

#include <core/compilers.h>
#include <core/memory/anonymous_allocator.h>

#if defined(MLW_MSVC) && (defined(MLW_X86) || defined(MLW_X64))
extern "C" unsigned __int64 __rdtsc();
#	pragma intrinsic(__rdtsc)
#endif

namespace
{
	MLW_FORCE_INLINE uint64 cycles()
	{
#if defined(MLW_X86) || defined(MLW_X64)
#	if defined(MLW_MSVC)
		return __rdtsc();
#	else
		return __builtin_ia32_rdtsc();
#	endif
#elif defined(MLW_ARM64)
		uint64 v;
		__asm__ volatile("mrs %0, cntvct_el0" : "=r"(v)); // fixed-freq counter, not core cycles
		return v;
#else
		return 0;
#endif
	}

	MLW_FORCE_INLINE uint64 min64(uint64 a, uint64 b) { return a < b ? a : b; }

	struct Rng
	{
		uint64 s;
		explicit Rng(uint64 seed) : s(seed ? seed : 0x9e3779b97f4a7c15ull) {}
		uint64 next()
		{
			s ^= s >> 12;
			s ^= s << 25;
			s ^= s >> 27;
			return s * 0x2545f4914f6cdd1dull;
		}
	};

	template <typename T>
	T *xalloc(isize n)
	{
		auto a = core::default_allocator();
		return static_cast<T *>(a.realloc(a.ctx, nullptr, 0, n * static_cast<isize>(sizeof(T)), alignof(T)));
	}
	template <typename T>
	void xfree(T *p, isize n)
	{
		auto a = core::default_allocator();
		a.realloc(a.ctx, p, n * static_cast<isize>(sizeof(T)), 0, alignof(T));
	}

	// ticks/op scaled by 1000 so you get ~3 fractional digits from integer math.
	void report(const char *phase, uint64 ticks, isize ops)
	{
		const uint64 milli = (ticks * 1000ull) / static_cast<uint64>(ops);
		println("  {}: {}.{} ticks/op  ({} total, {} ops)",
				phase, milli / 1000, milli % 1000, ticks, (int64)ops);
	}

	volatile uint64 g_sink = 0;

	void run()
	{
		constexpr isize N = 1 << 27; // ~1M live keys
		constexpr int REPS = 5;      // non-mutating phases
		constexpr int REPS_MUT = 3;  // rebuild-each-time phases

		Rng rng(0xBEEF);

		// distinct keys 0..N-1, shuffled; values are a hash of the key
		int32 *keys = xalloc<int32>(N);
		usize *vals = xalloc<usize>(N);
		int32 *live = xalloc<int32>(N);
		for (isize i = 0; i < N; ++i) keys[i] = static_cast<int32>(i);
		for (isize i = N - 1; i > 0; --i)
		{
			const isize j = static_cast<isize>(rng.next() % static_cast<uint64>(i + 1));
			const int32 t = keys[i]; keys[i] = keys[j]; keys[j] = t;
		}
		for (isize i = 0; i < N; ++i) vals[i] = rng.next();

		uint64 sink = 0;

		println("=== {} : N = {} ===", BENCH_NAME, (int64)N);

		// ---- INSERT into a fresh table (includes all growth/rehash) ----------
		{
			uint64 best = ~0ull;
			for (int r = 0; r < REPS_MUT; ++r)
			{
				BenchMap<int32, usize> m{};
				MLW_COMPILER_BARRIER();
				const uint64 t0 = cycles();
				for (isize i = 0; i < N; ++i)
					m.put(keys[i], vals[i]);
				const uint64 t1 = cycles();
				MLW_COMPILER_BARRIER();
				best = min64(best, t1 - t0);
				sink += static_cast<uint64>(m.len());
			}
			report("insert (grow)", best, N);
		}

		// persistent populated map for the read-only phases
		BenchMap<int32, usize> m{};
		for (isize i = 0; i < N; ++i)
			m.put(keys[i], vals[i]);

		// ---- LOOKUP, all hit -------------------------------------------------
		{
			uint64 best = ~0ull;
			for (int r = 0; r < REPS; ++r)
			{
				MLW_COMPILER_BARRIER();
				const uint64 t0 = cycles();
				for (isize i = 0; i < N; ++i)
					sink += m.get(keys[i]).unwrap();
				const uint64 t1 = cycles();
				MLW_COMPILER_BARRIER();
				best = min64(best, t1 - t0);
			}
			report("lookup hit", best, N);
		}

		// ---- LOOKUP, all miss (keys N..2N-1 are absent) ----------------------
		{
			uint64 best = ~0ull;
			for (int r = 0; r < REPS; ++r)
			{
				MLW_COMPILER_BARRIER();
				const uint64 t0 = cycles();
				for (isize i = 0; i < N; ++i)
					sink += m.contains(static_cast<int32>(i + N)) ? 1u : 0u;
				const uint64 t1 = cycles();
				MLW_COMPILER_BARRIER();
				best = min64(best, t1 - t0);
			}
			report("lookup miss", best, N);
		}

		// ---- ITERATE ---------------------------------------------------------
		{
			uint64 best = ~0ull;
			for (int r = 0; r < REPS; ++r)
			{
				MLW_COMPILER_BARRIER();
				const uint64 t0 = cycles();
				for (auto &e : m)
					sink += e.value;
				const uint64 t1 = cycles();
				MLW_COMPILER_BARRIER();
				best = min64(best, t1 - t0);
			}
			report("iterate", best, m.len());
		}

		// ---- CHURN: N rounds of (remove + insert), steady live set ~N --------
		// This is the tombstone-sensitivity test: dumb remove piles up
		// tombstones here, smart remove should not.
		{
			for (isize i = 0; i < N; ++i)
				live[i] = keys[i];
			int32 nextKey = static_cast<int32>(N);

			MLW_COMPILER_BARRIER();
			const uint64 t0 = cycles();
			for (isize r = 0; r < N; ++r)
			{
				const isize j = static_cast<isize>(rng.next() % static_cast<uint64>(N));
				m.remove(live[j]);
				const int32 nk = nextKey++;
				m.put(nk, static_cast<usize>(nk) * 2654435761ull);
				live[j] = nk;
			}
			const uint64 t1 = cycles();
			MLW_COMPILER_BARRIER();
			report("churn (rm+ins)", t1 - t0, N);
		}

		// ---- LOOKUP hit AFTER churn: the number to watch for remove changes --
		{
			uint64 best = ~0ull;
			for (int r = 0; r < REPS; ++r)
			{
				MLW_COMPILER_BARRIER();
				const uint64 t0 = cycles();
				for (isize i = 0; i < N; ++i)
					sink += m.get(live[i]).unwrap();
				const uint64 t1 = cycles();
				MLW_COMPILER_BARRIER();
				best = min64(best, t1 - t0);
			}
			report("hit post-churn", best, N);
		}

		// ---- ERASE everything ------------------------------------------------
		{
			MLW_COMPILER_BARRIER();
			const uint64 t0 = cycles();
			for (isize i = 0; i < N; ++i)
				m.remove(live[i]);
			const uint64 t1 = cycles();
			MLW_COMPILER_BARRIER();
			report("erase all", t1 - t0, N);
		}

		g_sink = sink;
		xfree(keys, N);
		xfree(vals, N);
		xfree(live, N);
	}
} // namespace

int32 mallowMain()
{
	run();
	println("sink = {}", (uint64)g_sink);
	return 0;
}