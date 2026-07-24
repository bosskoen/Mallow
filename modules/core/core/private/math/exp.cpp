// =============================================================================
//  Double-precision base-e and base-2 exponential - core::mlwExp(f64) / core::mlwExp2(f64)
//
//  From Arm optimized-routines, obtained via musl libc (math/exp.c).
//  Adapted for Mallow (project types / conventions).
//
//  Copyright (c) 2018, Arm Limited.
//  SPDX-License-Identifier: MIT
// =============================================================================
#include "core/libc/math.h"
#include "math/helpers.hpp"

#include "exp_data.inl"

/* Handle cases that may overflow or underflow when computing the result that
   is scale*(1+TMP) without intermediate rounding.  The bit representation of
   scale is in SBITS, however it has a computed exponent that may have
   overflown into the sign bit so that needs to be adjusted before using it as
   a double.  (int32_t)KI is the k used in the argument reduction and exponent
   adjustment of scale, positive k here means the result may overflow and
   negative k means the result may underflow. */
static inline f64 exp_specialcase(f64 tmp, uint64 sbits, uint64 ki)
{
	f64 scale, y;

	if ((ki & 0x80000000) == 0) {
		/* k > 0, the exponent of scale might have overflowed by <= 460.  */
		sbits -= 1009ull << 52;
		scale = core::mlwBitCast<f64>(sbits);
		y = 0x1p1009 * (scale + scale * tmp);
		return eval_as_double(y);
	}
	/* k < 0, need special care in the subnormal range.  */
	sbits += 1022ull << 52;
	scale = core::mlwBitCast<f64>(sbits);
	y = scale + scale * tmp;
	if (y < 1.0) {
		/* Round y to the right precision before scaling it into the subnormal
		 range to avoid double rounding that can cause 0.5+E/2 ulp error where
		 E is the worst-case ulp error outside the subnormal range.  So this
		 is only useful if the goal is better than 1 ulp worst-case error.  */
		f64 hi, lo;
		lo = scale - y + scale * tmp;
		hi = 1.0 + y;
		lo = 1.0 - hi + y + lo;
		y = eval_as_double(hi + lo) - 1.0;
		/* Avoid -0.0 with downward rounding.  */
		if (y == 0.0)
			y = 0.0;
		/* The underflow exception needs to be signaled explicitly.  */
		fp_force_eval(fp_barrier(0x1p-1022) * 0x1p-1022);
	}
	y = 0x1p-1022 * y;
	return eval_as_double(y);
}



f64 core::mlwExp(f64 x)
{
	uint32 abstop;
	uint64 ki, idx, top, sbits;
	f64 kd, z, r, r2, scale, tail, tmp;

	abstop = top12(x) & 0x7ff;
	if (MLW_UNLIKELY(abstop - top12(0x1p-54) >= top12(512.0) - top12(0x1p-54))) {
		if (abstop - top12(0x1p-54) >= 0x80000000)
			/* Avoid spurious underflow for tiny x.  */
			/* Note: 0 is common input.  */
			return 1.0 + x;
		if (abstop >= top12(1024.0)) {
			if (core::mlwBitCast<uint64>(x) == core::mlwBitCast<uint64>(-core::NumericLimits<f64>::infinity))
				return 0.0;
			if (abstop >= top12(core::NumericLimits<f64>::infinity))
				return 1.0 + x;
			if (core::mlwBitCast<uint64>(x) >> 63)
				return __math_uflow(0);
			else
				return __math_oflow(0);
		}
		/* Large x is special cased below.  */
		abstop = 0;
	}

	/* exp(x) = 2^(k/N) * exp(r), with exp(r) in [2^(-1/2N),2^(1/2N)].  */
	/* x = ln2/N*k + r, with int k and r in [-ln2/2N, ln2/2N].  */
	z = __exp_data.invln2N * x;

	/* z - kd is in [-1, 1] in non-nearest rounding modes.  */
	kd = eval_as_double(z + __exp_data.shift);
	ki = core::mlwBitCast<uint64>(kd);
	kd -= __exp_data.shift;

	r = x + kd * __exp_data.negln2hiN + kd * __exp_data.negln2loN;
	/* 2^(k/N) ~= scale * (1 + tail).  */
	idx = 2 * (ki % N);
	top = ki << (52 - EXP_TABLE_BITS);
	tail = core::mlwBitCast<f64>(__exp_data.tab[idx]);
	/* This is only a valid scale when -1023*N < k < 1024*N.  */
	sbits = __exp_data.tab[idx + 1] + top;
	/* exp(x) = 2^(k/N) * exp(r) ~= scale + scale * (tail + exp(r) - 1).  */
	/* Evaluation is optimized assuming superscalar pipelined execution.  */
	r2 = r * r;

	constexpr f64 C2 = __exp_data.poly[0];
	constexpr f64 C3 = __exp_data.poly[1];
	constexpr f64 C4 = __exp_data.poly[2];
	constexpr f64 C5 = __exp_data.poly[3];
	/* Without fma the worst case error is 0.25/N ulp larger.  */
	/* Worst case error is less than 0.5+1.11/N+(abs poly error * 2^53) ulp.  */
	tmp = tail + r + r2 * (C2 + r * C3) + r2 * r2 * (C4 + r * C5);
	if (MLW_UNLIKELY(abstop == 0))
		return exp_specialcase(tmp, sbits, ki);
	scale = core::mlwBitCast<f64>(sbits);
	/* Note: tmp == 0 or |tmp| > 2^-200 and scale > 2^-739, so there
	   is no spurious underflow here even without fma.  */
	return eval_as_double(scale + scale * tmp);
}


/* Handle cases that may overflow or underflow when computing the result that
   is scale*(1+TMP) without intermediate rounding.  The bit representation of
   scale is in SBITS, however it has a computed exponent that may have
   overflown into the sign bit so that needs to be adjusted before using it as
   a double.  (int32_t)KI is the k used in the argument reduction and exponent
   adjustment of scale, positive k here means the result may overflow and
   negative k means the result may underflow.  */
static inline f64 exp2_specialcase(f64 tmp, uint64 sbits, uint64 ki)
{
	f64 scale, y;

	if ((ki & 0x80000000) == 0) {
		/* k > 0, the exponent of scale might have overflowed by 1.  */
		sbits -= 1ull << 52;
		scale = core::mlwBitCast<f64>(sbits);
		y = 2 * (scale + scale * tmp);
		return eval_as_double(y);
	}
	/* k < 0, need special care in the subnormal range.  */
	sbits += 1022ull << 52;
	scale = core::mlwBitCast<f64>(sbits);
	y = scale + scale * tmp;
	if (y < 1.0) {
		/* Round y to the right precision before scaling it into the subnormal
		   range to avoid double rounding that can cause 0.5+E/2 ulp error where
		   E is the worst-case ulp error outside the subnormal range.  So this
		   is only useful if the goal is better than 1 ulp worst-case error.  */
		f64 hi, lo;
		lo = scale - y + scale * tmp;
		hi = 1.0 + y;
		lo = 1.0 - hi + y + lo;
		y = eval_as_double(hi + lo) - 1.0;
		/* Avoid -0.0 with downward rounding.  */
		if (y == 0.0)
			y = 0.0;
		/* The underflow exception needs to be signaled explicitly.  */
		fp_force_eval(fp_barrier(0x1p-1022) * 0x1p-1022);
	}
	y = 0x1p-1022 * y;
	return eval_as_double(y);
}


f64 core::mlwExp2(f64 x)
{
	uint32 abstop;
	uint64 ki, idx, top, sbits;
	f64 kd, r, r2, scale, tail, tmp;

	abstop = top12(x) & 0x7ff;
	if (MLW_UNLIKELY(abstop - top12(0x1p-54) >= top12(512.0) - top12(0x1p-54))) {
		if (abstop - top12(0x1p-54) >= 0x80000000)
			/* Avoid spurious underflow for tiny x.  */
			/* Note: 0 is common input.  */
			return  1.0 + x;
		if (abstop >= top12(1024.0)) {
			if (core::mlwBitCast<uint64>(x) == core::mlwBitCast<uint64>(-core::NumericLimits<f64>::infinity))
				return 0.0;
			if (abstop >= top12(core::NumericLimits<f64>::infinity))
				return 1.0 + x;
			if (!(core::mlwBitCast<uint64>(x) >> 63))
				return __math_oflow(0);
			else if (core::mlwBitCast<uint64>(x) >= core::mlwBitCast<uint64>(-1075.0))
				return __math_uflow(0);
		}
		if (2 * core::mlwBitCast<uint64>(x) > 2 * core::mlwBitCast<uint64>(928.0))
			/* Large x is special cased below.  */
			abstop = 0;
	}

	/* exp2(x) = 2^(k/N) * 2^r, with 2^r in [2^(-1/2N),2^(1/2N)].  */
	/* x = k/N + r, with int k and r in [-1/2N, 1/2N].  */
	kd = eval_as_double(x + __exp_data.exp2_shift);
	ki = core::mlwBitCast<uint64>(kd); /* k.  */
	kd -= __exp_data.exp2_shift; /* k/N for int k.  */
	r = x - kd;
	/* 2^(k/N) ~= scale * (1 + tail).  */
	idx = 2 * (ki % N);
	top = ki << (52 - EXP_TABLE_BITS);
	tail = core::mlwBitCast<f64>(__exp_data.tab[idx]);
	/* This is only a valid scale when -1023*N < k < 1024*N.  */
	sbits = __exp_data.tab[idx + 1] + top;
	/* exp2(x) = 2^(k/N) * 2^r ~= scale + scale * (tail + 2^r - 1).  */
	/* Evaluation is optimized assuming superscalar pipelined execution.  */
	r2 = r * r;


	constexpr f64 C1 = __exp_data.exp2_poly[0];
	constexpr f64 C2 = __exp_data.exp2_poly[1];
	constexpr f64 C3 = __exp_data.exp2_poly[2];
	constexpr f64 C4 = __exp_data.exp2_poly[3];
	constexpr f64 C5 = __exp_data.exp2_poly[4];

	/* Without fma the worst case error is 0.5/N ulp larger.  */
	/* Worst case error is less than 0.5+0.86/N+(abs poly error * 2^53) ulp.  */
	tmp = tail + r * C1 + r2 * (C2 + r * C3) + r2 * r2 * (C4 + r * C5);
	if (MLW_UNLIKELY(abstop == 0))
		return exp2_specialcase(tmp, sbits, ki);
	scale = core::mlwBitCast<f64>(sbits);
	/* Note: tmp == 0 or |tmp| > 2^-65 and scale > 2^-928, so there
	   is no spurious underflow here even without fma.  */
	return eval_as_double(scale + scale * tmp);
}
