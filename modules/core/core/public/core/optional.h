#pragma once
#include "macro.h"
#include "traits.h"

/// \file
/// \brief Optional value container for the freestanding core.
///
/// `Optional<T>` represents either a value of type `T` or no value at all.
/// The reference specialization `Optional<T&>` stores a non-owning reference.

namespace core
{

    /// \brief Tag type to construct an Optional's value in-place.
    /// \see Optional::Optional(OptionalInPlace, Args&&...)
    /// \see Optional::emplace
    inline constexpr struct OptionalInPlace
    {
    } optional_in_place{};

    // forward declare all specializations
    template <typename T>
    class Optional;

    template <typename T>
    class Optional<T &>;

    /// \brief Disallowed: storing an rvalue reference would dangle.
    ///
    /// Rvalue references are not a storable category here; using this
    /// specialization is a hard compile error.
    template <typename T>
    class Optional<T &&>
    {
        static_assert(sizeof(T) == 0, "Optional<T&&> is disallowed — storing rvalue references is unsafe");
    };

    /// \brief Disallowed: a raw pointer already encodes absence as `nullptr`.
    ///
    /// `Optional<T*>` would have two ways to spell "empty" (a null pointer and
    /// a None state), which is ambiguous. Use `Optional<T>` for an owned value,
    /// or a `NonNull` wrapper when you need a guaranteed-non-null pointer.
    template <typename T>
    class Optional<T *>
    {
        static_assert(sizeof(T) == 0,
                      "Optional<T*> is disallowed — a raw pointer already encodes None as nullptr. "
                      "Use Optional<T> or a NonNull wrapper instead.");
    };

    /// \ingroup formattable
    /// \brief An optional value that may contain a `T`.
    ///
    /// Stores the value inline (no heap) in aligned storage plus a presence
    /// flag. To hold a type that is neither copyable nor movable, construct it
    /// in place with \ref optional_in_place or \ref emplace.
    ///
    /// \tparam T The stored value type.
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

        /// \brief Construct an empty Optional (None).
        Optional() : has_value(false) {}

        /// \brief Construct an empty Optional (None) from `nullptr`.
        Optional(decltype(nullptr)) : has_value(false) {}

        /// \brief Construct the contained `T` in place from `args`.
        ///
        /// No copy or move of `T` occurs, so this is the only way to store a
        /// type that is neither copyable nor movable (e.g. `Lock<MCS>`). Select
        /// it with the \ref optional_in_place tag.
        ///
        /// \param args Forwarded to `T`'s constructor.
        template <typename... Args>
        Optional(OptionalInPlace, Args &&...args)
            requires core::is_constructible_v<T, Args...>
            : has_value(true)
        {
            new (storage) T(forward<Args>(args)...);
        }

        /// \brief Construct an Optional containing a copy of `val`.
        Optional(const T &val)
            requires core::is_copy_constructible_v<T>
            : has_value(true)
        {
            new (storage) T(val);
        }

        /// \brief Construct an Optional containing `val` by moving.
        Optional(T &&val)
            requires core::is_move_constructible_v<T>
            : has_value(true)
        {
            new (storage) T(move(val));
        }

        // ── copy / move of Optional ───────────────────────────────────────

        /// \brief Copy-construct from another Optional.
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

        /// \brief Move-construct from another Optional.
        /// \note The source is left empty (None) afterward. This differs from
        ///       std::optional, whose moved-from source stays engaged.
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

        /// \brief Copy-assign from another Optional.
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

        /// \brief Move-assign from another Optional.
        /// \note The source is left empty (None) afterward, as with the move
        ///       constructor.
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

        /// \brief Reset to empty (None).
        Optional &operator=(decltype(nullptr)) noexcept
        {
            reset();
            return *this;
        }

        // ── destructor ────────────────────────────────────────────────────

        /// \brief Destroy the contained value if present.
        ~Optional() { reset(); }

        // ── in-place construction ─────────────────────────────────────────

        /// \brief Construct a new `T` in place, replacing any current value.
        ///
        /// Destroys the existing value first (if any), then constructs `T`
        /// directly inside storage — no move required — so this works for
        /// non-movable types. Returns a reference to the new value.
        ///
        /// \param args Forwarded to `T`'s constructor.
        template <typename... Args>
        T &emplace(Args &&...args)
        {
            reset();
            new (storage) T(forward<Args>(args)...);
            has_value = true;
            return *ptr();
        }

        // ── state ─────────────────────────────────────────────────────────

        /// \brief Returns true if a value is present.
        bool isSome() const { return has_value; }

        /// \brief Returns true if no value is present.
        bool isNone() const { return !has_value; }

        /// \brief Allows `if (opt)` to test presence.
        explicit operator bool() const { return has_value; }

        /// \brief Destroy the contained value if present and become empty.
        void reset()
        {
            if (has_value)
            {
                ptr()->~T();
                has_value = false;
            }
        }

        // ── value access ──────────────────────────────────────────────────

        /// \brief Access the contained value. \pre `isSome()`; unchecked.
        T &operator*() { return *ptr(); }
        /// \copydoc operator*()
        const T &operator*() const { return *ptr(); }
        /// \brief Access members of the contained value. \pre `isSome()`; unchecked.
        T *operator->() { return ptr(); }
        /// \copydoc operator->()
        const T *operator->() const { return ptr(); }

        /// \brief Return the contained value, or panic if empty.
        T &unwrap()
        {
            if (!has_value)
                panic("unwrap on None");
            return *ptr();
        }

        /// \copydoc unwrap()
        const T &unwrap() const
        {
            if (!has_value)
                panic("unwrap on None");
            return *ptr();
        }

        /// \brief Return the contained value, or panic with `msg` if empty.
        T &expect(const char *msg)
        {
            if (!has_value)
                panic(msg);
            return *ptr();
        }

        /// \copydoc expect()
        const T &expect(const char *msg) const
        {
            if (!has_value)
                panic(msg);
            return *ptr();
        }

        /// \brief Return the contained value, or `default_val` if empty.
        /// \note Consumes the contained value: on success it is moved out and
        ///       the Optional is left empty (None).
        T unwrapOr(T default_val)
            requires core::is_move_constructible_v<T>
        {
            if (has_value)
            {
                T val = move(*ptr());
                reset();
                return val;
            }
            return move(default_val);
        }

        /// \brief Return the contained value, or the result of `fn()` if empty.
        /// \note Consumes the contained value: on success it is moved out and
        ///       the Optional is left empty (None).
        template <typename F>
        T unwrapOrElse(F &&fn)
            requires core::is_invocable_v<F> && core::is_same_v<core::invoke_result_t<F>, T>
        {
            if (has_value)
            {
                T val = move(*ptr());
                reset();
                return val;
            }
            return forward<F>(fn)();
        }

        /// \brief Move the contained value out and become empty. Panics if empty.
        T take()
            requires core::is_move_constructible_v<T>
        {
            if (!has_value)
                panic("take on None");
            T val = move(*ptr());
            reset();
            return val;
        }

        /// \brief Format as `None` or the contained value.
        /// \note Customization point: invoked by the format core, not called
        ///       directly. Enabled only when `T` is formattable into `Buf`.
        template<core::FormatBuffer Buf>
            requires core::FormattableValue<T, Buf>
        void format(Buf& buffer) const{
            if(isSome()){
                mlw_write(buffer, "{}",  *ptr());
            }else{
                mlw_write(buffer, "None");
            }
        }
    };

    // ── reference specialization ──────────────────────────────────────────

    /// \ingroup formattable
    /// \brief An optional non-owning reference to a `T`.
    ///
    /// Stores a pointer internally: no ownership, no destructor, and copies
    /// freely. Empty is represented by a null internal pointer.
    ///
    /// \tparam T The referenced type.
    template <typename T>
    class Optional<T &>
    {
    private:
        T *ptr = nullptr;

    public:
        /// \brief Construct an empty Optional (None).
        Optional() : ptr(nullptr) {}
        /// \brief Construct an empty Optional (None) from `nullptr`.
        Optional(decltype(nullptr)) : ptr(nullptr) {}
        /// \brief Construct referring to `ref`.
        Optional(T &ref) : ptr(&ref) {}

        // references copy freely
        Optional(const Optional &) = default;
        Optional &operator=(const Optional &) = default;
        Optional(Optional &&) = default;
        Optional &operator=(Optional &&) = default;

        ~Optional() = default; // no ownership — nothing to destroy

        /// \brief Rebind to refer to `ref` (does not touch the previous referent).
        void emplace(T &ref) { ptr = &ref; }

        /// \brief Become empty (None); the referent is untouched.
        void reset() { ptr = nullptr; }

        /// \brief Returns true if referring to something.
        bool isSome() const { return ptr != nullptr; }
        /// \brief Returns true if empty.
        bool isNone() const { return ptr == nullptr; }
        /// \brief Allows `if (opt)` to test presence.
        explicit operator bool() const { return ptr != nullptr; }

        /// \brief Access the referent. \pre `isSome()`; unchecked.
        T &operator*() { return *ptr; }
        /// \brief Access members of the referent. \pre `isSome()`; unchecked.
        T *operator->() { return ptr; }

        /// \brief Return the referent, or panic if empty.
        T &unwrap()
        {
            if (!ptr)
                panic("unwrap on None");
            return *ptr;
        }

        /// \brief Return the referent, or panic with `msg` if empty.
        T &expect(const char *msg)
        {
            if (!ptr)
                panic(msg);
            return *ptr;
        }

        /// \brief Return the referent, or `default_val` if empty.
        /// \note Non-consuming: returns a reference; the Optional is unchanged.
        T &unwrapOr(T &default_val)
        {
            return ptr ? *ptr : default_val;
        }

        /// \brief Format as `None` or the referent.
        /// \note Customization point: invoked by the format core, not called
        ///       directly. Enabled only when `T` is formattable into `Buf`.
        template<core::FormatBuffer Buf>
            requires core::FormattableValue<T, Buf>
        void format(Buf& buffer) const{
            if(isSome()){
                mlw_write(buffer, "{}",  *ptr);
            }else{
                mlw_write(buffer, "None");
            }
        }
    };

} // namespace core