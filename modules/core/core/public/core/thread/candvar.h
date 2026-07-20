#pragma once
#include "lock.h"
#include "mutex.h"

/// \file
/// \brief Condition variable support for the public synchronization API.
///
/// `core::sync::CondVar` provides a simple condition variable built on top of
/// the platform's futex-like primitives. It is intended to be used with
/// `core::sync::Mutex` and `core::sync::Lock<Mutex>` to wait for predicates
/// and wake waiting threads.

namespace core::sync
{

    class Mutex;
    class CondVar
    {
        Atomic<uint32> lock_value{0};

    public:
        CondVar() = default;
        ~CondVar() = default; // could add a release check

        CondVar(const CondVar &) = delete;
        CondVar &operator=(const CondVar &) = delete;
        CondVar(CondVar &&) = delete;
        CondVar &operator=(CondVar &&) = delete;

        /// \brief Wait until the predicate returns true.
        ///
        /// Releases the provided lock while waiting and re-acquires it before
        /// returning. The callback `fn` is invoked in a loop to handle
        /// spurious wakeups.
        ///
        /// \param lock The mutex lock held by the waiting thread.
        /// \param fn   Predicate that returns true when the wait should stop.
        template <typename F>
            requires is_invocable_v<F> && is_same_v<invoke_result_t<F>, bool>
        void wait(Lock<Mutex> &lock, F &&fn)
        {
            while (!fn())
            {
                uint32 val = lock_value.load(MemoryOrder::Acquire);
                lock.unlock();
                detail::mlwFutexWait(lock_value.rawPtrUnsafe(), val);
                lock.relock();
            }
        }

        /// \brief Wake one thread waiting on this condition variable.
        ///
        /// Uses release semantics to publish the wake event before waking the
        /// waiter.
        MLW_FORCE_INLINE void wakeOne()
        {
            lock_value.fetchAdd(1, MemoryOrder::Release);
            detail::mlwFutexWakeOne(lock_value.rawPtrUnsafe());
        };

        /// \brief Wake all threads waiting on this condition variable.
        ///
        /// Uses release semantics to publish the wake event before waking the
        /// waiters.
        MLW_FORCE_INLINE void wakeAll()
        {
            lock_value.fetchAdd(1, MemoryOrder::Release);
            detail::mlwFutexWakeAll(lock_value.rawPtrUnsafe());
        };
    };
} // namespace core::sync
