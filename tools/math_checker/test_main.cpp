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

#include <core/libc/math.h>

static uint32_t bits(float f){ uint32_t u; std::memcpy(&u,&f,4); return u; }

static uint64_t ulp_diff(float a, float b){
    int32_t ia=(int32_t)bits(a), ib=(int32_t)bits(b);
    if (ia < 0) ia = 0x80000000 - ia;
    if (ib < 0) ib = 0x80000000 - ib;
    return (uint64_t)std::abs((int64_t)ia - (int64_t)ib);
}

static uint64_t bits64(double d){ uint64_t u; std::memcpy(&u,&d,8); return u; }
static uint64_t ulp_diff64(double a, double b){
    int64_t ia=(int64_t)bits64(a), ib=(int64_t)bits64(b);
    if (ia < 0) ia = (int64_t)0x8000000000000000ull - ia;
    if (ib < 0) ib = (int64_t)0x8000000000000000ull - ib;
    return (uint64_t)std::llabs(ia - ib);
}


static std::atomic<uint64_t> g_done{0};

struct Result { uint64_t worst_ulp; uint64_t worst_in;  uint64_t count_0; uint64_t count_1; uint64_t count_hi;};

// sweep slice [lo, hi) — hi is uint64 so the last thread can reach 0x100000000
static Result sweep_expf(uint32_t thread_id, uint32_t nthreads)
{
    mpfr_t ref;
    mpfr_init2(ref, 200);

    Result r{0, 0, 0,0,0};
    uint64_t local = 0;

    // striped: this thread takes inputs thread_id, thread_id+nthreads, ...
    for (uint64_t u = thread_id; u <= 0xFFFFFFFFull; u += nthreads) {
        uint32_t uu = (uint32_t)u;
        float x; std::memcpy(&x, &uu, 4);

        if (!std::isnan(x)) {
            mpfr_set_flt(ref, x, MPFR_RNDN);
            mpfr_exp(ref, ref, MPFR_RNDN);
            float correct = mpfr_get_flt(ref, MPFR_RNDN);
            float got = core::mlwExp(x);
            if (!(std::isnan(correct) && std::isnan(got))) {
                uint64_t d = ulp_diff(got, correct);
                if (d > r.worst_ulp) { r.worst_ulp = d; r.worst_in = uu; }
                if (d == 0)      r.count_0++;
                else if (d == 1) r.count_1++;
                else             r.count_hi++; 
            }
        }
        if (((++local) & 0xFFFFF) == 0)
            g_done.fetch_add(0x100000, std::memory_order_relaxed);
    }
    g_done.fetch_add(local & 0xFFFFF, std::memory_order_relaxed);

    mpfr_clear(ref);
    return r;
}

static Result sweep_exp_f64(uint64_t seed, uint64_t count)
{
    mpfr_t ref; mpfr_init2(ref, 200);
    std::mt19937_64 rng(seed);            // per-thread RNG, seeded distinctly

    Result r{0,0,0,0,0};
    uint64_t local = 0;

    for (uint64_t n = 0; n < count; ++n) {
        // random double biased toward exp's interesting range.
        // exp overflows past ~709, underflows below ~-745, so sample [-750,750]
        // plus occasional full-range bit patterns for edge coverage.
        double x;
        if ((n & 7) == 0) {
            // 1/8 of samples: raw random bit pattern (catches weird inputs)
            uint64_t u = rng();
            std::memcpy(&x, &u, 8);
            if (!std::isfinite(x)) { --n; continue; }   // skip inf/nan, redo
        } else {
            // 7/8: uniform in the meaningful domain
            double t = (double)rng() / (double)UINT64_MAX;   // [0,1]
            x = -750.0 + t * 1500.0;                         // [-750, 750]
        }

        mpfr_set_d(ref, x, MPFR_RNDN);
        mpfr_exp(ref, ref, MPFR_RNDN);
        double correct = mpfr_get_d(ref, MPFR_RNDN);
        double got = core::mlwExp(x);

        if (std::isnan(correct) && std::isnan(got)) { }
        else {
            uint64_t d = ulp_diff64(got, correct);
            if (d == 0) r.count_0++;
            else if (d == 1) r.count_1++;
            else r.count_hi++;
            if (d > r.worst_ulp) {
                r.worst_ulp = d;
                r.worst_in = (uint32_t)(bits64(x) >> 32); // store hi word; see note
            }
        }
        if (((++local) & 0xFFFFF) == 0)
            g_done.fetch_add(0x100000, std::memory_order_relaxed);
    }
    g_done.fetch_add(local & 0xFFFFF, std::memory_order_relaxed);
    mpfr_clear(ref);
    return r;
}
int main()
{
    unsigned nthreads = std::thread::hardware_concurrency();
    if (!nthreads) nthreads = 4;

    const uint64_t SAMPLES_PER_THREAD = 100'000'000ull;   // 100M each; tune
    const uint64_t TOTAL_SAMPLES = SAMPLES_PER_THREAD * nthreads;
    g_done = 0;

    printf("exp (f64) sampled: %llu total on %u threads\n",
           (unsigned long long)TOTAL_SAMPLES, nthreads);

    auto t0 = std::chrono::steady_clock::now();

    std::vector<std::thread> pool;
    std::vector<Result> results(nthreads);
    for (unsigned i = 0; i < nthreads; ++i)
        pool.emplace_back([i, SAMPLES_PER_THREAD, &results]{
            results[i] = sweep_exp_f64(0x9E3779B97F4A7C15ull + i, SAMPLES_PER_THREAD);
        });

    // progress bar against TOTAL_SAMPLES
    while (true) {
        uint64_t done = g_done.load(std::memory_order_relaxed);
        double pct = 100.0 * (double)done / (double)TOTAL_SAMPLES;
        int bars = (int)(pct/2);
        printf("\r[");
        for (int b=0;b<50;b++) putchar(b<bars?'#':' ');
        printf("] %6.2f%%", pct);
        fflush(stdout);
        if (done >= TOTAL_SAMPLES) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    for (auto& t : pool) t.join();

    auto t1 = std::chrono::steady_clock::now();
    double secs = std::chrono::duration<double>(t1 - t0).count();

    Result o{0,0,0,0,0};
    for (auto& r : results) {
        if (r.worst_ulp > o.worst_ulp) { o.worst_ulp=r.worst_ulp; o.worst_in=r.worst_in; }
        o.count_0+=r.count_0; o.count_1+=r.count_1; o.count_hi+=r.count_hi;
    }
    printf("\nexp (f64) max ULP: %llu\n", (unsigned long long)o.worst_ulp);
    printf("  exact(0): %llu  faithful(1): %llu  WRONG(>=2): %llu\n",
           (unsigned long long)o.count_0,(unsigned long long)o.count_1,
           (unsigned long long)o.count_hi);
    printf("  time: %.1f s  (%.1f M samples/s)\n",
           secs, TOTAL_SAMPLES / secs / 1e6);
    return 0;
}