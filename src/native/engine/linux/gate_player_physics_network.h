#ifndef X4_GATE_PLAYER_PHYSICS_NETWORK_H
#define X4_GATE_PLAYER_PHYSICS_NETWORK_H

#include "lockstep_stock_record.h"
#include "stock_keyboard_replay.h"

/* Explicit disposable two-player experiment. The keyboard coordinator checks
 * both complete records before either slot callback. This adapter owns only
 * P2's input and one Update inside the existing admitted worker scope. P1's
 * Update remains owned by X4. No camera, seat transfer or general input support
 * is implied, and all native storage remains resident until process exit. */
static int keyboard_probe_independent_after_path_join(uint64_t generation);
static int keyboard_probe_independent_neutral(void);
static uint64_t player_network_last = 4;
static unsigned player_network_held;
static unsigned player_network_busy;

typedef struct {
    uint64_t generation;
    const stock_input_record *record;
} player_network_input;

#include "gate_player_throttle_discard.h"

static float player_network_read_keys(unsigned expected) {
    unsigned char keys[8], throttle_keys[2], strafe_ignored, throttle_ignored;
    float digital = atomic_load_explicit(&player_physics_ready, memory_order_acquire)
        ? player_physics_replay_read_input(keys) : lifecycle_input_read(keys);
    /* STATE 6/7 (horizontal) and 8/9 (vertical) occupy 0x3d8da72..75.
     * Their key block and suppression flag are distinct from steering.
     * Key bytes sit in the existing main bank; the suppression flag is a
     * read-only admission guard outside the copied bank. */
    lifecycle_discard_read((uintptr_t)LX(0x3d8da72), keys + 4, 4);
    lifecycle_discard_read((uintptr_t)LX(0x3e5b299), &strafe_ignored, 1);
    lifecycle_discard_require(!strafe_ignored, "network_strafe_suppressed");
    lifecycle_discard_read((uintptr_t)LX(0x3d8da7a), throttle_keys, sizeof throttle_keys);
    lifecycle_discard_read((uintptr_t)LX(0x3e5b298), &throttle_ignored, 1);
    lifecycle_discard_require(!throttle_ignored, "network_throttle_suppressed");
    unsigned expected_throttle = (expected >> 11) & 3u;
    if (player_throttle_enabled()) {
        lifecycle_discard_require(!expected_throttle && player_throttle_phase <= 2,
                                  "network_scripted_throttle_ownership");
        expected_throttle = player_throttle_phase == 1 ? 1u : 0u;
    }
    for (unsigned index = 0; index < 2; ++index) {
        lifecycle_discard_require(throttle_keys[index] == ((expected_throttle >> index) & 1u),
                                  "network_throttle_readback");
    }
    for (unsigned index = 0; index < 8; ++index) {
        lifecycle_discard_require(keys[index] == ((expected >> index) & 1u),
                                  "network_key_readback");
    }
    return digital;
}

/* Log the exact immutable wire record alongside measured key state. The
 * evaluator must compare this against both clients' relay/capture records;
 * an engine return alone is not evidence that the right input was applied. */
static void player_network_record(const player_network_input *input, unsigned before,
                                  unsigned after, float digital, const char *stage) {
    unsigned char bytes[STOCK_INPUT_MAX_BYTES];
    unsigned length = stock_input_encode(input->record, bytes, sizeof bytes);
    lifecycle_discard_require(length != 0, "network_record_encoding");
    static const char digits[] = "0123456789abcdef";
    char encoded[STOCK_INPUT_MAX_BYTES * 2 + 1];
    for (unsigned index = 0; index < length; ++index) {
        encoded[index * 2] = digits[bytes[index] >> 4];
        encoded[index * 2 + 1] = digits[bytes[index] & 15];
    }
    encoded[length * 2] = 0;
    char message[768];
    int count = snprintf(message, sizeof message,
        "X4LAN LIFECYCLE_DISCARD physics=network tick=%llu slot=2 stage=%s "
        "held_before=%u held_after=%u digital=%.9g record=%s\n",
        (unsigned long long)input->generation, stage, before, after, (double)digital, encoded);
    lifecycle_discard_require(count > 0 && (size_t)count < sizeof message, "network_record_format");
    lifecycle_discard_write(message);
}

static void player_network_update_input(void *context) {
    const player_network_input *input = context;
    lifecycle_discard_require(player_recurring_physics_entered() && lifecycle_input_pins() &&
        input->generation == stats.issued, "network_input_admission");
    int throttle_probe = player_throttle_enabled();
    if (throttle_probe && (input->generation == player_throttle_start_tick() ||
                           input->generation == player_throttle_start_tick() + 1)) {
        lifecycle_discard_require(input->record && !input->record->count,
                                  "throttle_script_admission");
    }
    unsigned before = player_network_held;
    float digital = player_network_read_keys(before);
    player_network_record(input, before, before, digital, "before");
    for (unsigned index = 0; index < input->record->count; ++index) {
        const stock_input_event *event = &input->record->events[index];
        if (event->id == 13 || event->id == 14) {
            player_throttle_require_pins();
        }
        unsigned bit = 1u << (event->id - 2);
        /* Keep the obligation before native entry. Any failure discards this
         * process, with no retry or locally fabricated release. */
        player_network_held = event->value ? player_network_held | bit : player_network_held & ~bit;
        ((void (*)(int, unsigned char))LX(0x166c6f0))((int)event->id, (unsigned char)event->value);
        (void)player_network_read_keys(player_network_held);
    }
    digital = player_network_read_keys(player_network_held);
    player_network_record(input, before, player_network_held, digital, "applied");
    if (throttle_probe) {
        player_throttle_apply(input->generation, input->record);
    }
    lifecycle_discard_update_once(3);
    if (throttle_probe) {
        player_throttle_after_update(input->generation);
    }
    digital = player_network_read_keys(player_network_held);
    player_network_record(input, before, player_network_held, digital, "updated");
}

static void player_physics_network_apply_p2(uint64_t generation, const stock_input_record *record) {
    /* The coordinator already validates both rows. Keep a second local check
     * before shifting key bits or entering the native P2 scope. */
    if (player_recurring_physics != 3 || !player_recurring_physics_entered() ||
        player_network_busy || generation != player_network_last + 1 || generation != stats.issued ||
        !record || record->flags || record->count > STOCK_INPUT_MAX_EVENTS ||
        record->capture16 != (uint16_t)(generation - 2)) {
        player_recurring_fatal("network_record_admission");
    }
    keyboard_replay_config policy = {.independent_players = 1,
                                     .scripted_throttle = (unsigned)player_throttle_enabled()};
    for (unsigned index = 0; index < record->count; ++index) {
        const stock_input_event *event = &record->events[index];
        if (event->kind != STOCK_INPUT_STATE ||
            !keyboard_replay_allowed_state(&policy, event->id) || event->value > 1)
            player_recurring_fatal("network_record_policy");
    }
    player_network_busy = 1;
    player_network_input input = {generation, record};
    if (generation == FWRR_FIRST_GENERATION) {
        player_physics_begin_with_input(1, player_network_update_input, &input);
    } else {
        player_physics_replay_begin_with_input(generation, player_network_update_input, &input);
    }
    player_network_last = generation;
    player_network_busy = 0;
}

static void player_physics_network_begin(uint64_t generation) {
    if (player_recurring_physics != 3 || !player_recurring_physics_entered())
        player_recurring_fatal("network_frame_admission");
    if (generation > FWRR_FIRST_GENERATION) {
        player_physics_frame_record(generation, "begin");
    }
    if (!keyboard_probe_independent_after_path_join(generation) || player_network_last != generation ||
        player_network_busy || !player_physics_identity()) {
        player_recurring_fatal("network_frame_replay");
    }
}

static void player_physics_network_finish(uint64_t generation, unsigned final) {
    lifecycle_discard_require(player_network_last == generation && !player_network_busy,
                              "network_tail_sequence");
    if (final) {
        lifecycle_discard_require(!player_throttle_enabled() || player_throttle_phase == 2,
                                  "throttle_script_incomplete");
        lifecycle_discard_require(!player_network_held && keyboard_probe_independent_neutral(),
                                  "network_final_keys_held");
    }
    player_physics_frames_finish(generation, final);
}
#endif
