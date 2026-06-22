#include <core/thread/spinlock.h>
#include <core/thread/mutex.h>
#include <core/thread/candvar.h>

using namespace core::sync;



void testCondVar() {
    // test 1: predicate already true, should not block
    {
        Mutex mtx;
        CondVar cv;
        Lock lock(mtx);
        bool called = false;
        cv.wait(lock, [&]{ called = true; return true; });
        mlw_assert(called);
    }

    // test 2: predicate called until true
    {
        Mutex mtx;
        CondVar cv;
        Lock lock(mtx);
        int calls = 0;
        // this would infinite loop if wait doesn't check predicate first
        cv.wait(lock, [&]{ 
            calls++;
            return true;  // true on first call
        });
        mlw_assert(calls == 1);
    }

    // test 3: lock is re-held after wait returns
    {
        Mutex mtx;
        CondVar cv;
        Lock lock(mtx);
        cv.wait(lock, []{ return true; });
        mlw_assert(lock.isHeld());  // if you have this
    }

    // test 4: wakeOne and wakeAll don't crash on no waiters
    {
        CondVar cv;
        cv.wakeOne();
        cv.wakeAll();
    }

    // test 5: destructor safety (debug only)
    {
        CondVar cv;
        // just destructs cleanly with no waiters
    }
}


struct asdsa{
    bool x = true;

    const bool* ptr()const{ return &x;}

    template<core::FormatBuffer Buf>
    void format(Buf& buffer) const {
        const bool x = ptr();
        mlw_write(buffer, "this is a int: {}", x);
    }
};

f64 dev(f64 x, f64 v){
    return x/v;
}

void float_test(){
    // basic values
println("zero:        {}", 0.0);
println("one:         {}", 1.0);
println("negative:    {}", -1.0);

// decimal
println("pi:          {}", 3.14159265358979);
println("negative pi: {}", -3.14159265358979);

// precision trimming
println("1.5:         {}", 1.5);
println("1.0:         {}", 1.0);
println("1.25:        {}", 1.25);

// small values -> scientific
println("1e-4:        {}", 1e-4);   // boundary, should NOT go scientific
println("9.99e-5:     {}", 9.99e-5); // should go scientific
println("1e-10:       {}", 1e-10);
println("negative sm: {}", -1e-10);

// large values -> scientific  
println("1e15:        {}", 1e15);   // boundary, should NOT go scientific
println("1.01e15:     {}", 1.01e15); // should go scientific
println("1e20:        {}", 1e20);
println("negative lg: {}", -1e20);

// special
uint64 infBits = 0x7FF0000000000000ULL;
uint64 negInfBits = 0xFFF0000000000000ULL;
uint64 nanBits = 0x7FF8000000000000ULL;

f64 posInf, negInf, nan;
core::mlwMemcpy(&posInf, &infBits, sizeof(f64));
core::mlwMemcpy(&negInf, &negInfBits, sizeof(f64));
core::mlwMemcpy(&nan, &nanBits, sizeof(f64));

println("NaN:  {}", nan);
println("+Inf: {}", posInf);
println("-Inf: {}", negInf);

// float promoted to double
f32 f = 3.14f;
println("float:       {}", f);
uint32 fInfBits = 0x7F800000U;
f32 fInf;
core::mlwMemcpy(&fInf, &fInfBits, sizeof(f32));
println("float inf:   {}", fInf);
}
int mallowMain() {
    sint x[3] = {42, 2, 1};
    const sint* const p = nullptr;
    println("Hello, {}! {} {} {} {} {}", "world", 2, 3.14, &x, x, p);
    //panic("", "s");

    void* ptr = core::mlwAlignedAlloc(100, 64);
    static_cast<char*>(ptr)[0] = 1;
    print("some text\n");
    core::CStr thing = core::CStr{static_cast<char*>(ptr), 100}; 
    print("Hello, {}! {} {} {} {} {}", thing, 2, 3.14, &x, x, ptr);
    float_test();

    Mutex l{};

    Lock<Mutex> b{l};

    core::Optional<Lock<Mutex>> v{};

    Lock<Mutex>::tryLock(l, v);

    core::Optional<bool> opt_bool{true};

    Atomic<uint16> vase{2141};
    
    if(v.isNone()){
        println("hell {} {}", asdsa{}, vase);
    }

    testCondVar();

    return 0;
}