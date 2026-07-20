#pragma once

#include "atomic.h"
#include "lock.h"

/// \file
/// \brief Spinlock family: TTAS, TicketLock, and MCS, plus the Lock<MCS> guard.

namespace core::sync
{
    /// \namespace core::sync::spin_lock
    /// \brief Busy-wait locks trading fairness for footprint.
    ///
    /// A menu along one axis. \ref spin_lock::TTAS is the smallest but unfair;
    /// \ref spin_lock::TicketLock adds strict FIFO fairness; \ref spin_lock::MCS
    /// adds cache-local spinning so it scales under contention, at the cost of a
    /// caller-provided queue node. All three busy-spin (no sleeping), so they
    /// suit short critical sections only, and all abort if destroyed while held.
    namespace spin_lock
    {
        /// \brief Test-and-test-and-set spinlock: one bool, no fairness.
        ///
        /// The smallest lock here. It spins on a relaxed load (the "test") and
        /// only attempts the atomic exchange (the "test-and-set") once the lock
        /// looks free, which keeps the contended cache line quiet while waiting.
        ///
        /// \warning No ordering among waiters: under contention a thread can be
        ///          starved indefinitely. Use only for very short, low-
        ///          contention sections; reach for \ref TicketLock if you need
        ///          fairness.
        struct TTAS
        {
        private:
            Atomic<bool> lock_value{false};

        public:
            TTAS() = default;

            /// \brief Destroy the lock. Panics if still held.
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

            /// \brief Try to acquire without spinning. Returns true on success.
            MLW_FORCE_INLINE bool tryLock()
            {
                bool expected = false;
                if (lock_value.compareExchangeStrong(expected, true, MemoryOrder::Acquire, MemoryOrder::Relaxed))
                {
                    return true;
                }
                return false;
            };

            /// \brief Acquire, spinning until the lock is taken.
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

            /// \brief Release the lock.
            /// \note The store is Relaxed on purpose: the release edge is carried
            ///       by the Acquire CAS the next locker performs, so no explicit
            ///       Release is needed here for the handoff to be correct.
            MLW_FORCE_INLINE void unlock()
            {
                lock_value.store(false, MemoryOrder::Relaxed);
            }
        };

        /// \brief Ticket spinlock: strict FIFO fairness.
        ///
        /// Each locker draws a monotonically increasing ticket and spins until
        /// `now_serving` reaches it, so threads acquire in arrival order and none
        /// is starved. Fairer than \ref TTAS, but every waiter spins on the same
        /// `now_serving` line, so it does not scale as well as \ref MCS under
        /// heavy contention.
        ///
        /// \note The ticket counters are `uint32` and wrap; only the difference
        ///       between them matters, so wrap-around is harmless — do not
        ///       "fix" it with a wider type.
        struct TicketLock
        {
        private:
            Atomic<uint32> next_ticket{0};   ///< Next ticket to hand out.
            Atomic<uint32> now_serving{0};   ///< Ticket currently allowed in.

        public:
            TicketLock() = default;

            /// \brief Destroy the lock. Panics if held or if waiters remain.
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

            /// \brief Try to acquire without spinning. Returns true on success.
            /// \note Succeeds only when completely uncontended (no holder and no
            ///       waiters); any queued waiter makes the CAS fail and this
            ///       returns false.
            MLW_FORCE_INLINE bool tryLock() noexcept
            {
                uint32 serving = now_serving.load(MemoryOrder::Relaxed);
                return next_ticket.compareExchangeStrong(serving, serving + 1, MemoryOrder::Acquire, MemoryOrder::Relaxed);
            }

            /// \brief Acquire, spinning until this thread's ticket is served.
            MLW_FORCE_INLINE void lock()
            {
                uint32 my_ticket = next_ticket.fetchAdd(1, MemoryOrder::Relaxed);
                while (now_serving.load(MemoryOrder::Acquire) != my_ticket)
                    MLW_CPU_PAUSE();
            };

            /// \brief Release, admitting the next ticket in line.
            MLW_FORCE_INLINE void unlock()
            {
                now_serving.fetchAdd(1, MemoryOrder::Release);
            };
        };

        /// \brief MCS queue spinlock: each waiter spins on its own cache line.
        ///
        /// Waiters form a linked queue and each spins on a flag inside *its own*
        /// \ref Node rather than a shared location, so adding waiters adds no
        /// contention on a common cache line. This is what lets MCS scale where
        /// \ref TTAS and \ref TicketLock degrade.
        ///
        /// The cost is the node: every `lock`/`unlock`/`tryLock` call takes a
        /// \ref Node reference that must stay alive and not move for the whole
        /// critical section, because its address is linked into the queue.
        ///
        /// \note Prefer \ref core::sync::Lock "Lock<spin_lock::MCS>", which owns
        ///       the node on the stack for you and handles its lifetime.
        struct MCS
        {
            /// \brief Per-waiter queue node. One per thread, per acquisition.
            struct Node
            {
                Atomic<Node *> next{nullptr};  ///< Next waiter behind this one, or null.
                Atomic<bool> locked{false};    ///< True while parked; the predecessor
                                               ///< sets it false to grant the lock.
            };

        private:
            Atomic<Node *> tail{nullptr};      ///< Most recently enqueued waiter.

        public:
            /// \brief Acquire, spinning on `me`'s local flag until granted.
            ///
            /// Atomically swaps `me` in as the queue tail. If there was a
            /// predecessor, links behind it and spins on `me.locked` until the
            /// predecessor's `unlock` clears it; if there was none, the lock is
            /// free and taken immediately.
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

            /// \brief Release, granting the lock to the next queued waiter.
            ///
            /// If `me` has a linked successor, clears its flag to wake it. If no
            /// successor is visible yet, tries to reset the tail to empty.
            MLW_FORCE_INLINE void unlock(Node &me) noexcept
            {
                Node *next = me.next.load(MemoryOrder::Acquire);
                if (next == nullptr)
                {
                    // No successor linked yet. Either we are truly last (CAS the
                    // tail back to null and we're done), or a successor has
                    // already swapped itself into the tail in lock() but has NOT
                    // yet run its `prev->next.store(&me)`. In that second case the
                    // CAS fails and we must spin until their next-link becomes
                    // visible — this wait is bounded by that tiny window, not a
                    // deadlock, even though it looks like one.
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

            /// \brief Destroy the lock. Panics if an owner or waiters remain.
            ~MCS()
            {
                if (tail.load(MemoryOrder::Relaxed) != nullptr)
                {
                    panic("MCS lock destroyed with active owner or waiters");
                }
            }
            MCS(const MCS &) = delete;
            MCS &operator=(const MCS &) = delete;
            MCS(MCS &&) = delete;
            MCS &operator=(MCS &&) = delete;

            /// \brief Try to acquire without spinning, using `me`. True on success.
            ///
            /// Succeeds only if the queue is empty (tail is null). On success
            /// `me` becomes the tail; on failure nothing is enqueued.
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

    /// \brief RAII guard specialization for \ref core::sync::spin_lock::MCS.
    ///
    /// Acquires on construction, releases on destruction, and — unlike the
    /// generic \ref Lock — carries the MCS queue node itself, so callers never
    /// manage node lifetime.
    ///
    /// \note Non-movable by design. The embedded `me` node's address is linked
    ///       into the lock's queue while held; moving the guard would leave the
    ///       lock pointing at a stale node. Move is deleted (as on the primary
    ///       \ref Lock) so every Lock<T> shares one ownership contract. To hand
    ///       a held guard around, use the \ref tryLock out-parameter, not a move.
    template <>
    struct Lock<spin_lock::MCS>
    {
    private:
        spin_lock::MCS &lock_obj;
        spin_lock::MCS::Node me; // lives on the stack here, exactly as long as the lock
        bool locked = false;

        /// \internal Tag selecting the adopt constructor: take ownership of an
        /// already-held lock without acquiring. Used only by the tryLock path.
        struct AdoptTag
        {
        };
        MLW_FORCE_INLINE Lock(spin_lock::MCS &obj, AdoptTag) : lock_obj(obj), locked(true) {}
        friend Optional<Lock<spin_lock::MCS>>;

    public:
        /// \brief True if this guard currently holds the lock.
        MLW_FORCE_INLINE bool isHeld() const { return locked; }

        /// \brief Release the held lock (no-op if not held).
        MLW_FORCE_INLINE void unlock()
        {
            if (locked)
            {
                lock_obj.unlock(me);
                locked = false;
            }
        }

        /// \brief Reacquire after a manual \ref unlock; no-op if already held.
        MLW_FORCE_INLINE void relock()
        {
            if (locked)
                return;
            lock_obj.lock(me);
            locked = true;
        }
        Lock() = delete;

        /// \brief Construct and block until the lock is acquired.
        MLW_FORCE_INLINE explicit Lock(spin_lock::MCS &obj) : lock_obj(obj)
        {
            lock_obj.lock(me);
            locked = true;
        }

        MLW_FORCE_INLINE ~Lock()
        {
            unlock();
        }

        /// \brief Try to acquire, constructing the guard into `result`.
        ///
        /// Because Lock is non-movable it cannot be returned by value, so the
        /// result is delivered through a caller-provided Optional.
        ///
        /// \note Construct-then-attempt ordering is deliberate: it emplaces via
        ///       the AdoptTag constructor (which does NOT lock, only marks the
        ///       guard held) so that `me` has a stable address, then runs the
        ///       real `tryLock(me)` against that node. On failure it clears the
        ///       flag and resets the Optional so `~Lock` won't release a lock it
        ///       never took. Do not reorder these steps.
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