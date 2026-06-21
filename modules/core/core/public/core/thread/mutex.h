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
        constexpr static uint BUSY_CYCLES  = 64;
    public:
        // no copy no move

        void lock();
        void unlock();
        bool tryLock();
    };

} // namespace core::sync
