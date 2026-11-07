#pragma once

#if defined(MLW_LINUX) && defined(FREE_MLW_BUILD)

//=============================================================================
// x86-64
//=============================================================================

#if defined(MLW_X64)

enum : long
{
    SYS_READ = 0,
    SYS_WRITE = 1,
    SYS_CLOSE = 3,
    SYS_OPENAT = 257,
    SYS_LSEEK = 8,
    SYS_FSYNC = 74,
    SYS_FTRUNCATE = 77,
    SYS_STATX = 332,
    SYS_FCHMOD=91,
    SYS_FCHMODAT=268,

    SYS_UNLINKAT = 263,
    SYS_MKDIRAT = 258,
    SYS_RENAMEAT = 264,

    SYS_MMAP = 9,
    SYS_MPROTECT = 10,
    SYS_MUNMAP = 11,
    SYS_MREMAP = 25,

    SYS_CLONE = 56,

    SYS_FUTEX = 202,

    SYS_EXIT_GROUP = 231, // exit full aplication
    SYS_GETDENTS64 = 217,

    SYS_GETCWD = 79,


    SYS_EXIT = 60, // exit thread

    SYS_SCHED_GETAFFINITY = 204,
};

inline long syscall(long n)
{
    long ret;

    asm volatile(
        "syscall"
        : "=a"(ret)
        : "a"(n)
        : "rcx", "r11", "memory");

    return ret;
}

inline long syscall(long n, long a1)
{
    long ret;

    asm volatile(
        "syscall"
        : "=a"(ret)
        : "a"(n),
          "D"(a1)
        : "rcx", "r11", "memory");

    return ret;
}

inline long syscall(long n, long a1, long a2)
{
    long ret;

    asm volatile(
        "syscall"
        : "=a"(ret)
        : "a"(n),
          "D"(a1),
          "S"(a2)
        : "rcx", "r11", "memory");

    return ret;
}

inline long syscall(long n, long a1, long a2, long a3)
{
    long ret;

    asm volatile(
        "syscall"
        : "=a"(ret)
        : "a"(n),
          "D"(a1),
          "S"(a2),
          "d"(a3)
        : "rcx", "r11", "memory");

    return ret;
}

inline long syscall(long n, long a1, long a2, long a3, long a4)
{
    long ret;

    register long r10 asm("r10") = a4;

    asm volatile(
        "syscall"
        : "=a"(ret)
        : "a"(n),
          "D"(a1),
          "S"(a2),
          "d"(a3),
          "r"(r10)
        : "rcx", "r11", "memory");

    return ret;
}

inline long syscall(long n, long a1, long a2, long a3,
                    long a4, long a5)
{
    long ret;

    register long r10 asm("r10") = a4;
    register long r8 asm("r8") = a5;

    asm volatile(
        "syscall"
        : "=a"(ret)
        : "a"(n),
          "D"(a1),
          "S"(a2),
          "d"(a3),
          "r"(r10),
          "r"(r8)
        : "rcx", "r11", "memory");

    return ret;
}

inline long syscall(long n, long a1, long a2, long a3,
                    long a4, long a5, long a6)
{
    long ret;

    register long r10 asm("r10") = a4;
    register long r8 asm("r8") = a5;
    register long r9 asm("r9") = a6;

    asm volatile(
        "syscall"
        : "=a"(ret)
        : "a"(n),
          "D"(a1),
          "S"(a2),
          "d"(a3),
          "r"(r10),
          "r"(r8),
          "r"(r9)
        : "rcx", "r11", "memory");

    return ret;
}

inline long clone(int (*fn)(void *), void *stack_top, long flags,
                  void *arg, int *ptid, int *ctid)
{
    unsigned long top = ((unsigned long)stack_top) & ~15UL; // 16-align
    void **s = (void **)top;
    *--s = (void *)fn; // [s+8]
    *--s = arg;        // [s]   (child pops arg first, then fn)

    long ret;
    register long r10 asm("r10") = (long)ctid; // syscall arg3
    register long r8 asm("r8") = 0;            // syscall arg4 = tls = 0
    asm volatile(
        "syscall\n\t"
        "testq %%rax, %%rax\n\t"
        "jnz 1f\n\t"
        // ---- child: rsp = s ----
        "xorl %%ebp, %%ebp\n\t" // terminate unwind chain
        "popq %%rdi\n\t"        // arg  -> first-arg reg
        "popq %%rax\n\t"        // fn
        "callq *%%rax\n\t"      // fn(arg); rsp now 16-aligned at call
        "movl %%eax, %%edi\n\t" // exit code = fn's return
        "movl $60, %%eax\n\t"   // SYS_exit  (NOT 231/exit_group)
        "syscall\n\t"
        "hlt\n\t"
        "1:\n\t"
        : "=a"(ret)
        : "a"((long)SYS_CLONE), "D"(flags), "S"((long)s),
          "d"((long)ptid), "r"(r10), "r"(r8)
        : "rcx", "r11", "memory");
    return ret;
}

//=============================================================================
// x86 (i386)
//=============================================================================

#elif defined(MLW_X86)

enum : long
{
    SYS_READ = 3,
    SYS_WRITE = 4,
    SYS_CLOSE = 6,
    SYS_OPENAT = 295,
    SYS_LSEEK = 19,
    SYS_FSYNC = 118,
    SYS_FTRUNCATE = 93,
    SYS_LLSEEK = 140,
    SYS_FTRUNCATE64 = 194,
    SYS_STATX = 383,
    SYS_UNLINKAT = 301,
    SYS_MKDIRAT = 296,
    SYS_RENAMEAT = 302,
    SYS_FCHMOD=94,
    SYS_FCHMODAT=306,

    SYS_MMAP2 = 192,
    SYS_MPROTECT = 125,
    SYS_MUNMAP = 91,
    SYS_MREMAP = 163,

    SYS_CLONE = 120,

    SYS_FUTEX = 240,
    SYS_GETDENTS64 = 220,

    SYS_EXIT_GROUP = 252, // exit full aplication

    SYS_GETCWD = 183,

    SYS_EXIT = 1, // exit thread
    SYS_SCHED_GETAFFINITY = 242,
};

inline long syscall(long n)
{
    long ret;

    asm volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(n)
        : "memory");

    return ret;
}

inline long syscall(long n, long a1)
{
    long ret;

    asm volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(n),
          "b"(a1)
        : "memory");

    return ret;
}

inline long syscall(long n, long a1, long a2)
{
    long ret;

    asm volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(n),
          "b"(a1),
          "c"(a2)
        : "memory");

    return ret;
}

inline long syscall(long n, long a1, long a2, long a3)
{
    long ret;

    asm volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(n),
          "b"(a1),
          "c"(a2),
          "d"(a3)
        : "memory");

    return ret;
}

inline long syscall(long n, long a1, long a2,
                    long a3, long a4)
{
    long ret;

    asm volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(n),
          "b"(a1),
          "c"(a2),
          "d"(a3),
          "S"(a4)
        : "memory");

    return ret;
}

inline long syscall(long n, long a1, long a2,
                    long a3, long a4, long a5)
{
    long ret;

    asm volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(n),
          "b"(a1),
          "c"(a2),
          "d"(a3),
          "S"(a4),
          "D"(a5)
        : "memory");

    return ret;
}

inline long syscall(long n, long a1, long a2,
                    long a3, long a4,
                    long a5, long a6)
{
    long ret;

    register long bp asm("ebp") = a6;

    asm volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(n),
          "b"(a1),
          "c"(a2),
          "d"(a3),
          "S"(a4),
          "D"(a5),
          "r"(bp)
        : "memory");

    return ret;
}

inline long clone(int (*fn)(void *), void *stack_top, long flags,
                  void *arg, int *ptid, int *ctid)
{
    unsigned long top = ((unsigned long)stack_top) & ~15UL;
    void **s = (void **)top;
    *--s = arg;        // [s+4]
    *--s = (void *)fn; // [s]   child pops fn, leaves arg at [esp]

    long ret;
    register long esi_tls asm("esi") = 0;           // syscall arg3 = tls = 0
    register long edi_ctid asm("edi") = (long)ctid; // syscall arg4 = ctid
    asm volatile(
        "int $0x80\n\t"
        "testl %%eax, %%eax\n\t"
        "jnz 1f\n\t"
        // ---- child: esp = s, [esp]=fn, [esp+4]=arg ----
        "xorl %%ebp, %%ebp\n\t"
        "popl %%ecx\n\t" // fn  -> ecx (ecx is dead in child)
        // esp now points at arg; call fn with arg already at [esp] -> [esp+4] after call
        "calll *%%ecx\n\t"      // fn(arg)
        "movl %%eax, %%ebx\n\t" // exit code
        "movl $1, %%eax\n\t"    // SYS_exit (i386) = 1
        "int $0x80\n\t"
        "1:\n\t"
        : "=a"(ret)
        : "a"((long)SYS_CLONE), "b"(flags), "c"((long)s),
          "d"((long)ptid), "r"(esi_tls), "r"(edi_ctid)
        : "memory");
    return ret;
}

#elif defined(MLW_ARM32)

enum : long
{
    SYS_READ = 3,
    SYS_WRITE = 4,
    SYS_CLOSE = 6,
    SYS_OPENAT = 322,
    SYS_LSEEK = 19,
    SYS_FSYNC = 118,
    SYS_FTRUNCATE = 93,
    SYS_LLSEEK = 140,
    SYS_FTRUNCATE64 = 194,
    SYS_STATX = 397,
    SYS_UNLINKAT = 328,
    SYS_MKDIRAT = 323,
    SYS_RENAMEAT = 329,
    SYS_FCHMOD=94,
    SYS_FCHMODAT=333,

    SYS_MMAP2 = 192,
    SYS_MPROTECT = 125,
    SYS_MUNMAP = 91,
    SYS_MREMAP = 163,

    SYS_CLONE = 120,

    SYS_FUTEX = 240,
    SYS_GETDENTS64 = 217,

    SYS_EXIT_GROUP = 248, // exit full aplication

    SYS_GETCWD =183,

    SYS_EXIT = 1, // exit thread
    SYS_SCHED_GETAFFINITY = 241,
};

inline long syscall(long n)
{
    register long r7 asm("r7") = n;
    register long r0 asm("r0");

    asm volatile(
        "svc #0"
        : "=r"(r0)
        : "r"(r7)
        : "memory");

    return r0;
}

inline long syscall(long n, long a1)
{
    register long r7 asm("r7") = n;
    register long r0 asm("r0") = a1;

    asm volatile(
        "svc #0"
        : "+r"(r0)
        : "r"(r7)
        : "memory");

    return r0;
}

inline long syscall(long n, long a1, long a2)
{
    register long r7 asm("r7") = n;
    register long r0 asm("r0") = a1;
    register long r1 asm("r1") = a2;

    asm volatile(
        "svc #0"
        : "+r"(r0)
        : "r"(r1), "r"(r7)
        : "memory");

    return r0;
}

inline long syscall(long n, long a1, long a2, long a3)
{
    register long r7 asm("r7") = n;
    register long r0 asm("r0") = a1;
    register long r1 asm("r1") = a2;
    register long r2 asm("r2") = a3;

    asm volatile(
        "svc #0"
        : "+r"(r0)
        : "r"(r1), "r"(r2), "r"(r7)
        : "memory");

    return r0;
}

inline long syscall(long n, long a1, long a2, long a3, long a4)
{
    register long r7 asm("r7") = n;
    register long r0 asm("r0") = a1;
    register long r1 asm("r1") = a2;
    register long r2 asm("r2") = a3;
    register long r3 asm("r3") = a4;

    asm volatile(
        "svc #0"
        : "+r"(r0)
        : "r"(r1), "r"(r2), "r"(r3), "r"(r7)
        : "memory");

    return r0;
}

inline long syscall(long n, long a1, long a2, long a3,
                    long a4, long a5)
{
    register long r7 asm("r7") = n;
    register long r0 asm("r0") = a1;
    register long r1 asm("r1") = a2;
    register long r2 asm("r2") = a3;
    register long r3 asm("r3") = a4;
    register long r4 asm("r4") = a5;

    asm volatile(
        "svc #0"
        : "+r"(r0)
        : "r"(r1), "r"(r2), "r"(r3),
          "r"(r4), "r"(r7)
        : "memory");

    return r0;
}

inline long syscall(long n, long a1, long a2, long a3,
                    long a4, long a5, long a6)
{
    register long r7 asm("r7") = n;
    register long r0 asm("r0") = a1;
    register long r1 asm("r1") = a2;
    register long r2 asm("r2") = a3;
    register long r3 asm("r3") = a4;
    register long r4 asm("r4") = a5;
    register long r5 asm("r5") = a6;

    asm volatile(
        "svc #0"
        : "+r"(r0)
        : "r"(r1), "r"(r2), "r"(r3),
          "r"(r4), "r"(r5), "r"(r7)
        : "memory");

    return r0;
}

inline long clone(int (*fn)(void *), void *stack_top, long flags,
                  void *arg, int *ptid, int *ctid)
{
    unsigned long top = ((unsigned long)stack_top) & ~7UL; // 8-align
    void **s = (void **)top;
    *--s = arg;        // [s+4]
    *--s = (void *)fn; // [s]

    register long r7 asm("r7") = (long)SYS_CLONE;
    register long r0 asm("r0") = flags;      // arg0
    register long r1 asm("r1") = (long)s;    // arg1 = child stack
    register long r2 asm("r2") = (long)ptid; // arg2
    register long r3 asm("r3") = 0;          // arg3 = tls = 0
    register long r4 asm("r4") = (long)ctid; // arg4
    asm volatile(
        "svc #0\n\t"
        "cmp r0, #0\n\t"
        "bne 1f\n\t"
        // ---- child: sp = s, [sp]=fn, [sp+4]=arg ----
        "mov r11, #0\n\t"      // fp = 0
        "ldr r2, [sp]\n\t"     // fn
        "ldr r0, [sp, #4]\n\t" // arg -> first-arg reg
        "add sp, sp, #8\n\t"
        "blx r2\n\t" // fn(arg)
        // r0 = fn's return = exit code
        "mov r7, #1\n\t" // SYS_exit (arm32) = 1
        "svc #0\n\t"
        "1:\n\t"
        : "+r"(r0)
        : "r"(r7), "r"(r1), "r"(r2), "r"(r3), "r"(r4)
        : "memory");
    return r0;
}

#elif defined(MLW_ARM64)

enum : long
{
    SYS_READ = 63,
    SYS_WRITE = 64,
    SYS_CLOSE = 57,
    SYS_OPENAT = 56,
    SYS_LSEEK = 62,
    SYS_FSYNC = 82,
    SYS_FTRUNCATE = 46,
    SYS_STATX = 291,
    SYS_UNLINKAT = 35,
    SYS_MKDIRAT = 34,
    SYS_RENAMEAT2 = 276,
    SYS_FCHMOD=52,
    SYS_FCHMODAT=53,

    SYS_MMAP = 222,
    SYS_MPROTECT = 226,
    SYS_MUNMAP = 215,
    SYS_MREMAP = 216,

    SYS_CLONE = 220,

    SYS_FUTEX = 98,
    SYS_GETDENTS64 = 61,

    SYS_EXIT_GROUP = 94, // exit full aplication

    SYS_EXIT = 93, // exit thread

    SYS_GETCWD = 17,

    SYS_SCHED_GETAFFINITY = 123,
};

inline long syscall(long n)
{
    register long x8 asm("x8") = n;
    register long x0 asm("x0");

    asm volatile(
        "svc #0"
        : "=r"(x0)
        : "r"(x8)
        : "memory");

    return x0;
}

inline long syscall(long n, long a1)
{
    register long x8 asm("x8") = n;
    register long x0 asm("x0") = a1;

    asm volatile(
        "svc #0"
        : "+r"(x0)
        : "r"(x8)
        : "memory");

    return x0;
}

inline long syscall(long n, long a1, long a2)
{
    register long x8 asm("x8") = n;
    register long x0 asm("x0") = a1;
    register long x1 asm("x1") = a2;

    asm volatile(
        "svc #0"
        : "+r"(x0)
        : "r"(x1), "r"(x8)
        : "memory");

    return x0;
}

inline long syscall(long n, long a1, long a2, long a3)
{
    register long x8 asm("x8") = n;
    register long x0 asm("x0") = a1;
    register long x1 asm("x1") = a2;
    register long x2 asm("x2") = a3;

    asm volatile(
        "svc #0"
        : "+r"(x0)
        : "r"(x1), "r"(x2), "r"(x8)
        : "memory");

    return x0;
}

inline long syscall(long n, long a1, long a2,
                    long a3, long a4)
{
    register long x8 asm("x8") = n;
    register long x0 asm("x0") = a1;
    register long x1 asm("x1") = a2;
    register long x2 asm("x2") = a3;
    register long x3 asm("x3") = a4;

    asm volatile(
        "svc #0"
        : "+r"(x0)
        : "r"(x1), "r"(x2), "r"(x3), "r"(x8)
        : "memory");

    return x0;
}

inline long syscall(long n, long a1, long a2,
                    long a3, long a4, long a5)
{
    register long x8 asm("x8") = n;
    register long x0 asm("x0") = a1;
    register long x1 asm("x1") = a2;
    register long x2 asm("x2") = a3;
    register long x3 asm("x3") = a4;
    register long x4 asm("x4") = a5;

    asm volatile(
        "svc #0"
        : "+r"(x0)
        : "r"(x1), "r"(x2), "r"(x3),
          "r"(x4), "r"(x8)
        : "memory");

    return x0;
}

inline long syscall(long n, long a1, long a2,
                    long a3, long a4,
                    long a5, long a6)
{
    register long x8 asm("x8") = n;
    register long x0 asm("x0") = a1;
    register long x1 asm("x1") = a2;
    register long x2 asm("x2") = a3;
    register long x3 asm("x3") = a4;
    register long x4 asm("x4") = a5;
    register long x5 asm("x5") = a6;

    asm volatile(
        "svc #0"
        : "+r"(x0)
        : "r"(x1), "r"(x2), "r"(x3),
          "r"(x4), "r"(x5), "r"(x8)
        : "memory");

    return x0;
}
inline long clone(int (*fn)(void *), void *stack_top, long flags,
                  void *arg, int *ptid, int *ctid)
{
    unsigned long top = ((unsigned long)stack_top) & ~15UL; // 16-align
    void **s = (void **)top;
    s -= 2;            // reserve one 16-byte pair
    s[0] = (void *)fn; // [sp]
    s[1] = arg;        // [sp+8]

    register long x8 asm("x8") = (long)SYS_CLONE;
    register long x0 asm("x0") = flags;      // arg0
    register long x1 asm("x1") = (long)s;    // arg1 = child stack
    register long x2 asm("x2") = (long)ptid; // arg2
    register long x3 asm("x3") = 0;          // arg3 = tls = 0
    register long x4 asm("x4") = (long)ctid; // arg4
    asm volatile(
        "svc #0\n\t"
        "cbnz x0, 1f\n\t"
        // ---- child: sp = s, [sp]=fn, [sp+8]=arg ----
        "mov x29, #0\n\t"           // fp = 0
        "ldp x2, x0, [sp], #16\n\t" // x2=fn, x0=arg ; sp += 16 (stays 16-aligned)
        "blr x2\n\t"                // fn(arg)
        // x0 = fn's return = exit code
        "mov x8, #93\n\t" // SYS_exit (arm64) = 93
        "svc #0\n\t"
        "1:\n\t"
        : "+r"(x0)
        : "r"(x8), "r"(x1), "r"(x2), "r"(x3), "r"(x4)
        : "memory");
    return x0;
}

#endif

static long retrySyscall(long num, long a, long b, long c)
{
    for (;;)
    {
        long r = syscall(num, a, b, c);
        if (r == -4 /*EINTR*/)
            continue; // signal interrupted; try again
        return r;
    }
}

static long retrySyscall(long num, long a, long b, long c, long d)
{
    for (;;)
    {
        long r = syscall(num, a, b, c, d);
        if (r == -4 /*EINTR*/)
            continue; // signal interrupted; try again
        return r;
    }
}


#define MAP_FAILED ((void *)-1)

enum : long
{
    SC_PAGESIZE = 30,
    SC_NPROCESSORS_ONLN = 84,
};

enum : long
{
    PROT_NONE = 0x0,
    PROT_READ = 0x1,
    PROT_WRITE = 0x2,
    PROT_EXEC = 0x4,
};
enum : long
{
    MAP_SHARED = 0x01,
    MAP_PRIVATE = 0x02,
    MAP_FIXED = 0x10,
    MAP_ANONYMOUS = 0x20,
    MAP_ANON = MAP_ANONYMOUS,
    MAP_NORESERVE = 0x4000,
    MAP_POPULATE = 0x8000,
    MAP_STACK = 0x20000,
};
enum : long
{
    MREMAP_MAYMOVE = 1,
    MREMAP_FIXED = 2,
    MREMAP_DONTUNMAP = 4,
};

enum : long
{
    FUTEX_WAIT = 0,
    FUTEX_WAKE = 1,
    FUTEX_REQUEUE = 3,
    FUTEX_CMP_REQUEUE = 4,
    FUTEX_WAKE_OP = 5,
    FUTEX_WAIT_BITSET = 9,
    FUTEX_WAKE_BITSET = 10,
    FUTEX_PRIVATE_FLAG = 128, // 0x80
    FUTEX_CLOCK_REALTIME = 256,
    FUTEX_WAIT_PRIVATE = (FUTEX_WAIT | FUTEX_PRIVATE_FLAG), // 128
    FUTEX_WAKE_PRIVATE = (FUTEX_WAKE | FUTEX_PRIVATE_FLAG), // 129
};

enum : long
{
    AT_PAGESZ = 6,
    AT_HWCAP = 16,
    AT_FDCWD = -100,
};

enum : long
{
    CLONE_VM = 0x00000100,
    CLONE_FS = 0x00000200,
    CLONE_FILES = 0x00000400,
    CLONE_SIGHAND = 0x00000800,
    CLONE_THREAD = 0x00010000,
    CLONE_SYSVSEM = 0x00040000,
    CLONE_SETTLS = 0x00080000,
    CLONE_PARENT_SETTID = 0x00100000,
    CLONE_CHILD_CLEARTID = 0x00200000,
};

static inline long is_err(long r)
{
    return r < 0 && r > -4096;
}

inline long write(int fd, const void *buf, unsigned long n)
{
    long r = syscall(SYS_WRITE, fd, (long)buf, (long)n);
    return is_err(r) ? -1 : r;
}

inline void *mmap(void *addr, unsigned long len, int prot,
                  int flags, int fd, long off)
{
#if defined(MLW_X64) || defined(MLW_ARM64)
    long r = syscall(SYS_MMAP, (long)addr, (long)len, prot, flags, fd, off);
#else
    // 32-bit ABIs have no plain mmap; mmap2's offset is in fixed 4096-byte
    // units (independent of the real page size), so scale it here.
    long r = syscall(SYS_MMAP2, (long)addr, (long)len, prot, flags, fd,
                     off / 4096);
#endif
    return is_err(r) ? MAP_FAILED : (void *)r;
}

inline int munmap(void *addr, unsigned long len)
{
    long r = syscall(SYS_MUNMAP, (long)addr, (long)len);
    return is_err(r) ? -1 : 0;
}

inline int mprotect(void *addr, unsigned long len, int prot)
{
    long r = syscall(SYS_MPROTECT, (long)addr, (long)len, prot);
    return is_err(r) ? -1 : 0;
}

inline void *mremap(void *old_addr, unsigned long old_sz,
                    unsigned long new_sz, int flags, void *new_addr = 0)
{
    if (!(flags & MREMAP_FIXED))
    { // only then is a 5th arg present
        new_addr = 0;
    }
    long r = syscall(SYS_MREMAP, (long)old_addr, (long)old_sz,
                     (long)new_sz, flags, (long)new_addr);
    return is_err(r) ? MAP_FAILED : (void *)r;
}

inline long futex(int *uaddr, int op, int val, const void *timeout /*timespec*/)
{
    long r = syscall(SYS_FUTEX, (long)uaddr, op, val, (long)timeout, 0, 0);
    return is_err(r) ? -1 : r;
}

extern unsigned long mlw_pagesize;
extern int mlw_argc;
extern char **mlw_argv;
extern char **mlw_envp;
extern const unsigned long *mlw_auxv;

struct timespec
{
    long tv_sec;  // whole seconds
    long tv_nsec; // nanoseconds (0 .. 999,999,999)
};

struct StatxTimestamp
{
    int64 sec;
    uint32 nsec;
    uint32 _reserved;
};

struct Statx
{
    uint32 mask;                   //   0
    uint32 blksize;                //   4
    uint64 attributes;             //   8
    uint32 nlink;                  //  16
    uint32 uid;                    //  20
    uint32 gid;                    //  24
    uint16 mode;                   //  28
    uint16 _spare0;                //  30
    uint64 ino;                    //  32
    uint64 size;                   //  40
    uint64 blocks;                 //  48
    uint64 attributes_mask;        //  56
    StatxTimestamp atime;          //  64
    StatxTimestamp btime;          //  80
    StatxTimestamp ctime;          //  96
    StatxTimestamp mtime;          // 112
    uint32 rdev_major, rdev_minor; // 128
    uint32 dev_major, dev_minor;   // 136
    uint64 mnt_id;                 // 144
    uint64 _spare2;                // 152
    uint64 _spare3[12];            // 160 .. 256
};

static_assert(sizeof(Statx) == 256, "statx ABI is a fixed 256 bytes");

struct linux_dirent64
{
    uint64 d_ino;    //  0
    int64  d_off;    //  8
    uint16 d_reclen; // 16  total record length incl. padding — advance by this
    uint8  d_type;   // 18
    char   d_name[]; // 19  NUL-terminated (flexible array member)
};

enum : uint8 { DT_UNKNOWN=0, DT_FIFO=1, DT_CHR=2, DT_DIR=4,
               DT_BLK=6, DT_REG=8, DT_LNK=10, DT_SOCK=12 };

enum : uint16
{
    S_IFMT = 0170000,
    S_IFREG = 0100000,
    S_IFDIR = 0040000,
    S_IFLNK = 0120000
};

enum : long
{
    AT_EMPTY_PATH = 0x1000,
    AT_SYMLINK_NOFOLLOW = 0x100,
    AT_REMOVEDIR = 0x200
};
enum : uint32
{
    STATX_TYPE = 0x0001,
    STATX_MODE = 0x0002,
    STATX_SIZE = 0x0200,
    STATX_BTIME = 0x0800,
    STATX_BASIC_STATS = 0x07ff
};
// Linux x86-64 open flags (octal in the kernel ABI)
enum : long
{
    O_RDONLY = 0,
    O_WRONLY = 1,
    O_RDWR = 2,
    O_CREAT = 0100,
    O_TRUNC = 01000,
    O_APPEND = 02000,
    O_EXCL = 0200,
    O_DIRECTORY = 0200000,
};

#elif defined(MLW_MAC)
#error "mac not implemented"

#else
#error "no windows."
#endif