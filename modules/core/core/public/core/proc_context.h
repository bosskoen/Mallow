#pragma once
#include "typedef.h"

namespace core{

    struct SysInfo {
    usize page_size;
    usize alloc_gran;
    uint32   cpu_count;
    // optional, add only if used:
    // uptr  affinity_mask;
    enum class CPUType { x64, arm32, arm64, x86, unknown} arch;
    };

    extern struct ProcContext{
        index_t               argc;
        char**               argv;       // pointer to OS-owned array (or parsed-once on Win)
        char**               envp = nullptr;       // pointer to OS-owned array / nothing on windwos
        SysInfo getSysInfo();
    } PROC_CONTEXT;


}