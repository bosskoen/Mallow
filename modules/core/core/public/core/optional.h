#pragma once
#include "macro.h"
#include "traits.h"

namespace core
{

    inline constexpr struct OptionalInPlace
    {
    } optional_in_place{};

    // forward declare all specializations
    template <typename T>
    class Optional;

    template <typename T>
    class Optional<T &>;

    // rvalue references disallowed — makes no sense to store a dangling rvalue
    template <typename T>
    class Optional<T &&>
    {
        static_assert(sizeof(T) == 0, "Optional<T&&> is disallowed — storing rvalue references is unsafe");
    };

    // pointers disallowed — nullptr already encodes absence, Optional<T*> is ambiguous
    template <typename T>
    class Optional<T *>
    {
        static_assert(sizeof(T) == 0,
                      "Optional<T*> is disallowed — a raw pointer already encodes None as nullptr. "
                      "Use Optional<T> or a NonNull wrapper instead.");
    };

    // ── value type ────────────────────────────────────────────────────────
    template <typename T>
    class Optional
    {
    private:
        alignas(T) char storage[sizeof(T)];
        bool has_value = false;

        T* ptr() { return MLW_LAUNDER(reinterpret_cast<T*>(&storage)); }
        const T* ptr() const { return MLW_LAUNDER(reinterpret_cast<const T*>(&storage)); }

    public:
        // ── constructors ─────────────────────────────────────────────────

        Optional() : has_value(false) {}
        Optional(decltype(nullptr)) : has_value(false) {}

        // then in Optional:
        template <typename... Args>
        Optional(OptionalInPlace, Args &&...args)
            requires core::is_constructible_v<T, Args...>
            : has_value(true)
        {
            new (storage) T(forward<Args>(args)...);
        }

        Optional(const T &val)
            requires core::is_copy_constructible_v<T>
            : has_value(true)
        {
            new (storage) T(val);
        }

        Optional(T &&val)
            requires core::is_move_constructible_v<T>
            : has_value(true)
        {
            new (storage) T(move(val));
        }

        // ── copy / move of Optional ───────────────────────────────────────

        Optional(const Optional &other)
            requires core::is_copy_constructible_v<T>
            : has_value(false)
        {
            if (other.has_value)
            {
                new (storage) T(*other.ptr());
                has_value = true;
            }
        }

        Optional(Optional &&other)
            requires core::is_move_constructible_v<T>
            : has_value(false)
        {
            if (other.has_value)
            {
                new (storage) T(move(*other.ptr()));
                has_value = true;
                other.reset();
            }
        }

        // ── assignment ────────────────────────────────────────────────────

        Optional &operator=(const Optional &other)
            requires core::is_copy_constructible_v<T>
        {
            if (this != &other)
            {
                reset();
                if (other.has_value)
                {
                    new (storage) T(*other.ptr());
                    has_value = true;
                }
            }
            return *this;
        }

        Optional &operator=(Optional &&other)
            requires core::is_move_constructible_v<T>
        {
            if (this != &other)
            {
                reset();
                if (other.has_value)
                {
                    new (storage) T(move(*other.ptr()));
                    has_value = true;
                    other.reset();
                }
            }
            return *this;
        }

        Optional &operator=(decltype(nullptr)) noexcept
        {
            reset();
            return *this;
        }

        // ── destructor ────────────────────────────────────────────────────

        ~Optional() { reset(); }

        // ── in-place construction ─────────────────────────────────────────
        // key for non-movable types like Lock<MCS>
        // constructs directly inside storage — no move needed

        template <typename... Args>
        T &emplace(Args &&...args)
        {
            reset();
            new (storage) T(forward<Args>(args)...);
            has_value = true;
            return *ptr();
        }

        // ── state ─────────────────────────────────────────────────────────

        bool isSome() const { return has_value; }
        bool isNone() const { return !has_value; }
        explicit operator bool() const { return has_value; }

        void reset()
        {
            if (has_value)
            {
                ptr()->~T();
                has_value = false;
            }
        }

        // ── value access ──────────────────────────────────────────────────

        T &operator*() { return *ptr(); }
        const T &operator*() const { return *ptr(); }
        T *operator->() { return ptr(); }
        const T *operator->() const { return ptr(); }

        T &unwrap()
        {
            if (!has_value)
                panic("unwrap on None");
            return *ptr();
        }

        const T &unwrap() const
        {
            if (!has_value)
                panic("unwrap on None");
            return *ptr();
        }

        T &expect(const char *msg)
        {
            if (!has_value)
                panic(msg);
            return *ptr();
        }

        const T &expect(const char *msg) const
        {
            if (!has_value)
                panic(msg);
            return *ptr();
        }

        // takes by value so caller can pass a literal or moved default
        T unwrapOr(T default_val)
            requires core::is_move_constructible_v<T>
        {
            return has_value ? move(*ptr()) : move(default_val);
        }

        template <typename F>
        T unwrapOrElse(F &&fn)
            requires core::is_invocable_v<F> && core::is_same_v<core::invoke_result_t<F>, T>
        {
            return has_value ? move(*ptr()) : forward<F>(fn)();
        }

        // moves value out — leaves Optional as None
        T take()
            requires core::is_move_constructible_v<T>
        {
            if (!has_value)
                panic("take on None");
            T val = move(*ptr());
            reset();
            return val;
        }

        //
        template<core::FormatBuffer Buf>
           // requires core::FormattableValue<T, Buf> 
        void format(Buf& buffer) const{
            if(isSome()){
                mlw_write(buffer, "{}",  *ptr());
            }else{
                mlw_write(buffer, "None");
            }
        }
    };

    // ── reference specialization ──────────────────────────────────────────
    // stores a pointer internally — no ownership, no destructor needed

    template <typename T>
    class Optional<T &>
    {
    private:
        T *ptr = nullptr;

    public:
        Optional() : ptr(nullptr) {}
        Optional(decltype(nullptr)) : ptr(nullptr) {}
        Optional(T &ref) : ptr(&ref) {}

        // references copy freely
        Optional(const Optional &) = default;
        Optional &operator=(const Optional &) = default;
        Optional(Optional &&) = default;
        Optional &operator=(Optional &&) = default;

        ~Optional() = default; // no ownership — nothing to destroy

        // rebind the reference
        void emplace(T &ref) { ptr = &ref; }

        void reset() { ptr = nullptr; }

        bool isSome() const { return ptr != nullptr; }
        bool isNone() const { return ptr == nullptr; }
        explicit operator bool() const { return ptr != nullptr; }

        T &operator*() { return *ptr; }
        T *operator->() { return ptr; }

        T &unwrap()
        {
            if (!ptr)
                panic("unwrap on None");
            return *ptr;
        }

        T &expect(const char *msg)
        {
            if (!ptr)
                panic(msg);
            return *ptr;
        }

        T &unwrapOr(T &default_val)
        {
            return ptr ? *ptr : default_val;
        }

        //
        template<core::FormatBuffer Buf>
            //requires core::FormattableValue<T, Buf> 
        void format(Buf& buffer) const{
            if(isSome()){
                mlw_write(buffer, "{}",  *ptr);
            }else{
                mlw_write(buffer, "None");
            }
        }
    };

} // namespace core
