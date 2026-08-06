// core/core/tests/test_traits.cpp
//
// Coverage for core/traits.h. Almost everything here is a *compile-time*
// contract, so it is checked with static_assert: if the header regresses the
// translation unit fails to build, which is a stronger guarantee than a
// runtime bool. Each bool test_*() exists only so the runner has something to
// call and report; a translation unit that compiled already proved the point,
// so they simply return true.
//
// The one genuinely runtime-observable behaviour is move()/forward(), checked
// in test_traits_move_forward with an instrumented type.

#include "core/traits.h"

using namespace core;

namespace
{
    // Instrumented type: counts how many times it was copied vs moved so the
    // move/forward tests can prove the correct overload was selected.
    struct Probe
    {
        int v = 0;
        static inline int copies = 0;
        static inline int moves  = 0;

        Probe() = default;
        Probe(int x) : v(x) {}
        Probe(const Probe& o) : v(o.v) { ++copies; }
        Probe(Probe&& o) noexcept : v(o.v) { o.v = -1; ++moves; }
        Probe& operator=(const Probe&) { ++copies; return *this; }
        Probe& operator=(Probe&& o) noexcept { v = o.v; o.v = -1; ++moves; return *this; }
    };

    struct Base {};
    struct Derived : Base {};
    struct NoDefault { NoDefault() = delete; NoDefault(int) {} };
}

namespace core_core_test
{
    // ---- is_same -----------------------------------------------------------
    bool test_traits_is_same()
    {
        static_assert(is_same_v<int, int>);
        static_assert(!is_same_v<int, unsigned>);
        static_assert(!is_same_v<int, const int>);   // cv matters
        static_assert(!is_same_v<int, int&>);         // ref matters
        static_assert(same_as<f64, f64>);             // concept form
        return true;
    }

    // ---- is_bool -----------------------------------------------------------
    bool test_traits_is_bool()
    {
        static_assert(is_bool_v<bool>);
        static_assert(!is_bool_v<int8>);
        static_assert(!is_bool_v<char>);
        return true;
    }

    // ---- is_integer --------------------------------------------------------
    bool test_traits_is_integer()
    {
        static_assert(is_integer_v<uint8>  && is_integer_v<int8>);
        static_assert(is_integer_v<uint16> && is_integer_v<int16>);
        static_assert(is_integer_v<uint32> && is_integer_v<int32>);
        static_assert(is_integer_v<uint64> && is_integer_v<int64>);
        static_assert(!is_integer_v<bool>);   // bool is deliberately excluded
        static_assert(!is_integer_v<f32>);
        static_assert(!is_integer_v<char>);   // char is not one of the aliases
        return true;
    }

    // ---- make_unsigned -----------------------------------------------------
    bool test_traits_make_unsigned()
    {
        static_assert(is_same_v<make_unsigned_t<int8>,  uint8>);
        static_assert(is_same_v<make_unsigned_t<int16>, uint16>);
        static_assert(is_same_v<make_unsigned_t<int32>, uint32>);
        static_assert(is_same_v<make_unsigned_t<int64>, uint64>);
        static_assert(is_same_v<make_unsigned_t<uint32>, uint32>);  // already unsigned
        return true;
    }

    // ---- is_float ----------------------------------------------------------
    bool test_traits_is_float()
    {
        static_assert(is_float_v<f32> && is_float_v<f64>);
        static_assert(!is_float_v<int32> && !is_float_v<bool>);
        return true;
    }

    // ---- is_pointer + Valuetype -------------------------------------------
    bool test_traits_is_pointer()
    {
        static_assert(is_pointer_v<int*>);
        static_assert(is_pointer_v<const int*>);
        static_assert(is_pointer_v<int* const>);
        static_assert(!is_pointer_v<int>);
        static_assert(!is_pointer_v<int&>);
        static_assert(is_same_v<is_pointer<int*>::Valuetype, int>);
        static_assert(is_same_v<is_pointer<const int*>::Valuetype, const int>);
        return true;
    }

    // ---- is_signed ---------------------------------------------------------
    bool test_traits_is_signed()
    {
        static_assert(is_signed_v<int8> && is_signed_v<int64>);
        static_assert(!is_signed_v<uint8> && !is_signed_v<uint64>);
        static_assert(!is_signed_v<f32>);   // trait only classifies the signed integer aliases
        return true;
    }

    // ---- is_array + extent -------------------------------------------------
    bool test_traits_is_array()
    {
        static_assert(is_array_v<int[4]>);
        static_assert(!is_array_v<int>);
        static_assert(!is_array_v<int*>);
        static_assert(is_array<int[4]>::size == 4);
        static_assert(is_same_v<is_array<char[7]>::Valuetype, char>);
        return true;
    }

    // ---- remove_ref / is_reference ----------------------------------------
    bool test_traits_remove_ref()
    {
        static_assert(is_same_v<remove_ref_t<int&>,  int>);
        static_assert(is_same_v<remove_ref_t<int&&>, int>);
        static_assert(is_same_v<remove_ref_t<int>,   int>);
        static_assert(is_reference_v<int&>);
        static_assert(is_reference_v<int&&>);
        static_assert(!is_reference_v<int>);
        return true;
    }

    // ---- remove_cv / remove_const -----------------------------------------
    bool test_traits_remove_cv()
    {
        static_assert(is_same_v<remove_cv_t<const int>,          int>);
        static_assert(is_same_v<remove_cv_t<volatile int>,       int>);
        static_assert(is_same_v<remove_cv_t<const volatile int>, int>);
        static_assert(is_same_v<remove_const_t<const int>,       int>);
        static_assert(is_same_v<remove_const_t<int>,             int>);
        // remove_const leaves volatile alone
        static_assert(is_same_v<remove_const_t<volatile int>,    volatile int>);
        return true;
    }

    // ---- constructible / copyable / movable / assignable ------------------
    bool test_traits_constructible()
    {
        static_assert(is_constructible_v<Probe, int>);
        static_assert(!is_constructible_v<NoDefault>);       // no default ctor
        static_assert(is_constructible_v<NoDefault, int>);

        static_assert(is_copy_constructible_v<Probe>);
        static_assert(is_move_constructible_v<Probe>);
        static_assert(is_copy_assignable_v<Probe>);
        static_assert(is_move_assignable_v<Probe>);

        static_assert(is_trivially_copyable_v<int>);
        static_assert(!is_trivially_copyable_v<Probe>);      // has user-defined copy
        return true;
    }

    // ---- is_invocable / invoke_result -------------------------------------
    bool test_traits_invocable()
    {
        auto ret_int    = [](int)   { return 0; };
        auto ret_double = [](int)   { return 0.0; };
        (void)ret_int; (void)ret_double;

        static_assert(is_invocable_v<decltype(ret_int), int>);
        static_assert(is_invocable_v<decltype(ret_int), char>);   // char converts to int
        static_assert(!is_invocable_v<decltype(ret_int), int, int>); // too many args
        static_assert(!is_invocable_v<decltype(ret_int), Probe>);    // Probe -> int? no

        static_assert(is_same_v<invoke_result_t<decltype(ret_int), int>,    int>);
        static_assert(is_same_v<invoke_result_t<decltype(ret_double), int>, double>);
        return true;
    }

    // ---- move / forward (runtime-observable) ------------------------------
    bool test_traits_move_forward()
    {
        Probe::copies = Probe::moves = 0;

        Probe a{7};
        Probe b = core::move(a);          // should MOVE, not copy
        if (Probe::moves != 1 || Probe::copies != 0) return false;
        if (b.v != 7) return false;
        if (a.v != -1) return false;      // our move ctor blanks the source

        // forward: perfect-forward an lvalue as lvalue (copy) ...
        auto sink = [](Probe p) { return p.v; };
        Probe c{9};
        (void)sink(core::forward<Probe&>(c));   // lvalue -> copy
        if (Probe::copies != 1) return false;

        // ... and an rvalue as rvalue (move)
        (void)sink(core::forward<Probe>(Probe{11})); // rvalue -> move
        if (Probe::moves < 2) return false;
        return true;
    }

    // ---- type_list is usable as a compile-time list ----------------------
    bool test_traits_type_list()
    {
        // Just prove the template instantiates and is distinct per pack.
        static_assert(!is_same_v<type_list<int>, type_list<int, int>>);
        static_assert(is_same_v<type_list<int, f32>, type_list<int, f32>>);
        return true;
    }
} // namespace core_core_test
