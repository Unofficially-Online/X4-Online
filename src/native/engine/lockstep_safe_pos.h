/* FR27: the GetSafePos solver, one portable implementation for both executables (Option B; our own code).
 *
 * Zone::GetSafePos and the sector's (Windows 0x835b80 / 0x786d10, Linux 0x1155360 / 0x1d36d90) push a random point
 * out of the zone's collision boxes with a solver (Windows 0x14e4320, Linux 0x1a4b9f0) that the two compilers
 * built from the same algorithm with different floating-point association, so the same inputs give results 1-2 ulp
 * apart (FR26 live: 084533, adbc2) and the AI's positions drift from there. Read offline on both executables and
 * replayed on the FR25 captures (both executables' own bytes under emulation reproduce the live bits), the first
 * difference of adbc2 is in the push-out: the probe segment's length 2|c| + 2R + 2r is ((|c| + R) + R) + ((r + r) +
 * |c|) on Windows and ((|c| + R) + r) * 2 on Linux, and the slab test's parameters are (bound - p) * (1/d) on Windows
 * and (bound - p) / d on Linux. This file is the solver once, in the order Windows evaluates it (with the armed
 * exact-reciprocal table: every rcp(rsqrt(x)) of it is sqrt(x), the lone rsqrt 1/sqrt(x) exactly rounded), so the
 * Windows results stay what they were and Linux takes them over; both executables' entries jump here
 * (native_gate_fp_redirect_solver = 'redirect': lockstep_fp_redirect.h fsp_run, the entries in lockstep_fp_redirect_win.h
 * and linux/gate_fp_redirect.h). Strict IEEE single precision: no fast-math,
 * no contraction, every sum in the order written; sinf/cosf/atan2f are the portable ones (src/native/portable_math,
 * the same the portable_math redirect gives both executables).
 *
 * The algorithm (Windows 0x14e4320 and callees, our reading):
 *   boxes: 0x70 bytes each: +0 half size, +0x10 centre, +0x20 radius, +0x30 position, +0x40/+0x50/+0x60 rotation rows.
 *   world := empty (half 0, centre 0, radius 0); p0 := *p.
 *   repeat: q := *p; for each box i of the first n: local := R^T (q - pos) (the rows as columns), and when local lies
 *     inside the box's centre +- half widened by the radius r on all three axes (strictly), the box is absorbed:
 *     world := its world-space bounds (identity shortcut, else the 8 corners through the 4x4 with a divide by w;
 *     0x14e1ad0/0x14e17d0) while |world.radius| < 1e-4, else world := world united with the box's corners through
 *     its transform (0x14e1bd0); *p := 0 + p0; then either the large-box push (world.radius > a9 > 0: along the axis
 *     of least penetration, y only when allowed) or the push-out (0x14e2040): when p0 lies inside world +- r, cast
 *     the segment p0 + normalize(dir) * L from inside world widened by 1.01 r and take its exit (0x14e26b0); when
 *     |a5| >= 1e-4 also try the directions at pitch/yaw +-a5 (yaw always, pitch when y is allowed) and keep the exit
 *     closest to the origin; *p := result - 0; swap box i with box n-1, n -= 1, start over. Stop when a pass absorbs
 *     nothing.
 *   A move direction (Windows' eighth argument, Linux r9) adds a swept-time factor per box (0x14e3980 and two
 *     helpers); that path is not reimplemented: fsp_solve returns FSP_NATIVE and the caller runs the native solver
 *     (counted). Linux's extra x flag (ecx; Windows has none and always tries the yaw pair) is ignored, counted when 0.
 * Comparisons keep the branch sense of the native comiss/ucomiss tests (an unordered compare goes where native goes);
 * min/max are the SSE forms (a < b ? a : b, a > b ? a : b). */
#ifndef LOCKSTEP_SAFE_POS_H
#define LOCKSTEP_SAFE_POS_H
#include <float.h>
#include <stdint.h>
#include <string.h>
#include "../portable_math/portable_math.h"
#if defined(__FAST_MATH__) || !defined(FLT_EVAL_METHOD) || FLT_EVAL_METHOD!=0
#error "lockstep_safe_pos.h needs strict IEEE single precision (no fast-math, FLT_EVAL_METHOD 0)"
#endif
#if defined(__clang__)
#pragma clang fp contract(off)
#endif
#define FSP_BOX 0x70u
#define FSP_MAX_BOXES 100000u
enum {FSP_DONE=0,FSP_NATIVE=1};
typedef struct {float h[4],c[4],r[4];} fsp_world; /* +0 half, +0x10 centre, +0x20 radius (and 12 bytes the copies carry) */
static float fsp_f(uint32_t bits) {float f;memcpy(&f,&bits,4);return f;}
static uint32_t fsp_u(float f) {uint32_t u;memcpy(&u,&f,4);return u;}
static float fsp_neg(float f) {return fsp_f(fsp_u(f)^0x80000000u);} /* xorps with the sign */
static float fsp_abs(float f) {return fsp_f(fsp_u(f)&0x7fffffffu);} /* andps with 0x7fffffff */
static float fsp_min(float a,float b) {return a<b?a:b;} /* minss a,b */
static float fsp_max(float a,float b) {return a>b?a:b;} /* maxss a,b */
static float fsp_sqrt(float x) { /* sqrtss: correctly rounded */
#if defined(__SSE__) || defined(__x86_64__) || defined(_M_X64)
    float r;__asm__("sqrtss %1, %0":"=x"(r):"x"(x));return r;
#else
    return __builtin_sqrtf(x);
#endif
}
static float fsp_len3(const float v[4]) { /* |v|: x*x + (y*y + z*z), the native order */
    const float x=v[0]*v[0],y=v[1]*v[1],z=v[2]*v[2];
    return fsp_sqrt(x+(y+z));
}
static void fsp_ld(float out[4],const void *p) {memcpy(out,p,16);}
static void fsp_st(void *p,const float v[4]) {memcpy(p,v,16);}
/* 0x14e17d0: the box (half, centre) through the 4x4 m (rows 0-2, then the translation row), 8 corners each divided by
 * its own w, their bounds as half/centre, radius |half|. */
static void fsp_box_transform(fsp_world *b,const float m[4][4]) {
    const float *h=b->h,*c=b->c;
    const float sxl=c[0]-h[0],sxh=h[0]+c[0],syl=c[1]-h[1],syh=h[1]+c[1],szl=c[2]-h[2],szh=h[2]+c[2];
    float A[4],B[4],C[4],D[4],E[4],F[4],G[4],H[4];
    for(int k=0;k<4;k++) {
        const float xl=sxl*m[0][k],xh=sxh*m[0][k],yl=syl*m[1][k],yh=syh*m[1][k],zl=szl*m[2][k],zh=szh*m[2][k];
        A[k]=(zl+(xl+yl))+m[3][k];
        B[k]=(zh+(xl+yl))+m[3][k];
        C[k]=((yh+xl)+zl)+m[3][k];
        D[k]=((xh+yl)+zl)+m[3][k];
    }
    const float aw=A[3],bw=B[3],cw=C[3],dw=D[3];
    for(int k=0;k<4;k++) {A[k]=A[k]/aw;B[k]=B[k]/bw;C[k]=C[k]/cw;D[k]=D[k]/dw;}
    float mx[4],mn[4];
    for(int k=0;k<4;k++) {
        const float da=D[k]-A[k];
        E[k]=(C[k]+B[k])-A[k];
        F[k]=B[k]+da;
        G[k]=C[k]+da;
        H[k]=E[k]+da;
        mx[k]=A[k];mn[k]=A[k];
        mx[k]=fsp_max(mx[k],B[k]);mn[k]=fsp_min(mn[k],B[k]);
        mx[k]=fsp_max(mx[k],C[k]);mn[k]=fsp_min(mn[k],C[k]);
        mx[k]=fsp_max(mx[k],E[k]);mn[k]=fsp_min(mn[k],E[k]);
        mx[k]=fsp_max(mx[k],D[k]);mn[k]=fsp_min(mn[k],D[k]);
        mx[k]=fsp_max(mx[k],F[k]);mn[k]=fsp_min(mn[k],F[k]);
        mx[k]=fsp_max(mx[k],G[k]);mn[k]=fsp_min(mn[k],G[k]);
        mx[k]=fsp_max(mx[k],H[k]);mn[k]=fsp_min(mn[k],H[k]);
    }
    for(int k=0;k<4;k++) {b->h[k]=(mx[k]-mn[k])*0.5f;b->c[k]=(mx[k]+mn[k])*0.5f;}
    b->r[0]=fsp_len3(b->h);
}
/* The box's own transform as a 4x4: its rows and (position.xyz, 1). */
static void fsp_box_matrix(const unsigned char *box,float m[4][4]) {
    fsp_ld(m[0],box+0x40);fsp_ld(m[1],box+0x50);fsp_ld(m[2],box+0x60);fsp_ld(m[3],box+0x30);m[3][3]=1.0f;
}
/* 0x14e1ad0: the box in world space; the identity transform (position 0, rows the unit axes, == as cmpeqps) copies. */
static void fsp_box_world(fsp_world *out,const unsigned char *box) {
    memcpy(out,box,sizeof(*out));
    float pos[4],r0[4],r1[4],r2[4];fsp_ld(pos,box+0x30);fsp_ld(r0,box+0x40);fsp_ld(r1,box+0x50);fsp_ld(r2,box+0x60);
    if(pos[0]==0.0f && pos[1]==0.0f && pos[2]==0.0f && r0[0]==1.0f && r0[1]==0.0f && r0[2]==0.0f &&
       r1[0]==0.0f && r1[1]==1.0f && r1[2]==0.0f && r2[0]==0.0f && r2[1]==0.0f && r2[2]==1.0f) return;
    float m[4][4];fsp_box_matrix(box,m);
    fsp_box_transform(out,m);
}
/* 0x14e1bd0 (with the box's transform): world := bounds of world and the box's corners through m (no divide); a box
 * of radius under 1e-4 is its transformed centre. */
static void fsp_world_merge(fsp_world *w,const unsigned char *box,const float m[4][4]) {
    float lo[4],hi[4],h[4],c[4],br;
    for(int k=0;k<4;k++) {lo[k]=w->c[k]-w->h[k];hi[k]=w->h[k]+w->c[k];}
    fsp_ld(h,box);fsp_ld(c,box+0x10);memcpy(&br,box+0x20,4);
    if(!(fsp_abs(br)>=1e-4f)) {
        float pt[4];
        for(int k=0;k<4;k++) { /* (c.y,c.y,c.y,c.w) r1 + (c.x,..,c.w) r0 + (c.z,..,c.w) r2 + t */
            const float cx=k==3?c[3]:c[0],cy=k==3?c[3]:c[1],cz=k==3?c[3]:c[2];
            pt[k]=((cy*m[1][k]+cx*m[0][k])+cz*m[2][k])+m[3][k];
        }
        for(int k=0;k<4;k++) {lo[k]=fsp_min(lo[k],pt[k]);hi[k]=fsp_max(hi[k],pt[k]);}
    }else {
        const float xh=h[0]+c[0],yh=h[1]+c[1],zh=h[2]+c[2],xl=c[0]-h[0],yl=c[1]-h[1],zl=c[2]-h[2];
        float P[8][4];
        for(int k=0;k<4;k++) {
            const int w=k==3; /* the corner coefficients carry +0 in lane 3 (unpcklps with a zeroed register) */
            const float A0=(w?0.0f:xh)*m[0][k],A1=(w?0.0f:yh)*m[1][k],A2=(w?0.0f:zh)*m[2][k];
            const float B0=(w?0.0f:xl)*m[0][k],B1=(w?0.0f:yl)*m[1][k],B2=(w?0.0f:zl)*m[2][k];
            const float a01=A0+A1,b1a0=B1+A0,b0a1=B0+A1,b01=B0+B1;
            P[0][k]=(A2+a01)+m[3][k];
            P[1][k]=(B2+a01)+m[3][k];
            P[2][k]=(b1a0+A2)+m[3][k];
            P[3][k]=(b1a0+B2)+m[3][k];
            P[4][k]=(b0a1+B2)+m[3][k];
            P[5][k]=(b01+B2)+m[3][k];
            P[6][k]=(b0a1+A2)+m[3][k];
            P[7][k]=(b01+A2)+m[3][k];
        }
        static const int order[8]={0,1,2,3,6,4,7,5}; /* the native min/max order: P1 P2 P3 P4 P7 P5 P8 P6 */
        for(int j=0;j<8;j++) for(int k=0;k<4;k++) {lo[k]=fsp_min(lo[k],P[order[j]][k]);hi[k]=fsp_max(hi[k],P[order[j]][k]);}
    }
    for(int k=0;k<4;k++) {w->h[k]=(hi[k]-lo[k])*0.5f;w->c[k]=(hi[k]+lo[k])*0.5f;}
    w->r[0]=fsp_len3(w->h);
}
/* 0x14e26b0 (flag 0): the segment p..end against the box centre +- half; (tmin, tmax), or (-1e10, 1e10) on a miss. */
static void fsp_slab(const float h[4],const float c[4],const float p[4],const float end[4],float *tmin_out,float *tmax_out) {
    float lo[4],hi[4],d[4];
    for(int k=0;k<4;k++) {lo[k]=c[k]-h[k];hi[k]=h[k]+c[k];d[k]=end[k]-p[k];}
    const float far_lo=fsp_f(0xd01502f9u),far_hi=fsp_f(0x501502f9u);
    float tmin=far_lo,tmax=far_hi;
    for(int k=0;k<3;k++) {
        if(fsp_abs(d[k])>=1e-4f) {
            const float inv=1.0f/d[k],th=(hi[k]-p[k])*inv,tl=(lo[k]-p[k])*inv;
            const float a=fsp_max(fsp_min(th,tl),tmin),b=fsp_min(fsp_max(th,tl),tmax);
            if(a>b || !(b>=0.0f)) goto miss;
            tmin=a;tmax=b;
        }else {
            if(!(p[k]>=lo[k]) || p[k]>hi[k]) goto miss;
        }
    }
    *tmin_out=tmin;*tmax_out=tmax;return;
miss:
    *tmin_out=far_lo;*tmax_out=far_hi;
}
/* The direction of the angle search from (sin, cos) of its first and second angle (the native shuffle and products). */
static void fsp_search_dir(float v[4],float s1,float c1,float s2,float c2) {
    const float zero=0.0f,one=1.0f;
    const float y0=((zero*zero)+zero)+(zero*one),y1=((c1*zero)+zero)+(s1*one),y2=((fsp_neg(s1)*zero)+zero)+(c1*one);
    v[0]=((c2*y0)+(y1*zero))+(s2*y2);
    v[1]=((zero*y0)+(y1*one))+(zero*y2);
    v[2]=((fsp_neg(s2)*y0)+(y1*zero))+(c2*y2);
    v[3]=((zero*y0)+(y1*zero))+(zero*y2);
}
/* 0x14e2040: push p out of the world box (radius r) along dir; a5 >= 1e-4 adds the angle search. */
static void fsp_push_out(const fsp_world *w,float p_io[4],float r,const float dir[4],float a5,int allow_y) {
    float P[4];memcpy(P,p_io,16);
    for(int k=0;k<3;k++) if(!((w->c[k]-w->h[k])<P[k]+r)) return;
    for(int k=0;k<3;k++) if(!(P[k]-r<w->h[k]+w->c[k])) return;
    const float r101=r*fsp_f(0x3f8147aeu),r2=r+r;
    float eh[4],ec[4];
    for(int k=0;k<4;k++) {eh[k]=r101+w->h[k];ec[k]=w->c[k];}
    const float cl=fsp_len3(w->c);
    const float dd=(dir[0]*dir[0]+(dir[1]*dir[1]+dir[2]*dir[2]))+fsp_f(0x129e74d2u);
    const float inv=1.0f/fsp_sqrt(dd);
    const float L=((cl+w->r[0])+w->r[0])+(r2+cl);
    float seg[4],end[4],q[4],tmin,tmax;
    for(int k=0;k<4;k++) {seg[k]=(inv*dir[k])*L;end[k]=P[k]+seg[k];}
    fsp_slab(eh,ec,P,end,&tmin,&tmax);
    for(int k=0;k<4;k++) q[k]=tmax*seg[k]+P[k];
    if(!(fsp_abs(a5)>=1e-4f)) {memcpy(p_io,q,16);return;}
    float best=fsp_len3(q),pitch,yaw;
    if(dir[0]<0.0f || dir[0]>0.0f || dir[2]<0.0f || dir[2]>0.0f) { /* ucomiss; jne: unordered counts as equal */
        const float hl=fsp_sqrt(dir[0]*dir[0]+(0.0f+dir[2]*dir[2]));
        pitch=x4pm_atan2f(dir[1],hl);
        yaw=x4pm_atan2f(dir[0],dir[2]);
    }else {
        yaw=0.0f;
        pitch=dir[1]>0.0f?fsp_f(0x3fc90fdbu):fsp_f(0xbfc90fdbu);
    }
    for(int loop=0;loop<2;loop++) {
        if(loop==1 && !allow_y) break;
        for(float k=-1.0f;;) {
            float first,second,s1,c1,s2,c2,v[4];
            if(loop==0) {first=pitch;second=k*a5+yaw;}
            else {first=k*a5+pitch;second=yaw;}
            x4pm_sincosf(first,&s1,&c1);x4pm_sincosf(second,&s2,&c2);
            fsp_search_dir(v,s1,c1,s2,c2);
            float cs[4],ce[4],cand[4];
            for(int j=0;j<4;j++) {cs[j]=v[j]*L;ce[j]=P[j]+cs[j];}
            fsp_slab(eh,ec,P,ce,&tmin,&tmax);
            for(int j=0;j<4;j++) cand[j]=tmax*cs[j]+P[j];
            const float dist=fsp_len3(cand);
            if(!(dist>=best)) {best=dist;memcpy(q,cand,16);}
            k=k+2.0f;
            if(!(k<=1.0f)) break;
        }
    }
    memcpy(p_io,q,16);
}
/* The large-box push (inline in 0x14e4320): along the axis of least penetration (y only when allowed). */
static void fsp_large_push(const fsp_world *w,const float P[4],float r,int allow_y,float out[4]) {
    memcpy(out,P,16);
    for(int k=0;k<3;k++) if(!((w->c[k]-w->h[k])<P[k]+r)) return;
    for(int k=0;k<3;k++) if(!(P[k]-r<w->h[k]+w->c[k])) return;
    float d[4],e[4],pen[4];
    for(int k=0;k<4;k++) {d[k]=P[k]-w->c[k];e[k]=r+w->h[k];pen[k]=e[k]-fsp_abs(d[k]);}
    int axis;
    if(allow_y && !(pen[1]>=pen[0]) && !(pen[1]>=pen[2])) axis=1;
    else axis=!(pen[0]>=pen[2])?0:2;
    const float s=!(d[axis]>=0.0f)?fsp_neg(e[axis]):e[axis];
    for(int k=0;k<4;k++) out[k]=(k==axis?s:d[k])+w->c[k];
}
/* The solver (Windows 0x14e4320 / Linux 0x1a4b9f0) on the game's memory: begin..begin+n*0x70 the boxes (reordered as
 * native reorders them), p the point (16 bytes, read and written), dir 16 bytes. FSP_NATIVE: a move direction. */
static int fsp_solve(unsigned char *begin,size_t n,float *p,float r,const float *dir_in,float a5,int allow_y,
                     const float *move,float a9) {
    if(move) return FSP_NATIVE;
    float p0[4],dir[4];fsp_ld(p0,p);fsp_ld(dir,dir_in);
    fsp_world w;memset(&w,0,sizeof(w));
    while(n) {
        float q[4];fsp_ld(q,p);
        int absorbed=0;
        for(size_t i=0;i<n;i++) {
            unsigned char *box=begin+i*FSP_BOX;
            float h[4],c[4],pos[4],r0[4],r1[4],r2[4],d[4],local[4];
            fsp_ld(h,box);fsp_ld(c,box+0x10);fsp_ld(pos,box+0x30);fsp_ld(r0,box+0x40);fsp_ld(r1,box+0x50);fsp_ld(r2,box+0x60);
            for(int k=0;k<4;k++) d[k]=q[k]-pos[k];
            const float cx[4]={r0[0],r1[0],r2[0],r2[3]},cy[4]={r0[1],r1[1],r2[1],r2[3]},cz[4]={r0[2],r1[2],r2[2],r2[3]};
            for(int k=0;k<4;k++) {
                const float dx=k==3?d[3]:d[0],dy=k==3?d[3]:d[1],dz=k==3?d[3]:d[2];
                local[k]=(dy*cy[k]+dx*cx[k])+dz*cz[k];
            }
            int inside=1;
            for(int k=0;k<3;k++) if(!((c[k]-h[k])<local[k]+r)) inside=0;
            if(inside) for(int k=0;k<3;k++) if(!(local[k]-r<h[k]+c[k])) inside=0;
            if(!inside) {for(int k=0;k<4;k++) q[k]=q[k]-0.0f;fsp_st(p,q);continue;} /* native: p = q - 0 */
            if(!(fsp_abs(w.r[0])>=1e-4f)) fsp_box_world(&w,box);
            else {float m[4][4];fsp_box_matrix(box,m);fsp_world_merge(&w,box,m);}
            float start[4],res[4];
            for(int k=0;k<4;k++) start[k]=0.0f+p0[k];
            fsp_st(p,start);
            if(a9>0.0f && w.r[0]>a9) fsp_large_push(&w,start,r,allow_y,res);
            else {fsp_push_out(&w,start,r,dir,a5,allow_y);memcpy(res,start,16);}
            for(int k=0;k<4;k++) res[k]=res[k]-0.0f;
            fsp_st(p,res);
            unsigned char *last=begin+(n-1)*FSP_BOX;
            if(last!=box) {unsigned char t[FSP_BOX];memcpy(t,box,FSP_BOX);memcpy(box,last,FSP_BOX);memcpy(last,t,FSP_BOX);}
            n--;absorbed=1;
            break;
        }
        if(!absorbed) break; /* a pass that absorbed nothing; after an absorption the scan starts over */
    }
    return FSP_DONE;
}
/* Known answers (synthetic scenes of our own; the expected points and orders are what Windows' solver 0x14e4320 gives
 * with the exact-reciprocal table armed, replayed offline): (a) three boxes, rotated ones merged into the first, the
 * push-out with the angle search over yaw and pitch (Linux's segment length or slab division would change its bits);
 * (b) three boxes, the large-box push along x. fsp_selftest() is 1 when this build reproduces both (no contraction,
 * no reassociation, the portable sinf/cosf/atan2f). */
static const uint32_t fsp_kat_a_boxes[3*28]={
    0x41af6a0du,0x4181338bu,0x414bd2e5u,0x00000000u,0xc055f8e7u,0xbf4ac679u,0x3f434d59u,0x3f800000u,0x41f084c2u,0x1e089852u,0x0000000au,0x00000000u,
    0x4089a9a0u,0xc05c1bd7u,0x3e93d102u,0x00000000u,0x3f2e28b3u,0xbedefcd1u,0x3f16e8edu,0x00000000u,0x3f2974f5u,0x3ca228dau,0xbf3fd1c6u,0x00000000u,
    0x3ea11be1u,0x3f6663cau,0x3e9a7fdfu,0x3f800000u,
    0x41d2ce5au,0x4193935du,0x418fd044u,0x00000000u,0x4018f972u,0xc110bf0du,0xc04efcfau,0x3f800000u,0x421364e4u,0x29e561d8u,0x000000b7u,0x00000000u,
    0x40b644a0u,0x40d8242fu,0xc115a3cau,0x3f800000u,0xbf52c1b2u,0x3e7e9ff2u,0xbf02a298u,0x00000000u,0x3df6c44eu,0x3f747bf7u,0x3e8abcb1u,0x00000000u,
    0x3f0e01f8u,0x3e25794fu,0xbf50f233u,0x00000000u,
    0x41b2e8bdu,0x41c54bb0u,0x41db133au,0x00000000u,0x40376ce1u,0xc0911b71u,0x40bcf8a6u,0x3f800000u,0x422c6dfeu,0xa88cdf5du,0x000000cfu,0x00000000u,
    0x404a435cu,0xc00f7f9fu,0xc11a48b7u,0x00000000u,0xbef2f0d0u,0x3f5ef0beu,0x3e035d43u,0x00000000u,0xbd59d435u,0xbe320092u,0x3f7bbc14u,0x00000000u,
    0x3f60ef47u,0x3eeb6660u,0x3e03e2b5u,0x3f800000u};
static const uint32_t fsp_kat_b_boxes[3*28]={
    0x4328ba5eu,0x4343f020u,0x42b08a08u,0x00000000u,0x41b51419u,0xc2023d48u,0x42c587efu,0x3f800000u,0x43889ce8u,0x8b139e18u,0x0000007fu,0x00000000u,
    0x4235515fu,0x4277c277u,0xc28c48c8u,0x00000000u,0x3f800000u,0x00000000u,0x00000000u,0x00000000u,0x00000000u,0x3f800000u,0x00000000u,0x00000000u,
    0x00000000u,0x00000000u,0x3f800000u,0x00000000u,
    0x43278222u,0x438d00e3u,0x43270646u,0x00000000u,0x42b5af8eu,0xc2b16b4fu,0xc212f246u,0x3f800000u,0x43b80a75u,0xf0fc41f4u,0x000000e3u,0x00000000u,
    0x00000000u,0x00000000u,0x00000000u,0x3f800000u,0xbd07438du,0xbf7eef76u,0x3dade45bu,0x00000000u,0x3e415454u,0xbdb79583u,0xbf7a58b5u,0x00000000u,
    0x3f7b40feu,0xbc853b3du,0x3e438e84u,0x3f800000u,
    0x42acfb65u,0x43793300u,0x41570aefu,0x00000000u,0x400a609eu,0x42b02d7cu,0xc287c529u,0x3f800000u,0x43840fe5u,0x39acb803u,0x000000bcu,0x00000000u,
    0x3f1594c3u,0xc28ff8f4u,0x42a1680du,0x3f800000u,0x3eb56fbdu,0x3eb83c35u,0xbf5cf428u,0x00000000u,0xbee7b641u,0xbf3de071u,0xbefd7539u,0x00000000u,
    0xbf517befu,0x3f10e76fu,0xbdccc748u,0x00000000u};
static int fsp_kat_one(const uint32_t *boxes,unsigned n,const uint32_t p[4],const uint32_t dir[4],uint32_t r,uint32_t a5,int allow_y,
                       uint32_t a9,const uint32_t out[4],const unsigned char *order) {
    _Alignas(16) unsigned char b[3*FSP_BOX];_Alignas(16) float pt[4],d[4];
    if(n>3) return 0;
    memcpy(b,boxes,n*FSP_BOX);memcpy(pt,p,16);memcpy(d,dir,16);
    if(fsp_solve(b,n,pt,fsp_f(r),d,fsp_f(a5),allow_y,NULL,fsp_f(a9))!=FSP_DONE || memcmp(pt,out,16)) return 0;
    for(unsigned k=0;k<n;k++) if(memcmp(b+k*FSP_BOX,(const unsigned char*)boxes+order[k]*FSP_BOX,0x24)) return 0;
    return 1;
}
static int fsp_selftest(void) {
    static const uint32_t pa[4]={0x404dcb7cu,0x40626557u,0xbf030421u,0x3f800000u},da[4]={0x3f6ee60au,0xbd9c4598u,0xbeb3cc79u,0},
        oa[4]={0x41f34a4au,0x3efe2f08u,0x41e3b01du,0x3f800000u};
    static const uint32_t pb[4]={0xc244bb37u,0x41d115f9u,0x4216f068u,0},db[4]={0x3e091bbeu,0xbf7c211bu,0xbde11fdfu,0},
        ob[4]={0xc481263cu,0x41d115f8u,0x4216f068u,0};
    static const unsigned char order_a[3]={1,2,0},order_b[3]={2,1,0};
    return fsp_kat_one(fsp_kat_a_boxes,3,pa,da,0x412ae48eu,0x3f96a609u,1,0x00000000u,oa,order_a) &&
           fsp_kat_one(fsp_kat_b_boxes,3,pb,db,0x443cac1au,0x3fa18839u,0,0x4231ff37u,ob,order_b);
}
#endif
