#pragma once
#include "../compilers.h"
 
/// \file
/// \brief Process and thread termination, and the per-thread id.
 
namespace core
{
    /// \brief Unique id for the calling thread.
    ///
    /// Assigned lazily on first access (thread_local init) from a global counter.
    /// \note Ids are handed out in *first-touch* order, not thread-spawn order,
    ///       and are never reused, so the value only reflects "which thread
    ///       touched this first," not creation sequence. The counter is uint32.
    const extern thread_local uint32 thread_id;
 
    /// \brief Exit the process gracefully, running CRT teardown first.
    ///
    /// Runs thread-local and global destructors and tears down the per-thread
    /// caches before asking the OS to exit. Use this for a normal shutdown; use
    /// \ref mlwTerminate for an immediate abort with no teardown.
    ///
    /// \warning Undefined behavior if called from any thread other than the main
    ///          thread, or after the main thread has already exited (global
    ///          teardown must run exactly once, from the main thread).
    /// \note Teardown timing differs by platform: on Windows, ExitProcess lets
    ///       the loader fire DLL_PROCESS_DETACH, where the teardown block runs;
    ///       on Linux there is no such callback, so teardown runs inline here
    ///       before the exit syscall.
    MLW_NO_RETURN void mlwExit(int32 status);
 
    /// \todo User-registerable atexit handlers (not required, but convenient).
    /// \todo Per-thread graceful exit (distinct from process exit).
 
    /// \brief Terminate the process immediately, without any teardown.
    ///
    /// No destructors, no cache teardown — the fastest way out, for
    /// unrecoverable states. Prefer \ref mlwExit for normal shutdown.
    MLW_NO_RETURN void mlwTerminate(int32 status);
}