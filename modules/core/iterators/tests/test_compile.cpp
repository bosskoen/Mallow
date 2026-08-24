// core/core/tests/test_itorator.cpp
//
// Coverage for core/itorator.h. These tests intentionally use the public
// iterator surface in fairly ordinary ways: borrowing and owning sources,
// adapters, consumers, exhaustion, and small composed pipelines.
//
// The iterator API is pull-based and consuming operations are &&-qualified,
// so named iterators are explicitly core::move()'d when they are consumed.

#include <iter/iterator.h>
#include <stl/vector.h>

using namespace core;

namespace core_iterators_test
{
    // Move-only test value. Useful for exercising the owning iterator path:
    // values must move through into_iter(), adapters, and collect() without
    // accidentally requiring a copy.
    struct MoveOnly
    {
        int value;

        explicit MoveOnly(int v) : value(v) {}

        MoveOnly(MoveOnly &&) = default;
        MoveOnly &operator=(MoveOnly &&) = default;

        MoveOnly(const MoveOnly &) = delete;
        MoveOnly &operator=(const MoveOnly &) = delete;
    };

    bool vector_equals_move_only(const Vector<MoveOnly> &v,
                                 const int *expected,
                                 isize n)
    {
        if (v.len() != n)
            return false;

        for (isize i = 0; i < n; ++i)
            if (v[i].value != expected[i])
                return false;

        return true;
    }

    Vector<MoveOnly> make_move_only_vector()
    {
        Vector<MoveOnly> v;
        v.emplace(1);
        v.emplace(2);
        v.emplace(3);
        v.emplace(4);
        v.emplace(5);
        return v;
    }

    // Small helper so the individual tests do not depend on any external
    // assertion or test framework.
    bool vector_equals(const Vector<int> &v, const int *expected, isize n)
    {
        if (v.len() != n)
            return false;

        for (isize i = 0; i < n; ++i)
            if (v[i] != expected[i])
                return false;

        return true;
    }

    Vector<int> make_vector()
    {
        Vector<int> v;
        v.push(1);
        v.push(2);
        v.push(3);
        v.push(4);
        v.push(5);
        return v;
    }

    // ---- basic borrowing ---------------------------------------------------

    bool test_borrow_iter_next_and_exhaustion()
    {
        Vector<int> v = make_vector();
        auto it = itorator::borrow_iter(v);

        auto a = it.next();
        auto b = it.next();
        auto c = it.next();
        auto d = it.next();
        auto e = it.next();
        auto end = it.next();

        return a && b && c && d && e &&
               *a == 1 && *b == 2 && *c == 3 && *d == 4 && *e == 5 &&
               !end;
    }

    bool test_borrow_iter_yields_references()
    {
        Vector<int> v = make_vector();

        auto it = itorator::borrow_iter(v);
        auto first = it.next();
        if (!first)
            return false;

        *first = 42;

        return v[0] == 42;
    }

    bool test_borrow_iter_empty()
    {
        Vector<int> v;
        auto it = itorator::borrow_iter(v);

        return !it.next() && !it.next();
    }

    // ---- owning iteration --------------------------------------------------

    bool test_into_iter_next_and_exhaustion()
    {
        Vector<int> v = make_vector();
        auto it = itorator::into_iter(core::move(v));

        auto a = it.next();
        auto b = it.next();
        auto c = it.next();
        auto d = it.next();
        auto e = it.next();
        auto end = it.next();

        return a && b && c && d && e &&
               *a == 1 && *b == 2 && *c == 3 && *d == 4 && *e == 5 &&
               !end;
    }

    // ---- map ---------------------------------------------------------------

    bool test_map_transforms_items()
    {
        Vector<int> v = make_vector();

        auto out = itorator::borrow_iter(v)
                       .map([](int x)
                            { return x * 10; })
                       .collect<Vector<int>>();

        const int expected[] = {10, 20, 30, 40, 50};
        return vector_equals(out, expected, 5);
    }

    bool test_map_on_empty()
    {
        Vector<int> v;

        auto out = itorator::borrow_iter(v)
                       .map([](int x)
                            { return x + 1; })
                       .collect<Vector<int>>();

        return out.isEmpty();
    }

    // ---- filter ------------------------------------------------------------

    bool test_filter_keeps_matching_items()
    {
        Vector<int> v = make_vector();

        auto out = itorator::borrow_iter(v)
                       .filter([](int x)
                               { return x % 2 == 0; })
                       .collect<Vector<int>>();

        const int expected[] = {2, 4};
        return vector_equals(out, expected, 2);
    }

    bool test_filter_no_matches()
    {
        Vector<int> v = make_vector();

        auto out = itorator::borrow_iter(v)
                       .filter([](int x)
                               { return x > 100; })
                       .collect<Vector<int>>();

        return out.isEmpty();
    }

    // ---- take / skip -------------------------------------------------------

    bool test_take_limits_output()
    {
        Vector<int> v = make_vector();

        auto out = itorator::borrow_iter(v)
                       .take(3)
                       .collect<Vector<int>>();

        const int expected[] = {1, 2, 3};
        return vector_equals(out, expected, 3);
    }

    bool test_take_more_than_available()
    {
        Vector<int> v = make_vector();

        auto out = itorator::borrow_iter(v)
                       .take(100)
                       .collect<Vector<int>>();

        const int expected[] = {1, 2, 3, 4, 5};
        return vector_equals(out, expected, 5);
    }

    bool test_take_zero_and_negative()
    {
        Vector<int> v = make_vector();

        auto zero = itorator::borrow_iter(v)
                        .take(0)
                        .count();

        auto negative = itorator::borrow_iter(v)
                            .take(-1)
                            .count();

        return zero == 0 && negative == 0;
    }

    bool test_skip_drops_prefix()
    {
        Vector<int> v = make_vector();

        auto out = itorator::borrow_iter(v)
                       .skip(2)
                       .collect<Vector<int>>();

        const int expected[] = {3, 4, 5};
        return vector_equals(out, expected, 3);
    }

    bool test_skip_past_end()
    {
        Vector<int> v = make_vector();

        return itorator::borrow_iter(v)
                   .skip(100)
                   .count() == 0;
    }

    // ---- enumerate ---------------------------------------------------------

    bool test_enumerate_numbers_items()
    {
        Vector<int> v = make_vector();

        auto it = itorator::borrow_iter(v).enumerate();

        auto a = it.next();
        auto b = it.next();
        auto c = it.next();

        if (!a || !b || !c)
            return false;

        return (*a).index == 0 && (*a).value == 1 &&
               (*b).index == 1 && (*b).value == 2 &&
               (*c).index == 2 && (*c).value == 3;
    }

    bool test_enumerate_preserves_borrowed_values()
    {
        Vector<int> v = make_vector();
        auto it = itorator::borrow_iter(v).enumerate();

        auto first = it.next();
        if (!first)
            return false;

        (*first).value = 99;

        return (*first).index == 0 && v[0] == 99;
    }

    // ---- consumers ---------------------------------------------------------

    bool test_count_two()
    {
        Vector<int> v = make_vector();

        return itorator::borrow_iter(v).count() == 5;
    }

    bool test_for_each()
    {
        Vector<int> v = make_vector();
        int sum = 0;

        itorator::borrow_iter(v).forEach([&sum](int x)
                                         { sum += x; });

        return sum == 15;
    }

    bool test_find()
    {
        Vector<int> v = make_vector();

        auto found = itorator::borrow_iter(v)
                         .find([](int x)
                               { return x == 3; });

        auto missing = itorator::borrow_iter(v)
                           .find([](int x)
                                 { return x == 99; });

        return found && *found == 3 && !missing;
    }

    bool test_any()
    {
        Vector<int> v = make_vector();

        return itorator::borrow_iter(v).any([](int x)
                                            { return x == 4; }) &&
               !itorator::borrow_iter(v).any([](int x)
                                             { return x > 100; });
    }

    bool test_all_two()
    {
        Vector<int> v = make_vector();

        return itorator::borrow_iter(v).all([](int x)
                                            { return x > 0; }) &&
               !itorator::borrow_iter(v).all([](int x)
                                             { return x < 5; });
    }

    bool test_all_on_empty_is_true()
    {
        Vector<int> v;

        return itorator::borrow_iter(v).all([](int)
                                            { return false; });
    }

    // ---- range-for bridge --------------------------------------------------

    bool test_range_for_consumes_iterator()
    {
        Vector<int> v = make_vector();
        int sum = 0;

        for (int x : itorator::range_for(itorator::borrow_iter(v)))
            sum += x;

        return sum == 15;
    }

    bool test_range_member_consumes_iterator()
    {
        Vector<int> v = make_vector();
        int sum = 0;

        for (int x : itorator::borrow_iter(v).range())
            sum += x * 2;

        return sum == 30;
    }

    // ---- composed, normal-ish pipelines -----------------------------------

    bool test_composed_pipeline()
    {
        Vector<int> v = make_vector();

        auto out = itorator::borrow_iter(v)
                       .skip(1)
                       .filter([](int x)
                               { return x % 2 != 0; })
                       .map([](int x)
                            { return x * x; })
                       .take(2)
                       .collect<Vector<int>>();

        // skip -> {2,3,4,5}
        // odd  -> {3,5}
        // sqr  -> {9,25}
        const int expected[] = {9, 25};
        return vector_equals(out, expected, 2);
    }

    bool test_enumerate_map_pipeline()
    {
        Vector<int> v = make_vector();

        auto out = itorator::borrow_iter(v)
                       .enumerate()
                       .map([](auto slot)
                            { return static_cast<int>(slot.index) + slot.value; })
                       .collect<Vector<int>>();

        const int expected[] = {1, 3, 5, 7, 9};
        return vector_equals(out, expected, 5);
    }

    bool test_into_iter_pipeline()
    {
        Vector<int> v = make_vector();

        auto out = itorator::into_iter(core::move(v))
                       .filter([](int x)
                               { return x >= 3; })
                       .map([](int x)
                            { return x * 10; })
                       .collect<Vector<int>>();

        const int expected[] = {30, 40, 50};
        return vector_equals(out, expected, 3);
    }

    // ---- move-only values --------------------------------------------------

    bool test_into_iter_moves_move_only_values()
    {
        Vector<MoveOnly> v = make_move_only_vector();

        auto it = itorator::into_iter(core::move(v));

        auto a = it.next();
        auto b = it.next();
        auto c = it.next();
        auto d = it.next();
        auto e = it.next();
        auto end = it.next();

        return a && b && c && d && e &&
               a->value == 1 &&
               b->value == 2 &&
               c->value == 3 &&
               d->value == 4 &&
               e->value == 5 &&
               !end;
    }

    bool test_move_only_into_iter_collect()
    {
        Vector<MoveOnly> v = make_move_only_vector();

        auto out = itorator::into_iter(core::move(v))
                       .collect<Vector<MoveOnly>>();

        const int expected[] = {1, 2, 3, 4, 5};
        return vector_equals_move_only(out, expected, 5);
    }

    bool test_move_only_map_collect()
    {
        Vector<MoveOnly> v = make_move_only_vector();

        auto out = itorator::into_iter(core::move(v))
                       .map([](MoveOnly x)
                            { return MoveOnly{x.value * 10}; })
                       .collect<Vector<MoveOnly>>();

        const int expected[] = {10, 20, 30, 40, 50};
        return vector_equals_move_only(out, expected, 5);
    }

    bool test_move_only_filter_collect()
    {
        Vector<MoveOnly> v = make_move_only_vector();

        auto out = itorator::into_iter(core::move(v))
                       .filter([](const MoveOnly &x)
                               { return x.value % 2 == 0; })
                       .collect<Vector<MoveOnly>>();

        const int expected[] = {2, 4};
        return vector_equals_move_only(out, expected, 2);
    }

    bool test_move_only_take_collect()
    {
        Vector<MoveOnly> v = make_move_only_vector();

        auto out = itorator::into_iter(core::move(v))
                       .skip(1)
                       .take(3)
                       .collect<Vector<MoveOnly>>();

        const int expected[] = {2, 3, 4};
        return vector_equals_move_only(out, expected, 3);
    }

    bool test_move_only_enumerate_collect()
    {
        Vector<MoveOnly> v = make_move_only_vector();

        auto out = itorator::into_iter(core::move(v))
                       .enumerate()
                       .map([](auto &slot)
                            { return MoveOnly{
                                  static_cast<int>(slot.index) + slot.value.value}; })
                       .collect<Vector<MoveOnly>>();

        const int expected[] = {1, 3, 5, 7, 9};
        return vector_equals_move_only(out, expected, 5);
    }

    bool test_move_only_composed_pipeline()
    {
        Vector<MoveOnly> v = make_move_only_vector();

        auto out = itorator::into_iter(core::move(v))
                       .filter([](const MoveOnly &x)
                               { return x.value >= 2; })
                       .skip(1)
                       .map([](MoveOnly x)
                            { return MoveOnly{x.value * x.value}; })
                       .take(2)
                       .collect<Vector<MoveOnly>>();

        // filter -> {2,3,4,5}
        // skip   -> {3,4,5}
        // map    -> {9,16,25}
        // take   -> {9,16}
        const int expected[] = {9, 16};
        return vector_equals_move_only(out, expected, 2);
    }

    bool test_move_only_range_for()
    {
        Vector<MoveOnly> v = make_move_only_vector();

        int sum = 0;

        for (auto &&x : itorator::into_iter(core::move(v)).range())
            sum += x.value;

        return sum == 15;
    }

    bool test_tap_mutates_borrow_in_place()
    {
        Vector<int> v = make_vector(); // {1,2,3,4,5}
        auto out = itorator::borrow_iter(v)
                       .tap([](int &x)
                            { x *= 2; })
                       .collect<Vector<int>>();
        const int expected[] = {2, 4, 6, 8, 10};
        // out has the doubled values, AND the source was mutated in place
        return vector_equals(out, expected, 5) &&
               vector_equals(v, expected, 5);
    }

    bool test_tap_owned_then_move()
    {
        Vector<MoveOnly> v = make_move_only_vector();
        auto out = itorator::into_iter(core::move(v))
                       .tap([](MoveOnly &m)
                            { m.value += 100; })     // mutate the in-flight owned item
                       .collect<Vector<MoveOnly>>(); // then move it into out
        const int expected[] = {101, 102, 103, 104, 105};
        return vector_equals_move_only(out, expected, 5);
    }

    bool test_tap_is_lazy()
    {
        Vector<int> v = make_vector(); // {1,2,3,4,5}
        int calls = 0;
        auto out = itorator::borrow_iter(v)
                       .tap([&](int &)
                            { ++calls; })
                       .take(2) // only 2 items ever pulled through
                       .collect<Vector<int>>();
        const int expected[] = {1, 2};
        return calls == 2 && vector_equals(out, expected, 2); // tap fired exactly twice
    }

    bool test_byref_partial_consumption()
    {
        Vector<int> v = make_vector(); // {1,2,3,4,5}
        auto it = itorator::into_iter(core::move(v));
        auto first = it.byRef().take(2).collect<Vector<int>>(); // {1,2}
        auto rest = it.byRef().collect<Vector<int>>();          // {3,4,5}
        const int e1[] = {1, 2};
        const int e2[] = {3, 4, 5};
        return vector_equals(first, e1, 2) && vector_equals(rest, e2, 3);
    }

    namespace static_checks
    {
        using core::is_reference_v;
        using core::is_same_v;

        // sources (formed as types only � no next() instantiated)
        using BInt = decltype(itorator::borrow_iter(core::declval<core::Vector<int> &>()));
        using OMove = decltype(itorator::into_iter(core::declval<core::Vector<MoveOnly>>()));

        // --- map ALWAYS yields a value, never a reference ---
        using M_ret_ref = decltype(core::declval<BInt &&>()
                                       .map([](int &x) -> int &
                                            { return x; })); // fn returns int&
        static_assert(!is_reference_v<M_ret_ref::Item>, "map must strip references to a value");
        static_assert(is_same_v<M_ret_ref::Item, int>);

        // --- move_item picks the right calling convention ---
        using M_byval = decltype(core::declval<OMove &&>()
                                     .map([](MoveOnly m)
                                          { return MoveOnly{m.value * 10}; }));
        static_assert(M_byval::move_item, "owned + by-value fn => move into fn");
        static_assert(is_same_v<M_byval::Item, MoveOnly>);

        using M_byref = decltype(core::declval<OMove &&>()
                                     .map([](MoveOnly &m)
                                          { return m.value; }));
        static_assert(!M_byref::move_item, "lvalue-ref fn must receive an lvalue, not a moved value");

        using M_borrow = decltype(core::declval<BInt &&>()
                                      .map([](int &x)
                                           { return x; }));
        static_assert(!M_borrow::move_item, "never move out of a borrowed element");

        // --- tap PRESERVES category ---
        using I_borrow = decltype(core::declval<BInt &&>().tap([](int &x)
                                                               { x++; }));
        static_assert(is_reference_v<typename I_borrow::Item>, "tap over a borrow keeps the reference");
        using I_owned = decltype(core::declval<OMove &&>().tap([](MoveOnly &m)
                                                               { m.value++; }));
        static_assert(!is_reference_v<typename I_owned::Item>, "tap over an owned source keeps the value");

        // --- filter / take are passthrough ---
        using F_borrow = decltype(core::declval<BInt &&>().filter([](const int &x)
                                                                  { return x > 0; }));
        static_assert(is_reference_v<F_borrow::Item>);
        using T_owned = decltype(core::declval<OMove &&>().take(2));
        static_assert(!is_reference_v<T_owned::Item>);

        // --- a by-value move-only predicate must be REJECTED (cleanly, not a hard copy error) ---
        template <typename It, typename P>
        concept can_filter = requires(It it, P p) { core::move(it).filter(p); };

        static_assert(!can_filter<OMove, decltype([](MoveOnly)
                                                  { return true; })>,
                      "filtering an owned move-only source with a by-value predicate must be rejected");
        static_assert(can_filter<OMove, decltype([](const MoveOnly &)
                                                 { return true; })>,
                      "const-ref is the correct, copy-free predicate signature");

        static_assert(!can_filter<BInt, decltype([](int &x)
                                                 { return x > 0; })>,
                      "a mutating predicate must be rejected by filter");
        static_assert(can_filter<BInt, decltype([](int x)
                                                { return x > 0; })>,
                      "a trivial by-value predicate is fine (it copies the int)");

        template <typename It, typename F>
        concept can_tap = requires(It it, F f) { core::move(it).tap(f); };

        static_assert(can_tap<BInt, decltype([](int &x)
                                             { x++; })>,
                      "tap accepts a void side-effect on a mutable reference");
        static_assert(!can_tap<BInt, decltype([](int &x)
                                              { return x; })>,
                      "tap must reject a value-returning lambda — use map for that");
        static_assert(can_tap<OMove, decltype([](MoveOnly &m)
                                              { m.value++; })>,
                      "tap works over an owned source too");

        template <typename It>
        concept byref_on_lvalue = requires(It it) { it.byRef(); }; // no move needed
        static_assert(byref_on_lvalue<OMove>, "byRef is callable on a named lvalue");
    }

} // namespace core_itorator_test