#pragma once

/// \brief Shortcut macro for propagating `Result` error values.
///
/// `TRY(var, res)` evaluates `res` and returns early with `Err` if it is
/// an error; otherwise it declares `var` and binds the contained success
/// value to it. `var` stays in scope for the rest of the enclosing block.
///
/// \param var  Name to declare and bind the success value to. Must be unused
///             in the current scope (the macro also declares `_try_##var`).
/// \param res  A `Result` expression to evaluate.
///
/// \warning Returns early **from the current function**, which must have a
///          return type constructible from `core::Err{...}`.
/// \warning Expands to several declarations in the *enclosing* scope, so it
///          cannot be the body of a brace-less `if`/`for`/`while`
///          (`if (c) TRY(x, f());` guards only the first statement). Use it
///          at block scope.
///
/// \note Unlike \ref if_ok / \ref if_err, `var` remains alive after the macro,
///       so you can use it on later lines.
///
/// \code
/// Result<int, Error> foo();
///
/// Result<Ok, Error> bar()
/// {
///     TRY(x, foo());   // declares x; returns Err early if foo() failed
///
///     auto v = foo();
///     TRY(y, v);       // different name; bind from an existing Result
///
///     // use x and y here
///     return Ok{};
/// }
/// \endcode
#define TRY(var, res)                                             \
    auto _try_##var = (res);                                      \
    if (_try_##var.isErr()) return core::Err{_try_##var.takeError()}; \
    auto var = _try_##var.takeValue()

#if defined(MLW_GCC) || defined(MLW_CLANG)
/// \brief Expression form of \ref TRY, for use inside an expression.
///
/// Yields the success value directly, so it can appear where a value is
/// expected. On error it returns early from the enclosing function, exactly
/// like \ref TRY.
///
/// \param res  A `Result` expression to evaluate.
///
/// \warning Uses a GNU statement-expression extension: available only on
///          GCC and Clang (hence the `#if` guard), not portable.
/// \warning Despite being an expression, it can `return` out of the enclosing
///          function on error. The function's return type must be constructible
///          from `core::Err{...}`.
///
/// \code
/// int x = TRY_EXPR(open_file("x"));   // returns Err from the caller on failure
/// \endcode
#define TRY_EXPR(res)                                             \
    ({                                                            \
        auto _try_r = (res);                                      \
        if (_try_r.isErr()) return core::Err{_try_r.takeError()}; \
        _try_r.takeValue();                                       \
    })
#endif

/// \brief Execute `expr` and run the body if the result is Ok.
///
/// `var` is bound to a reference to the success value, valid only inside the
/// block. An optional `else` runs when the result is not Ok.
///
/// \param var   Name bound to the success value as `T&` inside the block.
/// \param expr  A `Result` expression to evaluate.
/// \param ...   The block to run on success (pass it braced).
///
/// \note `var` is scoped to the block only; it does not outlive it. This is
///       the opposite of \ref TRY, which keeps `var` in the enclosing scope.
///
/// \code
/// auto res = compute();
/// if_ok(value, res, {
///     // value is T& here
/// })
/// else {
///     // res was Err
/// }
/// \endcode
#define if_ok(var, expr, ...)                    \
    if (auto _r_##var = (expr); _r_##var.isOk()) \
        if (auto &var = _r_##var.value(); true)  \
            __VA_ARGS__                          \
        else                                     \
        {                                        \
        }

/// \brief Execute `expr` and run the body if the result is Err.
///
/// `var` is bound to a reference to the error value, valid only inside the
/// block. An optional `else` runs when the result is not Err.
///
/// \param var   Name bound to the error value as `E&` inside the block.
/// \param expr  A `Result` expression to evaluate.
/// \param ...   The block to run on error (pass it braced).
///
/// \note `var` is scoped to the block only; it does not outlive it. This is
///       the opposite of \ref TRY, which keeps `var` in the enclosing scope.
///
/// \code
/// auto res = compute();
/// if_err(err, res, {
///     // err is E& here
/// })
/// else {
///     // res was Ok
/// }
/// \endcode
#define if_err(var, expr, ...)                    \
    if (auto _r_##var = (expr); _r_##var.isErr()) \
        if (auto &var = _r_##var.error(); true)   \
            __VA_ARGS__                           \
        else                                      \
        {                                         \
        }