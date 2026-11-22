/* Doors open for slots 2..N on foot as they open for X4's own player (part of lockstep_pilot_foot_body.h's room change;
 * on with it when the pins match; no stop reason of its own: a door that cannot be asked about stays as X4 has it).
 *
 * What X4 does [F, pinned 9.00]. A station's doors are parts tagged `door trigger_nearby` (a corridor's door_01, a dock
 * bay's transporter booth, interiors_arg_airlock_01_body: two door halves, ConnectionForanim_door_left/right), with
 * the animations deactivated, activating, active, deactivating. The tag sets bit 34 of the connection's flags (+0xf0;
 * the template reader 0x889cb0, the tag's value in the global 0x395cf48; bit 35: the tag on the part or above it).
 * U::Player's own update (0x7113e0, vfunc674), which X4 runs for every player object every frame, keeps the player's
 * list of interactables (+0x8a698: the containers near it at attention 9, 0x739a80) and then walks every trigger of
 * them (0x73a0f0). For each trigger it asks whether anyone stands at it: the entities of the container's registries
 * (the station's, the module's, the ship's rooms, the trigger's room: class 0x2c entities, +0x68 == 0), each given to
 * 0x739e30(_, entity, component, trigger) at four calls (0x73a4fe, 0x73a5e0, 0x73a7f3, 0x73a89c). 0x739e30 refuses an
 * entity flagged 0x200 (+0x3d0) and an NPC (class 0x48) without the flag 0x40 (crew at a post; idle people and the
 * players' stand-in figures do not open doors), asks the trigger's own filter (+0x48, 0x558740 with +0x20), wants the
 * entity in the trigger's room (0x75b5f0 against the trigger component's room) and then its place in the component
 * (0x3dde10) inside a cylinder round the trigger's box (the box +8: half sizes +0, centre +0x10, radius +0x30,
 * height +0x34). Someone there: a trigger at rest (state 4) is activated (TriggerAnimationEvent 4 to the room,
 * 0x73a9e0) and stays open; nobody: an open one (state 2) is deactivated (0x23).
 * A walking slot's player is in none of those registries (its parent is an upward pointer, lockstep_pilot_foot_body.h),
 * so no door ever opened for players 2..N (playtest of 21 Nov: player 2 stood at the closed door of a transporter
 * booth; its figure beside it opens nothing either, as an NPC without a post).
 * Here 0x739e30's entry jumps to a wrapper (0x73a0f0's four calls are its only ones; two of them cross a 16-byte block,
 * so they cannot be re-pointed one by one: run of 21 Nov 21:52 found the module not installed): X4's answer for the
 * entity it asks about, through a trampoline (the entry's first instruction, `mov [rsp+8],rcx`, then the rest of
 * X4's code), and when that is no, X4's test for each walking slot's player: the flag 0x200, the trigger's own filter
 * (0x558740 when +0x48 is set), the room (X4's 0x75b5f0 for the player and for the trigger's component: the gate keeps
 * the player's upward pointer at the room it walks in), and the cylinder with the player's place in the component from
 * GetTransformRelativeTo at the game's time (0x3db8a0, as gate.pilot_foot_place reads it). X4's own test with P_k as the
 * entity answered no for every one of 125,000 asks with player 2 at the booth's door (run of 21 Nov 22:13): its place
 * comes from 0x3dde10, which does not see P_k where it walks (P_k is off the tree). The answer for one trigger is kept
 * for the rest of that tick's walk of it (the same component, trigger and tick: the slots do not move within the pass).
 * All of it runs where X4 runs it, on every client at the same tick, from simulation state only.
 * Status (PILOT_FOOT bodies row): doors=installed/asks/slot asks/opened/other room/far (opened: a trigger nobody else
 * stood at, held by a slot's player). */
#define PFDOOR_PRESENT 0x739e30u
#define PFDOOR_PRESENT_SIZE 0x2bfu
#define PFDOOR_PRESENT_SHA "21d4e03f3429cf82ca133a323968fc86f9d66b4a333ca6086fbe9d0c4f4d5ebf"
#define PFDOOR_PASS 0x73a0f0u
#define PFDOOR_PASS_SIZE 0x8e7u
#define PFDOOR_PASS_SHA "4093c861f4ca1ed18c97b61590687c169b79f1d3a6d37b51ac5297c03c600e75"
#define PFDOOR_ROOM 0x75b5f0u                 /* (positional, out room, 1): the room an entity stands in */
#define PFDOOR_ROOM_SIZE 0x184u
#define PFDOOR_ROOM_SHA "c5003016ebb044f24bc33276d42569ca67c792248f9a8e11f415cb2611061658"
#define PFDOOR_FILTER 0x558740u               /* (entity, trigger + 0x20): the trigger's own condition on the entity */
#define PFDOOR_FILTER_SIZE 0xf0u
#define PFDOOR_FILTER_SHA "a33a0032429d1223183b89f2cb3ac372e98d390d3b65d11f5ea90a713e260956"
#define PFDOOR_STUB 0xf80u                    /* the slots' near page, after the figure's (0xf60): the entry's jump */
#define PFDOOR_TRAMPOLINE 0xfa0u              /* X4's first instruction, then a jump to the rest (5 + 14 bytes) */
static const unsigned char pfdoor_head[5]={0x48,0x89,0x4c,0x24,0x08};   /* mov [rsp+8],rcx */
typedef uint64_t (*pfdoor_present_fn)(void *,void *,void *,void *);
static pfdoor_present_fn pfdoor_native;       /* the trampoline at the arming, a test's */
static unsigned char pfdoor_patched[5];
static struct {const void *component,*trigger;uint64_t tick;int present,valid;} pfdoor_last;
/* The players of the slots 2..N that walk (a test may stand in its own). */
static unsigned char *pfdoor_walker(unsigned k) {
    return pfslot_walking_fpc(k) && pfslots[k].walking && !pilot_slots[k-1].dead?pslots[k].player:NULL;
}
static unsigned char *(*pfdoor_walker_of)(unsigned)=pfdoor_walker;
typedef void **(*pfdoor_room_fn)(void *,void **,unsigned char);
typedef unsigned char (*pfdoor_filter_fn)(void *,void *);
static pfdoor_room_fn pfdoor_room;
static pfdoor_filter_fn pfdoor_filter;
/* X4's cylinder (0x739e30's end): the place's distance outside the box's half sizes along each axis, then within the
 * radius across and the height up and down. box: half sizes [0..2], centre [4..6], radius [12], height [13]. */
static int pfdoor_in_cylinder(const float place[3],const float box[14]) {
    float d[3];
    for(unsigned i=0;i<3;i++) {
        float x=place[i]-box[4+i];
        if(x<=0.0f) {x=box[i]+x;if(x>=0.0f) x=0.0f;}
        else {x=x-box[i];if(x<=0.0f) x=0.0f;}
        d[i]=x;
    }
    return d[0]*d[0]+d[2]*d[2]<box[12]*box[12]+0.0001f && fabsf(d[1])<box[13]+0.0001f;
}
/* X4's test for a walking slot's player (header): 1 it stands at the trigger. */
static int pfdoor_slot_near(unsigned char *player,void *component,void *trigger) {
    uint32_t flags=0;uint64_t filter=0;float *box=NULL;float b[14];double now=0;void *mine=NULL,*theirs=NULL;int fault=0,in=0;
    __attribute__((aligned(16))) float m[16];
    if(!pftr_relative || !pfdoor_room || !pfdoor_filter || !pnat_fn.time || !pnat_fn.time(&now)) return 0;
    if(!read_local(player+0x3d0,&flags,4) || (flags&0x200u) || !read_local((unsigned char*)trigger+0x48,&filter,8) ||
       !read_local((unsigned char*)trigger+8,&box,8) || !box || !read_local(box,b,sizeof(b))) return 0;
    pftr_identity(m);
    __try {
        if(filter && !pfdoor_filter(player,(unsigned char*)trigger+0x20)) in=-1;
        else {
            pfdoor_room(player,&mine,1);pfdoor_room(component,&theirs,1);
            if(!mine || mine!=theirs) in=-2;
            else {pftr_relative(player,m,now,component,0);in=pfdoor_in_cylinder(m,b);}
        }
    }
    __except(GetExceptionCode()==EXCEPTION_ACCESS_VIOLATION?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH) {fault=1;}
    if(fault || in<0) {if(in==-2) InterlockedIncrement64(&pfdoor_other_room);return 0;}
    if(!in) InterlockedIncrement64(&pfdoor_far);
    return in;
}
static int (*pfdoor_slot_at)(unsigned char *,void *,void *)=pfdoor_slot_near;
/* Whether a walking slot's player stands at that trigger. */
static int pfdoor_slots_at(void *unused,void *component,void *trigger) {
    (void)unused;
    if(pfdoor_last.valid && pfdoor_last.component==component && pfdoor_last.trigger==trigger && pfdoor_last.tick==stats.issued)
        return pfdoor_last.present;
    int present=0;
    for(unsigned k=2;k<=PILOT_SLOTS && !present;k++) {
        unsigned char *player=pfdoor_walker_of(k);
        if(!player) continue;
        InterlockedIncrement64(&pfdoor_slot_asks);
        if(pfdoor_slot_at(player,component,trigger)) present=1;
    }
    pfdoor_last.component=component;pfdoor_last.trigger=trigger;pfdoor_last.tick=stats.issued;pfdoor_last.present=present;pfdoor_last.valid=1;
    if(present) InterlockedIncrement64(&pfdoor_opened);
    return present;
}
/* The four calls of 0x73a0f0 (header): X4's answer, else whether a walking slot stands there. */
static uint64_t pfdoor_present(void *unused,void *entity,void *component,void *trigger) {
    const uint64_t answer=pfdoor_native(unused,entity,component,trigger);
    InterlockedIncrement64(&pfdoor_asks);
    if((answer&0xffu) || !pfdoor_installed) return answer;
    return pfdoor_slots_at(unused,component,trigger)?(answer&~(uint64_t)0xffu)|1u:answer;
}
static void pfdoor_install(int on) {
    if(on && !pfdoor_installed) {
        DWORD old,ignored;int32_t rel=0;
        if(!pnat_base || (!pslot_page && !(pslot_page=pslot_alloc_page()))) return;
        if(!pftr_installed || !hash_ok(image+PFDOOR_PRESENT,PFDOOR_PRESENT_SIZE,PFDOOR_PRESENT_SHA) || !hash_ok(image+PFDOOR_PASS,PFDOOR_PASS_SIZE,PFDOOR_PASS_SHA) ||
           !hash_ok(image+PFDOOR_ROOM,PFDOOR_ROOM_SIZE,PFDOOR_ROOM_SHA) || !hash_ok(image+PFDOOR_FILTER,PFDOOR_FILTER_SIZE,PFDOOR_FILTER_SHA) ||
           memcmp(pnat_base+PFDOOR_PRESENT,pfdoor_head,sizeof(pfdoor_head))) return;
        pfdoor_room=(pfdoor_room_fn)(image+PFDOOR_ROOM);pfdoor_filter=(pfdoor_filter_fn)(image+PFDOOR_FILTER);pfdoor_slot_at=pfdoor_slot_near;
        if(!VirtualProtect(pslot_page,PVIEW_PAGE,PAGE_READWRITE,&old)) return;
        pview_abs_jump(pslot_page+PFDOOR_STUB,(void*)pfdoor_present);
        memcpy(pslot_page+PFDOOR_TRAMPOLINE,pfdoor_head,sizeof(pfdoor_head));
        pview_abs_jump(pslot_page+PFDOOR_TRAMPOLINE+sizeof(pfdoor_head),pnat_base+PFDOOR_PRESENT+sizeof(pfdoor_head));
        if(!VirtualProtect(pslot_page,PVIEW_PAGE,PAGE_EXECUTE_READ,&ignored)) return;
        FlushInstructionCache(GetCurrentProcess(),pslot_page,PVIEW_PAGE);
        pfdoor_native=(pfdoor_present_fn)(void*)(pslot_page+PFDOOR_TRAMPOLINE);pfdoor_walker_of=pfdoor_walker;
        memset(&pfdoor_last,0,sizeof(pfdoor_last));pfdoor_asks=pfdoor_slot_asks=pfdoor_opened=pfdoor_other_room=pfdoor_far=0;
        rel=(int32_t)((intptr_t)(pslot_page+PFDOOR_STUB)-(intptr_t)(pnat_base+PFDOOR_PRESENT+5));
        pfdoor_patched[0]=0xe9;memcpy(pfdoor_patched+1,&rel,4);
        if(!pview_cas(PFDOOR_PRESENT,pfdoor_head,pfdoor_patched,5)) return;
        pfdoor_installed=1;
    } else if(!on && pfdoor_installed) {
        if(pview_cas(PFDOOR_PRESENT,pfdoor_patched,pfdoor_head,5)) pfdoor_installed=0;
    }
}
