#include "player_control_banks.h"
#include <assert.h>
#include <stdio.h>

/* No engine memory/calls. Different values in every descriptor make omission of
 * a region observable; injected failures occur after prior successful writes. */
typedef struct {
    unsigned char live[PCB_VALUE_BYTES];
    unsigned char baseline[PCB_VALUE_BYTES];
    pcb_scope scope;
    pcb_slot first;
    pcb_slot second;
    uint64_t thread;
    int boundary;
    int guards;
    unsigned calls;
    unsigned entry_fail_at;
    unsigned entry_foreign_equal;
    unsigned restore_block;
    unsigned read_fail_region;
    unsigned read_fail_byte;
    unsigned read_fail_once;
    unsigned final_read_fail;
    unsigned foreign_on_restore;
    unsigned foreign_offset;
    unsigned unknown_applies;
    unsigned unknown_read_failure;
    unsigned restore_unknown_once;
    unsigned restore_read_fail_count;
    pcb_write_result failure_result;
    pcb_zone_result zone_result;
    unsigned zone_active;
    unsigned zone_pending;
    unsigned zone_restore_fail;
    unsigned zone_restores;
    unsigned canonical_holds;
    unsigned slot_holds;
    unsigned late_foreign;
} fixture;

static uint64_t fake_thread(void *context) {
    return ((fixture *)context)->thread;
}

static int fake_boundary(void *context) {
    return ((fixture *)context)->boundary;
}

static int fake_guards(void *context) {
    return ((fixture *)context)->guards;
}

static int fake_read(void *context, unsigned region_index, unsigned offset,
                     unsigned char *bytes, unsigned length) {
    fixture *state = context;
    const pcb_region *region = &pcb_regions[region_index];
    assert(offset + length <= region->length);
    if (state->restore_read_fail_count && state->scope.phase == PCB_RESTORING &&
        region_index == 1 && offset == 77) {
        --state->restore_read_fail_count;
        return 0;
    }
    if (state->read_fail_once && region_index == state->read_fail_region &&
        offset == state->read_fail_byte) {
        state->read_fail_once = 0;
        return 0;
    }
    if (state->final_read_fail && state->scope.phase == PCB_RESTORING &&
        !state->scope.zone_pending && length > 1) {
        state->final_read_fail = 0;
        return 0;
    }
    memcpy(bytes, state->live + region->offset + offset, length);
    return 1;
}

static pcb_write_result fake_replace(void *context, unsigned region_index,
                                     unsigned offset, unsigned char expected,
                                     unsigned char replacement) {
    fixture *state = context;
    unsigned flat_index = pcb_regions[region_index].offset + offset;
    ++state->calls;
    if (state->scope.phase == PCB_RESTORING) {
        if (state->restore_unknown_once && region_index == 1 && offset == 77) {
            state->restore_unknown_once = 0;
            assert(state->live[flat_index] == expected);
            state->live[flat_index] = replacement;
            state->restore_read_fail_count = 1;
            return PCB_UNKNOWN;
        }
        if (state->foreign_on_restore && flat_index == state->foreign_offset) {
            state->foreign_on_restore = 0;
            state->live[flat_index] = (unsigned char)(expected ^ 0x17);
            if (state->live[flat_index] == state->baseline[flat_index]) {
                state->live[flat_index] ^= 0x41;
            }
            return PCB_NOT_CHANGED;
        }
        if (state->restore_block && flat_index == state->restore_block - 1) {
            return PCB_NOT_CHANGED;
        }
    }
    if (state->scope.phase == PCB_ENTERING && state->entry_fail_at == state->calls) {
        if (state->entry_foreign_equal) {
            state->live[flat_index] = replacement;
        }
        if (state->failure_result == PCB_UNKNOWN && state->unknown_applies) {
            assert(state->live[flat_index] == expected);
            state->live[flat_index] = replacement;
        }
        if (state->unknown_read_failure) {
            state->read_fail_once = 1;
            state->read_fail_region = region_index;
            state->read_fail_byte = offset;
        }
        return state->failure_result;
    }
    if (state->live[flat_index] != expected) {
        return PCB_NOT_CHANGED;
    }
    state->live[flat_index] = replacement;
    return PCB_CHANGED;
}

static pcb_zone_result fake_zone_enter(void *context, const pcb_slot *slot) {
    fixture *state = context;
    assert(slot == &state->first || slot == &state->second);
    assert(state->canonical_holds == 1 && state->slot_holds == 1);
    if (state->zone_result != PCB_ZONE_INACTIVE) {
        state->zone_active = 1;
        state->zone_pending = 1;
    }
    return state->zone_result;
}

static int fake_zone_restore(void *context, const pcb_slot *slot) {
    fixture *state = context;
    assert(slot == &state->first || slot == &state->second);
    ++state->zone_restores;
    assert(state->zone_pending);
    /* The synthetic external owner retains both independent references. */
    assert(state->canonical_holds == 1 && state->slot_holds == 1);
    if (state->zone_restore_fail) {
        return 0;
    }
    state->zone_active = 0;
    state->zone_pending = 0;
    if (state->late_foreign) {
        state->live[0] ^= 0x37;
        state->late_foreign = 0;
    }
    return 1;
}

static void setup(fixture *state) {
    memset(state, 0, sizeof(*state));
    state->thread = 11;
    state->boundary = 1;
    state->guards = 1;
    state->zone_result = PCB_ZONE_ACTIVE;
    state->canonical_holds = 1;
    state->slot_holds = 1;
    for (unsigned byte_index = 0; byte_index < PCB_VALUE_BYTES; ++byte_index) {
        state->baseline[byte_index] = (unsigned char)(byte_index * 17 + 0x23);
        state->first.bytes[byte_index] = state->baseline[byte_index] ^ 0xff;
        state->second.bytes[byte_index] = state->baseline[byte_index] ^ 0x55;
    }
    memcpy(state->live, state->baseline, PCB_VALUE_BYTES);
    state->first.initialized = 1;
    state->second.initialized = 1;
    pcb_operations operations = {
        state, fake_thread, fake_boundary, fake_guards, fake_read, fake_replace,
        fake_zone_enter, fake_zone_restore,
    };
    assert(pcb_scope_initialize(&state->scope, &operations, 11));
}

static void assert_clean(const fixture *state) {
    assert(state->scope.phase == PCB_IDLE);
    assert(!state->scope.zone_pending && !state->zone_active && !state->zone_pending);
    assert(!memcmp(state->live, state->baseline, PCB_VALUE_BYTES));
    assert(state->canonical_holds == 1 && state->slot_holds == 1);
}

static void test_descriptors_and_initialization(void) {
    unsigned length = 0;
    for (unsigned region_index = 0; region_index < PCB_REGION_COUNT; ++region_index) {
        assert(pcb_regions[region_index].offset == length);
        length += pcb_regions[region_index].length;
        assert(pcb_regions[region_index].va != 0x3e4cdd8);
        assert(pcb_regions[region_index].va != 0x3ee9b40);
    }
    assert(length == PCB_VALUE_BYTES);
    length = 0;
    for (unsigned slice_index = 0;
         slice_index < sizeof(pcb_main_slices) / sizeof(pcb_main_slices[0]);
         ++slice_index) {
        assert(pcb_main_slices[slice_index].offset == length);
        length += pcb_main_slices[slice_index].length;
    }
    assert(length == PCB_MAIN_BYTES);
    assert(pcb_regions[1].length == 0x140);
    unsigned char canonical[PCB_VALUE_BYTES];
    memset(canonical, 0x73, sizeof(canonical));
    pcb_slot slot = {0};
    pcb_initial_policy policy = {0};
    policy.enabled = 1;
    policy.contexts = 1;
    for (unsigned key_index = 0; key_index < 4; ++key_index) {
        policy.neutral_device[0xc8 + key_index] = 1;
    }
    assert(pcb_slot_initialize(&slot, canonical, &policy));
    assert(!memcmp(slot.bytes + 0x278, canonical + 0x278, 0x140));
    assert(slot.bytes[0x30] == 0x73);             /* SIM preserved. */
    assert(slot.bytes[0x54] == 0x73);             /* Opaque DEVICE preserved. */
    assert(slot.bytes[0xc1] == 0x73);             /* LOCAL padding preserved. */
    assert(slot.bytes[0xc0] == 1 && slot.bytes[0x109] == 1);
    for (unsigned key_index = 0; key_index < 4; ++key_index) {
        assert(slot.bytes[0xc8 + key_index] == 0);
        assert(slot.bytes[0xc4 + key_index] == 0);
    }
    for (unsigned byte_index = 0; byte_index < 8; ++byte_index) {
        assert(slot.bytes[0x3c4 + byte_index] == 0);
    }
    assert(!pcb_slot_initialize(&slot, canonical, &policy));
    memset(&slot, 0, sizeof(slot));
    policy.neutral_device[0x30] = 1;              /* Cannot zero SIM via policy. */
    assert(!pcb_slot_initialize(&slot, canonical, &policy));
    assert(!slot.initialized);
    policy.neutral_device[0x30] = 0;
    policy.enabled = 2;
    assert(!pcb_slot_initialize(&slot, canonical, &policy));
}

static void test_independent_stores_and_full_restore(void) {
    fixture state;
    setup(&state);
    unsigned char second_initial[PCB_VALUE_BYTES];
    memcpy(second_initial, state.second.bytes, sizeof(second_initial));
    assert(pcb_enter(&state.scope, &state.first));
    assert(!memcmp(state.live, state.first.bytes, PCB_VALUE_BYTES));
    assert(!pcb_enter(&state.scope, &state.second));
    for (unsigned region_index = 0; region_index < PCB_REGION_COUNT; ++region_index) {
        unsigned last = pcb_regions[region_index].offset + pcb_regions[region_index].length - 1;
        state.live[last] ^= 0x62;
    }
    unsigned char first_updated[PCB_VALUE_BYTES];
    memcpy(first_updated, state.live, sizeof(first_updated));
    assert(pcb_exit(&state.scope));
    assert_clean(&state);
    assert(!memcmp(state.first.bytes, first_updated, PCB_VALUE_BYTES));
    assert(!memcmp(state.second.bytes, second_initial, PCB_VALUE_BYTES));
    assert(pcb_enter(&state.scope, &state.second));
    assert(!memcmp(state.live, second_initial, PCB_VALUE_BYTES));
    assert(pcb_exit(&state.scope));
    assert_clean(&state);
    assert(pcb_enter(&state.scope, &state.first));
    assert(!memcmp(state.live, first_updated, PCB_VALUE_BYTES));
    assert(pcb_exit(&state.scope));
    assert_clean(&state);
}

static void test_admission_refusals(void) {
    fixture state;
    setup(&state);
    state.thread = 12;
    assert(!pcb_enter(&state.scope, &state.first));
    assert(!state.calls && !state.zone_active);
    state.thread = 11;
    state.boundary = 0;
    assert(!pcb_enter(&state.scope, &state.first));
    state.boundary = 1;
    state.guards = 0;
    assert(!pcb_enter(&state.scope, &state.first));
    state.guards = 1;
    state.read_fail_region = 3;
    state.read_fail_once = 1;
    assert(!pcb_enter(&state.scope, &state.first));
    assert(!state.calls && !state.zone_active);
    assert_clean(&state);
    assert(pcb_enter(&state.scope, &state.first));
    state.thread = 12;
    assert(!pcb_exit(&state.scope));
    assert(state.scope.phase == PCB_ACTIVE && state.zone_active);
    state.thread = 11;
    state.guards = 0;
    assert(!pcb_exit(&state.scope));
    state.guards = 1;
    assert(pcb_exit(&state.scope));
    assert_clean(&state);
}

static void test_partial_entry_and_rollback_retry(void) {
    fixture state;
    setup(&state);
    state.entry_fail_at = 990;                    /* After writes across all five regions. */
    state.failure_result = PCB_NOT_CHANGED;
    state.restore_block = 13;                    /* Earlier write cannot restore. */
    assert(!pcb_enter(&state.scope, &state.first));
    assert(state.scope.phase == PCB_RESTORING);
    assert(state.scope.pending[12] == PCB_OWNED);
    assert(state.live[12] == state.first.bytes[12]);
    assert(state.live[11] == state.baseline[11]);
    assert(!pcb_enter(&state.scope, &state.second));
    state.thread = 99;
    assert(!pcb_restore(&state.scope));
    state.thread = 11;
    state.restore_block = 0;
    assert(pcb_restore(&state.scope));
    assert_clean(&state);
}

static void test_failed_entry_never_adopts_foreign_bytes(void) {
    fixture state;
    setup(&state);
    state.entry_fail_at = 700;
    state.failure_result = PCB_NOT_CHANGED;
    state.entry_foreign_equal = 1;
    assert(!pcb_enter(&state.scope, &state.first));
    assert(state.scope.phase == PCB_RESTORING);
    assert(state.scope.pending[699] == PCB_FOREIGN);
    assert(state.live[699] == state.first.bytes[699]);
    assert(!pcb_restore(&state.scope));
    assert(state.live[699] == state.first.bytes[699]);
    state.live[699] = state.baseline[699];
    assert(pcb_restore(&state.scope));
    assert(!strcmp(state.scope.failure_reason, "write_refused"));
    assert_clean(&state);
}

static void test_unknown_entry_effect(void) {
    for (unsigned applies = 0; applies <= 1; ++applies) {
        fixture state;
        setup(&state);
        state.entry_fail_at = 700;                /* Coefficient extension beyond prefix54. */
        state.failure_result = PCB_UNKNOWN;
        state.unknown_applies = applies;
        state.unknown_read_failure = 1;
        assert(!pcb_enter(&state.scope, &state.first));
        assert(state.scope.phase == PCB_RESTORING);
        assert(state.scope.pending[699] == PCB_UNCERTAIN);
        assert(pcb_restore(&state.scope));
        assert_clean(&state);
    }
}

static void test_unknown_restore_write_retains_obligation(void) {
    fixture state;
    setup(&state);
    assert(pcb_enter(&state.scope, &state.first));
    state.restore_unknown_once = 1;
    assert(!pcb_exit(&state.scope));
    unsigned flat_index = pcb_regions[1].offset + 77;
    assert(state.live[flat_index] == state.baseline[flat_index]);
    assert(state.scope.pending[flat_index] == PCB_UNCERTAIN);
    assert(state.scope.phase == PCB_RESTORING);
    assert(!pcb_enter(&state.scope, &state.second));
    assert(pcb_restore(&state.scope));
    assert_clean(&state);
}

static void test_foreign_value_preserved(void) {
    fixture state;
    setup(&state);
    assert(pcb_enter(&state.scope, &state.first));
    state.foreign_on_restore = 1;
    state.foreign_offset = 777;
    assert(!pcb_exit(&state.scope));
    unsigned char foreign = state.live[777];
    assert(foreign != state.baseline[777]);
    assert(state.scope.pending[777] == PCB_FOREIGN);
    assert(!pcb_restore(&state.scope));
    assert(state.live[777] == foreign);
    /* Even a later byte equal to our old slot value is not our ownership. */
    state.live[777] = state.scope.expected[777];
    assert(!pcb_restore(&state.scope));
    assert(state.live[777] == state.scope.expected[777]);
    state.live[777] = state.baseline[777];
    assert(pcb_restore(&state.scope));
    assert_clean(&state);
}

static void test_capture_read_failure_is_transactional(void) {
    fixture state;
    setup(&state);
    assert(pcb_enter(&state.scope, &state.first));
    unsigned char original_slot[PCB_VALUE_BYTES];
    memcpy(original_slot, state.first.bytes, sizeof(original_slot));
    state.live[0] ^= 0x33;
    state.live[995] ^= 0x44;
    state.read_fail_region = 4;
    state.read_fail_once = 1;
    assert(!pcb_exit(&state.scope));
    assert(state.scope.phase == PCB_ACTIVE && state.zone_active);
    assert(!memcmp(state.first.bytes, original_slot, sizeof(original_slot)));
    assert(!pcb_restore(&state.scope));            /* Must capture Update changes first. */
    assert(pcb_exit(&state.scope));
    assert(state.first.bytes[0] == (unsigned char)(original_slot[0] ^ 0x33));
    assert_clean(&state);
}

static void test_external_zone_obligation(void) {
    fixture state;
    setup(&state);
    state.zone_result = PCB_ZONE_UNCERTAIN;
    state.zone_restore_fail = 1;
    assert(!pcb_enter(&state.scope, &state.first));
    assert(!state.calls);
    assert(state.scope.zone_pending && state.scope.phase == PCB_RESTORING);
    assert(!pcb_restore(&state.scope));
    assert(state.scope.zone_pending && state.canonical_holds && state.slot_holds);
    state.zone_restore_fail = 0;
    assert(pcb_restore(&state.scope));
    assert_clean(&state);
    state.zone_result = PCB_ZONE_INACTIVE;
    unsigned restores = state.zone_restores;
    assert(!pcb_enter(&state.scope, &state.first));
    assert(state.zone_restores == restores);
    assert_clean(&state);
    state.zone_result = PCB_ZONE_ACTIVE;
    assert(pcb_enter(&state.scope, &state.first));
    state.zone_restore_fail = 1;
    assert(!pcb_exit(&state.scope));
    assert(!memcmp(state.live, state.baseline, PCB_VALUE_BYTES));
    assert(state.scope.zone_pending && state.scope.phase == PCB_RESTORING);
    state.zone_restore_fail = 0;
    assert(pcb_restore(&state.scope));
    assert_clean(&state);
}

static void test_final_verification_failure(void) {
    fixture state;
    setup(&state);
    assert(pcb_enter(&state.scope, &state.first));
    state.final_read_fail = 1;
    assert(!pcb_exit(&state.scope));
    assert(state.scope.phase == PCB_RESTORING);
    assert(!memcmp(state.live, state.baseline, PCB_VALUE_BYTES));
    assert(pcb_restore(&state.scope));
    assert_clean(&state);
    assert(pcb_enter(&state.scope, &state.first));
    state.late_foreign = 1;
    assert(!pcb_exit(&state.scope));
    assert(state.scope.pending[0] == PCB_FOREIGN);
    assert(!pcb_restore(&state.scope));
    state.live[0] = state.baseline[0];
    assert(pcb_restore(&state.scope));
    assert_clean(&state);
}

int main(void) {
    test_descriptors_and_initialization();
    test_independent_stores_and_full_restore();
    test_admission_refusals();
    test_partial_entry_and_rollback_retry();
    test_failed_entry_never_adopts_foreign_bytes();
    test_unknown_entry_effect();
    test_unknown_restore_write_retains_obligation();
    test_foreign_value_preserved();
    test_capture_read_failure_is_transactional();
    test_external_zone_obligation();
    test_final_verification_failure();
    puts("player control banks: 11 scenario groups passed");
    return 0;
}
