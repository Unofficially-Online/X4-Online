/* UI reloads held while a session is live (native_gate_ui_reload_hold; a session default since its run of 11 Nov; part of
 * stock piloting's native pieces, lockstep_pilot_native.h).
 *
 * What X4 does [F, pinned 9.00]. One byte (image+0x3d19908) asks for a reload of the whole UI. It is set by the Lua call
 * ScheduleReloadUI (2bc080; the stock helper.lua calls it when a menu's view cannot be made), by SetHUDScaleOption
 * (24d930) and SetMenuWidthScale (24f9a0), by the "uiscale" option (b46270), by the input action that toggles controller
 * mode (122c8ed, which also flips the byte MD reads as player.input.controller), by the /reloadui chat command (ac019f)
 * and by a graphics event of type 1 in the queue 9fdc20 drains (a device or window change). UniverseFrameUpdate acts on
 * it at 9fe75a: cmp byte,0 / je 9fe771 / call ab1c40 (the reload) / mov byte,0.
 * A reload ends the Lua state the probe lives in. The probe then boots a second time inside the session, its arming is
 * refused and the client leaves (live sx2, 11 Nov 2026: client 2 at tick 1179, "DEFAULTS ..." a second time, then
 * "pilot slots post refused", ABORT, the relay's stall drop; the same day the user lost two sessions to a window resize).
 *
 * Here. From the first live pass of the pilot window (take+1: every reload a start needs, the start menu's and the
 * load's, is long over) to the stop the je is a jmp (two bytes at 9fe761): X4 neither acts on the flag nor clears it.
 * With the other native pieces at stop+1, and on any abort, the two bytes go back before X4 runs on, so a held reload
 * happens in X4's first frame after the session, from X4 itself. No resume of the probe is attempted.
 * The cost: what asked for the reload waits. After a window resize the UI keeps the layout of the old size and a UI
 * scale change shows only after the session; the second flag next to it (0x3d19909, ab2600) is left alone.
 * Status (UI_REPLAY_STATUS): reload_hold=armed:patched:held:first_tick:passes (held: the flag was seen set in a live
 * pass; the tick of the first such pass; how many passes saw it). */
#define UIRH_PIN 0x9fe750u
#define UIRH_PIN_SIZE 0x40u
#define UIRH_PIN_SHA "61bf9d8cdac18431880d89e6c9e1328dad2b8090ac047ac1a9b01ae2339517be"
#define UIRH_JUMP 0x9fe761u
#define UIRH_FLAG 0x3d19908u
#define UIRH_ORIGINAL 0xc6000b34d8e80e74ULL   /* 74 0e | e8 d8 34 0b 00 | c6: je 9fe771; call ab1c40; mov byte ... */
#define UIRH_PATCHED 0xc6000b34d8e80eebULL    /* eb 0e: jmp 9fe771 */
static int uirh_armed,uirh_patched,uirh_held;
static unsigned char *uirh_site;                 /* the image's 9fe761, a test's */
static const volatile unsigned char *uirh_flag;  /* the image's byte, a test's */
static uint64_t uirh_first_tick,uirh_passes;
static const char *uirh_why="-";
static int uirh_swap(uint64_t expected,uint64_t value) {
    DWORD old,ignored;
    if(!uirh_site || !VirtualProtect(uirh_site,8,PAGE_EXECUTE_READWRITE,&old)) return 0;
    uint64_t actual=(uint64_t)InterlockedCompareExchange64((volatile LONG64*)uirh_site,(LONG64)value,(LONG64)expected);
    VirtualProtect(uirh_site,8,old,&ignored);FlushInstructionCache(GetCurrentProcess(),uirh_site,8);
    return actual==expected;
}
/* Every live pass of the pilot window: the hold in place and intact; a reload asked for is noted. 0: uirh_why. */
static int uirh_pass(uint64_t tick) {
    if(!uirh_armed) return 1;
    if(!uirh_patched) {
        if(!uirh_swap(UIRH_ORIGINAL,UIRH_PATCHED)) {uirh_why="reload_hold_install";return 0;}
        uirh_patched=1;
    } else {
        uint64_t now=0;
        if(!read_local(uirh_site,&now,8) || now!=UIRH_PATCHED) {uirh_why="reload_hold_lost";return 0;}
    }
    if(uirh_flag && *uirh_flag) {
        if(!uirh_held) {uirh_held=1;uirh_first_tick=tick;}
        ++uirh_passes;
    }
    return 1;
}
/* At the stop and on an abort, before X4 runs on: X4's own bytes again; the flag is X4's to act on. */
static int uirh_uninstall(void) {
    if(uirh_patched && uirh_swap(UIRH_PATCHED,UIRH_ORIGINAL)) uirh_patched=0;
    return !uirh_patched;
}
/* gate.ui_reload_hold(): after gate.pilot_stock, before the first tick. */
static int uirh_arm(void) {
    if(uirh_armed) return 1;
    if(stats.issued) {uirh_why="reload_hold_after_first_tick";return 0;}
    if(!pnat_armed || pnat_mode!=PNAT_MODE_STOCK) {uirh_why="reload_hold_without_stock";return 0;}
    if(!hash_ok(image+UIRH_PIN,UIRH_PIN_SIZE,UIRH_PIN_SHA)) {uirh_why="reload_hold_pins";return 0;}
    uirh_site=image+UIRH_JUMP;uirh_flag=image+UIRH_FLAG;
    uirh_patched=0;uirh_held=0;uirh_first_tick=0;uirh_passes=0;uirh_why="-";
    uirh_armed=1;return 1;
}
static int uirh_status(char *out,size_t capacity) {
    int n=snprintf(out,capacity," reload_hold=%d:%d:%d:%llu:%llu",uirh_armed,uirh_patched,uirh_held,(unsigned long long)uirh_first_tick,
                   (unsigned long long)uirh_passes);
    return n>0 && (size_t)n<capacity?n:0;
}
