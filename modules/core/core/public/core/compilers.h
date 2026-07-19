#pragma once
#include "typedef.h"

/// \file
/// \brief Compiler and architecture compatibility macros and helpers.
///
/// This header exposes small portability shims (likely/unlikely, force-inline,
/// count-trailing-zeros, fences, debugbreak, etc.) that map to compiler/arch
/// builtins. Only the public macros are documented here; implementations are
/// platform-specific and intentionally terse.

#if !defined(MLW_X64) && !defined(MLW_ARM64) && !defined(MLW_X86) && !defined(MLW_ARM32)
#error "unknown architecture - add architecture specific definitions"
#endif

#if !defined(MLW_MSVC) && !defined(MLW_GCC) && !defined(MLW_CLANG)
#error "unknown compiler - add compiler specific definitions"
#endif

#if defined(MLW_MSVC)
#define MLW_ASSUME_ALIGNED(ptr, alignment) ((__assume((reinterpret_cast<usize>(ptr) & ((alignment) - 1)) == 0)), (ptr))
#elif defined(MLW_GCC) || defined(MLW_CLANG)
#define MLW_ASSUME_ALIGNED(ptr, alignment) reinterpret_cast<decltype(ptr)>(__builtin_assume_aligned(ptr, alignment))

#endif

/// \def MLW_LAUNDER(ptr)
/// \brief Laxly obtain a pointer to an object after placement-new.
///
/// `MLW_LAUNDER(ptr)` maps to `std::launder`/compiler builtin where needed to
/// restrict optimizer assumptions about object identity after placement-new.
/// On platforms where no launder is required it is a no-op.

#if defined(MLW_GCC) || defined(MLW_CLANG)

#define MLW_LAUNDER(ptr) __builtin_launder(ptr)
#elif defined(MLW_MSVC)
// launder emits no code anywhere; it only restrains optimizer assumptions about
// object identity after placement-new. MSVC doesn't make those assumptions
// (its own std::launder is identity), so identity is correct here � not a stopgap.
// Do NOT substitute a fence/NOP: those are CPU memory barriers, orthogonal to this.
#define MLW_LAUNDER(ptr) (ptr)
#endif

/// \def MLW_LIKELY(x)
/// \brief Hint to the optimizer that `x` is usually true.
/// Maps to `__builtin_expect` on GCC/Clang; a no-op elsewhere.

/// \def MLW_UNLIKELY(x)
/// \brief Hint to the optimizer that `x` is usually false.
/// Maps to `__builtin_expect` on GCC/Clang; a no-op elsewhere.

#if defined(MLW_GCC) || defined(MLW_CLANG)
	#define MLW_LIKELY(x)   (__builtin_expect(!!(x), 1))
	#define MLW_UNLIKELY(x) (__builtin_expect(!!(x), 0))
#else                       // MSVC and anything else: no-op, still correct
    #define MLW_LIKELY(x)   (x)
    #define MLW_UNLIKELY(x) (x)
#endif

/// \def MLW_CTZ(x)
/// \brief Count trailing zero helpers.
///
/// `MLW_CTZ(x)` returns the number of trailing zero bits.
/// These map to efficient compiler intrinsics where available.

#if defined(MLW_MSVC)
/// \cond
static __forceinline usize mlw_ctz64(uint64 x)
{
	unsigned long idx;
	_BitScanForward64(&idx, x);
	return static_cast<usize>(idx);
}
/// \endcond

#define MLW_CTZ(x) mlw_ctz64(x)
#elif defined(MLW_GCC) || defined(MLW_CLANG)

#define MLW_CTZ(x) static_cast<usize>(__builtin_ctzll(x))
#endif


/// \def MLW_CLZ(x)
/// \brief Count leading zero helpers.
///
/// `MLW_CTZ(x)` returns the number of leading zero bits.
/// These map to efficient compiler intrinsics where available.

#if defined(MLW_MSVC)
/// \cond
static __forceinline usize mlw_clz64(uint64 x)
{
	unsigned long idx;
	_BitScanReverse64(&idx, x);
	return 63u - static_cast<usize>(idx);
}
/// \endcond

#define MLW_CLZ(x) mlw_clz64(x)

#elif defined(MLW_GCC) || defined(MLW_CLANG)

#define MLW_CLZ(x) __builtin_clzll(x)

#endif

/// \def MLW_FORCE_INLINE
/// \brief Force an inline request to the compiler where supported.

#if defined(MLW_MSVC)
#define MLW_FORCE_INLINE __forceinline
#elif defined(MLW_GCC) || defined(MLW_CLANG)
#define MLW_FORCE_INLINE __attribute__((always_inline)) inline
#else
#define MLW_FORCE_INLINE inline
#endif

/// \def MLW_NO_RETURN
/// \brief Function attribute indicating the function does not return.

#if defined(MLW_MSVC)
#define MLW_NO_RETURN __declspec(noreturn)
#elif defined(MLW_GCC) || defined(MLW_CLANG)
#define MLW_NO_RETURN __attribute__((noreturn))
#else
#define MLW_NO_RETURN [[noreturn]]
#endif



/// \defgroup memory_order Memory Order Constants
/// \brief Ordering values passed to the `Atomic` helpers.
///
/// On GCC/Clang these map directly to the `__atomic` builtins. On MSVC the
/// interlocked intrinsics are always sequentially consistent, so `Atomic`
/// ignores these values and always emits a full barrier — they exist there
/// only for source compatibility. Never depend on the numeric values.
/// @{

/// \def MLW_MO_RELAXED
/// \brief No ordering, only atomicity.
/// \def MLW_MO_ACQUIRE
/// \brief Later reads/writes cannot move before this load.
/// \def MLW_MO_RELEASE
/// \brief Earlier reads/writes cannot move after this store.
/// \def MLW_MO_ACQ_REL
/// \brief Both acquire and release, for read-modify-write ops.
/// \def MLW_MO_SEQ_CST
/// \brief Full sequential consistency (single total order).

/// @}

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




/// \cond
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
/// \endcond

/// \defgroup fences Compiler Barriers and CPU Fences
/// \brief Compiler-reordering barriers, CPU memory fences, and spin hints.
///
/// Each maps to the architecture's native instruction (x86 `mfence`/`lfence`/
/// `sfence`, ARM `dmb` variants) or, for the compiler barrier, an empty asm
/// clobber. These order memory at the CPU level; they are unrelated to
/// \ref MLW_LAUNDER, which only restrains the compiler's object-identity model.
/// @{

/// \def MLW_COMPILER_BARRIER
/// \brief Prevent the compiler from reordering memory ops across this point.
///        Emits no CPU instruction.
/// \def MLW_CPU_PAUSE
/// \brief Spin-loop hint (x86 `pause`, ARM `yield`); reduces contention.
/// \def MLW_FENCE_FULL
/// \brief Full memory barrier: no load or store crosses in either direction.
/// \def MLW_FENCE_LOAD
/// \brief Load-acquire barrier: orders loads before later memory ops.
/// \def MLW_FENCE_STORE
/// \brief Store-release barrier: orders stores after earlier memory ops.

/// @}

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

/// \def MLW_DEBUGBREAK()
/// \brief Injects a breakpoint instruction for use while debugging.
///
/// \warning Only use when a debugger is attached; otherwise behavior may be
/// unpredictable.
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

/// \def MLW_UNREACHABLE()
/// \brief Hint that a code path is unreachable to the optimizer.
///
/// Expands to a compiler builtin where available; otherwise expands to a no-op.
#if defined(MLW_MSVC)
#define MLW_UNREACHABLE() __assume(0)
#elif defined(MLW_GCC) || defined(MLW_CLANG)
#define MLW_UNREACHABLE() __builtin_unreachable()
#else
#define MLW_UNREACHABLE() ((void)0)
#endif

#if defined(FREE_MLW_BUILD)
MLW_FORCE_INLINE void* operator new(decltype(sizeof(0)), void* ptr) noexcept { return ptr; }

void operator delete(void*, usize) noexcept;
#endif