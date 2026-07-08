#include <core/libc/math.h>
#include "math.h"

using namespace core;

namespace core_core_test
{
    bool test_floor_positive()
{
    return mlwFloor(0.0) == 0.0
        && mlwFloor(1.0) == 1.0
        && mlwFloor(1.5) == 1.0
        && mlwFloor(1.999) == 1.0
        && mlwFloor(42.75) == 42.0;
}

bool test_floor_negative()
{
    return mlwFloor(-1.0) == -1.0
        && mlwFloor(-1.5) == -2.0
        && mlwFloor(-1.001) == -2.0
        && mlwFloor(-42.75) == -43.0;
}

bool test_floor_small()
{
    return mlwFloor(0.5) == 0.0
        && mlwFloor(0.999) == 0.0
        && mlwFloor(-0.5) == -1.0
        && mlwFloor(-0.999) == -1.0;
}

bool test_floor_large()
{
    return mlwFloor(123456789.75) == 123456789.0
        && mlwFloor(-123456789.75) == -123456790.0;
}

bool test_floor_special()
{
    f64 inf = NumericLimits<f64>::infinity;

    return mlwFloor(inf) == inf
        && mlwFloor(-inf) == -inf
        && mlwIsNaN(mlwFloor(mlwNaN<f64>()));
}

bool test_copysign()
{
    return mlwCopySign(1.0, -2.0) == -1.0
        && mlwCopySign(-1.0, 2.0) == 1.0
        && mlwBitCast<uint64>(mlwCopySign(0.0, -1.0)) == mlwBitCast<uint64>(-0.0)
        && mlwBitCast<uint64>(mlwCopySign(-0.0, 1.0)) == mlwBitCast<uint64>(0.0)
        && mlwCopySign(42.5, 0.0) == 42.5
        && mlwCopySign(42.5, -0.0) == -42.5;
}
} // namespace core::core::test