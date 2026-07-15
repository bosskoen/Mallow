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

extern "C" unsigned long __getauxval(unsigned long type);

using uptr = unsigned long;

// Program-header struct differs between 32- and 64-bit ELF (field order!).
#if defined(MLW_X64) || defined(MLW_ARM64)
struct Elf_Phdr { unsigned type, flags; uptr offset, vaddr, paddr, filesz, memsz, align; };
#else
struct Elf_Phdr { unsigned type, offset, vaddr, paddr, filesz, memsz, flags, align; };
#endif

enum { AT_PHDR = 3, AT_PHNUM = 5, PT_TLS = 7 };

static uptr mlw_align_up(uptr x, uptr a) { return (x + a - 1) & ~(a - 1); }

static void mlw_setup_main_tls()
{
    // --- common: locate the program's TLS template via the aux vector ------
    auto phdr = (const Elf_Phdr*)__getauxval(AT_PHDR);
    unsigned long n = __getauxval(AT_PHNUM);
    const Elf_Phdr* t = nullptr;
    for (unsigned long i = 0; i < n; i++)
        if (phdr[i].type == PT_TLS) { t = &phdr[i]; break; }
    if (!t) return;                                   // no thread_local in program

    const uptr WORD = sizeof(void*);                  // 4 on 32-bit, 8 on 64-bit
    const uptr TCB  = 2 * WORD;                        // reserved control block
    uptr align = t->align < TCB ? TCB : t->align;      // honor PT_TLS p_align
    const char* img = (const char*)t->vaddr;

#if defined(MLW_ARM64) || defined(MLW_ARM32)
    // ---- Variant I: TP -> TCB, data above ---------------------------------
    uptr reserve = mlw_align_up(TCB, align);
    char* raw = (char*)mmap(nullptr, mlw_align_up(reserve + t->memsz + align, mlw_pagesize),
                            PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (raw == (char*)MAP_FAILED) return;
    char* base = (char*)mlw_align_up((uptr)raw, align);
    char* data = base + reserve;
    for (unsigned long i = 0; i < t->filesz; i++) data[i] = img[i];   // .tbss stays 0
    uptr tp = (uptr)base;

  #if defined(MLW_ARM64)
    asm volatile("msr tpidr_el0, %0" :: "r"(tp) : "memory");
  #else // MLW_ARM32 — set via the ARM-private set_tls syscall (0x0f0005)
    syscall1(0x0f0005, (long)tp);
    // NOTE: compile ARM with -mtp=cp15 (default on ARMv6K+/v7) so thread_local
    // reads TPIDRURO directly; otherwise provide __aeabi_read_tp.
  #endif

#else
    // ---- Variant II: TP -> TCB at top, data below -------------------------
    uptr tsize = mlw_align_up(t->memsz, align);        // size of the data region
    char* raw = (char*)mmap(nullptr, mlw_align_up(tsize + TCB + align, mlw_pagesize),
                            PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (raw == (char*)MAP_FAILED) return;
    uptr  tp   = mlw_align_up((uptr)raw + tsize, align);  // TP aligned, tsize below it
    char* data = (char*)(tp - tsize);
    for (unsigned long i = 0; i < t->filesz; i++) data[i] = img[i];
    *(void**)tp = (void*)tp;                            // self-pointer at %fs/%gs:0

  #if defined(MLW_X64)
    syscall(158 /*arch_prctl*/, 0x1002 /*ARCH_SET_FS*/, (long)tp, 0);
  #else // MLW_X86 — set_thread_area, then load the returned selector into %gs
    unsigned desc[4] = { 0xffffffffu, (unsigned)tp, 0x000fffffu, 0x51u };
    // entry=-1(pick), base=tp, limit=4GB pages, flags: 32bit|limit_in_pages|useable
    syscall(243 /*set_thread_area*/, (long)desc);
    unsigned short sel = (unsigned short)((desc[0] << 3) | 3);  // GDT, RPL 3
    asm volatile("movw %0, %%gs" :: "r"(sel));
  #endif
#endif
}
 
extern "C" void mlwStart();
extern "C" void _start_c(unsigned long* sp){
    long argc=(long)sp[0]; char** argv=(char**)(sp+1); char** envp=argv+argc+1;
    char** e=envp; while(*e) ++e;
    auto auxv=(const unsigned long*)(e+1);
    mlw_argc=(int)argc; mlw_argv=argv; mlw_envp=envp; mlw_auxv=auxv;
    for(auto a=auxv;a[0];a+=2) if(a[0]==AT_PAGESZ){ mlw_pagesize=a[1]; break; }

    mlw_setup_main_tls();
    mlwStart();
}
#endif