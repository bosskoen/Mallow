#pragma once


namespace core {

// ── the pieces the startup sequence is built from ───────────────────────
namespace detail {
    void mlw__crt_init_platforminf();            // fill PLATFORM_INFO (eager)
    void mlw__crt_distroy_format_buffer();       // free per-thread format buffer
}

} // namespace core
namespace crt
{
    // ── global ctor/dtor walking (ABI-specific implementation) ──────────────
    // run_global_ctors: MSVC walks .CRT$XC; Itanium walks .init_array.
    // run_global_dtors: drains whatever atexit/__cxa_atexit registered, LIFO.
    void run_global_ctors();
    void run_global_dtors();
    
    
    // ── thread-local dtor draining (the compiler-registered list) ───────────
    // Drains dtors registered via __tlregdtor / __cxa_thread_atexit for THIS thread.
    // Called from: Thread wrapper exit (spawned), mlwExit (main thread).
    void run_thread_local_ctors();
    void run_thread_local_dtors();
    
} // namespace crt

