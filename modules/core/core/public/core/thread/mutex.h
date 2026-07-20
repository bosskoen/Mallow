#pragma once

/// \file
/// \brief Defines core::sync::Mutex, an adaptive spin-then-futex mutex.

#include "atomic.h"

namespace core::sync
{
    namespace detail
    {
        // platform futex implemetation
        void mlwFutexWait(uint32 *ptr, uint32 expected) noexcept;
        void mlwFutexWakeOne(uint32 *ptr) noexcept;
        void mlwFutexWakeAll(uint32 *ptr) noexcept;
    }

    /// \brief Mutual-exclusion lock for a single owner at a time.
    ///
    /// Adaptive: `lock()` first tries an uncontended `0 -> 1` compare-exchange,
    /// then busy-spins briefly (`BUSY_CYCLES` iterations) before sleeping on a
    /// futex. Non-recursive — locking twice from the same thread deadlocks.
    class Mutex
    {
        // 0 = unlocked
        // 1 = locked, no waiters
        // 2 = locked, waiters sleeping
        Atomic<uint32> futex_obj{0};
        constexpr static uint BUSY_CYCLES = 64;

    public:
        Mutex() = default;

        /// \brief Destroy the mutex.
        ///
        /// Panics if the mutex is destroyed while still held by a thread.
        ~Mutex()
        {
            if (futex_obj.load() != 0)
            {
                panic("mutex destructed with a lock being held");
            }
        };

        Mutex(const Mutex &) = delete;
        Mutex(Mutex &&) = delete;
        Mutex &operator=(const Mutex &) = delete;
        Mutex &operator=(Mutex &&) = delete;

        /// \brief Acquire the mutex, blocking until it becomes available.
        void lock();

        /// \brief Release the mutex.
        void unlock();

        /// \brief Attempt to acquire the mutex without blocking.
        ///
        /// Returns true if the lock was acquired, false otherwise.
        bool tryLock();
    };

} // namespace core::sync
