/* BezierSpline::Set's segment length estimate in Option B's canonical form; original code, shared by the Windows
 * gate (native_gate_fp_redirect, lockstep_fp_redirect_win.h) and the Linux preload's own-bytes test.
 *
 * Set (Linux 0x1a45a80, Windows 0x14e4890) stores the control points P0..P3 and estimates the segment's length as
 *   len = (((|P1-P0| + |P2-P1|) + |P3-P2|) + |P3-P0|) * 0.5,   |v| = sqrt(v0*v0 + (v1*v1 + v2*v2)),
 * v = Pj - Pi lane by lane in single precision (lane 3 unused), each sqrt written as rcp(rsqrt(x)), which the
 * exact-reciprocal rewrite makes sqrt(x) on both platforms (Linux's copied chains included). Windows' composite-spline
 * builders 0x857960 and 0x31c640 inline Set and sum pairwise, ((|P2-P1| + |P1-P0|) + (|P3-P0| + |P3-P2|)) * 0.5: the
 * same terms, another rounding. This is the source's left-to-right order with exact sqrt.
 * The stored length is then 1.0 where len compares equal to 0: Linux (ucomiss, jp over a cmove) keeps a NaN,
 * Windows' inlined tail (ucomiss, jne over a store of 1.0) also turns a NaN into 1.0. */
#ifndef X4_LOCKSTEP_SPLINE_LENGTH_H
#define X4_LOCKSTEP_SPLINE_LENGTH_H
#include <float.h>
#include <stdint.h>
#include <string.h>
#include <xmmintrin.h>
#if defined(__FAST_MATH__) || !defined(FLT_EVAL_METHOD) || FLT_EVAL_METHOD!=0
#error "lockstep_spline_length.h needs strict IEEE single precision (no fast-math, FLT_EVAL_METHOD 0)"
#endif
#if defined(__clang__)
#pragma clang fp contract(off)
#endif
#define SL_ONE 0x3f800000u
static inline float sl_sqrt(float x) {return _mm_cvtss_f32(_mm_sqrt_ss(_mm_set_ss(x)));} /* SQRTSS: correctly rounded */
/* |b - a| as Set computes it: lane products, then lane 0 + (lane 1 + lane 2). */
static inline float sl_distance(const float *a,const float *b) {
    const float x=b[0]-a[0],y=b[1]-a[1],z=b[2]-a[2];
    const float xx=x*x,yy=y*y,zz=z*z;
    return sl_sqrt(xx+(yy+zz));
}
/* The canonical length (before the zero rule) from the four control points, 16 bytes each (lanes 0-2 used). */
static inline uint32_t sl_length(const unsigned char points[64]) {
    float p[4][4];memcpy(p,points,64);
    const float d10=sl_distance(p[0],p[1]),d21=sl_distance(p[1],p[2]),d32=sl_distance(p[2],p[3]),d30=sl_distance(p[0],p[3]);
    const float len=(((d10+d21)+d32)+d30)*0.5f;
    uint32_t bits;memcpy(&bits,&len,4);return bits;
}
/* Windows' inlined association, for the tests: ((d21 + d10) + (d30 + d32)) * 0.5. */
static inline uint32_t sl_length_pairwise(const unsigned char points[64]) {
    float p[4][4];memcpy(p,points,64);
    const float d10=sl_distance(p[0],p[1]),d21=sl_distance(p[1],p[2]),d32=sl_distance(p[2],p[3]),d30=sl_distance(p[0],p[3]);
    const float len=((d21+d10)+(d30+d32))*0.5f;
    uint32_t bits;memcpy(&bits,&len,4);return bits;
}
/* FR13: the same builders' Bezier handle length where the caller left it 0, a third of the segment's chord d. Linux
 * (0x1151760 and every Set caller forming handles) divides, d / 3.0f; Windows' builders (0x857d19, 0x857df0,
 * 0x31c9a9, 0x31ca80) multiply by the rounded reciprocal, d * 0.333333343f (k, from the register): another rounding,
 * which then moves P1 = P0 + s*T0 (or P2 = P3 - s*T1) by one ulp of the absolute coordinate now and then. */
static inline uint32_t sl_handle(uint32_t d_bits) {
    float d,s;memcpy(&d,&d_bits,4);s=d/3.0f;
    uint32_t bits;memcpy(&bits,&s,4);return bits;
}
static inline uint32_t sl_handle_windows(uint32_t d_bits,uint32_t k_bits) {
    float d,k,s;memcpy(&d,&d_bits,4);memcpy(&k,&k_bits,4);s=d*k;
    uint32_t bits;memcpy(&bits,&s,4);return bits;
}
/* FR22: BezierSpline::ComputeLength(n) (Windows 0x14e4f20, the BezierSpline vtable's +0x28; Linux 0x1a37520, +0x30),
 * the segment's arc length as n chords, stored at the segment's +8 over Set's estimate: the highway splines' composite
 * ComputeLength(100) (Windows 0x97267a, called by Highway::SetSpline 0x5f8e80 in the load) and a path builder's
 * ComputeLength(10) (Windows 0x896642) call it. With the coefficients A, B, C at +0x10 / +0x20 / +0x30 (cubic,
 * quadratic, linear; 16 bytes each, lanes 0-2 used):
 *   len = 0; for i < n: len = |D(u_i, u_i+1)| + len,   D(a,b) = ((a-b)*C + (a*a-b*b)*B) + (a*(a*a)-b*(b*b))*A per lane,
 * |v| as in Set. Linux takes u_i = i/n; Windows u_i = i*(1/n), one more rounding (equal when n is a power of two),
 * which moves a highway segment's length by an ulp or two (8bd90's highway, FR21: segments 2 and 3), then the
 * composite's total and knots, the affine map and the highway's speeds. This is Linux's. */
static inline float sl_bezier_chord(const float *c,float a,float b) {
    const float d1=a-b,a2=a*a,b2=b*b,d2=a2-b2,d3=a*a2-b*b2;
    const float x=(d1*c[8]+d2*c[4])+d3*c[0],y=(d1*c[9]+d2*c[5])+d3*c[1],z=(d1*c[10]+d2*c[6])+d3*c[2];
    const float xx=x*x,yy=y*y,zz=z*z;
    return sl_sqrt(xx+(yy+zz));
}
static inline uint32_t sl_arc_length(const unsigned char coefficients[48],uint32_t n) {
    float c[12];memcpy(c,coefficients,48);
    const float nf=(float)n;float len=0.0f;
    for(uint32_t i=0;i<n;i++) len=sl_bezier_chord(c,(float)i/nf,(float)(i+1u)/nf)+len;
    uint32_t bits;memcpy(&bits,&len,4);return bits;
}
/* Windows' form, for the tests: k = 1/n, u_i = i*k. */
static inline uint32_t sl_arc_length_windows(const unsigned char coefficients[48],uint32_t n) {
    float c[12];memcpy(c,coefficients,48);
    float len=0.0f;
    if(n) {const float k=1.0f/(float)n;for(uint32_t i=0;i<n;i++) len=sl_bezier_chord(c,(float)i*k,(float)(i+1u)*k)+len;}
    uint32_t bits;memcpy(&bits,&len,4);return bits;
}
/* FR22: AffineSpline<3>'s constructor (Windows 0x65a580, Linux 0x15fc9e0) weighs each knot pair i of the composite
 * (position p, speed s) by q_i = (p_i - p_i-1) / ((sqrt(s_i) + sqrt(s_i-1)) * 0.5) (Linux 0x15fcae4: one rounding of
 * 2*dp/sum); total += q_i, knot_i = q_i + knot_i-1, then every knot / total. Windows forms (2.0 / sum) * dp (0x65a66e
 * divss, 0x65a672 mulss): another rounding, in about a fifth of the pairs whose sum is not a power of two. sum and dp
 * arrive as both platforms compute them (sqrtss, addss, subss). */
static inline uint32_t sl_affine_weight(uint32_t sum_bits,uint32_t dp_bits) {
    float sum,dp,q;memcpy(&sum,&sum_bits,4);memcpy(&dp,&dp_bits,4);q=dp/(sum*0.5f);
    uint32_t bits;memcpy(&bits,&q,4);return bits;
}
static inline uint32_t sl_affine_weight_windows(uint32_t sum_bits,uint32_t dp_bits) {
    float sum,dp,q;memcpy(&sum,&sum_bits,4);memcpy(&dp,&dp_bits,4);q=(2.0f/sum)*dp;
    uint32_t bits;memcpy(&bits,&q,4);return bits;
}
static inline int sl_is_zero(uint32_t bits) {return (bits&0x7fffffffu)==0;}
static inline int sl_is_nan(uint32_t bits) {return (bits&0x7fffffffu)>0x7f800000u;}
static inline uint32_t sl_stored_linux(uint32_t len) {return sl_is_zero(len)?SL_ONE:len;}
static inline uint32_t sl_stored_windows(uint32_t len) {return sl_is_zero(len) || sl_is_nan(len)?SL_ONE:len;}
#endif
