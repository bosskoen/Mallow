#pragma once
#include "core/compilers.h"

#if defined(__FAST_MATH__)
#error "libm TU built with -ffast-math/-Ofast: overflow & nan handling will break"
#endif
#if defined(__FINITE_MATH_ONLY__) && (__FINITE_MATH_ONLY__ > 0)
#error "libm TU built with -ffinite-math-only: inf/nan branches get deleted"
#endif
#if defined(_M_FP_FAST)
#error "libm TU built with /fp:fast: overflow & nan handling will break"
#endif

static MLW_FORCE_INLINE f64 eval_as_double(f64 x)
{
	f64 y = x;
	return y;
}

static MLW_FORCE_INLINE f32 eval_as_float(f32 x)
{
	f32 y = x;
	return y;
}

static inline void fp_force_eval(f64 x)
{
	volatile f64 y;
	y = x;
	(void)y;
}

static inline void fp_force_evalf(f32 x)
{
	volatile f32 y;
	y = x;
	(void)y;
}

static MLW_FORCE_INLINE f64 fp_barrier(f64 x)
{
	volatile f64 y = x;
	return y;
}
static MLW_FORCE_INLINE f32 fp_barrierf(f32 x)
{
	volatile f32 y = x;
	return y;
}

static MLW_FORCE_INLINE f64 __math_xflow(uint32 sign, f64 y) { return eval_as_double(fp_barrier(sign ? -y : y) * y); }
static MLW_FORCE_INLINE f32 __math_xflowf(uint32 sign, f32 y) { return eval_as_float(fp_barrierf(sign ? -y : y) * y); }
static MLW_FORCE_INLINE f64 __math_oflow(uint32 sign) { return __math_xflow(sign, 0x1p769); }
static MLW_FORCE_INLINE f32 __math_oflowf(uint32 sign) { return __math_xflowf(sign, 0x1p97f); }
static MLW_FORCE_INLINE f64 __math_uflow(uint32 sign) { return __math_xflow(sign, 0x1p-767); }
static MLW_FORCE_INLINE f32 __math_uflowf(uint32 sign)
{
	return __math_xflowf(sign, 0x1p-95f);
}

static MLW_FORCE_INLINE f32 __math_invalid(f32 x)
{
	return (x - x) / (x - x);
}

static MLW_FORCE_INLINE f64 __math_invalid(f64 x)
{
	return (x - x) / (x - x);
}

static MLW_FORCE_INLINE f32 __math_divzerof(uint32 sign)
{
	return fp_barrierf(sign ? -1.0f : 1.0f) / fp_barrierf(0.0f);
}

static MLW_FORCE_INLINE f64 __math_divzero(uint32 sign)
{
	return fp_barrier(sign ? -1.0 : 1.0) / fp_barrier(0.0);
}