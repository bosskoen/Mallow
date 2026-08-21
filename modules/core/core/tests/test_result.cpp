// core/core/tests/test_result.cpp
//
// Behavioural coverage for core/result.h. Result is a discriminated union that
// is always either Ok or Err (no default state), so there are no "empty" edge
// cases; the risks are all in (a) constructing the right arm, (b) destroying
// the active arm exactly once, and (c) the accessors reading the live member.
//
// value()/error() are unchecked (UB if the wrong arm is live), so every access
// below is guarded by isOk()/isErr() first. The TRY / if_ok / if_err macros
// live in error.h and are covered in test_error.cpp.
//
// Leak-checking uses Probe::live, which must return to 0 at the end of a test.

#include "core/result.h"

using namespace core;

namespace
{
    struct Probe
    {
        int v = 0;
        static inline int live = 0;
        Probe() { ++live; }
        explicit Probe(int x) : v(x) { ++live; }
        Probe(const Probe& o) : v(o.v) { ++live; }
        Probe(Probe&& o) noexcept : v(o.v) { o.v = -1; ++live; }
        Probe& operator=(const Probe& o) { v = o.v; return *this; }
        Probe& operator=(Probe&& o) noexcept { v = o.v; o.v = -1; return *this; }
        ~Probe() { --live; }
    };
}

namespace core_core_test
{
    // ---- construct Ok from a bare value -----------------------------------
    bool test_result_ok_from_value()
    {
        Result<int, int> r = 5;                 // T&& / const T& ctor -> Ok
        if (!r.isOk() || r.isErr()) return false;
        if (!static_cast<bool>(r)) return false;
        return r.value() == 5;
    }

    // ---- construct Ok / Err via the wrappers ------------------------------
    bool test_result_ok_err_wrappers()
    {
        Result<int, int> ok = Ok{7};
        Result<int, int> err = Err{9};
        if (!ok.isOk() || ok.value() != 7) return false;
        if (!err.isErr() || err.error() != 9) return false;
        return true;
    }

    // ---- Err with a convertible inner error type --------------------------
    bool test_result_err_convertible()
    {
        // E = long, inner Err holds int; the templated Err ctor converts it.
        Result<int, long> r = Err{42};          // int -> long
        return r.isErr() && r.error() == 42L;
    }

    // ---- access + mutate the live arm -------------------------------------
    bool test_result_access()
    {
        Result<int, int> r = Ok{1};
        r.value() = 100;
        if (r.value() != 100) return false;

        Result<int, int> e = Err{2};
        e.error() = 200;
        return e.error() == 200;
    }

    // ---- takeValue / takeError move the payload out -----------------------
    bool test_result_take()
    {
        Probe::live = 0;
        {
            Result<Probe, int> r = Ok{Probe{11}};
            Probe p = r.takeValue();            // move out of the Ok arm
            if (p.v != 11) return false;
        }
        if (Probe::live != 0) return false;

        Result<int, Probe> e = Err{Probe{22}};
        Probe q = e.takeError();
        return q.v == 22;
    }

    // ---- valueOr: value on Ok, converted fallback on Err ------------------
    bool test_result_value_or()
    {
        Result<int, int> ok = Ok{3};
        if (ok.valueOr(999) != 3) return false;

        Result<int, int> err = Err{1};
        if (err.valueOr(999) != 999) return false;
        // valueOr is const and non-consuming: the Result is still Err after.
        return err.isErr();
    }

    // ---- copy construction duplicates the live arm ------------------------
    bool test_result_copy()
    {
        Probe::live = 0;
        {
            Result<Probe, int> a = Ok{Probe{5}};
            Result<Probe, int> b = a;           // copy the Ok arm
            if (!b.isOk() || b.value().v != 5) return false;
            if (!a.isOk() || a.value().v != 5) return false;   // source intact
            if (Probe::live != 2) return false; // two live Probes
        }
        return Probe::live == 0;
    }

    // ---- move construction transfers the live arm -------------------------
    bool test_result_move()
    {
        Probe::live = 0;
        {
            Result<Probe, int> a = Ok{Probe{6}};
            Result<Probe, int> b = core::move(a);
            if (!b.isOk() || b.value().v != 6) return false;
            // a still reports Ok (tag is not cleared); its Probe is moved-from.
            if (!a.isOk()) return false;
        }
        return Probe::live == 0;
    }

    // ---- assignment switches arms and destroys the old one ----------------
    bool test_result_assign_switch_arm()
    {
        Probe::live = 0;
        {
            Result<Probe, int> r = Ok{Probe{1}};
            if (Probe::live != 1) return false;
            r = Result<Probe, int>(Err{9});     // destroy Ok arm, become Err
            if (!r.isErr() || r.error() != 9) return false;
            if (Probe::live != 0) return false; // the Probe was destroyed
        }
        return Probe::live == 0;
    }

    // ---- FORMAT: needs the real IO/format stack ---------------------------
    // Ok(x) / Err(x). Only meaningful in the freestanding build.
    bool test_result_format()
    {
        auto eq = [](const char* got, isize n, const char* exp) {
            for (index_t i = 0; i < n; ++i) { if (!exp[i] || got[i] != exp[i]) return false; }
            return exp[n] == '\0';
        };
        {
            auto& buf = core::detail::getFormatBuffer(); buf.len = 0;
            Result<int, int> r = Ok{5};
            detail::formatValue(buf, r);
            if (!eq(buf.ptr, buf.len, "Ok(5)")) return false;
        }
        {
            auto& buf = core::detail::getFormatBuffer(); buf.len = 0;
            Result<int, int> r = Err{7};
            detail::formatValue(buf, r);
            if (!eq(buf.ptr, buf.len, "Err(7)")) return false;
        }
        return true;
    }
} // namespace core_core_test
