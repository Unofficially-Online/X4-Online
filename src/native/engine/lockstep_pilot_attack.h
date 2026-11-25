/* The player's attacks for every slot (part of the fire lease, lockstep_pilot_fire.h: armed with native_gate_pilot_fire,
 * a session default; stop reason 240).
 * User report, 24 Nov (rk8, player 2): shots of player 2 at a friendly ship did no damage, and the ship never turned
 * hostile as it does for player 1. The damage half was the hit claims' outgoing rule (lockstep_hit_native.h, fixed there).
 * This is the other half: what X4 does when the player's shot lands.
 * X4 9.00 has one rule for "the player's shot": the shooter (the class 0x49 ancestor of the weapon, bullet+0x2a0, or of a
 * missile's launcher) is the player's controlled ship, i.e. the first non-null entry of the universe player's controlled-object
 * vector (+0x308..+0x310) when it is the player's first class 0x70 ancestor (+0x70 chain; the player's vfunc +0x1c50 with 0
 * answers yes, 70e580). Inline in Bullet::UpdateFlight (d4f640: the ray's collision group 15, else 14; the two group filters
 * of the physics system, +0x2760 + 24*index, have the same masks, c6fa0, so no hit differs), Bullet::ApplyHit (d528f0: the
 * player's statistics and ACH_OVERKILL) and the attacked-event poster 51ff10, the one with a simulation consequence:
 *   0x520172  player && shooter == player's controlled ship: an AttackedComponentEvent (9b7cd0: kind, attacked object, hit
 *             component, weapon) posted on the PLAYER (99a3f0), which MD hears as event_player_attacked_object. The shared
 *             notifications.xml IllegalActivity.PlayerAttacks takes it: change_relation_on_attack attacker=player.controlled
 *             (the relation loss of faction.player), the warning, the reports to the police (AttackResponse). The attacked
 *             object's own "attacked by" handler (vfunc +0x1d48: Controllable 44ce30 -> Destructible 5203f0, the generic
 *             relation handling in code and the group's alarm) is NOT called for the player;
 *   otherwise the attacked-by handler, as for any NPC (slot k's ship is owned by faction.player: it counted as a player-owned
 *             NPC ship, Object::vfunc126 6d0b80 posts PlayerOwnedObjectAttackedObjectEvent).
 * Damage itself is never filtered by the shooter or a relation (51f7f0 asks the source, BulletDamageSource slot 17 = yes,
 * and the victim's modifier 51fcd0; the attacked-event throttle 51fd80 is per attacker and kind).
 * Here the decision at 0x520172 asks the same question for every player: a jump to a stub on a near page (sub rsp,0x20;
 * rcx = shooter rsi, rdx = X4's controlled ship of the singleton rdi, r8 = the singleton rbp; call patk_attacker; a player
 * back goes into rbp and on to X4's player path 0x52017c, NULL to the attacked-by path 0x5201c1). The answer: X4's own when
 * the singleton's ship shot (inside a slot's scope the singleton is that slot's player); else the slot player P_k
 * (lockstep_pilot_slots.h) whose controlled ship by X4's rule is the shooter (or slot 1's player inside another slot's
 * scope); else none. So slot k's shot posts its event on P_k: X4's player path builds it and posts it at 0x5201ba, a call
 * answered here too (patk_post). X4 posts at once (99a3f0 with immediate 1 and the hit's time): an event whose source has
 * no listener of its type is dropped there, and P_k has none of its own, so the event first goes to the scripts nobody
 * owns inside slot k's scope (lockstep_pilot_death.h pvehk_forward: player.entity/ship/controlled are slot k's there;
 * notifications.xml IllegalActivity), then X4's post runs on P_k (slot k's own cues), as the per-slot notices
 * pvehk_event_now do. First live run without that (ff-s2, 24 Nov): three attacks posted on P_2, none heard, relation
 * unchanged. The same for the second post an attack by a player-owned object makes (Object::vfunc126 0x6d0b80, the
 * attacker's own AttackedComponentEvent): PlayerOwnedObjectAttackedObjectEvent on the universe player, where X4 decides
 * whether a crewed target bails out (patk_owner_post). Deterministic on every client; what the script shows is slot
 * k's (notices per player). Bullets, missiles, turrets of the ship and rams alike: every caller of 51ff10. Not ported:
 * the player's combat check 0x70d280 that vfunc126 runs on the universe player (P_1) for every player-owned attacker,
 * and Object::vfunc125's post for a player-owned VICTIM (its call crosses two 16-byte blocks).
 * Not here (stock for slot 1 only, presentation): the statistics and the achievement of ApplyHit, the collision group index.
 * patk_slot_of_ship() is the one "whose shot is it" answer for other modules (the fire sound, presentation per client).
 * Pins: 51ff10..0x520172 and 0x52017c..0x5202d5 (no relocations; the post call inside), Object::vfunc126 (its post call
 * inside), every site's bytes, PostEvent.
 * Written by 16-byte CAS at arm (before the first tick), the sites and the page re-checked every gate tick (240:
 * site_changed, page_changed), restored at stop. */
#define PATK_STOP_REASON 240u
#define PATK_FN 0x51ff10u
#define PATK_SITE 0x520172u          /* test rbp,rbp; je 5201c1; cmp rsi,rdi; jne 5201c1 */
#define PATK_PLAYER_PATH 0x52017cu   /* the AttackedComponentEvent posted on rbp */
#define PATK_GROUP_PATH 0x5201c1u    /* the attacked object's vfunc +0x1d48 */
#define PATK_CONTROLLING 0x1c50u     /* the player's vfunc: controls something (with 0: yes) */
#define PATK_IS_CLASS 0x11e8u        /* component vtable: IsClass */
#define PATK_CLASS_CONTROLLED 0x70
#define PATK_DEPTH 16u
#define PATK_STUB_HANDLER 0x30u      /* the stub's data: handler, player path, attacked-by path */
#define PATK_POST_SITE 0x5201bau     /* call 99a3f0: the player path's post on rbp */
#define PATK_POST 0x99a3f0u          /* EventSource::PostEvent(source, event, time, immediate) */
#define PATK_POST_STUB 0x60u         /* on the page: jmp [rip]; patk_post */
#define PATK_OWNER_FN 0x6d0b80u      /* Object::vfunc126: a player-owned object attacked something */
#define PATK_OWNER_SITE 0x6d0cd4u    /* its call 99a3f0: the PlayerOwnedObjectAttackedObjectEvent on the universe player */
#define PATK_OWNER_STUB 0x70u        /* on the page: jmp [rip]; patk_owner_post */
#define PATK_STUB_SIZE 0x80u
static const unsigned char patk_site_original[10]={0x48,0x85,0xed,0x74,0x4a,0x48,0x3b,0xf7,0x75,0x45};
static const struct {unsigned rva,size;const char *sha;} patk_pins_list[]={
    {PATK_FN,0x262u,"0bfb19c54e1ced3b5b4bb7ad3de337a7fc8768973700ebeefe816b8734d1a223"},
    {PATK_PLAYER_PATH,0x159u,"d01378f842e98d830963b5a217c8eb84bd1bb4f8a9eb883c1593779ad868752a"},
    {PATK_POST,807u,"5ca12a5d17041238645af6d6028c718bb3714cc53e9dd6963769d51827087c0d"},
    {PATK_OWNER_FN,0x170u,"f5c7a43bd378fe80652506886775e7ee8b56aee7c80f8079d68561bfa80cc8ae"}};
static int patk_installed,patk_done,patk_restored,patk_pins_checked;
static unsigned char *patk_page;
static volatile LONG64 patk_own,patk_group,patk_slot[PILOT_SLOTS+1],patk_told,patk_unscoped,patk_owner_posts,patk_failures;
static const char *patk_stage="off";
static unsigned char *(*patk_singleton_test)(void);   /* tests: the universe player */
static void (*patk_told_test)(unsigned k,unsigned char *event,double time);   /* tests: in place of slot k's scope and forward */

/* X4's rule: the player's controlled ship, or NULL. */
static unsigned char *patk_controlled(unsigned char *player) {
    void **vtable=NULL,*entry=NULL;unsigned char **at=NULL,**end=NULL,*first=NULL,*c=NULL;
    if(!player || !read_local(player,&vtable,8) || !vtable || !read_local((unsigned char*)vtable+PATK_CONTROLLING,&entry,8) || !entry ||
       !((unsigned char(*)(void*,unsigned char))entry)(player,0)) return NULL;
    if(!read_local(player+0x308,&at,8) || !read_local(player+0x310,&end,8) || !at || end<at || end-at>4096) return NULL;
    for(;at<end && !first;at++) if(!read_local(at,&first,8)) return NULL;
    if(!first || !read_local(player+0x70,&c,8)) return NULL;
    for(unsigned depth=0;c && depth<PATK_DEPTH;depth++) {
        void *is=NULL;
        if(!read_local(c,&vtable,8) || !vtable || !read_local((unsigned char*)vtable+PATK_IS_CLASS,&is,8) || !is) return NULL;
        if(((unsigned char(*)(void*,int))is)(c,PATK_CLASS_CONTROLLED)) return c==first?c:NULL;
        unsigned char *parent=NULL;
        if(!read_local(c+0x70,&parent,8) || parent==c) return NULL;
        c=parent;
    }
    return NULL;
}
static unsigned char *patk_singleton(void) {
    unsigned char *universe=NULL,*player=NULL;
    if(patk_singleton_test) return patk_singleton_test();
    return read_local(image+VPL_UNIVERSE,&universe,8) && universe && read_local(universe+VPS_SINGLETON,&player,8)?player:NULL;
}
/* Slot 1's player: the canonical one the slot module latched, else the singleton (no scopes without it). */
static unsigned char *patk_player1(void) {return pslot_p1?pslot_p1:pstk_scope_slot?NULL:patk_singleton();}
/* Whose shot: the slot (1..N) whose player controls `ship` by X4's rule, else 0. The same answer on every client. */
static unsigned patk_slot_of_ship(const unsigned char *ship) {
    if(!ship) return 0;
    unsigned char *p1=patk_player1();
    if(p1 && patk_controlled(p1)==ship) return 1;
    for(unsigned k=2;k<=PILOT_SLOTS;k++) if(pslots[k].player && patk_controlled(pslots[k].player)==ship) return k;
    return 0;
}
/* The stub's question at 0x520172: the player the attack is posted on, NULL for the attacked-by path. */
static unsigned char *patk_attacker(unsigned char *shooter,unsigned char *controlled,unsigned char *player) {
    if(player && shooter==controlled) {InterlockedIncrement64(&patk_own);return player;}   /* X4's own answer */
    const unsigned k=player?patk_slot_of_ship(shooter):0;
    unsigned char *const p=k==1?patk_player1():k?pslots[k].player:NULL;
    if(p && p!=player) {InterlockedIncrement64(&patk_slot[k]);return p;}
    InterlockedIncrement64(&patk_group);return NULL;
}
/* The player path's post (0x5201ba). Slot k's player (k >= 2): the scripts nobody owns hear it first, inside slot k's
 * scope, then X4's post on P_k (which drops it unless one of slot k's own cues listens). Anyone else: X4's post. */
static void patk_post(unsigned char *source,unsigned char *event,double time,unsigned char immediate) {
    unsigned k=0;
    for(unsigned j=2;source && j<=PILOT_SLOTS && !k;j++) if(pslots[j].player==source) k=j;
    int entered=0;
    if(k && patk_told_test) {patk_told_test(k,event,time);InterlockedIncrement64(&patk_told);}
    else if(k) {
        entered=pstk_scope_slot==k?0:pslot_enter(k);
        if(pstk_scope_slot==k) {pvehk_forward(k,event,time);InterlockedIncrement64(&patk_told);}
        else InterlockedIncrement64(&patk_unscoped);
    }
    ((void(*)(void*,void*,double,unsigned char))(void*)(image+PATK_POST))(source,event,time,immediate);
    if(entered) (void)pslot_leave(k);
}
/* Object::vfunc126's post (0x6d0cd4): a player-owned object attacked something, and X4 tells the universe player
 * (PlayerOwnedObjectAttackedObjectEvent, the attacker's ID at +0x28; MD event_player_owned_attacked_object). The bail-out
 * of a crewed target is decided there (notifications.xml PlayerOwnedShipAttacks: hull below 75 %, shield below 20 %, a
 * chance quartered unless the attacker is player.occupiedship, every 30 s; the pilot bails when the rest of the crew has).
 * Outside every scope that player is player 1, so slot k's ship got a quarter of the chance and player 1's lines. When the
 * attacker is the ship slot k's player controls, the event goes to P_k as the attack itself does (patk_post). */
static uint64_t patk_ship_runtime(unsigned k) {
    unsigned char *ship=pslots[k].player?patk_controlled(pslots[k].player):NULL;uint64_t id=0;
    return ship && read_local(ship+8,&id,8)?id:0;
}
static void patk_owner_post(unsigned char *source,unsigned char *event,double time,unsigned char immediate) {
    uint64_t id=0;unsigned k=0;
    if(event && read_local(event+0x28,&id,8) && id) for(unsigned j=2;j<=PILOT_SLOTS && !k;j++) if(patk_ship_runtime(j)==id) k=j;
    if(k) {InterlockedIncrement64(&patk_owner_posts);patk_post(pslots[k].player,event,time,immediate);return;}
    ((void(*)(void*,void*,double,unsigned char))(void*)(image+PATK_POST))(source,event,time,immediate);
}
/* The redirected calls: X4's call of PostEvent at each site, ours to the page's jump. */
static const struct {unsigned rva,stub;} patk_calls[]={{PATK_POST_SITE,PATK_POST_STUB},{PATK_OWNER_SITE,PATK_OWNER_STUB}};
#define PATK_CALLS (sizeof(patk_calls)/sizeof(*patk_calls))
/* sub rsp,0x20; mov rcx,rsi; mov rdx,rdi; mov r8,rbp; call [handler]; add rsp,0x20; test rax,rax; je +9; mov rbp,rax;
 * jmp [player path]; jmp [attacked-by path]; the three addresses at +0x30. At +0x60 and +0x70 the posts' jumps:
 * jmp [rip]; patk_post / patk_owner_post. */
static void patk_write_stub(unsigned char *p) {
    static const unsigned char code[0x2b]={0x48,0x83,0xec,0x20, 0x48,0x89,0xf1, 0x48,0x89,0xfa, 0x49,0x89,0xe8,
        0xff,0x15,0x1d,0,0,0, 0x48,0x83,0xc4,0x20, 0x48,0x85,0xc0, 0x74,0x09, 0x48,0x89,0xc5,
        0xff,0x25,0x13,0,0,0, 0xff,0x25,0x15,0,0,0};
    static const unsigned char jump[6]={0xff,0x25,0,0,0,0};
    void *const data[3]={(void*)patk_attacker,(void*)(image+PATK_PLAYER_PATH),(void*)(image+PATK_GROUP_PATH)};
    void *const targets[PATK_CALLS]={(void*)patk_post,(void*)patk_owner_post};
    memset(p,0xcc,PATK_STUB_SIZE);
    memcpy(p,code,sizeof(code));memcpy(p+PATK_STUB_HANDLER,data,sizeof(data));
    for(unsigned i=0;i<PATK_CALLS;i++) {memcpy(p+patk_calls[i].stub,jump,6);memcpy(p+patk_calls[i].stub+6,&targets[i],8);}
}
static int patk_stub_intact(void) {
    unsigned char want[PATK_STUB_SIZE];
    if(!patk_page) return 0;
    patk_write_stub(want);return !memcmp(patk_page,want,sizeof(want));
}
/* The ten bytes of the decision and the five of each post call, original (0) or ours (1). */
static void patk_site_bytes(int ours,unsigned char out[10]) {
    memcpy(out,patk_site_original,10);
    if(!ours) return;
    out[0]=0xe9;int32_t rel=(int32_t)((intptr_t)patk_page-(intptr_t)(image+PATK_SITE+5));memcpy(out+1,&rel,4);
}
static void patk_call_bytes(unsigned i,int ours,unsigned char out[5]) {
    out[0]=0xe8;
    int32_t rel=(int32_t)((intptr_t)(ours?patk_page+patk_calls[i].stub:image+PATK_POST)-(intptr_t)(image+patk_calls[i].rva+5));memcpy(out+1,&rel,4);
}
/* 0 every site original, 1 every site ours, -1 anything else. */
static int patk_state(void) {
    unsigned char now[10],want[10],call[5],want_call[5];
    if(!read_local(image+PATK_SITE,now,10)) return -1;
    for(int ours=0;ours<=1;ours++) {
        if(ours && !patk_page) break;
        patk_site_bytes(ours,want);
        int same=!memcmp(now,want,10);
        for(unsigned i=0;same && i<PATK_CALLS;i++) {
            if(!read_local(image+patk_calls[i].rva,call,5)) return -1;
            patk_call_bytes(i,ours,want_call);same=!memcmp(call,want_call,5);
        }
        if(same) return ours;
    }
    return -1;
}
static int patk_pins(void) {
    if(!patk_pins_checked) {
        patk_pins_checked=1;
        for(unsigned i=0;i<sizeof(patk_pins_list)/sizeof(*patk_pins_list);i++)
            if(!hash_ok(image+patk_pins_list[i].rva,patk_pins_list[i].size,patk_pins_list[i].sha)) patk_pins_checked=-1;
    }
    return patk_pins_checked==1;
}
static unsigned char *patk_alloc_page(void) {
    uintptr_t at=(uintptr_t)(image+PATK_SITE)&~(uintptr_t)65535;
    for(uintptr_t distance=65536;distance<0x70000000;distance+=65536)
        for(int sign=-1;sign<=1;sign+=2) {
            unsigned char *p=VirtualAlloc((void*)(sign<0?at-distance:at+distance),0x1000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
            if(!p) continue;
            int reach=1;
            for(unsigned i=0;i<PATK_CALLS+1;i++) {
                intptr_t d=(intptr_t)p-(intptr_t)(image+(i<PATK_CALLS?patk_calls[i].rva:PATK_SITE)+5);
                if(!(d>INT32_MIN+0x1000 && d<INT32_MAX-0x1000)) reach=0;
            }
            if(reach) return p;
            VirtualFree(p,0,MEM_RELEASE);
        }
    return NULL;
}
/* Every site by CAS, the posts first (reached only through code ours or X4's own path); one fails: none. */
static int patk_swap(int ours) {
    unsigned char a[10],b[10],c[5],d[5];
    patk_site_bytes(!ours,a);patk_site_bytes(ours,b);
    if(ours) {
        unsigned done=0;
        for(;done<PATK_CALLS;done++) {patk_call_bytes(done,0,c);patk_call_bytes(done,1,d);if(!view_cas_bytes(patk_calls[done].rva,c,d,5)) break;}
        if(done==PATK_CALLS && view_cas_bytes(PATK_SITE,a,b,10)) return 1;
        while(done--) {patk_call_bytes(done,1,c);patk_call_bytes(done,0,d);(void)view_cas_bytes(patk_calls[done].rva,c,d,5);}
        return 0;
    }
    if(!view_cas_bytes(PATK_SITE,a,b,10)) return 0;
    int ok=1;
    for(unsigned i=0;i<PATK_CALLS;i++) {patk_call_bytes(i,1,c);patk_call_bytes(i,0,d);if(!view_cas_bytes(patk_calls[i].rva,c,d,5)) ok=0;}
    return ok;
}
/* With the fire lease's arm (ui thread, before the first tick). Refused: the fire lease refuses too (fail closed). */
static int patk_arm(void) {
    if(patk_installed) return 1;
    if(!patk_pins() || patk_state()!=0) {patk_stage="pins";return 0;}
    if(!patk_page) patk_page=patk_alloc_page();
    DWORD old,ignored;
    if(!patk_page || !VirtualProtect(patk_page,0x1000,PAGE_READWRITE,&old)) {patk_stage="page";return 0;}
    patk_write_stub(patk_page);
    if(!VirtualProtect(patk_page,0x1000,PAGE_EXECUTE_READ,&ignored)) {patk_stage="page";return 0;}
    FlushInstructionCache(GetCurrentProcess(),patk_page,0x1000);
    patk_own=patk_group=patk_told=patk_unscoped=patk_owner_posts=patk_failures=0;memset((void*)patk_slot,0,sizeof(patk_slot));
    patk_restored=0;
    if(!patk_swap(1)) {patk_stage="cas";return 0;}
    patk_done=1;patk_installed=1;patk_stage="armed";return 1;
}
static int patk_owned(void) {return !patk_done || (patk_state()==1 && patk_stub_intact());}
/* Every gate tick; a failure stops the gate with reason 240. */
static int patk_check(void) {
    if(!patk_installed) return 1;
    if(InterlockedCompareExchange64(&patk_failures,0,0)) return 0;
    const char *why=patk_owned()?NULL:patk_state()!=1?"site_changed":"page_changed";
    if(why) {patk_stage=why;InterlockedIncrement64(&patk_failures);return 0;}
    return 1;
}
/* At stop: the sites back by CAS (a foreign value is left); the page stays, so a call already past the jump returns. */
static void patk_restore(void) {
    if(!patk_installed) return;
    if(patk_done && patk_state()==1 && patk_swap(0)) patk_done=0;
    if(!patk_done) {patk_installed=0;patk_restored=1;}
}
static int patk_status(char *out,size_t capacity) {
    int n=snprintf(out,capacity,"attacks=%s own=%lld group=%lld told=%lld unscoped=%lld owner=%lld slots=",patk_stage,(long long)patk_own,
                   (long long)patk_group,(long long)patk_told,(long long)patk_unscoped,(long long)patk_owner_posts);
    for(unsigned k=1;n>0 && (size_t)n<capacity && k<=PILOT_SLOTS;k++)
        n+=snprintf(out+n,capacity-(size_t)n,"%s%lld",k>1?"/":"",(long long)patk_slot[k]);
    return n>0 && (size_t)n<capacity?n:0;
}
