#pragma once

#include "atomic_op.h"
#include "../macro.h"
#include "../bit.h"

/// \file
/// \brief Public atomic wrapper type for common atomic operations.
///
/// `core::sync::Atomic<T>` is the normal public interface for atomic values.
/// It uses the low-level primitive helpers in \ref atomic_op.h internally and
/// provides a safer, easier-to-use wrapper for integer, pointer, bool, and
/// floating-point atomics.

namespace core::sync
{
    namespace detail
    {
        /// \brief Integer storage type used to carry a floating-point value
        ///        through the integer atomic machinery.
        ///
        /// Floating-point atomics are performed on a same-width unsigned integer
        /// view of the storage (bit-cast in and out), because neither backend can
        /// operate on a float directly: the GCC/Clang `__atomic_*_n` builtins
        /// reject a floating-point pointer, and the MSVC `_Interlocked*` path
        /// converts by value (a truncating cast) rather than by bits. Reducing
        /// floats to `AtomicRaw<T>` makes every op go through the proven integer
        /// path on all backends. Non-float types map to themselves.
        template <typename T> struct AtomicRaw { using type = T; };
        template <> struct AtomicRaw<f32> { using type = uint32; };
        template <> struct AtomicRaw<f64> { using type = uint64; };
        template <typename T> using AtomicRaw_t = typename AtomicRaw<T>::type;

#if defined(MLW_MSVC) && defined(MLW_ARM64)

// Single-instruction acquire/release (ARMv8 LDAR/STLR) and barrier-free atomic
// relaxed access (__iso_volatile_*). Declared here in the file's existing
// hand-declared-intrinsic style; #include <intrin.h> is the vendor-supported
// alternative if you prefer. Signatures follow the MSVC ARM64 intrinsics docs
// (VERIFY on your toolchain — this block cannot be compiled outside MSVC/ARM64).
extern "C" unsigned __int8  __ldar8(unsigned __int8 volatile *);
extern "C" unsigned __int16 __ldar16(unsigned __int16 volatile *);
extern "C" unsigned __int32 __ldar32(unsigned __int32 volatile *);
extern "C" unsigned __int64 __ldar64(unsigned __int64 volatile *);
extern "C" void __stlr8(unsigned __int8 volatile *, unsigned __int8);
extern "C" void __stlr16(unsigned __int16 volatile *, unsigned __int16);
extern "C" void __stlr32(unsigned __int32 volatile *, unsigned __int32);
extern "C" void __stlr64(unsigned __int64 volatile *, unsigned __int64);
extern "C" __int8  __iso_volatile_load8(const volatile __int8 *);
extern "C" __int16 __iso_volatile_load16(const volatile __int16 *);
extern "C" __int32 __iso_volatile_load32(const volatile __int32 *);
extern "C" __int64 __iso_volatile_load64(const volatile __int64 *);
extern "C" void __iso_volatile_store8(volatile __int8 *, __int8);
extern "C" void __iso_volatile_store16(volatile __int16 *, __int16);
extern "C" void __iso_volatile_store32(volatile __int32 *, __int32);
extern "C" void __iso_volatile_store64(volatile __int64 *, __int64);


        // ARMv8 gives dedicated acquire/release instructions and a barrier-free
        // atomic access, so no DMB or RMW is needed. Each helper dispatches on
        // the width of T and bit-casts (never value-casts) to the matching
        // unsigned integer the intrinsic expects. NOTE: not compilable outside
        // MSVC/ARM64 — verify intrinsic signatures on the target toolchain.
        template <typename T> MLW_FORCE_INLINE T armLoadRelaxed(const T *p) noexcept
        {
            if constexpr (sizeof(T) == 1) return mlwBitCast<T>(__iso_volatile_load8(reinterpret_cast<const volatile __int8 *>(p)));
            else if constexpr (sizeof(T) == 2) return mlwBitCast<T>(__iso_volatile_load16(reinterpret_cast<const volatile __int16 *>(p)));
            else if constexpr (sizeof(T) == 4) return mlwBitCast<T>(__iso_volatile_load32(reinterpret_cast<const volatile __int32 *>(p)));
            else return mlwBitCast<T>(__iso_volatile_load64(reinterpret_cast<const volatile __int64 *>(p)));
        }
        template <typename T> MLW_FORCE_INLINE T armLoadAcquire(const T *p) noexcept
        {
            auto *q = const_cast<T *>(p); // LDAR reads; the volatile* is non-const
            if constexpr (sizeof(T) == 1) return mlwBitCast<T>(__ldar8(reinterpret_cast<unsigned __int8 volatile *>(q)));
            else if constexpr (sizeof(T) == 2) return mlwBitCast<T>(__ldar16(reinterpret_cast<unsigned __int16 volatile *>(q)));
            else if constexpr (sizeof(T) == 4) return mlwBitCast<T>(__ldar32(reinterpret_cast<unsigned __int32 volatile *>(q)));
            else return mlwBitCast<T>(__ldar64(reinterpret_cast<unsigned __int64 volatile *>(q)));
        }
        template <typename T> MLW_FORCE_INLINE void armStoreRelaxed(T *p, T v) noexcept
        {
            if constexpr (sizeof(T) == 1) __iso_volatile_store8(reinterpret_cast<volatile __int8 *>(p), mlwBitCast<__int8>(v));
            else if constexpr (sizeof(T) == 2) __iso_volatile_store16(reinterpret_cast<volatile __int16 *>(p), mlwBitCast<__int16>(v));
            else if constexpr (sizeof(T) == 4) __iso_volatile_store32(reinterpret_cast<volatile __int32 *>(p), mlwBitCast<__int32>(v));
            else __iso_volatile_store64(reinterpret_cast<volatile __int64 *>(p), mlwBitCast<__int64>(v));
        }
        template <typename T> MLW_FORCE_INLINE void armStoreRelease(T *p, T v) noexcept
        {
            if constexpr (sizeof(T) == 1) __stlr8(reinterpret_cast<unsigned __int8 volatile *>(p), mlwBitCast<unsigned __int8>(v));
            else if constexpr (sizeof(T) == 2) __stlr16(reinterpret_cast<unsigned __int16 volatile *>(p), mlwBitCast<unsigned __int16>(v));
            else if constexpr (sizeof(T) == 4) __stlr32(reinterpret_cast<unsigned __int32 volatile *>(p), mlwBitCast<unsigned __int32>(v));
            else __stlr64(reinterpret_cast<unsigned __int64 volatile *>(p), mlwBitCast<unsigned __int64>(v));
        }
#endif
    }

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

        /// \internal Same-width unsigned integer view for floating-point T; T
        /// itself otherwise. Float ops bit-cast to this and reuse Atomic<Raw>.
        using Raw = detail::AtomicRaw_t<T>;

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
            if constexpr (core::is_float_v<T>)
            {
                // Bit-cast through the integer atomic; never touches a float ptr.
                reinterpret_cast<Atomic<Raw> *>(this)->store(mlwBitCast<Raw>(val), order);
                return val;
            }
            else
            {

            if (order == MemoryOrder::Acquire)
                order = MemoryOrder::Relaxed;

            if (order == MemoryOrder::AcqRel)
                order = MemoryOrder::Release;
#if defined(MLW_MSVC)
#if defined(MLW_ARM64)
            // ARMv8: one instruction per op. STLR is release (and, paired with
            // LDAR loads, sequentially consistent); relaxed is a barrier-free
            // atomic store. No DMB, no RMW.
            if (order == MemoryOrder::Relaxed)
                detail::armStoreRelaxed(&value, val);
            else // Release or SeqCst
                detail::armStoreRelease(&value, val);
#elif defined(MLW_ARM32)
            // ARMv7 has no STLR: fall back to a full barrier + plain store.
            // (A 64-bit store here is not single-copy atomic on ARM32.)
            switch (order)
            {
            case MemoryOrder::Relaxed:
                *reinterpret_cast<volatile T *>(&value) = val;
                break;
            case MemoryOrder::Release:
                MLW_FENCE_FULL();
                *reinterpret_cast<volatile T *>(&value) = val;
                break;
            default: // SeqCst
                MLW_FENCE_FULL();
                *reinterpret_cast<volatile T *>(&value) = val;
                MLW_FENCE_FULL();
                break;
            }
#else
            switch (order)
            {
            case MemoryOrder::Relaxed:
                *reinterpret_cast<volatile T *>(&value) = val;
                break;
            case MemoryOrder::Release:
                MLW_COMPILER_BARRIER();
                *reinterpret_cast<volatile T *>(&value) = val;
                break;
            default: // SeqCst — x86 needs the store to carry a full fence (XCHG)
                mlwExchange(&value, val, MemoryOrder::SeqCst);
                break;
            }
#endif
#else
            __atomic_store_n(&value, val, static_cast<int>(order));
#endif
            return val;
            }
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
            if constexpr (core::is_float_v<T>)
            {
                return mlwBitCast<T>(reinterpret_cast<const Atomic<Raw> *>(this)->load(order));
            }
            else
            {

            if (order == MemoryOrder::Release)
                order = MemoryOrder::Relaxed;

            if (order == MemoryOrder::AcqRel)
                order = MemoryOrder::Acquire;

#if defined(MLW_MSVC)
#if defined(MLW_ARM64)
            // ARMv8: LDAR is a one-instruction acquire load (and, paired with
            // STLR stores, sequentially consistent); relaxed is a barrier-free
            // atomic load. No DMB.
            if (order == MemoryOrder::Relaxed)
                return detail::armLoadRelaxed(&value);
            return detail::armLoadAcquire(&value); // Acquire and SeqCst
#elif defined(MLW_ARM32)
            // ARMv7 has no LDAR: plain atomic load + the barrier the order needs.
            // (A 64-bit load here is not single-copy atomic on ARM32.)
            T result = *reinterpret_cast<const volatile T *>(&value);
            if (order == MemoryOrder::Acquire)
                MLW_FENCE_LOAD();
            else if (order == MemoryOrder::SeqCst)
                MLW_FENCE_FULL();
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
        }

        /// \brief Implicit conversion, equivalent to `load()` (Acquire).
        /// \note Every use of an `Atomic` in a value context performs an acquire
        ///       load, so `T x = myAtomic;` is not free — it is a synchronized
        ///       read. Call \ref load with Relaxed if you want a cheaper one.
        MLW_FORCE_INLINE operator T() const noexcept { return load(); };

        /// \brief Atomically replace the stored value and return the old value.
        MLW_FORCE_INLINE T exchange(T val, MemoryOrder m = MemoryOrder::AcqRel) noexcept
        {
            if constexpr (core::is_float_v<T>)
                return mlwBitCast<T>(reinterpret_cast<Atomic<Raw> *>(this)->exchange(mlwBitCast<Raw>(val), m));
            else
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
            if constexpr (core::is_float_v<T>)
            {
                // Float CAS is a BITWISE compare (like std::atomic<float>): -0.0
                // and +0.0 differ, and a NaN never matches itself.
                Raw rexp = mlwBitCast<Raw>(expected);
                bool ok = reinterpret_cast<Atomic<Raw> *>(this)->compareExchangeStrong(
                    rexp, mlwBitCast<Raw>(desired), success, fail);
                if (!ok)
                    expected = mlwBitCast<T>(rexp); // publish the observed value
                return ok;
            }
            else
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
            if constexpr (core::is_float_v<T>)
            {
                Raw rexp = mlwBitCast<Raw>(expected);
                bool ok = reinterpret_cast<Atomic<Raw> *>(this)->compareExchangeWeak(
                    rexp, mlwBitCast<Raw>(desired), success, fail);
                if (!ok)
                    expected = mlwBitCast<T>(rexp);
                return ok;
            }
            else
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