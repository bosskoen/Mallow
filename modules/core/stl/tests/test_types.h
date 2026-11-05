#pragma once
#include <core/memory/anonymous_allocator.h>

// ── element types ─────────────────────────────────────────────────────

// counts every lifecycle event so tests can assert exact copy/move/destroy.
struct Tracker
{
	static int ctors, copies, moves, dtors, live;
	int value;
	explicit Tracker(int v = 0) : value(v) { ++ctors; ++live; }
	Tracker(const Tracker &o) : value(o.value) { ++copies; ++live; }
	Tracker(Tracker &&o) noexcept : value(o.value) { o.value = -1; ++moves; ++live; }
	~Tracker() { --live; ++dtors; }
	Tracker &operator=(const Tracker &) = delete; // containers must never assign
	Tracker &operator=(Tracker &&) = delete;
	bool operator==(const Tracker &o) const { return value == o.value; }
	static void reset() { ctors = copies = moves = dtors = live = 0; }
};

// move-only, non-trivial: exercises the move-only relocation paths.
struct MoveOnly
{
	int value;
	explicit MoveOnly(int v = 0) : value(v) {}
	MoveOnly(const MoveOnly &) = delete;
	MoveOnly &operator=(const MoveOnly &) = delete;
	MoveOnly(MoveOnly &&o) noexcept : value(o.value) { o.value = -1; }
	MoveOnly &operator=(MoveOnly &&) = delete;
	~MoveOnly() = default;
};

// move-only but TRIVIALLY copyable (deleted copy, defaulted move): takes the
// fast memcpy paths, yet must never be copy-constructed.
struct TrivialMoveOnly
{
	int value;
	explicit TrivialMoveOnly(int v = 0) : value(v) {}
	TrivialMoveOnly(const TrivialMoveOnly &) = delete;
	TrivialMoveOnly(TrivialMoveOnly &&) = default;
	TrivialMoveOnly &operator=(TrivialMoveOnly &&) = default;
};

// ── allocators ────────────────────────────────────────────────────────

// Fails every request: exercises tryReserve's failure path, and proves that
// operations which shouldn't allocate (empty containers, inline strings) don't.
inline const core::AnonymousAllocator FAILING_ALLOC{
	[](const core::AnonymousAllocator *, void *, isize, isize, isize) -> void * { return nullptr; },
	nullptr};

// Forwards to the default allocator. `requests` counts allocs/resizes; `live`
// counts blocks currently allocated, so a test can check for leaks itself.
// Pinned in place: `ctx` points at the object itself.
struct CountingAlloc
{
	core::AnonymousAllocator a{&forward, this};
	isize requests = 0;
	isize live = 0;

	CountingAlloc() = default;
	CountingAlloc(const CountingAlloc &) = delete;
	const core::AnonymousAllocator *get() const { return &a; }

	static void *forward(const core::AnonymousAllocator *self, void *p, isize o, isize n, isize align)
	{
		CountingAlloc *c = static_cast<CountingAlloc *>(self->ctx);
		const core::AnonymousAllocator &d = core::default_allocator();
		void *r = d.realloc(&d, p, o, n, align);
		if (n != 0)
			++c->requests;
		if (!p && r)
			++c->live; // fresh block
		else if (p && n == 0)
			--c->live; // freed
		return r;
	}
};

#define TCHECK(cond)          \
	do                        \
	{                         \
		if (!(cond))          \
			return false;     \
	} while (0)
