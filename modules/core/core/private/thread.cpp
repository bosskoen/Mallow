#include "thread/thread.h"

#include "crt_internals.h"
#include "memory/galloc.h"
#include "macro.h"

#ifdef MLW_WINDOWS
#include <windows.h>

DWORD WINAPI threadEntry(LPVOID s)
{
#ifndef MLW_ABI_MSVC
    core::ThreadCache::mlw__first_crt_ctor();
    crt::run_thread_local_ctors(); // no real definition just symitry
#endif
    core::detail::ThreadStart *start = static_cast<core::detail::ThreadStart *>(s);
    start->fn(start->args);

    core::mlwFree(start);

#ifndef MLW_ABI_MSVC
    crt::run_thread_local_dtors();
    core::ThreadCache::mlw__crt_distroy_tc_storage();
    core::detail::mlw__crt_distroy_format_buffer();
#endif
    return 0;
}

void core::detail::sys_join(void* &handle)
{
    WaitForSingleObject(handle, INFINITE);
    CloseHandle(handle);
}
uint32 core::detail::sys_spawn(void* &h, ThreadStart *s)
{
    h = CreateThread(nullptr, 0, &threadEntry, s, 0, nullptr);
    return h ? 0u : (uint32)GetLastError(); // capture immediately
}

bool core::detail::try_join(void* &h, uint32 ms)
{
    uint32 r = WaitForSingleObject(h, ms);
    if (r == 0)
    {                      // WAIT_OBJECT_0 — thread done
        CloseHandle(h); // now not-joinable
        return true;       // ready
    }
    return false; // WAIT_TIMEOUT (or anything else) — still running, DON'T close
}

#elif defined(MLW_LINUX)

#include "posix/syscall_api.h"

void *mlw_setup_main_tls(usize &leng);

    constexpr int MLW_CLONE_FLAGS =
        CLONE_VM | CLONE_FS | CLONE_FILES | CLONE_SIGHAND | CLONE_THREAD |
        CLONE_SYSVSEM | CLONE_PARENT_SETTID | CLONE_CHILD_CLEARTID;

constexpr long DEFAULT_STACK = 1 << 20;     // 1 MiB, matches your PE default

struct LinuxThreadState
{
    volatile int tid; // CHILD_CLEARTID target: kernel zeroes + wakes on exit
    void *stack;      // mmap base to reclaim
    usize stack_size;
};

long ms_wait(volatile int *word, int expect, uint32 ms)
{
    timespec ts{(long)(ms / 1000), (long)((ms % 1000) * 1000000L)};
    return futex((int *)word, FUTEX_WAIT_PRIVATE, expect, ms ? &ts : nullptr);
}

int threadEntry(void *s)
{
    usize tls_size;
    void *tls_base = mlw_setup_main_tls(tls_size);

    core::ThreadCache::mlw__first_crt_ctor(); // the load-bearing one
    crt::run_thread_local_ctors();            // no-op on Itanium, kept for symmetry

    auto *start = static_cast<core::detail::ThreadStart *>(s);
    start->fn(start->args);
    core::mlwFree(start); // child owns ThreadStart, same as Windows

    crt::run_thread_local_dtors();
    core::ThreadCache::mlw__crt_distroy_tc_storage();
    core::detail::mlw__crt_distroy_format_buffer();

    munmap(tls_base, tls_size);

    return 0; // -> wrapper does SYS_exit (thread!), kernel clears tid + futex_wake
}

uint32 core::detail::sys_spawn(void* &h, ThreadStart *s)
{
    LinuxThreadState *st = static_cast<LinuxThreadState *>(mlwMalloc(sizeof(LinuxThreadState)));

    st->stack_size = DEFAULT_STACK;
    st->stack = mmap(nullptr, st->stack_size, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS | MAP_STACK, -1, 0);
    // (optional: mprotect the lowest page PROT_NONE as a guard)

    void *stack_top = (char *)st->stack + st->stack_size; // grows down

    long rc = clone(&threadEntry, stack_top, MLW_CLONE_FLAGS,
                        s,                // arg -> threadEntry(void*)
                        (int *)&st->tid,  // ptid: kernel writes child TID here...
                        (int *)&st->tid); // ctid: ...and clears+wakes it on exit

    if (rc < 0)
    { // clone failed: reclaim everything WE own
        munmap(st->stack, st->stack_size);
        mlwFree(st);
        // note: caller's spawn() still frees `s` on the error path (child never ran)
        return (uint32)(-rc); // errno
    }
    h = st; // stash state ptr in the handle slot
    return 0;
}

void core::detail::sys_join(void* &h)
{
    LinuxThreadState *st = (LinuxThreadState *)h;
    // kernel set tid at clone (CLONE_PARENT_SETTID) and clears it to 0 on exit.
    // loop because futex can return spuriously / on EINTR / on EAGAIN.
    for (;;)
    {
        int t = __atomic_load_n(&st->tid, __ATOMIC_ACQUIRE);
        if (t == 0)
            break;               // child already exited
        ms_wait(&st->tid, t, 0); // 0 = infinite
    }
    munmap(st->stack, st->stack_size);
    mlwFree(st); // now not-joinable (same contract as Windows)
}

bool core::detail::try_join(void* &h, uint32 ms)
{
    LinuxThreadState *st = (LinuxThreadState *)h;
    int t = __atomic_load_n(&st->tid, __ATOMIC_ACQUIRE);
    if (t != 0)
        ms_wait(&st->tid, t, ms); // relative timeout; ETIMEDOUT if still running
    if (__atomic_load_n(&st->tid, __ATOMIC_ACQUIRE) != 0)
        return false;                      // timeout — DON'T reclaim, stays joinable
    munmap(st->stack, st->stack_size); // ready — reclaim, mirror sys_join
    mlwFree(st);

    return true;
}
#else

#endif