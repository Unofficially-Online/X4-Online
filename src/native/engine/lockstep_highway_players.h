/* Every player's own superhighway, identically on every client (native_gate_highway_players, a session default; stop 237).
 * Measured 25 Nov (x4ls play --solo of a continued rk9 session, the same goto and throttle for both players): X4's own
 * player flies into the superhighway of cluster_104 (Mars to Venus to the Moon), rides it inside the tube for 3.5 s and
 * leaves it in sector001; player 2 entered it, saw no tube, no cockpit frame and no highway HUD, showed 0 m/s and sat at
 * the entry until a script moved the ship away (twice: 90 s and 80 s).
 *
 * What X4 does [F, X4.exe 9.00, read in the image]. A highway (U::Highway, vtable 0x2b091e0) whose attention rises to 8
 * (Highway slot 701 0x5ffb60; the player or the camera in it, with native_gate_view_points any slot's ship) creates its
 * HighwayScene (0x5ff650, at highway +0x680): the tube, its boosters and blockers, the lanes' traffic. Every ship in the
 * highway is handed to the scene: X4's player's ship as its player (0x5f0240 with 1), every other one as traffic
 * (0x5f0320: a HighwayShipController moves it relative to the player), at the scene's start (slot 701) and when a ship
 * enters (slot 129 0x5ff910) or leaves (slot 130 0x5ff9b0: traffic removed by 0x5f04a0). The test is X4's player's ship
 * (0x9f7780). The scene and its traffic follow the player through the camera anchor's controllable (0x9f9da0): the
 * scene's update (HighwayScene slot 674 0x5edf00) and slot 684, its speed scale 0x5ef2c0, its boosters 0x5f1110 and
 * HighwayBooster slot 870, the scene's controller (HighwaySceneController slots 6/7: the scene's transform and velocity
 * are the player's) and the traffic's (HighwayShipController slots 6/7); the traffic density asks X4's player's ship
 * (0x5efab4). The highway flight model takes the player branch (HighwayFlightControlModel slot 8 0xeb07a0, slot 14) for
 * the player's controllable (0x9f9800, by runtime ID).
 *
 * What went wrong for players 2..N: all of those answer slot 1 on every client (the camera anchor is X4's own player
 * outside the presentation scope, X4's player is slot 1's). Slot 2's highway had its scene (attention 8, live: highway
 * 0x3805e, scene 0x1848dc), but slot 2's ship was handed to it as traffic, whose controller placed it relative to slot 1
 * in another system: no speed, and the tube followed slot 1.
 *
 * Here, on every client alike (simulation): each of those 15 calls goes through a trampoline on this module's page near
 * the image that passes the call's context registers (the scene, booster or controller; the ship and the highway; the
 * ship's ID) to a rule. A highway's player:
 *   - X4's answer when that ship is in the highway (its parent is the highway or the highway's scene): slot 1, as stock;
 *   - else the slot 2..N ship the highway took as its player (recorded at slot 129 / slot 701, first come, cleared at
 *     slot 130 when it leaves);
 *   - else the first seated slot 2..N ship in slot order that is in it; else X4's answer (nothing changes).
 * The camera calls and the density answer that ship; slot 129 / 701 answer the asked slot ship as X4's player's ship when
 * it is (or becomes) the recorded player, slot 130 when it was; the ID calls answer the asked ID when its ship is its
 * highway's player. So each highway's scene follows its own player, the player is not its traffic, and its flight model
 * is the player's, as X4 makes them for its one player; slot k's own client sees the tube around slot k's ship (its camera
 * is in that ship). Every input is the simulation's (the slots' seats, the ships' parents, the record kept in the
 * simulation's order); nothing here reads a client's camera, view or input. Two players in one highway: the scene's
 * player is the first one (slot 1 whenever it is there, as X4 adds it), the other is its traffic, as X4 treats every
 * other ship in its player's highway. Not covered: the highway parameter of UniverseFrameUpdate (lockstep_region_camera.h
 * answers it per view point), the Linux gate (no twin: a mixed session leaves this off).
 * Pins (refuse the arming): the three functions and a block around each site (its context registers' set-up), the
 * sites' calls as X4's. Installed before the snapshot load (CAS of each 16-byte block a site touches; nothing runs them
 * then); every gate tick checks the sites (stop 237 'highway players', why site_lost); the stop restores by CAS. The
 * region camera's pins of the flight model read these sites as X4's (hwp_original_view). The camera probe (a diagnostic
 * on nine of the sites) is refused beside it. Status HIGHWAY_PLAYERS. */
#define HWP_STOP_REASON 237u
#define HWP_HIGHWAY_VT 0x2b091e0u        /* U::Highway */
#define HWP_SCENE 0x680u                 /* Highway: its HighwayScene */
#define HWP_CAMERA_CTRL 0x9f9da0u        /* the camera anchor's controllable (rcx unused, rdx out) */
#define HWP_PLAYER_SHIP 0x9f7780u        /* X4's player's ship (rdx out) */
#define HWP_PLAYER_ID 0x9f9800u          /* X4's player's controllable's runtime ID (returned) */
#define HWP_PAGE 0x1000u
#define HWP_TRAMP 32u                    /* bytes per trampoline */
#define HWP_DEPTH 12u
#define HWP_RECORDS 16u
enum {HWP_KIND_CAMERA=0,HWP_KIND_SHIP=1,HWP_KIND_ID=2};
enum {HWP_CTX_OBJECT=0,HWP_CTX_CONTROLLER=1,HWP_CTX_ADD=2,HWP_CTX_REMOVE=3,HWP_CTX_ID=4};
/* mov: the context into r8 (camera, ship kinds) or rdx (the ID kind); mov2: the highway into r9 (add, remove), else none. */
typedef struct {unsigned site,target;unsigned char kind,ctx,mov[3],mov2[3];unsigned block,block_size;const char *block_sha;} hwp_site;
static const hwp_site hwp_sites[]={
    {0x5edf47u,HWP_CAMERA_CTRL,HWP_KIND_CAMERA,HWP_CTX_OBJECT,{0x4c,0x8b,0xc6},{0},0x5edf24u,0x28u,
     "baec322ba8ad3475203b364ccfef48ed2c012f745bfe984b1ba88b80aa5111c4"},   /* HighwayScene slot 674: rsi the scene */
    {0x5ee8ceu,HWP_CAMERA_CTRL,HWP_KIND_CAMERA,HWP_CTX_OBJECT,{0x4c,0x8b,0xc7},{0},0x5ee8c0u,0x20u,
     "d719c5b990a56ff998c1652b3ca08655710c485a736a5cc7d189ec34378224c0"},   /* HighwayScene slot 684: rdi the scene */
    {0x5ec5ccu,HWP_CAMERA_CTRL,HWP_KIND_CAMERA,HWP_CTX_OBJECT,{0x4c,0x8b,0xc5},{0},0x5ec4a0u,0x131u,
     "9c56bd87488ce07eaa859b7eeba7a97f8dcb5ba6d97788e2dace949c5b053c96"},   /* HighwayBooster slot 870: rbp the booster */
    {0x5ef31du,HWP_CAMERA_CTRL,HWP_KIND_CAMERA,HWP_CTX_OBJECT,{0x4c,0x8b,0xc5},{0},0x5ef2f9u,0x29u,
     "80c1a7b986e4a5649f07e2833289fabeb9ec393204d47c8cc294f8938eb6f7f4"},   /* 0x5ef2c0, the speed scale: rbp the scene */
    {0x5f155fu,HWP_CAMERA_CTRL,HWP_KIND_CAMERA,HWP_CTX_OBJECT,{0x4d,0x8b,0xc5},{0},0x5f1549u,0x1bu,
     "8ba3c932742b78fcf6eb74e9c05b10cfae84d0542c3505eca09946f8a06afc51"},   /* 0x5f1110, the boosters: r13 the scene */
    {0xda0a6du,HWP_CAMERA_CTRL,HWP_KIND_CAMERA,HWP_CTX_CONTROLLER,{0x4c,0x8b,0xc3},{0},0xda0a38u,0x3au,
     "7f953016e07d8be767b24d46947856aa8182bfb6a737a970da108256469dfdf3"},   /* HighwaySceneController slot 6: rbx */
    {0xda0cf6u,HWP_CAMERA_CTRL,HWP_KIND_CAMERA,HWP_CTX_CONTROLLER,{0x4c,0x8b,0xc3},{0},0xda0cdcu,0x1fu,
     "138b539588ea94661ec8c4c98df5a106163682ad007438e289a9f5463e139c42"},   /* HighwaySceneController slot 7: rbx */
    {0xda0ff1u,HWP_CAMERA_CTRL,HWP_KIND_CAMERA,HWP_CTX_CONTROLLER,{0x4c,0x8b,0xc6},{0},0xda0fe5u,0x11u,
     "13d564ae30505f9787f6f760c974938281beb4361ec5c01028c0216e869cb38d"},   /* HighwayShipController slot 6: rsi */
    {0xda161bu,HWP_CAMERA_CTRL,HWP_KIND_CAMERA,HWP_CTX_CONTROLLER,{0x4c,0x8b,0xc7},{0},0xda1618u,0x08u,
     "03a2c15e5008bfada9ba69a491b900c809a2b7be22b25b2b5f931eb4980143f2"},   /* HighwayShipController slot 7: rdi */
    {0x5efab4u,HWP_PLAYER_SHIP,HWP_KIND_SHIP,HWP_CTX_OBJECT,{0x4d,0x8b,0xc7},{0},0x5efaadu,0x13u,
     "a0aca229d01c07ae66356c79d7fe6332ab276669a3df6e3df21532fe4dcc7da8"},   /* the traffic density 0x5ef820: r15 the scene */
    {0x5ff984u,HWP_PLAYER_SHIP,HWP_KIND_SHIP,HWP_CTX_ADD,{0x4c,0x8b,0xc3},{0x4c,0x8b,0xce},0x5ff979u,0x16u,
     "e177551f3cf991412849a193fa21f3ec754644635fd5fd2b29d4730da7d8b1ef"},   /* Highway slot 129: rbx the ship, rsi the highway */
    {0x5ffa49u,HWP_PLAYER_SHIP,HWP_KIND_SHIP,HWP_CTX_REMOVE,{0x4c,0x8b,0xc3},{0x4c,0x8b,0xce},0x5ffa44u,0x10u,
     "f7c6361fc4e45f71554323a7931a44d7427503bc2f41ce8fa9e280be7a798fa0"},   /* Highway slot 130: rbx the ship, rsi the highway */
    {0x5ffc70u,HWP_PLAYER_SHIP,HWP_KIND_SHIP,HWP_CTX_ADD,{0x4c,0x8b,0xc7},{0x4c,0x8b,0xce},0x5ffc6cu,0x16u,
     "8dd4294c73231a3b9c091aa7b52d25af1974be1e90f2715325eac516f51d01bc"},   /* Highway slot 701: rdi each ship, rsi the highway */
    {0xeb09a4u,HWP_PLAYER_ID,HWP_KIND_ID,HWP_CTX_ID,{0x48,0x8b,0xd7},{0},0xeb0995u,0x1eu,
     "594e043c3a45521793bd8cdd2033c7202dab10a7e7515d47ff15d3d7d79d6a29"},   /* the flight model's start: rdi the ship's ID */
    {0xeb1048u,HWP_PLAYER_ID,HWP_KIND_ID,HWP_CTX_ID,{0x48,0x8b,0xd3},{0},0xeb1039u,0x1du,
     "b66fa04e14632869d5330bf1c9b91e4fe31e4e1838f640d3820b209b9f4d7105"}};  /* the flight model's step: rbx the ship's ID */
#define HWP_SITES (sizeof(hwp_sites)/sizeof(*hwp_sites))
static const struct {unsigned rva,size;const char *sha;} hwp_fn_pins[]={
    {HWP_CAMERA_CTRL,0x10bu,"ed4a7c81450163592f2afd81d87ac3735f1da93b476f23a91baf0e1b53c04023"},
    {HWP_PLAYER_SHIP,0xd0u,"abfeb77ed86db4c268b0bbcb5d08c0a55e74652b87ecd88f7761bdd3791526ff"},
    {HWP_PLAYER_ID,0x150u,"3c5bd05017da5469bc73bfd7cd4a5e1571f0fffb8db98bb6e6d80eef958a5650"}};
typedef void **(*hwp_out_fn)(void *,void **);
typedef uint64_t (*hwp_id_fn)(void *);
typedef struct {unsigned char *highway,*ship;uint64_t id;} hwp_record;
static int hwp_armed,hwp_installed;
static const char *hwp_why="-";
static unsigned char *hwp_page;
static unsigned char hwp_patched[HWP_SITES][5];
static hwp_record hwp_records[HWP_RECORDS];
static SRWLOCK hwp_lock=SRWLOCK_INIT;
static volatile LONG64 hwp_calls[3],hwp_follow,hwp_players,hwp_ids,hwp_recorded_count,hwp_released,hwp_full,hwp_no_highway;
static struct {hwp_out_fn camera,ship;hwp_id_fn id;const unsigned char *highway_vt;} hwp_fn;   /* X4's, or a test's */
static unsigned char *(*hwp_slot_ship)(unsigned k);   /* tests; NULL: the seated slot's ship (pslots) */
static unsigned char *hwp_seated_ship(unsigned k) {
    if(hwp_slot_ship) return hwp_slot_ship(k);
    const pslot_slot *s=&pslots[k];
    return s->seated && s->ship?s->ship:NULL;
}
static uint64_t hwp_id_of(const unsigned char *p) {uint64_t id=0;if(p) read_local(p+8,&id,8);return id;}
/* The first U::Highway at p or up its parents; NULL none or unreadable. */
static unsigned char *hwp_highway_of(unsigned char *p) {
    for(unsigned depth=0;p && depth<HWP_DEPTH;depth++) {
        const unsigned char *vt=NULL;
        if(!read_local(p,&vt,8)) return NULL;
        if(vt==hwp_fn.highway_vt) return p;
        if(!read_local(p+0x70,&p,8)) return NULL;
    }
    return NULL;
}
/* A ship in the highway: its parent is the highway or the highway's scene. */
static int hwp_in(const unsigned char *ship,unsigned char *highway) {
    unsigned char *parent=NULL,*scene=NULL;
    if(!ship || !highway || !read_local(ship+0x70,&parent,8) || !parent) return 0;
    return parent==highway || (read_local(highway+HWP_SCENE,&scene,8) && scene && parent==scene);
}
static unsigned hwp_slot_of(const unsigned char *ship) {
    for(unsigned k=2;ship && k<=PILOT_SLOTS;k++) if(hwp_seated_ship(k)==ship) return k;
    return 0;
}
/* Under hwp_lock: the highway's recorded player (its ID checked), else NULL. */
static unsigned char *hwp_record_of(const unsigned char *highway) {
    for(unsigned i=0;i<HWP_RECORDS;i++)
        if(hwp_records[i].highway==highway && hwp_records[i].ship) return hwp_id_of(hwp_records[i].ship)==hwp_records[i].id?hwp_records[i].ship:NULL;
    return NULL;
}
/* The highway's player (header): NULL when X4's answer stands. */
static unsigned char *hwp_player_of(unsigned char *highway,unsigned char *native) {
    if(!highway) return NULL;
    if(native && hwp_in(native,highway)) return native;
    AcquireSRWLockShared(&hwp_lock);unsigned char *recorded=hwp_record_of(highway);ReleaseSRWLockShared(&hwp_lock);
    if(recorded && hwp_in(recorded,highway)) return recorded;
    for(unsigned k=2;k<=PILOT_SLOTS;k++) {unsigned char *ship=hwp_seated_ship(k);if(ship && hwp_in(ship,highway)) return ship;}
    return NULL;
}
/* Slot 129 / 701 (add): the asked slot ship becomes the highway's player when it has none; 1 = it is its player. */
static int hwp_record_add(unsigned char *highway,unsigned char *ship) {
    int is=0;
    AcquireSRWLockExclusive(&hwp_lock);
    unsigned char *recorded=hwp_record_of(highway);
    if(recorded==ship) is=1;
    else if(!recorded) {
        unsigned free_at=HWP_RECORDS;
        for(unsigned i=0;i<HWP_RECORDS;i++) {
            if(hwp_records[i].highway==highway) {free_at=i;break;}   /* a stale entry of this highway */
            if(!hwp_records[i].ship && free_at==HWP_RECORDS) free_at=i;
        }
        if(free_at<HWP_RECORDS) {hwp_records[free_at].highway=highway;hwp_records[free_at].ship=ship;hwp_records[free_at].id=hwp_id_of(ship);is=1;InterlockedIncrement64(&hwp_recorded_count);}
        else InterlockedIncrement64(&hwp_full);
    }
    ReleaseSRWLockExclusive(&hwp_lock);
    return is;
}
/* Slot 130 (remove): 1 = the leaving ship was the highway's player (the record goes). */
static int hwp_record_remove(unsigned char *highway,unsigned char *ship) {
    int was=0;
    AcquireSRWLockExclusive(&hwp_lock);
    for(unsigned i=0;i<HWP_RECORDS;i++)
        if(hwp_records[i].highway==highway && hwp_records[i].ship==ship) {memset(&hwp_records[i],0,sizeof(hwp_records[i]));was=1;InterlockedIncrement64(&hwp_released);}
    ReleaseSRWLockExclusive(&hwp_lock);
    return was;
}
static unsigned char *hwp_context_highway(const hwp_site *s,unsigned char *ctx) {
    if(s->ctx==HWP_CTX_CONTROLLER && ctx && !read_local(ctx+0x70,&ctx,8)) return NULL;
    return hwp_highway_of(ctx);
}
/* The camera and ship kinds: out in rdx, the context in r8, the highway in r9 (add, remove). */
static void **hwp_out_rule(unsigned i,void *unused,void **out,unsigned char *ctx,unsigned char *highway) {
    const hwp_site *s=&hwp_sites[i];
    void **answer=(s->kind==HWP_KIND_CAMERA?hwp_fn.camera:hwp_fn.ship)(unused,out);
    InterlockedIncrement64(&hwp_calls[s->kind]);
    if(!hwp_installed || !out) return answer;
    unsigned char *native=(unsigned char*)*out;
    if(s->ctx==HWP_CTX_OBJECT || s->ctx==HWP_CTX_CONTROLLER) {        /* the player of the context's highway */
        unsigned char *at=hwp_context_highway(s,ctx);
        if(!at) {InterlockedIncrement64(&hwp_no_highway);return answer;}
        unsigned char *player=hwp_player_of(at,native);
        if(player && player!=native) {*out=player;InterlockedIncrement64(&hwp_follow);}
        return answer;
    }
    /* "is this ship X4's player's" for a ship that enters / leaves: a slot ship that is (was) the highway's player */
    if(!ctx || ctx==native || !highway || !hwp_slot_of(ctx)) return answer;
    if(s->ctx==HWP_CTX_ADD) {
        if(native && hwp_in(native,highway)) return answer;   /* X4's own player is this highway's (stock) */
        if(hwp_record_add(highway,ctx)) {*out=ctx;InterlockedIncrement64(&hwp_players);}
    } else if(hwp_record_remove(highway,ctx)) {*out=ctx;InterlockedIncrement64(&hwp_players);}
    return answer;
}
/* The ID kind (rcx X4's, the ship's runtime ID in rdx): that ID when its ship is a slot's and its highway's player. */
static uint64_t hwp_id_rule(unsigned i,void *rcx,uint64_t id) {
    const uint64_t answer=hwp_fn.id(rcx);
    InterlockedIncrement64(&hwp_calls[HWP_KIND_ID]);(void)i;
    if(!hwp_installed || !id || id==answer) return answer;
    for(unsigned k=2;k<=PILOT_SLOTS;k++) {
        unsigned char *ship=hwp_seated_ship(k);
        if(!ship || hwp_id_of(ship)!=id) continue;
        unsigned char *highway=hwp_highway_of(ship);
        unsigned char *slot1=pilot_slots[0].object;
        if(highway && hwp_player_of(highway,slot1 && hwp_id_of(slot1)==answer?slot1:NULL)==ship) {InterlockedIncrement64(&hwp_ids);return id;}
        return answer;
    }
    return answer;
}
#define HWP_RULE(n) static void **hwp_rule_##n(void *a,void **b,unsigned char *c,unsigned char *d) {return hwp_out_rule(n,a,b,c,d);}
HWP_RULE(0) HWP_RULE(1) HWP_RULE(2) HWP_RULE(3) HWP_RULE(4) HWP_RULE(5) HWP_RULE(6) HWP_RULE(7) HWP_RULE(8)
HWP_RULE(9) HWP_RULE(10) HWP_RULE(11) HWP_RULE(12)
#undef HWP_RULE
static uint64_t hwp_rule_13(void *a,uint64_t id) {return hwp_id_rule(13,a,id);}
static uint64_t hwp_rule_14(void *a,uint64_t id) {return hwp_id_rule(14,a,id);}
static void *const hwp_rules[HWP_SITES]={(void*)hwp_rule_0,(void*)hwp_rule_1,(void*)hwp_rule_2,(void*)hwp_rule_3,(void*)hwp_rule_4,
    (void*)hwp_rule_5,(void*)hwp_rule_6,(void*)hwp_rule_7,(void*)hwp_rule_8,(void*)hwp_rule_9,(void*)hwp_rule_10,(void*)hwp_rule_11,
    (void*)hwp_rule_12,(void*)hwp_rule_13,(void*)hwp_rule_14};
/* mov r8|rdx, <context>; [mov r9, <highway>;] jmp [rip]; abs64 rule */
static void hwp_write_trampoline(unsigned char *p,unsigned i) {
    unsigned n=0;
    memcpy(p+n,hwp_sites[i].mov,3);n+=3;
    if(hwp_sites[i].mov2[0]) {memcpy(p+n,hwp_sites[i].mov2,3);n+=3;}
    p[n]=0xff;p[n+1]=0x25;memset(p+n+2,0,4);memcpy(p+n+6,&hwp_rules[i],8);
}
static void hwp_original(unsigned i,unsigned char out[5]) {
    const int32_t rel=(int32_t)((intptr_t)(image+hwp_sites[i].target)-(intptr_t)(image+hwp_sites[i].site+5));out[0]=0xe8;memcpy(out+1,&rel,4);
}
/* Another module's pin over [rva, rva+size) (lockstep_region_camera.h rc_pins_ok): these sites read as X4's. */
static void hwp_original_view(unsigned rva,unsigned char *copy,unsigned size) {
    if(!hwp_installed) return;
    for(unsigned i=0;i<HWP_SITES;i++) {
        unsigned char original[5];hwp_original(i,original);
        for(unsigned b=0;b<5;b++) {const unsigned at=hwp_sites[i].site+b;if(at>=rva && at<rva+size) copy[at-rva]=original[b];}
    }
}
static int hwp_pins(void) {
    for(unsigned i=0;i<sizeof(hwp_fn_pins)/sizeof(*hwp_fn_pins);i++)
        if(!hash_ok(image+hwp_fn_pins[i].rva,hwp_fn_pins[i].size,hwp_fn_pins[i].sha)) {pin_failed_rva=hwp_fn_pins[i].rva;return 0;}
    for(unsigned i=0;i<HWP_SITES;i++) {
        const hwp_site *s=&hwp_sites[i];unsigned char want[5],now[5];hwp_original(i,want);
        if(s->block>s->site || s->site+5>s->block+s->block_size || !hash_ok(image+s->block,s->block_size,s->block_sha) ||
           !read_local(image+s->site,now,5) || memcmp(now,want,5)) {pin_failed_rva=s->site;return 0;}
    }
    hwp_fn.camera=(hwp_out_fn)(void*)(image+HWP_CAMERA_CTRL);hwp_fn.ship=(hwp_out_fn)(void*)(image+HWP_PLAYER_SHIP);
    hwp_fn.id=(hwp_id_fn)(void*)(image+HWP_PLAYER_ID);hwp_fn.highway_vt=image+HWP_HIGHWAY_VT;
    return 1;
}
static unsigned char *hwp_alloc_page(void) {
    uintptr_t at=(uintptr_t)(image+0x7a0000u)&~(uintptr_t)65535;
    for(uintptr_t distance=65536;distance<0x70000000;distance+=65536)
        for(int sign=-1;sign<=1;sign+=2) {
            uintptr_t candidate=sign<0?at-distance:at+distance;
            unsigned char *p=VirtualAlloc((void*)candidate,HWP_PAGE,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
            if(!p) continue;
            int reachable=1;
            for(unsigned i=0;i<HWP_SITES;i++) {
                intptr_t d=(intptr_t)p-(intptr_t)(image+hwp_sites[i].site+5);
                if(d<=INT32_MIN+0x10000 || d>=INT32_MAX-0x10000) reachable=0;
            }
            if(reachable) return p;
            VirtualFree(p,0,MEM_RELEASE);
        }
    return NULL;
}
static void hwp_uninstall(void) {
    if(!hwp_installed) return;
    hwp_installed=0;   /* the rules answer X4's from here on */
    for(unsigned i=0;i<HWP_SITES;i++) {
        unsigned char original[5];hwp_original(i,original);
        if(!view_cas_bytes(hwp_sites[i].site,hwp_patched[i],original,5)) hwp_why="restore";
    }
}
/* The trampolines and the sites (CAS of each 16-byte block a site touches). 0 refused, nothing written. */
static int hwp_install(void) {
    if(hwp_installed) return 1;
    if(!hwp_page) hwp_page=hwp_alloc_page();
    if(!hwp_page) {hwp_why="page";return 0;}
    DWORD old,ignored;
    if(!VirtualProtect(hwp_page,HWP_PAGE,PAGE_READWRITE,&old)) {hwp_why="page";return 0;}
    memset(hwp_page,0xcc,HWP_PAGE);
    for(unsigned i=0;i<HWP_SITES;i++) hwp_write_trampoline(hwp_page+i*HWP_TRAMP,i);
    if(!VirtualProtect(hwp_page,HWP_PAGE,PAGE_EXECUTE_READ,&ignored)) {hwp_why="page";return 0;}
    FlushInstructionCache(GetCurrentProcess(),hwp_page,HWP_PAGE);
    memset(hwp_records,0,sizeof(hwp_records));
    hwp_installed=1;
    for(unsigned i=0;i<HWP_SITES;i++) {
        unsigned char original[5];hwp_original(i,original);
        const int32_t rel=(int32_t)((intptr_t)(hwp_page+i*HWP_TRAMP)-(intptr_t)(image+hwp_sites[i].site+5));
        hwp_patched[i][0]=0xe8;memcpy(hwp_patched[i]+1,&rel,4);
        if(!view_cas_bytes(hwp_sites[i].site,original,hwp_patched[i],5)) {
            for(unsigned j=0;j<i;j++) {unsigned char o[5];hwp_original(j,o);view_cas_bytes(hwp_sites[j].site,hwp_patched[j],o,5);}
            hwp_installed=0;hwp_why="site";return 0;
        }
    }
    return 1;
}
static const char *hwp_check_why="-";
static int hwp_check(void) {
    if(!hwp_installed) return 1;
    for(unsigned i=0;i<HWP_SITES;i++) {
        unsigned char now[5];
        if(!read_local(image+hwp_sites[i].site,now,5) || memcmp(now,hwp_patched[i],5)) {if(!strcmp(hwp_check_why,"-")) hwp_check_why="site_lost";return 0;}
    }
    return 1;
}
/* gate.highway_players(): before the snapshot load and the first admitted frame, on the UI thread; 1 armed. */
static int hwp_arm(void) {
    if(hwp_installed) return stats.active!=0;
    if(!stats.active || !boot_mode || boot_phase || stats.issued || GetCurrentThreadId()!=ui_thread) {hwp_why="late";return 0;}
    if(!hwp_pins()) {hwp_why="pins";return 0;}
    if(!hwp_install()) return 0;
    hwp_armed=1;return 1;
}
static int hwp_status(char *out,size_t capacity) {
    const int n=snprintf(out,capacity,"installed=%d calls=%lld/%lld/%lld follow=%lld players=%lld ids=%lld recorded=%lld released=%lld full=%lld "
        "no_highway=%lld why=%s check=%s",hwp_installed,(long long)hwp_calls[0],(long long)hwp_calls[1],(long long)hwp_calls[2],(long long)hwp_follow,
        (long long)hwp_players,(long long)hwp_ids,(long long)hwp_recorded_count,(long long)hwp_released,(long long)hwp_full,(long long)hwp_no_highway,
        hwp_why,hwp_check_why);
    return n>0 && (size_t)n<capacity?n:0;
}
