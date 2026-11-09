/* Personal missions per player (reports/LOCKSTEP-UI-MISSIONS-20261101.txt section 4: C1 (i)(ii) and C5), ON BY DEFAULT
 * in sessions (native_gate_missions_per_player, tools/dev/native_gate_defaults.lua; false turns it off). Needs the pilot
 * slots, the UI replay's missions and events families and native_gate_script_rng with its MD bit, whose scopes see every
 * MD frame and its cue.
 *
 * Model (c) for slots 2..N in its first form: offers stay the session's (the generic missions manager offers around slot
 * 1's player), and whoever accepts one owns it.
 *  calls   slot k's replayed mission calls (the events family's SignalObject and AddUITriggeredEvent, the missions family's
 *          briefing, abort and active mission) run on every client inside slot k's scope (lockstep_pilot_slots.h
 *          pslot_enter: the universe's player singleton is P_k, the canonical player block holds slot k's store), so MD's
 *          player.entity, player.ship and player.sector are slot k's in every action they reach (C5). A scope that cannot
 *          be entered stops the replay (ui_replay_fail): the call never runs as slot 1's on some clients.
 *  claims  an accept by slot k (SignalObject(actor, 'accept', offer), the stock menus' form) claims the offer cue (the
 *          mission's cue: create_offer cue=, create_mission offercue=) by its runtime ID (+8) for slot k: an ownership
 *          table filled by replayed calls only, so the same on every client (C1 i). Not the cue's root instance (+0x170):
 *          a library mission's root instance can be the manager that made every offer, and a static cue's is its script.
 *  frames  every MD frame whose cue is an owned cue or one of its descendants (the parent chain at +0x158, nearest owner
 *          first; the script RNG's MD scopes: a cue's actions, its timed and waiting checks, its event listeners) enters
 *          its slot's scope at the frame's start and leaves it at its end, so the mission's objectives, distances,
 *          inventory and rewards follow P_k (C1 ii). A frame already in that slot's scope continues in it (inside=); a
 *          frame of another slot's mission inside a scope is counted (conflicts=) and runs in the scope it found.
 * Not yet: a mission manager per slot (C2: every client's HUD lists every slot's missions), factions mode's rewards (C3),
 * per slot offers (C4), slot k's notifications on slot k's screen only (C6) and conversations (one manager per process).
 * Hot join: the table at T rides the package as the probe's 'missions_per_player' state (mpp_state on the saver,
 * mpp_restore on the joiner before its first tick), in table order, so the joiner's frames and hash follow the same claims. */
#define MPP_OWNERS 512u
#define MPP_DEPTH 24u         /* parents from a frame's cue up to its owner (JOIN_CUE_DEPTH is 64 for any tree) */
#define MPP_CUE_PARENT 0x158u /* the parent cue (JOIN_CUE_PARENT) */
#define MPP_CUE_ID 0x8u       /* the cue's runtime ID, its key in the universe map */
typedef struct {uint64_t cue;unsigned slot;} mpp_owner_rec;
static mpp_owner_rec mpp_owners[MPP_OWNERS];
static unsigned mpp_count;
static int mpp_armed;
static volatile LONG64 mpp_claims,mpp_claim_gone,mpp_full,mpp_frames,mpp_entered,mpp_inside,mpp_conflicts,mpp_refused,
                       mpp_leave_failed,mpp_calls,mpp_call_refused;

static unsigned mpp_owner_of(uint64_t cue_id) {
    for(unsigned i=0;i<mpp_count;i++) if(mpp_owners[i].cue==cue_id) return mpp_owners[i].slot;
    return 0;
}
/* The slot owning this MD cue (the cue or its nearest owned ancestor), 0 = none, not a cue or unreadable. The frames' path:
 * guarded copies (sample_read), no system call per parent. */
static unsigned mpp_slot_of(const unsigned char *cue) {
    uintptr_t vt=0,at=(uintptr_t)cue;
    if(!cue || !sample_read(cue,&vt,8) || vt!=(uintptr_t)(image+SCRIPT_RNG_CUE_VT)) return 0;
    for(unsigned up=0;at && up<MPP_DEPTH;up++) {
        uint64_t id=0;uintptr_t parent=0;
        if(!sample_read((void *)(at+MPP_CUE_ID),&id,8)) return 0;
        const unsigned k=id?mpp_owner_of(id):0;
        if(k) return k;
        if(!sample_read((void *)(at+MPP_CUE_PARENT),&parent,8)) return 0;
        at=parent;
    }
    return 0;
}
/* Slot k owns this cue from now on (a later claim by another slot takes it over). 1 claimed, 0 the table is full. */
static int mpp_claim_id(unsigned k,uint64_t cue_id) {
    for(unsigned i=0;i<mpp_count;i++) if(mpp_owners[i].cue==cue_id) {mpp_owners[i].slot=k;InterlockedIncrement64(&mpp_claims);return 1;}
    if(mpp_count>=MPP_OWNERS) {InterlockedIncrement64(&mpp_full);return 0;}
    mpp_owners[mpp_count].cue=cue_id;mpp_owners[mpp_count].slot=k;++mpp_count;InterlockedIncrement64(&mpp_claims);return 1;
}
/* A replayed accept: slot k claims the offer cue with this runtime ID. 1 claimed, 0 the cue is gone here (the same on every
 * client when the worlds agree) or the table is full. */
static int mpp_claim(unsigned k,uint64_t cue_id) {
    if(!mpp_armed || k<2 || k>PILOT_SLOTS) return 0;
    if(!uimis_cue(cue_id)) {InterlockedIncrement64(&mpp_claim_gone);return 0;}
    return mpp_claim_id(k,cue_id);
}
/* The script RNG's MD scope (lockstep_script_rng.h script_rng_enter): the slot whose scope this frame entered, 0 none. */
static unsigned mpp_frame_enter(void *cue) {
    if(!mpp_armed || !mpp_count) return 0;
    const unsigned k=mpp_slot_of(cue);
    if(!k) return 0;
    InterlockedIncrement64(&mpp_frames);
    if(pstk_scope_slot==k) {InterlockedIncrement64(&mpp_inside);return 0;}
    if(pstk_scope_slot) {InterlockedIncrement64(&mpp_conflicts);return 0;}
    if(!pslots[k].player || !pslot_scope_begin(k,&pslots[k],"mission")) {InterlockedIncrement64(&mpp_refused);return 0;}
    InterlockedIncrement64(&mpp_entered);return k;
}
static void mpp_frame_leave(unsigned k) {if(!pslot_leave(k)) InterlockedIncrement64(&mpp_leave_failed);}
/* A replayed call of slot k (C5): 1 = inside slot k's scope now (mpp_call_leave after the call), 0 = refused. */
static int mpp_call_enter(unsigned k) {
    if(!mpp_armed || k<2 || k>PILOT_SLOTS || !pslots[k].player || pstk_scope_slot || !pslot_scope_begin(k,&pslots[k],"mission_call")) {
        InterlockedIncrement64(&mpp_call_refused);return 0;
    }
    InterlockedIncrement64(&mpp_calls);return 1;
}
static int mpp_call_leave(unsigned k) {
    if(pstk_scope_slot!=k || !pslot_leave(k)) {InterlockedIncrement64(&mpp_leave_failed);return 0;}
    return 1;
}
/* The claims as the rows compare them: a hash of (cue, slot) in table order. */
static uint64_t mpp_owners_hash(void) {
    uint64_t h=14695981039346656037ULL;
    for(unsigned i=0;i<mpp_count;i++) h=(h^lockstep_mix(mpp_owners[i].cue*31u+mpp_owners[i].slot))*1099511628211ULL;
    return h;
}
/* The table as one line for the join package: "v1|" then "slot:hexid" per claim in table order, comma separated. 0 = no room. */
static int mpp_state(char *out,size_t cap) {
    int n=snprintf(out,cap,"v1|");
    for(unsigned i=0;n>0 && (size_t)n<cap && i<mpp_count;i++)
        n+=snprintf(out+n,cap-(size_t)n,"%s%u:%llx",i?",":"",mpp_owners[i].slot,(unsigned long long)mpp_owners[i].cue);
    return n>0 && (size_t)n<cap?n:0;
}
/* A joiner's table from the saver's line, armed and still empty: 1 restored, 0 refused (malformed, a slot out of range, an ID
 * 0 or repeated, more than the table holds; nothing is kept then). The cues need not be resolvable here: the IDs are the
 * session's (the package's runtime IDs). */
static int mpp_restore(const char *text,size_t length) {
    if(!mpp_armed || mpp_count || !text || length<3 || memcmp(text,"v1|",3)) return 0;
    mpp_owner_rec rows[MPP_OWNERS];unsigned count=0;size_t at=3;
    while(at<length) {
        unsigned slot=0;uint64_t id=0;size_t digits=0;
        while(at<length && text[at]>='0' && text[at]<='9' && slot<100) slot=slot*10u+(unsigned)(text[at++]-'0'),++digits;
        if(!digits || slot<2 || slot>PILOT_SLOTS || at>=length || text[at++]!=':') return 0;
        digits=0;
        for(;at<length && text[at]!=',';at++,digits++) {
            const char c=text[at];const unsigned v=c>='0' && c<='9'?(unsigned)(c-'0'):c>='a' && c<='f'?(unsigned)(c-'a'+10):16u;
            if(v>15 || digits>=16) return 0;
            id=id<<4|v;
        }
        if(!digits || !id || count>=MPP_OWNERS) return 0;
        for(unsigned i=0;i<count;i++) if(rows[i].cue==id) return 0;
        if(at<length && ++at==length) return 0;   /* a trailing comma */
        rows[count].cue=id;rows[count].slot=slot;++count;
    }
    memcpy(mpp_owners,rows,count*sizeof(*rows));mpp_count=count;return 1;
}
static int mpp_arm(void) {
    if(mpp_armed) return 1;
    if(stats.issued) return 0;   /* before the first tick, as every module the probe arms */
    mpp_count=0;mpp_armed=1;return 1;
}
static int mpp_status(char *out,size_t cap) {
    return snprintf(out,cap,"armed=%d owners=%u hash=%016llx claims=%lld gone=%lld full=%lld frames=%lld entered=%lld inside=%lld "
        "conflicts=%lld refused=%lld calls=%lld call_refused=%lld leave_failed=%lld",mpp_armed,mpp_count,(unsigned long long)mpp_owners_hash(),
        (long long)mpp_claims,(long long)mpp_claim_gone,(long long)mpp_full,(long long)mpp_frames,(long long)mpp_entered,(long long)mpp_inside,
        (long long)mpp_conflicts,(long long)mpp_refused,(long long)mpp_calls,(long long)mpp_call_refused,(long long)mpp_leave_failed);
}
