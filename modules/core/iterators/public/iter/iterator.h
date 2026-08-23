#pragma once

#include <core/compilers.h>
#include <core/optional.h>
#include <core/traits.h>

namespace itorator
{

    template <typename It>
    concept ItoratorType = requires(It t) {
        typename It::Item;
        { t.next() } -> core::same_as<core::Optional<typename It::Item>>;
    };

    template <typename Self>
    struct IterBase;

    template <typename Cur>
    struct BorrowIter : IterBase<BorrowIter<Cur>>
    {
        Cur cur, stop;
        BorrowIter(Cur begin, Cur end) : cur(begin), stop(end) {}
        using Item = core::remove_ref_t<decltype(*cur)> &; // yield T& deliberately
        core::Optional<Item> next()
        {
            if (cur == stop)
                return {};
            Item ref = *cur; // binds the reference; storage is stable for a container cursor
            ++cur;
            return ref;
        }
    };

    template <typename C>
    BorrowIter<decltype(core::declval<C &>().begin())> borrow_iter(C &c)
    {
        return {c.begin(), c.end()};
    }

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
            decltype(auto) operator*() { return *slot; }
            MLW_FORCE_INLINE Cursor &operator++()
            {
                slot = iter->next();
                return *this;
            }
            MLW_FORCE_INLINE bool operator!=(const End &) { return slot.isSome(); }
        };
        Cursor begin() { return {&iter, iter.next()}; }
        End end() { return {}; }
    };

    template <ItoratorType It>
    RangeFor<It> range_for(It &&iter) { return {core::forward<It>(iter)}; }

    template <typename Drainer>
    struct IntoIter : IterBase<IntoIter<Drainer>>
    {
        Drainer d;
        using Item = typename Drainer::Item;
        IntoIter(Drainer &&drainer) : d(core::move(drainer)) {}
        core::Optional<Item> next() { return d.next(); }
    };

    template <typename C>
    auto into_iter(C &&c)
    {                                           // the chainable public entry, in iter
        return IntoIter{core::move(c).drain()}; // move c, drain it, wrap for chaining
    }

    template <typename Self>
    struct IterBase
    {
        Self &self() { return static_cast<Self &>(*this); }

        RangeFor<Self> range() { return RangeFor<Self>{self()}; }

        // colect
        template <typename Out>
        Out collect()
        {
            Out out;
            Self &s = self();
            for (;;)
            {
                core::Optional<typename Self::Item> v = s.next();
                if (!v)
                    break;
                if constexpr (core::is_reference_v<typename Self::Item>)
                { // Item = T& → borrow → must copy
                    static_assert(core::is_copy_constructible_v<core::remove_ref_t<typename Self::Item>>,
                                  "collect() over a borrowing iterator must copy elements, but this element type "
                                  "is not copyable — use a draining source (into_iter/drain) to move them out instead.");
                    out.push(*v); // copy: the element belongs to the source
                }
                else
                {
                    // Item = T  → owned → safe to move
                    out.push(core::move(*v)); // move out of the slot
                }
            }
            return out; // NRVO
        }
        // map ??
        // filter ??
    };

    // for consuming
    // core::Optional<Item> next() {
    // core::Optional<Item> out;                 // empty
    // if (cur == stop) return out;              // NRVO applies HERE (same type) ✓
    // out.emplace(core::move(*cur));            // ONE move: element → Optional storage, in place
    // ++cur;
    // return out;

    // NRVO — no move of the Optional

} // namespace itorator
