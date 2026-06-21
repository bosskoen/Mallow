#include "thread/mutex.h"

#if defined(MLW_WINDOWS)
#include <windows.h>
//#include <synchapi.h> //need arch data
#elif defined(MLW_LINUX) 
#include <linux/futex.h>
#include <sys/syscall.h>
#elif defined(MLW_MAC)
#include <os/ulock.h>
#endif

MLW_FORCE_INLINE static void futexWait(uint32* ptr, uint32 expected) noexcept
{
#if defined(MLW_WINDOWS)
    WaitOnAddress(ptr, &expected, 4, INFINITE);
#elif defined(MLW_LINUX)
    syscall(SYS_futex, ptr, FUTEX_WAIT_PRIVATE, expected, nullptr);
#elif defined(MLW_MAC)
    __ulock_wait(UL_COMPARE_AND_WAIT, ptr, expected, 0);
#endif
}

static void futexWakeOne(uint32* ptr) noexcept
{
#if defined(MLW_WINDOWS)
    WakeByAddressSingle(ptr);
#elif defined(MLW_LINUX)
    syscall(SYS_futex, ptr, FUTEX_WAKE_PRIVATE, 1, nullptr);
#elif defined(MLW_MAC)
    __ulock_wake(UL_COMPARE_AND_WAIT, ptr, 0);
#endif
}

void core::sync::Mutex::lock()
{
    uint32 exp = 0;
    if (futex_obj.compareExchangeStrong(exp, 1, MemoryOrder::Acquire, MemoryOrder::Relaxed))
        return;

    for (uint i = 0; i < BUSY_CYCLES; i++)
    {
        exp = 0;
        if (futex_obj.compareExchangeStrong(exp, 1, MemoryOrder::Acquire, MemoryOrder::Relaxed))
            return;
        MLW_CPU_PAUSE();
    }

    while (true)
    {
        if (futex_obj.exchange(2, MemoryOrder::Acquire) == 0)
            return;

        futexWait(futex_obj.rawPtrUnsafe(), 2);
    }
}

void core::sync::Mutex::unlock()
{
    if (futex_obj.exchange(0, MemoryOrder::Release) == 2)
        futexWakeOne(futex_obj.rawPtrUnsafe());
}

bool core::sync::Mutex::tryLock()
{
    uint32 exp = 0;
    return futex_obj.compareExchangeStrong(exp, 1, MemoryOrder::Acquire, MemoryOrder::Relaxed);
}
