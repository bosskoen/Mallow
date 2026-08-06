// core/core/tests/test_rwlock.cpp
//
// Coverage for core/thread/rwlock.h: RWLock and its ReadLock / WriteLock RAII
// guards.
//
// Reader concurrency is checked DETERMINISTICALLY rather than by racing
// threads (a failed concurrency race would deadlock the runner): two ReadLocks
// held at the same time drive the internal reader count to 2, which an
// exclusive lock could never permit — so a passing count proves shared access
// is allowed without any timing dependence. Writer exclusivity is checked with
// K threads incrementing a PLAIN int under WriteLock; lost updates below K*N
// mean the exclusion failed.
//
// RWLock panics if destroyed with any reader/writer still active, so every path
// balances its locks.

#include "core/thread/rwlock.h"
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
        if (K > MAXK) return false;
        auto make = [&](int32 id) { return [body, id] { body(id); return 0; }; };
        using Fn = decltype(make(0));
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
    //  Direct API: read lock/unlock and write lock/unlock (with a node)
    // =======================================================================
    bool test_rwlock_direct_api()
    {
        RWLock r;
        r.readLock();
        r.readUnlock();
        spin_lock::MCS::Node node;
        r.writeLock(node);
        r.writeUnlock(node);
        return true;
    }

    // =======================================================================
    //  Shared readers: two ReadLocks held at once (reader count reaches 2)
    // =======================================================================
    // If RWLock granted read access exclusively, the second ReadLock could
    // never be taken while the first is held. Holding both proves shared reads.
    bool test_rwlock_shared_readers()
    {
        RWLock r;
        {
            ReadLock a{r};
            if (!a.isHeld()) return false;
            ReadLock b{r};                   // second concurrent reader on this thread
            if (!b.isHeld()) return false;
            if (r.readers.load() != 2) return false;
        }                                    // both released here
        if (r.readers.load() != 0) return false;
        return true;
    }

    // =======================================================================
    //  WriteLock RAII: isHeld / unlock / relock / release on scope exit
    // =======================================================================
    bool test_rwlock_write_guard()
    {
        RWLock r;
        {
            WriteLock w{r};
            if (!w.isHeld()) return false;
            if (r.writers.load() != 1) return false;
            w.unlock();
            if (w.isHeld()) return false;
            if (r.writers.load() != 0) return false;
            w.relock();
            if (!w.isHeld()) return false;
        }
        if (r.writers.load() != 0) return false;
        return true;
    }

    // =======================================================================
    //  ReadLock RAII: unlock / relock / release
    // =======================================================================
    bool test_rwlock_read_guard()
    {
        RWLock r;
        {
            ReadLock g{r};
            if (!g.isHeld() || r.readers.load() != 1) return false;
            g.unlock();
            if (g.isHeld() || r.readers.load() != 0) return false;
            g.relock();
            if (!g.isHeld() || r.readers.load() != 1) return false;
        }
        return r.readers.load() == 0;
    }

    // =======================================================================
    //  Writer exclusivity under contention (plain int guarded by WriteLock)
    // =======================================================================
    bool test_rwlock_writer_exclusion()
    {
        constexpr int32 K = 4, N = 4000;
        RWLock r;
        int32 counter = 0;                   // PLAIN int: only exclusion protects it
        bool ok = runOnThreads(K, [&r, &counter](int32) {
            for (int32 i = 0; i < N; ++i) { WriteLock w{r}; ++counter; }
        });
        return ok && counter == K * N;
    }
}
