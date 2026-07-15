// ============================================================================
// ulp_sweep.cpp  —  multi-function correctness sweep against MPFR (one TU)
//
// Plug your implementation into the registry near the bottom (search "EDIT
// HERE"), pair it with its MPFR reference, and run everything in one go.
//
// The verdict column is your table-mixup alarm: a correct musl-style double
// routine is faithfully rounded (<1 ulp), so it should show ZERO results at
// >=2 ulp. If you accidentally feed exp's data table into exp2 (or expf into
// exp10, etc.) you get a flood of huge-ulp errors and the row is marked
// SUSPECT. That is exactly the mistake you're worried about.
//
// Build (adjust the include path / link flags for your project):
//   g++ -O2 -std=c++17 ulp_sweep.cpp -I/path/to/your/includes \
//       -lmpfr -lgmp -pthread -o ulp_sweep
//
// Run:
//   ./ulp_sweep              # runs every enabled test
//   ./ulp_sweep exp2 log     # runs only tests whose name contains these
// ============================================================================

#include <mpfr.h>
#include <cstdint>
#include <cstdio>
#include <cmath>
#include <cstring>
#include <thread>
#include <vector>
#include <atomic>
#include <chrono>
#include <random>
#include <functional>
#include <string>
#include <utility>

#include <core/libc/math.h>   // <-- your library

// ---------------------------------------------------------------------------
// bit / ulp helpers  (monotonic-key trick; handles signed zero, inf, nan)
// ---------------------------------------------------------------------------
static uint32_t bits32(float  f){ uint32_t u; std::memcpy(&u,&f,4); return u; }
static uint64_t bits64(double d){ uint64_t u; std::memcpy(&u,&d,8); return u; }

static uint32_t key32(float f){ uint32_t b=bits32(f); return (b>>31)? ~b : (b|0x80000000u); }
static uint64_t key64(double d){ uint64_t b=bits64(d); return (b>>63)? ~b : (b|0x8000000000000000ull); }

static uint64_t ulp32(float a, float b){
    if (a==b) return 0;                                   // covers eq finite, same-sign inf, +0/-0
    if (std::isnan(a) && std::isnan(b)) return 0;
    uint32_t ka=key32(a), kb=key32(b);
    return ka>kb ? (uint64_t)(ka-kb) : (uint64_t)(kb-ka);
}
static uint64_t ulp64(double a, double b){
    if (a==b) return 0;
    if (std::isnan(a) && std::isnan(b)) return 0;
    uint64_t ka=key64(a), kb=key64(b);
    return ka>kb ? (ka-kb) : (kb-ka);
}

// ---------------------------------------------------------------------------
// result accumulator
// ---------------------------------------------------------------------------
struct Result {
    uint64_t worst = 0;
    double   in0 = 0, in1 = 0;      // worst-case input(s)
    uint64_t bits0 = 0, bits1 = 0;  // raw bit pattern(s) of the worst input(s)
    uint64_t c0 = 0, c1 = 0, chi = 0;
    uint64_t total = 0;
};
static void merge(Result& o, const Result& r){
    if (r.worst > o.worst){ o.worst=r.worst; o.in0=r.in0; o.in1=r.in1; o.bits0=r.bits0; o.bits1=r.bits1; }
    o.c0+=r.c0; o.c1+=r.c1; o.chi+=r.chi; o.total+=r.total;
}

// ---------------------------------------------------------------------------
// test descriptor
// ---------------------------------------------------------------------------
enum class Kind { F32_EXHAUSTIVE, F64_UNARY, F64_BINARY, F32_BINARY  };

using RefUnary  = std::function<void(mpfr_t, const mpfr_t)>;             // rop = f(op)
using RefBinary = std::function<void(mpfr_t, const mpfr_t, const mpfr_t)>;// rop = f(op1,op2)
using Samp1     = std::function<double(std::mt19937_64&)>;
using Samp2     = std::function<std::pair<double,double>(std::mt19937_64&)>;

struct Test {
    std::string name;
    Kind        kind;
    bool        enabled = true;

    std::function<float (float)>          f32;   // impl under test (F32_EXHAUSTIVE)
    std::function<double(double)>         f64;   // impl under test (F64_UNARY)
    std::function<double(double,double)>  f64b;  // impl under test (F64_BINARY)
     std::function<float(float,float)> f32b;

    RefUnary  ref1;   // reference for unary tests
    RefBinary ref2;   // reference for binary tests
    Samp1     samp1;  // input sampler for F64_UNARY
    Samp2     samp2;  // input sampler for F64_BINARY
};

// wrap a plain mpfr function (mpfr_exp, mpfr_log, ...) as a unary reference
typedef int (*MpfrUnary)(mpfr_t, const mpfr_t, mpfr_rnd_t);
typedef int (*MpfrBinary)(mpfr_t, const mpfr_t, const mpfr_t, mpfr_rnd_t);
static RefUnary  unaryRef (MpfrUnary  fn){ return [fn](mpfr_t r, const mpfr_t x){ fn(r,x,MPFR_RNDN); }; }
static RefBinary binaryRef(MpfrBinary fn){ return [fn](mpfr_t r, const mpfr_t a, const mpfr_t b){ fn(r,a,b,MPFR_RNDN); }; }

// ---------------------------------------------------------------------------
// samplers
// ---------------------------------------------------------------------------
static double uniform(std::mt19937_64& g, double lo, double hi){
    double t = (double)g() / (double)UINT64_MAX;
    return lo + t*(hi-lo);
}
// full-dynamic-range positive finite (roughly log-uniform over the exponent)
static double anyPositive(std::mt19937_64& g){
    uint64_t u = g() & ~(1ull<<63);
    double d; std::memcpy(&d,&u,8);
    if (!std::isfinite(d) || d==0.0) return 1.0;
    return d;
}
// any finite double, both signs, full dynamic range
static double anyFinite(std::mt19937_64& g){
    uint64_t u = g();
    double d; std::memcpy(&d,&u,8);
    if (!std::isfinite(d)) return uniform(g, -1e6, 1e6);
    return d;
}

// log family: stress the near-1 cancellation region, plus wide range
static double sampLog(std::mt19937_64& g){
    switch (g() & 3u){
        case 0:  return uniform(g, 0.5, 2.0);
        case 1:  return uniform(g, 1e-8, 1e8);
        default: return anyPositive(g);
    }
}
// exp domain ~[-745,709]; sample wider + occasional raw pattern
static double sampExp(std::mt19937_64& g){
    if ((g()&7u)==0){ uint64_t u=g(); double d; std::memcpy(&d,&u,8); return std::isfinite(d)?d:0.0; }
    return uniform(g, -750.0, 750.0);
}
// exp2 domain ~[-1074,1024]
static double sampExp2(std::mt19937_64& g){
    if ((g()&7u)==0){ uint64_t u=g(); double d; std::memcpy(&d,&u,8); return std::isfinite(d)?d:0.0; }
    return uniform(g, -1100.0, 1100.0);
}
// exp10 domain ~[-323,308]
static double sampExp10(std::mt19937_64& g){
    if ((g()&7u)==0){ uint64_t u=g(); double d; std::memcpy(&d,&u,8); return std::isfinite(d)?d:0.0; }
    return uniform(g, -330.0, 330.0);
}
// pow: mix positive bases, near-1 bases, and negative-base/integer-exp cases
static std::pair<double,double> sampPow(std::mt19937_64& g){
    switch (g() & 7u){
        case 0:  return { -uniform(g,0.0,100.0), (double)(int)uniform(g,-20,20) }; // neg base, int exp
        case 1:  return { uniform(g,0.0,2.0),   uniform(g,-50,50) };               // near-1 base
        default: return { anyPositive(g),       uniform(g,-10,10) };               // wide base, moderate exp
    }
}

// ---------------------------------------------------------------------------
// per-thread runners
// ---------------------------------------------------------------------------
static Result runF32b(const Test& t, uint64_t seed, uint64_t count, std::atomic<uint64_t>& prog){
    mpfr_t a,b; mpfr_init2(a,120); mpfr_init2(b,120);
    std::mt19937_64 rng(seed);
    Result r; uint64_t local=0;
    for (uint64_t i=0;i<count;i++){
        auto pr = t.samp2(rng);                 // reuse the pow sampler
        float x=(float)pr.first, y=(float)pr.second;
        mpfr_set_flt(a, x, MPFR_RNDN);
        mpfr_set_flt(b, y, MPFR_RNDN);
        t.ref2(a, a, b);                         // mpfr_pow
        float correct = mpfr_get_flt(a, MPFR_RNDN);
        float got     = t.f32b(x,y);
        uint64_t d = ulp32(got, correct);
        (d==0? r.c0 : d==1? r.c1 : r.chi)++;
        if (d>r.worst){ r.worst=d; r.in0=x; r.in1=y; r.bits0=bits32(x); r.bits1=bits32(y); }
        r.total++;
        if ((++local & 0xFFFFF)==0) prog.fetch_add(0x100000, std::memory_order_relaxed);
    }
    prog.fetch_add(local & 0xFFFFF, std::memory_order_relaxed);
    mpfr_clear(a); mpfr_clear(b);
    return r;
}

static Result runF32(const Test& t, uint32_t tid, uint32_t n, std::atomic<uint64_t>& prog){
    mpfr_t ref; mpfr_init2(ref, 120);
    Result r; uint64_t local=0;
    for (uint64_t u=tid; u<=0xFFFFFFFFull; u+=n){
        uint32_t uu=(uint32_t)u; float x; std::memcpy(&x,&uu,4);
        mpfr_set_flt(ref, x, MPFR_RNDN);
        t.ref1(ref, ref);
        float correct = mpfr_get_flt(ref, MPFR_RNDN);
        float got     = t.f32(x);
        uint64_t d = ulp32(got, correct);
        (d==0? r.c0 : d==1? r.c1 : r.chi)++;
        if (d>r.worst){ r.worst=d; r.in0=x; r.bits0=uu; }
        r.total++;
        if ((++local & 0xFFFFF)==0) prog.fetch_add(0x100000, std::memory_order_relaxed);
    }
    prog.fetch_add(local & 0xFFFFF, std::memory_order_relaxed);
    mpfr_clear(ref);
    return r;
}
static Result runF64(const Test& t, uint64_t seed, uint64_t count, std::atomic<uint64_t>& prog){
    mpfr_t ref; mpfr_init2(ref, 200);
    std::mt19937_64 rng(seed);
    Result r; uint64_t local=0;
    for (uint64_t i=0;i<count;i++){
        double x = t.samp1(rng);
        mpfr_set_d(ref, x, MPFR_RNDN);
        t.ref1(ref, ref);
        double correct = mpfr_get_d(ref, MPFR_RNDN);
        double got     = t.f64(x);
        uint64_t d = ulp64(got, correct);
        (d==0? r.c0 : d==1? r.c1 : r.chi)++;
        if (d>r.worst){ r.worst=d; r.in0=x; r.bits0=bits64(x); }
        r.total++;
        if ((++local & 0xFFFFF)==0) prog.fetch_add(0x100000, std::memory_order_relaxed);
    }
    prog.fetch_add(local & 0xFFFFF, std::memory_order_relaxed);
    mpfr_clear(ref);
    return r;
}
static Result runF64b(const Test& t, uint64_t seed, uint64_t count, std::atomic<uint64_t>& prog){
    mpfr_t a,b; mpfr_init2(a,200); mpfr_init2(b,200);
    std::mt19937_64 rng(seed);
    Result r; uint64_t local=0;
    for (uint64_t i=0;i<count;i++){
        auto pr = t.samp2(rng);
        double x=pr.first, y=pr.second;
        mpfr_set_d(a, x, MPFR_RNDN);
        mpfr_set_d(b, y, MPFR_RNDN);
        t.ref2(a, a, b);
        double correct = mpfr_get_d(a, MPFR_RNDN);
        double got     = t.f64b(x,y);
        uint64_t d = ulp64(got, correct);
        (d==0? r.c0 : d==1? r.c1 : r.chi)++;
        if (d>r.worst){ r.worst=d; r.in0=x; r.in1=y; r.bits0=bits64(x); r.bits1=bits64(y); }
        r.total++;
        if ((++local & 0xFFFFF)==0) prog.fetch_add(0x100000, std::memory_order_relaxed);
    }
    prog.fetch_add(local & 0xFFFFF, std::memory_order_relaxed);
    mpfr_clear(a); mpfr_clear(b);
    return r;
}

// ---------------------------------------------------------------------------
// driver for a single test
// ---------------------------------------------------------------------------
static Result runTest(const Test& t, unsigned nthreads, uint64_t samplesPerThread){
    std::atomic<uint64_t> prog{0};
    const uint64_t total = (t.kind==Kind::F32_EXHAUSTIVE)
                         ? 0x100000000ull
                         : samplesPerThread * nthreads;

    printf("=> %-8s  (%s, %llu evals on %u threads)\n",
           t.name.c_str(),
           t.kind==Kind::F32_EXHAUSTIVE ? "exhaustive f32"
             : t.kind==Kind::F64_UNARY  ? "sampled f64"
                                        : "sampled f64 x2",
           (unsigned long long)total, nthreads);

    auto t0 = std::chrono::steady_clock::now();
    std::vector<std::thread> pool;
    std::vector<Result> res(nthreads);
    const uint64_t seed = 0x9E3779B97F4A7C15ull;

    for (unsigned i=0;i<nthreads;i++){
        pool.emplace_back([&,i]{
              if      (t.kind==Kind::F32_EXHAUSTIVE) res[i]=runF32 (t, i, nthreads, prog);
            else if (t.kind==Kind::F64_UNARY)      res[i]=runF64 (t, seed+i, samplesPerThread, prog);
            else if (t.kind==Kind::F64_BINARY)     res[i]=runF64b(t, seed+i, samplesPerThread, prog);
            else                                   res[i]=runF32b(t, seed+i, samplesPerThread, prog);
        });
    }

    while (true){
        uint64_t done = prog.load(std::memory_order_relaxed);
        double pct = 100.0*(double)done/(double)total;
        int bars=(int)(pct/2);
        printf("\r   [");
        for (int b=0;b<50;b++) putchar(b<bars?'#':' ');
        printf("] %6.2f%%", pct);
        fflush(stdout);
        if (done>=total) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    for (auto& th : pool) th.join();

    double secs = std::chrono::duration<double>(std::chrono::steady_clock::now()-t0).count();
    Result o; for (auto& r : res) merge(o, r);

    printf("\r   [##################################################] 100.00%%\n");
    printf("   max ulp: %llu   exact:%llu  faithful:%llu  WRONG(>=2):%llu   %.1fs (%.1f M/s)\n",
           (unsigned long long)o.worst,
           (unsigned long long)o.c0,(unsigned long long)o.c1,(unsigned long long)o.chi,
           secs, o.total/secs/1e6);
    if (t.kind==Kind::F64_BINARY)
        printf("   worst @ x=%.17g (0x%016llx), y=%.17g (0x%016llx)\n",
               o.in0,(unsigned long long)o.bits0, o.in1,(unsigned long long)o.bits1);
    else if (t.kind==Kind::F32_BINARY)
        printf("   worst @ x=%.9g (0x%08llx), y=%.9g (0x%08llx)\n",
               o.in0,(unsigned long long)o.bits0, o.in1,(unsigned long long)o.bits1);
    else if (t.kind==Kind::F64_UNARY)
        printf("   worst @ x=%.17g (0x%016llx)\n", o.in0,(unsigned long long)o.bits0);
    else
        printf("   worst @ x=%.9g (0x%08llx)\n", o.in0,(unsigned long long)o.bits0);
    printf("\n");
    return o;
}

// ===========================================================================
//  EDIT HERE  —  register the functions you want swept.
//
//  * Rename core::mlwXxx to match your actual API (the base file used
//    core::mlwExp for the double exp, so these are guesses; fix as needed).
//  * `enabled=false` to skip one; or pass names on the command line.
//  * The MPFR reference must match the function — that's the whole safety net.
// ===========================================================================
static std::vector<Test> buildTests(){
    std::vector<Test> T;

    // ---- double, single argument -----------------------------------------
    T.push_back({ "cbrt",  Kind::F64_UNARY, true, {},
                  [](double x){ return core::mlwCbrt (x); }, {}, {},
                  unaryRef(mpfr_cbrt),  {}, anyFinite, {} });

    T.push_back({ "exp",   Kind::F64_UNARY, true, {},
                  [](double x){ return core::mlwExp  (x); }, {}, {},
                  unaryRef(mpfr_exp),   {}, sampExp,   {} });

    T.push_back({ "exp2",  Kind::F64_UNARY, true, {},
                  [](double x){ return core::mlwExp2 (x); },{}, {},
                  unaryRef(mpfr_exp2),  {}, sampExp2,  {} });

    T.push_back({ "exp10", Kind::F64_UNARY, true, {},
                  [](double x){ return core::mlwExp10(x); },{}, {},
                  unaryRef(mpfr_exp10), {}, sampExp10, {} });

    T.push_back({ "log",   Kind::F64_UNARY, false, {},
                  [](double x){ return core::mlwLog  (x); },{}, {},
                  unaryRef(mpfr_log),   {}, sampLog,   {} });

    T.push_back({ "log2",  Kind::F64_UNARY, false, {},
                  [](double x){ return core::mlwLog2 (x); }, {},{},
                  unaryRef(mpfr_log2),  {}, sampLog,   {} });

    T.push_back({ "log10", Kind::F64_UNARY, true, {},
                  [](double x){ return core::mlwLog10(x); }, {},{},
                  unaryRef(mpfr_log10), {}, sampLog,   {} });

    // ---- double, two arguments --------------------------------------------
    T.push_back({ "pow",   Kind::F64_BINARY, true, {}, {},
                  [](double x,double y){ return core::mlwPow(x,y); },{},
                  {}, binaryRef(mpfr_pow), {}, sampPow });

        // ---- float, single argument (exhaustive: all 2^32, minutes not hours) --
    T.push_back({ "expf",   Kind::F32_EXHAUSTIVE, true,
                  [](float x){ return core::mlwExp  (x); }, {},{}, {},
                  unaryRef(mpfr_exp),   {}, {}, {} });

    T.push_back({ "exp2f",  Kind::F32_EXHAUSTIVE, true,
                  [](float x){ return core::mlwExp2 (x); }, {},{}, {},
                  unaryRef(mpfr_exp2),  {}, {}, {} });

    T.push_back({ "cbrtf",  Kind::F32_EXHAUSTIVE, true,
                  [](float x){ return core::mlwCbrt (x); }, {},{}, {},
                  unaryRef(mpfr_cbrt),  {}, {}, {} });

    T.push_back({ "exp10f", Kind::F32_EXHAUSTIVE, true,
                  [](float x){ return core::mlwExp10(x); }, {},{}, {},
                  unaryRef(mpfr_exp10), {}, {}, {} });

    // logf family: off until implemented
    T.push_back({ "logf",   Kind::F32_EXHAUSTIVE, false,
                  [](float x){ return core::mlwLog  (x); }, {},{}, {},
                  unaryRef(mpfr_log),   {}, {}, {} });
    T.push_back({ "log2f",  Kind::F32_EXHAUSTIVE, false,
                  [](float x){ return core::mlwLog2 (x); },{}, {}, {},
                  unaryRef(mpfr_log2),  {}, {}, {} });
    T.push_back({ "log10f", Kind::F32_EXHAUSTIVE, false,
                  [](float x){ return core::mlwLog10(x); }, {},{}, {},
                  unaryRef(mpfr_log10), {}, {}, {} });

     T.push_back({ "powf",  Kind::F32_BINARY, true,
                  {}, {}, {},   [](float x,float y){ return core::mlwPow(x,y); },       // f32, f64, f64b unused
                  {}, binaryRef(mpfr_pow),          // ref1 unused, ref2=pow
                  {}, sampPow                       // samp1 unused, samp2
                  });  // f32b

    // ---- optional: arbitrary-base log, ref = ln(x)/ln(base) ---------------
    // Enable and adjust the impl name to match your API (e.g. core::mlwLogB).
    // {
    //   Test lb;
    //   lb.name="logbase"; lb.kind=Kind::F64_BINARY; lb.enabled=true;
    //   lb.f64b=[](double x,double base){ return core::mlwLogB(x,base); };
    //   lb.ref2=[](mpfr_t r,const mpfr_t x,const mpfr_t base){
    //       mpfr_t lb2; mpfr_init2(lb2, mpfr_get_prec(r));
    //       mpfr_log(r, x, MPFR_RNDN);
    //       mpfr_log(lb2, base, MPFR_RNDN);
    //       mpfr_div(r, r, lb2, MPFR_RNDN);
    //       mpfr_clear(lb2);
    //   };
    //   lb.samp2=[](std::mt19937_64& g){ return std::make_pair(anyPositive(g), uniform(g,1.1,100.0)); };
    //   T.push_back(std::move(lb));
    // }

    return T;
}
// ===========================================================================

int main(int argc, char** argv){
    auto start = std::chrono::steady_clock::now();

    unsigned nthreads = std::thread::hardware_concurrency();
    if (!nthreads) nthreads = 4;

    // tune this to decide how long your PC gets to churn
    const uint64_t SAMPLES_PER_THREAD = 100'000'000ull;

    auto tests = buildTests();

    // optional name filter from argv
    auto wanted = [&](const std::string& n)->bool{
        if (argc<=1) return true;
        for (int i=1;i<argc;i++) if (n.find(argv[i])!=std::string::npos) return true;
        return false;
    };

    printf("threads=%u  samples/thread=%llu (f64 tests)\n\n",
           nthreads, (unsigned long long)SAMPLES_PER_THREAD);

    struct Row { std::string name; Result r; };
    std::vector<Row> summary;

    for (auto& t : tests){
        if (!t.enabled || !wanted(t.name)) continue;
        Result r = runTest(t, nthreads, SAMPLES_PER_THREAD);
        summary.push_back({t.name, r});
    }

    auto end = std::chrono::steady_clock::now();

auto elapsed = end - start;

auto hours = std::chrono::duration_cast<std::chrono::hours>(elapsed);
elapsed -= hours;

auto minutes = std::chrono::duration_cast<std::chrono::minutes>(elapsed);
elapsed -= minutes;

auto seconds = std::chrono::duration_cast<std::chrono::seconds>(elapsed);
elapsed -= seconds;

auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed);

printf("Elapsed: %02lld:%02lld:%02lld.%03lld\n",
       (long long)hours.count(),
       (long long)minutes.count(),
       (long long)seconds.count(),
       (long long)milliseconds.count());

    // ---- summary table: SUSPECT = has >=2 ulp results (likely wrong table) -
    printf("================= SUMMARY =================\n");
    printf("%-10s %10s %12s %14s  %s\n", "func", "max_ulp", "exact%%", "wrong(>=2)", "verdict");
    for (auto& s : summary){
        double exactpct = s.r.total ? 100.0*(double)s.r.c0/(double)s.r.total : 0.0;
        const char* v = (s.r.chi==0) ? "OK" : "*** SUSPECT ***";
        printf("%-10s %10llu %11.4f%% %14llu  %s\n",
               s.name.c_str(),
               (unsigned long long)s.r.worst, exactpct,
               (unsigned long long)s.r.chi, v);
    }
    printf("==========================================\n");
    printf("SUSPECT = produced >=2 ulp errors. A correct faithfully-rounded\n");
    printf("routine should be OK; a swapped data table lights this up hard.\n");
    return 0;
}