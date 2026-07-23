#include "libc/math.h"
#include "helpers.hpp"

// =============================================================================
//  Base-10 logarithm — core::mlwLog10(f64) / core::mlwLog10(f32)
//
//  Derived from FreeBSD libm (fdlibm), obtained via musl libc:
//    f64  from  /usr/src/lib/msun/src/e_log10.c   (Developed at SunSoft)
//    f32  from  /usr/src/lib/msun/src/e_log10f.c  (Developed at SunPro)
//  Adapted for Mallow (project types / conventions).
//
//  ====================================================
//  Copyright (C) 1993 by Sun Microsystems, Inc. All rights reserved.
//
//  Developed at SunSoft / SunPro, a Sun Microsystems, Inc. business.
//  Permission to use, copy, modify, and distribute this
//  software is freely granted, provided that this notice
//  is preserved.
//  ====================================================
// =============================================================================


//----------------------------------------------------------
// f64
//----------------------------------------------------------

  f64 core::mlwLog10(f64 x)
{

	static constexpr f64
		ivln10hi = 4.34294481878168880939e-01, /* 0x3fdbcb7b, 0x15200000 */
		ivln10lo = 2.50829467116452752298e-11, /* 0x3dbb9438, 0xca9aadd5 */
		log10_2hi = 3.01029995663611771306e-01, /* 0x3FD34413, 0x509F6000 */
		log10_2lo = 3.69423907715893078616e-13, /* 0x3D59FEF3, 0x11F12B36 */
		Lg1 = 6.666666666666735130e-01,  /* 3FE55555 55555593 */
		Lg2 = 3.999999999940941908e-01,  /* 3FD99999 9997FA04 */
		Lg3 = 2.857142874366239149e-01,  /* 3FD24924 94229359 */
		Lg4 = 2.222219843214978396e-01,  /* 3FCC71C5 1D8E78AF */
		Lg5 = 1.818357216161805012e-01,  /* 3FC74664 96CB03DE */
		Lg6 = 1.531383769920937332e-01,  /* 3FC39A09 D078C69F */
		Lg7 = 1.479819860511658591e-01;  /* 3FC2F112 DF3E5244 */

	union { f64 f; uint64 i; } u = { x };
	f64 hfsq, f, s, z, R, w, t1, t2, dk, y, hi, lo, val_hi, val_lo;
	uint32 hx;
	int k;

	hx = u.i >> 32;
	k = 0;
	if (hx < 0x00100000 || hx >> 31) {
		if (u.i << 1 == 0)
			return -1 / (x * x);  /* log(+-0)=-inf */
		if (hx >> 31)
			return __math_invalid(x); /* log(-#) = NaN */
		/* subnormal number, scale x up */
		k -= 54;
		x *= 0x1p54;
		u.f = x;
		hx = u.i >> 32;
	}
	else if (hx >= 0x7ff00000) {
		return x;
	}
	else if (hx == 0x3ff00000 && u.i << 32 == 0)
		return 0;

	/* reduce x into [sqrt(2)/2, sqrt(2)] */
	hx += 0x3ff00000 - 0x3fe6a09e;
	k += (int)(hx >> 20) - 0x3ff;
	hx = (hx & 0x000fffff) + 0x3fe6a09e;
	u.i = (uint64)hx << 32 | (u.i & 0xffffffff);
	x = u.f;

	f = x - 1.0;
	hfsq = 0.5 * f * f;
	s = f / (2.0 + f);
	z = s * s;
	w = z * z;
	t1 = w * (Lg2 + w * (Lg4 + w * Lg6));
	t2 = z * (Lg1 + w * (Lg3 + w * (Lg5 + w * Lg7)));
	R = t2 + t1;

	/* See log2.c for details. */
	/* hi+lo = f - hfsq + s*(hfsq+R) ~ log(1+f) */
	hi = f - hfsq;
	u.f = hi;
	u.i &= (uint64)-1 << 32;
	hi = u.f;
	lo = f - hi - hfsq + s * (hfsq + R);

	/* val_hi+val_lo ~ log10(1+f) + k*log10(2) */
	val_hi = hi * ivln10hi;
	dk = k;
	y = dk * log10_2hi;
	val_lo = dk * log10_2lo + (lo + hi) * ivln10lo + lo * ivln10hi;

	/*
	 * Extra precision in for adding y is not strictly needed
	 * since there is no very large cancellation near x = sqrt(2) or
	 * x = 1/sqrt(2), but we do it anyway since it costs little on CPUs
	 * with some parallelism and it reduces the error for many args.
	 */
	w = y + val_hi;
	val_lo += (y - w) + val_hi;
	val_hi = w;

	return val_lo + val_hi;
}


//----------------------------------------------------------
// f32
//----------------------------------------------------------

f32 core::mlwLog10(f32 x)
{

	static constexpr f32
		ivln10hi = 4.3432617188e-01f, /* 0x3ede6000 */
		ivln10lo = -3.1689971365e-05f, /* 0xb804ead9 */
		log10_2hi = 3.0102920532e-01f, /* 0x3e9a2080 */
		log10_2lo = 7.9034151668e-07f, /* 0x355427db */
		/* |(log(1+s)-log(1-s))/s - Lg(s)| < 2**-34.24 (~[-4.95e-11, 4.97e-11]). */
		Lg1 = 0xaaaaaa.0p-24, /* 0.66666662693 */
		Lg2 = 0xccce13.0p-25, /* 0.40000972152 */
		Lg3 = 0x91e9ee.0p-25, /* 0.28498786688 */
		Lg4 = 0xf89e26.0p-26; /* 0.24279078841 */

	union { f32 f; uint32 i; } u = { x };
	f32 hfsq, f, s, z, R, w, t1, t2, dk, hi, lo;
	uint32 ix;
	int k;

	ix = u.i;
	k = 0;
	if (ix < 0x00800000 || ix >> 31) {  /* x < 2**-126  */
		if (ix << 1 == 0)
			return -1 / (x * x);  /* log(+-0)=-inf */
		if (ix >> 31)
			return __math_invalid(x); /* log(-#) = NaN */
		/* subnormal number, scale up x */
		k -= 25;
		x *= 0x1p25f;
		u.f = x;
		ix = u.i;
	}
	else if (ix >= 0x7f800000) {
		return x;
	}
	else if (ix == 0x3f800000)
		return 0;

	/* reduce x into [sqrt(2)/2, sqrt(2)] */
	ix += 0x3f800000 - 0x3f3504f3;
	k += (int)(ix >> 23) - 0x7f;
	ix = (ix & 0x007fffff) + 0x3f3504f3;
	u.i = ix;
	x = u.f;

	f = x - 1.0f;
	s = f / (2.0f + f);
	z = s * s;
	w = z * z;
	t1 = w * (Lg2 + w * Lg4);
	t2 = z * (Lg1 + w * Lg3);
	R = t2 + t1;
	hfsq = 0.5f * f * f;

	hi = f - hfsq;
	u.f = hi;
	u.i &= 0xfffff000;
	hi = u.f;
	lo = f - hi - hfsq + s * (hfsq + R);
	dk = static_cast<f32>(k);
	return dk * log10_2lo + (lo + hi) * ivln10lo + lo * ivln10hi + hi * ivln10hi + dk * log10_2hi;
}
