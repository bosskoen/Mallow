#pragma once

#include "atomic.h"

namespace core::sync
{

    class Mutex
    {
        // 0 = unlocked
        // 1 = locked, no waiters
        // 2 = locked, waiters sleeping
        Atomic<uint32> futex_obj{0};
        constexpr static uint BUSY_CYCLES = 64;

    public:
        Mutex() = default;
        ~Mutex()
        {
            if (futex_obj.load() != 0)
            {
                panic("mutex destructed with a lock being held");
            }
        };
        Mutex(const &Mutex) = delete;
        Mutex(&&Mutex) = delete;
        Mutex &operator=(const &Mutex) = delete;
        Mutex &operator=(&&Mutex) = delete;

        void lock();
        void unlock();
        bool tryLock();
    };

} // namespace core::sync
