/* Determinism fix, DEFAULT OFF (native_gate_speech_clock = true): the unpaused clock that times NPC
 * speech lines starts the session at one value on every client.
 *
 * The unpaused clock is a double at 0x6b60e48 (under the critical section at 0x6b60e10).
 * The frame update at 0xfa3352..0xfa3372 adds the frame's dt to it unless the game is paused.
 * EntitySpeakData::SpeakNextLine reads it as the line's start (0x561392, stored at +0x20),
 * and the 0.05 s ticker (vfunc1 0x563d30) ends the line once now >= start + duration (the read at
 * 0x5640d4). With native_gate_speak_timer every line ends on that text deadline. In a session every
 * frame adds the same fixed step, but the clock's value at the first gated frame is whatever each
 * client's boot (menu and loading frames, unfixed) accumulated. A deadline that falls on a frame
 * boundary compares differently at different magnitudes. night7 seed 1111111:
 * NPC_UseCases.OnSpeakFinished of the mentor's line ended at tick 3748 on client 1 and at 3750 on
 * client 2 (both started it at 3636). Its listeners ran two ticks apart: relay mismatches at
 * 3749 and 3750, equal again afterwards. Reproduced by the triage rerun at the same tick.
 *
 * At the first gated frame after arming, the clock is set to SCLOCK_VALUE under the engine's
 * lock. That is forward only: a clock already past it (18 hours of uptime) refuses. From then on
 * every client adds the same steps. Lines started before the set end at the first ticker after
 * it, on the same tick everywhere. Pins: the four instructions that read or write the clock (their
 * rip displacements name it). Every gate tick re-checks them; a failure stops the gate with reason
 * 196. Nothing is restored at stop: the clock only runs on. The status shows the clock's bits, so
 * clients can be compared tick by tick.
 * Joining a running session (hotjoin, checkpoint loads): the clients in the session hold
 * SCLOCK_VALUE plus every frame's addition since tick 0. A joiner at tick T needs that exact
 * double: the same T additions of the fixed step, in order, from SCLOCK_VALUE (not
 * SCLOCK_VALUE + T * step, which rounds differently). The hot join carries the saver's value at T
 * in its GATE record; the joiner's admission writes it and marks the clock set (stage join), so this
 * module neither sets the start again nor refuses (lockstep_join.h join_admit_unpaused). */
#define SCLOCK_CLOCK 0x6b60e48u
#define SCLOCK_LOCK 0x6b60e10u
#define SCLOCK_VALUE 65536.0
#define SCLOCK_STOP 196u /* docs/LOCKSTEP-REGISTRY.txt */
#define SCLOCK_SITES 4u
#ifndef SCLOCK_OWN_PLATFORM
static const uint32_t sclock_site[SCLOCK_SITES]={0x561392u,0x5640d4u,0xfa3352u,0xfa335eu};
/* movsd xmm6,[rip+d] (reads) and movsd xmm0,[rip+d] / movsd [rip+d],xmm0 (the update), d naming SCLOCK_CLOCK. */
static const unsigned char sclock_code[SCLOCK_SITES][4]={{0xf2,0x0f,0x10,0x35},{0xf2,0x0f,0x10,0x35},{0xf2,0x0f,0x10,0x05},{0xf2,0x0f,0x11,0x05}};
#endif
static int sclock_armed,sclock_set;
static uint64_t sclock_set_tick,sclock_before_bits;
static volatile LONG64 sclock_failures;
static const char *sclock_stage="idle";
static int sclock_pins(void) {
    for(unsigned i=0;i<SCLOCK_SITES;i++) {
        unsigned char code[8];int32_t d;
        if(!read_local(image+sclock_site[i],code,sizeof(code)) || memcmp(code,sclock_code[i],4)) return 0;
        memcpy(&d,code+4,4);
        if((int64_t)sclock_site[i]+8+d!=(int64_t)SCLOCK_CLOCK) return 0;
    }
    return 1;
}
static double sclock_read(void) {double v=0;read_local(image+SCLOCK_CLOCK,&v,sizeof(v));return v;}
/* Arm (UI thread, before the first tick); the clock is set by the first gated frame. */
static int sclock_arm(void) {
    if(sclock_armed) return stats.active!=0;
    if(!stats.active || stats.issued || GetCurrentThreadId()!=ui_thread) return 0;
    if(!sclock_pins()) {sclock_stage="pins";return 0;}
    sclock_armed=1;sclock_stage="armed";return 1;
}
/* Gate tick (main thread, no frame running): the set on the first one, the pins on every one. */
static int sclock_tick(void) {
    if(InterlockedCompareExchange64(&sclock_failures,0,0)) return 0;
    if(!sclock_pins()) {sclock_stage="pins_changed";InterlockedIncrement64(&sclock_failures);return 0;}
    if(sclock_set) return 1;
    CRITICAL_SECTION *lock=(CRITICAL_SECTION*)(image+SCLOCK_LOCK);
    EnterCriticalSection(lock);
    double before=*(volatile double*)(image+SCLOCK_CLOCK);
    int ok=isfinite(before) && before>=0 && before<SCLOCK_VALUE;
    if(ok) *(volatile double*)(image+SCLOCK_CLOCK)=SCLOCK_VALUE;
    LeaveCriticalSection(lock);
    memcpy(&sclock_before_bits,&before,8);
    if(!ok) {sclock_stage="past_value";InterlockedIncrement64(&sclock_failures);return 0;}
    sclock_set=1;sclock_set_tick=stats.issued;sclock_stage="set";return 1;
}
static int sclock_status(char *out,size_t capacity) {
    double now=sclock_read();uint64_t bits;memcpy(&bits,&now,8);
    int n=snprintf(out,capacity,"armed=%d set=%d set_tick=%llu before=%016llx clock=%016llx stage=%s failures=%lld",sclock_armed,sclock_set,
        (unsigned long long)sclock_set_tick,(unsigned long long)sclock_before_bits,(unsigned long long)bits,sclock_stage,(long long)sclock_failures);
    return n>0 && (size_t)n<capacity?n:0;
}
