#include "stl/vector.h"

namespace core_stl_test
{

	using core::Vector;

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
	int Tracker::ctors = 0, Tracker::copies = 0, Tracker::moves = 0,
		Tracker::dtors = 0, Tracker::live = 0;

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

	// ── lifetime ──────────────────────────────────────────────────────────

	bool test_vector_default_empty() {
		Vector<int> v;
		TCHECK(v.isEmpty());
		TCHECK(v.len() == 0);
		TCHECK(v.get(0).isNone());
		return true;
	}

	// deep copy: exact copies, matching values, independent buffer.
	// (catches the inverted `len == 0` clone guard: broken -> copies==0 + garbage.)
	bool test_vector_clone_deep_copy() {
		Tracker::reset();
		{
			Vector<Tracker> v;
			for (int i = 0; i < 3; ++i) v.push(Tracker{ i * 10 });
			int copies_before = Tracker::copies;

			Vector<Tracker> c = v.clone();
			TCHECK(Tracker::copies == copies_before + 3);   // exactly 3 element copies
			TCHECK(c.len() == 3);
			TCHECK(c.get(0).unwrap().value == 0);
			TCHECK(c.get(1).unwrap().value == 10);
			TCHECK(c.get(2).unwrap().value == 20);
			TCHECK(c.rawData() != v.rawData());             // separate storage

			// independence: mutating the clone does not touch the original
			c.get(0).unwrap().value = 999;
			TCHECK(v.get(0).unwrap().value == 0);
		}
		TCHECK(Tracker::live == 0);
		return true;
	}

	bool test_vector_clone_empty() {
		Vector<int> v;
		Vector<int> c = v.clone();
		TCHECK(c.isEmpty());
		TCHECK(c.len() == 0);
		return true;
	}

	// moving a Vector steals the buffer: no element moves/copies happen.
	bool test_vector_move_ctor_steals_buffer() {
		Tracker::reset();
		{
			Vector<Tracker> v;
			for (int i = 0; i < 3; ++i) v.push(Tracker{ i });
			int moves_before = Tracker::moves;
			int copies_before = Tracker::copies;

			Vector<Tracker> v2 = core::move(v);
			TCHECK(Tracker::moves == moves_before);    // buffer pointer stolen, elements untouched
			TCHECK(Tracker::copies == copies_before);
			TCHECK(v.isEmpty());                        // source emptied
			TCHECK(v2.len() == 3);
			TCHECK(v2.get(2).unwrap().value == 2);
		}
		TCHECK(Tracker::live == 0);
		return true;
	}

	// move-assign frees the destination's old elements, then steals the source.
	// (fails on the `if (this == &other)` / missing-return bug -> good.)
	bool test_vector_move_assign() {
		Tracker::reset();
		{
			Vector<Tracker> a;
			for (int i = 0; i < 2; ++i) a.push(Tracker{ i });   // a: 2 live
			Vector<Tracker> b;
			for (int i = 0; i < 3; ++i) b.push(Tracker{ 100 + i }); // b: 3 live
			TCHECK(Tracker::live == 5);

			a = core::move(b);
			TCHECK(Tracker::live == 3);                 // a's old 2 destroyed; b's 3 now in a
			TCHECK(b.isEmpty());
			TCHECK(a.len() == 3);
			TCHECK(a.get(0).unwrap().value == 100);
		}
		TCHECK(Tracker::live == 0);
		return true;
	}

	// self move-assign must be a safe no-op (needs the this != &other guard).
	bool test_vector_self_move_assign() {
		Vector<int> a;
		for (int i = 0; i < 3; ++i) a.push(i);
		a = core::move(a);
		TCHECK(a.len() == 3);
		TCHECK(a.get(2).unwrap() == 2);
		return true;
	}

	bool test_vector_destructor_no_leak() {
		Tracker::reset();
		{ Vector<Tracker> v; for (int i = 0; i < 5; ++i) v.push(Tracker{ i }); }
		TCHECK(Tracker::live == 0);
		TCHECK(Tracker::ctors + Tracker::copies + Tracker::moves == Tracker::dtors);
		return true;
	}

	// ── capacity ──────────────────────────────────────────────────────────

	// after reserve, pushing exactly N causes NO relocation (only N move-ins).
	bool test_vector_reserve_no_relocation() {
		Tracker::reset();
		{
			Vector<Tracker> v;
			v.reserve(16);
			for (int i = 0; i < 16; ++i) v.push(Tracker{ i });
			TCHECK(Tracker::moves == 16);               // 16 push-ins, 0 relocation moves
			TCHECK(Tracker::copies == 0);
			TCHECK(v.len() == 16);
		}
		TCHECK(Tracker::live == 0);
		return true;
	}

	bool test_vector_shrink_to_fit_keeps_contents() {
		Tracker::reset();
		{
			Vector<Tracker> v;
			for (int i = 0; i < 20; ++i) v.push(Tracker{ i });   // grows past its length
			int live_before = Tracker::live;
			v.shrinkToFit();
			TCHECK(Tracker::live == live_before);       // shrink never destroys elements
			TCHECK(v.len() == 20);
			TCHECK(v.get(19).unwrap().value == 19);
		}
		TCHECK(Tracker::live == 0);
		return true;
	}

	bool test_vector_truncate() {
		Tracker::reset();
		{
			Vector<Tracker> v;
			for (int i = 0; i < 5; ++i) v.push(Tracker{ i });
			v.truncate(2);
			TCHECK(Tracker::live == 2);                 // 3 tail elements destroyed
			TCHECK(v.len() == 2);
			v.truncate(10);                             // no-op when n >= len
			TCHECK(v.len() == 2);
		}
		TCHECK(Tracker::live == 0);
		return true;
	}

	bool test_vector_clear_keeps_capacity() {
		Tracker::reset();
		{
			Vector<Tracker> v;
			for (int i = 0; i < 5; ++i) v.push(Tracker{ i });
			v.clear();
			TCHECK(Tracker::live == 0);
			TCHECK(v.isEmpty());
			TCHECK(v.len() == 0);
			// capacity kept: refill 5 without any relocation moves
			int moves_before = Tracker::moves;
			for (int i = 0; i < 5; ++i) v.push(Tracker{ i });
			TCHECK(Tracker::moves == moves_before + 5); // exactly 5 move-ins, no growth
		}
		TCHECK(Tracker::live == 0);
		return true;
	}

	// ── access ────────────────────────────────────────────────────────────

	bool test_vector_index_and_get_bounds() {
		Vector<int> v;
		for (int i = 0; i < 4; ++i) v.push(i * 2);
		TCHECK(v[0] == 0);
		TCHECK(v[3] == 6);
		TCHECK(v.get(0).isSome() && v.get(0).unwrap() == 0);
		TCHECK(v.get(3).isSome());
		TCHECK(v.get(4).isNone());       // == len
		TCHECK(v.get(-1).isNone());      // negative (signed-index guard)
		return true;
	}

	// front()/back() must return REFERENCES, not copies.
	// (catches the Optional{data[0]} CTAD bug that deduces Optional<T> and copies.)
	bool test_vector_front_back_are_references() {
		Tracker::reset();
		{
			Vector<Tracker> v;
			for (int i = 0; i < 3; ++i) v.push(Tracker{ i });
			int copies_before = Tracker::copies;

			TCHECK(v.front().isSome());
			TCHECK(v.back().isSome());
			TCHECK(Tracker::copies == copies_before);   // referencing, not copying

			v.front().unwrap().value = 77;              // mutate through the reference
			v.back().unwrap().value = 88;
			TCHECK(v[0].value == 77);                   // change is visible in the vector
			TCHECK(v[2].value == 88);
		}
		TCHECK(Tracker::live == 0);
		return true;
	}

	bool test_vector_front_back_empty() {
		Vector<int> v;
		TCHECK(v.front().isNone());
		TCHECK(v.back().isNone());
		return true;
	}

	// ── modifiers ─────────────────────────────────────────────────────────

	bool test_vector_push_copy_vs_move() {
		Tracker::reset();
		{
			Vector<Tracker> v;
			Tracker lvalue{ 1 };
			v.push(lvalue);                 // lvalue -> copy
			v.push(Tracker{ 2 });             // rvalue -> move
			TCHECK(Tracker::copies == 1);
			TCHECK(Tracker::moves == 1);
		}
		TCHECK(Tracker::live == 0);
		return true;
	}

	bool test_vector_push_assume_capacity() {
		Tracker::reset();
		{
			Vector<Tracker> v;
			v.reserve(4);
			int moves_before = Tracker::moves;
			for (int i = 0; i < 4; ++i) v.pushAssumeCapacity(Tracker{ i });
			TCHECK(Tracker::moves == moves_before + 4);  // 4 move-ins, no grow
			TCHECK(v.len() == 4);
		}
		TCHECK(Tracker::live == 0);
		return true;
	}

	bool test_vector_emplace_constructs_in_place() {
		Tracker::reset();
		{
			Vector<Tracker> v;
			Tracker& ref = v.emplace(42);   // built directly in the slot
			TCHECK(Tracker::ctors == 1);
			TCHECK(Tracker::copies == 0);
			TCHECK(Tracker::moves == 0);    // no intermediate temporary
			TCHECK(ref.value == 42);
			TCHECK(v[0].value == 42);
		}
		TCHECK(Tracker::live == 0);
		return true;
	}

	bool test_vector_pop_single_move() {
		Tracker::reset();
		{
			Vector<Tracker> v;
			for (int i = 0; i < 3; ++i) v.push(Tracker{ i });
			int moves_before = Tracker::moves;
			Tracker popped = v.pop();
			TCHECK(popped.value == 2);
			TCHECK(Tracker::moves == moves_before + 1);  // exactly one move out
			TCHECK(Tracker::copies == 0);
			TCHECK(v.len() == 2);
		}
		TCHECK(Tracker::live == 0);
		return true;
	}

	bool test_vector_insert_preserves_order() {
		Vector<int> v;
		for (int i = 0; i < 4; ++i) v.push(i);   // 0 1 2 3
		v.insert(1, 99);                          // 0 99 1 2 3
		TCHECK(v.len() == 5);
		TCHECK(v[0] == 0);
		TCHECK(v[1] == 99);
		TCHECK(v[2] == 1);
		TCHECK(v[4] == 3);
		v.insert(5, 77);                          // insert at end (i == len)
		TCHECK(v[5] == 77);
		return true;
	}

	bool test_vector_insert_no_leak() {
		Tracker::reset();
		{
			Vector<Tracker> v;
			for (int i = 0; i < 4; ++i) v.push(Tracker{ i });
			v.insert(2, Tracker{ 99 });
			TCHECK(Tracker::live == 5);           // shift constructs/destroys balance out
			TCHECK(v[2].value == 99);
		}
		TCHECK(Tracker::live == 0);
		return true;
	}

	bool test_vector_remove_preserves_order() {
		Tracker::reset();
		{
			Vector<Tracker> v;
			for (int i = 0; i < 5; ++i) v.push(Tracker{ i });   // 0 1 2 3 4
			Tracker got = v.remove(1);                         // remove '1' -> 0 2 3 4
			TCHECK(got.value == 1);
			TCHECK(v.len() == 4);
			TCHECK(v[0].value == 0);
			TCHECK(v[1].value == 2);
			TCHECK(v[3].value == 4);
		}
		TCHECK(Tracker::live == 0);
		return true;
	}

	bool test_vector_swap_remove() {
		Vector<int> v;
		for (int i = 0; i < 5; ++i) v.push(i);   // 0 1 2 3 4
		int got = v.swapRemove(1);                // last (4) fills slot 1 -> 0 4 2 3
		TCHECK(got == 1);
		TCHECK(v.len() == 4);
		TCHECK(v[1] == 4);                         // order NOT preserved
		TCHECK(v[3] == 3);
		int last = v.swapRemove(3);               // remove last: no swap needed
		TCHECK(last == 3);
		TCHECK(v.len() == 3);
		return true;
	}

	bool test_vector_extend_from_ptr_copies() {
		Tracker::reset();
		{
			Tracker src[3] = { Tracker{10}, Tracker{20}, Tracker{30} };
			int copies_before = Tracker::copies;
			Vector<Tracker> v;
			v.extendFromPtr(src, 3);
			TCHECK(Tracker::copies == copies_before + 3);  // copies, not moves (source retained)
			TCHECK(v.len() == 3);
			TCHECK(v[0].value == 10);
			TCHECK(v[2].value == 30);
			TCHECK(src[1].value == 20);                    // source untouched
		}
		TCHECK(Tracker::live == 0);
		return true;
	}

	// ── move-only element type ────────────────────────────────────────────

	bool test_vector_move_only_operations() {
		Vector<MoveOnly> v;
		for (int i = 0; i < 5; ++i) v.push(MoveOnly{ i });   // move-push
		TCHECK(v.len() == 5);
		TCHECK(v[3].value == 3);

		MoveOnly popped = v.pop();
		TCHECK(popped.value == 4);

		MoveOnly removed = v.remove(0);                     // shifts via move-construct
		TCHECK(removed.value == 0);
		TCHECK(v[0].value == 1);

		MoveOnly swapped = v.swapRemove(0);
		TCHECK(swapped.value == 1);
		return true;
	}

	// detection traits — evaluated in a dependent context so a failed
	// constraint yields `false` instead of a hard error (esp. on MSVC).
	template <typename V, typename = void>
	struct has_clone : core::false_type {};
	template <typename V>
	struct has_clone<V, core::void_t<decltype(core::declval<V&>().clone())>>
		: core::true_type {
	};

	template <typename V, typename E, typename = void>
	struct has_copy_push : core::false_type {};
	template <typename V, typename E>
	struct has_copy_push<V, E,
		core::void_t<decltype(core::declval<V&>().push(core::declval<const E&>()))>>
		: core::true_type {};

	static_assert(!has_clone<Vector<MoveOnly>>::value,
		"clone must not be callable for a move-only element");
	static_assert(!has_copy_push<Vector<MoveOnly>, MoveOnly>::value,
		"copy-push must not be callable for a move-only element");

	// and the positive controls, so the traits aren't vacuously passing:
	static_assert(has_clone<Vector<int>>::value, "clone should exist for int");
	static_assert(has_copy_push<Vector<int>, int>::value, "copy-push should exist for int");

	bool test_vector_move_only_constraints() {
		return true;   // the static_asserts above do the work at compile time
	}

	// ── trivial type end-to-end (exercises memcpy/memmove fast paths) ──────

	bool test_vector_trivial_int_paths() {
		Vector<int> v;
		for (int i = 0; i < 100; ++i) v.push(i);   // many grows via trivial realloc
		TCHECK(v.len() == 100);
		TCHECK(v[0] == 0);
		TCHECK(v[99] == 99);

		v.insert(50, -1);                           // memmove shift right
		TCHECK(v[50] == -1);
		TCHECK(v[51] == 50);

		int r = v.remove(50);                       // memmove shift left
		TCHECK(r == -1);
		TCHECK(v[50] == 50);

		while (!v.isEmpty()) v.pop();
		TCHECK(v.isEmpty());
		TCHECK(v.len() == 0);
		return true;
	} // namespace core_stl_test


	bool test_vector_range_for() {
		Vector<int> v;
		for (int i = 0; i < 5; ++i) v.push(i * 2);

		int sum = 0, count = 0;
		for (int x : v) { sum += x; ++count; }
		TCHECK(count == 5);
		TCHECK(sum == 0 + 2 + 4 + 6 + 8);

		// mutate through the non-const iterator
		for (int& x : v) x += 1;
		TCHECK(v[0] == 1);
		TCHECK(v[4] == 9);

		// const range-for yields const T& (this must compile)
		const Vector<int>& cv = v;
		int csum = 0;
		for (const int& x : cv) csum += x;
		TCHECK(csum == 1 + 3 + 5 + 7 + 9);
		return true;
	}


	// ── a capturing buffer that satisfies core::FormatBuffer ───────────────
// Writes into a fixed char array instead of stdout, so tests can inspect it.
	struct TestBuf {
		char data[256];
		isize len = 0;

		void append(const core::CStr& s) {
			for (isize i = 0; i < s.len; ++i)   // adjust to CStr's length accessor
				if (len < 255) data[len++] = s.ptr[i]; // adjust to CStr's data accessor
		}
		void append(char c) {
			if (len < 255) data[len++] = c;
		}

		bool equals(const char* expected) const {
			isize i = 0;
			for (; expected[i] != '\0'; ++i)
				if (i >= len || data[i] != expected[i]) return false;
			return i == len;   // exact length match, no trailing garbage
		}
	};

	// concept sanity: the test buffer really is a FormatBuffer
	static_assert(core::FormatBuffer<TestBuf>, "TestBuf must satisfy FormatBuffer");

#define TCHECK(cond) do { if (!(cond)) return false; } while (0)

	// ── format checks ─────────────────────────────────────────────────────

	bool test_format_empty_vector() {
		Vector<int> v;
		TestBuf b;
		mlw_write(b, "{}", v);
		TCHECK(b.equals("{}"));            // empty -> braces, no trailing comma
		return true;
	}

	bool test_format_single_element() {
		Vector<int> v;
		v.push(42);
		TestBuf b;
		mlw_write(b, "{}", v);
		TCHECK(b.equals("{42}"));          // one element, no separator
		return true;
	}

	bool test_format_multiple_elements() {
		Vector<int> v;
		for (int i = 1; i <= 3; ++i) v.push(i);
		TestBuf b;
		mlw_write(b, "{}", v);
		TCHECK(b.equals("{1, 2, 3}"));     // separators BETWEEN only, no trailing ", "
		return true;
	}

	bool test_format_in_context() {
		Vector<int> v;
		v.push(7);
		v.push(8);
		TestBuf b;
		mlw_write(b, "vec={} done", v);
		TCHECK(b.equals("vec={7, 8} done"));   // vector formats inside a larger string
		return true;
	}

	bool test_format_nested_vector() {
		Vector<Vector<int>> vv;
		Vector<int> a; a.push(1); a.push(2);
		Vector<int> b; b.push(3);
		vv.push(core::move(a));
		vv.push(core::move(b));
		TestBuf buf;
		mlw_write(buf, "{}", vv);
		TCHECK(buf.equals("{{1, 2}, {3}}"));   // recursion through formatValue's Formattable branch
		return true;
	}
}
