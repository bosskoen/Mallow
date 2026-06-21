#pragma once

#include "atomic_op.h"
#include "../macro.h"

namespace core::sync
{

    template <typename T>
    concept AtomicEligible =
        is_same_v<T, remove_cv_t<T>> &&
        (sizeof(T) == 1 || sizeof(T) == 2 || sizeof(T) == 4 || sizeof(T) == 8) && sizeof(T) <= sizeof(usize) &&
        (core::is_integer_v<T> || core::is_float_v<T> || core::is_pointer_v<T> || core::is_bool_v<T>);

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
        // asinments

        const T *rawPtr() const noexcept
        {
            return &value;
        }

        // Explicit escape hatch — caller acknowledges they're doing something unsafe
        T *rawPtrUnsafe() noexcept
        {
            return &value;
        }

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
        MLW_FORCE_INLINE T operator=(T v) noexcept { return store(v); };

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
        MLW_FORCE_INLINE operator T() const noexcept { return load(); };

        MLW_FORCE_INLINE T exchange(T val, MemoryOrder m = MemoryOrder::AcqRel) noexcept
        {
            return mlwExchange(&value, val, m);
        }

        // integer stuf
        T operator++() noexcept
            requires core::is_integer_v<T>
        {
            return mlwAddFetch(&value, 1, MemoryOrder::AcqRel);
        }
        T operator++(int) noexcept
            requires core::is_integer_v<T>
        {
            return mlwFetchAdd(&value, 1, MemoryOrder::AcqRel);
        }
        T operator--() noexcept
            requires core::is_integer_v<T>
        {
            return mlwSubFetch(&value, 1, MemoryOrder::AcqRel);
        }
        T operator--(int) noexcept
            requires core::is_integer_v<T>
        {
            return mlwFetchSub(&value, 1, MemoryOrder::AcqRel);
        }

        MLW_FORCE_INLINE T operator+=(T val) noexcept
            requires core::is_integer_v<T>
        {
            return mlwAddFetch(&value, val, MemoryOrder::AcqRel);
        }

        MLW_FORCE_INLINE T operator-=(T val) noexcept
            requires core::is_integer_v<T>
        {
            return mlwSubFetch(&value, val, MemoryOrder::AcqRel);
        }

        MLW_FORCE_INLINE T operator&=(T val) noexcept
            requires core::is_integer_v<T>
        {
            return mlwAndFetch(&value, val, MemoryOrder::AcqRel);
        }

        MLW_FORCE_INLINE T operator|=(T val) noexcept
            requires core::is_integer_v<T>
        {
            return mlwOrFetch(&value, val, MemoryOrder::AcqRel);
        }

        MLW_FORCE_INLINE T operator^=(T val) noexcept
            requires core::is_integer_v<T>
        {
            return mlwXorFetch(&value, val, MemoryOrder::AcqRel);
        }

        MLW_FORCE_INLINE T fetchAdd(T val, MemoryOrder order) noexcept
            requires core::is_integer_v<T>
        {
            return mlwFetchAdd(&value, val, order);
        }

        MLW_FORCE_INLINE T addFetch(T val, MemoryOrder order) noexcept
            requires core::is_integer_v<T>
        {
            return mlwAddFetch(&value, val, order);
        }

        MLW_FORCE_INLINE T fetchSub(T val, MemoryOrder order) noexcept
            requires core::is_integer_v<T>
        {
            return mlwFetchSub(&value, val, order);
        }

        MLW_FORCE_INLINE T subFetch(T val, MemoryOrder order) noexcept
            requires core::is_integer_v<T>
        {
            return mlwSubFetch(&value, val, order);
        }

        MLW_FORCE_INLINE T fetchAnd(T val, MemoryOrder order) noexcept
            requires core::is_integer_v<T>
        {
            return mlwFetchAnd(&value, val, order);
        }

        MLW_FORCE_INLINE T andFetch(T val, MemoryOrder order) noexcept
            requires core::is_integer_v<T>
        {
            return mlwAndFetch(&value, val, order);
        }

        MLW_FORCE_INLINE T fetchOr(T val, MemoryOrder order) noexcept
            requires core::is_integer_v<T>
        {
            return mlwFetchOr(&value, val, order);
        }

        MLW_FORCE_INLINE T orFetch(T val, MemoryOrder order) noexcept
            requires core::is_integer_v<T>
        {
            return mlwOrFetch(&value, val, order);
        }

        MLW_FORCE_INLINE T fetchXor(T val, MemoryOrder order) noexcept
            requires core::is_integer_v<T>
        {
            return mlwFetchXor(&value, val, order);
        }

        MLW_FORCE_INLINE T xorFetch(T val, MemoryOrder order) noexcept
            requires core::is_integer_v<T>
        {
            return mlwXorFetch(&value, val, order);
        }

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

        template <core::FormatBuffer Buf>
        void format(Buf &buffer) const
        {
            mlw_write(buffer, "{}", load(MemoryOrder::Relaxed));
        }
    };
} // namespace core::sync
