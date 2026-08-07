// core/core/tests/test_mutex.cpp
//
// Coverage for core/thread/mutex.h (Mutex) and core/thread/candvar.h (CondVar).
//
// Mutex::lock/unlock/tryLock and futex wait/wake are defined out-of-line in the
// per-OS thread.cpp, so these are runtime checks that exercise the real CRT
// seam in the freestanding tree. Single-threaded checks cover the tryLock
// contract; the contended check guards a PLAIN int across K threads. The
// CondVar check is a one-producer/one-consumer handoff — the predicate form of
// wait() re-checks under the lock, so there is no lost-wakeup hang even if the
// producer runs to completion before the consumer starts waiting.
//
// Mutex panics if destroyed while held, so every path unlocks.

#include "core/thread/mutex.h"
#include "core/thread/candvar.h"
#include "core/thread/lock.h"
#include "core/thread/thread.h"
#include "core/optional.h"
#include "core/typedef.h"

using namespace core;
using namespace core::sync;

namespace
{
    template <typename Body>
    bool runOnThreads(int32 K, Body body)
    {
        constexpr int32 MAXK = 8;
        if (K > MAXK)
            return false;
        auto make = [&](int32 id)
        { return [body, id]
          { body(id); return 0; }; };
        using Fn = decltype(make(0));
        Optional<ThreadHandle<Fn>> hs[MAXK];
        for (int32 i = 0; i < K; ++i)
            hs[i].emplace(make(i));
        for (int32 i = 0; i < K; ++i)
            if (hs[i].unwrap().spawn().isErr())
                return false;
        for (int32 i = 0; i < K; ++i)
            hs[i].unwrap().join();
        return true;
    }
}

namespace core_core_test
{
    // =======================================================================
    //  Mutex: tryLock / lock / unlock (single thread)
    // =======================================================================
    bool test_mutex_basic()
    {
        Mutex m;
        if (!m.tryLock())
            return false; // free -> acquired
        if (m.tryLock())
            return false; // held -> refused (non-recursive)
        m.unlock();
        if (!m.tryLock())
            return false; // free again
        m.unlock();
        m.lock(); // blocking acquire on a free mutex
        m.unlock();
        return true;
    }

    // =======================================================================
    //  Mutex via Lock<Mutex> RAII guard
    // =======================================================================
    bool test_mutex_lock_guard()
    {
        Mutex m;
        {
            Lock<Mutex> g{m};
            if (!g.isHeld())
                return false;
            if (m.tryLock())
                return false; // guard holds it
            g.unlock();
            if (!m.tryLock())
                return false; // free while released
            m.unlock();
            g.relock();
        }
        if (!m.tryLock())
            return false; // free after guard destroyed
        m.unlock();
        return true;
    }

    // =======================================================================
    //  Mutual exclusion under contention (plain int guarded by Mutex)
    // =======================================================================
    bool test_mutex_contended()
    {
        constexpr int32 K = 4, N = 5000;
        Mutex m;
        int32 counter = 0; // PLAIN int: only the mutex protects it
        bool ok = runOnThreads(K, [&m, &counter](int32)
                               {
            for (int32 i = 0; i < N; ++i) { Lock<Mutex> g{m}; ++counter; } });
        return ok && counter == K * N;
    }

    // =======================================================================
    //  CondVar: one producer, one consumer handoff
    // =======================================================================
    bool test_condvar_producer_consumer()
    {
        Mutex m;
        CondVar cv;
        bool ready = false;
        int32 data = 0;

        // Consumer waits for `ready`, then returns the value it observed.
        auto consumer = [&m, &cv, &ready, &data]() -> int32
        {
            Lock<Mutex> g{m};
            cv.wait(g, [&ready]
                    { return ready; }); // re-checks predicate under lock
            return data;                // lock held here; safe to read
        };
        ThreadHandle<decltype(consumer)> h{decltype(consumer)(consumer)};
        if (h.spawn().isErr())
            return false;

        // Producer: publish under the lock, then wake.
        {
            Lock<Mutex> g{m};
            data = 42;
            ready = true;
        }
        cv.wakeOne();

        int32 observed = h.join();
        return observed == 42;
    }

    // =======================================================================
    //  CondVar wakeAll: several waiters all released by one broadcast
    // =======================================================================
    bool test_condvar_wake_all()
    {
        bool sucses = true;
        for (int z = 0; z < 50; ++z)
        {
            Mutex m;
            CondVar cv;
            bool go = false;
            Atomic<int32> woke{0};

            constexpr int32 K = 3;
            auto waiter = [&m, &cv, &go, &woke](int32)
            {
                Lock<Mutex> g{m};
                cv.wait(g, [&go]
                        { return go; });
                woke.fetchAdd(1, MemoryOrder::AcqRel);
            };
            auto make = [&](int32 id)
            { return [waiter, id]
              { waiter(id); return 0; }; };
            using Fn = decltype(make(0));
            Optional<ThreadHandle<Fn>> hs[K];
            for (int32 i = 0; i < K; ++i)
                hs[i].emplace(make(i));
            for (int32 i = 0; i < K; ++i)
                if (hs[i].unwrap().spawn().isErr())
                    return false;

            {
                Lock<Mutex> g{m};
                go = true;
            }
            cv.wakeAll();

            for (int32 i = 0; i < K; ++i)
                hs[i].unwrap().join();
            if (woke.load() != K) sucses = false;
        }
        return sucses;
    }
}
