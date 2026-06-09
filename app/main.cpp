#include <core/macro.h>

int mallowMain() {
    i32 x[3] = {42, 2, 1};
    const i32* const p = nullptr;
    println("Hello, {}! {} {} {} {} {}", "world", 2, 3.14, &x, x, p);
    //panic("", "s");
    
    core::CStr thing = core::CStr::CStr("world!"); 
    print("Hello, {}! {} {} {} {} {}", thing, 2, 3.14, &x, x, p);

    return 0;
}