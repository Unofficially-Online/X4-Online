/* Hot join, the world side table (package v2..v9; lockstep_join.h; reports/LOCKSTEP-HOTJOIN-20261101.txt 1.6..1.13). What X4's save
 * rounds or omits in the census health and motion domains (lockstep_world_hash.h), captured for every portable identity at
 * the save boundary of T and written into the joiner's loaded objects at its admission:
 *
 *  HEALTH  every destructible object (world_class_flags bit 0: the census health domain): its hull (+0x280, double) and,
 *          when it has one, the shield group it reads (+0x290; a ship's weapons read their ship's group, a build storage's
 *          cargo bays their build storage's): value (+0x68) and changed time (+0x70), with the maximum (+0x40) and the
 *          generator count ((+0x18 - +0x10) / 8) as shape checks. Value and changed time are taken as the census hashes them
 *          (lockstep_state.h shield_full_at at T: a recharge complete at T reads value = max, changed = 0), so every saver
 *          writes the same record whatever its own UI read.
 *          J1 j1-11 [F]: X4's loader gives a shield group without saved state (a full shield; the empty group of a station,
 *          a build storage and its 42 cargo bays) its loading clock as changed time: the census preimages of the cargo bay
 *          0x12b62, the build storage 0x12b60 and the station 0x12b39 solve to hull 2500/1000/0, an empty group, changed
 *          7.083 on the session (save_009's clock) and 28.183 on the joiner (j633's); the fixture ships' raw rows read 7.083
 *          at tick 2. A recharging shield is saved as <group value= time=> with six significant digits (fixture actor 1:
 *          26.41633... in the session, 26.4163 on the joiner at tick 660).
 *  POSE    every positional object whose controller is a pure transform (the census motion domain: getter 0x10ba20 reads
 *          four 16-byte vectors at controller+0x10, 0x46ff70 at +0x20): the x, y, z lanes of those vectors, as the census
 *          hashes them. J1 j1-11 [F]: the save writes a component's local rotation as rounded Euler degrees and the loader
 *          rebuilds another float rotation (turret 0x12b48 yaw="180" roll="-180" -> yaw="-179.99998" roll="179.99998",
 *          defence module 0x12bb6 yaw -179.08069 -> -179.08067); every differing turret, shield generator and station module
 *          whose offset the j1-11 analysis parsed had a changed rotation text, every one with an unchanged text was equal.
 * Navigation controllers (a ship's flight state: two banks, references, deadlines), dock controllers and unknown
 * controllers are not in POSE; objects of an unknown class are in neither (the census claims nothing for them either).
 * Records are sorted by portable ID and hold no pointer and no runtime ID. Each section enters the portable digest as one
 * line, its count and the SHA-256 of its records (lockstep_join.h join_portable): every saver must agree.
 *
 * The joiner's admission finds each record's object through its own identity table (the import keyed every object of the
 * join save by the session's portable ID), checks the class, the group's shape and the controller's kind and getter, and
 * writes what differs: hull, the group's value and changed time, the pose lanes. A group shared by several records is
 * checked again after all writes (health_conflict). Strict mode refuses any record it cannot place (world:<what>); report
 * mode counts them (JOIN_STATUS health=, pose=) and samples up to two per kind (gate.join_unpaired rows).
 *
 * Version 3 (J1 j1-12's census at T+1: 16,341 objects still differing after HEALTH and POSE; report 1.7):
 *  NAV     every navigation controller (vtable 0x2c51158, lockstep_world_nav.h's links checked): the state the census hashes
 *          in its motion and timing domains. RefreshFromPathingState 0xea2a20 refreshes a controller once its refresh time
 *          (+0xc0) has passed and its cursor (+0xc8) differs from the read selector g_PathingStateBufferIndex 0x38f4ee4: it
 *          takes the bank (+0x30 + 0x1a0 x index) and the pending entry (+0x50 + 16 x index) that selector names and sets the
 *          cursor to it. WaitAndSwapBuffers 0xeac170 flips both selectors in every frame with a non-zero step, so the
 *          absolute bank index depends on how many such frames a client ran: j1-12's joiner had read 1 at T+1 where the
 *          session had 0, and every ship's cursor 1 [F: the census rows' raw tuples]. What matters is the cursor relative to
 *          the selector (the census's relation) and the banks by role: the record holds the relation and both banks by role,
 *          and the joiner writes them relative to its own selectors (the selectors themselves are not touched). j1-12: the
 *          relation differed for 9,817 of 10,010 ships, the loader filled both banks with the save's rounded state and mode
 *          27 (the session: two states, mode 1), the pending transition count was 0 for 3,157 ships (the session 1..5), the
 *          parameter due -1 for 58 (the session a time) [F].
 *  ATTN    the attention level +0x1ec of every present object and the attention timer's due +0x2e0 of every Object. j1-12:
 *          6,244 objects at level 2 where the session had 5 (3,931) or 3 (2,313), all inside zones near the players' ships
 *          (zones at 5 or below are not re-derived by Zone::Move; their level is the history of the view points) [F]. The
 *          level is written directly: SetAttention 0x3d5660 would post ChangedAttentionEvent (MD event_object_changed_attention,
 *          AI on_attentionchange) that the session handled long before T; the levels involved (2, 3, 5) are below 7, where
 *          the per-level flags (vfunc 688/689) that gate bodies are off on both sides [F], so no body differs by level.
 *  KEYS    the gate's first-call tables (attention_all, the nav cadence's probe and parameter inits, the fixture actors'
 *          attention and navigation first refresh): j1-12's joiner re-based every due at its first admitted call (ATTENTION_ALL
 *          tick=634 inits=3, the session 0) [F].
 *  SHIELD  the shield group's census fields that HEALTH does not hold (delay, rate, factor, the modifiers' factors): delay,
 *          rate and factor are written, the modifiers (a separate object) only compared (shield_mods).
 * The admission also names the joiner's own created identities that the package does not hold (created_extra; j1-12: 85
 * objects, seven ships with their parts and crew, created keys of tick 0, not in the join save [F]), with loaded=1 when the
 * save held the object without a session key (the import's unmapped set). */
#define JOIN_HULL 0x280u
#define JOIN_SHIELD_GROUP 0x290u
#define JOIN_SHIELD_BEGIN 0x10u
#define JOIN_SHIELD_RATE 0x28u
#define JOIN_SHIELD_MAX 0x40u
#define JOIN_SHIELD_DELAY 0x48u
#define JOIN_SHIELD_FACTOR 0x50u
#define JOIN_SHIELD_VALUE 0x68u
#define JOIN_SHIELD_CHANGED 0x70u
#define JOIN_SHIELD_SOURCE 0x90u          /* the group's ShieldRechargeSource (vtable 0x2b19700), whose +8 is the group */
#define JOIN_SHIELD_MODS 0xa0u
#define JOIN_SHIELD_SOURCE_VT 0x2b19700u
#define JOIN_CONTROLLER 0x200u
#define JOIN_POSE_GETTER_10 0x10ba20u     /* four vectors at controller+0x10 */
#define JOIN_POSE_GETTER_20 0x46ff70u     /* four vectors at controller+0x20 */
#define JOIN_NAV_VTABLE 0x2c51158u
#define JOIN_WORLD_ROWS 2u                /* sampled rows per kind (health, pose, ...) at the admission */
#define JOIN_ATTENTION 0x1ecu              /* the attention level (SetAttention 0x3d5660) */
#define JOIN_ATTENTION_DUE 0x2e0u          /* the attention timer's due (0x6d1ff0, an Object's) */
#define JOIN_NAV_REFRESH 0xc0u             /* the last refresh's game time (RefreshFromPathingState 0xea2a20) */
#define JOIN_NAV_CURSOR 0xc8u              /* the read selector at the last refresh (0, 1; 2 none) */
#define JOIN_NAV_PARAMS_DUE 0xd0u          /* the parameter refresh due and its enable +0xd8 (lockstep_nav_cadence.h) */
#define JOIN_NAV_PARAMS_ON 0xd8u
#define JOIN_NAV_PROBE_DUE 0x3e0u          /* the obstacle probe's due and enable, in the probe block (controller+0x48) */
#define JOIN_NAV_PROBE_ON 0x3e8u
#define JOIN_SHIELD_MOD_FACTORS 0x18u      /* the modifiers' three factors (lockstep_state.h shield_state) */
/* The engine's component lookup (tests replace it and its map). */
typedef void *(*join_find_fn)(void *map,uint64_t runtime,unsigned mode);
static join_find_fn join_world_find;
static void *join_world_map;
/* The joiner's own (runtime, portable) pairs, as the saver's IDS section is built (tests replace it). */
static int join_capture_ids(join_tables *t);
static int (*join_world_ids)(join_tables *)=join_capture_ids;
static unsigned char *join_object(uint64_t runtime) {
    void *map=join_world_map;join_find_fn find=join_world_find;
    if(!find) {find=(join_find_fn)(image+0xced70);map=NULL;if(!join_read(image+0x6cf1500,&map,8)) return NULL;}
    if(!map || !runtime) return NULL;
    unsigned char *object=find(map,runtime,4);uint64_t id=0;
    return object && join_read(object+8,&id,8) && id==runtime?object:NULL;
}
/* The shield group an object reads, checked as lockstep_state.h shield_state checks it: *group NULL when it has none.
 * now: the census's canonical form at that time (a complete recharge reads value = max, changed = 0; *full set). 0: a
 * group that is no ShieldRechargeSource's or unreadable fields. */
static int join_shield_read(const unsigned char *object,unsigned char **group,uint64_t *value,uint64_t *changed,uint64_t *maximum,
                            uint32_t *count,const double *now,int *full) {
    unsigned char *g=NULL,*source_group=NULL,*modifiers=NULL;void *source_vt=NULL;uintptr_t begin=0,end=0;uint64_t delay=0;
    uint32_t rate=0,factor=0,mods[3]={0x3f800000u,0x3f800000u,0x3f800000u};
    *group=NULL;*value=*changed=*maximum=0;*count=0;*full=0;
    if(!join_read(object+JOIN_SHIELD_GROUP,&g,8)) return 0;
    if(!g) return 1;
    if(!join_read(g+JOIN_SHIELD_SOURCE,&source_vt,8) || source_vt!=(void *)(image+JOIN_SHIELD_SOURCE_VT) ||
       !join_read(g+JOIN_SHIELD_SOURCE+8,&source_group,8) || source_group!=g ||
       !join_read(g+JOIN_SHIELD_BEGIN,&begin,8) || !join_read(g+JOIN_SHIELD_BEGIN+8,&end,8) || end<begin || (end-begin)%8 || (end-begin)/8>4096 ||
       !join_read(g+JOIN_SHIELD_VALUE,value,8) || !join_read(g+JOIN_SHIELD_CHANGED,changed,8) || !join_read(g+JOIN_SHIELD_MAX,maximum,8) ||
       !join_read(g+JOIN_SHIELD_DELAY,&delay,8) || !join_read(g+JOIN_SHIELD_RATE,&rate,4) || !join_read(g+JOIN_SHIELD_FACTOR,&factor,4) ||
       !join_read(g+JOIN_SHIELD_MODS,&modifiers,8) || (modifiers && !join_read(modifiers+0x18,mods,12))) return 0;
    *count=(uint32_t)((end-begin)/8);*group=g;
    if(now) {
        double v,c,m,d,full_value;float r,f,fm[3];
        memcpy(&v,value,8);memcpy(&c,changed,8);memcpy(&m,maximum,8);memcpy(&d,&delay,8);memcpy(&r,&rate,4);memcpy(&f,&factor,4);memcpy(fm,mods,12);
        if(shield_full_at(v,c,m,d,r,f,*count,fm,*now,&full_value)) {memcpy(value,&full_value,8);*changed=0;*full=1;}
    }
    return 1;
}
/* A controller's pose form as the census reads it: 1 (getter 0x10ba20, vectors at +0x10), 2 (0x46ff70, +0x20), 0 none (a
 * navigation, dock or unknown controller); *kind its vtable RVA. -1: unreadable. */
static int join_pose_form(const unsigned char *controller,uint32_t *kind) {
    uintptr_t vtable=0,getter=0;*kind=0;
    if(!join_read(controller,&vtable,8)) return -1;
    vtable=pslot_canonical_vtable(vtable);
    if(vtable<(uintptr_t)image || vtable>=(uintptr_t)(image+0x6fa9000)) return -1;
    *kind=(uint32_t)(vtable-(uintptr_t)image);
    if(*kind==JOIN_NAV_VTABLE || *kind==DOCK_CONTROLLER_VTABLE || *kind==UNDOCK_CONTROLLER_VTABLE) return 0;
    if(!join_read((void *)(vtable+0x30),&getter,8)) return -1;
    return getter==(uintptr_t)(image+JOIN_POSE_GETTER_10)?1:getter==(uintptr_t)(image+JOIN_POSE_GETTER_20)?2:0;
}
static int join_pose_lanes(const unsigned char *controller,unsigned form,uint32_t xyz[12]) {
    uint32_t words[16];
    if(!join_read(controller+(form==1?0x10u:0x20u),words,sizeof(words))) return 0;
    for(unsigned v=0;v<4;v++) memcpy(xyz+3*v,words+4*v,12);
    return 1;
}
static int join_health_cmp(const void *a,const void *b) {
    uint64_t x=((const join_health_rec *)a)->portable,y=((const join_health_rec *)b)->portable;return x<y?-1:x>y;
}
static int join_pose_cmp(const void *a,const void *b) {
    uint64_t x=((const join_pose_rec *)a)->portable,y=((const join_pose_rec *)b)->portable;return x<y?-1:x>y;
}
/* A shield group's census fields beyond value and changed time (lockstep_state.h shield_state): delay, rate, factor and the
 * modifiers' three factors (1.0 each without modifiers). 0: unreadable. */
static int join_shield_params(const unsigned char *group,join_shield_rec *r) {
    unsigned char *modifiers=NULL;r->mods[0]=r->mods[1]=r->mods[2]=0x3f800000u;r->flags=0;
    if(!join_read(group+JOIN_SHIELD_DELAY,&r->delay,8) || !join_read(group+JOIN_SHIELD_RATE,&r->rate,4) ||
       !join_read(group+JOIN_SHIELD_FACTOR,&r->factor,4) || !join_read(group+JOIN_SHIELD_MODS,&modifiers,8)) return 0;
    if(modifiers) {if(!join_read(modifiers+JOIN_SHIELD_MOD_FACTORS,r->mods,12)) return 0;r->flags|=JOIN_SHIELD_MODS_PRESENT;}
    return 1;
}
static uint64_t join_nav_key(uint64_t runtime) {unsigned klass;return runtime?join_key(runtime,&klass):0;}
/* A navigation controller as NAV holds it (lockstep_join.h join_nav_rec): the census's fields by role, references as portable
 * IDs. 0 with join_fault naming the object. */
static int join_nav_capture(unsigned char *object,unsigned char *controller,uint64_t runtime,uint64_t portable,join_nav_rec *r) {
    world_nav_snapshot n;const char *why="nav";uint32_t cursor=0;memset(r,0,sizeof(*r));r->portable=portable;
    if(!world_nav_take(join_read,(uintptr_t)image,(uintptr_t)object,(uintptr_t)controller,runtime,&n,&why))
        return join_note("nav,id=%llx,why=%s",(unsigned long long)portable,why);
    world_nav_resolve(&n,join_nav_key);
    memcpy(&cursor,n.nav+8,4);
    if(cursor>2) return join_note("nav,id=%llx,why=cursor,cursor=%u",(unsigned long long)portable,cursor);
    memcpy(&r->refresh,n.nav,8);memcpy(&r->params_due,n.nav+12,8);r->params_on=n.nav[20];
    r->relation=cursor==2?2u:cursor==n.links.read?0u:1u;
    if(!join_read((void *)(n.links.commands+JOIN_NAV_PROBE_DUE),&r->probe_due,8) || !join_read((void *)(n.links.commands+JOIN_NAV_PROBE_ON),&r->probe_on,1))
        return join_note("nav,id=%llx,why=probe",(unsigned long long)portable);
    /* A probe that is off: its due is per process (set at the boot with a per-client spread, never read until the probe is
     * re-armed with a fresh due) and the session's clients differ in it while their worlds stay equal: J1 j1-13 refused with
     * saver_disagree, NAV the only section apart, every one of the 9,916 differing records off and past due. Not carried. */
    if(!r->probe_on) r->probe_due=0;
    for(unsigned role=0;role<2;role++) {
        const unsigned p=role?n.links.write:n.links.read;const unsigned char *bank=n.banks[p];join_nav_bank *b=&r->bank[role];
        uint64_t refs[2];memcpy(refs,bank+0xb0,16);
        for(unsigned v=0;v<6;v++) memcpy(b->xyz+3*v,bank+16*v,12);
        for(unsigned j=0;j<2;j++) {b->ref[j]=n.bank_saved[p][j];if(refs[j] && !b->ref[j]) r->flags|=(uint8_t)(1u<<(3*role+j));}
        memcpy(&b->eta,bank+0x150,8);memcpy(&b->mode,bank+0xcc,4);b->arrived=bank[0x159];b->notified=bank[0x15a];b->layer=bank[0x15b];
        b->pending_state=n.states[p];b->pending_ref=n.saved[p];b->pending_count=n.pending[p][1];
        if(n.states[p]==WORLD_NAV_UNKNOWN) r->flags|=(uint8_t)(1u<<(3*role+2));
    }
    return 1;
}
static int join_key_cmp(const void *a,const void *b) {
    const join_key_rec *x=a,*y=b;
    if(x->table!=y->table) return x->table<y->table?-1:1;
    return x->key<y->key?-1:x->key>y->key;
}
/* KEYS: the first-call tables of the armed gate modules, sorted by (table, key). 0 with join_fault set. */
static int join_capture_keys(join_tables *t) {
    uint32_t cap=0;
    if(attention_all_armed) for(unsigned i=0;i<ATTENTION_ALL_TABLE;i++) if(attention_all_table[i]) {
        join_key_rec k={attention_all_table[i],JOIN_KEYS_ATTENTION_ALL,0};if(!JOIN_PUSH(t->keys,t->nkeys,cap,k)) return join_note("memory");
    }
    if(nav_probe_armed) for(unsigned i=0;i<NAV_PROBE_TABLE;i++) if(nav_probe_table[i]) {
        join_key_rec k={nav_probe_table[i],JOIN_KEYS_NAV_PROBE,0};if(!JOIN_PUSH(t->keys,t->nkeys,cap,k)) return join_note("memory");
    }
    if(nav_params_armed) for(unsigned i=0;i<NAV_PROBE_TABLE;i++) if(nav_params_table[i]) {
        join_key_rec k={nav_params_table[i],JOIN_KEYS_NAV_PARAMS,0};if(!JOIN_PUSH(t->keys,t->nkeys,cap,k)) return join_note("memory");
    }
    for(unsigned a=0;a<ACTOR_CAPACITY;a++) for(unsigned which=0;which<2;which++) {
        /* A slot without its object any more: nothing calls the hook for it, nothing to carry. A live actor without a portable
         * ID refuses (fixture actors are saved identities). */
        if(!(which?nav_initialized[a]:attention_initialized[a]) || !actor_ids[a]) continue;
        unsigned klass;uint64_t key=join_key(actor_ids[a],&klass);
        if(!key) return join_note("keys,actor=%u,why=%s",a+1,which?"nav":"attention");
        join_key_rec k={key,which?JOIN_KEYS_NAV_ACTOR:JOIN_KEYS_ATTENTION_ACTOR,a+1};if(!JOIN_PUSH(t->keys,t->nkeys,cap,k)) return join_note("memory");
    }
    if(t->nkeys) qsort(t->keys,t->nkeys,sizeof(*t->keys),join_key_cmp);
    for(uint32_t i=1;i<t->nkeys;i++) if(!join_key_cmp(&t->keys[i],&t->keys[i-1]))
        return join_note("keys_duplicate,table=%u,key=%llx",t->keys[i].table,(unsigned long long)t->keys[i].key);
    return 1;
}
static int join_shield_cmp(const void *a,const void *b) {
    uint64_t x=((const join_shield_rec *)a)->portable,y=((const join_shield_rec *)b)->portable;return x<y?-1:x>y;
}
static int join_nav_cmp(const void *a,const void *b) {
    uint64_t x=((const join_nav_rec *)a)->portable,y=((const join_nav_rec *)b)->portable;return x<y?-1:x>y;
}
static int join_attn_cmp(const void *a,const void *b) {
    uint64_t x=((const join_attn_rec *)a)->portable,y=((const join_attn_rec *)b)->portable;return x<y?-1:x>y;
}
/* ---- version 4 (J1 j1-14, report 1.8) ----
 * PATH. The pathing manager (*0x6cf21c8) runs every navigating Agent of a batch (UpdateBatch 0xeaecc0, from the batch's cursor;
 * native_gate_path_budget: the first N full, the rest cheap, the cursor moves to the first cheap one) and the gate stamps every
 * batched Agent's controller after the joined pass (lockstep_path_determinism.h). Batches and new agents are per process: a new
 * Agent (CreateAgent 0xeac530, which the loader calls for every ship) goes onto the new-agent list +0x170, and the next pass
 * (RunPass 0xeae3c0) assigns that list round-robin (AssignAgentToBatch 0xeaf2b0: indices +0x1b8/+0x1c0) before it updates the
 * batches. The load reset 0xeac1f0 (and the constructor) set +0x208, so the first wake after a load runs no pass at all.
 * J1 j1-14 [F]: the joiner's PATH_DETERMINISM at T+1 = 634 read count=0 (the session's tick 1 after its own snapshot load did
 * too, count=10029 at tick 2): no Agent of the joiner was updated or stamped in step T+1, so the 8,906 low-attention ships the
 * session stamped before the bank flip kept the refresh time and cursor NAV had written (nav.refresh_time and refresh_relation
 * at T+1), their banks missed one pathing step (motion), and the batch layout built at T+2 from the joiner's load order never
 * matched the session's (walk= differs at every later tick on the joiner, equal on the two savers).
 * The saver writes the manager's two batch vectors (count, round-robin index), every batch (flag, members in list order from
 * the sentinel by their owner's portable ID, the cursor's index) and the new-agent list. The joiner, whose batches are empty
 * after its load and whose new-agent list holds every loaded Agent, moves each list node (0x18 bytes: +0 prev, +8 next, +0x10
 * Agent; the same allocation CreateAgent makes and RunPass and RemoveAgent 0xeaeee0 free) from the new-agent list into the
 * session's batch at the session's position, sets each cursor and the round-robin indices, leaves the session's new agents on
 * the list in the session's order, parks its own extra Agents (counted; strict refuses them: j1-14's joiner had seven ships of its
 * own load) on a ring of the gate's that no pass reads, so they cannot shift the session's layout, and then wakes the pathing
 * thread once with the
 * skip flag set: Run 0xeabf00 copies the game time of T into its TLS clock and +0x200, runs no pass and clears the flag, so the
 * pass of T+1 runs with dt = 1/hz like the session's. No pointer, no runtime ID in the records.
 * REGION. A hazard region's DamageField (lockstep_region_rng.h) passes when the TLS time exceeds its due +0x10f8 and damages
 * every object in its zones in proportion to now - last (+0x10f0). The loader sets every field's due to the loading clock
 * [F: j1-14's joiner logged a pass of every field at T+1 with prior due 28.183 = T], so every field passed in the joiner's
 * first step: 325 objects of cluster_32 (186 Xenon ships with their weapons and shield generators) lost (float)(1/30) shield
 * there at T+1 with changed time T+1, where the session's last pass had been at 27.3163 (tick 607) and its next was due later.
 * The saver writes every field's last and due; the joiner writes them where they differ. */
#define JOIN_PATH_MANAGER PATH_DET_MANAGER   /* g_PathingManager 0x6cf21c8 */
#define JOIN_PATH_DONE 0x10u                 /* semaphore: Run releases it after a wake, WaitAndSwapBuffers waits for it */
#define JOIN_PATH_WAKE 0x18u                 /* semaphore: MoveWorker::Run releases it at a frame's start (0xef4614) */
#define JOIN_PATH_STATE 0x84u                /* int: 2 while the pathing thread's loop runs */
#define JOIN_PATH_WAITED 0x88u               /* byte: 0 from a pass's release until WaitAndSwapBuffers waited for it */
#define JOIN_PATH_PENDING 0x170u             /* the new-agent list: count +0x170, sentinel +0x178 */
#define JOIN_PATH_VECTORS 0x180u             /* the batch vectors {begin, end, capacity} at +0x180 and +0x198 */
#define JOIN_PATH_NEXT 0x1b8u                /* AssignAgentToBatch's round-robin index of vector 0 (+0x1b8) and 1 (+0x1c0) */
#define JOIN_PATH_TIME 0x200u                /* the last wake's game time (Run 0xeac06f) */
#define JOIN_PATH_SKIP 0x208u                /* byte: the next wake runs no pass; every wake clears it (Run 0xeac0c0, 0xeac0e2) */
#define JOIN_PATH_BATCH 0x20u                /* a batch: +0 flag byte, +8 count, +0x10 sentinel, +0x18 cursor */
#define JOIN_PATH_BATCHES_MAX 4096u
#define JOIN_PATH_LIST_MAX 1000000u
#define JOIN_REGION_VTABLE 0x2b2ad60u        /* U::Region (lockstep_region_rng.h: its slot 674 is Region::Move) */
/* The structure of a PATH section: the two vector records, every batch of each, every member of each batch, the new agents;
 * positions consecutive, a member's portable ID present exactly when its local flag is not; a cursor inside a non-empty batch
 * (UpdateBatch reads the Agent at the cursor), the sentinel (index 0) of an empty one. 0: not a PATH section. */
static int join_path_shape(const join_path_rec *r,uint32_t n) {
    uint32_t i=0,batches[2];
    for(unsigned v=0;v<2;v++,i++) {
        if(i>=n || r[i].kind!=JOIN_PATH_KIND_MANAGER || r[i].vector!=v || r[i].position || r[i].portable || r[i].flags ||
           r[i].batch>JOIN_PATH_BATCHES_MAX || (r[i].batch?r[i].value>=r[i].batch:r[i].value!=0)) return 0;
        batches[v]=r[i].batch;
    }
    const uint32_t first=i;
    for(unsigned v=0;v<2;v++) for(uint32_t b=0;b<batches[v];b++,i++) {
        if(i>=n || r[i].kind!=JOIN_PATH_KIND_BATCH || r[i].vector!=v || r[i].batch!=b || r[i].portable || r[i].flags>255u ||
           r[i].value>JOIN_PATH_LIST_MAX || (r[i].value?r[i].position>=r[i].value:r[i].position!=0)) return 0;
    }
    uint32_t at=first;
    for(unsigned v=0;v<2;v++) for(uint32_t b=0;b<batches[v];b++,at++) for(uint32_t p=0;p<r[at].value;p++,i++) {
        if(i>=n || r[i].kind!=JOIN_PATH_KIND_MEMBER || r[i].vector!=v || r[i].batch!=b || r[i].position!=p || r[i].value ||
           (r[i].flags&~JOIN_PATH_LOCAL) || (r[i].portable==0)!=((r[i].flags&JOIN_PATH_LOCAL)!=0)) return 0;
    }
    for(uint32_t p=0;i<n;p++,i++) {
        if(r[i].kind!=JOIN_PATH_KIND_PENDING || r[i].vector || r[i].batch || r[i].position!=p || r[i].value ||
           (r[i].flags&~JOIN_PATH_LOCAL) || (r[i].portable==0)!=((r[i].flags&JOIN_PATH_LOCAL)!=0)) return 0;
    }
    return 1;
}
/* ---- version 5: FLIGHT and SPLINE (J1 j1-15's census at T+1; report 1.9) ----
 * The census motion of a flying ship is its pathing banks (lockstep_world_nav.h). The Agent update (0xe8f250) copies the
 * read bank into the write bank and, on a full update, has the flight control model at Agent +0xf10 step into the write bank
 * (vfunc14); a type 3 model (LinearFlightControlModel) is not stepped on a cheap update (0xe848c0). The linear step's dt is
 * the game time minus the model's own last step +0x88 (the frame's dt only while +0x88 is -1, as the constructor 0xeb1a10
 * leaves it), so a ship stepped every fifth tick (a batch of ~500 with 100 full updates per tick) moves five frames at once.
 * The save writes no model state (<flightcontrolmodel type="linear"/>): the joiner's loaded models are new (+0x88 -1, the
 * new-leg flag +0x94 1, an empty leg spline), and each one's first step moved one frame from a new leg where the session's
 * moved five along its leg. */
#define JOIN_AGENT_MODEL 0xf10u              /* the Agent's flight control model */
#define JOIN_FLIGHT_LINEAR 0x2c4f610u        /* U::Path::LinearFlightControlModel (model type 3) */
#define JOIN_FLIGHT_OWNER 0x10u              /* the model's Agent */
#define JOIN_FLIGHT_WORD_AT 0x60u            /* SetState's third argument (0 from the Agent update) */
#define JOIN_FLIGHT_SPLINE 0xe0u             /* the leg spline, a Math::CompositeSpline<5> */
#define JOIN_FLIGHT_SPLINE_VT 0x2b6c6d0u
#define JOIN_FLIGHT_SEGMENTS_AT 0xf0u        /* spline +0x10: {begin, end, capacity} of 0xa0-byte Bezier segments */
#define JOIN_FLIGHT_PAIRS_AT 0x108u          /* spline +0x28: {begin, end, capacity} of 8-byte parameter pairs */
#define JOIN_FLIGHT_BEZIER_VT 0x2a84ef8u     /* Math::BezierSpline, at a segment's +0x10 (segment constructor 0x337280) */
#define JOIN_FLIGHT_BEZIER_AT 0x10u
#define JOIN_FLIGHT_ALLOC 0x974c0u           /* the engine's allocator (SMem below 0x1000 bytes, aligned above) */
#define JOIN_FLIGHT_IMAGE 0x6fa9000u
static const struct {uint16_t at,len;} join_flight_ranges[]={{0x20,0x40},{0x68,0x78},{0xe8,4},{0x120,0xc},{0x130,4}};
_Static_assert(0x40+0x78+4+0xc+4==JOIN_FLIGHT_STATE,"FLIGHT state ranges");
/* The records a FLIGHT section names in SPLINE, in order: 0 when they differ in any way (a record's own fields included). */
static int join_flight_shape(const join_flight_rec *f,uint32_t nf,const join_spline_rec *sp,uint32_t ns) {
    static const unsigned char zero[JOIN_FLIGHT_STATE];uint32_t j=0;
    for(uint32_t i=0;i<nf;i++) {
        const join_flight_rec *r=&f[i];
        if(!r->portable || !r->kind || r->reserved || (r->flags&~7u) || r->segments>JOIN_FLIGHT_SEGMENTS || r->pairs>JOIN_FLIGHT_PAIRS) return 0;
        if(r->flags&JOIN_FLIGHT_OTHER) {if(r->flags!=JOIN_FLIGHT_OTHER || r->kind==JOIN_FLIGHT_LINEAR || memcmp(r->state,zero,sizeof(zero))) return 0;}
        else if(r->kind!=JOIN_FLIGHT_LINEAR) return 0;
        if((r->flags&(JOIN_FLIGHT_OTHER|JOIN_FLIGHT_UNCARRIED)) && (r->segments || r->pairs)) return 0;
        for(uint32_t k=0;k<r->segments;k++,j++) {
            if(j>=ns || sp[j].portable!=r->portable || sp[j].part!=0 || sp[j].index!=k) return 0;
            for(unsigned b=JOIN_FLIGHT_BEZIER_AT;b<JOIN_FLIGHT_BEZIER_AT+8;b++) if(sp[j].data[b]) return 0;
        }
        for(uint32_t k=0;k*JOIN_FLIGHT_PAIRS_PER<r->pairs;k++,j++) {
            if(j>=ns || sp[j].portable!=r->portable || sp[j].part!=1 || sp[j].index!=k) return 0;
            uint32_t used=r->pairs-k*JOIN_FLIGHT_PAIRS_PER;if(used>JOIN_FLIGHT_PAIRS_PER) used=JOIN_FLIGHT_PAIRS_PER;
            for(unsigned b=used*8u;b<sizeof(sp[j].data);b++) if(sp[j].data[b]) return 0;
        }
    }
    return j==ns;
}
/* A vector {begin, end, capacity} of elements: *n its length, *capacity its room. 0 when malformed. */
static int join_flight_vector(const unsigned char *at,unsigned element,uintptr_t v[3],uint64_t *n,uint64_t *capacity) {
    if(!join_read(at,v,24)) return 0;
    if(!v[0]) {*n=*capacity=0;return !v[1] && !v[2];}
    if(v[1]<v[0] || v[2]<v[1] || (v[1]-v[0])%element || (v[2]-v[0])%element) return 0;
    *n=(v[1]-v[0])/element;*capacity=(v[2]-v[0])/element;return 1;
}
/* Padding inside the carried ranges: per process (J1 j1-18 saver_disagree, FLIGHT the only section apart, 108 records: +0xac..+0xaf
 * after the float leg length +0xa8 held ffffffff / 0 / stale bytes, +0x133 after the three spline flags +0x130..+0x132;
 * the session's clients stay equal with them apart). Carried as 0. */
static const struct {unsigned at,len;} join_flight_padding[]={{0xac,4},{0x133,1}};
static int join_flight_state_read(const unsigned char *model,unsigned char state[JOIN_FLIGHT_STATE]) {
    unsigned at=0;
    for(unsigned i=0;i<sizeof(join_flight_ranges)/sizeof(*join_flight_ranges);i++) {
        if(!join_read(model+join_flight_ranges[i].at,state+at,join_flight_ranges[i].len)) return 0;
        for(unsigned p=0;p<sizeof(join_flight_padding)/sizeof(*join_flight_padding);p++) {
            const unsigned lo=join_flight_ranges[i].at,hi=lo+join_flight_ranges[i].len;
            for(unsigned b=join_flight_padding[p].at;b<join_flight_padding[p].at+join_flight_padding[p].len;b++)
                if(b>=lo && b<hi) state[at+b-lo]=0;
        }
        at+=join_flight_ranges[i].len;
    }
    return 1;
}
/* ---- version 6: AGENT (J1 j1-20's census at T+1; report 1.10) ----
 * With FLIGHT the joiner stepped its ships from the session's leg and clock (j1-20: the watched 0x6a8b4 4 mm from the session at
 * T+1, j1-15 30.4 m), yet 1,790 of the 1,804 ships the pass of T+1 updated in full still differed, all of the formation members
 * and leaders among them; the watched leader 0x6dbae slowed at 405 m/s^2 on the joiner in j1-15 and j1-20 alike where the
 * session sped up at 251 m/s^2, the wingman 0xa488e sped up at 209 m/s^2 in both where the session held 4 m/s [F: the census
 * at T+1 and the j633 records]. Neither depends on the model's state: the step's speed and turn inputs are its Agent's (vfunc14
 * 0xeb27c0 reads the Agent at model+0x10: acceleration +0x340, deceleration +0x344, the speed limit +0x364, the arrival
 * distance +0x4c8, the mode bytes +0x4d0/+0x4d2, the turn rates +0x518..+0x520, the group it leads +0xf18 with each member's
 * +0x364/+0x4d8, the formation it follows +0xf20, the waypoint count) and the Agent update 0xe8f250 (the clamps +0x348..+0x360,
 * the throttle rates +0x38c/+0x390, the re-target flag +0xf33 that calls the waypoint follower 0xe8c380 and its SetState
 * before the step, the pending waypoints +0x6c0 that set it, +0xf5e/+0xf5f, +0xf77/+0xf79/+0xf7a). The save writes none of
 * them; the ship's engine parameters are recomputed by a scheduled event (<event event="updateengineparameters" time=...> in
 * j633, rounded) and at load. AGENT carries them: the joiner compares every field (agent_fields), writes the values (the first
 * JOIN_AGENT_WRITTEN; agent fixed) and only counts the flags, the vector lengths and the pending factor (agent state). */
static const uint16_t join_agent_value_at[JOIN_AGENT_VALUES]={0x340,0x344,0x348,0x34c,0x358,0x35c,0x360,0x364,0x36c,0x38c,0x390,0x4c8,0x4d8,
                                                              0x504,0x518,0x51c,0x520,0xf34};
#define JOIN_AGENT_WRITTEN 17u               /* +0xf34 (a factor the Agent update hands its model once, then clears) is compared */
#define JOIN_AGENT_FLAG_COUNT 10u
static const uint16_t join_agent_flag_at[JOIN_AGENT_FLAG_COUNT]={0x4d0,0x4d1,0x4d2,0x4d3,0xf33,0xf5e,0xf5f,0xf77,0xf79,0xf7a};
#define JOIN_AGENT_WAYPOINTS 0x6a8u          /* {begin, end} of the waypoints (0x90-byte entries) */
#define JOIN_AGENT_PENDING 0x6c0u            /* {begin, end} of the waypoints not applied yet */
#define JOIN_AGENT_WAYPOINT 0x90u
#define JOIN_AGENT_GROUP 0xf18u              /* the group the Agent leads: its member vector {begin, end} at +0x48 */
#define JOIN_AGENT_FORMATION 0xf20u          /* the formation the Agent follows */
#define JOIN_AGENT_FIELDS (JOIN_AGENT_VALUES+JOIN_AGENT_FLAG_COUNT+4u)
_Static_assert(JOIN_AGENT_FIELDS==sizeof(join_rx.agent_field)/sizeof(*join_rx.agent_field),"JOIN_STATUS agent_fields");
static const char *const join_agent_field_names[JOIN_AGENT_FIELDS]={
    "a340","a344","a348","a34c","a358","a35c","a360","a364","a36c","a38c","a390","a4c8","a4d8","a504","a518","a51c","a520","af34",
    "b4d0","b4d1","b4d2","b4d3","bf33","bf5e","bf5f","bf77","bf79","bf7a","waypoints","pending","members","formation"};
/* A vector {begin, end} of element-sized entries: *n its length (at most 0xffff). 0 malformed. */
static int join_vector_length(const unsigned char *at,unsigned element,uint64_t *n) {
    uintptr_t v[2];
    if(!join_read(at,v,16) || v[1]<v[0] || (v[1]-v[0])%element) return 0;
    *n=(v[1]-v[0])/element;return *n<=0xffffu;
}
/* An Agent's AGENT fields (r->portable kept). 0: unreadable or a vector out of shape. */
static int join_agent_read(const unsigned char *agent,join_agent_rec *r) {
    uint64_t portable=r->portable,n=0;unsigned char *group=NULL,*formation=NULL;memset(r,0,sizeof(*r));r->portable=portable;
    for(unsigned i=0;i<JOIN_AGENT_VALUES;i++) if(!join_read(agent+join_agent_value_at[i],&r->value[i],4)) return 0;
    for(unsigned i=0;i<JOIN_AGENT_FLAG_COUNT;i++) if(!join_read(agent+join_agent_flag_at[i],&r->flag[i],1)) return 0;
    if(!join_vector_length(agent+JOIN_AGENT_WAYPOINTS,JOIN_AGENT_WAYPOINT,&n)) return 0;
    r->waypoints=(uint16_t)n;
    if(!join_vector_length(agent+JOIN_AGENT_PENDING,JOIN_AGENT_WAYPOINT,&n)) return 0;
    r->pending=(uint16_t)n;
    if(!join_read(agent+JOIN_AGENT_GROUP,&group,8) || !join_read(agent+JOIN_AGENT_FORMATION,&formation,8)) return 0;
    if(group) {if(!join_vector_length(group+0x48,8u,&n)) return 0;r->members=(uint16_t)n;}
    r->formation=formation!=NULL;return 1;
}
/* AGENT exactly one record per FLIGHT record, in its order; reserved and the unused flag bytes 0, formation 0 or 1. */
static int join_agent_shape(const join_flight_rec *f,uint32_t nf,const join_agent_rec *a,uint32_t na) {
    if(na!=nf) return 0;
    for(uint32_t i=0;i<na;i++)
        if(a[i].portable!=f[i].portable || a[i].reserved || a[i].formation>1u || a[i].flag[JOIN_AGENT_FLAG_COUNT] || a[i].flag[JOIN_AGENT_FLAG_COUNT+1]) return 0;
    return 1;
}
static int join_agent_cmp(const void *a,const void *b) {
    uint64_t x=((const join_agent_rec *)a)->portable,y=((const join_agent_rec *)b)->portable;return x<y?-1:x>y;
}
/* ---- version 7: ROUTE (J1 j1-21's T+1 probe; report 1.11) ----
 * With AGENT the joiner's step inputs were the session's, yet at T+1 the joiner's linear models started a new leg on 2,026 of
 * the 2,040 ships the pass stepped in full and changed their target on 2,012, the session's on 37 and 55 (JOIN_STOP t1=). Its
 * Agents re-targeted before the step: the save writes an Agent's waypoint as <targetpoints> (4,852 of j633's 10,010
 * navigation Agents; none for the 5,111 formation wingmen) and the loader queues it as a pending waypoint (+0x6c0) with the
 * re-target flag +0xf33 set, where the session held it applied (+0x6a8: 9,600 Agents with one waypoint, no pending one, +0xf33
 * clear) [F: AGENT at T, j633's <movement> elements, the joiner's 'agent side=' rows f33=0/1 waypoints=1/0 pending=0/1].
 * The Agent update 0xe8f250, on a full update, moves pending waypoints into +0x6a8 (0xe8dfc0 per entry), sets +0xf33 and,
 * while +0xf33 is set, calls the waypoint follower 0xe8c380, which SetStates the model (vfunc3 0xeaf620: target, word +0x60,
 * +0x6c/+0x6d) from the first waypoint, or, with none and a formation (a wingman), from its slot: the step then starts a new
 * leg. ROUTE carries both vectors' U::Path::FlightPoint entries (vtable 0x2c4f578, 0x90 bytes: reference ID +0x10, connection
 * +0x18, transform +0x20..+0x5f, key +0x60, behaviour +0x64, an owned U::NavPoint +0x68 (vtable 0x2ae9380, 0x60 bytes:
 * reference +0x10, connection +0x18, transform +0x20..+0x5f), +0x70, flags +0x74..+0x7e, removal time +0x80, +0x88, +0x8c; +0x08
 * and +0x7f are not copied by the engine's own copy, so per process), the Agent's IDs the follower and the update compare
 * with them (+0xef8, +0xf08, +0xf48, +0xf60), the bytes they derive from the first waypoint (+0xf5c, +0xf5d, +0xf7b) or
 * default it with (+0xf74..+0xf76, +0xf70), and the linear model's SetState word +0x60 (FLIGHT's flags 2), every ID as a
 * portable ID. The joiner (boot phase 3) destroys its own entries with the engine's vector clear 0xf169e0 (each entry's
 * destructor releases its reference's waypoint registration) and copy-constructs the session's with the engine's push_back
 * 0xe89ed0 (its growth allocates; the copy registers), so the vectors' memory and the registrations are the engine's own; then
 * it reads them back. AGENT's flags (+0xf33 among them) are written from version 7 on. */
#define JOIN_ROUTE_POINT_VT 0x2c4f578u       /* U::Path::FlightPoint */
#define JOIN_ROUTE_NAV_VT 0x2ae9380u         /* U::NavPoint, a FlightPoint's sub-point */
#define JOIN_ROUTE_SUBPOINT 0x68u
#define JOIN_ROUTE_SUBSIZE 0x60u
#define JOIN_ROUTE_CLEAR 0xf169e0u           /* vector<FlightPoint>::clear (each entry's vtable slot 0 with 0) */
#define JOIN_ROUTE_PUSH 0xe89ed0u            /* vector<FlightPoint>::push_back (copy constructor 0xeaa6f0; growth 0xf23060) */
#define JOIN_ROUTE_F70 0xf70u
static const uint16_t join_route_ids_at[4]={0xef8,0xf08,0xf48,0xf60};
static const uint16_t join_route_bytes_at[6]={0xf5c,0xf5d,0xf74,0xf75,0xf76,0xf7b};
/* A point's carried bytes from an entry (and its sub-point, NULL without): +0x20..+0x67, +0x70..+0x8f with +0x7f as 0, the
 * sub-point's +0x20..+0x5f. */
static void join_route_pack(const unsigned char *entry,const unsigned char *sub,unsigned char data[JOIN_ROUTE_DATA]) {
    memset(data,0,JOIN_ROUTE_DATA);memcpy(data,entry+0x20,0x48);memcpy(data+0x48,entry+0x70,0x20);data[0x48+0xf]=0;
    if(sub) memcpy(data+0x68,sub+0x20,0x40);
}
static uint16_t join_route_count(const join_route_rec *h,unsigned list) {uint16_t n;memcpy(&n,h->data+2*list,2);return n;}
/* ROUTE one head per AGENT record, in its order (the lengths AGENT names), each followed by exactly its points (none for a head
 * flagged other); every unused field 0, a reference whose runtime ID had no portable ID 0. */
static int join_route_shape(const join_agent_rec *a,uint32_t na,const join_route_rec *r,uint32_t nr) {
    static const unsigned char zero[JOIN_ROUTE_DATA];uint32_t j=0;
    for(uint32_t i=0;i<na;i++) {
        if(j>=nr) return 0;
        const join_route_rec *h=&r[j++];const uint16_t n[2]={join_route_count(h,0),join_route_count(h,1)};
        if(h->portable!=a[i].portable || h->kind!=JOIN_ROUTE_HEAD || h->list || h->index || h->reserved ||
           (h->flags&~(JOIN_ROUTE_UNMAPPED|JOIN_ROUTE_OTHER|JOIN_ROUTE_LINEAR)) || (!(h->flags&JOIN_ROUTE_LINEAR) && (h->ref[0] || (h->flags&1u))) ||
           n[0]!=a[i].waypoints || n[1]!=a[i].pending || h->data[10] || h->data[11] || memcmp(h->data+16,zero,JOIN_ROUTE_DATA-16)) return 0;
        for(unsigned s=0;s<JOIN_ROUTE_REFS;s++) if(h->ref[s] && (h->flags>>s&1u)) return 0;
        if(h->flags&JOIN_ROUTE_OTHER) continue;
        if(n[0]>JOIN_ROUTE_POINTS || n[1]>JOIN_ROUTE_POINTS) return 0;
        for(unsigned list=0;list<2;list++) for(uint32_t k=0;k<n[list];k++) {
            if(j>=nr) return 0;
            const join_route_rec *p=&r[j++];
            if(p->portable!=h->portable || p->kind!=JOIN_ROUTE_POINT || p->list!=list || p->index!=k || p->reserved ||
               (p->flags&~(3u|JOIN_ROUTE_SUB|JOIN_ROUTE_CONN|JOIN_ROUTE_SUB_CONN)) || p->ref[2] || p->ref[3] || p->ref[4] || p->data[0x48+0xf] ||
               (p->ref[0] && (p->flags&1u)) || (p->ref[1] && (p->flags&2u))) return 0;
            if(!(p->flags&JOIN_ROUTE_SUB) && (p->ref[1] || (p->flags&(2u|JOIN_ROUTE_SUB_CONN)) || memcmp(p->data+0x68,zero,0x40))) return 0;
        }
    }
    return j==nr;
}
static int join_route_cmp(const void *a,const void *b) {
    const join_route_rec *x=a,*y=b;
    if(x->portable!=y->portable) return x->portable<y->portable?-1:1;
    if(x->kind!=y->kind) return x->kind<y->kind?-1:1;
    if(x->list!=y->list) return x->list<y->list?-1:1;
    return x->index<y->index?-1:x->index>y->index;
}
/* The saver: a runtime ID as a portable ID; flags bit slot when it has none. */
static uint64_t join_route_portable(uint64_t runtime,uint32_t *flags,unsigned slot) {
    unsigned klass;uint64_t portable=runtime?join_key(runtime,&klass):0;
    if(runtime && !portable) *flags|=1u<<slot;
    return portable;
}
/* One entry of a vector (its 0x90 bytes) and its sub-point (0x60 bytes, *sub 0 without): 0 unreadable, -1 not the FlightPoint /
 * NavPoint the record carries. */
static int join_route_entry(uintptr_t at,unsigned char entry[JOIN_AGENT_WAYPOINT],unsigned char sub[JOIN_ROUTE_SUBSIZE],uintptr_t *subp) {
    uintptr_t vtable=0;*subp=0;
    if(!join_read((void *)at,entry,JOIN_AGENT_WAYPOINT)) return 0;
    memcpy(&vtable,entry,8);memcpy(subp,entry+JOIN_ROUTE_SUBPOINT,8);
    if(vtable!=(uintptr_t)image+JOIN_ROUTE_POINT_VT) return -1;
    if(*subp) {
        if(!join_read((void *)*subp,sub,JOIN_ROUTE_SUBSIZE)) return 0;
        memcpy(&vtable,sub,8);if(vtable!=(uintptr_t)image+JOIN_ROUTE_NAV_VT) return -1;
    }
    return 1;
}
/* The saver: an Agent's ROUTE head and points (linear: its model is linear, word the model's +0x60). 0 with join_fault. */
static join_route_rec join_route_points[2*JOIN_ROUTE_POINTS];
static int join_capture_route(join_tables *t,const unsigned char *agent,uint64_t portable,int linear,uint64_t word,uint32_t *cap) {
    join_route_rec h;memset(&h,0,sizeof(h));h.portable=portable;h.kind=JOIN_ROUTE_HEAD;
    uintptr_t v[2][3];uint64_t n[2],room=0;uint32_t np=0;
    if(!join_flight_vector(agent+JOIN_AGENT_WAYPOINTS,JOIN_AGENT_WAYPOINT,v[0],&n[0],&room) ||
       !join_flight_vector(agent+JOIN_AGENT_PENDING,JOIN_AGENT_WAYPOINT,v[1],&n[1],&room) || n[0]>0xffffu || n[1]>0xffffu)
        return join_note("route,id=%llx,check=vectors",(unsigned long long)portable);
    if(linear) {h.flags|=JOIN_ROUTE_LINEAR;h.ref[0]=join_route_portable(word,&h.flags,0);}
    for(unsigned i=0;i<4;i++) {
        uint64_t id=0;
        if(!join_read(agent+join_route_ids_at[i],&id,8)) return join_note("route,id=%llx,check=ids",(unsigned long long)portable);
        h.ref[1+i]=join_route_portable(id,&h.flags,1+i);
    }
    const uint16_t lengths[2]={(uint16_t)n[0],(uint16_t)n[1]};memcpy(h.data,lengths,4);
    for(unsigned i=0;i<6;i++) if(!join_read(agent+join_route_bytes_at[i],h.data+4+i,1)) return join_note("route,id=%llx,check=bytes",(unsigned long long)portable);
    if(!join_read(agent+JOIN_ROUTE_F70,h.data+12,4)) return join_note("route,id=%llx,check=bytes",(unsigned long long)portable);
    if(n[0]>JOIN_ROUTE_POINTS || n[1]>JOIN_ROUTE_POINTS) h.flags|=JOIN_ROUTE_OTHER;
    for(unsigned list=0;list<2 && !(h.flags&JOIN_ROUTE_OTHER);list++) for(uint64_t k=0;k<n[list];k++) {
        unsigned char entry[JOIN_AGENT_WAYPOINT],sub[JOIN_ROUTE_SUBSIZE];uintptr_t subp=0;uint64_t id=0,connection=0;
        int got=join_route_entry(v[list][0]+(uintptr_t)k*JOIN_AGENT_WAYPOINT,entry,sub,&subp);
        if(!got) return join_note("route,id=%llx,check=entry,list=%u,index=%llu",(unsigned long long)portable,list,(unsigned long long)k);
        if(got<0) {h.flags|=JOIN_ROUTE_OTHER;break;}
        join_route_rec *p=&join_route_points[np++];memset(p,0,sizeof(*p));
        p->portable=portable;p->kind=JOIN_ROUTE_POINT;p->list=(uint16_t)list;p->index=(uint16_t)k;
        memcpy(&id,entry+0x10,8);p->ref[0]=join_route_portable(id,&p->flags,0);
        memcpy(&connection,entry+0x18,8);if(connection) p->flags|=JOIN_ROUTE_CONN;
        if(subp) {
            p->flags|=JOIN_ROUTE_SUB;memcpy(&id,sub+0x10,8);p->ref[1]=join_route_portable(id,&p->flags,1);
            memcpy(&connection,sub+0x18,8);if(connection) p->flags|=JOIN_ROUTE_SUB_CONN;
        }
        join_route_pack(entry,subp?sub:NULL,p->data);
    }
    if(h.flags&JOIN_ROUTE_OTHER) np=0;
    if(!JOIN_PUSH(t->route,t->nroute,*cap,h)) return join_note("memory");
    for(uint32_t k=0;k<np;k++) if(!JOIN_PUSH(t->route,t->nroute,*cap,join_route_points[k])) return join_note("memory");
    return 1;
}
/* ---- version 8: PARAM and DOCK (J1 j1-22's census at T+1; report 1.12) ----
 * With ROUTE the probe of T+1 was equal on the savers and the joiner (t1=9997:9997:2040:37:145:55:0) and the census at T+1
 * differed in 359 objects: the motion of 294 of the 2,040 linear models the pass stepped in full, of 48 ships docking, of the
 * 13 ships with a steering or spline model; 4 health (a hit). Of the stepped linear models whose heading error +0x98 at T was
 * 1e-4 rad or more 279 of 289 differed, of the others 15 of 1,751 [F: census at 634, FLIGHT at T]: the turn. The linear step
 * 0xeb27c0 turns yaw, pitch and roll toward its basis by rate x dt (0xeb1db0 with XMM2 = Agent +0x378, +0x37c, +0x380, XMM3 =
 * the step's dt), and the Agent update 0xe8f250 clamps the angular velocity by ten times the same rates [F: the step's
 * disassembly; the decompiler drops those register arguments, so 1.10 named only +0x518..+0x520]. AGENT does not carry them.
 * They sit in the Agent's own flight parameter slot +0x340 (the third of three 0x1a0-byte slots: +0x000 and +0x1a0 by the
 * pathing selectors), filled from the ship and its engines (0x6dc610 on the object 0x153730 builds over a cleared 0x140-byte
 * buffer; the refresh 0xea1e80 adds +0x160..+0x19b: a vector, a runtime ID, the refresh time +0x178, the mode bytes +0x190..);
 * the update's tail 0xe8f020 copies the read slot into the own copy (0xe84970: +0x000..+0x19b) when the read slot's refresh
 * time is newer. PARAM carries all three slots by role, so the step, the clamp and that copy see the session's values.
 * DOCK: a ship docking or undocking is flown by a DockMovementController (0x2c3fdc0) or UndockMovementController (0x2c3e568)
 * whose transform the evaluators 0xd6e810 / 0xd6e150 compute from three poses, the initial linear and angular velocity, the
 * start time, the animation and two options (lockstep_dock.h). The save writes them rounded (<time start="13.95"/>, velocity
 * with six digits, rotations as degrees): j633's 48 differing dock controllers all had a start time or a vector the text
 * cannot hold, the 39 equal ones start="-18" / "-9" and exact offsets [F: j633.xml.gz joined with the census at 634]. */
#define JOIN_PARAM_STRIDE 0x1a0u             /* the read and write slots at Agent + 0x1a0 x selector */
#define JOIN_PARAM_OWN 0x340u                /* the Agent's own copy, the third slot */
#define JOIN_PARAM_SPAN 0x19cu               /* what the engine's slot copy 0xe84970 copies */
#define JOIN_PARAM_OBJECT 0x140u             /* the flight parameter object +0x000..+0x13f */
#define JOIN_PARAM_VECTOR 0x160u
#define JOIN_PARAM_ID 0x170u
#define JOIN_PARAM_TIME 0x178u
#define JOIN_PARAM_TAIL 0x180u
#define JOIN_PARAM_TAIL_LEN 0x1cu            /* +0x180..+0x19b */
#define JOIN_PARAM_TURN 0x38u                /* the turn rates +0x38..+0x43 (the own copy's +0x378..+0x380) */
#define JOIN_PARAM_READ_INDEX 0x38f4ee4u     /* g_PathingStateBufferIndex */
#define JOIN_PARAM_WRITE_INDEX 0x2f4b6f4u    /* g_PathingStateBufferIndex2 */
#define JOIN_PARAMB_VECTOR 0x140u            /* a record's slot block: object, vector, portable ID, time, tail, four 0 */
#define JOIN_PARAMB_ID 0x150u
#define JOIN_PARAMB_TIME 0x158u
#define JOIN_PARAMB_TAIL 0x160u
#define JOIN_PARAMB_PAD 0x17cu
_Static_assert(JOIN_PARAMB_TAIL+JOIN_PARAM_TAIL_LEN==JOIN_PARAMB_PAD && JOIN_PARAMB_PAD+4u==JOIN_PARAM_SLOT,"PARAM slot block");
static const char *const join_param_roles[JOIN_PARAM_ROLES]={"read","write","own"};
/* The pathing selectors (WaitAndSwapBuffers 0xeac170 flips them each frame with a step): 0 when not one each. */
static int join_param_selectors(unsigned *read,unsigned *write) {
    uint32_t r=2,w=2;
    if(!join_read(image+JOIN_PARAM_READ_INDEX,&r,4) || !join_read(image+JOIN_PARAM_WRITE_INDEX,&w,4) || r>1u || w>1u || r+w!=1u) return 0;
    *read=r;*write=w;return 1;
}
static unsigned join_param_offset(unsigned role,unsigned read,unsigned write) {
    return role==0?read*JOIN_PARAM_STRIDE:role==1?write*JOIN_PARAM_STRIDE:JOIN_PARAM_OWN;
}
/* Padding inside the flight parameter object: per process (J1 j1-23 saver_disagree, PARAM the only section apart, 4,430
 * records in all three slots alike: +0x5c..+0x5f, +0xe9..+0xeb after the flag byte +0xe8 and +0x13c..+0x13f after three
 * floats held stale heap bytes, string fragments among them). +0xf0..+0xff: a per-process cache (j1-24: one record of
 * 10,010 held a 4-byte value and a heap pointer on one saver, zeros on the other; the session's clients stay equal), not
 * portable. Carried as 0; the joiner keeps its own. */
static const struct {unsigned at,len;} join_param_padding[]={{0x5c,4},{0xe9,3},{0xf0,16},{0x13c,4}};
static void join_param_pad_copy(unsigned char *to,const unsigned char *from) {
    for(unsigned i=0;i<sizeof(join_param_padding)/sizeof(*join_param_padding);i++)
        if(from) memcpy(to+join_param_padding[i].at,from+join_param_padding[i].at,join_param_padding[i].len);
        else memset(to+join_param_padding[i].at,0,join_param_padding[i].len);
}
/* A slot's carried bytes as a record's block (id: +0x170 as the block holds it). */
static void join_param_pack(const unsigned char *slot,uint64_t id,unsigned char block[JOIN_PARAM_SLOT]) {
    memset(block,0,JOIN_PARAM_SLOT);memcpy(block,slot,JOIN_PARAM_OBJECT);join_param_pad_copy(block,NULL);
    memcpy(block+JOIN_PARAMB_VECTOR,slot+JOIN_PARAM_VECTOR,16);
    memcpy(block+JOIN_PARAMB_ID,&id,8);memcpy(block+JOIN_PARAMB_TIME,slot+JOIN_PARAM_TIME,8);
    memcpy(block+JOIN_PARAMB_TAIL,slot+JOIN_PARAM_TAIL,JOIN_PARAM_TAIL_LEN);
}
/* PARAM exactly one record per AGENT record, in its order; reserved, unused flags and each block's pad 0, an unmapped ID 0. */
static int join_param_shape(const join_agent_rec *a,uint32_t na,const join_param_rec *p,uint32_t np) {
    if(np!=na) return 0;
    for(uint32_t i=0;i<np;i++) {
        if(p[i].portable!=a[i].portable || p[i].reserved || (p[i].flags&~7u)) return 0;
        for(unsigned r=0;r<JOIN_PARAM_ROLES;r++) {
            uint64_t id;uint32_t pad;memcpy(&id,p[i].slot[r]+JOIN_PARAMB_ID,8);memcpy(&pad,p[i].slot[r]+JOIN_PARAMB_PAD,4);
            if(pad || (id && (p[i].flags>>r&1u))) return 0;
        }
    }
    return 1;
}
static int join_param_cmp(const void *a,const void *b) {
    uint64_t x=((const join_param_rec *)a)->portable,y=((const join_param_rec *)b)->portable;return x<y?-1:x>y;
}
/* The saver: an Agent's three slots by role (the runtime ID +0x170 as a portable ID). 0 with join_fault. */
static int join_capture_param(join_tables *t,const unsigned char *agent,uint64_t portable,uint32_t *cap) {
    join_param_rec r;unsigned read=0,write=0;unsigned char slot[JOIN_PARAM_SPAN];memset(&r,0,sizeof(r));r.portable=portable;
    if(!join_param_selectors(&read,&write)) return join_note("param,id=%llx,check=selectors",(unsigned long long)portable);
    for(unsigned role=0;role<JOIN_PARAM_ROLES;role++) {
        uint64_t id=0;
        if(!join_read(agent+join_param_offset(role,read,write),slot,sizeof(slot)))
            return join_note("param,id=%llx,role=%s",(unsigned long long)portable,join_param_roles[role]);
        memcpy(&id,slot+JOIN_PARAM_ID,8);join_param_pack(slot,join_route_portable(id,&r.flags,role),r.slot[role]);
    }
    return JOIN_PUSH(t->param,t->nparam,*cap,r)?1:join_note("memory");
}
static int join_dock_kind(uint32_t kind) {return kind==DOCK_CONTROLLER_VTABLE || kind==UNDOCK_CONTROLLER_VTABLE;}
/* DOCK records of a dock or undock controller, unused fields 0 (an undock controller's second option and vectors 8..13). */
static int join_dock_shape(const join_dock_rec *d,uint32_t n) {
    for(uint32_t i=0;i<n;i++) {
        if(!d[i].portable || !join_dock_kind(d[i].kind) || d[i].flags || d[i].reserved || d[i].option[0]>1u || d[i].option[1]>1u) return 0;
        if(d[i].kind==UNDOCK_CONTROLLER_VTABLE) {
            if(d[i].option[1]) return 0;
            for(unsigned k=8u*3u;k<JOIN_DOCK_VECTORS*3u;k++) if(d[i].xyz[k]) return 0;
        }
    }
    return 1;
}
/* A vector's place in a dock controller: 0..3 at +0x20, 4.. at +0x80 (a dock controller's 12 and 13 at +0x100). */
static unsigned join_dock_at(unsigned k) {return k<4u?0x20u+16u*k:0x80u+16u*(k-4u);}
/* A dock or undock controller's census fields (lockstep_dock.h dock_controller_state) into r (portable kept). 0: unreadable, not
 * the object's, an option byte past 1. */
static int join_dock_read(const unsigned char *controller,const unsigned char *object,uint32_t kind,join_dock_rec *r) {
    unsigned char *owner=NULL;const int undock=kind==UNDOCK_CONTROLLER_VTABLE;const unsigned vectors=undock?8u:JOIN_DOCK_VECTORS;
    uint64_t portable=r->portable;memset(r,0,sizeof(*r));r->portable=portable;r->kind=kind;
    if(!join_read(controller+0x70,&owner,8) || owner!=object || !join_read(controller+0x60,&r->time,8) ||
       !join_read(controller+(undock?0xc0u:0x120u),&r->animation,4) || !join_read(controller+(undock?0xc4u:0x124u),r->option,undock?1u:2u) ||
       r->option[0]>1u || r->option[1]>1u) return 0;
    for(unsigned k=0;k<vectors;k++) if(!join_read(controller+join_dock_at(k),r->xyz+3*k,12)) return 0;
    return 1;
}
static int join_dock_cmp(const void *a,const void *b) {
    uint64_t x=((const join_dock_rec *)a)->portable,y=((const join_dock_rec *)b)->portable;return x<y?-1:x>y;
}
/* ---- version 9: STEER and STEERPT (J1 j1-25's census at T+1; report 1.13) ----
 * With PARAM and DOCK the census at T+1 differed in 17 objects: the 13 ships of the pathing pass's vector 0 whose flight control
 * model is no linear one (10 U::Path::SteeringFlightControlModel 0x2c4e9c8, 3 U::Path::SplineFlightControlModel 0x2c4f480; the
 * fixture's dogfight in tempzone 0x13e75f), three of them with the timing fields bank0.arrived/notified, and the ship hit at T+1
 * by a projectile in flight at T with its three weapons [F: census at 634, FLIGHT at T]. FLIGHT holds those models' kind only.
 * The save writes a steering model as <flightcontrolmodel type="steering"><behavior type=".."/><defaultbehavior type=".."/>, so
 * the loader builds a new model (the constructor 0xec40c0: +0x88 and +0x1c0 -1, two draws of the thread's RNG into +0x33c and
 * +0x340, the radius +0x330 from the ship, +0x350 from its extents) and new behaviours through the factory 0xec54b0 (more draws
 * in some, e.g. EvasiveSimple's direction byte +0x68; each its own construction time) [F: decompiler, read-only]. The model
 * [F: the constructor, SetState 0xec5170, the step 0xec7200 and its disassembly]:
 *   +0x10 the Agent; +0x20..+0x5f the target transform; +0x60 the target's runtime ID, +0x68, +0x6c..+0x6f flags; +0x70 the
 *   active behaviour, +0x78 the default one (heap objects, vtable at +0); +0x80 a runtime ID (the target's ancestor of class
 *   0x49); +0x88 the avoidance cache's publication time and +0x90 its vector of 0x80-byte contacts (0xeaf660 copies them from the
 *   obstacle publisher's list for the ship when its time differs); +0xa8..+0xb3 state words; +0xb8..+0x117 four timer triples
 *   (the step copies +0xb8 to +0xc0, +0xd0 to +0xd8, +0xe8 to +0xf0, +0x100 to +0x108); +0x120..+0x1bf the current and the set
 *   target, +0x1c0 SetState's time; +0x1d0..+0x1ef scalars, +0x1f0..+0x24f the step's copy of the bank, +0x250..+0x270 the
 *   target's extents; +0x280..+0x2ff the step's frames; +0x300..+0x31f rewritten at each step's start (+0x60, +0x80, +0x330, a
 *   constant, +0x68, dt); +0x330..+0x343 the radius, its square, ten times it and the two draws; +0x350..+0x35f; +0x360 the path
 *   (a vector of 0x90-byte U::Path::FlightPoints, ROUTE's entries), +0x378 its job's next time (a draw), +0x380 a flag, +0x388 an
 *   async path job whose results become the path; +0x390, +0x394.
 *   Never written by the constructor (padding or stale heap: per process, J1 learned it three times): +0x18, +0xb4, +0x118, +0x1c8,
 *   +0x274..+0x27f, +0x300..+0x32f (+0x300..+0x31f the step rewrites before it reads them), +0x344..+0x34f, +0x381..+0x387, +0x398. The w lanes of the vectors are not carried (DOCK's,
 *   NAV's rule).
 * STEER carries the scalars and the vectors' x, y, z lanes (join_steer_ranges), the two IDs as portable IDs, the behaviours by
 * class (join_steer_classes: the fields each constructor writes; their vtable +0 and the model pointer +8 not) and the path's
 * FlightPoints (STEERPT, rebuilt with ROUTE's engine vector functions). Not carried, counted: the avoidance cache (a copy of the
 * obstacle publisher's list, whose own state is not in the package: the joiner's model keeps -1 and refreshes at its first
 * step), a pending path job (steer job), a behaviour class this build does not know (steer other). */
#define JOIN_STEER_VTABLE 0x2c4e9c8u          /* U::Path::SteeringFlightControlModel */
#define JOIN_STEER_IDS_AT0 0x60u              /* the target's runtime ID (SetState's third argument) */
#define JOIN_STEER_IDS_AT1 0x80u              /* the runtime ID of the target's ancestor of class 0x49 (SetState) */
#define JOIN_STEER_BEHAVIOURS 0x70u           /* the active behaviour +0x70, the default one +0x78 */
#define JOIN_STEER_CONTACTS 0x90u             /* the avoidance cache: {begin, end, capacity} of 0x80-byte contacts */
#define JOIN_STEER_CONTACT 0x80u
#define JOIN_STEER_PATH 0x360u                /* {begin, end, capacity} of 0x90-byte FlightPoints */
#define JOIN_STEER_JOB_AT 0x388u              /* the pending async path job (0 none) */
#define JOIN_STEER_LIST 2u                    /* STEERPT's points: ROUTE point records with list 2 */
typedef struct {uint16_t at,len;uint8_t vectors;} join_steer_range;   /* vectors: 16-byte groups, their x, y, z lanes carried */
static const join_steer_range join_steer_ranges[]={
    {0x20,0x40,1},{0x68,8,0},{0xa8,12,0},{0xb8,0x60,0},{0x120,0xa0,1},{0x1c0,8,0},{0x1d0,0x20,0},{0x1f0,0x80,1},{0x270,4,0},
    {0x280,0x80,1},{0x330,0x14,0},{0x350,0x10,1},{0x378,9,0},{0x390,8,0}};
#define JOIN_STEER_NRANGES ((unsigned)(sizeof(join_steer_ranges)/sizeof(*join_steer_ranges)))
typedef struct {uint32_t vtable;uint32_t n;join_steer_range r[5];} join_steer_class;
static const join_steer_class join_steer_classes[]={
    {0x2c4edc0u,2,{{0x10,2,0},{0x18,1,0}}},                                          /* GenericSteeringBehaviour (0x20 bytes) */
    {0x2c4dc30u,3,{{0x10,2,0},{0x20,0x40,1},{0x60,0x18,0}}},                         /* DogfightBehaviour1 (0x80, 0xed1560) */
    {0x2c4e840u,5,{{0x10,2,0},{0x20,0x40,1},{0x60,0x18,0},{0x80,0xc,0},{0x90,8,0}}}, /* ChaseBehaviour (0xa0: Dogfight1's, +0x80..) */
    {0x2c4dab0u,3,{{0x10,2,0},{0x18,5,0},{0x20,8,0}}},                               /* GainDistanceBehaviour (0x28, 0xed22e0) */
    {0x2c4d288u,3,{{0x10,2,0},{0x20,0x40,1},{0x60,10,0}}}};                          /* EvasiveSimpleSteeringBehaviour (0x70) */
#define JOIN_STEER_NCLASSES ((unsigned)(sizeof(join_steer_classes)/sizeof(*join_steer_classes)))
static unsigned join_steer_bytes(const join_steer_range *r,unsigned n) {
    unsigned b=0;for(unsigned i=0;i<n;i++) b+=r[i].vectors?r[i].len/16u*12u:r[i].len;return b;
}
static int join_steer_class_of(uint32_t vtable) {
    for(unsigned c=0;c<JOIN_STEER_NCLASSES;c++) if(join_steer_classes[c].vtable==vtable) return (int)c;
    return -1;
}
/* The carried bytes of `at` (a model or a behaviour) into out, in range order. 0 unreadable. */
static int join_steer_read(const unsigned char *at,const join_steer_range *r,unsigned n,unsigned char *out) {
    unsigned o=0;
    for(unsigned i=0;i<n;i++) {
        if(!r[i].vectors) {if(!join_read(at+r[i].at,out+o,r[i].len)) return 0;o+=r[i].len;continue;}
        for(unsigned v=0;v<r[i].len/16u;v++,o+=12) if(!join_read(at+r[i].at+16u*v,out+o,12)) return 0;
    }
    return 1;
}
/* The carried bytes back into `at` (the joiner's); 1 when one differed. */
static int join_poke(uintptr_t at,const void *want,size_t n);   /* the joiner's write (below) */
static int join_steer_write(unsigned char *at,const join_steer_range *r,unsigned n,const unsigned char *in) {
    unsigned o=0;int changed=0;
    for(unsigned i=0;i<n;i++) {
        if(!r[i].vectors) {changed|=join_poke((uintptr_t)at+r[i].at,in+o,r[i].len);o+=r[i].len;continue;}
        for(unsigned v=0;v<r[i].len/16u;v++,o+=12) changed|=join_poke((uintptr_t)at+r[i].at+16u*v,in+o,12);
    }
    return changed;
}
static int join_steer_zero(const unsigned char *p,size_t n) {for(size_t i=0;i<n;i++) if(p[i]) return 0;return 1;}
/* A STEERPT point record as ROUTE's points are (join_route_shape), list 2. */
static int join_steer_point_shape(const join_route_rec *p,uint64_t portable,uint32_t index) {
    static const unsigned char zero[0x40];
    if(p->portable!=portable || p->kind!=JOIN_ROUTE_POINT || p->list!=JOIN_STEER_LIST || p->index!=index || p->reserved ||
       (p->flags&~(3u|JOIN_ROUTE_SUB|JOIN_ROUTE_CONN|JOIN_ROUTE_SUB_CONN)) || p->ref[2] || p->ref[3] || p->ref[4] || p->data[0x48+0xf] ||
       (p->ref[0] && (p->flags&1u)) || (p->ref[1] && (p->flags&2u))) return 0;
    if(!(p->flags&JOIN_ROUTE_SUB) && (p->ref[1] || (p->flags&(2u|JOIN_ROUTE_SUB_CONN)) || memcmp(p->data+0x68,zero,0x40))) return 0;
    return 1;
}
/* STEER exactly one record per FLIGHT record of a steering model (kind only), in its order; unused bytes 0, a behaviour's state
 * only as its class carries it, an ID beside its unmapped bit refused; STEERPT exactly the points each names (none when flagged). */
static int join_steer_shape(const join_flight_rec *f,uint32_t nf,const join_steer_rec *s,uint32_t ns,const join_route_rec *p,uint32_t np) {
    const unsigned model=join_steer_bytes(join_steer_ranges,JOIN_STEER_NRANGES);uint32_t k=0,j=0;
    for(uint32_t i=0;i<nf;i++) {
        if(f[i].kind!=JOIN_STEER_VTABLE) continue;
        if(k>=ns) return 0;
        const join_steer_rec *r=&s[k++];
        if(r->portable!=f[i].portable || r->kind!=JOIN_STEER_VTABLE || r->reserved || (r->flags&~63u) ||
           !join_steer_zero(r->model+model,JOIN_STEER_MODEL-model)) return 0;
        for(unsigned slot=0;slot<2;slot++) if(r->ref[slot] && (r->flags>>slot&1u)) return 0;
        for(unsigned b=0;b<2;b++) {
            const int c=r->behaviour[b]?join_steer_class_of(r->behaviour[b]):-1,uncarried=(r->flags&(JOIN_STEER_UNCARRIED0<<b))!=0;
            if(!r->behaviour[b] && uncarried) return 0;
            if(r->behaviour[b] && (c<0)!=uncarried) return 0;
            const unsigned used=c<0?0u:join_steer_bytes(join_steer_classes[c].r,join_steer_classes[c].n);
            if(!join_steer_zero(r->state[b]+used,JOIN_STEER_STATE-used)) return 0;
        }
        if(r->flags&JOIN_STEER_POINTS) continue;
        if(r->points>JOIN_ROUTE_POINTS) return 0;
        for(uint32_t q=0;q<r->points;q++,j++) if(j>=np || !join_steer_point_shape(&p[j],r->portable,q)) return 0;
    }
    return k==ns && j==np;
}
static int join_steer_cmp(const void *a,const void *b) {
    uint64_t x=((const join_steer_rec *)a)->portable,y=((const join_steer_rec *)b)->portable;return x<y?-1:x>y;
}
/* The saver: a steering model's STEER record and its path's STEERPT points. 0 with join_fault. */
static join_route_rec join_steer_points[JOIN_ROUTE_POINTS];
static int join_capture_steer(join_tables *t,const unsigned char *agent,const unsigned char *model,uint64_t portable,uint32_t *scap,uint32_t *pcap) {
    join_steer_rec r;memset(&r,0,sizeof(r));r.portable=portable;r.kind=JOIN_STEER_VTABLE;
    unsigned char *owner=NULL,*behaviour=NULL;uint64_t id=0,n=0,room=0;uintptr_t v[3],job=0,vtable=0;uint32_t np=0;
    if(!join_read(model+JOIN_FLIGHT_OWNER,&owner,8) || owner!=agent || !join_steer_read(model,join_steer_ranges,JOIN_STEER_NRANGES,r.model))
        return join_note("steer,id=%llx,check=model",(unsigned long long)portable);
    if(!join_read(model+JOIN_STEER_IDS_AT0,&id,8)) return join_note("steer,id=%llx,check=ids",(unsigned long long)portable);
    r.ref[0]=join_route_portable(id,&r.flags,0);
    if(!join_read(model+JOIN_STEER_IDS_AT1,&id,8)) return join_note("steer,id=%llx,check=ids",(unsigned long long)portable);
    r.ref[1]=join_route_portable(id,&r.flags,1);
    if(!join_flight_vector(model+JOIN_STEER_CONTACTS,JOIN_STEER_CONTACT,v,&n,&room))
        return join_note("steer,id=%llx,check=contacts",(unsigned long long)portable);
    r.contacts=(uint16_t)(n>0xffffu?0xffffu:n);
    if(!join_read(model+JOIN_STEER_JOB_AT,&job,8)) return join_note("steer,id=%llx,check=job",(unsigned long long)portable);
    if(job) r.flags|=JOIN_STEER_JOB;
    for(unsigned b=0;b<2;b++) {
        if(!join_read(model+JOIN_STEER_BEHAVIOURS+8u*b,&behaviour,8)) return join_note("steer,id=%llx,check=behaviour",(unsigned long long)portable);
        if(!behaviour) continue;
        if(!join_read(behaviour,&vtable,8) || vtable<(uintptr_t)image || vtable>=(uintptr_t)image+JOIN_FLIGHT_IMAGE)
            return join_note("steer,id=%llx,check=behaviour_vtable,slot=%u",(unsigned long long)portable,b);
        r.behaviour[b]=(uint32_t)(vtable-(uintptr_t)image);
        const int c=join_steer_class_of(r.behaviour[b]);
        if(c<0) {r.flags|=JOIN_STEER_UNCARRIED0<<b;continue;}
        if(!join_steer_read(behaviour,join_steer_classes[c].r,join_steer_classes[c].n,r.state[b]))
            return join_note("steer,id=%llx,check=behaviour_state,slot=%u",(unsigned long long)portable,b);
    }
    if(!join_flight_vector(model+JOIN_STEER_PATH,JOIN_AGENT_WAYPOINT,v,&n,&room))
        return join_note("steer,id=%llx,check=path",(unsigned long long)portable);
    r.points=(uint16_t)(n>0xffffu?0xffffu:n);
    if(n>JOIN_ROUTE_POINTS) r.flags|=JOIN_STEER_POINTS;
    for(uint64_t k=0;k<n && !(r.flags&JOIN_STEER_POINTS);k++) {
        unsigned char entry[JOIN_AGENT_WAYPOINT],sub[JOIN_ROUTE_SUBSIZE];uintptr_t subp=0;uint64_t connection=0;
        int got=join_route_entry(v[0]+(uintptr_t)k*JOIN_AGENT_WAYPOINT,entry,sub,&subp);
        if(!got) return join_note("steer,id=%llx,check=point,index=%llu",(unsigned long long)portable,(unsigned long long)k);
        if(got<0) {r.flags|=JOIN_STEER_POINTS;break;}
        join_route_rec *p=&join_steer_points[np++];memset(p,0,sizeof(*p));
        p->portable=portable;p->kind=JOIN_ROUTE_POINT;p->list=JOIN_STEER_LIST;p->index=(uint16_t)k;
        memcpy(&id,entry+0x10,8);p->ref[0]=join_route_portable(id,&p->flags,0);
        memcpy(&connection,entry+0x18,8);if(connection) p->flags|=JOIN_ROUTE_CONN;
        if(subp) {
            p->flags|=JOIN_ROUTE_SUB;memcpy(&id,sub+0x10,8);p->ref[1]=join_route_portable(id,&p->flags,1);
            memcpy(&connection,sub+0x18,8);if(connection) p->flags|=JOIN_ROUTE_SUB_CONN;
        }
        join_route_pack(entry,subp?sub:NULL,p->data);
    }
    if(r.flags&JOIN_STEER_POINTS) np=0;
    if(!JOIN_PUSH(t->steer,t->nsteer,*scap,r)) return join_note("memory");
    for(uint32_t k=0;k<np;k++) if(!JOIN_PUSH(t->steerpt,t->nsteerpt,*pcap,join_steer_points[k])) return join_note("memory");
    return 1;
}
/* ---- the step of T+1 on both sides (JOIN_STATUS t1=listed:probed:stepped:newleg:pending:retarget:missing) ----
 * A saver lists every linear model it captured (FLIGHT) with its target at T, a joiner every one FLIGHT wrote; after the step of
 * T+1 (the MoveWorker joined) each is read again: stepped = its last step time +0x88 is the game time of T+1 (a full update),
 * newleg = and its leg started then (+0x80), pending = a SetState since its leg started (+0x6d), retarget = its target
 * +0x20..+0x5f is no longer T's. The two sides' counts tell whether the joiner's Agents re-targeted or restarted the legs FLIGHT
 * restored before stepping them (the session at T: 9,455 of 9,997 linear models without a SetState since their leg started). */
typedef struct {uint64_t runtime;unsigned char target[0x40];} join_t1_item;
static struct {
    join_t1_item *items;uint32_t n,cap,listed;int armed;uint64_t tick,probed,stepped,newleg,pending,retarget,missing;
} join_t1;
static void join_t1_reset(void) {free(join_t1.items);memset(&join_t1,0,sizeof(join_t1));}
static int join_t1_add(uint64_t runtime,const unsigned char *target) {
    join_t1_item item;item.runtime=runtime;memcpy(item.target,target,sizeof(item.target));
    return JOIN_PUSH(join_t1.items,join_t1.n,join_t1.cap,item);
}
static void join_t1_arm(uint64_t tick) {join_t1.tick=tick;join_t1.listed=join_t1.n;join_t1.armed=join_t1.n!=0;}
/* An object's navigation Agent (the nav controller's +0x40), NULL without one. */
static unsigned char *join_nav_agent(const unsigned char *object) {
    unsigned char *controller=NULL,*agent=NULL;uintptr_t nav=0;
    if(!join_read(object+JOIN_CONTROLLER,&controller,8) || !controller || !join_read(controller,&nav,8) || nav!=(uintptr_t)image+JOIN_NAV_VTABLE ||
       !join_read(controller+0x40,&agent,8)) return NULL;
    return agent;
}
static void join_t1_probe(uint64_t completed,double now) {
    if(!join_t1.armed || completed<=join_t1.tick) return;
    if(completed==join_t1.tick+1) {
        for(uint32_t i=0;i<join_t1.n;i++) {
            const join_t1_item *it=&join_t1.items[i];unsigned char *object=join_object(it->runtime),*agent=object?join_nav_agent(object):NULL,*model=NULL;
            uintptr_t vtable=0;double last=0,start=0;unsigned char pending=0,target[0x40];
            if(!agent || !join_read(agent+JOIN_AGENT_MODEL,&model,8) || !model || !join_read(model,&vtable,8) ||
               vtable!=(uintptr_t)image+JOIN_FLIGHT_LINEAR || !join_read(model+0x88,&last,8) || !join_read(model+0x80,&start,8) ||
               !join_read(model+0x6d,&pending,1) || !join_read(model+0x20,target,sizeof(target))) {++join_t1.missing;continue;}
            ++join_t1.probed;
            if(last==now) {++join_t1.stepped;if(start==now) ++join_t1.newleg;}
            if(pending) ++join_t1.pending;
            if(memcmp(target,it->target,sizeof(target))) ++join_t1.retarget;
        }
    }
    free(join_t1.items);join_t1.items=NULL;join_t1.n=join_t1.cap=0;join_t1.armed=0;   /* once (a later tick: nothing probed) */
}
/* The nodes of a ring list from its sentinel (+8 next, +0 prev, every link checked both ways), count of them. 0 with join_fault. */
static int join_path_ring(uintptr_t sentinel,uint64_t count,uintptr_t **out,const char *what) {
    uintptr_t *nodes=NULL,prev=sentinel,node=0,back=0;*out=NULL;
    if(!sentinel || count>JOIN_PATH_LIST_MAX) return join_note("path,check=%s,sentinel=%llx,count=%llu",what,JOIN_P(sentinel),(unsigned long long)count);
    if(count && !(nodes=malloc((size_t)count*sizeof(*nodes)))) return join_note("memory");
    if(!join_read((void *)(sentinel+8),&node,8)) {free(nodes);return join_note("path,check=%s_head",what);}
    for(uint64_t i=0;i<count;i++) {
        uintptr_t next=0;
        if(!node || node==sentinel || !join_read((void *)node,&back,8) || back!=prev || !join_read((void *)(node+8),&next,8))
            {free(nodes);return join_note("path,check=%s_link,index=%llu,node=%llx",what,(unsigned long long)i,JOIN_P(node));}
        nodes[i]=node;prev=node;node=next;
    }
    if(node!=sentinel || !join_read((void *)sentinel,&back,8) || back!=prev)
        {free(nodes);return join_note("path,check=%s_close,count=%llu,end=%llx",what,(unsigned long long)count,JOIN_P(node));}
    *out=nodes;return 1;
}
/* A list node's Agent's owner by its portable ID (0: none). */
static uint64_t join_path_owner(uintptr_t node) {
    uintptr_t agent=0,owner=0;uint64_t runtime=0;unsigned klass=WORLD_KEY_NONE;
    if(!join_read((void *)(node+0x10),&agent,8) || !agent || !join_read((void *)(agent+PATH_DET_AGENT_OWNER),&owner,8) || !owner ||
       !join_read((void *)(owner+8),&runtime,8) || !runtime) return 0;
    uint64_t key=join_key(runtime,&klass);return klass==WORLD_KEY_NONE?0:key;
}
static unsigned char *join_path_manager(void) {unsigned char *m=NULL;return join_read(image+JOIN_PATH_MANAGER,&m,8)?m:NULL;}
/* A batch vector's bounds: 0 unless a whole number of batches within the bound. */
static int join_path_vector(const unsigned char *manager,unsigned v,uintptr_t *begin,uint32_t *count) {
    uintptr_t bounds[2]={0,0};
    if(!join_read(manager+JOIN_PATH_VECTORS+0x18u*v,bounds,16) || bounds[1]<bounds[0] || (bounds[1]-bounds[0])%JOIN_PATH_BATCH ||
       (bounds[1]-bounds[0])/JOIN_PATH_BATCH>JOIN_PATH_BATCHES_MAX) return 0;
    *begin=bounds[0];*count=(uint32_t)((bounds[1]-bounds[0])/JOIN_PATH_BATCH);return 1;
}
typedef struct {unsigned char flag;uint64_t count;uintptr_t sentinel,cursor;} join_path_batch;
static int join_path_batch_read(uintptr_t at,join_path_batch *b) {
    return join_read((void *)at,&b->flag,1) && join_read((void *)(at+8),&b->count,8) && join_read((void *)(at+0x10),&b->sentinel,8) &&
           join_read((void *)(at+0x18),&b->cursor,8);
}
/* The saver: PATH. 0 with join_fault. */
static int join_capture_path(join_tables *t) {
    unsigned char *manager=join_path_manager();uint32_t cap=0,nb[2];uintptr_t begin[2];uint64_t next[2];
    if(!manager) return join_note("path,check=manager");
    if(!join_read(manager+JOIN_PATH_NEXT,next,16)) return join_note("path,check=next");
    for(unsigned v=0;v<2;v++) {
        if(!join_path_vector(manager,v,&begin[v],&nb[v]) || (nb[v]?next[v]>=nb[v]:next[v]!=0))
            return join_note("path,check=vector,vector=%u,next=%llu",v,(unsigned long long)next[v]);
        join_path_rec r={0,JOIN_PATH_KIND_MANAGER,v,nb[v],0,(uint32_t)next[v],0};if(!JOIN_PUSH(t->path,t->npath,cap,r)) return join_note("memory");
    }
    for(int members=0;members<2;members++) for(unsigned v=0;v<2;v++) for(uint32_t b=0;b<nb[v];b++) {
        join_path_batch batch;uintptr_t *nodes=NULL;uint32_t cursor=0;
        if(!join_path_batch_read(begin[v]+(uintptr_t)b*JOIN_PATH_BATCH,&batch)) return join_note("path,check=batch,vector=%u,batch=%u",v,b);
        if(!join_path_ring(batch.sentinel,batch.count,&nodes,"batch")) {join_note_more(",vector=%u,batch=%u",v,b);return 0;}
        while(cursor<batch.count && nodes[cursor]!=batch.cursor) ++cursor;
        if(batch.count?cursor==batch.count:batch.cursor!=batch.sentinel)
            {free(nodes);return join_note("path,check=cursor,vector=%u,batch=%u,cursor=%llx",v,b,JOIN_P(batch.cursor));}
        int ok=1;
        if(!members) {join_path_rec r={0,JOIN_PATH_KIND_BATCH,v,b,cursor,(uint32_t)batch.count,batch.flag};ok=JOIN_PUSH(t->path,t->npath,cap,r);}
        else for(uint32_t i=0;i<batch.count && ok;i++) {
            uint64_t key=join_path_owner(nodes[i]);join_path_rec r={key,JOIN_PATH_KIND_MEMBER,v,b,i,0,key?0u:JOIN_PATH_LOCAL};
            ok=JOIN_PUSH(t->path,t->npath,cap,r);
        }
        free(nodes);if(!ok) return join_note("memory");
    }
    uint64_t count=0;uintptr_t sentinel=0,*nodes=NULL;
    if(!join_read(manager+JOIN_PATH_PENDING,&count,8) || !join_read(manager+JOIN_PATH_PENDING+8,&sentinel,8)) return join_note("path,check=pending");
    if(!join_path_ring(sentinel,count,&nodes,"pending")) return 0;
    for(uint32_t i=0;i<count;i++) {
        uint64_t key=join_path_owner(nodes[i]);join_path_rec r={key,JOIN_PATH_KIND_PENDING,0,0,i,0,key?0u:JOIN_PATH_LOCAL};
        if(!JOIN_PUSH(t->path,t->npath,cap,r)) {free(nodes);return join_note("memory");}
    }
    free(nodes);
    if(!join_path_shape(t->path,t->npath)) return join_note("path,check=shape");
    return 1;
}
static int join_region_cmp(const void *a,const void *b) {
    const join_region_rec *x=a,*y=b;
    if(x->region!=y->region) return x->region<y->region?-1:1;
    return x->field<y->field?-1:x->field>y->field;
}
/* A region's field vector (+0x288); 0 unless readable and bounded. */
static int join_region_fields(const unsigned char *region,uintptr_t *begin,uint32_t *count) {
    uintptr_t bounds[2]={0,0};
    if(!join_read(region+REGION_RNG_FIELDS,bounds,16) || bounds[1]<bounds[0] || (bounds[1]-bounds[0])%8 || (bounds[1]-bounds[0])/8>REGION_RNG_FIELD_MAX ||
       (bounds[1]>bounds[0] && !bounds[0])) return 0;
    *begin=bounds[0];*count=(uint32_t)((bounds[1]-bounds[0])/8);return 1;
}
/* The field at index i when it is a DamageField; NULL otherwise. */
static unsigned char *join_region_damage(uintptr_t begin,uint32_t i) {
    unsigned char *field=NULL;uintptr_t vtable=0;
    return join_read((void *)(begin+8u*(uintptr_t)i),&field,8) && field && join_read(field,&vtable,8) &&
           vtable==(uintptr_t)(image+REGION_RNG_FIELD_VTABLE)?field:NULL;
}
/* The saver: a region's damage fields into REGION. 0 with join_fault. */
static int join_capture_region(join_tables *t,const unsigned char *region,uint64_t portable,uint32_t *cap) {
    uintptr_t begin=0;uint32_t n=0;
    if(!join_region_fields(region,&begin,&n)) return join_note("region,id=%llx,check=fields",(unsigned long long)portable);
    for(uint32_t i=0;i<n;i++) {
        unsigned char *field=join_region_damage(begin,i);join_region_rec g={portable,0,0,i,0};
        if(!field) continue;
        if(!join_read(field+REGION_RNG_LAST,&g.last,8) || !join_read(field+REGION_RNG_DUE,&g.due,8))
            return join_note("region,id=%llx,field=%u,check=times",(unsigned long long)portable,i);
        if(!JOIN_PUSH(t->region,t->nregion,*cap,g)) return join_note("memory");
    }
    return 1;
}
/* The saver, after the tables and identities (t->ids): HEALTH, POSE, SHIELD, NAV and ATTN of every present object, REGION of
 * every hazard region, then KEYS and PATH. now: the game time of T. 0 with join_fault naming the object and the check. */
static unsigned char join_flight_segments[JOIN_FLIGHT_SEGMENTS*JOIN_FLIGHT_SEGMENT];
static unsigned char join_flight_pairs[JOIN_FLIGHT_PAIRS*8u];
/* The saver: a navigation Agent's flight control model into FLIGHT (and a linear model's spline into SPLINE). A model the
 * record cannot hold whole is flagged (JOIN_FLIGHT_OTHER, _UNCARRIED, _WORD), never refused; a layout that is not the pinned
 * one is (0 with join_fault). */
static int join_capture_flight(join_tables *t,const unsigned char *controller,uint64_t runtime,uint64_t portable,uint32_t *fcap,uint32_t *scap,
                               uint32_t *acap,uint32_t *rcap,uint32_t *pcap,uint32_t *stcap,uint32_t *stpcap) {
    unsigned char *agent=NULL,*model=NULL,*owner=NULL;uintptr_t vtable=0,spline=0,seg[3],pair[3];uint64_t word=0,nseg=0,npair=0,room=0;
    if(!join_read(controller+0x40,&agent,8) || !agent || !join_read(agent+JOIN_AGENT_MODEL,&model,8))
        return join_note("flight,id=%llx,check=agent",(unsigned long long)portable);
    if(!model) return 1;
    if(!join_read(model,&vtable,8) || vtable<(uintptr_t)image || vtable>=(uintptr_t)image+JOIN_FLIGHT_IMAGE)
        return join_note("flight,id=%llx,check=vtable",(unsigned long long)portable);
    {   /* version 6: the Agent's step inputs, one record per FLIGHT record */
        join_agent_rec a;memset(&a,0,sizeof(a));a.portable=portable;
        if(!join_agent_read(agent,&a)) return join_note("agent,id=%llx",(unsigned long long)portable);
        if(!JOIN_PUSH(t->agent,t->nagent,*acap,a)) return join_note("memory");
        if(!join_capture_param(t,agent,portable,pcap)) return 0;   /* version 8: its flight parameter slots, one record per AGENT record */
    }
    {   /* version 7: its waypoint entries, the IDs around them and the linear model's SetState word */
        const int linear=vtable==(uintptr_t)image+JOIN_FLIGHT_LINEAR;uint64_t w=0;
        if(linear && !join_read(model+JOIN_FLIGHT_WORD_AT,&w,8)) return join_note("flight,id=%llx,check=word",(unsigned long long)portable);
        if(!join_capture_route(t,agent,portable,linear,w,rcap)) return 0;
    }
    join_flight_rec r;memset(&r,0,sizeof(r));r.portable=portable;r.kind=(uint32_t)(vtable-(uintptr_t)image);
    if(r.kind!=JOIN_FLIGHT_LINEAR) {
        r.flags=JOIN_FLIGHT_OTHER;
        if(r.kind==JOIN_STEER_VTABLE && !join_capture_steer(t,agent,model,portable,stcap,stpcap)) return 0;   /* version 9 */
        return JOIN_PUSH(t->flight,t->nflight,*fcap,r)?1:join_note("memory");
    }
    if(!join_read(model+JOIN_FLIGHT_OWNER,&owner,8) || owner!=agent || !join_flight_state_read(model,r.state) ||
       !join_read(model+JOIN_FLIGHT_WORD_AT,&word,8) || !join_read(model+JOIN_FLIGHT_SPLINE,&spline,8) ||
       spline!=(uintptr_t)image+JOIN_FLIGHT_SPLINE_VT || !join_flight_vector(model+JOIN_FLIGHT_SEGMENTS_AT,JOIN_FLIGHT_SEGMENT,seg,&nseg,&room) ||
       !join_flight_vector(model+JOIN_FLIGHT_PAIRS_AT,8u,pair,&npair,&room))
        return join_note("flight,id=%llx,check=linear",(unsigned long long)portable);
    if(word) r.flags|=JOIN_FLIGHT_WORD;
    if(nseg>JOIN_FLIGHT_SEGMENTS || npair>JOIN_FLIGHT_PAIRS) r.flags|=JOIN_FLIGHT_UNCARRIED;
    if(!(r.flags&JOIN_FLIGHT_UNCARRIED)) {
        if((nseg && !join_read((void *)seg[0],join_flight_segments,(size_t)nseg*JOIN_FLIGHT_SEGMENT)) ||
           (npair && !join_read((void *)pair[0],join_flight_pairs,(size_t)npair*8u)))
            return join_note("flight,id=%llx,check=spline",(unsigned long long)portable);
        for(uint64_t k=0;k<nseg;k++) {
            uintptr_t bezier;memcpy(&bezier,join_flight_segments+k*JOIN_FLIGHT_SEGMENT+JOIN_FLIGHT_BEZIER_AT,8);
            if(bezier!=(uintptr_t)image+JOIN_FLIGHT_BEZIER_VT) r.flags|=JOIN_FLIGHT_UNCARRIED;
        }
    }
    if(r.flags&JOIN_FLIGHT_UNCARRIED) nseg=npair=0;
    r.segments=(uint32_t)nseg;r.pairs=(uint32_t)npair;
    if(!JOIN_PUSH(t->flight,t->nflight,*fcap,r)) return join_note("memory");
    (void)join_t1_add(runtime,r.state);   /* the T+1 probe's list (diagnostic: without memory fewer are listed, t1's first count) */
    for(uint32_t k=0;k<r.segments;k++) {
        join_spline_rec g;memset(&g,0,sizeof(g));g.portable=portable;g.part=0;g.index=k;
        memcpy(g.data,join_flight_segments+(size_t)k*JOIN_FLIGHT_SEGMENT,JOIN_FLIGHT_SEGMENT);memset(g.data+JOIN_FLIGHT_BEZIER_AT,0,8);
        if(!JOIN_PUSH(t->spline,t->nspline,*scap,g)) return join_note("memory");
    }
    for(uint32_t k=0;k*JOIN_FLIGHT_PAIRS_PER<r.pairs;k++) {
        join_spline_rec g;memset(&g,0,sizeof(g));g.portable=portable;g.part=1;g.index=k;
        uint32_t used=r.pairs-k*JOIN_FLIGHT_PAIRS_PER;if(used>JOIN_FLIGHT_PAIRS_PER) used=JOIN_FLIGHT_PAIRS_PER;
        memcpy(g.data,join_flight_pairs+(size_t)k*JOIN_FLIGHT_PAIRS_PER*8u,used*8u);
        if(!JOIN_PUSH(t->spline,t->nspline,*scap,g)) return join_note("memory");
    }
    return 1;
}
static int join_flight_cmp(const void *a,const void *b) {
    uint64_t x=((const join_flight_rec *)a)->portable,y=((const join_flight_rec *)b)->portable;return x<y?-1:x>y;
}
static int join_spline_cmp(const void *a,const void *b) {
    const join_spline_rec *x=a,*y=b;
    if(x->portable!=y->portable) return x->portable<y->portable?-1:1;
    if(x->part!=y->part) return x->part<y->part?-1:1;
    return x->index<y->index?-1:x->index>y->index;
}
static int join_capture_world(join_tables *t,double now) {
    uint32_t hcap=0,pcap=0,scap=0,ncap=0,acap=0,gcap=0,fcap=0,lcap=0,agcap=0,rcap=0,pacap=0,dcap=0,stcap=0,stpcap=0;join_t1_reset();
    for(uint32_t i=0;i<t->nids;i++) {
        const join_id_rec *id=&t->ids[i];unsigned char *object=join_object(id->runtime);uintptr_t vtable=0;
        if(!object) continue;
        if(!join_read(object,&vtable,8)) return join_note("object,id=%llx",(unsigned long long)id->portable);
        if(vtable==(uintptr_t)(image+JOIN_REGION_VTABLE) && !join_capture_region(t,object,id->portable,&gcap)) return 0;
        unsigned classes=world_class_flags(vtable);
        if(classes&0x80000000u) continue;
        {   /* ATTN: every present object of a known class (the census timing domain reads +0x1ec of every one) */
            join_attn_rec a;memset(&a,0,sizeof(a));a.portable=id->portable;
            if(!join_read(object+JOIN_ATTENTION,&a.level,4) || ((classes&4u) && !join_read(object+JOIN_ATTENTION_DUE,&a.due,8)))
                return join_note("attention,id=%llx",(unsigned long long)id->portable);
            if(classes&4u) a.flags|=JOIN_ATTN_DUE;
            if(!JOIN_PUSH(t->attn,t->nattn,acap,a)) return join_note("memory");
        }
        if(classes&1u) {
            join_health_rec h;memset(&h,0,sizeof(h));h.portable=id->portable;unsigned char *group;int full;
            if(!join_read(object+JOIN_HULL,&h.hull,8)) return join_note("hull,id=%llx",(unsigned long long)id->portable);
            if(!join_shield_read(object,&group,&h.value,&h.changed,&h.maximum,&h.count,&now,&full))
                return join_note("shield,id=%llx",(unsigned long long)id->portable);
            h.flags=(group?JOIN_HEALTH_SHIELD:0u)|(full?JOIN_HEALTH_FULL:0u);
            if(!JOIN_PUSH(t->health,t->nhealth,hcap,h)) return join_note("memory");
            if(group) {
                join_shield_rec r;memset(&r,0,sizeof(r));r.portable=id->portable;
                if(!join_shield_params(group,&r)) return join_note("shield_params,id=%llx",(unsigned long long)id->portable);
                if(!JOIN_PUSH(t->shield,t->nshield,scap,r)) return join_note("memory");
            }
        }
        if(classes&2u) {
            unsigned char *controller=NULL;
            if(!join_read(object+JOIN_CONTROLLER,&controller,8)) return join_note("controller,id=%llx",(unsigned long long)id->portable);
            if(!controller) continue;
            join_pose_rec p;memset(&p,0,sizeof(p));p.portable=id->portable;
            int form=join_pose_form(controller,&p.kind);
            if(form<0) return join_note("controller_vtable,id=%llx",(unsigned long long)id->portable);
            if(p.kind==JOIN_NAV_VTABLE) {
                join_nav_rec r;if(!join_nav_capture(object,controller,id->runtime,id->portable,&r)) return 0;
                if(!JOIN_PUSH(t->nav,t->nnav,ncap,r)) return join_note("memory");
                if(!join_capture_flight(t,controller,id->runtime,id->portable,&fcap,&lcap,&agcap,&rcap,&pacap,&stcap,&stpcap)) return 0;   /* versions 5..9 */
                continue;
            }
            if(join_dock_kind(p.kind)) {   /* version 8: a ship docking or undocking */
                join_dock_rec d;memset(&d,0,sizeof(d));d.portable=id->portable;
                if(!join_dock_read(controller,object,p.kind,&d)) return join_note("dock,id=%llx",(unsigned long long)id->portable);
                if(!JOIN_PUSH(t->dock,t->ndock,dcap,d)) return join_note("memory");
                continue;
            }
            if(!form) continue;
            p.form=(uint32_t)form;
            if(!join_pose_lanes(controller,p.form,p.xyz)) return join_note("pose,id=%llx",(unsigned long long)id->portable);
            if(!JOIN_PUSH(t->pose,t->npose,pcap,p)) return join_note("memory");
        }
    }
    if(t->nhealth) qsort(t->health,t->nhealth,sizeof(*t->health),join_health_cmp);
    if(t->npose) qsort(t->pose,t->npose,sizeof(*t->pose),join_pose_cmp);
    if(t->nshield) qsort(t->shield,t->nshield,sizeof(*t->shield),join_shield_cmp);
    if(t->nnav) qsort(t->nav,t->nnav,sizeof(*t->nav),join_nav_cmp);
    if(t->nattn) qsort(t->attn,t->nattn,sizeof(*t->attn),join_attn_cmp);
    for(uint32_t i=1;i<t->nhealth;i++) if(t->health[i].portable==t->health[i-1].portable) return join_note("health_duplicate,id=%llx",(unsigned long long)t->health[i].portable);
    for(uint32_t i=1;i<t->npose;i++) if(t->pose[i].portable==t->pose[i-1].portable) return join_note("pose_duplicate,id=%llx",(unsigned long long)t->pose[i].portable);
    for(uint32_t i=1;i<t->nnav;i++) if(t->nav[i].portable==t->nav[i-1].portable) return join_note("nav_duplicate,id=%llx",(unsigned long long)t->nav[i].portable);
    for(uint32_t i=1;i<t->nattn;i++) if(t->attn[i].portable==t->attn[i-1].portable) return join_note("attn_duplicate,id=%llx",(unsigned long long)t->attn[i].portable);
    if(t->nregion) qsort(t->region,t->nregion,sizeof(*t->region),join_region_cmp);
    for(uint32_t i=1;i<t->nregion;i++) if(!join_region_cmp(&t->region[i],&t->region[i-1]))
        return join_note("region_duplicate,id=%llx,field=%u",(unsigned long long)t->region[i].region,t->region[i].field);
    if(t->nflight) qsort(t->flight,t->nflight,sizeof(*t->flight),join_flight_cmp);
    if(t->nspline) qsort(t->spline,t->nspline,sizeof(*t->spline),join_spline_cmp);
    for(uint32_t i=1;i<t->nflight;i++) if(t->flight[i].portable==t->flight[i-1].portable)
        return join_note("flight_duplicate,id=%llx",(unsigned long long)t->flight[i].portable);
    if(t->nagent) qsort(t->agent,t->nagent,sizeof(*t->agent),join_agent_cmp);   /* pushed with FLIGHT: the same portable IDs */
    if(t->nroute) qsort(t->route,t->nroute,sizeof(*t->route),join_route_cmp);   /* each Agent's head and points, keys unique */
    if(t->nparam) qsort(t->param,t->nparam,sizeof(*t->param),join_param_cmp);   /* pushed with AGENT: the same portable IDs */
    if(t->ndock) qsort(t->dock,t->ndock,sizeof(*t->dock),join_dock_cmp);
    for(uint32_t i=1;i<t->ndock;i++) if(t->dock[i].portable==t->dock[i-1].portable)
        return join_note("dock_duplicate,id=%llx",(unsigned long long)t->dock[i].portable);
    if(t->nsteer) qsort(t->steer,t->nsteer,sizeof(*t->steer),join_steer_cmp);          /* pushed with FLIGHT: the same portable IDs */
    if(t->nsteerpt) qsort(t->steerpt,t->nsteerpt,sizeof(*t->steerpt),join_route_cmp);  /* (portable, index): kind and list constant */
    return join_capture_keys(t) && join_capture_path(t);
}

/* ---- the joiner ---- */
static int join_id_portable_cmp(const void *a,const void *b) {
    uint64_t x=((const join_id_rec *)a)->portable,y=((const join_id_rec *)b)->portable;return x<y?-1:x>y;
}
static uint64_t join_runtime_of(const join_tables *mine,uint64_t portable) {
    uint32_t lo=0,hi=mine->nids;
    while(lo<hi) {uint32_t mid=(lo+hi)/2;uint64_t p=mine->ids[mid].portable;if(p==portable) return mine->ids[mid].runtime;if(p<portable) lo=mid+1;else hi=mid;}
    return 0;
}
static void join_world_row(unsigned *rows,const char *format,...) {
    if(*rows>=JOIN_WORLD_ROWS || join_rx.nrows>=JOIN_ROWS) return;
    ++*rows;va_list list;va_start(list,format);vsnprintf(join_rx.rows[join_rx.nrows],sizeof(join_rx.rows[0]),format,list);va_end(list);++join_rx.nrows;
}
/* ---- the joiner, version 3 ---- */
static int join_unmapped_has(uint64_t runtime);   /* lockstep_join.h: the import's unmapped set */
static int join_u64_cmp(const void *a,const void *b) {uint64_t x=*(const uint64_t *)a,y=*(const uint64_t *)b;return x<y?-1:x>y;}
/* Write n bytes at a live address when they differ; 1 written. */
static int join_poke(uintptr_t at,const void *want,size_t n) {
    if(!memcmp((const void *)at,want,n)) return 0;
    memcpy((void *)at,want,n);return 1;
}
/* NAV's field groups, counted per record that needed them (JOIN_STATUS nav_fields, in this order). */
enum {JOIN_NAVF_CURSOR,JOIN_NAVF_REFRESH,JOIN_NAVF_PARAMS,JOIN_NAVF_PROBE,JOIN_NAVF_VECTORS,JOIN_NAVF_REFS,JOIN_NAVF_MODE,JOIN_NAVF_ETA,
      JOIN_NAVF_LAYER,JOIN_NAVF_PENDING,JOIN_NAVF_COUNT};
static const char *const join_navf_names[JOIN_NAVF_COUNT]={"cursor","refresh","params","probe","vectors","refs","mode","eta","layer","pending"};
static void join_nav_fields_text(char *out,size_t cap) {
    size_t used=0;out[0]=0;
    for(unsigned f=0;f<JOIN_NAVF_COUNT;f++) {
        int n=snprintf(out+used,cap-used,"%s%llu",f?":":"",(unsigned long long)join_rx.nav_field[f]);
        if(n<0 || (size_t)n>=cap-used) return;
        used+=(size_t)n;
    }
}
/* The joiner's runtime ID of a portable reference: 0 for none, *ok 0 when it has no object for it. */
static uint64_t join_nav_runtime(const join_tables *mine,uint64_t portable,int *ok) {
    if(!portable) return 0;
    uint64_t runtime=join_runtime_of(mine,portable);if(!runtime) *ok=0;return runtime;
}
/* One NAV record into the joiner's controller, relative to the joiner's own selectors. Returns the field groups written;
 * *problem 1 missing, 2 shape (the census's links or the probe block), 3 a reference without an object (written without it). */
static unsigned join_nav_admit(const join_nav_rec *r,const join_tables *mine,int *problem,char *why_out,size_t why_cap) {
    uint64_t runtime=join_runtime_of(mine,r->portable);unsigned char *object=join_object(runtime),*controller=NULL;
    world_nav_snapshot n;const char *why="controller";unsigned fixed=0;*problem=0;snprintf(why_out,why_cap,"-");
    if(!object) {*problem=1;return 0;}
    if(!join_read(object+JOIN_CONTROLLER,&controller,8) || !controller ||
       !world_nav_take(join_read,(uintptr_t)image,(uintptr_t)object,(uintptr_t)controller,runtime,&n,&why)) {
        *problem=2;snprintf(why_out,why_cap,"%s",why);return 0;
    }
    unsigned char probe_on=0;uint64_t probe_due=0;
    if(!join_read((void *)(n.links.commands+JOIN_NAV_PROBE_DUE),&probe_due,8) || !join_read((void *)(n.links.commands+JOIN_NAV_PROBE_ON),&probe_on,1)) {
        *problem=2;snprintf(why_out,why_cap,"probe");return 0;
    }
    /* The cursor as the session's relation to the read selector, in the joiner's selectors (lockstep_join.h join_nav_rec). */
    uint32_t cursor=r->relation==2?2u:r->relation==0?n.links.read:n.links.write;
    if(join_poke((uintptr_t)controller+JOIN_NAV_CURSOR,&cursor,4)) fixed|=1u<<JOIN_NAVF_CURSOR;
    if(join_poke((uintptr_t)controller+JOIN_NAV_REFRESH,&r->refresh,8)) fixed|=1u<<JOIN_NAVF_REFRESH;
    if(join_poke((uintptr_t)controller+JOIN_NAV_PARAMS_DUE,&r->params_due,8) | join_poke((uintptr_t)controller+JOIN_NAV_PARAMS_ON,&r->params_on,1))
        fixed|=1u<<JOIN_NAVF_PARAMS;
    /* An off probe's due is not carried (join_nav_capture): the joiner keeps its own, which nothing reads while it is off. */
    if((r->probe_on && join_poke(n.links.commands+JOIN_NAV_PROBE_DUE,&r->probe_due,8)) | join_poke(n.links.commands+JOIN_NAV_PROBE_ON,&r->probe_on,1))
        fixed|=1u<<JOIN_NAVF_PROBE;
    for(unsigned role=0;role<2;role++) {
        const unsigned p=role?n.links.write:n.links.read;const join_nav_bank *b=&r->bank[role];
        const uintptr_t bank=n.links.slots+(uintptr_t)p*WORLD_NAV_BANK_BYTES,pending=n.links.pending+16u*p;
        for(unsigned v=0;v<6;v++) if(join_poke(bank+16u*v,b->xyz+3*v,12)) fixed|=1u<<JOIN_NAVF_VECTORS;
        for(unsigned j=0;j<2;j++) {
            int ok=!(r->flags>>(3*role+j)&1u);uint64_t want=ok?join_nav_runtime(mine,b->ref[j],&ok):0;
            if(!ok) {*problem=3;continue;}
            if(join_poke(bank+0xb0+8u*j,&want,8)) fixed|=1u<<JOIN_NAVF_REFS;
        }
        if(join_poke(bank+0xcc,&b->mode,4)) fixed|=1u<<JOIN_NAVF_MODE;
        if(join_poke(bank+0x150,&b->eta,8) | join_poke(bank+0x159,&b->arrived,1) | join_poke(bank+0x15a,&b->notified,1)) fixed|=1u<<JOIN_NAVF_ETA;
        if(join_poke(bank+0x15b,&b->layer,1)) fixed|=1u<<JOIN_NAVF_LAYER;
        {
            int ok=!(r->flags>>(3*role+2)&1u);uint64_t want=ok && b->pending_state==WORLD_NAV_SAVED?join_nav_runtime(mine,b->pending_ref,&ok):0;
            if(!ok) *problem=3;
            else if(join_poke(pending,&want,8) | join_poke(pending+8,&b->pending_count,8)) fixed|=1u<<JOIN_NAVF_PENDING;
        }
    }
    return fixed;
}
static int join_admit_nav(const join_tables *s,const join_tables *mine) {
    unsigned rows=0,samples=0;
    for(uint32_t i=0;i<s->nnav;i++) {
        const join_nav_rec *r=&s->nav[i];int problem=0;char why[48];
        unsigned fixed=join_nav_admit(r,mine,&problem,why,sizeof(why));
        if(problem==1) {
            ++join_rx.nav_missing;
            join_world_row(&rows,"nav side=session id=%016llx why=missing runtime=%llx",(unsigned long long)r->portable,
                           (unsigned long long)join_runtime_of(mine,r->portable));
            continue;
        }
        if(problem==2) {++join_rx.nav_shape;join_world_row(&rows,"nav side=joiner id=%016llx why=shape check=%s",(unsigned long long)r->portable,why);continue;}
        if(problem==3) {++join_rx.nav_unmapped;join_world_row(&rows,"nav side=session id=%016llx why=unmapped flags=%x",(unsigned long long)r->portable,r->flags);}
        ++join_rx.nav_applied;
        if(!fixed) continue;
        ++join_rx.nav_fixed;
        for(unsigned f=0;f<JOIN_NAVF_COUNT;f++) if(fixed>>f&1u) ++join_rx.nav_field[f];
        if(!samples++) {
            char text[160];size_t used=0;text[0]=0;
            for(unsigned f=0;f<JOIN_NAVF_COUNT;f++) if(fixed>>f&1u) {
                int k=snprintf(text+used,sizeof(text)-used,"%s%s",used?",":"",join_navf_names[f]);if(k>0 && (size_t)k<sizeof(text)-used) used+=(size_t)k;
            }
            if(join_rx.nrows<JOIN_ROWS) snprintf(join_rx.rows[join_rx.nrows++],sizeof(join_rx.rows[0]),"nav side=joiner id=%016llx fixed=%s relation=%u",
                                                 (unsigned long long)r->portable,text,r->relation);
        }
    }
    return 1;
}
/* ATTN: the level and an Object's due, written where they differ (no SetAttention: lockstep_join_world.h ATTN). */
static int join_admit_attn(const join_tables *s,const join_tables *mine) {
    unsigned rows=0,samples=0;
    for(uint32_t i=0;i<s->nattn;i++) {
        const join_attn_rec *a=&s->attn[i];uint64_t runtime=join_runtime_of(mine,a->portable);unsigned char *object=join_object(runtime);uintptr_t vtable=0;
        if(!object) {
            ++join_rx.attn_missing;
            join_world_row(&rows,"attn side=session id=%016llx why=missing runtime=%llx",(unsigned long long)a->portable,(unsigned long long)runtime);
            continue;
        }
        unsigned classes=join_read(object,&vtable,8)?world_class_flags(vtable):0x80000000u;uint32_t level=0;uint64_t due=0;
        if((classes&0x80000000u) || !(classes&4u)!=!(a->flags&JOIN_ATTN_DUE) || !join_read(object+JOIN_ATTENTION,&level,4) ||
           ((classes&4u) && !join_read(object+JOIN_ATTENTION_DUE,&due,8))) {
            ++join_rx.attn_shape;
            join_world_row(&rows,"attn side=joiner id=%016llx why=shape classes=%x flags=%u vtable=%llx",(unsigned long long)a->portable,classes,a->flags,
                           (unsigned long long)(vtable?vtable-(uintptr_t)image:0));
            continue;
        }
        if(level!=a->level) {
            memcpy(object+JOIN_ATTENTION,&a->level,4);++join_rx.attn_level_fixed;
            if(samples<JOIN_WORLD_ROWS && join_rx.nrows<JOIN_ROWS) {
                ++samples;snprintf(join_rx.rows[join_rx.nrows++],sizeof(join_rx.rows[0]),"attn side=joiner id=%016llx level=%u/%u vtable=%llx",
                                   (unsigned long long)a->portable,level,a->level,(unsigned long long)(vtable-(uintptr_t)image));
            }
        }
        if((a->flags&JOIN_ATTN_DUE) && due!=a->due) {memcpy(object+JOIN_ATTENTION_DUE,&a->due,8);++join_rx.attn_due_fixed;}
        ++join_rx.attn_applied;
    }
    return 1;
}
/* KEYS into the joiner's own tables (armed before its load, empty until its first admitted frame). */
static int join_admit_keys(const join_tables *s) {
    unsigned rows=0,actor_rows=0;
    for(uint32_t i=0;i<s->nkeys;i++) {
        const join_key_rec *k=&s->keys[i];int r=-2;
        switch(k->table) {
        case JOIN_KEYS_ATTENTION_ALL: if(attention_all_armed) r=attention_all_first_call(k->key);break;
        case JOIN_KEYS_NAV_PROBE: if(nav_probe_armed) r=nav_first_call(nav_probe_table,&nav_probe_count,k->key);break;
        case JOIN_KEYS_NAV_PARAMS: if(nav_params_armed) r=nav_first_call(nav_params_table,&nav_params_count,k->key);break;
        default: {
            /* A snapshot fixture actor by its index and saved ID (the joiner binds the same actor at that index at T). */
            unsigned a=k->actor-1u,*flag=k->table==JOIN_KEYS_NAV_ACTOR?&nav_initialized[a]:&attention_initialized[a];
            if(!snapshot_configured || a>=12 || snapshot_actors[a].saved!=k->key) {
                ++join_rx.keys_actor;
                join_world_row(&actor_rows,"keys side=session table=%u actor=%u key=%016llx why=actor",k->table,k->actor,(unsigned long long)k->key);
                continue;
            }
            r=*flag?0:1;*flag=1;break;
        }
        }
        if(r==-2) {
            ++join_rx.keys_unarmed;
            join_world_row(&rows,"keys side=joiner table=%u key=%016llx why=unarmed",k->table,(unsigned long long)k->key);
        } else if(r<0) {
            ++join_rx.keys_full;join_world_row(&rows,"keys side=joiner table=%u key=%016llx why=full",k->table,(unsigned long long)k->key);
        } else if(r) ++join_rx.keys_applied[k->table];
        else ++join_rx.keys_known;
    }
    return 1;
}
/* SHIELD: delay, rate and factor written where they differ; the modifiers' factors compared. A group several records read is
 * checked again after all writes. */
static int join_admit_shield(const join_tables *s,const join_tables *mine) {
    unsigned rows=0;unsigned char *placed=s->nshield?calloc(s->nshield,1):NULL;
    if(s->nshield && !placed) return join_fail_rx(JOIN_WHY_CAPTURE,"memory");
    for(uint32_t i=0;i<s->nshield;i++) {
        const join_shield_rec *r=&s->shield[i];uint64_t runtime=join_runtime_of(mine,r->portable);unsigned char *object=join_object(runtime),*group=NULL;
        uint64_t value,changed,maximum;uint32_t count;int full;join_shield_rec now;
        if(!object) {
            ++join_rx.shield_missing;
            join_world_row(&rows,"shield side=session id=%016llx why=missing runtime=%llx",(unsigned long long)r->portable,(unsigned long long)runtime);
            continue;
        }
        if(!join_shield_read(object,&group,&value,&changed,&maximum,&count,NULL,&full) || !group || !join_shield_params(group,&now) ||
           (now.flags&JOIN_SHIELD_MODS_PRESENT)!=(r->flags&JOIN_SHIELD_MODS_PRESENT)) {
            ++join_rx.shield_shape;
            join_world_row(&rows,"shield side=joiner id=%016llx why=shape group=%d mods=%u/%u",(unsigned long long)r->portable,group!=NULL,
                           now.flags&JOIN_SHIELD_MODS_PRESENT,r->flags&JOIN_SHIELD_MODS_PRESENT);
            continue;
        }
        int wrote=join_poke((uintptr_t)group+JOIN_SHIELD_DELAY,&r->delay,8) | join_poke((uintptr_t)group+JOIN_SHIELD_RATE,&r->rate,4) |
                  join_poke((uintptr_t)group+JOIN_SHIELD_FACTOR,&r->factor,4);
        if(wrote) {
            ++join_rx.shield_fixed;
            join_world_row(&rows,"shield side=joiner id=%016llx why=fixed delay=%016llx/%016llx rate=%08x/%08x factor=%08x/%08x",
                           (unsigned long long)r->portable,(unsigned long long)now.delay,(unsigned long long)r->delay,now.rate,r->rate,now.factor,r->factor);
        }
        if(memcmp(now.mods,r->mods,12)) {
            ++join_rx.shield_mods;
            join_world_row(&rows,"shield side=joiner id=%016llx why=mods mods=%08x,%08x,%08x/%08x,%08x,%08x",(unsigned long long)r->portable,
                           now.mods[0],now.mods[1],now.mods[2],r->mods[0],r->mods[1],r->mods[2]);
        }
        placed[i]=1;++join_rx.shield_applied;
    }
    for(uint32_t i=0;i<s->nshield;i++) {
        if(!placed[i]) continue;
        const join_shield_rec *r=&s->shield[i];unsigned char *object=join_object(join_runtime_of(mine,r->portable)),*group=NULL;
        uint64_t value,changed,maximum;uint32_t count;int full;join_shield_rec now;
        if(!object || !join_shield_read(object,&group,&value,&changed,&maximum,&count,NULL,&full) || !group || !join_shield_params(group,&now) ||
           now.delay!=r->delay || now.rate!=r->rate || now.factor!=r->factor) {
            ++join_rx.shield_conflict;
            join_world_row(&rows,"shield side=joiner id=%016llx why=conflict",(unsigned long long)r->portable);
        }
    }
    free(placed);return 1;
}
/* The joiner's own created identities that the package does not name: made by its load (loaded=0) or loaded from the save
 * without a session key (loaded=1, the import's unmapped set). */
static int join_admit_created(const join_tables *s,const join_tables *mine) {
    uint64_t *keys=NULL;uint32_t n=0;unsigned rows=0;
    for(uint32_t i=0;i<s->nids;i++) n+=s->ids[i].klass==WORLD_KEY_CREATED;
    if(n && !(keys=malloc(n*sizeof(*keys)))) return join_fail_rx(JOIN_WHY_CAPTURE,"memory");
    n=0;for(uint32_t i=0;i<s->nids;i++) if(s->ids[i].klass==WORLD_KEY_CREATED) keys[n++]=s->ids[i].portable;
    if(n) qsort(keys,n,sizeof(*keys),join_u64_cmp);
    for(uint32_t i=0;i<mine->nids;i++) {
        const join_id_rec *m=&mine->ids[i];if(m->klass!=WORLD_KEY_CREATED) continue;
        uint32_t lo=0,hi=n;int found=0;
        while(lo<hi && !found) {uint32_t mid=(lo+hi)/2;if(keys[mid]==m->portable) found=1;else if(keys[mid]<m->portable) lo=mid+1;else hi=mid;}
        if(found) continue;
        int loaded=join_unmapped_has(m->runtime);unsigned char *object=join_object(m->runtime);uintptr_t vtable=0;
        if(object) join_read(object,&vtable,8);
        ++join_rx.created_extra;join_rx.created_extra_loaded+=(uint64_t)loaded;
        if(rows<3 && join_rx.nrows<JOIN_ROWS) {
            ++rows;snprintf(join_rx.rows[join_rx.nrows++],sizeof(join_rx.rows[0]),"created side=joiner id=%016llx runtime=%llx vtable=%llx loaded=%d",
                            (unsigned long long)m->portable,(unsigned long long)m->runtime,
                            (unsigned long long)(vtable>=(uintptr_t)image?vtable-(uintptr_t)image:vtable),loaded);
        }
    }
    free(keys);return 1;
}
/* The joiner: REGION. */
static int join_admit_region(const join_tables *s,const join_tables *mine) {
    unsigned rows=0;
    for(uint32_t i=0;i<s->nregion;i++) {
        const join_region_rec *g=&s->region[i];uint64_t runtime=join_runtime_of(mine,g->region);unsigned char *object=join_object(runtime),*field=NULL;
        uintptr_t vtable=0,begin=0;uint32_t n=0;
        if(!object) {
            ++join_rx.region_missing;
            join_world_row(&rows,"region side=session id=%016llx field=%u why=missing runtime=%llx",(unsigned long long)g->region,g->field,
                           (unsigned long long)runtime);
            continue;
        }
        if(!join_read(object,&vtable,8) || vtable!=(uintptr_t)(image+JOIN_REGION_VTABLE) || !join_region_fields(object,&begin,&n) || g->field>=n ||
           !(field=join_region_damage(begin,g->field))) {
            ++join_rx.region_shape;
            join_world_row(&rows,"region side=joiner id=%016llx field=%u why=shape vtable=%llx fields=%u",(unsigned long long)g->region,g->field,
                           (unsigned long long)(vtable>=(uintptr_t)image?vtable-(uintptr_t)image:vtable),n);
            continue;
        }
        if(join_poke((uintptr_t)field+REGION_RNG_LAST,&g->last,8) | join_poke((uintptr_t)field+REGION_RNG_DUE,&g->due,8)) ++join_rx.region_fixed;
        ++join_rx.region_applied;
    }
    return 1;
}
/* One wake of the pathing thread with the skip flag set (the pathing thread idle, no pass in flight): Run copies TimerBase's game
 * time (T, restored) into its TLS clock and +0x200, runs no pass and clears the flag. 0 with why. Tests replace it. */
static int join_path_prime_native(unsigned char *manager,char *why,size_t cap) {
    int32_t state=0;unsigned char waited=0,skip=1;HANDLE wake=NULL,done=NULL;double time=0,game=*(double *)(image+GAME_RVA);
    if(!join_read(manager+JOIN_PATH_STATE,&state,4) || !join_read(manager+JOIN_PATH_WAITED,&waited,1) || !join_read(manager+JOIN_PATH_WAKE,&wake,8) ||
       !join_read(manager+JOIN_PATH_DONE,&done,8) || state!=2 || waited!=1 || !wake || !done) {snprintf(why,cap,"state=%d,waited=%u",state,waited);return 0;}
    *(volatile unsigned char *)(manager+JOIN_PATH_SKIP)=1;
    LONG prior=-1;
    if(!ReleaseSemaphore(wake,1,&prior)) {snprintf(why,cap,"wake");return 0;}
    if(prior!=0) {snprintf(why,cap,"wake_prior=%ld",(long)prior);return 0;}
    if(WaitForSingleObject(done,5000)!=WAIT_OBJECT_0) {snprintf(why,cap,"done");return 0;}
    skip=*(volatile unsigned char *)(manager+JOIN_PATH_SKIP);memcpy(&time,manager+JOIN_PATH_TIME,8);
    if(skip || time!=game) {snprintf(why,cap,"skip=%u,time=%.17g/%.17g",skip,time,game);return 0;}
    return 1;
}
static int (*join_path_prime)(unsigned char *,char *,size_t)=join_path_prime_native;
typedef struct {uint64_t key;uintptr_t node;uint32_t order;uint8_t used;} join_path_node;
static int join_path_node_cmp(const void *a,const void *b) {
    const join_path_node *x=a,*y=b;
    if(x->key!=y->key) return x->key<y->key?-1:1;
    return x->order<y->order?-1:x->order>y->order;
}
static join_path_node *join_path_find(join_path_node *nodes,uint32_t n,uint64_t key) {
    uint32_t lo=0,hi=n;
    while(lo<hi) {uint32_t mid=(lo+hi)/2;if(nodes[mid].key==key) return &nodes[mid];if(nodes[mid].key<key) lo=mid+1;else hi=mid;}
    return NULL;
}
/* Ring surgery on lists the joiner's pathing thread does not touch now (it waits for its wake). */
static void join_path_unlink(uintptr_t node) {
    uintptr_t prev=*(uintptr_t *)node,next=*(uintptr_t *)(node+8);*(uintptr_t *)(prev+8)=next;*(uintptr_t *)next=prev;
}
/* The joiner's extra Agents' nodes (report mode): a ring of the gate's own; no pass assigns or updates them, RemoveAgent
 * 0xeaeee0 finds them on neither list and frees the Agent without its node. */
static uintptr_t join_path_parked[3];
static void join_path_append(uintptr_t sentinel,uintptr_t node);
static void join_path_park(uintptr_t node) {
    if(!join_path_parked[0]) join_path_parked[0]=join_path_parked[1]=(uintptr_t)join_path_parked;
    join_path_append((uintptr_t)join_path_parked,node);
}
static void join_path_append(uintptr_t sentinel,uintptr_t node) {
    uintptr_t last=*(uintptr_t *)sentinel;*(uintptr_t *)node=last;*(uintptr_t *)(node+8)=sentinel;*(uintptr_t *)(last+8)=node;*(uintptr_t *)sentinel=node;
}
static void join_path_shape_row(unsigned *rows,const char *format,...) {
    char check[160];va_list list;va_start(list,format);vsnprintf(check,sizeof(check),format,list);va_end(list);
    ++join_rx.path_shape;join_world_row(rows,"path side=joiner why=shape check=%s",check);
}
/* The joiner: PATH, then the pathing thread's wake. A strict joiner that would refuse writes nothing. */
static int join_admit_path(const join_tables *s,int strict) {
    unsigned rows=0,missing_rows=0,extra_rows=0;unsigned char *manager=join_path_manager();
    if(!s->npath) {join_path_shape_row(&rows,"records");return 1;}
    if(!manager) {join_path_shape_row(&rows,"manager");return 1;}
    const join_path_rec *r=s->path;uintptr_t begin[2];uint32_t nb[2];
    for(unsigned v=0;v<2;v++) if(!join_path_vector(manager,v,&begin[v],&nb[v]) || nb[v]!=r[v].batch)
        {join_path_shape_row(&rows,"batches,vector=%u,joiner=%u,session=%u",v,nb[v],r[v].batch);return 1;}
    for(unsigned v=0,at=2;v<2;v++) for(uint32_t b=0;b<nb[v];b++,at++) {
        join_path_batch batch;uintptr_t links[2]={0,0};
        if(!join_path_batch_read(begin[v]+(uintptr_t)b*JOIN_PATH_BATCH,&batch) || batch.flag!=r[at].flags || batch.count || batch.cursor!=batch.sentinel ||
           !batch.sentinel || !join_read((void *)batch.sentinel,links,16) || links[0]!=batch.sentinel || links[1]!=batch.sentinel) {
            join_path_shape_row(&rows,"batch,vector=%u,batch=%u,flag=%u/%u,count=%llu",v,b,batch.flag,r[at].flags,(unsigned long long)batch.count);
            return 1;
        }
    }
    uint64_t count=0;uintptr_t sentinel=0,*ring=NULL;
    if(!join_read(manager+JOIN_PATH_PENDING,&count,8) || !join_read(manager+JOIN_PATH_PENDING+8,&sentinel,8) ||
       !join_path_ring(sentinel,count,&ring,"pending")) {join_path_shape_row(&rows,"%s",join_fault);return 1;}
    join_path_node *nodes=count?calloc((size_t)count,sizeof(*nodes)):NULL;uintptr_t *place=calloc(s->npath,sizeof(*place));
    unsigned char *used=count?calloc((size_t)count,1):NULL;
    if((count && (!nodes || !used)) || !place) {free(ring);free(nodes);free(place);free(used);return join_fail_rx(JOIN_WHY_CAPTURE,"memory");}
    for(uint32_t i=0;i<count;i++) {nodes[i].key=join_path_owner(ring[i]);nodes[i].node=ring[i];nodes[i].order=i;}
    if(count) qsort(nodes,(size_t)count,sizeof(*nodes),join_path_node_cmp);
    /* Plan: every session member and new Agent by its owner (nodes without a portable ID sort first, key 0). */
    uint32_t keyed=0;while(keyed<count && !nodes[keyed].key) ++keyed;
    for(uint32_t i=keyed+1;i<count;i++) if(nodes[i].key==nodes[i-1].key)
        {join_path_shape_row(&rows,"pending_duplicate,id=%016llx",(unsigned long long)nodes[i].key);free(ring);free(nodes);free(place);free(used);return 1;}
    uint64_t shape_before=join_rx.path_shape;
    for(uint32_t i=0;i<s->npath;i++) {
        if(r[i].kind<JOIN_PATH_KIND_MEMBER) continue;
        if(r[i].flags&JOIN_PATH_LOCAL) {++join_rx.path_local;continue;}
        join_path_node *n=join_path_find(nodes+keyed,(uint32_t)count-keyed,r[i].portable);
        if(!n) {
            ++join_rx.path_missing;
            join_world_row(&missing_rows,"path side=session id=%016llx why=missing kind=%u at=%u/%u/%u",(unsigned long long)r[i].portable,r[i].kind,
                           r[i].vector,r[i].batch,r[i].position);
            continue;
        }
        if(n->used) {join_path_shape_row(&rows,"session_duplicate,id=%016llx",(unsigned long long)r[i].portable);continue;}
        n->used=1;place[i]=n->node;
    }
    for(uint32_t i=0;i<count;i++) {
        used[nodes[i].order]=nodes[i].used;
        if(nodes[i].used) continue;
        ++join_rx.path_extra;
        join_world_row(&extra_rows,"path side=joiner id=%016llx why=extra",(unsigned long long)nodes[i].key);
    }
    if(strict && (join_rx.path_missing || join_rx.path_local || join_rx.path_extra || join_rx.path_shape!=shape_before))
        {free(ring);free(nodes);free(place);free(used);return 1;}
    /* Apply: the session's batches from the new-agent list, each cursor, then the new-agent list in the session's order, the
     * joiner's own extra Agents parked, the round-robin indices. */
    uint32_t i=2,m=2+nb[0]+nb[1];   /* the batch records, the first member record */
    for(unsigned v=0;v<2;v++) for(uint32_t b=0;b<nb[v];b++,i++) {
        const join_path_rec *batch=&r[i];uintptr_t at=begin[v]+(uintptr_t)b*JOIN_PATH_BATCH,bs=*(uintptr_t *)(at+0x10),cursor=0;uint64_t placed=0;
        for(uint32_t p=0;p<batch->value;p++,m++) {
            uintptr_t node=place[m];if(!node) continue;
            join_path_unlink(node);join_path_append(bs,node);++placed;++join_rx.path_members;
            if(p==batch->position) cursor=node;
        }
        *(uint64_t *)(at+8)=placed;*(volatile uint64_t *)(manager+JOIN_PATH_PENDING)-=placed;
        *(uintptr_t *)(at+0x18)=cursor?cursor:*(uintptr_t *)(bs+8);   /* the sentinel itself when nothing was placed */
    }
    for(uint32_t k=m;k<s->npath;k++) if(place[k]) {join_path_unlink(place[k]);join_path_append(sentinel,place[k]);++join_rx.path_pending;}
    for(uint32_t k=0;k<count;k++) if(!used[k]) {   /* the extras, parked in their order */
        join_path_unlink(ring[k]);join_path_park(ring[k]);--*(volatile uint64_t *)(manager+JOIN_PATH_PENDING);
    }
    for(unsigned v=0;v<2;v++) {uint64_t next=r[v].value;memcpy(manager+JOIN_PATH_NEXT+8u*v,&next,8);}
    free(ring);free(nodes);free(place);free(used);
    char why[96];
    if(join_path_prime(manager,why,sizeof(why))) ++join_rx.path_primed;
    else join_path_shape_row(&rows,"prime,%s",why);
    return 1;
}
/* ---- the joiner, version 5 ---- */
static void *join_flight_alloc_native(size_t bytes) {return ((void *(*)(size_t))(void *)(image+JOIN_FLIGHT_ALLOC))(bytes);}
static void *(*join_flight_alloc)(size_t)=join_flight_alloc_native;   /* tests replace it */
/* One spline vector of the joiner's model holding the session's n elements: its own buffer when it has the room, else a new
 * one from the engine's allocator when the load left the vector empty (begin 0; capacity = n, the size class the engine's
 * free derives from it). 1 done, 0 a smaller buffer of its own (not freed here: shape), -1 the allocator failed. */
static int join_flight_put(unsigned char *at,unsigned element,const unsigned char *data,uint32_t n,int *allocated,int *changed) {
    uintptr_t v[3];uint64_t have=0,room=0;
    if(!join_flight_vector(at,element,v,&have,&room)) return 0;
    if(n>room) {
        if(v[0]) return 0;
        unsigned char *p=join_flight_alloc((size_t)n*element);if(!p) return -1;
        v[0]=(uintptr_t)p;v[2]=v[0]+(uintptr_t)n*element;memcpy(at,&v[0],8);memcpy(at+16,&v[2],8);*allocated=1;
    }
    if(n && join_poke(v[0],data,(size_t)n*element)) *changed=1;
    uintptr_t end=v[0]+(uintptr_t)n*element;
    if(join_poke((uintptr_t)at+8,&end,8)) *changed=1;
    return 1;
}
/* FLIGHT and SPLINE into the joiner's models (boot phase 3: no pass runs). The model must be the session's kind; a linear
 * model gets the session's spline first (each segment with this process's BezierSpline vtable), then its state. */
static int join_admit_flight(const join_tables *s,const join_tables *mine) {
    unsigned rows=0;uint32_t j=0;join_t1_reset();
    for(uint32_t i=0;i<s->nflight;i++) {
        const join_flight_rec *r=&s->flight[i];
        while(j<s->nspline && s->spline[j].portable<r->portable) ++j;
        const join_spline_rec *g=s->spline+j;
        uint64_t runtime=join_runtime_of(mine,r->portable);unsigned char *object=join_object(runtime),*controller=NULL,*agent=NULL,*model=NULL,*owner=NULL;
        uintptr_t vtable=0,nav=0,spline=0;
        if(!object) {
            ++join_rx.flight_missing;
            join_world_row(&rows,"flight side=session id=%016llx why=missing runtime=%llx",(unsigned long long)r->portable,(unsigned long long)runtime);
            continue;
        }
        if(!join_read(object+JOIN_CONTROLLER,&controller,8) || !controller || !join_read(controller,&nav,8) || nav!=(uintptr_t)image+JOIN_NAV_VTABLE ||
           !join_read(controller+0x40,&agent,8) || !agent || !join_read(agent+JOIN_AGENT_MODEL,&model,8)) {
            ++join_rx.flight_shape;
            join_world_row(&rows,"flight side=joiner id=%016llx why=shape check=agent",(unsigned long long)r->portable);
            continue;
        }
        if(!model || !join_read(model,&vtable,8) || vtable!=(uintptr_t)image+r->kind) {
            ++join_rx.flight_kind;
            join_world_row(&rows,"flight side=joiner id=%016llx why=kind kind=%x/%llx",(unsigned long long)r->portable,r->kind,
                           (unsigned long long)(model && vtable?vtable-(uintptr_t)image:0));
            continue;
        }
        if(r->flags==JOIN_FLIGHT_OTHER && r->kind==JOIN_STEER_VTABLE && s->version>=9) continue;   /* STEER carries it (join_admit_steer) */
        if(r->flags&(JOIN_FLIGHT_OTHER|JOIN_FLIGHT_UNCARRIED)) {
            ++join_rx.flight_other;
            join_world_row(&rows,"flight side=session id=%016llx why=other kind=%x flags=%x",(unsigned long long)r->portable,r->kind,r->flags);
            continue;
        }
        if(!join_read(model+JOIN_FLIGHT_OWNER,&owner,8) || owner!=agent || !join_read(model+JOIN_FLIGHT_SPLINE,&spline,8) ||
           spline!=(uintptr_t)image+JOIN_FLIGHT_SPLINE_VT) {
            ++join_rx.flight_shape;
            join_world_row(&rows,"flight side=joiner id=%016llx why=shape check=linear",(unsigned long long)r->portable);
            continue;
        }
        for(uint32_t k=0;k<r->segments;k++) {   /* the session's segments with this process's BezierSpline vtable */
            uintptr_t bezier=(uintptr_t)image+JOIN_FLIGHT_BEZIER_VT;
            memcpy(join_flight_segments+(size_t)k*JOIN_FLIGHT_SEGMENT,g[k].data,JOIN_FLIGHT_SEGMENT);
            memcpy(join_flight_segments+(size_t)k*JOIN_FLIGHT_SEGMENT+JOIN_FLIGHT_BEZIER_AT,&bezier,8);
        }
        for(uint32_t k=0;k*JOIN_FLIGHT_PAIRS_PER<r->pairs;k++) {
            uint32_t used=r->pairs-k*JOIN_FLIGHT_PAIRS_PER;if(used>JOIN_FLIGHT_PAIRS_PER) used=JOIN_FLIGHT_PAIRS_PER;
            memcpy(join_flight_pairs+(size_t)k*JOIN_FLIGHT_PAIRS_PER*8u,g[r->segments+k].data,used*8u);
        }
        int allocated=0,changed=0;
        int put=join_flight_put(model+JOIN_FLIGHT_SEGMENTS_AT,JOIN_FLIGHT_SEGMENT,join_flight_segments,r->segments,&allocated,&changed);
        if(put>0) put=join_flight_put(model+JOIN_FLIGHT_PAIRS_AT,8u,join_flight_pairs,r->pairs,&allocated,&changed);
        if(put<=0) {
            if(put<0) ++join_rx.flight_alloc;else ++join_rx.flight_shape;
            join_world_row(&rows,"flight side=joiner id=%016llx why=%s segments=%u pairs=%u",(unsigned long long)r->portable,put<0?"alloc":"spline",
                           r->segments,r->pairs);
            continue;
        }
        unsigned at=0;
        for(unsigned k=0;k<sizeof(join_flight_ranges)/sizeof(*join_flight_ranges);k++) {
            if(join_poke((uintptr_t)model+join_flight_ranges[k].at,r->state+at,join_flight_ranges[k].len)) changed=1;
            at+=join_flight_ranges[k].len;
        }
        /* The rest written; the word +0x60 stays the joiner's own. It is SetState's third argument (the waypoint follower 0xe8c380
         * passes its waypoint's key, +0x10), which the linear step does not read: counted apart from other (J1 j1-20: other 9,999 =
         * 13 other models + 9,986 words). Version 7 carries it as a portable ID in ROUTE's head (join_admit_route writes it). */
        if((r->flags&JOIN_FLIGHT_WORD) && s->version<7) {
            ++join_rx.flight_word;
            join_world_row(&rows,"flight side=session id=%016llx why=word",(unsigned long long)r->portable);
        }
        ++join_rx.flight_applied;
        if(changed) ++join_rx.flight_fixed;
        if(allocated) ++join_rx.flight_spline;
        (void)join_t1_add(runtime,r->state);   /* the T+1 probe's list (diagnostic: without memory fewer are listed, t1's first count) */
    }
    return 1;
}
/* JOIN_STATUS agent_fields: name:records for every field that differed, '-' when none. */
static void join_agent_fields_text(char *out,size_t cap) {
    size_t used=0;out[0]=0;
    for(unsigned i=0;i<JOIN_AGENT_FIELDS && used<cap;i++) {
        if(!join_rx.agent_field[i]) continue;
        int n=snprintf(out+used,cap-used,"%s%s:%llu",used?",":"",join_agent_field_names[i],(unsigned long long)join_rx.agent_field[i]);
        if(n<0 || (size_t)n>=cap-used) break;
        used+=(size_t)n;
    }
    if(!used) snprintf(out,cap,"-");
}
/* A field of an AGENT record as a number (the rows' first=<field>:<session>/<joiner>). */
static uint32_t join_agent_field(const join_agent_rec *r,unsigned f) {
    if(f<JOIN_AGENT_VALUES) return r->value[f];
    if(f<JOIN_AGENT_VALUES+JOIN_AGENT_FLAG_COUNT) return r->flag[f-JOIN_AGENT_VALUES];
    const uint32_t counts[4]={r->waypoints,r->pending,r->members,r->formation};return counts[f-JOIN_AGENT_VALUES-JOIN_AGENT_FLAG_COUNT];
}
/* AGENT into the joiner's Agents (boot phase 3: no pass runs): every field compared and counted per field; the values written
 * (agent fixed), the flags, the vector lengths and +0xf34 only counted (agent state). Version 7: the flags written too (fixed;
 * J1 j1-21: the loader leaves +0xf33 set on 9,737 Agents whose session's was clear), the two vector lengths left to ROUTE (it
 * rebuilds both vectors); +0xf34, the group's members and the formation stay counted. Rows name the differing fields. */
static int join_admit_agent(const join_tables *s,const join_tables *mine) {
    unsigned rows=0,state_rows=0;const int v7=s->version>=7;
    for(uint32_t i=0;i<s->nagent;i++) {
        const join_agent_rec *r=&s->agent[i];uint64_t runtime=join_runtime_of(mine,r->portable);
        unsigned char *object=join_object(runtime),*agent=object?join_nav_agent(object):NULL;
        if(!object) {
            ++join_rx.agent_missing;
            join_world_row(&rows,"agent side=session id=%016llx why=missing runtime=%llx",(unsigned long long)r->portable,(unsigned long long)runtime);
            continue;
        }
        join_agent_rec own;memset(&own,0,sizeof(own));own.portable=r->portable;
        if(!agent || !join_agent_read(agent,&own)) {
            ++join_rx.agent_shape;
            join_world_row(&rows,"agent side=joiner id=%016llx why=shape",(unsigned long long)r->portable);
            continue;
        }
        ++join_rx.agent_applied;
        char names[256];size_t used=0;int fixed=0,state=0,first=-1;names[0]=0;
        const uint32_t counts[4][2]={{r->waypoints,own.waypoints},{r->pending,own.pending},{r->members,own.members},{r->formation,own.formation}};
        for(unsigned f=0;f<JOIN_AGENT_FIELDS;f++) {
            int differs=f<JOIN_AGENT_VALUES?r->value[f]!=own.value[f]:f<JOIN_AGENT_VALUES+JOIN_AGENT_FLAG_COUNT?
                        r->flag[f-JOIN_AGENT_VALUES]!=own.flag[f-JOIN_AGENT_VALUES]:counts[f-JOIN_AGENT_VALUES-JOIN_AGENT_FLAG_COUNT][0]!=
                                                                                     counts[f-JOIN_AGENT_VALUES-JOIN_AGENT_FLAG_COUNT][1];
            if(!differs) continue;
            ++join_rx.agent_field[f];if(first<0) first=(int)f;
            int n=snprintf(names+used,sizeof(names)-used,"%s%s",used?",":"",join_agent_field_names[f]);
            if(n>0 && (size_t)n<sizeof(names)-used) used+=(size_t)n;
            if(f<JOIN_AGENT_WRITTEN) {join_poke((uintptr_t)agent+join_agent_value_at[f],&r->value[f],4);fixed=1;}
            else if(v7 && f>=JOIN_AGENT_VALUES && f<JOIN_AGENT_VALUES+JOIN_AGENT_FLAG_COUNT) {
                join_poke((uintptr_t)agent+join_agent_flag_at[f-JOIN_AGENT_VALUES],&r->flag[f-JOIN_AGENT_VALUES],1);fixed=1;
            }
            else if(!(v7 && f>=JOIN_AGENT_VALUES+JOIN_AGENT_FLAG_COUNT && f<JOIN_AGENT_VALUES+JOIN_AGENT_FLAG_COUNT+2)) state=1;
        }
        if(fixed) {
            ++join_rx.agent_fixed;
            join_world_row(&rows,"agent side=joiner id=%016llx why=values fields=%s first=%s:%08x/%08x",(unsigned long long)r->portable,names,
                           join_agent_field_names[first],join_agent_field(r,(unsigned)first),join_agent_field(&own,(unsigned)first));
        }
        if(state) {
            ++join_rx.agent_state;
            join_world_row(&state_rows,"agent side=joiner id=%016llx why=state fields=%s f33=%u/%u waypoints=%u/%u pending=%u/%u members=%u/%u"
                           " formation=%u/%u",(unsigned long long)r->portable,names,r->flag[4],own.flag[4],r->waypoints,own.waypoints,r->pending,
                           own.pending,r->members,own.members,r->formation,own.formation);
        }
    }
    return 1;
}
/* ---- the joiner, version 7 ---- */
typedef void (*join_route_clear_fn)(void *vector);
typedef void (*join_route_push_fn)(void *vector,const void *point);
static void join_route_clear_native(void *vector) {((void (*)(void *))(void *)(image+JOIN_ROUTE_CLEAR))(vector);}
static void join_route_push_native(void *vector,const void *point) {((void (*)(void *,const void *))(void *)(image+JOIN_ROUTE_PUSH))(vector,point);}
static join_route_clear_fn join_route_clear=join_route_clear_native;   /* tests replace them */
static join_route_push_fn join_route_push=join_route_push_native;
/* The joiner's runtime ID of a carried reference (0 none); *ok 0 when the session's had no portable ID or the joiner no object. */
static uint64_t join_route_runtime(const join_tables *mine,uint64_t portable,uint32_t flags,unsigned slot,int *ok) {
    if(flags>>slot&1u) {*ok=0;return 0;}
    return join_nav_runtime(mine,portable,ok);
}
/* The joiner's entry at `at` as the point names it: a FlightPoint with the mapped reference, no connection, the sub-point (a
 * NavPoint with its mapped reference) exactly when the session's had one, the carried bytes equal. */
static int join_route_equal(uintptr_t at,const join_route_rec *p,uint64_t ref,uint64_t subref) {
    unsigned char entry[JOIN_AGENT_WAYPOINT],sub[JOIN_ROUTE_SUBSIZE],have[JOIN_ROUTE_DATA];uintptr_t subp=0;uint64_t id=0,connection=0;
    if(join_route_entry(at,entry,sub,&subp)<=0 || !subp!=!(p->flags&JOIN_ROUTE_SUB)) return 0;
    memcpy(&id,entry+0x10,8);memcpy(&connection,entry+0x18,8);
    if(id!=ref || connection) return 0;
    if(subp) {memcpy(&id,sub+0x10,8);memcpy(&connection,sub+0x18,8);if(id!=subref || connection) return 0;}
    join_route_pack(entry,subp?sub:NULL,have);
    return !memcmp(have,p->data,JOIN_ROUTE_DATA);
}
/* Both of the joiner's vectors equal the session's points (n their lengths). */
static int join_route_vectors_equal(unsigned char *agent,const join_route_rec *points,const uint16_t n[2],uint64_t (*refs)[2]) {
    uintptr_t v[2][3];uint64_t have[2],room=0;uint32_t k=0;
    if(!join_flight_vector(agent+JOIN_AGENT_WAYPOINTS,JOIN_AGENT_WAYPOINT,v[0],&have[0],&room) ||
       !join_flight_vector(agent+JOIN_AGENT_PENDING,JOIN_AGENT_WAYPOINT,v[1],&have[1],&room) || have[0]!=n[0] || have[1]!=n[1]) return 0;
    for(unsigned list=0;list<2;list++) for(uint32_t i=0;i<n[list];i++,k++)
        if(!join_route_equal(v[list][0]+(uintptr_t)i*JOIN_AGENT_WAYPOINT,&points[k],refs[k][0],refs[k][1])) return 0;
    return 1;
}
/* ROUTE into the joiner's Agents (boot phase 3, after AGENT wrote the flags; no pass runs). Per head: the Agent's vectors, when
 * they differ from the session's, cleared and refilled through the engine (route rebuilt), then read back (route shape when
 * they still differ); the Agent's IDs and bytes and the linear model's word written (route fixed). A head the record could not
 * carry or a point with a connection: route other; a reference without a portable ID or without the joiner's object: route
 * unmapped; neither touches the Agent. */
static int join_admit_route(const join_tables *s,const join_tables *mine) {
    static uint64_t refs[2*JOIN_ROUTE_POINTS][2];
    static __attribute__((aligned(16))) unsigned char entry[JOIN_AGENT_WAYPOINT],sub[JOIN_ROUTE_SUBSIZE];
    unsigned rows=0;uint32_t j=0;
    while(j<s->nroute) {
        const join_route_rec *h=&s->route[j++];const uint16_t n[2]={join_route_count(h,0),join_route_count(h,1)};
        const join_route_rec *points=s->route+j;const uint32_t np=(h->flags&JOIN_ROUTE_OTHER)?0u:(uint32_t)n[0]+n[1];j+=np;
        uint64_t runtime=join_runtime_of(mine,h->portable);unsigned char *object=join_object(runtime),*agent=object?join_nav_agent(object):NULL;
        if(!object) {
            ++join_rx.route_missing;
            join_world_row(&rows,"route side=session id=%016llx why=missing runtime=%llx",(unsigned long long)h->portable,(unsigned long long)runtime);
            continue;
        }
        if(!agent) {
            ++join_rx.route_shape;
            join_world_row(&rows,"route side=joiner id=%016llx why=shape check=agent",(unsigned long long)h->portable);
            continue;
        }
        uint32_t connections=0;for(uint32_t k=0;k<np;k++) connections|=points[k].flags&(JOIN_ROUTE_CONN|JOIN_ROUTE_SUB_CONN);
        if((h->flags&JOIN_ROUTE_OTHER) || connections) {
            ++join_rx.route_other;
            join_world_row(&rows,"route side=session id=%016llx why=other flags=%x points=%x waypoints=%u pending=%u",(unsigned long long)h->portable,
                           h->flags,connections,n[0],n[1]);
            continue;
        }
        int ok=1;uint64_t ids[JOIN_ROUTE_REFS];
        for(unsigned i=0;i<JOIN_ROUTE_REFS;i++) ids[i]=join_route_runtime(mine,h->ref[i],h->flags,i,&ok);
        for(uint32_t k=0;k<np;k++) for(unsigned i=0;i<2;i++) refs[k][i]=join_route_runtime(mine,points[k].ref[i],points[k].flags,i,&ok);
        if(!ok) {
            ++join_rx.route_unmapped;
            join_world_row(&rows,"route side=session id=%016llx why=unmapped flags=%x refs=%llx/%llx/%llx/%llx/%llx",(unsigned long long)h->portable,
                           h->flags,(unsigned long long)h->ref[0],(unsigned long long)h->ref[1],(unsigned long long)h->ref[2],
                           (unsigned long long)h->ref[3],(unsigned long long)h->ref[4]);
            continue;
        }
        unsigned char *model=NULL;uintptr_t vtable=0;
        if((h->flags&JOIN_ROUTE_LINEAR) && (!join_read(agent+JOIN_AGENT_MODEL,&model,8) || !model || !join_read(model,&vtable,8) ||
                                            vtable!=(uintptr_t)image+JOIN_FLIGHT_LINEAR)) {
            ++join_rx.route_shape;
            join_world_row(&rows,"route side=joiner id=%016llx why=shape check=model",(unsigned long long)h->portable);
            continue;
        }
        {uintptr_t v[3];uint64_t have=0,room=0;
         if(!join_flight_vector(agent+JOIN_AGENT_WAYPOINTS,JOIN_AGENT_WAYPOINT,v,&have,&room) ||
            !join_flight_vector(agent+JOIN_AGENT_PENDING,JOIN_AGENT_WAYPOINT,v,&have,&room)) {
             ++join_rx.route_shape;
             join_world_row(&rows,"route side=joiner id=%016llx why=shape check=vectors",(unsigned long long)h->portable);
             continue;
         }}
        if(!join_route_vectors_equal(agent,points,n,refs)) {
            /* The joiner's entries destroyed (each releases its reference's registration), the session's copy-constructed in order
             * (each registers its reference); the engine allocates when a vector has no room. */
            join_route_clear(agent+JOIN_AGENT_WAYPOINTS);join_route_clear(agent+JOIN_AGENT_PENDING);
            for(uint32_t k=0;k<np;k++) {
                const join_route_rec *p=&points[k];uintptr_t subp=(p->flags&JOIN_ROUTE_SUB)?(uintptr_t)sub:0;
                memset(entry,0,sizeof(entry));memset(sub,0,sizeof(sub));
                memcpy(entry+0x10,&refs[k][0],8);memcpy(entry+0x20,p->data,0x48);memcpy(entry+0x70,p->data+0x48,0x20);
                memcpy(entry+JOIN_ROUTE_SUBPOINT,&subp,8);
                if(subp) {memcpy(sub+0x10,&refs[k][1],8);memcpy(sub+0x20,p->data+0x68,0x40);}
                join_route_push(agent+(p->list?JOIN_AGENT_PENDING:JOIN_AGENT_WAYPOINTS),entry);
            }
            if(!join_route_vectors_equal(agent,points,n,refs)) {
                ++join_rx.route_shape;
                join_world_row(&rows,"route side=joiner id=%016llx why=shape check=rebuilt waypoints=%u pending=%u",(unsigned long long)h->portable,n[0],n[1]);
                continue;
            }
            ++join_rx.route_rebuilt;
        }
        int changed=0;
        for(unsigned i=0;i<4;i++) changed|=join_poke((uintptr_t)agent+join_route_ids_at[i],&ids[1+i],8);
        for(unsigned i=0;i<6;i++) changed|=join_poke((uintptr_t)agent+join_route_bytes_at[i],h->data+4+i,1);
        changed|=join_poke((uintptr_t)agent+JOIN_ROUTE_F70,h->data+12,4);
        if(model) changed|=join_poke((uintptr_t)model+JOIN_FLIGHT_WORD_AT,&ids[0],8);
        if(changed) ++join_rx.route_fixed;
        ++join_rx.route_applied;
    }
    return 1;
}
/* ---- the joiner, version 8 ---- */
/* PARAM into the joiner's Agents by role against its own selectors (boot phase 3: no pass runs): each slot's carried bytes
 * compared and written where they differ, +0x170 mapped to the joiner's runtime ID (a session ID without a portable ID, or one
 * the joiner has no object for: param unmapped, the joiner's ID kept, the rest written). Rows name the first differing word. */
static int join_admit_param(const join_tables *s,const join_tables *mine) {
    unsigned rows=0,read=0,write=0;unsigned char own[JOIN_PARAM_SPAN];
    const int selectors=join_param_selectors(&read,&write);
    for(uint32_t i=0;i<s->nparam;i++) {
        const join_param_rec *r=&s->param[i];uint64_t runtime=join_runtime_of(mine,r->portable);
        unsigned char *object=join_object(runtime),*agent=object?join_nav_agent(object):NULL;
        if(!object) {
            ++join_rx.param_missing;
            join_world_row(&rows,"param side=session id=%016llx why=missing runtime=%llx",(unsigned long long)r->portable,(unsigned long long)runtime);
            continue;
        }
        if(!agent || !selectors) {
            ++join_rx.param_shape;
            join_world_row(&rows,"param side=joiner id=%016llx why=shape check=%s",(unsigned long long)r->portable,agent?"selectors":"agent");
            continue;
        }
        int fixed=0,unmapped=0,first_role=-1;unsigned first_at=0;uint32_t first_s=0,first_j=0;
        for(unsigned role=0;role<JOIN_PARAM_ROLES;role++) {
            unsigned char *slot=agent+join_param_offset(role,read,write),want[JOIN_PARAM_SPAN];const unsigned char *b=r->slot[role];
            uint64_t portable=0,id=0;int ok=1;memcpy(&portable,b+JOIN_PARAMB_ID,8);
            if(!join_read(slot,own,sizeof(own))) {fixed=-1;break;}
            memcpy(want,own,sizeof(want));
            memcpy(want,b,JOIN_PARAM_OBJECT);join_param_pad_copy(want,own);memcpy(want+JOIN_PARAM_VECTOR,b+JOIN_PARAMB_VECTOR,16);
            memcpy(want+JOIN_PARAM_TIME,b+JOIN_PARAMB_TIME,8);memcpy(want+JOIN_PARAM_TAIL,b+JOIN_PARAMB_TAIL,JOIN_PARAM_TAIL_LEN);
            id=join_route_runtime(mine,portable,r->flags,role,&ok);
            if(ok) memcpy(want+JOIN_PARAM_ID,&id,8);else unmapped=1;
            if(!memcmp(want,own,sizeof(want))) continue;
            if(first_role<0)
                for(unsigned at=0;at<JOIN_PARAM_SPAN;at+=4) if(memcmp(want+at,own+at,4)) {
                    first_role=(int)role;first_at=at;memcpy(&first_s,want+at,4);memcpy(&first_j,own+at,4);break;
                }
            if(role==2 && memcmp(want+JOIN_PARAM_TURN,own+JOIN_PARAM_TURN,12)) ++join_rx.param_turn;
            ++*(role==0?&join_rx.param_read:role==1?&join_rx.param_write:&join_rx.param_own);
            join_poke((uintptr_t)slot,want,sizeof(want));fixed=1;
        }
        if(fixed<0) {
            ++join_rx.param_shape;
            join_world_row(&rows,"param side=joiner id=%016llx why=shape check=slot",(unsigned long long)r->portable);
            continue;
        }
        if(unmapped) {
            ++join_rx.param_unmapped;
            join_world_row(&rows,"param side=session id=%016llx why=unmapped flags=%x",(unsigned long long)r->portable,r->flags);
        }
        if(fixed) {
            ++join_rx.param_fixed;
            join_world_row(&rows,"param side=joiner id=%016llx why=values first=%s+%x:%08x/%08x",(unsigned long long)r->portable,
                           join_param_roles[first_role<0?0:first_role],first_at,first_s,first_j);
        }
        ++join_rx.param_applied;
    }
    return 1;
}
/* DOCK into the joiner's dock and undock controllers: the controller of the same kind owned by the object, every census field
 * compared, the record written when one differs (only the x, y, z lanes of each vector; the w lanes are padding). */
static int join_admit_dock(const join_tables *s,const join_tables *mine) {
    unsigned rows=0;
    for(uint32_t i=0;i<s->ndock;i++) {
        const join_dock_rec *r=&s->dock[i];uint64_t runtime=join_runtime_of(mine,r->portable);
        unsigned char *object=join_object(runtime),*controller=NULL;uintptr_t vtable=0;
        if(!object) {
            ++join_rx.dock_missing;
            join_world_row(&rows,"dock side=session id=%016llx why=missing runtime=%llx",(unsigned long long)r->portable,(unsigned long long)runtime);
            continue;
        }
        uint32_t kind=0;
        if(join_read(object+JOIN_CONTROLLER,&controller,8) && controller && join_read(controller,&vtable,8) &&
           vtable>=(uintptr_t)image && vtable<(uintptr_t)image+JOIN_FLIGHT_IMAGE) kind=(uint32_t)(vtable-(uintptr_t)image);
        if(kind!=r->kind) {
            ++join_rx.dock_kind;
            join_world_row(&rows,"dock side=joiner id=%016llx why=kind kind=%x/%x",(unsigned long long)r->portable,r->kind,kind);
            continue;
        }
        join_dock_rec own;memset(&own,0,sizeof(own));own.portable=r->portable;
        if(!join_dock_read(controller,object,kind,&own)) {
            ++join_rx.dock_shape;
            join_world_row(&rows,"dock side=joiner id=%016llx why=shape",(unsigned long long)r->portable);
            continue;
        }
        if(memcmp(&own,r,sizeof(own))) {
            const int undock=kind==UNDOCK_CONTROLLER_VTABLE;const unsigned vectors=undock?8u:JOIN_DOCK_VECTORS;
            join_world_row(&rows,"dock side=joiner id=%016llx why=values time=%016llx/%016llx",(unsigned long long)r->portable,
                           (unsigned long long)r->time,(unsigned long long)own.time);
            for(unsigned k=0;k<vectors;k++) join_poke((uintptr_t)controller+join_dock_at(k),r->xyz+3*k,12);
            join_poke((uintptr_t)controller+0x60,&r->time,8);join_poke((uintptr_t)controller+(undock?0xc0u:0x120u),&r->animation,4);
            join_poke((uintptr_t)controller+(undock?0xc4u:0x124u),r->option,undock?1u:2u);
            ++join_rx.dock_fixed;
        }
        ++join_rx.dock_applied;
    }
    return 1;
}
/* ---- the joiner, version 9 ---- */
/* The joiner's path vector (model +0x360) equals the session's points (refs: the joiner's runtime IDs of each point's two). */
static int join_steer_path_equal(unsigned char *model,const join_route_rec *points,uint32_t n,uint64_t (*refs)[2]) {
    uintptr_t v[3];uint64_t have=0,room=0;
    if(!join_flight_vector(model+JOIN_STEER_PATH,JOIN_AGENT_WAYPOINT,v,&have,&room) || have!=n) return 0;
    for(uint32_t i=0;i<n;i++) if(!join_route_equal(v[0]+(uintptr_t)i*JOIN_AGENT_WAYPOINT,&points[i],refs[i][0],refs[i][1])) return 0;
    return 1;
}
/* STEER and STEERPT into the joiner's steering models (boot phase 3: no pass runs). Per record: the model must be the steering
 * class owned by the ship's Agent; its path vector, when it differs from the session's points, cleared and refilled through the
 * engine (ROUTE's functions: each entry's destructor releases, each copy registers its reference; steer rebuilt) and read back;
 * the carried ranges, the two IDs (mapped) and each behaviour of the same class written (steer fixed). Counted without touching
 * the model: missing, kind, shape (no Agent, another owner, a path that reads back otherwise), unmapped (an ID or a point's
 * reference without the joiner's object). Counted, the rest written: other (a behaviour class this build does not carry, a
 * behaviour of another class or none on the joiner, a path the record could not carry or with a connection pointer), job (an
 * async path job pending at T on the session; the joiner has none). */
static int join_admit_steer(const join_tables *s,const join_tables *mine) {
    static uint64_t refs[JOIN_ROUTE_POINTS][2];
    static __attribute__((aligned(16))) unsigned char entry[JOIN_AGENT_WAYPOINT],sub[JOIN_ROUTE_SUBSIZE];
    unsigned rows=0;uint32_t j=0;
    for(uint32_t i=0;i<s->nsteer;i++) {
        const join_steer_rec *r=&s->steer[i];
        while(j<s->nsteerpt && s->steerpt[j].portable<r->portable) ++j;
        const join_route_rec *points=s->steerpt+j;
        const uint32_t np=(r->flags&JOIN_STEER_POINTS)?0u:r->points;
        uint64_t runtime=join_runtime_of(mine,r->portable);unsigned char *object=join_object(runtime),*agent=object?join_nav_agent(object):NULL;
        unsigned char *model=NULL,*owner=NULL;uintptr_t vtable=0;
        if(!object) {
            ++join_rx.steer_missing;
            join_world_row(&rows,"steer side=session id=%016llx why=missing runtime=%llx",(unsigned long long)r->portable,(unsigned long long)runtime);
            continue;
        }
        if(!agent || !join_read(agent+JOIN_AGENT_MODEL,&model,8) || !model) {
            ++join_rx.steer_shape;
            join_world_row(&rows,"steer side=joiner id=%016llx why=shape check=agent",(unsigned long long)r->portable);
            continue;
        }
        if(!join_read(model,&vtable,8) || vtable!=(uintptr_t)image+r->kind) {
            ++join_rx.steer_kind;
            join_world_row(&rows,"steer side=joiner id=%016llx why=kind kind=%x/%llx",(unsigned long long)r->portable,r->kind,
                           (unsigned long long)(vtable?vtable-(uintptr_t)image:0));
            continue;
        }
        if(!join_read(model+JOIN_FLIGHT_OWNER,&owner,8) || owner!=agent) {
            ++join_rx.steer_shape;
            join_world_row(&rows,"steer side=joiner id=%016llx why=shape check=owner",(unsigned long long)r->portable);
            continue;
        }
        int ok=1,id_ok=1;uint64_t ids[2];
        for(unsigned k=0;k<2;k++) {int one=1;ids[k]=join_route_runtime(mine,r->ref[k],r->flags,k,&one);if(!one) id_ok=0;}
        for(uint32_t k=0;k<np;k++) for(unsigned q=0;q<2;q++) refs[k][q]=join_route_runtime(mine,points[k].ref[q],points[k].flags,q,&ok);
        uint32_t connections=0;for(uint32_t k=0;k<np;k++) connections|=points[k].flags&(JOIN_ROUTE_CONN|JOIN_ROUTE_SUB_CONN);
        if(!ok) {
            ++join_rx.steer_unmapped;
            join_world_row(&rows,"steer side=session id=%016llx why=unmapped check=points points=%u",(unsigned long long)r->portable,np);
            continue;
        }
        unsigned other=0;
        if((r->flags&JOIN_STEER_POINTS) || connections) other|=1u;
        else {
            uintptr_t v[3];uint64_t have=0,room=0;
            if(!join_flight_vector(model+JOIN_STEER_PATH,JOIN_AGENT_WAYPOINT,v,&have,&room)) {
                ++join_rx.steer_shape;
                join_world_row(&rows,"steer side=joiner id=%016llx why=shape check=path",(unsigned long long)r->portable);
                continue;
            }
            if(!join_steer_path_equal(model,points,np,refs)) {
                join_route_clear(model+JOIN_STEER_PATH);
                for(uint32_t k=0;k<np;k++) {
                    const join_route_rec *p=&points[k];uintptr_t subp=(p->flags&JOIN_ROUTE_SUB)?(uintptr_t)sub:0;
                    memset(entry,0,sizeof(entry));memset(sub,0,sizeof(sub));
                    memcpy(entry+0x10,&refs[k][0],8);memcpy(entry+0x20,p->data,0x48);memcpy(entry+0x70,p->data+0x48,0x20);
                    memcpy(entry+JOIN_ROUTE_SUBPOINT,&subp,8);
                    if(subp) {memcpy(sub+0x10,&refs[k][1],8);memcpy(sub+0x20,p->data+0x68,0x40);}
                    join_route_push(model+JOIN_STEER_PATH,entry);
                }
                if(!join_steer_path_equal(model,points,np,refs)) {
                    ++join_rx.steer_shape;
                    join_world_row(&rows,"steer side=joiner id=%016llx why=shape check=rebuilt points=%u",(unsigned long long)r->portable,np);
                    continue;
                }
                ++join_rx.steer_rebuilt;
            }
        }
        int changed=join_steer_write(model,join_steer_ranges,JOIN_STEER_NRANGES,r->model);
        if(id_ok) {changed|=join_poke((uintptr_t)model+JOIN_STEER_IDS_AT0,&ids[0],8);changed|=join_poke((uintptr_t)model+JOIN_STEER_IDS_AT1,&ids[1],8);}
        else {
            ++join_rx.steer_unmapped;
            join_world_row(&rows,"steer side=session id=%016llx why=unmapped check=ids flags=%x",(unsigned long long)r->portable,r->flags);
        }
        for(unsigned b=0;b<2;b++) {
            unsigned char *behaviour=NULL;uintptr_t bvt=0;
            if(!join_read(model+JOIN_STEER_BEHAVIOURS+8u*b,&behaviour,8) || (behaviour && !join_read(behaviour,&bvt,8))) {other|=2u<<b;continue;}
            const uint32_t have=behaviour && bvt>=(uintptr_t)image?(uint32_t)(bvt-(uintptr_t)image):0u;
            if(have!=r->behaviour[b] || (r->flags&(JOIN_STEER_UNCARRIED0<<b))) {if(have || r->behaviour[b]) other|=2u<<b;continue;}
            const int k=join_steer_class_of(have);
            if(!behaviour || k<0) {if(behaviour) other|=2u<<b;continue;}   /* a class the record does not carry (shape refuses it) */
            changed|=join_steer_write(behaviour,join_steer_classes[k].r,join_steer_classes[k].n,r->state[b]);
        }
        if(other) {
            ++join_rx.steer_other;
            join_world_row(&rows,"steer side=session id=%016llx why=other what=%x flags=%x behaviours=%x/%x points=%u",(unsigned long long)r->portable,
                           other,r->flags,r->behaviour[0],r->behaviour[1],r->points);
        }
        if(r->flags&JOIN_STEER_JOB) {
            ++join_rx.steer_job;
            join_world_row(&rows,"steer side=session id=%016llx why=job",(unsigned long long)r->portable);
        }
        if(changed) ++join_rx.steer_fixed;
        ++join_rx.steer_applied;
    }
    return 1;
}
/* Every world record (HEALTH, POSE; version 3 SHIELD, NAV, ATTN, KEYS; version 4 REGION, PATH; version 5 FLIGHT) into the joiner's objects (boot
 * phase 3, main thread, the MoveWorker and the pathing thread idle). 0: strict refusal. */
static int join_admit_world(void) {
    const join_tables *s=&join_rx.t;int strict=join_rx.mode==JOIN_STRICT;unsigned hrows=0,prows=0;
    if(s->version<2) return 1;   /* a version 1 package carries no world table (a strict joiner refused it at the load) */
    join_tables mine;memset(&mine,0,sizeof(mine));
    if(!join_world_ids(&mine)) {join_tables_free(&mine);return join_fail_rx(JOIN_WHY_CAPTURE,"world:ids");}
    if(mine.nids) qsort(mine.ids,mine.nids,sizeof(*mine.ids),join_id_portable_cmp);
    unsigned char *placed=s->nhealth?calloc(s->nhealth,1):NULL;
    if(s->nhealth && !placed) {join_tables_free(&mine);return join_fail_rx(JOIN_WHY_CAPTURE,"memory");}
    for(uint32_t i=0;i<s->nhealth;i++) {
        const join_health_rec *h=&s->health[i];uint64_t runtime=join_runtime_of(&mine,h->portable);
        unsigned char *object=join_object(runtime),*group=NULL;uintptr_t vtable=0;uint64_t hull=0,value=0,changed=0,maximum=0;uint32_t count=0;int full=0;
        if(!object) {
            ++join_rx.health_missing;
            join_world_row(&hrows,"health side=session id=%016llx why=missing runtime=%llx",(unsigned long long)h->portable,(unsigned long long)runtime);
            continue;
        }
        unsigned classes=join_read(object,&vtable,8)?world_class_flags(vtable):0x80000000u;
        if((classes&0x80000000u) || !(classes&1u)) {
            ++join_rx.health_shape;
            join_world_row(&hrows,"health side=joiner id=%016llx why=class classes=%x vtable=%llx",(unsigned long long)h->portable,classes,
                           (unsigned long long)(vtable?vtable-(uintptr_t)image:0));
            continue;
        }
        if(!join_read(object+JOIN_HULL,&hull,8) || !join_shield_read(object,&group,&value,&changed,&maximum,&count,NULL,&full) ||
           !group!=!(h->flags&JOIN_HEALTH_SHIELD) || (group && (maximum!=h->maximum || count!=h->count))) {
            ++join_rx.health_shape;
            join_world_row(&hrows,"health side=joiner id=%016llx why=shape group=%d/%u max=%016llx/%016llx count=%u/%u",(unsigned long long)h->portable,
                           group!=NULL,h->flags&JOIN_HEALTH_SHIELD,(unsigned long long)maximum,(unsigned long long)h->maximum,count,h->count);
            continue;
        }
        if(hull!=h->hull) {memcpy(object+JOIN_HULL,&h->hull,8);++join_rx.health_hull_fixed;}
        if(group && (value!=h->value || changed!=h->changed)) {
            memcpy(group+JOIN_SHIELD_VALUE,&h->value,8);memcpy(group+JOIN_SHIELD_CHANGED,&h->changed,8);++join_rx.health_shield_fixed;
        }
        placed[i]=1;++join_rx.health_applied;
    }
    /* A group several records read (a ship and its weapons) must read every record's value after all writes. */
    for(uint32_t i=0;i<s->nhealth;i++) {
        if(!placed[i] || !(s->health[i].flags&JOIN_HEALTH_SHIELD)) continue;
        const join_health_rec *h=&s->health[i];unsigned char *object=join_object(join_runtime_of(&mine,h->portable)),*group=NULL;
        uint64_t value=0,changed=0,maximum=0;uint32_t count=0;int full=0;
        if(!object || !join_shield_read(object,&group,&value,&changed,&maximum,&count,NULL,&full) || !group || value!=h->value || changed!=h->changed) {
            ++join_rx.health_conflict;
            join_world_row(&hrows,"health side=joiner id=%016llx why=conflict value=%016llx/%016llx changed=%016llx/%016llx",(unsigned long long)h->portable,
                           (unsigned long long)value,(unsigned long long)h->value,(unsigned long long)changed,(unsigned long long)h->changed);
        }
    }
    free(placed);
    for(uint32_t i=0;i<s->npose;i++) {
        const join_pose_rec *p=&s->pose[i];uint64_t runtime=join_runtime_of(&mine,p->portable);
        unsigned char *object=join_object(runtime),*controller=NULL;uintptr_t vtable=0;uint32_t kind=0,xyz[12];
        if(!object) {
            ++join_rx.pose_missing;
            join_world_row(&prows,"pose side=session id=%016llx why=missing runtime=%llx",(unsigned long long)p->portable,(unsigned long long)runtime);
            continue;
        }
        unsigned classes=join_read(object,&vtable,8)?world_class_flags(vtable):0x80000000u;
        int form=(classes&0x80000000u) || !(classes&2u) || !join_read(object+JOIN_CONTROLLER,&controller,8) || !controller?-1:join_pose_form(controller,&kind);
        if(form!=(int)p->form || kind!=p->kind || !join_pose_lanes(controller,p->form,xyz)) {
            ++join_rx.pose_shape;
            join_world_row(&prows,"pose side=joiner id=%016llx why=shape classes=%x kind=%x/%x form=%d/%u",(unsigned long long)p->portable,classes,
                           kind,p->kind,form,p->form);
            continue;
        }
        if(memcmp(xyz,p->xyz,sizeof(xyz))) {
            unsigned char *at=controller+(p->form==1?0x10u:0x20u);
            for(unsigned v=0;v<4;v++) memcpy(at+16*v,p->xyz+3*v,12);
            ++join_rx.pose_fixed;
        }
        ++join_rx.pose_applied;
    }
    if(s->version>=3) {join_admit_shield(s,&mine);join_admit_nav(s,&mine);join_admit_attn(s,&mine);join_admit_keys(s);}
    if(s->version>=5) join_admit_flight(s,&mine);   /* before PATH primes the pathing thread (no pass runs either way) */
    if(s->version>=6) join_admit_agent(s,&mine);
    if(s->version>=7) join_admit_route(s,&mine);   /* after AGENT's flags, before PATH primes the pathing thread */
    if(s->version>=8) {join_admit_param(s,&mine);join_admit_dock(s,&mine);}   /* PARAM after AGENT (its words are in the own copy) */
    if(s->version>=9) join_admit_steer(s,&mine);   /* the steering models FLIGHT holds the kind of; before PATH primes the thread */
    if(s->version>=4) {join_admit_region(s,&mine);join_admit_path(s,strict);}
    if(!join_admit_created(s,&mine)) {join_tables_free(&mine);return 0;}
    join_tables_free(&mine);
    if(strict && (join_rx.health_missing || join_rx.health_shape || join_rx.health_conflict))
        return join_fail_rx(JOIN_WHY_MISMATCH,join_rx.health_missing?"world:health_missing":join_rx.health_shape?"world:health_shape":"world:health_conflict");
    if(strict && (join_rx.pose_missing || join_rx.pose_shape))
        return join_fail_rx(JOIN_WHY_MISMATCH,join_rx.pose_missing?"world:pose_missing":"world:pose_shape");
    if(strict && (join_rx.shield_missing || join_rx.shield_shape || join_rx.shield_conflict || join_rx.shield_mods))
        return join_fail_rx(JOIN_WHY_MISMATCH,join_rx.shield_missing?"world:shield_missing":join_rx.shield_shape?"world:shield_shape":
                            join_rx.shield_conflict?"world:shield_conflict":"world:shield_mods");
    if(strict && (join_rx.nav_missing || join_rx.nav_shape || join_rx.nav_unmapped))
        return join_fail_rx(JOIN_WHY_MISMATCH,join_rx.nav_missing?"world:nav_missing":join_rx.nav_shape?"world:nav_shape":"world:nav_unmapped");
    if(strict && (join_rx.attn_missing || join_rx.attn_shape))
        return join_fail_rx(JOIN_WHY_MISMATCH,join_rx.attn_missing?"world:attn_missing":"world:attn_shape");
    if(strict && (join_rx.keys_unarmed || join_rx.keys_actor || join_rx.keys_full))
        return join_fail_rx(JOIN_WHY_MISMATCH,join_rx.keys_unarmed?"world:keys_unarmed":join_rx.keys_actor?"world:keys_actor":"world:keys_full");
    if(strict && join_rx.created_extra) return join_fail_rx(JOIN_WHY_MISMATCH,"world:created_extra");
    if(strict && (join_rx.region_missing || join_rx.region_shape))
        return join_fail_rx(JOIN_WHY_MISMATCH,join_rx.region_missing?"world:region_missing":"world:region_shape");
    if(strict && (join_rx.path_missing || join_rx.path_local || join_rx.path_extra || join_rx.path_shape))
        return join_fail_rx(JOIN_WHY_MISMATCH,join_rx.path_missing?"world:path_missing":join_rx.path_local?"world:path_local":
                            join_rx.path_extra?"world:path_extra":"world:path_shape");
    if(strict && (join_rx.flight_missing || join_rx.flight_kind || join_rx.flight_other || join_rx.flight_shape || join_rx.flight_alloc ||
                  join_rx.flight_word))
        return join_fail_rx(JOIN_WHY_MISMATCH,join_rx.flight_missing?"world:flight_missing":join_rx.flight_kind?"world:flight_kind":
                            join_rx.flight_other?"world:flight_other":join_rx.flight_shape?"world:flight_shape":
                            join_rx.flight_alloc?"world:flight_alloc":"world:flight_word");
    if(strict && (join_rx.agent_missing || join_rx.agent_shape || join_rx.agent_state))
        return join_fail_rx(JOIN_WHY_MISMATCH,join_rx.agent_missing?"world:agent_missing":join_rx.agent_shape?"world:agent_shape":"world:agent_state");
    if(strict && (join_rx.route_missing || join_rx.route_other || join_rx.route_unmapped || join_rx.route_shape))
        return join_fail_rx(JOIN_WHY_MISMATCH,join_rx.route_missing?"world:route_missing":join_rx.route_other?"world:route_other":
                            join_rx.route_unmapped?"world:route_unmapped":"world:route_shape");
    if(strict && (join_rx.param_missing || join_rx.param_shape || join_rx.param_unmapped))
        return join_fail_rx(JOIN_WHY_MISMATCH,join_rx.param_missing?"world:param_missing":join_rx.param_shape?"world:param_shape":"world:param_unmapped");
    if(strict && (join_rx.dock_missing || join_rx.dock_kind || join_rx.dock_shape))
        return join_fail_rx(JOIN_WHY_MISMATCH,join_rx.dock_missing?"world:dock_missing":join_rx.dock_kind?"world:dock_kind":"world:dock_shape");
    if(strict && (join_rx.steer_missing || join_rx.steer_kind || join_rx.steer_shape || join_rx.steer_unmapped || join_rx.steer_other || join_rx.steer_job))
        return join_fail_rx(JOIN_WHY_MISMATCH,join_rx.steer_missing?"world:steer_missing":join_rx.steer_kind?"world:steer_kind":
                            join_rx.steer_shape?"world:steer_shape":join_rx.steer_unmapped?"world:steer_unmapped":
                            join_rx.steer_other?"world:steer_other":"world:steer_job");
    return 1;
}
