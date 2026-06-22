#pragma once
#include "lock.h"
#include "atomic.h"

namespace core::sync
{
    class Mutex;
    class CondVar
    {
        Atomic<uint32> lock_value{0};

    public:
        CondVar() = default;
        ~CondVar() = default; // could add a releace check

        CondVar(const CondVar &) = delete;
        CondVar &operator=(const CondVar &) = delete;
        CondVar(CondVar &&) = delete;
        CondVar &operator=(CondVar &&) = delete;

        template <typename F>
            requires is_invocable_v<F> && is_same_v<invoke_result_t<F>, bool>
        void wait(Lock<Mutex> &lock, F &&fn)
        {
            while (!fn())
            {
                uint32 val = lock_value.load(MemoryOrder::Acquire);
                lock.unlock();
                mlwFutexWait(lock_value.rawPtrUnsafe(), val);
                lock.relock();
            }
        }

        MLW_FORCE_INLINE void wakeOne()
        {
            lock_value.fetchAdd(1, MemoryOrder::Release);
            mlwFutexWakeOne(lock_value.rawPtrUnsafe());
        };
        MLW_FORCE_INLINE void wakeAll()
        {
            lock_value.fetchAdd(1, MemoryOrder::Release);
            mlwFutexWakeAll(lock_value.rawPtrUnsafe());
        };
    };
} // namespace core::sync
