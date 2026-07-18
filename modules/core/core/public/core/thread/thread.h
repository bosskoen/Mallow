#pragma once

#include "core/io/handle.h"
#include "core/traits.h"
#include "core/libc/mem.h"
#include "core/result.h"
namespace core
{
    namespace detail
    {
        struct ThreadStart
        {
            void (*fn)(void *);
            void *args;
        };

        template <typename R>
        struct ReturnSlot
        {
            bool has_return = false;
            alignas(R) uint8 result[sizeof(R)]{};
        };

        template <>
        struct ReturnSlot<void>
        {
        };

        
        void sys_join(void* &);
        bool try_join(void*&,uint32);

        uint32 sys_spawn(void* &, ThreadStart *);
    }

    struct Unit
    {
    }; // or reuse whatever empty struct you already have

    struct ThreadError
    {
        enum Kind : uint8
        {
            CreateFailed,
            DoubleStart
        } kind;
        uint32 os_code; // GetLastError() / errno — captured at the failure site
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

    template <typename Fn, typename R = core::invoke_result_t<Fn>>
        requires core::is_invocable_v<Fn>
    class ThreadHandle
    {
        struct ThreadParameters
        {
            Fn fn; // decayed copy of the closure
            detail::ReturnSlot<R> ret;
        } *params = nullptr;

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

        void* handle = nullptr;

    public:
        explicit ThreadHandle(Fn &&f) : handle(0)
        {
            params = static_cast<ThreadParameters *>(mlwAlignedAlloc(sizeof(ThreadParameters), alignof(ThreadParameters)));
            new (params) ThreadParameters{core::forward<Fn>(f), {}};
        }

        ~ThreadHandle()
        {
            if (!params)
                return;
            if (handle != nullptr)
                panic("ThreadHandle destroyed without join() being called.");

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
            handle = 0;
        };

        ThreadHandle(const ThreadHandle &) = delete;
        ThreadHandle &operator=(const ThreadHandle &) = delete;

        ThreadHandle(ThreadHandle &&other) noexcept : params(other.params), handle(other.handle)
        {
            other.params = nullptr;
            other.handle = nullptr;
        }
        ThreadHandle &operator=(ThreadHandle &&other) noexcept
        {
            if (this != &other)
            {
                params = other.params;
                handle = other.handle;
                other.params = nullptr;
                other.handle = nullptr;
            }
            return *this;
        }

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

        static core::Result<core::ThreadHandle<Fn>, ThreadError> spawn(Fn &&fn)
        {
            core::ThreadHandle<Fn> h{core::forward<Fn>(fn)}; // OOM here -> panic
            if (Result<Unit, ThreadError> r = h.spawn(); r.isErr())
                return core::Err{r.takeError()};
            return core::move(h); // hand the running thread out
        }

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
        MLW_FORCE_INLINE bool joinable() const {
            if (!params || handle == 0) return false; 
        }

        //detach()
    };

} // namespace core
