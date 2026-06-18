#pragma once
#include "compilers.h"
#include "traits.h"

namespace core::sync
{
    enum class MemoryOrder : int32
    {
        Relaxed = MLW_MO_RELAXED,
        Acquire = MLW_MO_ACQUIRE,
        Release = MLW_MO_RELEASE,
        AcqRel = MLW_MO_ACQ_REL,
        SeqCst = MLW_MO_SEQ_CST,
    };
    template <typename T>
        requires core::is_integer_v<T> && (sizeof(T) == 8 || sizeof(T) == 4 || sizeof(T) == 2 || sizeof(T) == 1)
    MLW_FORCE_INLINE T mlwFetchAdd(T *ptr, T value, MemoryOrder order = MemoryOrder::SeqCst)
    {
#if defined(MLW_MSVC)
        if constexpr (sizeof(T) == 1)
        {
#if defined(MLW_ARM64) || defined(MLW_ARM32)
            switch (order)
            {
            case MemoryOrder::Relaxed:
                return static_cast<T>(_InterlockedExchangeAdd8_nf(reinterpret_cast<volatile char *>(ptr), static_cast<char>(value)));
            case MemoryOrder::Acquire:
                return static_cast<T>(_InterlockedExchangeAdd8_acq(reinterpret_cast<volatile char *>(ptr), static_cast<char>(value)));
            case MemoryOrder::Release:
                return static_cast<T>(_InterlockedExchangeAdd8_rel(reinterpret_cast<volatile char *>(ptr), static_cast<char>(value)));
            default:
                return static_cast<T>(_InterlockedExchangeAdd8(reinterpret_cast<volatile char *>(ptr), static_cast<char>(value)));
            }
#else
            return static_cast<T>(_InterlockedExchangeAdd8(reinterpret_cast<volatile char *>(ptr), static_cast<char>(value)));
#endif
        }
        else if constexpr (sizeof(T) == 2)
        {
#if defined(MLW_ARM64) || defined(MLW_ARM32)
            switch (order)
            {
            case MemoryOrder::Relaxed:
                return static_cast<T>(_InterlockedExchangeAdd16_nf(reinterpret_cast<volatile short *>(ptr), static_cast<short>(value)));
            case MemoryOrder::Acquire:
                return static_cast<T>(_InterlockedExchangeAdd16_acq(reinterpret_cast<volatile short *>(ptr), static_cast<short>(value)));
            case MemoryOrder::Release:
                return static_cast<T>(_InterlockedExchangeAdd16_rel(reinterpret_cast<volatile short *>(ptr), static_cast<short>(value)));
            default:
                return static_cast<T>(_InterlockedExchangeAdd16(reinterpret_cast<volatile short *>(ptr), static_cast<short>(value)));
            }
#else
            return static_cast<T>(_InterlockedExchangeAdd16(reinterpret_cast<volatile short *>(ptr), static_cast<short>(value)));
#endif
        }
        else if constexpr (sizeof(T) == 4)
        {
#if defined(MLW_ARM64) || defined(MLW_ARM32)
            switch (order)
            {
            case MemoryOrder::Relaxed:
                return static_cast<T>(_InterlockedExchangeAdd_nf(reinterpret_cast<volatile long *>(ptr), static_cast<long>(value)));
            case MemoryOrder::Acquire:
                return static_cast<T>(_InterlockedExchangeAdd_acq(reinterpret_cast<volatile long *>(ptr), static_cast<long>(value)));
            case MemoryOrder::Release:
                return static_cast<T>(_InterlockedExchangeAdd_rel(reinterpret_cast<volatile long *>(ptr), static_cast<long>(value)));
            default:
                return static_cast<T>(_InterlockedExchangeAdd(reinterpret_cast<volatile long *>(ptr), static_cast<long>(value)));
            }
#else

            return static_cast<T>(_InterlockedExchangeAdd(reinterpret_cast<volatile long *>(ptr), static_cast<long>(value)));
#endif
        }
        else
        {
#if defined(MLW_ARM64) || defined(MLW_ARM32)
            switch (order)
            {
            case MemoryOrder::Relaxed:
                return static_cast<T>(_InterlockedExchangeAdd64_nf(reinterpret_cast<volatile __int64 *>(ptr), static_cast<__int64>(value)));
            case MemoryOrder::Acquire:
                return static_cast<T>(_InterlockedExchangeAdd64_acq(reinterpret_cast<volatile __int64 *>(ptr), static_cast<__int64>(value)));
            case MemoryOrder::Release:
                return static_cast<T>(_InterlockedExchangeAdd64_rel(reinterpret_cast<volatile __int64 *>(ptr), static_cast<__int64>(value)));
            default:
                return static_cast<T>(_InterlockedExchangeAdd64(reinterpret_cast<volatile __int64 *>(ptr), static_cast<__int64>(value)));
            }
#else
            return static_cast<T>(_InterlockedExchangeAdd64(reinterpret_cast<volatile __int64 *>(ptr), static_cast<__int64>(value)));
#endif
        }
#else
        return static_cast<T>(__atomic_fetch_add(ptr, value, static_cast<int>(order)));
#endif
    }

    template <typename T>
        requires core::is_integer_v<T> && (sizeof(T) == 8 || sizeof(T) == 4 || sizeof(T) == 2 || sizeof(T) == 1)
    MLW_FORCE_INLINE T mlwAddFetch(T *ptr, T value, MemoryOrder order = MemoryOrder::SeqCst)
    {
        return mlwFetchAdd(ptr, value, order) + value;
    }

    template <typename T>
        requires core::is_integer_v<T> && (sizeof(T) == 8 || sizeof(T) == 4 || sizeof(T) == 2 || sizeof(T) == 1)
    MLW_FORCE_INLINE T mlwFetchSub(T *ptr, T value, MemoryOrder order = MemoryOrder::SeqCst)
    {
#if defined(MLW_MSVC)
        // negating and calling add gives the correct fetch_sub result
        // works for both signed and unsigned since two's complement wraps correctly
        return mlwFetchAdd(ptr, static_cast<T>(0 - value), order);
#else
        return static_cast<T>(__atomic_fetch_sub(ptr, value, static_cast<int>(order)));
#endif
    }

    template <typename T>
        requires core::is_integer_v<T> && (sizeof(T) == 8 || sizeof(T) == 4 || sizeof(T) == 2 || sizeof(T) == 1)
    MLW_FORCE_INLINE T mlwSubFetch(T *ptr, T value, MemoryOrder order = MemoryOrder::SeqCst)
    {
        return mlwFetchSub(ptr, value, order) - value;
    }

    template <typename T>
        requires(sizeof(T) == 8 || sizeof(T) == 4 || sizeof(T) == 2 || sizeof(T) == 1)
    MLW_FORCE_INLINE T mlwExchange(T *ptr, T value, MemoryOrder order = MemoryOrder::SeqCst)
    {
#if defined(MLW_MSVC)
        // no 8-bit exchange on MSVC — widen to 16
        if constexpr (sizeof(T) == 1)
        {
#if defined(MLW_ARM64) || defined(MLW_ARM32)
            switch (order)
            {
            case MemoryOrder::Relaxed:
                return static_cast<T>(_InterlockedExchange8_nf(reinterpret_cast<volatile char *>(ptr), static_cast<char>(value)));
            case MemoryOrder::Acquire:
                return static_cast<T>(_InterlockedExchange8_acq(reinterpret_cast<volatile char *>(ptr), static_cast<char>(value)));
            case MemoryOrder::Release:
                return static_cast<T>(_InterlockedExchange8_rel(reinterpret_cast<volatile char *>(ptr), static_cast<char>(value)));
            default:
                return static_cast<T>(_InterlockedExchange8(reinterpret_cast<volatile char *>(ptr), static_cast<char>(value)));
            }
#else

            return static_cast<T>(_InterlockedExchange8(reinterpret_cast<volatile char *>(ptr), static_cast<char>(value)));
#endif
        }
        else if constexpr (sizeof(T) == 2)
        {
#if defined(MLW_ARM64) || defined(MLW_ARM32)
            switch (order)
            {
            case MemoryOrder::Relaxed:
                return static_cast<T>(_InterlockedExchange16_nf(reinterpret_cast<volatile short *>(ptr), static_cast<short>(value)));
            case MemoryOrder::Acquire:
                return static_cast<T>(_InterlockedExchange16_acq(reinterpret_cast<volatile short *>(ptr), static_cast<short>(value)));
            case MemoryOrder::Release:
                return static_cast<T>(_InterlockedExchange16_rel(reinterpret_cast<volatile short *>(ptr), static_cast<short>(value)));
            default:
                return static_cast<T>(_InterlockedExchange16(reinterpret_cast<volatile short *>(ptr), static_cast<short>(value)));
            }
#else
            return static_cast<T>(_InterlockedExchange16(reinterpret_cast<volatile short *>(ptr), static_cast<short>(value)));
#endif
        }
        else if constexpr (sizeof(T) == 4)
        {
#if defined(MLW_ARM64) || defined(MLW_ARM32)
            switch (order)
            {
            case MemoryOrder::Relaxed:
                return static_cast<T>(_InterlockedExchange_nf(reinterpret_cast<volatile long *>(ptr), static_cast<long>(value)));
            case MemoryOrder::Acquire:
                return static_cast<T>(_InterlockedExchange_acq(reinterpret_cast<volatile long *>(ptr), static_cast<long>(value)));
            case MemoryOrder::Release:
                return static_cast<T>(_InterlockedExchange_rel(reinterpret_cast<volatile long *>(ptr), static_cast<long>(value)));
            default:
                return static_cast<T>(_InterlockedExchange(reinterpret_cast<volatile long *>(ptr), static_cast<long>(value)));
            }
#else
            return static_cast<T>(_InterlockedExchange(reinterpret_cast<volatile long *>(ptr), static_cast<long>(value)));
#endif
        }
        else
        {
#if defined(MLW_ARM64) || defined(MLW_ARM32)
            switch (order)
            {
            case MemoryOrder::Relaxed:
                return static_cast<T>(_InterlockedExchange64_nf(reinterpret_cast<volatile __int64 *>(ptr), static_cast<__int64>(value)));
            case MemoryOrder::Acquire:
                return static_cast<T>(_InterlockedExchange64_acq(reinterpret_cast<volatile __int64 *>(ptr), static_cast<__int64>(value)));
            case MemoryOrder::Release:
                return static_cast<T>(_InterlockedExchange64_rel(reinterpret_cast<volatile __int64 *>(ptr), static_cast<__int64>(value)));
            default:
                return static_cast<T>(_InterlockedExchange64(reinterpret_cast<volatile __int64 *>(ptr), static_cast<__int64>(value)));
            }
#else
            return static_cast<T>(_InterlockedExchange64(reinterpret_cast<volatile __int64 *>(ptr), static_cast<__int64>(value)));
#endif
        }
#else
        return static_cast<T>(__atomic_exchange_n(ptr, value, static_cast<int>(order)));
#endif
    }

    template <typename T>
        requires core::is_integer_v<T> && (sizeof(T) == 8 || sizeof(T) == 4 || sizeof(T) == 2 || sizeof(T) == 1)
    MLW_FORCE_INLINE T mlwFetchAnd(T *ptr, T value, MemoryOrder order = MemoryOrder::SeqCst)
    {
#if defined(MLW_MSVC)
        if constexpr (sizeof(T) == 1)
        {
#if defined(MLW_ARM64) || defined(MLW_ARM32)
            switch (order)
            {
            case MemoryOrder::Relaxed:
                return static_cast<T>(_InterlockedAnd8_nf(reinterpret_cast<volatile char *>(ptr), static_cast<char>(value)));
            case MemoryOrder::Acquire:
                return static_cast<T>(_InterlockedAnd8_acq(reinterpret_cast<volatile char *>(ptr), static_cast<char>(value)));
            case MemoryOrder::Release:
                return static_cast<T>(_InterlockedAnd8_rel(reinterpret_cast<volatile char *>(ptr), static_cast<char>(value)));
            default:
                return static_cast<T>(_InterlockedAnd8(reinterpret_cast<volatile char *>(ptr), static_cast<char>(value)));
            }
#else
            return static_cast<T>(_InterlockedAnd8(reinterpret_cast<volatile char *>(ptr), static_cast<char>(value)));
#endif
        }
        else if constexpr (sizeof(T) == 2)
        {
#if defined(MLW_ARM64) || defined(MLW_ARM32)
            switch (order)
            {
            case MemoryOrder::Relaxed:
                return static_cast<T>(_InterlockedAnd16_nf(reinterpret_cast<volatile short *>(ptr), static_cast<short>(value)));
            case MemoryOrder::Acquire:
                return static_cast<T>(_InterlockedAnd16_acq(reinterpret_cast<volatile short *>(ptr), static_cast<short>(value)));
            case MemoryOrder::Release:
                return static_cast<T>(_InterlockedAnd16_rel(reinterpret_cast<volatile short *>(ptr), static_cast<short>(value)));
            default:
                return static_cast<T>(_InterlockedAnd16(reinterpret_cast<volatile short *>(ptr), static_cast<short>(value)));
            }
#else
            return static_cast<T>(_InterlockedAnd16(reinterpret_cast<volatile short *>(ptr), static_cast<short>(value)));
#endif
        }
        else if constexpr (sizeof(T) == 4)
        {
#if defined(MLW_ARM64) || defined(MLW_ARM32)
            switch (order)
            {
            case MemoryOrder::Relaxed:
                return static_cast<T>(_InterlockedAnd_nf(reinterpret_cast<volatile long *>(ptr), static_cast<long>(value)));
            case MemoryOrder::Acquire:
                return static_cast<T>(_InterlockedAnd_acq(reinterpret_cast<volatile long *>(ptr), static_cast<long>(value)));
            case MemoryOrder::Release:
                return static_cast<T>(_InterlockedAnd_rel(reinterpret_cast<volatile long *>(ptr), static_cast<long>(value)));
            default:
                return static_cast<T>(_InterlockedAnd(reinterpret_cast<volatile long *>(ptr), static_cast<long>(value)));
            }
#else
            return static_cast<T>(_InterlockedAnd(reinterpret_cast<volatile long *>(ptr), static_cast<long>(value)));
#endif
        }
        else
        {
#if defined(MLW_ARM64) || defined(MLW_ARM32)
            switch (order)
            {
            case MemoryOrder::Relaxed:
                return static_cast<T>(_InterlockedAnd64_nf(reinterpret_cast<volatile __int64 *>(ptr), static_cast<__int64>(value)));
            case MemoryOrder::Acquire:
                return static_cast<T>(_InterlockedAnd64_acq(reinterpret_cast<volatile __int64 *>(ptr), static_cast<__int64>(value)));
            case MemoryOrder::Release:
                return static_cast<T>(_InterlockedAnd64_rel(reinterpret_cast<volatile __int64 *>(ptr), static_cast<__int64>(value)));
            default:
                return static_cast<T>(_InterlockedAnd64(reinterpret_cast<volatile __int64 *>(ptr), static_cast<__int64>(value)));
            }
#else
            return static_cast<T>(_InterlockedAnd64(reinterpret_cast<volatile __int64 *>(ptr), static_cast<__int64>(value)));
#endif
        }
#else
        return static_cast<T>(__atomic_fetch_and(ptr, value, static_cast<int>(order)));
#endif
    }

    template <typename T>
        requires core::is_integer_v<T> && (sizeof(T) == 8 || sizeof(T) == 4 || sizeof(T) == 2 || sizeof(T) == 1)
    MLW_FORCE_INLINE T mlwAndFetch(T *ptr, T value, MemoryOrder order = MemoryOrder::SeqCst)
    {
        return mlwFetchAnd(ptr, value, order) & value;
    }

    template <typename T>
        requires core::is_integer_v<T> && (sizeof(T) == 8 || sizeof(T) == 4 || sizeof(T) == 2 || sizeof(T) == 1)
    MLW_FORCE_INLINE T mlwFetchOr(T *ptr, T value, MemoryOrder order = MemoryOrder::SeqCst)
    {
#if defined(MLW_MSVC)
        if constexpr (sizeof(T) == 1)
        {
#if defined(MLW_ARM64) || defined(MLW_ARM32)
            switch (order)
            {
            case MemoryOrder::Relaxed:
                return static_cast<T>(_InterlockedOr8_nf(reinterpret_cast<volatile char *>(ptr), static_cast<char>(value)));
            case MemoryOrder::Acquire:
                return static_cast<T>(_InterlockedOr8_acq(reinterpret_cast<volatile char *>(ptr), static_cast<char>(value)));
            case MemoryOrder::Release:
                return static_cast<T>(_InterlockedOr8_rel(reinterpret_cast<volatile char *>(ptr), static_cast<char>(value)));
            default:
                return static_cast<T>(_InterlockedOr8(reinterpret_cast<volatile char *>(ptr), static_cast<char>(value)));
            }
#else
            return static_cast<T>(_InterlockedOr8(reinterpret_cast<volatile char *>(ptr), static_cast<char>(value)));
#endif
        }
        else if constexpr (sizeof(T) == 2)
        {
#if defined(MLW_ARM64) || defined(MLW_ARM32)
            switch (order)
            {
            case MemoryOrder::Relaxed:
                return static_cast<T>(_InterlockedOr16_nf(reinterpret_cast<volatile short *>(ptr), static_cast<short>(value)));
            case MemoryOrder::Acquire:
                return static_cast<T>(_InterlockedOr16_acq(reinterpret_cast<volatile short *>(ptr), static_cast<short>(value)));
            case MemoryOrder::Release:
                return static_cast<T>(_InterlockedOr16_rel(reinterpret_cast<volatile short *>(ptr), static_cast<short>(value)));
            default:
                return static_cast<T>(_InterlockedOr16(reinterpret_cast<volatile short *>(ptr), static_cast<short>(value)));
            }
#else
            return static_cast<T>(_InterlockedOr16(reinterpret_cast<volatile short *>(ptr), static_cast<short>(value)));
#endif
        }
        else if constexpr (sizeof(T) == 4)
        {
#if defined(MLW_ARM64) || defined(MLW_ARM32)
            switch (order)
            {
            case MemoryOrder::Relaxed:
                return static_cast<T>(_InterlockedOr_nf(reinterpret_cast<volatile long *>(ptr), static_cast<long>(value)));
            case MemoryOrder::Acquire:
                return static_cast<T>(_InterlockedOr_acq(reinterpret_cast<volatile long *>(ptr), static_cast<long>(value)));
            case MemoryOrder::Release:
                return static_cast<T>(_InterlockedOr_rel(reinterpret_cast<volatile long *>(ptr), static_cast<long>(value)));
            default:
                return static_cast<T>(_InterlockedOr(reinterpret_cast<volatile long *>(ptr), static_cast<long>(value)));
            }
#else
            return static_cast<T>(_InterlockedOr(reinterpret_cast<volatile long *>(ptr), static_cast<long>(value)));
#endif
        }
        else
        {
#if defined(MLW_ARM64) || defined(MLW_ARM32)
            switch (order)
            {
            case MemoryOrder::Relaxed:
                return static_cast<T>(_InterlockedOr64_nf(reinterpret_cast<volatile __int64 *>(ptr), static_cast<__int64>(value)));
            case MemoryOrder::Acquire:
                return static_cast<T>(_InterlockedOr64_acq(reinterpret_cast<volatile __int64 *>(ptr), static_cast<__int64>(value)));
            case MemoryOrder::Release:
                return static_cast<T>(_InterlockedOr64_rel(reinterpret_cast<volatile __int64 *>(ptr), static_cast<__int64>(value)));
            default:
                return static_cast<T>(_InterlockedOr64(reinterpret_cast<volatile __int64 *>(ptr), static_cast<__int64>(value)));
            }
#else
            return static_cast<T>(_InterlockedOr64(reinterpret_cast<volatile __int64 *>(ptr), static_cast<__int64>(value)));
#endif
        }
#else
        return static_cast<T>(__atomic_fetch_or(ptr, value, static_cast<int>(order)));
#endif
    }

    template <typename T>
        requires core::is_integer_v<T> && (sizeof(T) == 8 || sizeof(T) == 4 || sizeof(T) == 2 || sizeof(T) == 1)
    MLW_FORCE_INLINE T mlwOrFetch(T *ptr, T value, MemoryOrder order = MemoryOrder::SeqCst)
    {
        return mlwFetchOr(ptr, value, order) | value;
    }

    template <typename T>
        requires core::is_integer_v<T> && (sizeof(T) == 8 || sizeof(T) == 4 || sizeof(T) == 2 || sizeof(T) == 1)
    MLW_FORCE_INLINE T mlwFetchXor(T *ptr, T value, MemoryOrder order = MemoryOrder::SeqCst)
    {
#if defined(MLW_MSVC)
        if constexpr (sizeof(T) == 1)
        {
#if defined(MLW_ARM64) || defined(MLW_ARM32)
            switch (order)
            {
            case MemoryOrder::Relaxed:
                return static_cast<T>(_InterlockedXor8_nf(reinterpret_cast<volatile char *>(ptr), static_cast<char>(value)));
            case MemoryOrder::Acquire:
                return static_cast<T>(_InterlockedXor8_acq(reinterpret_cast<volatile char *>(ptr), static_cast<char>(value)));
            case MemoryOrder::Release:
                return static_cast<T>(_InterlockedXor8_rel(reinterpret_cast<volatile char *>(ptr), static_cast<char>(value)));
            default:
                return static_cast<T>(_InterlockedXor8(reinterpret_cast<volatile char *>(ptr), static_cast<char>(value)));
            }
#else
            return static_cast<T>(_InterlockedXor8(reinterpret_cast<volatile char *>(ptr), static_cast<char>(value)));
#endif
        }
        else if constexpr (sizeof(T) == 2)
        {
#if defined(MLW_ARM64) || defined(MLW_ARM32)
            switch (order)
            {
            case MemoryOrder::Relaxed:
                return static_cast<T>(_InterlockedXor16_nf(reinterpret_cast<volatile short *>(ptr), static_cast<short>(value)));
            case MemoryOrder::Acquire:
                return static_cast<T>(_InterlockedXor16_acq(reinterpret_cast<volatile short *>(ptr), static_cast<short>(value)));
            case MemoryOrder::Release:
                return static_cast<T>(_InterlockedXor16_rel(reinterpret_cast<volatile short *>(ptr), static_cast<short>(value)));
            default:
                return static_cast<T>(_InterlockedXor16(reinterpret_cast<volatile short *>(ptr), static_cast<short>(value)));
            }
#else
            return static_cast<T>(_InterlockedXor16(reinterpret_cast<volatile short *>(ptr), static_cast<short>(value)));
#endif
        }
        else if constexpr (sizeof(T) == 4)
        {
#if defined(MLW_ARM64) || defined(MLW_ARM32)
            switch (order)
            {
            case MemoryOrder::Relaxed:
                return static_cast<T>(_InterlockedXor_nf(reinterpret_cast<volatile long *>(ptr), static_cast<long>(value)));
            case MemoryOrder::Acquire:
                return static_cast<T>(_InterlockedXor_acq(reinterpret_cast<volatile long *>(ptr), static_cast<long>(value)));
            case MemoryOrder::Release:
                return static_cast<T>(_InterlockedXor_rel(reinterpret_cast<volatile long *>(ptr), static_cast<long>(value)));
            default:
                return static_cast<T>(_InterlockedXor(reinterpret_cast<volatile long *>(ptr), static_cast<long>(value)));
            }
#else
            return static_cast<T>(_InterlockedXor(reinterpret_cast<volatile long *>(ptr), static_cast<long>(value)));
#endif
        }
        else
        {
#if defined(MLW_ARM64) || defined(MLW_ARM32)
            switch (order)
            {
            case MemoryOrder::Relaxed:
                return static_cast<T>(_InterlockedXor64_nf(reinterpret_cast<volatile __int64 *>(ptr), static_cast<__int64>(value)));
            case MemoryOrder::Acquire:
                return static_cast<T>(_InterlockedXor64_acq(reinterpret_cast<volatile __int64 *>(ptr), static_cast<__int64>(value)));
            case MemoryOrder::Release:
                return static_cast<T>(_InterlockedXor64_rel(reinterpret_cast<volatile __int64 *>(ptr), static_cast<__int64>(value)));
            default:
                return static_cast<T>(_InterlockedXor64(reinterpret_cast<volatile __int64 *>(ptr), static_cast<__int64>(value)));
            }
#else
            return static_cast<T>(_InterlockedXor64(reinterpret_cast<volatile __int64 *>(ptr), static_cast<__int64>(value)));
#endif
        }
#else
        return static_cast<T>(__atomic_fetch_xor(ptr, value, static_cast<int>(order)));
#endif
    }

    template <typename T>
        requires core::is_integer_v<T> && (sizeof(T) == 8 || sizeof(T) == 4 || sizeof(T) == 2 || sizeof(T) == 1)
    MLW_FORCE_INLINE T mlwXorFetch(T *ptr, T value, MemoryOrder order = MemoryOrder::SeqCst)
    {
        return mlwFetchXor(ptr, value, order) ^ value;
    }

    template <typename T>
        requires(sizeof(T) == 1 || sizeof(T) == 2 || sizeof(T) == 4 || sizeof(T) == 8)
    MLW_FORCE_INLINE bool mlwCasStrong(T *ptr, T &expected, T desired, MemoryOrder success, MemoryOrder fail)
    {
#if defined(MLW_MSVC)
        if constexpr (sizeof(T) == 1)
        {
            char old;
#if defined(MLW_ARM64) || defined(MLW_ARM32)
            switch (success)
            {
            case MemoryOrder::Relaxed:
                old = _InterlockedCompareExchange8_nf(reinterpret_cast<volatile char *>(ptr), static_cast<char>(desired), static_cast<char>(expected));
                break;
            case MemoryOrder::Acquire:
                old = _InterlockedCompareExchange8_acq(reinterpret_cast<volatile char *>(ptr), static_cast<char>(desired), static_cast<char>(expected));
                break;
            case MemoryOrder::Release:
                old = _InterlockedCompareExchange8_rel(reinterpret_cast<volatile char *>(ptr), static_cast<char>(desired), static_cast<char>(expected));
                break;
            default:
                old = _InterlockedCompareExchange8(reinterpret_cast<volatile char *>(ptr), static_cast<char>(desired), static_cast<char>(expected));
                break;
            }
#else
            old = _InterlockedCompareExchange8(reinterpret_cast<volatile char *>(ptr), static_cast<char>(desired), static_cast<char>(expected));
#endif
            if (old == static_cast<char>(expected))
                return true;
                if (fail == MemoryOrder::Acquire || fail == MemoryOrder::SeqCst)
    MLW_FENCE_LOAD();
            expected = static_cast<T>(old);
            return false;
        }
        else if constexpr (sizeof(T) == 2)
        {
            short old;
#if defined(MLW_ARM64) || defined(MLW_ARM32)
            switch (success)
            {
            case MemoryOrder::Relaxed:
                old = _InterlockedCompareExchange16_nf(reinterpret_cast<volatile short *>(ptr), static_cast<short>(desired), static_cast<short>(expected));
                break;
            case MemoryOrder::Acquire:
                old = _InterlockedCompareExchange16_acq(reinterpret_cast<volatile short *>(ptr), static_cast<short>(desired), static_cast<short>(expected));
                break;
            case MemoryOrder::Release:
                old = _InterlockedCompareExchange16_rel(reinterpret_cast<volatile short *>(ptr), static_cast<short>(desired), static_cast<short>(expected));
                break;
            default:
                old = _InterlockedCompareExchange16(reinterpret_cast<volatile short *>(ptr), static_cast<short>(desired), static_cast<short>(expected));
                break;
            }
#else
            old = _InterlockedCompareExchange16(reinterpret_cast<volatile short *>(ptr), static_cast<short>(desired), static_cast<short>(expected));
#endif
            if (old == static_cast<short>(expected))
                return true;
                if (fail == MemoryOrder::Acquire || fail == MemoryOrder::SeqCst)
    MLW_FENCE_LOAD();
            expected = static_cast<T>(old);
            return false;
        }
        else if constexpr (sizeof(T) == 4)
        {
            long old;
#if defined(MLW_ARM64) || defined(MLW_ARM32)
            switch (success)
            {
            case MemoryOrder::Relaxed:
                old = _InterlockedCompareExchange_nf(reinterpret_cast<volatile long *>(ptr), static_cast<long>(desired), static_cast<long>(expected));
                break;
            case MemoryOrder::Acquire:
                old = _InterlockedCompareExchange_acq(reinterpret_cast<volatile long *>(ptr), static_cast<long>(desired), static_cast<long>(expected));
                break;
            case MemoryOrder::Release:
                old = _InterlockedCompareExchange_rel(reinterpret_cast<volatile long *>(ptr), static_cast<long>(desired), static_cast<long>(expected));
                break;
            default:
                old = _InterlockedCompareExchange(reinterpret_cast<volatile long *>(ptr), static_cast<long>(desired), static_cast<long>(expected));
                break;
            }
#else
            old = _InterlockedCompareExchange(reinterpret_cast<volatile long *>(ptr), static_cast<long>(desired), static_cast<long>(expected));
#endif
            if (old == static_cast<long>(expected))
                return true;
                if (fail == MemoryOrder::Acquire || fail == MemoryOrder::SeqCst)
    MLW_FENCE_LOAD();
            expected = static_cast<T>(old);
            return false;
        }
        else
        {
            __int64 old;
#if defined(MLW_ARM64) || defined(MLW_ARM32)
            switch (success)
            {
            case MemoryOrder::Relaxed:
                old = _InterlockedCompareExchange64_nf(reinterpret_cast<volatile __int64 *>(ptr), static_cast<__int64>(desired), static_cast<__int64>(expected));
                break;
            case MemoryOrder::Acquire:
                old = _InterlockedCompareExchange64_acq(reinterpret_cast<volatile __int64 *>(ptr), static_cast<__int64>(desired), static_cast<__int64>(expected));
                break;
            case MemoryOrder::Release:
                old = _InterlockedCompareExchange64_rel(reinterpret_cast<volatile __int64 *>(ptr), static_cast<__int64>(desired), static_cast<__int64>(expected));
                break;
            default:
                old = _InterlockedCompareExchange64(reinterpret_cast<volatile __int64 *>(ptr), static_cast<__int64>(desired), static_cast<__int64>(expected));
                break;
            }
#else
            old = _InterlockedCompareExchange64(reinterpret_cast<volatile __int64 *>(ptr), static_cast<__int64>(desired), static_cast<__int64>(expected));
#endif
            if (old == static_cast<__int64>(expected))
                return true;
                if (fail == MemoryOrder::Acquire || fail == MemoryOrder::SeqCst)
    MLW_FENCE_LOAD();
            expected = static_cast<T>(old);
            return false;
        }
#else
        // GCC/Clang — weak=false for strong, updates expected on failure automatically
        return __atomic_compare_exchange_n(ptr, &expected, desired,
                                           /*weak=*/false,
                                           static_cast<int>(success),
                                           static_cast<int>(fail));
#endif
    }

    // weak — on MSVC same as strong since interlocked functions retry internally
    // on GCC/Clang ARM this maps to native LDAXR/STLXR without retry wrapper
    template <typename T>
        requires(sizeof(T) == 1 || sizeof(T) == 2 || sizeof(T) == 4 || sizeof(T) == 8)
    MLW_FORCE_INLINE bool mlwCasWeak(T *ptr, T &expected, T desired, MemoryOrder success, MemoryOrder fail)
    {
#if defined(MLW_MSVC)
        // no genuine weak CAS on MSVC — fall through to strong
        return mlwCasStrong(ptr, expected, desired, success, fail);
#else
        return __atomic_compare_exchange_n(ptr, &expected, desired,
                                           /*weak=*/true,
                                           static_cast<int>(success),
                                           static_cast<int>(fail));
#endif
    }
} // namespace core::sync
