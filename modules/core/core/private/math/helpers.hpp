#pragma once
#include "core/compilers.h"

static MLW_FORCE_INLINE f64 eval_as_double(f64 x) {
	f64 y = x;
	return y;
}

static MLW_FORCE_INLINE f32 eval_as_float(f32 x)
{
	f32 y = x;
	return y;
}

constexpr static MLW_FORCE_INLINE f64 __math_oflow(int sign)
{


	return sign ? -core::NumericLimits<f64>::infinity : core::NumericLimits<f64>::infinity;
}

constexpr static MLW_FORCE_INLINE f32 __math_oflowf(int sign)
{


	return sign ? -core::NumericLimits<f32>::infinity : core::NumericLimits<f32>::infinity;
}

constexpr static MLW_FORCE_INLINE f64 __math_uflow(int sign)
{


	return sign ? -0.0 : 0.0;
}

constexpr static MLW_FORCE_INLINE f32 __math_uflowf(int sign)
{


	return sign ? -0.0f : 0.0f;
}


