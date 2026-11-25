/* The turrets of a player 2..N's ship in "attack my current target" fire at that player's target, identically on every
 * client (native_gate_turret_assist, a session default; stop 239).
 * Measured 25 Nov (two clients, each player in a Teladi bomber with four turrets, xenon fighters 700 m ahead of each
 * ship, the modes set through the replayed UI): with the turrets' script running as the ship's player (lockstep_mission_
 * owners.h, the aboard rule) player 2's turrets fired in every mode that has a target, but in "attack my current target"
 * they fired at player 1's target (player 2 targeting TB2, player 1 TA2: player 2's turrets hit TA2) and at nothing while
 * player 1's target was out of their range.
 *
 * What X4 does [F, X4.exe 9.00, read in the image]. A ship's update (Defensible / Container slot 674, 0x4f0bc0) runs the
 * weapon-mode update 0x4ef380 for up to 25 of its turrets, each about once a second (its next time at turret +0x360, the
 * delay from the thread's random stream). The mode is turret +0x308 (11, the group's mode, resolved by 0x819330 first);
 * mode 13 ("attack my current target") asks for X4's player's soft target: 0x9fccd0 reads the universe's player
 * (+0x230), its +0x6dd0 UniverseID, and answers that component from the registry (0xced70 on the map at 0x6cf1500; an
 * ID it no longer finds is cleared). That target's object (class 0x24 up its parents) becomes the turret's target
 * (Turret::SetTarget 0x80c3e0, or a shoot controller for a turret whose macro aims by itself) when its owner is not the
 * turret's owner and the may-attack test 0x3eb370 passes. The other modes pick from the ship's own lists by kind.
 *
 * What went wrong for players 2..N: outside a slot's scope X4's player is slot 1's on every client, so every ship's
 * mode 13 turrets took slot 1's target.
 *
 * Here, on every client alike (simulation): the call at 0x4ef43f goes through a trampoline on this module's page near the
 * image (mov r8, rdi: the turret; jmp to the rule). The rule calls X4's 0x9fccd0 as it is (slot 1's answer, X4's clear of
 * slot 1's stale ID unchanged), then, when the turret is a part (up to four parents) of the ship a slot 2..N player is
 * seated in (its seat, also while on foot from it or docked), answers that player's soft target instead: P_k's +0x6dd0
 * through the same registry lookup, none when it has none or its object is gone (P_k's own getter clears that ID in its
 * scope). Every input is the simulation's: the slots' seats and ships, P_k's soft target (the UI replay's target family
 * sets it on every client at the same tick). Slot 1's ship and every other ship keep X4's answer: the stock rule that
 * mode 13 follows X4's own player.
 * Pins (refuse the arming): the update's block from the turret's register (mov rdi, rdx at 0x4ef3af) to after the call,
 * X4's call there, and the getter (the universe, the soft target's offset, the registry and its map inside it). Installed
 * before the snapshot load (CAS of the call; nothing runs it then); every gate tick checks the site (stop 239 'turret
 * assist', why site_lost); the stop restores by CAS. Status TURRET_ASSIST. Not covered: the Linux gate (no twin: a mixed
 * session leaves this off). */
#define TAST_STOP_REASON 239u
#define TAST_SITE 0x4ef43fu            /* the weapon-mode update 0x4ef380: call 0x9fccd0, mode 13's target */
#define TAST_PLAYER_TARGET 0x9fccd0u   /* X4's player's soft target (rcx unused, out in rdx) */
#define TAST_FIND 0xced70u             /* the component registry's find (map, ID, 4) */
#define TAST_MAP 0x6cf1500u            /* the UniverseID map */
#define TAST_SOFT_TARGET 0x6dd0u       /* Player: the soft target's UniverseID */
#define TAST_PARENT 0x70u
#define TAST_DEPTH 4u                  /* turret -> its slot / group -> ... -> the ship */
#define TAST_PAGE 0x1000u
static const struct {unsigned rva,size;const char *sha;} tast_pins_list[]={
    {0x4ef3afu,0xa2u,"93d13d41a8b1dac372630936cd2aa7c9dee43f9627f51b67db8aa84f0f3ccdc0"},   /* rdi = the turret .. X4's call */
    {TAST_PLAYER_TARGET,0x99u,"1daf4751819eede80b0fa50d5502a82ba2be53dc5fbf8763bf12b3da8683603f"}};
typedef void **(*tast_out_fn)(void *,void **);
typedef void *(*tast_find_fn)(void *,uint64_t,unsigned);
static int tast_armed,tast_installed;
static const char *tast_why="-",*tast_check_why="-";
static unsigned char *tast_page;
static unsigned char tast_patched[5];
static volatile LONG64 tast_calls,tast_answered[PILOT_SLOTS+1],tast_changed,tast_none;
static struct {tast_out_fn target;tast_find_fn find;} tast_fn;   /* X4's (tast_pins), or a test's */
static void *(*tast_find_test)(uint64_t id);                         /* tests: in place of the map and X4's find */
static void *tast_find(uint64_t id) {
    void *map=NULL;
    if(tast_find_test) return id?tast_find_test(id):NULL;
    return id && read_local(image+TAST_MAP,&map,8) && map?tast_fn.find(map,id,4):NULL;
}
/* The slot 2..N whose seat's ship holds the turret (itself or up to TAST_DEPTH parents up), 0 none. */
static unsigned tast_slot_of(unsigned char *turret) {
    unsigned char *p=NULL;
    if(!turret || !read_local(turret+TAST_PARENT,&p,8)) return 0;
    for(unsigned depth=1;p && depth<=TAST_DEPTH;depth++) {
        for(unsigned k=2;k<=PILOT_SLOTS;k++)
            if(pslots[k].player && pslots[k].seated && pslots[k].ship==p) return k;
        unsigned char *up=NULL;
        if(!read_local(p+TAST_PARENT,&up,8) || up==p) break;
        p=up;
    }
    return 0;
}
/* The trampoline's target (any thread the ship's update runs on): X4's answer, or slot k's target for slot k's turret. */
static void **tast_rule(void *unused,void **out,unsigned char *turret) {
    void **answer=tast_fn.target(unused,out);
    InterlockedIncrement64(&tast_calls);
    if(!tast_installed || !out) return answer;
    const unsigned k=tast_slot_of(turret);
    if(!k) return answer;
    uint64_t id=0;
    void *target=read_local(pslots[k].player+TAST_SOFT_TARGET,&id,8) && id?tast_find(id):NULL;
    if(!target) InterlockedIncrement64(&tast_none);
    if(target!=*out) InterlockedIncrement64(&tast_changed);
    *out=target;InterlockedIncrement64(&tast_answered[k]);
    return answer;
}
/* mov r8, rdi; jmp [rip]; abs64 tast_rule */
static void tast_write_trampoline(unsigned char *p) {
    static const unsigned char code[9]={0x4c,0x8b,0xc7,0xff,0x25,0,0,0,0};void *t=(void*)tast_rule;
    memcpy(p,code,9);memcpy(p+9,&t,8);
}
static void tast_original(unsigned char out[5]) {
    const int32_t rel=(int32_t)((intptr_t)(image+TAST_PLAYER_TARGET)-(intptr_t)(image+TAST_SITE+5));out[0]=0xe8;memcpy(out+1,&rel,4);
}
static int tast_pins(void) {
    for(unsigned i=0;i<sizeof(tast_pins_list)/sizeof(*tast_pins_list);i++)
        if(!hash_ok(image+tast_pins_list[i].rva,tast_pins_list[i].size,tast_pins_list[i].sha)) {pin_failed_rva=tast_pins_list[i].rva;return 0;}
    unsigned char want[5],now[5];tast_original(want);
    if(!read_local(image+TAST_SITE,now,5) || memcmp(now,want,5)) {pin_failed_rva=TAST_SITE;return 0;}
    tast_fn.target=(tast_out_fn)(void*)(image+TAST_PLAYER_TARGET);tast_fn.find=(tast_find_fn)(void*)(image+TAST_FIND);
    return 1;
}
static unsigned char *tast_alloc_page(void) {
    uintptr_t at=(uintptr_t)(image+TAST_SITE)&~(uintptr_t)65535;
    for(uintptr_t distance=65536;distance<0x70000000;distance+=65536)
        for(int sign=-1;sign<=1;sign+=2) {
            unsigned char *p=VirtualAlloc((void*)(sign<0?at-distance:at+distance),TAST_PAGE,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
            if(!p) continue;
            const intptr_t d=(intptr_t)p-(intptr_t)(image+TAST_SITE+5);
            if(d>INT32_MIN+0x10000 && d<INT32_MAX-0x10000) return p;
            VirtualFree(p,0,MEM_RELEASE);
        }
    return NULL;
}
/* The trampoline and the site (CAS of the call). 0 refused, nothing written. */
static int tast_install(void) {
    if(tast_installed) return 1;
    if(!tast_page) tast_page=tast_alloc_page();
    if(!tast_page) {tast_why="page";return 0;}
    DWORD old,ignored;
    if(!VirtualProtect(tast_page,TAST_PAGE,PAGE_READWRITE,&old)) {tast_why="page";return 0;}
    memset(tast_page,0xcc,64);tast_write_trampoline(tast_page);
    if(!VirtualProtect(tast_page,TAST_PAGE,PAGE_EXECUTE_READ,&ignored)) {tast_why="page";return 0;}
    FlushInstructionCache(GetCurrentProcess(),tast_page,TAST_PAGE);
    unsigned char original[5];tast_original(original);
    const int32_t rel=(int32_t)((intptr_t)tast_page-(intptr_t)(image+TAST_SITE+5));
    tast_patched[0]=0xe8;memcpy(tast_patched+1,&rel,4);
    tast_installed=1;   /* before the site: an update that reaches it already answers */
    if(!view_cas_bytes(TAST_SITE,original,tast_patched,5)) {tast_installed=0;tast_why="site";return 0;}
    return 1;
}
static void tast_uninstall(void) {
    if(!tast_installed) return;
    tast_installed=0;   /* the rule answers X4's from here on */
    unsigned char original[5];tast_original(original);
    if(!view_cas_bytes(TAST_SITE,tast_patched,original,5)) tast_why="restore";
}
/* Every gate tick (lockstep_gate.c, stop 239). */
static int tast_check(void) {
    if(!tast_installed) return 1;
    unsigned char now[5];
    if(read_local(image+TAST_SITE,now,5) && !memcmp(now,tast_patched,5)) return 1;
    if(!strcmp(tast_check_why,"-")) tast_check_why="site_lost";
    return 0;
}
/* gate.turret_assist(): before the snapshot load and the first admitted frame, on the UI thread; 1 armed. */
static int tast_arm(void) {
    if(tast_installed) return stats.active!=0;
    if(!stats.active || !boot_mode || boot_phase || stats.issued || GetCurrentThreadId()!=ui_thread) {tast_why="late";return 0;}
    if(!tast_pins()) {tast_why="pins";return 0;}
    if(!tast_install()) return 0;
    tast_armed=1;return 1;
}
static int tast_status(char *out,size_t capacity) {
    int n=snprintf(out,capacity,"installed=%d calls=%lld changed=%lld none=%lld why=%s check=%s slots=",tast_installed,(long long)tast_calls,
                   (long long)tast_changed,(long long)tast_none,tast_why,tast_check_why);
    for(unsigned k=2;n>0 && (size_t)n<capacity && k<=PILOT_SLOTS;k++)
        n+=snprintf(out+n,capacity-(size_t)n,"%s%lld",k>2?"/":"",(long long)tast_answered[k]);
    return n>0 && (size_t)n<capacity?n:0;
}
