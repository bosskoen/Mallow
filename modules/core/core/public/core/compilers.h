#pragma once
#include "typedef.h"

#if !defined(MLW_X64) && !defined(MLW_ARM64) && !defined(MLW_X86) && !defined(MLW_ARM32)
#error "unknown architecture - add architecture specific definitions"
#endif

#if !defined(MLW_MSVC) && !defined(MLW_GCC) && !defined(MLW_CLANG)
#error "unknown compiler - add compiler specific definitions"
#endif

#if defined(MLW_MSVC)
#define MLW_ASSUME_ALIGNED(ptr, alignment) ((__assume((reinterpret_cast<usize>(ptr) & ((alignment) - 1)) == 0)), (ptr))
#elif defined(MLW_GCC) || defined(MLW_CLANG)
#define MLW_ASSUME_ALIGNED(ptr, alignment) __builtin_assume_aligned(ptr, alignment)

#endif

#if defined(MLW_MSVC)
static __forceinline usize mlw_ctz64(uint64 x)
{
	unsigned long idx;
	_BitScanForward64(&idx, x);
	return static_cast<usize>(idx);
}
#define MLW_CTZ(x) mlw_ctz64(x)
#elif defined(MLW_GCC) || defined(MLW_CLANG)

#define MLW_CTZ(x) __builtin_ctzll(x)
#endif

#if defined(MLW_MSVC)

static __forceinline usize mlw_clz64(uint64 x)
{
	unsigned long idx;
	_BitScanReverse64(&idx, x);
	return 63u - static_cast<usize>(idx);
}

#define MLW_CLZ(x) mlw_clz64(x)

#elif defined(MLW_GCC) || defined(MLW_CLANG)

#define MLW_CLZ(x) __builtin_clzll(x)

#endif

#if defined(MLW_MSVC)
#define MLW_FORCE_INLINE __forceinline
#elif defined(MLW_GCC) || defined(MLW_CLANG)
#define MLW_FORCE_INLINE __attribute__((always_inline)) inline
#else
#define MLW_FORCE_INLINE inline
#endif

#if defined(MLW_MSVC)
#define MLW_NO_RETURN __declspec(noreturn)
#elif defined(MLW_GCC) || defined(MLW_CLANG)
#define MLW_NO_RETURN __attribute__((noreturn))
#else
#define MLW_NO_RETURN [[noreturn]]
#endif

#if defined(MLW_MSVC)
// MSVC interlocked functions are always seq_cst on x86
// these values are only used as documentation / passed to the Atomic
// class which will ignore them on MSVC and always emit full barriers
#define MLW_MO_RELAXED 0
#define MLW_MO_ACQUIRE 1
#define MLW_MO_RELEASE 2
#define MLW_MO_ACQ_REL 3
#define MLW_MO_SEQ_CST 4
#else
// GCC/Clang: these map directly to the __atomic builtins
#define MLW_MO_RELAXED __ATOMIC_RELAXED
#define MLW_MO_ACQUIRE __ATOMIC_ACQUIRE
#define MLW_MO_RELEASE __ATOMIC_RELEASE
#define MLW_MO_ACQ_REL __ATOMIC_ACQ_REL
#define MLW_MO_SEQ_CST __ATOMIC_SEQ_CST
#endif

#if defined(MLW_MSVC)
#if defined(MLW_X64) || defined(MLW_X86)
// forward declare MSVC x86/x64 intrinsics manually
extern "C" void _ReadWriteBarrier();
extern "C" void _mm_pause();
extern "C" void _mm_mfence();
extern "C" void _mm_lfence();
extern "C" void _mm_sfence();

#pragma intrinsic(_ReadWriteBarrier)
#pragma intrinsic(_mm_pause)
#pragma intrinsic(_mm_mfence)
#pragma intrinsic(_mm_lfence)
#pragma intrinsic(_mm_sfence)
#elif defined(MLW_ARM64)
extern "C" void __yield();
extern "C" void __dmb(unsigned int);
#endif
#endif

#if defined(MLW_MSVC)
#define MLW_COMPILER_BARRIER() _ReadWriteBarrier()
#if defined(MLW_X64) || defined(MLW_X86)
#define MLW_CPU_PAUSE() _mm_pause()
#define MLW_FENCE_FULL() _mm_mfence()
#define MLW_FENCE_LOAD() _mm_lfence()
#define MLW_FENCE_STORE() _mm_sfence()
#elif defined(MLW_ARM64) || defined(MLW_ARM32)
#define MLW_CPU_PAUSE() __yield()
#define MLW_FENCE_FULL() __dmb(_ARM64_BARRIER_ISH)
#define MLW_FENCE_LOAD() __dmb(_ARM64_BARRIER_ISHLD)
#define MLW_FENCE_STORE() __dmb(_ARM64_BARRIER_ISHST)
#endif
#elif defined(MLW_GCC) || defined(MLW_CLANG)
#define MLW_COMPILER_BARRIER() __asm__ volatile("" ::: "memory")
#if defined(MLW_X64) || defined(MLW_X86)
#define MLW_CPU_PAUSE() __asm__ volatile("pause" ::: "memory")
#define MLW_FENCE_FULL() __asm__ volatile("mfence" ::: "memory")
#define MLW_FENCE_LOAD() __asm__ volatile("lfence" ::: "memory")
#define MLW_FENCE_STORE() __asm__ volatile("sfence" ::: "memory")
#elif defined(MLW_ARM64) || defined(MLW_ARM32)
#define MLW_CPU_PAUSE() __asm__ volatile("yield" ::: "memory")
#define MLW_FENCE_FULL() __asm__ volatile("dmb ish" ::: "memory")
#define MLW_FENCE_LOAD() __asm__ volatile("dmb ishld" ::: "memory")
#define MLW_FENCE_STORE() __asm__ volatile("dmb ishst" ::: "memory")
#endif
#endif

#if defined(MLW_MSVC)

#elif defined(MLW_GCC) || defined(MLW_CLANG)

#endif

#if defined(MLW_DEBUG)
#if defined(MLW_MSVC)
#define MLW_DEBUGBREAK() __debugbreak()
#elif defined(MLW_X64) || defined(MLW_X86)
#define MLW_DEBUGBREAK() __asm__ volatile("int3")
#elif defined(MLW_ARM64)
#define MLW_DEBUGBREAK() __asm__ volatile("brk #0")
#elif defined(MLW_ARM32)
#define MLW_DEBUGBREAK() __asm__ volatile("bkpt #0")
#else
#define MLW_DEBUGBREAK() __builtin_trap() // fallback, not a true breakpoint but stops execution
#endif
#else
#define MLW_DEBUGBREAK()
#endif

MLW_FORCE_INLINE void* operator new(decltype(sizeof(0)), void* ptr) { return ptr; }