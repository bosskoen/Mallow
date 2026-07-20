#pragma once

/// \file
/// \brief Generic RAII lock wrapper and lockability concept.
///
/// `core::sync::Lock<T>` provides an RAII guard for lockable objects. It
/// supports any type satisfying the `Lockable` concept, and there is one
/// explicit specialization for `Lock<spin_lock::MCS>` that stores the
/// per-thread MCS node on the stack.

#include "../traits.h"
#include "../compilers.h"
#include "../optional.h"

namespace core::sync
{

    /// \brief Concept for types usable with \ref Lock.
    ///
    /// A `Lockable` type must support `lock()`, `unlock()`, and `tryLock()`.
    template <typename T>
    concept Lockable = requires(T &lock) {
        { lock.lock() } -> same_as<void>;
        { lock.unlock() } -> same_as<void>;
        { lock.tryLock() } -> same_as<bool>;
    };

    /// \brief RAII lock guard for a lockable object.
    ///
    /// Acquires the lock on construction and releases it on destruction.
    /// Use `core::sync::Lock<T>` for normal scoped locking and only use the
    /// raw lock object directly when you need a custom locking pattern.
    template <typename T>
    struct Lock
    {

        static_assert(Lockable<T>,
                      "Lock<T> requires T to satisfy Lockable, "
                      "or provide an explicit specialization like Lock<spin_lock::MCS>");


    private:
        T &lock_obj;
        bool locked = false;

         struct AdoptTag{};
        MLW_FORCE_INLINE Lock(T &obj, AdoptTag) : lock_obj(obj), locked(true) {}

        friend class core::Optional<Lock<T>>;

    public:
        /// \brief Returns true if this guard currently owns the lock.
        MLW_FORCE_INLINE bool isHeld() const { return locked;}

        /// \brief Release the lock if it is currently held.
        MLW_FORCE_INLINE void unlock()
        {
            if (locked)
            {
                lock_obj.unlock();
                locked = false;
            }
        }

        /// \brief Re-acquire the lock if it was previously released.
        MLW_FORCE_INLINE void relock(){
            if (locked) return;
            lock_obj.lock();
            locked = true;
        }

        Lock() = delete;

        /// \brief Acquire the lock on construction.
        MLW_FORCE_INLINE explicit Lock(T &obj) : lock_obj(obj)
        {
            lock_obj.lock();
            locked = true;
        }

        /// \brief Release the lock on destruction.
        MLW_FORCE_INLINE ~Lock() { unlock(); }
        // MLW_FORCE_INLINE static Lock adopt(T& obj)

        /// \brief Attempt to acquire the lock and return the result in `result`.
        ///
        /// If locking succeeds, `result` contains an engaged `Lock<T>` guard.
        /// Otherwise `result` remains disengaged.
        MLW_FORCE_INLINE static void tryLock(T &obj, core::Optional<Lock<T>>& result)
        {
            result.reset();
            if (obj.tryLock())
                result.emplace(obj, AdoptTag{});
        }

        // Non-movable by design. The MCS specialization embeds an MCS::Node whose
        // address the lock records while held; moving the guard would leave the lock
        // pointing at a stale node. Move is deleted on the primary too so every
        // Lock<T> has the same ownership contract. Return via Optional / out-param
        // (see tryLock), not by value.
        Lock(const Lock &) = delete;
        Lock &operator=(const Lock &) = delete;
        Lock(Lock &&) = delete;
        Lock &operator=(Lock &&) = delete;
    };
} // namespace core::sync
