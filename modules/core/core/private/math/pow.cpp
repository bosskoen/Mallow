// =============================================================================
//  Double-precision power - core::mlwPow(f64)
//
//  From Arm optimized-routines, obtained via musl libc (math/pow.c).
//  Adapted for Mallow (project types / conventions).
//
//  Copyright (c) 2018, Arm Limited.
//  SPDX-License-Identifier: MIT
// =============================================================================



#include "libc/math.h"
#include "helpers.hpp"

#include "exp_data.inl"
#include "pow_data.inl"




/* Handle cases that may overflow or underflow when computing the result that
   is scale*(1+TMP) without intermediate rounding.  The bit representation of
   scale is in SBITS, however it has a computed exponent that may have
   overflown into the sign bit so that needs to be adjusted before using it as
   a double.  (int32_t)KI is the k used in the argument reduction and exponent
   adjustment of scale, positive k here means the result may overflow and
   negative k means the result may underflow.  */
static inline f64 pow_specialcase(f64 tmp, uint64 sbits, uint64 ki)
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
	/* Note: sbits is signed scale.  */
	scale = core::mlwBitCast<f64>(sbits);
	y = scale + scale * tmp;
	if (core::mlwAbs(y) < 1.0) {
		/* Round y to the right precision before scaling it into the subnormal
		   range to avoid double rounding that can cause 0.5+E/2 ulp error where
		   E is the worst-case ulp error outside the subnormal range.  So this
		   is only useful if the goal is better than 1 ulp worst-case error.  */
		f64 hi, lo, one = 1.0;
		if (y < 0.0)
			one = -1.0;
		lo = scale - y + scale * tmp;
		hi = one + y;
		lo = one - hi + y + lo;
		y = eval_as_double(hi + lo) - one;
		/* Fix the sign of 0.  */
		if (y == 0.0)
			y = core::mlwBitCast<f64>(sbits & 0x8000000000000000);
		/* The underflow exception needs to be signaled explicitly.  */
		fp_force_eval(fp_barrier(0x1p-1022) * 0x1p-1022);
	}
	y = 0x1p-1022 * y;
	return eval_as_double(y);
}

/* Compute y+TAIL = log(x) where the rounded result is y and TAIL has about
   additional 15 bits precision.  IX is the bit representation of x, but
   normalized in the subnormal range using the sign bit for the exponent.  */
static inline f64 log_inline(uint64 ix, f64* tail)
{
	constexpr const pow_log_data::table_data(&T)[1 << POW_LOG_TABLE_BITS] = __pow_log_data.tab;
	constexpr const f64(&A)[POW_LOG_POLY_ORDER - 1] = __pow_log_data.poly;
	/* double_t for better performance on targets with FLT_EVAL_METHOD==2.  */
	f64 z, r, y, invc, logc, logctail, kd, hi, t1, t2, lo, lo1, lo2, p;
	uint64 iz, tmp;
	int k, i;

	/* x = 2^k z; where z is in range [OFF,2*OFF) and exact.
	   The range is split into N subintervals.
	   The ith subinterval contains z and c is near its center.  */
	tmp = ix - 0x3fe6955500000000;
	i = (tmp >> (52 - POW_LOG_TABLE_BITS)) % NPD;
	k = (int64)tmp >> 52; /* arithmetic shift */
	iz = ix - (tmp & 0xfffULL << 52);
	z = core::mlwBitCast<f64>(iz);
	kd = (f64)k;

	/* log(x) = k*Ln2 + log(c) + log1p(z/c-1).  */
	invc = T[i].invc;
	logc = T[i].logc;
	logctail = T[i].logctail;

	/* Note: 1/c is j/N or j/N/2 where j is an integer in [N,2N) and
	 |z/c - 1| < 1/N, so r = z/c - 1 is exactly representible.  */
#if MLW_HAS_FAST_FMA
	r = core::mlwFma(z, invc, -1.0);
#else
	 /* Split z such that rhi, rlo and rhi*rhi are exact and |rlo| <= |r|.  */
	f64 zhi = core::mlwBitCast<f64>((iz + (1ULL << 31)) & (-1ULL << 32));
	f64 zlo = z - zhi;
	f64 rhi = zhi * invc - 1.0;
	f64 rlo = zlo * invc;
	r = rhi + rlo;
#endif

	/* k*Ln2 + log(c) + r.  */
	t1 = kd * __pow_log_data.ln2hi + logc;
	t2 = t1 + r;
	lo1 = kd * __pow_log_data.ln2lo + logctail;
	lo2 = t1 - t2 + r;

	/* Evaluation is optimized assuming superscalar pipelined execution.  */
	f64 ar, ar2, ar3, lo3, lo4;
	ar = A[0] * r; /* A[0] = -0.5.  */
	ar2 = r * ar;
	ar3 = r * ar2;
	/* k*Ln2 + log(c) + r + A[0]*r*r.  */
#if MLW_HAS_FAST_FMA
	hi = t2 + ar2;
	lo3 = core::mlwFma(ar, r, -ar2);
	lo4 = t2 - hi + ar2;
#else
	f64 arhi = A[0] * rhi;
	f64 arhi2 = rhi * arhi;
	hi = t2 + arhi2;
	lo3 = rlo * (ar + arhi);
	lo4 = t2 - hi + arhi2;
#endif
	/* p = log1p(r) - r - A[0]*r*r.  */
	p = (ar3 * (A[1] + r * A[2] +
		ar2 * (A[3] + r * A[4] + ar2 * (A[5] + r * A[6]))));
	lo = lo1 + lo2 + lo3 + lo4 + p;
	y = hi + lo;
	*tail = hi - y + lo;
	return y;
}

/* Computes sign*exp(x+xtail) where |xtail| < 2^-8/N and |xtail| <= |x|.
   The sign_bias argument is SIGN_BIAS or 0 and sets the sign to -1 or 1.  */
static inline f64 exp_inline(f64 x, f64 xtail, uint32 sign_bias)
{
	uint32 abstop;
	uint64 ki, idx, top, sbits;
	/* double_t for better performance on targets with FLT_EVAL_METHOD==2.  */
	f64 kd, z, r, r2, scale, tail, tmp;

	abstop = top12(x) & 0x7ff;
	if (MLW_UNLIKELY(abstop - top12(0x1p-54) >=
		top12(512.0) - top12(0x1p-54))) {
		if (abstop - top12(0x1p-54) >= 0x80000000) {
			/* Avoid spurious underflow for tiny x.  */
			/* Note: 0 is common input.  */
			f64 one = 1.0 + x;
			return sign_bias ? -one : one;
		}
		if (abstop >= top12(1024.0)) {
			/* Note: inf and nan are already handled.  */
			if (core::mlwBitCast<uint64>(x) >> 63)
				return __math_uflow(sign_bias);
			else
				return __math_oflow(sign_bias);
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
	/* The code assumes 2^-200 < |xtail| < 2^-8/N.  */
	r += xtail;
	/* 2^(k/N) ~= scale * (1 + tail).  */
	idx = 2 * (ki % N);
	top = (ki + sign_bias) << (52 - EXP_TABLE_BITS);
	tail = core::mlwBitCast<f64>(__exp_data.tab[idx]);
	/* This is only a valid scale when -1023*N < k < 1024*N.  */
	sbits = __exp_data.tab[idx + 1] + top;
	/* exp(x) = 2^(k/N) * exp(r) ~= scale + scale * (tail + exp(r) - 1).  */
	/* Evaluation is optimized assuming superscalar pipelined execution.  */
	r2 = r * r;


	constexpr f64 C2 = __exp_data.poly[5 - EXP_POLY_ORDER];
	constexpr f64 C3 = __exp_data.poly[6 - EXP_POLY_ORDER];
	constexpr f64 C4 = __exp_data.poly[7 - EXP_POLY_ORDER];
	constexpr f64 C5 = __exp_data.poly[8 - EXP_POLY_ORDER];

	/* Without fma the worst case error is 0.25/N ulp larger.  */
	/* Worst case error is less than 0.5+1.11/N+(abs poly error * 2^53) ulp.  */
	tmp = tail + r + r2 * (C2 + r * C3) + r2 * r2 * (C4 + r * C5);
	if (MLW_UNLIKELY(abstop == 0))
		return pow_specialcase(tmp, sbits, ki);
	scale = core::mlwBitCast<f64>(sbits);
	/* Note: tmp == 0 or |tmp| > 2^-200 and scale > 2^-739, so there
	   is no spurious underflow here even without fma.  */
	return eval_as_double(scale + scale * tmp);
}



static inline int checkint(uint64 iy)
{
	int e = iy >> 52 & 0x7ff;
	if (e < 0x3ff)
		return 0;
	if (e > 0x3ff + 52)
		return 2;
	if (iy & ((1ULL << (0x3ff + 52 - e)) - 1))
		return 0;
	if (iy & (1ULL << (0x3ff + 52 - e)))
		return 1;
	return 2;
}



/* Returns 1 if input is the bit representation of 0, infinity or nan.  */
static inline int zeroinfnan(uint64 i)
{
	return 2 * i - 1 >= 2 * core::mlwBitCast<uint64>(core::NumericLimits<f64>::infinity) - 1;
}

f64 core::mlwPow(f64 x, f64 y)
{
	uint32 sign_bias = 0;
	uint64 ix, iy;
	uint32 topx, topy;

	ix = core::mlwBitCast<uint64>(x);
	iy = core::mlwBitCast<uint64>(y);
	topx = top12(x);
	topy = top12(y);
	if (MLW_UNLIKELY(topx - 0x001 >= 0x7ff - 0x001 ||
		(topy & 0x7ff) - 0x3be >= 0x43e - 0x3be)) {
		/* Note: if |y| > 1075 * ln2 * 2^53 ~= 0x1.749p62 then pow(x,y) = inf/0
		   and if |y| < 2^-54 / 1075 ~= 0x1.e7b6p-65 then pow(x,y) = +-1.  */
		   /* Special cases: (x < 0x1p-126 or inf or nan) or
			  (|y| < 0x1p-65 or |y| >= 0x1p63 or nan).  */
		if (MLW_UNLIKELY(zeroinfnan(iy))) {
			if (2 * iy == 0)
				return 1.0;
			if (ix == core::mlwBitCast<uint64>(1.0))
				return 1.0;
			if (2 * ix > 2 * core::mlwBitCast<uint64>(core::NumericLimits<f64>::infinity) ||
				2 * iy > 2 * core::mlwBitCast<uint64>(core::NumericLimits<f64>::infinity))
				return x + y;
			if (2 * ix == 2 * core::mlwBitCast<uint64>(1.0))
				return 1.0;
			if ((2 * ix < 2 * core::mlwBitCast<uint64>(1.0)) == !(iy >> 63))
				return 0.0; /* |x|<1 && y==inf or |x|>1 && y==-inf.  */
			return y * y;
		}
		if (MLW_UNLIKELY(zeroinfnan(ix))) {
			f64 x2 = x * x;
			if (ix >> 63 && checkint(iy) == 1)
				x2 = -x2;
			/* Without the barrier some versions of clang hoist the 1/x2 and
			   thus division by zero exception can be signaled spuriously.  */
			return iy >> 63 ? fp_barrier(1 / x2) : x2;
		}
		/* Here x and y are non-zero finite.  */
		if (ix >> 63) {
			/* Finite x < 0.  */
			int yint = checkint(iy);
			if (yint == 0)
				return __math_invalid(x);
			if (yint == 1)
				sign_bias = (0x800 << EXP_TABLE_BITS);
			ix &= 0x7fffffffffffffff;
			topx &= 0x7ff;
		}
		if ((topy & 0x7ff) - 0x3be >= 0x43e - 0x3be) {
			/* Note: sign_bias == 0 here because y is not odd.  */
			if (ix == core::mlwBitCast<uint64>(1.0))
				return 1.0;
			if ((topy & 0x7ff) < 0x3be) {
				/* |y| < 2^-65, x^y ~= 1 + y*log(x).  */

				return ix > core::mlwBitCast<uint64>(1.0) ? 1.0 + y :
					1.0 - y;
			}
			return (ix > core::mlwBitCast<uint64>(1.0)) == (topy < 0x800) ?
				__math_oflow(0) :
				__math_uflow(0);
		}
		if (topx == 0) {
			/* Normalize subnormal x so exponent becomes negative.  */
			ix = core::mlwBitCast<uint64>(x * 0x1p52);
			ix &= 0x7fffffffffffffff;
			ix -= 52ULL << 52;
		}
	}

	f64 lo;
	f64 hi = log_inline(ix, &lo);
	f64 ehi, elo;
#if MLW_HAS_FAST_FMA
	ehi = y * hi;
	elo = y * lo + core::mlwFma(y, hi, -ehi);
#else
	f64 yhi = core::mlwBitCast<f64>(iy & -1ULL << 27);
	f64 ylo = y - yhi;
	f64 lhi = core::mlwBitCast<f64>(core::mlwBitCast<uint64>(hi) & -1ULL << 27);
	f64 llo = hi - lhi + lo;
	ehi = yhi * lhi;
	elo = ylo * lhi + y * llo; /* |elo| < |ehi| * 2^-25.  */
#endif
	return exp_inline(ehi, elo, sign_bias);
}