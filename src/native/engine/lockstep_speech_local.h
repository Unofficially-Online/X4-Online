/* Spoken lines per client, DEFAULT ON in play (native_gate_speech_local = true; needs native_gate_speak_timer, whose stop
 * reason 139 and block pin it shares; Windows gate).
 *
 * X4 speaks for one player: scripts start a line because of where the player is (slot 1's ship on every client), and
 * U::EntitySpeakData::SpeakNextLine 0x561330 starts its voice sound on whatever PC runs it. So every client plays slot 1's
 * comm chatter, wherever its own player is, and a hot joiner plays the session's past chatter again while it replays the
 * ticks since its save (user report, 10 Nov: the first instance's combat chatter on the second). The simulation does not
 * need the sound: with the speak timer every line ends on its text timer.
 *
 * This client starts no voice sound (lockstep_speak_timer.h's second site, the je at 0x56163b as two nops) while
 *   - it is a hot joiner before its handover (the replay of the session's past), or
 *   - its own slot is not slot 1 and its ship is in another sector (class 0x58) than slot 1's player: the lines are about
 *     slot 1's surroundings.
 * In slot 1's sector a client hears what slot 1 hears. Decided once per step in the probe's frame (gate.speech_local_frame:
 * the simulation's own thread, canonical state, so no line is being started while the site changes), by a 16-bit CAS. A
 * site that holds neither value is a failure of the speak timer's (its tick check stops, 139). An unseated slot (dead, in
 * a suit, walking) keeps the last answer. Not covered: lines X4 would speak for another player's own surroundings (never
 * started: the scripts ask about slot 1), other sounds of the replay (positional, at slot 1's camera until the handover). */
static int spl_armed,spl_muted;
static uint64_t spl_frames,spl_toggles,spl_catchup,spl_far,spl_near,spl_unknown;
static const char *spl_stage="idle";
static int join_catching_up(void);
static int (*spl_catching_up)(void)=join_catching_up;   /* tests replace it */
/* The class-0x58 (sector) ancestor of a component, itself included; NULL when none. */
static void *spl_sector_of(void *object) {
    unsigned char *p=object;
    for(unsigned depth=0;p && depth<24;depth++) {
        void **vtable=NULL;if(!read_local(p,&vtable,8) || !vtable) return NULL;
        int (*is_a)(void *,int)=NULL;if(!read_local((unsigned char*)vtable+0x11e8,&is_a,8) || !is_a) return NULL;
        if(is_a(p,0x58)) return p;
        if(!read_local(p+0x70,&p,8)) return NULL;
    }
    return NULL;
}
/* 1 mute, 0 play, -1 unknown (keep). */
static int spl_want(void) {
    if(spl_catching_up()) {++spl_catchup;return 1;}
    const unsigned slot=stats.slot;
    if(slot<2 || slot>PILOT_SLOTS) return 0;
    const pslot_slot *s=&pslots[slot];
    if(!s->seated || s->walking || !s->ship || !pslot_p1) {++spl_unknown;return -1;}
    void *mine=spl_sector_of(s->ship),*theirs=spl_sector_of(pslot_p1);
    if(!mine || !theirs) {++spl_unknown;return -1;}
    if(mine!=theirs) {++spl_far;return 1;}
    ++spl_near;return 0;
}
static int spl_set(int mute) {
    if(mute==spl_muted) return 1;
    if(!speak_site_swap(SPEAK_MUTE_SITE,mute?SPEAK_MUTE_ORIGINAL:SPEAK_MUTE_PATCHED,mute?SPEAK_MUTE_PATCHED:SPEAK_MUTE_ORIGINAL)) {
        spl_stage="cas";speak_timer_stage="mute_site";InterlockedIncrement64(&speak_timer_failures);return 0;
    }
    spl_muted=mute;++spl_toggles;return 1;
}
/* gate.speech_local(): before the first tick, after the speak timer (an identical rerun after a UI reload answers 1). */
static int spl_arm(void) {
    if(spl_armed) return stats.active!=0;
    if(!stats.active || GetCurrentThreadId()!=ui_thread || !speak_timer_installed || !speak_timer_patched) {spl_stage="refused";return 0;}
    if(speak_mute_site()!=SPEAK_MUTE_ORIGINAL) {spl_stage="site";return 0;}
    spl_frames=spl_toggles=spl_catchup=spl_far=spl_near=spl_unknown=0;spl_muted=0;spl_armed=1;spl_stage="armed";return 1;
}
/* gate.speech_local_frame(): once per step, in the probe's frame. Answers the state (1 muted). */
static int spl_frame(void) {
    if(!spl_armed || !stats.active) return 0;
    ++spl_frames;
    const int want=spl_want();
    if(want>=0) (void)spl_set(want);
    return spl_muted;
}
/* Stop: the site back before the speak timer's own restore. A foreign value stays. */
static void spl_restore(void) {
    if(!spl_armed) return;
    if(spl_muted && speak_site_swap(SPEAK_MUTE_SITE,SPEAK_MUTE_PATCHED,SPEAK_MUTE_ORIGINAL)) spl_muted=0;
    spl_armed=0;spl_stage="restored";
}
static int spl_status(char *out,size_t capacity) {
    int n=snprintf(out,capacity,"armed=%d muted=%d site=%04x frames=%llu toggles=%llu catchup=%llu far=%llu near=%llu unknown=%llu stage=%s",
        spl_armed,spl_muted,speak_mute_site(),(unsigned long long)spl_frames,(unsigned long long)spl_toggles,(unsigned long long)spl_catchup,
        (unsigned long long)spl_far,(unsigned long long)spl_near,(unsigned long long)spl_unknown,spl_stage);
    return n>0 && (size_t)n<capacity?n:0;
}
