#include "core/traits.h"
using namespace core;
namespace core_core_test
{
    bool test_test(){
        return true;
    }

    bool test_is_same() {
    return is_same_v<i32, i32>
        && !is_same_v<i32, i64>
        && !is_same_v<bool, i32>;
}

bool test_is_bool() {
    return is_bool_v<bool>
        && !is_bool_v<i32>
        && !is_bool_v<f32>;
}

bool test_is_integer() {
    return is_integer_v<u8>
        && is_integer_v<i64>
        && is_integer_v<isize>
        && is_integer_v<index_t>
        && !is_integer_v<bool>
        && !is_integer_v<f32>;
}

bool test_is_float() {
    return is_float_v<f32>
        && is_float_v<f64>
        && !is_float_v<i32>
        && !is_float_v<bool>;
}
} // namespace core_test