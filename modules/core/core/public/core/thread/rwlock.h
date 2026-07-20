#pragma once
#include "spinlock.h"

/// \file
/// \brief Reader/writer spinlock and its RAII read/write guards.
///
/// Defines core::sync::RWLock (concurrent readers or one exclusive writer, all
/// waiting done by busy-spin) together with the scoped guards
/// core::sync::ReadLock and core::sync::WriteLock.

namespace core::sync
{
    /// \brief A reader/writer spinlock: concurrent readers or one exclusive writer.
    ///
    /// Permits multiple readers at once, or a single writer exclusively. All
    /// waiting is busy-spin (there is no sleeping/futex path), and writers queue
    /// on an MCS spinlock for FIFO ordering among themselves.
    ///
    /// \par Behavior
    /// - **Writer-priority.** A reader blocks while any writer is active *or*
    ///   pending, so a waiting writer keeps new readers out. See \ref readLock.
    /// - **Spin-only.** Waiters burn a core while spinning, so hold these locks
    ///   only for short critical sections.
    /// - **Non-recursive.** Re-acquiring from a thread that already holds the
    ///   lock will hang.
    ///
    /// \warning A continuous stream of writers can starve readers indefinitely.
    ///          The guarantee is one-directional: readers never starve writers.
    /// \warning Destroying an RWLock while any read or write lock is still held
    ///          calls panic() and aborts.
    struct RWLock
    {
        Atomic<int32> readers{0};             ///< Count of active readers.
        Atomic<int32> writers{0};             ///< Count of active + pending writers.
        spin_lock::MCS writer_queue{};        ///< FIFO queue serializing writers.

        RWLock() = default;

        /// \brief Destroy the lock. Panics if any reader or writer is still active.
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

        /// \brief Acquire shared read access (blocks while any writer is active or waiting).
        ///
        /// Writer-priority: waits until no writer is active *or* pending. The
        /// increment-then-recheck sequence (bump `readers`, re-test `writers`,
        /// back out if a writer appeared) closes the race where a writer arrives
        /// just as a reader is entering.
        ///
        /// \warning Busy-spins while blocked; and under a steady stream of
        ///          writers a reader can wait indefinitely (see the type note).
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

        /// \brief Release shared read access.
        MLW_FORCE_INLINE void readUnlock()
        {
            readers.fetchSub(1, MemoryOrder::Release);
        }

        /// \brief Acquire exclusive write access (blocks until sole owner).
        ///
        /// Registers as a pending writer first (which stops new readers), then
        /// queues behind any earlier writers, then spins until existing readers
        /// have drained.
        ///
        /// \param node A queue node for writer ordering. It must stay alive and
        ///             not move until the matching \ref writeUnlock, because the
        ///             MCS queue records its address while the writer waits.
        void writeLock(spin_lock::MCS::Node &node)
        {
            writers.fetchAdd(1, MemoryOrder::Acquire); // block new readers
            writer_queue.lock(node);                   // queue behind other writers
            while (readers.load(MemoryOrder::Acquire) > 0)
                MLW_CPU_PAUSE(); // drain existing readers
        }

        /// \brief Release exclusive write access.
        /// \param node The same node passed to the matching \ref writeLock.
        MLW_FORCE_INLINE void writeUnlock(spin_lock::MCS::Node &node)
        {
            writer_queue.unlock(node);
            writers.fetchSub(1, MemoryOrder::Release);
        }
    };

    /// \brief RAII guard for exclusive write access to an \ref RWLock.
    ///
    /// Acquires on construction, releases on destruction. Owns the MCS queue
    /// node internally, so callers don't manage node lifetime themselves.
    /// Non-copyable and non-movable: the owned node's address is registered in
    /// the queue while held, so the guard cannot be relocated.
    struct WriteLock
    {

    private:
        RWLock &lock_obj;
        bool locked = false;
        spin_lock::MCS::Node node{};

    public:
        /// \brief True if this guard currently holds the write lock.
        MLW_FORCE_INLINE bool isHeld() const { return locked; }

        /// \brief Release the held write lock (no-op if not held).
        MLW_FORCE_INLINE void unlock()
        {
            if (locked)
            {
                lock_obj.writeUnlock(node);
                locked = false;
            }
        }

        /// \brief Reacquire the write lock after a manual \ref unlock; no-op if held.
        MLW_FORCE_INLINE void relock()
        {
            if (locked)
                return;
            lock_obj.writeLock(node);
            locked = true;
        }
        WriteLock() = delete;

        /// \brief Construct and block until exclusive write access is acquired.
        MLW_FORCE_INLINE explicit WriteLock(RWLock &obj) : lock_obj(obj)
        {
            lock_obj.writeLock(node);
            locked = true;
        }
        MLW_FORCE_INLINE ~WriteLock() { unlock(); }

        WriteLock(const WriteLock &) = delete;
        WriteLock &operator=(const WriteLock &) = delete;
        WriteLock(WriteLock &&) = delete;
        WriteLock &operator=(WriteLock &&) = delete;
    };

    /// \brief RAII guard for shared read access to an \ref RWLock.
    ///
    /// Acquires on construction, releases on destruction. Non-copyable and
    /// non-movable, matching \ref WriteLock's ownership contract.
    struct ReadLock
    {

    private:
        RWLock &lock_obj;
        bool locked = false;

    public:
        /// \brief True if this guard currently holds a read lock.
        MLW_FORCE_INLINE bool isHeld() const { return locked; }

        /// \brief Release the held read lock (no-op if not held).
        MLW_FORCE_INLINE void unlock()
        {
            if (locked)
            {
                lock_obj.readUnlock();
                locked = false;
            }
        }

        /// \brief Reacquire the read lock after a manual \ref unlock; no-op if held.
        MLW_FORCE_INLINE void relock()
        {
            if (locked)
                return;
            lock_obj.readLock();
            locked = true;
        }
        ReadLock() = delete;

        /// \brief Construct and block until shared read access is acquired.
        MLW_FORCE_INLINE explicit ReadLock(RWLock &obj) : lock_obj(obj)
        {
            lock_obj.readLock();
            locked = true;
        }
        MLW_FORCE_INLINE ~ReadLock() { unlock(); }

        ReadLock(const ReadLock &) = delete;
        ReadLock &operator=(const ReadLock &) = delete;
        ReadLock(ReadLock &&) = delete;
        ReadLock &operator=(ReadLock &&) = delete;
    };
} // namespace core::sync