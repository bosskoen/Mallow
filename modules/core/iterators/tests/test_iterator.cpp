// core/iterators/tests/test_iterator.cpp
//
// Coverage for iter/iterator.h (the pull-based iterator + adapters).
//
// Two flavours of check, matching the module's two kinds of guarantee:
//   * COMPILE-TIME (static_assert): concept satisfaction and Item-type wiring
//     — these are the properties that must hold for the templates to be sound
//     at all, so they belong at compile time.
//   * RUNTIME (bool test_*): behaviour of next()/adapters/consumers over real
//     containers.
//
// The short-circuiting consumers (take/find/any/all) are the ones where a
// behaviour test alone is not enough: producing the right answer doesn't prove
// the source wasn't fully drained. So there's an instrumented source,
// `Counting`, that records how many times next() was pulled — the pull count is
// what actually proves early termination.
//
// The &&-consume convention (methods move *this) is exercised by test_move_named.
// The one compile-FAIL property is noted at the bottom (collect over a borrow of
// a move-only type), the same way test_bit notes the size-mismatch static_assert.

#include "iter/iterator.h"
#include <stl/vector.h>
#include <stl/string.h>

using namespace core;
using namespace itorator;

namespace core_iterators_test
{
    // ------------------------------------------------------------------------
    //  Instrumented source: yields 0..limit-1 and counts every next() pull.
    //  Used to prove short-circuiting — the answer *and* the pull count matter.
    // ------------------------------------------------------------------------
    struct Counting : IterBase<Counting>
    {
        int cur;
        int limit;
        int *pulls;
        Counting(int lim, int *p) : cur(0), limit(lim), pulls(p) {}

        using Item = int;
        core::Optional<int> next()
        {
            ++(*pulls);
            if (cur >= limit)
                return {};
            return cur++;
        }
    };

    // ------------------------------------------------------------------------
    //  Compile-time: concepts and Item-type wiring
    // ------------------------------------------------------------------------
    static_assert(ItoratorType<BorrowIter<int *>>);
    static_assert(ItoratorType<Counting>);
    static_assert(ItoratorType<Take<Counting>>);
    static_assert(ItoratorType<Enumerate<Counting>>);

    // BorrowIter yields references INTO the container.
    static_assert(core::is_same_v<BorrowIter<int *>::Item, int &>);
    // Take is a passthrough — it preserves the upstream's Item exactly.
    static_assert(core::is_same_v<Take<Counting>::Item, int>);
    // Enumerate wraps into its Slot pair.
    static_assert(core::is_same_v<Enumerate<Counting>::Item, Enumerate<Counting>::Slot>);

    // ------------------------------------------------------------------------
    //  Sources
    // ------------------------------------------------------------------------

    // borrow_iter drives begin()/end() and yields T& — manual next() drive.
    bool test_borrow_iter_values()
    {
        Vector<int> v;
        v.push(10);
        v.push(20);
        v.push(30);

        int sum = 0;
        auto it = borrow_iter(v);
        for (;;)
        {
            auto x = it.next();
            if (!x)
                break;
            sum += *x;
        }
        return sum == 60 && v.len() == 3; // source untouched by a borrow
    }

    // range() adapts a pipeline to a range-based for.
    bool test_range_for()
    {
        Vector<int> v;
        v.push(1);
        v.push(2);
        v.push(3);

        int sum = 0;
        for (auto &x : borrow_iter(v).range()) // x is int& (borrow passthrough)
            sum += x;
        return sum == 6;
    }

    // ------------------------------------------------------------------------
    //  Adapters
    // ------------------------------------------------------------------------

    bool test_map()
    {
        int pulls = 0, sum = 0;
        Counting(3, &pulls) // 0,1,2
            .map([](int x)
                 { return x * 10; }) // 0,10,20
            .forEach([&](int x)
                     { sum += x; });
        return sum == 30;
    }

    bool test_filter()
    {
        int pulls = 0, sum = 0;
        Counting(6, &pulls) // 0..5
            .filter([](int x)
                    { return x % 2 == 0; }) // 0,2,4
            .forEach([&](int x)
                     { sum += x; });
        return sum == 6;
    }

    bool test_skip()
    {
        int pulls = 0, sum = 0;
        Counting(5, &pulls) // 0..4
            .skip(2)        // drop 0,1
            .forEach([&](int x)
                     { sum += x; }); // 2+3+4
        return sum == 9;
    }

    bool test_enumerate()
    {
        Vector<int> v;
        v.push(100);
        v.push(200);
        v.push(300);

        isize idx_sum = 0, val_sum = 0;
        auto it = borrow_iter(v).enumerate();
        for (;;)
        {
            auto x = it.next();
            if (!x)
                break;
            idx_sum += (*x).index; // 0+1+2
            val_sum += (*x).value; // value is int& (borrow passthrough)
        }
        return idx_sum == 3 && val_sum == 600;
    }

    // take stops WITHOUT over-pulling the source: pulls == the number taken.
    bool test_take_short_circuits()
    {
        int pulls = 0;
        isize n = Counting(1000, &pulls).take(2).count();
        return n == 2 && pulls == 2; // proves the 1000-element source was pulled twice
    }

    // ------------------------------------------------------------------------
    //  Consumers
    // ------------------------------------------------------------------------

    // collect over a BORROW copies; the source is left intact.
    bool test_collect_borrow_copies()
    {
        Vector<int> v;
        v.push(5);
        v.push(6);
        v.push(7);

        Vector<int> out = borrow_iter(v).collect<Vector<int>>();
        return out.len() == 3 && v.len() == 3 && out[0] == 5 && out[2] == 7;
    }

    bool test_chain()
    {
        Vector<int> v;
        for (int i = 0; i < 10; ++i)
            v.push(i);

        Vector<int> out = borrow_iter(v)
                              .filter([](int x)
                                      { return x % 2 == 0; }) // 0,2,4,6,8
                              .map([](int x)
                                   { return x * 10; }) // 0,20,40,60,80
                              .collect<Vector<int>>();
        return out.len() == 5 && out[0] == 0 && out[4] == 80 && v.len() == 10;
    }

    bool test_count()
    {
        int pulls = 0;
        return Counting(5, &pulls).count() == 5;
    }

    // find returns the first hit and stops: pulls == index+1 of the match.
    bool test_find_short_circuits()
    {
        int pulls = 0;
        auto r = Counting(1000, &pulls).find([](int x)
                                             { return x == 2; });
        return r.isSome() && *r == 2 && pulls == 3; // pulled 0,1,2 then stopped
    }

    // any stops on the first hit; all stops on the first miss.
    bool test_any_all()
    {
        int p1 = 0, p2 = 0, p3 = 0;
        bool a = Counting(10, &p1).any([](int x)
                                       { return x == 3; }); // true, stops at 3
        bool b = Counting(10, &p2).all([](int x)
                                       { return x < 100; }); // true, all pass
        bool c = Counting(10, &p3).all([](int x)
                                       { return x < 3; }); // false, stops at 3
        return a && b && !c && p1 == 4 && p3 == 4;         // 4 = pulled through the deciding element
    }

    // ------------------------------------------------------------------------
    //  Ownership convention: consuming a NAMED iterator needs core::move.
    //  (it.count() on the lvalue would be a compile error — && qualified.)
    // ------------------------------------------------------------------------
    bool test_move_named()
    {
        Vector<int> v;
        v.push(1);
        v.push(2);

        auto it = borrow_iter(v);
        isize n = core::move(it).count(); // explicit consume of a named iterator
        return n == 2;
    }

    // ------------------------------------------------------------------------
    //  Owning source (into_iter / drain): yields OWNED values, so it works with
    //  move-only element types where a borrow+collect could not.
    //
    //  Guarded because it depends on the container exposing drain(). Define
    //  MLW_TEST_DRAIN once Vector::drain() lands to enable it.
    // ------------------------------------------------------------------------
    bool test_into_iter_moves()
    {
        Vector<String> v;
        v.push(String(CStr("alpha")));
        v.push(String(CStr("beta")));
        v.push(String(CStr("gamma")));

        // moves each String out of v; v is emptied by the drain.
        isize n = into_iter(core::move(v)).count();
        return n == 3;
    }

    bool test_into_iter_collect_moveonly()
    {
        Vector<String> v;
        v.push(String(CStr("x")));
        v.push(String(CStr("y")));

        // OWNED source -> collect moves; move-only String is fine here.
        Vector<String> out = into_iter(core::move(v)).collect<Vector<String>>();
        return out.len() == 2 && out[0] == CStr("x") && out[1] == CStr("y");
    }

    // ------------------------------------------------------------------------
    //  Aggregate runner (AND of every runtime test).
    // ------------------------------------------------------------------------
    bool test_all()
    {
        bool ok = true;
        ok = ok && test_borrow_iter_values();
        ok = ok && test_range_for();
        ok = ok && test_map();
        ok = ok && test_filter();
        ok = ok && test_skip();
        ok = ok && test_enumerate();
        ok = ok && test_take_short_circuits();
        ok = ok && test_collect_borrow_copies();
        ok = ok && test_chain();
        ok = ok && test_count();
        ok = ok && test_find_short_circuits();
        ok = ok && test_any_all();
        ok = ok && test_move_named();
        ok = ok && test_into_iter_moves();
        ok = ok && test_into_iter_collect_moveonly();
        return ok;
    }

   
    // NOTE: collect<Vector<String>>() over a *borrowing* iterator is a hard
    // static_assert failure by design — a borrow must copy, and String is
    // move-only. It therefore cannot be exercised at runtime without breaking
    // the build. With a compile-fail harness, this should NOT compile:
    //     Vector<String> sv; /* ... */
    //     borrow_iter(sv).collect<Vector<String>>();   // static_assert: not copyable
    // The owning path is the correct way to move move-only elements out:
    //     into_iter(core::move(sv)).collect<Vector<String>>();
    //
    // NOTE: the &&-qualified chaining methods cannot be called on an lvalue, so
    //     auto it = borrow_iter(v);
    //     it.map(f);              // should NOT compile — write core::move(it).map(f)
    // is likewise a compile-time-only property (see test_move_named for the
    // correct form).
} // namespace core_iterators_test
