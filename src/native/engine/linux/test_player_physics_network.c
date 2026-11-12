/* Engine-free adapter test. The native entry and both scope owners below are
 * deliberately small stubs; this does not emulate X4 or prove exclusion. */
#include <assert.h>
#include <setjmp.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "lockstep_stock_record.h"

#define FWRR_FIRST_GENERATION 5
static _Atomic unsigned player_physics_ready;
static unsigned player_recurring_physics = 3;
static struct { uint64_t issued; } stats;
static unsigned char keys[8], throttle_keys[2], strafe_ignored, throttle_ignored;
static unsigned entered = 1, identity = 1, pins = 1, bad_read, reads, neutral = 1, omit_strafe_write;
static unsigned first_calls, replay_calls, update_calls, native_calls, finish_calls;
static unsigned events[64], event_count;
static const char *failure;
static jmp_buf escape;
static void fail(const char *why) { failure = why; longjmp(escape, 1); }
static void lifecycle_discard_require(int ok, const char *why) { if (!ok) fail(why); }
static void player_recurring_fatal(const char *why) { fail(why); }
static int player_recurring_physics_entered(void) { return entered; }
static int lifecycle_input_pins(void) { return pins; }
static int player_physics_identity(void) { return identity; }
static float read_keys(unsigned char *out) {
    memcpy(out, keys, 4);
    if (++reads == bad_read) out[0] ^= 1;
    return 0.0f;
}
static float lifecycle_input_read(unsigned char *out) { return read_keys(out); }
static float player_physics_replay_read_input(unsigned char *out) { return read_keys(out); }
static void lifecycle_discard_update_once(int which) { assert(which == 3); ++update_calls; }
static void lifecycle_discard_write(const char *message) { assert(message); }
static void fake_state(int id, unsigned char value) {
    assert(((id >= 2 && id <= 9) || id == 13 || id == 14) && value <= 1);
    events[event_count++] = (unsigned)id * 10 + value;
    ++native_calls;
    if (id >= 13) throttle_keys[id - 13] = value;
    else if (id <= 5 || !omit_strafe_write) keys[id - 2] = value;
}
#define LX(va) ((va) == 0x166c6f0 ? (void *)(uintptr_t)&fake_state : (void *)(uintptr_t)(va))
static void lifecycle_discard_read(uintptr_t address, void *out, size_t length) {
    if (address == 0x3d8da72 && length == 4) memcpy(out, keys + 4, 4);
    else if (address == 0x3d8da7a && length == 2) memcpy(out, throttle_keys, 2);
    else if (address == 0x3e5b298 && length == 1) memcpy(out, &throttle_ignored, 1);
    else if (address == 0x3e5b299 && length == 1) memcpy(out, &strafe_ignored, 1);
    else assert(0);
}
static void player_physics_begin_with_input(unsigned framed, void (*callback)(void *), void *arg) {
    assert(framed == 1 && !atomic_load(&player_physics_ready));
    ++first_calls; callback(arg); atomic_store(&player_physics_ready, 1);
}
static void player_physics_replay_begin_with_input(uint64_t generation, void (*callback)(void *), void *arg) {
    assert(generation > 5 && atomic_load(&player_physics_ready));
    ++replay_calls; if (!identity) fail("scope_identity"); callback(arg);
}
static void player_physics_frame_record(uint64_t tick, const char *stage) { (void)tick; (void)stage; }
static void player_physics_frames_finish(uint64_t tick, unsigned final) { (void)tick; (void)final; ++finish_calls; }
static int keyboard_probe_independent_after_path_join(uint64_t generation);
static int keyboard_probe_independent_neutral(void) { return neutral; }
#define X4_GATE_PLAYER_THROTTLE_DISCARD_H
static unsigned player_throttle_phase, throttle_enabled, throttle_applied, throttle_updated;
static uint64_t throttle_start = 5;
static int player_throttle_enabled(void) { return throttle_enabled; }
static uint64_t player_throttle_start_tick(void) { return throttle_start; }
static void player_throttle_require_pins(void) { if (!pins) fail("throttle_pins"); }
static void player_throttle_apply(uint64_t tick, const stock_input_record *record) {
    if (tick != throttle_start && tick != throttle_start + 1) return;
    assert(tick == throttle_start + player_throttle_phase && !record->count);
    assert(update_calls == player_throttle_phase + throttle_start - 5);
    fake_state(13, player_throttle_phase == 0);
    ++player_throttle_phase;
    ++throttle_applied;
}
static void player_throttle_after_update(uint64_t tick) {
    if (tick != throttle_start && tick != throttle_start + 1) return;
    assert(tick == throttle_start - 1 + player_throttle_phase &&
           update_calls == player_throttle_phase + throttle_start - 5);
    ++throttle_updated;
}
#include "gate_player_physics_network.h"
static stock_input_record coordinated;
static int keyboard_probe_independent_after_path_join(uint64_t generation) {
    player_physics_network_apply_p2(generation, &coordinated);
    return 1;
}
static void reset(void) {
    atomic_store(&player_physics_ready, 0); player_network_last=4; player_network_held=0; player_network_busy=0;
    memset(keys,0,sizeof keys); memset(throttle_keys,0,sizeof throttle_keys); strafe_ignored=throttle_ignored=0; first_calls=replay_calls=update_calls=native_calls=finish_calls=event_count=0;
    entered=identity=pins=neutral=1; bad_read=reads=omit_strafe_write=0; failure=NULL;
    throttle_enabled=player_throttle_phase=throttle_applied=throttle_updated=0;
    throttle_start = 5; memset(&coordinated,0,sizeof coordinated);
}
static void tick(uint64_t n) { stats.issued=n; coordinated.capture16=(uint16_t)(n-2); player_physics_network_begin(n); }
static void event(unsigned i, unsigned id, unsigned value) {
    coordinated.events[i]=(stock_input_event){.kind=STOCK_INPUT_STATE,.id=id,.value=value};
}
int main(void) {
    reset();
    coordinated.count=1; event(0,2,1); tick(5);
    assert(first_calls==1 && replay_calls==0 && update_calls==1 && player_network_held==1);
    coordinated.count=0; tick(6); assert(player_network_held==1 && update_calls==2 && native_calls==1);
    coordinated.count=3; event(0,3,1); event(1,2,0); event(2,3,0); tick(7);
    assert(replay_calls==2 && update_calls==3 && player_network_held==0);
    assert(event_count==4 && events[0]==21 && events[1]==31 && events[2]==20 && events[3]==30);
    player_physics_network_finish(7,1); assert(finish_calls==1);

    reset(); coordinated.count=2; event(0,2,1); event(1,10,1);
    if (!setjmp(escape)) { tick(5); assert(0); }
    assert(!strcmp(failure,"network_record_policy") && !first_calls && !native_calls && !update_calls);

    reset(); coordinated.count=0; tick(5); identity=0;
    if (!setjmp(escape)) { tick(6); assert(0); }
    assert(!strcmp(failure,"scope_identity") && update_calls==1 && !native_calls);

    reset(); coordinated.count=1; event(0,2,1); bad_read=1;
    if (!setjmp(escape)) { tick(5); assert(0); }
    assert(!strcmp(failure,"network_key_readback") && !native_calls && !update_calls);

    reset(); coordinated.count=1; event(0,2,1); bad_read=2;
    if (!setjmp(escape)) { tick(5); assert(0); }
    assert(!strcmp(failure,"network_key_readback") && native_calls==1 && !update_calls);

    reset(); coordinated.count=1; event(0,2,1); tick(5);
    if (!setjmp(escape)) { player_physics_network_finish(5,1); assert(0); }
    assert(!strcmp(failure,"network_final_keys_held") && !finish_calls);

    reset(); coordinated.count=0; tick(5); neutral=0;
    if (!setjmp(escape)) { player_physics_network_finish(5,1); assert(0); }
    assert(!strcmp(failure,"network_final_keys_held") && !finish_calls);

    /* Noncontiguous strafe keys persist across records, with exact ordered releases. */
    reset(); coordinated.count=2; event(0,6,1); event(1,7,1); tick(5);
    assert(player_network_held == 48 && keys[4] == 1 && keys[5] == 1);
    coordinated.count=0; tick(6); assert(player_network_held == 48 && native_calls == 2);
    coordinated.count=3; event(0,6,0); event(1,7,0); event(2,6,1); tick(7);
    assert(player_network_held == 16 && event_count == 5);
    assert(events[0] == 61 && events[1] == 71 && events[2] == 60 && events[3] == 70 && events[4] == 61);
    coordinated.count=1; event(0,6,0); tick(8);
    assert(!player_network_held); player_physics_network_finish(8,1);

    /* Vertical keys use distinct bits and actual native addresses in the stub. */
    reset(); coordinated.count=2; event(0,8,1); event(1,9,1); tick(5);
    assert(player_network_held == 192 && keys[6] == 1 && keys[7] == 1);
    coordinated.count=0; tick(6); assert(player_network_held == 192 && native_calls == 2);
    coordinated.count=2; event(0,8,0); event(1,9,0); tick(7);
    assert(!player_network_held && events[0] == 81 && events[1] == 91 && events[2] == 80 && events[3] == 90);
    player_physics_network_finish(7,1);

    reset(); strafe_ignored=1; coordinated.count=1; event(0,8,1);
    if (!setjmp(escape)) { tick(5); assert(0); }
    assert(!strcmp(failure,"network_strafe_suppressed") && !native_calls && !update_calls);

    reset(); omit_strafe_write=1; coordinated.count=1; event(0,9,1);
    if (!setjmp(escape)) { tick(5); assert(0); }
    assert(!strcmp(failure,"network_key_readback") && native_calls == 1 && !update_calls);

    reset(); strafe_ignored=1; coordinated.count=1; event(0,6,1);
    if (!setjmp(escape)) { tick(5); assert(0); }
    assert(!strcmp(failure,"network_strafe_suppressed") && !native_calls && !update_calls);

    reset(); omit_strafe_write=1; coordinated.count=1; event(0,6,1);
    if (!setjmp(escape)) { tick(5); assert(0); }
    assert(!strcmp(failure,"network_key_readback") && native_calls == 1 && !update_calls);

    reset(); keys[4]=1; coordinated.count=0;
    if (!setjmp(escape)) { tick(5); assert(0); }
    assert(!strcmp(failure,"network_key_readback") && !native_calls);

    for (unsigned bad = 0; bad < 3; ++bad) {
        reset(); coordinated.count=1;
        event(0, bad == 0 ? 10 : bad == 1 ? 65535 : 6, bad == 2 ? 2 : 1);
        if (!setjmp(escape)) { tick(5); assert(0); }
        assert(!strcmp(failure,"network_record_policy") && !native_calls && !update_calls);
    }

    reset(); coordinated.count=2; event(0,13,1); event(1,14,1); tick(5);
    assert(player_network_held == 0x1800 && throttle_keys[0] && throttle_keys[1]);
    coordinated.count=0; tick(6);
    assert(player_network_held == 0x1800 && native_calls == 2 && update_calls == 2);
    coordinated.count=2; event(0,13,0); event(1,14,0); tick(7);
    assert(!player_network_held && !throttle_keys[0] && !throttle_keys[1]);
    player_physics_network_finish(7,1);

    reset(); throttle_ignored=1; coordinated.count=1; event(0,13,1);
    if (!setjmp(escape)) { tick(5); assert(0); }
    assert(!strcmp(failure,"network_throttle_suppressed") && !native_calls);

    reset(); throttle_enabled=1; player_network_held=0x800; coordinated.count=0;
    if (!setjmp(escape)) { tick(5); assert(0); }
    assert(!strcmp(failure,"network_scripted_throttle_ownership") && !native_calls && !update_calls);

    reset(); throttle_enabled=1; coordinated.count=1; event(0,13,0);
    if (!setjmp(escape)) { tick(5); assert(0); }
    assert(!strcmp(failure,"network_record_policy") && !native_calls && !update_calls);

    reset(); throttle_enabled=1; coordinated.count=0; tick(5); tick(6);
    assert(throttle_applied==2 && throttle_updated==2 && update_calls==2);
    assert(!throttle_keys[0] && !player_network_held);
    player_physics_network_finish(6,1);

    reset(); throttle_enabled=1; throttle_start=6; coordinated.count=0;
    tick(5); assert(!throttle_keys[0] && !player_throttle_phase && update_calls==1);
    tick(6); assert(throttle_keys[0] && player_throttle_phase==1 && update_calls==2);
    tick(7); assert(!throttle_keys[0] && player_throttle_phase==2 && update_calls==3);
    assert(!player_network_held && throttle_applied==2 && throttle_updated==2);
    player_physics_network_finish(7,1);

    reset(); throttle_enabled=1; coordinated.count=1; event(0,2,1);
    if (!setjmp(escape)) { tick(5); assert(0); }
    assert(!strcmp(failure,"throttle_script_admission") && !native_calls && !update_calls);

    reset(); coordinated.count=0; tick(5);
    if (!setjmp(escape)) { tick(5); assert(0); }
    assert(!strcmp(failure,"network_record_admission") && update_calls==1);
    return 0;
}
