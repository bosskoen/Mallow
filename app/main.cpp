#include <core/macro.h>
#include <core/libc/mem.h>

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
u64 infBits = 0x7FF0000000000000ULL;
u64 negInfBits = 0xFFF0000000000000ULL;
u64 nanBits = 0x7FF8000000000000ULL;

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
u32 fInfBits = 0x7F800000U;
f32 fInf;
core::mlwMemcpy(&fInf, &fInfBits, sizeof(f32));
println("float inf:   {}", fInf);
}
int mallowMain() {
    i32 x[3] = {42, 2, 1};
    const i32* const p = nullptr;
    println("Hello, {}! {} {} {} {} {}", "world", 2, 3.14, &x, x, p);
    //panic("", "s");
    
    core::CStr thing = core::CStr::CStr("world!"); 
    print("Hello, {}! {} {} {} {} {}", thing, 2, 3.14, &x, x, p);
    float_test();
    return 0;
}