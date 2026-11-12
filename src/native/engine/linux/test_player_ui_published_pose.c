#include "player_ui_published_pose.h"
#include <assert.h>
#include <stdio.h>

struct fixture {
    _Alignas(16) unsigned char actor[PLAYER_UI_POSE_ACTOR_EXTENT];
    _Alignas(16) float original[16], scratch[16], delta[16];
    int owner_ok, fail_read, reads, fail_read_at, cas_result, cas_calls;
};

static int owner(void *context, uintptr_t actor, unsigned bank) {
    struct fixture *test = context;
    return test->owner_ok && actor == (uintptr_t)test->actor && bank <= 1;
}
static int read_memory(void *context, uintptr_t address, void *out, size_t size) {
    struct fixture *test = context;
    ++test->reads;
    if (test->fail_read || test->reads == test->fail_read_at) return 0;
    memcpy(out, (const void *)address, size);
    return 1;
}
static int exchange(void *context, uintptr_t address, uintptr_t expected, uintptr_t desired) {
    struct fixture *test = context;
    ++test->cas_calls;
    if (test->cas_result != 1) return test->cas_result;
    uintptr_t current;
    memcpy(&current, (void *)address, sizeof current);
    if (current != expected) return 0;
    memcpy((void *)address, &desired, sizeof desired);
    return 1;
}
static uintptr_t slot_address(struct fixture *test, unsigned bank) {
    return (uintptr_t)test->actor + 0xe0 + bank * 0x80 + 0x28;
}
static void set_slot(struct fixture *test, unsigned bank, uintptr_t value) {
    memcpy((void *)slot_address(test, bank), &value, sizeof value);
}
static uintptr_t get_slot(struct fixture *test, unsigned bank) {
    uintptr_t value;
    memcpy(&value, (void *)slot_address(test, bank), sizeof value);
    return value;
}
static void initialize(struct fixture *test, unsigned bank) {
    memset(test, 0, sizeof *test);
    test->owner_ok = test->cas_result = 1;
    test->original[0] = 2;
    test->original[3] = -0.0f;
    test->original[4] = test->original[9] = test->original[14] = 1;
    test->original[7] = 8; test->original[11] = 9; test->original[15] = 10;
    test->delta[4] = test->delta[9] = test->delta[14] = 1;
    set_slot(test, bank, (uintptr_t)test->original);
}
int main(void) {
    struct fixture test;
    player_ui_published_pose_lease lease;
    player_ui_published_pose_ops ops = {&test, owner, read_memory, exchange};
    for (unsigned bank = 0; bank <= 1; ++bank) {
        initialize(&test, bank); memset(&lease, 0, sizeof lease);
        float saved[16]; memcpy(saved, test.original, sizeof saved);
        assert(player_ui_published_pose_begin(&lease, &ops, (uintptr_t)test.actor, bank, test.scratch, test.delta));
        assert(lease.status == PLAYER_POSE_ACTIVE && get_slot(&test, bank) == (uintptr_t)test.scratch);
        assert(!memcmp(saved, test.original, sizeof saved) && !memcmp(saved, test.scratch, sizeof saved));
        assert(!player_ui_published_pose_begin(&lease, &ops, (uintptr_t)test.actor, bank, test.scratch, test.delta));
        assert(player_ui_published_pose_end(&lease));
        assert(lease.status == PLAYER_POSE_CLOSED && get_slot(&test, bank) == (uintptr_t)test.original);
        assert(test.cas_calls == 2 && !player_ui_published_pose_end(&lease));
    }
    initialize(&test, 0); memset(&lease, 0, sizeof lease);
    test.delta[0] = 3;
    assert(player_ui_published_pose_begin(&lease, &ops, (uintptr_t)test.actor, 0, test.scratch, test.delta));
    assert(test.scratch[0] == 5 && test.original[0] == 2);
    set_slot(&test, 0, (uintptr_t)test.delta);
    assert(!player_ui_published_pose_end(&lease) && lease.status == PLAYER_POSE_UNCERTAIN);
    assert(get_slot(&test, 0) == (uintptr_t)test.delta && test.cas_calls == 1);

    initialize(&test, 0); memset(&lease, 0, sizeof lease);
    assert(player_ui_published_pose_begin(&lease, &ops, (uintptr_t)test.actor, 0, test.scratch, test.delta));
    test.owner_ok = 0;
    assert(!player_ui_published_pose_end(&lease) && lease.status == PLAYER_POSE_UNCERTAIN);
    assert(test.cas_calls == 1 && get_slot(&test, 0) == (uintptr_t)test.scratch);

    initialize(&test, 0); memset(&lease, 0, sizeof lease);
    test.fail_read_at = 4; /* slot readback after successful installation */
    assert(!player_ui_published_pose_begin(&lease, &ops, (uintptr_t)test.actor, 0, test.scratch, test.delta));
    assert(lease.status == PLAYER_POSE_UNCERTAIN && test.cas_calls == 1);
    assert(get_slot(&test, 0) == (uintptr_t)test.scratch);
    assert(!player_ui_published_pose_end(&lease) && test.cas_calls == 1);

    initialize(&test, 0); memset(&lease, 0, sizeof lease);
    assert(player_ui_published_pose_begin(&lease, &ops, (uintptr_t)test.actor, 0, test.scratch, test.delta));
    test.cas_result = 0;
    assert(!player_ui_published_pose_end(&lease) && lease.status == PLAYER_POSE_UNCERTAIN);
    assert(get_slot(&test, 0) == (uintptr_t)test.scratch && test.cas_calls == 2);

    initialize(&test, 0); memset(&lease, 0, sizeof lease);
    test.cas_result = -1;
    assert(!player_ui_published_pose_begin(&lease, &ops, (uintptr_t)test.actor, 0, test.scratch, test.delta));
    assert(lease.status == PLAYER_POSE_UNCERTAIN && test.cas_calls == 1);
    initialize(&test, 0); memset(&lease, 0, sizeof lease);
    test.cas_result = 0;
    assert(!player_ui_published_pose_begin(&lease, &ops, (uintptr_t)test.actor, 0, test.scratch, test.delta));
    assert(lease.status == PLAYER_POSE_REFUSED && test.cas_calls == 1);
    initialize(&test, 0); memset(&lease, 0, sizeof lease);
    test.fail_read = 1;
    assert(!player_ui_published_pose_begin(&lease, &ops, (uintptr_t)test.actor, 0, test.scratch, test.delta));
    assert(lease.status == PLAYER_POSE_NEW && test.cas_calls == 0);
    initialize(&test, 0); memset(&lease, 0, sizeof lease);
    set_slot(&test, 0, 0);
    assert(!player_ui_published_pose_begin(&lease, &ops, (uintptr_t)test.actor, 0, test.scratch, test.delta));
    assert(lease.status == PLAYER_POSE_NEW && test.cas_calls == 0);
    initialize(&test, 0); memset(&lease, 0, sizeof lease);
    set_slot(&test, 0, (uintptr_t)test.original + 4);
    assert(!player_ui_published_pose_begin(&lease, &ops, (uintptr_t)test.actor, 0, test.scratch, test.delta));
    assert(test.cas_calls == 0);
    initialize(&test, 0); memset(&lease, 0, sizeof lease);
    test.original[4] = 2;
    assert(!player_ui_published_pose_begin(&lease, &ops, (uintptr_t)test.actor, 0, test.scratch, test.delta));
    assert(lease.status == PLAYER_POSE_REFUSED && test.cas_calls == 0);
    initialize(&test, 0); memset(&lease, 0, sizeof lease);
    set_slot(&test, 0, (uintptr_t)&lease);
    unsigned char lease_before[sizeof lease];
    memcpy(lease_before, &lease, sizeof lease);
    assert(!player_ui_published_pose_begin(&lease, &ops, (uintptr_t)test.actor, 0, test.scratch, test.delta));
    assert(!memcmp(lease_before, &lease, sizeof lease) && test.cas_calls == 0);
    initialize(&test, 0); memset(&lease, 0, sizeof lease);
    memcpy(test.actor + 0x300, test.original, sizeof test.original);
    set_slot(&test, 0, (uintptr_t)(test.actor + 0x300));
    assert(player_ui_published_pose_begin(&lease, &ops, (uintptr_t)test.actor, 0, test.scratch, test.delta));
    assert(player_ui_published_pose_end(&lease));
    assert(get_slot(&test, 0) == (uintptr_t)(test.actor + 0x300));
    initialize(&test, 0); memset(&lease, 0, sizeof lease);
    set_slot(&test, 0, (uintptr_t)(test.actor + 0x100));
    assert(!player_ui_published_pose_begin(&lease, &ops, (uintptr_t)test.actor, 0, test.scratch, test.delta));
    assert(test.cas_calls == 0);
    initialize(&test, 0); memset(&lease, 0, sizeof lease);
    unsigned char actor_before[sizeof test.actor];
    memcpy(actor_before, test.actor, sizeof actor_before);
    assert(!player_ui_published_pose_begin(&lease, &ops, (uintptr_t)test.actor, 0,
                                           (float *)(void *)(test.actor + 0x200), test.delta));
    assert(!memcmp(actor_before, test.actor, sizeof actor_before) && test.cas_calls == 0);
    player_ui_published_pose_lease *overlapping_lease = (void *)(test.actor + 0x200);
    assert(!player_ui_published_pose_begin(overlapping_lease, &ops, (uintptr_t)test.actor, 0,
                                           test.scratch, test.delta));
    assert(!memcmp(actor_before, test.actor, sizeof actor_before) && test.cas_calls == 0);
    initialize(&test, 0); memset(&lease, 0, sizeof lease);
    assert(!player_ui_published_pose_begin(&lease, &ops, (uintptr_t)test.actor, 0, test.original, test.delta));
    assert(test.cas_calls == 0);
    assert(!player_ui_published_pose_begin(&lease, &ops, (uintptr_t)test.actor, 0, test.delta, test.delta));
    assert(test.cas_calls == 0);
    puts("published pose transaction PASS");
}
