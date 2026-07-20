#include "thread/mutex.h"

#if defined(MLW_WINDOWS)
#include <windows.h>
//#include <synchapi.h> //need arch data
#elif defined(MLW_LINUX) 
#include "posix/syscall_api.h"
#elif defined(MLW_MAC)
#include <os/os_sync_wait_on_address.h>
#endif

void core::sync::detail::mlwFutexWait(uint32* ptr, uint32 expected) noexcept
{
    #if defined(MLW_WINDOWS)
    WaitOnAddress(ptr, &expected, 4, INFINITE);
    #elif defined(MLW_LINUX)
    syscall(SYS_FUTEX, (long)ptr, FUTEX_WAIT_PRIVATE, expected, 0);
    #elif defined(MLW_MAC)
    os_sync_wait_on_address(ptr, expected, sizeof(uint32), OS_SYNC_WAIT_ON_ADDRESS_NONE);
    #endif
}

void core::sync::detail::mlwFutexWakeOne(uint32* ptr) noexcept
{
    #if defined(MLW_WINDOWS)
    WakeByAddressSingle(ptr);
    #elif defined(MLW_LINUX)
    syscall(SYS_FUTEX, (long)ptr, FUTEX_WAKE_PRIVATE, 1, 0);
    #elif defined(MLW_MAC)
    os_sync_wake_by_address_any(ptr, sizeof(uint32), OS_SYNC_WAKE_BY_ADDRESS_NONE);
    #endif
}
void core::sync::detail::mlwFutexWakeAll(uint32 *ptr) noexcept
{
    #if defined(MLW_WINDOWS)
    WakeByAddressAll(ptr);
    #elif defined(MLW_LINUX)
    syscall(SYS_FUTEX, (long)ptr, FUTEX_WAKE_PRIVATE, NumericLimits<uint32>::max, 0);
    #elif defined(MLW_MAC)
    os_sync_wake_by_address_all(ptr, sizeof(uint32), OS_SYNC_WAKE_BY_ADDRESS_NONE);
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

        detail::mlwFutexWait(futex_obj.rawPtrUnsafe(), 2);
    }
}

void core::sync::Mutex::unlock()
{
    if (futex_obj.exchange(0, MemoryOrder::Release) == 2)
        detail::mlwFutexWakeOne(futex_obj.rawPtrUnsafe());
}

bool core::sync::Mutex::tryLock()
{
    uint32 exp = 0;
    return futex_obj.compareExchangeStrong(exp, 1, MemoryOrder::Acquire, MemoryOrder::Relaxed);
}
