#pragma once
#include "typedef.h"
#include "traits.h"
#include "core/compilers.h"
#include "io/format.h"

/// \file
/// \brief A simple `Result` type for functions that may return a value or an error.

namespace core
{
    /// \brief Wraps an error value for construction of a \ref Result.
    template <typename E> struct Err { E value; };
    template <typename E> Err(E) -> Err<E>;

    /// \brief Wraps a successful value for construction of a \ref Result.
    template <typename T> struct Ok { T value; };
    template <typename T> Ok(T) -> Ok<T>;

    /// \ingroup formattable
    /// \brief A return type representing either a value of type \p T or an error of type \p E.
    ///
    /// Result is a discriminated union that stores either the success value or the
    /// error value. It does not support default construction, references, or void
    /// alternatives.
    ///
    /// \tparam T The success value type.
    /// \tparam E The error value type.
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

        /// \brief Construct a successful result from a const reference.
        Result(const T &v)  : ok_(true)  { new (&val_) T(v); }
        /// \brief Construct a successful result from an rvalue.
        Result(T &&v)       : ok_(true)  { new (&val_) T(core::move(v)); }

        /// \brief Construct a successful result from \ref Ok.
        Result(const Ok<T> &o) : ok_(true) { new (&val_) T(o.value); }
        /// \brief Construct a successful result from \ref Ok by moving.
        Result(Ok<T> &&o)      : ok_(true) { new (&val_) T(core::move(o.value)); }

        /// \brief Construct an error result from an \ref Err wrapper.
        ///
        /// \tparam E2 An error type that is constructible to \p E.
        /// \note This allows returning `Err{inner_error}` when the enclosing
        ///       function's error type differs from the inner error type.
        template <typename E2>
            requires is_constructible_v<E, E2>
        Result(Err<E2> e) : ok_(false) { new (&err_) E(core::move(e.value)); }

        /// \brief Deleted: a Result must be explicitly Ok or Err.
        Result() = delete;

        // --- special members (conditionally present) ------------------------

        /// \brief Copy-construct a Result when both arms are copyable.
        Result(const Result &o)
            requires (is_copy_constructible_v<T> && is_copy_constructible_v<E>)
            : ok_(o.ok_)
        {
            if (ok_) new (&val_) T(o.val_);
            else     new (&err_) E(o.err_);
        }

        /// \brief Move-construct a Result when both arms are movable.
        Result(Result &&o)
            requires (is_move_constructible_v<T> && is_move_constructible_v<E>)
            : ok_(o.ok_)
        {
            if (ok_) new (&val_) T(core::move(o.val_));
            else     new (&err_) E(core::move(o.err_));
        }

        /// \brief Copy-assign a Result when both arms are copyable.
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

        /// \brief Move-assign a Result when both arms are movable.
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

        /// \brief Destroy the active variant member.
        ~Result() { destroy(); }

        // --- queries / access -----------------------------------------------

        /// \brief Returns true if the result is ok.
        bool isOk()  const { return ok_; }
        /// \brief Returns true if the result is an error.
        bool isErr() const { return !ok_; }
        /// \brief Allows `if (result)` to mean success.
        explicit operator bool() const { return ok_; }

        /// \brief Access the stored success value; undefined unless `isOk()`.
        T       &value()       { return val_; }
        /// \brief Access the stored success value; undefined unless `isOk()`.
        const T &value() const { return val_; }
        /// \brief Access the stored error value; undefined unless `isErr()`.
        E       &error()       { return err_; }
        /// \brief Access the stored error value; undefined unless `isErr()`.
        const E &error() const { return err_; }

        /// \brief Move out the stored success value.
        /// \note Only valid when `isOk()`.
        T takeValue() { return core::move(val_); }
        /// \brief Move out the stored error value.
        /// \note Only valid when `isErr()`.
        E takeError() { return core::move(err_); }

        /// \brief Return the stored success value or a fallback if not ok.
        ///
        /// The fallback is converted to \p T when the result is an error.
        template <typename U>
        T valueOr(U &&fallback) const
        {
            return ok_ ? val_ : T(core::forward<U>(fallback));
        }

        // --- formatting (present only if both arms are formattable) ----------

        /// \brief Format the Result as `Ok(...)` or `Err(...)` when both arms are formattable.
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