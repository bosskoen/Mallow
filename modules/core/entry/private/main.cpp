#include <core/io/terminal.h>
#include <core/libc/process.h>
#include <core/memory/galloc.h>
#include <core/libc/mem.h>

#include "core/../../private/crt_internals.h"

extern int32 mallowMain();

extern "C" void mlwStart(){
    core::detail::mlw__crt_init_platforminf();
    new (&core::mlw_g_alloc) core::GAlloc{};

    core::ThreadCache::mlw__first_crt_ctor();
    crt::run_global_ctors();
    crt::run_thread_local_ctors();


    //comand line parsing
    // core::terminal::args.count = c;
    // core::terminal::args.values = v;

    int32 cr =  mallowMain();

    core::mlwExit(cr);

    MLW_UNREACHABLE();
}