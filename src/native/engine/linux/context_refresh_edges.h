#ifndef X4_CONTEXT_REFRESH_EDGES_H
#define X4_CONTEXT_REFRESH_EDGES_H

/* Unwired policy for authenticated bind and destructor refresh suppression.
 * No installer, memory protection, or engine hooks are provided here. The
 * integration must authenticate the P2 object and serialize its lifecycle;
 * this helper does not establish bank, context-reader, animation, or zone
 * ownership. No token may outlive its storage. */
#include <stdatomic.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <limits.h>

#define CRE_IMAGE_SHA256 "113f73d74570084f6ce18a659b365009eccf579fb66dbabb5dad2b1718045f20"
#define CRE_REFRESH_SHA256 "239c4ec0e606c95d16d15966608a361b2fc25f4680cee6242936314735602233"
#define CRE_DESTRUCTOR_SHA256 "0c89cab7cb46b07e7b029854f2d6d49d71ceea7934aee8a0ab5b1ac351080ab2"
#define CRE_BIND_SHA256 "f8cf846d66831724c43071694062f0725497f83178efc18b525da66e275fdad7"
#define CRE_REFRESH_VA UINT64_C(0x165d860)
#define CRE_BIND_VA UINT64_C(0x165dc20)
#define CRE_DESTRUCTOR_EDGE_VA UINT64_C(0x164c302)
#define CRE_BIND_HELPER_VA UINT64_C(0x1438b30)
#define CRE_BIND_HELPER_SHA256 "b952e948a3f0e0685270384d305cbac6bc106939ec20333c949016cf148f983d"
static const unsigned char cre_destructor_original[5] = {0xe8,0x59,0x15,0x01,0x00};

typedef enum { CRE_BIND = 1, CRE_DESTRUCT = 2 } cre_phase;
/* An integration supplies a correctly typed shim; do not cast the native
 * void refresh to this callback type. No native return value is specified. */
typedef uintptr_t (*cre_original_fn)(uintptr_t);
typedef int (*cre_auth_fn)(void *context, const void *object, cre_phase phase);
typedef struct {
    atomic_uint state; /* 0 idle, 1 reserved/active, 2 irrecoverably uncertain */
    const void *object;
    cre_phase phase;
    unsigned hits;
} cre_token;
static _Thread_local cre_token *cre_current;

static int cre_begin(cre_token *token, const void *object, cre_phase phase,
                     cre_auth_fn authenticate, void *context) {
    if (!token || !object || !authenticate ||
        (phase != CRE_BIND && phase != CRE_DESTRUCT)) return 0;
    if (cre_current) {
        atomic_store(&cre_current->state, 2);
        return 0;
    }
    unsigned idle = 0;
    if (!atomic_compare_exchange_strong(&token->state, &idle, 1)) return 0;
    cre_current = token;
    if (!authenticate(context, object, phase)) {
        cre_current = NULL;
        if (atomic_load(&token->state) == 1) atomic_store(&token->state, 0);
        return 0;
    }
    if (atomic_load(&token->state) != 1) {
        cre_current = NULL;
        return 0;
    }
    token->object = object;
    token->phase = phase;
    token->hits = 0;
    return 1;
}

/* Forward unexpected edges once and poison the scope; caller must discard
 * uncertain state. Outside a scope there is no modification of the call. */
static uintptr_t cre_dispatch(cre_phase edge, uintptr_t arg, const void *actual,
                              cre_original_fn original) {
    cre_token *token = cre_current;
    if (!original) {
        if (token) atomic_store(&token->state, 2);
        return 0;
    }
    if (!token) return original(arg);
    if (atomic_load(&token->state) != 1 || token->phase != edge || token->object != actual ||
        !token->object || token->hits != 0) {
        atomic_store(&token->state, 2);
        return original(arg);
    }
    token->hits++;
    return 0; /* native refresh has void return */
}

/* expected_hits may be zero when the native destructor branch is not taken.
 * A wrong-thread end permanently poisons the token without clearing its owner. */
static int cre_end(cre_token *token, unsigned expected_hits) {
    if (!token) return 0;
    if (cre_current != token) {
        atomic_store(&token->state, 2);
        return 0;
    }
    if (expected_hits > 1 || atomic_load(&token->state) != 1 ||
        token->hits != expected_hits) {
        atomic_store(&token->state, 2);
        cre_current = NULL;
        return 0;
    }
    cre_current = NULL;
    token->object = NULL;
    token->phase = 0;
    atomic_store(&token->state, 0);
    return 1;
}

/* A near stub jumps without changing registers or stack. It must be made RX
 * before a site can point to it; this encoder does not manage that obligation. */
static void cre_emit_jump(unsigned char out[14], uintptr_t destination) {
    out[0] = 0xff; out[1] = 0x25;
    memset(out + 2, 0, 4);
    memcpy(out + 6, &destination, 8);
}
static inline void cre_emit_destructor_jump(unsigned char out[17], uintptr_t destination) {
    /* mov rsi,rbx; jmp [rip+0]. RDI, RBX and the original return address are preserved. */
    out[0] = 0x48; out[1] = 0x89; out[2] = 0xde;
    cre_emit_jump(out + 3, destination);
}
static int cre_encode_edge(unsigned char out[5], unsigned char opcode,
                           uintptr_t site, uintptr_t target) {
    if ((opcode != 0xe8 && opcode != 0xe9) || site > UINTPTR_MAX - 5) return 0;
    uintptr_t next = site + 5;
    int64_t displacement;
    if (target >= next) {
        if (target - next > INT32_MAX) return 0;
        displacement = (int64_t)(target - next);
    } else {
        if (next - target > (uint64_t)INT32_MAX + 1) return 0;
        displacement = -(int64_t)(next - target);
    }
    int32_t rel = (int32_t)displacement;
    out[0] = opcode;
    memcpy(out + 1, &rel, 4);
    return 1;
}
#endif
