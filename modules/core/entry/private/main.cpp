#include <core/io/terminal.h>
#include <core/libc/process.h>
#include <core/memory/galloc.h>
#include <core/libc/mem.h>

#include "core/../../private/crt_internals.h"

#include <core/proc_context.h>

#if MLW_WINDOWS
#include <windows.h>

// call AFTER your allocator is up (you're deferring this — good)
void mlw_build_win_args() {
    int wargc = 0;
    LPWSTR* wargv = CommandLineToArgvW(GetCommandLineW(), &wargc);
    // wargv[i] is a null-terminated UTF-16 string; wargc is the count

    // 1. array of char* pointers, +1 for the NULL terminator (Linux convention)
    char** argv = (char**)core::mlwMalloc((wargc + 1) * sizeof(char*));

    for (int i = 0; i < wargc; ++i) {
        // 2. how many UTF-8 bytes does this wide string need?
        int len = WideCharToMultiByte(
            CP_UTF8, 0,        // UTF-8, no special flags
            wargv[i], -1,      // -1 = source is null-terminated, measure whole thing
            nullptr, 0,        // null dst + 0 size = "just tell me the byte count"
            nullptr, nullptr);
        // len includes the null terminator

        // 3. allocate and actually convert
        char* s = (char*)core::mlwMalloc(len);
        WideCharToMultiByte(CP_UTF8, 0, wargv[i], -1, s, len, nullptr, nullptr);
        argv[i] = s;
    }
    argv[wargc] = nullptr;    // NULL-terminate the array, like Linux argv

    core::PROC_CONTEXT.argc = wargc;
    core::PROC_CONTEXT.argv = argv;

    LocalFree(wargv);         // <-- MUST free what CommandLineToArgvW returned
}
#endif




extern int32 mallowMain();

extern "C" void mlwStart(){
    
    
    core::detail::mlw__crt_init_platforminf();
    new (&core::mlw_g_alloc) core::GAlloc{};


    core::detail::ThreadCache::mlw__first_crt_ctor();
    crt::run_global_ctors();
    crt::run_thread_local_ctors();

#if MLW_WINDOWS
    mlw_build_win_args();
#endif
    int32 cr =  mallowMain();

    core::mlwExit(cr);

    MLW_UNREACHABLE();
}



#if defined(MLW_LINUX)
#include <core/../../private/posix/syscall_api.h>



#define AT_PAGESZ 6
#if defined(MLW_ARM64)
asm(".text\n.balign 4\n.global _start\n.type _start,%function\n_start:\n\t mov x0, sp\n\t bl _start_c\n\t brk #0\n");
#elif defined(MLW_X64)
asm(".text\n.balign 16\n.global _start\n.type _start,@function\n_start:\n\t xor %rbp,%rbp\n\t mov %rsp,%rdi\n\t and $-16,%rsp\n\t call _start_c\n\t hlt\n");
#elif defined(MLW_X86)
asm(".text\n.balign 16\n.global _start\n.type _start,@function\n_start:\n\t xor %ebp,%ebp\n\t mov %esp,%eax\n\t and $-16,%esp\n\t sub $12,%esp\n\t push %eax\n\t call _start_c\n\t hlt\n");
#elif defined(MLW_ARM32)
asm(".text\n.balign 4\n.global _start\n.type _start,%function\n_start:\n\t mov r0, sp\n\t bl _start_c\n\t udf #0\n");
#endif


// ===========================================================================
//  mlw_setup_main_tls() — main-thread TLS bring-up, all four arches.
//
//  Two ABI variants:
//    Variant I  (ARM/AArch64): thread pointer -> TCB, TLS data ABOVE it.
//                              variable addr = tp + reserve + offset
//    Variant II (x86/x86-64):  thread pointer -> TCB at TOP, data BELOW it.
//                              variable addr = tp - tls_size + offset
//
//  Requires (already in your CRT): __getauxval(), mmap(), mlw_pagesize,
//  and mlw_auxv set before this runs. Call it in _start_c before mlwStart().
//
//  Verified live under qemu: MLW_ARM64, MLW_X64, MLW_X86.
//  MLW_ARM32: same structure as ARM64 (Variant I, 8-byte TCB); by analogy.
// ===========================================================================



void* mlw_setup_main_tls(usize& leng, usize page_size);
 

extern const unsigned long* mlw_crt_auxv; 

extern "C" void mlwStart();
extern "C" void _start_c(unsigned long* sp){
    long argc=(long)sp[0]; 
    char** argv=(char**)(sp+1); 
    char** envp=argv+argc+1;
    char** e=envp; while(*e) ++e;
    auto auxv=(const unsigned long*)(e+1);

    mlw_crt_auxv=auxv;
    
     usize page = 4096;                        // fallback
    for (auto a = auxv; a[0]; a += 2)
        if (a[0] == AT_PAGESZ) { page = a[1]; break; } 


    usize dummy;
    mlw_setup_main_tls(dummy, page);

    core::PROC_CONTEXT.argc = (index_t)argc;
    core::PROC_CONTEXT.argv = argv;
    core::PROC_CONTEXT.envp = envp;

    mlwStart();
}
#endif