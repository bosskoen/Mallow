#pragma once
// ── element types ─────────────────────────────────────────────────────

	// counts every lifecycle event so tests can assert exact copy/move/destroy.
	struct Tracker {
		static int ctors, copies, moves, dtors, live;
		int value;
		explicit Tracker(int v = 0) : value(v) { ++ctors; ++live; }
		Tracker(const Tracker& o) : value(o.value) { ++copies; ++live; }
		Tracker(Tracker&& o) noexcept : value(o.value) { o.value = -1; ++moves; ++live; }
		~Tracker() { --live; ++dtors; }
		Tracker& operator=(const Tracker&) = delete;   // vector must never assign
		Tracker& operator=(Tracker&&) = delete;
		static void reset() { ctors = copies = moves = dtors = live = 0; }
    };

	// move-only, non-trivial: exercises the move-only relocation paths.
	struct MoveOnly {
		int value;
		explicit MoveOnly(int v = 0) : value(v) {}
		MoveOnly(const MoveOnly&) = delete;
		MoveOnly& operator=(const MoveOnly&) = delete;
		MoveOnly(MoveOnly&& o) noexcept : value(o.value) { o.value = -1; }
		MoveOnly& operator=(MoveOnly&&) = delete;
		~MoveOnly() = default;
	};

    #define TCHECK(cond) do { if (!(cond)) return false; } while (0)