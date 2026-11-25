/* A player's cover (disguise) and a ship's stance for every player, a session default (native_gate_cover_slots;
 * stop reason 246). Report: docs/PLAYTEST.md "Cover and stances for players 2..N".
 *
 * X4's cover [F, X4.exe 9.00, pinned below]: an object has a true owner and a cover owner (Object +0x348/+0x350, an
 * entity +0x2c0/+0x2c8); its "owner", which every relation test reads, is the cover owner when one is set. Setting a
 * controllable's cover (Object slot 722 with notify) goes to its pilot when it has one (Controllable slot 720: the pilot
 * post's entity, inside slot k's scope P_k), and the pilot's own setter puts the cover on the ship it controls (its
 * vector +0x308, notify off) and on itself: the ship and the player wear the same faction. The player's cover ability is
 * MD's (md/cover.xml RegisterCoverAbility): a table on the player entity's blackboard, ship -> faction, and a cooldown
 * time. Three readers ask "the player" (the universe singleton) about it: the ability's faction of a ship (0x741910),
 * whether a ship can take the player's cover (Controllable slot 723: its defaults' flag or that table) and whether the
 * cooldown is over (CanPlayerActivateCoverAbility). The player's last chosen faction for a ship that may take any cover
 * is U::Player +0x8a780.
 *
 * Players 2..N, before this module (24 Nov build, rk14):
 *   keys      TOGGLE_COVER (0x179) and TOGGLE_STANCE (0x17d) were refused for every player (lockstep_pilot_stock.h),
 *             and the docked menu's cover button and dropdown (SetObjectCoverFaction, SetPlayerLastCoverFaction) and its
 *             stance dropdown (SetShipStance) were refused by the UI guard: nobody could put a cover on or switch the
 *             Envoy's cloak stance in a session, player 1 included.
 *   menus     client k's docked menu asked the readers in the presentation scope, whose player is X4's own (player 1's
 *             entity): slot k's ability (on P_k's blackboard, where slot k's story copy put it) never showed, the
 *             cooldown and the "Cover active as" line were player 1's.
 * Here, the same for every slot as X4 does it for its own player:
 *   keys      the two keys ride the stock record (PSTK_REPLAY) and their replay runs X4's two cases of U::Player's action
 *             handler 0x71bac0 for that slot's player (slot 1 canonical, slot k inside its scope, where the singleton is
 *             P_k), on every client at the same tick: the ship's cover from the ability's faction or the last chosen one,
 *             off when the player wears one; the next stance of the ship's list. Restated from the pinned case bytes
 *             (0x71c29c..0x71c4d8) with X4's own calls, so the handler's notice (the input feedback "Cover activated",
 *             "deactivated", "unavailable", the stance's) goes to the slot's own client only: X4 shows it on the UI of
 *             whoever runs the handler, and here every client runs it.
 *   menus     the flight family replays SetObjectCoverFaction, SetPlayerLastCoverFaction and SetShipStance in the slot's
 *             scope (lockstep_ui_replay.h). On a client of slot k >= 2, in its own UI's local phase (the docked menu, the
 *             HUD's mission ticker, the player name of the menus), the four readers answer for P_k: entry jumps on the
 *             four exports to this module's page; slot 1's client, the probe's own frame, an apply and any other thread
 *             get X4's function (the entry's first bytes moved to a trampoline).
 *   MD        md/cover.xml's cover ability manager hears every player's ship (the player groups); its lines for the
 *             player are slot k's in tools/dev/native_gate_player_cover_manager.xml (a diff), and the voice lines run in
 *             slot k's scope through a signal on P_k (lockstep_pilot_death.h's due forward).
 * Status COVER_SLOTS: installed, keys and the slot whose replay ran them, notices shown / held, the readers' answers for
 * the slot, why. Pins refuse the arming (pins:<rva>); a lost entry jump stops the session (246, entry_lost). */
#define PCOV_STOP_REASON 246u
#define PCOV_KEY_COVER 0x179                /* INPUT_ACTION_TOGGLE_COVER */
#define PCOV_KEY_STANCE 0x17d               /* INPUT_ACTION_TOGGLE_STANCE */
#define PCOV_HANDLER 0x71bac0u              /* U::Player's action handler: the stock handler both keys reach */
#define PCOV_CASES 0x71c29cu                /* its two cases, restated below */
#define PCOV_CASES_SIZE 0x23cu
#define PCOV_CASES_SHA "a546a76491c795ac00ace7d0e02587483addf79d0de0eef34dc681c8aac9d58e"
#define PCOV_OCCUPIED 0x9f7a40u             /* (unused, out): the universe player's occupied ship */
#define PCOV_ABILITY 0x741910u              /* (player, ship): the faction of the player's cover ability table for the ship */
#define PCOV_DEFAULTS 0x328f30u             /* (macro): a ship's defaults: stances +0x1450 (vector), +0x1468 (wrap) */
#define PCOV_STANCE_CAN 0x7e2160u           /* (ship): its stance cooldown is over */
#define PCOV_STANCE_SET 0x7e21b0u           /* (ship, stance) */
#define PCOV_STANCE_OFF 0x7e2730u           /* (ship): back to no stance */
#define PCOV_NOTICE 0x1257950u              /* the input feedback (manager, action, state, 0x4d, data) */
#define PCOV_NOTICE_OWNER 0x6cf1890u
#define PCOV_NOTICE_DATA 0x31c0f60u
#define PCOV_CTRL_DEFAULTS 0x493ae0u        /* (macro) -> a controllable's defaults: +0x798 the player's cover allowed */
#define PCOV_COVERABLE_FN 0x44d0e0u         /* Controllable slot 723 */
#define PCOV_UNIVERSE 0x3d198f8u            /* +0x230: the player singleton */
#define PCOV_COOLDOWN_KEY 0x2f59b68u        /* the blackboard key of $x4ep1_player_cover_ability_cooldown (CanPlayerActivate...) */
#define PCOV_SLOT_HAS_COVER 705u            /* Entity: +0x2c8 != 0 */
#define PCOV_SLOT_COVER 707u                /* Entity: +0x2c8 */
#define PCOV_SLOT_SET_COVER 722u            /* Object / Entity: (faction, notify) */
#define PCOV_SLOT_COVERABLE 723u            /* Controllable */
#define PCOV_SLOT_IS_CLASS 572u             /* Component +0x11e0 */
#define PCOV_PLAYER_LAST 0x8a780u           /* U::Player: the last chosen cover faction */
#define PCOV_PLAYER_COVER 0x2c8u            /* Entity: the cover owner */
#define PCOV_OBJECT_COVER 0x350u            /* Object: the cover owner */
#define PCOV_BLACKBOARD 0x330u              /* Entity: the blackboard tree's header (+8 its root; node: +8 left, +0x10 right,
                                               +0x20 key, +0x48 value: +0 type, 8 a time, +8 the double) */
#define PCOV_SHIP_MACRO 0x60u
#define PCOV_SHIP_STANCE 0xbe8u
#define PCOV_DEF_STANCES 0x1450u
#define PCOV_DEF_STANCE_NONE 0x1468u
#define PCOV_CTRL_ALLOWED 0x798u
#define PCOV_CLASS_SHIP 0x75
#define PCOV_CLASS_CONTROLLABLE 0x70
#define PCOV_PAGE 0x1000u
#define PCOV_STRIDE 0x40u                   /* a reader's stub (+0) and trampoline (+0x20) */
#define PCOV_READS 4u
enum {PCOV_R_ABILITY=0,PCOV_R_COVERABLE,PCOV_R_CAN,PCOV_R_COVER};
/* The readers' entries: their first instructions (whole, moved to the trampoline; one RIP-relative displacement at rip,
 * relative to the end of the head), the function's pin. */
static const struct {const char *name;unsigned rva,size;const char *sha;unsigned char head[11];unsigned char length,rip;} pcov_reads[PCOV_READS]={
    {"GetObjectCoverAbilityFaction",0x1a2370u,0xb9u,"47533d2b8d6d016f3b9baa79d2ec8f29b9fa7014df9ad1a3a8c1fc66e9970dbc",
     {0x48,0x89,0x5c,0x24,0x08,0x57},6,0},
    {"IsPlayerCoverable",0x1b3560u,0x7bu,"5e1a9914b71fffef57bf5152fce6e65e989e7eefdcab56cb9e5ece76fca88d19",
     {0x48,0x89,0x5c,0x24,0x08,0x57},6,0},
    {"CanPlayerActivateCoverAbility",0x177680u,0x86u,"9c4dcbfa34ac675533dfcc9020244f4ed14f8fe7a124e4a785d37e04cc13f452",
     {0x48,0x83,0xec,0x38,0x48,0x8b,0x05,0x6d,0x22,0xba,0x03},11,7},
    {"GetPlayerCoverFaction",0xb05dc0u,0x69u,"e570ee0fed455c95b62f6eebd13e7bb3b36240b7c34b86321b47f4a899db836c",
     {0x40,0x53,0x48,0x83,0xec,0x20},6,0}};
static const struct {unsigned rva,size;const char *sha;} pcov_pins[]={
    {PCOV_HANDLER,0xb77u,"a491b89effdc2077f03828adaa8efa6aad734413f50f833d5b0bbef6c1063698"},
    {PCOV_CASES,PCOV_CASES_SIZE,PCOV_CASES_SHA},
    {PCOV_OCCUPIED,0xe9u,"8efce9c56d221e55412198e61584bd1e1085e0551a1ca36db0ccdb650afe2f0d"},
    {PCOV_ABILITY,0x224u,"8e39e3858ee370ce2faa2c300df63f98f0e8363aaf2839675453cf0bf09c64c8"},
    {PCOV_DEFAULTS,0xc1u,"a8ab768cb684798727ee8e517fce83e2ca8b06e0bde227fea205309d2568afa9"},
    {PCOV_STANCE_CAN,0x49u,"cb34652d9ebf16495a037d27dadb1dff51ee101daed21be8f09117c51c299cbb"},
    {PCOV_STANCE_SET,0x578u,"0871c414d1d7f8b13a11bfa3daca7eac493c08f76bd54fb00e58066531e12cb4"},
    {PCOV_STANCE_OFF,0x3cu,"0c0b75b956b04ac67bfef46c08d644b5d10fe0665e60ebe57f47d205e3db85e9"},
    {PCOV_NOTICE,0x4fu,"49413c6fba9dc915f92b1f477e5c72924d8f0d5ec6d4cff0c7e47aadf0090d65"},
    {PCOV_CTRL_DEFAULTS,0xbbu,"04075af2d8fb37ca6fd4b3e57e72f946e2db185dbc70dfaa9a4b6d641bdca99d"},
    {PCOV_COVERABLE_FN,0x6du,"e1a3eef54113ababad7a652d0d90949152cf2992c3c2b73b92bec11e53a214c9"}};
#define PCOV_PIN_COUNT (sizeof(pcov_pins)/sizeof(*pcov_pins))
typedef struct {
    void **(*occupied)(void *,void **);
    unsigned char *(*ability)(void *player,void *ship);
    unsigned char *(*defaults)(void *macro);
    unsigned char (*stance_can)(void *ship);
    void (*stance_set)(void *ship,void *stance);
    void (*stance_off)(void *ship);
    void (*notice)(void *owner,int action,int state,int kind,void *data);
    unsigned char *(*ctrl_defaults)(void *macro);
} pcov_fns;
static pcov_fns pcov_fn;                    /* X4's at the arming; tests set their own */
static unsigned char *pcov_base;            /* the image, or a test region (the notice's owner and data, the singleton) */
static void *pcov_coverable_fn;             /* Controllable slot 723 as X4 has it (the readers answer for P_k only then) */
static int pcov_armed,pcov_installed,pcov_pins_checked;
static const char *pcov_why="-";
static unsigned char *pcov_page;
static unsigned char pcov_patched[PCOV_READS][5],pcov_on[PCOV_READS];
static void *pcov_original[PCOV_READS];     /* the trampolines (X4's functions); tests set their own */
static unsigned pcov_own_slot_test;         /* tests: this client's own slot */
static unsigned char *(*pcov_view_test)(void);   /* tests: the presentation's player */
static volatile LONG64 pcov_keys[2],pcov_key_slots[PILOT_SLOTS+1],pcov_shown,pcov_held,pcov_none,pcov_faults,pcov_reads_slot[PCOV_READS],
    pcov_reads_stock[PCOV_READS];
static unsigned char pcov_last_state[PILOT_SLOTS+1];   /* the notice state of each slot's last cover key (2 on, 0x14 off, 0x22 none) */
/* Each slot's last cover key as X4 left it: who the ship's pilot was (Ship slot 1072: P the slot's player, n another
 * entity, 0 nobody), then whether the ship and the player wear a cover (y/n). */
static char pcov_last_key[PILOT_SLOTS+1][4];
#define PCOV_SLOT_PILOT 1072u   /* Ship: the entity at its pilot post that controls it, else its aipilot */

static void **pcov_vtable(const void *object) {void **vt=NULL;memcpy(&vt,object,8);return vt;}
static unsigned char *pcov_load(const unsigned char *at) {unsigned char *p=NULL;memcpy(&p,at,8);return p;}
static unsigned char *pcov_singleton(void) {
    unsigned char *universe=NULL,*player=NULL;
    if(!read_local(pcov_base+PCOV_UNIVERSE,&universe,8) || !universe || !read_local(universe+0x230,&player,8)) return NULL;
    return player;
}
static unsigned pcov_own_slot(void) {return pcov_own_slot_test?pcov_own_slot_test:pstk_capture_slot?pstk_capture_slot:1u;}
/* A faction's ID, as X4's getters answer it: its std::string at +0x10 (inline below 16 characters). */
static const char *pcov_faction_id(const unsigned char *faction) {
    uint64_t capacity=0;
    if(!faction || !read_local(faction+0x28,&capacity,8)) return "";
    return capacity<16u?(const char*)(faction+0x10):(const char*)pcov_load(faction+0x10);
}
static unsigned char pcov_has_cover(unsigned char *player) {return ((unsigned char(*)(void*))pcov_vtable(player)[PCOV_SLOT_HAS_COVER])(player);}
static void pcov_set_cover(unsigned char *object,unsigned char *faction) {
    ((void(*)(void*,void*,unsigned char))pcov_vtable(object)[PCOV_SLOT_SET_COVER])(object,faction,1);
}
static void pcov_notice(unsigned slot,int action,int state) {
    if(slot<=PILOT_SLOTS && action==PCOV_KEY_COVER) pcov_last_state[slot]=(unsigned char)state;
    if(slot!=pcov_own_slot()) {InterlockedIncrement64(&pcov_held);return;}   /* X4's notice: the slot's own client only */
    InterlockedIncrement64(&pcov_shown);
    pcov_fn.notice(pcov_load(pcov_base+PCOV_NOTICE_OWNER),action,state,0x4d,pcov_base+PCOV_NOTICE_DATA);
}
/* X4's TOGGLE_COVER case (0x71c2a1), for the singleton (the slot's player in its scope). */
static void pcov_cover_key(unsigned slot,unsigned char *player) {
    void *out=NULL;pcov_fn.occupied(NULL,&out);
    unsigned char *ship=out;
    if(!ship) {InterlockedIncrement64(&pcov_none);return;}
    unsigned char *faction=pcov_fn.ability(player,ship);
    if(faction) {   /* the ship's cover ability (a story's): its faction, off while the player wears a cover */
        pcov_notice(slot,PCOV_KEY_COVER,pcov_has_cover(player)?0x14:2);
        pcov_set_cover(ship,pcov_has_cover(player)?NULL:faction);
        return;
    }
    if(!((unsigned char(*)(void*))pcov_vtable(ship)[PCOV_SLOT_COVERABLE])(ship)) {InterlockedIncrement64(&pcov_none);return;}
    unsigned char *last=pcov_load(player+PCOV_PLAYER_LAST);
    pcov_notice(slot,PCOV_KEY_COVER,pcov_has_cover(player)?0x14:last?2:0x22);
    pcov_set_cover(ship,pcov_has_cover(player)?NULL:last);
}
/* X4's TOGGLE_STANCE case (0x71c3e8): the next stance of the ship's list; after the last, none when the defaults' word
 * +0x1468 is 0, else the first again. */
static void pcov_stance_key(unsigned slot) {
    void *out=NULL;pcov_fn.occupied(NULL,&out);
    unsigned char *ship=out;
    if(!ship) {InterlockedIncrement64(&pcov_none);return;}
    unsigned char *defaults=pcov_fn.defaults(pcov_load(ship+PCOV_SHIP_MACRO));
    unsigned char **begin=(unsigned char**)pcov_load(defaults+PCOV_DEF_STANCES),**end=(unsigned char**)pcov_load(defaults+PCOV_DEF_STANCES+8);
    if(begin==end || !pcov_fn.stance_can(ship)) {InterlockedIncrement64(&pcov_none);return;}
    unsigned char *current=pcov_load(ship+PCOV_SHIP_STANCE),*next=NULL;
    if(!current) next=begin[0];
    else {
        unsigned char **at=begin;
        while(at!=end && *at!=current) at++;
        if((size_t)(at-begin)+1u<(size_t)(end-begin)) next=at[1];
        else if(pcov_load(pcov_fn.defaults(pcov_load(ship+PCOV_SHIP_MACRO))+PCOV_DEF_STANCE_NONE)) next=begin[0];
    }
    pcov_notice(slot,PCOV_KEY_STANCE,2);
    if(next) pcov_fn.stance_set(ship,next);else pcov_fn.stance_off(ship);
}
/* lockstep_pilot_stock.h: a replayed id of this module (the keys ride the record while it is installed). */
static int pcov_key_replayed(unsigned kind,int id) {
    return pcov_installed && kind==PSTK_ACTION && (id==PCOV_KEY_COVER || id==PCOV_KEY_STANCE);
}
/* The replay of one key: every handler the id reaches as pstk_replay_walk calls them, X4's player handler restated. */
static void pcov_key_walk(unsigned char *header,unsigned kind,int id,uint32_t value) {
    pnat_node *upper=NULL,*n=pnat_first((pnat_node*)header,id,&upper);
    const unsigned slot=pstk_scope_slot?pstk_scope_slot:1u;
    for(;n && n!=upper;n=pnat_next(n)) {
        if(n->handler!=(void*)(image+PCOV_HANDLER)) {pstk_call(n->handler,kind,id,value);continue;}
        unsigned char *player=pcov_singleton();
        InterlockedIncrement64(&pcov_keys[id==PCOV_KEY_STANCE]);
        if(slot<=PILOT_SLOTS) InterlockedIncrement64(&pcov_key_slots[slot]);
        if(!player) {InterlockedIncrement64(&pcov_none);continue;}
        __try {
            if(id==PCOV_KEY_COVER) {
                pcov_cover_key(slot,player);
                void *out=NULL;pcov_fn.occupied(NULL,&out);unsigned char *ship=out;
                if(ship && slot<=PILOT_SLOTS) {
                    void *pilot=NULL;((void**(*)(void*,void**))pcov_vtable(ship)[PCOV_SLOT_PILOT])(ship,&pilot);
                    pcov_last_key[slot][0]=pilot==(void*)player?'P':pilot?'n':'0';
                    pcov_last_key[slot][1]=pcov_load(ship+PCOV_OBJECT_COVER)?'y':'n';
                    pcov_last_key[slot][2]=pcov_load(player+PCOV_PLAYER_COVER)?'y':'n';pcov_last_key[slot][3]=0;
                }
            } else pcov_stance_key(slot);
        }
        __except(GetExceptionCode()==EXCEPTION_ACCESS_VIOLATION?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH) {
            InterlockedIncrement64(&pcov_faults);
        }
    }
}

/* ---- the readers on client k ---- */
/* The player the readers answer for: slot k's (>= 2) on its own client, in that client's UI (a local phase), never in an
 * apply, the probe's frame or a slot's scope; NULL: X4's function. */
static unsigned char *pcov_view_player(void) {
    if(pcov_view_test) return pcov_view_test();
    const unsigned k=pres_slot;
    if(!pcov_installed || k<2 || k>PILOT_SLOTS || pstk_scope_slot || GetCurrentThreadId()!=ui_thread || !uirep_local_phase() ||
       InterlockedCompareExchange(&uirep_replaying,0,0) || InterlockedCompareExchange(&rid_probe_frame,0,0)) return NULL;
    return pslots[k].player;
}
static int pcov_is(unsigned char *component,int cls) {
    return component && ((unsigned char(*)(void*,int))pcov_vtable(component)[PCOV_SLOT_IS_CLASS])(component,cls)!=0;
}
/* Slot 723 of a controllable (0x44d0e0) for a given player: the controllable's defaults allow the player's cover, or the
 * player's ability table names it. */
static unsigned char pcov_coverable_for(unsigned char *player,unsigned char *controllable) {
    const unsigned char *defaults=pcov_fn.ctrl_defaults(pcov_load(controllable+PCOV_SHIP_MACRO));
    unsigned char allowed=0;
    if(defaults && read_local(defaults+PCOV_CTRL_ALLOWED,&allowed,1) && allowed) return 1;
    return pcov_fn.ability(player,controllable)!=NULL;
}
/* CanPlayerActivateCoverAbility (0x177680) for a given player: no cooldown time on its blackboard, or that time passed. */
static unsigned char pcov_can_for(unsigned char *player,double now) {
    uint64_t key=0;
    if(!read_local(pcov_base+PCOV_COOLDOWN_KEY,&key,8)) return 1;
    unsigned char *const header=player+PCOV_BLACKBOARD,*node=pcov_load(header+8),*found=header;
    for(unsigned depth=0;node && depth<64;depth++) {
        uint64_t at=0;memcpy(&at,node+0x20,8);
        if(at<key) node=pcov_load(node+0x10);else {found=node;node=pcov_load(node+8);}
    }
    uint64_t at=0;
    if(found==header || (memcpy(&at,found+0x20,8),key<at)) return 1;
    const unsigned char *value=pcov_load(found+0x48);int type=0;double time=0;
    if(!value || !read_local(value,&type,4) || type!=8 || !read_local(value+8,&time,8) || !(time>-0.9999)) return 1;
    return !(now<time);
}
static const char *pcov_ui_ability(uint64_t id) {
    unsigned char *player=pcov_view_player(),*ship=player?uirep_component(id):NULL;
    if(!ship || !pcov_is(ship,PCOV_CLASS_SHIP)) {InterlockedIncrement64(&pcov_reads_stock[PCOV_R_ABILITY]);
        return ((const char*(*)(uint64_t))pcov_original[PCOV_R_ABILITY])(id);}
    InterlockedIncrement64(&pcov_reads_slot[PCOV_R_ABILITY]);
    return pcov_faction_id(pcov_fn.ability(player,ship));
}
static unsigned char pcov_ui_coverable(uint64_t id) {
    unsigned char *player=pcov_view_player(),*object=player?uirep_component(id):NULL;
    if(!object || !pcov_is(object,PCOV_CLASS_CONTROLLABLE) || pcov_vtable(object)[PCOV_SLOT_COVERABLE]!=pcov_coverable_fn) {
        InterlockedIncrement64(&pcov_reads_stock[PCOV_R_COVERABLE]);
        return ((unsigned char(*)(uint64_t))pcov_original[PCOV_R_COVERABLE])(id);
    }
    InterlockedIncrement64(&pcov_reads_slot[PCOV_R_COVERABLE]);
    return pcov_coverable_for(player,object);
}
static unsigned char pcov_ui_can(void) {
    unsigned char *player=pcov_view_player();double now=0;
    if(!player || !pnat_time_tls(&now)) {InterlockedIncrement64(&pcov_reads_stock[PCOV_R_CAN]);return ((unsigned char(*)(void))pcov_original[PCOV_R_CAN])();}
    InterlockedIncrement64(&pcov_reads_slot[PCOV_R_CAN]);
    return pcov_can_for(player,now);
}
static const char *pcov_ui_cover(void) {
    unsigned char *player=pcov_view_player();
    if(!player) {InterlockedIncrement64(&pcov_reads_stock[PCOV_R_COVER]);return ((const char*(*)(void))pcov_original[PCOV_R_COVER])();}
    InterlockedIncrement64(&pcov_reads_slot[PCOV_R_COVER]);
    if(!pcov_has_cover(player)) return "";
    const unsigned char *faction=((unsigned char*(*)(void*))pcov_vtable(player)[PCOV_SLOT_COVER])(player);
    return faction?pcov_faction_id(faction):"";
}
static void *const pcov_wrappers[PCOV_READS]={(void*)pcov_ui_ability,(void*)pcov_ui_coverable,(void*)pcov_ui_can,(void*)pcov_ui_cover};

/* ---- pins, page, entries ---- */
static int pcov_check_pins(const unsigned char *base) {
    for(unsigned i=0;i<PCOV_PIN_COUNT;i++) if(!hash_ok(base+pcov_pins[i].rva,pcov_pins[i].size,pcov_pins[i].sha)) {pin_failed_rva=pcov_pins[i].rva;return 0;}
    for(unsigned i=0;i<PCOV_READS;i++)
        if(!hash_ok(base+pcov_reads[i].rva,pcov_reads[i].size,pcov_reads[i].sha) || memcmp(base+pcov_reads[i].rva,pcov_reads[i].head,pcov_reads[i].length)) {
            pin_failed_rva=pcov_reads[i].rva;return 0;
        }
    return 1;
}
static unsigned char *pcov_alloc_page(void) {
    uintptr_t at=(uintptr_t)(image+0x1000000u)&~(uintptr_t)65535;
    for(uintptr_t distance=65536;distance<0x60000000;distance+=65536)
        for(int sign=-1;sign<=1;sign+=2) {
            uintptr_t candidate=sign<0?at-distance:at+distance;
            unsigned char *p=VirtualAlloc((void*)candidate,PCOV_PAGE,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
            if(!p) continue;
            int reachable=1;
            for(unsigned i=0;i<PCOV_READS;i++) {   /* the entry jumps, and the moved displacement back to its target */
                intptr_t d=(intptr_t)p-(intptr_t)(image+pcov_reads[i].rva+5);
                if(d<=INT32_MIN+0x10000 || d>=INT32_MAX-0x10000) reachable=0;
            }
            if(reachable) return p;
            VirtualFree(p,0,MEM_RELEASE);
        }
    return NULL;
}
/* Stub i: jmp [rip] to the wrapper; trampoline i: the head (its displacement moved), jmp [rip] behind it. 0: a
 * displacement that does not reach. */
static int pcov_write_entry(unsigned char *p,unsigned i) {
    memset(p,0xcc,PCOV_STRIDE);
    pview_abs_jump(p,pcov_wrappers[i]);
    unsigned char *t=p+0x20;const unsigned n=pcov_reads[i].length;
    memcpy(t,pcov_reads[i].head,n);
    if(pcov_reads[i].rip) {
        int32_t old=0;memcpy(&old,t+pcov_reads[i].rip,4);
        const int64_t moved=(int64_t)old+((int64_t)(intptr_t)(image+pcov_reads[i].rva)-(int64_t)(intptr_t)t);
        if(moved<INT32_MIN || moved>INT32_MAX) return 0;
        const int32_t value=(int32_t)moved;memcpy(t+pcov_reads[i].rip,&value,4);
    }
    pview_abs_jump(t+n,image+pcov_reads[i].rva+n);
    return 1;
}
static void pcov_uninstall(void) {
    for(unsigned i=0;i<PCOV_READS;i++)
        if(pcov_on[i] && pview_cas(pcov_reads[i].rva,pcov_patched[i],pcov_reads[i].head,5)) pcov_on[i]=0;
    int left=0;for(unsigned i=0;i<PCOV_READS;i++) left|=pcov_on[i];
    if(!left) pcov_installed=0;else pcov_why="restore";
}
static int pcov_install(void) {
    if(!pcov_page) pcov_page=pcov_alloc_page();
    if(!pcov_page) {pcov_why="page";return 0;}
    DWORD old,ignored;
    if(!VirtualProtect(pcov_page,PCOV_PAGE,PAGE_READWRITE,&old)) {pcov_why="page";return 0;}
    memset(pcov_page,0xcc,PCOV_PAGE);
    for(unsigned i=0;i<PCOV_READS;i++)
        if(!pcov_write_entry(pcov_page+i*PCOV_STRIDE,i)) {VirtualProtect(pcov_page,PCOV_PAGE,PAGE_EXECUTE_READ,&ignored);pcov_why="displacement";return 0;}
    if(!VirtualProtect(pcov_page,PCOV_PAGE,PAGE_EXECUTE_READ,&ignored)) {pcov_why="page";return 0;}
    FlushInstructionCache(GetCurrentProcess(),pcov_page,PCOV_PAGE);
    for(unsigned i=0;i<PCOV_READS;i++) pcov_original[i]=pcov_page+i*PCOV_STRIDE+0x20;
    pcov_installed=1;   /* the wrappers answer for X4 from the first jump on */
    for(unsigned i=0;i<PCOV_READS;i++) {
        const int32_t rel=(int32_t)((intptr_t)(pcov_page+i*PCOV_STRIDE)-(intptr_t)(image+pcov_reads[i].rva+5));
        pcov_patched[i][0]=0xe9;memcpy(pcov_patched[i]+1,&rel,4);
        if(!pview_cas(pcov_reads[i].rva,pcov_reads[i].head,pcov_patched[i],5)) {pcov_uninstall();pcov_installed=0;pcov_why="entry";return 0;}
        pcov_on[i]=1;
    }
    return 1;
}
/* Every gate tick: each entry still jumps to this page (else stop 246, entry_lost). */
static int pcov_check(void) {
    if(!pcov_installed) return 1;
    for(unsigned i=0;i<PCOV_READS;i++) {
        unsigned char now[5];
        if(!read_local(image+pcov_reads[i].rva,now,5) || memcmp(now,pcov_patched[i],5)) {pcov_why="entry_lost";return 0;}
    }
    return 1;
}
static void pcov_fns_init(void) {
    pcov_base=image;
    pcov_fn.occupied=(void**(*)(void*,void**))(image+PCOV_OCCUPIED);pcov_fn.ability=(unsigned char*(*)(void*,void*))(image+PCOV_ABILITY);
    pcov_fn.defaults=(unsigned char*(*)(void*))(image+PCOV_DEFAULTS);pcov_fn.stance_can=(unsigned char(*)(void*))(image+PCOV_STANCE_CAN);
    pcov_fn.stance_set=(void(*)(void*,void*))(image+PCOV_STANCE_SET);pcov_fn.stance_off=(void(*)(void*))(image+PCOV_STANCE_OFF);
    pcov_fn.notice=(void(*)(void*,int,int,int,void*))(image+PCOV_NOTICE);pcov_fn.ctrl_defaults=(unsigned char*(*)(void*))(image+PCOV_CTRL_DEFAULTS);
}
/* gate.cover_slots(): before the snapshot load and the first admitted frame, on the UI thread, with the stock slots, the
 * presentation and the UI replay's flight family armed; 1 armed. */
static int pcov_arm(void) {
    if(pcov_installed) return stats.active!=0;
    if(!stats.active || !boot_mode || boot_phase || stats.issued || GetCurrentThreadId()!=ui_thread) {pcov_why="late";return 0;}
    if(!(uirep_families&UIREP_FAMILY_FLIGHT) || !uirep_fn.find) {pcov_why="needs_flight_family";return 0;}
    if(!pcov_pins_checked) pcov_pins_checked=pcov_check_pins(image)?1:-1;
    if(pcov_pins_checked!=1) {pcov_why="pins";return 0;}
    void **vt=(void**)(image+0x2b84a98u);   /* U::Ship's vtable: slot 723 as the readers expect it */
    pcov_coverable_fn=vt[PCOV_SLOT_COVERABLE];
    if(pcov_coverable_fn!=(void*)(image+PCOV_COVERABLE_FN)) {pin_failed_rva=PCOV_COVERABLE_FN;pcov_why="pins";return 0;}
    pcov_fns_init();
    if(!pcov_install()) return 0;
    pcov_armed=1;pcov_why="-";return 1;
}
/* gate.cover_slots_look(k): slot k's cover as the simulation holds it (canonical, read only): its ship's cover owner,
 * its player's, the ability's faction for the ship, the last chosen one, whether the cooldown is over, the last notice,
 * the last key's pilot and covers (pcov_last_key). */
static int pcov_look(unsigned k,char *out,size_t capacity) {
    unsigned char *player=k==1?pslot_p1:k>=2 && k<=PILOT_SLOTS?pslots[k].player:NULL,*ship=NULL;double now=0;
    /* also with the module off (a measurement of X4's own state): its pins hold, X4's functions are called as they are */
    if(!pcov_installed && image && !pcov_base) {
        if(!pcov_pins_checked) pcov_pins_checked=pcov_check_pins(image)?1:-1;
        if(pcov_pins_checked==1) pcov_fns_init();
    }
    if(!pcov_base || !pcov_fn.ability || !player) return snprintf(out,capacity,"slot=%u none",k);
    if(k==1) {void *out_ship=NULL;if(!pstk_scope_slot) pcov_fn.occupied(NULL,&out_ship);ship=out_ship;}
    else if(pslots[k].seated) ship=pslots[k].ship;
    const char *ship_cover="-",*player_cover="-",*ability="-",*last="-";unsigned char can=0;
    __try {
        if(ship) {unsigned char *f=pcov_load(ship+PCOV_OBJECT_COVER);ship_cover=f?pcov_faction_id(f):"none";
                  f=pcov_fn.ability(player,ship);ability=f?pcov_faction_id(f):"none";}
        {unsigned char *f=pcov_load(player+PCOV_PLAYER_COVER);player_cover=f?pcov_faction_id(f):"none";}
        {unsigned char *f=pcov_load(player+PCOV_PLAYER_LAST);last=f?pcov_faction_id(f):"none";}
        can=pnat_time_tls(&now)?pcov_can_for(player,now):0;
    }
    __except(GetExceptionCode()==EXCEPTION_ACCESS_VIOLATION?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH) {return snprintf(out,capacity,"slot=%u fault",k);}
    unsigned char *stance=ship?pcov_load(ship+PCOV_SHIP_STANCE):NULL;
    return snprintf(out,capacity,"slot=%u ship=%s ship_cover=%s player_cover=%s ability=%s last=%s stance=%s can=%u notice=%x key=%s",k,ship?"yes":"none",
                    ship_cover,player_cover,ability,last,stance?"on":"none",can,k<=PILOT_SLOTS?pcov_last_state[k]:0u,
                    k<=PILOT_SLOTS && pcov_last_key[k][0]?pcov_last_key[k]:"-");
}
static int pcov_status(char *out,size_t capacity) {
    int n=snprintf(out,capacity,"installed=%d keys=%lld/%lld shown=%lld held=%lld none=%lld failures=%lld reads_slot=%lld/%lld/%lld/%lld "
        "reads_stock=%lld/%lld/%lld/%lld why=%s slots=",pcov_installed,(long long)pcov_keys[0],(long long)pcov_keys[1],(long long)pcov_shown,
        (long long)pcov_held,(long long)pcov_none,(long long)pcov_faults,(long long)pcov_reads_slot[0],(long long)pcov_reads_slot[1],
        (long long)pcov_reads_slot[2],(long long)pcov_reads_slot[3],(long long)pcov_reads_stock[0],(long long)pcov_reads_stock[1],
        (long long)pcov_reads_stock[2],(long long)pcov_reads_stock[3],pcov_why);
    for(unsigned k=1;n>0 && (size_t)n<capacity && k<=PILOT_SLOTS;k++)
        n+=snprintf(out+n,capacity-(size_t)n,"%s%lld",k>1?"/":"",(long long)pcov_key_slots[k]);
    return n>0 && (size_t)n<capacity?n:0;
}
