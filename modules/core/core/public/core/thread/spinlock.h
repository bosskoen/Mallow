#pragma once

#include "atomic.h"
#include "lock.h"

namespace core::sync
{
    namespace spin_lock
    {
        struct TTAS
        {
        private:
            Atomic<bool> lock_value{false};

        public:
            TTAS() = default;
            ~TTAS()
            {
                if (lock_value.load() == true)
                {
                    panic("TTAS spin lock destroyed while locked");
                }
            };
            TTAS(const TTAS &) = delete;
            TTAS &operator=(const TTAS &) = delete;
            TTAS(TTAS &&) = delete;
            TTAS &operator=(TTAS &&) = delete;

            MLW_FORCE_INLINE bool tryLock()
            {
                bool expected = false;
                if (lock_value.compareExchangeStrong(expected, true, MemoryOrder::Acquire, MemoryOrder::Relaxed))
                {
                    return true;
                }
                return false;
            };
            MLW_FORCE_INLINE void lock()
            {
                while (true)
                {
                    while (lock_value.load(MemoryOrder::Relaxed))
                        MLW_CPU_PAUSE();

                    bool expected = false;
                    if (lock_value.compareExchangeWeak(expected, true, MemoryOrder::Acquire, MemoryOrder::Relaxed))
                    {
                        return;
                    }
                }
            };
            MLW_FORCE_INLINE void unlock()
            {
                lock_value.store(false, MemoryOrder::Relaxed);
            }
        };

        struct TicketLock
        {
        private:
            Atomic<uint32> next_ticket{0};
            Atomic<uint32> now_serving{0};

        public:
            TicketLock() = default;
            ~TicketLock()
            {
                uint32 serving = now_serving.load(MemoryOrder::Relaxed);
                uint32 next = next_ticket.load(MemoryOrder::Relaxed);
                if (next != serving)
                {
                    panic("TicketLock destroyed while locked or with waiters");
                }
            }
            TicketLock(const TicketLock &) = delete;
            TicketLock &operator=(const TicketLock &) = delete;
            TicketLock(TicketLock &&) = delete;
            TicketLock &operator=(TicketLock &&) = delete;

            MLW_FORCE_INLINE bool tryLock() noexcept
            {
                uint32 serving = now_serving.load(MemoryOrder::Relaxed);
                return next_ticket.compareExchangeStrong(serving, serving + 1, MemoryOrder::Acquire, MemoryOrder::Relaxed);
            }
            MLW_FORCE_INLINE void lock()
            {
                uint32 my_ticket = next_ticket.fetchAdd(1, MemoryOrder::Relaxed);
                while (now_serving.load(MemoryOrder::Acquire) != my_ticket)
                    MLW_CPU_PAUSE();
            };
            MLW_FORCE_INLINE void unlock()
            {
                now_serving.fetchAdd(1, MemoryOrder::Release);
            };
        };

        struct MCS
        {
            struct Node
            {
                Atomic<Node *> next{nullptr};
                Atomic<bool> locked{false}; // true if the thread is spinning
            };

        private:
            Atomic<Node *> tail{nullptr};

        public:
            MLW_FORCE_INLINE void lock(Node &me) noexcept
            {
                me.next.store(nullptr, MemoryOrder::Relaxed);
                me.locked.store(true, MemoryOrder::Relaxed);

                Node *prev = tail.exchange(&me, MemoryOrder::AcqRel);
                if (prev != nullptr)
                {
                    prev->next.store(&me, MemoryOrder::Release);
                    while (me.locked.load(MemoryOrder::Acquire))
                        MLW_CPU_PAUSE();
                }
            }

            MLW_FORCE_INLINE void unlock(Node &me) noexcept
            {
                Node *next = me.next.load(MemoryOrder::Acquire);
                if (next == nullptr)
                {
                    Node *expected = &me;
                    if (tail.compareExchangeStrong(expected, static_cast<Node *>(nullptr),
                                                   MemoryOrder::Release, MemoryOrder::Relaxed))
                        return;

                    while ((next = me.next.load(MemoryOrder::Acquire)) == nullptr)
                        MLW_CPU_PAUSE();
                }
                next->locked.store(false, MemoryOrder::Release);
            }

            MCS() = default;
            ~MCS() {
    if (tail.load(MemoryOrder::Relaxed) != nullptr) {
        panic("MCS lock destroyed with active owner or waiters");
    }
}
            MCS(const MCS &) = delete;
            MCS &operator=(const MCS &) = delete;
            MCS(MCS &&) = delete;
            MCS &operator=(MCS &&) = delete;

            MLW_FORCE_INLINE bool tryLock(Node &me) noexcept
            {
                Node *expected = nullptr;
                me.next.store(nullptr, MemoryOrder::Relaxed);
                me.locked.store(false, MemoryOrder::Relaxed);
                return tail.compareExchangeStrong(expected, &me,
                                                  MemoryOrder::Acquire, MemoryOrder::Relaxed);
            }
        };
    } // namespace spin_lock

    // specialization — carries the node
    template <>
    struct Lock<spin_lock::MCS>
    {
    private:
        spin_lock::MCS &lock_obj;
        spin_lock::MCS::Node me; // lives on the stack here, exactly as long as the lock
        bool locked = false;

        struct AdoptTag
        {
        };
        MLW_FORCE_INLINE Lock(spin_lock::MCS &obj, AdoptTag) : lock_obj(obj), locked(true) {}
        friend Optional<Lock<spin_lock::MCS>>;

    public:
        MLW_FORCE_INLINE void unlock()
        {
            if (locked)
            {
                lock_obj.unlock(me);
                locked = false;
            }
        }

        MLW_FORCE_INLINE void relock()[
            if(locked) return;
            lock_obj.lock(me);
            locked = true;
        ]
        Lock() = delete;

        MLW_FORCE_INLINE explicit Lock(spin_lock::MCS &obj) : lock_obj(obj)
        {
            lock_obj.lock(me);
            locked = true;
        }

        MLW_FORCE_INLINE ~Lock()
        {
            unlock();
        }

        MLW_FORCE_INLINE static void tryLock(spin_lock::MCS &obj, Optional<Lock<spin_lock::MCS>> &result)
        {
            result.reset();
            result.emplace(obj, AdoptTag{});
            if (!result->lock_obj.tryLock(result->me))
            {
                result->locked = false;
                result.reset();
            }
        }

        // try lock
        Lock(const Lock &) = delete;
        Lock &operator=(const Lock &) = delete;
        Lock(Lock &&) = delete;
        Lock &operator=(Lock &&) = delete;
    };

} // namespace core::sync
