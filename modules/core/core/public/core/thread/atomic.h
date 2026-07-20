#pragma once

#include "atomic_op.h"
#include "../macro.h"

/// \file
/// \brief Public atomic wrapper type for common atomic operations.
///
/// `core::sync::Atomic<T>` is the normal public interface for atomic values.
/// It uses the low-level primitive helpers in \ref atomic_op.h internally and
/// provides a safer, easier-to-use wrapper for integer, pointer, bool, and
/// floating-point atomics.

namespace core::sync
{

    /// \brief Constraint on the types `Atomic` can wrap.
    ///
    /// Satisfied by an unqualified integer, floating-point, pointer, or bool
    /// type of 1, 2, 4, or 8 bytes that also fits in a machine word. The size
    /// and word limits exist because lock-free hardware atomics only reach up
    /// to a single word; larger or exotic types cannot be made atomic here.
    template <typename T>
    concept AtomicEligible =
        is_same_v<T, remove_cv_t<T>> &&
        (sizeof(T) == 1 || sizeof(T) == 2 || sizeof(T) == 4 || sizeof(T) == 8) && sizeof(T) <= sizeof(usize) &&
        (core::is_integer_v<T> || core::is_float_v<T> || core::is_pointer_v<T> || core::is_bool_v<T>);

    /// \ingroup formattable
    /// \brief Portable atomic wrapper — the preferred public API for atomic values.
    ///
    /// Selects the correct atomic implementation per platform and applies sane
    /// default memory orders so the common acquire/release/seq-cst patterns are
    /// handled correctly.
    ///
    /// \par Guarantees
    /// - **Not copyable or movable.** All four operations are deleted: a value
    ///   cannot be read and written atomically as a single copy step. Pass an
    ///   `Atomic` by reference.
    /// - **Naturally aligned.** Storage is `alignas(sizeof(T))`, which keeps the
    ///   operations lock-free and tear-free on the supported sizes.
    /// - **Default orders:** `store` is Release, `load` is Acquire, and the
    ///   read-modify-write ops (operators, `fetch*`/`*Fetch`, `exchange`) are
    ///   AcqRel. Pass an explicit \ref MemoryOrder to override.
    ///
    /// \note Naming: `fetchX` returns the value *before* the operation; `Xfetch`
    ///       and the compound operators (`++`, `+=`, ...) return the value
    ///       *after*.
    /// \note On MSVC all memory orders are advisory — interlocked operations are
    ///       always sequentially consistent, so a weaker order still emits a
    ///       full barrier. See \ref core::sync::MemoryOrder.
    ///
    /// \tparam T A type satisfying \ref AtomicEligible.
    template <AtomicEligible T>
    struct Atomic
    {
    private:
        alignas(sizeof(T)) T value;

    public:
        // constructing
        Atomic() = default;
        explicit Atomic(T val) : value(val) {};
        Atomic(const Atomic &) = delete;
        Atomic &operator=(const Atomic &) = delete;
        Atomic(Atomic &&) = delete;
        Atomic &operator=(Atomic &&) = delete;
        // assignments

        /// \brief Get a const pointer to the underlying storage, for inspection.
        /// \warning Reads through this pointer are ordinary, non-atomic reads and
        ///          are not synchronized with atomic operations on the value.
        const T *rawPtr() const noexcept
        {
            return &value;
        }

        /// \brief Get a mutable pointer to the underlying storage.
        /// \warning Bypasses atomicity entirely. Any access through this pointer
        ///          is unsynchronized; mutating the value this way while other
        ///          threads use the atomic ops is a data race. You own all
        ///          synchronization. Use only when you know exactly why.
        T *rawPtrUnsafe() noexcept
        {
            return &value;
        }

        /// \brief Store `val` with the given ordering (default Release).
        /// \note An order invalid for a store is quietly adjusted: Acquire
        ///       becomes Relaxed and AcqRel becomes Release, since no load side
        ///       exists for a pure store.
        MLW_FORCE_INLINE T store(T val, MemoryOrder order = MemoryOrder::Release) noexcept
        {

            if (order == MemoryOrder::Acquire)
                order = MemoryOrder::Relaxed;

            if (order == MemoryOrder::AcqRel)
                order = MemoryOrder::Release;
#if defined(MLW_MSVC)
            switch (order)
            {
            case MemoryOrder::Relaxed:
                *reinterpret_cast<volatile T *>(&value) = val;
                break;
            case MemoryOrder::Release:
#if defined(MLW_ARM64) || defined(MLW_ARM32)
                mlwExchange(&value, val, MemoryOrder::Release);
#else
                MLW_COMPILER_BARRIER();
                *reinterpret_cast<volatile T *>(&value) = val;
#endif
                break;
            default: // SeqCst
                mlwExchange(&value, val, MemoryOrder::SeqCst);
                break;
            }
#else
            __atomic_store_n(&value, val, static_cast<int>(order));
#endif
            return val;
        }

        /// \brief Assignment shorthand for `store(v)` with the default order.
        /// \note Uses the default Release store; for another order call \ref store.
        MLW_FORCE_INLINE T operator=(T v) noexcept { return store(v); };

        /// \brief Load the value with the given ordering (default Acquire).
        /// \note An order invalid for a load is quietly adjusted: Release becomes
        ///       Relaxed and AcqRel becomes Acquire, since no store side exists
        ///       for a pure load.
        MLW_FORCE_INLINE T load(MemoryOrder order = MemoryOrder::Acquire) const noexcept
        {

            if (order == MemoryOrder::Release)
                order = MemoryOrder::Relaxed;

            if (order == MemoryOrder::AcqRel)
                order = MemoryOrder::Acquire;

#if defined(MLW_MSVC)
#if defined(MLW_ARM64) || defined(MLW_ARM32)
            // ARM needs a real barrier — volatile read alone is not acquire
            T result = *reinterpret_cast<const volatile T *>(&value);
            MLW_FENCE_LOAD();
            return result;
#else
            // x86 TSO — all loads are acquire by default
            // only need compiler barrier for SeqCst to prevent reordering
            T result = *reinterpret_cast<const volatile T *>(&value);
            if (order == MemoryOrder::SeqCst)
                MLW_COMPILER_BARRIER();
            return result;
#endif
#else
            return __atomic_load_n(&value, static_cast<int>(order));
#endif
        }

        /// \brief Implicit conversion, equivalent to `load()` (Acquire).
        /// \note Every use of an `Atomic` in a value context performs an acquire
        ///       load, so `T x = myAtomic;` is not free — it is a synchronized
        ///       read. Call \ref load with Relaxed if you want a cheaper one.
        MLW_FORCE_INLINE operator T() const noexcept { return load(); };

        /// \brief Atomically replace the stored value and return the old value.
        MLW_FORCE_INLINE T exchange(T val, MemoryOrder m = MemoryOrder::AcqRel) noexcept
        {
            return mlwExchange(&value, val, m);
        }

        // integer stuf
        /// \brief Pre-increment the atomic integer and return the new value.
        T operator++() noexcept
            requires core::is_integer_v<T>
        {
            return mlwAddFetch(&value, 1, MemoryOrder::AcqRel);
        }
        /// \brief Post-increment the atomic integer and return the old value.
        T operator++(int) noexcept
            requires core::is_integer_v<T>
        {
            return mlwFetchAdd(&value, 1, MemoryOrder::AcqRel);
        }
        /// \brief Pre-decrement the atomic integer and return the new value.
        T operator--() noexcept
            requires core::is_integer_v<T>
        {
            return mlwSubFetch(&value, 1, MemoryOrder::AcqRel);
        }
        /// \brief Post-decrement the atomic integer and return the old value.
        T operator--(int) noexcept
            requires core::is_integer_v<T>
        {
            return mlwFetchSub(&value, 1, MemoryOrder::AcqRel);
        }

        /// \brief Atomically add `val` and return the new value.
        MLW_FORCE_INLINE T operator+=(T val) noexcept
            requires core::is_integer_v<T>
        {
            return mlwAddFetch(&value, val, MemoryOrder::AcqRel);
        }

        /// \brief Atomically subtract `val` and return the new value.
        MLW_FORCE_INLINE T operator-=(T val) noexcept
            requires core::is_integer_v<T>
        {
            return mlwSubFetch(&value, val, MemoryOrder::AcqRel);
        }

        /// \brief Atomically AND `val` with the current value, returning the new value.
        MLW_FORCE_INLINE T operator&=(T val) noexcept
            requires core::is_integer_v<T>
        {
            return mlwAndFetch(&value, val, MemoryOrder::AcqRel);
        }

        /// \brief Atomically OR `val` into the current value, returning the new value.
        MLW_FORCE_INLINE T operator|=(T val) noexcept
            requires core::is_integer_v<T>
        {
            return mlwOrFetch(&value, val, MemoryOrder::AcqRel);
        }

        /// \brief Atomically XOR `val` with the current value, returning the new value.
        MLW_FORCE_INLINE T operator^=(T val) noexcept
            requires core::is_integer_v<T>
        {
            return mlwXorFetch(&value, val, MemoryOrder::AcqRel);
        }

        /// \brief Atomically add `val`, returning the previous value.
        MLW_FORCE_INLINE T fetchAdd(T val, MemoryOrder order) noexcept
            requires core::is_integer_v<T>
        {
            return mlwFetchAdd(&value, val, order);
        }

        /// \brief Atomically add `val`, returning the new value.
        MLW_FORCE_INLINE T addFetch(T val, MemoryOrder order) noexcept
            requires core::is_integer_v<T>
        {
            return mlwAddFetch(&value, val, order);
        }

        /// \brief Atomically subtract `val`, returning the previous value.
        MLW_FORCE_INLINE T fetchSub(T val, MemoryOrder order) noexcept
            requires core::is_integer_v<T>
        {
            return mlwFetchSub(&value, val, order);
        }

        /// \brief Atomically subtract `val`, returning the new value.
        MLW_FORCE_INLINE T subFetch(T val, MemoryOrder order) noexcept
            requires core::is_integer_v<T>
        {
            return mlwSubFetch(&value, val, order);
        }

        /// \brief Atomically AND `val` with the current value, returning the previous value.
        MLW_FORCE_INLINE T fetchAnd(T val, MemoryOrder order) noexcept
            requires core::is_integer_v<T>
        {
            return mlwFetchAnd(&value, val, order);
        }

        /// \brief Atomically AND `val` with the current value, returning the new value.
        MLW_FORCE_INLINE T andFetch(T val, MemoryOrder order) noexcept
            requires core::is_integer_v<T>
        {
            return mlwAndFetch(&value, val, order);
        }

        /// \brief Atomically OR `val` into the current value, returning the previous value.
        MLW_FORCE_INLINE T fetchOr(T val, MemoryOrder order) noexcept
            requires core::is_integer_v<T>
        {
            return mlwFetchOr(&value, val, order);
        }

        /// \brief Atomically OR `val` into the current value, returning the new value.
        MLW_FORCE_INLINE T orFetch(T val, MemoryOrder order) noexcept
            requires core::is_integer_v<T>
        {
            return mlwOrFetch(&value, val, order);
        }

        /// \brief Atomically XOR `val` with the current value, returning the previous value.
        MLW_FORCE_INLINE T fetchXor(T val, MemoryOrder order) noexcept
            requires core::is_integer_v<T>
        {
            return mlwFetchXor(&value, val, order);
        }

        /// \brief Atomically XOR `val` with the current value, returning the new value.
        MLW_FORCE_INLINE T xorFetch(T val, MemoryOrder order) noexcept
            requires core::is_integer_v<T>
        {
            return mlwXorFetch(&value, val, order);
        }

        /// \brief Strong compare-and-exchange.
        ///
        /// If the stored value equals `expected`, replaces it with `desired` and
        /// returns true. Otherwise loads the current value into `expected` and
        /// returns false.
        ///
        /// \param success  Ordering applied when the exchange succeeds.
        /// \param fail     Ordering applied when it fails (a pure load).
        ///
        /// \note The failure order is normalized: it is first clamped to be no
        ///       stronger than `success`, then release semantics are stripped
        ///       (Release becomes Relaxed, AcqRel becomes Acquire) because no
        ///       store happens on the failure path.
        MLW_FORCE_INLINE bool compareExchangeStrong(T &expected, T desired,
                                                    MemoryOrder success = MemoryOrder::AcqRel, MemoryOrder fail = MemoryOrder::Acquire) noexcept
        {
            // 1. clamp first — before stripping Release semantics
            if (fail > success)
                fail = success;

            // 2. then fix invalid orders for a fail path (no write happened)
            if (fail == MemoryOrder::Release)
                fail = MemoryOrder::Relaxed;
            if (fail == MemoryOrder::AcqRel)
                fail = MemoryOrder::Acquire;
            return mlwCasStrong(&value, expected, desired, success, fail);
        }

        /// \brief Weak compare-and-exchange; may fail spuriously.
        ///
        /// Behaves like \ref compareExchangeStrong but is allowed to fail even
        /// when the values match, so it must be used inside a retry loop. Prefer
        /// the strong form unless a weak CAS loop is specifically better on the
        /// target platform.
        ///
        /// \note The failure order is normalized the same way as in
        ///       \ref compareExchangeStrong.
        MLW_FORCE_INLINE bool compareExchangeWeak(T &expected, T desired,
                                                  MemoryOrder success = MemoryOrder::AcqRel, MemoryOrder fail = MemoryOrder::Acquire) noexcept
        {
            // 1. clamp first — before stripping Release semantics
            if (fail > success)
                fail = success;

            // 2. then fix invalid orders for a fail path (no write happened)
            if (fail == MemoryOrder::Release)
                fail = MemoryOrder::Relaxed;
            if (fail == MemoryOrder::AcqRel)
                fail = MemoryOrder::Acquire;

            return mlwCasWeak(&value, expected, desired, success, fail);
        }

        /// \brief Format the current value.
        /// \note Reads a Relaxed snapshot for printing; this is a racy read, not
        ///       a synchronization point, and establishes no ordering.
        template <core::FormatBuffer Buf>
        void format(Buf &buffer) const
        {
            mlw_write(buffer, "{}", load(MemoryOrder::Relaxed));
        }
    };
} // namespace core::sync