#ifndef X4_LINUX_PLAYER_REFERENCE_PROBE_H
#define X4_LINUX_PLAYER_REFERENCE_PROBE_H

/* Default-off, engine-free P1 reference round trip. No factory, seating or P2.
 * The adapter owns all native calls and fault containment. Include no guessed
 * SEH/signal recovery here: an interrupted native call is UNCERTAIN.
 *
 * request/run/status require serialized management and resident callback state.
 * boundary() must establish the admitted MoveWorker, completed path join, no
 * input/update/selected callback depth, no nested operation, and stable P1
 * ownership/reference writers throughout run(). A zero depth alone is not proof.
 * sample() must use the existing fresh canonical identity sampler, require ready,
 * and return its COMPLETE normalized sample (padding initialized), not just a
 * pointer subset. Each reference read/call uses that sample's fresh P1 address.
 * pins() must bind the exact pinned ELF, both functions and relocated vtables.
 * Rechecking boundaries cannot replace lifetime/exclusion across native calls.
 *
 * Marker 1 is deliberately refused: acquire changes it to 2 irreversibly.
 * Counts require >=2 and signed overflow room. Native release takes P1+0x38,
 * never P1, and must not enter its zero-count deferred-destruction path.
 * Any failed run is terminal, including a refused release. Never retry release
 * or infer cleanup from a later count alone; retain unresolved obligations.
 */
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define P1_REFERENCE_IDENTITY_MAX 512u
#define P1_REFERENCE_ELF_SHA256 "113f73d74570084f6ce18a659b365009eccf579fb66dbabb5dad2b1718045f20"
#define P1_REFERENCE_ACQUIRE_VA 0xa238b0u
#define P1_REFERENCE_RELEASE_VA 0xa14540u
#define P1_REFERENCE_PLAYER_VTABLE_VA 0x3b5f7a0u
#define P1_REFERENCE_SUBOBJECT_VTABLE_VA 0x3b61a10u
#define P1_REFERENCE_ZERO_TARGET_VA 0x18a7df0u

typedef struct {
    unsigned va;
    unsigned length;
    const char *sha256;
} p1_reference_pin;

static const p1_reference_pin p1_reference_pins[] = {
    {P1_REFERENCE_ACQUIRE_VA, 21,
     "0e7c126afb520730e79452b02c5cedd696ecdc918f11b71aab1501dead392f84"},
    {P1_REFERENCE_RELEASE_VA, 37,
     "7368cef95d1efcc8cda06dfd53df92ca026f1da024481b82f3c7a86b8db75587"},
};

typedef struct {
    int ready;
    uintptr_t player;
    unsigned length;
    unsigned char bytes[P1_REFERENCE_IDENTITY_MAX];
} p1_reference_identity;

typedef struct {
    uintptr_t player_vtable;
    uintptr_t reference_vtable;
    uintptr_t zero_target;
    uint32_t count;
    uint32_t marker;
} p1_reference_state;

/* NOT_CALLED guarantees native entry was never invoked and no effect occurred.
 * RETURNED means normal return only; the core must still verify its effect.
 * Any interrupted/ambiguous invocation is UNCERTAIN, even if a count looks right. */
typedef enum {
    P1_REFERENCE_NOT_CALLED,
    P1_REFERENCE_RETURNED,
    P1_REFERENCE_UNCERTAIN
} p1_reference_call_result;

typedef struct {
    void *context;
    int (*boundary)(void *context);
    int (*pins)(void *context, const p1_reference_pin *pins, unsigned count);
    int (*sample)(void *context, p1_reference_identity *identity);
    int (*read_reference)(void *context, uintptr_t player, p1_reference_state *state);
    p1_reference_call_result (*acquire)(void *context, uintptr_t function, uintptr_t player);
    p1_reference_call_result (*release)(void *context, uintptr_t function, uintptr_t subobject);
} p1_reference_operations;

typedef enum {
    P1_REFERENCE_NO_OBLIGATION,
    P1_REFERENCE_ACQUIRE_PENDING,
    P1_REFERENCE_HOLD_VERIFIED,
    P1_REFERENCE_RELEASE_PENDING,
    P1_REFERENCE_READBACK_PENDING
} p1_reference_obligation;

typedef struct {
    unsigned requested;
    unsigned attempted;
    unsigned passed;
    unsigned failed;
    p1_reference_obligation obligation;
    const char *reason;
    p1_reference_operations operations;
    uintptr_t image;
    unsigned identity_bytes;
    uint32_t initial_count;
    uint32_t last_observed_count;
    uint32_t last_observed_marker;
    unsigned reference_samples;
    p1_reference_identity initial_identity;
} p1_reference_probe;

typedef struct {
    unsigned requested;
    unsigned attempted;
    unsigned passed;
    unsigned failed;
    unsigned clean;
    p1_reference_obligation obligation;
    const char *reason;
    uint32_t initial_count;
    uint32_t last_observed_count;
    uint32_t last_observed_marker;
    unsigned reference_samples;
} p1_reference_probe_status;

static inline int p1_reference_fail(p1_reference_probe *probe, const char *reason) {
    probe->failed = 1;
    probe->reason = reason;
    return 0;
}

/* Caller supplies a zero-initialized, never reinitialized probe. */
static inline int p1_reference_request(p1_reference_probe *probe,
                                      const p1_reference_operations *operations,
                                      uintptr_t image, unsigned identity_bytes) {
    if (!probe || probe->requested || probe->attempted || probe->failed ||
        probe->obligation != P1_REFERENCE_NO_OBLIGATION || !operations ||
        !operations->boundary || !operations->pins || !operations->sample ||
        !operations->read_reference || !operations->acquire || !operations->release ||
        !image || image > UINTPTR_MAX - P1_REFERENCE_SUBOBJECT_VTABLE_VA ||
        !identity_bytes || identity_bytes > P1_REFERENCE_IDENTITY_MAX) {
        return 0;
    }
    probe->operations = *operations;
    probe->image = image;
    probe->identity_bytes = identity_bytes;
    probe->requested = 1;
    probe->reason = "requested";
    return 1;
}

static inline uintptr_t p1_reference_address(const p1_reference_probe *probe, unsigned va) {
    return probe->image + va - 0x400000u;
}

static inline int p1_reference_snapshot(p1_reference_probe *probe,
                                       p1_reference_identity *identity,
                                       p1_reference_state *state) {
    p1_reference_operations *operations = &probe->operations;
    if (!operations->boundary(operations->context)) {
        return p1_reference_fail(probe, "boundary");
    }
    if (!operations->pins(operations->context, p1_reference_pins,
                         sizeof(p1_reference_pins) / sizeof(p1_reference_pins[0]))) {
        return p1_reference_fail(probe, "pins");
    }
    memset(identity, 0, sizeof(*identity));
    memset(state, 0, sizeof(*state));
    if (!operations->sample(operations->context, identity) || !identity->ready ||
        !identity->player || identity->player > UINTPTR_MAX - 0x48 ||
        identity->length != probe->identity_bytes) {
        return p1_reference_fail(probe, "identity");
    }
    if (probe->initial_identity.player &&
        (identity->player != probe->initial_identity.player ||
         memcmp(identity->bytes, probe->initial_identity.bytes, probe->identity_bytes))) {
        return p1_reference_fail(probe, "identity_changed");
    }
    if (!operations->read_reference(operations->context, identity->player, state)) {
        return p1_reference_fail(probe, "reference_read");
    }
    probe->last_observed_count = state->count;
    probe->last_observed_marker = state->marker;
    ++probe->reference_samples;
    if (state->player_vtable != p1_reference_address(probe, P1_REFERENCE_PLAYER_VTABLE_VA) ||
        state->reference_vtable != p1_reference_address(probe, P1_REFERENCE_SUBOBJECT_VTABLE_VA) ||
        state->zero_target != p1_reference_address(probe, P1_REFERENCE_ZERO_TARGET_VA)) {
        return p1_reference_fail(probe, "vtable");
    }
    if (state->marker != 2) {
        return p1_reference_fail(probe, "marker");
    }
    return 1;
}

/* Exactly one attempt. Failure never implicitly retries an acquire or release. */
static inline int p1_reference_run(p1_reference_probe *probe) {
    if (!probe || !probe->requested || probe->attempted || probe->failed) {
        return 0;
    }
    probe->attempted = 1;
    p1_reference_identity identity = {0};
    p1_reference_state state = {0};
    if (!p1_reference_snapshot(probe, &identity, &state)) {
        return 0;
    }
    if (state.count < 2 || state.count >= INT32_MAX) {
        return p1_reference_fail(probe, "initial_count");
    }
    probe->initial_identity = identity;
    probe->initial_count = state.count;
    p1_reference_operations *operations = &probe->operations;
    probe->obligation = P1_REFERENCE_ACQUIRE_PENDING;
    p1_reference_call_result result = operations->acquire(
        operations->context, p1_reference_address(probe, P1_REFERENCE_ACQUIRE_VA), identity.player);
    if (result == P1_REFERENCE_NOT_CALLED) {
        probe->obligation = P1_REFERENCE_NO_OBLIGATION;
        return p1_reference_fail(probe, "acquire_not_called");
    }
    if (result != P1_REFERENCE_RETURNED) {
        return p1_reference_fail(probe, "acquire_uncertain");
    }
    if (!p1_reference_snapshot(probe, &identity, &state)) {
        return 0;
    }
    if (state.count != probe->initial_count + 1) {
        return p1_reference_fail(probe, "acquire_count");
    }
    probe->obligation = P1_REFERENCE_HOLD_VERIFIED;
    /* A separate fresh check immediately before release; no cached fallback. */
    if (!p1_reference_snapshot(probe, &identity, &state)) {
        return 0;
    }
    if (state.count != probe->initial_count + 1) {
        return p1_reference_fail(probe, "release_count");
    }
    probe->obligation = P1_REFERENCE_RELEASE_PENDING;
    result = operations->release(operations->context,
                                 p1_reference_address(probe, P1_REFERENCE_RELEASE_VA),
                                 identity.player + 0x38);
    if (result == P1_REFERENCE_NOT_CALLED) {
        probe->obligation = P1_REFERENCE_HOLD_VERIFIED;
        return p1_reference_fail(probe, "release_not_called");
    }
    if (result != P1_REFERENCE_RETURNED) {
        return p1_reference_fail(probe, "release_uncertain");
    }
    probe->obligation = P1_REFERENCE_READBACK_PENDING;
    if (!p1_reference_snapshot(probe, &identity, &state)) {
        return 0;
    }
    if (state.count != probe->initial_count) {
        return p1_reference_fail(probe, "final_count");
    }
    probe->obligation = P1_REFERENCE_NO_OBLIGATION;
    probe->passed = 1;
    probe->reason = "passed";
    return 1;
}

static inline p1_reference_probe_status p1_reference_status(const p1_reference_probe *probe) {
    p1_reference_probe_status status = {0};
    if (probe) {
        status.requested = probe->requested;
        status.attempted = probe->attempted;
        status.passed = probe->passed;
        status.failed = probe->failed;
        status.clean = probe->obligation == P1_REFERENCE_NO_OBLIGATION;
        status.obligation = probe->obligation;
        status.reason = probe->reason ? probe->reason : "off";
        status.initial_count = probe->initial_count;
        status.last_observed_count = probe->last_observed_count;
        status.last_observed_marker = probe->last_observed_marker;
        status.reference_samples = probe->reference_samples;
    }
    return status;
}
#endif
