#include "libc/math.h"
#include "macro.h"
#include "libc/mem.h"

namespace {
    // e^x via range reduction + Taylor. Good to ~1e-9 for moderate x.
    f64 dirty_exp(f64 x) {
        if (x != x) return x;                 // NaN
        if (x > 709.0)  return 1e308 * 1e308; // overflow → inf
        if (x < -745.0) return 0.0;           // underflow

        // reduce: e^x = 2^k * e^r, k = round(x / ln2), r = x - k*ln2
        constexpr f64 LN2 = 0.6931471805599453;
        f64 kf = x / LN2;
        // round to nearest without <cmath>
        int64 k = (int64)(kf + (kf >= 0 ? 0.5 : -0.5));
        f64 r = x - (f64)k * LN2;

        // e^r, r in [-0.35, 0.35], Taylor to 8 terms
        f64 term = 1.0, sum = 1.0;
        for (int n = 1; n <= 10; ++n) {
            term *= r / (f64)n;
            sum  += term;
        }

        // multiply by 2^k via bit manipulation of the exponent
        // 2^k = reinterpret((1023 + k) << 52)
        if (k > 1023)  return 1e308 * 1e308;
        if (k < -1022) return 0.0;
        uint64 bits = (uint64)(1023 + k) << 52;
        f64 pow2;
        core::mlwMemcpy(&pow2, &bits, sizeof(pow2)); // or your mlwMemcpy
        return sum * pow2;
    }

    // ln(x) via range reduction + atanh series.
    f64 dirty_log(f64 x) {
        if (x != x)  return x;                 // NaN
        if (x < 0.0) return (x - x) / (x - x); // NaN (0/0)
        if (x == 0.0) return -1e308 * 1e308;   // -inf

        // decompose x = m * 2^e, m in [1,2), via the exponent bits
        uint64 bits;
        core::mlwMemcpy(&bits, &x, sizeof(bits));
        int64 e = (int64)((bits >> 52) & 0x7FF) - 1023;
        // set exponent to 1023 → mantissa in [1,2)
        bits = (bits & 0x000FFFFFFFFFFFFFull) | (1023ull << 52);
        f64 m;
        core::mlwMemcpy(&m, &bits, sizeof(m));

        // ln(m) = 2*atanh((m-1)/(m+1)), series on t
        f64 t = (m - 1.0) / (m + 1.0);
        f64 t2 = t * t;
        f64 sum = 0.0, tp = t;
        for (int n = 1; n <= 15; n += 2) {
            sum += tp / (f64)n;
            tp  *= t2;
        }
        constexpr f64 LN2 = 0.6931471805599453;
        return 2.0 * sum + (f64)e * LN2;
    }
}

f64 core::mlwPow(f64 base, f64 power) {
    // handle the cheap/exact cases so the common path isn't garbage
    if (power == 0.0) return 1.0;
    if (base == 1.0)  return 1.0;
    if (base == 0.0)  return power > 0.0 ? 0.0 : (1e308 * 1e308);

    // integer-power fast path: exact-ish, and handles negative base
    if (power == (f64)(int64)power && power >= -64.0 && power <= 64.0) {
        int64 p = (int64)power;
        bool neg = p < 0;
        if (neg) p = -p;
        f64 r = 1.0, b = base;
        while (p) { if (p & 1) r *= b; b *= b; p >>= 1; }
        return neg ? 1.0 / r : r;
    }

    // general path: base^power = e^(power * ln(base)); needs base > 0
    if (base < 0.0) return (base - base) / (base - base); // NaN: neg base, non-int power
    return dirty_exp(power * dirty_log(base));
}

f32 core::mlwPow(f32 base, f32 power) {
    return (f32)core::mlwPow((f64)base, (f64)power);
}