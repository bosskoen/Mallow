#include "libc/math.h"
#include "helpers.hpp"

// =============================================================================
//  Single-precision base-e logarithm — core::mlwLog(f32)
//
//  Derived from Arm optimized-routines, obtained via musl libc:
//    kernel  from  math/logf.c
//    table   from  math/logf_data.c
//  Adapted for Mallow (project types / conventions).
//
//  Copyright (c) 2017-2018, Arm Limited.
//  SPDX-License-Identifier: MIT
// =============================================================================

static constexpr int LOGF_TABLE_BITS = 4;
static constexpr int LOGF_POLY_ORDER = 4;

static constexpr int NF = 1 << LOGF_TABLE_BITS;

static constexpr struct logf_data {
	struct {
		f64 invc, logc;
	} tab[1 << LOGF_TABLE_BITS];
	f64 ln2;
	f64 poly[LOGF_POLY_ORDER - 1]; /* First order coefficient is 1.  */
} __logf_data = {
  .tab = {
  { 0x1.661ec79f8f3bep+0, -0x1.57bf7808caadep-2 },
  { 0x1.571ed4aaf883dp+0, -0x1.2bef0a7c06ddbp-2 },
  { 0x1.49539f0f010bp+0, -0x1.01eae7f513a67p-2 },
  { 0x1.3c995b0b80385p+0, -0x1.b31d8a68224e9p-3 },
  { 0x1.30d190c8864a5p+0, -0x1.6574f0ac07758p-3 },
  { 0x1.25e227b0b8eap+0, -0x1.1aa2bc79c81p-3 },
  { 0x1.1bb4a4a1a343fp+0, -0x1.a4e76ce8c0e5ep-4 },
  { 0x1.12358f08ae5bap+0, -0x1.1973c5a611cccp-4 },
  { 0x1.0953f419900a7p+0, -0x1.252f438e10c1ep-5 },
  { 0x1p+0, 0x0p+0 },
  { 0x1.e608cfd9a47acp-1, 0x1.aa5aa5df25984p-5 },
  { 0x1.ca4b31f026aap-1, 0x1.c5e53aa362eb4p-4 },
  { 0x1.b2036576afce6p-1, 0x1.526e57720db08p-3 },
  { 0x1.9c2d163a1aa2dp-1, 0x1.bc2860d22477p-3 },
  { 0x1.886e6037841edp-1, 0x1.1058bc8a07ee1p-2 },
  { 0x1.767dcf5534862p-1, 0x1.4043057b6ee09p-2 },
  },
  .ln2 = 0x1.62e42fefa39efp-1,
  .poly = {
  -0x1.00ea348b88334p-2, 0x1.5575b0be00b6ap-2, -0x1.ffffef20a4123p-2,
  }
};

/*
ULP error: 0.818 (nearest rounding.)
Relative error: 1.957 * 2^-26 (before rounding.)
*/

f32 core::mlwLog(f32 x)
{
	f64 z, r, r2, y, y0, invc, logc;
	uint32 ix, iz, tmp;
	int k, i;

	ix = mlwBitCast<uint32>(x);
	/* Fix sign of zero with downward rounding when x==1.  */
	if (MLW_UNLIKELY(ix == 0x3f800000))
		return 0;
	if (MLW_UNLIKELY(ix - 0x00800000 >= 0x7f800000 - 0x00800000)) {
		/* x < 0x1p-126 or inf or nan.  */
		if (ix * 2 == 0)
			return __math_divzerof(1);
		if (ix == 0x7f800000) /* log(inf) == inf.  */
			return x;
		if ((ix & 0x80000000) || ix * 2 >= 0xff000000)
			return __math_invalid(x);
		/* x is subnormal, normalize it.  */
		ix = mlwBitCast<uint32>(x * 0x1p23f);
		ix -= 23 << 23;
	}

	/* x = 2^k z; where z is in range [0x3f330000,2*0x3f330000] and exact.
	   The range is split into N subintervals.
	   The ith subinterval contains z and c is near its center.  */
	tmp = ix - 0x3f330000;
	i = (tmp >> (23 - LOGF_TABLE_BITS)) % NF;
	k = (int32)tmp >> 23; /* arithmetic shift */
	iz = ix - (tmp & 0xff800000);
	invc = __logf_data.tab[i].invc;
	logc = __logf_data.tab[i].logc;
	z = (f64)mlwBitCast<f32>(iz);

	/* log(x) = log1p(z/c-1) + log(c) + k*Ln2 */
	r = z * invc - 1;
	y0 = logc + (f64)k * __logf_data.ln2;

	/* Pipelined polynomial evaluation to approximate log1p(r).  */
	r2 = r * r;
	y = __logf_data.poly[1] * r + __logf_data.poly[2];
	y = __logf_data.poly[0] * r2 + y;
	y = y * r2 + (y0 + r);
	return eval_as_float(static_cast<f32>(y));
}
