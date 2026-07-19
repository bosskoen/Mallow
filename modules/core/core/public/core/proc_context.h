#pragma once
#include "typedef.h"

/// \file
/// \brief Process context and system information for the freestanding core.

namespace core
{

    /// \brief System information for the current process.
    struct SysInfo
    {
        usize page_size;   ///< System page size in bytes.
        usize alloc_gran;  ///< Allocation granularity in bytes.
        uint32 cpu_count;  ///< Number of CPUs available to the process.
        // optional, add only if used:
        // uptr affinity_mask;
        enum class CPUType { x64, arm32, arm64, x86, unknown } arch; ///< CPU architecture.
    };

    /// \brief Global process context for command line and environment access.
    extern struct ProcContext
    {
        index_t argc;        ///< Argument count.
        char **argv;         ///< Pointer to OS-owned argument array.
        char **envp = nullptr; ///< Pointer to OS-owned environment array, or nullptr on Windows.

        /// \brief Query system information from this process context.
        SysInfo getSysInfo();
    } PROC_CONTEXT;


}