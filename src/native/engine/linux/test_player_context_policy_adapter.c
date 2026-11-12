#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <setjmp.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "player_context_policy.h"

static struct { uint64_t issued; int active; unsigned slot; } stats = {5, 1, 2};
static int boot_phase = 3, wire_clients = 2, worker = 1, entered = 1, identity = 1, collect_good = 1;
static uint64_t stock_observer_worker_thread = 77;
static struct { uintptr_t owner; } sko_session = {0x1000};
static uintptr_t lifecycle_discard_ship = 0x1234, lifecycle_discard_controller = 0x5678;
static _Atomic unsigned player_physics_ready = 1;
static int (*lifecycle_context_policy_check)(const input_context_census_sample *, const input_context_census_entries *);
static int (*lifecycle_context_policy_owner)(void);
static int (*lifecycle_context_policy_suffix)(char *, size_t);
static input_context_census_sample fake_sample;
static input_context_census_entries fake_entries;
static char last_line[1200], failure[80];
static unsigned writes;
static jmp_buf escape;
static uint64_t GetCurrentThreadId(void) { return worker ? 77 : 78; }
static int player_recurring_physics_entered(void) { return entered; }
static void *gate_tls_self(void) { return &stats; }
static int gate_tls_worker(void *tls) { return tls && worker; }
static int player_physics_identity(void) { return identity; }
static int accept_ui(void) { return 1; }
static int stock_observer_worker_boundary(void) { return worker; }
static int player_seat_preflight_read(void *context, uintptr_t address, void *out, size_t length) {
    (void)context; (void)address; (void)out; (void)length; return 0;
}
static int fake_collect(const input_context_census_operations *ops, uintptr_t manager,
                        input_context_census_sample *sample, input_context_census_entries *entries) {
    assert(ops && manager == 0x1010);
    *sample = fake_sample; *entries = fake_entries; return collect_good;
}
#define input_context_census_collect_entries fake_collect
static void lockstep_sha256(const void *data, size_t length, unsigned char *digest) {
    const unsigned char *bytes = data; memset(digest, 0, 32);
    for (size_t i = 0; i < length; ++i) digest[i % 32] ^= bytes[i];
}
static void hex32(const unsigned char *data, char *out) {
    static const char hex[] = "0123456789abcdef";
    for (unsigned i = 0; i < 32; ++i) { out[2*i] = hex[data[i] >> 4]; out[2*i+1] = hex[data[i] & 15]; }
    out[64] = 0;
}
static unsigned diagnostic_count;
static void player_context_diagnostic(const char *stage, uint64_t tick, uint64_t thread,
        uintptr_t manager, int collected, unsigned complete, unsigned tree_valid,
        unsigned nodes, unsigned targets, const char *reason,
        const unsigned char (*payloads)[16], unsigned count) {
    (void)tick; (void)thread; (void)manager; (void)collected; (void)complete; (void)tree_valid;
    (void)nodes; (void)targets; (void)reason; assert(stage && payloads && count <= 16);
    ++diagnostic_count;
}
static void lifecycle_discard_write(const char *message) { ++writes; snprintf(last_line, sizeof last_line, "%s", message); }
static void lifecycle_discard_require(int ok, const char *reason) {
    if (!ok) { snprintf(failure, sizeof failure, "%s", reason); longjmp(escape, 1); }
}
#include "gate_player_context_policy_adapter.h"

static void setup(input_context_census_sample *sample, input_context_census_entries *entries, int after) {
    static const unsigned before[] = {0,1,2,3,4,5,7,8,9,10,12,15,16};
    static const unsigned later[] = {0,1,2,3,4,5,7,8,9,11,12,13,14,15,16};
    const unsigned *keys = after ? later : before;
    memset(sample, 0, sizeof *sample); memset(entries, 0, sizeof *entries);
    entries->count = after ? 15 : 13;
    sample->complete = sample->tree_valid = 1; sample->target_present = 7;
    sample->node_count = entries->count; sample->manager = 0x1010;
    for (unsigned i = 0; i < entries->count; ++i) {
        unsigned c = keys[i], requester = c == 13 || c == 14 ? 20 : 6;
        unsigned char *p = entries->payloads[i];
        p[0] = c; p[4] = requester; p[8] = requester == 20 ? 5 : 0; p[12] = requester != 20;
        p[13] = (unsigned char)(c ^ 0x5a);
        for (unsigned j = 0; j < 3; ++j)
            if (c == (unsigned[]){8,15,16}[j]) memcpy(sample->payloads[j], p, 16);
    }
}

int main(void) {
    input_context_census_sample before, after;
    static input_context_census_entries old, current;
    setup(&before, &old, 0); setup(&after, &current, 1);
    fake_sample = before; fake_entries = old;
    assert(!setenv("X4_DEV_PLAYER_PHYSICS", "single-frame-discard", 1));
    player_context_adapter_scope_begin(5); /* inactive single-frame mode */
    assert(!setenv("X4_DEV_PLAYER_PHYSICS", "network-frames-discard", 1));
    if (!setjmp(escape)) { player_context_adapter_scope_begin(5); assert(0); }
    assert(!strcmp(failure, "context_policy_scope_begin"));
    player_context_adapter_init();
    assert(writes == 1 && strstr(last_line, "event=init tick=5 boundary=ready epoch=1"));
    assert(lifecycle_context_policy_owner() && lifecycle_context_policy_check(&before, &old));
    char suffix[160]; assert(lifecycle_context_policy_suffix(suffix, sizeof suffix));
    assert(strstr(suffix, " context_policy_v=1 manager=1010 epoch=1 full_hash="));
    player_context_adapter_scope_begin(5);
    assert(lifecycle_context_policy_check(&before, &old));
    fake_entries.payloads[0][15] ^= 1;
    assert(!lifecycle_context_policy_check(&before, &fake_entries));
    assert(diagnostic_count == 2);
    fake_entries = old; player_context_adapter_scope_end(5);
    stats.issued = 6; entered = 0;
    assert(camera_context_anchor_adopt(&player_camera_anchor, 77, 0x1000, 0x1234, 0x5678,
                                        &before, &old, &after, &current));
    player_context_adapter_camera_rebase(&before, &old, &after, &current);
    assert(writes == 2 && strstr(last_line, "event=camera tick=6 boundary=camera epoch=2"));
    entered = 1; fake_sample = after; fake_entries = current;
    player_context_adapter_outside(6, PLAYER_CONTEXT_FRAME_BEGIN);
    fake_entries.payloads[15][0] = 59; fake_entries.payloads[15][4] = 8; fake_entries.payloads[15][12] = 1;
    fake_entries.count = fake_sample.node_count = 16;
    player_context_adapter_outside(6, PLAYER_CONTEXT_FRAME_END);
    assert(writes == 3 && strstr(last_line, "event=add tick=6 boundary=end epoch=3"));
    player_context_adapter_scope_begin(6); player_context_adapter_scope_end(6);
    fake_entries = current; fake_sample = after;
    player_context_adapter_outside(6, PLAYER_CONTEXT_FRAME_END);
    assert(writes == 4 && strstr(last_line, "event=remove tick=6 boundary=end epoch=4"));
    worker = 0;
    assert(!lifecycle_context_policy_owner());
    if (!setjmp(escape)) { player_context_adapter_scope_begin(6); assert(0); }
    assert(!strcmp(failure, "context_policy_scope_begin"));
    worker = 1; collect_good = 0; fake_sample.complete = 0;
    unsigned before_failure = diagnostic_count;
    if (!setjmp(escape)) { player_context_adapter_scope_begin(6); assert(0); }
    assert(!strcmp(failure, "context_policy_scope_begin") && diagnostic_count == before_failure + 2);
    puts("context policy adapter snapshots, rebase and scope checks PASS");
}
