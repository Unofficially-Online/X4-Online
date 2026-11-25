/* ---- The HUD's objects around a player 2..N on its own client (native_gate_pilot_present_hud_zone, a session default) ----
 * Report (rk9, 24 Nov, live, player 2 of a game continued from a save, x4ls host --milestone): player 2 could not target
 * ships, and the blue boxes were missing around nearby objects but shown on gates and on some objects such as the
 * mission's target ship. rk8 (a fresh session) targeted ships fine.
 *
 * What X4 does [F, X4.exe 9.00, read in the image]. The HUD's target elements are UI messages (UI::ICom::Message) that
 * the UI's message manager (CMessageManager, [0x6cf2788]; UI::CUIController::Init copies it to 0x38f4c38, where its
 * sources read it) makes from its sources. The manager keeps "the player" at +0x60 (set through 0xaea500, which tells
 * every source: their vtable slot 570 with the old player) and its sources in a map: header node at +0x20, the first
 * node at +0x40 (a node: +0 parent, +8 left, +0x10 right, the sources as a vector at +0x28..+0x30).
 * The sources of the objects in space are UI::ICom::AdjacentZoneScanner's three subclasses (BasicMessageSource,
 * FastObjectSource, PlayerCapShipSource; vtables 0x2bec628, 0x2be59a0, 0x2be7d78, each with the scanner's slot 570
 * 0xad8dc0 and 569 0xad8f00). Slot 569 (once, +0x18 the flag) runs slot 570; slot 570 moves the scanner's listeners of
 * changedzone (0x57) and changedhighwaysection (0x4b) from the old player to the manager's player and runs the rescan
 * 0xad8f30 with the thread's time (TLS +0x300). The rescan walks from the manager's player up the parents (+0x70) to
 * the first zone (IsOfClass 0x6d, vtable +0x11e8): ONE zone, kept at the scanner's +0x28; it drops the old zone and its
 * adjacent zones (zone +0xc0, kind 6) from the scanner, listens for tempzoneadded / tempzoneremoved on the new zone's
 * sector, and adds the new zone and its adjacent zones: every object in them gets its message (0xada4b0 -> 0xada9f0 ->
 * 0xada7a0), the HUD shows a target element for it while its conditions hold. The changedzone and changedhighwaysection
 * handlers (slots 114 and 118, 0xad92e0) rescan the same way. Gates and mission objectives come from other sources
 * (the points of interest, UI::ICom::MissionObjectiveScanner), which do not ask the player's zone.
 *
 * What went wrong. The manager's player is X4's own player on every client (lockstep_pilot_foot.h pfoot_k1_manager puts
 * it back every pass), and the rescan runs from X4's events on that player, outside any presentation scope: on client
 * k the scanners held slot 1's zone (and its neighbours), never slot k's. Ships around player 2 had no message, so no
 * box and nothing to target; gates and the mission's own ship kept theirs. rk8's fresh session started both players
 * 300 m apart in one zone, so player 2 saw its own surroundings; the continue put slot 1 in zone004 and slot 2 in
 * zone005 of the same sector (the save: X4's player in its ship there, slot 2's ship as saved), and player 2 flew on
 * into another system.
 *
 * Here: on client k (k = 2..N, this client's own slot), every UI frame inside its presentation scope (lockstep_pilot_
 * present.h pres_ui_move, after the scope opens, before X4's UI move): the zone of X4's player there (its parent is
 * slot k's room in the scope, so X4's own walk finds slot k's zone) is compared with each scanner's +0x28; a scanner
 * that holds another zone is rescanned by X4's own slot 570 with the manager's player as the old one (no listener
 * moves: the rescan alone). A rescan X4 makes outside the scope (slot 1's zone change) is put right at the next UI
 * frame. Only a scanner X4 has started (+0x18) and only when the manager's player is X4's own; the scanners are
 * collected first, then rescanned. The scope routes every runtime ID the rescan takes to the UI's local list
 * (lockstep_runtime_ids.h, pres_open), the thread's simulation RNG word is held around each call (pres_rng_hold),
 * and nothing of the simulation changes: the messages, their listeners and the HUD are this client's own.
 * A fault in X4's rescan ends this module on this client (faults=, the HUD as before), not the session.
 * Not covered: a highway section change inside one zone (X4 rescans at changedhighwaysection; the zone compared here
 * stays the same).
 * Pins (refuse the arming, the probe asserts): 0xad8dc0, 0xad8f30, 0xad8f00, 0xaea500, the four vtables' slot 570.
 * Status in the PILOT_INPUTS present row: hud_zone=on/passes/rescans/scanners/faults/other/nozone[/zone ids before>after
 * of the last rescan]. */
#define PHZ_MANAGER 0x38f4c38u           /* the message manager as its sources read it */
#define PHZ_MANAGER_PLAYER 0x60u
#define PHZ_MAP_HEADER 0x20u             /* the manager: its sources' map, the header node */
#define PHZ_MAP_FIRST 0x40u              /* the manager: the first node (header +0x20) */
#define PHZ_NODE_PARENT 0x0u
#define PHZ_NODE_LEFT 0x8u
#define PHZ_NODE_RIGHT 0x10u
#define PHZ_NODE_BEGIN 0x28u             /* a node: its sources, begin and end */
#define PHZ_NODE_END 0x30u
#define PHZ_SCAN_SLOT 570u               /* a source's vtable +0x11d0: re-anchor and rescan */
#define PHZ_SCAN 0xad8dc0u               /* UI::ICom::AdjacentZoneScanner's slot 570 */
#define PHZ_STARTED 0x18u                /* a scanner: slot 569 ran (its flag) */
#define PHZ_ZONE 0x28u                   /* a scanner: the zone it holds */
#define PHZ_IS_CLASS 0x11e8u             /* a component's vtable: IsOfClass(int) */
#define PHZ_ZONE_CLASS 0x6d
#define PHZ_PARENT 0x70u
#define PHZ_ID 0x8u                      /* a component: its runtime ID */
#define PHZ_DEPTH 24u
#define PHZ_NODES 512u
#define PHZ_SCANNERS 64u
static const struct {unsigned rva,size;const char *sha;} phz_pin_table[]={
    {0xad8dc0u,0x133u,"47b31ca32fab76d84f60e69c0a892ecf9e39d67c34e450534884f901dd338a4e"},   /* slot 570: the listeners, the rescan */
    {0xad8f30u,0x3aeu,"5f63d879e13c6ec737818503f54bbc8bee9862463179a7b878f35dd4c937f4c0"},   /* the rescan: the player's zone, +0x28 */
    {0xad8f00u,0x26u,"a26b6d913dff927c768d742f7e87f9ae5ba3bd2de03a655778725f9c314be787"},    /* slot 569: +0x18, then slot 570 */
    {0xaea500u,0x151u,"9b71ed71191fdc44740aeda13c9378871b317e07fca72bdcccba4fd738a9557d"}};  /* the player setter: the map, slot 570 */
#define PHZ_PIN_COUNT (sizeof(phz_pin_table)/sizeof(*phz_pin_table))
/* AdjacentZoneScanner, BasicMessageSource, FastObjectSource, PlayerCapShipSource: slot 570 is the scanner's. */
static const uint32_t phz_vtables[]={0x2beea08u,0x2bec628u,0x2be59a0u,0x2be7d78u};
#define PHZ_VTABLES (sizeof(phz_vtables)/sizeof(*phz_vtables))
static int phz_on,phz_asked;             /* armed on a client of slot 2..N; asked (gate.pilot_present_hud_zone) */
static unsigned char **phz_manager_at;   /* where the manager is read (image+PHZ_MANAGER, a test's) */
static const void *phz_scan;             /* the scanner's slot 570 (image+PHZ_SCAN, a test's) */
static unsigned phz_last_scanners;
static uint64_t phz_last_from,phz_last_to;
static volatile LONG64 phz_passes,phz_rescans,phz_faults,phz_other,phz_nozone,phz_unreadable;
static int phz_pins(void) {
    for(unsigned i=0;i<PHZ_PIN_COUNT;i++) if(!hash_ok(image+phz_pin_table[i].rva,phz_pin_table[i].size,phz_pin_table[i].sha)) {pin_failed_rva=phz_pin_table[i].rva;return 0;}
    for(unsigned i=0;i<PHZ_VTABLES;i++) if(((void *const*)(image+phz_vtables[i]))[PHZ_SCAN_SLOT]!=(void*)(image+PHZ_SCAN)) {pin_failed_rva=phz_vtables[i];return 0;}
    phz_manager_at=(unsigned char**)(image+PHZ_MANAGER);phz_scan=image+PHZ_SCAN;
    return 1;
}
typedef unsigned char (*phz_is_class_fn)(void *component,int klass);
typedef void (*phz_scan_fn)(void *source,void *old_player);
/* X4's walk in the rescan: the first zone among p and its parents; NULL none or unreadable. */
static unsigned char *phz_zone_of(unsigned char *p) {
    for(unsigned depth=0;p && depth<PHZ_DEPTH;depth++) {
        unsigned char *vtable=NULL;phz_is_class_fn is_a=NULL;
        if(!read_local(p,&vtable,8) || !vtable || !read_local(vtable+PHZ_IS_CLASS,&is_a,8) || !is_a) return NULL;
        if(is_a(p,PHZ_ZONE_CLASS)) return p;
        if(!read_local(p+PHZ_PARENT,&p,8)) return NULL;
    }
    return NULL;
}
/* The next node of the map, as 0xaea500 walks it; NULL unreadable. */
static unsigned char *phz_next(unsigned char *node) {
    unsigned char *at=NULL,*up=NULL,*left=NULL;
    if(!read_local(node+PHZ_NODE_RIGHT,&at,8)) return NULL;
    if(at) {
        for(unsigned depth=0;depth<PHZ_DEPTH;depth++) {
            if(!read_local(at+PHZ_NODE_LEFT,&left,8)) return NULL;
            if(!left) return at;
            at=left;
        }
        return NULL;
    }
    if(!read_local(node+PHZ_NODE_PARENT,&up,8) || !up) return NULL;
    for(unsigned depth=0;depth<PHZ_DEPTH;depth++) {
        if(!read_local(up+PHZ_NODE_LEFT,&left,8)) return NULL;
        if(left==node) return up;
        node=up;
        if(!read_local(node+PHZ_NODE_PARENT,&up,8) || !up) return NULL;
    }
    return NULL;
}
static uint64_t phz_id(const unsigned char *component) {uint64_t id=0;if(component) read_local(component+PHZ_ID,&id,8);return id;}
/* Every UI frame in this client's own presentation scope (pres_ui_move). */
static void phz_scope_pass(void) {
    unsigned char *manager=NULL,*player=NULL,*zone=NULL,*node=NULL,*scanners[PHZ_SCANNERS],*from[PHZ_SCANNERS];
    unsigned found=0,count=0;
    if(!phz_on || !phz_manager_at || !phz_scan || !InterlockedCompareExchange(&pres_open,0,0)) return;
    InterlockedIncrement64(&phz_passes);
    if(!read_local(phz_manager_at,&manager,8) || !manager || !read_local(manager+PHZ_MANAGER_PLAYER,&player,8)) {InterlockedIncrement64(&phz_unreadable);return;}
    if(!player || player!=pslot_p1) {InterlockedIncrement64(&phz_other);return;}   /* not X4's own player: left as it is */
    int fault=0;
    __try {zone=phz_zone_of(player);}
    __except(GetExceptionCode()==EXCEPTION_ACCESS_VIOLATION?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH) {fault=1;}
    if(fault) {phz_on=0;InterlockedIncrement64(&phz_faults);return;}
    if(!zone) {InterlockedIncrement64(&phz_nozone);return;}
    if(!read_local(manager+PHZ_MAP_FIRST,&node,8)) {InterlockedIncrement64(&phz_unreadable);return;}
    for(unsigned n=0;node && node!=manager+PHZ_MAP_HEADER;n++) {
        unsigned char *begin=NULL,*end=NULL;
        if(n>=PHZ_NODES || !read_local(node+PHZ_NODE_BEGIN,&begin,8) || !read_local(node+PHZ_NODE_END,&end,8) || end<begin ||
           (size_t)(end-begin)>8u*4096u) {InterlockedIncrement64(&phz_unreadable);return;}
        for(unsigned char *at=begin;at<end;at+=8) {
            unsigned char *source=NULL,*vtable=NULL,*held=NULL,started=0;const void *scan=NULL;
            if(!read_local(at,&source,8) || !source || !read_local(source,&vtable,8) || !vtable ||
               !read_local(vtable+8u*PHZ_SCAN_SLOT,&scan,8) || scan!=phz_scan) continue;
            found++;
            if(!read_local(source+PHZ_STARTED,&started,1) || !started || !read_local(source+PHZ_ZONE,&held,8) || held==zone) continue;
            if(count<PHZ_SCANNERS) {scanners[count]=source;from[count]=held;count++;}
        }
        node=phz_next(node);
    }
    phz_last_scanners=found;
    for(unsigned i=0;i<count;i++) {
        uint64_t word=0,*rng=pres_rng_hold(&word);
        __try {((phz_scan_fn)phz_scan)(scanners[i],player);}
        __except(GetExceptionCode()==EXCEPTION_ACCESS_VIOLATION?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH) {fault=1;}
        pres_rng_back(rng,word);
        if(fault) {phz_on=0;InterlockedIncrement64(&phz_faults);return;}
        InterlockedIncrement64(&phz_rescans);
        phz_last_from=phz_id(from[i]);phz_last_to=phz_id(zone);
    }
}
static void phz_reset(void) {phz_passes=phz_rescans=phz_faults=phz_other=phz_nozone=phz_unreadable=0;phz_last_scanners=0;phz_last_from=phz_last_to=0;}
static int phz_status(char *out,size_t capacity) {
    int n=snprintf(out,capacity," hud_zone=%d/%lld/%lld/%u/%lld/%lld/%lld",phz_on,(long long)phz_passes,(long long)phz_rescans,phz_last_scanners,
                   (long long)phz_faults,(long long)phz_other,(long long)phz_nozone);
    if(n>0 && (size_t)n<capacity && phz_unreadable) n+=snprintf(out+n,capacity-(size_t)n,"/u%lld",(long long)phz_unreadable);
    if(n>0 && (size_t)n<capacity && phz_rescans) n+=snprintf(out+n,capacity-(size_t)n,"/%llx>%llx",(unsigned long long)phz_last_from,(unsigned long long)phz_last_to);
    return n>0 && (size_t)n<capacity?n:0;
}
