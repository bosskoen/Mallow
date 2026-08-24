#pragma once

#include <core/compilers.h>
#include <core/optional.h>
#include <core/traits.h>

/// \file
/// \brief Lazy, pull-based iterators and composable adapters (Rust-style).
///
/// \par Model
///   An iterator is any type with an associated `Item` and a
///   `Optional<Item> next()` that yields the next element or `None` at
///   exhaustion (see \ref itorator::ItoratorType). This is a *pull* model: no
///   work happens until something calls `next()`. Adapters (\ref Map,
///   \ref Filter, \ref Take, \ref Enumerate, \ref Tap) wrap an upstream iterator
///   and are themselves iterators, so pipelines compose into one flat,
///   allocation-free value with no virtual dispatch — the optimizer fuses the
///   whole chain.
///
/// \par Two kinds of operation
///   - **Adapters** are lazy and return a new iterator: `map`, `filter`,
///     `take`, `skip`, `enumerate`, `tap`.
///   - **Consumers** are eager, drive the chain to exhaustion, and return a
///     value (or nothing): `collect`, `forEach`, `fold`-family (`count`),
///     and the short-circuiting `find` / `any` / `all`. `range()` is a consumer
///     that hands control to a range-based `for`.
///
/// \par Ownership convention (important)
///   Iterators flow **down** a chain and are consumed as they go; they are
///   never stored long-term or passed *up*. Every chaining/consuming method is
///   therefore **rvalue-ref-qualified** (`&&`): it moves `*this` into the next
///   stage. Calling one on a named lvalue is a compile error — write
///   `core::move(it).map(f)` to consume a named iterator explicitly. Adapters
///   store their upstream **by value** (never by reference), so a pipeline
///   built from a temporary source cannot dangle.
///
/// \par Reference vs value items
///   A source's `Item` may be a reference (`T&`, e.g. \ref BorrowIter — a view
///   into a container) or a value (`T`, e.g. a draining source — owned output).
///   Adapters preserve this category unless they transform it (\ref Map yields
///   whatever the mapping function returns, always as a *value*; \ref Tap and
///   the pass-through adapters preserve it exactly). Consumers that must store
///   an item (\ref collect) copy borrowed items and move owned ones.
///
/// \par Function-argument convention (how adapters call your callable)
///   - `map(fn)`   : for an *owned* item whose `fn` accepts an rvalue, the item
///                   is **moved** into `fn`; otherwise `fn` gets an lvalue. A
///                   borrowed item is always passed by reference (never moved).
///                   `fn`'s result is stored **by value** — returning a
///                   reference therefore copies, it does not alias.
///   - `filter/find/any/all(pred)` : `pred` only inspects, so it receives a
///                   `const T&`. A mutating `[](T&)` predicate is rejected.
///   - `tap(fn)`   : `fn` receives a mutable `T&` for its side effect and must
///                   return `void`; the item then passes through unchanged.

namespace itorator
{
    // -------------------------------------------------------------------------
    //  Concept
    // -------------------------------------------------------------------------

    /// \brief Satisfied by any pull iterator: has `Item` and `Optional<Item> next()`.
    template <typename It>
    concept ItoratorType = requires(It t) {
        typename It::Item;
        { t.next() } -> core::same_as<core::Optional<typename It::Item>>;
    };

    /// \brief CRTP base providing the adapter/consumer methods. Forward-declared
    ///        so the source/adapter structs below can inherit it.
    /// \tparam Self The concrete iterator type (must define `Item` and `next()`).
    template <typename Self>
    struct IterBase;

    // -------------------------------------------------------------------------
    //  Sources
    // -------------------------------------------------------------------------

    /// \brief Borrowing source: drives a container's `begin()/end()` cursors and
    ///        yields references (`T&`) into the container.
    ///
    /// \tparam Cur The container's cursor type (typically `T*` or a node cursor).
    /// \note The container must outlive this iterator; each yielded `T&` is a
    ///       borrow into the container's storage, valid only while that storage
    ///       is alive and unmodified. See \ref borrow_iter for the factory.
    template <typename Cur>
    struct BorrowIter : IterBase<BorrowIter<Cur>>
    {
        Cur cur, stop;

        /// \brief Construct from a `[begin, end)` cursor pair.
        BorrowIter(Cur begin, Cur end) : cur(begin), stop(end) {}

        /// \brief Yields `T&` deliberately (zero-copy borrow into the container).
        using Item = core::remove_ref_t<decltype(*cur)> &;

        core::Optional<Item> next()
        {
            if (cur == stop)
                return {};
            Item ref = *cur; // binds a reference; container storage is stable across ++
            ++cur;
            return ref;
        }
    };

    /// \brief Make a borrowing iterator over any container exposing `begin()/end()`.
    /// \note Borrows \p c; \p c must outlive the returned iterator and the whole
    ///       pipeline built from it.
    template <typename C>
        requires requires(C &c) { c.begin(); c.end(); }
    BorrowIter<decltype(core::declval<C &>().begin())> borrow_iter(C &c)
    {
        return {c.begin(), c.end()};
    }

    /// \brief Owning source: wraps a container's *drainer* (a `next()`-only,
    ///        container-owned consuming iterator) and adds chaining.
    ///
    /// \tparam Drainer A move-only type with `Item` and `Optional<Item> next()`
    ///         that owns and tears down its container as it yields owned values.
    /// \see into_iter
    template <typename Drainer>
    struct IntoIter : IterBase<IntoIter<Drainer>>
    {
        Drainer d;
        using Item = typename Drainer::Item;

        explicit IntoIter(Drainer &&drainer) : d(core::move(drainer)) {}
        core::Optional<Item> next() { return d.next(); }
    };

    /// \brief Consume a container by value and iterate it, yielding owned items.
    /// \note \p c is moved into the drainer and left empty; the returned iterator
    ///       owns the storage and frees it when destroyed (early abandonment is
    ///       safe — remaining elements are cleaned up by the container's dtor).
    template <typename C>
        requires requires(C &&c) { core::move(c).drain(); }
    auto into_iter(C &&c)
    {
        return IntoIter{core::move(c).drain()};
    }

    // -------------------------------------------------------------------------
    //  range-based for bridge
    // -------------------------------------------------------------------------

    /// \brief Adapts a pull iterator to `begin()/end()` so it works in a
    ///        range-based `for`. Not itself an \ref ItoratorType.
    /// \note Obtain one via \ref IterBase::range or \ref range_for.
    /// \note Bind the loop variable as `auto&&` so it works for both a borrowing
    ///       source (`T&`) and an owning/move-only one (`T`); `auto x` would copy.
    template <typename It>
    struct RangeFor
    {
        It iter;
        struct End
        {
        };
        struct Cursor
        {
            It *iter;
            core::Optional<typename It::Item> slot;
            decltype(auto) operator*() { return *slot; } // follows Item (ref or value)
            MLW_FORCE_INLINE Cursor &operator++()
            {
                slot = iter->next();
                return *this;
            }
            MLW_FORCE_INLINE bool operator!=(const End &) const { return slot.isSome(); }
        };
        Cursor begin() { return {&iter, iter.next()}; } // primed with the first pull
        End end() { return {}; }
    };

    /// \brief Free-function form of \ref IterBase::range.
    template <ItoratorType It>
    RangeFor<It> range_for(It &&iter) { return {core::forward<It>(iter)}; }

    // -------------------------------------------------------------------------
    //  Adapters
    // -------------------------------------------------------------------------

    /// \brief Yields `{ index, value }` pairs, numbering items from 0.
    /// \tparam It Upstream iterator type.
    template <ItoratorType It>
    struct Enumerate : IterBase<Enumerate<It>>
    {
        isize index;
        It itor;

        explicit Enumerate(It &&i) : index(0), itor(core::move(i)) {}

        /// \brief The yielded pair. `value` mirrors the upstream `Item` category
        ///        (a reference for a borrowing upstream, a value for an owning one).
        struct Slot
        {
            isize index;
            typename It::Item value;
        };
        using Item = Slot;

        core::Optional<Item> next()
        {
            core::Optional<typename It::Item> x = itor.next();
            if (!x)
                return {};
            if constexpr (core::is_reference_v<typename It::Item>)
                // borrow: bind the reference (moving would fail to bind T& from an rvalue)
                return core::Optional<Item>{core::optional_in_place, Slot{index++, *x}};
            else
                // owned: move the value out of the upstream's slot
                return core::Optional<Item>{core::optional_in_place, Slot{index++, core::move(*x)}};
        }
    };

    /// \brief Yields at most \p n items, then stops **without** pulling the
    ///        upstream again — the mechanism that makes partial consumption of
    ///        large or infinite sources possible.
    /// \tparam It Upstream iterator type.
    template <ItoratorType It>
    struct Take : IterBase<Take<It>>
    {
        It itor;
        isize remaining;

        Take(It &&i, isize n) : itor(core::move(i)), remaining(n) {}

        using Item = typename It::Item; // passthrough — preserves ref/value category

        core::Optional<Item> next()
        {
            if (remaining <= 0)
                return {}; // stop; upstream is never pulled again
            --remaining;
            return itor.next(); // may itself be None if the upstream ran out first
        }
    };

    /// \brief Transforms each item via \p fn; `Item` is whatever `fn` returns,
    ///        always stored **by value**.
    /// \tparam It Upstream iterator type.
    /// \tparam F  Callable; the new item type is `remove_ref_t<result>`.
    ///
    /// \par Calling convention
    ///   For an *owned* upstream item whose `fn` accepts an rvalue, the item is
    ///   **moved** into `fn` (so a by-value move-only parameter works). Otherwise
    ///   `fn` receives an lvalue reference to the slot — this covers a borrowed
    ///   item (never moved out of the container) and an `fn` that wants `T&`.
    ///
    /// \warning `Item` is always a value: if `fn` returns a reference, the result
    ///          is **copied** into the slot, not aliased. To transform an owned
    ///          item cheaply, take by value (or `&&`) and return by value.
    template <ItoratorType It, typename F>
        requires(core::is_invocable_v<F, typename It::Item &> ||
                 core::is_invocable_v<F, typename It::Item &&>)
    struct Map : IterBase<Map<It, F>>
    {
        It itor;
        F fn;

        Map(It &&i, F &&f) : itor(core::move(i)), fn(core::move(f)) {}

        /// \brief True iff the upstream item is owned *and* `fn` accepts an
        ///        rvalue — the only case where we may move the item into `fn`.
        static constexpr bool move_item =
            !core::is_reference_v<typename It::Item> &&
            core::is_invocable_v<F, typename It::Item &&>;

        /// \brief The exact argument category `fn` is invoked with.
        using Arg = core::conditional_t<move_item,
                                        typename It::Item &&,
                                        typename It::Item &>;

        /// \brief The mapped item type, as a value (a mapper returning a
        ///        reference would risk dangling; store the result by value).
        using Item = core::remove_ref_t<core::invoke_result_t<F, Arg>>;
        static_assert(!core::is_same_v<Item, void>,
                      "map() function must return a value; use forEach()/tap() for side effects.");

        core::Optional<Item> next()
        {
            core::Optional<typename It::Item> x = itor.next();
            if (!x)
                return {};
            if constexpr (move_item)
                return core::Optional<Item>{core::optional_in_place, fn(core::move(*x))};
            else
                return core::Optional<Item>{core::optional_in_place, fn(*x)};
        }
    };

    /// \brief Yields only items for which \p pred returns true, unchanged.
    /// \tparam It Upstream iterator type.
    /// \tparam P  Predicate `const T& -> bool` — inspects only, never mutates.
    ///
    /// \note \p pred receives `const remove_ref_t<Item>&`, i.e. a genuine
    ///       `const T&` for both borrow and owned sources (a plain `const Item&`
    ///       would collapse away the const for a borrowing upstream). A mutating
    ///       `[](T&)` predicate is therefore rejected; a trivial by-value
    ///       `[](int)` is still accepted (it just copies).
    template <ItoratorType It, typename P>
        requires core::is_invocable_v<P, const core::remove_ref_t<typename It::Item> &> &&
                 core::is_same_v<bool,
                                 core::invoke_result_t<P, const core::remove_ref_t<typename It::Item> &>>
    struct Filter : IterBase<Filter<It, P>>
    {
        It itor;
        P pred;

        Filter(It &&i, P &&p) : itor(core::move(i)), pred(core::move(p)) {}

        using Item = typename It::Item; // passthrough — preserves ref/value category

        core::Optional<Item> next()
        {
            for (;;)
            {
                core::Optional<Item> x = itor.next();
                if (!x)
                    return {}; // upstream exhausted
                if (pred(*x))
                    return x; // matched — pass through unchanged (no copy/move)
                // else: keep pulling
            }
        }
    };

    /// \brief Runs \p fn on each item for its side effect, then yields that same
    ///        item onward **unchanged**, preserving the upstream Item category.
    ///
    /// Over a borrowing source `fn` gets a `T&` into the container, so mutations
    /// land in place; over an owning source it gets a `T&` to the in-flight
    /// value, which then moves on down the chain. \p fn must return `void`, so
    /// this cannot stand in for `map` (no new value is ever produced) and thus
    /// cannot dangle.
    /// \tparam It Upstream iterator type.  \tparam F Callable `T& -> void`.
    template <ItoratorType It, typename F>
        requires core::is_invocable_v<F, typename It::Item &> &&
                 core::is_same_v<void, core::invoke_result_t<F, typename It::Item &>>
    struct Tap : IterBase<Tap<It, F>>
    {
        It itor;
        F fn;

        Tap(It &&i, F &&f) : itor(core::move(i)), fn(core::move(f)) {}

        using Item = typename It::Item; // passthrough — preserves ref/value category

        core::Optional<Item> next()
        {
            core::Optional<Item> x = itor.next();
            if (x.isSome())
                fn(*x); // *x is an lvalue: T& for a borrow, or the owned slot's T&
            return x;   // borrow: ref passes on; owned: implicit-move on return
        }
    };

    /// \brief Non-owning view over another iterator: holds `Upstream&` and forwards
    ///        `next()`. Lets you consume an iterator *partially* and keep using the
    ///        original afterwards (cf. Rust's `Iterator::by_ref`).
    /// \note This is the one adapter that stores a reference (as a pointer, so it
    ///       stays movable). The referred-to iterator must outlive this view and the
    ///       whole chain built from it — which it does when you obtain one via
    ///       \ref IterBase::byRef on a named lvalue in an enclosing scope.
    template <ItoratorType It>
    struct RefIter : IterBase<RefIter<It>>
    {
        It *itor; // never null; pointer (not It&) so RefIter is movable/assignable
        explicit RefIter(It &i) : itor(&i) {}
        using Item = typename It::Item; // passthrough — preserves ref/value category
        core::Optional<Item> next() { return itor->next(); }
    };

    // -------------------------------------------------------------------------
    //  CRTP base: adapter + consumer methods
    // -------------------------------------------------------------------------

    /// \copydoc IterBase
    ///
    /// \note All chaining/consuming methods are `&&`-qualified: they consume the
    ///       iterator. The adapter-returning methods use `auto` return types so
    ///       that `Enumerate<Self>` / `Take<Self>` / `RangeFor<Self>` are formed
    ///       at *call* time (when `Self` is complete) rather than while `Self` is
    ///       still an incomplete CRTP base — see the file's design notes.
    template <typename Self>
    struct IterBase
    {
        /// \brief Recover the concrete derived iterator (CRTP downcast).
        Self &self() { return static_cast<Self &>(*this); }

        // --- range-based for -------------------------------------------------

        /// \brief Adapt to `begin()/end()` for use in a range-based `for`.
        RangeFor<Self> range() && { return RangeFor<Self>{core::move(self())}; }

        // --- adapters (lazy) -------------------------------------------------

        /// \brief Drop the first \p n items eagerly, then continue the chain.
        Self skip(isize n) &&
        {
            for (isize i = 0; i < n; ++i)
                if (!self().next())
                    break;
            return core::move(self());
        }

        /// \brief Borrow this iterator as a non-owning view so it can be consumed
        ///        partially; the original stays usable afterwards. `&`-qualified — the
        ///        only chaining method callable on a named lvalue without core::move.
        auto byRef() & { return RefIter<Self>{self()}; }

        /// \brief Number items from 0, yielding `{ index, value }`.
        auto enumerate() && { return Enumerate<Self>{core::move(self())}; }

        /// \brief Yield at most \p n items, then stop.
        auto take(isize n) && { return Take<Self>{core::move(self()), n}; }

        /// \brief Transform each item via \p fn.
        /// \tparam F Callable invoked with the item by value/rvalue or by lvalue
        ///           ref (see \ref Map); its result becomes the new item.
        template <typename F>
            requires(core::is_invocable_v<F, typename Self::Item &> ||
                     core::is_invocable_v<F, typename Self::Item &&>)
        Map<Self, F> map(F fn) &&
        {
            return Map<Self, F>{core::move(self()), core::move(fn)};
        }

        /// \brief Keep only items satisfying \p pred.
        /// \tparam P Predicate `const T& -> bool` (inspects, never mutates).
        template <typename P>
            requires core::is_invocable_v<P, const core::remove_ref_t<typename Self::Item> &>
        Filter<Self, P> filter(P pred) &&
        {
            return Filter<Self, P>{core::move(self()), core::move(pred)};
        }

        /// \brief Run \p fn on each item for its side effect, passing it onward
        ///        unchanged (lazy). \tparam F Callable `T& -> void`.
        template <typename F>
            requires core::is_invocable_v<F, typename Self::Item &> &&
                     core::is_same_v<void, core::invoke_result_t<F, typename Self::Item &>>
        Tap<Self, F> tap(F fn) &&
        {
            return Tap<Self, F>{core::move(self()), core::move(fn)};
        }

        // --- consumers (eager) ----------------------------------------------

        /// \brief Drain the whole iterator into an owning container \p Out.
        ///
        /// Copies items from a borrowing source (they belong to the container),
        /// moves items from an owning source. \p Out must expose `push`.
        /// \tparam Out Target container (e.g. `Vector<T>`).
        template <typename Out>
        Out collect() &&
        {
            Out out;
            Self &s = self();
            for (;;)
            {
                core::Optional<typename Self::Item> v = s.next();
                if (!v)
                    break;
                if constexpr (core::is_reference_v<typename Self::Item>)
                {
                    static_assert(core::is_copy_constructible_v<core::remove_ref_t<typename Self::Item>>,
                                  "collect() over a borrowing iterator must copy elements, but this element "
                                  "type is not copyable — use a draining source (into_iter) to move them out.");
                    out.push(*v); // copy: the element belongs to the source
                }
                else
                {
                    out.push(core::move(*v)); // move: we own the item
                }
            }
            return out; // NRVO
        }

        /// \brief Run \p fn for its side effects on every item (eager sibling of
        ///        \ref tap). Returns nothing.
        /// \tparam Fn Callable `T& -> void`.
        template <typename Fn>
            requires core::is_invocable_v<Fn, typename Self::Item &> &&
                     core::is_same_v<void, core::invoke_result_t<Fn, typename Self::Item &>>
        void forEach(Fn fn) &&
        {
            for (;;)
            {
                core::Optional<typename Self::Item> v = self().next();
                if (!v)
                    break;
                fn(*v);
            }
        }

        /// \brief First item satisfying \p pred, or `None`. Stops on the first hit.
        /// \tparam P Predicate `const T& -> bool`.
        template <typename P>
            requires core::is_invocable_v<P, const core::remove_ref_t<typename Self::Item> &>
        auto find(P pred) &&
        {
            for (;;)
            {
                core::Optional<typename Self::Item> v = self().next();
                if (!v)
                    return core::Optional<typename Self::Item>{};
                if (pred(*v))
                    return v; // stop
            }
        }

        /// \brief True if any item satisfies \p pred. Stops on the first hit.
        /// \tparam P Predicate `const T& -> bool`.
        template <typename P>
            requires core::is_invocable_v<P, const core::remove_ref_t<typename Self::Item> &>
        bool any(P pred) &&
        {
            for (;;)
            {
                core::Optional<typename Self::Item> v = self().next();
                if (!v)
                    return false;
                if (pred(*v))
                    return true; // stop
            }
        }

        /// \brief True if every item satisfies \p pred. Stops on the first miss.
        /// \tparam P Predicate `const T& -> bool`.
        template <typename P>
            requires core::is_invocable_v<P, const core::remove_ref_t<typename Self::Item> &>
        bool all(P pred) &&
        {
            for (;;)
            {
                core::Optional<typename Self::Item> v = self().next();
                if (!v)
                    return true; // exhausted with no failures
                if (!pred(*v))
                    return false; // stop on first miss
            }
        }

        /// \brief Count the items by draining the iterator.
        isize count() &&
        {
            isize n = 0;
            while (self().next())
                ++n;
            return n;
        }
    };

} // namespace itorator