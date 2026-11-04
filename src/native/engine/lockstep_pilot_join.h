/* Joining inside a running pilot window, stage W3 (reports/LOCKSTEP-JOIN-IN-WINDOW-20261104.txt), the session's side.
 *
 * W1b: X4 loads only a save with exactly one player (UniverseClass::Import 8dca31), and with the cockpit bodies connected
 * (lockstep_pilot_foot_body.h) every P_k is a saved player component of its seat's room. So a join save at T is armed on
 * every client of the session (the join core's save_at, savers or not: taking a body off changes the tree, and a client
 * that skipped it would part), and the body guard takes every body off in T's pass, as for a get-up, on the UI thread
 * where a slot's scope can open (w1p: the frame thread's join boundary cannot, 155 park_scope); they come back
 * PFBODY_SETTLE ticks later the same way. X4's save after step T then holds the real player only.
 *
 * gate.pilot_players_state(): per seated slot, the ID ranges of its steps and, for every slot a step allocated, the
 * session's runtime ID (generation<<25|slot), its created identity and creation tick (0:0 for allocations that are no
 * created component, or are gone). The probe's join provider 'pilot_players' carries it in the package (no format
 * change); the joiner's rebuild (W3, with the hot join's join_ids_replay) is not built yet: a joiner inside the window
 * still stops at its first pilot frame (91 join_inside_window). */

/* gate.pilot_join_park_at(T): every client that applies SAVE_AT, with T ahead of this tick. One park pending at a time. */
static int pjoin_park_arm(uint64_t tick) {
    if(!pilot_armed || tick<=stats.issued) return 0;
    if(pjoin_park_tick && pjoin_park_tick>=stats.issued) return pjoin_park_tick==tick;
    pjoin_park_tick=tick;return 1;
}
static uint32_t pjoin_created_tick(uint64_t runtime) {
    if(!created_enabled() || !runtime) return 0;
    uint32_t tick=0;AcquireSRWLockShared(&created_lock);
    if(created_by_runtime) {unsigned slot=created_index_slot(created_by_runtime,0,runtime);if(created_by_runtime[slot]) tick=created_live[created_by_runtime[slot]-1u].tick;}
    ReleaseSRWLockShared(&created_lock);
    return tick;
}
/* "v1/m<slot mask>|k:c0-c1,s0-s1,b0-b1;<runtime>:<created>:<tick>,...|..." for every slot of the mask, hex; a slot without a
 * player says so (k:none). The IDs listed are the slots the construct and seat steps allocated (the body's connect is
 * undone by the park at T and redone by the guard on every client alike). 0 when it does not fit. */
static int pjoin_state(char *out,size_t capacity) {
    unsigned char *map=rid_mode==RID_SET?rid_map():NULL;
    int n=snprintf(out,capacity,"v1/m%x",pslot_mask);
    for(unsigned k=2;n>0 && (size_t)n<capacity && k<=PILOT_SLOTS;k++) {
        if(!(pslot_mask>>(k-1)&1u)) continue;
        if(!pslots[k].player) {n+=snprintf(out+n,capacity-(size_t)n,"|%u:none",k);continue;}
        n+=snprintf(out+n,capacity-(size_t)n,"|%u:",k);
        for(unsigned step=0;n>0 && (size_t)n<capacity && step<PJOIN_STEPS;step++)
            n+=snprintf(out+n,capacity-(size_t)n,"%s%x-%x",step?",":"",pjoin_range[k][step][0],pjoin_range[k][step][1]);
        char sep=';';
        static const unsigned listed[2]={PJOIN_CONSTRUCT,PJOIN_SEAT};
        for(unsigned i=0;map && n>0 && (size_t)n<capacity && i<2;i++) {
            const uint32_t a=pjoin_range[k][listed[i]][0],b=pjoin_range[k][listed[i]][1];
            for(uint32_t slot=a;n>0 && (size_t)n<capacity && a && slot<b && slot-a<64u;slot++) {
                const uint64_t runtime=((uint64_t)(rid_slot_generation(map,slot)&0xffffu)<<25)|slot;
                const uint64_t created=created_lookup(runtime);
                n+=snprintf(out+n,capacity-(size_t)n,"%c%llx:%llx:%x",sep,(unsigned long long)runtime,(unsigned long long)created,
                            created?pjoin_created_tick(runtime):0u);
                sep=',';
            }
        }
    }
    return n>0 && (size_t)n<capacity?n:0;
}
static int pjoin_status_text(char *out,size_t capacity) {
    return snprintf(out,capacity," join_park=%llu/%llu parks=%lld bodies=%x parked=%d",(unsigned long long)pjoin_park_tick,
                    (unsigned long long)pjoin_last_park,(long long)pjoin_parks,pfbody_mask,pfbody_parked);
}

/* ---- the joiner's side (W3 R1, R2; reports/LOCKSTEP-JOIN-IN-WINDOW-20261104.txt section 8) ---- */
static int join_ids_replay(const uint64_t *ids,unsigned n);   /* lockstep_join_ids.h (the hot join's allocator) */
static int join_ids_replay_end(void);
static int join_package_line(const char *name,char *out,size_t capacity);   /* lockstep_gate.c: a line of the package's LUA section */
#define PJOIN_IDS_MAX 32u
typedef struct {unsigned n,listed;uint32_t range[PJOIN_STEPS][2];uint64_t ids[PJOIN_IDS_MAX],created[PJOIN_IDS_MAX];uint32_t tick[PJOIN_IDS_MAX];} pjoin_slot_rx;
static pjoin_slot_rx pjoin_rx[PILOT_SLOTS+1];
static unsigned pjoin_rx_mask,pjoin_rx_slots;   /* slots with a player to rebuild; the session's slot mask */
static int pjoin_resumed;
static const char *pjoin_resume_why="-";
/* pilot_players (pjoin_state's text): 1 parsed into pjoin_rx; 0 refused (pjoin_resume_why). The IDs listed must be the
 * construct and seat ranges' slots, in allocation order. */
__attribute__((unused)) static int pjoin_parse_players(const char *text) {
    unsigned mask=0;int used=0;
    memset(pjoin_rx,0,sizeof(pjoin_rx));pjoin_rx_mask=0;pjoin_rx_slots=0;
    if(!text || sscanf(text,"v1/m%x%n",&mask,&used)!=1 || (mask&1u) || mask>=(1u<<PILOT_SLOTS)) {pjoin_resume_why="players_head";return 0;}
    text+=used;
    while(*text) {
        unsigned k=0;
        if(sscanf(text,"|%u:%n",&k,&used)!=1 || k<2 || k>PILOT_SLOTS || !(mask>>(k-1)&1u)) {pjoin_resume_why="players_slot";return 0;}
        text+=used;
        if(!strncmp(text,"none",4)) {text+=4;continue;}
        pjoin_slot_rx *r=&pjoin_rx[k];
        for(unsigned step=0;step<PJOIN_STEPS;step++) {
            unsigned a=0,b=0;
            if(sscanf(text,step?",%x-%x%n":"%x-%x%n",&a,&b,&used)!=2 || b<a) {pjoin_resume_why="players_range";return 0;}
            r->range[step][0]=a;r->range[step][1]=b;text+=used;
        }
        if(*text==';') {
            text++;
            for(;;) {
                unsigned long long id=0,created=0;unsigned tick=0;
                if(r->n>=PJOIN_IDS_MAX || sscanf(text,"%llx:%llx:%x%n",&id,&created,&tick,&used)!=3) {pjoin_resume_why="players_ids";return 0;}
                r->ids[r->n]=id;r->created[r->n]=created;r->tick[r->n]=tick;r->n++;text+=used;
                if(*text!=',') break;
                text++;
            }
        }
        /* Every slot of the construct and seat ranges, in order (the slot part of each ID). */
        unsigned want=0;
        for(unsigned step=0;step<2;step++) for(uint32_t slot=r->range[step][0];slot<r->range[step][1];slot++) {
            if(want>=r->n || (uint32_t)(r->ids[want]&0x1ffffffu)!=slot) {pjoin_resume_why="players_order";return 0;}
            want++;
        }
        if(want!=r->n || !r->n) {pjoin_resume_why="players_count";return 0;}
        r->listed=1;pjoin_rx_mask|=1u<<(k-1);
    }
    pjoin_rx_slots=mask;return 1;
}
/* The session's created identities of slot k's rebuilt objects, before the boundary resolves this frame's creations
 * (lockstep_world_created.h: a runtime already in the table is skipped there, counted duplicate, and takes no ordinal). */
__attribute__((unused)) static int pjoin_created_keys(unsigned k) {
    const pjoin_slot_rx *r=&pjoin_rx[k];
    if(!created_enabled()) return 1;
    int ok=1;AcquireSRWLockExclusive(&created_lock);
    for(unsigned i=0;ok && i<r->n;i++) {
        if(!r->created[i]) continue;
        if(created_find_runtime(r->ids[i]) || created_find_id(r->created[i])) ok=0;
        else ok=created_live_add(r->ids[i],r->created[i],r->tick[i]);
    }
    if(ok) InterlockedIncrement64(&world_created_generation);
    ReleaseSRWLockExclusive(&created_lock);
    return ok;
}
/* R1: slot k's player and seat as the session built them at take-2 and take-1, with the session's runtime IDs (one replay
 * around both) and created identities. */
__attribute__((unused)) static int pjoin_rebuild_slot(unsigned k) {
    const pjoin_slot_rx *r=&pjoin_rx[k];
    if(pslots[k].player) {pjoin_resume_why="rebuild_player_exists";return 0;}
    if(!join_ids_replay(r->ids,r->n)) {pjoin_resume_why="rebuild_ids_arm";return 0;}
    const int built=pslot_construct(k) && pslot_seat(k);
    const int placed=join_ids_replay_end();
    if(!built) {pjoin_resume_why=pslot_why;return 0;}
    if(!placed) {pjoin_resume_why="rebuild_ids";return 0;}
    if(!pjoin_created_keys(k)) {pjoin_resume_why="rebuild_created";return 0;}
    return 1;
}

