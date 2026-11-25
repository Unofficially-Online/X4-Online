/* The HUD weapon panel of every player as X4 shows player 1's (native_gate_hud_weapons, a session default; stop 248).
 * User report (rk15, 25 Nov, player 2, a fighter in a game continued from a save): the weapon panel's selected weapons and
 * their bars were grey where stock (and the user's memory of earlier releases) has them blue.
 *
 * What X4 does [F, X4.exe 9.00, read in the image]. The panel (ui/core/lua/crosshair handling.lua, updateWeaponPanel)
 * colours a weapon's icon, its reload or heat bar and its clip bar by GetWeaponDetails3's active flag (with the damage
 * state, the ship's stance block IsObjectShootingDisabledByStanceOnly, the heat and the reload): active is the weapon
 * colour (azure) and its bars' active colours, inactive grey (crosshair_weapon_inactive, grey_64) and the bars' inactive
 * ones. GetWeaponDetails3 (0xb08750) takes the weapon of X4's player's occupied ship (the universe's player +0x230, up its
 * +0x70 parents to the first of class 0x75) and its active flag from 0x81d530 (primary) or 0x81d590 (secondary): the
 * weapon's shoot controller (+0x318) is a PlayerBombLauncherShootController (class 5: primary yes), or a
 * PlayerShootController (class 6) with its primary flag +0xa1 or its secondary flag +0xa2. X4 gives those to the weapons
 * of a ship a player pilots when they come into its active group (0x4e62b0: Weapon slots 999/1000, 0x81a6e0/0x81a760:
 * PlayerShootController(primary, secondary) through Launcher::SetShootController) and takes the flag or the controller
 * away when they leave it (0x4e6030: 0x81a7e0/0x81a8b0). So in stock the panel's weapon is active exactly when it is in
 * its ship's active primary group (ship +0x638 groups, +0x668 the active one; 0x4e4800) or secondary group (+0x650,
 * +0x66c; 0x4e4850). GetPrimaryWeapon, GetSecondaryWeapon, GetWeaponDetails and GetWeaponDetails2 read the same two tests.
 *
 * What went wrong. In a session every player's guns of the active primary group carry the gate's MindlessShootController
 * (lockstep_pilot_fire.h, the lease: class 3, X4's shooting from the replayed trigger on every client), and the other
 * weapons none or the missiles' BasicShootController while firing (lockstep_pilot_fire.h): neither class 5 nor 6, so the
 * panel showed every weapon inactive on every client, the selected ones too. The simulation is right (the lease is what
 * fires the guns the same on every client); only the panel's question went to the controller.
 *
 * Here, presentation only, on every client for the player its HUD presents (in client k's presentation scope X4's player
 * is slot k's, lockstep_pilot_present.h): the eight calls of the two tests in the five UI readers go through a trampoline
 * on this module's page near the image (jmp to hudw_primary or hudw_secondary). Each calls X4's test as it is and keeps a
 * yes. A no for a weapon whose controller is not X4's PlayerShootController (whose flags are X4's own answer) becomes X4's
 * group rule: yes when the weapon's ID is in the active primary (secondary) group of X4's player's occupied ship, found as
 * the readers find it. On the UI thread only; any other caller and every other read of the controllers keep X4's answer,
 * nothing is written, the simulation never asks these readers.
 * Pins (refuse the arming): the two tests, the readers' ship walk in GetWeaponDetails3, the group getters 0x4e4800 and
 * 0x4e4850, and each site's call as X4's. The sites are checked at every gate tick (stop 248, why site_lost); the stop
 * puts X4's calls back. Status HUD_WEAPONS. Not covered: the Linux gate (no twin: the panel there stays as before). */
#define HUDW_STOP_REASON 248u
#define HUDW_PRIMARY 0x81d530u          /* the weapon's primary active test: controller class 5, or class 6 with +0xa1 */
#define HUDW_SECONDARY 0x81d590u        /* the secondary one: class 6 with +0xa2 */
#define HUDW_CONTROLLER 0x318u          /* the weapon's shoot controller */
#define HUDW_PLAYER_VTABLE 0x2b7d1a8u   /* U::PlayerShootController: its flags are X4's own answer */
#define HUDW_ID 0x08u                   /* a component's UniverseID */
#define HUDW_GROUPS 0x638u              /* the ship's primary groups (vectors of UniverseIDs, 0x18 each) */
#define HUDW_ACTIVE 0x668u              /* ... the active one (int32, < 0 none) */
#define HUDW_GROUPS2 0x650u             /* the secondary groups */
#define HUDW_ACTIVE2 0x66cu
#define HUDW_UNIVERSE 0x3d198f8u        /* g_Universe: +0x230 X4's player */
#define HUDW_PLAYER 0x230u
#define HUDW_PARENT 0x70u
#define HUDW_IS_CLASS 0x11e8u           /* component vtable: of class? (this, class) */
#define HUDW_SHIP_CLASS 0x75u           /* the readers' occupied ship: the player's first parent of this class */
#define HUDW_DEPTH 16u
#define HUDW_PAGE 0x1000u
#define HUDW_SITES 8u
static const struct {unsigned rva,target;const char *reader;} hudw_sites[HUDW_SITES]={
    {0xb0607fu,HUDW_PRIMARY,"GetPrimaryWeapon"},{0xb06c9fu,HUDW_SECONDARY,"GetSecondaryWeapon"},
    {0xb07fd9u,HUDW_SECONDARY,"GetWeaponDetails"},{0xb07fe0u,HUDW_PRIMARY,"GetWeaponDetails"},
    {0xb08479u,HUDW_SECONDARY,"GetWeaponDetails2"},{0xb08480u,HUDW_PRIMARY,"GetWeaponDetails2"},
    {0xb08949u,HUDW_SECONDARY,"GetWeaponDetails3"},{0xb08950u,HUDW_PRIMARY,"GetWeaponDetails3"}};
static const struct {unsigned rva,size;const char *sha;} hudw_pins_list[]={
    {HUDW_PRIMARY,0x5fu,"369b33693045fe9914da4c91804955073a2725acb7ae048b952e685803fb9e8d"},
    {HUDW_SECONDARY,0x44u,"d47bf016074ff9fb79ca3549380b57e2c6a02f19d0f2d5d04ccbe245b716e7dd"},
    {0xb087c4u,0x5du,"274caa843980f5b1508d3f9faa78f2ea81f1d7d25962ccf73d2ff63b0a3bbcdb"},   /* GetWeaponDetails3: the ship walk */
    {0x4e4800u,0x49u,"65fe5f9eeae964f687317af978ed99b3cf38d0d012abdfe1f213513b5f7a3e1c"},   /* the active primary group */
    {0x4e4850u,0x49u,"32ca6f8c65b4f8e6b63335ef6847647e672a4689d4a9872812de0ce0f0cc0338"}};  /* the active secondary group */
#define HUDW_PIN_COUNT (sizeof(hudw_pins_list)/sizeof(*hudw_pins_list))
typedef unsigned char (*hudw_test_fn)(void *);
static int hudw_armed,hudw_installed;
static const char *hudw_why="-",*hudw_check_why="-";
static unsigned char *hudw_page;
static unsigned char hudw_patched[HUDW_SITES][5];
static struct {hudw_test_fn primary,secondary;} hudw_fn;   /* X4's (hudw_pins), or a test's */
static unsigned char *(*hudw_ship_test)(void);              /* tests: in place of the universe's player and its walk */
static int hudw_thread_any;                                  /* tests: any thread asks */
static volatile LONG64 hudw_asks,hudw_yes,hudw_own,hudw_answered[2],hudw_unlisted,hudw_no_ship,hudw_elsewhere,
    hudw_slots[PILOT_SLOTS+1];

static unsigned char *hudw_load(const unsigned char *at) {unsigned char *p=NULL;return at && read_local(at,&p,8)?p:NULL;}
/* X4's player's occupied ship as the readers find it: the universe's player, up its parents to the first of class 0x75. */
static unsigned char *hudw_player_ship(void) {
    if(hudw_ship_test) return hudw_ship_test();
    unsigned char *universe=hudw_load(image+HUDW_UNIVERSE),*player=universe?hudw_load(universe+HUDW_PLAYER):NULL;
    unsigned char *p=player?hudw_load(player+HUDW_PARENT):NULL;
    for(unsigned depth=0;p && depth<HUDW_DEPTH;depth++) {
        unsigned char *vtable=hudw_load(p),*of_class=vtable?hudw_load(vtable+HUDW_IS_CLASS):NULL;
        if(!of_class) return NULL;
        if(((unsigned char(*)(void *,unsigned))(void*)of_class)(p,HUDW_SHIP_CLASS)) return p;
        unsigned char *up=hudw_load(p+HUDW_PARENT);
        if(up==p) return NULL;
        p=up;
    }
    return NULL;
}
/* Is the ID in the ship's active group (0x4e4800 / 0x4e4850's vector)? */
static int hudw_listed(const unsigned char *ship,unsigned groups_at,unsigned active_at,uint64_t id) {
    unsigned char *groups[2]={NULL,NULL},*ids[2]={NULL,NULL};int32_t active=-1;
    if(!id || !ship || !read_local(ship+groups_at,groups,16) || !read_local(ship+active_at,&active,4) || active<0 || !groups[0] ||
       groups[1]<groups[0] || (size_t)(groups[1]-groups[0])%0x18u || (size_t)active>=(size_t)(groups[1]-groups[0])/0x18u ||
       !read_local(groups[0]+(size_t)active*0x18u,ids,16) || !ids[0] || ids[1]<ids[0] || (size_t)(ids[1]-ids[0])%8u) return 0;
    for(unsigned char *at=ids[0];at<ids[1];at+=8) {
        uint64_t listed=0;
        if(read_local(at,&listed,8) && listed==id) return 1;
    }
    return 0;
}
/* X4's answer x4 for the weapon, or X4's group rule for a no under another controller than X4's player one. */
static unsigned char hudw_rule(unsigned char *weapon,int secondary,unsigned char x4) {
    InterlockedIncrement64(&hudw_asks);
    if(x4) {InterlockedIncrement64(&hudw_yes);return x4;}
    if(!hudw_installed || !weapon) return x4;
    if(!hudw_thread_any && GetCurrentThreadId()!=ui_thread) {InterlockedIncrement64(&hudw_elsewhere);return x4;}
    unsigned char *controller=hudw_load(weapon+HUDW_CONTROLLER),*vtable=controller?hudw_load(controller):NULL;
    if(vtable==image+HUDW_PLAYER_VTABLE) {InterlockedIncrement64(&hudw_own);return x4;}   /* X4's own flags */
    unsigned char *ship=hudw_player_ship();
    if(!ship) {InterlockedIncrement64(&hudw_no_ship);return x4;}
    uint64_t id=0;
    if(!read_local(weapon+HUDW_ID,&id,8) ||
       !hudw_listed(ship,secondary?HUDW_GROUPS2:HUDW_GROUPS,secondary?HUDW_ACTIVE2:HUDW_ACTIVE,id)) {InterlockedIncrement64(&hudw_unlisted);return x4;}
    InterlockedIncrement64(&hudw_answered[secondary!=0]);
    const unsigned own=pstk_capture_slot;
    InterlockedIncrement64(&hudw_slots[own<=PILOT_SLOTS?own:0]);
    return 1;
}
static unsigned char hudw_primary(void *weapon) {return hudw_rule((unsigned char*)weapon,0,hudw_fn.primary(weapon));}
static unsigned char hudw_secondary(void *weapon) {return hudw_rule((unsigned char*)weapon,1,hudw_fn.secondary(weapon));}
/* The page: two trampolines, jmp [rip]; abs64 (hudw_primary at +0, hudw_secondary at +0x20). */
static void hudw_write_trampolines(unsigned char *p) {
    static const unsigned char jmp[6]={0xff,0x25,0,0,0,0};void *t[2]={(void*)hudw_primary,(void*)hudw_secondary};
    for(unsigned i=0;i<2;i++) {memcpy(p+i*0x20u,jmp,6);memcpy(p+i*0x20u+6,&t[i],8);}
}
static unsigned char *hudw_trampoline(unsigned target) {return hudw_page+(target==HUDW_SECONDARY?0x20u:0u);}
static void hudw_call(unsigned rva,const unsigned char *to,unsigned char out[5]) {
    const int32_t rel=(int32_t)((intptr_t)to-(intptr_t)(image+rva+5));out[0]=0xe8;memcpy(out+1,&rel,4);
}
static int hudw_pins(void) {
    for(unsigned i=0;i<HUDW_PIN_COUNT;i++)
        if(!hash_ok(image+hudw_pins_list[i].rva,hudw_pins_list[i].size,hudw_pins_list[i].sha)) {pin_failed_rva=hudw_pins_list[i].rva;return 0;}
    for(unsigned i=0;i<HUDW_SITES;i++) {
        unsigned char want[5],now[5];hudw_call(hudw_sites[i].rva,image+hudw_sites[i].target,want);
        if(!read_local(image+hudw_sites[i].rva,now,5) || memcmp(now,want,5)) {pin_failed_rva=hudw_sites[i].rva;return 0;}
    }
    hudw_fn.primary=(hudw_test_fn)(void*)(image+HUDW_PRIMARY);hudw_fn.secondary=(hudw_test_fn)(void*)(image+HUDW_SECONDARY);
    return 1;
}
static unsigned char *hudw_alloc_page(void) {
    uintptr_t at=(uintptr_t)(image+hudw_sites[0].rva)&~(uintptr_t)65535;
    for(uintptr_t distance=65536;distance<0x70000000;distance+=65536)
        for(int sign=-1;sign<=1;sign+=2) {
            unsigned char *p=VirtualAlloc((void*)(sign<0?at-distance:at+distance),HUDW_PAGE,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
            if(!p) continue;
            int reach=1;
            for(unsigned i=0;i<HUDW_SITES;i++) {
                const intptr_t d=(intptr_t)p-(intptr_t)(image+hudw_sites[i].rva+5);
                if(!(d>INT32_MIN+0x10000 && d<INT32_MAX-0x10000)) reach=0;
            }
            if(reach) return p;
            VirtualFree(p,0,MEM_RELEASE);
        }
    return NULL;
}
static void hudw_uninstall(void);
/* The trampolines and the eight sites (CAS of each call). 0 refused: nothing left written. */
static int hudw_install(void) {
    if(hudw_installed) return 1;
    if(!hudw_page) hudw_page=hudw_alloc_page();
    if(!hudw_page) {hudw_why="page";return 0;}
    DWORD old,ignored;
    if(!VirtualProtect(hudw_page,HUDW_PAGE,PAGE_READWRITE,&old)) {hudw_why="page";return 0;}
    memset(hudw_page,0xcc,0x40);hudw_write_trampolines(hudw_page);
    if(!VirtualProtect(hudw_page,HUDW_PAGE,PAGE_EXECUTE_READ,&ignored)) {hudw_why="page";return 0;}
    FlushInstructionCache(GetCurrentProcess(),hudw_page,HUDW_PAGE);
    hudw_installed=1;   /* before the sites: a reader that reaches one already answers */
    for(unsigned i=0;i<HUDW_SITES;i++) {
        unsigned char original[5];hudw_call(hudw_sites[i].rva,image+hudw_sites[i].target,original);
        hudw_call(hudw_sites[i].rva,hudw_trampoline(hudw_sites[i].target),hudw_patched[i]);
        if(!view_cas_bytes(hudw_sites[i].rva,original,hudw_patched[i],5)) {
            for(unsigned j=0;j<i;j++) {
                unsigned char back[5];hudw_call(hudw_sites[j].rva,image+hudw_sites[j].target,back);
                (void)view_cas_bytes(hudw_sites[j].rva,hudw_patched[j],back,5);
            }
            hudw_installed=0;hudw_why="site";return 0;
        }
    }
    return 1;
}
static void hudw_uninstall(void) {
    if(!hudw_installed) return;
    hudw_installed=0;   /* the rule answers X4's from here on */
    for(unsigned i=0;i<HUDW_SITES;i++) {
        unsigned char original[5];hudw_call(hudw_sites[i].rva,image+hudw_sites[i].target,original);
        if(!view_cas_bytes(hudw_sites[i].rva,hudw_patched[i],original,5)) hudw_why="restore";
    }
}
/* Every gate tick (lockstep_gate.c, stop 248). */
static int hudw_check(void) {
    if(!hudw_installed) return 1;
    for(unsigned i=0;i<HUDW_SITES;i++) {
        unsigned char now[5];
        if(!read_local(image+hudw_sites[i].rva,now,5) || memcmp(now,hudw_patched[i],5)) {
            if(!strcmp(hudw_check_why,"-")) hudw_check_why="site_lost";
            return 0;
        }
    }
    return 1;
}
/* gate.hud_weapons(): in a session, on the UI thread; 1 armed (an identical rerun after a UI reload too). */
static int hudw_arm(void) {
    if(hudw_installed) return 1;
    if(!stats.active || GetCurrentThreadId()!=ui_thread) {hudw_why="late";return 0;}
    if(!hudw_pins()) {hudw_why="pins";return 0;}
    if(!hudw_install()) return 0;
    hudw_armed=1;hudw_why="armed";return 1;
}
static int hudw_status(char *out,size_t capacity) {
    int n=snprintf(out,capacity,"installed=%d why=%s check=%s asks=%lld yes=%lld own=%lld answered=%lld/%lld unlisted=%lld no_ship=%lld "
                   "elsewhere=%lld slots=",hudw_installed,hudw_why,hudw_check_why,(long long)hudw_asks,(long long)hudw_yes,(long long)hudw_own,
                   (long long)hudw_answered[0],(long long)hudw_answered[1],(long long)hudw_unlisted,(long long)hudw_no_ship,
                   (long long)hudw_elsewhere);
    for(unsigned k=1;n>0 && (size_t)n<capacity && k<=PILOT_SLOTS;k++)
        n+=snprintf(out+n,capacity-(size_t)n,"%s%lld",k>1?"/":"",(long long)hudw_slots[k]);
    return n>0 && (size_t)n<capacity?n:0;
}
