/* Determinism fix, DEFAULT OFF: an NPC speech line ends on its text timer, never on
 * the local sound (native_gate_speak_timer = true).
 *
 * U::EntitySpeakData::SpeakNextLine 0x561330 starts the line's voice sound and
 * always sets the line's text duration (+0x18: characters * 0.08 s * the language's
 * factor, plus 0.6 s) from the start time +0x20 (unpaused real time, one fixed step
 * per frame). Then the line takes one of two ways, at 0x5617c9:
 *   no sound handle (+0x14 == 0): state 4, the 0.05 s game-time ticker ends the line
 *     when start + duration is reached (vfunc1 0x563d30). Same frame on every client.
 *   a sound handle: state 3 waits for the audio thread to report the sound's length
 *     (duration replaced by it), then state 5 ends the line when the sound's own state
 *     changes. That is the audio thread's wall clock, on one client's sound system.
 * OnSpeakFinished 0x562590 posts SpeakLineFinishedEvent and SpeakFinishedEvent, so
 * event_speak_finished cues (and the next line) run on the frame the local audio
 * decided. reg-missions: 8 of 9 lines ended on the same frame, one (ms=62982 and
 * ms=63049, two frames apart) gave its MissionSpeech cue instance a runtime ID that
 * many allocations apart on the two clients: relay mismatches at 1678 and 1679, the
 * next= words equal again at 1680, the IDs allocated in between off by one.
 *
 * The site's jne becomes two nops: every line takes state 4, also with a sound handle,
 * which only plays out (a line that ends before its sound does not stop it, as
 * natively). It is the way a client without a voice sound takes anyway, so no sound
 * device, voice setting or audio thread decides. All clients need the same language,
 * the duration uses its factor. native_gate_speech_game_time (lockstep_npc_deadline.h)
 * keeps the audio's length and only moves the end to the game clock, so it still
 * depends on the audio being ready and finished before the deadline; reg-missions ran
 * with it (routed=5). With both armed no line reaches state 5: routed= stays 0.
 * Pins: SpeakNextLine, vfunc1, OnSpeakFinished and the duration function, without
 * the words the speech modules replace (the two entries, the deadline and ready
 * words of vfunc1). Every gate tick re-checks the site and the pins; a failure stops
 * the gate with reason 139. Stop restores the site by CAS. */
#define SPEAK_TIMER_SITE 0x5617cdu
#define SPEAK_TIMER_ORIGINAL 0x7275u /* jne rel8 +0x72 (little endian 75 72) */
#define SPEAK_TIMER_PATCHED 0x9090u
#define SPEAK_TIMER_STOP 139u /* 130 until 1 November 2026 (the runtime IDs'); docs/LOCKSTEP-REGISTRY.txt */
#define SPEAK_TIMER_BLOCK 0x561340u
#define SPEAK_TIMER_BLOCK_SIZE 0x6c6u
#define SPEAK_TIMER_BLOCK_SHA "8c135b9a88e8627b0735963bead8048abf0b530deff6c98621d04367a2a9d478"
/* The second site in the same pinned block, lockstep_speech_local.h's (native_gate_speech_local): whether a line's voice sound
 * starts at all. 0x561637 cmp byte [rsi+0x60], 0 / 0x56163b je 0x561644 (the sound start) / mov eax, ebx / jmp 0x56179e (handle
 * 0): the speak data's own mute byte. The je as two nops: no line starts a sound on this client. With the timer site patched
 * a line with and without a handle take the same way, so the simulation cannot tell. Either value passes the block pin. */
#define SPEAK_MUTE_SITE 0x56163bu
#define SPEAK_MUTE_ORIGINAL 0x0774u /* je rel8 +7 (little endian 74 07) */
#define SPEAK_MUTE_PATCHED 0x9090u
/* The third site: the lipsync. U::BodyClass::Move 0x380dd0 starts a speaking character's lipsync animation
 * (U::BodyClass::StartLipsyncAnim 0x37c640, the file <voice>/<page>/lipsync/<line>), and when the animation has a length it
 * sets the speaker's line duration to it (0x37caeb: speak data = entity +0x378 (or its controlled one's); 0x37cafd comisd
 * xmm7 (0.0),[rax+0x18] / 0x37cb02 jnc / 0x37cb04 movsd [rax+0x18],xmm6). A body moves where it is presented, so that
 * store landed where and when each client's presentation ran it: rk17's two-client satellite run (26 Nov), 18 of 116
 * lines ended on a lipsync length instead of their text (player 2's wingmate's 30284002: 4.2 s by its text, 2.833 s by its
 * lipsync, the bits of the float 2833 * 0.001f), the same on both clients; its three-client runs did not agree: player
 * 2's 30284008 (2.6 s by its text) ended at 5,490 on one or two clients (its lipsync's 2.04 s) and at 5,506 on the others,
 * its use case asked for the next line 16 ticks apart and the runtime IDs parted there (census 5,700: 659 objects apart).
 * The jnc becomes a jmp over the store: the animation still plays; the line keeps the duration SpeakNextLine gave it, the
 * text's, on every client. Pinned (0x37cac0..0x37cb20 with the site as either value); armed and restored with the timer
 * site, checked every gate tick (lipsync_changed). */
#define SPEAK_LIPSYNC_SITE 0x37cb02u
#define SPEAK_LIPSYNC_ORIGINAL 0x0573u /* jnc rel8 +5 (little endian 73 05) */
#define SPEAK_LIPSYNC_PATCHED 0x05ebu  /* jmp rel8 +5 */
#define SPEAK_LIPSYNC_BLOCK 0x37cac0u
#define SPEAK_LIPSYNC_BLOCK_SIZE 0x60u
#define SPEAK_LIPSYNC_BLOCK_SHA "210ae677d7628c25d8ad91011a638e0d3b5b7417c4c1363bee530d9a9acae520"
static int speak_timer_installed,speak_timer_restored,speak_timer_patched,speak_lipsync_patched;
static volatile LONG64 speak_timer_failures;
static const char *speak_timer_stage="idle";
static int speak_timer_block_ok(void) {
    unsigned char copy[SPEAK_TIMER_BLOCK_SIZE];uint16_t site;
    if(!read_local(image+SPEAK_TIMER_BLOCK,copy,sizeof(copy))) return 0;
    memcpy(&site,copy+(SPEAK_TIMER_SITE-SPEAK_TIMER_BLOCK),2);
    if(site!=SPEAK_TIMER_ORIGINAL && site!=SPEAK_TIMER_PATCHED) return 0;
    site=SPEAK_TIMER_ORIGINAL;memcpy(copy+(SPEAK_TIMER_SITE-SPEAK_TIMER_BLOCK),&site,2);
    memcpy(&site,copy+(SPEAK_MUTE_SITE-SPEAK_TIMER_BLOCK),2);
    if(site!=SPEAK_MUTE_ORIGINAL && site!=SPEAK_MUTE_PATCHED) return 0;
    site=SPEAK_MUTE_ORIGINAL;memcpy(copy+(SPEAK_MUTE_SITE-SPEAK_TIMER_BLOCK),&site,2);
    return hash_ok(copy,sizeof(copy),SPEAK_TIMER_BLOCK_SHA);
}
/* Call sites of SpeakNextLine inside the pinned ranges that lockstep_speech_local.h re-points for a player's own lines
 * (the target readout): while one holds exactly its patched bytes, the range is hashed with the original call there.
 * The first live run of those sites stopped both clients at the first tick with 139 block_changed (14 Nov). */
#define SPEAK_TIMER_SITES 4u
static struct {unsigned rva;unsigned char original[5],patched[5];int on;} speak_timer_sites[SPEAK_TIMER_SITES];
static int speak_timer_range_ok(unsigned rva,unsigned size,const char *sha) {
    unsigned char copy[0x600];
    if(size>sizeof(copy) || !read_local(image+rva,copy,size)) return 0;
    for(unsigned i=0;i<SPEAK_TIMER_SITES;i++) {
        const unsigned at=speak_timer_sites[i].rva;
        if(speak_timer_sites[i].on && at>=rva && at+5u<=rva+size && !memcmp(copy+(at-rva),speak_timer_sites[i].patched,5))
            memcpy(copy+(at-rva),speak_timer_sites[i].original,5);
    }
    return hash_ok(copy,size,sha);
}
static int speak_lipsync_block_ok(void) {
    unsigned char copy[SPEAK_LIPSYNC_BLOCK_SIZE];uint16_t site;
    if(!read_local(image+SPEAK_LIPSYNC_BLOCK,copy,sizeof(copy))) return 0;
    memcpy(&site,copy+(SPEAK_LIPSYNC_SITE-SPEAK_LIPSYNC_BLOCK),2);
    if(site!=SPEAK_LIPSYNC_ORIGINAL && site!=SPEAK_LIPSYNC_PATCHED) return 0;
    site=SPEAK_LIPSYNC_ORIGINAL;memcpy(copy+(SPEAK_LIPSYNC_SITE-SPEAK_LIPSYNC_BLOCK),&site,2);
    return hash_ok(copy,sizeof(copy),SPEAK_LIPSYNC_BLOCK_SHA);
}
static int speak_timer_pins(void) {
    return speak_timer_block_ok() && speak_lipsync_block_ok() &&
        speak_timer_range_ok(0x563d30u,0x150u,"ed172734d9e208e4362f3ace721a011af63837b3c9a5ee6e34090fd2994b663c") &&
        speak_timer_range_ok(0x563e90u,0x120u,"6f0439b83aa9f3e49fb13aa9f9d95a8addcbe2da5e5abec96cb851fe4b67e9bb") &&
        speak_timer_range_ok(0x563fc0u,0x383u,"b1adad699b2f404f4143a80a1c78c96a20767d62515294a19861fc8e2c3dae79") &&
        speak_timer_range_ok(0x5625a0u,0x523u,"2d2a5cc8133bdd0e89c19a847d072d1b1894dddc66493137a82c18faaffa2c7a") &&
        hash_ok(image+0x1263650u,0xc3u,"f7cf46737b50cff9608a6a90e7fb2c4a68f92cf75da5d95c5ece5863be8ff04e");
}
static uint16_t speak_timer_site(void) {uint16_t v=0;read_local(image+SPEAK_TIMER_SITE,&v,2);return v;}
static int speak_site_swap(uint32_t at,uint16_t expected,uint16_t value) {
    DWORD old,ignored;if(!VirtualProtect(image+at,2,PAGE_EXECUTE_READWRITE,&old)) return 0;
    uint16_t actual=(uint16_t)InterlockedCompareExchange16((volatile SHORT*)(image+at),(SHORT)value,(SHORT)expected);
    VirtualProtect(image+at,2,old,&ignored);FlushInstructionCache(GetCurrentProcess(),image+at,2);
    return actual==expected;
}
static int speak_timer_swap(uint16_t expected,uint16_t value) {return speak_site_swap(SPEAK_TIMER_SITE,expected,value);}
static uint16_t speak_mute_site(void) {uint16_t v=0;read_local(image+SPEAK_MUTE_SITE,&v,2);return v;}
static uint16_t speak_lipsync_site(void) {uint16_t v=0;read_local(image+SPEAK_LIPSYNC_SITE,&v,2);return v;}
static int speak_timer_arm(void) {
    if(speak_timer_installed) return stats.active!=0; /* Identical rerun after a loading-UI reload. */
    if(!stats.active || (boot_mode && boot_phase) || stats.issued || GetCurrentThreadId()!=ui_thread) return 0;
    if(GetModuleHandleW(L"x4lan_fixedtick_64.dll")) {speak_timer_stage="foreign_module";return 0;}
    if(!speak_timer_pins()) {speak_timer_stage="pins";return 0;}
    if(speak_timer_site()!=SPEAK_TIMER_ORIGINAL) {speak_timer_stage="site";return 0;}
    if(speak_lipsync_site()!=SPEAK_LIPSYNC_ORIGINAL) {speak_timer_stage="lipsync_site";return 0;}
    if(!speak_timer_swap(SPEAK_TIMER_ORIGINAL,SPEAK_TIMER_PATCHED)) {speak_timer_stage="cas";return 0;}
    if(!speak_site_swap(SPEAK_LIPSYNC_SITE,SPEAK_LIPSYNC_ORIGINAL,SPEAK_LIPSYNC_PATCHED)) {
        (void)speak_timer_swap(SPEAK_TIMER_PATCHED,SPEAK_TIMER_ORIGINAL);speak_timer_stage="lipsync_cas";return 0;
    }
    speak_lipsync_patched=1;speak_timer_patched=1;speak_timer_restored=0;speak_timer_installed=1;speak_timer_stage="armed";return 1;
}
/* Main-thread tick check. */
static int speak_timer_check(void) {
    if(InterlockedCompareExchange64(&speak_timer_failures,0,0)) return 0;
    const char *why=speak_timer_site()!=SPEAK_TIMER_PATCHED?"site_changed":
        speak_lipsync_patched && speak_lipsync_site()!=SPEAK_LIPSYNC_PATCHED?"lipsync_changed":!speak_timer_pins()?"block_changed":NULL;
    if(why) {speak_timer_stage=why;InterlockedIncrement64(&speak_timer_failures);return 0;}
    return 1;
}
static int speak_timer_owned(void) {return !speak_timer_patched || speak_timer_site()==SPEAK_TIMER_PATCHED;}
static void speak_timer_restore(void) {
    if(!speak_timer_installed) return;
    if(speak_lipsync_patched) {
        if(!speak_site_swap(SPEAK_LIPSYNC_SITE,SPEAK_LIPSYNC_PATCHED,SPEAK_LIPSYNC_ORIGINAL)) return; /* a foreign value stays */
        speak_lipsync_patched=0;
    }
    if(speak_timer_patched) {
        if(!speak_timer_swap(SPEAK_TIMER_PATCHED,SPEAK_TIMER_ORIGINAL)) return; /* a foreign value stays */
        speak_timer_patched=0;
    }
    speak_timer_restored=1;speak_timer_installed=0;
}
static int speak_timer_status(char *out,size_t capacity) {
    int n=snprintf(out,capacity,"installed=%d site=%04x restored=%d stage=%s failures=%lld lipsync=%04x",speak_timer_installed,speak_timer_site(),
        speak_timer_restored,speak_timer_stage,(long long)speak_timer_failures,speak_lipsync_site());
    return n>0 && (size_t)n<capacity?n:0;
}
