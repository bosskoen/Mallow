#include <core/io/terminal.h>
#include <core/libc/process.h>
#include <core/memory/galloc.h>
#include <core/libc/mem.h>

#include "core/../../private/crt_internals.h"





extern int32 mallowMain();

extern "C" void mlwStart(){
    
    
    core::detail::mlw__crt_init_platforminf();
    new (&core::mlw_g_alloc) core::GAlloc{};


    core::ThreadCache::mlw__first_crt_ctor();
    crt::run_global_ctors();
    crt::run_thread_local_ctors();


    //comand line parsing
    // core::terminal::args.count = c;
    // core::terminal::args.values = v;

    int32 cr =  mallowMain();

    core::mlwExit(cr);

    MLW_UNREACHABLE();
}



#if defined(MLW_LINUX)
#include <core/../../private/posix/syscall_api.h>

unsigned long        mlw_pagesize;
int                  mlw_argc;
char**               mlw_argv;
char**               mlw_envp;
const unsigned long* mlw_auxv;

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

void* mlw_setup_main_tls(usize& leng);
 
extern "C" void mlwStart();
extern "C" void _start_c(unsigned long* sp){
    long argc=(long)sp[0]; char** argv=(char**)(sp+1); char** envp=argv+argc+1;
    char** e=envp; while(*e) ++e;
    auto auxv=(const unsigned long*)(e+1);
    mlw_argc=(int)argc; mlw_argv=argv; mlw_envp=envp; mlw_auxv=auxv;
    for(auto a=auxv;a[0];a+=2) if(a[0]==AT_PAGESZ){ mlw_pagesize=a[1]; break; }

    usize dummy;
    mlw_setup_main_tls(dummy);
    mlwStart();
}
#endif