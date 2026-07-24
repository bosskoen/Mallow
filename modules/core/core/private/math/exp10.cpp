// =============================================================================
//  Base-10 exponential - core::mlwExp10(f64) / core::mlwExp10(f32)
//
//  From musl libc (src/math/exp10.c, exp10f.c). Adapted for Mallow
//  (project types / conventions; routed through core:: exp2/mod/pow).
//
//  musl is MIT-licensed. Copyright (c) 2005-2020 Rich Felker, et al.
//
//  Permission is hereby granted, free of charge, to any person obtaining a copy of
//  this software and associated documentation files (the "Software"), to deal in
//  the Software without restriction, including without limitation the rights to
//  use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
//  of the Software, and to permit persons to whom the Software is furnished to do
//  so, subject to the following conditions:
//
//  The above copyright notice and this permission notice shall be included in all
//  copies or substantial portions of the Software.
//
//  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND — see the MIT
//  license text for the full disclaimer.
// =============================================================================

#include "libc/math.h"


f32 core::mlwExp10(f32 x)
{
	static const f32 p10[] = {
		1e-7f, 1e-6f, 1e-5f, 1e-4f, 1e-3f, 1e-2f, 1e-1f,
		1, 1e1, 1e2, 1e3, 1e4, 1e5, 1e6, 1e7
	};
	FloatParts p = mlwSplit(x);
	f32 n = p.integral;
	f32 y = p.fractional;

	//float n, y = modff(x, &n);
	union { f32 f; uint32 i; } u = { n };
	/* fabsf(n) < 8 without raising invalid on nan */
	if ((u.i >> 23 & 0xff) < 0x7f + 3) {
		if (!y) return p10[static_cast<int>(n) + 7];
		y = mlwExp2(3.32192809488736234787031942948939f * y); //float call
		return y * p10[static_cast<int>(n) + 7];
	}
	return static_cast<f32>(mlwExp2(3.32192809488736234787031942948939 * x)); //double call
}


f64 core::mlwExp10(f64 x)
{
	static const f64 p10[] = {
		1e-15, 1e-14, 1e-13, 1e-12, 1e-11, 1e-10,
		1e-9, 1e-8, 1e-7, 1e-6, 1e-5, 1e-4, 1e-3, 1e-2, 1e-1,
		1, 1e1, 1e2, 1e3, 1e4, 1e5, 1e6, 1e7, 1e8, 1e9,
		1e10, 1e11, 1e12, 1e13, 1e14, 1e15
	};
	DoubleParts p = mlwSplit(x);
	f64 n = p.integral;
	f64 y = p.fractional;
	union { f64 f; uint64 i; } u = { n };
	/* fabs(n) < 16 without raising invalid on nan */
	if ((u.i >> 52 & 0x7ff) < 0x3ff + 4) {
		if (!y) return p10[(int)n + 15];
		y = mlwExp2(3.32192809488736234787031942948939 * y);
		return y * p10[(int)n + 15];
	}
	return mlwPow(10.0, x);
}