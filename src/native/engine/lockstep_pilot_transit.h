/* ---- A player 2..N's own transitions on its own client (native_gate_pilot_present_transit, a session default; stop 236) ----
 * User report (24 Nov, a two-PC game, player 2): the effects of moving between sectors did not seem to work (jump gates,
 * accelerators, superhighways). Measured first as X4's own player (x4ls play --solo 1 of a continued rk9 session) and
 * then as player 2 (--solo 2) through the same accelerator (Mars to Venus), superhighway and jump gate.
 *
 * What X4 does for its player at a gate or an accelerator [F, X4.exe 9.00, read in the image]. A ship that enters a gate's
 * trigger reaches the gate's pass 0x5ce690 (this = the gate, rdx = the ship; from the trigger's handler 0x5cdbb0 and from
 * Gate slot 1068 0x5cf940; the highway's own gates call it too, 0xd85a42). The pass posts EnteredGateEvent on the ship
 * (and on X4's player when it is aboard), counts the player's gates_traversed / accelerators_traversed, spawns the gate's
 * jump effect (Gate::Defaults +0xc58: jump_gateeffect or jump_acceleratoreffect, while the gate's attention is
 * physical), then, only when the ship is the player's (0x9f7b30: X4's player's controlled ship, 0x55b150) and the camera
 * is not in an external view (0x9fc0e0: camera +0x868), spawns the cockpit gate effect into that ship's cockpit (ship
 * +0x410, 0x7cf7c0): 0x3ca1b0, EffectLibrary cpfx_gate_01 (the cockpit flash, 1 s plus a 1 s fade, and a camera and
 * controller rumble), then posts the WarpEvent that moves the ship to the destination (delivered a pass later by the
 * MoveWorker, lockstep_warp_rng.h) and plays gate_flythrough at the ship. On X4's own player the screen goes white
 * right after the jump and fades back in about 0.7 s (the cockpit effect and its rumble); the HUD then says "Travel Mode
 * aborted" and shows the new sector's name.
 *
 * What went wrong for players 2..N. The player test reads X4's player, slot 1's on every client: on client k slot k's
 * ship passed every gate and accelerator without the cockpit effect (no flash, no rumble) on its own screen. The test is
 * simulation code; the effect is presentation (an EffectObject: its ID comes from the view list, lockstep_runtime_ids.h).
 *
 * Here, on client k (k = 2..N, this client's own slot, ptran_on): the call at 0x5cefd4 (X4's player's ship for that
 * test) goes through a trampoline (mov r8, rdi: the passing ship) to ptran_gate_test, which calls X4's 0x9f7b30
 * unchanged and, when the passing ship is the ship slot k's player is in now, notes its ID (any thread; nothing of the
 * simulation changes: X4's answer and its branch stay slot 1's). In the next UI frame, inside slot k's presentation
 * scope (pres_ui_move, before X4's UI move), ptran_scope_pass runs X4's own steps of that branch: the player's ship
 * (0x9f7b30, slot k's in the scope) must be the noted one, the camera not external (0x9fc0e0, this client's camera),
 * X4's player's controlled ship (0x55b150) and its cockpit (0x7cf7c0), then 0x3ca1b0 on that cockpit. The scope routes
 * the effect's runtime IDs to the UI's local list, the thread's simulation RNG word is held around the call
 * (pres_rng_hold), so the cockpit effect is this client's own, as stock makes it for X4's player alone. One pending
 * pass is kept (a second gate before the next UI frame keeps the last ship). A fault in X4's steps ends the module on
 * this client (faults=, why=gate_fault), not the session. Client 1 never installs it: X4's own test is its own.
 * Pins (refuse the arming): the test's block 0x5cefc8 (0x70 bytes, the call as X4's), 0x9f7b30, 0x9fc0e0, 0x55b150,
 * 0x7cf7c0, 0x3ca1b0. The site is checked at every gate tick (stop 236 'pilot transit', why=gate_site_lost).
 * Status in the PILOT_INPUTS present row: transit=on/installed/notes/passes/effects/external/other/faults. */
#define PTRAN_STOP_REASON 236u
#define PTRAN_GATE_SITE 0x5cefd4u        /* the gate pass 0x5ce690: call 0x9f7b30 for the cockpit effect's player test */
#define PTRAN_PLAYER_SHIP 0x9f7b30u      /* X4's player's controlled ship (out pointer in rdx) */
#define PTRAN_EXTERNAL 0x9fc0e0u         /* IsExternalViewActive: the player camera's mode +0x868 */
#define PTRAN_CONTROLLED 0x55b150u       /* an entity's controlled ship (entity, out) */
#define PTRAN_COCKPIT 0x7cf7c0u          /* a ship's cockpit (+0x410, class 0x19 / 0x10) (ship, out) */
#define PTRAN_GATE_FX 0x3ca1b0u          /* the cockpit gate effect: cpfx_gate_01 into the cockpit */
#define PTRAN_UNIVERSE 0x3d198f8u
#define PTRAN_PLAYER 0x230u
#define PTRAN_PAGE 0x1000u
#define PTRAN_DEPTH 8u
static const struct {unsigned rva,size;const char *sha;} ptran_pin_table[]={
    {0x5cefc8u,0x70u,"e14dc90606b6db73699cc7cc7187eeb0e59f396629155dba1baedb94c65ae3c0"},   /* the test's block, X4's call */
    {PTRAN_PLAYER_SHIP,0x4au,"a326300841510371857652ce770f7e03d945864f24c191947e14b5871b340a74"},
    {PTRAN_EXTERNAL,0x26u,"ed70c0f5555909c41f494954270197fcbd76dd039eb11ccb77dab5e4ad317931"},
    {PTRAN_CONTROLLED,0x111u,"dd70be4494461ad9d35c55a1a413f1cdfc5c7bdd348974df2928f4853a1d2522"},
    {PTRAN_COCKPIT,0x75u,"6252c759309065569caed6fbf541bb9ea8dccd2146370adab9cf48e2f49baa0d"},
    {PTRAN_GATE_FX,0x420u,"a869e32f0bc54fa7a74b617b8ddee5332694bf3ca99849c8060f6c5ae689e37c"}};
#define PTRAN_PIN_COUNT (sizeof(ptran_pin_table)/sizeof(*ptran_pin_table))
typedef void **(*ptran_out_fn)(void *,void **);          /* 0x9f7b30 (rcx unused, out), 0x55b150 / 0x7cf7c0 (object, out) */
typedef unsigned char (*ptran_external_fn)(void);
typedef void (*ptran_fx_fn)(void *);
static int ptran_on,ptran_asked,ptran_installed;
static const char *ptran_why="-";
static unsigned char *ptran_page;
static unsigned char ptran_patched[5];
static volatile LONG64 ptran_pending;    /* the noted ship's runtime ID, 0 none */
static volatile LONG64 ptran_notes,ptran_passes,ptran_effects,ptran_external,ptran_other,ptran_faults;
/* X4's functions (pres_use_image), or a test's. */
static struct {ptran_out_fn player_ship,controlled,cockpit;ptran_external_fn external;ptran_fx_fn fx;unsigned char *universe_at;} ptran_fn;
static int ptran_pins(void) {
    for(unsigned i=0;i<PTRAN_PIN_COUNT;i++) if(!hash_ok(image+ptran_pin_table[i].rva,ptran_pin_table[i].size,ptran_pin_table[i].sha)) {pin_failed_rva=ptran_pin_table[i].rva;return 0;}
    ptran_fn.player_ship=(ptran_out_fn)(void*)(image+PTRAN_PLAYER_SHIP);ptran_fn.controlled=(ptran_out_fn)(void*)(image+PTRAN_CONTROLLED);
    ptran_fn.cockpit=(ptran_out_fn)(void*)(image+PTRAN_COCKPIT);ptran_fn.external=(ptran_external_fn)(void*)(image+PTRAN_EXTERNAL);
    ptran_fn.fx=(ptran_fx_fn)(void*)(image+PTRAN_GATE_FX);ptran_fn.universe_at=image+PTRAN_UNIVERSE;
    return 1;
}
/* The trampoline's target: X4's answer unchanged, the passing ship noted when it is slot k's (any thread). */
static void **ptran_gate_test(void *unused,void **out,unsigned char *ship) {
    void **answer=ptran_fn.player_ship(unused,out);
    if(ptran_installed && ship && pres_slot>=2 && pres_slot<=PILOT_SLOTS) {
        const pslot_slot *s=&pslots[pres_slot];
        unsigned char *own=s->seated && s->player?(unsigned char*)s->ship:NULL;uint64_t id=0;
        if(own && ship==own && read_local(ship+8,&id,8) && id) {InterlockedExchange64(&ptran_pending,(LONG64)id);InterlockedIncrement64(&ptran_notes);}
    }
    return answer;
}
/* mov r8, rdi; jmp [rip]; abs64 ptran_gate_test */
static void ptran_write_trampoline(unsigned char *p) {
    static const unsigned char code[9]={0x4c,0x8b,0xc7,0xff,0x25,0,0,0,0};void *t=(void*)ptran_gate_test;
    memcpy(p,code,9);memcpy(p+9,&t,8);
}
static unsigned char *ptran_alloc_page(void) {
    uintptr_t at=(uintptr_t)(pnat_base+PTRAN_GATE_SITE)&~(uintptr_t)65535;
    for(uintptr_t distance=65536;distance<0x70000000;distance+=65536)
        for(int sign=-1;sign<=1;sign+=2) {
            uintptr_t candidate=sign<0?at-distance:at+distance;
            unsigned char *p=VirtualAlloc((void*)candidate,PTRAN_PAGE,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
            if(!p) continue;
            intptr_t d=(intptr_t)p-(intptr_t)(pnat_base+PTRAN_GATE_SITE+5);
            if(d>INT32_MIN+0x1000 && d<INT32_MAX-0x1000) return p;
            VirtualFree(p,0,MEM_RELEASE);
        }
    return NULL;
}
static void ptran_original(unsigned char out[5]) {
    int32_t rel=(int32_t)((intptr_t)(pnat_base+PTRAN_PLAYER_SHIP)-(intptr_t)(pnat_base+PTRAN_GATE_SITE+5));out[0]=0xe8;memcpy(out+1,&rel,4);
}
/* At the start of presenting (pres_start, frame thread): the site, once. 0 = refused (the caller fails presenting). */
static int ptran_install(void) {
    if(!ptran_on || ptran_installed) return 1;
    unsigned char original[5];ptran_original(original);
    if(!ptran_page) ptran_page=ptran_alloc_page();
    if(!ptran_page) {ptran_why="page";return 0;}
    DWORD old,ignored;
    if(!VirtualProtect(ptran_page,PTRAN_PAGE,PAGE_READWRITE,&old)) {ptran_why="page";return 0;}
    memset(ptran_page,0xcc,64);ptran_write_trampoline(ptran_page);
    if(!VirtualProtect(ptran_page,PTRAN_PAGE,PAGE_EXECUTE_READ,&ignored)) {ptran_why="page";return 0;}
    FlushInstructionCache(GetCurrentProcess(),ptran_page,PTRAN_PAGE);
    int32_t rel=(int32_t)((intptr_t)ptran_page-(intptr_t)(pnat_base+PTRAN_GATE_SITE+5));
    ptran_patched[0]=0xe8;memcpy(ptran_patched+1,&rel,4);
    InterlockedExchange64(&ptran_pending,0);
    ptran_installed=1;   /* before the site: a pass that reaches it already notes */
    if(!pview_cas(PTRAN_GATE_SITE,original,ptran_patched,5)) {ptran_installed=0;ptran_why="site";return 0;}
    return 1;
}
static void ptran_uninstall(void) {
    if(!ptran_installed) return;
    unsigned char original[5];ptran_original(original);
    if(pview_cas(PTRAN_GATE_SITE,ptran_patched,original,5)) ptran_installed=0;   /* not ours: the tick check names it */
    InterlockedExchange64(&ptran_pending,0);
}
static int ptran_intact(void) {
    if(!ptran_installed) return 1;
    unsigned char now[5];return read_local(pnat_base+PTRAN_GATE_SITE,now,5) && !memcmp(now,ptran_patched,5);
}
/* The tick check (lockstep_gate.c, stop 236). */
static const char *ptran_check_why="-";
static int ptran_check(void) {
    if(ptran_intact()) return 1;
    if(!strcmp(ptran_check_why,"-")) ptran_check_why="gate_site_lost";
    return 0;
}
/* Every UI frame in this client's own presentation scope (pres_ui_move): X4's player branch of the gate pass for the
 * noted ship, as the pass runs it for X4's own player. */
static void ptran_scope_pass(void) {
    if(!ptran_installed || !InterlockedCompareExchange(&pres_open,0,0)) return;
    const uint64_t noted=(uint64_t)InterlockedExchange64(&ptran_pending,0);
    if(!noted) return;
    InterlockedIncrement64(&ptran_passes);
    unsigned char *universe=NULL,*player=NULL;
    if(!read_local(ptran_fn.universe_at,&universe,8) || !universe || !read_local(universe+PTRAN_PLAYER,&player,8) || !player) {InterlockedIncrement64(&ptran_other);return;}
    void *ship=NULL,*controlled=NULL,*cockpit=NULL;uint64_t id=0;int fault=0,spawned=0;
    uint64_t word=0,*rng=pres_rng_hold(&word);
    __try {
        ptran_fn.player_ship(NULL,&ship);
        if(!ship || !read_local((unsigned char*)ship+8,&id,8) || id!=noted) InterlockedIncrement64(&ptran_other);
        else if(ptran_fn.external()) InterlockedIncrement64(&ptran_external);
        else {
            ptran_fn.controlled(player,&controlled);
            if(controlled) ptran_fn.cockpit(controlled,&cockpit);
            if(cockpit) {ptran_fn.fx(cockpit);spawned=1;}
            else InterlockedIncrement64(&ptran_other);
        }
    }
    __except(GetExceptionCode()==EXCEPTION_ACCESS_VIOLATION?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH) {fault=1;}
    pres_rng_back(rng,word);
    if(fault) {InterlockedIncrement64(&ptran_faults);ptran_why="gate_fault";ptran_uninstall();return;}
    if(spawned) InterlockedIncrement64(&ptran_effects);
}
static void ptran_reset(void) {ptran_notes=ptran_passes=ptran_effects=ptran_external=ptran_other=ptran_faults=0;ptran_pending=0;ptran_why="-";}
static int ptran_status(char *out,size_t capacity) {
    const int n=snprintf(out,capacity," transit=%d/%d/%lld/%lld/%lld/%lld/%lld/%lld",ptran_on,ptran_installed,(long long)ptran_notes,(long long)ptran_passes,
        (long long)ptran_effects,(long long)ptran_external,(long long)ptran_other,(long long)ptran_faults);
    return n>0 && (size_t)n<capacity?n:0;
}
