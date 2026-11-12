/* Standalone offline test. Never loads X4 or calls a native game function.
 * Golden codec/script/staging cases retain the Windows expectations in
 * test_lockstep_pilot_inputs.h at 30bc0f2. That test and the pre-extraction
 * Windows pilot-input file are byte-identical to 161da3f. The later stock path
 * is outside this receiver and its golden cases. The shared helpers are the
 * actual functions included by Windows and this Linux receiver. */
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <fenv.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdlib.h>
#include "gate_pilot_receiver.h"

static const char *manifest = "0000000000176a13,0000000000176a74";

static void encode_record(uint64_t capture, int16_t z, unsigned char bytes[PILOT_RECORD_BYTES]) {
    pilot_record record;
    memset(&record, 0, sizeof(record));
    record.mode = PILOT_MODE_ASSIST;
    record.capture16 = (uint16_t)capture;
    record.q[2] = z;
    record.q[6] = z;
    pilot_encode(&record, bytes);
}

static void configure_wire(linux_pilot_receiver *receiver, uint64_t first, uint64_t last) {
    assert(linux_pilot_receiver_init(receiver));
    assert(linux_pilot_receiver_configure(receiver, manifest, PILOT_SOURCE_WIRE, 3, first, last, 0, 1) ==
           LINUX_PILOT_RECEIVER_CONFIGURED);
}

static void test_windows_golden_codec(void) {
    pilot_record record;
    memset(&record, 0, sizeof(record));
    record.mode = PILOT_MODE_ASSIST | PILOT_MODE_BOOST;
    record.buttons = 5;
    record.capture16 = 0xbeef;
    record.q[0] = 32767;
    record.q[1] = -32767;
    record.q[2] = pilot_quantize(0.5f);
    record.q[3] = pilot_quantize(-2.0f);
    record.q[6] = 1;
    record.target = UINT64_C(0x0123456789abcdef);
    const char *golden = "0103efbeff7f0180004001800000000001000500efcdab8967452301";
    unsigned char bytes[PILOT_RECORD_BYTES];
    unsigned char expected[PILOT_RECORD_BYTES];
    char hex[2 * PILOT_RECORD_BYTES + 1];
    assert(pilot_unhex(golden, strlen(golden), expected));
    pilot_encode(&record, bytes);
    assert(!memcmp(bytes, expected, sizeof(bytes)));
    pilot_hex(bytes, sizeof(bytes), hex);
    assert(!strcmp(hex, golden));
    pilot_record decoded;
    assert(pilot_decode(expected, &decoded));
    assert(decoded.mode == record.mode && decoded.buttons == 5 && decoded.capture16 == 0xbeef);
    assert(decoded.target == record.target && !memcmp(decoded.q, record.q, sizeof(record.q)));
    assert(pilot_axis(32767) == 1.0f && pilot_axis(-32767) == -1.0f && pilot_axis(0) == 0.0f);
    assert(pilot_quantize(3.0f) == 32767 && pilot_quantize(-3.0f) == -32767);

    /* Every wire field is retained, including known features with no Linux
     * actuator in this slice. This second vector includes all seven Q15 lanes. */
    const char *all_fields = "0117cdab018000c0ffff000001000040ff7f0a008877665544332211";
    assert(pilot_unhex(all_fields, strlen(all_fields), bytes) && pilot_decode(bytes, &decoded));
    const int16_t q[7] = {-32767, -16384, -1, 0, 1, 16384, 32767};
    assert(decoded.mode == 0x17 && decoded.buttons == 10 && decoded.capture16 == 0xabcd);
    assert(decoded.target == UINT64_C(0x1122334455667788) && !memcmp(decoded.q, q, sizeof(q)));
    pilot_encode(&decoded, expected);
    assert(!memcmp(bytes, expected, sizeof(bytes)));

    assert(!pilot_unhex(golden, 55, bytes) && !pilot_unhex(NULL, 56, bytes));
    char invalid_hex[57];
    memcpy(invalid_hex, golden, sizeof(invalid_hex));
    invalid_hex[0] = 'A';
    assert(!pilot_unhex(invalid_hex, 56, bytes));
    const unsigned positions[] = {0, 1, 18, 19};
    const unsigned char invalid[] = {2, 0x20, 0x10, 1};
    for (unsigned i = 0; i < sizeof(positions) / sizeof(positions[0]); i++) {
        assert(pilot_unhex(golden, 56, bytes));
        bytes[positions[i]] = invalid[i];
        assert(!pilot_decode(bytes, &decoded));
    }
    for (unsigned axis = 0; axis < 7; axis++) {
        assert(pilot_unhex(golden, 56, bytes));
        bytes[4 + axis * 2] = 0;
        bytes[5 + axis * 2] = 0x80;
        assert(!pilot_decode(bytes, &decoded));
    }
    assert(pilot_unhex(golden, 56, bytes));
    bytes[1] |= PILOT_MODE_TARGET_UNMAPPED;
    assert(!pilot_decode(bytes, &decoded));
    memset(bytes + 20, 0, 8);
    assert(pilot_decode(bytes, &decoded) && decoded.mode == (3 | PILOT_MODE_TARGET_UNMAPPED));

    /* Extraction preserves the Windows lrintf contract rather than silently
     * forcing another floating-point environment. */
    int saved_rounding = fegetround();
    assert(!fesetround(FE_TONEAREST));
    assert(pilot_quantize(0.5f) == 16384 && pilot_quantize(-0.5f) == -16384);
    assert(!fesetround(FE_DOWNWARD));
    assert(pilot_quantize(0.5f) == 16383 && pilot_quantize(-0.5f) == -16384);
    assert(!fesetround(saved_rounding));
    puts("pilot receiver: exact Windows golden bytes, seven Q15 lanes, metadata and strict decode PASS");
}

static void test_config_and_script(void) {
    uint64_t saved[PILOT_SLOTS];
    unsigned count = 0;
    assert(pilot_parse_manifest(manifest, saved, &count) && count == 2 && saved[1] == 0x176a74);
    const char *bad[] = {NULL, "", "0000000000000000", "0000000000176A13", "000000000176a13",
                        "0000000000176a13,", "0000000000176a13,0000000000176a13"};
    for (unsigned i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) assert(!pilot_parse_manifest(bad[i], saved, &count));
    char slots[(PILOT_SLOTS + 1) * 17 + 1];
    size_t used = 0;
    for (unsigned i = 1; i <= PILOT_SLOTS + 1; i++)
        used += (size_t)snprintf(slots + used, sizeof(slots) - used, "%s%016x", i > 1 ? "," : "", i);
    assert(!pilot_parse_manifest(slots, saved, &count));
    slots[PILOT_SLOTS * 17 - 1] = 0;
    assert(pilot_parse_manifest(slots, saved, &count) && count == PILOT_SLOTS && saved[PILOT_SLOTS - 1] == PILOT_SLOTS);
    assert(pilot_config_valid(2, PILOT_SOURCE_WIRE, 3, 10, 100, 0));
    assert(pilot_config_valid(PILOT_SLOTS, PILOT_SOURCE_WIRE, 30, 31, PILOT_TICK_MAX, 1));
    assert(!pilot_config_valid(0, PILOT_SOURCE_WIRE, 3, 10, 100, 0));
    assert(!pilot_config_valid(PILOT_SLOTS + 1, PILOT_SOURCE_WIRE, 3, 10, 100, 0));
    assert(!pilot_config_valid(1, PILOT_SOURCE_WIRE, 1, 10, 100, 0));
    assert(!pilot_config_valid(1, PILOT_SOURCE_WIRE, 31, 32, 100, 0));
    assert(!pilot_config_valid(1, PILOT_SOURCE_WIRE, 5, 5, 100, 0));
    assert(!pilot_config_valid(1, PILOT_SOURCE_SCRIPT, 1, 10, 100, 0));
    assert(!pilot_config_valid(1, PILOT_SOURCE_SCRIPT, 0, 4, 100, 0));
    assert(!pilot_config_valid(1, PILOT_SOURCE_SCRIPT, 0, 10, 9, 0));
    assert(!pilot_config_valid(1, PILOT_SOURCE_SCRIPT, 0, 10, PILOT_TICK_MAX + 1u, 0));
    assert(!pilot_config_valid(1, PILOT_SOURCE_WIRE, 3, 10, 100, 2));
    assert(pilot_delay_covers(PILOT_SOURCE_WIRE, 3, 1) && !pilot_delay_covers(PILOT_SOURCE_WIRE, 3, 2));

    linux_pilot_receiver receiver;
    assert(linux_pilot_receiver_init(&receiver));
    assert(linux_pilot_receiver_configure(&receiver, manifest, PILOT_SOURCE_WIRE, 3, 10, 100, 2, 1) == LINUX_PILOT_RECEIVER_INVALID_CONFIG);
    assert(linux_pilot_receiver_configure(&receiver, manifest, PILOT_SOURCE_WIRE, 3, 10, 100, UINT_MAX, 1) == LINUX_PILOT_RECEIVER_INVALID_CONFIG);
    assert(linux_pilot_receiver_configure(&receiver, manifest, PILOT_SOURCE_WIRE, 3, 10, 100, 0, 0) == LINUX_PILOT_RECEIVER_INVALID_CONFIG);
    assert(linux_pilot_receiver_configure(&receiver, manifest, PILOT_SOURCE_SCRIPT, 0, 30, 630, 0, 1) == LINUX_PILOT_RECEIVER_CONFIGURED);
    const uint64_t ticks[] = {29, 30, 150, 270, 390, 510, 630, 631};
    const int16_t expected[8][3] = {{0, 0, 0}, {32767, 0, 0}, {32767, -16384, 0},
                                  {16384, 0, -16384}, {0, 0, 0}, {0, 0, 0}, {32767, 0, 0}, {0, 0, 0}};
    for (unsigned i = 0; i < sizeof(ticks) / sizeof(ticks[0]); i++) {
        assert(linux_pilot_receiver_publish(&receiver, ticks[i]) == LINUX_PILOT_RECEIVER_FRAME_READY);
        linux_pilot_receiver_frame frame;
        assert(linux_pilot_receiver_copy_frame(&receiver, ticks[i], &frame) == LINUX_PILOT_RECEIVER_FRAME_READY);
        assert(!frame.steering_active && frame.count == 2);
        if (ticks[i] < 30 || ticks[i] > 630) assert(!frame.in_window && !frame.present);
        else {
            assert(frame.in_window && frame.present == 3);
            assert(frame.records[1].q[2] == expected[i][0] && frame.records[1].q[4] == expected[i][1]);
            assert(frame.records[1].q[3] == expected[i][2]);
            if (ticks[i] == 390) assert(frame.records[1].q[0] == -32767 && frame.records[1].q[5] == -16384);
        }
    }
    unsigned char bytes[PILOT_RECORD_BYTES];
    encode_record(630, 0, bytes);
    assert(linux_pilot_receiver_stage(&receiver, 1, bytes, sizeof(bytes), 630) == LINUX_PILOT_RECEIVER_WRONG_SOURCE);
    assert(linux_pilot_receiver_destroy(&receiver));
    puts("pilot receiver: Windows manifest/window/delay bounds and scripted plans PASS");
}

static void test_staging_publication(void) {
    linux_pilot_receiver receiver;
    configure_wire(&receiver, 10, 12);
    unsigned char bytes[PILOT_RECORD_BYTES];
    unsigned char changed[PILOT_RECORD_BYTES];
    encode_record(5, 32767, bytes);
    assert(linux_pilot_receiver_stage(&receiver, 1, bytes, sizeof(bytes), 5) == LINUX_PILOT_RECEIVER_STAGED);
    assert(linux_pilot_receiver_stage(&receiver, 1, bytes, sizeof(bytes), 5) == LINUX_PILOT_RECEIVER_DUPLICATE);
    encode_record(5, 0, changed);
    assert(linux_pilot_receiver_stage(&receiver, 1, changed, sizeof(changed), 5) == LINUX_PILOT_RECEIVER_CONFLICT);
    assert(linux_pilot_receiver_stage(&receiver, 0, bytes, sizeof(bytes), 5) == LINUX_PILOT_RECEIVER_INVALID_SLOT);
    assert(linux_pilot_receiver_stage(&receiver, 3, bytes, sizeof(bytes), 5) == LINUX_PILOT_RECEIVER_INVALID_SLOT);
    assert(linux_pilot_receiver_stage(&receiver, 1, bytes, sizeof(bytes) - 1, 5) == LINUX_PILOT_RECEIVER_INVALID_RECORD);
    assert(linux_pilot_receiver_stage(&receiver, 1, NULL, sizeof(bytes), 5) == LINUX_PILOT_RECEIVER_INVALID_RECORD);
    assert(linux_pilot_receiver_configure(&receiver, manifest, PILOT_SOURCE_WIRE, 3, 10, 12, 0, 1) == LINUX_PILOT_RECEIVER_CONFIGURED);
    assert(receiver.staged == 1); /* Identical reload did not reset staged state. */
    assert(linux_pilot_receiver_configure(&receiver, manifest, PILOT_SOURCE_WIRE, 4, 10, 12, 0, 1) == LINUX_PILOT_RECEIVER_INVALID_CONFIG);
    assert(linux_pilot_receiver_publish(&receiver, 7) == LINUX_PILOT_RECEIVER_FRAME_READY);
    assert(linux_pilot_receiver_publish(&receiver, 8) == LINUX_PILOT_RECEIVER_FRAME_READY);
    assert(!receiver.frame.present && !receiver.frame.in_window); /* No synthetic input outside window. */
    assert(linux_pilot_receiver_publish(&receiver, 9) == LINUX_PILOT_RECEIVER_FRAME_READY);

    encode_record(7, 32767, bytes);
    assert(linux_pilot_receiver_stage(&receiver, 1, bytes, sizeof(bytes), 9) == LINUX_PILOT_RECEIVER_STAGED);
    assert(linux_pilot_receiver_publish(&receiver, 10) == LINUX_PILOT_RECEIVER_MISSING_RECORD);
    assert(receiver.frame.tick == 9 && receiver.frame.present == 0);
    linux_pilot_receiver_frame untouched;
    memset(&untouched, 0xa5, sizeof(untouched));
    linux_pilot_receiver_frame sentinel = untouched;
    assert(linux_pilot_receiver_copy_frame(&receiver, 10, &untouched) == LINUX_PILOT_RECEIVER_NO_FRAME);
    assert(!memcmp(&untouched, &sentinel, sizeof(untouched)));
    encode_record(7, -32767, changed);
    assert(linux_pilot_receiver_stage(&receiver, 2, changed, sizeof(changed), 9) == LINUX_PILOT_RECEIVER_STAGED);
    assert(linux_pilot_receiver_publish(&receiver, 10) == LINUX_PILOT_RECEIVER_FRAME_READY);
    linux_pilot_receiver_frame retained;
    assert(linux_pilot_receiver_copy_frame(&receiver, 10, &retained) == LINUX_PILOT_RECEIVER_FRAME_READY);
    assert(retained.present == 3 && retained.records[0].q[2] == 32767 && retained.records[1].q[2] == -32767);
    assert(!retained.steering_active && !memcmp(retained.bytes[0], bytes, sizeof(bytes)));
    assert(linux_pilot_receiver_apply(&receiver) == LINUX_PILOT_RECEIVER_APPLY_NOT_IMPLEMENTED);

    /* All known metadata is retained, including buttons/modes/target. Staging
     * it successfully must never be represented as an implemented actuator. */
    assert(pilot_unhex("01170800018000c0ffff000001000040ff7f0a008877665544332211", 56, bytes));
    assert(linux_pilot_receiver_stage(&receiver, 1, bytes, sizeof(bytes), 10) == LINUX_PILOT_RECEIVER_STAGED);
    assert(linux_pilot_receiver_stage(&receiver, 2, bytes, sizeof(bytes), 10) == LINUX_PILOT_RECEIVER_STAGED);
    assert(linux_pilot_receiver_publish(&receiver, 11) == LINUX_PILOT_RECEIVER_FRAME_READY);
    linux_pilot_receiver_frame latest;
    assert(linux_pilot_receiver_copy_frame(&receiver, 11, &latest) == LINUX_PILOT_RECEIVER_FRAME_READY);
    assert(latest.records[0].mode == 0x17 && latest.records[0].buttons == 10);
    assert(latest.records[0].target == UINT64_C(0x1122334455667788) && !memcmp(latest.bytes[0], bytes, sizeof(bytes)));
    assert(retained.tick == 10 && retained.records[1].q[2] == -32767); /* Independent immutable copy. */
    uint64_t publications = receiver.publications;
    assert(linux_pilot_receiver_publish(&receiver, 11) == LINUX_PILOT_RECEIVER_FRAME_READY && receiver.publications == publications);
    assert(linux_pilot_receiver_stage(&receiver, 1, bytes, sizeof(bytes), 9) == LINUX_PILOT_RECEIVER_INVALID_TICK);
    assert(linux_pilot_receiver_publish(&receiver, 10) == LINUX_PILOT_RECEIVER_INVALID_TICK);
    assert(linux_pilot_receiver_publish(&receiver, 13) == LINUX_PILOT_RECEIVER_FRAME_READY && !receiver.frame.present);
    linux_pilot_receiver_stop(&receiver);
    assert(linux_pilot_receiver_stage(&receiver, 1, bytes, sizeof(bytes), 13) == LINUX_PILOT_RECEIVER_STOPPED);
    assert(linux_pilot_receiver_copy_frame(&receiver, 13, &latest) == LINUX_PILOT_RECEIVER_STOPPED);
    assert(linux_pilot_receiver_configure(&receiver, manifest, PILOT_SOURCE_WIRE, 3, 10, 12, 0, 1) == LINUX_PILOT_RECEIVER_STOPPED);
    assert(linux_pilot_receiver_apply(&receiver) == LINUX_PILOT_RECEIVER_APPLY_NOT_IMPLEMENTED);
    char status[768];
    assert(linux_pilot_receiver_status(&receiver, status, sizeof(status)));
    assert(strstr(status, "steering_active=0 apply=not_implemented") && strstr(status, "stopped=1"));
    assert(retained.tick == 10 && retained.records[1].q[2] == -32767);
    assert(linux_pilot_receiver_destroy(&receiver));
    puts("pilot receiver: duplicate/conflict/missing, transactional immutable frames, full metadata and inactive apply PASS");
}

static void test_wrap_echo_and_corruption(void) {
    linux_pilot_receiver receiver;
    configure_wire(&receiver, 65538, 65542);
    unsigned char bytes[PILOT_RECORD_BYTES];
    unsigned char changed[PILOT_RECORD_BYTES];
    encode_record(65535, 100, bytes);
    assert(linux_pilot_receiver_remember_local(&receiver, 65535, bytes, sizeof(bytes)) == LINUX_PILOT_RECEIVER_LOCAL_REMEMBERED);
    assert(linux_pilot_receiver_stage(&receiver, 1, bytes, sizeof(bytes), 65536) == LINUX_PILOT_RECEIVER_STAGED);
    assert(linux_pilot_receiver_stage(&receiver, 2, bytes, sizeof(bytes), 65536) == LINUX_PILOT_RECEIVER_STAGED);
    encode_record(65567, 100, changed);
    assert(linux_pilot_receiver_remember_local(&receiver, 65567, changed, sizeof(changed)) == LINUX_PILOT_RECEIVER_LOCAL_RING_FULL);
    assert(linux_pilot_receiver_publish(&receiver, 65538) == LINUX_PILOT_RECEIVER_FRAME_READY);
    assert(receiver.frame.records[0].capture16 == 65535);
    assert(linux_pilot_receiver_remember_local(&receiver, 65567, changed, sizeof(changed)) == LINUX_PILOT_RECEIVER_LOCAL_REMEMBERED);
    encode_record(65536, 123, bytes);
    /* Current frame is 65538; the unwrapped record is due at 65539. */
    assert(linux_pilot_receiver_stage(&receiver, 1, bytes, sizeof(bytes), 65538) == LINUX_PILOT_RECEIVER_STAGED);
    assert(linux_pilot_receiver_stage(&receiver, 2, bytes, sizeof(bytes), 65538) == LINUX_PILOT_RECEIVER_STAGED);
    assert(linux_pilot_receiver_publish(&receiver, 65539) == LINUX_PILOT_RECEIVER_FRAME_READY);
    assert(receiver.frame.records[0].capture16 == 0 && receiver.frame.records[0].q[2] == 123);
    assert(linux_pilot_receiver_stage(&receiver, 1, bytes, sizeof(bytes), 65539) == LINUX_PILOT_RECEIVER_LATE_RECORD);
    assert(linux_pilot_receiver_stage(&receiver, 1, bytes, sizeof(bytes), UINT64_MAX) == LINUX_PILOT_RECEIVER_INVALID_TICK);
    assert(linux_pilot_receiver_destroy(&receiver));

    configure_wire(&receiver, 10, 20);
    encode_record(4, 0, bytes);
    assert(linux_pilot_receiver_stage(&receiver, 1, bytes, sizeof(bytes), 3) == LINUX_PILOT_RECEIVER_INVALID_TICK);
    encode_record(0, 0, bytes);
    assert(linux_pilot_receiver_stage(&receiver, 1, bytes, sizeof(bytes), 0) == LINUX_PILOT_RECEIVER_LATE_RECORD);
    encode_record(7, 1, bytes);
    receiver.ring[10 % PILOT_RING].tick = 74; /* Synthetic occupied-future ring corruption. */
    receiver.ring[10 % PILOT_RING].present = 1;
    assert(linux_pilot_receiver_stage(&receiver, 1, bytes, sizeof(bytes), 9) == LINUX_PILOT_RECEIVER_RING_CONFLICT);
    memset(&receiver.ring[10 % PILOT_RING], 0, sizeof(receiver.ring[0]));
    assert(linux_pilot_receiver_stage(&receiver, 1, bytes, sizeof(bytes), 9) == LINUX_PILOT_RECEIVER_STAGED);
    assert(linux_pilot_receiver_stage(&receiver, 2, bytes, sizeof(bytes), 9) == LINUX_PILOT_RECEIVER_STAGED);
    receiver.ring[10 % PILOT_RING].bytes[1][2] = 8;
    assert(linux_pilot_receiver_publish(&receiver, 10) == LINUX_PILOT_RECEIVER_DELAY_MISMATCH && !receiver.frame_ready);
    receiver.ring[10 % PILOT_RING].bytes[1][2] = 7;
    receiver.ring[10 % PILOT_RING].bytes[1][19] = 1;
    assert(linux_pilot_receiver_publish(&receiver, 10) == LINUX_PILOT_RECEIVER_DELAY_MISMATCH && !receiver.frame_ready);
    receiver.ring[10 % PILOT_RING].bytes[1][19] = 0;
    assert(linux_pilot_receiver_publish(&receiver, 10) == LINUX_PILOT_RECEIVER_FRAME_READY);
    receiver.staged = receiver.duplicates = receiver.conflicts = receiver.missing = UINT64_MAX;
    receiver.late = receiver.publications = receiver.apply_refusals = UINT64_MAX;
    char status[768];
    assert(linux_pilot_receiver_status(&receiver, status, sizeof(status)));
    assert(strstr(status, "apply_refusals=18446744073709551615 steering_active=0 apply=not_implemented"));
    assert(!linux_pilot_receiver_status(&receiver, status, 8));
    assert(!linux_pilot_receiver_status(&receiver, NULL, sizeof(status)));
    assert(!linux_pilot_receiver_status(&receiver, status, 0));
    assert(linux_pilot_receiver_destroy(&receiver));
    puts("pilot receiver: capture16 wrap, exact echo, pending rings, invalid delayed records and status bounds PASS");
}

static void test_echo_ordering_failure(void) {
    unsigned char first[PILOT_RECORD_BYTES];
    unsigned char second[PILOT_RECORD_BYTES];
    encode_record(7, 100, first);
    encode_record(7, 200, second);
    for (unsigned order = 0; order < 3; order++) {
        linux_pilot_receiver receiver;
        configure_wire(&receiver, 10, 20);
        if (order == 0) {
            assert(linux_pilot_receiver_remember_local(&receiver, 7, first, sizeof(first)) == LINUX_PILOT_RECEIVER_LOCAL_REMEMBERED);
            assert(linux_pilot_receiver_stage(&receiver, 1, second, sizeof(second), 9) == LINUX_PILOT_RECEIVER_ECHO_MISMATCH);
        } else {
            assert(linux_pilot_receiver_stage(&receiver, 1, first, sizeof(first), 9) == LINUX_PILOT_RECEIVER_STAGED);
            if (order == 2) {
                assert(linux_pilot_receiver_stage(&receiver, 2, first, sizeof(first), 9) == LINUX_PILOT_RECEIVER_STAGED);
                assert(linux_pilot_receiver_publish(&receiver, 10) == LINUX_PILOT_RECEIVER_FRAME_READY);
            }
            assert(linux_pilot_receiver_remember_local(&receiver, 7, second, sizeof(second)) == LINUX_PILOT_RECEIVER_ECHO_MISMATCH);
        }
        assert(receiver.echo_failure);
        assert(linux_pilot_receiver_stage(&receiver, 1, first, sizeof(first), 10) == LINUX_PILOT_RECEIVER_ECHO_MISMATCH);
        assert(linux_pilot_receiver_publish(&receiver, 10) == LINUX_PILOT_RECEIVER_ECHO_MISMATCH);
        assert(linux_pilot_receiver_configure(&receiver, manifest, PILOT_SOURCE_WIRE, 3, 10, 20, 0, 1) == LINUX_PILOT_RECEIVER_ECHO_MISMATCH);
        linux_pilot_receiver_frame frame;
        assert(linux_pilot_receiver_copy_frame(&receiver, 10, &frame) == LINUX_PILOT_RECEIVER_ECHO_MISMATCH);
        char status[768];
        assert(linux_pilot_receiver_status(&receiver, status, sizeof(status)) && strstr(status, "echo_failure=1"));
        assert(linux_pilot_receiver_apply(&receiver) == LINUX_PILOT_RECEIVER_APPLY_NOT_IMPLEMENTED);
        linux_pilot_receiver_stop(&receiver);
        assert(linux_pilot_receiver_destroy(&receiver));
    }
    /* Reversed but matching evidence remains valid; no external ordering is
     * assumed, and no contradiction is manufactured from arrival order alone. */
    linux_pilot_receiver receiver;
    configure_wire(&receiver, 10, 20);
    assert(linux_pilot_receiver_stage(&receiver, 1, first, sizeof(first), 9) == LINUX_PILOT_RECEIVER_STAGED);
    assert(linux_pilot_receiver_remember_local(&receiver, 7, first, sizeof(first)) == LINUX_PILOT_RECEIVER_LOCAL_REMEMBERED);
    assert(linux_pilot_receiver_stage(&receiver, 2, second, sizeof(second), 9) == LINUX_PILOT_RECEIVER_STAGED);
    assert(linux_pilot_receiver_publish(&receiver, 10) == LINUX_PILOT_RECEIVER_FRAME_READY && !receiver.echo_failure);
    assert(linux_pilot_receiver_destroy(&receiver));
    puts("pilot receiver: own-echo contradictions latch in both orders and after data publication PASS");
}

typedef struct {
    linux_pilot_receiver *receiver;
    atomic_uint published;
    atomic_uint consumed;
} reader_context;

static void *frame_reader(void *opaque) {
    reader_context *context = opaque;
    for (unsigned expected = 10; expected <= 510; expected++) {
        while (atomic_load_explicit(&context->published, memory_order_acquire) != expected) sched_yield();
        linux_pilot_receiver_frame frame;
        assert(linux_pilot_receiver_copy_frame(context->receiver, expected, &frame) == LINUX_PILOT_RECEIVER_FRAME_READY);
        assert(frame.tick == expected && frame.present == 3 && !frame.steering_active);
        for (unsigned i = 0; i < 2; i++) {
            pilot_record decoded;
            assert(pilot_decode(frame.bytes[i], &decoded));
            assert(decoded.capture16 == (uint16_t)(expected - 3) && decoded.q[2] == (int16_t)expected);
            assert(!memcmp(decoded.q, frame.records[i].q, sizeof(decoded.q)));
            assert(frame.saved[i] == (i ? 0x176a74u : 0x176a13u));
        }
        atomic_store_explicit(&context->consumed, expected, memory_order_release);
    }
    return NULL;
}

static void test_concurrent_publication(void) {
    linux_pilot_receiver receiver;
    configure_wire(&receiver, 10, 510);
    reader_context context = {.receiver = &receiver};
    atomic_init(&context.published, 0);
    atomic_init(&context.consumed, 0);
    pthread_t reader;
    assert(!pthread_create(&reader, NULL, frame_reader, &context));
    for (unsigned tick = 10; tick <= 510; tick++) {
        unsigned char bytes[PILOT_RECORD_BYTES];
        encode_record(tick - 3, (int16_t)tick, bytes);
        assert(linux_pilot_receiver_stage(&receiver, 1, bytes, sizeof(bytes), tick - 1) == LINUX_PILOT_RECEIVER_STAGED);
        assert(linux_pilot_receiver_stage(&receiver, 2, bytes, sizeof(bytes), tick - 1) == LINUX_PILOT_RECEIVER_STAGED);
        assert(linux_pilot_receiver_publish(&receiver, tick) == LINUX_PILOT_RECEIVER_FRAME_READY);
        atomic_store_explicit(&context.published, tick, memory_order_release);
        while (atomic_load_explicit(&context.consumed, memory_order_acquire) != tick) sched_yield();
    }
    assert(!pthread_join(reader, NULL));
    linux_pilot_receiver_stop(&receiver);
    assert(linux_pilot_receiver_publish(&receiver, 511) == LINUX_PILOT_RECEIVER_STOPPED);
    assert(receiver.publications == 501 && receiver.staged == 1002);
    assert(linux_pilot_receiver_destroy(&receiver));
    puts("pilot receiver: 501 complete wire frames published/read across threads without partial frames PASS");
}

int main(void) {
    test_windows_golden_codec();
    test_config_and_script();
    test_staging_publication();
    test_wrap_echo_and_corruption();
    test_echo_ordering_failure();
    test_concurrent_publication();
    puts("PASS: receiver preparation only; native steering apply remains NOT IMPLEMENTED");
    return 0;
}
