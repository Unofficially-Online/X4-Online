/* native_gate_fp_redirect on the Windows X4 9.00 executable: the hooks of lockstep_fp_redirect.h. Included by
 * lockstep_gate.c only. Linux twin: linux/gate_fp_redirect.h.
 *
 * The refresh 0xea1e80 (rcx owner, rdx slot = r15 throughout, r8b flag; 0xb9f bytes). Its callers are not patched:
 * other modules pin their code (the world hash's nav layout 0xea4e90 holds two of them, 0xea50ec and 0xea50fc; the
 * creation trace and registry origin pins 0xea8cf0, the virtual players 0xd8ddb0), and the FR1 run stopped there.
 * Everything patched lies inside the refresh, whose bytes only this module and the parameter option (4096, at boot)
 * pin. The getter path begins at G4 0x5f91b0, called at 0xea22b3: its stub passes r15 (the slot) as a fourth
 * argument. G0 0x5f9340 at 0xea22ca (rcx comp = r14, rdx [owner+0x28], r8 rbp+0x50), M 0xea3dc0 at 0xea28fd (+0x4)
 * and 0xea2932 (+0x0) (xmm0 a, xmm1 b, xmm2 c, r9b flag). Every path to the single ret passes 0xea29bc (mov r13d,
 * 0x300; reached from 0xea1f45, 0xea2993, 0xea29a6, 0xea29b4 and by falling through, nothing enters its bytes
 * 1..5), after the last write to +0x0/+0x4/+0x24 and with every volatile register dead: a jmp there leads to a stub
 * that calls fr_tail_hook(r15) with rsp 16-aligned (the body's frame), does the mov and jumps back to 0xea29c2.
 * The frame: rbp = entry rsp - 0x128, the query vector at rbp-0x10 (w at rbp-0x8), the getters' buffer at
 * rbp+0x50: w = buffer - 0x58. comp[A] = +0x5f4 (+0x24 = 1/that, 0xea23da), comp[B] = +0x5f8.
 * Pins: the refresh's bytes (its patched ones as the file has them) and M's. */
#define FR_COMP_A 0x5f4
#define FR_COMP_B 0x5f8
#define FR_W_FROM_BUFFER (-0x58)
#define FR_M_PARAMS float a,float b,float c,uint64_t flag
#define FR_M_ARGS a,b,c,flag
#define FR_P_PARAMS void *o20,unsigned char *slot,uint64_t flag,double time,uint64_t a5,uint64_t a6 /* rcx, rdx, r8b, xmm3, two on the stack */
#define FR_P_ARGS o20,slot,flag,time,a5,a6
#include "lockstep_fp_redirect.h"
#define FR_REFRESH 0xea1e80u
#define FR_REFRESH_SIZE 0xb9fu
#define FR_REFRESH_SHA "2db0c3a56629a30189d8112ee5e9dec726ddabec4805380cdb7e8a6ec4c2deff"
#define FR_M_RVA 0xea3dc0u
#define FR_M_SIZE 0x44u
#define FR_M_SHA "6b00884c932de1ad5daa186fd5f34fdfbae3475228f5d91230359013a21c7694"
#define FR_SITES 8u
#define FR_TAIL 0xea29bcu
#define FR_TAIL_BACK 0xea29c2u
#define FR_TAIL_STUB 0x80u
#define FR_TAIL_DONE 0x100u
#define FR_PARAMS_STUB 0xc0u
#define FR_MULPS_STUB 0xe0u
#define FR_V_STUB 0x100u
#define FR_WRITER_STUB 0x160u
#define FR_LEN_STUB 0x180u      /* the two length stubs (below), 0x110 bytes each */
#define FR_LEN_STUB_SIZE 0x110u
#define FR_LEN_SITES 2u
#define FR_LEN_DONE 0x200u      /* fr_done bits 9 and 10 */
#define FR_H_STUB 0x3a0u        /* FR13: the four handle stubs, FR_LEN_STUB_SIZE bytes each */
#define FR_H_SITES 4u
#define FR_H_DONE 0x800u        /* fr_done bits 11..14 */
#define FR_PG_STUB (FR_H_STUB+FR_H_SITES*FR_LEN_STUB_SIZE) /* FR16: the PointGuard angle stub (0x7e0) */
#define FR_PG_DONE 0x8000u      /* fr_done bit 15 */
#define FR_AFF_STUB (FR_PG_STUB+FR_LEN_STUB_SIZE) /* FR22: the affine weight stub (0x8f0) */
#define FR_AFF_DONE 0x10000u    /* fr_done bit 16 */
#define FR_BL_DONE 0x20000u     /* fr_done bit 17: the BezierSpline vtable slot */
#define FR_VEL_STUB (FR_AFF_STUB+FR_LEN_STUB_SIZE) /* FR23: the velocity stub (0xa00): native's block, then a frame at +0x20 */
#define FR_VEL_DONE 0x40000u    /* fr_done bit 18 */
#define FR_FAC_STUB (FR_VEL_STUB+0x20u+FR_LEN_STUB_SIZE) /* FR24: the facing factor stub (0xb30): native's block, k, then a frame at +0x20 */
#define FR_FAC_DONE 0x80000u    /* fr_done bit 19 */
#define FR_CODE (FR_FAC_STUB+0x20u+FR_LEN_STUB_SIZE) /* bytes of stub code on the page (0xc60) */
#define FR_P_BLOCK 0x6dcbb0u /* the first call 0x6dc610 from its table load to the +0x0..+0x1c block's end */
#define FR_P_BLOCK_SIZE 0x1eeu
#define FR_P_BLOCK_SHA "120b298b57b3001f3ae6971073a4d86d63fb4822e1be030e0631c42d50c4f9b7"
#define FR_W_SITE_BLOCK 0x6dca83u /* the defaults struct's last movups to the writer's call (rel32 as the file has it) */
#define FR_W_SITE_BLOCK_SIZE 0x31u
#define FR_W_SITE_BLOCK_SHA "845c9e9d967a86e07c60f19aa3293314c3f2d1d74f0cc0ad79c57ee469df3fdd"
#define FR_W_BODY 0x6db200u /* the writer and its multiplier helper 0x6db600, to that one's ret */
#define FR_W_BODY_SIZE 0x4fbu
#define FR_W_BODY_SHA "8aaec3da97c133118e58b96427cd2852fde5c97a6af460e9a647595e97fc09f7"
static const struct {uint32_t site,target;unsigned stub;const char *name;} fr_sites[FR_SITES]={
    {0xea22b3,0x5f91b0,0x00,"g4"},{0xea22ca,0x5f9340,0x20,"g0"},{0xea28fd,FR_M_RVA,0x40,"m4"},{0xea2932,FR_M_RVA,0x60,"m0"},
    {0xea1f2a,0x6dc610,FR_PARAMS_STUB,"params"}, /* the first call, every path: rcx [owner+0x28], rdx slot, r8b flag, xmm3 time */
    {0x6dcbda,0x6db200,FR_MULPS_STUB,"mulps"},   /* inside it: the branch that scales +0x0..+0xc and d */
    {0x6dcce6,0x3dc040,FR_V_STUB,"v"},           /* inside it: v into rsp+0x40 (rdx), right before the max block */
    {0x6dcaaf,0x6db200,FR_WRITER_STUB,"writer"}, /* inside it: the writer on the watched ships' path (rcx o20 = rdi, rdx r13, r8 rsp+0x70, r9 slot) */
};
static const unsigned char fr_tail_original[5]={0x41,0xbd,0x00,0x03,0x00}; /* mov r13d,0x300 (its last byte stays) */
/* FR10/FR11 (lockstep_fp_redirect.h fr_len_seen): the two composite-spline builders that inline BezierSpline::Set,
 * 0x857960 (CompositeSpline<5>: the path spline build 0xeb1e60 calls it at 0xeb2774; also 0x81190d, 0xebca7c) and
 * 0x31c640 (the same template, 8 callers), end every segment with the same tail:
 *   mulss V,xmm13 (0.5); ucomiss V,xmm14 (0.0); movss [r10+rax+0x18],V; jne +9; mov dword [r10+rax+0x18],1.0
 * V = xmm4 at 0x857f71, xmm9 at 0x31cc01. The segment record is r10+rax; its control points are stored at
 * +0x60..+0x90 before (0x857e5c..0x857e92, 0x31caf2..0x31cb22; rax loaded at 0x857e45 / 0x31cad5, neither register
 * written since). A jmp over the mulss (5 bytes, the whole instruction) leads to a stub: the mulss; rax, rcx, rdx,
 * r8-r11, xmm0-5 and V saved (rsp 16-aligned below the old one, rbp kept); fr_len_seen(which, r10+rax, V[0]) stores
 * the length; V[0] = its result, lanes 1-3 as they were; everything back; jmp to the tail's end (0x857f8c /
 * 0x31cc1c), where the flags are dead (inc rbx, then add rdx writes them all). The tails' other bytes stay. No
 * branch of either function and no rel32 call/jmp of .text lands inside a tail. No exact-reciprocal window overlaps a
 * tail (the last ends at 0x857f67 / 0x31cbf8); arming refuses if any patch does (fr_len_tail_clear). Pins: each block from its rax load
 * to the tail's end through the view (its exact-reciprocal windows as the file has them). */
static const struct {uint32_t site,back,pin,pin_size;unsigned reg;const char *sha;} fr_len_sites[FR_LEN_SITES]={
    {0x857f71u,0x857f8cu,0x857e45u,0x159u,4,"24bd229b7ebe7a0474ffd3a6e2d35d1d190736d5c3addcd39c9075b3bd8f8bf5"},
    {0x31cc01u,0x31cc1cu,0x31cad5u,0x160u,9,"a117e928fccb5687bde699598f8312b7150b065e35bf0dcbf12a590c3870c565"}};
static const unsigned char fr_len_original[FR_LEN_SITES][5]={{0xf3,0x41,0x0f,0x59,0xe5},{0xf3,0x45,0x0f,0x59,0xcd}}; /* mulss V,xmm13 */
/* FR13 (lockstep_fp_redirect.h fr_handle_seen): the same builders' Bezier handle length where the caller left it 0,
 * s = d * xmm8 with xmm8 = 0.333333343f (Linux divides by 3.0): mulss xmm2,xmm8 (f3 41 0f 59 d0, the whole
 * instruction) right after an exact-reciprocal window that makes d = sqrt(...) (the windows {0x857d11,8}, {0x857de8,8},
 * {0x31c9a1,8}, {0x31ca78,8} end at the sites; their trampolines return there). 0x857d19 / 0x31c9a9 the out-handle
 * (then jmp to its store [r9+rbx*8+4]), 0x857df0 / 0x31ca80 the in-handle (store [rcx+rbx*8+8] next). A jmp over the
 * mulss leads to a stub (the length stub's frame, no leading instruction): fr_handle_seen(which, xmm2[0], xmm8[0]);
 * xmm2[0] = its result, lanes 1-3 as they were; back after the site. Flags are dead there (comiss 0x857dbe / 0x31ca4e,
 * ucomiss 0x857f76 / 0x31cc06 come first); no branch of either builder and no .text rel32 lands in bytes 1..4; xmm8 is
 * written only by the prologue's load (0x857c7b / 0x31c90b) and the epilogue. Pins: each builder from that load to its
 * in-handle's end, through the view (the windows and these sites as the file has them). */
static const struct {uint32_t site,back;unsigned pin;} fr_h_sites[FR_H_SITES]={
    {0x857d19u,0x857d1eu,0},{0x857df0u,0x857df5u,0},{0x31c9a9u,0x31c9aeu,1},{0x31ca80u,0x31ca85u,1}};
static const struct {uint32_t pin,pin_size;const char *sha;} fr_h_pins[2]={
    {0x857c7bu,0x17au,"91d418701453a49e8e6dcd5a1a86fd8215a44e45903d8fe991816b7612090b25"},
    {0x31c90bu,0x17au,"b0540d91a5daf95243441ba3ed4109e47680e67f870dc5cdd76ce460311d67cf"}};
static const unsigned char fr_h_original[5]={0xf3,0x41,0x0f,0x59,0xd0}; /* mulss xmm2,xmm8 */
/* FR16 (lockstep_fp_redirect.h fr_pg_seen): PointGuardFormationShape vtable slot 6 0xe9c380 forms the slot's angle as
 *   0xe9c4bc movss xmm0,[0x2cbeab4] (pi_f, f3 0f 10 05 f0 25 e2 01); 0xe9c4c4 divss xmm0,xmm2 (xmm2 = (float)(m+1));
 *   0xe9c4c8 mulss xmm1,xmm0 (xmm1 = (float)rem); 0xe9c4cc movaps xmm0,xmm1; call 0x1750210 (sincos).
 * A jmp on the movss leads to a stub (the length stub's frame, no leading instruction, V = xmm1):
 * fr_pg_seen(0, xmm1[0], xmm2[0]); xmm1[0] = its result, lanes 1-3 as they were; back to 0xe9c4cc, whose movaps
 * hands it on (xmm0, dead until there, is not restored by native either). Both conversion paths (signed and the
 * >= 2^63 halving one) join at the movss; the only branch to it is 0xe9c4a5 (to its first byte), no branch of the
 * function and no .text rel32 lands in 0xe9c4bd..0xe9c4cb; flags are dead (the call at 0xe9c4cf comes first); no
 * volatile register is read after the call before being written. Pins: the whole function through the view, and
 * pi_f at 0x2cbeab4. No other module patches or pins this function. */
#define FR_PG_SITE 0xe9c4bcu
#define FR_PG_BACK 0xe9c4ccu
#define FR_PG_PIN 0xe9c380u
#define FR_PG_PIN_SIZE 0x27au
#define FR_PG_PIN_SHA "de55bc8f392aaeeae12e556805cb1d3087c641c6aa09f9e1daebeccd05aac051"
#define FR_PG_PI 0x2cbeab4u
#define FR_PG_PI_BITS 0x40490fdbu
static const unsigned char fr_pg_original[5]={0xf3,0x0f,0x10,0x05,0xf0}; /* movss xmm0,[rip+0x1e225f0], first 5 bytes */
/* FR22 (lockstep_fp_redirect.h fr_bl_seen, fr_aff_seen): the highways' spline data, built in the load by
 * Highway::SetSpline 0x5f8e80 (after this module is armed): its spline property's init 0x972400 calls the copied
 * composite's ComputeLength(100) (0x97267a; CompositeSpline 0x30f0d0: each segment's ComputeLength through its vtable,
 * the sum, the knots 0x31c430), then the AffineSpline over it (0x5f2e10 -> constructor 0x65a580) and the speeds
 * (+0x5f4/+0x5f8 = affine total / (affine[+0x18] * a global factor), 0x5f53b5).
 * (1) BezierSpline::ComputeLength 0x14e4f20 (rcx the segment, edx n; len at +8; u = i*(1/n), 1.0 at 0x2cbe5d4) is
 * reached only through the BezierSpline vtable 0x2a84ef8's +0x28 (0x2a84f20; also the abstract base's 0x2a84fc8, never
 * an object's): the composites' 0x30f0d0 and the path builder's ComputeLength(10) at 0x896642. That slot is swapped
 * (like FR19/FR21's): fr_bl_hook runs native, then fr_bl_seen on a BezierSpline (vptr 0x2a84ef8, whose +0x10 0x14e4a20
 * is the chord D(a,b) the strict code repeats). Pins: 0x14e4f20 and 0x14e4a20 to their rets, the +0x10 slot, 1.0.
 * (2) The AffineSpline constructor 0x65a580's knot weight: 0x65a66e divss xmm2,xmm1 (xmm2 = 2.0 from xmm6, 0x2cbe918;
 * xmm1 = sqrt(s_i-1)+sqrt(s_i)); 0x65a672 mulss xmm2,xmm0 (xmm0 = p_i-p_i-1). A jmp on the divss leads to a stub (the
 * length stub's frame, V = xmm2): fr_aff_seen(0, xmm1[0], xmm0[0]); xmm2[0] = its result, lanes 1-3 as they were (zero,
 * from the movss load); back to 0x65a676 (movaps xmm0,xmm2; addss ...), xmm0/xmm1 as native left them. Flags are dead
 * (the cmp at 0x65a697 comes first); no branch of the function and no .text rel32 lands in 0x65a66f..0x65a675; no
 * exact-reciprocal window, no other module's patch or pin there. Pins: the constructor 0x65a580..0x65a740 through the
 * view, and 2.0 at 0x2cbe918. */
#define FR_BL_SLOT 0x2a84f20u
#define FR_BL_VTABLE 0x2a84ef8u
#define FR_BL_FN 0x14e4f20u
#define FR_BL_FN_SIZE 0xb6u
#define FR_BL_FN_SHA "8eb4375492e3517fce22cacdaa6930abbf28386484209a0abe81ed56da4285c6"
#define FR_BL_CHORD 0x14e4a20u
#define FR_BL_CHORD_SIZE 0x60u
#define FR_BL_CHORD_SHA "f9e05c4148fd736cd2ca9bb900444e7ce6cf24e80ec681977bd2c9bbd545d143"
#define FR_BL_CHORD_SLOT 0x2a84f08u
#define FR_BL_ONE 0x2cbe5d4u
#define FR_AFF_SITE 0x65a66eu
#define FR_AFF_BACK 0x65a676u
#define FR_AFF_PIN 0x65a580u
#define FR_AFF_PIN_SIZE 0x1c0u
#define FR_AFF_PIN_SHA "31a326aaa1f30ba766f7541aa404efc8ff455dfe92422dc45e3cc0fad5b70a32"
#define FR_AFF_TWO 0x2cbe918u
static const unsigned char fr_aff_original[5]={0xf3,0x0f,0x5e,0xd1,0xf3}; /* divss xmm2,xmm1; the mulss's first byte */
/* FR23 (lockstep_fp_redirect.h fr_vel_seen): the Linear step 0xeb27c0 (rdi the model, rsi the written pathing bank)
 * stores the bank's velocity on the branch with [model+0x95] set as (s,s,s,0)*row2, s = xmm11 (the speed, maxss'ed
 * with xmm12 = 0.0, xorps at 0xeb2873 and never written again in the step):
 *   0xeb32fb je 0xeb331a ([rdi+0x95] clear: the other branch, a plain (0,0,s,0) store, untouched)
 *   0xeb32fd movaps xmm2,xmm11; movaps xmm3,xmm11; unpcklps xmm2,xmm12; unpcklps xmm3,xmm11; movlhps xmm3,xmm2;
 *   0xeb3310 mulps xmm3,[rsi+0x30]; 0xeb3314 movups [rsi+0x40],xmm3; 0xeb3318 jmp 0xeb331e (movups [rsi+0x50],xmm4)
 * A jmp on the first movaps leads to a stub: that block as it stands (27 bytes, position independent), then the length
 * stub's frame (V = xmm3): fr_vel_seen(0, rsi, xmm11[0]), which stores ours in 'redirect' and returns native's lane 0,
 * so xmm2/xmm3 and every other register leave as native left them; back to 0xeb331e. Flags are dead (comiss at
 * 0xeb3322 comes first); no branch of the step and no .text rel32 lands in 0xeb32fe..0xeb3301 (the step's only branch
 * into the block's range is 0xeb32fb -> 0xeb331a, the other branch, which stays native). Pins: the whole step through
 * the view (the world trace's pin, 0xeb27c0+5212) and the block's bytes from the site to the back address. */
#define FR_VEL_SITE 0xeb32fdu
#define FR_VEL_BACK 0xeb331eu
#define FR_VEL_BLOCK 27u
#define FR_VEL_PIN 0xeb27c0u
#define FR_VEL_PIN_SIZE 5212u
#define FR_VEL_PIN_SHA "7920e6e4ff99bf0d0c348eeab202b0a21f5e9d69c4005ba75d302eac7ea0c8a4"
static const unsigned char fr_vel_block[FR_VEL_BACK-FR_VEL_SITE]={0x41,0x0f,0x28,0xd3,0x41,0x0f,0x28,0xdb,0x41,0x0f,0x14,0xd4,0x41,0x0f,0x14,0xdb,
    0x0f,0x16,0xda,0x0f,0x59,0x5e,0x30,0x0f,0x11,0x5e,0x40,0xeb,0x04,0x0f,0x11,0x56,0x40}; /* through the other branch's store */
#define fr_vel_original fr_vel_block /* the first 5 bytes: movaps xmm2,xmm11; the next movaps's first byte */
/* FR24 (lockstep_fp_redirect.h fr_fac_seen): the same step's facing factor. m (the largest heading angle) is xmm1,
 * stored at [rdi+0x98] by 0xeb3a6c; 0xeb3a7c comiss m,pi/2; 0xeb3a83 jbe 0xeb3a91 (above: +0x9c = 0, untouched); then
 *   0xeb3a91 mulss xmm1,[0x2cbe484] (k = 0.636619747f); 0xeb3a99 movaps xmm0,xmm13 (1.0, loaded at 0xeb286a, callee-saved
 *   and not written again before the epilogue); 0xeb3a9d subss xmm0,xmm1; 0xeb3aa1 movss [rdi+0x9c],xmm0
 * A jmp on the mulss leads to a stub: that block with its operand on the stub page (a copy of k, which the pins hold
 * equal to the image's), then the length stub's frame (V = xmm0): fr_fac_seen(0, [rdi+0x98] (m), xmm0[0] (native));
 * xmm0[0] = its result, lanes 1-3 and xmm1 (m*k) as native left them; back to 0xeb3aa1, whose store and the comiss
 * at 0xeb3aa9 (the blend toward 0.5) then take it. Flags are dead there (that comiss writes them). The step's only
 * branch into 0xeb3a91..0xeb3aa0 is 0xeb3a83 (to its first byte); no .text rel32 lands inside. Pins: the whole step
 * through the view (as FR23's), the block's bytes from the site to the back address and k. */
#define FR_FAC_SITE 0xeb3a91u
#define FR_FAC_BACK 0xeb3aa1u
#define FR_FAC_K 0x2cbe484u
#define FR_FAC_K_BITS 0x3f22f983u
static const unsigned char fr_fac_block[FR_FAC_BACK-FR_FAC_SITE]={0xf3,0x0f,0x59,0x0d,0xeb,0xa9,0xe0,0x01,0x41,0x0f,0x28,0xc5,0xf3,0x0f,0x5c,0xc1};
#define fr_fac_original fr_fac_block /* the first 5 bytes: the mulss's opcode, modrm and the rel32's first byte */
static unsigned char *fr_page;
static uint32_t fr_done;
/* G4 with the slot: the refresh's path with the getters begins here. */
static float fr_g4_slot_hook(void *comp,void *o20,void *buf,void *slot) {
    fr_begin(slot);
    return fr_g4_hook(comp,o20,buf);
}
/* Every exit of the refresh, with its slot. */
static void fr_tail_hook(void *slot) {
    InterlockedIncrement64(&fr_calls);
    fr_ctx *c=fr_context(0);
    if(!c || !c->depth || c->slot!=slot) {if(fr_comparing()) InterlockedIncrement64(&fr_skipped);return;} /* the flag or early path */
    fr_end(c);
}
/* The first call's v and branch (lockstep_fp_redirect.h fr_p_new): 0x3dc040 at 0x6dcce6 (rcx ship, rdx v = rsp+0x40,
 * xmm2 time, r9, three on the stack; d is xmm7, callee-saved, which the stub passes as an eighth argument) and
 * 0x6db200 at 0x6dcbda (four arguments). K = [[0x146cf1538]+0x300] (r15, loaded at 0x6dcbb0). */
typedef uint64_t (*fr_v_fn)(void *,unsigned char *,double,uint64_t,uint64_t,uint64_t,uint64_t);
static fr_v_fn fr_native_v;
static uint64_t fr_v_hook(void *o20,unsigned char *buf,double time,uint64_t a4,uint64_t a5,uint64_t a6,uint64_t a7,float d) {
    const uint64_t r=fr_native_v(o20,buf,time,a4,a5,a6,a7);fr_v_seen(buf,d);return r;
}
/* Both calls of the writer 0x6db200 (rcx o20, rdx D, r8 A, r9 slot) run through fr_writer_run: the 'mulps' branch's
 * at 0x6dcbda (marked first) and the watched ships' at 0x6dcaaf (its stub jumps straight there). */
static uint64_t fr_mulps_hook(void *a,void *b,void *c,void *e) {fr_mulps_seen();return fr_writer_run(a,b,c,e);}
static uint32_t fr_k_read(void) {
    const unsigned char *table=NULL;uint32_t k=0;
    if(!read_local(image+0x6cf1538u,&table,8) || !table || !read_local(table+0x300,&k,4)) return 0;
    return k;
}
/* The writer's layout on Windows: table [0x146cf1538] (+0x5050 scale, +0x300 K, +0x318 clamp), D +0x904..+0x914,
 * multipliers +0x988..+0x994 (0x6db600), virtuals +0x1fe8..+0x2008. */
static int fr_w_read(void *o20,const unsigned char *data,const unsigned char *a,fr_w_in *in) {
    return fr_w_gather(o20,data,a,image+0x6cf1538u,0x5050,0x300,0x318,0x904,0x988,0x1fe8,in);
}
static void fr_write_jump(unsigned char *p,const void *target) {
    static const unsigned char code[6]={0xff,0x25,0,0,0,0};memcpy(p,code,6);memcpy(p+6,&target,8);
}
/* [REX.R] 0F op modrm(reg, [rsp+disp32]): movups loads (0x10) and stores (0x11) of the length stub's frame. */
static unsigned fr_len_sse(unsigned char *p,unsigned op,unsigned reg,uint32_t disp) {
    unsigned n=0;if(reg>=8) p[n++]=0x44;
    p[n++]=0x0f;p[n++]=(unsigned char)op;p[n++]=(unsigned char)(0x84|(reg&7)<<3);p[n++]=0x24;memcpy(p+n,&disp,4);return n+4;
}
/* The length stub of site i (FR_LEN_STUB_SIZE bytes; the hook and back pointers at +0xf8 / +0x100):
 *   mulss V,xmm13; push rbp; mov rbp,rsp; and rsp,-16; sub rsp,0xd0; rax rcx rdx r8-r11 -> [rsp+0x20..0x58];
 *   xmm0-5 -> [rsp+0x60..0xb0]; V -> [rsp+0xc0]; mov ecx,i; lea rdx,[r10+rax]; mov r8d,[rsp+0xc0];
 *   call [rip] -> fr_len_seen; mov [rsp+0xc0],eax; xmm0-5 back; V <- [rsp+0xc0]; the GPRs back; mov rsp,rbp; pop rbp;
 *   jmp [rip] -> the tail's end. */
static void fr_stub_write(unsigned char *p,const unsigned char *first,unsigned first_n,unsigned v,uint32_t which,
                          const unsigned char *args,unsigned args_n,void *hook,void *back) {
    unsigned n=0;
    memset(p,0xcc,FR_LEN_STUB_SIZE);
    if(first_n) {memcpy(p,first,first_n);n=first_n;}
    static const unsigned char enter[]={0x55,0x48,0x89,0xe5,0x48,0x83,0xe4,0xf0,0x48,0x81,0xec,0xd0,0x00,0x00,0x00,
        0x48,0x89,0x44,0x24,0x20,0x48,0x89,0x4c,0x24,0x28,0x48,0x89,0x54,0x24,0x30,0x4c,0x89,0x44,0x24,0x38,
        0x4c,0x89,0x4c,0x24,0x40,0x4c,0x89,0x54,0x24,0x48,0x4c,0x89,0x5c,0x24,0x50};
    memcpy(p+n,enter,sizeof(enter));n+=sizeof(enter);
    for(unsigned r=0;r<6;r++) n+=fr_len_sse(p+n,0x11,r,0x60+0x10*r);
    n+=fr_len_sse(p+n,0x11,v,0xc0);
    p[n++]=0xb9;memcpy(p+n,&which,4);n+=4;                                             /* mov ecx,which */
    memcpy(p+n,args,args_n);n+=args_n;                                                   /* ...; call [rip] (ff 15) */
    int32_t d=(int32_t)(0xf8-(n+4));memcpy(p+n,&d,4);n+=4;
    static const unsigned char keep[]={0x89,0x84,0x24,0xc0,0x00,0x00,0x00};                /* mov [rsp+0xc0],eax */
    memcpy(p+n,keep,sizeof(keep));n+=sizeof(keep);
    for(unsigned r=0;r<6;r++) n+=fr_len_sse(p+n,0x10,r,0x60+0x10*r);
    n+=fr_len_sse(p+n,0x10,v,0xc0);
    static const unsigned char leave[]={0x48,0x8b,0x44,0x24,0x20,0x48,0x8b,0x4c,0x24,0x28,0x48,0x8b,0x54,0x24,0x30,
        0x4c,0x8b,0x44,0x24,0x38,0x4c,0x8b,0x4c,0x24,0x40,0x4c,0x8b,0x54,0x24,0x48,0x4c,0x8b,0x5c,0x24,0x50,
        0x48,0x89,0xec,0x5d,0xff,0x25};
    memcpy(p+n,leave,sizeof(leave));n+=sizeof(leave);
    d=(int32_t)(0x100-(n+4));memcpy(p+n,&d,4);
    memcpy(p+0xf8,&hook,8);memcpy(p+0x100,&back,8);
}
static void fr_len_write_stub(unsigned char *p,unsigned i) {
    static const unsigned char args[]={0x49,0x8d,0x14,0x02,0x44,0x8b,0x84,0x24,0xc0,0x00,0x00,0x00,0xff,0x15}; /* lea rdx,[r10+rax]; mov r8d,[rsp+0xc0] */
    fr_stub_write(p,fr_len_original[i],5,fr_len_sites[i].reg,i,args,sizeof(args),(void*)fr_len_seen,(void*)(image+fr_len_sites[i].back));
}
/* FR13: the handle stub of site i: the same frame without a leading instruction, V = xmm2;
 *   mov ecx,i; movd edx,xmm2 (d); movd r8d,xmm8 (k); call [rip] -> fr_handle_seen; xmm2[0] = its result; back after the site. */
static void fr_h_write_stub(unsigned char *p,unsigned i) {
    static const unsigned char args[]={0x66,0x0f,0x7e,0xd2,0x66,0x45,0x0f,0x7e,0xc0,0xff,0x15};
    fr_stub_write(p,NULL,0,2,i,args,sizeof(args),(void*)fr_handle_seen,(void*)(image+fr_h_sites[i].back));
}
/* FR16: the PointGuard angle stub: the same frame, V = xmm1;
 *   mov ecx,0; movd edx,xmm1 (rem); movd r8d,xmm2 (m+1); call [rip] -> fr_pg_seen; xmm1[0] = its result; back to 0xe9c4cc. */
static void fr_pg_write_stub(unsigned char *p) {
    static const unsigned char args[]={0x66,0x0f,0x7e,0xca,0x66,0x41,0x0f,0x7e,0xd0,0xff,0x15};
    fr_stub_write(p,NULL,0,1,0,args,sizeof(args),(void*)fr_pg_seen,(void*)(image+FR_PG_BACK));
}
/* FR22: the affine weight stub: the same frame, V = xmm2;
 *   mov ecx,0; movd edx,xmm1 (sum); movd r8d,xmm0 (dp); call [rip] -> fr_aff_seen; xmm2[0] = its result; back to 0x65a676. */
static void fr_aff_write_stub(unsigned char *p) {
    static const unsigned char args[]={0x66,0x0f,0x7e,0xca,0x66,0x41,0x0f,0x7e,0xc0,0xff,0x15};
    fr_stub_write(p,NULL,0,2,0,args,sizeof(args),(void*)fr_aff_seen,(void*)(image+FR_AFF_BACK));
}
/* FR23: the velocity stub: native's block (0xeb32fd..0xeb3317, without its jmp); jmp +3 to the frame at +0x20 (V = xmm3):
 *   mov ecx,0; mov rdx,rsi (the bank); movd r8d,xmm11 (s); call [rip] -> fr_vel_seen; xmm3[0] = its result (native's
 *   own lane 0); back to 0xeb331e. */
static void fr_vel_write_stub(unsigned char *p) {
    memset(p,0xcc,0x20);memcpy(p,fr_vel_block,FR_VEL_BLOCK);p[FR_VEL_BLOCK]=0xeb;p[FR_VEL_BLOCK+1]=(unsigned char)(0x20-(FR_VEL_BLOCK+2));
    static const unsigned char args[]={0x48,0x89,0xf2,0x66,0x45,0x0f,0x7e,0xd8,0xff,0x15};
    fr_stub_write(p+0x20,NULL,0,3,0,args,sizeof(args),(void*)fr_vel_seen,(void*)(image+FR_VEL_BACK));
}
/* FR24: the facing factor stub: mulss xmm1,[rip -> +0x18] (k's copy); movaps xmm0,xmm13; subss xmm0,xmm1; jmp +6 to the
 * frame at +0x20 (V = xmm0): mov ecx,0; mov edx,[rdi+0x98] (m); movd r8d,xmm0 (native); call [rip] -> fr_fac_seen;
 * xmm0[0] = its result; back to 0xeb3aa1. */
static void fr_fac_write_stub(unsigned char *p) {
    memset(p,0xcc,0x20);memcpy(p,fr_fac_block,sizeof(fr_fac_block));
    const int32_t k_at=0x18-8;memcpy(p+4,&k_at,4); /* the mulss ends at +8 */
    p[16]=0xeb;p[17]=(unsigned char)(0x20-18);
    const uint32_t k=FR_FAC_K_BITS;memcpy(p+0x18,&k,4);
    static const unsigned char args[]={0x8b,0x97,0x98,0x00,0x00,0x00,0x66,0x41,0x0f,0x7e,0xc0,0xff,0x15};
    fr_stub_write(p+0x20,NULL,0,0,0,args,sizeof(args),(void*)fr_fac_seen,(void*)(image+FR_FAC_BACK));
}
/* 0x00 mov r9,r15; jmp [rip] -> fr_g4_slot_hook | 0x20/0x40/0x60 jmp [rip] -> G0, M4, M0 hooks | 0x80 the tail:
 * mov rcx,r15; sub rsp,0x20; call [rip] -> fr_tail_hook; add rsp,0x20; mov r13d,0x300; jmp [rip] -> 0xea29c2 |
 * 0xc0 jmp [rip] -> fr_params_hook | 0xe0 -> fr_mulps_hook | 0x100 the v stub | 0x160 jmp [rip] -> fr_writer_run. */
static void fr_write_code(unsigned char *p) {
    memset(p,0xcc,FR_CODE);
    p[0]=0x4d;p[1]=0x89;p[2]=0xf9;fr_write_jump(p+3,(void*)fr_g4_slot_hook);
    fr_write_jump(p+0x20,(void*)fr_g0_hook);fr_write_jump(p+0x40,(void*)fr_m4_hook);fr_write_jump(p+0x60,(void*)fr_m0_hook);
    static const unsigned char tail[]={0x4c,0x89,0xf9,0x48,0x83,0xec,0x20,0xff,0x15,0,0,0,0,0x48,0x83,0xc4,0x20,0x41,0xbd,0x00,0x03,0x00,0x00,0xff,0x25,0,0,0,0};
    unsigned char *t=p+FR_TAIL_STUB;memcpy(t,tail,sizeof(tail));
    int32_t d=(int32_t)(0x30-13);memcpy(t+9,&d,4);d=(int32_t)(0x38-(int32_t)sizeof(tail));memcpy(t+25,&d,4);
    void *hook=(void*)fr_tail_hook,*back=(void*)(image+FR_TAIL_BACK);memcpy(t+0x30,&hook,8);memcpy(t+0x38,&back,8);
    fr_write_jump(p+FR_PARAMS_STUB,(void*)fr_params_hook);fr_write_jump(p+FR_MULPS_STUB,(void*)fr_mulps_hook);
    /* 0x100: sub rsp,0x58; movss [rsp+0x38],xmm7; the three stack arguments down by 0x58; call [rip] -> fr_v_hook;
     * add rsp,0x58; ret (rsp 16-aligned at the call: the call site's alignment minus 8 minus 0x58). */
    static const unsigned char vstub[]={0x48,0x83,0xec,0x58,0xf3,0x0f,0x11,0x7c,0x24,0x38,
        0x48,0x8b,0x84,0x24,0x80,0,0,0,0x48,0x89,0x44,0x24,0x20,0x48,0x8b,0x84,0x24,0x88,0,0,0,0x48,0x89,0x44,0x24,0x28,
        0x48,0x8b,0x84,0x24,0x90,0,0,0,0x48,0x89,0x44,0x24,0x30,0xff,0x15,0,0,0,0,0x48,0x83,0xc4,0x58,0xc3};
    unsigned char *s=p+FR_V_STUB;memcpy(s,vstub,sizeof(vstub));
    d=(int32_t)(0x48-0x37);memcpy(s+0x33,&d,4);void *vh=(void*)fr_v_hook;memcpy(s+0x48,&vh,8);
    fr_write_jump(p+FR_WRITER_STUB,(void*)fr_writer_run); /* 0x160 */
    for(unsigned i=0;i<FR_LEN_SITES;i++) fr_len_write_stub(p+FR_LEN_STUB+FR_LEN_STUB_SIZE*i,i);
    for(unsigned i=0;i<FR_H_SITES;i++) fr_h_write_stub(p+FR_H_STUB+FR_LEN_STUB_SIZE*i,i);
    fr_pg_write_stub(p+FR_PG_STUB);
    fr_aff_write_stub(p+FR_AFF_STUB);
    fr_vel_write_stub(p+FR_VEL_STUB);
    fr_fac_write_stub(p+FR_FAC_STUB);
}
static int32_t fr_rel(uint32_t from,const void *target) {return (int32_t)((intptr_t)target-(intptr_t)(image+from+5));}
static int32_t fr_rel_ours(unsigned i) {return fr_rel(fr_sites[i].site,fr_page+fr_sites[i].stub);}
static int32_t fr_rel_native(unsigned i) {return fr_rel(fr_sites[i].site,image+fr_sites[i].target);}
/* 0 original, 1 ours, -1 anything else. */
static int fr_site_state(unsigned i) {
    unsigned char now[5];if(!read_local(image+fr_sites[i].site,now,5) || now[0]!=0xe8) return -1;
    int32_t d;memcpy(&d,now+1,4);
    if(d==fr_rel_native(i)) return 0;
    if(fr_page && d==fr_rel_ours(i)) return 1;
    return -1;
}
static void fr_tail_jump(unsigned char out[5]) {out[0]=0xe9;const int32_t d=fr_rel(FR_TAIL,fr_page+FR_TAIL_STUB);memcpy(out+1,&d,4);}
static int fr_tail_state(void) {
    unsigned char now[5],ours[5];if(!read_local(image+FR_TAIL,now,5)) return -1;
    if(!memcmp(now,fr_tail_original,5)) return 0;
    if(fr_page) {fr_tail_jump(ours);if(!memcmp(now,ours,5)) return 1;}
    return -1;
}
/* The length sites: 0 original, 1 our jmp, -1 anything else (the raw bytes: the view shows the original ones). */
static unsigned char *fr_len_stub(unsigned i) {return fr_page+FR_LEN_STUB+FR_LEN_STUB_SIZE*i;}
static int fr_len_reach(unsigned i) {
    const intptr_t d=(intptr_t)fr_len_stub(i)-(intptr_t)(image+fr_len_sites[i].site+5);
    return d>INT32_MIN && d<INT32_MAX;
}
static void fr_len_jump(unsigned i,unsigned char out[5]) {out[0]=0xe9;const int32_t d=fr_rel(fr_len_sites[i].site,fr_len_stub(i));memcpy(out+1,&d,4);}
static int fr_len_state(unsigned i) {
    unsigned char now[5],ours[5];memcpy(now,(const void*)(image+fr_len_sites[i].site),5);
    if(!memcmp(now,fr_len_original[i],5)) return 0;
    if(fr_page && fr_len_reach(i)) {fr_len_jump(i,ours);if(!memcmp(now,ours,5)) return 1;}
    return -1;
}
/* The original-byte view (lockstep_gate.c fp_patch_view) over an installed length site. */
static int fr_len_overlaps(const void *source,size_t bytes) {
    const uintptr_t a=(uintptr_t)source,b=a+bytes;
    for(unsigned i=0;i<FR_LEN_SITES;i++) {
        const uintptr_t s=(uintptr_t)(image+fr_len_sites[i].site);
        if(fr_done&(FR_LEN_DONE<<i) && s<b && s+5>a) return 1;
    }
    for(unsigned i=0;i<FR_H_SITES;i++) {
        const uintptr_t s=(uintptr_t)(image+fr_h_sites[i].site);
        if(fr_done&(FR_H_DONE<<i) && s<b && s+5>a) return 1;
    }
    const uintptr_t s=(uintptr_t)(image+FR_PG_SITE);
    if(fr_done&FR_PG_DONE && s<b && s+5>a) return 1;
    const uintptr_t f=(uintptr_t)(image+FR_AFF_SITE);
    if(fr_done&FR_AFF_DONE && f<b && f+5>a) return 1;
    const uintptr_t v=(uintptr_t)(image+FR_VEL_SITE);
    if(fr_done&FR_VEL_DONE && v<b && v+5>a) return 1;
    const uintptr_t c=(uintptr_t)(image+FR_FAC_SITE);
    if(fr_done&FR_FAC_DONE && c<b && c+5>a) return 1;
    return 0;
}
static void fr_len_unpatch_copy(const void *source,void *copy,size_t bytes) {
    const uintptr_t a=(uintptr_t)source;
    for(unsigned i=0;i<FR_LEN_SITES;i++) {
        if(!(fr_done&(FR_LEN_DONE<<i))) continue;
        const uintptr_t s=(uintptr_t)(image+fr_len_sites[i].site);
        for(unsigned k=0;k<5;k++) if(s+k>=a && s+k<a+bytes) ((unsigned char*)copy)[s+k-a]=fr_len_original[i][k];
    }
    for(unsigned i=0;i<FR_H_SITES;i++) {
        if(!(fr_done&(FR_H_DONE<<i))) continue;
        const uintptr_t s=(uintptr_t)(image+fr_h_sites[i].site);
        for(unsigned k=0;k<5;k++) if(s+k>=a && s+k<a+bytes) ((unsigned char*)copy)[s+k-a]=fr_h_original[k];
    }
    if(fr_done&FR_PG_DONE) {
        const uintptr_t s=(uintptr_t)(image+FR_PG_SITE);
        for(unsigned k=0;k<5;k++) if(s+k>=a && s+k<a+bytes) ((unsigned char*)copy)[s+k-a]=fr_pg_original[k];
    }
    if(fr_done&FR_AFF_DONE) {
        const uintptr_t s=(uintptr_t)(image+FR_AFF_SITE);
        for(unsigned k=0;k<5;k++) if(s+k>=a && s+k<a+bytes) ((unsigned char*)copy)[s+k-a]=fr_aff_original[k];
    }
    if(fr_done&FR_VEL_DONE) {
        const uintptr_t s=(uintptr_t)(image+FR_VEL_SITE);
        for(unsigned k=0;k<5;k++) if(s+k>=a && s+k<a+bytes) ((unsigned char*)copy)[s+k-a]=fr_vel_original[k];
    }
    if(fr_done&FR_FAC_DONE) {
        const uintptr_t s=(uintptr_t)(image+FR_FAC_SITE);
        for(unsigned k=0;k<5;k++) if(s+k>=a && s+k<a+bytes) ((unsigned char*)copy)[s+k-a]=fr_fac_original[k];
    }
}
/* No other module has patched site i's tail (site..back): its raw bytes are what the original-byte view shows. (Not
 * fp_patch_overlaps: the exact-reciprocal view answers 'yes' for the whole image once it is armed, which it is
 * before this module in the boot; FR10/FR11/W5 refused there.) */
static int fr_len_tail_clear(unsigned i) {
    unsigned char raw[0x20],viewed[0x20];const unsigned n=fr_len_sites[i].back-fr_len_sites[i].site;
    if(n>sizeof(raw) || !read_local(image+fr_len_sites[i].site,viewed,n)) return 0;
    memcpy(raw,(const void*)(image+fr_len_sites[i].site),n);
    return !memcmp(raw,viewed,n);
}
/* FR13: the handle sites, as the length sites (0 original, 1 our jmp, -1 anything else; clear = raw bytes are what the
 * view shows; pins through the view). */
static unsigned char *fr_h_stub(unsigned i) {return fr_page+FR_H_STUB+FR_LEN_STUB_SIZE*i;}
static int fr_h_reach(unsigned i) {
    const intptr_t d=(intptr_t)fr_h_stub(i)-(intptr_t)(image+fr_h_sites[i].site+5);
    return d>INT32_MIN && d<INT32_MAX;
}
static void fr_h_jump(unsigned i,unsigned char out[5]) {out[0]=0xe9;const int32_t d=fr_rel(fr_h_sites[i].site,fr_h_stub(i));memcpy(out+1,&d,4);}
static int fr_h_state(unsigned i) {
    unsigned char now[5],ours[5];memcpy(now,(const void*)(image+fr_h_sites[i].site),5);
    if(!memcmp(now,fr_h_original,5)) return 0;
    if(fr_page && fr_h_reach(i)) {fr_h_jump(i,ours);if(!memcmp(now,ours,5)) return 1;}
    return -1;
}
static int fr_h_clear(unsigned i) {
    unsigned char viewed[5];
    return read_local(image+fr_h_sites[i].site,viewed,5) && !memcmp((const void*)(image+fr_h_sites[i].site),viewed,5);
}
static int fr_h_pins_ok(void) {
    static unsigned char block[0x200];
    for(unsigned i=0;i<2;i++)
        if(fr_h_pins[i].pin_size>sizeof(block) || !read_local(image+fr_h_pins[i].pin,block,fr_h_pins[i].pin_size) ||
           !hash_ok(block,fr_h_pins[i].pin_size,fr_h_pins[i].sha)) return 0;
    return 1;
}
/* FR16: the PointGuard angle site, as the handle sites; clear = the raw bytes of site..back are what the view shows. */
static unsigned char *fr_pg_stub(void) {return fr_page+FR_PG_STUB;}
static int fr_pg_reach(void) {
    const intptr_t d=(intptr_t)fr_pg_stub()-(intptr_t)(image+FR_PG_SITE+5);
    return d>INT32_MIN && d<INT32_MAX;
}
static void fr_pg_jump(unsigned char out[5]) {out[0]=0xe9;const int32_t d=fr_rel(FR_PG_SITE,fr_pg_stub());memcpy(out+1,&d,4);}
static int fr_pg_state(void) {
    unsigned char now[5],ours[5];memcpy(now,(const void*)(image+FR_PG_SITE),5);
    if(!memcmp(now,fr_pg_original,5)) return 0;
    if(fr_page && fr_pg_reach()) {fr_pg_jump(ours);if(!memcmp(now,ours,5)) return 1;}
    return -1;
}
static int fr_pg_clear(void) {
    unsigned char viewed[FR_PG_BACK-FR_PG_SITE];
    return read_local(image+FR_PG_SITE,viewed,sizeof(viewed)) && !memcmp((const void*)(image+FR_PG_SITE),viewed,sizeof(viewed));
}
static int fr_pg_pins_ok(void) {
    static unsigned char block[FR_PG_PIN_SIZE];uint32_t pi=0;
    return read_local(image+FR_PG_PIN,block,FR_PG_PIN_SIZE) && hash_ok(block,FR_PG_PIN_SIZE,FR_PG_PIN_SHA) &&
           read_local(image+FR_PG_PI,&pi,4) && pi==FR_PG_PI_BITS;
}
/* FR22: the affine weight site, as the PointGuard one. */
static unsigned char *fr_aff_stub(void) {return fr_page+FR_AFF_STUB;}
static int fr_aff_reach(void) {
    const intptr_t d=(intptr_t)fr_aff_stub()-(intptr_t)(image+FR_AFF_SITE+5);
    return d>INT32_MIN && d<INT32_MAX;
}
static void fr_aff_jump(unsigned char out[5]) {out[0]=0xe9;const int32_t d=fr_rel(FR_AFF_SITE,fr_aff_stub());memcpy(out+1,&d,4);}
static int fr_aff_state(void) {
    unsigned char now[5],ours[5];memcpy(now,(const void*)(image+FR_AFF_SITE),5);
    if(!memcmp(now,fr_aff_original,5)) return 0;
    if(fr_page && fr_aff_reach()) {fr_aff_jump(ours);if(!memcmp(now,ours,5)) return 1;}
    return -1;
}
static int fr_aff_clear(void) {
    unsigned char viewed[FR_AFF_BACK-FR_AFF_SITE];
    return read_local(image+FR_AFF_SITE,viewed,sizeof(viewed)) && !memcmp((const void*)(image+FR_AFF_SITE),viewed,sizeof(viewed));
}
static int fr_aff_pins_ok(void) {
    static unsigned char block[FR_AFF_PIN_SIZE];uint32_t two=0;
    return read_local(image+FR_AFF_PIN,block,FR_AFF_PIN_SIZE) && hash_ok(block,FR_AFF_PIN_SIZE,FR_AFF_PIN_SHA) &&
           read_local(image+FR_AFF_TWO,&two,4) && two==0x40000000u;
}
/* FR23: the velocity site, as the affine one; its pins: the step through the view and the block site..back. */
static unsigned char *fr_vel_stub(void) {return fr_page+FR_VEL_STUB;}
static int fr_vel_reach(void) {
    const intptr_t d=(intptr_t)fr_vel_stub()-(intptr_t)(image+FR_VEL_SITE+5);
    return d>INT32_MIN && d<INT32_MAX;
}
static void fr_vel_jump(unsigned char out[5]) {out[0]=0xe9;const int32_t d=fr_rel(FR_VEL_SITE,fr_vel_stub());memcpy(out+1,&d,4);}
static int fr_vel_state(void) {
    unsigned char now[5],ours[5];memcpy(now,(const void*)(image+FR_VEL_SITE),5);
    if(!memcmp(now,fr_vel_original,5)) return 0;
    if(fr_page && fr_vel_reach()) {fr_vel_jump(ours);if(!memcmp(now,ours,5)) return 1;}
    return -1;
}
static int fr_vel_clear(void) {
    unsigned char viewed[FR_VEL_BACK-FR_VEL_SITE];
    return read_local(image+FR_VEL_SITE,viewed,sizeof(viewed)) && !memcmp((const void*)(image+FR_VEL_SITE),viewed,sizeof(viewed));
}
static int fr_vel_pins_ok(void) {
    static unsigned char step[FR_VEL_PIN_SIZE];unsigned char block[FR_VEL_BACK-FR_VEL_SITE];
    return read_local(image+FR_VEL_PIN,step,FR_VEL_PIN_SIZE) && hash_ok(step,FR_VEL_PIN_SIZE,FR_VEL_PIN_SHA) &&
           read_local(image+FR_VEL_SITE,block,sizeof(block)) && !memcmp(block,fr_vel_block,sizeof(block));
}
/* FR24: the facing factor site, as the velocity one; its pins: the step through the view, the block site..back, k. */
static unsigned char *fr_fac_stub(void) {return fr_page+FR_FAC_STUB;}
static int fr_fac_reach(void) {
    const intptr_t d=(intptr_t)fr_fac_stub()-(intptr_t)(image+FR_FAC_SITE+5);
    return d>INT32_MIN && d<INT32_MAX;
}
static void fr_fac_jump(unsigned char out[5]) {out[0]=0xe9;const int32_t d=fr_rel(FR_FAC_SITE,fr_fac_stub());memcpy(out+1,&d,4);}
static int fr_fac_state(void) {
    unsigned char now[5],ours[5];memcpy(now,(const void*)(image+FR_FAC_SITE),5);
    if(!memcmp(now,fr_fac_original,5)) return 0;
    if(fr_page && fr_fac_reach()) {fr_fac_jump(ours);if(!memcmp(now,ours,5)) return 1;}
    return -1;
}
static int fr_fac_clear(void) {
    unsigned char viewed[FR_FAC_BACK-FR_FAC_SITE];
    return read_local(image+FR_FAC_SITE,viewed,sizeof(viewed)) && !memcmp((const void*)(image+FR_FAC_SITE),viewed,sizeof(viewed));
}
static int fr_fac_pins_ok(void) {
    static unsigned char step[FR_VEL_PIN_SIZE];unsigned char block[FR_FAC_BACK-FR_FAC_SITE];uint32_t k=0;
    return read_local(image+FR_VEL_PIN,step,FR_VEL_PIN_SIZE) && hash_ok(step,FR_VEL_PIN_SIZE,FR_VEL_PIN_SHA) &&
           read_local(image+FR_FAC_SITE,block,sizeof(block)) && !memcmp(block,fr_fac_block,sizeof(block)) &&
           read_local(image+FR_FAC_K,&k,4) && k==FR_FAC_K_BITS;
}
/* FR22: the BezierSpline vtable's ComputeLength slot: native first, then ours on a BezierSpline's segment. */
typedef void (*fr_bl_fn)(void *,uint32_t);
static void fr_bl_hook(unsigned char *segment,uint32_t n) {
    ((fr_bl_fn)(void*)(image+FR_BL_FN))(segment,n);
    const void *vt=NULL;
    if(!segment || !read_local(segment,&vt,8) || vt!=(const void*)(image+FR_BL_VTABLE)) {InterlockedIncrement64(&fr_hits[FR_HIT_BL_FOREIGN]);return;}
    fr_bl_seen(segment,n);
}
/* 0 native, 1 ours, -1 anything else. */
static int fr_bl_state(void) {
    void *now=NULL;if(!read_local(image+FR_BL_SLOT,&now,8)) return -1;
    if(now==(void*)(image+FR_BL_FN)) return 0;
    if(now==(void*)fr_bl_hook) return 1;
    return -1;
}
static int fr_bl_swap(void *expected,void *value) {
    unsigned char *p=image+FR_BL_SLOT;DWORD old,ignored;
    if(!VirtualProtect(p,8,PAGE_READWRITE,&old)) return 0;
    void *actual=InterlockedCompareExchangePointer((void*volatile*)p,value,expected);
    VirtualProtect(p,8,old,&ignored);return actual==expected;
}
static int fr_bl_pins_ok(void) {
    static unsigned char fn[FR_BL_FN_SIZE],chord[FR_BL_CHORD_SIZE];void *slot=NULL;uint32_t one=0;
    return read_local(image+FR_BL_FN,fn,FR_BL_FN_SIZE) && hash_ok(fn,FR_BL_FN_SIZE,FR_BL_FN_SHA) &&
           read_local(image+FR_BL_CHORD,chord,FR_BL_CHORD_SIZE) && hash_ok(chord,FR_BL_CHORD_SIZE,FR_BL_CHORD_SHA) &&
           read_local(image+FR_BL_CHORD_SLOT,&slot,8) && slot==(void*)(image+FR_BL_CHORD) && read_local(image+FR_BL_ONE,&one,4) && one==0x3f800000u;
}
static int fr_len_pins(void) {
    static unsigned char block[0x200];
    for(unsigned i=0;i<FR_LEN_SITES;i++)
        if(fr_len_sites[i].pin_size>sizeof(block) || !read_local(image+fr_len_sites[i].pin,block,fr_len_sites[i].pin_size) ||
           !hash_ok(block,fr_len_sites[i].pin_size,fr_len_sites[i].sha)) return 0;
    return 1;
}
/* The refresh's bytes with the patched ones as the file has them, and M's. */
static int fr_pins(void) {
    static unsigned char copy[FR_REFRESH_SIZE];
    static unsigned char block[FR_P_BLOCK_SIZE],wsite[FR_W_SITE_BLOCK_SIZE],wbody[FR_W_BODY_SIZE];
    if(!read_local(image+FR_REFRESH,copy,FR_REFRESH_SIZE) || !read_local(image+FR_P_BLOCK,block,FR_P_BLOCK_SIZE) || fr_tail_state()<0) return 0;
    if(!read_local(image+FR_W_SITE_BLOCK,wsite,FR_W_SITE_BLOCK_SIZE) || !read_local(image+FR_W_BODY,wbody,FR_W_BODY_SIZE)) return 0;
    for(unsigned i=0;i<FR_SITES;i++) {
        if(fr_site_state(i)<0) return 0;
        const uint32_t s=fr_sites[i].site;const int32_t d=fr_rel_native(i);
        if(s>=FR_REFRESH && s+5<=FR_REFRESH+FR_REFRESH_SIZE) memcpy(copy+(s-FR_REFRESH)+1,&d,4);
        if(s>=FR_P_BLOCK && s+5<=FR_P_BLOCK+FR_P_BLOCK_SIZE) memcpy(block+(s-FR_P_BLOCK)+1,&d,4);
        if(s>=FR_W_SITE_BLOCK && s+5<=FR_W_SITE_BLOCK+FR_W_SITE_BLOCK_SIZE) memcpy(wsite+(s-FR_W_SITE_BLOCK)+1,&d,4);
    }
    memcpy(copy+(FR_TAIL-FR_REFRESH),fr_tail_original,5);
    unsigned char m[FR_M_SIZE];
    return hash_ok(copy,FR_REFRESH_SIZE,FR_REFRESH_SHA) && hash_ok(block,FR_P_BLOCK_SIZE,FR_P_BLOCK_SHA) &&
        hash_ok(wsite,FR_W_SITE_BLOCK_SIZE,FR_W_SITE_BLOCK_SHA) && hash_ok(wbody,FR_W_BODY_SIZE,FR_W_BODY_SHA) &&
        read_local(image+FR_M_RVA,m,FR_M_SIZE) && hash_ok(m,FR_M_SIZE,FR_M_SHA) && fr_len_pins() && fr_h_pins_ok() && fr_pg_pins_ok() &&
        fr_aff_pins_ok() && fr_bl_pins_ok() && fr_vel_pins_ok() && fr_fac_pins_ok();
}
static unsigned char *fr_alloc_page(void) {
    uintptr_t at=(uintptr_t)(image+FR_REFRESH)&~(uintptr_t)65535;
    for(uintptr_t distance=65536;distance<0x70000000;distance+=65536)
        for(int sign=-1;sign<=1;sign+=2) {
            uintptr_t candidate=sign<0?at-distance:at+distance;
            unsigned char *p=VirtualAlloc((void*)candidate,0x1000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
            if(!p) continue;
            intptr_t d=(intptr_t)p-(intptr_t)(image+FR_REFRESH);
            if(d>INT32_MIN+0x10000 && d<INT32_MAX-0x10000) return p;
            VirtualFree(p,0,MEM_RELEASE);
        }
    return NULL;
}
static int fr_site_cas(unsigned i,int32_t expected,int32_t value) {
    unsigned char e[4],v[4];memcpy(e,&expected,4);memcpy(v,&value,4);return view_cas_bytes(fr_sites[i].site+1,e,v,4);
}
static void fr_restore(void) {
    if(!fr_installed) return;
    /* G4 first: no new refresh begins while the rest goes back. */
    for(unsigned i=0;i<FR_SITES;i++)
        if((fr_done>>i)&1u && fr_site_state(i)==1 && fr_site_cas(i,fr_rel_ours(i),fr_rel_native(i))) fr_done&=~(1u<<i);
    if(fr_done&FR_TAIL_DONE && fr_tail_state()==1) {
        unsigned char jump[5];fr_tail_jump(jump);
        if(view_cas_bytes(FR_TAIL,jump,fr_tail_original,5)) fr_done&=~FR_TAIL_DONE;
    }
    for(unsigned i=0;i<FR_LEN_SITES;i++)
        if(fr_done&(FR_LEN_DONE<<i) && fr_len_state(i)==1) {
            unsigned char jump[5];fr_len_jump(i,jump);
            if(view_cas_bytes(fr_len_sites[i].site,jump,fr_len_original[i],5)) fr_done&=~(FR_LEN_DONE<<i);
        }
    for(unsigned i=0;i<FR_H_SITES;i++)
        if(fr_done&(FR_H_DONE<<i) && fr_h_state(i)==1) {
            unsigned char jump[5];fr_h_jump(i,jump);
            if(view_cas_bytes(fr_h_sites[i].site,jump,fr_h_original,5)) fr_done&=~(FR_H_DONE<<i);
        }
    if(fr_done&FR_PG_DONE && fr_pg_state()==1) {
        unsigned char jump[5];fr_pg_jump(jump);
        if(view_cas_bytes(FR_PG_SITE,jump,fr_pg_original,5)) fr_done&=~FR_PG_DONE;
    }
    if(fr_done&FR_AFF_DONE && fr_aff_state()==1) {
        unsigned char jump[5];fr_aff_jump(jump);
        if(view_cas_bytes(FR_AFF_SITE,jump,fr_aff_original,5)) fr_done&=~FR_AFF_DONE;
    }
    if(fr_done&FR_BL_DONE && fr_bl_state()==1 && fr_bl_swap((void*)fr_bl_hook,(void*)(image+FR_BL_FN))) fr_done&=~FR_BL_DONE;
    if(fr_done&FR_VEL_DONE && fr_vel_state()==1) {
        unsigned char jump[5];fr_vel_jump(jump);
        if(view_cas_bytes(FR_VEL_SITE,jump,fr_vel_original,5)) fr_done&=~FR_VEL_DONE;
    }
    if(fr_done&FR_FAC_DONE && fr_fac_state()==1) {
        unsigned char jump[5];fr_fac_jump(jump);
        if(view_cas_bytes(FR_FAC_SITE,jump,fr_fac_original,5)) fr_done&=~FR_FAC_DONE;
    }
    if(!fr_done) {fr_installed=0;fr_restored=1;fr_mode=FR_OFF;}
}
/* Before the snapshot load and the first admitted frame, on the MoveWorker; a rerun with the same mode is accepted. */
static int fr_arm(int mode,uint64_t ticks) {
    if(fr_installed) return stats.active && fr_mode==mode && fr_ticks==ticks;
    if(!fr_arm_ready(mode,ticks)) return 0;
    if(fr_tail_state()!=0) {fr_stage="pins";return 0;}
    for(unsigned i=0;i<FR_SITES;i++) if(fr_site_state(i)!=0) {fr_stage="pins";return 0;}
    for(unsigned i=0;i<FR_LEN_SITES;i++)
        if(fr_len_state(i)!=0 || !fr_len_tail_clear(i)) {fr_stage="pins";return 0;}
    for(unsigned i=0;i<FR_H_SITES;i++)
        if(fr_h_state(i)!=0 || !fr_h_clear(i)) {fr_stage="pins";return 0;}
    if(fr_pg_state()!=0 || !fr_pg_clear()) {fr_stage="pins";return 0;}
    if(fr_aff_state()!=0 || !fr_aff_clear() || fr_bl_state()!=0) {fr_stage="pins";return 0;}
    if(fr_vel_state()!=0 || !fr_vel_clear()) {fr_stage="pins";return 0;}
    if(fr_fac_state()!=0 || !fr_fac_clear()) {fr_stage="pins";return 0;}
    if(!fr_pins()) {fr_stage="pins";return 0;}
    if(!fr_page) fr_page=fr_alloc_page();
    if(!fr_page) {fr_stage="page";return 0;}
    for(unsigned i=0;i<FR_LEN_SITES;i++) if(!fr_len_reach(i)) {fr_stage="page";return 0;}
    for(unsigned i=0;i<FR_H_SITES;i++) if(!fr_h_reach(i)) {fr_stage="page";return 0;}
    if(!fr_pg_reach() || !fr_aff_reach() || !fr_vel_reach() || !fr_fac_reach()) {fr_stage="page";return 0;}
    DWORD old,ignored;if(!VirtualProtect(fr_page,0x1000,PAGE_READWRITE,&old)) {fr_stage="page";return 0;}
    fr_write_code(fr_page);
    if(!VirtualProtect(fr_page,0x1000,PAGE_EXECUTE_READ,&ignored)) {fr_stage="page";return 0;}
    FlushInstructionCache(GetCurrentProcess(),fr_page,0x1000);
    fr_native_refresh=(fr_refresh_fn)(void*)(image+FR_REFRESH);
    fr_native_g4=(fr_getter_fn)(void*)(image+0x5f91b0u);fr_native_g0=(fr_getter_fn)(void*)(image+0x5f9340u);
    fr_native_m=(fr_m_fn)(void*)(image+FR_M_RVA);fr_native_params=(fr_params_fn)(void*)(image+0x6dc610u);
    fr_native_v=(fr_v_fn)(void*)(image+0x3dc040u);fr_native_writer=(fr_writer_fn)(void*)(image+FR_W_BODY);
    fr_mode=mode;fr_installed=1;fr_restored=0;fr_done=0;
    /* The length sites first (independent of the refresh), then G0, the Ms and the first call, then the tail, G4
     * last: a refresh that begins finds the rest in place. */
    for(unsigned i=0;i<FR_LEN_SITES;i++) {
        unsigned char jump[5];fr_len_jump(i,jump);
        if(!view_cas_bytes(fr_len_sites[i].site,fr_len_original[i],jump,5)) {fr_stage="cas";fr_restore();fr_installed=0;fr_mode=FR_OFF;return 0;}
        fr_done|=FR_LEN_DONE<<i;
    }
    for(unsigned i=0;i<FR_H_SITES;i++) { /* FR13: as the length sites, independent of the refresh */
        unsigned char jump[5];fr_h_jump(i,jump);
        if(!view_cas_bytes(fr_h_sites[i].site,fr_h_original,jump,5)) {fr_stage="cas";fr_restore();fr_installed=0;fr_mode=FR_OFF;return 0;}
        fr_done|=FR_H_DONE<<i;
    }
    { /* FR16: the PointGuard angle, likewise */
        unsigned char jump[5];fr_pg_jump(jump);
        if(!view_cas_bytes(FR_PG_SITE,fr_pg_original,jump,5)) {fr_stage="cas";fr_restore();fr_installed=0;fr_mode=FR_OFF;return 0;}
        fr_done|=FR_PG_DONE;
    }
    { /* FR22: the affine weight, likewise; then the BezierSpline vtable's ComputeLength slot */
        unsigned char jump[5];fr_aff_jump(jump);
        if(!view_cas_bytes(FR_AFF_SITE,fr_aff_original,jump,5)) {fr_stage="cas";fr_restore();fr_installed=0;fr_mode=FR_OFF;return 0;}
        fr_done|=FR_AFF_DONE;
        if(!fr_bl_swap((void*)(image+FR_BL_FN),(void*)fr_bl_hook)) {fr_stage="cas";fr_restore();fr_installed=0;fr_mode=FR_OFF;return 0;}
        fr_done|=FR_BL_DONE;
    }
    { /* FR23: the velocity store, likewise */
        unsigned char jump[5];fr_vel_jump(jump);
        if(!view_cas_bytes(FR_VEL_SITE,fr_vel_original,jump,5)) {fr_stage="cas";fr_restore();fr_installed=0;fr_mode=FR_OFF;return 0;}
        fr_done|=FR_VEL_DONE;
    }
    { /* FR24: the facing factor, likewise */
        unsigned char jump[5];fr_fac_jump(jump);
        if(!view_cas_bytes(FR_FAC_SITE,fr_fac_original,jump,5)) {fr_stage="cas";fr_restore();fr_installed=0;fr_mode=FR_OFF;return 0;}
        fr_done|=FR_FAC_DONE;
    }
    for(unsigned k=1;k<=FR_SITES;k++) {
        const unsigned i=k%FR_SITES;
        if(i==0) {
            unsigned char jump[5];fr_tail_jump(jump);
            if(!view_cas_bytes(FR_TAIL,fr_tail_original,jump,5)) {fr_stage="cas";fr_restore();fr_installed=0;fr_mode=FR_OFF;return 0;}
            fr_done|=FR_TAIL_DONE;
        }
        if(!fr_site_cas(i,fr_rel_native(i),fr_rel_ours(i))) {fr_stage="cas";fr_restore();fr_installed=0;fr_mode=FR_OFF;return 0;}
        fr_done|=1u<<i;
    }
    fr_stage="armed";return 1;
}
static int fr_owned(void) {
    for(unsigned i=0;i<FR_SITES;i++) if((fr_done>>i)&1u && fr_site_state(i)!=1) return 0;
    if(fr_done&FR_TAIL_DONE && fr_tail_state()!=1) return 0;
    for(unsigned i=0;i<FR_LEN_SITES;i++) if(fr_done&(FR_LEN_DONE<<i) && fr_len_state(i)!=1) return 0;
    for(unsigned i=0;i<FR_H_SITES;i++) if(fr_done&(FR_H_DONE<<i) && fr_h_state(i)!=1) return 0;
    if(fr_done&FR_PG_DONE && fr_pg_state()!=1) return 0;
    if(fr_done&FR_AFF_DONE && fr_aff_state()!=1) return 0;
    if(fr_done&FR_BL_DONE && fr_bl_state()!=1) return 0;
    if(fr_done&FR_VEL_DONE && fr_vel_state()!=1) return 0;
    if(fr_done&FR_FAC_DONE && fr_fac_state()!=1) return 0;
    unsigned char want[FR_CODE];fr_write_code(want);
    return !memcmp(fr_page,want,FR_CODE);
}
static int fr_check(void) {
    if(InterlockedCompareExchange64(&fr_failures,0,0)) return 0;
    if(!fr_owned()) {fr_stage="site_changed";InterlockedIncrement64(&fr_failures);return 0;}
    return 1;
}
/* The Lua methods (lockstep_fp_redirect_lua.h) come with the gate's other ones, after its Lua ABI. */
