#pragma once
#include "typedef.h"

namespace core{

// is_same
template<typename A, typename B> struct is_same       { static constexpr bool value = false; };
template<typename A>             struct is_same<A, A> { static constexpr bool value = true;  };

template<typename A, typename B>
constexpr bool is_same_v = is_same<A, B>::value;

template<typename A, typename B>
concept same_as = is_same_v<A, B>;

// is_bool
template<typename T> struct is_bool { static constexpr bool value = false; };
template<>           struct is_bool<bool> { static constexpr bool value = true; };

template<typename T>
constexpr bool is_bool_v = is_bool<T>::value;

// is_integer
template<typename T> struct is_integer { static constexpr bool value = false; };
template<> struct is_integer<u8>  { static constexpr bool value = true; };
template<> struct is_integer<u16> { static constexpr bool value = true; };
template<> struct is_integer<u32> { static constexpr bool value = true; };
template<> struct is_integer<u64> { static constexpr bool value = true; };
template<> struct is_integer<i8>  { static constexpr bool value = true; };
template<> struct is_integer<i16> { static constexpr bool value = true; };
template<> struct is_integer<i32> { static constexpr bool value = true; };
template<> struct is_integer<i64> { static constexpr bool value = true; };

// they are tecnicly defined as i32 but they could change in the future so we need to check for them as well
// template<> struct is_integer<isize> { static constexpr bool value = true; };
// template<> struct is_integer<usize> { static constexpr bool value = true; };
// template<> struct is_integer<index_t> { static constexpr bool value = true; };


#if !defined(MLW_NO_I128)
    template<> struct is_integer<u128> { static constexpr bool value = true; };
    template<> struct is_integer<i128> { static constexpr bool value = true; };
#endif


template<typename T>
constexpr bool is_integer_v = is_integer<T>::value;

// is_float
template<typename T> struct is_float { static constexpr bool value = false; };
template<> struct is_float<f32> { static constexpr bool value = true; };
template<> struct is_float<f64> { static constexpr bool value = true; };

template<typename T>
constexpr bool is_float_v = is_float<T>::value;

template<typename T> struct is_pointer              { static constexpr bool value = false; };
template<typename T> struct is_pointer<T*>          { static constexpr bool value = true; using Valuetype = T; };
template<typename T> struct is_pointer<const T*>    { static constexpr bool value = true; using Valuetype = const T; };

template<typename T>
constexpr bool is_pointer_v = is_pointer<T>::value;

template<typename T> struct is_signed { static constexpr bool value = false; };
template<> struct is_signed<i8>  { static constexpr bool value = true; };
template<> struct is_signed<i16> { static constexpr bool value = true; };
template<> struct is_signed<i32> { static constexpr bool value = true; };
template<> struct is_signed<i64> { static constexpr bool value = true; };
#if !defined(MLW_NO_I128)
template<> struct is_signed<i128> { static constexpr bool value = true; };
#endif
// template<> struct is_integer<isize> { static constexpr bool value = true; };
// template<> struct is_integer<index_t> { static constexpr bool value = true; };
template<typename T>
constexpr bool is_signed_v = is_signed<T>::value;


template<typename T>        struct is_array          { static constexpr bool value = false; };
template<typename T, usize N> struct is_array<T[N]>  { static constexpr bool value = true; using Valuetype = T; static constexpr usize size = N; };

template<typename T>
constexpr bool is_array_v = is_array<T>::value;

template <typename T> struct remove_ref      { using type = T; };  // base case, nothing to remove
template <typename T> struct remove_ref<T&>  { using type = T; };  // T& -> T
template <typename T> struct remove_ref<T&&> { using type = T; };

template <typename T>
using remove_ref_t = typename remove_ref<T>::type;

} // namespace core