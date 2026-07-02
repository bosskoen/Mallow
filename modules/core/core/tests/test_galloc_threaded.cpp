// GAlloc cross-thread correctness and benchmark tests
// Uses OS thread APIs directly — no std::thread or std::atomic.
// Adjust the namespace to match your module layout.

#include "core/memory/galloc.h"
#include <cstdio>
#include <cstdlib>

#if defined(MLW_WINDOWS)
#include <windows.h>
#elif defined(MLW_LINUX) || defined(MLW_MAC)
#include <pthread.h>
#include <time.h>
#include <unistd.h>
#endif

using namespace core;

namespace core_core_test
{

// ============================================================================
//  Minimal thread wrapper
// ============================================================================

struct Thread {
#if defined(MLW_WINDOWS)
	HANDLE handle = nullptr;
#else
	pthread_t handle{};
#endif
	void (*fn)(void*) = nullptr;
	void* arg = nullptr;

#if defined(MLW_WINDOWS)
	static DWORD WINAPI entry(LPVOID p) {
		Thread* t = static_cast<Thread*>(p);
		t->fn(t->arg);
		return 0;
	}
#else
	static void* entry(void* p) {
		Thread* t = static_cast<Thread*>(p);
		t->fn(t->arg);
		return nullptr;
	}
#endif

	void start() {
#if defined(MLW_WINDOWS)
		handle = CreateThread(nullptr, 0, entry, this, 0, nullptr);
#else
		pthread_create(&handle, nullptr, entry, this);
#endif
	}

	void join() {
#if defined(MLW_WINDOWS)
		WaitForSingleObject(handle, INFINITE);
		CloseHandle(handle);
		handle = nullptr;
#else
		pthread_join(handle, nullptr);
#endif
	}
};

static int hw_threads() {
#if defined(MLW_WINDOWS)
	SYSTEM_INFO si;
	GetSystemInfo(&si);
	int n = static_cast<int>(si.dwNumberOfProcessors);
#else
	int n = static_cast<int>(sysconf(_SC_NPROCESSORS_ONLN));
#endif
	return n < 2 ? 2 : n;
}

static uint64 now_ns() {
#if defined(MLW_LINUX) || defined(MLW_MAC)
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return static_cast<uint64>(ts.tv_sec) * 1000000000ULL + static_cast<uint64>(ts.tv_nsec);
#elif defined(MLW_WINDOWS)
	static LARGE_INTEGER freq = []{ LARGE_INTEGER f; QueryPerformanceFrequency(&f); return f; }();
	LARGE_INTEGER count;
	QueryPerformanceCounter(&count);
	return static_cast<uint64>(static_cast<double>(count.QuadPart) / freq.QuadPart * 1e9);
#endif
}

static volatile void* bench_sink;

// ============================================================================
//  Helpers
// ============================================================================

static void fill(void* p, usize size, uint8 pattern) {
	uint8* b = static_cast<uint8*>(p);
	for (usize i = 0; i < size; ++i) b[i] = static_cast<uint8>(pattern + (i & 0xFF));
}

static bool verify(void* p, usize size, uint8 pattern) {
	uint8* b = static_cast<uint8*>(p);
	for (usize i = 0; i < size; ++i) {
		if (b[i] != static_cast<uint8>(pattern + (i & 0xFF))) return false;
	}
	return true;
}

// ============================================================================
//  Cross-thread free — small
// ============================================================================

bool test_cross_thread_free_small() {
	constexpr int COUNT = 10000;
	constexpr usize SIZE = 32;

	struct Ctx {
		void* ptrs[10000];
	} ctx;

	for (int i = 0; i < COUNT; ++i) {
		ctx.ptrs[i] = mlw_g_alloc.alloc(SIZE);
		if (!ctx.ptrs[i]) return false;
		fill(ctx.ptrs[i], SIZE, static_cast<uint8>(i));
	}

	for (int i = 0; i < COUNT; ++i) {
		if (!verify(ctx.ptrs[i], SIZE, static_cast<uint8>(i))) return false;
	}

	Thread t;
	t.fn = [](void* arg) {
		Ctx* c = static_cast<Ctx*>(arg);
		for (int i = 0; i < 10000; ++i) {
			mlw_g_alloc.free(c->ptrs[i]);
		}
	};
	t.arg = &ctx;
	t.start();
	t.join();

	void* probe = mlw_g_alloc.alloc(SIZE);
	if (!probe) return false;
	mlw_g_alloc.free(probe);

	return true;
}

// ============================================================================
//  Cross-thread free — medium
// ============================================================================

bool test_cross_thread_free_medium() {
	constexpr int COUNT = 1000;
	constexpr usize SIZE = 512;

	struct Ctx {
		void* ptrs[1000];
	} ctx;

	for (int i = 0; i < COUNT; ++i) {
		ctx.ptrs[i] = mlw_g_alloc.alloc(SIZE);
		if (!ctx.ptrs[i]) return false;
		fill(ctx.ptrs[i], SIZE, static_cast<uint8>(i));
	}

	for (int i = 0; i < COUNT; ++i) {
		if (!verify(ctx.ptrs[i], SIZE, static_cast<uint8>(i))) return false;
	}

	Thread t;
	t.fn = [](void* arg) {
		Ctx* c = static_cast<Ctx*>(arg);
		for (int i = 0; i < 1000; ++i) {
			mlw_g_alloc.free(c->ptrs[i]);
		}
	};
	t.arg = &ctx;
	t.start();
	t.join();

	void* probe = mlw_g_alloc.alloc(SIZE);
	if (!probe) return false;
	mlw_g_alloc.free(probe);

	return true;
}

// ============================================================================
//  Concurrent alloc/free — same size class
// ============================================================================

bool test_concurrent_alloc_free() {
	constexpr int COUNT = 50000;
	constexpr usize SIZE = 64;

	struct Ctx {
		sync::Atomic<bool> failure{false};
		uint8 pattern;
	};

	Ctx ctx_a; ctx_a.pattern = 0xAA;
	Ctx ctx_b; ctx_b.pattern = 0xBB;

	auto worker = [](void* arg) {
		Ctx* c = static_cast<Ctx*>(arg);
		for (int i = 0; i < 50000; ++i) {
			void* p = mlw_g_alloc.alloc(64);
			if (!p) { c->failure.store(true, sync::MemoryOrder::Relaxed); return; }
			fill(p, 64, c->pattern);
			if (!verify(p, 64, c->pattern)) { c->failure.store(true, sync::MemoryOrder::Relaxed); return; }
			mlw_g_alloc.free(p);
		}
	};

	Thread t1; t1.fn = worker; t1.arg = &ctx_a;
	Thread t2; t2.fn = worker; t2.arg = &ctx_b;
	t1.start();
	t2.start();
	t1.join();
	t2.join();

	return !ctx_a.failure.load(sync::MemoryOrder::Relaxed)
		&& !ctx_b.failure.load(sync::MemoryOrder::Relaxed);
}

// ============================================================================
//  Concurrent alloc/free — mixed sizes
// ============================================================================

bool test_concurrent_mixed_sizes() {
	constexpr int COUNT = 20000;

	struct Ctx {
		sync::Atomic<bool> failure{false};
		usize size;
		uint8 pattern;
	};

	Ctx ctxs[4];
	ctxs[0].size = 8;    ctxs[0].pattern = 0x11;
	ctxs[1].size = 64;   ctxs[1].pattern = 0x22;
	ctxs[2].size = 256;  ctxs[2].pattern = 0x33;
	ctxs[3].size = 4096; ctxs[3].pattern = 0x44;

	auto worker = [](void* arg) {
		Ctx* c = static_cast<Ctx*>(arg);
		for (int i = 0; i < 20000; ++i) {
			void* p = mlw_g_alloc.alloc(c->size);
			if (!p) { c->failure.store(true, sync::MemoryOrder::Relaxed); return; }
			fill(p, c->size, c->pattern);
			if (!verify(p, c->size, c->pattern)) { c->failure.store(true, sync::MemoryOrder::Relaxed); return; }
			mlw_g_alloc.free(p);
		}
	};

	Thread threads[4];
	for (int i = 0; i < 4; ++i) {
		threads[i].fn = worker;
		threads[i].arg = &ctxs[i];
		threads[i].start();
	}
	for (int i = 0; i < 4; ++i) threads[i].join();

	for (int i = 0; i < 4; ++i) {
		if (ctxs[i].failure.load(sync::MemoryOrder::Relaxed)) return false;
	}
	return true;
}

// ============================================================================
//  Producer-consumer
// ============================================================================

bool test_producer_consumer() {
	constexpr int COUNT = 100000;
	constexpr int RING_SIZE = 1024;
	constexpr usize SIZE = 128;

	struct Ctx {
		void* ring[1024];
		sync::Atomic<int> write_idx{0};
		sync::Atomic<int> read_idx{0};
		sync::Atomic<bool> producer_done{false};
		sync::Atomic<bool> failure{false};
	} ctx;

	Thread producer;
	producer.fn = [](void* arg) {
		Ctx* c = static_cast<Ctx*>(arg);
		for (int i = 0; i < 100000; ++i) {
			while (c->write_idx.load(sync::MemoryOrder::Acquire) -
			       c->read_idx.load(sync::MemoryOrder::Acquire) >= 1023) {}
			void* p = mlw_g_alloc.alloc(128);
			if (!p) { c->failure.store(true, sync::MemoryOrder::Relaxed); return; }
			fill(p, 128, static_cast<uint8>(i));
			int w = c->write_idx.load(sync::MemoryOrder::Relaxed);
			c->ring[w % 1024] = p;
			c->write_idx.store(w + 1, sync::MemoryOrder::Release);
		}
		c->producer_done.store(true, sync::MemoryOrder::Release);
	};
	producer.arg = &ctx;

	Thread consumer;
	consumer.fn = [](void* arg) {
		Ctx* c = static_cast<Ctx*>(arg);
		int i = 0;
		while (true) {
			int w = c->write_idx.load(sync::MemoryOrder::Acquire);
			int r = c->read_idx.load(sync::MemoryOrder::Relaxed);
			if (r >= w) {
				if (c->producer_done.load(sync::MemoryOrder::Acquire) &&
				    r >= c->write_idx.load(sync::MemoryOrder::Acquire)) break;
				continue;
			}
			void* p = c->ring[r % 1024];
			if (!verify(p, 128, static_cast<uint8>(i))) {
				c->failure.store(true, sync::MemoryOrder::Relaxed);
				mlw_g_alloc.free(p);
				return;
			}
			mlw_g_alloc.free(p);
			c->read_idx.store(r + 1, sync::MemoryOrder::Release);
			++i;
		}
	};
	consumer.arg = &ctx;

	producer.start();
	consumer.start();
	producer.join();
	consumer.join();

	return !ctx.failure.load(sync::MemoryOrder::Relaxed);
}

// ============================================================================
//  Thread exit — orphan migration
// ============================================================================

bool test_thread_exit_orphan_migration() {
	constexpr int COUNT = 500;
	constexpr usize SIZE = 256;

	struct Ctx {
		void* ptrs[500];
	} ctx;

	Thread t;
	t.fn = [](void* arg) {
		Ctx* c = static_cast<Ctx*>(arg);
		for (int i = 0; i < 500; ++i) {
			c->ptrs[i] = mlw_g_alloc.alloc(256);
			if (c->ptrs[i]) fill(c->ptrs[i], 256, static_cast<uint8>(i));
		}
	};
	t.arg = &ctx;
	t.start();
	t.join();

	for (int i = 0; i < COUNT; ++i) {
		if (!ctx.ptrs[i]) return false;
		if (!verify(ctx.ptrs[i], SIZE, static_cast<uint8>(i))) return false;
	}

	for (int i = 0; i < COUNT; ++i) {
		mlw_g_alloc.free(ctx.ptrs[i]);
	}

	void* probe = mlw_g_alloc.alloc(SIZE);
	if (!probe) return false;
	fill(probe, SIZE, 0xFF);
	if (!verify(probe, SIZE, 0xFF)) { mlw_g_alloc.free(probe); return false; }
	mlw_g_alloc.free(probe);

	return true;
}

// ============================================================================
//  Thread exit — small allocations
// ============================================================================

bool test_thread_exit_orphan_small() {
	constexpr int COUNT = 2000;

	struct Ctx {
		void* ptrs[2000];
		usize sizes[2000];
	} ctx;

	Thread t;
	t.fn = [](void* arg) {
		Ctx* c = static_cast<Ctx*>(arg);
		for (int i = 0; i < 2000; ++i) {
			usize s;
			switch (i % 4) {
				case 0: s = 8; break;
				case 1: s = 32; break;
				case 2: s = 64; break;
				default: s = 128; break;
			}
			c->sizes[i] = s;
			c->ptrs[i] = mlw_g_alloc.alloc(s);
			if (c->ptrs[i]) fill(c->ptrs[i], s, static_cast<uint8>(i));
		}
	};
	t.arg = &ctx;
	t.start();
	t.join();

	for (int i = 0; i < COUNT; ++i) {
		if (!ctx.ptrs[i]) return false;
		if (!verify(ctx.ptrs[i], ctx.sizes[i], static_cast<uint8>(i))) return false;
	}
	for (int i = 0; i < COUNT; ++i) {
		mlw_g_alloc.free(ctx.ptrs[i]);
	}

	return true;
}

// ============================================================================
//  Multiple threads exiting
// ============================================================================

bool test_many_threads_exit() {
	constexpr int THREAD_COUNT = 16;
	constexpr int ALLOCS_PER_THREAD = 200;
	constexpr usize SIZE = 64;

	struct Ctx {
		void* ptrs[200];
		int id;
	} ctxs[16];

	for (int ti = 0; ti < THREAD_COUNT; ++ti) {
		ctxs[ti].id = ti;
		Thread t;
		t.fn = [](void* arg) {
			Ctx* c = static_cast<Ctx*>(arg);
			for (int i = 0; i < 200; ++i) {
				c->ptrs[i] = mlw_g_alloc.alloc(64);
				if (c->ptrs[i]) fill(c->ptrs[i], 64, static_cast<uint8>(c->id + i));
			}
		};
		t.arg = &ctxs[ti];
		t.start();
		t.join();
	}

	for (int ti = 0; ti < THREAD_COUNT; ++ti) {
		for (int i = 0; i < ALLOCS_PER_THREAD; ++i) {
			if (!ctxs[ti].ptrs[i]) return false;
			if (!verify(ctxs[ti].ptrs[i], SIZE, static_cast<uint8>(ti + i))) return false;
		}
	}

	for (int ti = 0; ti < THREAD_COUNT; ++ti) {
		for (int i = 0; i < ALLOCS_PER_THREAD; ++i) {
			mlw_g_alloc.free(ctxs[ti].ptrs[i]);
		}
	}

	return true;
}

// ============================================================================
//  Stress — concurrent alloc + cross-thread free
// ============================================================================

bool test_stress_cross_thread() {
	constexpr int THREAD_COUNT = 4;
	constexpr int COUNT = 10000;
	constexpr usize SIZE = 128;

	struct SharedCtx {
		void* ptrs[4][10000];
		sync::Atomic<int> phase{0};
		sync::Atomic<bool> failure{false};
	} shared;

	struct WorkerCtx {
		SharedCtx* shared;
		int id;
	} wctxs[4];

	auto worker = [](void* arg) {
		WorkerCtx* wc = static_cast<WorkerCtx*>(arg);
		SharedCtx* s = wc->shared;
		int id = wc->id;

		// phase 1: allocate
		for (int i = 0; i < 10000; ++i) {
			s->ptrs[id][i] = mlw_g_alloc.alloc(128);
			if (!s->ptrs[id][i]) { s->failure.store(true, sync::MemoryOrder::Relaxed); return; }
			fill(s->ptrs[id][i], 128, static_cast<uint8>(id * 37 + i));
		}

		s->phase.fetchAdd(1, sync::MemoryOrder::AcqRel);
		while (s->phase.load(sync::MemoryOrder::Acquire) < 4) {}

		// phase 2: verify own, free other thread's first half
		for (int i = 0; i < 10000; ++i) {
			if (!verify(s->ptrs[id][i], 128, static_cast<uint8>(id * 37 + i))) {
				s->failure.store(true, sync::MemoryOrder::Relaxed);
				return;
			}
		}

		int target = (id + 1) % 4;
		for (int i = 0; i < 5000; ++i) {
			mlw_g_alloc.free(s->ptrs[target][i]);
		}

		s->phase.fetchAdd(1, sync::MemoryOrder::AcqRel);
		while (s->phase.load(sync::MemoryOrder::Acquire) < 8) {}

		// phase 3: free own second half
		for (int i = 5000; i < 10000; ++i) {
			mlw_g_alloc.free(s->ptrs[id][i]);
		}
	};

	for (int i = 0; i < THREAD_COUNT; ++i) {
		wctxs[i].shared = &shared;
		wctxs[i].id = i;
	}

	Thread threads[4];
	for (int i = 0; i < THREAD_COUNT; ++i) {
		threads[i].fn = worker;
		threads[i].arg = &wctxs[i];
		threads[i].start();
	}
	for (int i = 0; i < THREAD_COUNT; ++i) {
		threads[i].join();
	}

	void* probe = mlw_g_alloc.alloc(SIZE);
	if (probe) mlw_g_alloc.free(probe);

	return !shared.failure.load(sync::MemoryOrder::Relaxed);
}

// ============================================================================
//  Benchmarks — galloc vs malloc threaded
// ============================================================================

struct CycleBenchCtx {
	usize size;
	int count;
	bool use_galloc;
};

static void cycle_bench_worker(void* arg) {
	CycleBenchCtx* c = static_cast<CycleBenchCtx*>(arg);
	if (c->use_galloc) {
		for (int i = 0; i < c->count; ++i) {
			void* p = mlw_g_alloc.alloc(c->size);
			bench_sink = p;
			mlw_g_alloc.free(p);
		}
	} else {
		for (int i = 0; i < c->count; ++i) {
			void* p = ::malloc(c->size);
			bench_sink = p;
			::free(p);
		}
	}
}

static void run_threaded_cycle_bench(const char* label, usize size, int per_thread, int num_threads) {
	Thread* threads = static_cast<Thread*>(::malloc(num_threads * sizeof(Thread)));
	CycleBenchCtx* ctxs = static_cast<CycleBenchCtx*>(::malloc(num_threads * sizeof(CycleBenchCtx)));

	// galloc
	for (int i = 0; i < num_threads; ++i) {
		ctxs[i] = {size, per_thread, true};
		threads[i].fn = cycle_bench_worker;
		threads[i].arg = &ctxs[i];
	}
	uint64 t0 = now_ns();
	for (int i = 0; i < num_threads; ++i) threads[i].start();
	for (int i = 0; i < num_threads; ++i) threads[i].join();
	uint64 t1 = now_ns();

	// malloc
	for (int i = 0; i < num_threads; ++i) {
		ctxs[i] = {size, per_thread, false};
		threads[i].fn = cycle_bench_worker;
		threads[i].arg = &ctxs[i];
	}
	uint64 t2 = now_ns();
	for (int i = 0; i < num_threads; ++i) threads[i].start();
	for (int i = 0; i < num_threads; ++i) threads[i].join();
	uint64 t3 = now_ns();

	uint64 total_ops = static_cast<uint64>(per_thread) * num_threads;
	printf("    %-24s  galloc: %4llu ns/op    malloc: %4llu ns/op\n",
		label,
		(unsigned long long)(t1 - t0) / total_ops,
		(unsigned long long)(t3 - t2) / total_ops);

	::free(threads);
	::free(ctxs);
}

struct BulkBenchCtx {
	usize size;
	int count;
	bool use_galloc;
};

static void bulk_bench_worker(void* arg) {
	BulkBenchCtx* c = static_cast<BulkBenchCtx*>(arg);
	void** ptrs = static_cast<void**>(::malloc(c->count * sizeof(void*)));
	if (c->use_galloc) {
		for (int i = 0; i < c->count; ++i) ptrs[i] = mlw_g_alloc.alloc(c->size);
		for (int i = 0; i < c->count; ++i) mlw_g_alloc.free(ptrs[i]);
	} else {
		for (int i = 0; i < c->count; ++i) ptrs[i] = ::malloc(c->size);
		for (int i = 0; i < c->count; ++i) ::free(ptrs[i]);
	}
	::free(ptrs);
}

static void run_threaded_bulk_bench(const char* label, usize size, int per_thread, int num_threads) {
	Thread* threads = static_cast<Thread*>(::malloc(num_threads * sizeof(Thread)));
	BulkBenchCtx* ctxs = static_cast<BulkBenchCtx*>(::malloc(num_threads * sizeof(BulkBenchCtx)));

	// galloc
	for (int i = 0; i < num_threads; ++i) {
		ctxs[i] = {size, per_thread, true};
		threads[i].fn = bulk_bench_worker;
		threads[i].arg = &ctxs[i];
	}
	uint64 t0 = now_ns();
	for (int i = 0; i < num_threads; ++i) threads[i].start();
	for (int i = 0; i < num_threads; ++i) threads[i].join();
	uint64 t1 = now_ns();

	// malloc
	for (int i = 0; i < num_threads; ++i) {
		ctxs[i] = {size, per_thread, false};
		threads[i].fn = bulk_bench_worker;
		threads[i].arg = &ctxs[i];
	}
	uint64 t2 = now_ns();
	for (int i = 0; i < num_threads; ++i) threads[i].start();
	for (int i = 0; i < num_threads; ++i) threads[i].join();
	uint64 t3 = now_ns();

	uint64 total_ops = static_cast<uint64>(per_thread) * num_threads;
	printf("    %-24s  galloc: %4llu ns/op    malloc: %4llu ns/op\n",
		label,
		(unsigned long long)(t1 - t0) / total_ops,
		(unsigned long long)(t3 - t2) / total_ops);

	::free(threads);
	::free(ctxs);
}

static void run_cross_thread_bench(const char* label, usize size, int count) {
	constexpr int RING_SIZE = 4096;

	// --- galloc ---
	struct GCtx {
		void* ring[4096];
		sync::Atomic<int> w{0};
		sync::Atomic<int> r{0};
		sync::Atomic<bool> done{false};
		usize size;
		int count;
	} gctx;
	gctx.size = size;
	gctx.count = count;

	auto gprod = [](void* arg) {
		GCtx* c = static_cast<GCtx*>(arg);
		for (int i = 0; i < c->count; ++i) {
			while (c->w.load(sync::MemoryOrder::Acquire) -
			       c->r.load(sync::MemoryOrder::Acquire) >= 4095) {}
			int wi = c->w.load(sync::MemoryOrder::Relaxed);
			c->ring[wi % 4096] = mlw_g_alloc.alloc(c->size);
			c->w.store(wi + 1, sync::MemoryOrder::Release);
		}
		c->done.store(true, sync::MemoryOrder::Release);
	};
	auto gcons = [](void* arg) {
		GCtx* c = static_cast<GCtx*>(arg);
		while (true) {
			int w = c->w.load(sync::MemoryOrder::Acquire);
			int ri = c->r.load(sync::MemoryOrder::Relaxed);
			if (ri >= w) {
				if (c->done.load(sync::MemoryOrder::Acquire) &&
				    ri >= c->w.load(sync::MemoryOrder::Acquire)) break;
				continue;
			}
			mlw_g_alloc.free(c->ring[ri % 4096]);
			c->r.store(ri + 1, sync::MemoryOrder::Release);
		}
	};

	Thread gt1, gt2;
	gt1.fn = gprod; gt1.arg = &gctx;
	gt2.fn = gcons; gt2.arg = &gctx;
	uint64 t0 = now_ns();
	gt1.start(); gt2.start();
	gt1.join(); gt2.join();
	uint64 t1 = now_ns();

	// --- malloc ---
	struct MCtx {
		void* ring[4096];
		sync::Atomic<int> w{0};
		sync::Atomic<int> r{0};
		sync::Atomic<bool> done{false};
		usize size;
		int count;
	} mctx;
	mctx.size = size;
	mctx.count = count;

	auto mprod = [](void* arg) {
		MCtx* c = static_cast<MCtx*>(arg);
		for (int i = 0; i < c->count; ++i) {
			while (c->w.load(sync::MemoryOrder::Acquire) -
			       c->r.load(sync::MemoryOrder::Acquire) >= 4095) {}
			int wi = c->w.load(sync::MemoryOrder::Relaxed);
			c->ring[wi % 4096] = ::malloc(c->size);
			c->w.store(wi + 1, sync::MemoryOrder::Release);
		}
		c->done.store(true, sync::MemoryOrder::Release);
	};
	auto mcons = [](void* arg) {
		MCtx* c = static_cast<MCtx*>(arg);
		while (true) {
			int w = c->w.load(sync::MemoryOrder::Acquire);
			int ri = c->r.load(sync::MemoryOrder::Relaxed);
			if (ri >= w) {
				if (c->done.load(sync::MemoryOrder::Acquire) &&
				    ri >= c->w.load(sync::MemoryOrder::Acquire)) break;
				continue;
			}
			::free(c->ring[ri % 4096]);
			c->r.store(ri + 1, sync::MemoryOrder::Release);
		}
	};

	Thread mt1, mt2;
	mt1.fn = mprod; mt1.arg = &mctx;
	mt2.fn = mcons; mt2.arg = &mctx;
	uint64 t2 = now_ns();
	mt1.start(); mt2.start();
	mt1.join(); mt2.join();
	uint64 t3 = now_ns();

	printf("    %-24s  galloc: %4llu ns/op    malloc: %4llu ns/op\n",
		label,
		(unsigned long long)(t1 - t0) / count,
		(unsigned long long)(t3 - t2) / count);
}

bool test_bench_threaded_cycle() {
	int hw = hw_threads();
	printf("\n    --- threaded alloc/free cycle (%d threads, 100000 each) ---\n", hw);
	run_threaded_cycle_bench("8 byte",   8,   100000, hw);
	run_threaded_cycle_bench("64 byte",  64,  100000, hw);
	run_threaded_cycle_bench("128 byte", 128, 100000, hw);
	run_threaded_cycle_bench("256 byte", 256, 100000, hw);
	run_threaded_cycle_bench("1 KB",     1024, 100000, hw);
	run_threaded_cycle_bench("4 KB",     4096, 100000, hw);
	return true;
}

bool test_bench_threaded_bulk() {
	int hw = hw_threads();
	printf("\n    --- threaded bulk alloc then free (%d threads) ---\n", hw);
	run_threaded_bulk_bench("8 byte x10000",   8,   10000, hw);
	run_threaded_bulk_bench("64 byte x10000",  64,  10000, hw);
	run_threaded_bulk_bench("128 byte x10000", 128, 10000, hw);
	run_threaded_bulk_bench("256 byte x10000", 256, 10000, hw);
	run_threaded_bulk_bench("1 KB x5000",      1024, 5000, hw);
	run_threaded_bulk_bench("4 KB x2000",      4096, 2000, hw);
	return true;
}

bool test_bench_cross_thread_free() {
	printf("\n    --- producer-consumer cross-thread free ---\n");
	run_cross_thread_bench("64 byte x200000",  64,  200000);
	run_cross_thread_bench("256 byte x200000", 256, 200000);
	run_cross_thread_bench("1 KB x100000",     1024, 100000);
	return true;
}

} // namespace core_memory_test