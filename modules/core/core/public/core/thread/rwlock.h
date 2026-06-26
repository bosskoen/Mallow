#pragma once
#include "spinlock.h"

namespace core::sync
{
    struct RWLock
    {
        Atomic<int32> readers{0};
        Atomic<int32> writers{0};
        spin_lock::MCS writer_queue{};

        RWLock() = default;
        ~RWLock()
        {
            if (readers.load(MemoryOrder::Relaxed) != 0 || writers.load(MemoryOrder::Relaxed) != 0)
            {
                panic("RWLock lock destroyed with active writers or readers");
            }
        }
        RWLock(const RWLock &) = delete;
        RWLock &operator=(const RWLock &) = delete;
        RWLock(RWLock &&) = delete;
        RWLock &operator=(RWLock &&) = delete;

        void readLock()
        {
            while (true)
            {
                while (writers.load(MemoryOrder::Acquire) > 0)
                {
                    MLW_CPU_PAUSE();
                }
                readers.fetchAdd(1, MemoryOrder::Acquire);

                if (writers.load(MemoryOrder::Acquire) == 0)
                    return;

                readers.fetchSub(1, MemoryOrder::Release);
            }
        }

        MLW_FORCE_INLINE void readUnlock()
        {
            readers.fetchSub(1, MemoryOrder::Release);
        }

        void writeLock(spin_lock::MCS::Node &node)
        {
            writers.fetchAdd(1, MemoryOrder::Acquire); // block new readers
            writer_queue.lock(node);                   // queue behind other writers
            while (readers.load(MemoryOrder::Acquire) > 0)
                MLW_CPU_PAUSE(); // drain existing readers
        }

        MLW_FORCE_INLINE void writeUnlock(spin_lock::MCS::Node &node)
        {
            writer_queue.unlock(node);
            writers.fetchSub(1, MemoryOrder::Release);
        }
    };

    struct WriteLock
    {

    private:
        RWLock &lock_obj;
        bool locked = false;
        spin_lock::MCS::Node node{};

        //  struct AdoptTag{};
        // MLW_FORCE_INLINE Lock(T &obj, AdoptTag) : lock_obj(obj), locked(true) {}

        // friend class core::Optional<Lock<T>>;

    public:
        MLW_FORCE_INLINE bool isHeld() const { return locked; }
        MLW_FORCE_INLINE void unlock()
        {
            if (locked)
            {
                lock_obj.writeUnlock(node);
                locked = false;
            }
        }
        MLW_FORCE_INLINE void relock()
        {
            if (locked)
                return;
            lock_obj.writeLock(node);
            locked = true;
        }
        WriteLock() = delete;
        MLW_FORCE_INLINE explicit WriteLock(RWLock &obj) : lock_obj(obj)
        {
            lock_obj.writeLock(node);
            locked = true;
        }
        MLW_FORCE_INLINE ~WriteLock() { unlock(); }

        // MLW_FORCE_INLINE static void tryLock(T &obj, core::Optional<Lock<T>>& result)
        // {
        //     result.reset();
        //     if (obj.tryLock())
        //         result.emplace(obj, AdoptTag{});
        // }

        WriteLock(const WriteLock &) = delete;
        WriteLock &operator=(const WriteLock &) = delete;
        WriteLock(WriteLock &&) = delete;
        WriteLock &operator=(WriteLock &&) = delete;
    };

    struct ReadLock
    {

    private:
        RWLock &lock_obj;
        bool locked = false;

        //  struct AdoptTag{};
        // MLW_FORCE_INLINE Lock(T &obj, AdoptTag) : lock_obj(obj), locked(true) {}

        // friend class core::Optional<Lock<T>>;

    public:
        MLW_FORCE_INLINE bool isHeld() const { return locked; }
        MLW_FORCE_INLINE void unlock()
        {
            if (locked)
            {
                lock_obj.readUnlock();
                locked = false;
            }
        }
        MLW_FORCE_INLINE void relock()
        {
            if (locked)
                return;
            lock_obj.readLock();
            locked = true;
        }
        ReadLock() = delete;
        MLW_FORCE_INLINE explicit ReadLock(RWLock &obj) : lock_obj(obj)
        {
            lock_obj.readLock();
            locked = true;
        }
        MLW_FORCE_INLINE ~ReadLock() { unlock(); }

        // MLW_FORCE_INLINE static void tryLock(T &obj, core::Optional<Lock<T>>& result)
        // {
        //     result.reset();
        //     if (obj.tryLock())
        //         result.emplace(obj, AdoptTag{});
        // }

        ReadLock(const ReadLock &) = delete;
        ReadLock &operator=(const ReadLock &) = delete;
        ReadLock(ReadLock &&) = delete;
        ReadLock &operator=(ReadLock &&) = delete;
    };
} // namespace core::sync
