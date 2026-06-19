#pragma once
#include "traits.h"
#include "compilers.h"

namespace core::sync
{


    template<typename T>
    concept Lockable = requires (T& lock){
        {lock.lock()} -> same_as<void>;
        {lock.unlock()} -> same_as<void>;
        {lock.tryLock()} -> same_as<bool>;
    };

    template<Lockable T>
    struct Lock{
        private:
        T& lock_obj;
        bool locked = false;
        public:
        MLW_FORCE_INLINE void unlock() {if(locked){ lock_obj.unlock(); locked = false;}}
        Lock() = delete;
        MLW_FORCE_INLINE explicit Lock(T& obj) : lock_obj(obj){
            lock_obj.lock();
            locked = true;
        }
        MLW_FORCE_INLINE ~Lock() {unlock();}
        //MLW_FORCE_INLINE static Lock adopt(T& obj)
        MLW_FORCE_INLINE void /*needs to be a options but i dont have that jet*/ tryLock(T& obj){/*do stuf*/}

        //mave move poseble?
            Lock(const Lock &) = delete;
            Lock &operator=(const Lock &) = delete;
            Lock(Lock &&) = delete;
            Lock &operator=(Lock &&) = delete;
    }
} // namespace core::sync

