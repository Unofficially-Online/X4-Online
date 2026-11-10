/* Joining inside a running pilot window, W3 R5 (reports/LOCKSTEP-JOIN-IN-WINDOW-20261104.txt sections 8 and 11): the
 * per-slot modules' state at T, carried in 'pilot_state' after R3/R4's tables. lockstep_pilot_join.h calls these; this file
 * comes after lockstep_ui_replay.h for the UI replay's types.
 *
 * Carried (the saver in frame T, the joiner in its first pass at T+1, after R1-R4):
 *   m5      one blob: each slot's death record (lockstep_pilot_death.h: states, counts, ticks, runtime IDs), the death
 *           module's seat counters, each slot's window runtime ID, each slot's leased guns by runtime ID with their group and
 *           last trigger (lockstep_pilot_fire.h), the UI replay's order queue aliases and every slot's applied creates
 *           (lockstep_ui_replay.h: runtime IDs, the same on every client), and for the checks below the modules' arming, the
 *           foot states and the missiles shooting.
 *   dh      slot 1's held keys (pdeath_held: released at a respawn's seat).
 *   urK_I   the UI replay's staged sections with an apply tick after T (uirep_ring slot K index I), as R4 carries the stock
 *           ring: captured before T, in rows the joiner's catch-up does not carry.
 *   hrI     the hit claims staged for an apply tick after T (hit_stage_ring index I: tick, count, the staged claims).
 *   hc      the consumed projectile keys (hit_consumed).
 * The joiner binds each leased gun to its own object by runtime ID. The controller comes with the save (w1x: the joiner's
 * own lease at T+1 found every slot gun with its MindlessShootController, installs 0); a gun that is gone or whose
 * controller is not a Mindless refuses. Without the carried lease the joiner leased anew at T+1, and a gun firing then
 * would have waited on the joiner only.
 * A slot ship other than the window's (a respawn before T; 10 Nov: before, refused as slot_respawned): the session's slot
 * points at a ship built after the load, which the joiner's own resolve (the config's saved ID) cannot find. The joiner's
 * objects sit at the session's runtime IDs (native_gate_join_ids, the created identities), so the joiner takes the session's
 * ID for that slot as the death module's seat does on a session client (runtime, reversed, renewed) and resolves it, before
 * R1 rebuilds the slot's player in it. Refused when no live object has that ID (slot_ship_missing) or it does not resolve
 * (slot_ship_unresolved). Status adopted_ships=.
 * Refused (stop 91, join_resume=<why>), not carried yet: a slot dead, stranded, in its suit or respawning at T
 * (death_at_T); missiles shooting at T (missiles_at_T); a gun the simulation holds at T (fire_foreign); slot 1 or a slot k
 * on foot at T (foot_at_T); a module armed on one side only (modules_armed). */
#define PJOIN_M5_VERSION 1u
typedef struct {uint32_t leased,count,empty;int32_t group;uint64_t runtime[PFIRE_GUNS];unsigned char flag[PFIRE_GUNS],foreign[PFIRE_GUNS];} pjoin_fire_tx;
typedef struct {
    uint32_t version,armed;   /* armed: 1 death, 2 fire, 4 foot, 8 foot slots, 16 UI replay, 32 hit claims (native) */
    uint32_t death_delay,death_suit,death_dead,death_released,death_retired;
    uint32_t missiles,foot_state,walking;
    uint64_t runtime[PILOT_SLOTS];
    pdeath_slot death[PILOT_SLOTS];
    pjoin_fire_tx fire[PILOT_SLOTS];
    uint32_t q_count,q_applied[PILOT_SLOTS];
    uirep_q_alias q[UIREP_Q_ALIASES];
} pjoin_m5;
static pjoin_m5 pjoin_m5_rx;
static unsigned char pjoin_dh_rx[2][512];
static int pjoin_m5_seen,pjoin_dh_seen;
static uirep_staged pjoin_ui_ring[PILOT_SLOTS][UIREP_RING];
static uint64_t pjoin_hit_tick[HIT_RING];
static unsigned pjoin_hit_count[HIT_RING];
static hit_staged pjoin_hit_ring[HIT_RING][HIT_STAGED_MAX];
static uint64_t pjoin_hit_consumed[HIT_CONSUMED_CAP];
static unsigned pjoin_hit_consumed_count;
static int pjoin_m5_refuse(const char *why) {pjoin_resume_why=why;return 0;}
static unsigned pjoin_ship_adopt;   /* bit i: slot i+1's ship on the session is not the one this client resolved */
static unsigned pjoin_ships_adopted;
static unsigned pjoin_m5_armed(void) {
    return (pdeath_armed?1u:0u)|(pfire_armed?2u:0u)|(pfoot_armed?4u:0u)|(pfslot_armed?8u:0u)|(uirep_armed?16u:0u)|(hitn_armed?32u:0u);
}
static void pjoin_m5_pack(pjoin_m5 *m) {
    memset(m,0,sizeof(*m));
    m->version=PJOIN_M5_VERSION;m->armed=pjoin_m5_armed();
    m->death_delay=pdeath_delay;m->death_suit=(uint32_t)pdeath_suit;m->death_dead=pdeath_dead;
    m->death_released=pdeath_released;m->death_retired=pdeath_retired;
    m->foot_state=pfoot_armed?pfoot_state:PFOOT_SEATED;
    for(unsigned k=2;pfslot_armed && k<=PILOT_SLOTS;k++) if(pfslots[k].walking) m->walking|=1u<<(k-1);
    for(unsigned i=0;i<PILOT_SLOTS;i++) {
        m->runtime[i]=pilot_slots[i].runtime;memcpy(&m->death[i],&pdeath_slots[i],sizeof(pdeath_slot));
        if(pfire_missiles[i].shooting) m->missiles|=1u<<i;
        const pfire_slot *f=&pfire_slots[i];pjoin_fire_tx *x=&m->fire[i];
        x->leased=f->leased;x->count=f->count;x->empty=f->empty;x->group=f->group;
        for(unsigned g=0;g<f->count && g<PFIRE_GUNS;g++) {x->runtime[g]=f->guns[g].runtime;x->flag[g]=f->guns[g].flag;x->foreign[g]=f->guns[g].foreign;}
    }
    m->q_count=uirep_q_alias_count;memcpy(m->q_applied,uirep_q_applied,sizeof(m->q_applied));memcpy(m->q,uirep_q_aliases,sizeof(m->q));
}
/* The saver: the entries after R3/R4's (lockstep_pilot_join.h pjoin_tables_state); the new length, -1 when it does not fit. */
static int pjoin_m5_put(char *out,size_t capacity,int n,uint64_t t) {
    static pjoin_m5 m;static unsigned char hit[16+HIT_STAGED_MAX*sizeof(hit_staged)];
    pjoin_m5_pack(&m);
    n+=snprintf(out+n,capacity-(size_t)n,"|m5=");
    if(n<=0 || (size_t)n>=capacity || (n=pjoin_hex_put(out,capacity,n,(const unsigned char*)&m,sizeof(m)))<0) return -1;
    n+=snprintf(out+n,capacity-(size_t)n,"|dh=");
    if(n<=0 || (size_t)n>=capacity || (n=pjoin_hex_put(out,capacity,n,&pdeath_held[0][0],sizeof(pdeath_held)))<0) return -1;
    for(unsigned k=0;k<PILOT_SLOTS;k++) for(unsigned i=0;i<UIREP_RING;i++) {
        const uirep_staged *e=&uirep_ring[k][i];
        if(!e->present || e->tick<=t) continue;
        n+=snprintf(out+n,capacity-(size_t)n,"|ur%u_%u=",k,i);
        if(n<=0 || (size_t)n>=capacity || (n=pjoin_hex_raw(out,capacity,n,(const unsigned char*)e,sizeof(*e)))<0) return -1;
    }
    for(unsigned i=0;i<HIT_RING;i++) {
        const unsigned count=hit_stage_count[i];
        if(hit_stage_tick[i]<=t || !count || count>HIT_STAGED_MAX) continue;
        memset(hit,0,sizeof(hit));memcpy(hit,&hit_stage_tick[i],8);memcpy(hit+8,&count,4);memcpy(hit+16,hit_stage_ring[i],count*sizeof(hit_staged));
        n+=snprintf(out+n,capacity-(size_t)n,"|hr%u=",i);
        if(n<=0 || (size_t)n>=capacity || (n=pjoin_hex_raw(out,capacity,n,hit,16+count*sizeof(hit_staged)))<0) return -1;
    }
    if(hit_consumed_count && hit_consumed_count<=HIT_CONSUMED_CAP) {
        n+=snprintf(out+n,capacity-(size_t)n,"|hc=");
        if(n<=0 || (size_t)n>=capacity || (n=pjoin_hex_raw(out,capacity,n,(const unsigned char*)hit_consumed,hit_consumed_count*8u))<0) return -1;
    }
    return n>0 && (size_t)n<capacity?n:-1;
}
static void pjoin_m5_reset(void) {
    memset(&pjoin_m5_rx,0,sizeof(pjoin_m5_rx));memset(pjoin_dh_rx,0,sizeof(pjoin_dh_rx));pjoin_m5_seen=pjoin_dh_seen=0;
    memset(pjoin_ui_ring,0,sizeof(pjoin_ui_ring));memset(pjoin_hit_tick,0,sizeof(pjoin_hit_tick));memset(pjoin_hit_count,0,sizeof(pjoin_hit_count));
    pjoin_hit_consumed_count=0;
}
/* The joiner's parse, one entry: 1 taken, 0 refused (pjoin_resume_why), -1 not an R5 name. */
static int pjoin_m5_get(const char *name,size_t length,const char *value,size_t chars) {
    unsigned k=0,i=0;int used=0;
    if(length==2 && !memcmp(name,"m5",2)) {
        if(pjoin_m5_seen || !pjoin_hex_get(value,chars,(unsigned char*)&pjoin_m5_rx,sizeof(pjoin_m5_rx))) return pjoin_m5_refuse("state_m5");
        pjoin_m5_seen=1;return 1;
    }
    if(length==2 && !memcmp(name,"dh",2)) {
        if(pjoin_dh_seen || !pjoin_hex_get(value,chars,&pjoin_dh_rx[0][0],sizeof(pjoin_dh_rx))) return pjoin_m5_refuse("state_m5");
        pjoin_dh_seen=1;return 1;
    }
    if(length==2 && !memcmp(name,"hc",2)) {
        if(pjoin_hit_consumed_count || !chars || chars%16 || chars/16>HIT_CONSUMED_CAP ||
           !pjoin_hex_get(value,chars,(unsigned char*)pjoin_hit_consumed,chars/2)) return pjoin_m5_refuse("state_hit");
        pjoin_hit_consumed_count=(unsigned)(chars/16);return 1;
    }
    if(length>2 && name[0]=='u' && name[1]=='r') {
        if(sscanf(name,"ur%u_%u%n",&k,&i,&used)!=2 || (size_t)used!=length || k>=PILOT_SLOTS || i>=UIREP_RING || pjoin_ui_ring[k][i].present ||
           !pjoin_hex_get(value,chars,(unsigned char*)&pjoin_ui_ring[k][i],sizeof(uirep_staged)) || !pjoin_ui_ring[k][i].present ||
           pjoin_ui_ring[k][i].length>UIREP_SECTION_MAX) return pjoin_m5_refuse("state_ui");
        return 1;
    }
    if(length>2 && name[0]=='h' && name[1]=='r') {
        static unsigned char hit[16+HIT_STAGED_MAX*sizeof(hit_staged)];unsigned count=0;
        if(sscanf(name,"hr%u%n",&i,&used)!=1 || (size_t)used!=length || i>=HIT_RING || pjoin_hit_count[i] || chars<32 || chars%2 ||
           chars/2>sizeof(hit) || !pjoin_hex_get(value,chars,hit,chars/2)) return pjoin_m5_refuse("state_hit");
        memcpy(&pjoin_hit_tick[i],hit,8);memcpy(&count,hit+8,4);
        if(!count || count>HIT_STAGED_MAX || chars/2!=16+count*sizeof(hit_staged) || pjoin_hit_tick[i]%HIT_RING!=i) return pjoin_m5_refuse("state_hit");
        memcpy(pjoin_hit_ring[i],hit+16,count*sizeof(hit_staged));pjoin_hit_count[i]=count;
        return 1;
    }
    return -1;
}
static int pjoin_m5_complete(uint64_t t) {
    if(!pjoin_m5_seen || !pjoin_dh_seen) return pjoin_m5_refuse("state_missing");
    for(unsigned k=0;k<PILOT_SLOTS;k++) for(unsigned i=0;i<UIREP_RING;i++)
        if(pjoin_ui_ring[k][i].present && (pjoin_ui_ring[k][i].tick<=t || pjoin_ui_ring[k][i].tick%UIREP_RING!=i)) return pjoin_m5_refuse("state_ui");
    for(unsigned i=0;i<HIT_RING;i++) if(pjoin_hit_count[i] && pjoin_hit_tick[i]<=t) return pjoin_m5_refuse("state_hit");
    return 1;
}
/* The joiner, before R1 (nothing has changed when one refuses): every check, and the guns bound to this client's objects. */
static pfire_slot pjoin_fire_rx[PILOT_SLOTS];
static int pjoin_m5_check(void) {
    const pjoin_m5 *m=&pjoin_m5_rx;
    if(m->version!=PJOIN_M5_VERSION) return pjoin_m5_refuse("m5_version");
    if(m->armed!=pjoin_m5_armed() || (pdeath_armed && (m->death_delay!=pdeath_delay || (int)m->death_suit!=pdeath_suit)))
        return pjoin_m5_refuse("modules_armed");
    if(m->death_dead) return pjoin_m5_refuse("death_at_T");
    for(unsigned i=0;i<PILOT_SLOTS;i++) {
        const pdeath_slot *d=&m->death[i];
        if(d->state!=PDEATH_ALIVE || d->lost || d->in_suit || d->suit_tick) return pjoin_m5_refuse("death_at_T");
    }
    pjoin_ship_adopt=0;
    for(unsigned i=0;i<pilot_slot_count && i<PILOT_SLOTS;i++) {   /* a ship built after the load: the session's ID must be a live object here */
        unsigned char *object=NULL;uint64_t id=0;
        if(m->runtime[i]==pilot_slots[i].runtime) continue;
        if(!m->runtime[i] || !pdeath_find || !(object=pdeath_find(pdeath_map,m->runtime[i],4)) || !read_local(object+8,&id,8) || id!=m->runtime[i])
            return pjoin_m5_refuse("slot_ship_missing");
        pjoin_ship_adopt|=1u<<i;
    }
    if(m->missiles) return pjoin_m5_refuse("missiles_at_T");
    if(m->foot_state!=PFOOT_SEATED || m->walking) return pjoin_m5_refuse("foot_at_T");
    memset(pjoin_fire_rx,0,sizeof(pjoin_fire_rx));
    for(unsigned i=0;pfire_armed && i<PILOT_SLOTS;i++) {
        const pjoin_fire_tx *x=&m->fire[i];pfire_slot *f=&pjoin_fire_rx[i];
        if(x->count>PFIRE_GUNS || (x->count && !x->leased)) return pjoin_m5_refuse("fire_count");
        f->leased=x->leased;f->count=x->count;f->empty=x->empty;f->group=x->group;
        for(unsigned g=0;g<x->count;g++) {
            unsigned char *gun=(unsigned char*)pfire_find(x->runtime[g]),*controller=NULL;
            if(x->foreign[g]) return pjoin_m5_refuse("fire_foreign");
            if(!gun || !read_local(gun+PFIRE_CONTROLLER,&controller,8)) return pjoin_m5_refuse("fire_gun");
            if(!pfire_is(controller,PFIRE_MINDLESS_VTABLE)) return pjoin_m5_refuse("fire_controller");
            f->guns[g].object=gun;f->guns[g].controller=controller;f->guns[g].runtime=x->runtime[g];f->guns[g].flag=x->flag[g];
        }
    }
    for(unsigned i=0;i<HIT_RING;i++)
        if(pjoin_hit_count[i] && hit_stage_tick[i]==pjoin_hit_tick[i] && pjoin_hit_count[i]+hit_stage_count[i]>HIT_STAGED_MAX) return pjoin_m5_refuse("hit_ring");
    return 1;
}
/* The joiner, after the checks and before R1: each slot whose ship is not the window's takes the session's (header). */
static int pjoin_m5_adopt_ships(void) {
    const pjoin_m5 *m=&pjoin_m5_rx;
    for(unsigned i=0;i<pilot_slot_count && i<PILOT_SLOTS;i++) {
        pilot_slot *s=&pilot_slots[i];
        if(!(pjoin_ship_adopt>>i&1u)) continue;
        s->runtime=m->runtime[i];s->reversed=1;s->renewed=1;s->dead=0;s->seen=0;
        if(!pilot_resolve(s,pdeath_find,pdeath_map,pdeath_player)) return pjoin_m5_refuse("slot_ship_unresolved");
        ++pjoin_ships_adopted;
    }
    pjoin_ship_adopt=0;
    return 1;
}
/* The joiner, after R4: the checked state in. The hit claims staged after T go first in their apply tick (the session staged
 * them from earlier rows than any the joiner has). */
static void pjoin_m5_apply(void) {
    const pjoin_m5 *m=&pjoin_m5_rx;
    if(pdeath_armed) {
        memcpy(pdeath_slots,m->death,sizeof(pdeath_slots));memcpy(pdeath_held,pjoin_dh_rx,sizeof(pdeath_held));
        pdeath_released=m->death_released;pdeath_retired=m->death_retired;
    }
    if(pfire_armed) memcpy(pfire_slots,pjoin_fire_rx,sizeof(pfire_slots));
    if(uirep_armed) {
        uirep_q_alias_count=m->q_count;memcpy(uirep_q_applied,m->q_applied,sizeof(uirep_q_applied));memcpy(uirep_q_aliases,m->q,sizeof(uirep_q_aliases));
        for(unsigned k=0;k<PILOT_SLOTS;k++) for(unsigned i=0;i<UIREP_RING;i++) {
            const uirep_staged *e=&pjoin_ui_ring[k][i];
            if(e->present && !(uirep_ring[k][i].present && uirep_ring[k][i].tick==e->tick)) uirep_ring[k][i]=*e;
        }
    }
    for(unsigned i=0;i<HIT_RING;i++) {
        if(!pjoin_hit_count[i]) continue;
        static hit_staged merged[HIT_STAGED_MAX];unsigned n=pjoin_hit_count[i];
        memcpy(merged,pjoin_hit_ring[i],n*sizeof(hit_staged));
        if(hit_stage_tick[i]==pjoin_hit_tick[i]) {memcpy(merged+n,hit_stage_ring[i],hit_stage_count[i]*sizeof(hit_staged));n+=hit_stage_count[i];}
        memcpy(hit_stage_ring[i],merged,n*sizeof(hit_staged));hit_stage_tick[i]=pjoin_hit_tick[i];hit_stage_count[i]=n;
    }
    if(pjoin_hit_consumed_count) {memcpy(hit_consumed,pjoin_hit_consumed,pjoin_hit_consumed_count*8u);hit_consumed_count=pjoin_hit_consumed_count;}
}
