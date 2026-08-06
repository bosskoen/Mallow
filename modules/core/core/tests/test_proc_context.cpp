// core/core/tests/test_proc_context.cpp
//
// Coverage for core/proc_context.h. There is only one function, getSysInfo(),
// and its exact return values are host-dependent, so the tests assert
// invariants that must hold on any supported target rather than fixed numbers.
//
// NOTE: getSysInfo() performs a real syscall / auxv lookup, so it only runs
// meaningfully inside the freestanding runtime (this file still compiles
// everywhere).

#include "core/proc_context.h"

using namespace core;

namespace core_core_test
{
    // ---- page size is a non-zero power of two -----------------------------
    bool test_proc_page_size()
    {
        SysInfo si = PROC_CONTEXT.getSysInfo();
        if (si.page_size == 0) return false;
        // power-of-two check
        if ((si.page_size & (si.page_size - 1)) != 0) return false;
        return true;
    }

    // ---- allocation granularity is non-zero -------------------------------
    bool test_proc_alloc_gran()
    {
        SysInfo si = PROC_CONTEXT.getSysInfo();
        if (si.alloc_gran == 0) return false;
        if ((si.alloc_gran & (si.alloc_gran - 1)) != 0) return false;
        return true;
    }

    // ---- architecture is one of the known enum values ---------------------
    bool test_proc_arch_known()
    {
        SysInfo si = PROC_CONTEXT.getSysInfo();
        switch (si.arch)
        {
            case SysInfo::CPUType::x64:
            case SysInfo::CPUType::arm32:
            case SysInfo::CPUType::arm64:
            case SysInfo::CPUType::x86:
                return true;                     // any concrete arch is fine
            case SysInfo::CPUType::unknown:
            default:
                return false;                    // should be resolved on a real target
        }
    }

    // ---- at least one CPU is reported -------------------------------------
    // If this fails on Linux it very likely indicates the shadowed-`count`
    // variable in the Linux getSysInfo() (the inner block redeclares `count`,
    // so the outer one stays 0). See private/proc_context.cpp.
    bool test_proc_cpu_count()
    {
        SysInfo si = PROC_CONTEXT.getSysInfo();
        return si.cpu_count >= 1;
    }

    // ---- the global context object is addressable and well-typed ----------
    bool test_proc_context_fields()
    {
        // Compile/access smoke test: the fields exist with the documented types
        // and PROC_CONTEXT is a single shared instance.
        index_t argc = PROC_CONTEXT.argc;
        char** argv  = PROC_CONTEXT.argv;
        (void)argc; (void)argv;
        // argc is never negative for a real process context.
        return PROC_CONTEXT.argc >= 0;
    }
} // namespace core_core_test
