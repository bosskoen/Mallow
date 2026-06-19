#pragma once
#include "traits.h"
#include "compilers.h"
#include "optional.h"

namespace core::sync
{

    template <typename T>
    concept Lockable = requires(T &lock) {
        { lock.lock() } -> same_as<void>;
        { lock.unlock() } -> same_as<void>;
        { lock.tryLock() } -> same_as<bool>;
    };

    template <Lockable T>
    struct Lock
    {
    private:
        T &lock_obj;
        bool locked = false;

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
        MLW_FORCE_INLINE void core::Optional<Lock<T>> tryLock(T &obj)
        {
            if (obj.tryLock())
            {
                
            }else{
                return Optional{nullptr};
            }
        }

        // mave move poseble?
        Lock(const Lock &) = delete;
        Lock &operator=(const Lock &) = delete;
        Lock(Lock &&) = delete;
        Lock &operator=(Lock &&) = delete;
    }
} // namespace core::sync
