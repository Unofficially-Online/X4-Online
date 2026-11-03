/* FR30: the drone factors of get_attackstrength's drone path, one implementation for both executables (Option B; our
 * own code).
 *
 * get_attackstrength (Windows 0x4f43d0, Linux 0x1d1b9d0) adds every drone's damage (Windows 0x4f1eb0, Linux 0x1ded490:
 * per drone the weapon damage 0xf1140 / 0xf05ff0 times a float factor f, then the damage add). f starts as a global
 * factor and is scaled by percentages the drone path rolls; MSVC turned each "/ 100" of the source into "* 0.01f" and
 * one "/ pi" into "* (1/pi)", GCC kept the divisions, and the binary32 results differ in about a quarter of the cases
 * (both executables' own bytes, 3000 random inputs per site). FR29b live (fr29bw / vps-linux-FR29, tick 546, attacker
 * 965ad on 07336a): the first drone's f was 0x3f5b5eea on Windows and 0x3f5b5eeb on Linux from equal inputs, so its
 * damage 146.48154654384803 vs 146.4815567326765, the call's result 290.3193773927377 vs 290.31938758156616, and the
 * float the apply takes 0x439128e1 vs 0x439128e2: the 07336a shield split of tick 546. Read on both instruction
 * streams (our reading), the drone path's arithmetic is the same except at four places:
 *   spread  the drones' spread s = ((r + m) * 0.5) * p15 (r a draw below m, the attacker's larger extent): Windows
 *           s * 0x3ea2f983 (0x4f2100), Linux s / 0x40490fdb (pi, 0x1dedf9a). It sets the loss percentage the roll
 *           below draws from.
 *   time    the percentage p lost to the time limit: f *= ((100 - p) * 0.01f) * f1 (Windows 0x4f24c1, the multiply
 *           by xmm8 = 0x3c23d70a), Linux ((100 - p) / 100) * f1 (0x1dee22a).
 *   roll    the rolled loss x = (draw + q) * 0.5 below the percentage q: f *= ((100 - x) * 0.01f) * f1 (Windows
 *           0x4f252b), Linux (100 - x) / 100 (0x1dedb03). Tick 546: x = 0x40992f02, 0x3f73becc vs 0x3f73becd.
 *   miss    the threshold a draw must beat for a counted drone between 250 and 1000 of p16: Windows (p16 - 250) *
 *           0x39d1b718 (0x4f2778), Linux 0 + (0x3e99999a * (p16 - 250)) / 750 (0x1dee2c1; a lerp GCC inlined).
 * The rest (the draws, the clamps, the lerps GCC calls 0x1a48040 where MSVC inlines them, tanf through the portable
 * math, the damage scale and add) gives the same bits on both. This file is the four results in Windows' order;
 * native_gate_fp_redirect_drones = 'redirect' puts them in place of each native result on both executables
 * (lockstep_fp_redirect.h fdr_seen; the sites in lockstep_fp_redirect_win.h and linux/gate_fp_redirect.h). Strict IEEE
 * single precision: no fast-math, no contraction, the constants as the Windows image holds them. */
#ifndef LOCKSTEP_DRONE_FACTORS_H
#define LOCKSTEP_DRONE_FACTORS_H
#include <float.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#if defined(__FAST_MATH__) || !defined(FLT_EVAL_METHOD) || FLT_EVAL_METHOD!=0
#error "lockstep_drone_factors.h needs strict IEEE single precision (no fast-math, FLT_EVAL_METHOD 0)"
#endif
#if defined(__clang__)
#pragma clang fp contract(off)
#endif
#if defined(__FMA__)
#error "lockstep_drone_factors.h: build without FMA (a contracted multiply-add would round once)"
#endif
#define FDR_HUNDREDTH 0x1.47ae14p-7f   /* 0x3c23d70a: Windows [0x2cbe1a4] */
#define FDR_INV_PI 0x1.45f306p-2f      /* 0x3ea2f983: Windows [0x2cbe394] */
#define FDR_MISS_SLOPE 0x1.a36e30p-12f /* 0x39d1b718: Windows [0x2cbe0b8] = 0.3/750 rounded */
#define FDR_MISS_CAP 0x1.333334p-2f    /* 0x3e99999a: 0.3 */
/* What survives of a percentage p lost: (100 - p) / 100, as Windows evaluates it. */
static float fdr_keep(float p) {return (100.0f-p)*FDR_HUNDREDTH;}
/* The spread's angle factor: s / pi, as Windows evaluates it. */
static float fdr_spread(float s) {return s*FDR_INV_PI;}
/* The miss threshold of p16: 0 up to 250, 0.3 from 1000, linear between (Windows 0x4f274c..0x4f2784; an unordered
 * p16 takes the 0 branch as the native ja does). */
static float fdr_miss(float p16) {
    if(!(p16>250.0f)) return 0.0f;
    if(!(p16<1000.0f)) return FDR_MISS_CAP;
    return (p16-250.0f)*FDR_MISS_SLOPE;
}
enum {FDR_SPREAD=0,FDR_TIME=1,FDR_ROLL=2,FDR_MISS=3,FDR_SITES=4};
static float fdr_value(unsigned kind,float in) {
    return kind==FDR_SPREAD?fdr_spread(in):kind==FDR_MISS?fdr_miss(in):fdr_keep(in);
}
/* Known answers, each one Linux's own order gets wrong by an ulp: the tick-546 roll (x = 0x40992f02: Windows 0x3f73becc,
 * Linux 0x3f73becd), a time percentage (33.3: 0x3f2ac082 / 0x3f2ac083), the spread of 3 (0x3f747644 / 0x3f747645) and
 * the threshold at 400 and 600 (0x3d75c290 / 0x3d75c28f, 0x3e0f5c29 / 0x3e0f5c2a), with the threshold's ends. The
 * inputs pass through a volatile, so the compiler's own folding is not what is tested. fdr_selftest() is 1 when this
 * build computes all of them. */
static int fdr_selftest(void) {
    static const struct {unsigned kind;uint32_t in,out;} kat[]={
        {FDR_ROLL,0x40992f02u,0x3f73beccu},{FDR_TIME,0x42053333u,0x3f2ac082u},{FDR_SPREAD,0x40400000u,0x3f747644u},
        {FDR_MISS,0x43c80000u,0x3d75c290u},{FDR_MISS,0x44160000u,0x3e0f5c29u},{FDR_MISS,0x437a0000u,0x00000000u},
        {FDR_MISS,0x447a0000u,0x3e99999au},{FDR_MISS,0x42c80000u,0x00000000u},{FDR_TIME,0x00000000u,0x3f800000u}};
    for(unsigned i=0;i<sizeof(kat)/sizeof(kat[0]);i++) {
        volatile uint32_t in=kat[i].in;uint32_t b=in,o;float f;memcpy(&f,&b,4);
        const float r=fdr_value(kat[i].kind,f);memcpy(&o,&r,4);
        if(o!=kat[i].out) return 0;
    }
    /* the tick-546 drone: f = 0.9 * (keep(x) * 1.0) = 0x3f5b5eea (Linux 0x3f5b5eeb) */
    volatile uint32_t xb=0x40992f02u,gb=0x3f666666u;uint32_t a=xb,g=gb,o;float x,f31;memcpy(&x,&a,4);memcpy(&f31,&g,4);
    const float f=f31*(fdr_keep(x)*1.0f);memcpy(&o,&f,4);
    return o==0x3f5b5eeau;
}
#endif
