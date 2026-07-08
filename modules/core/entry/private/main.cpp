#include <core/io/terminal.h>
#include <core/libc/process.h>
#include <core/memory/galloc.h>
#include <core/libc/mem.h>

#include <Windows.h>


extern "C" void WINAPI __dyn_tls_init(PVOID, DWORD, LPVOID);
extern "C" void run_cpp_ctors();

extern int32 mallowMain();

extern "C" void mlwStart(){
    core::detail::mlw__crt_init_platforminf();
    new (&core::mlw_g_alloc) core::GAlloc{};

    core::ThreadCache::mlw__first_crt_ctor();
    run_cpp_ctors();
    __dyn_tls_init(nullptr, DLL_THREAD_ATTACH, nullptr);


    //comand line parsing
    // core::terminal::args.count = c;
    // core::terminal::args.values = v;

    int32 cr =  mallowMain();

    core::mlwExit(cr);

    MLW_UNREACHABLE();
}