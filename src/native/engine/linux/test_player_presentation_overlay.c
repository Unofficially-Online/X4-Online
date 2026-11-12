#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "player_presentation_overlay.h"

static _Alignas(16) unsigned char players[2][0x6ab0];
static _Alignas(16) unsigned char rooms[2][64];
static unsigned writes;
static unsigned reads;
static unsigned fail_write_at;
static unsigned fail_restore_at;
static unsigned fail_read_at;
static unsigned lose_owner_at;
static uint32_t permitted_bank;
static int owned;

static int owner(void *context, uintptr_t one, uintptr_t two, uintptr_t room, uint32_t bank) {
    (void)context;
    return owned && one == (uintptr_t)players[0] && two == (uintptr_t)players[1] &&
        room == (uintptr_t)rooms[1] && bank == permitted_bank;
}

static int read_memory(void *context, uintptr_t address, void *out, size_t length) {
    (void)context;
    ++reads;
    if (reads == fail_read_at) return 0;
    memcpy(out, (void *)address, length);
    return 1;
}

static int write_memory(void *context, uintptr_t address, const void *value, size_t length) {
    (void)context;
    ++writes;
    if (writes == lose_owner_at) owned = 0;
    if (writes == fail_write_at) {
        memcpy((void *)address, value, length / 2);
        return 0;
    }
    if (writes == fail_restore_at) return 0;
    memcpy((void *)address, value, length);
    return 1;
}

static const player_presentation_overlay_ops ops = {NULL, owner, read_memory, write_memory};

static void setup(uint32_t bank) {
    memset(players, 0, sizeof players);
    for (unsigned i = 0; i < 2; ++i) {
        uintptr_t room = (uintptr_t)rooms[i];
        memcpy(players[i] + PRESENTATION_PARENT_OFFSET, &room, 8);
        for (unsigned view = 0; view < 2; ++view) {
            memcpy(players[i] + PRESENTATION_VIEW_PARENT_OFFSET + view * PRESENTATION_VIEW_BANK_STRIDE,
                   &room, 8);
        }
        player_presentation_vector vector = {
            0x1000 + i * 0x100, 0x1008 + i * 0x100, 0x1010 + i * 0x100
        };
        memcpy(players[i] + PRESENTATION_VECTOR_OFFSET, &vector, sizeof vector);
        uint32_t position = i + 3;
        memcpy(players[i] + PRESENTATION_POSITION_OFFSET, &position, sizeof position);
    }
    writes = 0;
    reads = 0;
    fail_write_at = 0;
    fail_restore_at = 0;
    fail_read_at = 0;
    lose_owner_at = 0;
    permitted_bank = bank;
    owned = 1;
}

static int begin(player_presentation_overlay *overlay) {
    return player_presentation_overlay_begin(overlay, &ops, (uintptr_t)players[0],
        (uintptr_t)players[1], (uintptr_t)rooms[1], permitted_bank);
}

static int end(player_presentation_overlay *overlay) {
    return player_presentation_overlay_end(overlay, (uintptr_t)players[0],
        (uintptr_t)players[1], (uintptr_t)rooms[1], permitted_bank);
}

static void test_success(uint32_t bank) {
    unsigned char before[sizeof players[0]];
    player_presentation_overlay overlay = {0};
    setup(bank);
    memcpy(before, players[0], sizeof before);
    assert(begin(&overlay));
    assert(overlay.status == PRESENTATION_OVERLAY_ACTIVE && writes == 4);
    assert(!memcmp(players[0] + PRESENTATION_PARENT_OFFSET, players[1] + PRESENTATION_PARENT_OFFSET, 8));
    assert(!memcmp(players[0] + PRESENTATION_VECTOR_OFFSET, players[1] + PRESENTATION_VECTOR_OFFSET, 24));
    size_t selected = PRESENTATION_VIEW_PARENT_OFFSET + bank * PRESENTATION_VIEW_BANK_STRIDE;
    assert(!memcmp(players[0] + selected, players[1] + PRESENTATION_PARENT_OFFSET, 8));
    assert(end(&overlay));
    assert(overlay.status == PRESENTATION_OVERLAY_CLOSED && writes == 8);
    assert(!memcmp(before, players[0], sizeof before));
    assert(!end(&overlay) && writes == 8);
}

static void test_partial_writes(void) {
    for (unsigned failure = 1; failure <= 4; ++failure) {
        unsigned char before[sizeof players[0]];
        player_presentation_overlay overlay = {0};
        setup(0);
        memcpy(before, players[0], sizeof before);
        fail_write_at = failure;
        assert(!begin(&overlay));
        assert(overlay.status == PRESENTATION_OVERLAY_FAILED && overlay.touched == 0);
        assert(!memcmp(before, players[0], sizeof before));
        unsigned previous = writes;
        assert(!begin(&overlay) && !end(&overlay) && writes == previous);
    }
}

static void test_read_failures(void) {
    unsigned char before[sizeof players[0]];
    player_presentation_overlay overlay = {0};
    setup(0);
    fail_read_at = 2;
    assert(!begin(&overlay) && overlay.status == PRESENTATION_OVERLAY_FAILED && !writes);

    setup(0);
    memset(&overlay, 0, sizeof overlay);
    memcpy(before, players[0], sizeof before);
    fail_read_at = 8; /* Seven capture reads succeed; first write's verification fails. */
    assert(!begin(&overlay) && overlay.status == PRESENTATION_OVERLAY_FAILED);
    assert(writes == 2 && !memcmp(before, players[0], sizeof before));
}

static void test_restore_failures(void) {
    for (unsigned failing_restore = 1; failing_restore <= 4; ++failing_restore) {
        player_presentation_overlay overlay = {0};
        setup(0);
        assert(begin(&overlay));
        fail_restore_at = 4 + failing_restore;
        assert(!end(&overlay));
        assert(overlay.status == PRESENTATION_OVERLAY_UNCERTAIN);
        assert(overlay.touched == 5 - failing_restore);
        unsigned previous = writes;
        assert(!end(&overlay) && writes == previous);
    }
    player_presentation_overlay overlay = {0};
    setup(0);
    fail_write_at = 2;
    fail_restore_at = 3;
    assert(!begin(&overlay) && overlay.status == PRESENTATION_OVERLAY_UNCERTAIN);
    assert(overlay.touched == 2 && writes == 3);
    unsigned previous = writes;
    assert(!end(&overlay) && writes == previous);

    setup(0);
    memset(&overlay, 0, sizeof overlay);
    fail_write_at = 2;
    lose_owner_at = 3; /* Owner disappears during the first rollback write. */
    assert(!begin(&overlay) && overlay.status == PRESENTATION_OVERLAY_UNCERTAIN);
    assert(overlay.touched == 2 && writes == 3);
    previous = writes;
    assert(!end(&overlay) && writes == previous);
}

static void test_refusals(void) {
    player_presentation_overlay overlay = {0};
    setup(0);
    lose_owner_at = 2;
    assert(!begin(&overlay) && overlay.status == PRESENTATION_OVERLAY_UNCERTAIN && writes == 2);
    unsigned previous = writes;
    assert(!end(&overlay) && writes == previous);

    setup(0);
    memset(&overlay, 0, sizeof overlay);
    assert(begin(&overlay));
    previous = writes;
    assert(!player_presentation_overlay_end(&overlay, (uintptr_t)players[0],
        (uintptr_t)players[1], (uintptr_t)rooms[0], 0));
    assert(overlay.status == PRESENTATION_OVERLAY_UNCERTAIN && writes == previous);

    setup(0);
    memset(&overlay, 0, sizeof overlay);
    assert(begin(&overlay));
    previous = writes;
    assert(!begin(&overlay) && overlay.status == PRESENTATION_OVERLAY_UNCERTAIN && writes == previous);

    setup(0);
    memset(&overlay, 0, sizeof overlay);
    players[0][PRESENTATION_VIEW_PARENT_OFFSET] ^= 1;
    assert(!begin(&overlay) && overlay.status == PRESENTATION_OVERLAY_FAILED && !writes);

    setup(0);
    memset(&overlay, 0, sizeof overlay);
    players[1][PRESENTATION_VECTOR_OFFSET + 8] ^= 1;
    assert(!begin(&overlay) && overlay.status == PRESENTATION_OVERLAY_FAILED && !writes);

    setup(0);
    memset(&overlay, 0, sizeof overlay);
    assert(begin(&overlay));
    previous = writes;
    players[0][PRESENTATION_PARENT_OFFSET] ^= 1;
    assert(!end(&overlay) && overlay.status == PRESENTATION_OVERLAY_UNCERTAIN && writes == previous);

    setup(0);
    memset(&overlay, 0, sizeof overlay);
    assert(!player_presentation_overlay_begin(&overlay, &ops, (uintptr_t)players[0],
        (uintptr_t)players[1], (uintptr_t)rooms[1], 2));
    assert(overlay.status == PRESENTATION_OVERLAY_FAILED && !writes);

    setup(0);
    memset(&overlay, 0, sizeof overlay);
    assert(!player_presentation_overlay_begin(&overlay, NULL, (uintptr_t)players[0],
        (uintptr_t)players[1], (uintptr_t)rooms[1], 0));
    assert(overlay.status == PRESENTATION_OVERLAY_FAILED && !begin(&overlay) && !writes);

    setup(0);
    memset(&overlay, 0, sizeof overlay);
    assert(begin(&overlay));
    previous = writes;
    owned = 0;
    assert(!end(&overlay) && overlay.status == PRESENTATION_OVERLAY_UNCERTAIN && writes == previous);
}

int main(void) {
    test_success(0);
    test_success(1);
    test_partial_writes();
    test_read_failures();
    test_restore_failures();
    test_refusals();
    puts("presentation overlay transactions and refusal PASS");
}
