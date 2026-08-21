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

    template <typename Cur>
    struct BorrowIter
    {
        Cur cur, stop;
        using Item = core::remove_ref_t<decltype(*cur)>&; // yield T& deliberately
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
    BorrowIter<decltype(core::declval<C>().begin())> borrow_iter(C &c)
    {
        return {c.begin(), c.end()};
    }

    template <ItoratorType It>
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


    // for consuming
    // core::Optional<Item> next() {
    // core::Optional<Item> out;                 // empty
    // if (cur == stop) return out;              // NRVO applies HERE (same type) ✓
    // out.emplace(core::move(*cur));            // ONE move: element → Optional storage, in place
    // ++cur;
    // return out;  
    
    // NRVO — no move of the Optional


} // namespace itorator
