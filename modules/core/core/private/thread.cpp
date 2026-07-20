/// \file
/// \brief Platform implementations of the thread seam declared in thread.h.
///
/// Defines core::detail::sys_spawn / sys_join / try_join for Windows
/// (CreateThread) and Linux (raw clone). Both honor the same contract: the
/// spawned thread owns and frees its ThreadStart, and the joiner reclaims the
/// OS resources (handle / stack + state). This file is where the per-OS CRT
/// bring-up and teardown ordering lives; the header only declares the seam.

#include "thread/thread.h"

#include "crt_internals.h"
#include "memory/galloc.h"
#include "macro.h"

#ifdef MLW_WINDOWS
#include <windows.h>

// OS entry stub. Windows calls this with the ThreadStart pointer as LPVOID.
// The per-thread CRT bring-up/teardown is skipped under the MSVC ABI, where the
// MSVC runtime already runs thread-local init/fini for us; under a non-MSVC ABI
// on Windows we do it ourselves, mirroring the Linux path.
DWORD WINAPI threadEntry(LPVOID s)
{
#ifndef MLW_ABI_MSVC
    core::ThreadCache::mlw__first_crt_ctor();
    crt::run_thread_local_ctors(); // no real definition just symitry
#endif
    core::detail::ThreadStart *start = static_cast<core::detail::ThreadStart *>(s);
    start->fn(start->args);

    core::mlwFree(start); // child owns ThreadStart (see thread.h); free after fn returns

#ifndef MLW_ABI_MSVC
    crt::run_thread_local_dtors();
    core::ThreadCache::mlw__crt_distroy_tc_storage();
    core::detail::mlw__crt_distroy_format_buffer();
#endif
    return 0;
}

// On Windows the opaque handle IS the OS thread HANDLE (no side state struct).
void core::detail::sys_join(void* &handle)
{
    WaitForSingleObject(handle, INFINITE);
    CloseHandle(handle); // reclaim; handle is now not-joinable
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
    // WAIT_TIMEOUT — still running, DON'T close (stays joinable).
    // NOTE: WAIT_FAILED also lands here and is treated as "still running"; a
    // genuinely failed wait would therefore never reclaim. Acceptable given the
    // handle is always valid here, but worth knowing.
    return false;
}

#elif defined(MLW_LINUX)

#include "posix/syscall_api.h"
#include "libc/mem.h"

// ── Linux threading model ──────────────────────────────────────────────────
// We build threads on the raw clone() wrapper rather than pthreads. Each thread
// gets its own mmap'd stack and a small heap-allocated LinuxThreadState that
// outlives the thread until the joiner reclaims it. Join/timeout are done with
// the kernel's CLEARTID+futex mechanism, not a library primitive.
//
// Lifetime split (mirrors Windows):
//   * ThreadStart  — owned by the child, freed inside threadEntry.
//   * stack + state — owned by the joiner, reclaimed in sys_join / try_join.

// Set up this thread's TLS block; returns base and (via out param) length.
void *mlw_setup_main_tls(usize &leng, usize pagesize);

// clone() flag set. Together these make the child a real sibling thread sharing
// the parent's address space and descriptors, and wire up the join mechanism:
//   CLONE_VM/FS/FILES/SIGHAND/THREAD/SYSVSEM — share memory, cwd, fd table,
//     signal handlers, thread group, and SysV semaphore undo (i.e. a thread,
//     not a process).
//   CLONE_PARENT_SETTID  — kernel writes the child TID into our ptid slot.
//   CLONE_CHILD_CLEARTID — kernel zeroes the ctid slot and futex-wakes it when
//     the child exits. This is what join() waits on.
    constexpr int MLW_CLONE_FLAGS =
        CLONE_VM | CLONE_FS | CLONE_FILES | CLONE_SIGHAND | CLONE_THREAD |
        CLONE_SYSVSEM | CLONE_PARENT_SETTID | CLONE_CHILD_CLEARTID;

constexpr long DEFAULT_STACK = 1 << 20;     // 1 MiB, matches your PE default

// Side state kept per thread. `tid` is both the PARENT_SETTID target (set at
// clone) and the CHILD_CLEARTID target (zeroed + futex-woken on exit), so the
// joiner watches this single word. `stack`/`stack_size` are the mmap to reclaim.
struct LinuxThreadState
{
    volatile int tid; // CHILD_CLEARTID target: kernel zeroes + wakes on exit
    void *stack;      // mmap base to reclaim
    usize stack_size;
};

// infinite wait — used by sys_join
static long futex_wait_forever(volatile int *word, int expect)
{
    return futex((int *)word, FUTEX_WAIT, expect, nullptr); // null timeout = block forever
}

// finite wait — used by try_join; ms == 0 means poll (return immediately)
static long futex_wait_timed(volatile int *word, int expect, uint32 ms)
{
    timespec ts{(long)(ms / 1000), (long)((ms % 1000) * 1000000L)};
    return futex((int *)word, FUTEX_WAIT, expect, &ts); // always a timeout, even {0,0}
}

// Child entry, invoked by the clone() wrapper on the new stack.
int threadEntry(void *s)
{
    // TLS must exist before any CRT/allocator use below.
    usize tls_size;
    void *tls_base = mlw_setup_main_tls(tls_size, core::PLATFORM_INFO.page_size);

    core::ThreadCache::mlw__first_crt_ctor(); // the load-bearing one: per-thread allocator caches
    crt::run_thread_local_ctors();            // no-op on Itanium, kept for symmetry

    auto *start = static_cast<core::detail::ThreadStart *>(s);
    start->fn(start->args);
    core::mlwFree(start); // child owns ThreadStart, same as Windows

    crt::run_thread_local_dtors();
    core::ThreadCache::mlw__crt_distroy_tc_storage();
    core::detail::mlw__crt_distroy_format_buffer();

    // Tear down our own TLS. The mmap'd *stack* is NOT freed here — the thread
    // is still running on it. The joiner reclaims the stack after it sees tid==0
    // (see sys_join), which the kernel sets only once we have fully exited and
    // no longer touch the stack. That ordering is what makes it safe.
    //could be nullptr but if that is julst let it crash. it sould have chashed already.
    munmap(tls_base, tls_size);

    return 0; // -> wrapper does SYS_exit (thread!), kernel clears tid + futex_wake
}

uint32 core::detail::sys_spawn(void* &h, ThreadStart *s)
{
    LinuxThreadState *st = static_cast<LinuxThreadState *>(mlwMalloc(sizeof(LinuxThreadState)));

    st->stack_size = DEFAULT_STACK;
    // NOTE: mmap result isn't checked; a MAP_FAILED here yields a bad stack_top
    // and clone below fails, which the error path handles (munmap of MAP_FAILED
    // is a harmless no-op). Add a guard page via mprotect if desired.
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
        futex_wait_forever(&st->tid, t); 
    }
    // Safe now: tid==0 means the child has exited and no longer uses the stack.
    munmap(st->stack, st->stack_size);
    mlwFree(st); // now not-joinable (same contract as Windows)
}

bool core::detail::try_join(void* &h, uint32 ms)
{
    LinuxThreadState *st = (LinuxThreadState *)h;
    int t = __atomic_load_n(&st->tid, __ATOMIC_ACQUIRE);
    if (t != 0)
        futex_wait_timed(&st->tid, t, ms); // relative timeout; ETIMEDOUT if still running
    // NOTE: ms_wait treats ms==0 as *infinite*, so try_join(h, 0) blocks rather
    // than polling — the opposite of the Windows path. Callers wanting a poll
    // must pass a nonzero timeout.
    if (__atomic_load_n(&st->tid, __ATOMIC_ACQUIRE) != 0)
        return false;                      // timeout — DON'T reclaim, stays joinable
    munmap(st->stack, st->stack_size); // ready — reclaim, mirror sys_join
    mlwFree(st);

    return true;
}
#else

#endif