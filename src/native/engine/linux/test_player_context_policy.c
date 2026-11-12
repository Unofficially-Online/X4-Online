#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "player_context_policy.h"

enum { OWNER = 0x1000, SHIP = 0x1234, CONTROLLER = 0x5678, THREAD = 77 };
static input_context_census_sample sample;
static input_context_census_entries base;
static input_context_census_entries with_help;
static player_context_policy policy;

static void make_sample(unsigned count) {
    static const unsigned keys[] = {0, 1, 2, 3, 4, 5, 7, 8, 9, 11, 12, 13, 14, 15, 16};
    memset(&sample, 0, sizeof sample);
    memset(&base, 0, sizeof base);
    base.count = count;
    sample.complete = sample.tree_valid = 1;
    sample.manager = OWNER + 0x10;
    sample.node_count = count;
    sample.target_present = 7;
    unsigned skip = 15 - count;
    for (unsigned index = 0; index < count; ++index) {
        unsigned key = keys[index + (index >= 10 ? skip : 0)];
        unsigned char *payload = base.payloads[index];
        payload[0] = (unsigned char)key;
        payload[4] = (key == 13 || key == 14) ? 20 : 6;
        payload[8] = (key == 13 || key == 14) ? 5 : 0;
        payload[12] = (key == 13 || key == 14) ? 0 : 1;
        payload[13] = (unsigned char)(key ^ 0x51);
        static const unsigned targets[] = {8, 15, 16};
        for (unsigned target = 0; target < 3; ++target) {
            if (key == targets[target]) {
                memcpy(sample.payloads[target], payload, 16);
            }
        }
    }
    with_help = base;
    with_help.count++;
    unsigned char *help = with_help.payloads[count];
    help[0] = 59;
    help[4] = 8;
    help[12] = 1;
}

static void reset(unsigned count) {
    memset(&policy, 0, sizeof policy);
    make_sample(count);
    assert(player_context_policy_init(&policy, count, THREAD, OWNER, SHIP, CONTROLLER, 1, &sample, &base));
}

static player_context_result observe(uint64_t tick, player_context_boundary boundary,
                                     const input_context_census_entries *entries) {
    sample.node_count = entries->count;
    return player_context_policy_observe_outside(&policy, tick, THREAD, OWNER, SHIP, CONTROLLER,
                                                  boundary, &sample, entries);
}

static void test_valid(unsigned count) {
    reset(count);
    assert(observe(5, PLAYER_CONTEXT_FRAME_BEGIN, &base) == PLAYER_CONTEXT_UNCHANGED);
    assert(policy.transitions == 0);
    assert(observe(6, PLAYER_CONTEXT_FRAME_END, &with_help) == PLAYER_CONTEXT_ADDED);
    assert(policy.transitions == 1);
    assert(observe(7, PLAYER_CONTEXT_FRAME_BEGIN, &with_help) == PLAYER_CONTEXT_UNCHANGED);
    assert(policy.transitions == 1);
    assert(player_context_policy_scope_begin(&policy, 7, THREAD, OWNER, SHIP, CONTROLLER, 1, &sample, &with_help));
    assert(player_context_policy_scope_end(&policy, 7, THREAD, OWNER, SHIP, CONTROLLER, 1, &sample, &with_help));
    assert(observe(8, PLAYER_CONTEXT_FRAME_BEGIN, &base) == PLAYER_CONTEXT_REMOVED);
    assert(policy.transitions == 2);
}

static void test_invalid_entries(unsigned count) {
    for (unsigned field = 0; field < 16; ++field) {
        reset(count);
        input_context_census_entries altered = with_help;
        altered.payloads[count][field] ^= 1;
        assert(observe(6, PLAYER_CONTEXT_FRAME_END, &altered) == PLAYER_CONTEXT_REFUSED);
        assert(policy.failed);
        assert(observe(7, PLAYER_CONTEXT_FRAME_BEGIN, &base) == PLAYER_CONTEXT_REFUSED);
    }
    reset(count);
    input_context_census_entries altered = base;
    altered.payloads[0][15] ^= 1;
    assert(observe(6, PLAYER_CONTEXT_FRAME_END, &altered) == PLAYER_CONTEXT_REFUSED);
    reset(count);
    altered = with_help;
    memcpy(altered.payloads[1], altered.payloads[0], 16);
    assert(observe(6, PLAYER_CONTEXT_FRAME_END, &altered) == PLAYER_CONTEXT_REFUSED);
    reset(count);
    altered = base;
    unsigned char tmp[16];
    memcpy(tmp, altered.payloads[0], 16);
    memcpy(altered.payloads[0], altered.payloads[1], 16);
    memcpy(altered.payloads[1], tmp, 16);
    assert(observe(6, PLAYER_CONTEXT_FRAME_END, &altered) == PLAYER_CONTEXT_REFUSED);
    reset(count);
    sample.tree_valid = 0;
    assert(observe(6, PLAYER_CONTEXT_FRAME_END, &base) == PLAYER_CONTEXT_REFUSED);
}

static void test_initial_admission(void) {
    make_sample(15);
    memset(&policy, 0, sizeof policy);
    assert(!player_context_policy_init(&policy, 14, THREAD, OWNER, SHIP, CONTROLLER, 1, &sample, &base));
    assert(policy.failed);
    memset(&policy, 0, sizeof policy);
    assert(!player_context_policy_init(&policy, 15, THREAD, OWNER, SHIP, CONTROLLER, 0, &sample, &base));
    memset(&policy, 0, sizeof policy);
    assert(!player_context_policy_init(&policy, 15, THREAD, UINTPTR_MAX, SHIP, CONTROLLER, 1, &sample, &base));
    memset(&policy, 0, sizeof policy);
    assert(!player_context_policy_init(&policy, 15, THREAD, OWNER, 0, CONTROLLER, 1, &sample, &base));
    memset(&policy, 0, sizeof policy);
    sample.manager++;
    assert(!player_context_policy_init(&policy, 15, THREAD, OWNER, SHIP, CONTROLLER, 1, &sample, &base));
}

static void test_boundaries_and_identity(void) {
    reset(15);
    assert(!player_context_policy_scope_end(&policy, 5, THREAD, OWNER, SHIP, CONTROLLER, 1, &sample, &base));
    reset(15);
    assert(observe(6, (player_context_boundary)0, &with_help) == PLAYER_CONTEXT_REFUSED);
    reset(15);
    assert(player_context_policy_scope_begin(&policy, 5, THREAD, OWNER, SHIP, CONTROLLER, 1, &sample, &base));
    assert(observe(5, PLAYER_CONTEXT_FRAME_END, &with_help) == PLAYER_CONTEXT_REFUSED);
    reset(15);
    assert(player_context_policy_scope_begin(&policy, 5, THREAD, OWNER, SHIP, CONTROLLER, 1, &sample, &base));
    sample.node_count = with_help.count;
    assert(!player_context_policy_scope_end(&policy, 5, THREAD, OWNER, SHIP, CONTROLLER, 1, &sample, &with_help));
    reset(15);
    assert(player_context_policy_scope_begin(&policy, 5, THREAD, OWNER, SHIP, CONTROLLER, 1, &sample, &base));
    assert(!player_context_policy_scope_begin(&policy, 5, THREAD, OWNER, SHIP, CONTROLLER, 1, &sample, &base));
    reset(15);
    assert(!player_context_policy_scope_begin(&policy, 5, THREAD + 1, OWNER, SHIP, CONTROLLER, 1, &sample, &base));
    reset(15);
    assert(!player_context_policy_scope_begin(&policy, 5, THREAD, OWNER + 1, SHIP, CONTROLLER, 1, &sample, &base));
    reset(15);
    assert(!player_context_policy_scope_begin(&policy, 5, THREAD, OWNER, SHIP + 1, CONTROLLER, 1, &sample, &base));
    reset(15);
    assert(!player_context_policy_scope_begin(&policy, 5, THREAD, OWNER, SHIP, CONTROLLER + 1, 1, &sample, &base));
    reset(15);
    sample.manager++;
    assert(observe(6, PLAYER_CONTEXT_FRAME_BEGIN, &base) == PLAYER_CONTEXT_REFUSED);
    reset(15);
    assert(!player_context_policy_scope_begin(&policy, 5, THREAD, OWNER, SHIP, CONTROLLER, 0, &sample, &base));
    reset(15);
    assert(observe(6, PLAYER_CONTEXT_FRAME_BEGIN, &base) == PLAYER_CONTEXT_UNCHANGED);
    assert(observe(5, PLAYER_CONTEXT_FRAME_END, &base) == PLAYER_CONTEXT_REFUSED);
}

int main(void) {
    (void)player_camera_anchor;
    test_valid(13);
    test_valid(15);
    test_invalid_entries(13);
    test_invalid_entries(15);
    test_initial_admission();
    test_boundaries_and_identity();
    puts("player context policy: ok");
    return 0;
}
