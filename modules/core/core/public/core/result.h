#pragma once
#include "typedef.h"
#include "traits.h"
#include "core/compilers.h"
#include "io/format.h"

namespace core
{
    template <typename E> struct Err { E value; };
    template <typename E> Err(E) -> Err<E>;

    template <typename T> struct Ok { T value; };
    template <typename T> Ok(T) -> Ok<T>;

    template <typename T, typename E>
    class Result
    {
        static_assert(!is_reference_v<T> && !is_reference_v<E>,
                      "Result cannot hold reference types; use a pointer");
        static_assert(!is_same_v<remove_cv_t<T>, void> && !is_same_v<remove_cv_t<E>, void>,
                      "Result cannot hold void; use an empty struct");

        bool ok_;
        union
        {
            T val_;
            E err_;
        };

        void destroy()
        {
            if (ok_) val_.~T();
            else     err_.~E();
        }

    public:
        // --- construction ---------------------------------------------------

        Result(const T &v)  : ok_(true)  { new (&val_) T(v); }
        Result(T &&v)       : ok_(true)  { new (&val_) T(core::move(v)); }

        Result(const Ok<T> &o) : ok_(true) { new (&val_) T(o.value); }
        Result(Ok<T> &&o)      : ok_(true) { new (&val_) T(core::move(o.value)); }

        // Err<E2> -> Result: the "From"/cast site. E must be constructible from E2.
        // This is what makes `return Err{ inner_error };` work when the enclosing
        // function's error type differs from the inner one.
        template <typename E2>
            requires is_constructible_v<E, E2>
        Result(Err<E2> e) : ok_(false) { new (&err_) E(core::move(e.value)); }

        // no default ctor — a Result is always ok or err, never neither
        Result() = delete;

        // --- special members (conditionally present) ------------------------

        Result(const Result &o)
            requires (is_copy_constructible_v<T> && is_copy_constructible_v<E>)
            : ok_(o.ok_)
        {
            if (ok_) new (&val_) T(o.val_);
            else     new (&err_) E(o.err_);
        }

        Result(Result &&o)
            requires (is_move_constructible_v<T> && is_move_constructible_v<E>)
            : ok_(o.ok_)
        {
            if (ok_) new (&val_) T(core::move(o.val_));
            else     new (&err_) E(core::move(o.err_));
        }

        Result &operator=(const Result &o)
            requires (is_copy_constructible_v<T> && is_copy_constructible_v<E>)
        {
            if (this == &o) return *this;
            destroy();
            ok_ = o.ok_;
            if (ok_) new (&val_) T(o.val_);
            else     new (&err_) E(o.err_);
            return *this;
        }

        Result &operator=(Result &&o)
            requires (is_move_constructible_v<T> && is_move_constructible_v<E>)
        {
            if (this == &o) return *this;
            destroy();
            ok_ = o.ok_;
            if (ok_) new (&val_) T(core::move(o.val_));
            else     new (&err_) E(core::move(o.err_));
            return *this;
        }

        ~Result() { destroy(); }

        // --- queries / access -----------------------------------------------

        bool isOk()  const { return ok_; }
        bool isErr() const { return !ok_; }
        explicit operator bool() const { return ok_; }

        // unchecked — guard with isOk()/isErr(), or use TRY. no exceptions to throw.
        T       &value()       { return val_; }
        const T &value() const { return val_; }
        E       &error()       { return err_; }
        const E &error() const { return err_; }

        // move-out accessors — used by TRY, return by value so nothing dangles
        T takeValue() { return core::move(val_); }
        E takeError() { return core::move(err_); }

        template <typename U>
        T valueOr(U &&fallback) const
        {
            return ok_ ? val_ : T(core::forward<U>(fallback));
        }

        // --- formatting (present only if both arms are formattable) ----------

        template <FormatBuffer Buffer>
            requires (FormattableValue<T, Buffer> && FormattableValue<E, Buffer>)
        void format(Buffer &buffer) const
        {
            if (ok_)
            {
                buffer.append(CStr("Ok("));
                detail::formatValue(buffer, val_);
                buffer.append(')');
            }
            else
            {
                buffer.append(CStr("Err("));
                detail::formatValue(buffer, err_);
                buffer.append(')');
            }
        }
    };
}