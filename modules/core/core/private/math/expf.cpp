// =============================================================================
//  Single-precision base-e and base-2 exponential - core::mlwExp(f32) / core::mlwExp2(f32)
//
//  From Arm optimized-routines, obtained via musl libc (math/expf.c, exp2f.c).
//  Adapted for Mallow (project types / conventions).
//
//  Copyright (c) 2017-2018, Arm Limited.
//  SPDX-License-Identifier: MIT
// =============================================================================


#include "libc/math.h"
#include "helpers.hpp"

#include "expf_data.inl"


/*
ULP error: 0.502 (nearest rounding.)
Relative error: 1.69 * 2^-34 in [-ln2/64, ln2/64] (before rounding.)
Wrong count: 170635 (all nearest rounding wrong results with fma.)
Non-nearest ULP error: 1 (rounded ULP error)
*/
f32 core::mlwExp(f32 x)
{
	constexpr const f64(&C)[EXP2F_POLY_ORDER] = __exp2f_data.poly_scaled;

	uint32 abstop;
	uint64 ki, t;
	f64 kd, xd, z, r, r2, y, s;

	xd = (f64)x;
	abstop = top12(x) & 0x7ff;
	if (MLW_UNLIKELY(abstop >= top12(88.0f))) {
		/* |x| >= 88 or x is nan.  */
		if (core::mlwBitCast<uint32>(x) == core::mlwBitCast<uint32>(-core::NumericLimits<f32>::infinity))
			return 0.0f;
		if (abstop >= top12(core::NumericLimits<f32>::infinity))
			return x + x;
		if (x > 0x1.62e42ep6f) /* x > log(0x1p128) ~= 88.72 */
			return __math_oflowf(0);
		if (x < -0x1.9fe368p6f) /* x < log(0x1p-150) ~= -103.97 */
			return __math_uflowf(0);
	}

	/* x*N/Ln2 = k + r with r in [-1/2, 1/2] and int k.  */
	z = __exp2f_data.invln2_scaled * xd;

	/* Round and convert z to int, the result is in [-150*N, 128*N] and
	   ideally ties-to-even rule is used, otherwise the magnitude of r
	   can be bigger which gives larger approximation error.  */

	kd = eval_as_double(z + __exp2f_data.shift);
	ki = core::mlwBitCast<uint64>(kd);
	kd -= __exp2f_data.shift;

	r = z - kd;

	/* exp(x) = 2^(k/N) * 2^(r/N) ~= s * (C0*r^3 + C1*r^2 + C2*r + 1) */
	t = __exp2f_data.tab[ki % N];
	t += ki << (52 - EXP2F_TABLE_BITS);
	s = core::mlwBitCast<f64>(t);
	z = C[0] * r + C[1];
	r2 = r * r;
	y = C[2] * r + 1;
	y = z * r2 + y;
	y = y * s;
	return eval_as_float(static_cast<f32>(y));
}

/*
ULP error: 0.502 (nearest rounding.)
Relative error: 1.69 * 2^-34 in [-1/64, 1/64] (before rounding.)
Wrong count: 168353 (all nearest rounding wrong results with fma.)
Non-nearest ULP error: 1 (rounded ULP error)
*/
f32 core::mlwExp2(f32 x)
{

	constexpr const f64(&C)[EXP2F_POLY_ORDER] = __exp2f_data.poly;
	uint32 abstop;
	uint64 ki, t;
	f64 kd, xd, z, r, r2, y, s;

	xd = (f64)x;
	abstop = top12(x) & 0x7ff;
	if (MLW_UNLIKELY(abstop >= top12(128.0f))) {
		/* |x| >= 128 or x is nan.  */
		if (core::mlwBitCast<uint32>(x) == core::mlwBitCast<uint32>(-core::NumericLimits<f32>::infinity))
			return 0.0f;
		if (abstop >= top12(core::NumericLimits<f32>::infinity))
			return x + x;
		if (x > 0.0f)
			return __math_oflowf(0);
		if (x <= -150.0f)
			return __math_uflowf(0);
	}

	/* x = k/N + r with r in [-1/(2N), 1/(2N)] and int k.  */
	kd = eval_as_double(xd + __exp2f_data.shift_scaled);
	ki = core::mlwBitCast<uint64>(kd);
	kd -= __exp2f_data.shift_scaled; /* k/N for int k.  */
	r = xd - kd;

	/* exp2(x) = 2^(k/N) * 2^r ~= s * (C0*r^3 + C1*r^2 + C2*r + 1) */
	t = __exp2f_data.tab[ki % N];
	t += ki << (52 - EXP2F_TABLE_BITS);
	s = core::mlwBitCast<f64>(t);
	z = C[0] * r + C[1];
	r2 = r * r;
	y = C[2] * r + 1;
	y = z * r2 + y;
	y = y * s;
	return eval_as_float(static_cast<f32>(y));
}