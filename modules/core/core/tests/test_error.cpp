// core/core/tests/test_error.cpp
//
// Coverage for core/error.h: the TRY / TRY_EXPR early-return macros and the
// if_ok / if_err binding macros. All four operate on a Result, so the tests
// drive them through small helper functions whose return type is a Result
// (a requirement TRY/TRY_EXPR impose on the enclosing function).
//
// The helpers live in an anonymous namespace and are deliberately NOT named
// test_*, so the test runner's scanner does not pick them up as test cases.

#include "core/result.h"
#include "core/error.h"

using namespace core;

namespace
{
    // Source of Results the macros consume.
    Result<int, int> produce(bool fail, int val)
    {
        if (fail) return Err{-1};
        return Ok{val};
    }

    // TRY: binds the success value and keeps it in scope; returns Err early on
    // failure. Enclosing return type must accept core::Err{...}.
    Result<int, int> chain_with_try(bool fail)
    {
        TRY(x, produce(fail, 10));   // early-returns Err{-1} if fail
        TRY(y, produce(false, 5));   // second binding stays in scope too
        return Ok{x + y};            // only reached on success -> 15
    }

#if defined(MLW_GCC) || defined(MLW_CLANG)
    // TRY_EXPR: expression form; yields the value inline, still early-returns
    // from the enclosing function on error.
    Result<int, int> chain_with_try_expr(bool fail)
    {
        int x = TRY_EXPR(produce(fail, 100));
        return Ok{x + 1};            // 101 on success
    }
#endif
}

namespace core_core_test
{
    // ---- TRY: success path binds and continues ----------------------------
    bool test_error_try_ok()
    {
        auto r = chain_with_try(false);
        return r.isOk() && r.value() == 15;
    }

    // ---- TRY: failure path short-circuits with the error ------------------
    bool test_error_try_err()
    {
        auto r = chain_with_try(true);
        return r.isErr() && r.error() == -1;
    }

    // ---- TRY_EXPR: success + failure --------------------------------------
    // TRY_EXPR uses a GNU statement-expression, so it only exists on GCC/Clang.
    // The function is always defined (names are discovered textually and cannot
    // be compiled out); on a toolchain without TRY_EXPR it simply skips.
    bool test_error_try_expr()
    {
#if defined(MLW_GCC) || defined(MLW_CLANG)
        auto ok = chain_with_try_expr(false);
        if (!ok.isOk() || ok.value() != 101) return false;
        auto err = chain_with_try_expr(true);
        return err.isErr() && err.error() == -1;
#else
        return true;
#endif
    }

    // ---- if_ok: body runs on Ok, else-branch on Err -----------------------
    bool test_error_if_ok()
    {
        bool ran_ok = false, ran_else = false;
        int captured = 0;

        auto res = produce(false, 7);
        if_ok(v, res, {
            ran_ok = true;
            captured = v;            // v is a reference to the success value
        })
        else { ran_else = true; }

        if (!ran_ok || ran_else || captured != 7) return false;

        // now the Err case: body must be skipped, else-branch must run
        ran_ok = ran_else = false;
        auto res2 = produce(true, 0);
        if_ok(v, res2, {
            (void)v;
            ran_ok = true;
        })
        else { ran_else = true; }

        return !ran_ok && ran_else;
    }

    // ---- if_err: body runs on Err, else-branch on Ok ----------------------
    bool test_error_if_err()
    {
        bool ran_err = false, ran_else = false;
        int captured = 0;

        auto res = produce(true, 0);
        if_err(e, res, {
            ran_err = true;
            captured = e;            // e is a reference to the error value
        })
        else { ran_else = true; }

        if (!ran_err || ran_else || captured != -1) return false;

        // Ok case: body skipped, else runs
        ran_err = ran_else = false;
        auto res2 = produce(false, 3);
        if_err(e, res2, {
            (void)e;
            ran_err = true;
        })
        else { ran_else = true; }

        return !ran_err && ran_else;
    }
} // namespace core_core_test
