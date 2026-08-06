// core/core/tests/test_spinlock.cpp
//
// Coverage for core/thread/spinlock.h: the three spin locks (TTAS, TicketLock,
// MCS), the generic Lock<T> RAII guard, and the Lock<spin_lock::MCS>
// specialization that owns its queue node.
//
// Single-threaded checks cover the tryLock/lock/unlock/isHeld contracts and the
// tryLock-into-Optional out-parameter path. The contended checks spawn K
// library threads that each do N guarded increments of a PLAIN int; if the lock
// did not actually exclude, lost updates make the total come out below K*N.
// (A plain int is used deliberately — an atomic counter would pass even with a
// broken lock, hiding the bug.)
//
// Every lock panics if destroyed while held, so every path unlocks first.

#include "core/thread/spinlock.h"
#include "core/thread/thread.h"
#include "core/optional.h"
#include "core/typedef.h"

using namespace core;
using namespace core::sync;
using namespace core::sync::spin_lock;

namespace
{
    // Run `body(i)` on K library threads, i in [0,K), and join them all.
    // Returns false if any spawn failed. Not named test_* (helper).
    template <typename Body>
    bool runOnThreads(int32 K, Body body)
    {
        // Small fixed cap keeps this a stack array in a freestanding tree.
        constexpr int32 MAXK = 8;
        if (K > MAXK) return false;
        auto make = [&](int32 id) {
            return [body, id] { body(id); return 0; };
        };
        using Fn = decltype(make(0));
        // Construct handles one by one.
        Optional<ThreadHandle<Fn>> hs[MAXK];
        for (int32 i = 0; i < K; ++i) hs[i].emplace(make(i));
        for (int32 i = 0; i < K; ++i) if (hs[i].unwrap().spawn().isErr()) return false;
        for (int32 i = 0; i < K; ++i) hs[i].unwrap().join();
        return true;
    }
}

namespace core_core_test
{
    // =======================================================================
    //  TTAS: tryLock / lock / unlock
    // =======================================================================
    bool test_spin_ttas_basic()
    {
        TTAS m;
        if (!m.tryLock()) return false;      // free -> acquired
        if (m.tryLock()) return false;       // held -> refused
        m.unlock();
        if (!m.tryLock()) return false;      // free again
        m.unlock();
        m.lock();                            // blocking acquire on a free lock
        m.unlock();
        return true;
    }

    // =======================================================================
    //  TicketLock: tryLock only succeeds when fully uncontended
    // =======================================================================
    bool test_spin_ticket_basic()
    {
        TicketLock m;
        if (!m.tryLock()) return false;
        if (m.tryLock()) return false;       // already held
        m.unlock();
        m.lock();
        m.unlock();
        return true;
    }

    // =======================================================================
    //  MCS: direct node API (lock/unlock/tryLock take a caller Node)
    // =======================================================================
    bool test_spin_mcs_basic()
    {
        MCS m;
        MCS::Node n1;
        if (!m.tryLock(n1)) return false;    // empty queue -> success
        MCS::Node n2;
        if (m.tryLock(n2)) return false;     // occupied -> failure
        m.unlock(n1);
        MCS::Node n3;
        m.lock(n3);                          // blocking acquire, now free
        m.unlock(n3);
        return true;
    }

    // =======================================================================
    //  Lock<TTAS> RAII: acquires on construct, isHeld, unlock/relock, frees
    // =======================================================================
    bool test_spin_lock_guard_raii()
    {
        TTAS m;
        {
            Lock<TTAS> g{m};
            if (!g.isHeld()) return false;
            if (m.tryLock()) return false;   // guard holds it
            g.unlock();
            if (g.isHeld()) return false;    // now released
            if (!m.tryLock()) return false;  // free while guard released
            m.unlock();
            g.relock();
            if (!g.isHeld()) return false;
        }                                    // ~Lock releases
        if (!m.tryLock()) return false;      // free after guard destroyed
        m.unlock();
        return true;
    }

    // =======================================================================
    //  Lock<TicketLock>::tryLock -> Optional out-param
    // =======================================================================
    bool test_spin_lock_trylock_optional()
    {
        TicketLock m;
        Optional<Lock<TicketLock>> g;
        Lock<TicketLock>::tryLock(m, g);     // free -> engaged
        if (!g.isSome()) return false;
        if (!g.unwrap().isHeld()) return false;

        Optional<Lock<TicketLock>> g2;
        Lock<TicketLock>::tryLock(m, g2);    // already held -> disengaged
        if (g2.isSome()) return false;

        g.reset();                           // release the first guard
        Optional<Lock<TicketLock>> g3;
        Lock<TicketLock>::tryLock(m, g3);    // free again -> engaged
        if (!g3.isSome()) return false;
        g3.reset();
        return true;
    }

    // =======================================================================
    //  Lock<MCS> specialization: owns the node internally
    // =======================================================================
    bool test_spin_lock_mcs_guard()
    {
        MCS m;
        {
            Lock<MCS> g{m};
            if (!g.isHeld()) return false;
            g.unlock();
            if (g.isHeld()) return false;
            g.relock();
            if (!g.isHeld()) return false;
        }
        // tryLock out-param path on the MCS specialization
        Optional<Lock<MCS>> g2;
        Lock<MCS>::tryLock(m, g2);
        if (!g2.isSome()) return false;
        Optional<Lock<MCS>> g3;
        Lock<MCS>::tryLock(m, g3);           // held -> disengaged
        if (g3.isSome()) return false;
        g2.reset();
        return true;
    }

    // =======================================================================
    //  Mutual exclusion under contention (plain int guarded by each lock type)
    // =======================================================================
    bool test_spin_ttas_mutual_exclusion()
    {
        constexpr int32 K = 4, N = 5000;
        TTAS m;
        int32 counter = 0;                   // PLAIN int: only the lock protects it
        bool ok = runOnThreads(K, [&m, &counter](int32) {
            for (int32 i = 0; i < N; ++i) { Lock<TTAS> g{m}; ++counter; }
        });
        return ok && counter == K * N;
    }

    bool test_spin_ticket_mutual_exclusion()
    {
        constexpr int32 K = 4, N = 5000;
        TicketLock m;
        int32 counter = 0;
        bool ok = runOnThreads(K, [&m, &counter](int32) {
            for (int32 i = 0; i < N; ++i) { Lock<TicketLock> g{m}; ++counter; }
        });
        return ok && counter == K * N;
    }

    bool test_spin_mcs_mutual_exclusion()
    {
        constexpr int32 K = 4, N = 5000;
        MCS m;
        int32 counter = 0;
        bool ok = runOnThreads(K, [&m, &counter](int32) {
            for (int32 i = 0; i < N; ++i) { Lock<MCS> g{m}; ++counter; }
        });
        return ok && counter == K * N;
    }
}
