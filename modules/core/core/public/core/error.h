#pragma once

#define TRY(var, res)                                             \
    auto _try_##var = (res);                                      \
    if (_try_##var.isErr()) return core::Err{_try_##var.takeError()}; \
    auto var = _try_##var.takeValue()

#if defined(MLW_GCC) || defined(MLW_CLANG)
// inline-expression sugar: auto f = TRY_EXPR(open_file("x"));
#define TRY_EXPR(res)                                             \
    ({                                                            \
        auto _try_r = (res);                                      \
        if (_try_r.isErr()) return core::Err{_try_r.takeError()}; \
        _try_r.takeValue();                                       \
    })
#endif

// if_ok(name, expr, {...})  name is bound to the VALUE (T&) inside the block
// optional trailing else runs on the error case
#define if_ok(var, expr, ...)                    \
    if (auto _r_##var = (expr); _r_##var.isOk()) \
        if (auto &var = _r_##var.value(); true)  \
            __VA_ARGS__                          \
        else                                     \
        {                                        \
        }

// if_err(var, expr, {...}) name is bound to the ERROR (E&)
#define if_err(var, expr, ...)                    \
    if (auto _r_##var = (expr); _r_##var.isErr()) \
        if (auto &var = _r_##var.error(); true)   \
            __VA_ARGS__                           \
        else                                      \
        {                                         \
        }
