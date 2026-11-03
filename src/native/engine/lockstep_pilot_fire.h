/* Default-off firing for pilot slots (native_gate_pilot_fire; reports/
 * LOCKSTEP-HIT-CLAIMS-NATIVE-20261029.txt part A). The record applied at tick
 * t (trigger bit 0) makes the slot ship's primary guns fire at t on every
 * client through the native weapon code: each gun of the ship's active
 * primary weapon group (ship+0x638 groups of UniverseIDs, active index
 * +0x668; guns are exactly U::Weapon 0x2b68278, so turrets and missile
 * launchers are excluded) gets a native MindlessShootController (vtable
 * 0x2b7cf68, the controller the stock MD/AI action <shoot> installs: no
 * target, no angle/range/line-of-sight gate, gimbals at rest) at lease start,
 * and its one-byte trigger (controller+0xa0, read by slot 14 0x7e58a0) is
 * written from the record every tick at the pathing kick (path_release, on
 * the MoveWorker, after the pilot frame table is published, before slot 3).
 * Weapon::Move 0x8132d0 then does cooldown, heat, ammo, salvo and launch in
 * the component walk of that tick, as for any AI gun; the launch draws sit in
 * the gun's Move scope (keyed per ship with native_gate_actor_rng_all) and the
 * creation-frame hit test of each bullet is camera-independent only with
 * native_gate_bullet_cadence='every' (both required).
 * Lease (tick = window start, every client, slot order, gun order of the
 * group): a gun without a controller gets one from 0x819480 (start shooting:
 * installs Mindless, sets the trigger, posts the stock shoot events); a gun
 * with another controller first goes through 0x819680 (stop shooting:
 * deletes it); a gun that is firing waits for a later tick. Then the trigger
 * is cleared. Every tick, before the trigger is written, each leased gun is
 * checked: still found by its runtime id (else it was destroyed: dropped,
 * counted), its controller still ours (pointer and Mindless vtable), and the
 * trigger byte still what we wrote. Who changes them decides what happens:
 * - The simulation, identically on every client at the same tick: an
 *   AI-flown slot ship's fight script (<shoot_at> installs LargeTarget/
 *   Multiple controllers through 0x81ac60, <stop_shooting>/<shoot> clear or
 *   set our trigger through 0x819680/0x819480), or the weapon-mode update
 *   0x4ef380. The gun is re-taken at that path_release - stop shooting
 *   deletes the foreign controller, start shooting installs a Mindless - and
 *   the trigger is rewritten; counted (retaken, trigger_restored), not fatal.
 *   A gun that is firing is not taken mid-shot: it waits (retake_waits) and
 *   its trigger is left alone that tick, as at the lease.
 * - Possibly this client alone: any change on the local player's ship (its
 *   weapon-group and fire keys act there) and any player controller class
 *   (PlayerShootController 0x2b7d1a8, PlayerBombLauncherShootController
 *   0x2b7d2c8; vtables pinned by slots) on any ship. These stop the gate
 *   (reason 112, stage controller_replaced), fail closed.
 * The last replacement (tick, slot, gun, controller class, local or sim) is
 * in the status. The window end clears the triggers once (a gun the
 * simulation took meanwhile is just let go); every stop clears the triggers
 * that are still ours. Mode 'script' fires every slot in a fixed
 * schedule (30 of every 120 ticks from window start) for runs without humans;
 * 'record' uses the record's trigger bit, masked by its travel bit (the
 * engine refuses to fire during travel for the local player's ship only, so
 * the mask keeps that decision on every client). PFIRE_1 receipts (per tick:
 * guns firing, their last-shot times and magazines) go into the declared
 * state.
 * Stock slots (lockstep_pilot_stock.h) have no pilot record: 'record' reads the trigger bit of
 * the slot's stock record. X4's own PlayerShootController is on such a ship's guns (the seat, each
 * handoff, weapon mode updates): those run from replayed records, in the same tick on every client,
 * so it is taken back like the simulation's. Mindless means gimbals at rest: the guns shoot straight
 * ahead, not at the replayed aim; the stock fire and aim path per slot is a later step.
 * The magazine is refilled lazily: 0x81db10(gun, now) writes the last shot time (+0x310) and the
 * magazine (+0x350) once the reload time has passed, from the HUD's FFI readers (GetPrimaryWeapon,
 * GetSecondaryWeapon, GetWeaponDetails*) as well as from the simulation, and with --present each client's
 * HUD reads another ship's guns (live fire-S: digest mismatch from a few ticks after a burst until the next
 * one). So the receipt step calls it for every leased gun, on every client, at the frame's game time,
 * before it reads them: the state no longer depends on who asked. */
#define PFIRE_WEAPON_VTABLE 0x2b68278u
#define PFIRE_MINDLESS_VTABLE 0x2b7cf68u
#define PFIRE_PLAYER_VTABLE 0x2b7d1a8u      /* U::PlayerShootController */
#define PFIRE_PLAYER_BOMB_VTABLE 0x2b7d2c8u /* U::PlayerBombLauncherShootController */
#define PFIRE_START 0x819480u
#define PFIRE_STOP 0x819680u
#define PFIRE_CONTROLLER 0x318u
#define PFIRE_FLAG 0xa0u
#define PFIRE_FIRING 0x30du
#define PFIRE_LAST_SHOT 0x310u
#define PFIRE_MAGAZINE 0x350u
#define PFIRE_RELOAD 0x81db10u          /* the lazy refill: gun, now */
#define PFIRE_GROUPS 0x638u
#define PFIRE_ACTIVE 0x668u
#define PFIRE_GUNS 16u
/* Missiles (the UI replay's missile family, lockstep_ui_replay.h): the slot's secondary group (+0x650 groups, +0x66c active,
 * as the group cycle 0x4e4e20 reads them), its U::MissileLauncher weapons. While the record's secondary trigger is held and
 * the slot's soft target (its player's +0x6dd0, replayed by the target family) exists, each launcher gets the controller the
 * AI's <shoot_at> installs: 0x81ac60(launcher, target, empty list, 0, range 0 = the macro's, 0), a BasicShootController that
 * aims, locks and launches by itself in Weapon::Move; released (or the target or group changed), 0x819680 stops it. Every
 * client, path_release, slot order. */
#define PFIRE_GROUPS2 0x650u
#define PFIRE_ACTIVE2 0x66cu
#define PFIRE_LAUNCHER_VTABLE 0x2b02988u  /* U::MissileLauncher */
#define PFIRE_SHOOT_AT 0x81ac60u
#define PFIRE_SOFT_TARGET 0x6dd0u
static const unsigned char *uirep_slot_player(unsigned slot);  /* lockstep_ui_replay.h: P_1 or slot k's P_k */
#define PFIRE_STOP_REASON 112u
#define PFIRE_DOMAIN 0x5046495245315631ULL /* "PFIRE1V1" */
enum {PFIRE_OFF,PFIRE_RECORD,PFIRE_SCRIPT};
static const struct {unsigned rva,size;const char *sha;} pfire_pins_list[]={
    {0x8132d0u,5713u,"0b79f8f9b390ea52241a6c13375953a9f07f0603febc82958d4213fdea7fe80f"}, /* Weapon::Move */
    {0x816d20u,4047u,"c8a35140a1d105611fb540f6545cb9f27b9fcfe22f078e98f547b78185ad4b00"}, /* shoot decision */
    {PFIRE_START,0x1f7u,"6536c679336d2b92f34066a34c62bc08dbfc540d4cb69daccdaebaa6ee090194"},
    {PFIRE_STOP,0x25cu,"a323ec4c4597f75ad11b44a7b7527c45abb9e4a960f832e59dcc5d37bd669694"},
    {PFIRE_RELOAD,250u,"e47963e6bdc2e6a0facf04d22d94c0ae8e58b166db860fda0d9edfcb85442849"},
    {0x7e58a0u,0xbu,"9dd5800bf9ca5c2937f710272b8e16835452fc2c0d6dbcbbf0ee0e796f666176"},  /* Mindless slot 14: flag != 0 */
    {0x7e58b0u,0x9u,"9babe5dc4dedce743a2daf48c90b7b0f48cc6759837f769fdc1a95034b815c99"}, /* Mindless slot 31: set flag */
    {PFIRE_SHOOT_AT,0x3fcu,"4fc605b1000cc534a25f8249ac9d4b468ab46cc0c8b460f4b20bc5aef8662e88"}}; /* <shoot_at>'s controller installer (missiles) */
static const struct {unsigned slot,target;} pfire_vtable_slots[]={{0x70u,0x7e58a0u},{0x88u,0xa3a20u},{0xe0u,0x9c980u},{0xf8u,0x7e58b0u}};
/* The player controller classes, recognised by vtable: each pinned by slots
 * that only that class uses (PlayerShootController slots 10/11, the bomb
 * launcher's slot 14). */
static const struct {unsigned vtable,slot,target;} pfire_player_slots[]={
    {PFIRE_PLAYER_VTABLE,0x50u,0x7e5bd0u},{PFIRE_PLAYER_VTABLE,0x58u,0x7e5be0u},{PFIRE_PLAYER_BOMB_VTABLE,0x70u,0x7e5c20u}};
static const struct {unsigned vtable;const char *name;} pfire_classes[]={
    {PFIRE_MINDLESS_VTABLE,"mindless"},{PFIRE_PLAYER_VTABLE,"player"},{PFIRE_PLAYER_BOMB_VTABLE,"player_bomb"},
    {0x2b7fcc0u,"basic"},{0x2b7d088u,"large_target"},{0x2b7cdf8u,"multiple"}};
typedef unsigned char (*pfire_shoot_fn)(void *);
typedef unsigned char (*pfire_reload_fn)(void *,double);
static int (*pfire_now)(double *)=pnat_time_tls;   /* the frame's game time, as the FFI readers pass it */
static uint64_t pfire_reload_skips;
typedef struct {unsigned char *object,*controller;uint64_t runtime;unsigned char flag,foreign;} pfire_gun;
typedef struct {unsigned count,leased,empty;int32_t group;pfire_gun guns[PFIRE_GUNS];} pfire_slot;
typedef struct {unsigned count,shooting;int32_t group;uint64_t target;pfire_gun launchers[PFIRE_GUNS];} pfire_missile_slot;
typedef void (*pfire_shoot_at_fn)(void *,void *,void *,unsigned char,float,unsigned char);
static pfire_missile_slot pfire_missiles[PILOT_SLOTS];
static uint64_t pfire_missile_starts,pfire_missile_stops,pfire_missile_empty,pfire_missile_no_target;
static pfire_shoot_at_fn pfire_shoot_at_override; /* tests */
static pfire_slot pfire_slots[PILOT_SLOTS];
static int pfire_mode,pfire_armed,pfire_pins_checked,pfire_released;
static unsigned pfire_script_mask;   /* PFIRE_SCRIPT: bit s-1 = slot s fires; 0 = every slot */
static uint64_t pfire_frames,pfire_tick,pfire_triggered,pfire_lease_waits,pfire_installs,pfire_replaced_at_lease,pfire_cleared,
    pfire_receipt_tick,pfire_receipt,pfire_replaced_by_sim,pfire_retaken,pfire_retake_waits,pfire_trigger_restored,pfire_guns_lost,
    pfire_let_go,pfire_relets;
/* The last replacement seen: tick, slot (1-based), gun index, class, local. */
static uint64_t pfire_last_tick;
static unsigned pfire_last_slot,pfire_last_gun;
static int pfire_last_local;
static const char *pfire_last_class="none";
static unsigned pfire_frame_firing;
static volatile LONG64 pfire_failures;
static const char *pfire_stage="off";
static pilot_find_fn pfire_find_override; /* tests */

static int pfire_pins(void) {
    if(!pfire_pins_checked) {
        pfire_pins_checked=1;
        for(unsigned i=0;i<sizeof(pfire_pins_list)/sizeof(*pfire_pins_list);i++)
            if(!hash_ok(image+pfire_pins_list[i].rva,pfire_pins_list[i].size,pfire_pins_list[i].sha)) pfire_pins_checked=-1;
        for(unsigned i=0;i<sizeof(pfire_vtable_slots)/sizeof(*pfire_vtable_slots);i++) {
            uintptr_t entry=0;
            if(!read_local(image+PFIRE_MINDLESS_VTABLE+pfire_vtable_slots[i].slot,&entry,8) ||
               entry!=(uintptr_t)(image+pfire_vtable_slots[i].target)) pfire_pins_checked=-1;
        }
        for(unsigned i=0;i<sizeof(pfire_player_slots)/sizeof(*pfire_player_slots);i++) {
            uintptr_t entry=0;
            if(!read_local(image+pfire_player_slots[i].vtable+pfire_player_slots[i].slot,&entry,8) ||
               entry!=(uintptr_t)(image+pfire_player_slots[i].target)) pfire_pins_checked=-1;
        }
    }
    return pfire_pins_checked==1;
}
static void *pfire_find(uint64_t runtime) {
    void *map=NULL;
    if(pfire_find_override) return pfire_find_override(NULL,runtime,4);
    return runtime && read_local(image+0x6cf1500,&map,8) && map?((pilot_find_fn)(image+0xced70))(map,runtime,4):NULL;
}
static int pfire_write_byte(unsigned char *at,unsigned char value) {
    SIZE_T done=0;return WriteProcessMemory(GetCurrentProcess(),at,&value,1,&done) && done==1;
}
static int pfire_is(const unsigned char *object,unsigned vtable_rva) {
    uintptr_t vtable=0;return object && read_local(object,&vtable,8) && vtable==(uintptr_t)(image+vtable_rva);
}
static int pfire_under(unsigned char *component,const unsigned char *ship) {
    for(unsigned depth=0;component && depth<16;depth++) {
        if(component==ship) return 1;
        unsigned char *parent=NULL;
        if(!read_local(component+0x70,&parent,8) || parent==component) return 0;
        component=parent;
    }
    return 0;
}
/* The guns of the ship's active primary group, in group order. */
static unsigned pfire_collect_in(unsigned char *ship,pfire_gun out[PFIRE_GUNS],unsigned groups_at,unsigned active_at,unsigned vtable);
static unsigned pfire_collect(unsigned char *ship,pfire_gun out[PFIRE_GUNS]) {return pfire_collect_in(ship,out,PFIRE_GROUPS,PFIRE_ACTIVE,PFIRE_WEAPON_VTABLE);}
static unsigned pfire_collect_in(unsigned char *ship,pfire_gun out[PFIRE_GUNS],unsigned groups_at,unsigned active_at,unsigned vtable) {
    unsigned char *groups[2]={NULL,NULL},*ids[2]={NULL,NULL};uint32_t active=0;unsigned n=0;
    if(!read_local(ship+groups_at,groups,16) || !read_local(ship+active_at,&active,4) || !groups[0] ||
       groups[1]<groups[0] || (size_t)(groups[1]-groups[0])%0x18 || active>=(size_t)(groups[1]-groups[0])/0x18 ||
       !read_local(groups[0]+(size_t)active*0x18,ids,16) || !ids[0] || ids[1]<ids[0] || (size_t)(ids[1]-ids[0])%8) return 0;
    for(unsigned char *at=ids[0];at<ids[1] && n<PFIRE_GUNS;at+=8) {
        uint64_t runtime=0;unsigned char *gun;
        if(!read_local(at,&runtime,8) || !(gun=pfire_find(runtime)) || !pfire_is(gun,vtable) || !pfire_under(gun,ship)) continue;
        out[n].object=gun;out[n].runtime=runtime;out[n].controller=NULL;out[n].flag=out[n].foreign=0;++n;
    }
    return n;
}
static unsigned char pfire_wanted(const pilot_slot *s,unsigned slot,uint64_t tick) {
    if(pilot_slot_silent(slot,tick,pilot_delay)) return 0;   /* absent (hot join): no trigger before its first record */
    if(pfire_mode==PFIRE_SCRIPT) return (!pfire_script_mask || (pfire_script_mask>>(slot-1)&1u)) && tick>=pilot_start && (tick-pilot_start+slot*7)%120<30;
    if(pnat_mode==PNAT_MODE_STOCK && (pstk_slot_mask>>(slot-1)&1u)) return pstk_fire_tick[slot-1]==tick && pstk_fire_on[slot-1];
    return s->driving && (s->record.buttons&1) && !(s->record.mode&PILOT_MODE_TRAVEL);
}
/* Lease one slot's guns; a gun that is firing waits (identical on every client). */
static int pfire_lease(pfire_slot *f,unsigned char *ship) {
    pfire_gun found[PFIRE_GUNS];unsigned n=pfire_collect(ship,found);
    if(!n) {++f->empty;return 1;}
    if(!read_local(ship+PFIRE_ACTIVE,&f->group,4)) return 0;
    for(unsigned i=0;i<n;i++) {
        unsigned char firing=0;
        if(!read_local(found[i].object+PFIRE_FIRING,&firing,1)) return 0;
        if(firing) {++pfire_lease_waits;return 1;}
    }
    for(unsigned i=0;i<n;i++) {
        unsigned char *gun=found[i].object,*controller=NULL;
        if(!read_local(gun+PFIRE_CONTROLLER,&controller,8)) return 0;
        if(controller && !pfire_is(controller,PFIRE_MINDLESS_VTABLE)) {
            ((pfire_shoot_fn)(image+PFIRE_STOP))(gun);++pfire_replaced_at_lease; /* deletes the foreign controller */
            if(!read_local(gun+PFIRE_CONTROLLER,&controller,8) || controller) return 0;
        }
        if(!controller) {((pfire_shoot_fn)(image+PFIRE_START))(gun);++pfire_installs;}
        if(!read_local(gun+PFIRE_CONTROLLER,&controller,8) || !pfire_is(controller,PFIRE_MINDLESS_VTABLE) ||
           !pfire_write_byte(controller+PFIRE_FLAG,0)) return 0;
        found[i].controller=controller;
    }
    memcpy(f->guns,found,sizeof(found));f->count=n;f->leased=1;return 1;
}
static const char *pfire_class(const unsigned char *controller) {
    uintptr_t vtable=0;
    if(!controller) return "none";
    if(!read_local(controller,&vtable,8)) return "unreadable";
    for(unsigned i=0;i<sizeof(pfire_classes)/sizeof(*pfire_classes);i++)
        if(vtable==(uintptr_t)(image+pfire_classes[i].vtable)) return pfire_classes[i].name;
    return "other";
}
/* A leased gun's controller is no longer ours. On the local player's ship, or
 * with a player controller class (or an unreadable one) anywhere, the change
 * may exist on this client only: fail closed (0). Otherwise the simulation
 * made it on every client at this tick: with take, the gun is taken back now
 * (1) or, while it is firing, waits (2); without take it is let go (2). */
static int pfire_replaced(const pilot_slot *s,pfire_gun *g,unsigned slot,unsigned index,unsigned char *controller,int take) {
    const char *kind=pfire_class(controller);
    int stock=pnat_mode==PNAT_MODE_STOCK && (pstk_slot_mask>>(slot-1)&1u);
    int local=!strcmp(kind,"unreadable") || (!stock && (s->player || !strcmp(kind,"player") || !strcmp(kind,"player_bomb")));
    if(local || !g->foreign) {
        pfire_last_tick=stats.issued;pfire_last_slot=slot;pfire_last_gun=index;pfire_last_class=kind;pfire_last_local=local;
    }
    if(local) {pfire_stage="controller_replaced";return 0;}
    if(!g->foreign) {g->foreign=1;++pfire_replaced_by_sim;}
    if(!take) return 2;
    unsigned char firing=0,flag=0;
    if(!read_local(g->object+PFIRE_FIRING,&firing,1)) {pfire_stage="retake";return 0;}
    if(firing) {++pfire_retake_waits;return 2;} /* not taken mid-shot, as at the lease */
    if(controller && !pfire_is(controller,PFIRE_MINDLESS_VTABLE)) {
        ((pfire_shoot_fn)(image+PFIRE_STOP))(g->object); /* deletes the simulation's controller */
        if(!read_local(g->object+PFIRE_CONTROLLER,&controller,8) || controller) {pfire_stage="retake";return 0;}
    }
    if(!controller) {
        ((pfire_shoot_fn)(image+PFIRE_START))(g->object);++pfire_installs;
        if(!read_local(g->object+PFIRE_CONTROLLER,&controller,8)) {pfire_stage="retake";return 0;}
    }
    if(!pfire_is(controller,PFIRE_MINDLESS_VTABLE) || !read_local(controller+PFIRE_FLAG,&flag,1)) {pfire_stage="retake";return 0;}
    g->controller=controller;g->flag=flag;g->foreign=0;++pfire_retaken;return 1;
}
/* Keep a leased slot's guns ours and their triggers at want before this
 * tick's walk. A gun no longer found (destroyed) is dropped. */
static int pfire_hold(const pilot_slot *s,pfire_slot *f,unsigned slot,unsigned char want) {
    for(unsigned g=0;g<f->count;) {
        pfire_gun *gun=&f->guns[g];unsigned char *controller=NULL,flag=0;
        if(pfire_find(gun->runtime)!=gun->object) {
            memmove(gun,gun+1,(size_t)(f->count-g-1)*sizeof(*gun));--f->count;++pfire_guns_lost;continue;
        }
        if(!read_local(gun->object+PFIRE_CONTROLLER,&controller,8)) {pfire_stage="read";return 0;}
        if(controller!=gun->controller || !pfire_is(controller,PFIRE_MINDLESS_VTABLE)) {
            int kept=pfire_replaced(s,gun,slot,g,controller,1);
            if(!kept) return 0;
            if(kept==2) {++g;continue;}
            controller=gun->controller;
        }
        if(!read_local(controller+PFIRE_FLAG,&flag,1)) {pfire_stage="read";return 0;}
        if(flag!=gun->flag) ++pfire_trigger_restored; /* <shoot>/<stop_shooting> on our controller */
        if(flag!=want && !pfire_write_byte(controller+PFIRE_FLAG,want)) {pfire_stage="write";return 0;}
        gun->flag=want;++g;
    }
    return 1;
}
/* After the window: each trigger still set is cleared once; a gun the
 * simulation took meanwhile is let go. */
static int pfire_end(const pilot_slot *s,pfire_slot *f,unsigned slot) {
    for(unsigned g=0;f->leased && g<f->count;g++) {
        pfire_gun *gun=&f->guns[g];unsigned char *controller=NULL;
        if(!gun->flag || gun->foreign) continue;
        if(pfire_find(gun->runtime)!=gun->object) {gun->flag=0;continue;}
        if(!read_local(gun->object+PFIRE_CONTROLLER,&controller,8)) {pfire_stage="read";return 0;}
        if(controller!=gun->controller || !pfire_is(controller,PFIRE_MINDLESS_VTABLE)) {
            if(!pfire_replaced(s,gun,slot,g,controller,0)) return 0;
            gun->flag=0;++pfire_let_go;continue;
        }
        if(!pfire_write_byte(controller+PFIRE_FLAG,0)) {pfire_stage="write";return 0;}
        gun->flag=0;
    }
    return 1;
}
/* Missiles of one slot this tick (end: the window is over or the session stops: whatever shoots stops). */
static void pfire_missiles_stop(pfire_missile_slot *m) {
    for(unsigned l=0;l<m->count;l++)
        if(pfire_find(m->launchers[l].runtime)==m->launchers[l].object) ((pfire_shoot_fn)(image+PFIRE_STOP))(m->launchers[l].object);
    m->shooting=0;m->count=0;++pfire_missile_stops;
}
static uint64_t pfire_slot_target(unsigned slot) {
    const unsigned char *player=uirep_slot_player(slot);uint64_t target=0;
    if(player) read_local(player+PFIRE_SOFT_TARGET,&target,8);
    return target;
}
static int pfire_missiles_tick(const pilot_slot *s,unsigned slot,uint64_t tick,int end) {
    pfire_missile_slot *m=&pfire_missiles[slot-1];
    int want=!end && pstk_fire_tick[slot-1]==tick && pstk_fire2_on[slot-1];
    uint64_t target=want?pfire_slot_target(slot):0;unsigned char *object=target?pfire_find(target):NULL;int32_t group=-2;
    read_local(s->object+PFIRE_ACTIVE2,&group,4);
    if(want && !object) ++pfire_missile_no_target;
    if(m->shooting && (!want || !object || target!=m->target || group!=m->group)) pfire_missiles_stop(m);
    if(!want || !object || m->shooting) return 1;
    m->count=pfire_collect_in(s->object,m->launchers,PFIRE_GROUPS2,PFIRE_ACTIVE2,PFIRE_LAUNCHER_VTABLE);
    if(!m->count) {++pfire_missile_empty;return 1;}
    pfire_shoot_at_fn shoot_at=pfire_shoot_at_override?pfire_shoot_at_override:(pfire_shoot_at_fn)(void*)(image+PFIRE_SHOOT_AT);
    for(unsigned l=0;l<m->count;l++) {
        uint64_t list[3]={0,0,0};   /* no further targets: one target, a basic controller */
        shoot_at(m->launchers[l].object,object,list,0,0.0f,0);
    }
    m->shooting=1;m->target=target;m->group=group;++pfire_missile_starts;return 1;
}
/* path_release, MoveWorker, after the pilot frame for this tick is published. */
static int pfire_before_release(void) {
    if(!pfire_armed) return 1;
    uint64_t tick=stats.issued;
    if(pilot_frame_tick!=tick) {pfire_stage="frame";return 0;}
    for(unsigned i=0;i<pilot_slot_count;i++) {
        pilot_slot *s=&pilot_slots[i];pfire_slot *f=&pfire_slots[i];
        if(tick<pilot_start || !s->resolved || !pilot_slot_enrolled(i+1,tick)) continue;   /* absent: its guns stay its AI's */
        if(uirep_families&UIREP_FAMILY_MISSILES) pfire_missiles_tick(s,i+1,tick,tick>pilot_stop);
        if(tick>pilot_stop) {if(!pfire_end(s,f,i+1)) return 0;continue;}
        if(!f->leased && !pfire_lease(f,s->object)) {pfire_stage="lease";return 0;}
        if(!f->leased) continue;
        /* The UI replay's weapon family changes a slot's active group (lockstep_ui_replay.h), the same tick on every
         * client: the old guns' triggers are cleared and the new group is leased. */
        int32_t group=0;
        if((uirep_families&UIREP_FAMILY_WEAPONS) && read_local(s->object+PFIRE_ACTIVE,&group,4) && group!=f->group) {
            if(!pfire_end(s,f,i+1)) return 0;
            f->leased=0;f->count=0;++pfire_relets;
            if(!pfire_lease(f,s->object)) {pfire_stage="lease";return 0;}
            if(!f->leased) continue;
        }
        unsigned char want=pfire_wanted(s,i+1,tick);
        if(!pfire_hold(s,f,i+1,want)) return 0;
        if(want) ++pfire_triggered;
    }
    pfire_tick=tick;++pfire_frames;pfire_stage="live";return 1;
}
/* Slot 6: the guns' native firing state after this tick's walk. */
static int hyb_slot_private(unsigned slot); /* lockstep_hybrid.h */
/* With hybrid sectors (native_gate_hybrid) an owning slot's guns are reloaded as before but stay out of the
 * fold and the firing count: they are its sector's state. */
static int pfire_frame_text(char *out,size_t capacity) {
    uint64_t fold=0;unsigned firing=0;double now=0;int timed=pfire_now(&now);
    if(!timed) ++pfire_reload_skips;
    for(unsigned i=0;i<pilot_slot_count;i++) {
        pfire_slot *f=&pfire_slots[i];
        for(unsigned g=0;f->leased && g<f->count;g++) {
            unsigned char active=0;double last=0;uint32_t magazine=0;uint64_t bits=0;
            if(pfire_find(f->guns[g].runtime)!=f->guns[g].object) continue; /* destroyed in this tick's walk */
            if(timed) ((pfire_reload_fn)(image+PFIRE_RELOAD))(f->guns[g].object,now);
            if(!read_local(f->guns[g].object+PFIRE_FIRING,&active,1) || !read_local(f->guns[g].object+PFIRE_LAST_SHOT,&last,8) ||
               !read_local(f->guns[g].object+PFIRE_MAGAZINE,&magazine,4)) {pfire_stage="receipt";InterlockedIncrement64(&pfire_failures);return 0;}
            if(hyb_slot_private(i+1)) continue;
            memcpy(&bits,&last,8);firing+=active!=0;
            fold+=lockstep_seed(PFIRE_DOMAIN,stats.issued,pilot_slots[i].saved,g,bits,((uint64_t)active<<32)|magazine);
        }
    }
    for(unsigned i=0;(uirep_families&UIREP_FAMILY_MISSILES) && i<pilot_slot_count;i++)
        if(!hyb_slot_private(i+1)) fold+=lockstep_seed(PFIRE_DOMAIN^0x4dULL,stats.issued,pilot_slots[i].saved,pfire_missiles[i].shooting,
            pfire_missiles[i].shooting?pfire_missiles[i].target:0,pfire_missiles[i].count);
    pfire_receipt_tick=stats.issued;pfire_receipt=fold;pfire_frame_firing=firing;
    int n=snprintf(out,capacity,"PFIRE_1:%llu:%u:%016llx",(unsigned long long)stats.issued,firing,(unsigned long long)fold);
    /* Missiles: per slot shooting.launchers after the fold, the target in the fold (runtime IDs agree on every client). */
    for(unsigned i=0;n>0 && (size_t)n<capacity && (uirep_families&UIREP_FAMILY_MISSILES) && i<pilot_slot_count;i++) {
        const pfire_missile_slot *m=&pfire_missiles[i];
        int more=hyb_slot_private(i+1)?snprintf(out+n,capacity-(size_t)n,"%sh",i?"/":":M"):
                 snprintf(out+n,capacity-(size_t)n,"%s%u.%u",i?"/":":M",m->shooting,m->count);
        if(more<0) return 0;
        n+=more;
    }
    return n>0 && (size_t)n<capacity?n:0;
}
/* The guns as the receipt reads them (slot:gun:active:last shot bits:magazine:controller class). */
static int pfire_guns_text(char *out,size_t capacity) {
    int n=0;out[0]=0;
    for(unsigned i=0;i<pilot_slot_count;i++) {
        pfire_slot *f=&pfire_slots[i];
        for(unsigned g=0;f->leased && g<f->count;g++) {
            unsigned char active=0,*controller=NULL;uint64_t bits=0;uint32_t magazine=0;
            if(pfire_find(f->guns[g].runtime)!=f->guns[g].object) continue;
            if(!read_local(f->guns[g].object+PFIRE_FIRING,&active,1) || !read_local(f->guns[g].object+PFIRE_LAST_SHOT,&bits,8) ||
               !read_local(f->guns[g].object+PFIRE_MAGAZINE,&magazine,4) || !read_local(f->guns[g].object+PFIRE_CONTROLLER,&controller,8)) continue;
            int more=snprintf(out+n,capacity-(size_t)n,"%s%u:%u:%u:%016llx:%u:%s",n?",":"",i+1,g,active,(unsigned long long)bits,magazine,pfire_class(controller));
            if(more<0 || (size_t)(n+more)>=capacity) return n;
            n+=more;
        }
    }
    return n;
}
static void pfire_restore(void) {
    if(!pfire_armed || pfire_released) return;
    for(unsigned i=0;i<pilot_slot_count;i++)
        for(unsigned g=0;pfire_slots[i].leased && g<pfire_slots[i].count;g++) {
            pfire_gun *gun=&pfire_slots[i].guns[g];unsigned char *controller=NULL;
            if(!gun->flag || gun->foreign || pfire_find(gun->runtime)!=gun->object ||
               !read_local(gun->object+PFIRE_CONTROLLER,&controller,8) || controller!=gun->controller ||
               !pfire_is(controller,PFIRE_MINDLESS_VTABLE)) continue; /* only triggers that are still ours */
            if(pfire_write_byte(controller+PFIRE_FLAG,0)) {gun->flag=0;++pfire_cleared;}
        }
    pfire_released=1;
}
static int pfire_check(void) {return !pfire_armed || !InterlockedCompareExchange64(&pfire_failures,0,0);}
static int pfire_arm(int mode) {
    if(pfire_armed) return mode==pfire_mode && stats.active;
    if((mode!=PFIRE_RECORD && mode!=PFIRE_SCRIPT) || !pilot_armed || !stats.active || stats.issued || GetCurrentThreadId()!=ui_thread ||
       !world_identity_installed || !pfire_pins()) {pfire_stage="refused";return 0;}
    memset(pfire_slots,0,sizeof(pfire_slots));memset(pfire_missiles,0,sizeof(pfire_missiles));pfire_mode=mode;pfire_armed=1;pfire_released=0;pfire_stage="armed";return 1;
}
static int pfire_status(char *out,size_t capacity) {
    int n=snprintf(out,capacity,"mode=%s armed=%d frames=%llu tick=%llu triggered=%llu installs=%llu replaced_at_lease=%llu lease_waits=%llu "
        "replaced_by_sim=%llu retaken=%llu retake_waits=%llu trigger_restored=%llu guns_lost=%llu let_go=%llu relets=%llu "
        "missiles=%llu/%llu/%llu/%llu "
        "last_replaced=t%llu/s%u/g%u/%s/%s cleared=%llu firing=%u receipt=%016llx stage=%s failures=%lld",
        pfire_mode==PFIRE_RECORD?"record":pfire_mode==PFIRE_SCRIPT?"script":"off",
        pfire_armed,(unsigned long long)pfire_frames,(unsigned long long)pfire_tick,(unsigned long long)pfire_triggered,
        (unsigned long long)pfire_installs,(unsigned long long)pfire_replaced_at_lease,(unsigned long long)pfire_lease_waits,
        (unsigned long long)pfire_replaced_by_sim,(unsigned long long)pfire_retaken,(unsigned long long)pfire_retake_waits,
        (unsigned long long)pfire_trigger_restored,(unsigned long long)pfire_guns_lost,(unsigned long long)pfire_let_go,
        (unsigned long long)pfire_relets,(unsigned long long)pfire_missile_starts,(unsigned long long)pfire_missile_stops,
        (unsigned long long)pfire_missile_empty,(unsigned long long)pfire_missile_no_target,(unsigned long long)pfire_last_tick,pfire_last_slot,pfire_last_gun,pfire_last_class,pfire_last_local?"local":"sim",
        (unsigned long long)pfire_cleared,pfire_frame_firing,(unsigned long long)pfire_receipt,pfire_stage,(long long)pfire_failures);
    for(unsigned i=0;n>0 && (size_t)n<capacity && i<pilot_slot_count;i++)
        n+=snprintf(out+n,capacity-(size_t)n," s%u=%u/%u/%u",i+1,pfire_slots[i].leased,pfire_slots[i].count,pfire_slots[i].empty);
    return n>0 && (size_t)n<capacity?n:0;
}
