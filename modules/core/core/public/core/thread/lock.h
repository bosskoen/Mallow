#pragma once
#include "../traits.h"
#include "../compilers.h"
#include "../optional.h"

namespace core::sync
{

    template <typename T>
    concept Lockable = requires(T &lock) {
        { lock.lock() } -> same_as<void>;
        { lock.unlock() } -> same_as<void>;
        { lock.tryLock() } -> same_as<bool>;
    };

    template <typename T>
    struct Lock
    {

        static_assert(Lockable<T>,
                      "Lock<T> requires T to satisfy Lockable, "
                      "or provide an explicit specialization like Lock<MCS>");

    private:
        T &lock_obj;
        bool locked = false;

         struct AdoptTag{};
        MLW_FORCE_INLINE Lock(T &obj, AdoptTag) : lock_obj(obj), locked(true) {}

        friend class core::Optional<Lock<T>>;

    public:
        MLW_FORCE_INLINE void unlock()
        {
            if (locked)
            {
                lock_obj.unlock();
                locked = false;
            }
        }
        Lock() = delete;
        MLW_FORCE_INLINE explicit Lock(T &obj) : lock_obj(obj)
        {
            lock_obj.lock();
            locked = true;
        }
        MLW_FORCE_INLINE ~Lock() { unlock(); }
        // MLW_FORCE_INLINE static Lock adopt(T& obj)

        MLW_FORCE_INLINE static void tryLock(T &obj, core::Optional<Lock<T>>& result)
        {
            result.reset();
            if (obj.tryLock())
                result.emplace(obj, AdoptTag{});
        }

        // mave move poseble?
        Lock(const Lock &) = delete;
        Lock &operator=(const Lock &) = delete;
        Lock(Lock &&) = delete;
        Lock &operator=(Lock &&) = delete;
    };
} // namespace core::sync
