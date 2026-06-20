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
template<> struct is_integer<uint8>  { static constexpr bool value = true; };
template<> struct is_integer<uint16> { static constexpr bool value = true; };
template<> struct is_integer<uint32> { static constexpr bool value = true; };
template<> struct is_integer<uint64> { static constexpr bool value = true; };
template<> struct is_integer<int8>  { static constexpr bool value = true; };
template<> struct is_integer<int16> { static constexpr bool value = true; };
template<> struct is_integer<int32> { static constexpr bool value = true; };
template<> struct is_integer<int64> { static constexpr bool value = true; };

// they are tecnicly defined as i32 but they could change in the future so we need to check for them as well
// template<> struct is_integer<isize> { static constexpr bool value = true; };
// template<> struct is_integer<usize> { static constexpr bool value = true; };
// template<> struct is_integer<index_t> { static constexpr bool value = true; };


#if !defined(MLW_NO_I128)
    template<> struct is_integer<uint128> { static constexpr bool value = true; };
    template<> struct is_integer<int128> { static constexpr bool value = true; };
#endif


template<typename T>
constexpr bool is_integer_v = is_integer<T>::value;

// is_float
template<typename T> struct is_float { static constexpr bool value = false; };
template<> struct is_float<f32> { static constexpr bool value = true; };
template<> struct is_float<f64> { static constexpr bool value = true; };

template<typename T>
constexpr bool is_float_v = is_float<T>::value;

template<typename T> struct is_pointer                 { static constexpr bool value = false; };
template<typename T> struct is_pointer<T*>             { static constexpr bool value = true; using Valuetype = T; };
template<typename T> struct is_pointer<T* const>       { static constexpr bool value = true; using Valuetype = T; };
template<typename T> struct is_pointer<const T*>       { static constexpr bool value = true; using Valuetype = const T; };
template<typename T> struct is_pointer<const T* const> { static constexpr bool value = true; using Valuetype = const T; };

template<typename T>
constexpr bool is_pointer_v = is_pointer<T>::value;

template<typename T> struct is_signed { static constexpr bool value = false; };
template<> struct is_signed<int8>  { static constexpr bool value = true; };
template<> struct is_signed<int16> { static constexpr bool value = true; };
template<> struct is_signed<int32> { static constexpr bool value = true; };
template<> struct is_signed<int64> { static constexpr bool value = true; };
#if !defined(MLW_NO_I128)
template<> struct is_signed<int128> { static constexpr bool value = true; };
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


// trivially copyable — compiler builtin, no way to implement without it
template<typename T>
struct is_trivially_copyable {
    static constexpr bool value = __is_trivially_copyable(T);
};

template<typename T, typename... Args>
struct is_constructible {
    static constexpr bool value = __is_constructible(T, Args...);
};

template<typename T, typename... Args>
inline constexpr bool is_constructible_v = is_constructible<T, Args...>::value;



// copy/move constructible and assignable — also builtins
template<typename T>
struct is_copy_constructible {
    static constexpr bool value = __is_constructible(T, const T&);
};

template<typename T>
constexpr bool is_copy_constructible_v = is_copy_constructible<T>::value;

template<typename T>
struct is_move_constructible {
    static constexpr bool value = __is_constructible(T, T&&);
};

template<typename T>
constexpr bool is_move_constructible_v = is_move_constructible<T>::value;

template<typename T>
struct is_copy_assignable {
    static constexpr bool value = __is_assignable(T&, const T&);
};

template<typename T>
struct is_move_assignable {
    static constexpr bool value = __is_assignable(T&, T&&);
};


// is_invocable — can F be called with Args...

template<typename T>
struct add_rvalue_reference {
    using type = T&&;
};

template<>
struct add_rvalue_reference<void> {
    using type = void;
};

template<>
struct add_rvalue_reference<const void> {
    using type = const void;
};

template<>
struct add_rvalue_reference<volatile void> {
    using type = volatile void;
};

template<>
struct add_rvalue_reference<const volatile void> {
    using type = const volatile void;
};

template<typename T>
using add_rvalue_reference_t =
    typename add_rvalue_reference<T>::type;

    
template<typename T>
typename add_rvalue_reference<T>::type declval() noexcept;

template<typename F, typename... Args>
struct is_invocable {
private:
    template<typename G, typename... A>
    static constexpr auto check(int)
        -> decltype(declval<G>()(declval<A>()...), bool{}) { return true; }

    template<typename...>
    static constexpr bool check(...) { return false; }

public:
    static constexpr bool value = check<F, Args...>(0);
};

template<typename F, typename... Args>
inline constexpr bool is_invocable_v = is_invocable<F, Args...>::value;


template<typename F, typename... Args>
struct invoke_result {
    using type = decltype(declval<F>()(declval<Args>()...));
};


template<typename F, typename... Args>
using invoke_result_t = typename invoke_result<F, Args...>::type;

template<typename T> struct remove_cv                { using type = T; };
template<typename T> struct remove_cv<const T>       { using type = T; };
template<typename T> struct remove_cv<volatile T>    { using type = T; };
template<typename T> struct remove_cv<const volatile T> { using type = T; };

template<typename T>
using remove_cv_t = typename remove_cv<T>::type;


template<typename T>
 constexpr remove_ref_t<T>&& move(T&& t) noexcept {
     return static_cast<remove_ref_t<T>&&>(t);
 }

template<typename T>
constexpr T&& forward(remove_ref_t<T>& t) noexcept {
    return static_cast<T&&>(t);
}

template<typename T>
constexpr T&& forward(remove_ref_t<T>&& t) noexcept {
    return static_cast<T&&>(t);
}
} // namespace core