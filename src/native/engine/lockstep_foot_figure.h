/* A client does not draw its own player's stand-in figure (with native_gate_foot_avatars; presentation only, this
 * client only; no stop reason: without the pin nothing is hooked and the figure is drawn as before).
 *
 * The figures (tools/dev/native_gate_ui_replay.xml, cue Avatar) are simulation objects: every client has every
 * player's, its own included, 0.4 m behind where that player stands. A player saw its own all the same (playtest
 * of 17 Nov: another character in front of you, following you, while you walk backward out of your ship; in the
 * scripted pictures it fills the view inside the cockpit). X4 has no figure for its own player on foot at all.
 * U::NPC's draw entry (vtable 0x2b38a48 slot 693, 0x6bec90) queues the body (its call of
 * U::BodyClass::QueueRenderObjects 0x382a70 at 0x6bf405) and then goes on to the rest (its place for the views, its
 * children). The entry points at a wrapper that marks the thread while the one NPC whose ID is this client's own
 * figure's is drawn, and the body call points at a wrapper that queues nothing on a marked thread. Only the body is
 * left out: a first form skipped the whole routine, and the figure's place as the views have it stood still (the
 * test op foot_meet, which steers by it, never arrived; 17 Nov). The probe tells the ID when the cue has made the
 * figure (gate.foot_figure_own). Nothing of the simulation is read or written: the figure is where it was on every
 * client, the other clients draw it.
 * Status (PILOT_FOOT row): figure=installed/<own ID>/skipped. */
#define FFIG_NPC_VTABLE 0x2b38a48u
#define FFIG_DRAW_SLOT 693u
#define FFIG_DRAW 0x6bec90u
#define FFIG_DRAW_SHA "81786975fb288cff97eaf4bdf0ea0795867bf0d5bf4e2d0d664c5c6a6c470e82"   /* its first 0x40 bytes */
#define FFIG_BODY 0x382a70u
#define FFIG_BODY_SITE 0x6bf405u          /* the NPC routine's call of it */
#define FFIG_BODY_STUB 0xf60u             /* on the slots' near page, after the transporter's (0xf40) */
typedef void (*ffig_draw_fn)(void *,void *,unsigned char,uint32_t,uint32_t,void *);
typedef void (*ffig_body_fn)(void *,void *,unsigned char,float,uint64_t,uint64_t,uint64_t,uint64_t);
static ffig_draw_fn ffig_native;          /* the image's, a test's */
static ffig_body_fn ffig_body_native;
static volatile LONG64 ffig_own_id,ffig_skips;
static int ffig_installed;
static unsigned char ffig_site_patched[5];
static __thread int ffig_hiding;          /* this thread draws this client's own figure */
static void ffig_draw(void *self,void *view,unsigned char a,uint32_t b,uint32_t c,void *d) {
    const uint64_t own=(uint64_t)InterlockedCompareExchange64(&ffig_own_id,0,0);uint64_t id=0;
    const int mine=own && self && read_local((unsigned char*)self+8,&id,8) && id==own;
    const int was=ffig_hiding;
    if(mine) ffig_hiding=1;
    ffig_native(self,view,a,b,c,d);
    ffig_hiding=was;
}
/* The body's queueing, as the NPC routine calls it (the fourth argument is a float in xmm3, the rest as passed). */
static void ffig_body(void *body,void *view,unsigned char a,float b,uint64_t c,uint64_t d,uint64_t e,uint64_t f) {
    if(ffig_hiding) {InterlockedIncrement64(&ffig_skips);return;}
    ffig_body_native(body,view,a,b,c,d,e,f);
}
/* This client's own figure (0: none). The entry is ours from the first call on (the frame thread). */
static int ffig_own(uint64_t id) {
    if(!ffig_installed) {
        if(!hash_ok(image+FFIG_DRAW,0x40,FFIG_DRAW_SHA)) return 0;
        if(!pnat_base || (!pslot_page && !(pslot_page=pslot_alloc_page()))) return 0;
        ffig_native=(ffig_draw_fn)(image+FFIG_DRAW);ffig_body_native=(ffig_body_fn)(image+FFIG_BODY);
        if(!pres_site_install(FFIG_BODY_SITE,FFIG_BODY,FFIG_BODY_STUB,(void*)ffig_body,ffig_site_patched)) return 0;
        if(!script_rng_slot_swap(FFIG_NPC_VTABLE+FFIG_DRAW_SLOT*8u,image+FFIG_DRAW,(void*)ffig_draw)) {
            (void)pres_site_uninstall(FFIG_BODY_SITE,FFIG_BODY,ffig_site_patched);return 0;
        }
        ffig_installed=1;
    }
    InterlockedExchange64(&ffig_own_id,(LONG64)id);
    return 1;
}
static void ffig_restore(void) {
    InterlockedExchange64(&ffig_own_id,0);
    if(ffig_installed && script_rng_slot_swap(FFIG_NPC_VTABLE+FFIG_DRAW_SLOT*8u,(void*)ffig_draw,image+FFIG_DRAW) &&
       pres_site_uninstall(FFIG_BODY_SITE,FFIG_BODY,ffig_site_patched)) ffig_installed=0;
}
static int ffig_status(char *out,size_t capacity) {
    int n=snprintf(out,capacity,"figure=%d/%llx/%lld",ffig_installed,(unsigned long long)ffig_own_id,(long long)ffig_skips);
    return n>0 && (size_t)n<capacity?n:0;
}
