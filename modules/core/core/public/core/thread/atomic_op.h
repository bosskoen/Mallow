#pragma once
#include "../compilers.h"
#include "../traits.h"

#if defined(MLW_MSVC)

extern "C"
{
    char _InterlockedExchangeAdd8(volatile char *, char);
    short _InterlockedExchangeAdd16(volatile short *, short);
    long _InterlockedExchangeAdd(volatile long *, long);
    __int64 _InterlockedExchangeAdd64(volatile __int64 *, __int64);

    char _InterlockedExchange8(volatile char *, char);
    short _InterlockedExchange16(volatile short *, short);
    long _InterlockedExchange(volatile long *, long);
    __int64 _InterlockedExchange64(volatile __int64 *, __int64);

    char _InterlockedAnd8(volatile char *, char);
    short _InterlockedAnd16(volatile short *, short);
    long _InterlockedAnd(volatile long *, long);
    __int64 _InterlockedAnd64(volatile __int64 *, __int64);

    char _InterlockedOr8(volatile char *, char);
    short _InterlockedOr16(volatile short *, short);
    long _InterlockedOr(volatile long *, long);
    __int64 _InterlockedOr64(volatile __int64 *, __int64);

    char _InterlockedXor8(volatile char *, char);
    short _InterlockedXor16(volatile short *, short);
    long _InterlockedXor(volatile long *, long);
    __int64 _InterlockedXor64(volatile __int64 *, __int64);

    char _InterlockedCompareExchange8(volatile char *, char, char);
    short _InterlockedCompareExchange16(volatile short *, short, short);
    long _InterlockedCompareExchange(volatile long *, long, long);
    __int64 _InterlockedCompareExchange64(volatile __int64 *, __int64, __int64);
}

#pragma intrinsic(_InterlockedExchangeAdd8)
#pragma intrinsic(_InterlockedExchangeAdd16)
#pragma intrinsic(_InterlockedExchangeAdd)
#pragma intrinsic(_InterlockedExchangeAdd64)

#pragma intrinsic(_InterlockedExchange8)
#pragma intrinsic(_InterlockedExchange16)
#pragma intrinsic(_InterlockedExchange)
#pragma intrinsic(_InterlockedExchange64)

#pragma intrinsic(_InterlockedAnd8)
#pragma intrinsic(_InterlockedAnd16)
#pragma intrinsic(_InterlockedAnd)
#pragma intrinsic(_InterlockedAnd64)

#pragma intrinsic(_InterlockedOr8)
#pragma intrinsic(_InterlockedOr16)
#pragma intrinsic(_InterlockedOr)
#pragma intrinsic(_InterlockedOr64)

#pragma intrinsic(_InterlockedXor8)
#pragma intrinsic(_InterlockedXor16)
#pragma intrinsic(_InterlockedXor)
#pragma intrinsic(_InterlockedXor64)

#pragma intrinsic(_InterlockedCompareExchange8)
#pragma intrinsic(_InterlockedCompareExchange16)
#pragma intrinsic(_InterlockedCompareExchange)
#pragma intrinsic(_InterlockedCompareExchange64)

// ARM variants are never intrinsics on x86/x64 — they are library calls
// so no #pragma intrinsic needed for the _nf/_acq/_rel suffixed ones
#if defined(MLW_ARM64) || defined(MLW_ARM32)
extern "C"
{
    char _InterlockedExchangeAdd8_nf(volatile char *, char);
    char _InterlockedExchangeAdd8_acq(volatile char *, char);
    char _InterlockedExchangeAdd8_rel(volatile char *, char);
    short _InterlockedExchangeAdd16_nf(volatile short *, short);
    short _InterlockedExchangeAdd16_acq(volatile short *, short);
    short _InterlockedExchangeAdd16_rel(volatile short *, short);
    long _InterlockedExchangeAdd_nf(volatile long *, long);
    long _InterlockedExchangeAdd_acq(volatile long *, long);
    long _InterlockedExchangeAdd_rel(volatile long *, long);
    __int64 _InterlockedExchangeAdd64_nf(volatile __int64 *, __int64);
    __int64 _InterlockedExchangeAdd64_acq(volatile __int64 *, __int64);
    __int64 _InterlockedExchangeAdd64_rel(volatile __int64 *, __int64);

    char _InterlockedExchange8_nf(volatile char *, char);
    char _InterlockedExchange8_acq(volatile char *, char);
    char _InterlockedExchange8_rel(volatile char *, char);
    short _InterlockedExchange16_nf(volatile short *, short);
    short _InterlockedExchange16_acq(volatile short *, short);
    short _InterlockedExchange16_rel(volatile short *, short);
    long _InterlockedExchange_nf(volatile long *, long);
    long _InterlockedExchange_acq(volatile long *, long);
    long _InterlockedExchange_rel(volatile long *, long);
    __int64 _InterlockedExchange64_nf(volatile __int64 *, __int64);
    __int64 _InterlockedExchange64_acq(volatile __int64 *, __int64);
    __int64 _InterlockedExchange64_rel(volatile __int64 *, __int64);

    char _InterlockedAnd8_nf(volatile char *, char);
    char _InterlockedAnd8_acq(volatile char *, char);
    char _InterlockedAnd8_rel(volatile char *, char);
    short _InterlockedAnd16_nf(volatile short *, short);
    short _InterlockedAnd16_acq(volatile short *, short);
    short _InterlockedAnd16_rel(volatile short *, short);
    long _InterlockedAnd_nf(volatile long *, long);
    long _InterlockedAnd_acq(volatile long *, long);
    long _InterlockedAnd_rel(volatile long *, long);
    __int64 _InterlockedAnd64_nf(volatile __int64 *, __int64);
    __int64 _InterlockedAnd64_acq(volatile __int64 *, __int64);
    __int64 _InterlockedAnd64_rel(volatile __int64 *, __int64);

    char _InterlockedOr8_nf(volatile char *, char);
    char _InterlockedOr8_acq(volatile char *, char);
    char _InterlockedOr8_rel(volatile char *, char);
    short _InterlockedOr16_nf(volatile short *, short);
    short _InterlockedOr16_acq(volatile short *, short);
    short _InterlockedOr16_rel(volatile short *, short);
    long _InterlockedOr_nf(volatile long *, long);
    long _InterlockedOr_acq(volatile long *, long);
    long _InterlockedOr_rel(volatile long *, long);
    __int64 _InterlockedOr64_nf(volatile __int64 *, __int64);
    __int64 _InterlockedOr64_acq(volatile __int64 *, __int64);
    __int64 _InterlockedOr64_rel(volatile __int64 *, __int64);

    char _InterlockedXor8_nf(volatile char *, char);
    char _InterlockedXor8_acq(volatile char *, char);
    char _InterlockedXor8_rel(volatile char *, char);
    short _InterlockedXor16_nf(volatile short *, short);
    short _InterlockedXor16_acq(volatile short *, short);
    short _InterlockedXor16_rel(volatile short *, short);
    long _InterlockedXor_nf(volatile long *, long);
    long _InterlockedXor_acq(volatile long *, long);
    long _InterlockedXor_rel(volatile long *, long);
    __int64 _InterlockedXor64_nf(volatile __int64 *, __int64);
    __int64 _InterlockedXor64_acq(volatile __int64 *, __int64);
    __int64 _InterlockedXor64_rel(volatile __int64 *, __int64);

    char _InterlockedCompareExchange8_nf(volatile char *, char, char);
    char _InterlockedCompareExchange8_acq(volatile char *, char, char);
    char _InterlockedCompareExchange8_rel(volatile char *, char, char);
    short _InterlockedCompareExchange16_nf(volatile short *, short, short);
    short _InterlockedCompareExchange16_acq(volatile short *, short, short);
    short _InterlockedCompareExchange16_rel(volatile short *, short, short);
    long _InterlockedCompareExchange_nf(volatile long *, long, long);
    long _InterlockedCompareExchange_acq(volatile long *, long, long);
    long _InterlockedCompareExchange_rel(volatile long *, long, long);
    __int64 _InterlockedCompareExchange64_nf(volatile __int64 *, __int64, __int64);
    __int64 _InterlockedCompareExchange64_acq(volatile __int64 *, __int64, __int64);
    __int64 _InterlockedCompareExchange64_rel(volatile __int64 *, __int64, __int64);
}
#pragma intrinsic(_InterlockedExchangeAdd8)
#pragma intrinsic(_InterlockedExchangeAdd16)
#pragma intrinsic(_InterlockedExchangeAdd)
#pragma intrinsic(_InterlockedExchangeAdd64)

#pragma intrinsic(_InterlockedExchange8)
#pragma intrinsic(_InterlockedExchange16)
#pragma intrinsic(_InterlockedExchange)
#pragma intrinsic(_InterlockedExchange64)

#pragma intrinsic(_InterlockedAnd8)
#pragma intrinsic(_InterlockedAnd16)
#pragma intrinsic(_InterlockedAnd)
#pragma intrinsic(_InterlockedAnd64)

#pragma intrinsic(_InterlockedOr8)
#pragma intrinsic(_InterlockedOr16)
#pragma intrinsic(_InterlockedOr)
#pragma intrinsic(_InterlockedOr64)

#pragma intrinsic(_InterlockedXor8)
#pragma intrinsic(_InterlockedXor16)
#pragma intrinsic(_InterlockedXor)
#pragma intrinsic(_InterlockedXor64)

#pragma intrinsic(_InterlockedCompareExchange8)
#pragma intrinsic(_InterlockedCompareExchange16)
#pragma intrinsic(_InterlockedCompareExchange)
#pragma intrinsic(_InterlockedCompareExchange64)

#endif // MLW_ARM64 || MLW_ARM32

#endif // MLW_MSVC

namespace core::sync
{

    namespace detail
    {
        // convert any atomic-eligible type to its integer representation
        template <typename T>
        MLW_FORCE_INLINE auto toInt(T val)
        {
            if constexpr (core::is_pointer_v<T>)
                return reinterpret_cast<usize>(val);
            else
                return val;
        }

        // convert back from integer representation to T
        template <typename T, typename I>
        MLW_FORCE_INLINE T fromInt(I val)
        {
            if constexpr (core::is_pointer_v<T>)
                return reinterpret_cast<T>(static_cast<usize>(val));
            else
                return static_cast<T>(val);
        }
    }

    enum class MemoryOrder : int32
    {
        Relaxed = MLW_MO_RELAXED,
        Acquire = MLW_MO_ACQUIRE,
        Release = MLW_MO_RELEASE,
        AcqRel = MLW_MO_ACQ_REL,
        SeqCst = MLW_MO_SEQ_CST,
    };

    // make shure teh oreder is corect for cas operation to work corectly
    static_assert((int)MemoryOrder::Relaxed < (int)MemoryOrder::Acquire);
    static_assert((int)MemoryOrder::Acquire < (int)MemoryOrder::Release);
    static_assert((int)MemoryOrder::Release < (int)MemoryOrder::AcqRel);
    static_assert((int)MemoryOrder::AcqRel < (int)MemoryOrder::SeqCst);

    template <typename T>
        requires core::is_integer_v<T> && (sizeof(T) == 8 || sizeof(T) == 4 || sizeof(T) == 2 || sizeof(T) == 1) && (sizeof(T) <= sizeof(usize)) 
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
        requires core::is_integer_v<T> && (sizeof(T) == 8 || sizeof(T) == 4 || sizeof(T) == 2 || sizeof(T) == 1) && (sizeof(T) <= sizeof(usize))
    MLW_FORCE_INLINE T mlwAddFetch(T *ptr, T value, MemoryOrder order = MemoryOrder::SeqCst)
    {
        return mlwFetchAdd(ptr, value, order) + value;
    }

    template <typename T>
        requires core::is_integer_v<T> && (sizeof(T) == 8 || sizeof(T) == 4 || sizeof(T) == 2 || sizeof(T) == 1) && (sizeof(T) <= sizeof(usize)) 
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
        requires core::is_integer_v<T> && (sizeof(T) == 8 || sizeof(T) == 4 || sizeof(T) == 2 || sizeof(T) == 1) && (sizeof(T) <= sizeof(usize)) 
    MLW_FORCE_INLINE T mlwSubFetch(T *ptr, T value, MemoryOrder order = MemoryOrder::SeqCst)
    {
        return mlwFetchSub(ptr, value, order) - value;
    }

    template <typename T>
        requires(sizeof(T) == 8 || sizeof(T) == 4 || sizeof(T) == 2 || sizeof(T) == 1) && (sizeof(T) <= sizeof(usize))
    MLW_FORCE_INLINE T mlwExchange(T *ptr, T value, MemoryOrder order = MemoryOrder::SeqCst)
    {
#if defined(MLW_MSVC)
        if constexpr (sizeof(T) == 1)
        {
            char ival = static_cast<char>(detail::toInt(value));
#if defined(MLW_ARM64) || defined(MLW_ARM32)
            switch (order)
            {
            case MemoryOrder::Relaxed:
                return detail::fromInt<T>(_InterlockedExchange8_nf(reinterpret_cast<volatile char *>(ptr), ival));
            case MemoryOrder::Acquire:
                return detail::fromInt<T>(_InterlockedExchange8_acq(reinterpret_cast<volatile char *>(ptr), ival));
            case MemoryOrder::Release:
                return detail::fromInt<T>(_InterlockedExchange8_rel(reinterpret_cast<volatile char *>(ptr), ival));
            default:
                return detail::fromInt<T>(_InterlockedExchange8(reinterpret_cast<volatile char *>(ptr), ival));
            }
#else
            return detail::fromInt<T>(_InterlockedExchange8(reinterpret_cast<volatile char *>(ptr), ival));
#endif
        }
        else if constexpr (sizeof(T) == 2)
        {
            short ival = static_cast<short>(detail::toInt(value));
#if defined(MLW_ARM64) || defined(MLW_ARM32)
            switch (order)
            {
            case MemoryOrder::Relaxed:
                return detail::fromInt<T>(_InterlockedExchange16_nf(reinterpret_cast<volatile short *>(ptr), ival));
            case MemoryOrder::Acquire:
                return detail::fromInt<T>(_InterlockedExchange16_acq(reinterpret_cast<volatile short *>(ptr), ival));
            case MemoryOrder::Release:
                return detail::fromInt<T>(_InterlockedExchange16_rel(reinterpret_cast<volatile short *>(ptr), ival));
            default:
                return detail::fromInt<T>(_InterlockedExchange16(reinterpret_cast<volatile short *>(ptr), ival));
            }
#else
            return detail::fromInt<T>(_InterlockedExchange16(reinterpret_cast<volatile short *>(ptr), ival));
#endif
        }
        else if constexpr (sizeof(T) == 4)
        {
            long ival = static_cast<long>(detail::toInt(value));
#if defined(MLW_ARM64) || defined(MLW_ARM32)
            switch (order)
            {
            case MemoryOrder::Relaxed:
                return detail::fromInt<T>(_InterlockedExchange_nf(reinterpret_cast<volatile long *>(ptr), ival));
            case MemoryOrder::Acquire:
                return detail::fromInt<T>(_InterlockedExchange_acq(reinterpret_cast<volatile long *>(ptr), ival));
            case MemoryOrder::Release:
                return detail::fromInt<T>(_InterlockedExchange_rel(reinterpret_cast<volatile long *>(ptr), ival));
            default:
                return detail::fromInt<T>(_InterlockedExchange(reinterpret_cast<volatile long *>(ptr), ival));
            }
#else
            return detail::fromInt<T>(_InterlockedExchange(reinterpret_cast<volatile long *>(ptr), ival));
#endif
        }
        else
        {
            __int64 ival = static_cast<__int64>(detail::toInt(value));
#if defined(MLW_ARM64) || defined(MLW_ARM32)
            switch (order)
            {
            case MemoryOrder::Relaxed:
                return detail::fromInt<T>(_InterlockedExchange64_nf(reinterpret_cast<volatile __int64 *>(ptr), ival));
            case MemoryOrder::Acquire:
                return detail::fromInt<T>(_InterlockedExchange64_acq(reinterpret_cast<volatile __int64 *>(ptr), ival));
            case MemoryOrder::Release:
                return detail::fromInt<T>(_InterlockedExchange64_rel(reinterpret_cast<volatile __int64 *>(ptr), ival));
            default:
                return detail::fromInt<T>(_InterlockedExchange64(reinterpret_cast<volatile __int64 *>(ptr), ival));
            }
#else
            return detail::fromInt<T>(_InterlockedExchange64(reinterpret_cast<volatile __int64 *>(ptr), ival));
#endif
        }
#else
        return static_cast<T>(__atomic_exchange_n(ptr, value, static_cast<int>(order)));
#endif
    }

    template <typename T>
        requires core::is_integer_v<T> && (sizeof(T) == 8 || sizeof(T) == 4 || sizeof(T) == 2 || sizeof(T) == 1) && (sizeof(T) <= sizeof(usize))
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
        requires core::is_integer_v<T> && (sizeof(T) == 8 || sizeof(T) == 4 || sizeof(T) == 2 || sizeof(T) == 1) && (sizeof(T) <= sizeof(usize)) 
    MLW_FORCE_INLINE T mlwAndFetch(T *ptr, T value, MemoryOrder order = MemoryOrder::SeqCst)
    {
        return mlwFetchAnd(ptr, value, order) & value;
    }

    template <typename T>
        requires core::is_integer_v<T> && (sizeof(T) == 8 || sizeof(T) == 4 || sizeof(T) == 2 || sizeof(T) == 1) && (sizeof(T) <= sizeof(usize)) 
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
        requires core::is_integer_v<T> && (sizeof(T) == 8 || sizeof(T) == 4 || sizeof(T) == 2 || sizeof(T) == 1) && (sizeof(T) <= sizeof(usize))
    MLW_FORCE_INLINE T mlwOrFetch(T *ptr, T value, MemoryOrder order = MemoryOrder::SeqCst)
    {
        return mlwFetchOr(ptr, value, order) | value;
    }

    template <typename T>
        requires core::is_integer_v<T> && (sizeof(T) == 8 || sizeof(T) == 4 || sizeof(T) == 2 || sizeof(T) == 1) && (sizeof(T) <= sizeof(usize))
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
        requires core::is_integer_v<T> && (sizeof(T) == 8 || sizeof(T) == 4 || sizeof(T) == 2 || sizeof(T) == 1) && (sizeof(T) <= sizeof(usize)) 
    MLW_FORCE_INLINE T mlwXorFetch(T *ptr, T value, MemoryOrder order = MemoryOrder::SeqCst)
    {
        return mlwFetchXor(ptr, value, order) ^ value;
    }

    template <typename T>
        requires(sizeof(T) == 1 || sizeof(T) == 2 || sizeof(T) == 4 || sizeof(T) == 8) && (sizeof(T) <= sizeof(usize))
    MLW_FORCE_INLINE bool mlwCasStrong(T *ptr, T &expected, T desired, MemoryOrder success, MemoryOrder fail)
    {
#if defined(MLW_MSVC)
        if constexpr (sizeof(T) == 1)
        {
            char exp = static_cast<char>(detail::toInt(expected));
            char des = static_cast<char>(detail::toInt(desired));
            char old;
#if defined(MLW_ARM64) || defined(MLW_ARM32)
            switch (success)
            {
            case MemoryOrder::Relaxed:
                old = _InterlockedCompareExchange8_nf(reinterpret_cast<volatile char *>(ptr), des, exp);
                break;
            case MemoryOrder::Acquire:
                old = _InterlockedCompareExchange8_acq(reinterpret_cast<volatile char *>(ptr), des, exp);
                break;
            case MemoryOrder::Release:
                old = _InterlockedCompareExchange8_rel(reinterpret_cast<volatile char *>(ptr), des, exp);
                break;
            default:
                old = _InterlockedCompareExchange8(reinterpret_cast<volatile char *>(ptr), des, exp);
                break;
            }
#else
            old = _InterlockedCompareExchange8(reinterpret_cast<volatile char *>(ptr), des, exp);
#endif
            if (old == exp)
                return true;
            if (fail == MemoryOrder::Acquire || fail == MemoryOrder::SeqCst)
                MLW_FENCE_LOAD();
            expected = detail::fromInt<T>(old);
            return false;
        }
        else if constexpr (sizeof(T) == 2)
        {
            short exp = static_cast<short>(detail::toInt(expected));
            short des = static_cast<short>(detail::toInt(desired));
            short old;
#if defined(MLW_ARM64) || defined(MLW_ARM32)
            switch (success)
            {
            case MemoryOrder::Relaxed:
                old = _InterlockedCompareExchange16_nf(reinterpret_cast<volatile short *>(ptr), des, exp);
                break;
            case MemoryOrder::Acquire:
                old = _InterlockedCompareExchange16_acq(reinterpret_cast<volatile short *>(ptr), des, exp);
                break;
            case MemoryOrder::Release:
                old = _InterlockedCompareExchange16_rel(reinterpret_cast<volatile short *>(ptr), des, exp);
                break;
            default:
                old = _InterlockedCompareExchange16(reinterpret_cast<volatile short *>(ptr), des, exp);
                break;
            }
#else
            old = _InterlockedCompareExchange16(reinterpret_cast<volatile short *>(ptr), des, exp);
#endif
            if (old == exp)
                return true;
            if (fail == MemoryOrder::Acquire || fail == MemoryOrder::SeqCst)
                MLW_FENCE_LOAD();
            expected = detail::fromInt<T>(old);
            return false;
        }
        else if constexpr (sizeof(T) == 4)
        {
            long exp = static_cast<long>(detail::toInt(expected));
            long des = static_cast<long>(detail::toInt(desired));
            long old;
#if defined(MLW_ARM64) || defined(MLW_ARM32)
            switch (success)
            {
            case MemoryOrder::Relaxed:
                old = _InterlockedCompareExchange_nf(reinterpret_cast<volatile long *>(ptr), des, exp);
                break;
            case MemoryOrder::Acquire:
                old = _InterlockedCompareExchange_acq(reinterpret_cast<volatile long *>(ptr), des, exp);
                break;
            case MemoryOrder::Release:
                old = _InterlockedCompareExchange_rel(reinterpret_cast<volatile long *>(ptr), des, exp);
                break;
            default:
                old = _InterlockedCompareExchange(reinterpret_cast<volatile long *>(ptr), des, exp);
                break;
            }
#else
            old = _InterlockedCompareExchange(reinterpret_cast<volatile long *>(ptr), des, exp);
#endif
            if (old == exp)
                return true;
            if (fail == MemoryOrder::Acquire || fail == MemoryOrder::SeqCst)
                MLW_FENCE_LOAD();
            expected = detail::fromInt<T>(old);
            return false;
        }
        else
        {
            __int64 exp = static_cast<__int64>(detail::toInt(expected));
            __int64 des = static_cast<__int64>(detail::toInt(desired));
            __int64 old;
#if defined(MLW_ARM64) || defined(MLW_ARM32)
            switch (success)
            {
            case MemoryOrder::Relaxed:
                old = _InterlockedCompareExchange64_nf(reinterpret_cast<volatile __int64 *>(ptr), des, exp);
                break;
            case MemoryOrder::Acquire:
                old = _InterlockedCompareExchange64_acq(reinterpret_cast<volatile __int64 *>(ptr), des, exp);
                break;
            case MemoryOrder::Release:
                old = _InterlockedCompareExchange64_rel(reinterpret_cast<volatile __int64 *>(ptr), des, exp);
                break;
            default:
                old = _InterlockedCompareExchange64(reinterpret_cast<volatile __int64 *>(ptr), des, exp);
                break;
            }
#else
            old = _InterlockedCompareExchange64(reinterpret_cast<volatile __int64 *>(ptr), des, exp);
#endif
            if (old == exp)
                return true;
            if (fail == MemoryOrder::Acquire || fail == MemoryOrder::SeqCst)
                MLW_FENCE_LOAD();
            expected = detail::fromInt<T>(old);
            return false;
        }
#else
        return __atomic_compare_exchange_n(ptr, &expected, desired,
                                           /*weak=*/false,
                                           static_cast<int>(success),
                                           static_cast<int>(fail));
#endif
    }

    // weak — on MSVC same as strong since interlocked functions retry internally
    // on GCC/Clang ARM this maps to native LDAXR/STLXR without retry wrapper
    template <typename T>
        requires(sizeof(T) == 1 || sizeof(T) == 2 || sizeof(T) == 4 || sizeof(T) == 8) && (sizeof(T) <= sizeof(usize))
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
