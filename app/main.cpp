#include <core/thread/spinlock.h>
#include <core/thread/mutex.h>
#include <core/thread/candvar.h>
#include <core/variant.h>
#include <core/libc/mem.h>

using namespace core::sync;

using a_lot_of_types = core::Variant<int32, int64, float, double, char, bool, core::CStr, core::Optional<int>, core::Optional<float>, core::Optional<double>, core::Optional<char>, core::Optional<bool>, core::Optional<core::CStr>>;

void testCondVar() {
    // test 1: predicate already true, should not block
    //{
    //    Mutex mtx;
    //    CondVar cv;
    //    Lock lock(mtx);
    //    bool called = false;
    //    cv.wait(lock, [&]{ called = true; return true; });
    //    mlw_assert(called);
    //}

    //// test 2: predicate called until true
    //{
    //    Mutex mtx;
    //    CondVar cv;
    //    Lock lock(mtx);
    //    int calls = 0;
    //    // this would infinite loop if wait doesn't check predicate first
    //    cv.wait(lock, [&]{ 
    //        calls++;
    //        return true;  // true on first call
    //    });
    //    mlw_assert(calls == 1);
    //}

    //// test 3: lock is re-held after wait returns
    //{
    //    Mutex mtx;
    //    CondVar cv;
    //    Lock lock(mtx);
    //    cv.wait(lock, []{ return true; });
    //    mlw_assert(lock.isHeld());  // if you have this
    //}

    //// test 4: wakeOne and wakeAll don't crash on no waiters
    //{
    //    CondVar cv;
    //    cv.wakeOne();
    //    cv.wakeAll();
    //}

    //// test 5: destructor safety (debug only)
    //{
    //    CondVar cv;
    //    // just destructs cleanly with no waiters
    //}
}


struct asdsa{
    bool x = true;

    const bool* ptr()const{ return &x;}

    asdsa() { print("somthing dom\n"); }

    ~asdsa() { 1 + 1; x = false; }

    template<core::FormatBuffer Buf>
    void format(Buf& buffer) const {
        const bool x = *ptr();
        mlw_write(buffer, "this is a int: {}", x);
    }
};

void float_test(){

    static asdsa sadas{};
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
println("NaN:  {}", core::NumericLimits<f64>::nan);
println("+Inf: {}", core::NumericLimits<f64>::infinity);
println("-Inf: {}", -core::NumericLimits<f64>::infinity);
println("float inf:   {}", core::NumericLimits<f32>::infinity);
// float promoted to double
f32 f = 3.14f;
println("float:       {}", f);

}

extern "C" {
    int QueryPerformanceCounter(long long* count);
    int QueryPerformanceFrequency(long long* freq);
}

void numeric_limits_test(){
    println("u8  max: {}", core::NumericLimits<uint8>::max);   // 255
   println("i8  min: {}", core::NumericLimits<int8>::min);    // -128
    println("u32 max: {}", core::NumericLimits<uint32>::max);  // 4294967295
   println("i32 min: {}", core::NumericLimits<int32>::min);   // -2147483648
    println("u64 max: {}", core::NumericLimits<uint64>::max);  // 18446744073709551615
    println("i64 min: {}", core::NumericLimits<int64>::min);   // -9223372036854775808
    println("f32 eps: {}", core::NumericLimits<f32>::epsilon);
    println("f64 eps: {}", core::NumericLimits<f64>::epsilon);
    println("f32 inf: {}", core::NumericLimits<f32>::infinity);
    println("f64 nan: {}", core::NumericLimits<f64>::nan);
}

int32 mallowMain() {

    sint x[3] = {42, 2, 1};
    const sint* const p = nullptr;

    println("Hello, {}! {} {} {} {} {}", "world", 2, 3.14, &x, x, p);
    //panic("", "s");

    print("before alloc");
    void* ptr = core::mlwAlignedAlloc(100, 64);
    print("after alloc");
    static_cast<char*>(ptr)[0] = 1;
    print("some text\n");
    core::CStr thing = core::CStr{static_cast<char*>(ptr), 100}; 
    print("Hello, {}! {} {} {} {} {}\n", thing, 2, 3.14, &x, x, ptr);
    float_test();

    long long freq, t0, t1;
    QueryPerformanceFrequency(&freq);

    volatile f64 sink = 0;               // volatile so the loop can't be optimized away
    QueryPerformanceCounter(&t0);
    for (uint64 i = 0; i < 5'000'000; ++i) {
        f64 x = 0.001 * (i & 0x3FF);
        sink += core::mlwExp10(x);
    }
    QueryPerformanceCounter(&t1);
    f64 secs = double(t1 - t0) / double(freq);

	println("mlwExp10 took {} seconds", secs);

    QueryPerformanceFrequency(&freq);

    sink = 0;               // volatile so the loop can't be optimized away
    QueryPerformanceCounter(&t0);
    for (uint64 i = 0; i < 5'000'000; ++i) {
        f64 x = 0.001 * (i & 0x3FF);
        sink += core::mlwPow(10. , x);
    }
    QueryPerformanceCounter(&t1);
    secs = double(t1 - t0) / double(freq);
	println("mlwpow10 took {} seconds", secs);

    QueryPerformanceFrequency(&freq);

    volatile f32 sinkf = 0;               // volatile so the loop can't be optimized away
    QueryPerformanceCounter(&t0);
    for (uint64 i = 0; i < 5'000'000; ++i) {
        f32 x = 0.001f * (i & 0x3FF);
        sinkf += core::mlwExp10(x);
    }
    QueryPerformanceCounter(&t1);
    secs = double(t1 - t0) / double(freq);

    println("mlwExp10f took {} seconds", secs);

    QueryPerformanceFrequency(&freq);

    sinkf = 0;               // volatile so the loop can't be optimized away
    QueryPerformanceCounter(&t0);
    for (uint64 i = 0; i < 5'000'000; ++i) {
        f32 x = 0.001f * (i & 0x3FF);
        sinkf += core::mlwPow(10.0f, x);
    }
    QueryPerformanceCounter(&t1);
    secs = double(t1 - t0) / double(freq);
    println("mlwpow10f took {} seconds", secs);


    Mutex l{};

    Lock<Mutex> b{l};

    core::Optional<Lock<Mutex>> v{};

    Lock<Mutex>::tryLock(l, v);

    core::Optional<bool> opt_bool{true};

    Atomic<uint16> vase{2141};
    
    asdsa asddsad{};

    if(v.isNone()){
       println("hell {} {}", asddsad, vase);
    }

    testCondVar();
    numeric_limits_test();

	a_lot_of_types var = static_cast<int64>(42);
    println("{}", var);
    sint asf = 2;

    return 0;
}