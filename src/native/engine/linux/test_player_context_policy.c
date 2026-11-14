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

/* Exact payloads retained from selection-waits-first tick 122. */
static void decode_rows(input_context_census_entries *entries, const char *const *hex, unsigned count) {
    memset(entries, 0, sizeof *entries); entries->count = count;
    for (unsigned row = 0; row < count; ++row)
        for (unsigned byte = 0; byte < 16; ++byte) {
            unsigned value; assert(sscanf(hex[row] + byte * 2, "%2x", &value) == 1);
            entries->payloads[row][byte] = (unsigned char)value;
        }
}
static void test_selection_after_camera(void) {
    static const unsigned char extra[5][16] = {
        {4,0,0,0,8,0,0,0,0,0,0,0,1,0,0,0}, {44,0,0,0,13,0,0,0,0,0,0,0,1,0,0,0},
        {65,0,0,0,13,0,0,0,0,0,0,0,1,0,0,0}, {66,0,0,0,13,0,0,0,0,0,0,0,1,0,0,0},
        {67,0,0,0,13,0,0,0,0,0,0,0,1,0,0,0}};
    reset(15);
    input_context_census_entries selected = base;
    for (unsigned i = 0; i < 5; ++i) {
        unsigned pos = 0;
        while (pos < selected.count &&
               (input_context_census_u32(selected.payloads[pos]) < input_context_census_u32(extra[i]) ||
               (input_context_census_u32(selected.payloads[pos]) == input_context_census_u32(extra[i]) &&
                input_context_census_u32(selected.payloads[pos] + 4) < input_context_census_u32(extra[i] + 4)))) ++pos;
        memmove(selected.payloads[pos + 1], selected.payloads[pos], (selected.count - pos) * 16u);
        memcpy(selected.payloads[pos], extra[i], 16); ++selected.count;
    }
    assert(observe(6, PLAYER_CONTEXT_FRAME_END, &selected) == PLAYER_CONTEXT_ADDED);
    assert(policy.optional_mask == 2 && selected.count == 20);
    assert(player_context_policy_scope_begin(&policy, 7, THREAD, OWNER, SHIP, CONTROLLER, 1, &sample, &selected));
    assert(player_context_policy_scope_end(&policy, 7, THREAD, OWNER, SHIP, CONTROLLER, 1, &sample, &selected));
    assert(observe(8, PLAYER_CONTEXT_FRAME_BEGIN, &base) == PLAYER_CONTEXT_REMOVED);
}

static void test_selection_observed(void) {
    static const char *const base_hex[] = {"02000000040000000000000001000000", "03000000040000000000000001000000", "050000000a0000000000000001000000", "060000000600000000000000017f0000", "080000000600000000000000017f0000", "09000000060000000000000001000000", "0a000000060000000000000001000000", "0f0000000600000000000000017f0000", "100000000600000000000000017f0000", "2d0000000800000000000000017f0000", "2f0000000800000000000000017f0001", "46000000060000000000000001000000", "47000000060000000000000001000000"};
    static const char *const observed_hex[] = {"02000000040000000000000001000000", "03000000040000000000000001000000", "04000000080000000000000001000000", "050000000a0000000000000001000000", "060000000600000000000000017f0000", "080000000600000000000000017f0000", "09000000060000000000000001000000", "0a000000060000000000000001000000", "0f0000000600000000000000017f0000", "100000000600000000000000017f0000", "2c0000000d0000000000000001000000", "2d0000000800000000000000017f0000", "2f0000000800000000000000017f0001", "410000000d0000000000000001000000", "420000000d0000000000000001000000", "430000000d0000000000000001000000", "46000000060000000000000001000000", "47000000060000000000000001000000"};
    input_context_census_entries initial, selected;
    decode_rows(&initial, base_hex, 13); decode_rows(&selected, observed_hex, 18);
    memset(&sample, 0, sizeof sample); sample.complete = sample.tree_valid = 1;
    sample.manager = OWNER + 0x10; sample.node_count = 13; sample.target_present = 7;
    memcpy(sample.payloads[0], initial.payloads[4], 16);
    memcpy(sample.payloads[1], initial.payloads[7], 16);
    memcpy(sample.payloads[2], initial.payloads[8], 16);
    memset(&policy, 0, sizeof policy);
    assert(player_context_policy_init(&policy, 13, THREAD, OWNER, SHIP, CONTROLLER, 1, &sample, &initial));
    assert(observe(121, PLAYER_CONTEXT_FRAME_END, &initial) == PLAYER_CONTEXT_UNCHANGED);
    assert(observe(122, PLAYER_CONTEXT_FRAME_END, &selected) == PLAYER_CONTEXT_ADDED);
    assert(policy.optional_mask == 2);
    assert(player_context_policy_scope_begin(&policy, 123, THREAD, OWNER, SHIP, CONTROLLER, 1, &sample, &selected));
    assert(player_context_policy_scope_end(&policy, 123, THREAD, OWNER, SHIP, CONTROLLER, 1, &sample, &selected));
    assert(observe(124, PLAYER_CONTEXT_FRAME_BEGIN, &initial) == PLAYER_CONTEXT_REMOVED);
    for (unsigned row = 0; row < 18; ++row) for (unsigned byte = 0; byte < 16; ++byte) {
        input_context_census_entries bad = selected; bad.payloads[row][byte] ^= 1;
        memset(&policy, 0, sizeof policy); sample.node_count = 13;
        assert(player_context_policy_init(&policy, 13, THREAD, OWNER, SHIP, CONTROLLER, 1, &sample, &initial));
        assert(observe(122, PLAYER_CONTEXT_FRAME_END, &bad) == PLAYER_CONTEXT_REFUSED);
    }
    static const unsigned group_rows[] = {2, 10, 13, 14, 15};
    for (unsigned i = 0; i < sizeof group_rows / sizeof group_rows[0]; ++i) {
        input_context_census_entries incomplete = selected;
        unsigned row = group_rows[i];
        memmove(incomplete.payloads[row], incomplete.payloads[row + 1], (17 - row) * 16u);
        incomplete.count = 17;
        memset(&policy, 0, sizeof policy); sample.node_count = 13;
        assert(player_context_policy_init(&policy, 13, THREAD, OWNER, SHIP, CONTROLLER, 1, &sample, &initial));
        assert(observe(122, PLAYER_CONTEXT_FRAME_END, &incomplete) == PLAYER_CONTEXT_REFUSED);
    }
    input_context_census_entries bad = selected;
    memcpy(bad.payloads[2], bad.payloads[3], 16);
    memset(&policy, 0, sizeof policy); sample.node_count = 13;
    assert(player_context_policy_init(&policy, 13, THREAD, OWNER, SHIP, CONTROLLER, 1, &sample, &initial));
    assert(observe(122, PLAYER_CONTEXT_FRAME_END, &bad) == PLAYER_CONTEXT_REFUSED);
    memset(&policy, 0, sizeof policy); sample.node_count = 13;
    assert(player_context_policy_init(&policy, 13, THREAD, OWNER, SHIP, CONTROLLER, 1, &sample, &initial));
    sample.node_count = 18;
    assert(!player_context_policy_observe_outside(&policy, 122, THREAD+1, OWNER, SHIP, CONTROLLER,
                                                   PLAYER_CONTEXT_FRAME_END, &sample, &selected));
    memset(&policy, 0, sizeof policy); sample.node_count = 13;
    assert(player_context_policy_init(&policy, 13, THREAD, OWNER, SHIP, CONTROLLER, 1, &sample, &initial));
    assert(player_context_policy_scope_begin(&policy, 122, THREAD, OWNER, SHIP, CONTROLLER, 1, &sample, &initial));
    sample.node_count = 18;
    assert(!player_context_policy_scope_end(&policy, 122, THREAD, OWNER, SHIP, CONTROLLER, 1, &sample, &selected));
}

int main(void) {
    (void)player_camera_anchor;
    test_selection_observed();
    test_selection_after_camera();
    test_valid(13);
    test_valid(15);
    test_invalid_entries(13);
    test_invalid_entries(15);
    test_initial_admission();
    test_boundaries_and_identity();
    puts("player context policy: ok");
    return 0;
}
