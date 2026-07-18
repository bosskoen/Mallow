#pragma once
#include "../compilers.h"

namespace core{
    const extern thread_local uint32 thread_id;

	//UB if called from a thread that is not the main thread, or if called after the main thread has exited.
    MLW_NO_RETURN void mlwExit(int32 status);

    //TODO thread exit?
    //TODO atexit


   MLW_NO_RETURN void mlwTerminate(int32 status);

}