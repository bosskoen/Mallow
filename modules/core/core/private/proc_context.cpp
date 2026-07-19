#include "proc_context.h"

constinit core::ProcContext core::PROC_CONTEXT{};

#ifdef MLW_WINDOWS
#include <windows.h>

core::SysInfo core::ProcContext::getSysInfo(){
		SYSTEM_INFO si;
		GetSystemInfo(&si);
        core::SysInfo::CPUType type =core::SysInfo::CPUType::unknown ;
        switch(si.wProcessorArchitecture){
            case PROCESSOR_ARCHITECTURE_AMD64:
                type = core::SysInfo::CPUType::x64;
            break;
            case PROCESSOR_ARCHITECTURE_ARM:
                type = core::SysInfo::CPUType::arm32;
            break;
            case PROCESSOR_ARCHITECTURE_ARM64:
                type = core::SysInfo::CPUType::arm64;
            break;
            case PROCESSOR_ARCHITECTURE_INTEL:
                type = core::SysInfo::CPUType::x86;
            break;
        }

     core::SysInfo info{
            .page_size = static_cast<usize>(si.dwPageSize),
            .alloc_gran = static_cast<usize>(si.dwAllocationGranularity),
            .cpu_count = si.dwNumberOfProcessors,
    // optional, add only if used:
    // uptr  affinity_mask;
            .arch = type,
        };
        return info;
}

#elif defined(MLW_LINUX)
#include "posix/syscall_api.h"

extern "C" unsigned long __getauxval(unsigned long type);

core::SysInfo core::ProcContext::getSysInfo(){

    #ifdef MLW_ARM32
        core::SysInfo::CPUType type = core::SysInfo::CPUType::arm32;
    #elif defined(MLW_ARM64)
        core::SysInfo::CPUType type = core::SysInfo::CPUType::arm64;
    #elif defined(MLW_X64)
        core::SysInfo::CPUType type = core::SysInfo::CPUType::x64;
    #elif defined(MLW_X86)
        core::SysInfo::CPUType type = core::SysInfo::CPUType::x86;
    #endif

       usize page = (usize)__getauxval(6 /*AT_PAGESZ*/);
    if (page == 0) page = 4096;  

    uint32 count = 0;
    unsigned long mask[16] = {0};          // 16*64 = 1024 CPUs max, plenty
    long r = syscall(SYS_SCHED_GETAFFINITY, 0 /*self*/, sizeof(mask), (long)mask);
    if (r < 0) {count= 1; }     
    else{            // fallback: assume 1 core
    // r = number of BYTES written; popcount those bytes
    uint32 count = 0;
    unsigned long words = (unsigned long)r / sizeof(unsigned long);
    for (unsigned long i = 0; i < words; ++i)
        count += (uint32)__builtin_popcountl(mask[i]);   // __builtin_popcountl
    count = count ? count : 1;
     }

     core::SysInfo info{
            .page_size = page,
            .alloc_gran= page,
            .cpu_count = count,
    // optional, add only if used:
    // uptr  affinity_mask;
            .arch = type,
        };
        return info;
}

#else
#error "not implemented"
#endif