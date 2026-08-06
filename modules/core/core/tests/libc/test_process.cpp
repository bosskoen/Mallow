// core/core/tests/test_process.cpp
//
// Coverage for core/libc/process.h.
//
// The surface here is deliberately tiny: a per-thread id and two process-exit
// functions. mlwExit / mlwTerminate are MLW_NO_RETURN and actually tear down
// and end the process, so they CANNOT be invoked from a test that needs to
// return — instead we take their addresses (an ODR-use that forces the linker
// to resolve the real symbols) without ever calling them. thread_id is read
// and checked for the one property guaranteed on a single thread: stability.

#include "core/libc/process.h"
#include "core/typedef.h"

using namespace core;

namespace core_core_test
{
    // =======================================================================
    //  thread_id: readable and stable within one thread
    // =======================================================================
    // Ids are assigned lazily on first touch from a uint32 counter and never
    // reused. Across the whole runner (single thread per test dispatch) the
    // value the current thread sees must not change between reads. We cannot
    // assert a specific numeric value — it depends on first-touch order — only
    // that repeated reads agree.
    bool test_process_thread_id_stable()
    {
        uint32 first  = thread_id;
        uint32 second = thread_id;
        if (first != second) return false;
        // Reading through a volatile sink to defeat any CSE, then compare again.
        volatile uint32 sink = thread_id;
        return static_cast<uint32>(sink) == first;
    }

    // =======================================================================
    //  Exit-symbol linkage  (addresses only — never invoked)
    // =======================================================================
    // Calling either function would terminate the runner. Taking their
    // addresses ODR-uses them, so this test fails to LINK if the symbols are
    // missing, while remaining completely safe to execute: the pointers are
    // stored and checked, never dereferenced/called.
    bool test_process_exit_symbols_link()
    {
        void (*const pExit)(int32)      = &mlwExit;
        void (*const pTerminate)(int32) = &mlwTerminate;
        // Volatile sink so the compiler cannot fold these away as unused.
        void (*volatile sink)(int32) = pExit;
        if (sink == nullptr) return false;
        sink = pTerminate;
        return sink != nullptr;
    }
}
