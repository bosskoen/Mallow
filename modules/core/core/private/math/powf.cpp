// =============================================================================
//  Single-precision power - core::mlwPow(f32)
//
//  From Arm optimized-routines, obtained via musl libc (math/powf.c).
//  Adapted for Mallow (project types / conventions).
//
//  Copyright (c) 2017-2018, Arm Limited.
//  SPDX-License-Identifier: MIT
// =============================================================================


#include "libc/math.h"
#include "helpers.hpp"

#include "powf_data.inl"
#include "expf_data.inl"


/* Returns 0 if not int, 1 if odd int, 2 if even int.  The argument is
   the bit representation of a non-zero finite floating-point value.  */
static inline int checkint(uint32 iy)
{
	int e = iy >> 23 & 0xff;
	if (e < 0x7f)
		return 0;
	if (e > 0x7f + 23)
		return 2;
	if (iy & ((1 << (0x7f + 23 - e)) - 1))
		return 0;
	if (iy & (1 << (0x7f + 23 - e)))
		return 1;
	return 2;
}

/* Subnormal input is normalized so ix has negative biased exponent.
   Output is multiplied by N (POWF_SCALE) if TOINT_INTRINICS is set.  */
static inline f64 log2_inline(uint32 ix)
{
	constexpr const f64(&A)[POWF_LOG2_POLY_ORDER] = __powf_log2_data.poly;
	f64 z, r, r2, r4, p, q, y, y0, invc, logc;
	uint32 iz, top, tmp;
	int k, i;

	/* x = 2^k z; where z is in range [OFF,2*OFF] and exact.
	   The range is split into N subintervals.
	   The ith subinterval contains z and c is near its center.  */
	tmp = ix - 0x3f330000;
	i = (tmp >> (23 - POWF_LOG2_TABLE_BITS)) % NPF;
	top = tmp & 0xff800000;
	iz = ix - top;
	k = (int32)top >> (23); /* arithmetic shift */
	invc = __powf_log2_data.tab[i].invc;
	logc = __powf_log2_data.tab[i].logc;
	z = (double)core::mlwBitCast<f32>(iz);

	/* log2(x) = log1p(z/c-1)/ln2 + log2(c) + k */
	r = z * invc - 1;
	y0 = logc + (f64)k;

	/* Pipelined polynomial evaluation to approximate log1p(r)/ln2.  */
	r2 = r * r;
	y = A[0] * r + A[1];
	p = A[2] * r + A[3];
	r4 = r2 * r2;
	q = A[4] * r + y0;
	q = p * r2 + q;
	y = y * r4 + q;
	return y;
}

/* The output of log2 and thus the input of exp2 is either scaled by N
   (in case of fast toint intrinsics) or not.  The unscaled xd must be
   in [-1021,1023], sign_bias sets the sign of the result.  */
static inline f32 exp2_inline(f64 xd, uint32 sign_bias)
{
	constexpr const f64(&C)[EXP2F_POLY_ORDER] = __exp2f_data.poly;
	uint64 ki, ski, t;
	f64 kd, z, r, r2, y, s;

	/* x = k/N + r with r in [-1/(2N), 1/(2N)] */
	kd = eval_as_double(xd + __exp2f_data.shift_scaled);
	ki = core::mlwBitCast<uint64>(kd);
	kd -= __exp2f_data.shift_scaled; /* k/N */

	r = xd - kd;

	/* exp2(x) = 2^(k/N) * 2^r ~= s * (C0*r^3 + C1*r^2 + C2*r + 1) */
	t = __exp2f_data.tab[ki % N];
	ski = ki + sign_bias;
	t += ski << (52 - EXP2F_TABLE_BITS);
	s = core::mlwBitCast<f64>(t);
	z = C[0] * r + C[1];
	r2 = r * r;
	y = C[2] * r + 1;
	y = z * r2 + y;
	y = y * s;
	return eval_as_float(static_cast<f32>(y));
}

static inline int zeroinfnan(uint32 ix)
{
	return 2 * ix - 1 >= 2u * 0x7f800000 - 1;
}


/*
ULP error: 0.82 (~ 0.5 + relerr*2^24)
relerr: 1.27 * 2^-26 (Relative error ~= 128*Ln2*relerr_log2 + relerr_exp2)
relerr_log2: 1.83 * 2^-33 (Relative error of logx.)
relerr_exp2: 1.69 * 2^-34 (Relative error of exp2(ylogx).)
*/
f32 core::mlwPow(f32 x, f32 y)
{
	uint32 sign_bias = 0;
	uint32 ix, iy;

	ix = core::mlwBitCast<uint32>(x);
	iy = core::mlwBitCast < uint32>(y);
	if (MLW_UNLIKELY(ix - 0x00800000 >= 0x7f800000 - 0x00800000 ||
		zeroinfnan(iy))) {
		/* Either (x < 0x1p-126 or inf or nan) or (y is 0 or inf or nan).  */
		if (MLW_UNLIKELY(zeroinfnan(iy))) {
			if (2 * iy == 0)
				return  1.0f;
			if (ix == 0x3f800000)
				return 1.0f;
			if (2 * ix > 2u * 0x7f800000 ||
				2 * iy > 2u * 0x7f800000)
				return x + y;
			if (2 * ix == 2 * 0x3f800000)
				return 1.0f;
			if ((2 * ix < 2 * 0x3f800000) == !(iy & 0x80000000))
				return 0.0f; /* |x|<1 && y==inf or |x|>1 && y==-inf.  */
			return y * y;
		}
		if (MLW_UNLIKELY(zeroinfnan(ix))) {
			f32 x2 = x * x;
			if (ix & 0x80000000 && checkint(iy) == 1)
				x2 = -x2;
			/* Without the barrier some versions of clang hoist the 1/x2 and
			   thus division by zero exception can be signaled spuriously.  */
			return iy & 0x80000000 ? fp_barrierf(1 / x2) : x2;
		}
		/* x and y are non-zero finite.  */
		if (ix & 0x80000000) {
			/* Finite x < 0.  */
			int yint = checkint(iy);
			if (yint == 0)
				return __math_invalid(x);
			if (yint == 1)
				sign_bias = (1 << (EXP2F_TABLE_BITS + 11));
			ix &= 0x7fffffff;
		}
		if (ix < 0x00800000) {
			/* Normalize subnormal x so exponent becomes negative.  */
			ix = core::mlwBitCast<uint32>(x * 0x1p23f);
			ix &= 0x7fffffff;
			ix -= 23 << 23;
		}
	}
	f64 logx = log2_inline(ix);
	f64 ylogx = y * logx; /* cannot overflow, y is single prec.  */
	if (MLW_UNLIKELY((core::mlwBitCast<uint64>(ylogx) >> 47 & 0xffff) >=
		core::mlwBitCast<uint64>(126.0) >> 47)) {
		/* |y*log(x)| >= 126.  */
		if (ylogx > 0x1.fffffffd1d571p+6)
			return __math_oflowf(sign_bias);
		if (ylogx <= -150.0)
			return __math_uflowf(sign_bias);
	}
	return exp2_inline(ylogx, sign_bias);
}