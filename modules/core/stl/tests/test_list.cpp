// list_tests.cpp
//
// List tests, matching the core_stl_test harness (Tracker / MoveOnly / TCHECK).
//
// These reuse the SAME Tracker, MoveOnly and TCHECK already defined alongside
// the Vector tests -- they are NOT redefined here (that would be a redefinition
// error). If the list tests live in a separate translation unit, include the
// shared test header that declares those types before this block.
//
// Optional API assumed (from the vector tests): .isNone() / .isSome() / .unwrap().
// C++20 required (List uses concepts).

#include "stl/list.h"
#include "test_types.h"

namespace core_stl_test
{

	using core::List;

	// ── lifetime ──────────────────────────────────────────────────────────

	bool test_list_default_empty() {
		List<int> v;
		TCHECK(v.isEmpty());
		TCHECK(v.len() == 0);
		TCHECK(v.front().isNone());
		TCHECK(v.back().isNone());
		TCHECK(v.begin() == v.end());
		int count = 0;
		for (int x : v) { (void)x; ++count; }
		TCHECK(count == 0);
		return true;
	}

	// deep copy: exact element copies, matching values, independent nodes.
	// (catches the inverted `length == 0` clone guard: broken -> copies==0.)
	bool test_list_clone_deep_copy() {
		Tracker::reset();
		{
			List<Tracker> v;
			for (int i = 0; i < 3; ++i) v.pushBack(Tracker{ i * 10 });
			int copies_before = Tracker::copies;

			List<Tracker> c = v.clone();
			TCHECK(Tracker::copies == copies_before + 3);   // exactly 3 element copies
			TCHECK(c.len() == 3);

			// values match, in order
			int expected[] = { 0, 10, 20 };
			int i = 0;
			for (Tracker& t : c) TCHECK(t.value == expected[i++]);
			TCHECK(i == 3);

			// separate storage: the clone's first element is a different object
			TCHECK(&c.front().unwrap() != &v.front().unwrap());

			// independence: mutating the clone does not touch the original
			c.front().unwrap().value = 999;
			TCHECK(v.front().unwrap().value == 0);
		}
		TCHECK(Tracker::live == 0);
		return true;
	}

	bool test_list_clone_empty() {
		Tracker::reset();
		{
			List<Tracker> v;
			List<Tracker> c = v.clone();
			TCHECK(c.isEmpty());
			TCHECK(c.len() == 0);
			TCHECK(Tracker::copies == 0);
		}
		TCHECK(Tracker::live == 0);
		return true;
	}

	// every constructed element is destroyed exactly once; pop returns by move.
	bool test_list_lifetime_no_leak() {
		Tracker::reset();
		{
			List<Tracker> v;
			for (int i = 0; i < 5; ++i) v.emplaceBack(i);   // in place: no copies
			TCHECK(Tracker::copies == 0);
			TCHECK(Tracker::live == 5);

			Tracker popped = v.popFront();                   // moved out
			TCHECK(popped.value == 0);
			TCHECK(v.len() == 4);
		}
		TCHECK(Tracker::live == 0);
		return true;
	}

	// ── ordering ──────────────────────────────────────────────────────────

	bool test_list_push_back_order() {
		List<int> v;
		for (int i = 1; i <= 3; ++i) v.pushBack(i);
		TCHECK(v.len() == 3);
		TCHECK(!v.isEmpty());
		TCHECK(v.front().isSome() && v.front().unwrap() == 1);
		TCHECK(v.back().isSome() && v.back().unwrap() == 3);

		int expected = 1;
		for (int x : v) TCHECK(x == expected++);
		TCHECK(expected == 4);
		return true;
	}

	bool test_list_push_front_order() {
		List<int> v;
		v.pushFront(1);
		v.pushFront(2);
		v.pushFront(3);                                       // -> [3,2,1]
		TCHECK(v.len() == 3);
		TCHECK(v.front().unwrap() == 3);
		TCHECK(v.back().unwrap() == 1);

		int expected = 3;
		for (int x : v) TCHECK(x == expected--);
		return true;
	}

	// ── modifiers ─────────────────────────────────────────────────────────

	bool test_list_pop_front_back() {
		List<int> v;
		for (int i = 1; i <= 3; ++i) v.pushBack(i);          // [1,2,3]
		TCHECK(v.popFront() == 1);                            // [2,3]
		TCHECK(v.popBack() == 3);                             // [2]
		TCHECK(v.len() == 1);
		TCHECK(v.front().unwrap() == 2);
		TCHECK(v.back().unwrap() == 2);
		TCHECK(v.popFront() == 2);                            // []
		TCHECK(v.isEmpty());
		return true;
	}

	bool test_list_emplace_returns_ref() {
		List<int> v;
		int& a = v.emplaceBack(10);                          // [10]
		int& b = v.emplaceFront(20);                         // [20,10]
		TCHECK(a == 10);
		TCHECK(b == 20);
		TCHECK(v.front().unwrap() == 20);
		TCHECK(v.back().unwrap() == 10);
		a = 11;                                              // ref aliases storage
		TCHECK(v.back().unwrap() == 11);
		return true;
	}

	bool test_list_insert_positions() {
		List<int> v;
		for (int i = 1; i <= 3; ++i) v.pushBack(i);          // [1,2,3]
		v.insert(0, 0);                                      // [0,1,2,3]
		v.insert(2, 99);                                     // [0,1,99,2,3]
		v.insert(v.len(), 4);                                // [0,1,99,2,3,4]  (i == length)
		TCHECK(v.len() == 6);

		int expected[] = { 0, 1, 99, 2, 3, 4 };
		int i = 0;
		for (int x : v) TCHECK(x == expected[i++]);
		TCHECK(i == 6);
		return true;
	}

	bool test_list_remove_positions() {
		List<int> v;
		for (int i = 1; i <= 5; ++i) v.pushBack(i);          // [1,2,3,4,5]
		TCHECK(v.remove(0) == 1);                            // [2,3,4,5]  (front)
		TCHECK(v.remove(2) == 4);                            // [2,3,5]    (middle)
		TCHECK(v.remove(v.len() - 1) == 5);                  // [2,3]      (back)
		TCHECK(v.len() == 2);

		int expected[] = { 2, 3 };
		int i = 0;
		for (int x : v) TCHECK(x == expected[i++]);
		return true;
	}

	bool test_list_clear() {
		Tracker::reset();
		{
			List<Tracker> v;
			for (int i = 0; i < 4; ++i) v.emplaceBack(i);
			v.clear();
			TCHECK(v.isEmpty());
			TCHECK(v.len() == 0);
			TCHECK(v.begin() == v.end());
			TCHECK(Tracker::live == 0);                      // clear destroyed all 4
			v.emplaceBack(7);                                // reusable afterwards
			TCHECK(v.len() == 1 && v.front().unwrap().value == 7);
		}
		TCHECK(Tracker::live == 0);
		return true;
	}

	// ── iteration ─────────────────────────────────────────────────────────

	bool test_list_iterator_mutation() {
		List<int> v;
		for (int i = 1; i <= 3; ++i) v.pushBack(i);
		for (int& x : v) x += 10;
		int expected[] = { 11, 12, 13 };
		int i = 0;
		for (int x : v) TCHECK(x == expected[i++]);
		return true;
	}

	bool test_list_const_iteration() {
		List<int> tmp;
		for (int i = 1; i <= 4; ++i) tmp.pushBack(i);
		const List<int>& v = tmp;
		long sum = 0;
		for (int x : v) sum += x;
		TCHECK(sum == 10);
		TCHECK(v.front().unwrap() == 1);
		TCHECK(v.back().unwrap() == 4);
		return true;
	}

	// ── move semantics of the container ───────────────────────────────────

	bool test_list_move_ctor_empties_source() {
		List<int> a;
		for (int i = 1; i <= 3; ++i) a.pushBack(i);
		List<int> b = static_cast<List<int>&&>(a);           // move-construct
		TCHECK(b.len() == 3);
		TCHECK(a.isEmpty());                                 // source left empty+valid
		TCHECK(a.len() == 0);
		TCHECK(a.begin() == a.end());
		TCHECK(b.front().unwrap() == 1 && b.back().unwrap() == 3);
		return true;
	}

	bool test_list_move_assign() {
		List<int> a;
		for (int i = 1; i <= 3; ++i) a.pushBack(i);
		List<int> b;
		b.pushBack(99);
		b = static_cast<List<int>&&>(a);                     // old contents of b released
		TCHECK(b.len() == 3);
		TCHECK(a.isEmpty());
		int expected = 1;
		for (int x : b) TCHECK(x == expected++);
		return true;
	}

	// ── move-only element type ────────────────────────────────────────────

	bool test_list_move_only() {
		List<MoveOnly> v;
		v.pushBack(MoveOnly{ 1 });                           // rvalue -> pushBack(T&&)
		v.emplaceBack(2);
		TCHECK(v.len() == 2);
		TCHECK(v.front().unwrap().value == 1);
		TCHECK(v.back().unwrap().value == 2);

		MoveOnly popped = v.popFront();                      // move out
		TCHECK(popped.value == 1);
		TCHECK(v.len() == 1);
		TCHECK(v.front().unwrap().value == 2);
		return true;
	}

	// ── optional: only if you added at() ──────────────────────────────────

	bool test_list_get() {
		List<int> v;
		for (int i = 1; i <= 3; ++i) v.pushBack(i);          // [1,2,3]
		TCHECK(v.get(0).isSome() && v.get(0).unwrap() == 1);
		TCHECK(v.get(2).isSome() && v.get(2).unwrap() == 3);
		TCHECK(v.get(-1).isNone());
		TCHECK(v.get(3).isNone());
		return true;
	}

} // namespace core_stl_test