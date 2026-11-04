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
