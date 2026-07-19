/// \file
/// \brief Type-safe tagged union (\ref core::Variant) for the freestanding core.
///
/// A hand-rolled analogue of std::variant: no RTTI, no exceptions, no heap.
/// Storage is an inline aligned byte buffer sized to the largest alternative,
/// plus a one-byte discriminant.

#pragma once
#include "traits.h"
#include "compilers.h"
#include "io/format.h"

namespace core
{
    namespace detail
    {
        template <typename U, typename... Ts>
        inline constexpr bool variant_contains = (is_same_v<U, Ts> || ...);

        template <typename U, typename T, typename... R>
        constexpr uint8 variant_index_of()
        {
            if constexpr (is_same_v<U, T>)
                return 0;
            else
                return static_cast<uint8>(1 + variant_index_of<U, R...>()); // ill-formed if U absent
        }
        template <typename T, typename... R>
        constexpr bool appears_once = (usize(is_same_v<T, R>) + ... + usize(0)) == 1;

        template <usize I, typename T, typename... R>
        struct variant_nth
        {
            using type = typename variant_nth<I - 1, R...>::type;
        };
        template <typename T, typename... R>
        struct variant_nth<0, T, R...>
        {
            using type = T;
        };
        template <usize I, typename... Ts>
        using variant_nth_t = typename variant_nth<I, Ts...>::type;

        // T with the same const-ness as S, keeps the shared dispatcher const-correct
        template <typename S, typename T>
        struct variant_like
        {
            using type = T;
        };
        template <typename S, typename T>
        struct variant_like<const S, T>
        {
            using type = const T;
        };
        template <typename S, typename T>
        using variant_like_t = typename variant_like<S, T>::type;

        template <typename... Ts>
        constexpr usize variant_max_size()
        {
            usize a[] = {usize(0), sizeof(Ts)...};
            usize m = 0;
            for (usize x : a)
                if (x > m)
                    m = x;
            return m;
        }
        template <typename... Ts>
        constexpr usize variant_max_align()
        {
            usize a[] = {usize(1), alignof(Ts)...};
            usize m = 1;
            for (usize x : a)
                if (x > m)
                    m = x;
            return m;
        }
    } // namespace detail

    /// \brief Tag type selecting Variant's in-place constructor for \p T. \see variant_in_place
    template <typename T>
    struct VariantInPlace
    {
    };
    /// \brief Pass as the first arg to Variant to build \p T in place: `variant_in_place<T>`. \see Variant::Variant(VariantInPlace<T>, Args&&...)
    template <typename T>
    inline constexpr VariantInPlace<T> variant_in_place{};

    /// \ingroup formattable
    /// \brief A type-safe union holding exactly one value from \p Ts at a time.
    ///
    /// Storage is inline: an `alignas`-correct byte buffer sized to the largest
    /// alternative, plus a `uint8` discriminant (hence at most 255 alternatives).
    /// The copy constructor, move constructor, and both assignments exist only
    /// when *every* alternative supports the matching operation; otherwise that
    /// special member is removed via `requires`, so the Variant is correctly
    /// non-copyable and/or non-movable.
    ///
    /// \tparam Ts  The alternative types. Each must be non-void and non-reference,
    ///             and all must be pairwise unique. At least one is required.
    ///
    /// \note There is no default constructor: a Variant always holds a value.
    /// \note To store a type that is neither copyable nor movable, use the
    ///       in-place constructor together with \ref variant_in_place.
    template <typename... Ts>
    class Variant
    {
        static constexpr usize N = sizeof...(Ts);

        static_assert(N >= 1, "Variant needs at least one alternative");
        static_assert(N <= NumericLimits<uint8>::max, "Variant: too many alternatives for uint8 tag");
        static_assert((!is_reference_v<Ts> && ...), "Variant cannot hold reference types; use a pointer");
        static_assert((!is_same_v<Ts, void> && ...), "Variant cannot hold void");
        static_assert((detail::appears_once<Ts, Ts...> && ...), "Variant alternatives must be unique");

        alignas(detail::variant_max_align<Ts...>()) uint8 buf[detail::variant_max_size<Ts...>()];
        uint8 idx;

        // single dispatcher used by dtor / copy / move / public visit.
        // Self is deduced as `Variant` or `const Variant`; variant_like_t carries
        // that const-ness onto the live member, so const visits get `const T&`.
        template <uint8 I = 0, typename Self, typename F>
        static void run(Self &self, F &&f)
        {
            if constexpr (I < N)
            {
                if (self.idx == I)
                {
                    using T = detail::variant_nth_t<I, Ts...>;
                    using LT = detail::variant_like_t<Self, T>;
                    f(*MLW_LAUNDER(reinterpret_cast<LT *>(self.buf)));
                    return;
                }
                run<static_cast<uint8>(I + 1)>(self, static_cast<F &&>(f));
            }
        }

        void destroy()
        {
            run(*this, [](auto &x)
                { using T = remove_ref_t<decltype(x)>; x.~T(); });
        }

    public:
        // --- construction ---------------------------------------------------

        Variant() = delete; ///< No default constructor: a Variant always holds a value.

        /// \brief Construct by storing an alternative; copies an lvalue, moves an rvalue.
        /// \tparam U  Deduced argument type; its decayed form \c D must be one of \p Ts.
        /// This overload excludes \c Variant itself, so it never shadows the copy or
        /// move constructors, and is disabled for any type not in \p Ts.
        template <typename U, typename D = remove_const_t<remove_ref_t<U>>>
            requires(!is_same_v<D, Variant> && detail::variant_contains<D, Ts...>)
        Variant(U &&v) : idx(detail::variant_index_of<D, Ts...>())
        {
            new (buf) D(core::forward<U>(v));
        }

        /// \brief Construct \p T in place from \p args, with no copy or move of \p T.
        /// This is the only constructor able to store a type that is neither
        /// copyable nor movable.
        /// \tparam T     The alternative to build; must be one of \p Ts.
        /// \param  tag   Selects this overload; use \ref variant_in_place.
        /// \param  args  Forwarded to T's constructor.
        template <typename T, typename... Args>
            requires detail::variant_contains<T, Ts...>
        explicit Variant(VariantInPlace<T>, Args &&...args) : idx(detail::variant_index_of<T, Ts...>())
        {
            static_assert(is_constructible_v<T, Args...>, "Variant: alternative is not constructible from these arguments");
            new (buf) T(core::forward<Args>(args)...);
        }

        // --- special members (conditionally present) ------------------------
        // Each `requires` is a fold over the members: if any member type lacks
        // the operation, the whole special member is removed -> the Variant is
        // correctly non-copyable / non-movable. A declared-but-constrained-away
        // copy/move ctor still suppresses any implicit bitwise version.

        /// \brief Copy constructor. Present only if every alternative is copy-constructible.
        Variant(const Variant &o)
            requires(is_copy_constructible_v<Ts> && ...)
            : idx(o.idx)
        {
            run(o, [this](const auto &x)
                { using T = remove_ref_t<decltype(x)>; new (buf) T(x); });
        }

        /// \brief Move constructor. Present only if every alternative is move-constructible.
        Variant(Variant &&o)
            requires(is_move_constructible_v<Ts> && ...)
            : idx(o.idx)
        {
            run(o, [this](auto &x)
                { using T = remove_ref_t<decltype(x)>; new (buf) T(core::move(x)); });
        }

        /// \brief Copy assignment (destroy-then-construct).
        /// Requires only *constructible*, not assignable: the old value is destroyed
        /// first, and with no exceptions there is nothing to unwind.
        Variant &operator=(const Variant &o)
            requires(is_copy_constructible_v<Ts> && ...)
        {
            if (this == &o)
                return *this;
            destroy();
            idx = o.idx;
            run(o, [this](const auto &x)
                { using T = remove_ref_t<decltype(x)>; new (buf) T(x); });
            return *this;
        }

        /// \brief Move assignment (destroy-then-construct). See copy assignment for rationale.
        Variant &operator=(Variant &&o)
            requires(is_move_constructible_v<Ts> && ...)
        {
            if (this == &o)
                return *this;
            destroy();
            idx = o.idx;
            run(o, [this](auto &x)
                { using T = remove_ref_t<decltype(x)>; new (buf) T(core::move(x)); });
            return *this;
        }

        /// \brief Destructor; runs the active alternative's destructor
        ~Variant() { destroy(); }

        // --- access by real type name ---------------------------------------

        /// \brief Returns true iff the active alternative is \p T.
        template <typename T>
        bool is() const { return idx == detail::variant_index_of<T, Ts...>(); }

        /// \brief Access the active value as \p T, unchecked.
        /// \pre `is<T>()` is true. Otherwise the behaviour is undefined.
        ///      Guard with is<T>() first, or prefer visit().
        template <typename T>
        T &get() { return *MLW_LAUNDER(reinterpret_cast<T *>(buf)); }
        /// \copydoc get()
        template <typename T>
        const T &get() const { return *MLW_LAUNDER(reinterpret_cast<const T *>(buf)); }

        /// \brief The zero-based tag of the active alternative (its position in \p Ts).
        uint8 index() const { return idx; }

        /// \brief Invoke \p f with a reference to the active alternative.
        /// \param f  Callable accepting every alternative as `T&`.
        template <typename F>
            requires(is_invocable_v<F, Ts &> && ...)
        void visit(F &&f)
        {
            run(*this, static_cast<F &&>(f));
        }

        /// \brief Const overload of visit(); \p f receives each alternative as `const T&`.
        template <typename F>
            requires(is_invocable_v<F, const Ts &> && ...)
        void visit(F &&f) const
        {
            run(*this, static_cast<F &&>(f));
        }

        /// \brief Formatting customization point; not called directly.
        /// Invoked by the format core when a Variant is printed. Formats whichever
        /// alternative is currently active. Enabled only when every alternative is
        /// itself formattable into \p Buffer.
        /// \note You normally format a Variant via print / the format API, not by
        ///       calling this. See \ref FormattableValue.
        template <FormatBuffer Buffer>
            requires(FormattableValue<Ts, Buffer> && ...)
        void format(Buffer &buffer) const
        {
            visit([&buffer](const auto &x)
                  {
                // x is `const T&` for the live alternative; formatValue dispatches it
                // the same way the format core dispatches any value (member format,
                // ADL free format, or built-in for primitives)
                detail::formatValue(buffer, x); });
        }
    };
} // namespace core