
#include "core/thread/mutex.h"
#include "core/thread/lock.h"
#include "core/thread/candvar.h"
extern "C"{


static int const epoch_start = 1;
__declspec(thread) int _Init_thread_epoch = 1;
int _Init_global_epoch = 1; //no atomic store throw atomic prosses?

static core::sync::Mutex global_lock{};
static core::sync::CondVar global_cv{};

static constexpr int UNINITIOLISED = 0;
static constexpr int BEING_INITIOLISED = -1;
// initiolised >= 1;

void __cdecl _Init_thread_header(int* guard){
    // at this point it is know that the cariable is maby not initiolised

    core::sync::Lock l{global_lock};

    global_cv.wait(l, [guard]()->bool{ return *guard != BEING_INITIOLISED; });

    if(*guard == UNINITIOLISED){
        *guard = BEING_INITIOLISED;

        // give controll back to the initioliser and releace lock to stop blocking other initiolisesions
        return;
    }
     _Init_thread_epoch = _Init_global_epoch;

}

void __cdecl _Init_thread_footer(int* guard){
    core::sync::Lock l{global_lock};
    ++_Init_global_epoch;
    //reinterpret_cast<core::sync::Atomic<int>*>( guard)->store(_Init_global_epoch, core::sync::MemoryOrder::Release);
    *guard = _Init_global_epoch;
    _Init_thread_epoch = _Init_global_epoch;
    l.unlock();
    global_cv.wakeAll();
}

}