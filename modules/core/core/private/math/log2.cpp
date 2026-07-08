#include "libc/math.h"
#include "math.h"
//======================================================================
//  mlw_log2_internal.h
//  Private glue for the double-precision log2 port
//  (adapted from glibc, LGPL-2.1-or-later).
//
//  Include this ONLY in mlw_log2.cpp and in your table file
//  (mlw_log2_data.cpp). It defines short macros (N, OFF, ...) that you
//  do NOT want leaking into the rest of the code base, so keep it out
//  of public headers.
//======================================================================

 
namespace core {
 


 
// Only matters if you ever run under a non-default FP rounding mode.
#define WANT_ROUNDING 1
 
//----------------------------------------------------------------------
// Error results. Freestanding: we just return the correct IEEE value,
// no errno and no FP exception flags.
// Do NOT compile the log2 files with -ffast-math / -ffinite-math-only,
// or these specials can be optimized away.
//----------------------------------------------------------------------
static MLW_FORCE_INLINE f64 mlwMathDivzero(uint32 sign)   // log2(0)              -> -inf
{
    return sign ? -NumericLimits<f64>::infinity : NumericLimits<f64>::infinity;
}
static MLW_FORCE_INLINE f64 mlwMathInvalid(f64 /*x*/)     // log2(<0) or log2(nan) -> nan
{
    return NumericLimits<f64>::nan;
}
 
//----------------------------------------------------------------------
// Table shape. These stay MACROS on purpose: your table file uses them
// inside #if guards ("#if N == 64"), which only the preprocessor reads.
//----------------------------------------------------------------------
#define LOG2_TABLE_BITS  6
#define LOG2_POLY_ORDER  7
#define LOG2_POLY1_ORDER 11
#define N   (1 << LOG2_TABLE_BITS)
#define OFF 0x3fe6000000000000ull
 
//----------------------------------------------------------------------
// IMPORTANT: poly1 is declared BEFORE poly. glibc's table initializes
// them in that order, and C++20 requires designated initializers to
// follow declaration order -- so the struct must match or you get a
// compile error. The function refers to them by name, so this ordering
// does not affect the math at all.
//----------------------------------------------------------------------
struct log2_data
{
    f64 invln2hi;
    f64 invln2lo;
    f64 poly1[LOG2_POLY1_ORDER - 1];
    f64 poly [LOG2_POLY_ORDER  - 1];
    struct { f64 invc, logc; } tab[N];
#ifndef __FP_FAST_FMA
    struct { f64 chi, clo; } tab2[N];
#endif
};
 

#define LOG2F_TABLE_BITS 4
#define LOG2F_POLY_ORDER 4

// NOTE: tab is DOUBLE precision even though the function is float —
// log2f does its internal math in double. poly has POLY_ORDER entries
// (not -1): the code uses A[0]..A[3].
struct log2f_data
{
    struct { f64 invc, logc; } tab[1 << LOG2F_TABLE_BITS];
    f64 poly[LOG2F_POLY_ORDER];
};

inline f32 mlwMathDivzerof(uint32 sign)   // log2f(0) -> -inf
{
    return sign ? -NumericLimits<f32>::infinity : NumericLimits<f32>::infinity;
}
inline f32 mlwMathInvalidf(f32 /*x*/)     // log2f(<0)/nan -> nan
{
    return NumericLimits<f32>::nan;
}
 
} // namespace core

constexpr core::log2f_data mlwLog2fData = {
  .tab = {
  { 0x1.661ec79f8f3bep+0, -0x1.efec65b963019p-2 },
  { 0x1.571ed4aaf883dp+0, -0x1.b0b6832d4fca4p-2 },
  { 0x1.49539f0f010bp+0, -0x1.7418b0a1fb77bp-2 },
  { 0x1.3c995b0b80385p+0, -0x1.39de91a6dcf7bp-2 },
  { 0x1.30d190c8864a5p+0, -0x1.01d9bf3f2b631p-2 },
  { 0x1.25e227b0b8eap+0, -0x1.97c1d1b3b7afp-3 },
  { 0x1.1bb4a4a1a343fp+0, -0x1.2f9e393af3c9fp-3 },
  { 0x1.12358f08ae5bap+0, -0x1.960cbbf788d5cp-4 },
  { 0x1.0953f419900a7p+0, -0x1.a6f9db6475fcep-5 },
  { 0x1p+0, 0x0p+0 },
  { 0x1.e608cfd9a47acp-1, 0x1.338ca9f24f53dp-4 },
  { 0x1.ca4b31f026aap-1, 0x1.476a9543891bap-3 },
  { 0x1.b2036576afce6p-1, 0x1.e840b4ac4e4d2p-3 },
  { 0x1.9c2d163a1aa2dp-1, 0x1.40645f0c6651cp-2 },
  { 0x1.886e6037841edp-1, 0x1.88e9c2c1b9ff8p-2 },
  { 0x1.767dcf5534862p-1, 0x1.ce0a44eb17bccp-2 },
  },
  .poly = {
  -0x1.712b6f70a7e4dp-2, 0x1.ecabf496832ep-2, -0x1.715479ffae3dep-1,
  0x1.715475f35c8b8p0,
  }
};


constexpr core::log2_data mlwLog2Data = {
// First coefficient: 0x1.71547652b82fe1777d0ffda0d24p0
.invln2hi = 0x1.7154765200000p+0,
.invln2lo = 0x1.705fc2eefa200p-33,
.poly1 = {
#if LOG2_POLY1_ORDER == 11
// relative error: 0x1.2fad8188p-63
// in -0x1.5b51p-5 0x1.6ab2p-5
-0x1.71547652b82fep-1,
0x1.ec709dc3a03f7p-2,
-0x1.71547652b7c3fp-2,
0x1.2776c50f05be4p-2,
-0x1.ec709dd768fe5p-3,
0x1.a61761ec4e736p-3,
-0x1.7153fbc64a79bp-3,
0x1.484d154f01b4ap-3,
-0x1.289e4a72c383cp-3,
0x1.0b32f285aee66p-3,
#endif
},
.poly = {
#if N == 64 && LOG2_POLY_ORDER == 7
// relative error: 0x1.a72c2bf8p-58
// abs error: 0x1.67a552c8p-66
// in -0x1.f45p-8 0x1.f45p-8
-0x1.71547652b8339p-1,
0x1.ec709dc3a04bep-2,
-0x1.7154764702ffbp-2,
0x1.2776c50034c48p-2,
-0x1.ec7b328ea92bcp-3,
0x1.a6225e117f92ep-3,
#endif
},
/* Algorithm:

	x = 2^k z
	log2(x) = k + log2(c) + log2(z/c)
	log2(z/c) = poly(z/c - 1)

where z is in [1.6p-1; 1.6p0] which is split into N subintervals and z falls
into the ith one, then table entries are computed as

	tab[i].invc = 1/c
	tab[i].logc = (double)log2(c)
	tab2[i].chi = (double)c
	tab2[i].clo = (double)(c - (double)c)

where c is near the center of the subinterval and is chosen by trying +-2^29
floating point invc candidates around 1/center and selecting one for which

	1) the rounding error in 0x1.8p10 + logc is 0,
	2) the rounding error in z - chi - clo is < 0x1p-64 and
	3) the rounding error in (double)log2(c) is minimized (< 0x1p-68).

Note: 1) ensures that k + logc can be computed without rounding error, 2)
ensures that z/c - 1 can be computed as (z - chi - clo)*invc with close to a
single rounding error when there is no fast fma for z*invc - 1, 3) ensures
that logc + poly(z/c - 1) has small error, however near x == 1 when
|log2(x)| < 0x1p-4, this is not enough so that is special cased.  */
.tab = {
#if N == 64
{0x1.724286bb1acf8p+0, -0x1.1095feecdb000p-1},
{0x1.6e1f766d2cca1p+0, -0x1.08494bd76d000p-1},
{0x1.6a13d0e30d48ap+0, -0x1.00143aee8f800p-1},
{0x1.661ec32d06c85p+0, -0x1.efec5360b4000p-2},
{0x1.623fa951198f8p+0, -0x1.dfdd91ab7e000p-2},
{0x1.5e75ba4cf026cp+0, -0x1.cffae0cc79000p-2},
{0x1.5ac055a214fb8p+0, -0x1.c043811fda000p-2},
{0x1.571ed0f166e1ep+0, -0x1.b0b67323ae000p-2},
{0x1.53909590bf835p+0, -0x1.a152f5a2db000p-2},
{0x1.5014fed61adddp+0, -0x1.9217f5af86000p-2},
{0x1.4cab88e487bd0p+0, -0x1.8304db0719000p-2},
{0x1.49539b4334feep+0, -0x1.74189f9a9e000p-2},
{0x1.460cbdfafd569p+0, -0x1.6552bb5199000p-2},
{0x1.42d664ee4b953p+0, -0x1.56b23a29b1000p-2},
{0x1.3fb01111dd8a6p+0, -0x1.483650f5fa000p-2},
{0x1.3c995b70c5836p+0, -0x1.39de937f6a000p-2},
{0x1.3991c4ab6fd4ap+0, -0x1.2baa1538d6000p-2},
{0x1.3698e0ce099b5p+0, -0x1.1d98340ca4000p-2},
{0x1.33ae48213e7b2p+0, -0x1.0fa853a40e000p-2},
{0x1.30d191985bdb1p+0, -0x1.01d9c32e73000p-2},
{0x1.2e025cab271d7p+0, -0x1.e857da2fa6000p-3},
{0x1.2b404cf13cd82p+0, -0x1.cd3c8633d8000p-3},
{0x1.288b02c7ccb50p+0, -0x1.b26034c14a000p-3},
{0x1.25e2263944de5p+0, -0x1.97c1c2f4fe000p-3},
{0x1.234563d8615b1p+0, -0x1.7d6023f800000p-3},
{0x1.20b46e33eaf38p+0, -0x1.633a71a05e000p-3},
{0x1.1e2eefdcda3ddp+0, -0x1.494f5e9570000p-3},
{0x1.1bb4a580b3930p+0, -0x1.2f9e424e0a000p-3},
{0x1.19453847f2200p+0, -0x1.162595afdc000p-3},
{0x1.16e06c0d5d73cp+0, -0x1.f9c9a75bd8000p-4},
{0x1.1485f47b7e4c2p+0, -0x1.c7b575bf9c000p-4},
{0x1.12358ad0085d1p+0, -0x1.960c60ff48000p-4},
{0x1.0fef00f532227p+0, -0x1.64ce247b60000p-4},
{0x1.0db2077d03a8fp+0, -0x1.33f78b2014000p-4},
{0x1.0b7e6d65980d9p+0, -0x1.0387d1a42c000p-4},
{0x1.0953efe7b408dp+0, -0x1.a6f9208b50000p-5},
{0x1.07325cac53b83p+0, -0x1.47a954f770000p-5},
{0x1.05197e40d1b5cp+0, -0x1.d23a8c50c0000p-6},
{0x1.03091c1208ea2p+0, -0x1.16a2629780000p-6},
{0x1.0101025b37e21p+0, -0x1.720f8d8e80000p-8},
{0x1.fc07ef9caa76bp-1, 0x1.6fe53b1500000p-7},
{0x1.f4465d3f6f184p-1, 0x1.11ccce10f8000p-5},
{0x1.ecc079f84107fp-1, 0x1.c4dfc8c8b8000p-5},
{0x1.e573a99975ae8p-1, 0x1.3aa321e574000p-4},
{0x1.de5d6f0bd3de6p-1, 0x1.918a0d08b8000p-4},
{0x1.d77b681ff38b3p-1, 0x1.e72e9da044000p-4},
{0x1.d0cb5724de943p-1, 0x1.1dcd2507f6000p-3},
{0x1.ca4b2dc0e7563p-1, 0x1.476ab03dea000p-3},
{0x1.c3f8ee8d6cb51p-1, 0x1.7074377e22000p-3},
{0x1.bdd2b4f020c4cp-1, 0x1.98ede8ba94000p-3},
{0x1.b7d6c006015cap-1, 0x1.c0db86ad2e000p-3},
{0x1.b20366e2e338fp-1, 0x1.e840aafcee000p-3},
{0x1.ac57026295039p-1, 0x1.0790ab4678000p-2},
{0x1.a6d01bc2731ddp-1, 0x1.1ac056801c000p-2},
{0x1.a16d3bc3ff18bp-1, 0x1.2db11d4fee000p-2},
{0x1.9c2d14967feadp-1, 0x1.406464ec58000p-2},
{0x1.970e4f47c9902p-1, 0x1.52dbe093af000p-2},
{0x1.920fb3982bcf2p-1, 0x1.651902050d000p-2},
{0x1.8d30187f759f1p-1, 0x1.771d2cdeaf000p-2},
{0x1.886e5ebb9f66dp-1, 0x1.88e9c857d9000p-2},
{0x1.83c97b658b994p-1, 0x1.9a80155e16000p-2},
{0x1.7f405ffc61022p-1, 0x1.abe186ed3d000p-2},
{0x1.7ad22181415cap-1, 0x1.bd0f2aea0e000p-2},
{0x1.767dcf99eff8cp-1, 0x1.ce0a43dbf4000p-2},
#endif
},
#ifndef __FP_FAST_FMA
.tab2 = {
# if N == 64
{0x1.6200012b90a8ep-1, 0x1.904ab0644b605p-55},
{0x1.66000045734a6p-1, 0x1.1ff9bea62f7a9p-57},
{0x1.69fffc325f2c5p-1, 0x1.27ecfcb3c90bap-55},
{0x1.6e00038b95a04p-1, 0x1.8ff8856739326p-55},
{0x1.71fffe09994e3p-1, 0x1.afd40275f82b1p-55},
{0x1.7600015590e1p-1, -0x1.2fd75b4238341p-56},
{0x1.7a00012655bd5p-1, 0x1.808e67c242b76p-56},
{0x1.7e0003259e9a6p-1, -0x1.208e426f622b7p-57},
{0x1.81fffedb4b2d2p-1, -0x1.402461ea5c92fp-55},
{0x1.860002dfafcc3p-1, 0x1.df7f4a2f29a1fp-57},
{0x1.89ffff78c6b5p-1, -0x1.e0453094995fdp-55},
{0x1.8e00039671566p-1, -0x1.a04f3bec77b45p-55},
{0x1.91fffe2bf1745p-1, -0x1.7fa34400e203cp-56},
{0x1.95fffcc5c9fd1p-1, -0x1.6ff8005a0695dp-56},
{0x1.9a0003bba4767p-1, 0x1.0f8c4c4ec7e03p-56},
{0x1.9dfffe7b92da5p-1, 0x1.e7fd9478c4602p-55},
{0x1.a1fffd72efdafp-1, -0x1.a0c554dcdae7ep-57},
{0x1.a5fffde04ff95p-1, 0x1.67da98ce9b26bp-55},
{0x1.a9fffca5e8d2bp-1, -0x1.284c9b54c13dep-55},
{0x1.adfffddad03eap-1, 0x1.812c8ea602e3cp-58},
{0x1.b1ffff10d3d4dp-1, -0x1.efaddad27789cp-55},
{0x1.b5fffce21165ap-1, 0x1.3cb1719c61237p-58},
{0x1.b9fffd950e674p-1, 0x1.3f7d94194cep-56},
{0x1.be000139ca8afp-1, 0x1.50ac4215d9bcp-56},
{0x1.c20005b46df99p-1, 0x1.beea653e9c1c9p-57},
{0x1.c600040b9f7aep-1, -0x1.c079f274a70d6p-56},
{0x1.ca0006255fd8ap-1, -0x1.a0b4076e84c1fp-56},
{0x1.cdfffd94c095dp-1, 0x1.8f933f99ab5d7p-55},
{0x1.d1ffff975d6cfp-1, -0x1.82c08665fe1bep-58},
{0x1.d5fffa2561c93p-1, -0x1.b04289bd295f3p-56},
{0x1.d9fff9d228b0cp-1, 0x1.70251340fa236p-55},
{0x1.de00065bc7e16p-1, -0x1.5011e16a4d80cp-56},
{0x1.e200002f64791p-1, 0x1.9802f09ef62ep-55},
{0x1.e600057d7a6d8p-1, -0x1.e0b75580cf7fap-56},
{0x1.ea00027edc00cp-1, -0x1.c848309459811p-55},
{0x1.ee0006cf5cb7cp-1, -0x1.f8027951576f4p-55},
{0x1.f2000782b7dccp-1, -0x1.f81d97274538fp-55},
{0x1.f6000260c450ap-1, -0x1.071002727ffdcp-59},
{0x1.f9fffe88cd533p-1, -0x1.81bdce1fda8bp-58},
{0x1.fdfffd50f8689p-1, 0x1.7f91acb918e6ep-55},
{0x1.0200004292367p+0, 0x1.b7ff365324681p-54},
{0x1.05fffe3e3d668p+0, 0x1.6fa08ddae957bp-55},
{0x1.0a0000a85a757p+0, -0x1.7e2de80d3fb91p-58},
{0x1.0e0001a5f3fccp+0, -0x1.1823305c5f014p-54},
{0x1.11ffff8afbaf5p+0, -0x1.bfabb6680bac2p-55},
{0x1.15fffe54d91adp+0, -0x1.d7f121737e7efp-54},
{0x1.1a00011ac36e1p+0, 0x1.c000a0516f5ffp-54},
{0x1.1e00019c84248p+0, -0x1.082fbe4da5dap-54},
{0x1.220000ffe5e6ep+0, -0x1.8fdd04c9cfb43p-55},
{0x1.26000269fd891p+0, 0x1.cfe2a7994d182p-55},
{0x1.2a00029a6e6dap+0, -0x1.00273715e8bc5p-56},
{0x1.2dfffe0293e39p+0, 0x1.b7c39dab2a6f9p-54},
{0x1.31ffff7dcf082p+0, 0x1.df1336edc5254p-56},
{0x1.35ffff05a8b6p+0, -0x1.e03564ccd31ebp-54},
{0x1.3a0002e0eaeccp+0, 0x1.5f0e74bd3a477p-56},
{0x1.3e000043bb236p+0, 0x1.c7dcb149d8833p-54},
{0x1.4200002d187ffp+0, 0x1.e08afcf2d3d28p-56},
{0x1.460000d387cb1p+0, 0x1.20837856599a6p-55},
{0x1.4a00004569f89p+0, -0x1.9fa5c904fbcd2p-55},
{0x1.4e000043543f3p+0, -0x1.81125ed175329p-56},
{0x1.51fffcc027f0fp+0, 0x1.883d8847754dcp-54},
{0x1.55ffffd87b36fp+0, -0x1.709e731d02807p-55},
{0x1.59ffff21df7bap+0, 0x1.7f79f68727b02p-55},
{0x1.5dfffebfc3481p+0, -0x1.180902e30e93ep-54},
# endif
},
#endif /* __FP_FAST_FMA */
};


f64 core::mlwLog2(f64 x)
{
   // Local aliases so the ported body stays close to upstream without
    // leaking short macro names (T, A, B, ...) into the code base.
    const auto& T  = mlwLog2Data.tab;
#ifndef __FP_FAST_FMA
    const auto& T2 = mlwLog2Data.tab2;
#endif
    const auto& A  = mlwLog2Data.poly;
    const auto& B  = mlwLog2Data.poly1;
    const f64   InvLn2hi = mlwLog2Data.invln2hi;
    const f64   InvLn2lo = mlwLog2Data.invln2lo;
 
    f64 z, r, r2, r4, y, invc, logc, kd, hi, lo, t1, t2, t3, p;
    uint64 ix, iz, tmp;
    uint32 top;
    int32 k, i;
 
    ix  = mlwBitCast<uint64>(x);
    top = (uint32)(ix >> 48);
 
    const uint64 LO = mlwBitCast<uint64>(1.0 - 0x1.5b51p-5);
    const uint64 HI = mlwBitCast<uint64>(1.0 + 0x1.6ab2p-5);
    if (MLW_UNLIKELY(ix - LO < HI - LO))
    {
        // Handle inputs close to 1.0 separately.
        // Fix sign of zero with downward rounding when x == 1.
        if (WANT_ROUNDING && MLW_UNLIKELY(ix == mlwBitCast<uint64>(1.0)))
            return 0;
        r = x - 1.0;
#ifdef __FP_FAST_FMA
        hi = r * InvLn2hi;
        lo = r * InvLn2lo + __builtin_fma(r, InvLn2hi, -hi);
#else
        f64 rhi, rlo;
        rhi = mlwBitCast<f64>(mlwBitCast<uint64>(r) & (~0ull << 32));
        rlo = r - rhi;
        hi = rhi * InvLn2hi;
        lo = rlo * InvLn2hi + r * InvLn2lo;
#endif
        r2 = r * r;
        r4 = r2 * r2;
        p = r2 * (B[0] + r * B[1]);
        y = hi + p;
        lo += hi - y + p;
        lo += r4 * (B[2] + r * B[3] + r2 * (B[4] + r * B[5])
                    + r4 * (B[6] + r * B[7] + r2 * (B[8] + r * B[9])));
        y += lo;
        return y;
    }
    if (MLW_UNLIKELY(top - 0x0010 >= 0x7ff0 - 0x0010))
    {
        // x < 0x1p-1022, or inf, or nan.
        if (ix * 2 == 0)
            return mlwMathDivzero(1);
        if (ix == mlwBitCast<uint64>(NumericLimits<f64>::infinity)) // log2(inf) == inf
            return x;
        if ((top & 0x8000) || (top & 0x7ff0) == 0x7ff0)
            return mlwMathInvalid(x);
        // x is subnormal, normalize it.
        ix  = mlwBitCast<uint64>(x * 0x1p52);
        ix -= 52ull << 52;
    }
 
    // x = 2^k z; z in [OFF, 2*OFF), split into N subintervals; the ith
    // subinterval holds z and c is near its center.
    tmp  = ix - OFF;
    i    = (int32)((tmp >> (52 - LOG2_TABLE_BITS)) % N);
    k    = (int32)((int64)tmp >> 52);          // arithmetic shift
    iz   = ix - (tmp & (0xfffull << 52));
    invc = T[i].invc;
    logc = T[i].logc;
    z    = mlwBitCast<f64>(iz);
    kd   = (f64)k;
 
    // log2(x) = log2(z/c) + log2(c) + k;  r ~= z/c - 1, |r| < 1/(2*N).
#ifdef __FP_FAST_FMA
    r  = __builtin_fma(z, invc, -1.0);
    t1 = r * InvLn2hi;
    t2 = r * InvLn2lo + __builtin_fma(r, InvLn2hi, -t1);
#else
    f64 rhi, rlo;
    r   = (z - T2[i].chi - T2[i].clo) * invc;
    rhi = mlwBitCast<f64>(mlwBitCast<uint64>(r) & (~0ull << 32));
    rlo = r - rhi;
    t1  = rhi * InvLn2hi;
    t2  = rlo * InvLn2hi + r * InvLn2lo;
#endif
 
    // hi + lo = r/ln2 + log2(c) + k.
    t3 = kd + logc;
    hi = t3 + t1;
    lo = t3 - hi + t1 + t2;
 
    // log2(r+1) = r/ln2 + r^2*poly(r).
    r2 = r * r;
    r4 = r2 * r2;
    p  = A[0] + r * A[1] + r2 * (A[2] + r * A[3]) + r4 * (A[4] + r * A[5]);
    y  = lo + r2 * p + hi;
    return y;
}


f32 core::mlwLog2(f32 x)
{
    const auto& T = mlwLog2fData.tab;
    const auto& A = mlwLog2fData.poly;

    f64 z, r, r2, p, y, y0, invc, logc;
    uint32 ix, iz, top, tmp;
    int32 k, i;

    ix = mlwBitCast<uint32>(x);

#if WANT_ROUNDING
    // Fix sign of zero with downward rounding when x == 1.
    if (MLW_UNLIKELY(ix == 0x3f800000u))
        return 0.0f;
#endif
    if (MLW_UNLIKELY(ix - 0x00800000u >= 0x7f800000u - 0x00800000u))
    {
        // x < 0x1p-126, or inf, or nan.
        if (ix * 2 == 0)
            return mlwMathDivzerof(1);
        if (ix == 0x7f800000u)                       // log2(inf) == inf
            return x;
        if ((ix & 0x80000000u) || ix * 2 >= 0xff000000u)
            return mlwMathInvalidf(x);
        // subnormal: normalize
        ix  = mlwBitCast<uint32>(x * 0x1p23f);
        ix -= 23u << 23;
    }

    // x = 2^k z; z in [OFF, 2*OFF], split into N subintervals.
    tmp  = ix - OFF;
    i    = (int32)((tmp >> (23 - LOG2F_TABLE_BITS)) % N);
    top  = tmp & 0xff800000u;
    iz   = ix - top;
    k    = (int32)tmp >> 23;                          // arithmetic shift
    invc = T[i].invc;
    logc = T[i].logc;
    z    = (f64) mlwBitCast<f32>(iz);

    // log2(x) = log1p(z/c - 1)/ln2 + log2(c) + k
    r  = z * invc - 1.0;
    y0 = logc + (f64) k;

    r2 = r * r;
    y  = A[1] * r + A[2];
    y  = A[0] * r2 + y;
    p  = A[3] * r + y0;
    y  = y * r2 + p;
    return (f32) y;
}