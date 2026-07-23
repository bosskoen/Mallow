// =============================================================================
//  Single-precision base-2 logarithm — core::mlwLog2(f32)
//
//  Derived from Arm optimized-routines, obtained via musl libc:
//    kernel  from  math/log2f.c
//    table   from  math/log2f_data.c
//  Adapted for Mallow (project types / conventions).
//
//  Copyright (c) 2017-2018, Arm Limited.
//  SPDX-License-Identifier: MIT
// =============================================================================

#include "libc/math.h"
#include "helpers.hpp"



static constexpr int LOG2F_TABLE_BITS = 4;
static constexpr int LOG2F_POLY_ORDER = 4;
static constexpr struct log2f_data {
	struct {
		f64 invc, logc;
	} tab[1 << LOG2F_TABLE_BITS];
	f64 poly[LOG2F_POLY_ORDER];
} __log2f_data = {
  .tab = {
  { 0x1.661ec79f8f3bep+0, -0x1.efec65b963019p-2 },
  { 0x1.571ed4aaf883dp+0, -0x1.b0b6832d4fca4p-2 },
  { 0x1.49539f0f010bp+0, -0x1.7418b0a1fb77bp-2 },
  { 0x1.3c995b0b80385p+0, -0x1.39de91a6dcf7bp-2 },
  { 0x1.30d190c8864a5p+0, -0x1.01d9bf3f2b631p-2 },
  { 0x1.25e227b0b8eap+0, -0x1.97c1d1b3b7afp-3 },
  { 0x1.1bb4a4a1a343fp+0, -0x1.2f9e393af3c9fp-3 },
  { 0x1.12358f08ae5bap+0, -0x1.960cbbf788d5cp-4 },
  { 0x1.0953f419900a7p+0, -0x1.a6f9db6475fcep-5 },
  { 0x1p+0, 0x0p+0 },
  { 0x1.e608cfd9a47acp-1, 0x1.338ca9f24f53dp-4 },
  { 0x1.ca4b31f026aap-1, 0x1.476a9543891bap-3 },
  { 0x1.b2036576afce6p-1, 0x1.e840b4ac4e4d2p-3 },
  { 0x1.9c2d163a1aa2dp-1, 0x1.40645f0c6651cp-2 },
  { 0x1.886e6037841edp-1, 0x1.88e9c2c1b9ff8p-2 },
  { 0x1.767dcf5534862p-1, 0x1.ce0a44eb17bccp-2 },
  },
  .poly = {
  -0x1.712b6f70a7e4dp-2, 0x1.ecabf496832ep-2, -0x1.715479ffae3dep-1,
  0x1.715475f35c8b8p0,
  }
};

/*
ULP error: 0.752 (nearest rounding.)
Relative error: 1.9 * 2^-26 (before rounding.)
*/
static constexpr int N2F = (1 << LOG2F_TABLE_BITS);

f32 core::mlwLog2(f32 x)
{
	f64 z, r, r2, p, y, y0, invc, logc;
	uint32 ix, iz, top, tmp;
	int k, i;

	ix = core::mlwBitCast<uint32>(x);
	/* Fix sign of zero with downward rounding when x==1.  */
	if (MLW_UNLIKELY(ix == 0x3f800000))
		return 0;
	if (MLW_UNLIKELY(ix - 0x00800000 >= 0x7f800000 - 0x00800000)) {
		/* x < 0x1p-126 or inf or nan.  */
		if (ix * 2 == 0)
			return __math_divzerof(1);
		if (ix == 0x7f800000) /* log2(inf) == inf.  */
			return x;
		if ((ix & 0x80000000) || ix * 2 >= 0xff000000)
			return __math_invalid(x);
		/* x is subnormal, normalize it.  */
		ix = core::mlwBitCast<uint32>(x * 0x1p23f);
		ix -= 23 << 23;
	}

	/* x = 2^k z; where z is in range [0x3f330000,2*0x3f330000] and exact.
	   The range is split into N subintervals.
	   The ith subinterval contains z and c is near its center.  */
	tmp = ix - 0x3f330000;
	i = (tmp >> (23 - LOG2F_TABLE_BITS)) % N2F;
	top = tmp & 0xff800000;
	iz = ix - top;
	k = (int32)tmp >> 23; /* arithmetic shift */
	invc = __log2f_data.tab[i].invc;
	logc = __log2f_data.tab[i].logc;
	z = (f64)core::mlwBitCast<f32>(iz);

	/* log2(x) = log1p(z/c-1)/ln2 + log2(c) + k */
	r = z * invc - 1;
	y0 = logc + (f64)k;

	constexpr const f64(&A)[LOG2F_POLY_ORDER] = __log2f_data.poly;
	/* Pipelined polynomial evaluation to approximate log1p(r)/ln2.  */
	r2 = r * r;
	y = A[1] * r + A[2];
	y = A[0] * r2 + y;
	p = A[3] * r + y0;
	y = y * r2 + p;
	return eval_as_float(static_cast<f32>(y));
}
