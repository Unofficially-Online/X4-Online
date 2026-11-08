/* Portable SHA-256 (FIPS 180-4) for gate builds without BCrypt: the Linux gate's
 * build pins, its file pin and gate.sha256 (the probe's per-tick digest). Same
 * results as BCrypt SHA-256; vectors in linux/test_gate_linux.c. Original code. */
#ifndef LOCKSTEP_SHA256_H
#define LOCKSTEP_SHA256_H
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#if !defined(LOCKSTEP_SHA256_DISABLE_HARDWARE) && defined(__linux__) && defined(__x86_64__) && \
    (defined(__GNUC__) || defined(__clang__))
#include <cpuid.h>
#include <immintrin.h>
#define LOCKSTEP_SHA256_X86 1
#else
#define LOCKSTEP_SHA256_X86 0
#endif
typedef struct {
    uint32_t h[8];
    uint64_t bytes;
    unsigned char block[64];
    size_t used;
    int hardware;
} lockstep_sha256_ctx;
static const uint32_t lockstep_sha256_k[64]={
    0x428a2f98u,0x71374491u,0xb5c0fbcfu,0xe9b5dba5u,0x3956c25bu,0x59f111f1u,0x923f82a4u,0xab1c5ed5u,
    0xd807aa98u,0x12835b01u,0x243185beu,0x550c7dc3u,0x72be5d74u,0x80deb1feu,0x9bdc06a7u,0xc19bf174u,
    0xe49b69c1u,0xefbe4786u,0x0fc19dc6u,0x240ca1ccu,0x2de92c6fu,0x4a7484aau,0x5cb0a9dcu,0x76f988dau,
    0x983e5152u,0xa831c66du,0xb00327c8u,0xbf597fc7u,0xc6e00bf3u,0xd5a79147u,0x06ca6351u,0x14292967u,
    0x27b70a85u,0x2e1b2138u,0x4d2c6dfcu,0x53380d13u,0x650a7354u,0x766a0abbu,0x81c2c92eu,0x92722c85u,
    0xa2bfe8a1u,0xa81a664bu,0xc24b8b70u,0xc76c51a3u,0xd192e819u,0xd6990624u,0xf40e3585u,0x106aa070u,
    0x19a4c116u,0x1e376c08u,0x2748774cu,0x34b0bcb5u,0x391c0cb3u,0x4ed8aa4au,0x5b9cca4fu,0x682e6ff3u,
    0x748f82eeu,0x78a5636fu,0x84c87814u,0x8cc70208u,0x90befffau,0xa4506cebu,0xbef9a3f7u,0xc67178f2u};
static uint32_t lockstep_sha256_ror(uint32_t x,unsigned n) {return (x>>n)|(x<<(32u-n));}
static void lockstep_sha256_compress_scalar(lockstep_sha256_ctx *c,const unsigned char *p) {
    uint32_t w[64],a,b,d,e,f,g,h,s;
    for(unsigned i=0;i<16;i++) w[i]=(uint32_t)p[4*i]<<24|(uint32_t)p[4*i+1]<<16|(uint32_t)p[4*i+2]<<8|p[4*i+3];
    for(unsigned i=16;i<64;i++) {
        uint32_t s0=lockstep_sha256_ror(w[i-15],7)^lockstep_sha256_ror(w[i-15],18)^(w[i-15]>>3);
        uint32_t s1=lockstep_sha256_ror(w[i-2],17)^lockstep_sha256_ror(w[i-2],19)^(w[i-2]>>10);
        w[i]=w[i-16]+s0+w[i-7]+s1;
    }
    a=c->h[0];b=c->h[1];s=c->h[2];d=c->h[3];e=c->h[4];f=c->h[5];g=c->h[6];h=c->h[7];
    for(unsigned i=0;i<64;i++) {
        uint32_t t1=h+(lockstep_sha256_ror(e,6)^lockstep_sha256_ror(e,11)^lockstep_sha256_ror(e,25))+((e&f)^(~e&g))+lockstep_sha256_k[i]+w[i];
        uint32_t t2=(lockstep_sha256_ror(a,2)^lockstep_sha256_ror(a,13)^lockstep_sha256_ror(a,22))+((a&b)^(a&s)^(b&s));
        h=g;g=f;f=e;e=d+t1;d=s;s=b;b=a;a=t1+t2;
    }
    c->h[0]+=a;c->h[1]+=b;c->h[2]+=s;c->h[3]+=d;c->h[4]+=e;c->h[5]+=f;c->h[6]+=g;c->h[7]+=h;
}
/* SHA256RNDS2 performs two rounds; SHA256MSG1/2 extend four schedule
 * words at a time. The four schedule vectors form a rotating 16-word window.
 * SHA256MSG1 contributes the older small-sigma terms, ALIGNR selects W[t-7]
 * through W[t-4], and SHA256MSG2 completes the newer small-sigma terms.
 * The round vectors use [F,E,B,A] and [H,G,D,C], low lane first. */
#if LOCKSTEP_SHA256_X86
__attribute__((target("sha,ssse3")))
static void lockstep_sha256_compress_hardware(lockstep_sha256_ctx *context, const unsigned char *input_bytes) {
    uint32_t initial[16];
    for (unsigned i = 0; i < 16; ++i)
        initial[i] = (uint32_t)input_bytes[4*i]<<24 | (uint32_t)input_bytes[4*i+1]<<16 | (uint32_t)input_bytes[4*i+2]<<8 | input_bytes[4*i+3];
    __m128i schedule[4];
    for (unsigned i = 0; i < 4; ++i)
        schedule[i] = _mm_set_epi32((int)initial[4*i+3], (int)initial[4*i+2],
                                    (int)initial[4*i+1], (int)initial[4*i]);
    __m128i first = _mm_set_epi32((int)context->h[0], (int)context->h[1], (int)context->h[4], (int)context->h[5]);
    __m128i second = _mm_set_epi32((int)context->h[2], (int)context->h[3], (int)context->h[6], (int)context->h[7]);
    for (unsigned group = 0; group < 16; ++group) {
        unsigned slot = group & 3u;
        if (group >= 4) {
            __m128i older = _mm_sha256msg1_epu32(schedule[slot], schedule[(slot+1)&3u]);
            __m128i middle = _mm_alignr_epi8(schedule[(slot+3)&3u], schedule[(slot+2)&3u], 4);
            schedule[slot] = _mm_sha256msg2_epu32(_mm_add_epi32(older, middle), schedule[(slot+3)&3u]);
        }
        unsigned index = 4 * group;
        __m128i constants = _mm_set_epi32((int)lockstep_sha256_k[index+3], (int)lockstep_sha256_k[index+2],
                                          (int)lockstep_sha256_k[index+1], (int)lockstep_sha256_k[index]);
        __m128i input = _mm_add_epi32(schedule[slot], constants);
        second = _mm_sha256rnds2_epu32(second, first, input);
        input = _mm_shuffle_epi32(input, 0x0e);
        first = _mm_sha256rnds2_epu32(first, second, input);
    }
    uint32_t first_lanes[4], second_lanes[4];
    _mm_storeu_si128((__m128i *)(void *)first_lanes, first);
    _mm_storeu_si128((__m128i *)(void *)second_lanes, second);
    context->h[0] += first_lanes[3]; context->h[1] += first_lanes[2];
    context->h[2] += second_lanes[3]; context->h[3] += second_lanes[2];
    context->h[4] += first_lanes[1]; context->h[5] += first_lanes[0];
    context->h[6] += second_lanes[1]; context->h[7] += second_lanes[0];
}
#endif

#if LOCKSTEP_SHA256_X86
/* This predicate is separate from CPUID so feature-denial paths can be tested
 * while the accelerated function is still present in the binary. */
static int lockstep_sha256_features_supported(unsigned max_leaf, unsigned leaf1_ecx, unsigned leaf7_ebx) {
    return max_leaf >= 7 && (leaf1_ecx & (1u << 9)) && (leaf7_ebx & (1u << 29));
}
static int lockstep_sha256_probe(void) {
    unsigned max_leaf, leaf1_ecx = 0, leaf7_ebx = 0;
#ifdef LOCKSTEP_SHA256_TEST_CPUID
    lockstep_sha256_test_cpuid(&max_leaf, &leaf1_ecx, &leaf7_ebx);
#else
    unsigned eax, ebx, ecx, edx;
    max_leaf = __get_cpuid_max(0, NULL);
    if (max_leaf >= 1 && __get_cpuid(1, &eax, &ebx, &ecx, &edx)) leaf1_ecx = ecx;
    if (max_leaf >= 7 && __get_cpuid_count(7, 0, &eax, &ebx, &ecx, &edx)) leaf7_ebx = ebx;
#endif
    return lockstep_sha256_features_supported(max_leaf, leaf1_ecx, leaf7_ebx);
}
#endif

/* Each context captures the decision once. Atomic publication is thread-safe;
 * racing first calls may independently probe but cannot observe partial data.
 * Linux x86-64 guarantees SSE2; the hardware path also requires SSSE3 (leaf 1
 * ECX bit 9) and SHA (leaf 7 EBX bit 29). */
static int lockstep_sha256_hardware_available(void) {
#if LOCKSTEP_SHA256_X86
    static int cached;
    int value = __atomic_load_n(&cached, __ATOMIC_ACQUIRE);
    if (!value) {
        value = lockstep_sha256_probe() ? 2 : 1;
        __atomic_store_n(&cached, value, __ATOMIC_RELEASE);
    }
    return value == 2;
#else
    return 0;
#endif
}
static void lockstep_sha256_compress(lockstep_sha256_ctx *c, const unsigned char *p) {
#if LOCKSTEP_SHA256_X86
    if (c->hardware) { lockstep_sha256_compress_hardware(c, p); return; }
#endif
    lockstep_sha256_compress_scalar(c, p);
}
static void lockstep_sha256_init(lockstep_sha256_ctx *c) {
    static const uint32_t start[8]={0x6a09e667u,0xbb67ae85u,0x3c6ef372u,0xa54ff53au,0x510e527fu,0x9b05688cu,0x1f83d9abu,0x5be0cd19u};
    memcpy(c->h,start,sizeof(start));c->bytes=0;c->used=0;c->hardware=lockstep_sha256_hardware_available();
}
static void lockstep_sha256_update(lockstep_sha256_ctx *c,const void *data,size_t length) {
    const unsigned char *p=(const unsigned char*)data;c->bytes+=length; /* C and C++ */
    if(c->used) {
        size_t take=64-c->used<length?64-c->used:length;
        memcpy(c->block+c->used,p,take);c->used+=take;p+=take;length-=take;
        if(c->used<64) return;
        lockstep_sha256_compress(c,c->block);c->used=0;
    }
    for(;length>=64;p+=64,length-=64) lockstep_sha256_compress(c,p);
    if(length) {memcpy(c->block,p,length);c->used=length;}
}
static void lockstep_sha256_final(lockstep_sha256_ctx *c,unsigned char out[32]) {
    uint64_t bits=c->bytes*8u;unsigned char pad[72]={0x80};
    size_t n=c->used<56?56-c->used:120-c->used;
    for(unsigned i=0;i<8;i++) pad[n+i]=(unsigned char)(bits>>(56-8*i));
    lockstep_sha256_update(c,pad,n+8);
    for(unsigned i=0;i<8;i++) for(unsigned j=0;j<4;j++) out[4*i+j]=(unsigned char)(c->h[i]>>(24-8*j));
}
static void lockstep_sha256(const void *data,size_t length,unsigned char out[32]) {
    lockstep_sha256_ctx c;lockstep_sha256_init(&c);lockstep_sha256_update(&c,data,length);lockstep_sha256_final(&c,out);
}
#endif
