/* A ship that protects a player's own ship (a story wingman: the Terran cadet's wingmate, X4's ProtectShip order on that
 * player's ship) is drawn in the player's own colour on that player's own client (native_gate_escort_present, a session
 * default; Windows gate; presentation only, client local).
 *
 * The user's requirement (25 Nov): the cadet start's wingman shows green, as an ally, on its player's screen, for every
 * player alike. X4 9.00 colours a target green only when the player faction owns it: the HUD's target system and the
 * target monitor ask GetRelationStatus4 / GetRelationStatus3 (and GetRelationStatus), whose "own property" status 5 is
 * 0x8c71e0 (the owner is the player faction and it is not the player's own ship, 0x3e88d0); the map's and the HUD radar's
 * icons (U::UniverseMapBehavior, and U::RadarBehavior built on it) take their colour in 0xdb6770, which asks 0x3e88d0
 * itself at two calls. The wingmate is the Terran faction's ship, so it was blue for every player (player 1 included:
 * runs C and D of 24 Nov, the stock scripts on X4's own player).
 *
 * Here, for the ships of this client's escort set only, both answers are "own property":
 *   - the entry of 0x8c71e0 goes through a stub on this module's own near page (its first instruction on a trampoline);
 *   - the two calls of 0x3e88d0 in 0xdb6770 call a stub on that page instead (which calls 0x3e88d0 for every other object).
 * Nothing else asks 0x8c71e0 (its three callers are the three UI exports), and 0xdb6770 draws; the simulation's owner,
 * relations and every other test of "the player's own" stay X4's (no set_owner, no script sees a change).
 * The set is this client's own player's: the probe reads the ships that protect that player's ship (the list the
 * ProtectShip order keeps on the player's entity, tools/dev/native_gate_player_escort.xml, $X4LAN_Escorts) every half
 * second and hands their IDs here (gate.escort_present_set). A component is matched by its ID at +8. */
#define ESC_REL 0x8c71e0u                /* the UI's "own property" test of a component */
#define ESC_REL_SIZE 0x100u
#define ESC_REL_SHA "66467379c0487224c5fe4a1973e1478d83ced9741010cc13b5e71e243fecfed6"
static const unsigned char esc_rel_head[5]={0x48,0x89,0x5c,0x24,0x20};   /* mov [rsp+0x20],rbx */
static const uint32_t esc_rel_callers[3]={0xafe421u,0xb06517u,0xb068ceu};   /* GetRelationStatus, 3, 4 */
#define ESC_MAP 0xdb6770u                /* UniverseMapBehavior: an object's icon colour (the map, the HUD radar) */
#define ESC_MAP_SIZE 0x140u
#define ESC_MAP_SHA "406a035b7327c53516608ea71652af39a04acbaafb32260649c82372fc101150"
#define ESC_OWNED 0x3e88d0u              /* the player faction owns it and it is not the player's own ship */
static const uint32_t esc_map_sites[2]={0xdb6823u,0xdb6865u};   /* call 0x3e88d0, rcx = the object (mov rcx,rbx) */
#define ESC_SITES 2u
#define ESC_REL_STUB 0x00u               /* on this module's own near page */
#define ESC_REL_TRAMPOLINE 0x20u         /* mov [rsp+0x20],rbx; then on at +5 */
#define ESC_MAP_STUB 0x40u
#define ESC_MAX 16u
typedef unsigned char (*esc_fn)(unsigned char *component);
static unsigned char *esc_page;
static int esc_armed,esc_rel_installed,esc_map_installed[ESC_SITES];
static unsigned char esc_rel_patched[5],esc_map_original[ESC_SITES][5],esc_map_patched[ESC_SITES][5];
static volatile uint64_t esc_ids[ESC_MAX];
static volatile LONG esc_count;
static volatile LONG64 esc_rel_hits,esc_map_hits,esc_sets;
static const char *esc_why="-";
static esc_fn esc_rel_test,esc_owned_test;   /* tests: X4's two functions */

/* One of this client's escorts (the ship's ID at +8; X4 hands both functions a live component). */
static int esc_has(const unsigned char *component) {
    LONG n=esc_count;uint64_t id;
    if(n<=0 || !component) return 0;
    memcpy(&id,component+8,8);
    for(LONG i=0;i<n && i<(LONG)ESC_MAX;i++) if(esc_ids[i]==id) return 1;
    return 0;
}
static __attribute__((noinline)) unsigned char esc_rel_own(unsigned char *component) {
    if(esc_has(component)) {InterlockedIncrement64(&esc_rel_hits);return 1;}
    return (esc_rel_test?esc_rel_test:(esc_fn)(void*)(esc_page+ESC_REL_TRAMPOLINE))(component);
}
static __attribute__((noinline)) unsigned char esc_map_own(unsigned char *component) {
    if(esc_has(component)) {InterlockedIncrement64(&esc_map_hits);return 1;}
    return (esc_owned_test?esc_owned_test:(esc_fn)(void*)(pnat_base+ESC_OWNED))(component);
}
/* This client's set, whole (a new list replaces the old; 0 IDs clears it). The hooks may read it from another thread: the
 * count goes to 0 first and to the new length last. 1 taken, 0 refused (too many). */
static int esc_set(const uint64_t *ids,unsigned n) {
    if(n>ESC_MAX) return 0;
    InterlockedExchange(&esc_count,0);
    for(unsigned i=0;i<n;i++) esc_ids[i]=ids[i];
    InterlockedExchange(&esc_count,(LONG)n);InterlockedIncrement64(&esc_sets);
    return 1;
}
/* The pinned bytes of this build: the test's first 0x100 bytes and its head, the map colour's first 0x140 bytes with its
 * two calls of 0x3e88d0, and the three exports that call the test. */
static int esc_pins(void) {
    if(!pnat_base || !hash_ok(pnat_base+ESC_REL,ESC_REL_SIZE,ESC_REL_SHA) || memcmp(pnat_base+ESC_REL,esc_rel_head,sizeof(esc_rel_head))) return 0;
    if(!hash_ok(pnat_base+ESC_MAP,ESC_MAP_SIZE,ESC_MAP_SHA)) return 0;
    for(unsigned i=0;i<ESC_SITES;i++) {
        int32_t rel;memcpy(&rel,pnat_base+esc_map_sites[i]+1,4);
        if(pnat_base[esc_map_sites[i]]!=0xe8 || esc_map_sites[i]+5u+(uint32_t)rel!=ESC_OWNED || ((esc_map_sites[i]+4)&~15u)!=(esc_map_sites[i]&~15u)) return 0;
    }
    for(unsigned i=0;i<3;i++) {
        int32_t rel;memcpy(&rel,pnat_base+esc_rel_callers[i]+1,4);
        if(pnat_base[esc_rel_callers[i]]!=0xe8 || esc_rel_callers[i]+5u+(uint32_t)rel!=ESC_REL) return 0;
    }
    return ((ESC_REL+4)&~15u)==(ESC_REL&~15u);
}
static void esc_restore(void) {
    if(esc_rel_installed && pview_cas(ESC_REL,esc_rel_patched,esc_rel_head,5)) esc_rel_installed=0;
    for(unsigned i=0;i<ESC_SITES;i++)
        if(esc_map_installed[i] && pview_cas(esc_map_sites[i],esc_map_patched[i],esc_map_original[i],5)) esc_map_installed[i]=0;
    if(!esc_rel_installed && !esc_map_installed[0] && !esc_map_installed[1]) esc_armed=0;
}
static int esc_arm(void) {
    DWORD old,ignored;int32_t rel;
    if(esc_armed) return 1;
    if(!esc_pins()) {esc_why="pins";return 0;}
    if(!esc_page && !(esc_page=pslot_alloc_page())) {esc_why="page";return 0;}
    if(!VirtualProtect(esc_page,PVIEW_PAGE,PAGE_READWRITE,&old)) {esc_why="protect";return 0;}
    memset(esc_page,0xcc,0x60);
    pview_abs_jump(esc_page+ESC_REL_STUB,(void*)esc_rel_own);
    memcpy(esc_page+ESC_REL_TRAMPOLINE,esc_rel_head,sizeof(esc_rel_head));
    pview_abs_jump(esc_page+ESC_REL_TRAMPOLINE+sizeof(esc_rel_head),pnat_base+ESC_REL+sizeof(esc_rel_head));
    pview_abs_jump(esc_page+ESC_MAP_STUB,(void*)esc_map_own);
    if(!VirtualProtect(esc_page,PVIEW_PAGE,PAGE_EXECUTE_READ,&ignored)) {esc_why="protect";return 0;}
    FlushInstructionCache(GetCurrentProcess(),esc_page,PVIEW_PAGE);
    esc_rel_hits=esc_map_hits=esc_sets=0;esc_why="-";
    rel=(int32_t)((intptr_t)(esc_page+ESC_REL_STUB)-(intptr_t)(pnat_base+ESC_REL+5));
    esc_rel_patched[0]=0xe9;memcpy(esc_rel_patched+1,&rel,4);
    if(!pview_cas(ESC_REL,esc_rel_head,esc_rel_patched,5)) {esc_why="rel_entry";return 0;}
    esc_rel_installed=1;
    for(unsigned i=0;i<ESC_SITES;i++) {
        memcpy(esc_map_original[i],pnat_base+esc_map_sites[i],5);
        rel=(int32_t)((intptr_t)(esc_page+ESC_MAP_STUB)-(intptr_t)(pnat_base+esc_map_sites[i]+5));
        esc_map_patched[i][0]=0xe8;memcpy(esc_map_patched[i]+1,&rel,4);
        if(!pview_cas(esc_map_sites[i],esc_map_original[i],esc_map_patched[i],5)) {esc_why="map_call";esc_restore();return 0;}
        esc_map_installed[i]=1;
    }
    esc_armed=1;
    return 1;
}
static int esc_status(char *out,size_t capacity) {
    int n=snprintf(out,capacity,"armed=%d set=%ld sets=%lld rel=%lld map=%lld why=%s",esc_armed,(long)esc_count,(long long)esc_sets,
        (long long)esc_rel_hits,(long long)esc_map_hits,esc_why);
    return n>0 && (size_t)n<capacity?n:0;
}
