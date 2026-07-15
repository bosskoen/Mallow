#pragma once

#if defined(MLW_LINUX) && defined(FREE_MLW_BUILD)

//=============================================================================
// x86-64
//=============================================================================

#if defined(MLW_X64)


enum
{
    SYS_WRITE       =   1,

    SYS_MMAP        =   9,
    SYS_MPROTECT    =  10,
    SYS_MUNMAP      =  11,
    SYS_MREMAP      =  25,

    SYS_CLONE       =  56,

    SYS_FUTEX       = 202,

    SYS_EXIT_GROUP  = 231,
};


inline long syscall(long n)
{
    long ret;

    asm volatile
    (
        "syscall"
        : "=a"(ret)
        : "a"(n)
        : "rcx", "r11", "memory"
    );

    return ret;
}

inline long syscall(long n, long a1)
{
    long ret;

    asm volatile
    (
        "syscall"
        : "=a"(ret)
        : "a"(n),
          "D"(a1)
        : "rcx", "r11", "memory"
    );

    return ret;
}

inline long syscall(long n, long a1, long a2)
{
    long ret;

    asm volatile
    (
        "syscall"
        : "=a"(ret)
        : "a"(n),
          "D"(a1),
          "S"(a2)
        : "rcx", "r11", "memory"
    );

    return ret;
}

inline long syscall(long n, long a1, long a2, long a3)
{
    long ret;

    asm volatile
    (
        "syscall"
        : "=a"(ret)
        : "a"(n),
          "D"(a1),
          "S"(a2),
          "d"(a3)
        : "rcx", "r11", "memory"
    );

    return ret;
}

inline long syscall(long n, long a1, long a2, long a3, long a4)
{
    long ret;

    register long r10 asm("r10") = a4;

    asm volatile
    (
        "syscall"
        : "=a"(ret)
        : "a"(n),
          "D"(a1),
          "S"(a2),
          "d"(a3),
          "r"(r10)
        : "rcx", "r11", "memory"
    );

    return ret;
}

inline long syscall(long n, long a1, long a2, long a3,
                            long a4, long a5)
{
    long ret;

    register long r10 asm("r10") = a4;
    register long r8  asm("r8")  = a5;

    asm volatile
    (
        "syscall"
        : "=a"(ret)
        : "a"(n),
          "D"(a1),
          "S"(a2),
          "d"(a3),
          "r"(r10),
          "r"(r8)
        : "rcx", "r11", "memory"
    );

    return ret;
}

inline long syscall(long n, long a1, long a2, long a3,
                            long a4, long a5, long a6)
{
    long ret;

    register long r10 asm("r10") = a4;
    register long r8  asm("r8")  = a5;
    register long r9  asm("r9")  = a6;

    asm volatile
    (
        "syscall"
        : "=a"(ret)
        : "a"(n),
          "D"(a1),
          "S"(a2),
          "d"(a3),
          "r"(r10),
          "r"(r8),
          "r"(r9)
        : "rcx", "r11", "memory"
    );

    return ret;
}

//=============================================================================
// x86 (i386)
//=============================================================================

#elif defined(MLW_X86)


enum
{
    SYS_WRITE       =   4,

    SYS_MMAP2       = 192,
    SYS_MPROTECT    = 125,
    SYS_MUNMAP      =  91,
    SYS_MREMAP      = 163,

    SYS_CLONE       = 120,

    SYS_FUTEX       = 240,

    SYS_EXIT_GROUP  = 252,
};


inline long syscall(long n)
{
    long ret;

    asm volatile
    (
        "int $0x80"
        : "=a"(ret)
        : "a"(n)
        : "memory"
    );

    return ret;
}

inline long syscall(long n, long a1)
{
    long ret;

    asm volatile
    (
        "int $0x80"
        : "=a"(ret)
        : "a"(n),
          "b"(a1)
        : "memory"
    );

    return ret;
}

inline long syscall(long n, long a1, long a2)
{
    long ret;

    asm volatile
    (
        "int $0x80"
        : "=a"(ret)
        : "a"(n),
          "b"(a1),
          "c"(a2)
        : "memory"
    );

    return ret;
}

inline long syscall(long n, long a1, long a2, long a3)
{
    long ret;

    asm volatile
    (
        "int $0x80"
        : "=a"(ret)
        : "a"(n),
          "b"(a1),
          "c"(a2),
          "d"(a3)
        : "memory"
    );

    return ret;
}

inline long syscall(long n, long a1, long a2,
                            long a3, long a4)
{
    long ret;

    asm volatile
    (
        "int $0x80"
        : "=a"(ret)
        : "a"(n),
          "b"(a1),
          "c"(a2),
          "d"(a3),
          "S"(a4)
        : "memory"
    );

    return ret;
}

inline long syscall(long n, long a1, long a2,
                            long a3, long a4, long a5)
{
    long ret;

    asm volatile
    (
        "int $0x80"
        : "=a"(ret)
        : "a"(n),
          "b"(a1),
          "c"(a2),
          "d"(a3),
          "S"(a4),
          "D"(a5)
        : "memory"
    );

    return ret;
}

inline long syscall(long n, long a1, long a2,
                            long a3, long a4,
                            long a5, long a6)
{
    long ret;

    register long bp asm("ebp") = a6;

    asm volatile
    (
        "int $0x80"
        : "=a"(ret)
        : "a"(n),
          "b"(a1),
          "c"(a2),
          "d"(a3),
          "S"(a4),
          "D"(a5),
          "r"(bp)
        : "memory"
    );

    return ret;
}



#elif defined(MLW_ARM32)

enum
{
    SYS_WRITE       =   4,

    SYS_MMAP2       = 192,
    SYS_MPROTECT    = 125,
    SYS_MUNMAP      =  91,
    SYS_MREMAP      = 163,

    SYS_CLONE       = 120,

    SYS_FUTEX       = 240,

    SYS_EXIT_GROUP  = 248,
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

#elif defined(MLW_ARM64)

enum
{
    SYS_WRITE       =  64,

    SYS_MMAP        = 222,
    SYS_MPROTECT    = 226,
    SYS_MUNMAP      = 215,
    SYS_MREMAP      = 216,

    SYS_CLONE       = 220,

    SYS_FUTEX       =  98,

    SYS_EXIT_GROUP  =  94,
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


#endif

#define MAP_FAILED    ((void *)-1)
#define MREMAP_FIXED  2
#define AT_PAGESZ     6
#define AT_HWCAP      16

#define SC_PAGESIZE          30
#define SC_NPROCESSORS_ONLN  84

#define PROT_NONE   0x0
#define PROT_READ   0x1
#define PROT_WRITE  0x2
#define PROT_EXEC   0x4

// mmap flags  (same on x86/x86-64/arm/arm64 — the asm-generic values)
#define MAP_SHARED     0x01
#define MAP_PRIVATE    0x02
#define MAP_FIXED      0x10
#define MAP_ANONYMOUS  0x20
#define MAP_ANON       MAP_ANONYMOUS
#define MAP_NORESERVE  0x4000
#define MAP_POPULATE   0x8000

// mremap flags
#define MREMAP_MAYMOVE  1
#define MREMAP_FIXED    2
#define MREMAP_DONTUNMAP 4


// futex op codes  (kernel ABI — identical on every arch)
#define FUTEX_WAIT            0
#define FUTEX_WAKE            1
#define FUTEX_REQUEUE         3
#define FUTEX_CMP_REQUEUE     4
#define FUTEX_WAKE_OP         5
#define FUTEX_WAIT_BITSET     9
#define FUTEX_WAKE_BITSET     10

#define FUTEX_PRIVATE_FLAG    128      // 0x80
#define FUTEX_CLOCK_REALTIME  256

#define FUTEX_WAIT_PRIVATE    (FUTEX_WAIT | FUTEX_PRIVATE_FLAG)   // 128
#define FUTEX_WAKE_PRIVATE    (FUTEX_WAKE | FUTEX_PRIVATE_FLAG)   // 129


static inline int is_err(long r) { return r < 0 && r > -4096; }

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
             unsigned long new_sz, int flags, void* new_addr = 0)
{
    if (!(flags & MREMAP_FIXED)) {          // only then is a 5th arg present
        new_addr =0;
    }
    long r = syscall(SYS_MREMAP, (long)old_addr, (long)old_sz,
                      (long)new_sz, flags, (long)new_addr);
    return is_err(r) ? MAP_FAILED : (void *)r;
}

extern unsigned long        mlw_pagesize;
extern int                  mlw_argc;
extern char**               mlw_argv;
extern char**               mlw_envp;
extern const unsigned long* mlw_auxv;


#elif defined(MLW_MAC)
#error "mac not implemented"

#else
#error "no windows."
#endif