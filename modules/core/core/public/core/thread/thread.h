#pragma once

#include "core/traits.h"
#include "core/libc/mem.h"
#include "core/result.h"

/// \file
/// \brief Owning handle to an OS thread, with typed return-value passing.
///
/// `core::ThreadHandle<Fn>` spawns a thread running a closure and carries its
/// return value back to the joiner. It is move-only, must be joined before
/// destruction, and is single-use (spawn once, join once).
///
/// The `core::detail` declarations here are the CRT platform seam: their
/// definitions live in the per-OS `thread.cpp` (Windows `CreateThread`, Linux
/// `clone`). This header defines the contract those definitions must honor.

namespace core
{
    namespace detail
    {
        /// \brief Trampoline payload handed to the OS thread entry point.
        ///
        /// A plain function pointer plus its argument. \ref sys_spawn packages
        /// one of these and the OS entry stub calls `fn(args)`.
        ///
        /// \note Ownership: heap-allocated by \ref ThreadHandle::spawn and owned
        ///       by the *spawned thread*, which frees it after `fn` returns. The
        ///       one exception is spawn failure: the thread never ran, so the
        ///       caller frees it instead. This asymmetry is deliberate and is
        ///       mirrored in both the Windows and Linux entry stubs.
        struct ThreadStart
        {
            void (*fn)(void *);
            void *args;
        };

        /// \brief Aligned storage for a thread's return value of type `R`.
        ///
        /// The value is move-constructed into `result` by the thread and moved
        /// out (then destroyed in place) by the joiner. `has_return` guards the
        /// slot so it is neither read before the thread writes it nor destroyed
        /// twice.
        template <typename R>
        struct ReturnSlot
        {
            bool has_return = false;
            alignas(R) uint8 result[sizeof(R)]{};
        };

        /// \brief Empty return slot for `void`-returning threads.
        template <>
        struct ReturnSlot<void>
        {
        };

        /// \brief Block until the thread behind `handle` exits, then reclaim it.
        ///
        /// After return the handle is no longer joinable (its OS resources are
        /// released). Platform seam — see `thread.cpp`.
        ///
        /// \note `handle` is opaque: on Windows it is an OS thread `HANDLE`; on
        ///       Linux it is a `LinuxThreadState*` (tid + mmap'd stack). `join`
        ///       closes the handle / munmaps the stack and frees the state.
        void sys_join(void* &);

        /// \brief Wait up to `ms` for the thread to exit; reclaim if it has.
        ///
        /// Returns true and reclaims resources if the thread finished (same
        /// cleanup as \ref sys_join). Returns false on timeout, leaving the
        /// handle joinable. Platform seam — see `thread.cpp`.
        bool try_join(void*&, uint32);

        /// \brief Start an OS thread running `s->fn(s->args)`.
        ///
        /// On success stores the opaque handle in the reference and returns 0.
        /// On failure returns the OS error code (Windows `GetLastError`, Linux
        /// `errno`) captured at the failure site and does **not** free `s` — the
        /// caller owns it on the error path. Platform seam — see `thread.cpp`.
        uint32 sys_spawn(void* &, ThreadStart *);
    }

    /// \brief Empty value type used as the success payload of a valueless Result.
    struct Unit
    {
    };

    /// \ingroup formattable
    /// \brief Error returned by \ref ThreadHandle::spawn.
    struct ThreadError
    {
        /// \brief What went wrong.
        enum Kind : uint8
        {
            CreateFailed,   ///< The OS refused to create the thread.
            DoubleStart     ///< spawn() called on an already-running handle.
        } kind;

        uint32 os_code; ///< OS error (GetLastError()/errno), captured at the failure site. 0 for DoubleStart.

        /// \brief Format the error for logging.
        template <core::FormatBuffer Buf>
        void format(Buf &b) const { switch (kind){
            case CreateFailed:
                mlw_write(b, "ThreadError(create failed, os={})", os_code); 
            break;
            case DoubleStart:
            mlw_write(b, "ThreadHandle already running");
            break;
        }
    }
            
    };

    /// \brief Move-only owning handle to one OS thread, carrying a typed result.
    ///
    /// Spawns a thread running the closure `Fn` and delivers its `R`-typed return
    /// value to whoever joins. Typically created through the static \ref spawn
    /// factory rather than constructed directly.
    ///
    /// \par Contract
    /// - **Move-only.** Copy is deleted; move transfers ownership and leaves the
    ///   source empty (moved-from).
    /// - **Must be joined.** Destroying a handle whose thread is still running
    ///   (i.e. spawned but not yet joined) calls panic(). A moved-from handle
    ///   (`params == nullptr`) is the one state that destroys cleanly without a
    ///   join.
    /// - **Single-use.** A handle spawns once and joins once; \ref join and a
    ///   successful \ref tryJoin consume the result and mark the handle done.
    ///
    /// \tparam Fn The callable to run on the new thread.
    /// \tparam R  Its return type, deduced as `invoke_result_t<Fn>`.
    template <typename Fn, typename R = core::invoke_result_t<Fn>>
        requires core::is_invocable_v<Fn>
    class ThreadHandle
    {
        /// \internal Heap block shared with the thread: the closure copy and its
        /// return slot. Allocated in the constructor, freed in \ref release.
        struct ThreadParameters
        {
            Fn fn; // decayed copy of the closure
            detail::ReturnSlot<R> ret;
        } *params = nullptr;

        /// \internal OS entry trampoline: runs the closure and, for non-void R,
        /// move-constructs the result into the slot and flags it ready.
        static void threadCall(void *arg)
        {
            ThreadParameters *params = static_cast<ThreadParameters *>(arg);
            if constexpr (core::is_same_v<R, void>)
            {
                params->fn();
            }
            else
            {
                new (params->ret.result) R(params->fn());
                params->ret.has_return = true;
            }
        }

        void* handle = nullptr; ///< Opaque OS handle; null before spawn and after join.

        /// \internal Release owned state, enforcing the must-join contract.
        /// Shared by the destructor and move-assignment so the invariant and the
        /// return-slot teardown live in exactly one place.
        void release()
        {
            if (!params)
                return;
            if (handle != nullptr)
                panic("ThreadHandle discarded without join() being called.");

            if constexpr (!core::is_same_v<R, void>)
            {
                if (params->ret.has_return)
                {
                    reinterpret_cast<R *>(params->ret.result)->~R();
                    params->ret.has_return = false;
                }
            }
            params->~ThreadParameters();
            mlwFree(params);
            params = nullptr;
            handle = nullptr;
        }

    public:
        /// \brief Build a handle holding the closure; does not start the thread.
        /// \note Allocates the shared parameter block; panics on OOM. Call
        ///       \ref spawn to actually start the thread.
        explicit ThreadHandle(Fn &&f) : handle(0)
        {
            params = static_cast<ThreadParameters *>(mlwAlignedAlloc(sizeof(ThreadParameters), alignof(ThreadParameters)));
            new (params) ThreadParameters{core::forward<Fn>(f), {}};
        }

        /// \brief Destroy the handle. Panics if the thread was never joined.
        ~ThreadHandle()
        {
            release();
        };

        ThreadHandle(const ThreadHandle &) = delete;
        ThreadHandle &operator=(const ThreadHandle &) = delete;

        /// \brief Move-construct, taking ownership and emptying the source.
        ThreadHandle(ThreadHandle &&other) noexcept : params(other.params), handle(other.handle)
        {
            other.params = nullptr;
            other.handle = nullptr;
        }

        /// \brief Move-assign. Releases current state first (enforcing must-join),
        ///        then takes ownership of `other` and empties it.
        ThreadHandle &operator=(ThreadHandle &&other) noexcept
        {
            if (this != &other)
            {
                release(); // free/enforce-join on what we currently own before overwriting
                params = other.params;
                handle = other.handle;
                other.params = nullptr;
                other.handle = nullptr;
            }
            return *this;
        }

        /// \brief Start the thread. Returns Unit on success, ThreadError on failure.
        ///
        /// \note Lifetime of the `ThreadStart` block `s` is split: on success the
        ///       spawned thread owns and frees it (see \ref detail::ThreadStart);
        ///       on failure the thread never ran, so this frees it here. That is
        ///       why the error path calls `mlwFree(s)` and the success path does
        ///       not — it is not a leak.
        core::Result<Unit, ThreadError> spawn()
        {
            if (handle != nullptr) return Err{ThreadError{ThreadError::DoubleStart,0}};
            //maby zero out retern

            detail::ThreadStart *s = ::new (core::mlwMalloc(sizeof(detail::ThreadStart))) detail::ThreadStart{&threadCall, params};
            if (uint32 code = detail::sys_spawn(handle, s); code != 0)
            {
                core::mlwFree(s); // thread never ran -> IT never frees s -> WE must
                return core::Err{ThreadError{ThreadError::CreateFailed, code}};
            }
            return Unit{};
        }

        /// \brief Convenience factory: construct, spawn, and hand back a running handle.
        /// \note `mlwMalloc` routes through the aligned allocator, so freeing
        ///       both the aligned `params` and the plain `s` with `mlwFree` is
        ///       valid — the pairing is intentional (documented in the allocator).
        static core::Result<core::ThreadHandle<Fn>, ThreadError> spawn(Fn &&fn)
        {
            core::ThreadHandle<Fn> h{core::forward<Fn>(fn)}; // OOM here -> panic
            if (Result<Unit, ThreadError> r = h.spawn(); r.isErr())
                return core::Err{r.takeError()};
            return core::move(h); // hand the running thread out
        }

        /// \brief Block until the thread finishes and return its value.
        ///
        /// Consumes the result: moves it out of the slot, destroys the husk, and
        /// marks the handle done. Single-use — calling twice panics.
        ///
        /// \warning Panics if called on a moved-from handle, on a handle that was
        ///          never spawned or already joined, or (for non-void `R`) if the
        ///          thread somehow produced no value.
        R join()
        {
            if (!params)
                panic("ThreadHandle::join() called on a moved-from ThreadHandle.");
            if (handle == nullptr)
                panic("ThreadHandle::join() call twice or on a non started thread.");

            detail::sys_join(handle);
            if constexpr (!core::is_same_v<R, void>)
            {
                if (params->ret.has_return)
                {
                    R *slot = reinterpret_cast<R *>(params->ret.result);
                    R out = core::move(*slot); // move-construct out of the slot
                    slot->~R();                // destroy the moved-from husk in the slot
                    params->ret.has_return = false;
                    handle = nullptr;
                    return out;
                }
                panic("ThreadHandle::join() called on a thread that did not return a value.");
            }
            else
            {
                handle = 0;
                return;
            }
        };

        /// \brief Join with a timeout of `ms` milliseconds.
        ///
        /// `ms == 0` polls (returns immediately); a nonzero value waits up to
        /// that many milliseconds. For an unbounded wait use \ref join instead.
        /// Both platforms behave identically here.
        ///
        /// \note The return type depends on `R`: `bool` for a void thread (true
        ///       once finished), or `Optional<R>` otherwise (engaged with the
        ///       value once finished, empty on timeout). The two branches treat
        ///       "nothing to wait on" (moved-from / not started) *oppositely*:
        ///       the void form returns `true` (done), the value form returns an
        ///       empty `Optional`.
        ///
        /// \note Best-effort timing: a spurious futex wake can make this report
        ///       "still running" slightly before `ms` elapses. The join result
        ///       is always correct; only the timeout precision is approximate.
        auto tryJoin(uint32 ms){
            if constexpr (core::is_same_v<R, void>) {
                if (!params || handle == nullptr) return true;   // nothing to wait on = "done"
                return detail::try_join(handle, ms);
            }else{
               if (!params || handle == 0) return Optional<R>{}; 
                if (!detail::try_join(handle, ms)) return Optional<R>{}; 
                if (params->ret.has_return)
                {
                    R *slot = reinterpret_cast<R *>(params->ret.result);
                    Optional<R> out{ core::move(*slot)}; // move-construct out of the slot
                    slot->~R();                // destroy the moved-from husk in the slot
                    params->ret.has_return = false;
                    handle = nullptr;
                    return out;
                }
                return Optional<R>{}; 
            }
        }

        /// \brief True if the handle owns a spawned, not-yet-joined thread.
        MLW_FORCE_INLINE bool joinable() const {
            return params && handle != 0;
        }

        /// \todo detach(): release ownership without joining (needs the OS side
        ///       to reclaim the thread's resources itself).
    };

} // namespace core