#pragma once
#include "../compilers.h"


extern "C" int32 __cdecl atexit(void(__cdecl* fn)(void));

namespace core{
    const extern thread_local uint32 thread_id;

	//UB if called from a thread that is not the main thread, or if called after the main thread has exited.
    MLW_NO_RETURN void mlwExit(int32 status);


   MLW_NO_RETURN void mlwTerminate(int32 status);

}