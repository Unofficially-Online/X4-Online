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
/* NAVT (v13, section 53): the banks' vectors +0x60..+0xaf, x, y, z lanes by role (lockstep_join.h join_navt_rec). j1-139:
 * every live fixture actor's banks differed there at T+1 though NAV wrote the rest; both of the session's clients alike. The
 * lasting word is +0x88 (the +0x80 vector's z lane): the joiner's load leaves a default (0.025, 0.05, 0) where the session
 * had a value it moves each step (actor 5: 0.490 at 634, 0.495 at 650; the joiner 0.025 -> 0.187), and at 651 actor 5's
 * velocity parted by 2 m/s in one step, actor 11's at 652 (the census does not hash the banks' tail, so T+1 read 0).
 * The w lanes are left out as NAV's. Status navt=written:equal:missing navt_cap=. */
#define JOIN_NAVT_FROM 0x60u
static uint64_t join_navt_written,join_navt_equal,join_navt_missing;
static int join_navt_cmp(const void *a,const void *b) {
    uint64_t x=((const join_navt_rec *)a)->portable,y=((const join_navt_rec *)b)->portable;return x<y?-1:x>y;
}
/* A navigation controller as NAV holds it (lockstep_join.h join_nav_rec): the census's fields by role, references as portable
 * IDs; with tail, its NAVT record. 0 with join_fault naming the object. */
static int join_nav_capture(unsigned char *object,unsigned char *controller,uint64_t runtime,uint64_t portable,join_nav_rec *r,
                            join_navt_rec *tail) {
    world_nav_snapshot n;const char *why="nav";uint32_t cursor=0;memset(r,0,sizeof(*r));r->portable=portable;
    if(tail) {memset(tail,0,sizeof(*tail));tail->portable=portable;}
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
        if(tail) for(unsigned v=0;v<JOIN_NAVT_VECTORS;v++) memcpy(tail->xyz[role]+3*v,bank+JOIN_NAVT_FROM+16*v,12);
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
#define JOIN_FLIGHT_NOMODEL 1u               /* a FLIGHT record's kind for a pilot slot ship's Agent without a flight model */
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
    {0x2c4d288u,3,{{0x10,2,0},{0x20,0x40,1},{0x60,10,0}}},                           /* EvasiveSimpleSteeringBehaviour (0x70) */
    /* SpiralBehaviour (0x30, the factory 0xec6249): +0x20 its creation time (the weave's phase in its update 0xed5d10:
     * sincos((now - +0x20) * k)), +0x28 a thread draw (draw % 100 > 50: the spiral's direction). Not carried, a loaded one has
     * the load's time and its own draw: w1z25, 176c95's want +0x1e0 (0.3086 on the session, -0.7635 on the joiner). */
    {0x2c4e3c0u,4,{{0x10,2,0},{0x18,4,0},{0x20,8,0},{0x28,1,0}}},
    /* The factory's other constructors that draw (its scan, w1z26: 176cef's active DogfightBehaviour2 uncarried, its want
     * +0x1d0 54.11 against -85.09): */
    {0x2c4db70u,5,{{0x10,2,0},{0x20,0x40,1},{0x60,0x18,0},{0x80,4,0},{0x88,8,0}}},  /* DogfightBehaviour2 (0x90: Dogfight1's, +0x80 a draw +-1, +0x88 its creation time) */
    {0x2c4dcf0u,4,{{0x10,2,0},{0x18,4,0},{0x20,0x18,0},{0x38,1,0}}},              /* CrazyIvanBehaviour (0x50: its time, two drawn times, a drawn direction) */
    {0x2c4d1c8u,3,{{0x10,2,0},{0x20,0x40,1},{0x60,10,0}}},                         /* EvasiveRandomSteeringBehaviour (0x70: EvasiveSimple's) */
    {0x2c4f0c0u,3,{{0x10,2,0},{0x20,0x10,1},{0x30,4,0}}},                          /* ParallelPathSteeringBehaviour (0x40: a drawn offset) */
    {0x2c4ed00u,5,{{0x10,2,0},{0x18,4,0},{0x20,0x10,0},{0x30,0x10,1},{0x40,1,0}}}, /* FrontalCapitalCombatSteeringBehaviour (0x50: a drawn time) */
    {0x2c4e6c0u,3,{{0x10,2,0},{0x18,8,0},{0x20,0x40,1}}}};                         /* MiningBehaviour (0x60: a drawn sign) */
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
/* UNDK (opt-in native_gate_join_undk; in-window report 57, w1z205): the shipyard containers of the late join's T+1 fly a Steering
 * model whose active behaviour is a U::Path::UndockPathBehaviour (0x2c4e0b0, 0xe0 bytes; the factory 0xec60b4, base constructor
 * 0xed2a10). STEER carries no state of that class; the joiner's load constructs it afresh (+0x24 -1, +0x30 100, identity rows, its
 * embedded objects' vtables +0x38/+0x98 0) where the session's is mid-undock (+0x18 an ID, +0x28 and +0x90 progress, +0x30 300, two
 * vectors +0x40/+0x50, two transforms), and the joiner restarts the undock: a tick late (717c0, 3116/3117). UNDK carries +0x10..
 * +0xd7: the ID through portables, the vtables as RVAs; +0xd8 (a heap pointer) stays the joiner's. Status undk=on:written:equal:
 * missing:unmapped undk_cap=. */
#define JOIN_UNDK_VTABLE 0x2c4e0b0u
static uint64_t join_runtime_of(const join_tables *mine,uint64_t portable);   /* below */
static unsigned char *join_nav_agent(const unsigned char *object);   /* below */
#define JOIN_UNDK_FROM 0x10u
static int join_undk_on;
static uint64_t join_undk_written,join_undk_equal,join_undk_missing,join_undk_unmapped;static uint32_t join_undk_capn;
static const unsigned join_undk_vt_at[2]={0x38u,0x98u};   /* the embedded objects' vtables when set (offsets in the behaviour) */
/* w1z210: the qwords at +0x38 and +0x98 hold an image vtable only on some containers (717ab); on 717bb +0x38 is the float
 * -13545.0 and +0x9c -1.0, which the RVA translation wrote as 0. Flags: 2/4 the slot raw, 8/16 a heap pointer (the joiner's kept):
 * an aligned value above 4 GB (w1z211: 717db's +0x38 the float -29327.75 over a zero high word, below 4 GB, is a value). */
enum {JOIN_UNDK_RAW=2u,JOIN_UNDK_HEAP=8u};
static int join_undk_cmp(const void *a,const void *b) {
    uint64_t x=((const join_undk_rec *)a)->portable,y=((const join_undk_rec *)b)->portable;return x<y?-1:x>y;
}
static int join_undk_push(join_tables *t,const unsigned char *behaviour,uint64_t portable) {
    join_undk_rec r;memset(&r,0,sizeof(r));r.portable=portable;uint64_t id=0;uint32_t f=0;
    if(!join_read(behaviour+JOIN_UNDK_FROM,r.state,JOIN_UNDK_BYTES)) return 1;
    memcpy(&id,r.state+8,8);memset(r.state+8,0,8);
    if(id) {r.ref=join_route_portable(id,&f,0);if(!r.ref) r.flags|=1u;}
    for(unsigned k=0;k<2;k++) {   /* an image pointer as its RVA; a heap pointer left (flags 8 << k); any other value raw (2 << k) */
        uintptr_t v=0;memcpy(&v,r.state+join_undk_vt_at[k]-JOIN_UNDK_FROM,8);uint64_t keep;
        if(v>=(uintptr_t)image && v-(uintptr_t)image<JOIN_FLIGHT_IMAGE) keep=(uint64_t)(v-(uintptr_t)image);
        else if(v>=0x100000000ull && v<0x7fffffffffffull && !(v&7u)) {keep=0;r.flags|=JOIN_UNDK_HEAP<<k;}   /* the heap lies above 4 GB */
        else {keep=v;r.flags|=JOIN_UNDK_RAW<<k;}
        memcpy(r.state+join_undk_vt_at[k]-JOIN_UNDK_FROM,&keep,8);
    }
    return JOIN_PUSH(t->undk,t->nundk,join_undk_capn,r);
}
/* One record into a behaviour of the class: 1 written, 0 equal, -1 its ID unmapped (left). */
static int join_undk_write(unsigned char *behaviour,const join_undk_rec *r,const join_tables *mine) {
    unsigned char want[JOIN_UNDK_BYTES];memcpy(want,r->state,sizeof(want));
    uint64_t runtime=0;
    if(r->flags&1u) return -1;
    if(r->ref && !(runtime=join_runtime_of(mine,r->ref))) return -1;
    memcpy(want+8,&runtime,8);
    for(unsigned k=0;k<2;k++) {
        unsigned char *slot=want+join_undk_vt_at[k]-JOIN_UNDK_FROM;uint64_t rva=0;uintptr_t v;memcpy(&rva,slot,8);
        if(r->flags&(JOIN_UNDK_RAW<<k)) continue;   /* the session's value as it was */
        if(r->flags&(JOIN_UNDK_HEAP<<k)) {if(!join_read(behaviour+join_undk_vt_at[k],&v,8)) return -1;}   /* the joiner's own */
        else v=rva?(uintptr_t)image+(uintptr_t)rva:0;
        memcpy(slot,&v,8);
    }
    return join_poke((uintptr_t)(behaviour+JOIN_UNDK_FROM),want,sizeof(want));
}
static void join_admit_undk(const join_tables *s,const join_tables *mine) {
    join_undk_written=join_undk_equal=join_undk_missing=join_undk_unmapped=0;
    if(!join_undk_on) return;
    for(uint32_t i=0;i<s->nundk;i++) {
        const join_undk_rec *r=&s->undk[i];
        unsigned char *object=join_object(join_runtime_of(mine,r->portable)),*agent=object?join_nav_agent(object):NULL,*model=NULL,*beh=NULL;
        uintptr_t mvt=0,bvt=0;
        if(!agent || !join_read(agent+JOIN_AGENT_MODEL,&model,8) || !model || !join_read(model,&mvt,8) || mvt!=(uintptr_t)image+JOIN_STEER_VTABLE ||
           !join_read(model+JOIN_STEER_BEHAVIOURS,&beh,8) || !beh || !join_read(beh,&bvt,8) || bvt!=(uintptr_t)image+JOIN_UNDK_VTABLE) {++join_undk_missing;continue;}
        const int w=join_undk_write(beh,r,mine);
        if(w<0) ++join_undk_unmapped; else if(w) ++join_undk_written; else ++join_undk_equal;
    }
}
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
        if(!b && r.behaviour[b]==JOIN_UNDK_VTABLE && !join_undk_push(t,behaviour,portable)) return join_note("memory");   /* UNDK */
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
/* The steering and spline models at the same probe, by name (report 1.14): both sides list the Agents whose solver SOLVER carries as
 * active; after the step of T+1 one row each (gate.join_t1_row; JOIN_T1 lines at JOIN_STOP): what the model asked for (+0x1d0..+0x1ef,
 * the wanted velocities), its avoidance cache (time, contacts) and the solver's clock. A ship that differs in the census at T+1 with
 * equal rows differs inside the solver; with another 'want' or other contacts the difference came in before it. */
#define JOIN_T1X 32u
static struct {uint64_t runtime[JOIN_T1X],portable[JOIN_T1X];unsigned n,nrows;char rows[JOIN_T1X][300];} join_t1x;
static void join_t1x_add(uint64_t runtime,uint64_t portable) {
    if(join_t1x.n<JOIN_T1X) {join_t1x.runtime[join_t1x.n]=runtime;join_t1x.portable[join_t1x.n]=portable;++join_t1x.n;}
}
static const char *join_t1x_row(unsigned i) {return i>=1 && i<=join_t1x.nrows?join_t1x.rows[i-1]:NULL;}
static void join_t1_reset(void) {free(join_t1.items);memset(&join_t1,0,sizeof(join_t1));memset(&join_t1x,0,sizeof(join_t1x));}
static int join_t1_add(uint64_t runtime,const unsigned char *target) {
    join_t1_item item;item.runtime=runtime;memcpy(item.target,target,sizeof(item.target));
    return JOIN_PUSH(join_t1.items,join_t1.n,join_t1.cap,item);
}
static void join_t1_arm(uint64_t tick) {join_t1.tick=tick;join_t1.listed=join_t1.n;join_t1.armed=join_t1.n!=0 || join_t1x.n!=0;}
/* An object's navigation Agent (the controller's +0x40), NULL without one: a NavigationController's, or a pilot slot ship's
 * (X4's PlayerController or a slot's vtable copy of it, lockstep_pilot_slots.h; the lockstep session's w1z5: an in-window
 * joiner's slot ships loaded their Agents without a solver, with default limits and the load's timestamps). */
static unsigned char *join_nav_agent(const unsigned char *object) {
    unsigned char *controller=NULL,*agent=NULL;uintptr_t vt=0;
    if(!join_read(object+JOIN_CONTROLLER,&controller,8) || !controller || !join_read(controller,&vt,8)) return NULL;
    vt=pslot_canonical_vtable(vt);
    if((vt!=(uintptr_t)image+JOIN_NAV_VTABLE && vt!=(uintptr_t)image+PSLOT_VTABLE) || !join_read(controller+0x40,&agent,8)) return NULL;
    return agent;
}
#define JOIN_AGENT_SOLVER 0xf80u             /* the Agent's motion solver (0: it never stepped a steering or spline model) */
/* AGTT (v13, section 56): the Agent's times its load resets, which AGENT does not carry. j1-148 (the join watch's Agent dump of
 * actors 5 and 11, both sides, T .. T+20): +0x658 the session's 7.083 (its load at the start), the joiner's 28.183 (its load);
 * +0xf38 the session's 12.316, the joiner's -1 (actor 11); at 651 actor 5's pathing step writes another command into its banks
 * (+0xad0 ..) with every other word equal. The lockstep session's in-window dump (slot agents): +0x658 alike and +0xac0 (22.43
 * against the joiner's 39.66). Status agtt=written:equal:missing agtt_cap=. */
/* +0xf40: the time of the Agent's last travel mode decision (0xe9187e: no decision while now <= +0xf40 + its interval; +0xf38 the
 * time it posted the last one, 0xe91868). j1-156: the joiner's loaded Agents decided at once (Start/StopTravelModeEvent for 168 ships
 * at 635..636 the session had none of; 6da74 stopped at 636 where the session's stopped at 646). */
static const unsigned join_agtt_at[JOIN_AGTT_FIELDS]={0x658u,0xf40u,0xf38u};
static uint64_t join_agtt_written,join_agtt_equal,join_agtt_missing;static uint32_t join_agtt_capn;
/* AGCB (v13, section 57): a pilot slot ship's Agent's command and probe block (+0x6e0, 0x400 bytes). The lockstep session's w1z7
 * (in-window, slot 1's Agent at 634): +0x6e0..+0x720 the session's 100/150/.../139.1, the joiner's 0; the direction tables
 * +0x980..+0xa88 other; the probe's due +0xac0 the joiner's load time. NAV writes only the probe's due and enable, for nav
 * ships, whose blocks come out equal (j1-152: byte-equal, no per-process word in them). Raw, written at the admission. */
static uint64_t join_agcb_written,join_agcb_equal,join_agcb_missing;static uint32_t join_agcb_capn;
/* native_gate_join_agcb_nav (opt-in; in-window report 57, w1z213/w1z221): the saver records the block of every NavigationController
 * ship's Agent too, and the joiner writes those only with the option. j1-152 found nav ships' blocks equal after the load; a ship
 * whose block the session computed shortly before T is not: the shipyard container 717eb (launched before T) has +0x6d8..+0x72c
 * zero on the joiner (the session's per-axis limits 551.98, 99.6, 275.99, 437.29, 291.53, 501.75) and other direction tables
 * +0x980.., and its Agent skips the flight model's update at T+1 (no spline update call on the joiner at 3034, w1z221), one tick
 * behind from then on. */
static int join_agcb_nav_on;
/* AGCB's record of a controller's Agent (+0x40): 1 recorded or nothing to record, 0 out of memory. */
static int join_capture_agcb(join_tables *t,const unsigned char *controller,uint64_t portable) {
    unsigned char *agent=NULL;join_agcb_rec *c=calloc(1,sizeof(*c));
    if(!c) return join_note("memory");
    c->portable=portable;
    const int ok=join_read(controller+0x40,&agent,8) && agent && join_read(agent+JOIN_AGCB_AT,c->block,JOIN_AGCB_BYTES);
    if(ok && !JOIN_PUSH(t->agcb,t->nagcb,join_agcb_capn,*c)) {free(c);return join_note("memory");}
    free(c);return 1;
}
/* PBNK (v13, section 58): a pilot slot ship's Agent's pathing state banks (Agent +0xad0 + 0x1a0 x bank, pending +0xed0 + 16 x bank),
 * which NAV and NAVT carry for a NavigationController's Agent only. The lockstep session's w1z8 (slot 2's Agent at 634): the two
 * poses in the other banks (a role), the tails (+0xb38 260.22, +0xb40/+0xb48, +0xb50, +0xcf8) 0 on the joiner, the pending
 * counts +0xed8/+0xee8 1 against 0. By role as NAV, relative to the joiner's selectors at the admission. */
static uint64_t join_pbnk_written,join_pbnk_equal,join_pbnk_missing,join_pbnk_unmapped;static uint32_t join_pbnk_capn;
/* PEND (v13, section 59): the pending view cache flags. BuildCache 0x3d4a80's child loop calls PropagateCacheFlags 0x3d46b0 with
 * (the ancestors' due specification flags | child +0x1e0) and so republishes the parts the child's view words allow. j1-158 raw
 * dumps: after the load the joiner's components hold 0x19 (parent, pose, 0x10) where the session's hold 0 (the carrier 0x8d8b1,
 * its part 0x8d8b2, 0x92789; the docked XS ship c:8002306b2d933056 holds 0x19 on both sides): the joiner's publication at T+1
 * of every object (the live poses into the views, which the GRAVP restore undoes for the settled low-attention ones). With
 * native_gate_join_pend (default off) the joiner takes the session's word on every one of its components (0 where the package has
 * none), never on one whose views lack a pose (a load-created view the engine has yet to publish once): status pend=on:written:
 * equal:unposed:missing. */
#define JOIN_PEND_VIEW_POSE 0x108u   /* view 0's pose pointer: JOIN_VIEW_SLOTS + JOIN_VIEW_POSE (below), view 1 at +0x80 */
static int join_pend_on;
static uint64_t join_pend_written,join_pend_equal,join_pend_unposed,join_pend_missing;static uint32_t join_pend_capn;
static int join_pend_cmp(const void *a,const void *b) {
    uint64_t x=((const join_pend_rec *)a)->portable,y=((const join_pend_rec *)b)->portable;return x<y?-1:x>y;
}
static void join_capture_pend(join_tables *t) {
    join_pend_capn=0;
    for(uint32_t i=0;i<t->nids;i++) {
        unsigned char *object=join_object(t->ids[i].runtime);join_pend_rec r={0,0,0};
        if(!object || !t->ids[i].portable || !join_read(object+JOIN_PEND_AT,&r.word,4) || !r.word) continue;
        r.portable=t->ids[i].portable;
        if(!JOIN_PUSH(t->pend,t->npend,join_pend_capn,r)) return;
    }
    if(t->npend>1) qsort(t->pend,t->npend,sizeof(*t->pend),join_pend_cmp);
    {uint32_t k=0;for(uint32_t i=0;i<t->npend;i++) {if(k && t->pend[k-1].portable==t->pend[i].portable) continue;t->pend[k++]=t->pend[i];}t->npend=k;}
}
static void join_admit_pend(const join_tables *s,const join_tables *mine) {
    join_pend_written=join_pend_equal=join_pend_unposed=join_pend_missing=0;
    if(!join_pend_on) return;
    for(uint32_t i=0;i<mine->nids;i++) {
        unsigned char *object=join_object(mine->ids[i].runtime),*p0=NULL,*p1=NULL;uint32_t have=0,want=0;
        if(!object || !mine->ids[i].portable) continue;
        if(!join_read(object+JOIN_PEND_AT,&have,4)) {++join_pend_missing;continue;}
        uint32_t lo=0,hi=s->npend;
        while(lo<hi) {uint32_t mid=lo+(hi-lo)/2u;if(s->pend[mid].portable<mine->ids[i].portable) lo=mid+1u;else hi=mid;}
        if(lo<s->npend && s->pend[lo].portable==mine->ids[i].portable) want=s->pend[lo].word;
        if(have==want) {++join_pend_equal;continue;}
        if(!join_read(object+JOIN_PEND_VIEW_POSE,&p0,8) || !join_read(object+JOIN_PEND_VIEW_POSE+0x80u,&p1,8) || !p0 || !p1)
            {++join_pend_unposed;continue;}
        if(join_poke((uintptr_t)(object+JOIN_PEND_AT),&want,4)) ++join_pend_written; else ++join_pend_equal;
    }
}
static void join_admit_specdrop(const join_tables *s,const join_tables *mine);   /* lockstep_join_specdrop.h (opt-in) */
static int join_pbnk_cmp(const void *a,const void *b) {
    uint64_t x=((const join_pbnk_rec *)a)->portable,y=((const join_pbnk_rec *)b)->portable;return x<y?-1:x>y;
}
static int join_agcb_cmp(const void *a,const void *b) {
    uint64_t x=((const join_agcb_rec *)a)->portable,y=((const join_agcb_rec *)b)->portable;return x<y?-1:x>y;
}
/* One AGTT record for the AGENT record just pushed, from that Agent (j1-149: a lookup by portable ID during the world capture,
 * before the ID list is sorted, read other Agents). */
static int join_agtt_cmp(const void *a,const void *b) {
    uint64_t x=((const join_agtt_rec *)a)->portable,y=((const join_agtt_rec *)b)->portable;return x<y?-1:x>y;
}
/* ARES (opt-in native_gate_join_ares; in-window report 57, w1z195): the Agent result banks. JOIN_WATCHRAW of the in-window late join
 * (T=3033): the session's escort 6f226 and its leader 6f1e7 hold both banks filled at T (+0x50 0xf, +0x5a 1; +0 a runtime ID, +8 a
 * count and a distance), the joiner's load leaves them 0; at T+1 the joiner's Agents read an empty result bank, the escorts take
 * fresh Splines and the formation update the session posts at T+1 comes a step late. Captured with AGTT (every Agent AGENT carries),
 * written at the admission by role through each side's read selector. Status ares=on:written:equal:missing:unmapped ares_cap=. */
#define JOIN_ARES_AT 0xe10u
#define JOIN_ARES_BANK 0x60u
#define JOIN_READ_SELECTOR 0x38f4ee4u    /* g_PathingStateBufferIndex */
static int join_ares_on;
static uint64_t join_ares_written,join_ares_equal,join_ares_missing,join_ares_unmapped;static uint32_t join_ares_capn;
static uint32_t join_read_selector_native(void) {uint32_t sel=2;if(!join_read(image+JOIN_READ_SELECTOR,&sel,4)) sel=2;return sel;}
static uint32_t (*join_read_selector)(void)=join_read_selector_native;   /* tests replace it */
static int join_ares_cmp(const void *a,const void *b) {
    uint64_t x=((const join_ares_rec *)a)->portable,y=((const join_ares_rec *)b)->portable;return x<y?-1:x>y;
}
static int join_ares_push(join_tables *t,const unsigned char *agent,uint64_t portable) {
    const uint32_t sel=join_read_selector();if(sel>1u) return 1;
    join_ares_rec r;memset(&r,0,sizeof(r));r.portable=portable;
    for(unsigned role=0;role<2;role++) {
        const unsigned b=role?1u-sel:sel;uint64_t runtime=0;unsigned klass=0;
        if(!join_read(agent+JOIN_ARES_AT+JOIN_ARES_BANK*b,r.bank[role],JOIN_ARES_BANK)) return 1;
        memcpy(&runtime,r.bank[role],8);memset(r.bank[role],0,8);
        if(runtime) {r.ref[role]=join_key(runtime,&klass);if(!r.ref[role]) r.flags|=1u<<role;}
    }
    return JOIN_PUSH(t->ares,t->nares,join_ares_capn,r);
}
static int join_agtt_push(join_tables *t,const unsigned char *agent,uint64_t portable) {
    join_agtt_rec r;memset(&r,0,sizeof(r));r.portable=portable;
    for(unsigned k=0;k<JOIN_AGTT_FIELDS;k++) if(join_agtt_at[k] && !join_read(agent+join_agtt_at[k],&r.v[k],8)) r.v[k]=0;
    return JOIN_PUSH(t->agtt,t->nagtt,join_agtt_capn,r) && join_ares_push(t,agent,portable);   /* ARES with it */
}
#define JOIN_SPLINE_VTABLE 0x2c4f480u        /* U::Path::SplineFlightControlModel */
#define JOIN_SPLINE_STATE 0x218u             /* the spline model's state word (8: no route, it brakes) */
static void join_t1x_probe(void) {
    for(unsigned i=0;i<join_t1x.n;i++) {
        unsigned char *object=join_object(join_t1x.runtime[i]),*agent=object?join_nav_agent(object):NULL,*model=NULL,*solver=NULL;
        uintptr_t vtable=0,v[3];uint64_t rem=0,clock=0,stamp=0,contacts=0,room=0;uint32_t subs=0,state=0;unsigned char want[32];char hex[65]="-";
        if(agent && (!join_read(agent+JOIN_AGENT_MODEL,&model,8) || !join_read(agent+JOIN_AGENT_SOLVER,&solver,8))) {model=NULL;solver=NULL;}
        if(model && !join_read(model,&vtable,8)) vtable=0;
        if(solver && (!join_read(solver+8,&rem,8) || !join_read(solver+0x18,&subs,4) || !join_read(solver+0x40,&clock,8))) solver=NULL;
        const uint32_t kind=vtable>=(uintptr_t)image?(uint32_t)(vtable-(uintptr_t)image):0u;
        if(kind==JOIN_STEER_VTABLE) {
            if(!join_read(model+0x88,&stamp,8)) stamp=0;
            if(!join_flight_vector(model+JOIN_STEER_CONTACTS,JOIN_STEER_CONTACT,v,&contacts,&room)) contacts=0;
            if(join_read(model+0x1d0,want,32)) for(unsigned b=0;b<32;b++) snprintf(hex+2*b,3,"%02x",want[b]);
        } else if(kind==JOIN_SPLINE_VTABLE && !join_read(model+JOIN_SPLINE_STATE,&state,4)) state=0;
        snprintf(join_t1x.rows[join_t1x.nrows++],sizeof(join_t1x.rows[0]),
                 "id=%016llx kind=%x solver=%d rem=%016llx subs=%u clock=%016llx stamp=%016llx contacts=%llu state=%u want=%s",
                 (unsigned long long)join_t1x.portable[i],kind,solver!=NULL,(unsigned long long)rem,subs,(unsigned long long)clock,
                 (unsigned long long)stamp,(unsigned long long)contacts,state,hex);
    }
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
        join_t1x_probe();
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
/* ---- version 10: SOLVER and BULLET (J1 j1-27's census at T+1; report 1.14) ----
 * The ten steering ships of j1-27 had every byte of their models carried and still differed at T+1: a model with +0xa8 = 1 does not
 * integrate itself, it hands the wanted velocities to the Agent's motion solver +0xf80 (0xe95fb0 allocates and prepares it, 0x1197fe0 and
 * its core 0x1197970 step it, the spline model's step too). The solver is a flat block of 0x18f60 bytes: JPM controller subobjects with
 * their velocity, acceleration and profile state and a 0.02 s fixed-step clock (+0 the step, +8 the remainder, 0.02 fresh). The save
 * does not hold it, the loader does not build it: the joiner's first step would start it from nothing [F: 0xe95fb0, the constructor
 * 0xd47ac0 and its subobject constructors, run under an emulator to read which bytes they write: 78,114 of 102,240, 41 pointers, all
 * vtables or into the object itself (+0x16a0, +0x55a8, +0xa0c8, +0xeec8, +0x118a0, +0x16398, +0x18d90); the destructors of its 13
 * classes free nothing but themselves, so it owns no heap]. So the record is the whole object with its pointers made relative.
 * The joiner (boot phase 3) gets its Agent's solver through the engine's own 0xe95fb0 when it has none (no RNG, no event; it writes only
 * into the solver and the model getter it calls returns a constant) and overwrites every byte with the session's; the unset bytes come
 * along, which is harmless (nothing reads them) and keeps a field the constructor skips and a later step sets.
 * The census of the bullets: zone +0xf0/+0xf8 the children vector of view 0, 0x80 further the other view's (the children walker
 * 0x10c9910); a bullet is a U::Bullet there, its shooter +0x2a0 (setup 0xd50f30), its controller at the component's +0x200. */
#define JOIN_SOLVER_PREPARE 0xe95fb0u
#define JOIN_ZONE_VTABLE 0x2b66080u          /* U::Zone */
#define JOIN_BULLET_VTABLE 0x2c42ac8u        /* U::Bullet */
#define JOIN_BULLET_SHOOTER 0x2a0u
#define JOIN_BULLET_LINEAR 0x2c3f7c0u        /* its linear movement controller (0xb0 bytes; a static or beam one: the first 0x60 read) */
#define JOIN_CHILDREN 0xf0u                  /* a component's children vector {begin, end} of view 0 */
#define JOIN_VIEW_STRIDE 0x80u
#define JOIN_GRAVB_READ 0x38f5260u            /* the radar worker's mapped bounds buffer the update compares with (JOIN_GRAVB) */
#define JOIN_GRAVB_WRITE 0x2f4b99cu           /* and the one it writes */
#define JOIN_VIEW_INDEX 0x38f4eb4u           /* g_ComponentViewCacheIndex: the view the main thread reads */
#define JOIN_CHILDREN_MAX 4000000u
#define JOIN_MACRO_VTABLE 0x2ba5430u         /* U::Macro: +0x20 its name, an MSVC string (lockstep_ai_schedule.h) */
#define JOIN_PARENT 0x70u                    /* a component's parent (the census's parent field, lockstep_world_hash.h) */
static uint32_t join_solver_dormant;
static join_solver_rec join_solver_scratch;
static join_bullet_rec join_bullet_scratch;
/* Every qword of data that points into [self, self+n) as its offset, into the image as its RVA. list: their (offset/8)<<1 | image, in
 * offset order; returns the count, cap+1 when they do not fit (the data past that point stays raw). Without a list only normalized. */
static uint32_t join_relative(unsigned char *data,size_t n,uintptr_t self,uint32_t *list,uint32_t cap) {
    uint32_t count=0;
    for(size_t at=0;at+8u<=n;at+=8) {
        uint64_t q;memcpy(&q,data+at,8);uint32_t in_image=0;
        if(q>=self && q<self+n) q-=self;
        else if(q>=(uintptr_t)image && q<(uintptr_t)image+JOIN_FLIGHT_IMAGE) {q-=(uintptr_t)image;in_image=1;}
        else continue;
        if(list) {
            if(count==cap) return cap+1u;
            list[count]=(uint32_t)(at/8u)<<1|in_image;
        }
        ++count;memcpy(data+at,&q,8);
    }
    return count;
}
static int join_solver_cmp(const void *a,const void *b) {
    uint64_t x=((const join_solver_rec *)a)->portable,y=((const join_solver_rec *)b)->portable;return x<y?-1:x>y;
}
static int join_bullet_cmp(const void *a,const void *b) {
    const join_bullet_rec *x=a,*y=b;
    if(x->zone!=y->zone) return x->zone<y->zone?-1:1;
    return x->position<y->position?-1:x->position>y->position;
}
/* SOLVER by portable ID; known flags, its pointer list in offset order with each value in range, the rest of the list 0. */
static int join_solver_shape(const join_solver_rec *s,uint32_t n) {
    for(uint32_t i=0;i<n;i++) {
        const join_solver_rec *r=&s[i];
        if(!r->portable || (i && r->portable<=s[i-1].portable) || (r->flags&~3u) || r->reserved || r->pointers>JOIN_SOLVER_POINTERS ||
           ((r->flags&JOIN_SOLVER_OVERFLOW) && r->pointers)) return 0;
        for(uint32_t k=0;k<JOIN_SOLVER_POINTERS;k++) {
            const uint32_t e=r->pointer[k];
            if(k>=r->pointers) {if(e) return 0;continue;}
            const uint32_t at=(e>>1)*8u;uint64_t q;
            if(at+8u>JOIN_SOLVER_BYTES || (k && (e>>1)<=(r->pointer[k-1]>>1))) return 0;
            memcpy(&q,r->data+at,8);
            if(q>=((e&1u)?(uint64_t)JOIN_FLIGHT_IMAGE:(uint64_t)JOIN_SOLVER_BYTES)) return 0;
        }
    }
    return 1;
}
/* BULLET by (zone, position), index counting each zone's bullets; known flags; a shooter only without its bit; a macro name of
 * printable characters, 0 behind it, none with its bit. */
static int join_bullet_shape(const join_bullet_rec *s,uint32_t n) {
    for(uint32_t i=0;i<n;i++) {
        const join_bullet_rec *r=&s[i];const int same=i && r->zone==s[i-1].zone;size_t length=0;
        if(!r->zone || (i && join_bullet_cmp(&s[i-1],r)>=0) || r->index!=(same?s[i-1].index+1u:0u) || (r->flags&~7u) ||
           ((r->flags&JOIN_BULLET_NO_SHOOTER) && r->shooter) || ((r->flags&JOIN_BULLET_NO_MOTION) && r->motion_kind)) return 0;
        while(length<JOIN_BULLET_MACRO && r->macro[length]) {if(r->macro[length]<33 || r->macro[length]>126) return 0;++length;}
        if(length==JOIN_BULLET_MACRO || !length!=!!(r->flags&JOIN_BULLET_NO_MACRO)) return 0;
        for(size_t k=length;k<JOIN_BULLET_MACRO;k++) if(r->macro[k]) return 0;
    }
    return 1;
}
/* Version 12 (j1-41): a solver qword that is an address of the session's own memory (a vector's begin, a stale pointer in scratch space)
 * means nothing in another process; the session carries it as JOIN_SOLVER_HEAP and the joiner keeps its own value there. A qword that
 * only looks like an address (an int pair 0, 0x7a; the old test kept the joiner's value for those too and lost the session's data) is
 * not mapped here and rides as it is. */
#define JOIN_SOLVER_HEAP 0x8000000000000007ull
#define JOIN_HEAPISH(q) ((q)>=0x100000000ull && (q)<0x800000000000ull && !((q)&7u))
static uint64_t join_mapped_lo,join_mapped_hi;   /* the last committed region found (one solver's capture) */
static int join_mapped_os(uint64_t q) {
    if(q>=join_mapped_lo && q<join_mapped_hi) return 1;
    MEMORY_BASIC_INFORMATION m;
    if(!VirtualQuery((const void *)(uintptr_t)q,&m,sizeof(m)) || m.State!=MEM_COMMIT || !m.Protect || (m.Protect&(PAGE_NOACCESS|PAGE_GUARD)))
        return 0;
    join_mapped_lo=(uintptr_t)m.BaseAddress;join_mapped_hi=join_mapped_lo+m.RegionSize;return 1;
}
static int (*join_mapped)(uint64_t)=join_mapped_os;
/* A solver block's bytes into r: its pointers into itself or the image made relative and listed, every address of this process's
 * memory marked JOIN_SOLVER_HEAP (version 12); JOIN_SOLVER_OVERFLOW and the bytes raw when the list does not fit. 0: unreadable. */
static int join_solver_fill(join_solver_rec *r,const unsigned char *solver) {
    if(!join_read(solver,r->data,JOIN_SOLVER_BYTES)) return 0;
    const uint32_t count=join_relative(r->data,JOIN_SOLVER_BYTES,(uintptr_t)solver,r->pointer,JOIN_SOLVER_POINTERS);
    if(count>JOIN_SOLVER_POINTERS) {r->flags|=JOIN_SOLVER_OVERFLOW;memset(r->pointer,0,sizeof(r->pointer));join_read(solver,r->data,JOIN_SOLVER_BYTES);}
    else {
        r->pointers=count;join_mapped_lo=join_mapped_hi=0;
        for(unsigned at=0;at+8u<=JOIN_SOLVER_BYTES;at+=8) {   /* the list's qwords are offsets and RVAs now, below 4 GiB */
            uint64_t q;memcpy(&q,r->data+at,8);
            if(JOIN_HEAPISH(q) && join_mapped(q)) {q=JOIN_SOLVER_HEAP;memcpy(r->data+at,&q,8);++join_cap.solver_mapped;}
        }
    }
    return 1;
}
/* The Agent's solver at T into SOLVER (none: nothing). A dormant one (a linear model's) only up to the cap. 0 with join_fault. */
static int join_capture_solver(join_tables *t,const unsigned char *agent,uint64_t runtime,uint64_t portable,uint32_t kind,uint32_t *cap) {
    unsigned char *solver=NULL;
    if(!join_read(agent+JOIN_AGENT_SOLVER,&solver,8)) return join_note("solver,id=%llx,check=agent",(unsigned long long)portable);
    if(!solver) return 1;
    const int active=kind==JOIN_STEER_VTABLE || kind==JOIN_SPLINE_VTABLE;
    if(!active && join_solver_dormant>=JOIN_SOLVER_DORMANT) {++join_cap.solver_over;return 1;}
    join_solver_rec *r=&join_solver_scratch;memset(r,0,sizeof(*r));
    r->portable=portable;r->kind=kind;r->flags=active?JOIN_SOLVER_ACTIVE:0u;
    if(!join_solver_fill(r,solver)) return join_note("solver,id=%llx,check=data",(unsigned long long)portable);
    if(!JOIN_PUSH(t->solver,t->nsolver,*cap,*r)) return join_note("memory");
    if(active) join_t1x_add(runtime,portable);else ++join_solver_dormant;
    return 1;
}
/* PCSV (v13, section 75; the admission opt-in, native_gate_join_pcsv) [w1z159/w1z160, in window: slot 2's ship 0x7a8fe comes out
 * of step T+1 an ulp apart (position y 0x4703a18e against 0x4703a18f, rotation rows 1 .. 2 ulps) with every PCTL word and the
 * ship's pose equal at T. Its update d95180 calls 0xd93440, which steps the controller's own motion solver (0x1197fe0 with
 * controller+0x3b0 at 0xd93554: the same 0x18f60-byte JPM block an Agent keeps at +0xf80, built by 0xd47ac0 in each
 * PlayerController constructor at 0xd8379e/0xd83d1b/0xd8401f) and copies the transform it integrated (+0x100) to +0x60 at
 * 0xd9455f. The save does not hold that solver; PCTL carries only its first words.] Every PlayerController's (X4's or a slot's
 * vtable copy) solver at T, as SOLVER keeps an Agent's: the bytes with pointers into itself and the image relative, the
 * session's heap addresses marked (the joiner keeps its own there), kind PSLOT_VTABLE, by the ship's portable ID. With the opt-in
 * the admission (boot phase 3) writes it into the joiner's controller of the same ship. Status pcsv=on:written:equal:missing:shape
 * pcsv_cap=. */
#define JOIN_PCSV_AT 0x3b0u
static int join_pcsv_on;
static uint32_t join_pcsv_capn;
static uint64_t join_pcsv_written,join_pcsv_equal,join_pcsv_missing,join_pcsv_shape;
static int join_capture_pcsv(join_tables *t,const unsigned char *controller,uint64_t portable) {
    join_solver_rec *r=&join_solver_scratch;memset(r,0,sizeof(*r));
    r->portable=portable;r->kind=PSLOT_VTABLE;
    if(!join_solver_fill(r,controller+JOIN_PCSV_AT)) return join_note("pcsv,id=%llx,check=data",(unsigned long long)portable);
    return JOIN_PUSH(t->pcsv,t->npcsv,join_pcsv_capn,*r)?1:join_note("memory");
}
/* The ship's PlayerController (X4's or a slot's vtable copy), NULL for any other controller. */
static unsigned char *join_pcsv_controller(const unsigned char *object) {
    unsigned char *controller=NULL;uintptr_t vt=0;
    if(!object || !join_read(object+JOIN_CONTROLLER,&controller,8) || !controller || !join_read(controller,&vt,8)) return NULL;
    return pslot_canonical_vtable(vt)==(uintptr_t)image+PSLOT_VTABLE?controller:NULL;
}
/* A bullet's macro name into out (0 filled); 0: not readable. */
static int join_bullet_macro(const unsigned char *bullet,char *out) {
    unsigned char *macro=NULL;uintptr_t vtable=0;uint64_t length=0,capacity=0;const unsigned char *data;
    if(!join_read(bullet+0x60,&macro,8) || !macro || !join_read(macro,&vtable,8) || vtable!=(uintptr_t)image+JOIN_MACRO_VTABLE ||
       !join_read(macro+0x30,&length,8) || !length || length>=JOIN_BULLET_MACRO || !join_read(macro+0x38,&capacity,8) || capacity<length) return 0;
    data=macro+0x20;
    if(capacity>=16 && (!join_read(data,&data,8) || !data)) return 0;
    if(!join_read(data,out,(size_t)length)) {memset(out,0,JOIN_BULLET_MACRO);return 0;}
    for(uint64_t i=0;i<length;i++) if(out[i]<33 || out[i]>126) {memset(out,0,JOIN_BULLET_MACRO);return 0;}
    return 1;
}
/* One bullet into BULLET (the census: what cannot be read is counted in world_cap's last field, it does not fail the save). zone: the
 * key of its parent (+0x70) as BULLET names it; position: its registry slot (ID order, the same on every client); index set after the sort. */
static void join_capture_bullet(join_tables *t,unsigned char *child,uint32_t slot,uint32_t *cap) {
    unsigned char *motion=NULL,*parent=NULL;uintptr_t vtable=0,shooter=0;uint64_t id=0,zone=0;unsigned klass;
    join_bullet_rec *r=&join_bullet_scratch;memset(r,0,sizeof(*r));
    if(!join_read(child,r->bullet,JOIN_BULLET_BYTES)) {++join_cap.bullet_fault;return;}
    memcpy(&parent,r->bullet+JOIN_PARENT,8);
    if(!parent || !join_read(parent+8,&zone,8) || !zone) {++join_cap.bullet_fault;return;}
    r->zone=join_key(zone,&klass);
    if(!r->zone) {r->zone=zone|JOIN_BULLET_ZONE_RUNTIME;++join_cap.bullet_unkeyed;}
    r->position=slot;
    memcpy(&shooter,r->bullet+JOIN_BULLET_SHOOTER,8);
    if(shooter && join_read((void *)(shooter+8u),&id,8)) r->shooter=join_route_portable(id,&r->flags,0);
    else if(!shooter) r->flags|=JOIN_BULLET_NO_SHOOTER;
    if(!join_bullet_macro(child,r->macro)) r->flags|=JOIN_BULLET_NO_MACRO;
    memcpy(&motion,r->bullet+JOIN_CONTROLLER,8);
    if(!motion || !join_read(motion,&vtable,8) || vtable<(uintptr_t)image || vtable>=(uintptr_t)image+JOIN_FLIGHT_IMAGE) r->flags|=JOIN_BULLET_NO_MOTION;
    else {
        const size_t bytes=vtable==(uintptr_t)image+JOIN_BULLET_LINEAR?JOIN_BULLET_MOTION:0x60u;
        r->motion_kind=(uint32_t)(vtable-(uintptr_t)image);
        if(join_read(motion,r->motion,bytes)) (void)join_relative(r->motion,bytes,(uintptr_t)motion,NULL,0);
        else memset(r->motion,0,sizeof(r->motion));
    }
    (void)join_relative(r->bullet,JOIN_BULLET_BYTES,(uintptr_t)child,NULL,0);
    if(!JOIN_PUSH(t->bullet,t->nbullet,*cap,*r)) ++join_cap.bullet_fault;
}
/* Every live U::Bullet of the component registry (j1-28/29: bullets are in no zone's children vector, 1,428 zones and 14,044 children
 * walked, none). The registry U::ComponentRegistry::Find 0xced70 reads: blocks of 2^20 slots at +0x58 (32 of them), 32-byte buckets of
 * three: the object pointers +0, +8, +0x10, 16-bit generations at +0x18, a flag nibble per slot at bit 16+4k of +0x1c; a slot is live
 * when its nibble has bit 0 and bit 2 (Find's mode 4). The walk is by slot, so the order is the runtime IDs' (synchronized). */
#define JOIN_REGISTRY 0x6cf1500u
#define JOIN_REGISTRY_BLOCKS 0x58u
static unsigned char *join_bullet_registry;          /* tests: a synthetic registry; NULL: the engine's */
static uint32_t join_bullet_block_slots=1u<<20;      /* tests shrink it */
static void join_capture_bullets(join_tables *t,uint32_t *cap) {
    unsigned char *reg=join_bullet_registry;
    if(!reg && (!join_read(image+JOIN_REGISTRY,&reg,8) || !reg)) {++join_cap.bullet_fault;return;}
    for(uint32_t b=0;b<32u;b++) {
        unsigned char *block=NULL;
        if(!join_read(reg+JOIN_REGISTRY_BLOCKS+8u*b,&block,8)) {++join_cap.bullet_fault;return;}
        if(!block) continue;
        ++join_cap.bullet_blocks;
        const uint32_t buckets=(join_bullet_block_slots+2u)/3u;
        for(uint32_t k=0;k<buckets;k++) {
            unsigned char bucket[32];uint32_t flags;
            if(!join_read(block+(size_t)k*32u,bucket,32)) {++join_cap.bullet_fault;break;}
            memcpy(&flags,bucket+0x1c,4);
            for(uint32_t e=0;e<3u && k*3u+e<join_bullet_block_slots;e++) {
                const uint32_t nibble=flags>>(16u+4u*e)&15u;unsigned char *object;uintptr_t vtable=0;
                if((nibble&5u)!=5u) continue;
                memcpy(&object,bucket+8u*e,8);
                if(!object) continue;
                ++join_cap.bullet_live;
                if(!join_read(object,&vtable,8)) {++join_cap.bullet_fault;continue;}
                if(vtable==(uintptr_t)image+JOIN_BULLET_VTABLE) join_capture_bullet(t,object,b*(1u<<20)+k*3u+e,cap);
            }
        }
    }
}
/* ---- version 11: GRAV, GRAVC, GRAVS (the radar; report 1.15) ----
 * The Gravidar unit table and the units' layout as lockstep_gravidar_digest.h and lockstep_gravidar_deadlines.h read them (the
 * lookup 0x10b2dc0 pinned): *0x6cf1650 the AI::Gravidar (vtable 0x2c68860), 256 buckets, lock +0x88+b*0x28, tree +0x2890+b*0x40
 * (head, root at +8, children +8/+0x10, unit at +0x28); an AI::ActiveGravidarUnit (0x2c67610, 0xc8 bytes; the constructor 0x10ac0c0):
 * +8 the owner's runtime ID, +0x18 its lock, +0x40..+0x67 the class word, radar factor +0x44 and their neighbours, +0x68 and +0x80
 * the primary and second contact vectors (runtime IDs, sorted), +0x98 and +0xa0 pointers to two more, +0xa8/+0xb0/+0xb8 the last
 * scan, the next due and the last has_scanned post, +0xc0, +0xc4 the flags; an AI::GravidarUnit (0x2c67660) is its first 0x68
 * bytes. Capture under the bucket lock and the unit's lock (the Gravidar thread is idle at the boundary as at the admission, the
 * locks only make it certain); nothing is written. What cannot be read is counted (grav_cap) and does not fail the save. */
#define JOIN_GRAV_WORKER 0x6cf1650u
#define JOIN_GRAV_WORKER_VT 0x2c68860u
#define JOIN_GRAV_ACTIVE_VT 0x2c67610u
#define JOIN_GRAV_PASSIVE_VT 0x2c67660u
#define JOIN_GRAV_PASSIVE_BYTES 0x68u
#define JOIN_GRAV_BUCKETS 256u
#define JOIN_GRAV_NODES 400000u
#define JOIN_GRAV_CONTACTS_MAX 65536u
#define JOIN_GRAV_SECTOR_VT 0x2b1ea48u
#define JOIN_GRAV_LOOKUP 0x10b2dc0u
static unsigned char *join_grav_worker_test;        /* tests: a synthetic worker */
static void join_grav_lock_native(void *cs,int enter) {if(enter) EnterCriticalSection((CRITICAL_SECTION *)cs);else LeaveCriticalSection((CRITICAL_SECTION *)cs);}
static void (*join_grav_lock)(void *,int)=join_grav_lock_native;   /* tests: no locks */
static void *join_grav_lookup_native(void *worker,uint64_t runtime) {return ((void *(*)(void *,uint64_t))(void *)(image+JOIN_GRAV_LOOKUP))(worker,runtime);}
static void *(*join_grav_lookup)(void *,uint64_t)=join_grav_lookup_native;
static unsigned char *join_grav_worker(void) {
    unsigned char *w=join_grav_worker_test;uintptr_t vtable=0;
    if(!w && !join_read(image+JOIN_GRAV_WORKER,&w,8)) return NULL;
    return w && join_read(w,&vtable,8) && vtable==(uintptr_t)image+JOIN_GRAV_WORKER_VT?w:NULL;
}
static int join_grav_cmp(const void *a,const void *b) {
    uint64_t x=((const join_grav_rec *)a)->portable,y=((const join_grav_rec *)b)->portable;return x<y?-1:x>y;
}
static int join_gravc_cmp(const void *a,const void *b) {
    const join_gravc_rec *x=a,*y=b;
    if(x->portable!=y->portable) return x->portable<y->portable?-1:1;
    if(x->list!=y->list) return x->list<y->list?-1:1;
    return x->index<y->index?-1:x->index>y->index;
}
/* A vector {begin, end, capacity} of 8-byte entries at at: 1 with *n and *begin, 0 unreadable or implausible. */
static int join_grav_vector(const unsigned char *at,uintptr_t *begin,uint64_t *n) {
    uintptr_t v[3];
    if(!join_read(at,v,sizeof(v)) || v[1]<v[0] || v[2]<v[1] || ((v[1]-v[0])&7u) || ((v[2]-v[0])&7u) || (!v[0] && (v[1] || v[2])) ||
       (v[1]-v[0])/8u>JOIN_GRAV_CONTACTS_MAX) return 0;
    *begin=v[0];*n=(v[1]-v[0])/8u;return 1;
}
static uint64_t join_grav_ids[JOIN_GRAV_CONTACTS_MAX];
static uint32_t join_grav_ccap;
/* One list of a unit into GRAVC (the session's order) and its count. 0: unreadable (counted, the record keeps what was read). */
static int join_grav_list(join_tables *t,join_grav_rec *r,unsigned list,const unsigned char *at) {
    uintptr_t begin=0;uint64_t n=0;
    if(!at) return 1;
    if(!join_grav_vector(at,&begin,&n) || (n && !join_read((void *)begin,join_grav_ids,(size_t)n*8u))) return 0;
    for(uint64_t i=0;i<n;i++) {
        unsigned klass;join_gravc_rec c;memset(&c,0,sizeof(c));
        c.portable=r->portable;c.list=list;c.index=(uint32_t)i;c.contact=join_grav_ids[i]?join_key(join_grav_ids[i],&klass):0;
        if(!c.contact) c.flags=JOIN_GRAVC_UNMAPPED;
        if(!JOIN_PUSH(t->gravc,t->ngravc,join_grav_ccap,c)) return 0;
    }
    r->count[list]=(uint32_t)n;return 1;
}
static uint32_t join_grav_cap_units;
/* ---- version 12: GRAVP (the published poses the radar reads; report 1.16) ---- */
#define JOIN_VIEW_SLOTS 0xe0u                /* object+0xe0+view*0x80: the component view cache (parent +0, the pose pointer +0x28) */
#define JOIN_VIEW_POSE 0x28u
static unsigned char **join_gravp_objects;static uint32_t join_gravp_nobjects,join_gravp_ocap,join_gravp_cap;
static int join_ptr_cmp(const void *a,const void *b) {
    uintptr_t x=(uintptr_t)*(unsigned char *const *)a,y=(uintptr_t)*(unsigned char *const *)b;return x<y?-1:x>y;
}
static int join_gravp_cmp(const void *a,const void *b) {
    const join_gravp_rec *x=a,*y=b;
    if(x->portable!=y->portable) return x->portable<y->portable?-1:1;
    return x->rel<y->rel?-1:x->rel>y->rel;
}
static uint32_t join_view_index(void) {uint32_t v=0;return join_read(image+JOIN_VIEW_INDEX,&v,4) && v<2u?v:0u;}
/* The radar's objects and their ancestors below the sector (deduplicated), both views' poses relative to the current view. */
static void join_capture_gravp(join_tables *t) {
    const uint32_t cur=join_view_index();
    if(join_gravp_nobjects>1) qsort(join_gravp_objects,join_gravp_nobjects,sizeof(*join_gravp_objects),join_ptr_cmp);
    join_gravp_cap=0;
    for(uint32_t i=0;i<join_gravp_nobjects;i++) {
        if(i && join_gravp_objects[i]==join_gravp_objects[i-1]) continue;
        unsigned char *link=join_gravp_objects[i];uint64_t key=0;unsigned klass;uint64_t runtime=0;
        if(!join_read(link+8,&runtime,8) || !runtime || !(key=join_key(runtime,&klass))) {++join_cap.grav_unmapped;continue;}
        for(uint32_t rel=0;rel<2u;rel++) {
            join_gravp_rec r;memset(&r,0,sizeof(r));unsigned char *pose=NULL;
            r.portable=key;r.rel=rel;
            if(!join_read(link+JOIN_VIEW_SLOTS+(size_t)(cur^rel)*0x80u+JOIN_VIEW_POSE,&pose,8)) {++join_cap.grav_fault;continue;}
            if(pose) {if(join_read(pose,r.pose,sizeof(r.pose))) r.flags=JOIN_GRAVP_PRESENT;else {++join_cap.grav_fault;continue;}}
            if(!JOIN_PUSH(t->gravp,t->ngravp,join_gravp_cap,r)) {++join_cap.grav_fault;break;}
        }
    }
    free(join_gravp_objects);join_gravp_objects=NULL;join_gravp_nobjects=join_gravp_ocap=0;
    if(t->ngravp) qsort(t->gravp,t->ngravp,sizeof(*t->gravp),join_gravp_cmp);
    {uint32_t k=0;for(uint32_t i=0;i<t->ngravp;i++) {if(k && !join_gravp_cmp(&t->gravp[k-1],&t->gravp[i])) continue;t->gravp[k++]=t->gravp[i];}t->ngravp=k;}
}
/* A unit's owner and its ancestors below the sector into the list GRAVP walks. */
static void join_gravp_note(unsigned char *object) {
    for(unsigned depth=0;object && depth<8u;depth++) {
        uintptr_t vtable=0;
        if(!join_read(object,&vtable,8) || vtable==(uintptr_t)image+JOIN_GRAV_SECTOR_VT) break;
        if(!JOIN_PUSH(join_gravp_objects,join_gravp_nobjects,join_gravp_ocap,object)) return;
        if(!join_read(object+JOIN_PARENT,&object,8)) return;
    }
}
/* By (portable, rel), each once, rel 0 or 1, known flags, no bytes without a pose. */
/* Version 13 (report 1.18): the settled published poses restored after the joiner's first two frames. j1-85: the session keeps a
 * low-attention ship's published pose stale in both views (attacker 0xad530: 22 km from its live position); the joiner's load
 * publication writes the live pose into the back view during T+1 (its readers still see the stale front view), the views swap, and
 * from T+2 its radar and its low-attention combat (bboxdistanceto) read the live pose. The engine's publication list is not found
 * (report 1.18), so at the boundaries after T+1 and T+2 the joiner puts the session's pose back into every view of an object whose
 * two views the session had equal (settled: the session publishes it neither then nor later, as the rows show); objects in flight
 * are left alone. By runtime ID, resolved at each boundary. */
static uint64_t join_runtime_of(const join_tables *mine,uint64_t portable);   /* below */
/* j1-88: in flight is no sign of the session's publishing either (radar units 0xa119b and 0x96ed1: their two views differ at T, the
 * session publishes neither at T+1 nor T+2, the joiner's load writes the live pose): the session publishes an object of high
 * attention every frame (0x6f256, attention 6) and the rest on its own occasions. So every view of every GRAVP or VIEWP object whose
 * published attention (VIEWF) is below JOIN_VIEWF_KEEP gets the session's pose of that view back, by the view's slot as the
 * admission mapped it (the slots swap roles every frame, their contents stay). */
/* j1-158 (JOIN_WATCH, JOIN_WATCHPCF on 0x92789 and the XS ship c:8002306b2d933056): the restore ran over the engine's own
 * republication. 0x92789 changes zone at 636: on both sides BuildCache (0x3d4d0f, flags 0x19) republishes view 1 at 636 and view 0 at
 * 637 with the new zone 0x10e as cached parent and the pose relative to it; the joiner's restores after 637 and 638 put the pose
 * relative to the old zone 0x109 back under the new parent (its radar position from 644: the new zone plus the old pose, 15 units
 * lose it). The XS ship undocks at 641 (cached parent dock part 0x8d8b2 -> zone 0x29), the restores after 642 and 643 put its pose
 * relative to the dock back (its radar position: the zone's origin, 3 units fewer add it at 643). So each entry keeps its view's
 * cached parent after the admission and is dropped for good once the view names another one (status gravp_moved). */
/* w1z177 (in window, native_gate_gravidar_posts_probe on L miner a63f1 and Kha'ak fighter 82aa2): 82aa2 jumped ~40 km inside its zone
 * within the restore's ticks; the engine republished its views on both sides, the parent stayed, and the joiner's restore put its
 * pose of T back into one slot, which kept it until that view's next publication: at 682 the joiner's scan gathered 82aa2 40 km
 * away and a63f1 dropped it (60.9 km against a 40.5 km bound), the session kept it. The restore mends the load's republication
 * (its pending view words publish every low-attention object once in the first frames, the live pose against the session's
 * older settled one) and nothing after it [w1z178, a first try: dropping at a 1 m distance or a change after the first check
 * dropped 17,784 entries and the load's republications with them; a63f1 then had one contact more on the joiner at 651.
 * w1z179: the load's first publications lie 102 < 1 m, 100 < 100 m, 708 < 1 km, 7287 < 10 km and 3270 further from the session's
 * settled poses, so no distance tells a jump from a stale settled pose: dropping those over 10 km parted the posts at 641]. So
 * the first publication after the admission that differs from the session's pose is the load's (own0, mended); any later one
 * that is neither the session's pose nor own0 is the engine's own, and the entry drops for good (status gravp_moved_pose;
 * gravp_first= the first publications' distances from the session's pose: < 1 m, < 100 m, < 1 km, < 10 km, more). */
typedef struct {uint64_t runtime;uint32_t slot,dropped;const unsigned char *pose;unsigned char *parent;unsigned char own0[0x40];int seen,extra;} join_gravp_settled;
static join_gravp_settled *join_gravp_set;static uint32_t join_gravp_nset,join_gravp_setcap;
static uint64_t join_gravp_restored,join_gravp_checked,join_gravp_gone,join_gravp_moved,join_gravp_moved_pose;
static uint64_t join_gravp_first[5];
static float join_gravp_dist(const unsigned char *own,const unsigned char *want) {
    float a[3],b[3];memcpy(a,own,12);memcpy(b,want,12);
    const float dx=a[0]-b[0],dy=a[1]-b[1],dz=a[2]-b[2],d2=dx*dx+dy*dy+dz*dz;
    return d2==d2?sqrtf(d2):INFINITY;   /* NaN counts as far */
}
/* GRAVP settled (native_gate_join_gravp_settled, opt-in): the late join (T=3033; j1-381..384, gravidar post rows): the session keeps
 * the published pose of some objects of attention 3..5 stale as well, both views equal (miner 0x95f9a in travel mode, attention 5:
 * its settled pose 33 km behind its live one; 0x74ef1 and 0x8fbb8 at 5, 0x80fc5 at 3). The restore above left them to the joiner,
 * whose load published the live pose in the first frames: from 3037 its radar units dropped 0x95f9a (and the contacts of observer
 * 0x74ef1) where the session's kept them, the joiner alone ran the fight scripts' event_gravidar_has_scanned handlers, and the
 * zone 0x561 escorts parted at 3092. With the option, a GRAVP object whose two views the session had equal, and every VIEWP object
 * (settled by definition), is restored below attention JOIN_GRAVP_SETTLED_KEEP too; an object in flight (its views apart) stays
 * under JOIN_VIEWF_KEEP. Attention 6 and more stay with the joiner (j1-88: published every frame).
 * x4mods-67's in-window regression at T=633 (w1z215/w1z216/w1z219: 25 differing at 900, a Xenon and Split fight at a cluster_408
 * gatezone; w1z218, the same set without it, 0): an object of attention 3..5 is one the engine republishes on its own soon after
 * the join where a fight raises it, and the restore through T+10 took that republication for the load's and put T's pose back.
 * So these entries (extra) are restored at the boundary after T+1 only, after the admission's write; the low-attention ones keep
 * the whole window (JOIN_GRAVP_SETTLED_STEPS; the caller sets join_gravp_step to the boundary's step after T). */
#define JOIN_GRAVP_SETTLED_KEEP 6u
#define JOIN_GRAVP_SETTLED_STEPS 1u
static uint32_t join_gravp_step;   /* the restore's boundary: steps after T (0: tests, every entry) */
static int join_gravp_settled_on;
static int join_gravp_attention_below(const join_tables *s,uint64_t portable,uint32_t keep) {
    uint32_t lo=0,hi=s->nviewf;
    while(lo<hi) {uint32_t mid=lo+(hi-lo)/2u;if(s->viewf[mid].portable<portable) lo=mid+1u;else hi=mid;}
    if(lo>=s->nviewf || s->viewf[lo].portable!=portable) return 0;   /* unknown: left to the joiner */
    return s->viewf[lo].attention[0]<keep && (s->viewf[lo].attention[1]&~JOIN_VIEWF_INFLIGHT)<keep;
}
static int join_gravp_low_attention(const join_tables *s,uint64_t portable) {return join_gravp_attention_below(s,portable,JOIN_VIEWF_KEEP);}
/* GRAVP records are sorted by (portable, rel): the object's two views, both present and equal. */
static int join_gravp_views_equal(const join_tables *s,uint32_t i) {
    const join_gravp_rec *a=&s->gravp[i],*b=NULL;
    if(i+1u<s->ngravp && s->gravp[i+1u].portable==a->portable) b=&s->gravp[i+1u];
    else if(i && s->gravp[i-1u].portable==a->portable) b=&s->gravp[i-1u];
    return b && b->rel!=a->rel && (a->flags&b->flags&JOIN_GRAVP_PRESENT) && !memcmp(a->pose,b->pose,sizeof(a->pose));
}
static uint64_t join_gravp_settled_extra;   /* status gravp_settled=on:entries beyond the low-attention ones */
static void join_gravp_settled_build(const join_tables *s,const join_tables *mine) {
    const uint32_t cur=join_view_index();
    free(join_gravp_set);join_gravp_set=NULL;join_gravp_nset=join_gravp_setcap=0;join_gravp_restored=join_gravp_checked=join_gravp_gone=join_gravp_moved=0;
    join_gravp_moved_pose=0;memset(join_gravp_first,0,sizeof(join_gravp_first));join_gravp_settled_extra=0;
    for(uint32_t i=0;i<s->ngravp;i++) {
        const join_gravp_rec *a=&s->gravp[i];
        if(!(a->flags&JOIN_GRAVP_PRESENT) || a->rel>1u) continue;
        const int low=join_gravp_low_attention(s,a->portable);
        if(!low && !(join_gravp_settled_on && join_gravp_views_equal(s,i) && join_gravp_attention_below(s,a->portable,JOIN_GRAVP_SETTLED_KEEP))) continue;
        const uint64_t runtime=join_runtime_of(mine,a->portable);
        join_gravp_settled e={runtime,cur^a->rel,0u,a->pose,NULL,{0},0,!low};
        if(runtime && !JOIN_PUSH(join_gravp_set,join_gravp_nset,join_gravp_setcap,e)) return;
        if(runtime && !low) ++join_gravp_settled_extra;
    }
    for(uint32_t i=0;i<s->nviewp;i++) {   /* VIEWP: the other zone-held objects' settled poses, both slots */
        const int low=join_gravp_low_attention(s,s->viewp[i].portable);
        if(!low && !(join_gravp_settled_on && join_gravp_attention_below(s,s->viewp[i].portable,JOIN_GRAVP_SETTLED_KEEP))) continue;
        const uint64_t runtime=join_runtime_of(mine,s->viewp[i].portable);
        if(runtime && !low) join_gravp_settled_extra+=2u;
        for(uint32_t v=0;v<2u && runtime;v++) {
            join_gravp_settled e={runtime,v,0u,s->viewp[i].pose,NULL,{0},0,!low};
            if(!JOIN_PUSH(join_gravp_set,join_gravp_nset,join_gravp_setcap,e)) return;
        }
    }
}
/* native_gate_join_view_skip (diagnostic, a joiner): leave view-bank writes of the admission out. 1 VIEWF (both views' flags and
 * attention, written only with native_gate_join_viewf_write), 2 VIEWK, 4 the GRAVP/VIEWP pose restores after T+1, 8 GRAVD (the cached parent in both views). The lockstep
 * session's captures: a joiner's window alternates two camera views after its STOP (w1x, j1-132, j1-135 with the watch off),
 * ordinary clients do not; a bank written settled here that X4 does not republish would alternate once the gate stops. */
static unsigned join_view_skip;
/* native_gate_join_viewf_write (diagnostic, default off): the joiner writes the session's VIEWF words (both views' flags and
 * attention). Off since j1-138: with them a joiner's window alternates every presented frame after its STOP, the HUD layer
 * missing on every other one (j1-132, j1-135, j1-137 with only bit 4 skipped); without them none (j1-136 mask 15, j1-138 mask
 * 1), and the census is the same (900: 172 both ways; j1-82 already: equal to no VIEWF). The package still carries VIEWF: the
 * restore after T+1 selects its low-attention objects by the session's published attention. */
static int join_viewf_write;
/* native_gate_join_viewf_bits (default 0): without join_viewf_write, the joiner takes only these bits of the session's VIEWF flag
 * words (both views; the attention and the other bits stay its own), as with join_viewf_write never on a kept object (in flight, or
 * above one). PropagateCacheFlags 0x3d46b0 republishes a view's part only while the view's word holds its bit (1 the cached parent
 * from the live one, 2 the children, 8 the pose, 0x40 the attention) and clears the bit where the live flags +0x1e4 hold it; a load
 * leaves the current view's words pending (0x240059, 0x40059 where the session has 0x250058, 0x10040). j1-157: the joiner refreshes
 * the cached parent of 0x92789 at its zone change but not its pose (its radar position: the new zone plus the pose relative to the
 * old one). */
static uint32_t join_viewf_bits;
/* Each entry's view's cached parent at the end of the admission (after GRAVD wrote the ships' ones). */
static void join_gravp_settled_parents(void) {
    for(uint32_t i=0;i<join_gravp_nset;i++) {
        unsigned char *object=join_object(join_gravp_set[i].runtime),*parent=NULL;
        if(object && join_read(object+JOIN_VIEW_SLOTS+(size_t)join_gravp_set[i].slot*0x80u,&parent,8)) join_gravp_set[i].parent=parent;
    }
}
static void join_gravp_restore(void) {
    if(join_view_skip&4u) return;
    for(uint32_t i=0;i<join_gravp_nset;i++) {
        if(join_gravp_set[i].dropped || (join_gravp_set[i].extra && join_gravp_step>JOIN_GRAVP_SETTLED_STEPS)) continue;
        unsigned char *object=join_object(join_gravp_set[i].runtime);const unsigned char *want=join_gravp_set[i].pose;
        if(!object) {++join_gravp_gone;continue;}
        unsigned char *parent=NULL;
        if(!join_read(object+JOIN_VIEW_SLOTS+(size_t)join_gravp_set[i].slot*0x80u,&parent,8)) continue;
        if(parent!=join_gravp_set[i].parent) {join_gravp_set[i].dropped=1;++join_gravp_moved;continue;}   /* republished by the engine */
        unsigned char *pose=NULL,own[0x40];
        if(!join_read(object+JOIN_VIEW_SLOTS+(size_t)join_gravp_set[i].slot*0x80u+JOIN_VIEW_POSE,&pose,8) || !pose || !join_read(pose,own,sizeof(own))) continue;
        ++join_gravp_checked;
        if(!memcmp(own,want,sizeof(own))) continue;   /* the session's pose */
        if(!join_gravp_set[i].seen) {   /* the first publication since the admission: the load's */
            const float d=join_gravp_dist(own,want);
            ++join_gravp_first[d<1.0f?0:d<100.0f?1:d<1000.0f?2:d<10000.0f?3:4];
            memcpy(join_gravp_set[i].own0,own,sizeof(own));join_gravp_set[i].seen=1;
        } else if(memcmp(own,join_gravp_set[i].own0,sizeof(own))) {join_gravp_set[i].dropped=1;++join_gravp_moved_pose;continue;}   /* the engine's own */
        if(join_poke((uintptr_t)pose,want,0x40)) ++join_gravp_restored;
    }
}
static int join_gravp_shape(const join_gravp_rec *g,uint32_t n) {
    for(uint32_t i=0;i<n;i++) {
        const join_gravp_rec *r=&g[i];
        if(!r->portable || r->rel>1u || (r->flags&~JOIN_GRAVP_PRESENT) || (i && join_gravp_cmp(&g[i-1],r)>=0)) return 0;
        if(!(r->flags&JOIN_GRAVP_PRESENT)) for(unsigned b=0;b<sizeof(r->pose);b++) if(r->pose[b]) return 0;
    }
    return 1;
}
static int join_grav_unit(join_tables *t,unsigned char *unit) {
    uintptr_t vtable=0;uint64_t runtime=0;unsigned klass;join_grav_rec r;memset(&r,0,sizeof(r));
    if(!join_read(unit,&vtable,8) || !join_read(unit+8,&runtime,8)) {++join_cap.grav_fault;return 1;}
    const int active=vtable==(uintptr_t)image+JOIN_GRAV_ACTIVE_VT;
    if(!active && vtable!=(uintptr_t)image+JOIN_GRAV_PASSIVE_VT) {++join_cap.grav_fault;return 1;}
    r.portable=runtime?join_key(runtime,&klass):0;
    if(!r.portable) {++join_cap.grav_unmapped;return 1;}
    join_gravp_note(join_object(runtime));   /* version 12: its published poses */
    r.kind=(uint32_t)(vtable-(uintptr_t)image);
    if(!active) {
        ++join_cap.grav_passive;
        if(!join_read(unit,r.unit,JOIN_GRAV_PASSIVE_BYTES)) {++join_cap.grav_fault;return 1;}
        memset(r.unit,0,0x40);
        {   /* +0x10: the player view's viewer, as for an active unit (below) */
            uint64_t viewer=0;unsigned vk;
            if(join_read(unit+0x10,&viewer,8) && viewer) {uint64_t key=join_key(viewer,&vk);memcpy(r.unit+0x10,&key,8);}
        }
    } else {
        unsigned char *second=NULL,*third=NULL;int ok;uint32_t before=t->ngravc;r.flags=JOIN_GRAV_ACTIVE;
        join_grav_lock(unit+0x18,1);
        ok=join_read(unit,r.unit,JOIN_GRAV_BYTES) && join_read(unit+0x98,&second,8) && join_read(unit+0xa0,&third,8) &&
           join_grav_list(t,&r,0,unit+0x68) && join_grav_list(t,&r,1,unit+0x80) && join_grav_list(t,&r,2,second) && join_grav_list(t,&r,3,third);
        join_grav_lock(unit+0x18,0);
        if(!ok) {   /* a unit read in part: its contacts dropped, the record flagged (the joiner writes nothing of it) */
            ++join_cap.grav_fault;r.flags|=JOIN_GRAV_UNCARRIED;t->ngravc=before;memset(r.count,0,sizeof(r.count));
        }
        memset(r.unit,0,0x40);memset(r.unit+0x68,0,0x40);
        {   /* +0x10: the viewer whose player view last took the unit (0x10abc70 compares it with the viewer's ID and posts
             * ObjectEnteredPlayerViewEvent on a change; j1-153: a joiner's first radar apply posted it for every unit the
             * session's had taken at its start), carried as the viewer's portable ID */
            uint64_t viewer=0;unsigned vk;
            if(join_read(unit+0x10,&viewer,8) && viewer) {uint64_t key=join_key(viewer,&vk);memcpy(r.unit+0x10,&key,8);}
        }
    }
    if(!JOIN_PUSH(t->grav,t->ngrav,join_grav_cap_units,r)) {++join_cap.grav_fault;return 0;}
    return 1;
}
/* In-order walk of one bucket's tree (as gdig_tree, through join_read). */
static int join_grav_tree(join_tables *t,const unsigned char *head) {
    const unsigned char *stack[64],*node=NULL;unsigned depth=0,count=0;
    if(!join_read(head+8,&node,8)) return 0;
    while(node || depth) {
        while(node) {
            if(depth==64 || ++count>JOIN_GRAV_NODES) return 0;
            stack[depth++]=node;if(!join_read(node+8,&node,8)) return 0;
        }
        node=stack[--depth];
        unsigned char *unit=NULL;if(!join_read(node+0x28,&unit,8)) return 0;
        if(unit && !join_grav_unit(t,unit)) return 0;
        if(!join_read(node+0x10,&node,8)) return 0;
    }
    return 1;
}
/* gdl_seen without inserting (lockstep_gravidar_deadlines.h gdl_first_move's table). */
static int join_gdl_seen(uint64_t runtime) {
    if(!gdl_armed || !runtime) return 0;
    unsigned h=(unsigned)(lockstep_mix(runtime)&(GDL_TABLE-1));
    for(unsigned n=0;n<GDL_TABLE;n++,h=(h+1)&(GDL_TABLE-1)) {if(gdl_seen[h]==runtime) return 1;if(!gdl_seen[h]) return 0;}
    return 0;
}
/* ---- version 12: GRAVN (the zone groups' neighbour lists; report 1.16) ----
 * The worker's sector entries (+0x68b8, 32 bytes: [0] sector, [1] [2] the group vector of 0x58-byte groups: [0] zone, +0x38 the neighbour
 * group indices), read and written under the list lock +0x6890 (Update 0x10b1810 takes it). Zones and neighbours without a portable key
 * are counted (grav_cap's first field) and left out. */
#define JOIN_GRAVN_GROUP 0x58u
static uint32_t join_gravn_cap;
/* GRAVN extra (opt-in native_gate_join_gravn_extra) [w1z164..w1z166, in window: tempzone c:800278ac659ccc22 of sector 0x852 exists
 * on both sides at T, empty; the session's Gravidar has no group for it yet, the joiner's load made one (JOIN_UNPAIRED gravn
 * why=extra). Ship 96a98 warps into it at 637 on both sides; the session's Update 0x10b1810 then adds the zone's group and puts it
 * into its neighbours' lists, the joiner already has the group and adds nothing. GRAVN had rewritten every other group's list
 * with the session's of T, which cannot name the tempzone, so the joiner's scan at 652 never paired zone group 1 with it: unit
 * 9f134 dropped 96a98 (12 -> 11, inside its bound) and 90532 did not gain it (4 -> 5 on the session); from there the units'
 * has_scanned times parted (the Gravidar digest's times at 660), two defence computers took event_gravidar_has_scanned at 1764
 * on the joiner only, and 6d92f's pilot picked another target by 1800.] With the opt-in each joiner group the session has no
 * record of keeps its place in the lists GRAVN writes: every group in the extra group's own neighbour list (the joiner's load
 * built it from the zone's spatial query, as the session's add will) gets the extra group's index added. Status
 * gravn_kept=on:indices added. */
static int join_gravn_extra_on;
static uint64_t join_gravn_kept;
#define JOIN_GRAVN_EXTRA_MAX 16u
static int join_gravn_cmp(const void *a,const void *b) {
    const join_gravn_rec *x=a,*y=b;
    if(x->sector!=y->sector) return x->sector<y->sector?-1:1;
    if(x->zone!=y->zone) return x->zone<y->zone?-1:1;
    return x->neighbour<y->neighbour?-1:x->neighbour>y->neighbour;
}
/* A sector entry's group vector: begin and count. 0: implausible. */
static int join_gravn_groups(uintptr_t entry,unsigned char **sector,uintptr_t *begin,uint64_t *n) {
    uintptr_t g[2];
    if(!join_read((void *)entry,sector,8) || !*sector || !join_read((void *)(entry+8),g,16) || g[1]<g[0] || (g[1]-g[0])%JOIN_GRAVN_GROUP ||
       (g[1]-g[0])/JOIN_GRAVN_GROUP>65536u) return 0;
    *begin=g[0];*n=(g[1]-g[0])/JOIN_GRAVN_GROUP;return 1;
}
static uint64_t join_gravn_key(const unsigned char *component) {
    uint64_t runtime=0;unsigned klass;
    return component && join_read(component+8,&runtime,8) && runtime?join_key(runtime,&klass):0;
}
static void join_capture_gravn(join_tables *t,unsigned char *w) {
    uintptr_t v[2];join_gravn_cap=0;
    join_grav_lock(w+0x6890,1);
    if(join_read(w+0x68b8,v,sizeof(v)) && v[1]>=v[0] && !((v[1]-v[0])&31u) && (v[1]-v[0])/32u<=65536u)
        for(uintptr_t p=v[0];p<v[1];p+=32) {
            unsigned char *sector=NULL;uintptr_t begin=0;uint64_t n=0,skey;
            if(!join_gravn_groups(p,&sector,&begin,&n)) {++join_cap.grav_fault;continue;}
            if(!(skey=join_gravn_key(sector))) {++join_cap.grav_unmapped;continue;}
            for(uint64_t i=0;i<n;i++) {
                unsigned char grp[JOIN_GRAVN_GROUP],*zone=NULL;uintptr_t nb[3];uint64_t zkey,pushed=0;
                if(!join_read((void *)(begin+i*JOIN_GRAVN_GROUP),grp,JOIN_GRAVN_GROUP)) {++join_cap.grav_fault;continue;}
                memcpy(&zone,grp,8);memcpy(nb,grp+0x38,24);
                if(!(zkey=join_gravn_key(zone))) {++join_cap.grav_unmapped;continue;}
                const uint64_t count=nb[0] && nb[1]>=nb[0] && !((nb[1]-nb[0])&7u)?(nb[1]-nb[0])/8u:0;
                for(uint64_t k=0;k<count && k<4096u;k++) {
                    uint64_t idx=0,nkey;unsigned char *nz=NULL;
                    if(!join_read((void *)(nb[0]+k*8u),&idx,8) || idx>=n || !join_read((void *)(begin+idx*JOIN_GRAVN_GROUP),&nz,8) ||
                       !(nkey=join_gravn_key(nz))) {++join_cap.grav_unmapped;continue;}
                    join_gravn_rec r={skey,zkey,nkey,0,0};
                    if(!JOIN_PUSH(t->gravn,t->ngravn,join_gravn_cap,r)) {++join_cap.grav_fault;break;}
                    ++pushed;
                }
                if(!pushed) {join_gravn_rec r={skey,zkey,0,JOIN_GRAVN_NONE,0};if(!JOIN_PUSH(t->gravn,t->ngravn,join_gravn_cap,r)) ++join_cap.grav_fault;}
            }
        }
    else ++join_cap.grav_fault;
    join_grav_lock(w+0x6890,0);
    if(t->ngravn) qsort(t->gravn,t->ngravn,sizeof(*t->gravn),join_gravn_cmp);
    {   /* one record per (sector, zone, neighbour): two zones with one key would make two groups one; both are dropped (counted) */
        uint32_t k=0;
        for(uint32_t i=0;i<t->ngravn;i++) {
            if(k && !join_gravn_cmp(&t->gravn[k-1],&t->gravn[i])) {++join_cap.grav_fault;continue;}
            t->gravn[k++]=t->gravn[i];
        }
        t->ngravn=k;
    }
}
/* Sorted strictly by (sector, zone, neighbour), a sector and a zone, NONE alone for its zone with neighbour 0, else a neighbour. */
static int join_gravn_shape(const join_gravn_rec *g,uint32_t n) {
    for(uint32_t i=0;i<n;i++) {
        const join_gravn_rec *r=&g[i];
        if(!r->sector || !r->zone || r->reserved || (r->flags&~JOIN_GRAVN_NONE) || !r->neighbour!=!!(r->flags&JOIN_GRAVN_NONE)) return 0;
        if(i && join_gravn_cmp(&g[i-1],r)>=0) return 0;
        if((r->flags&JOIN_GRAVN_NONE) && ((i && g[i-1].sector==r->sector && g[i-1].zone==r->zone) ||
                                           (i+1<n && g[i+1].sector==r->sector && g[i+1].zone==r->zone))) return 0;
    }
    return 1;
}
/* ---- version 12: GRAVQ (the worker's private command list at T; report 1.16) ----
 * AI::ActiveGravidarUnit slot 7 0x10acd70 posts GravidarHasScannedEvent (0x18 bytes from SMem: vtable, 0, 0) by FUN_140998430 into the
 * worker's list +0x69b0 (a std::list: the sentinel pointer, the size +0x69b8; a node of 0x40 bytes: next, prev, the kind 3 (a dword),
 * the source's runtime ID, the event, the post time, 0, 0). The publish 0x10b12c0 (the top of the MoveWorker's next frame, the worker
 * ready) splices the list into U::g_EventCommandQueue 0x39943a8 (0x99ab20), which U::EventSystem_FlushCommandQueue 0x99abe0 delivers.
 * So the events of the pass of T are in that list at the boundary: the session delivers them at T+1, a joiner never had them (j1-42:
 * 137 script rows of 634 on the session only, fight.attack.object.* 'Station Attacked' handlers on event_gravidar_has_scanned). The
 * capture walks the list under the ready lock +0x6960 with the worker idle (ready +0x6988 = 1); the admission appends the rebuilt
 * nodes to the joiner's list the same way, so its publish of T+1 splices them. */
#define JOIN_GRAVQ_LIST 0x69b0u
#define JOIN_GRAVQ_EVENT_VT 0x2baf7d8u   /* U::GravidarHasScannedEvent::vftable */
#define JOIN_GRAVQ_QUEUE 0x39943a8u      /* U::g_EventCommandQueue (the sentinel pointer, the size +8) */
#define JOIN_GRAVQ_MAX 65536u
static void join_gravq_cs_native(void *cs,int enter) {if(enter) EnterCriticalSection((CRITICAL_SECTION *)cs);else LeaveCriticalSection((CRITICAL_SECTION *)cs);}
static void (*join_gravq_cs)(void *,int)=join_gravq_cs_native;   /* tests replace it */
static uint32_t join_gravq_cap;
static void join_capture_gravq(join_tables *t,unsigned char *w) {
    uint64_t global=0;unsigned char *sentinel=NULL,*node;uint64_t size=0;
    join_gravq_cap=0;join_cap.gravq_busy=0;
    if(join_read(image+JOIN_GRAVQ_QUEUE+8u,&global,8)) join_cap.gravq_global=(uint32_t)(global>0xffffffffull?0xffffffffull:global);
    if(!join_read(w+JOIN_GRAVQ_LIST,&sentinel,8) || !sentinel) return;
    join_gravq_cs(w+AI_SCHED_W_READY_LOCK,1);
    if(*(volatile unsigned char *)(w+AI_SCHED_W_READY)!=1 || !join_read(w+JOIN_GRAVQ_LIST+8u,&size,8) || size>JOIN_GRAVQ_MAX ||
       !join_read(sentinel,&node,8)) {++join_cap.gravq_busy;join_gravq_cs(w+AI_SCHED_W_READY_LOCK,0);return;}
    for(uint64_t i=0;i<size && node && node!=sentinel;i++) {
        uint64_t q[8],e[3]={0};join_gravq_rec r;memset(&r,0,sizeof(r));unsigned klass;
        if(!join_read(node,q,sizeof(q))) {++join_cap.grav_fault;break;}
        r.index=(uint32_t)i;r.kind=(uint32_t)q[2];memcpy(&r.time,&q[5],8);
        if(q[3] && !(r.portable=join_key(q[3],&klass))) r.flags|=JOIN_GRAVQ_UNMAPPED;
        if(q[4] && join_read((void *)(uintptr_t)q[4],e,sizeof(e)) && e[0]>=(uintptr_t)image && e[0]-(uintptr_t)image<JOIN_FLIGHT_IMAGE)
            r.event=(uint32_t)(e[0]-(uintptr_t)image);
        r.q[0]=e[1];r.q[1]=e[2];
        if(r.kind!=3u || (q[2]>>32) || q[6] || q[7] || !q[3] || r.event!=JOIN_GRAVQ_EVENT_VT || e[1] || e[2]) r.flags|=JOIN_GRAVQ_OTHER;
        if(!JOIN_PUSH(t->gravq,t->ngravq,join_gravq_cap,r)) {++join_cap.grav_fault;break;}
        node=(unsigned char *)(uintptr_t)q[0];
    }
    join_gravq_cs(w+AI_SCHED_W_READY_LOCK,0);
}
/* By index from 0, known flags, a source without a portable ID only with UNMAPPED. */
static int join_gravq_shape(const join_gravq_rec *g,uint32_t n) {
    for(uint32_t i=0;i<n;i++) {
        const join_gravq_rec *r=&g[i];
        if(r->index!=i || (r->flags&~3u) || !r->portable!=!!(r->flags&JOIN_GRAVQ_UNMAPPED)) return 0;
    }
    return 1;
}
/* ---- version 12: JOBE (the job engine's schedule; report 1.16) ---- */
#define JOIN_JOBE_ENGINE 0x6cf2300u   /* U::JobEngine * (created by 0x9e8270, 0x68 bytes) */
#define JOIN_JOBE_VT 0x2c49ae0u
#define JOIN_JOBE_ASSIGN 0x975a0u
/* The load's JobEngine::Init (0xf0b340, from 0x9eb8cb) runs a full job pass when the global job switch 0x2f4b6de is on: the
 * session's own load ran it at its boot, its schedule since rides JOBE, and the joiner's would spawn job ships the session never had
 * (j1-43: seven, with parts and crew). UniverseClass::Import sets the switch from the save's <jobs enabled> before the first
 * component of <jobs> (GameClass::Load writes it only for a new game); every import the joiner observes after that turns it off
 * again (counted), so Init passes no job and leaves the engine idle (+9 0). The admission sets the switch, +8 and +9 as the session
 * had them, with its schedule, before the first frame. */
#define JOIN_JOBE_SWITCH 0x2f4b6deu
static unsigned char *join_jobe_switch_test;   /* tests: a synthetic switch */
static volatile unsigned char *join_jobe_switch(void) {return join_jobe_switch_test?join_jobe_switch_test:(volatile unsigned char *)(image+JOIN_JOBE_SWITCH);}
static unsigned char *join_jobe_engine(void);
static void join_jobe_hold(void) {
    volatile unsigned char *g=join_jobe_switch();unsigned char *e=join_jobe_engine();
    if(*g) {*g=0;InterlockedIncrement64((volatile LONG64 *)&join_rx.jobe_held);}
    /* j1-48: the engine object outlives the main menu's game with +9 (running) set, so its update ran on the first frame after the
     * load without any Init: off as well (its own 0x100 in the count) */
    if(e && ((volatile unsigned char *)e)[9]) {((volatile unsigned char *)e)[9]=0;InterlockedAdd64((volatile LONG64 *)&join_rx.jobe_held,0x100);}
}
static unsigned char *join_jobe_engine_test;   /* tests: a synthetic engine */
static unsigned char *join_jobe_engine(void) {
    unsigned char *e=join_jobe_engine_test;uintptr_t vt=0;
    if(!e && !join_read(image+JOIN_JOBE_ENGINE,&e,8)) return NULL;
    return e && join_read(e,&vt,8) && vt==(uintptr_t)image+JOIN_JOBE_VT?e:NULL;
}
static void *join_jobe_assign_native(void *s,const char *text,size_t n) {return ((void *(*)(void *,const char *,size_t))(void *)(image+JOIN_JOBE_ASSIGN))(s,text,n);}
static void *(*join_jobe_assign)(void *,const char *,size_t)=join_jobe_assign_native;   /* tests replace it */
static int join_jobe_read(unsigned char *e,join_jobe_rec *r) {
    unsigned char b[2];uint64_t size=0,capacity=0;char *text=NULL;memset(r,0,sizeof(*r));
    if(!join_read(e+8,b,2) || !join_read(e+0x10,&r->last,8) || !join_read(e+0x28,&size,8) || !join_read(e+0x30,&capacity,8)) return 0;
    r->flags=(b[0]?JOIN_JOBE_ENABLED:0u)|(b[1]?JOIN_JOBE_RUNNING:0u)|(*join_jobe_switch()?JOIN_JOBE_GATE:0u);
    if(size>=JOIN_JOBE_CURSOR) return 0;
    text=capacity>15u?NULL:(char *)(e+0x18);
    if(!text && !join_read(e+0x18,&text,8)) return 0;
    if(size && !join_read(text,r->cursor,(size_t)size)) return 0;
    r->len=(uint32_t)size;return 1;
}
static void join_capture_jobe(join_tables *t) {
    unsigned char *e=join_jobe_engine();join_jobe_rec r;
    if(!e) return;
    if(!join_jobe_read(e,&r)) {++join_cap.grav_fault;return;}
    if(!(t->jobe=malloc(sizeof(r)))) return;
    t->jobe[0]=r;t->njobe=1;
}
/* At most one, known flags, a printable cursor shorter than the field, zero behind it. */
static int join_jobe_shape(const join_jobe_rec *j,uint32_t n) {
    if(n>1u) return 0;
    for(uint32_t i=0;i<n;i++) {
        if((j[i].flags&~7u) || j[i].len>=JOIN_JOBE_CURSOR) return 0;
        for(uint32_t k=0;k<JOIN_JOBE_CURSOR;k++) {const unsigned char c=(unsigned char)j[i].cursor[k];if(k<j[i].len?c<33u || c>126u:c!=0u) return 0;}
    }
    return 1;
}
/* Census, read-only (report 1.16): the events waiting in their sources at T. U::EventSource::PostEvent 0x99a3f0 delivers a post at
 * once only when asked to and due; else it inserts the event into the source's own time-ordered tree (source+0x20, created on demand:
 * root +8, leftmost +0x20, count +0x28; nodes parent, left, right, colour, time +0x20, event +0x28) and the source into
 * U::g_EventSourceSchedule (root 0x2f6a9c8, leftmost 0x2f6a9e0, count 0x2f6a9e8; nodes keyed (time +0x20, source +0x28)). The save
 * keeps none of them (j1-47: the order ready/cancelled events the session's job ships got at 634). Counted by event class. */
#define JOIN_EVQ_ROOT 0x2f6a9c8u
#define JOIN_EVQ_LEFT 0x2f6a9e0u
#define JOIN_EVQ_COUNT 0x2f6a9e8u
static unsigned char *join_evq_head_test;   /* tests: a synthetic header (root +0, leftmost +0x18, count +0x20) */
static unsigned char *join_evq_at(uint32_t at) {return join_evq_head_test?join_evq_head_test+(at-JOIN_EVQ_ROOT):image+at;}
static unsigned char *join_evq_next(unsigned char *node) {
    unsigned char *n=NULL,*p=NULL;
    if(!join_read(node+0x10,&n,8)) return NULL;
    if(n) {unsigned char *l=NULL;while(join_read(n+8,&l,8) && l) n=l;return n;}
    for(unsigned guard=0;guard<64;guard++) {
        if(!join_read(node,&p,8) || !p) return NULL;
        unsigned char *pr=NULL;if(!join_read(p+0x10,&pr,8)) return NULL;
        if(pr!=node) return p;
        node=p;
    }
    return NULL;
}
static void join_evq_census(double now,uint32_t *sources,uint32_t *events,uint32_t *due,uint32_t *fault,char *top,size_t cap) {
    enum {TOP=12};uint32_t vt[TOP]={0},cnt[TOP]={0};double lo[TOP],hi[TOP];uint64_t count=0;unsigned char *node=NULL;
    *sources=*events=*due=*fault=0;top[0]=0;
    if(!join_read(join_evq_at(JOIN_EVQ_COUNT),&count,8) || !count || !join_read(join_evq_at(JOIN_EVQ_LEFT),&node,8)) return;
    for(uint64_t i=0;i<count && i<200000u && node;i++,node=join_evq_next(node)) {
        unsigned char *src=NULL,*q=NULL,*e=NULL;uint64_t n=0;
        if(!join_read(node+0x28,&src,8) || !src || !join_read(src+0x20,&q,8) || !q || !join_read(q+0x28,&n,8) || !join_read(q+0x20,&e,8)) {++*fault;continue;}
        ++*sources;
        for(uint64_t k=0;k<n && k<4096u && e;k++,e=join_evq_next(e)) {
            unsigned char *ev=NULL;uintptr_t v=0;double t=0;
            if(!join_read(e+0x28,&ev,8) || !ev || !join_read(ev,&v,8) || !join_read(e+0x20,&t,8)) {++*fault;break;}
            ++*events;if(t<=now+0.04) ++*due;
            uint32_t rva=v>=(uintptr_t)image && v-(uintptr_t)image<JOIN_FLIGHT_IMAGE?(uint32_t)(v-(uintptr_t)image):0u;
            for(unsigned j=0;j<TOP;j++) {
                if(cnt[j] && vt[j]==rva) {++cnt[j];if(t<lo[j]) lo[j]=t;if(t>hi[j]) hi[j]=t;break;}
                if(!cnt[j]) {vt[j]=rva;cnt[j]=1;lo[j]=hi[j]=t;break;}
            }
        }
    }
    size_t at=0;
    for(unsigned j=0;j<TOP && cnt[j] && at+48<cap;j++)
        at+=(size_t)snprintf(top+at,cap-at,"%s%x*%u@%.3f-%.3f",j?",":"",vt[j],cnt[j],lo[j]-now,hi[j]-now);
}
/* native_gate_join_evq_dump (diagnostic, default off): every pending event, one JOIN_EVQ_ROW per event (side s at the capture, j at
 * the admission; source as a portable key or r:runtime, class RVA, time relative to T), written once to the crash note and log. */
static int join_evq_dump_on;
static void join_evq_dump(char side,double now) {
    uint64_t count=0;unsigned char *node=NULL;size_t cap=4u<<20,at=0;char *out;
    if(!join_evq_dump_on || !join_read(join_evq_at(JOIN_EVQ_COUNT),&count,8) || !count || !join_read(join_evq_at(JOIN_EVQ_LEFT),&node,8) || !(out=malloc(cap))) return;
    uint32_t row=0;
    for(uint64_t i=0;i<count && i<200000u && node;i++,node=join_evq_next(node)) {
        unsigned char *src=NULL,*q=NULL,*e=NULL;uint64_t n=0,runtime=0;unsigned klass;
        if(!join_read(node+0x28,&src,8) || !src || !join_read(src+8,&runtime,8) || !join_read(src+0x20,&q,8) || !q || !join_read(q+0x28,&n,8) ||
           !join_read(q+0x20,&e,8)) continue;
        uint64_t key=runtime?join_key(runtime,&klass):0;
        for(uint32_t k=0;k<n && k<4096u && e && at+160<cap;k++,e=join_evq_next(e)) {
            unsigned char *ev=NULL;uintptr_t v=0;double t=0;
            if(!join_read(e+0x28,&ev,8) || !ev || !join_read(ev,&v,8) || !join_read(e+0x20,&t,8)) break;
            uint32_t rva=v>=(uintptr_t)image && v-(uintptr_t)image<JOIN_FLIGHT_IMAGE?(uint32_t)(v-(uintptr_t)image):0u;
            at+=(size_t)snprintf(out+at,cap-at,"[X4LAN] NATIVE_GATE JOIN_EVQ_ROW side=%c row=%u k=%u src=%s%llx cls=%x t=%.9f\r\n",side,row++,k,
                                 key?"":"r:",(unsigned long long)(key?key:runtime),rva,t-now);
        }
    }
    if(at) crash_note_write(out,(int)at);
    free(out);
}
/* ---- version 13: EVQT (report 1.17) ---- */
#define JOIN_EVQT_POST 0x99a3f0u     /* U::EventSource::PostEvent(source, event, double time, char immediate) */
#define JOIN_EVQT_REMOVE 0x99a870u   /* (source, int type, double before; -1: all) */
#define JOIN_EVQT_MAX 400000u
/* The classes the joiner rebuilds: no payload (0x18 bytes from SMem: vtable, 0, the post's context) and posted at a time of their own,
 * the load scheduling its own (j1-50). */
static const uint32_t join_evqt_rebuilt[]={0x2baf650u /* UpdateEngineParametersEvent 0x20f */,0x2ac1710u /* UpdateBuildEvent 0x20d */,
                                           0x2ac17b8u /* UpdateTradeOffersEvent 0x215 */};
static int join_evqt_is_rebuilt(uint32_t cls) {
    for(unsigned i=0;i<sizeof(join_evqt_rebuilt)/sizeof(*join_evqt_rebuilt);i++) if(join_evqt_rebuilt[i]==cls) return 1;
    return 0;
}
/* The type ID a class's slot 1 returns (mov eax, imm32; ret), 0 when it is not that. */
static uint32_t join_evqt_type_native(uint32_t cls) {
    uintptr_t slot=0;unsigned char code[6];uint32_t id=0;
    if(!cls || !join_read(image+cls+8,&slot,8) || slot<(uintptr_t)image || !join_read((void *)slot,code,6) || code[0]!=0xb8 || code[5]!=0xc3) return 0;
    memcpy(&id,code+1,4);return id;
}
static uint32_t (*join_evqt_type)(uint32_t)=join_evqt_type_native;   /* tests replace it */
static void join_evqt_post_native(void *source,void *event,double time,char immediate) {
    ((void (*)(void *,void *,double,char))(void *)(image+JOIN_EVQT_POST))(source,event,time,immediate);
}
static void join_evqt_remove_native(void *source,int type,double before) {
    ((void (*)(void *,int,double))(void *)(image+JOIN_EVQT_REMOVE))(source,type,before);
}
static void (*join_evqt_post)(void *,void *,double,char)=join_evqt_post_native;   /* tests replace them */
static void (*join_evqt_remove)(void *,int,double)=join_evqt_remove_native;
static int join_evqt_cmp(const void *a,const void *b) {
    const join_evqt_rec *x=a,*y=b;
    if(x->portable!=y->portable) return x->portable<y->portable?-1:1;
    if(x->cls!=y->cls) return x->cls<y->cls?-1:1;
    if(x->time!=y->time) {double p,q;memcpy(&p,&x->time,8);memcpy(&q,&y->time,8);return p<q?-1:p>q?1:(x->time<y->time?-1:1);}
    return x->index<y->index?-1:x->index>y->index;
}
static int64_t join_pord_walk(int drop,uint32_t *v,uint32_t *n);   /* lockstep_join.h (PORD) */
static int64_t join_pord_saver;static uint32_t join_pord_svt[4],join_pord_svtn[4];
static void join_capture_evqt(join_tables *t) {
    uint64_t count=0;unsigned char *node=NULL;uint32_t cap=0;
    join_pord_saver=join_pord_walk(0,join_pord_svt,join_pord_svtn);   /* PORD: the session's queued Path orders at T (counted only) */
    if(!join_read(join_evq_at(JOIN_EVQ_COUNT),&count,8) || !count || !join_read(join_evq_at(JOIN_EVQ_LEFT),&node,8)) return;
    for(uint64_t i=0;i<count && i<200000u && node;i++,node=join_evq_next(node)) {
        unsigned char *src=NULL,*q=NULL,*e=NULL;uint64_t n=0,runtime=0;unsigned klass;
        if(!join_read(node+0x28,&src,8) || !src || !join_read(src+8,&runtime,8) || !join_read(src+0x20,&q,8) || !q || !join_read(q+0x28,&n,8) ||
           !join_read(q+0x20,&e,8)) {++join_cap.evq_fault;continue;}
        const uint64_t key=runtime?join_key(runtime,&klass):0;
        for(uint32_t k=0;k<n && k<4096u && e;k++,e=join_evq_next(e)) {
            unsigned char *ev=NULL;uintptr_t v=0;join_evqt_rec r;memset(&r,0,sizeof(r));
            if(!join_read(e+0x28,&ev,8) || !ev || !join_read(ev,&v,8) || !join_read(e+0x20,&r.time,8)) {++join_cap.evq_fault;break;}
            r.portable=key;r.flags=key?0u:JOIN_EVQT_UNMAPPED;r.index=k;
            r.cls=v>=(uintptr_t)image && v-(uintptr_t)image<JOIN_FLIGHT_IMAGE?(uint32_t)(v-(uintptr_t)image):0u;r.type=join_evqt_type(r.cls);
            if(t->nevqt>=JOIN_EVQT_MAX || !JOIN_PUSH(t->evqt,t->nevqt,cap,r)) {++join_cap.evq_fault;return;}
        }
    }
    if(t->nevqt>1) qsort(t->evqt,t->nevqt,sizeof(*t->evqt),join_evqt_cmp);
}
/* Sorted by (portable, class, time, place), known flags, a portable source exactly without UNMAPPED. */
static int join_evqt_shape(const join_evqt_rec *v,uint32_t n) {
    for(uint32_t i=0;i<n;i++) {
        if((v[i].flags&~JOIN_EVQT_UNMAPPED) || !v[i].portable!=!!(v[i].flags&JOIN_EVQT_UNMAPPED) || (i && join_evqt_cmp(&v[i-1],&v[i])>0)) return 0;
    }
    return 1;
}
/* ---- version 13: GRAVD (report 1.17) ---- */
static int join_is_ship_vt(uintptr_t vt) {
    static const uint32_t ships[]={0x2b7feb8u /* ShipSmall */,0x2b82440u /* ShipMedium */,0x2b9bff8u /* ShipLarge */,
                                   0x2b99a88u /* ShipExtraLarge */,0x2b7d738u /* ShipExtraSmall */};
    for(unsigned i=0;i<sizeof(ships)/sizeof(*ships);i++) if(vt==(uintptr_t)image+ships[i]) return 1;
    return 0;
}
static int join_gravd_cmp(const void *a,const void *b) {
    const join_gravd_rec *x=a,*y=b;
    if(x->portable!=y->portable) return x->portable<y->portable?-1:1;
    return x->rel<y->rel?-1:x->rel>y->rel;
}
static void join_capture_gravd(join_tables *t) {
    const uint32_t cur=join_view_index();uint32_t cap=0;
    for(uint32_t i=0;i<t->nids;i++) {
        unsigned char *object=join_object(t->ids[i].runtime);uintptr_t vt=0;
        if(!object || !join_read(object,&vt,8) || !join_is_ship_vt(vt)) continue;
        for(uint32_t rel=0;rel<2u;rel++) {
            join_gravd_rec r;memset(&r,0,sizeof(r));unsigned char *parent=NULL;uint64_t runtime=0;unsigned klass;
            r.portable=t->ids[i].portable;r.rel=rel;
            if(!join_read(object+JOIN_VIEW_SLOTS+(size_t)(cur^rel)*0x80u,&parent,8)) continue;
            if(!parent) r.flags=JOIN_GRAVD_NONE;
            else if(!join_read(parent+8,&runtime,8) || !runtime || !(r.parent=join_key(runtime,&klass))) r.flags=JOIN_GRAVD_UNMAPPED;
            if(!JOIN_PUSH(t->gravd,t->ngravd,cap,r)) return;
        }
    }
    if(t->ngravd>1) qsort(t->gravd,t->ngravd,sizeof(*t->gravd),join_gravd_cmp);
}
/* ---- version 13: GRAVB (section 60) ----
 * The Gravidar's mapped bounds maps (JOIN_GRAVB rows): j1-161, after the load the joiner's three maps are empty where the session's
 * hold its 153 sectors; its Gravidar refills them fifteen sectors a step (16, 31, .. 153 at 644) and posts MappedBoundsChangedEvent
 * to the Player for each new one (636..644, the session none), equal to the session's once full. GRAVB carries the three maps by
 * role (the read buffer, the write buffer by their selectors, the base); the admission inserts each entry into the joiner's map of
 * that role through the map's own operator[] 0x10beb90 (the Gravidar thread idle at the boundary, as for GRAV) and writes x, y, r.
 * Tree map: header +8 root, +0x20 leftmost, +0x28 size; node +0 parent, +8 left, +0x10 right, +0x20 the ID, +0x28 x, y, r. */
#define JOIN_GRAVB_MAPS 0x6e10u      /* the two buffers, 0x40 bytes each */
#define JOIN_GRAVB_BASE 0x6e90u
#define JOIN_GRAVB_INSERT 0x10beb90u
#define JOIN_GRAVB_MAX 4096u
static uint64_t join_gravb_written,join_gravb_equal,join_gravb_missing,join_gravb_unmapped;
static void *join_gravb_insert_native(void *map,uint64_t *key) {return ((void *(*)(void *,uint64_t *))(void *)(image+JOIN_GRAVB_INSERT))(map,key);}
static void *(*join_gravb_insert)(void *,uint64_t *)=join_gravb_insert_native;
/* The in-order successor of a node (NULL on a fault; the header at the end). */
static unsigned char *join_gravb_next(unsigned char *head,unsigned char *node) {
    unsigned char *next=NULL;
    if(join_read(node+0x10,&next,8) && next) {   /* the right subtree's leftmost */
        node=next;
        for(unsigned d=0;d<64u && join_read(node+8,&next,8) && next;d++) node=next;
        return node;
    }
    for(unsigned d=0;d<64u;d++) {                 /* up while coming from the right */
        unsigned char *parent=NULL,*right=NULL;
        if(!join_read(node,&parent,8) || !parent) return NULL;
        if(parent==head || !join_read(parent+0x10,&right,8) || right!=node) return parent;
        node=parent;
    }
    return NULL;
}
/* The three maps by role, or 0 when the worker or a selector cannot be read. */
static int join_gravb_maps(unsigned char *maps[JOIN_GRAVB_ROLES]) {
    unsigned char *w=join_grav_worker();uint32_t rsel=0,wsel=0;
    if(!w || !join_read(image+JOIN_GRAVB_READ,&rsel,4) || !join_read(image+JOIN_GRAVB_WRITE,&wsel,4) || rsel>1u || wsel>1u) return 0;
    maps[0]=w+JOIN_GRAVB_MAPS+0x40u*rsel;maps[1]=w+JOIN_GRAVB_MAPS+0x40u*wsel;maps[2]=w+JOIN_GRAVB_BASE;
    return 1;
}
static int join_gravb_cmp(const void *a,const void *b) {
    const join_gravb_rec *x=a,*y=b;
    if(x->portable!=y->portable) return x->portable<y->portable?-1:1;
    return x->role<y->role?-1:x->role>y->role;
}
static void join_capture_gravb(join_tables *t) {
    unsigned char *maps[JOIN_GRAVB_ROLES];uint32_t cap=0;
    join_gravb_unmapped=0;
    if(!join_gravb_maps(maps)) return;
    for(uint32_t role=0;role<JOIN_GRAVB_ROLES;role++) {
        unsigned char *head=maps[role],*node=NULL;uint64_t size=0;
        if(!join_read(head+0x28,&size,8) || !join_read(head+0x20,&node,8) || size>JOIN_GRAVB_MAX) continue;
        for(uint64_t k=0;k<size && node && node!=head;k++,node=join_gravb_next(head,node)) {
            join_gravb_rec r;uint64_t key=0;unsigned klass;memset(&r,0,sizeof(r));
            if(!join_read(node+0x20,&key,8) || !join_read(node+0x28,r.v,12)) break;
            r.role=role;r.portable=key?join_key(key,&klass):0;
            if(!r.portable) {++join_gravb_unmapped;continue;}
            if(!JOIN_PUSH(t->gravb,t->ngravb,cap,r)) return;
        }
    }
    if(t->ngravb>1) qsort(t->gravb,t->ngravb,sizeof(*t->gravb),join_gravb_cmp);
    {uint32_t k=0;for(uint32_t i=0;i<t->ngravb;i++) {if(k && !join_gravb_cmp(&t->gravb[k-1],&t->gravb[i])) continue;t->gravb[k++]=t->gravb[i];}t->ngravb=k;}
}
static void join_admit_gravb(const join_tables *s,const join_tables *mine) {
    unsigned char *maps[JOIN_GRAVB_ROLES];
    join_gravb_written=join_gravb_equal=join_gravb_missing=0;
    if(!s->ngravb) return;
    if(!join_gravb_maps(maps)) {join_gravb_missing+=s->ngravb;return;}
    for(uint32_t i=0;i<s->ngravb;i++) {
        const join_gravb_rec *r=&s->gravb[i];uint64_t runtime=join_runtime_of(mine,r->portable);uint32_t own[3];
        unsigned char *v=runtime?join_gravb_insert(maps[r->role],&runtime):NULL;
        if(!v) {++join_gravb_missing;continue;}
        if(join_read(v,own,12) && !memcmp(own,r->v,12)) {++join_gravb_equal;continue;}
        if(join_poke((uintptr_t)v,r->v,12)) ++join_gravb_written; else ++join_gravb_equal;
    }
}
/* ---- version 13: VIEWL (section 61) ----
 * j1-161 (actor 5's raw object, session against joiner at 646..650): +0x3c8 and +0x3c9 0x0101 on the session, 0 on the joiner (the
 * lockstep session's slot ships alike). They are the object's live view and player view flags: its listener slots 0xbb8 (ObjectEntered-
 * LiveView, 0x6d1020: registers the object with the live view manager through 0x73f360, vfunc 0x17b0, +0x3c8 = 1), 0xbc0 (left: the
 * flag cleared and unregistered), 0xbd0 (EnteredPlayerView, 0x6d1130: 0x7075d0, +0x3c9 = 1) and 0xbd8 (left, 0x6d11b0). The radar
 * posts those events on a change of its units' view states (GRAV +0x54/+0x58), which GRAV now carries, so a joiner never gets them for
 * an object already in view at T: neither the flags nor the registration. VIEWL carries the flags of every object of the handler family
 * (slot 0xbb8 the base 0x6d1020) with either set; with native_gate_join_viewl (default off) the admission runs the object's own enter or
 * leave handler where the joiner's flag differs (at T's game time). Status viewl=on:entered:left:equal:other:missing. */
#define JOIN_VIEWL_FLAGS 0x3c8u
#define JOIN_VIEWL_SLOT 0xbb8u            /* entered live view; +8 left, +0x18 entered player view, +0x20 left */
#define JOIN_VIEWL_BASE 0x6d1020u
static int join_viewl_on;
static uint64_t join_viewl_entered,join_viewl_left,join_viewl_equal,join_viewl_other,join_viewl_missing;
/* The object's vtable when it is of the handler family (its slot JOIN_VIEWL_SLOT the base handler), else NULL. */
static uintptr_t *join_viewl_vtable(unsigned char *object) {
    uintptr_t *vt=NULL,fn=0;
    if(!object || !join_read(object,&vt,8) || !vt || (uintptr_t)vt<(uintptr_t)image || (uintptr_t)vt-(uintptr_t)image>=JOIN_FLIGHT_IMAGE ||
       !join_read((unsigned char *)vt+JOIN_VIEWL_SLOT,&fn,8) || fn!=(uintptr_t)image+JOIN_VIEWL_BASE) return NULL;
    return vt;
}
static int join_viewl_cmp(const void *a,const void *b) {
    uint64_t x=((const join_viewl_rec *)a)->portable,y=((const join_viewl_rec *)b)->portable;return x<y?-1:x>y;
}
static void join_capture_viewl(join_tables *t) {
    uint32_t cap=0;
    for(uint32_t i=0;i<t->nids;i++) {
        unsigned char *object=t->ids[i].portable?join_object(t->ids[i].runtime):NULL,f[2]={0,0};
        if(!join_viewl_vtable(object) || !join_read(object+JOIN_VIEWL_FLAGS,f,2) || !(f[0]|f[1]) || f[0]>1u || f[1]>1u) continue;
        join_viewl_rec r;memset(&r,0,sizeof(r));r.portable=t->ids[i].portable;r.live=f[0];r.player=f[1];
        if(!JOIN_PUSH(t->viewl,t->nviewl,cap,r)) return;
    }
    if(t->nviewl>1) qsort(t->viewl,t->nviewl,sizeof(*t->viewl),join_viewl_cmp);
}
static void join_viewl_call(uintptr_t *vt,unsigned slot,unsigned char *object,double time) {
    uintptr_t fn=0;
    if(join_read((unsigned char *)vt+slot,&fn,8) && fn>=(uintptr_t)image && fn-(uintptr_t)image<JOIN_FLIGHT_IMAGE)
        ((void (*)(void *,void *,double))(void *)fn)(object,object,time);
}
/* One object to the session's flags (want), entering before leaving each view. */
static void join_viewl_apply(unsigned char *object,uintptr_t *vt,const unsigned char want[2],double time) {
    unsigned char have[2]={0,0};
    if(!join_read(object+JOIN_VIEWL_FLAGS,have,2)) {++join_viewl_missing;return;}
    if(have[0]>1u || have[1]>1u) {++join_viewl_other;return;}
    if(have[0]==want[0] && have[1]==want[1]) {++join_viewl_equal;return;}
    if(want[0]!=have[0]) {join_viewl_call(vt,JOIN_VIEWL_SLOT+(want[0]?0u:8u),object,time);if(want[0]) ++join_viewl_entered; else ++join_viewl_left;}
    if(want[1]!=have[1]) {join_viewl_call(vt,JOIN_VIEWL_SLOT+(want[1]?0x18u:0x20u),object,time);if(want[1]) ++join_viewl_entered; else ++join_viewl_left;}
}
/* The session's objects in portable ID order (the registrations the handlers make follow it), then the joiner's own flags that the
 * session does not have, left in its ID table's order. */
static void join_admit_viewl(const join_tables *s,const join_tables *mine) {
    double time=0;memcpy(&time,&s->gate.game,8);
    join_viewl_entered=join_viewl_left=join_viewl_equal=join_viewl_other=join_viewl_missing=0;
    if(!join_viewl_on) return;
    for(uint32_t i=0;i<s->nviewl;i++) {
        unsigned char *object=join_object(join_runtime_of(mine,s->viewl[i].portable));uintptr_t *vt=join_viewl_vtable(object);
        const unsigned char want[2]={s->viewl[i].live,s->viewl[i].player};
        if(!vt) {++join_viewl_missing;continue;}
        join_viewl_apply(object,vt,want,time);
    }
    for(uint32_t i=0;i<mine->nids;i++) {
        unsigned char *object=mine->ids[i].portable?join_object(mine->ids[i].runtime):NULL,have[2]={0,0};uintptr_t *vt=join_viewl_vtable(object);
        if(!vt || !join_read(object+JOIN_VIEWL_FLAGS,have,2) || !(have[0]|have[1])) continue;
        uint32_t lo=0,hi=s->nviewl;
        while(lo<hi) {uint32_t mid=lo+(hi-lo)/2u;if(s->viewl[mid].portable<mine->ids[i].portable) lo=mid+1u;else hi=mid;}
        if(lo<s->nviewl && s->viewl[lo].portable==mine->ids[i].portable) continue;   /* the first pass */
        const unsigned char none[2]={0,0};
        join_viewl_apply(object,vt,none,time);
    }
}
/* By (ship, view) each once, view 0 or 1, known flags, a parent exactly without NONE or UNMAPPED. */
static int join_gravd_shape(const join_gravd_rec *g,uint32_t n) {
    for(uint32_t i=0;i<n;i++) {
        const join_gravd_rec *r=&g[i];
        if(!r->portable || r->rel>1u || (r->flags&~3u) || (r->flags==3u) || !r->parent!=!!r->flags || (i && join_gravd_cmp(&g[i-1],r)>=0)) return 0;
    }
    return 1;
}
/* native_gate_join_watch (diagnostic, default off; report 1.18): up to 8 objects by portable ID, one JOIN_WATCH row per object and
 * ancestor (up to the sector) at the saver's capture (side s), the joiner's admission after GRAVD (j) and after the steps T+1 and T+2
 * on every side (t, u): what the radar's gather 0x10b34c0 reads. It walks each zone's published children (the view cache block
 * object+0xe0+view*0x80: parent +0, children vector +0x10/+0x18, flags +0x3c) through 0x85dbe0, into a child only when it can contain
 * objects (slot 0x1340 through the static filter 0x2f6a650), takes a child when it is a ship (IsA 0x49 through slot 0x11e8) not docked
 * internally (Ship slot 1005: the cached parent IsA 0x28 and its slot 0x13a0), and drops one whose live flags +0x1e4 hold 0x20000 (no
 * parent). Per level: the live parent (+0x70), live flags, can-contain, and per view (rel 0 the current one) the cached parent, the
 * children count, whether the level below is among them and the view flags; on the object itself IsA 0x49, docked internally per view
 * and its radar unit (+0x40..+0x67, +0xa8..+0xc7, the contact counts). Read only apart from the engine's const predicates. */
#define JOIN_WATCH_MAX 8u
#define JOIN_WATCH_LEVELS 7u
#define JOIN_WATCH_PARENT 0x70u
#define JOIN_WATCH_FLAGS 0x1e4u
#define JOIN_WATCH_RAW 0x1400u     /* JOIN_WATCHRAW: the object's first bytes (a ship medium is larger than 0x800) */
#define JOIN_WATCH_REQ 0x318u      /* and what this pointer of it points at (a weapon's fire request) */
#define JOIN_WATCH_RAW_REQ 0x100u
#define JOIN_WATCH_MOV 0x200u      /* and the movement controller (Positional +0x200) */
#define JOIN_WATCH_RAW_MOV 0x200u
#define JOIN_WATCH_RAW_AGENT 0xf90u   /* and a nav or pilot controller's Agent (+0x40, 0xf90 bytes, lockstep_travel_snapshot.h) */
#define JOIN_WATCH_RAW_CMD 0x400u     /* and its command and probe block (+0x48; the probe's due +0x3e0, enable +0x3e8) */
#define JOIN_WATCH_RAW_BEH 0x100u     /* and a steering model's active behaviour (model +0x70; w1z198: the containers' UndockPathBehaviour) */
static struct {uint64_t portable[JOIN_WATCH_MAX],runtime[JOIN_WATCH_MAX];unsigned n;uint64_t tick;int armed;
               unsigned char *volatile object[JOIN_WATCH_MAX];} join_watch;
/* JOIN_WATCHPCF (diagnostic, with native_gate_join_watch): every PropagateCacheFlags 0x3d46b0 call on a watched object whose flags
 * hold 8 (the transform), from its five call sites through the shared thunk slot JOIN_PCF_THUNK (a rel32 CAS each, lockstep_join.h):
 * the step, the call site (return address RVA), the flags and the view index; the rows of each step go out with that step's
 * JOIN_WATCH rows. */
#define JOIN_PCF_FN 0x3d46b0u
#define JOIN_PCF_ROWS 2048u
typedef struct {uint64_t tick;uint32_t site,flags,view,watched;} join_pcf_row;
static join_pcf_row join_pcf_rows[JOIN_PCF_ROWS];
static volatile LONG join_pcf_n;
/* JOIN_WATCHRULE (diagnostic, with the watch): every line-of-fire ruling of the basic shoot controllers (vtable 0x2b7fcc0 slot
 * 0xe8, 0x7e6cb0(controller, hit, target, shooter, flag): 1 clear, 2 blocked, 0 a second ray), which the weapon's request check
 * 0x816d20 asks after its physics ray 0xbc4d0, logged from the first watch dump on through the vtable slot (an 8-byte CAS, put
 * back at the gate's stop) and written with the next dump: the hit, the target and the shooter as keys, the flag and the result.
 * j1-164: actor 5's weapon 0x176ba3 rules clear at 650 on the joiner only (its controller +0x84), one shot more. */
#define JOIN_RULE_SLOT 0x2b7fda8u      /* 0x2b7fcc0 + 0xe8 */
#define JOIN_RULE_FN 0x7e6cb0u
#define JOIN_RULE_ROWS 1024u
typedef struct {uint64_t tick;void *hit,*target,*shooter;uint32_t flag,result;} join_rule_row;
static join_rule_row join_rule_rows[JOIN_RULE_ROWS];
static volatile LONG join_rule_n;static volatile LONG64 join_rule_calls,join_rule_dropped;static int join_rule_installed;
static int join_rule_hook(void *ctl,void *hit,void *target,void *shooter,unsigned char flag) {
    const int r=((int (*)(void *,void *,void *,void *,unsigned char))(void *)(image+JOIN_RULE_FN))(ctl,hit,target,shooter,flag);
    InterlockedIncrement64(&join_rule_calls);
    const LONG k=InterlockedIncrement(&join_rule_n)-1;
    if(k<(LONG)JOIN_RULE_ROWS) {
        join_rule_row *w=&join_rule_rows[k];
        w->tick=stats.issued;w->hit=hit;w->target=target;w->shooter=shooter;w->flag=flag;w->result=(uint32_t)r;
    } else InterlockedIncrement64(&join_rule_dropped);
    return r;
}
static int join_rule_swap(uint64_t expected,uint64_t value) {
    DWORD old,ignored;
    if(!VirtualProtect(image+JOIN_RULE_SLOT,8,PAGE_READWRITE,&old)) return 0;
    const uint64_t actual=(uint64_t)InterlockedCompareExchange64((volatile LONG64 *)(image+JOIN_RULE_SLOT),(LONG64)value,(LONG64)expected);
    VirtualProtect(image+JOIN_RULE_SLOT,8,old,&ignored);
    return actual==expected;
}
/* JOIN_WATCHAIM (diagnostic, with the watch, as JOIN_WATCHRULE): every aim point of the basic shoot controllers (vtable 0x2b7fcc0
 * slot 0xa8, 0x7e72b0(controller, aim out, second out, target, weapon, origin, two weapon floats, five flags), which the request
 * check 0x816d20 asks before its angle; it reads the target through the view cache, 0x3dee20): the target and the weapon as keys,
 * the origin and the aim point (float bits), the result. j1-168: the angles differ from T+1 on the joiner, PEND or not (j1-169). */
#define JOIN_AIM_SLOT 0x2b7fd68u       /* 0x2b7fcc0 + 0xa8 */
#define JOIN_AIM_FN 0x7e72b0u
#define JOIN_AIM_ROWS 1024u
typedef struct {uint64_t tick;void *target,*weapon;uint32_t origin[3],aim[3],result,rot[12];} join_aim_row;   /* rot: the weapon frame's rows (origin+0x10..+0x3f) */
static join_aim_row join_aim_rows[JOIN_AIM_ROWS];
static volatile LONG join_aim_n;static volatile LONG64 join_aim_dropped;static int join_aim_installed;
typedef unsigned char (*join_aim_fn)(void *,float *,float *,void *,void *,const float *,float,float,unsigned char,unsigned char,
                                     unsigned char,unsigned char,unsigned char);
static unsigned char join_aim_hook(void *ctl,float *aim,float *second,void *target,void *weapon,const float *origin,float a,float b,
                                   unsigned char c,unsigned char d,unsigned char e,unsigned char f,unsigned char g) {
    const unsigned char r=((join_aim_fn)(void *)(image+JOIN_AIM_FN))(ctl,aim,second,target,weapon,origin,a,b,c,d,e,f,g);
    const LONG k=InterlockedIncrement(&join_aim_n)-1;
    if(k<(LONG)JOIN_AIM_ROWS) {
        join_aim_row *w=&join_aim_rows[k];
        w->tick=stats.issued;w->target=target;w->weapon=weapon;w->result=r;
        if(!origin || !join_read(origin,w->origin,12)) memset(w->origin,0,12);
        if(!origin || !join_read((const unsigned char *)origin+0x10,w->rot,sizeof(w->rot))) memset(w->rot,0,sizeof(w->rot));
        if(!aim || !join_read(aim,w->aim,12)) memset(w->aim,0,12);
    } else InterlockedIncrement64(&join_aim_dropped);
    return r;
}
static int join_aim_swap(uint64_t expected,uint64_t value) {
    DWORD old,ignored;
    if(!VirtualProtect(image+JOIN_AIM_SLOT,8,PAGE_READWRITE,&old)) return 0;
    const uint64_t actual=(uint64_t)InterlockedCompareExchange64((volatile LONG64 *)(image+JOIN_AIM_SLOT),(LONG64)value,(LONG64)expected);
    VirtualProtect(image+JOIN_AIM_SLOT,8,old,&ignored);
    return actual==expected;
}
static void join_rule_install(void) {
    if(!join_aim_installed && join_aim_swap((uint64_t)(uintptr_t)(image+JOIN_AIM_FN),(uint64_t)(uintptr_t)join_aim_hook)) join_aim_installed=1;
    if(!join_rule_installed && join_rule_swap((uint64_t)(uintptr_t)(image+JOIN_RULE_FN),(uint64_t)(uintptr_t)join_rule_hook)) join_rule_installed=1;
}
static void join_rule_restore(void) {
    if(join_aim_installed && join_aim_swap((uint64_t)(uintptr_t)join_aim_hook,(uint64_t)(uintptr_t)(image+JOIN_AIM_FN))) join_aim_installed=0;
    if(join_rule_installed && join_rule_swap((uint64_t)(uintptr_t)join_rule_hook,(uint64_t)(uintptr_t)(image+JOIN_RULE_FN))) join_rule_installed=0;
}
static volatile LONG64 join_pcf_calls,join_pcf_dropped;
static void join_pcf_native(void *object,uint32_t view,const uint32_t *flags) {
    ((void (*)(void *,uint32_t,const uint32_t *))(void *)(image+JOIN_PCF_FN))(object,view,flags);
}
static __attribute__((noinline)) void join_pcf_hook(void *object,uint32_t view,const uint32_t *flags) {
    InterlockedIncrement64(&join_pcf_calls);
    if(flags && (*flags&8u)) {
        for(unsigned i=0;i<join_watch.n && i<JOIN_WATCH_MAX;i++) {
            if(!object || join_watch.object[i]!=(unsigned char *)object) continue;
            const LONG k=InterlockedIncrement(&join_pcf_n)-1;
            if(k<0 || (ULONG)k>=JOIN_PCF_ROWS) {InterlockedIncrement64(&join_pcf_dropped);break;}
            const uintptr_t ret=(uintptr_t)__builtin_return_address(0);
            join_pcf_rows[k]=(join_pcf_row){stats.issued,ret>=(uintptr_t)image?(uint32_t)(ret-(uintptr_t)image):0u,*flags,view,i};
            break;
        }
    }
    join_pcf_native(object,view,flags);
}
/* JOIN_RPRT (diagnostic, native_gate_join_rprt with native_gate_join_watch; in-window report 57, w1z226): the destroyer 99d18's
 * controller cache parts by 1..2 ulps at its zone change 3135 with every watched input equal through 3134. At a zone change the
 * engine re-expresses a LinearFlightControlModel's plan by the relative transform it hands the model's vtable slot 7 (0xe84ea0:
 * +0x70 = rotation(rdx +0x10..+0x3f) x +0x70 + rdx +0x00). The hook (that vtable entry, 0x2c4f648; both sides, from the first
 * watch dump) records for a watched ship's model the tick, the matrix (16 floats) and +0x70 before and after; JOIN_RPRT rows
 * in the watch dump. */
#define JOIN_RPRT_ENTRY 0x2c4f648u
#define JOIN_RPRT_FN 0xe84ea0u
#define JOIN_RPRT_ROWS 256u
typedef struct {uint64_t tick;uint32_t watched,thread;float m[16],before[4],after[4];} join_rprt_row;
static join_rprt_row join_rprt_rows[JOIN_RPRT_ROWS];
static volatile LONG join_rprt_n;static LONG join_rprt_shown;static volatile LONG64 join_rprt_calls;
static int join_rprt_on,join_rprt_installed;
static void join_rprt_native(void *model,const float *m) {((void (*)(void *,const float *))(void *)(image+JOIN_RPRT_FN))(model,m);}
static void (*join_rprt_original)(void *,const float *)=join_rprt_native;   /* tests replace it */
static void join_rprt_hook(unsigned char *model,const float *m) {
    unsigned char *agent=NULL;int w=-1;float before[4]={0,0,0,0};
    InterlockedIncrement64(&join_rprt_calls);
    if(model && join_read(model+0x10,&agent,8) && agent)
        for(unsigned i=0;i<join_watch.n && i<JOIN_WATCH_MAX;i++)
            if(join_watch.object[i] && join_nav_agent(join_watch.object[i])==agent) {w=(int)i;break;}
    if(w>=0) (void)join_read(model+0x70,before,16);
    join_rprt_original(model,m);
    if(w<0) return;
    const LONG k=InterlockedIncrement(&join_rprt_n)-1;
    if(k<0 || (ULONG)k>=JOIN_RPRT_ROWS) return;
    join_rprt_row *r=&join_rprt_rows[k];r->tick=stats.issued;r->watched=(uint32_t)w;r->thread=GetCurrentThreadId();
    memcpy(r->m,m,sizeof(r->m));memcpy(r->before,before,16);(void)join_read(model+0x70,r->after,16);
}
#ifndef JOIN_OWN_PLATFORM
static int join_rprt_swap(void *expected,void *value) {
    DWORD old,ignored;if(!VirtualProtect(image+JOIN_RPRT_ENTRY,8,PAGE_READWRITE,&old)) return 0;
    void *actual=InterlockedCompareExchangePointer((void*volatile*)(image+JOIN_RPRT_ENTRY),value,expected);
    VirtualProtect(image+JOIN_RPRT_ENTRY,8,old,&ignored);return actual==expected;
}
static void join_rprt_install(void) {
    if(join_rprt_on && !join_rprt_installed && join_rprt_swap(image+JOIN_RPRT_FN,(void *)join_rprt_hook)) join_rprt_installed=1;
}
static void join_rprt_restore(void) {if(join_rprt_installed && join_rprt_swap((void *)join_rprt_hook,image+JOIN_RPRT_FN)) join_rprt_installed=0;}
#else
static void join_rprt_install(void) {}
static void join_rprt_restore(void) {}
#endif
/* The ID's place in the list (1-based; one already listed keeps its place: the probe's setup may run again), 0 refused. */
static unsigned join_watch_add(uint64_t portable) {
    if(!portable) return 0;
    for(unsigned i=0;i<join_watch.n;i++) if(join_watch.portable[i]==portable) return i+1u;
    if(join_watch.n>=JOIN_WATCH_MAX) return 0;
    join_watch.portable[join_watch.n++]=portable;return join_watch.n;
}
static void join_watch_arm(uint64_t tick) {join_watch.tick=tick;join_watch.armed=join_watch.n!=0;}
/* An engine object's predicate by vtable slot (const readers), -1 when it cannot be called. */
static int join_watch_call(unsigned char *object,uint32_t slot,int arg,int with_arg) {
#ifndef JOIN_OWN_PLATFORM
    uintptr_t vt=0,fn=0;
    if(!object || !join_read(object,&vt,8) || !vt || !join_read((void *)(vt+slot),&fn,8) || fn<(uintptr_t)image ||
       fn-(uintptr_t)image>=JOIN_FLIGHT_IMAGE) return -1;
    return with_arg?((char (*)(void *,int))fn)(object,arg)!=0:((char (*)(void *))fn)(object)!=0;
#else
    (void)object;(void)slot;(void)arg;(void)with_arg;return -1;   /* the Linux gate: its own slots (not yet) */
#endif
}
/* A pointer that is a live object (its runtime ID resolves to it), else NULL. */
static unsigned char *join_watch_live(unsigned char *p) {
    uint64_t runtime=0;return p && join_read(p+8,&runtime,8) && runtime && join_object(runtime)==p?p:NULL;
}
static uint64_t join_watch_key(unsigned char *p) {
    uint64_t runtime=0;unsigned klass;
    if(!p || !join_read(p+8,&runtime,8) || !runtime) return 0;
    uint64_t key=join_key(runtime,&klass);return key?key:(runtime|(1ull<<62));   /* bit 62: a runtime ID without portable key */
}
static size_t join_watch_hex(char *out,size_t cap,const unsigned char *at,size_t n) {
    unsigned char b[64];size_t k=0;
    if(n>sizeof(b) || !join_read(at,b,n)) return (size_t)snprintf(out,cap,"-");
    for(size_t i=0;i<n && k+3<cap;i++) k+=(size_t)snprintf(out+k,cap-k,"%02x",b[i]);
    return k;
}
static int join_watch_key_cmp(const void *a,const void *b) {uint64_t x=*(const uint64_t *)a,y=*(const uint64_t *)b;return x<y?-1:x>y;}
static int join_pcf_install(void);   /* lockstep_join.h: JOIN_WATCHPCF's call sites */
static unsigned join_pcf_mask;
/* JOIN_WATCHREFS (diagnostic, the first dump after T+1 only): who points at a watched object. j1-179: an escape pod destroyed at 683
 * on both sides (event 0x2baad90) is deleted on the session in the next step, on the joiner not before 720; its count at +0x40 (the
 * intrusive reference count, lock xadd at +0x40 throughout) is 2 higher on the joiner from the load on (10/12 at 633, 1/3 after the
 * destroy), its docking bay's too, the ship's other bay's 2 lower. This scans the process's committed private read-write memory for
 * the object's address and lists each hit: the holder (the nearest pointer into the image within 0x400 bytes before the hit, taken as
 * its vtable: RVA, the hit's offset from it, its key when it is a component) and the 16 bytes after the hit. At most 64 hits. */
#define JOIN_REFS_HITS 64u
#define JOIN_REFS_BACK 0x400u
static size_t join_watch_refs(char *out,size_t cap,char side,uint64_t tick,uint64_t portable,const unsigned char *object) {
#ifdef JOIN_OWN_PLATFORM
    (void)out;(void)cap;(void)side;(void)tick;(void)portable;(void)object;return 0;   /* the Linux gate: no VirtualQuery walk (not yet) */
#else
    static uintptr_t chunk[8192];size_t at=0;unsigned hits=0;uint64_t scanned=0;
    MEMORY_BASIC_INFORMATION mbi;const unsigned char *p=NULL;
    const uintptr_t want=(uintptr_t)object,lo=(uintptr_t)image,hi=(uintptr_t)image+JOIN_FLIGHT_IMAGE;
    while(hits<JOIN_REFS_HITS && VirtualQuery(p,&mbi,sizeof(mbi))==sizeof(mbi)) {
        const unsigned char *base=(const unsigned char *)mbi.BaseAddress,*end=base+mbi.RegionSize;
        if(mbi.State==MEM_COMMIT && mbi.Type==MEM_PRIVATE && !(mbi.Protect&(PAGE_GUARD|PAGE_NOACCESS)) &&
           (mbi.Protect&(PAGE_READWRITE|PAGE_EXECUTE_READWRITE|PAGE_WRITECOPY)) && base!=(const unsigned char *)chunk)
            for(const unsigned char *c=base;c<end && hits<JOIN_REFS_HITS;c+=sizeof(chunk)) {
                const size_t n=(size_t)(end-c)<sizeof(chunk)?(size_t)(end-c):sizeof(chunk);
                if((const unsigned char *)chunk>=c && (const unsigned char *)chunk<c+n) continue;   /* our own buffer */
                if(!join_read(c,chunk,n)) continue;
                scanned+=n;
                for(size_t k=0;k<n/8u && hits<JOIN_REFS_HITS;k++) {
                    if(chunk[k]!=want) continue;
                    const unsigned char *hit=c+8u*k;uintptr_t holder=0,vt=0,after[2]={0,0};
                    for(uintptr_t b=8u;b<=JOIN_REFS_BACK;b+=8u) {
                        uintptr_t w=0;
                        if((uintptr_t)hit<b || !join_read(hit-b,&w,8)) break;
                        if(w>=lo && w<hi) {holder=(uintptr_t)(hit-b);vt=w;break;}
                    }
                    (void)join_read(hit+8,after,16);
                    ++hits;
                    if(at+200u<cap) at+=(size_t)snprintf(out+at,cap-at,"[X4LAN] NATIVE_GATE JOIN_WATCHREFS side=%c tick=%llu id=%llx hit=%u addr=%llx holder=%x+%x key=%llx after=%llx,%llx ctx=",
                                                         side,(unsigned long long)tick,(unsigned long long)portable,hits,(unsigned long long)(uintptr_t)hit,
                                                         vt?(unsigned)(vt-lo):0u,
                                                         holder?(unsigned)((uintptr_t)hit-holder):0u,
                                                         (unsigned long long)(holder?join_watch_key((unsigned char *)holder):0u),
                                                         (unsigned long long)after[0],(unsigned long long)after[1]);
                    for(int q=-8;q<8 && at+24u<cap;q++) {   /* ctx: 0x40 bytes before the hit and 0x40 from it, as qwords (image pointers i+RVA) */
                        uintptr_t w=0;(void)join_read(hit+8*q,&w,8);
                        if(w>=lo && w<hi) at+=(size_t)snprintf(out+at,cap-at,"%si+%x",q==-8?"":",",(unsigned)(w-lo));
                        else at+=(size_t)snprintf(out+at,cap-at,"%s%llx",q==-8?"":",",(unsigned long long)w);
                    }
                    if(at+3u<cap) at+=(size_t)snprintf(out+at,cap-at,"\r\n");
                }
            }
        if(end<=p) break;
        p=end;
    }
    uint32_t count=0;(void)join_read(object+0x40,&count,4);
    if(at+160u<cap) at+=(size_t)snprintf(out+at,cap-at,"[X4LAN] NATIVE_GATE JOIN_WATCHREFS side=%c tick=%llu id=%llx hits=%u count=%u scanned=%llu\r\n",side,
                                         (unsigned long long)tick,(unsigned long long)portable,hits,count,(unsigned long long)scanned);
    return at;
#endif
}
/* JOIN_REFCOUNTS (diagnostic, with the watch, after T+1): the reference count +0x40 of every ship and docking bay of the census, by
 * portable ID (JOIN_REFCOUNT rows): j1-182 found the joiner's escape pods holding two more
 * references from the load on (their deletion after the destroy comes later, their radar contacts linger, ships near them move
 * apart at 780); this measures how many objects the load leaves with another count than the session's. */
static void join_refcounts_dump(uint64_t tick) {
    static const uint32_t types[]={0x2b7d738u,0x2b7feb8u,0x2b82440u,0x2b9bff8u,0x2b99a88u,0x2ad9ca0u};   /* ShipXS/S/M/L/XL, DockingBay */
    const size_t cap=1u<<16;char *out=malloc(cap);size_t at=0;
    if(!out) return;
    const unsigned total=world_hash_count+world_hash_created_count;unsigned shown=0;
    for(unsigned i=0;i<total;i++) {
        const world_hash_item *item=world_hash_item_at(i);
        unsigned char *o=item->runtime?join_object(item->runtime):NULL;uintptr_t vt=0;uint32_t count=0;
        if(!o || !join_read(o,&vt,8) || vt<(uintptr_t)image || !join_read(o+0x40,&count,4)) continue;
        const uint32_t rva=(uint32_t)(vt-(uintptr_t)image);int want=0;
        for(unsigned k=0;k<sizeof(types)/sizeof(types[0]);k++) want|=rva==types[k];
        if(!want) continue;
        if(at+96u>=cap) {crash_note_write(out,(int)at);at=0;}
        at+=(size_t)snprintf(out+at,cap-at,"[X4LAN] NATIVE_GATE JOIN_REFCOUNT tick=%llu id=%llx type=%x count=%u\r\n",(unsigned long long)tick,
                             (unsigned long long)item->saved,rva,count);
        ++shown;
    }
    at+=(size_t)snprintf(out+at,cap-at,"[X4LAN] NATIVE_GATE JOIN_REFCOUNT tick=%llu shown=%u total=%u\r\n",(unsigned long long)tick,shown,total);
    crash_note_write(out,(int)at);free(out);
}
/* JOIN_REFWATCH (diagnostic, with the watch, both sides): who changes the reference counts of the first four watched objects. j1-185:
 * created ships docked at T hold two more references on the joiner than on the session, held by objects the memory scan cannot name.
 * From the dump after T+1 to the end of the watch span each count (+0x40, 4 bytes) has a hardware write breakpoint (Dr0..Dr3) on every
 * thread (the current one through a raised exception the handler answers with the debug registers, the others suspended and set,
 * the threads started since at each later dump). Every write raises a single step after the storing instruction; the vectored handler
 * records the tick, the slot, the count written, the instruction and six callers (RtlVirtualUnwind on a copy of the context, image RVAs,
 * 'x' + the low 32 bits outside the image) and continues. Rows JOIN_REFWATCH at each dump. Diagnostic: the exception round trips cost
 * time on the writing threads (see lockstep_draw_watch.h for that caveat). */
#ifndef JOIN_OWN_PLATFORM
#include <tlhelp32.h>
#endif
#define JOIN_RW_SET 0xE04A5257u
#define JOIN_RW_ROWS 16384u
#define JOIN_RW_CHAIN 6u
#define JOIN_RW_PEEK 0x80u
typedef struct {uint64_t tick;uint32_t slot,count;uint64_t rip;uint64_t chain[JOIN_RW_CHAIN];uint64_t value;uint64_t regs[4];uint64_t regs2[2];uint32_t xmm[4];
                uint32_t peek_len;unsigned char peek[JOIN_RW_PEEK];} join_rw_row;
/* regs: rax, rbx, r12, r13; an execution breakpoint's row adds regs2 (rsi, rcx) and xmm (the low lane of xmm0..xmm3: a float argument or
 * result, e.g. the braking distance 0x6de670 returns to the slot refresh 0xea2118) */
/* native_gate_join_refwatch = {'<portable>:<deref>/<deref>:<offset>:<len>', ...} (hex; up to four; the derefs may be empty, len 1, 2, 4
 * or 8): watch that field instead (object -> *(object+deref) ... -> +offset). Without it: the first four watched objects' counts +0x40.
 * The portable ID must also be in native_gate_join_watch (the watch resolves it). */
typedef struct {uint64_t portable;uint32_t nderef,deref[3],offset,len;} join_rw_spec_t;
static join_rw_spec_t join_rw_spec[4];static uint32_t join_rw_nspec;
static uint32_t join_rw_len[4];
static unsigned char join_rw_exec[4];   /* an execution breakpoint (the pseudo ID fffffffffffffffe: the image, offset = RVA, len 1) */
/* An execution breakpoint's filter: 'fffffffffffffffe:<k>:<rva>:1' keeps only the hits where a general register holds watched
 * object k's pointer (1-based in native_gate_join_watch), its controller (+0x200) or that controller's agent (+0x40), taken
 * when the watch arms [w1z44: the braking distance call 0xea2118 runs for every ship's refresh, 16384 rows by tick 768,
 * the destroyer's at 794 among the dropped]. Without <k>: every hit. The filtered-out hits still cost the exception. */
static uintptr_t join_rw_match[4][3];
/* native_gate_join_refwatch_peek = {'<slot>:<reg>:<offset>[*<offset2>]:<len>', ...} (slot 0..3, the field's position in
 * native_gate_join_refwatch; reg rax rbx rcx rdx rsi rdi rbp rsp r8 .. r15; offset hex, a leading '-' below the register; with
 * *offset2 the qword there is a pointer and the copy starts at it plus offset2 (hex); len hex 1 .. 80): that execution breakpoint's
 * rows also copy len bytes, read in the handler at the hit, printed as peek= (hex, memory order; 'x' when unreadable) [j1-244: 176ad5's bullet hit at 1011 parts in the hit point alone, impulse equal; the raycast's segment
 * and result live in UpdateFlight's frame (0xbc4d0 from 0xd4fc61: result rbp+0x20 .. +0x58, segment rbp+0x70, rbp+0x80), the bullet's
 * own times in r14 (+0x268, +0x288)]. */
typedef struct {uint32_t reg,len,deref,offset2;int64_t offset;} join_rw_peek_t;
static join_rw_peek_t join_rw_peek[4];
/* native_gate_join_refwatch_at = n: with the watch, the fields are armed from the boundary of step n-1 on (both sides; the hits of
 * step n onward), not at T: unfiltered execution breakpoints on a busy path (every bullet hit) keep their 16384 rows for the steps that
 * matter. The watch stays armed for it past native_gate_join_watch_span: from n-1 to n-1+JOIN_RW_AT_STEPS each boundary refreshes the
 * watched objects, re-arms every 10 steps and prints the new rows, without the JOIN_WATCH dumps [w1z74/75: with the default span of 2
 * the watch ended at T+2 and the fields never armed]. */
#define JOIN_RW_AT_STEPS 600u
static uint64_t join_rw_at;
static int join_rw_peek_add(const char *text,size_t length) {   /* 1 set, 0 refused */
    static const char *const regs[16]={"rax","rbx","rcx","rdx","rsi","rdi","rbp","rsp","r8","r9","r10","r11","r12","r13","r14","r15"};
    char buf[48];if(!text || length<8u || length>=sizeof(buf)) return 0;
    memcpy(buf,text,length);buf[length]=0;
    if(buf[0]<'0' || buf[0]>'3' || buf[1]!=':') return 0;
    const unsigned slot=(unsigned)(buf[0]-'0');char *p=buf+2,*colon=strchr(p,':');
    if(!colon) return 0;
    *colon=0;unsigned reg=16u;for(unsigned r=0;r<16u;r++) if(!strcmp(p,regs[r])) {reg=r;break;}
    if(reg>=16u) return 0;
    p=colon+1;int negative=0;if(*p=='-') {negative=1;++p;}
    uint64_t v=0;int digits=0;
    for(;*p && *p!=':' && *p!='*';p++) {
        const char c=*p;const unsigned d=c>='0' && c<='9'?(unsigned)(c-'0'):c>='a' && c<='f'?(unsigned)(c-'a'+10):16u;
        if(d>15u || ++digits>8) return 0;
        v=v<<4|d;
    }
    if(!digits) return 0;
    const int64_t offset=negative?-(int64_t)v:(int64_t)v;
    uint32_t deref=0,offset2=0;
    if(*p=='*') {
        deref=1;digits=0;
        for(++p;*p && *p!=':';p++) {
            const char c=*p;const unsigned d=c>='0' && c<='9'?(unsigned)(c-'0'):c>='a' && c<='f'?(unsigned)(c-'a'+10):16u;
            if(d>15u || ++digits>6) return 0;
            offset2=offset2<<4|d;
        }
        if(!digits) return 0;
    }
    if(*p!=':') return 0;
    uint64_t n=0;digits=0;
    for(++p;*p;p++) {
        const char c=*p;const unsigned d=c>='0' && c<='9'?(unsigned)(c-'0'):c>='a' && c<='f'?(unsigned)(c-'a'+10):16u;
        if(d>15u || ++digits>2) return 0;
        n=n<<4|d;
    }
    if(!digits || !n || n>JOIN_RW_PEEK) return 0;
    join_rw_peek[slot].reg=reg;join_rw_peek[slot].offset=offset;join_rw_peek[slot].len=(uint32_t)n;
    join_rw_peek[slot].deref=deref;join_rw_peek[slot].offset2=offset2;return 1;
}
static int join_rw_add(const char *text,size_t length) {   /* 1 added, 0 refused */
    join_rw_spec_t s;memset(&s,0,sizeof(s));size_t i=0;unsigned field=0;uint64_t v=0;int digits=0;
    for(;i<=length;i++) {
        const char c=i<length?text[i]:':';
        const unsigned d=c>='0' && c<='9'?(unsigned)(c-'0'):c>='a' && c<='f'?(unsigned)(c-'a'+10):c>='A' && c<='F'?(unsigned)(c-'A'+10):16u;
        if(d<16u) {v=v<<4|d;++digits;if(digits>16) return 0;continue;}
        if(c=='/' && field==1u) {if(!digits || s.nderef>=3u) return 0;s.deref[s.nderef++]=(uint32_t)v;v=0;digits=0;continue;}
        if(c!=':') return 0;
        if(field==0u) {if(digits!=16) return 0;s.portable=v;}
        else if(field==1u) {if(digits) {if(s.nderef>=3u) return 0;s.deref[s.nderef++]=(uint32_t)v;}}
        else if(field==2u) {if(!digits) return 0;s.offset=(uint32_t)v;}
        else if(field==3u) {if(v!=1u && v!=2u && v!=4u && v!=8u) return 0;s.len=(uint32_t)v;}
        else return 0;
        ++field;v=0;digits=0;
    }
    if(field!=4u || !s.portable || !s.len || (s.offset&(s.len-1u))) return 0;   /* the end closes field 3 */
    for(uint32_t k=0;k<join_rw_nspec;k++) if(!memcmp(&join_rw_spec[k],&s,sizeof(s))) return 1;   /* the probe's config again (a UI reload) */
    if(join_rw_nspec>=4u) return 0;
    join_rw_spec[join_rw_nspec++]=s;return 1;
}
static join_rw_row join_rw_rows[JOIN_RW_ROWS];
static volatile LONG join_rw_n,join_rw_shown;static volatile LONG64 join_rw_dropped,join_rw_strays,join_rw_threads,join_rw_failed;
static volatile uintptr_t join_rw_addr[4];
static void *join_rw_veh_handle;
#ifndef JOIN_OWN_PLATFORM
static DWORD64 join_rw_dr7(void) {
    DWORD64 dr7=0;
    for(unsigned i=0;i<4u;i++) if(join_rw_addr[i]) {   /* local enable, write; LEN 00 1 byte, 01 2, 11 4, 10 8 */
        const unsigned len=join_rw_len[i]==1u?0u:join_rw_len[i]==2u?1u:join_rw_len[i]==8u?2u:3u;
        if(join_rw_exec[i]) {dr7|=1ull<<(2u*i);continue;}   /* RW 00 execute, LEN 00 */
        dr7|=(1ull<<(2u*i))|(1ull<<(16u+4u*i))|((DWORD64)len<<(18u+4u*i));
    }
    return dr7;
}
static void join_rw_load(CONTEXT *c) {
    c->ContextFlags|=CONTEXT_DEBUG_REGISTERS;   /* else the raised exception's continue drops them (j1-190: none caught in the load) */
    c->Dr0=join_rw_addr[0];c->Dr1=join_rw_addr[1];c->Dr2=join_rw_addr[2];c->Dr3=join_rw_addr[3];c->Dr6=0;c->Dr7=join_rw_dr7();
}
static LONG CALLBACK join_rw_veh(EXCEPTION_POINTERS *e) {
    CONTEXT *c=e->ContextRecord;const DWORD code=e->ExceptionRecord->ExceptionCode;
    if(code==JOIN_RW_SET) {join_rw_load(c);return EXCEPTION_CONTINUE_EXECUTION;}
    if(code!=EXCEPTION_SINGLE_STEP) return EXCEPTION_CONTINUE_SEARCH;
    const DWORD64 hit=c->Dr6&0xfu;unsigned slot=4u;
    for(unsigned i=0;i<4u;i++) if((hit>>i)&1u && join_rw_addr[i]) {slot=i;break;}
    if(slot>=4u) {
        if(c->Dr7==join_rw_dr7() && c->Dr7) {InterlockedIncrement64(&join_rw_strays);c->Dr6=0;return EXCEPTION_CONTINUE_EXECUTION;}
        return EXCEPTION_CONTINUE_SEARCH;
    }
    if(join_rw_exec[slot] && join_rw_match[slot][0]) {
        const DWORD64 g[]={c->Rax,c->Rbx,c->Rcx,c->Rdx,c->Rsi,c->Rdi,c->R8,c->R9,c->R10,c->R11,c->R12,c->R13,c->R14,c->R15};int mine=0;
        for(unsigned q=0;q<sizeof(g)/sizeof(*g) && !mine;q++) for(unsigned m=0;m<3u;m++) if(join_rw_match[slot][m] && g[q]==join_rw_match[slot][m]) {mine=1;break;}
        if(!mine) {c->Dr6=0;c->EFlags|=0x10000u;return EXCEPTION_CONTINUE_EXECUTION;}
    }
    const LONG k=InterlockedIncrement(&join_rw_n)-1;
    if(k<(LONG)JOIN_RW_ROWS) {
        join_rw_row *r=&join_rw_rows[k];uint64_t v=0;const uint32_t len=join_rw_len[slot]?join_rw_len[slot]:4u;
        r->tick=stats.issued;r->slot=slot;r->rip=c->Rip;
        if(join_rw_exec[slot]) {r->value=c->Rax;r->count=(uint32_t)c->R13;}
        else if(join_read((void *)join_rw_addr[slot],&v,len)) {r->value=v;r->count=(uint32_t)v;} else {r->value=~0ull;r->count=0xffffffffu;}
        r->regs[0]=c->Rax;r->regs[1]=c->Rbx;r->regs[2]=c->R12;r->regs[3]=c->R13;r->regs2[0]=c->Rsi;r->regs2[1]=c->Rcx;
        for(unsigned x=0;x<4u;x++) memcpy(&r->xmm[x],&c->FltSave.XmmRegisters[x],4);
        r->peek_len=0;
        if(join_rw_exec[slot] && join_rw_peek[slot].len) {
            DWORD64 base=0;
            switch(join_rw_peek[slot].reg) {
                case 0: base=c->Rax;break;case 1: base=c->Rbx;break;case 2: base=c->Rcx;break;case 3: base=c->Rdx;break;
                case 4: base=c->Rsi;break;case 5: base=c->Rdi;break;case 6: base=c->Rbp;break;case 7: base=c->Rsp;break;
                case 8: base=c->R8;break;case 9: base=c->R9;break;case 10: base=c->R10;break;case 11: base=c->R11;break;
                case 12: base=c->R12;break;case 13: base=c->R13;break;case 14: base=c->R14;break;default: base=c->R15;break;
            }
            DWORD64 at=base+(DWORD64)join_rw_peek[slot].offset;int ok=1;
            if(join_rw_peek[slot].deref) {uint64_t q=0;ok=join_read((const void *)(uintptr_t)at,&q,8) && q;at=q+join_rw_peek[slot].offset2;}
            if(ok && join_read((const void *)(uintptr_t)at,r->peek,join_rw_peek[slot].len)) r->peek_len=join_rw_peek[slot].len;
            else r->peek_len=~0u;
        }
        CONTEXT u=*c;
        for(unsigned f=0;f<JOIN_RW_CHAIN;f++) {
            DWORD64 base=0;PVOID data=NULL;DWORD64 frame=0;
            PRUNTIME_FUNCTION fn=RtlLookupFunctionEntry(u.Rip,&base,NULL);
            if(fn) RtlVirtualUnwind(UNW_FLAG_NHANDLER,base,u.Rip,fn,&u,&data,&frame,NULL);
            else {DWORD64 ret=0;if(!join_read((void *)u.Rsp,&ret,8)) break;u.Rip=ret;u.Rsp+=8;}
            r->chain[f]=u.Rip;
            if(!u.Rip) break;
        }
    } else InterlockedIncrement64(&join_rw_dropped);
    c->Dr6=0;
    if(join_rw_exec[slot]) c->EFlags|=0x10000u;   /* RF: run the instruction once without the breakpoint */
    return EXCEPTION_CONTINUE_EXECUTION;
}
static void join_rw_apply(void) {   /* the debug registers of every thread of the process to join_rw_addr */
    const DWORD self=GetCurrentThreadId(),pid=GetCurrentProcessId();
    HANDLE snap=CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD,0);
    if(snap!=INVALID_HANDLE_VALUE) {
        THREADENTRY32 te;te.dwSize=sizeof(te);
        for(BOOL more=Thread32First(snap,&te);more;more=Thread32Next(snap,&te)) {
            if(te.th32OwnerProcessID!=pid || te.th32ThreadID==self) continue;
            HANDLE h=OpenThread(THREAD_GET_CONTEXT|THREAD_SET_CONTEXT|THREAD_SUSPEND_RESUME,FALSE,te.th32ThreadID);
            if(!h) {InterlockedIncrement64(&join_rw_failed);continue;}
            if(SuspendThread(h)!=(DWORD)-1) {
                CONTEXT c;memset(&c,0,sizeof(c));c.ContextFlags=CONTEXT_DEBUG_REGISTERS;
                if(GetThreadContext(h,&c)) {join_rw_load(&c);if(SetThreadContext(h,&c)) InterlockedIncrement64(&join_rw_threads); else InterlockedIncrement64(&join_rw_failed);}
                else InterlockedIncrement64(&join_rw_failed);
                ResumeThread(h);
            } else InterlockedIncrement64(&join_rw_failed);
            CloseHandle(h);
        }
        CloseHandle(snap);
    }
    RaiseException(JOIN_RW_SET,0,0,NULL);   /* this thread: the handler loads the registers into the context it continues */
}
#endif
static void join_rw_arm(int on) {
#ifndef JOIN_OWN_PLATFORM
    if(on && !join_rw_veh_handle) join_rw_veh_handle=AddVectoredExceptionHandler(1,join_rw_veh);
    if(!join_rw_veh_handle) return;
    for(unsigned i=0;i<4u;i++) {
        uintptr_t a=0;uint32_t len=4u;
        if(on && join_rw_nspec) {
            if(i<join_rw_nspec) {
                const join_rw_spec_t *s=&join_rw_spec[i];unsigned char *o=NULL;
                memset(join_rw_match[i],0,sizeof(join_rw_match[i]));
                if(s->portable==~0ull) o=join_jobe_engine();   /* the pseudo ID ffffffffffffffff: the job engine (JOIN_JOBQ) */
                else if(s->portable==~1ull) {   /* fffffffffffffffe: the image (an execution breakpoint), its derefs field the filter */
                    o=(unsigned char *)image;
                    if(s->nderef && s->deref[0] && s->deref[0]<=join_watch.n && join_watch.object[s->deref[0]-1u]) {
                        unsigned char *w=join_watch.object[s->deref[0]-1u],*ctl=NULL,*agt=NULL;
                        join_rw_match[i][0]=(uintptr_t)w;
                        if(join_read(w+0x200,&ctl,8) && ctl) {
                            join_rw_match[i][1]=(uintptr_t)ctl;
                            if(join_read(ctl+0x40,&agt,8) && agt) join_rw_match[i][2]=(uintptr_t)agt;
                        }
                    }
                }
                for(unsigned w=0;w<join_watch.n && !o;w++) if(join_watch.portable[w]==s->portable) {o=join_watch.object[w];break;}
                for(uint32_t d=0;o && s->portable!=~1ull && d<s->nderef;d++) {unsigned char *p=NULL;o=join_read(o+s->deref[d],&p,8)?p:NULL;}
                if(o) a=(uintptr_t)(o+s->offset);
                len=s->len;join_rw_exec[i]=s->portable==~1ull;
            }
        } else if(on && i<join_watch.n && join_watch.object[i]) a=(uintptr_t)(join_watch.object[i]+0x40);
        join_rw_addr[i]=a;join_rw_len[i]=len;
    }
    join_rw_apply();
#else
    (void)on;
#endif
}
/* During the joiner's load (with native_gate_join_ids): the watched objects' counts from the moment join_ids gives each its
 * session ID (join_ids_assign_pinned), on the loading thread, so the load's own references show (j1-184: a ship created on the
 * session and docked at T holds two more on the joiner from the load on). join_rw_load_prepare at join_load maps the first four
 * watched portable IDs to the session's runtime IDs from the package. With JOIN_REFWATCH fields (no dereference): each field's
 * object by its portable or session runtime ID, the field itself watched from that moment (j1-206: who sets NPC +0x5a0, the
 * animation state, in the load; the session's 7 NPCs with one hold <npcanimation> in the join save, the joiner restores 5). */
static uint64_t join_rw_from;static uint32_t join_rw_early_found;   /* native_gate_join_refwatch_from (join_rw_early_probe) */
static uint64_t join_rw_load_runtime[4];static volatile LONG64 join_rw_load_calls,join_rw_load_hits;static uint32_t join_rw_load_prepared;
static void join_rw_load_prepare(const join_tables *t) {
    for(unsigned i=0;i<4u;i++) {
        join_rw_load_runtime[i]=0;
        if(join_rw_nspec) {   /* the fields' objects (no dereference in the load) */
            if(i>=join_rw_nspec || join_rw_spec[i].nderef) continue;
            const uint64_t want=join_rw_spec[i].portable;
            for(uint32_t k=0;k<t->nids;k++) if(t->ids[k].portable==want || t->ids[k].runtime==want) {join_rw_load_runtime[i]=t->ids[k].runtime;++join_rw_load_prepared;break;}
            continue;
        }
        if(i>=join_watch.n) continue;   /* default mode: the watched objects' counts */
        for(uint32_t k=0;k<t->nids;k++) if(t->ids[k].portable==join_watch.portable[i]) {join_rw_load_runtime[i]=t->ids[k].runtime;++join_rw_load_prepared;break;}
    }
}
static void join_rw_load_hit(void *object,uint64_t id) {
#ifndef JOIN_OWN_PLATFORM
    InterlockedIncrement64(&join_rw_load_calls);
    for(unsigned i=0;i<4u;i++) {
        if(!join_rw_load_runtime[i] || join_rw_load_runtime[i]!=id) continue;
        InterlockedIncrement64(&join_rw_load_hits);
        if(!join_rw_veh_handle) join_rw_veh_handle=AddVectoredExceptionHandler(1,join_rw_veh);
        if(!join_rw_veh_handle) return;
        if(join_rw_nspec) {join_rw_addr[i]=(uintptr_t)object+join_rw_spec[i].offset;join_rw_len[i]=join_rw_spec[i].len;}
        else {join_rw_addr[i]=(uintptr_t)object+0x40u;join_rw_len[i]=4u;}
        RaiseException(JOIN_RW_SET,0,0,NULL);   /* the loading thread; the others from the admission on (side k) */
    }
#else
    (void)object;(void)id;
#endif
}
static size_t join_rw_rows_text(char *out,size_t cap,char side,uint64_t tick) {
    size_t at=0;LONG n=join_rw_n;if(n>(LONG)JOIN_RW_ROWS) n=(LONG)JOIN_RW_ROWS;
    const uintptr_t lo=(uintptr_t)image,hi=(uintptr_t)image+JOIN_FLIGHT_IMAGE;
    for(LONG k=join_rw_shown;k<n && at+720u<cap;k++) {
        const join_rw_row *r=&join_rw_rows[k];
        const uint64_t who=join_rw_nspec?(r->slot<join_rw_nspec?join_rw_spec[r->slot].portable:0u):join_watch.portable[r->slot];
        at+=(size_t)snprintf(out+at,cap-at,"[X4LAN] NATIVE_GATE JOIN_REFWATCH side=%c tick=%llu at=%llu id=%llx slot=%u count=%u value=%llx rip=",side,
                             (unsigned long long)tick,(unsigned long long)r->tick,(unsigned long long)who,r->slot,r->count,(unsigned long long)r->value);
        for(unsigned f=0;f<=JOIN_RW_CHAIN;f++) {
            const uint64_t a=f?r->chain[f-1]:r->rip;
            if(f && !a) break;
            if(a>=lo && a<hi) at+=(size_t)snprintf(out+at,cap-at,"%s%x",f?",":"",(unsigned)(a-lo));
            else at+=(size_t)snprintf(out+at,cap-at,"%sx%x",f?",":"",(unsigned)a);
        }
        if(join_rw_nspec && r->slot<join_rw_nspec && join_rw_spec[r->slot].portable==~1ull)
            at+=(size_t)snprintf(out+at,cap-at," regs=%llx,%llx,%llx,%llx",(unsigned long long)r->regs[0],(unsigned long long)r->regs[1],
                                 (unsigned long long)r->regs[2],(unsigned long long)r->regs[3]);
        if(join_rw_nspec && r->slot<join_rw_nspec && join_rw_spec[r->slot].portable==~1ull) {
            at+=(size_t)snprintf(out+at,cap-at," regs2=%llx,%llx xmm=%08x,%08x,%08x,%08x",(unsigned long long)r->regs2[0],
                                 (unsigned long long)r->regs2[1],r->xmm[0],r->xmm[1],r->xmm[2],r->xmm[3]);
            /* rbx_obj= and rsi_obj=: what the two registers point at, as the watch keys a component (its portable key, or bit 62
             * and its runtime ID) and its vtable RVA (0: none in the image), read now at the dump, not in the handler [w1z46: which
             * component 0x6d6d80 hands the braking function 0x6de670 in rbx; the engines 79037..79039 have 1.0 there] */
            uintptr_t vb=0,vs=0;
            (void)join_read((const void *)(uintptr_t)r->regs[1],&vb,8);(void)join_read((const void *)(uintptr_t)r->regs2[0],&vs,8);
            at+=(size_t)snprintf(out+at,cap-at," rbx_obj=%llx/%x rsi_obj=%llx/%x",
                                 (unsigned long long)join_watch_key((unsigned char *)(uintptr_t)r->regs[1]),vb>=lo && vb<hi?(unsigned)(vb-lo):0u,
                                 (unsigned long long)join_watch_key((unsigned char *)(uintptr_t)r->regs2[0]),vs>=lo && vs<hi?(unsigned)(vs-lo):0u);
            if(r->peek_len==~0u) at+=(size_t)snprintf(out+at,cap-at," peek=x");
            else if(r->peek_len && r->peek_len<=JOIN_RW_PEEK) {
                at+=(size_t)snprintf(out+at,cap-at," peek=");
                for(uint32_t b=0;b<r->peek_len;b++) at+=(size_t)snprintf(out+at,cap-at,"%02x",r->peek[b]);
            }
        }
        at+=(size_t)snprintf(out+at,cap-at,"\r\n");
        join_rw_shown=k+1;
    }
    at+=(size_t)snprintf(out+at,cap-at,"[X4LAN] NATIVE_GATE JOIN_REFWATCH side=%c tick=%llu rows=%ld dropped=%lld strays=%lld threads=%lld failed=%lld load=%u:%lld:%lld early=%llu:%x\r\n",side,
                         (unsigned long long)tick,(long)join_rw_n,(long long)join_rw_dropped,(long long)join_rw_strays,(long long)join_rw_threads,
                         (long long)join_rw_failed,join_rw_load_prepared,(long long)join_rw_load_calls,(long long)join_rw_load_hits,
                         (unsigned long long)join_rw_from,join_rw_early_found);
    return at;
}
/* JOIN_OBJSAMPLE (diagnostic, with the watch, after T+1): the first JOIN_SAMPLE_PER objects of each of the classes below (in the
 * census's order, the same on both sides), their first JOIN_SAMPLE_BYTES raw, by portable ID. j1-199/201: the load leaves times at
 * the load time or at -1 where the session keeps older ones (the IK joints' time, +0x928, a ship's +0xb00, a pilot's +0x5b0..+0x5c0);
 * offline, the offsets where the joiner holds T or -1 and the session another value name the rest per class. */
#define JOIN_SAMPLE_PER 12u
#define JOIN_SAMPLE_BYTES 0x1000u
static void join_objsample_dump(uint64_t tick) {
    static const uint32_t types[]={0x2b7d738u,0x2b7feb8u,0x2b82440u,0x2b9bff8u,0x2b99a88u,0x2b764d8u,0x2b38a48u,0x2b462c8u,0x2ad9ca0u,
                                   0x2aaca18u,0x2aaa9d8u,0x2b68278u,0x2b6e910u};   /* ShipXS..XL, Station, NPC, Computer, DockingBay,
                                                                                      BuildStorage, BuildProcessor, Weapon, Turret */
    unsigned shown[sizeof(types)/sizeof(types[0])];memset(shown,0,sizeof(shown));
    const size_t cap=1u<<16;char *out=malloc(cap);size_t at=0;
    if(!out) return;
    const unsigned total=world_hash_count+world_hash_created_count;
    static unsigned char raw[JOIN_SAMPLE_BYTES];
    for(unsigned i=0;i<total;i++) {
        const world_hash_item *item=world_hash_item_at(i);
        unsigned char *o=item->runtime?join_object(item->runtime):NULL;uintptr_t vt=0;
        if(!o || !join_read(o,&vt,8) || vt<(uintptr_t)image) continue;
        unsigned k=0;
        for(;k<sizeof(types)/sizeof(types[0]);k++) if(vt-(uintptr_t)image==types[k]) break;
        if(k>=sizeof(types)/sizeof(types[0]) || shown[k]>=JOIN_SAMPLE_PER) continue;
        if(!join_read(o,raw,JOIN_SAMPLE_BYTES)) continue;
        ++shown[k];
        if(at+2u*JOIN_SAMPLE_BYTES+160u>=cap) {crash_note_write(out,(int)at);at=0;}
        at+=(size_t)snprintf(out+at,cap-at,"[X4LAN] NATIVE_GATE JOIN_OBJSAMPLE tick=%llu id=%llx type=%x raw=",(unsigned long long)tick,
                             (unsigned long long)item->saved,types[k]);
        static const char digits[]="0123456789abcdef";
        for(unsigned b=0;b<JOIN_SAMPLE_BYTES && at+3u<cap;b++) {out[at++]=digits[raw[b]>>4];out[at++]=digits[raw[b]&15u];}
        at+=(size_t)snprintf(out+at,cap-at,"\r\n");
    }
    if(at) crash_note_write(out,(int)at);
    free(out);
}
/* JOIN_NPCSLOT (diagnostic, with the watch, after T+1): every NPC's +0x5a0 (j1-203: the slot ship pilot 7a903 holds a pointer
 * there on the session, none on the joiner; NPC Move 0x6bc420 skips its whole behaviour block, the +0x5b0 stamp included, at
 * 0x6bc611 when it is null). Rows id:set:vt:[+0x1f0] (vt the pointee's vtable RVA, 0 outside the image), 48 per line. */
static void join_jobdump(char side,uint64_t tick);   /* lockstep_join.h: JOIN_JOBDUMP */
static void join_jobq_dump(char side,uint64_t tick);   /* lockstep_join.h: JOIN_JOBQ */
static void join_jobcnt_dump(uint64_t tick);   /* lockstep_join.h: JOIN_JOBCNT */
static void join_codew_step(uint64_t completed);   /* lockstep_join.h: JOIN_CODEW */
#define JOIN_NPC_VT 0x2b38a48u
#define JOIN_NPC_SLOT 0x5a0u
static void join_npcslot_dump(char side,uint64_t tick) {
    const size_t cap=1u<<16;char *out=malloc(cap);size_t at=0;unsigned n=0,set=0,line=0,nraw=0;
    unsigned char *raw_npc[16];uint64_t raw_id[16];
    if(!out) return;
    const unsigned total=world_hash_count+world_hash_created_count;
    for(unsigned i=0;i<total;i++) {
        const world_hash_item *item=world_hash_item_at(i);
        unsigned char *o=item->runtime?join_object(item->runtime):NULL;uintptr_t vt=0;
        if(!o || !join_read(o,&vt,8) || vt!=(uintptr_t)image+JOIN_NPC_VT) continue;
        unsigned char *p=NULL;uintptr_t pvt=0;uint64_t index=0;
        (void)join_read(o+JOIN_NPC_SLOT,&p,8);
        if(p) {(void)join_read(p,&pvt,8);(void)join_read(p+0x1f0,&index,8);++set;if(nraw<16u) {raw_npc[nraw]=o;raw_id[nraw++]=item->saved;}}
        ++n;
        const uint32_t rva=pvt>=(uintptr_t)image && pvt-(uintptr_t)image<JOIN_FLIGHT_IMAGE?(uint32_t)(pvt-(uintptr_t)image):0u;
        if(!line) at+=(size_t)snprintf(out+at,cap-at,"[X4LAN] NATIVE_GATE JOIN_NPCSLOT side=%c tick=%llu rows=",side,(unsigned long long)tick);
        at+=(size_t)snprintf(out+at,cap-at,"%s%llx:%d:%x:%llx",line?";":"",(unsigned long long)item->saved,p?1:0,rva,(unsigned long long)index);
        if(++line>=48u) {at+=(size_t)snprintf(out+at,cap-at,"\r\n");line=0;}
        if(at+512u>=cap) {crash_note_write(out,(int)at);at=0;}
    }
    if(line) at+=(size_t)snprintf(out+at,cap-at,"\r\n");
    at+=(size_t)snprintf(out+at,cap-at,"[X4LAN] NATIVE_GATE JOIN_NPCSLOT_SUM side=%c tick=%llu npcs=%u set=%u\r\n",side,(unsigned long long)tick,n,set);
    for(unsigned k=0;k<nraw;k++) {   /* JOIN_NPCANIM: the NPC's +0x4e0 .. +0x5e0 and its animation state's first 0x240 bytes */
        static const char digits[]="0123456789abcdef";unsigned char npc[0x100],anim[0x240];unsigned char *p=NULL;
        if(at+2u*(sizeof(npc)+sizeof(anim))+200u>=cap) {crash_note_write(out,(int)at);at=0;}
        memset(npc,0,sizeof(npc));memset(anim,0,sizeof(anim));
        (void)join_read(raw_npc[k]+0x4e0,npc,sizeof(npc));(void)join_read(raw_npc[k]+JOIN_NPC_SLOT,&p,8);if(p) (void)join_read(p,anim,sizeof(anim));
        at+=(size_t)snprintf(out+at,cap-at,"[X4LAN] NATIVE_GATE JOIN_NPCANIM side=%c tick=%llu id=%llx npc4e0=",side,(unsigned long long)tick,(unsigned long long)raw_id[k]);
        for(unsigned b=0;b<sizeof(npc);b++) {out[at++]=digits[npc[b]>>4];out[at++]=digits[npc[b]&15u];}
        at+=(size_t)snprintf(out+at,cap-at," anim=");
        for(unsigned b=0;b<sizeof(anim);b++) {out[at++]=digits[anim[b]>>4];out[at++]=digits[anim[b]&15u];}
        at+=(size_t)snprintf(out+at,cap-at,"\r\n");
    }
    crash_note_write(out,(int)at);free(out);
}
/* JOIN_LSTN (diagnostic, with JOIN_WATCH; j1-385): the late join's zone 0x561 escorts part at 3092 on the order in which the
 * carrier's defence NPC 0x123675 delivers an event_object_signalled ('blockingdockapproach cleared') to their pilots' handlers
 * (the first handler takes the dock slot): session cf8,c14,cbf,b30,..., joiner b30,b69,ba2,... (ascending, the load's). An event
 * source keeps, at +0x28, a vector of 0x28-byte entries {int type; vector<listener *> +8..+0x10} sorted by type (0x999ae0), which
 * 0x999120 appends to and DeliverEvent 0x99c470 walks in order; AddScriptListener 0xc9d0e0 also keeps a tree at +0x50 (header
 * embedded, root +0x58; node key int +0x20, listener +0x28) and the sources at +0x38..+0x40. Per watched object: both, the
 * listeners as kind.owner.script (1 AIScriptContext, 2 AICallStackContext (+0xc8), 3 a component, 0 vtable RVA). */
#define JOIN_LSTN_AI_VT 0x2c6a270u      /* AI::AIScriptContext */
#define JOIN_LSTN_STACK_VT 0x2c690b8u   /* AI::AICallStackContext */
static join_engine join_lstn_engine(void);   /* lockstep_join.h: join_engine_get */
static unsigned join_lstn_key(const join_engine *e,unsigned char *l,uint64_t *owner,uint64_t *script) {
    uintptr_t vt=0;uint64_t runtime=0;*owner=*script=0;
    if(!l || !join_read(l,&vt,8)) return 0;
    unsigned char *ctx=NULL;unsigned kind=0;
    if(vt==(uintptr_t)image+JOIN_LSTN_AI_VT) {ctx=l;kind=1;}
    else if(vt==(uintptr_t)image+JOIN_LSTN_STACK_VT) {if(!join_read(l+0xc8,&ctx,8)) ctx=NULL;kind=2;}
    if(kind) {
        join_ctx_rec k;unsigned f=0;
        if(ctx && join_ctx_key(e,(uintptr_t)ctx,&k,&f)) {*owner=k.owner;*script=k.script;}
        return kind;
    }
    if(join_read(l+8,&runtime,8) && runtime && join_object(runtime)==l) {unsigned klass;*owner=join_key(runtime,&klass);return 3;}
    *owner=vt>=(uintptr_t)image && vt-(uintptr_t)image<JOIN_FLIGHT_IMAGE?(uint64_t)(vt-(uintptr_t)image):0u;
    return 0;
}
static size_t join_lstn_dump(char *out,size_t cap,char side,uint64_t tick,uint64_t portable,unsigned char *o) {
    const join_engine e=join_lstn_engine();size_t at=0;uintptr_t table=0,v[2]={0,0};
    at+=(size_t)snprintf(out+at,cap-at,"[X4LAN] NATIVE_GATE JOIN_LSTN side=%c tick=%llu id=%llx t28=",side,(unsigned long long)tick,(unsigned long long)portable);
    if(join_read(o+0x28,&table,8) && table && join_read((void *)table,v,16) && v[1]>=v[0] && (v[1]-v[0])%0x28u==0 && (v[1]-v[0])/0x28u<=512u) {
        for(uintptr_t p=v[0];p<v[1] && at+4096<cap;p+=0x28u) {
            int32_t type=0;uintptr_t lv[2]={0,0};
            if(!join_read((void *)p,&type,4) || !join_read((void *)(p+8),lv,16) || lv[1]<lv[0] || (lv[1]-lv[0])&7u) continue;
            const uint64_t n=(lv[1]-lv[0])/8u;
            at+=(size_t)snprintf(out+at,cap-at,"%d/%llu:",type,(unsigned long long)n);
            for(uint64_t k=0;k<n && k<24u && at+128<cap;k++) {
                unsigned char *l=NULL;uint64_t ow=0,sc=0;
                if(!join_read((void *)(lv[0]+8u*k),&l,8)) continue;
                const unsigned kind=join_lstn_key(&e,l,&ow,&sc);
                at+=(size_t)snprintf(out+at,cap-at,"%u.%llx.%08x",kind,(unsigned long long)ow,(uint32_t)sc);
                /* the listener's own sources (+0x38..+0x40, AddSource 0xc9ce30 / RemoveSource 0xc9d070) as keys, '!' when this
                 * source is not among them (a registration its context does not know) */
                uintptr_t s[2]={0,0};
                if(kind && kind<3u && join_read(l+0x38,s,16) && s[1]>=s[0] && !((s[1]-s[0])&7u) && (s[1]-s[0])/8u<=64u) {
                    int has=0;at+=(size_t)snprintf(out+at,cap-at,"{");
                    for(uintptr_t q=s[0];q<s[1] && at+64<cap;q+=8u) {
                        unsigned char *x=NULL;uint64_t rt=0;unsigned klass;
                        if(!join_read((void *)q,&x,8) || !x) continue;
                        if(x==o) has=1;
                        if(q<s[0]+64u && join_read(x+8,&rt,8)) at+=(size_t)snprintf(out+at,cap-at,"%llx,",(unsigned long long)(rt?join_key(rt,&klass):0));
                    }
                    at+=(size_t)snprintf(out+at,cap-at,"}%s",has?"":"!");
                }
                at+=(size_t)snprintf(out+at,cap-at,";");
            }
            at+=(size_t)snprintf(out+at,cap-at,"|");
        }
    } else at+=(size_t)snprintf(out+at,cap-at,"-");
    {uintptr_t s[2]={0,0};
     if(join_read(o+0x38,s,16) && s[1]>=s[0] && !((s[1]-s[0])&7u)) {
        at+=(size_t)snprintf(out+at,cap-at," v38=%llu:",(unsigned long long)((s[1]-s[0])/8u));
        for(uintptr_t p=s[0];p<s[1] && p<s[0]+64u && at+64<cap;p+=8u) {
            unsigned char *x=NULL;uint64_t rt=0;unsigned klass;
            if(join_read((void *)p,&x,8) && x && join_read(x+8,&rt,8)) at+=(size_t)snprintf(out+at,cap-at,"%llx%s;",(unsigned long long)(rt?join_key(rt,&klass):0),x==o?"=self":"");
        }
     }}
    {unsigned char *hdr=o+0x50,*node=NULL;uint64_t size=0;
     (void)join_read(hdr+0x28,&size,8);at+=(size_t)snprintf(out+at,cap-at," m50=%llu:",(unsigned long long)size);
     if(join_read(hdr+8,&node,8) && node && node!=hdr) {
        unsigned char *l=NULL;for(unsigned g=0;g<64 && join_read(node+8,&l,8) && l;g++) node=l;   /* leftmost */
        for(unsigned k=0;k<128 && node && node!=hdr && at+128<cap;k++,node=join_evq_next(node)) {
            int32_t type=0;unsigned char *li=NULL;uint64_t ow=0,sc=0;
            if(!join_read(node+0x20,&type,4) || !join_read(node+0x28,&li,8)) break;
            const unsigned kind=join_lstn_key(&e,li,&ow,&sc);
            at+=(size_t)snprintf(out+at,cap-at,"%d/%u.%llx.%08x;",type,kind,(unsigned long long)ow,(uint32_t)sc);
        }
     }}
    at+=(size_t)snprintf(out+at,cap-at,"\r\n");
    return at<cap?at:cap;
}
/* JOIN_RSTORE (diagnostic, with JOIN_WATCH; j1-391): the late join's salvage pilot 0x9c696 finds no region recyclable at 3093 on
 * the joiner (FindRegionObjectWithYieldInSectorAction 0xbf5f80: the search 0xeee270 over the RegionManager's store, then the spawn
 * 0x7884a0 when it found none), the session one of zone 0x21fd6's two; the joiner's extra recyclable takes an object code and
 * every later code shifts (the 3318 job quotas). The store (lockstep_region_camera.h field_fill: *(0x6cf21e0)+0x60, entries
 * +0x40..+0x48; entry +0x8e0 the cell's zone, +0x10 its field tree, node +0x20 the field, +0x28 its record list, +0x30 the count;
 * a record 0xa0 bytes, consumed +0x90): per entry the cell's key, its fields, records and consumed ones. */
static size_t join_rstore_dump(char *out,size_t cap,char side,uint64_t tick) {
    unsigned char *manager=NULL,**v[2]={NULL,NULL};size_t at=0;uint64_t total=0,consumed=0;
    if(!join_read(image+0x6cf21e0u,&manager,8) || !manager || !join_read(manager+0x60+0x40,v,16) || v[1]<v[0]) return 0;
    const size_t n=(size_t)(v[1]-v[0]);
    at+=(size_t)snprintf(out+at,cap-at,"[X4LAN] NATIVE_GATE JOIN_RSTORE side=%c tick=%llu entries=%llu\r\n",side,(unsigned long long)tick,(unsigned long long)n);
    for(size_t i=0;i<n && i<4096u && at+512<cap;i++) {
        unsigned char *entry=NULL,*cell=NULL,*walk=NULL;uint64_t runtime=0,key=0;unsigned klass;uint32_t fields=0;uint64_t recs=0,cons=0;
        if(!join_read(v[0]+i,&entry,8) || !entry) continue;
        if(join_read(entry+0x8e0,&cell,8) && cell && join_read(cell+8,&runtime,8) && runtime) key=join_key(runtime,&klass);
        unsigned char *const header=entry+0x10;
        (void)join_read(entry+0x30,&walk,8);
        for(unsigned g=0;walk && walk!=header && g<256u;g++) {
            unsigned char *list=NULL,*node=NULL;++fields;
            if(join_read(walk+0x28,&list,8) && list && join_read(list,&node,8))
                for(unsigned k=0;node && node!=list && k<4096u;k++) {unsigned char c=0;(void)join_read(node+0x90,&c,1);++recs;cons+=c!=0;if(!join_read(node,&node,8)) break;}
            unsigned char *right=NULL;
            if(join_read(walk+0x10,&right,8) && right) {unsigned char *l=NULL;while(join_read(right+8,&l,8) && l) right=l;walk=right;continue;}
            unsigned char *parent=NULL;
            for(unsigned up=0;up<64u;up++) {unsigned char *pl=NULL;if(!join_read(walk,&parent,8) || !parent || !join_read(parent+8,&pl,8)) {parent=NULL;break;}if(pl==walk) break;walk=parent;}
            walk=parent;
        }
        total+=recs;consumed+=cons;
        at+=(size_t)snprintf(out+at,cap-at,"[X4LAN] NATIVE_GATE JOIN_RSTORE side=%c tick=%llu i=%llu cell=%llx fields=%u records=%llu consumed=%llu\r\n",side,
                             (unsigned long long)tick,(unsigned long long)i,(unsigned long long)key,fields,(unsigned long long)recs,(unsigned long long)cons);
    }
    at+=(size_t)snprintf(out+at,cap-at,"[X4LAN] NATIVE_GATE JOIN_RSTORE side=%c tick=%llu total records=%llu consumed=%llu\r\n",side,(unsigned long long)tick,
                         (unsigned long long)total,(unsigned long long)consumed);
    return at<cap?at:cap;
}
/* JOIN_RGNOBJ (diagnostic, with JOIN_WATCH on a region; j1-393): FindRegionObjectWithYieldInSectorAction's first search 0x7680a0
 * walks a region's own object list +0x2a0..+0x2a8 (runtime IDs; a Recyclable, isa 0x49, that can be towed or dismantled, by
 * distance through 0x3dde10's published pose) before its fields +0x288; the store search 0xeee270 only at sector attention 5 or
 * more. Per watched U::Region: the list in order as portable keys, and its fields' count. */
#define JOIN_RGN_VTABLE 0x2b2ad60u
/* The joiner's runtime ID of a watched portable: its own table's, else (an object its identity table leaves out, a region) the
 * session's runtime ID when that object exists here (the load keeps saved IDs). */
static uint64_t join_watch_runtime(const join_tables *s,const join_tables *mine,uint64_t portable) {
    uint64_t r=join_runtime_of(mine,portable);
    if(r) return r;
    for(uint32_t k=0;k<s->nids;k++) if(s->ids[k].portable==portable) return join_object(s->ids[k].runtime)?s->ids[k].runtime:0;
    return 0;
}
static size_t join_rgnobj_dump(char *out,size_t cap,char side,uint64_t tick,uint64_t portable,unsigned char *o) {
    uintptr_t vt=0,v[2]={0,0},f[2]={0,0};size_t at=0;
    if(!join_read(o,&vt,8) || vt!=(uintptr_t)image+JOIN_RGN_VTABLE || !join_read(o+0x2a0,v,16) || v[1]<v[0] || (v[1]-v[0])&7u) return 0;
    (void)join_read(o+0x288,f,16);
    at+=(size_t)snprintf(out+at,cap-at,"[X4LAN] NATIVE_GATE JOIN_RGNOBJ side=%c tick=%llu id=%llx fields=%llu objects=%llu:",side,(unsigned long long)tick,
                         (unsigned long long)portable,(unsigned long long)(f[1]>=f[0]?(f[1]-f[0])/8u:0),(unsigned long long)((v[1]-v[0])/8u));
    for(uintptr_t p=v[0];p<v[1] && p<v[0]+8u*512u && at+40<cap;p+=8u) {
        uint64_t runtime=0;unsigned klass;
        if(join_read((void *)p,&runtime,8)) at+=(size_t)snprintf(out+at,cap-at,"%s%llx",p==v[0]?"":",",(unsigned long long)(runtime?join_key(runtime,&klass):0));
    }
    /* every field with objects in its spawned list (+0x90): index:length, the first six as portable keys */
    for(uintptr_t q=f[0];f[1]>f[0] && q<f[1] && q<f[0]+8u*512u && at+256<cap;q+=8) {
        unsigned char *field=NULL,*sent=NULL,*node=NULL;unsigned n=0;uint64_t keys[6];
        if(!join_read((void *)q,&field,8) || !field || !join_read(field+0x90,&sent,8) || !sent || !join_read(sent,&node,8)) continue;
        for(unsigned g=0;node && node!=sent && g<100000u;g++) {
            unsigned char *obj=NULL;uint64_t rt=0;unsigned klass;
            if(n<6u) {keys[n]=0;if(join_read(node+0x10,&obj,8) && obj && join_read(obj+8,&rt,8) && rt) keys[n]=join_key(rt,&klass);if(!keys[n]) keys[n]=rt|(1ull<<62);}
            ++n;if(!join_read(node,&node,8)) break;
        }
        if(!n) continue;
        at+=(size_t)snprintf(out+at,cap-at," f%u:%u[",(unsigned)((q-f[0])/8u),n);
        for(unsigned k=0;k<n && k<6u;k++) at+=(size_t)snprintf(out+at,cap-at,"%s%llx",k?",":"",(unsigned long long)keys[k]);
        at+=(size_t)snprintf(out+at,cap-at,"]");
    }
    at+=(size_t)snprintf(out+at,cap-at,"\r\n");
    return at<cap?at:cap;
}
/* JOIN_RECYF (diagnostic, with JOIN_WATCH on a U::Recyclable; j1-396): the salvage search's field pass 0xedd0c0 walks each of a
 * region's fields (+0x288 vector) and its spawned-object list (field +0x90: a sentinel node, node +0x10 the object). Per watched
 * recyclable: every offset of its first 0x1000 bytes holding a U::Region pointer, and for that region the fields whose list holds
 * the recyclable (index:list length), or none. */
#define JOIN_RECY_VTABLE 0x2acca90u
static size_t join_recyf_dump(char *out,size_t cap,char side,uint64_t tick,uint64_t portable,unsigned char *o) {
    uintptr_t vt=0;size_t at=0;
    if(!join_read(o,&vt,8) || vt!=(uintptr_t)image+JOIN_RECY_VTABLE) return 0;
    at+=(size_t)snprintf(out+at,cap-at,"[X4LAN] NATIVE_GATE JOIN_RECYF side=%c tick=%llu id=%llx",side,(unsigned long long)tick,(unsigned long long)portable);
    unsigned found=0;
    for(unsigned off=8;off<0x1000u && at+512<cap;off+=8) {
        unsigned char *p=NULL;uintptr_t pvt=0;
        if(!join_read(o+off,&p,8) || !p || !join_read(p,&pvt,8) || pvt!=(uintptr_t)image+JOIN_RGN_VTABLE) continue;
        ++found;uintptr_t f[2]={0,0};uint64_t rt=0;unsigned klass;(void)join_read(p+8,&rt,8);
        at+=(size_t)snprintf(out+at,cap-at," region@%x=%llx",off,(unsigned long long)(rt?join_key(rt,&klass):0));
        if(!join_read(p+0x288,f,16) || f[1]<f[0] || (f[1]-f[0])/8u>4096u) continue;
        int any=0;
        for(uintptr_t q=f[0];q<f[1] && at+64<cap;q+=8) {
            unsigned char *field=NULL,*sent=NULL,*node=NULL;unsigned n=0;int in=0;
            if(!join_read((void *)q,&field,8) || !field || !join_read(field+0x90,&sent,8) || !sent || !join_read(sent,&node,8)) continue;
            for(unsigned g=0;node && node!=sent && g<100000u;g++) {unsigned char *obj=NULL;(void)join_read(node+0x10,&obj,8);if(obj==o) in=1;++n;if(!join_read(node,&node,8)) break;}
            if(in) {at+=(size_t)snprintf(out+at,cap-at," in=%u:%u",(unsigned)((q-f[0])/8u),n);any=1;}
        }
        if(!any) at+=(size_t)snprintf(out+at,cap-at," in=none");
    }
    if(!found) at+=(size_t)snprintf(out+at,cap-at," region=none");
    at+=(size_t)snprintf(out+at,cap-at,"\r\n");
    return at<cap?at:cap;
}
static void join_watch_dump(char side,uint64_t tick) {
    if(!join_watch.n) return;
    {size_t cap=1u<<20;char *o=malloc(cap);if(o) {const size_t at=join_rstore_dump(o,cap,side,tick);if(at) crash_note_write(o,(int)at);free(o);}}
    if(side=='t' || side=='k' || side=='s') join_npcslot_dump(side,tick);
    if(side=='t' || side=='k' || side=='s') {join_jobdump(side,tick);join_jobq_dump(side,tick);}
    if(side=='t') join_objsample_dump(tick);
    if(side=='t') join_refcounts_dump(tick);
    {static int tried;if(!tried && thunk) {tried=1;(void)join_pcf_install();}}   /* the thunk page exists from the gate's start */
    join_rprt_install();
    const uint32_t cur=join_view_index();size_t cap=1u<<18,at=0;char *out=malloc(cap);
    if(!out) return;
    for(unsigned i=0;i<join_watch.n;i++) {
        unsigned char *level=join_watch.runtime[i]?join_object(join_watch.runtime[i]):NULL,*below=NULL;
        join_watch.object[i]=level;
        if(!level) {at+=(size_t)snprintf(out+at,cap-at,"[X4LAN] NATIVE_GATE JOIN_WATCH side=%c tick=%llu id=%llx missing\r\n",side,
                                         (unsigned long long)tick,(unsigned long long)join_watch.portable[i]);continue;}
        {   /* JOIN_WATCHREFS after T+1, T+50 and T+57 (j1-181: the escape pod destroyed at 683 = T+50, the joiner's still alive at 690; j1-185: docked ships at T+1) */
            const uint64_t rel=tick-join_watch.tick;
            if((rel==1u || rel==50u || rel==57u) && at+65536u<cap) at+=join_watch_refs(out+at,cap-at,side,tick,join_watch.portable[i],level);
        }
        if(at+65536u<cap) at+=join_lstn_dump(out+at,cap-at,side,tick,join_watch.portable[i],level);   /* JOIN_LSTN: its listeners in order */
        if(at+65536u<cap) at+=join_rgnobj_dump(out+at,cap-at,side,tick,join_watch.portable[i],level);   /* JOIN_RGNOBJ: a region's objects */
        if(at+65536u<cap) at+=join_recyf_dump(out+at,cap-at,side,tick,join_watch.portable[i],level);   /* JOIN_RECYF: a recyclable's field lists */
        for(unsigned lvl=0;lvl<JOIN_WATCH_LEVELS && level && at+2600<cap;lvl++) {
            uintptr_t vt=0;unsigned char *parent=NULL;uint32_t flags=0;
            (void)join_read(level,&vt,8);(void)join_read(level+JOIN_WATCH_PARENT,&parent,8);(void)join_read(level+JOIN_WATCH_FLAGS,&flags,4);
            const uint32_t rva=vt>=(uintptr_t)image && vt-(uintptr_t)image<JOIN_FLIGHT_IMAGE?(uint32_t)(vt-(uintptr_t)image):0u;
            at+=(size_t)snprintf(out+at,cap-at,"[X4LAN] NATIVE_GATE JOIN_WATCH side=%c tick=%llu id=%llx lvl=%u key=%llx vt=%x par=%llx f=%08x cco=%d",
                                 side,(unsigned long long)tick,(unsigned long long)join_watch.portable[i],lvl,
                                 (unsigned long long)join_watch_key(level),rva,(unsigned long long)join_watch_key(parent),flags,
                                 join_watch_call(level,0x1340u,0,0));
            for(uint32_t rel=0;rel<2u;rel++) {
                const unsigned char *view=level+JOIN_VIEW_SLOTS+(size_t)(cur^rel)*0x80u;unsigned char *vp=NULL;uintptr_t ch[2]={0,0};
                uint32_t vf=0;uint64_t nch=0;int has=-1;
                (void)join_read(view,&vp,8);(void)join_read(view+0x3c,&vf,4);
                if(join_read(view+0x10,ch,16) && ch[1]>=ch[0] && !((ch[1]-ch[0])&7u) && (ch[1]-ch[0])/8u<=65536u) {
                    nch=(ch[1]-ch[0])/8u;
                    if(below) {
                        has=0;
                        for(uint64_t k=0;k<nch;k++) {uintptr_t c=0;if(join_read((void *)(ch[0]+k*8u),&c,8) && c==(uintptr_t)below) {has=1;break;}}
                    }
                }
                at+=(size_t)snprintf(out+at,cap-at," v%u=%llx:%llu:%d:%08x",rel,(unsigned long long)join_watch_key(vp),(unsigned long long)nch,has,vf);
                if(!lvl) {   /* the published pose's position (view +0x28; '-' none: the radar takes the live transform) */
                    unsigned char *pose=NULL;float pos[3];
                    if(join_read(view+JOIN_VIEW_POSE,&pose,8) && pose && join_read(pose,pos,12))
                        at+=(size_t)snprintf(out+at,cap-at,":p=%08x,%08x,%08x",*(uint32_t *)&pos[0],*(uint32_t *)&pos[1],*(uint32_t *)&pos[2]);
                    else at+=(size_t)snprintf(out+at,cap-at,":p=-");
                }
                if(!lvl) {   /* docked internally in this view, as Ship slot 1005 asks it */
                    unsigned char *live=join_watch_live(vp);int dock=-1;
                    if(live) {int isa=join_watch_call(live,0x11e8u,0x28,1);dock=isa<=0?isa:join_watch_call(live,0x13a0u,0,0);}
                    at+=(size_t)snprintf(out+at,cap-at,":%d",dock);
                }
            }
            if(!lvl) {
                unsigned char *w=join_grav_worker(),*unit=NULL;uintptr_t uvt=0;uint64_t runtime=join_watch.runtime[i];
                at+=(size_t)snprintf(out+at,cap-at," isa49=%d",join_watch_call(level,0x11e8u,0x49,1));
                {uint32_t attention=0;(void)join_read(level+0x1ec,&attention,4);at+=(size_t)snprintf(out+at,cap-at," att=%u",attention);}
                /* ctl=: the control link 0x44d790 (a controllable's control entity setter) writes on the entity: +0x3c0 (the post's
                 * +0x118), whether +0x3c8 (the post) is set, the flags +0x3d0 and +0x6a30 (the controlled object's runtime ID, when the
                 * post's +0x110 is set) as a portable key. j1-343: the waiting minelayers' pilots are not 'controlled' on the joiner. */
                {
                    uint64_t c0=0,c8=0,cid=0;uint32_t d0=0;unsigned kl=0;
                    (void)join_read(level+0x3c0,&c0,8);(void)join_read(level+0x3c8,&c8,8);(void)join_read(level+0x3d0,&d0,4);
                    (void)join_read(level+0x6a30,&cid,8);
                    uint64_t ckey=cid?join_key(cid,&kl):0;
                    const uintptr_t c8i=(uintptr_t)c8>=(uintptr_t)image && (uintptr_t)c8-(uintptr_t)image<JOIN_FLIGHT_IMAGE?(uintptr_t)c8-(uintptr_t)image:0;
                    at+=(size_t)snprintf(out+at,cap-at," ctl=%llx:%d:%llx:%08x:%llx",(unsigned long long)c0,c8!=0,(unsigned long long)c8i,d0,
                                         (unsigned long long)(ckey?ckey:cid?(cid|(1ull<<62)):0));
                    /* A U::NPC (vtable 0x2b38a48) only: its first reference slot (+0x308 vector) holding a controllable (class 0x70) as
                     * a portable key, and vt+0x1c50(1) (0x6c71f0, the test behind 'controlled': flags +0x3d0, +0x3d8, that slot, its
                     * ancestor chain, the post's control position 0x455ce0 and the NPC's link of kind 2 at +0xc0 to it). */
                    uintptr_t evt=0;
                    if(join_read(level,&evt,8) && evt==(uintptr_t)image+0x2b38a48u) {
                        unsigned char *sb=NULL,*se=NULL,*slot=NULL;uint64_t skey=0;
                        if(join_read(level+0x308,&sb,8) && join_read(level+0x310,&se,8) && sb && se>=sb && (size_t)(se-sb)<=0x400u)
                            for(unsigned char *q=sb;q<se;q+=8) if(join_read(q,&slot,8) && slot && join_watch_call(slot,0x11e8u,0x70,1)==1) {skey=join_watch_key(slot);break;}
                        at+=(size_t)snprintf(out+at,cap-at,":%llx:%d",(unsigned long long)skey,join_watch_call(level,0x1c50u,1,1));
                        /* links=: the connection links at *(+0xc0) (0x49aac0's container: 0x18-byte buckets by kind, begin +0, end +8,
                         * count +0x18; a bucket's list head +0, size +8): each kind 1 .. 4's list size, then kind 2's first link (node
                         * +0x20: +0x18 the target component as a portable key, +0x20 set). 0x6c71f0 wants kind 2's target to be the
                         * post's control position. */
                        unsigned char *lc=NULL,*lb=NULL,*le=NULL;uint32_t lcount=0;
                        at+=(size_t)snprintf(out+at,cap-at," links=");
                        if(join_read(level+0xc0,&lc,8) && lc && join_read(lc,&lb,8) && join_read(lc+8,&le,8) && join_read(lc+0x18,&lcount,4) &&
                           lb && le>=lb && (size_t)(le-lb)<=0x18u*16u) {
                            const unsigned nb=(unsigned)((size_t)(le-lb)/0x18u);
                            at+=(size_t)snprintf(out+at,cap-at,"%u/%u",lcount,nb);
                            for(unsigned k=0;k<4u && k<nb;k++) {
                                uint64_t lsize=0;(void)join_read(lb+k*0x18u+8u,&lsize,8);at+=(size_t)snprintf(out+at,cap-at,":%llu",(unsigned long long)lsize);
                            }
                            unsigned char *head=NULL,*node=NULL,*link=NULL,*tgt=NULL,*tslot=NULL;uint64_t lsize=0;
                            if(nb>=2u && join_read(lb+0x18u+8u,&lsize,8) && lsize && join_read(lb+0x18u,&head,8) && head && join_read(head,&node,8) &&
                               node && node!=head && join_read(node+0x20,&link,8) && link) {
                                (void)join_read(link+0x18,&tgt,8);(void)join_read(link+0x20,&tslot,8);
                                at+=(size_t)snprintf(out+at,cap-at,":k2=%llx/%d",(unsigned long long)join_watch_key(tgt),tslot!=NULL);
                            }
                            /* j1-348: the joiner's kind-2 entry has no link. The bucket, its first node (+0x10 .. +0x40: the payload), the
                             * link's vtable RVA and first 0x40 bytes and the slot's vtable RVA and first 0x40 bytes, pointers raw. */
                            if(nb>=2u) {
                                at+=(size_t)snprintf(out+at,cap-at," k2b=");at+=join_watch_hex(out+at,cap-at,lb+0x18u,0x18);
                                if(head && node && node!=head) {at+=(size_t)snprintf(out+at,cap-at," k2n=");at+=join_watch_hex(out+at,cap-at,node,0x40);}
                                uintptr_t lvt=0,svt=0;
                                if(link && join_read(link,&lvt,8)) {
                                    at+=(size_t)snprintf(out+at,cap-at," k2l=%x:",lvt>=(uintptr_t)image && lvt-(uintptr_t)image<JOIN_FLIGHT_IMAGE?(uint32_t)(lvt-(uintptr_t)image):0u);
                                    at+=join_watch_hex(out+at,cap-at,link,0x40);
                                }
                                if(tslot && join_read(tslot,&svt,8)) {
                                    at+=(size_t)snprintf(out+at,cap-at," k2s=%x:",svt>=(uintptr_t)image && svt-(uintptr_t)image<JOIN_FLIGHT_IMAGE?(uint32_t)(svt-(uintptr_t)image):0u);
                                    at+=join_watch_hex(out+at,cap-at,tslot,0x40);
                                }
                            }
                        } else at+=(size_t)snprintf(out+at,cap-at,"-");
                    }
                }
                for(uint32_t rel=0;rel<2u;rel++) {   /* both view blocks whole (pointers raw) */
                    at+=(size_t)snprintf(out+at,cap-at," vb%u=",rel);
                    at+=join_watch_hex(out+at,cap-at,level+JOIN_VIEW_SLOTS+(size_t)(cur^rel)*0x80u,0x40);
                    at+=join_watch_hex(out+at,cap-at,level+JOIN_VIEW_SLOTS+(size_t)(cur^rel)*0x80u+0x40u,0x40);
                }
                if(w) {join_grav_lock(w+0x88+(runtime&0xffu)*0x28u,1);unit=join_grav_lookup(w,runtime);join_grav_lock(w+0x88+(runtime&0xffu)*0x28u,0);}
                if(unit && join_read(unit,&uvt,8)) {
                    uintptr_t b=0;uint64_t n68=0,n80=0;const int active=uvt==(uintptr_t)image+JOIN_GRAV_ACTIVE_VT;
                    at+=(size_t)snprintf(out+at,cap-at," unit=%x u40=",(uint32_t)(uvt-(uintptr_t)image));
                    at+=join_watch_hex(out+at,cap-at,unit+0x40,0x28);
                    if(active) {
                        (void)join_grav_vector(unit+0x68,&b,&n68);(void)join_grav_vector(unit+0x80,&b,&n80);
                        at+=(size_t)snprintf(out+at,cap-at," ua8=");at+=join_watch_hex(out+at,cap-at,unit+0xa8,0x20);
                        at+=(size_t)snprintf(out+at,cap-at," n=%llu:%llu",(unsigned long long)n68,(unsigned long long)n80);
                        /* the primary contacts as portable keys, sorted (bit 62: a runtime ID without a key), up to 64 */
                        uint64_t keys[64];unsigned nk=0;
                        if(join_grav_vector(unit+0x68,&b,&n68) && n68 && n68<=64u && join_read((void *)b,join_grav_ids,(size_t)n68*8u)) {
                            for(uint64_t k=0;k<n68;k++) {unsigned klass;uint64_t key=join_grav_ids[k]?join_key(join_grav_ids[k],&klass):0;keys[nk++]=key?key:(join_grav_ids[k]|(1ull<<62));}
                            qsort(keys,nk,8,join_watch_key_cmp);
                        }
                        at+=(size_t)snprintf(out+at,cap-at," c=");
                        for(unsigned k=0;k<nk && at+20<cap;k++) at+=(size_t)snprintf(out+at,cap-at,"%s%llx",k?",":"",(unsigned long long)keys[k]);
                    }
                } else at+=(size_t)snprintf(out+at,cap-at," unit=-");
            }
            at+=(size_t)snprintf(out+at,cap-at,"\r\n");
            if(!lvl && at+16384u<cap) {
                /* JOIN_WATCHSPEC: where the subtree's view cache specification entries sit. +0x1e8 counts them over the subtree
                 * (AddCacheSpecification 0x3d5180 keeps it up the parent chain; BuildCache skips a subtree at 0); the list +0xd8
                 * (+0x80 capacity, inline at 4, +0x88 count; 0x20-byte entries kind, flags, class, truncation, interval, last).
                 * j1-161: actor 5's +0x1e8 is 295 on the session, 2 on the joiner, and SPEC carries none of them. Depth-first over
                 * the current view's published children (+0x10/+0x18) into those with a count, up to 400 nodes; per node with
                 * entries of its own: key, vtable RVA, +0x1e8, +0x1e0, the list's count and up to 4 entries. */
                unsigned char *stack[96];unsigned sp=0,nodes=0,shown=0;uint32_t total=0;
                at+=(size_t)snprintf(out+at,cap-at,"[X4LAN] NATIVE_GATE JOIN_WATCHSPEC side=%c tick=%llu id=%llx",side,
                                     (unsigned long long)tick,(unsigned long long)join_watch.portable[i]);
                stack[sp++]=level;
                while(sp && nodes<400u && at+512u<cap) {
                    unsigned char *o=stack[--sp],*list=NULL,*ents=NULL;uint32_t c1e8=0,c1e0=0;uint64_t lcap=0,lcount=0;uintptr_t ovt=0;
                    ++nodes;(void)join_read(o,&ovt,8);(void)join_read(o+0x1e8,&c1e8,4);(void)join_read(o+0x1e0,&c1e0,4);
                    if(join_read(o+0xd8u,&list,8) && list && join_read(list+0x80,&lcap,8) && join_read(list+0x88,&lcount,8) && lcount<=lcap) {
                        ents=list;
                        if(lcap!=4u && !join_read(list,&ents,8)) ents=NULL;
                    } else lcount=0;
                    total+=(uint32_t)lcount;
                    if(lcount && shown<64u) {
                        ++shown;
                        at+=(size_t)snprintf(out+at,cap-at," %llx/%x:%u:%x:%llu",(unsigned long long)join_watch_key(o),
                                             ovt>=(uintptr_t)image?(uint32_t)(ovt-(uintptr_t)image):0u,c1e8,c1e0,(unsigned long long)lcount);
                        for(uint64_t k=0;k<lcount && k<4u && ents;k++) {
                            uint32_t e4[4]={0,0,0,0};uint64_t iv=0;
                            if(!join_read(ents+k*0x20u,e4,16) || !join_read(ents+k*0x20u+16u,&iv,8)) break;
                            at+=(size_t)snprintf(out+at,cap-at,"%s%u.%x.%u.%u.%llx",k?",":"[",e4[0],e4[1],e4[2],e4[3],(unsigned long long)iv);
                            if(k+1u==lcount || k==3u) at+=(size_t)snprintf(out+at,cap-at,"]");
                        }
                    }
                    uintptr_t ch[2]={0,0};
                    if(c1e8 && join_read(o+JOIN_VIEW_SLOTS+(size_t)cur*0x80u+0x10u,ch,16) && ch[1]>=ch[0] && (ch[1]-ch[0])/8u<=4096u)
                        for(uintptr_t q=ch[0];q<ch[1] && sp<96u;q+=8u) {
                            unsigned char *c=NULL;uint32_t cc=0;
                            if(join_read((void *)q,&c,8) && c && join_read(c+0x1e8,&cc,4) && cc) stack[sp++]=c;
                        }
                }
                at+=(size_t)snprintf(out+at,cap-at," nodes=%u own=%u\r\n",nodes,total);
            }
            if(!lvl && at+2u*(JOIN_WATCH_RAW+JOIN_WATCH_RAW_REQ+JOIN_WATCH_RAW_MOV+JOIN_WATCH_RAW_AGENT+JOIN_WATCH_RAW_CMD+JOIN_WATCH_RAW_BEH)+400u<cap) {
                /* JOIN_WATCHRAW: the object's first bytes and those of what +0x318 points at (a weapon's fire request, which
                 * Weapon::Move 0x819a90 asks through its slot +0x80 before the fire procedure 0x817d20), pointers raw. */
                unsigned char *req=NULL;
                at+=(size_t)snprintf(out+at,cap-at,"[X4LAN] NATIVE_GATE JOIN_WATCHRAW side=%c tick=%llu id=%llx obj=",side,
                                     (unsigned long long)tick,(unsigned long long)join_watch.portable[i]);
                for(uint32_t o=0;o<JOIN_WATCH_RAW;o+=64u) at+=join_watch_hex(out+at,cap-at,level+o,64);
                at+=(size_t)snprintf(out+at,cap-at," req=");
                if(join_read(level+JOIN_WATCH_REQ,&req,8) && req) for(uint32_t o=0;o<JOIN_WATCH_RAW_REQ;o+=64u) at+=join_watch_hex(out+at,cap-at,req+o,64);
                else at+=(size_t)snprintf(out+at,cap-at,"-");
                /* j1-164: the session's shoot controllers of actor 5's two weapons hold the same two heap pointers at +0x90 and +0x98,
                 * the joiner's none, and the joiner's line-of-fire check comes out clear at 650 (one shot more). What they point at:
                 * per pointer its key (as a component: +8 a runtime ID with a portable key, else '-'), vtable RVA and first 0x80 bytes. */
                if(req) for(unsigned k=0;k<2u;k++) {
                    unsigned char *pp=NULL;uintptr_t pvt=0;
                    at+=(size_t)snprintf(out+at,cap-at," r%x=",0x90u+8u*k);
                    if(!join_read(req+0x90u+8u*k,&pp,8) || !pp) {at+=(size_t)snprintf(out+at,cap-at,"-");continue;}
                    (void)join_read(pp,&pvt,8);
                    at+=(size_t)snprintf(out+at,cap-at,"%llx/%x:",(unsigned long long)join_watch_key(pp),
                                         pvt>=(uintptr_t)image && pvt-(uintptr_t)image<JOIN_FLIGHT_IMAGE?(uint32_t)(pvt-(uintptr_t)image):0u);
                    at+=join_watch_hex(out+at,cap-at,pp,64);at+=join_watch_hex(out+at,cap-at,pp+64,64);
                }
                /* JOIN_WATCHATT (after this row): the components in its attachment container (+0xa0: a bucketed container,
                 * +0x08/+0x10 buckets of 0x20 {begin, end, ..}, +0x20 count; BuildCache refreshes each with 0x7f) and what +0x238 holds */
                char line[2400];size_t ln=0;
                {
                    unsigned char *att=NULL,*p238=NULL;uintptr_t bk[2]={0,0};uint32_t natt=0;
                    ln=(size_t)snprintf(line,sizeof(line),"[X4LAN] NATIVE_GATE JOIN_WATCHATT side=%c tick=%llu id=%llx att=",
                                        side,(unsigned long long)tick,(unsigned long long)join_watch.portable[i]);
                    if(join_read(level+0xa0,&att,8) && att && join_read(att+0x08,bk,16) && bk[1]>=bk[0] && (bk[1]-bk[0])/0x20u<=64u) {
                        (void)join_read(att+0x20,&natt,4);
                        for(uintptr_t b=bk[0];b<bk[1] && ln+40<sizeof(line);b+=0x20u) {
                            uintptr_t e[2]={0,0};
                            if(!join_read((void *)b,e,16) || e[1]<e[0] || (e[1]-e[0])/8u>64u) continue;
                            for(uintptr_t q=e[0];q<e[1] && ln+40<sizeof(line);q+=8u) {
                                unsigned char *c=NULL;
                                if(join_read((void *)q,&c,8) && c) ln+=(size_t)snprintf(line+ln,sizeof(line)-ln,"%llx,",(unsigned long long)join_watch_key(c));
                            }
                        }
                    } else ln+=(size_t)snprintf(line+ln,sizeof(line)-ln,"-");
                    ln+=(size_t)snprintf(line+ln,sizeof(line)-ln," n=%u p238=",natt);
                    if(join_read(level+0x238,&p238,8) && p238) {
                        unsigned char hx[0x40];
                        if(join_read(p238,hx,sizeof(hx))) for(unsigned k=0;k<sizeof(hx) && ln+4<sizeof(line);k++) ln+=(size_t)snprintf(line+ln,sizeof(line)-ln,"%02x",hx[k]);
                        ln+=(size_t)snprintf(line+ln,sizeof(line)-ln," p238key=%llx",(unsigned long long)join_watch_key(join_watch_live(p238)));
                    } else ln+=(size_t)snprintf(line+ln,sizeof(line)-ln,"-");
                    /* j1-171: the joiner's weapon frames sit at their rest pose. The frame slot 0x1ec8 = 0x81c960 takes the bone
                     * transform (0xe22b70) when the animation container +0x208 has a skeleton at +0x48, else the static connection
                     * transform. a208: the container's first 0x80 bytes; a48: the skeleton's vtable RVA and first 0x80 bytes;
                     * a48o: what the skeleton's +0 points at (its owner, key and vtable RVA). */
                    unsigned char *a208=NULL,*a48=NULL,*a48o=NULL;uintptr_t avt=0,aovt=0;
                    if(ln+800u<sizeof(line)) ln+=(size_t)snprintf(line+ln,sizeof(line)-ln," a208=");
                    if(ln+800u<sizeof(line) && join_read(level+0x208,&a208,8) && a208) {
                        unsigned char hx[0x80];
                        if(join_read(a208,hx,sizeof(hx))) for(unsigned k=0;k<sizeof(hx) && ln+4<sizeof(line);k++) ln+=(size_t)snprintf(line+ln,sizeof(line)-ln,"%02x",hx[k]);
                        ln+=(size_t)snprintf(line+ln,sizeof(line)-ln," a48=");
                        if(join_read(a208+0x48,&a48,8) && a48 && join_read(a48,&avt,8)) {
                            ln+=(size_t)snprintf(line+ln,sizeof(line)-ln,"%x:",avt>=(uintptr_t)image && avt-(uintptr_t)image<JOIN_FLIGHT_IMAGE?(uint32_t)(avt-(uintptr_t)image):0u);
                            if(join_read(a48,hx,sizeof(hx))) for(unsigned k=0;k<sizeof(hx) && ln+4<sizeof(line);k++) ln+=(size_t)snprintf(line+ln,sizeof(line)-ln,"%02x",hx[k]);
                            ln+=(size_t)snprintf(line+ln,sizeof(line)-ln," a48o=");
                            if(join_read(a48,&a48o,8) && a48o && join_read(a48o,&aovt,8))
                                ln+=(size_t)snprintf(line+ln,sizeof(line)-ln,"%llx/%x",(unsigned long long)join_watch_key(join_watch_live(a48o)),
                                                     aovt>=(uintptr_t)image && aovt-(uintptr_t)image<JOIN_FLIGHT_IMAGE?(uint32_t)(aovt-(uintptr_t)image):0u);
                            else ln+=(size_t)snprintf(line+ln,sizeof(line)-ln,"-");
                        } else ln+=(size_t)snprintf(line+ln,sizeof(line)-ln,"-");
                    } else if(ln+800u<sizeof(line)) ln+=(size_t)snprintf(line+ln,sizeof(line)-ln,"-");
                    if(ln>=sizeof(line)) ln=sizeof(line)-1;
                }
                at+=(size_t)snprintf(out+at,cap-at," mov=");
                if(join_read(level+JOIN_WATCH_MOV,&req,8) && req) for(uint32_t o=0;o<JOIN_WATCH_RAW_MOV;o+=64u) at+=join_watch_hex(out+at,cap-at,req+o,64);
                else at+=(size_t)snprintf(out+at,cap-at,"-");
                {   /* movp: the controller's qwords +0x70, +0x78 and +0x128 and 0x80 bytes of what each points at (the loaded and the live
                     * DockMovementController differ there: j1-186) */
                    static const uint32_t at_off[3]={0x70u,0x78u,0x128u};
                    at+=(size_t)snprintf(out+at,cap-at," movp=");
                    for(unsigned q=0;q<3u && req;q++) {
                        unsigned char *pp=NULL;
                        if(!join_read(req+at_off[q],&pp,8)) continue;
                        at+=(size_t)snprintf(out+at,cap-at,"%s%x:%llx:",q?";":"",at_off[q],(unsigned long long)(uintptr_t)pp);
                        if((uintptr_t)pp>0x10000u && !((uintptr_t)pp&7u)) {at+=join_watch_hex(out+at,cap-at,pp,64);at+=join_watch_hex(out+at,cap-at,pp+64,64);}
                    }
                }
                {   /* hj12 (x4mods-28, 9 Nov): actor 5's guns aimed at T+1 from a pose 1.8 ms of travel apart. 0x3db8a0 walks the parents
                     * through the movement controller, whose cached pose (+0x60..+0x9f, in mov= above) RefreshFromPathingState 0xea2a20
                     * rebuilds from the pathing block [controller+0x30]: two 0x1a0-byte entries per selector (a time +0x150, flags
                     * +0x159/+0x15a), the selector the word at 0x38f4ee4. sel: that word; pblk: the block's first 0x340 bytes. */
                    unsigned char *pb=NULL;uint32_t sel=0;
                    (void)join_read(image+0x38f4ee4u,&sel,4);
                    at+=(size_t)snprintf(out+at,cap-at," sel=%x pblk=",sel);
                    if(req && at+2u*0x340u+64u<cap && join_read(req+0x30,&pb,8) && (uintptr_t)pb>0x10000u && !((uintptr_t)pb&7u))
                        for(uint32_t o=0;o<0x340u;o+=64u) at+=join_watch_hex(out+at,cap-at,pb+o,64);
                    else at+=(size_t)snprintf(out+at,cap-at,"-");
                }
                {   /* j1-139..j1-147: actor 5's pathing step writes another command at 651 with every logged field equal before: its
                     * Agent and the controller's command and probe block, pointers raw (a nav or pilot controller only) */
                    unsigned char *ctl=req,*agent=NULL,*cmd=NULL;uintptr_t vt=0;
                    const int nav=ctl && join_read(ctl,&vt,8) && ((vt=pslot_canonical_vtable(vt))==(uintptr_t)image+JOIN_NAV_VTABLE ||
                                                               vt==(uintptr_t)image+PSLOT_VTABLE);
                    at+=(size_t)snprintf(out+at,cap-at," agent=");
                    if(nav && join_read(ctl+0x40,&agent,8) && agent) for(uint32_t o=0;o<JOIN_WATCH_RAW_AGENT;o+=16u) at+=join_watch_hex(out+at,cap-at,agent+o,16);
                    else at+=(size_t)snprintf(out+at,cap-at,"-");
                    at+=(size_t)snprintf(out+at,cap-at," cmd=");
                    if(nav && join_read(ctl+0x48,&cmd,8) && cmd) for(uint32_t o=0;o<JOIN_WATCH_RAW_CMD;o+=64u) at+=join_watch_hex(out+at,cap-at,cmd+o,64);
                    else at+=(size_t)snprintf(out+at,cap-at,"-");
                    /* beh=: the steering model's active behaviour (its vtable RVA, then its first JOIN_WATCH_RAW_BEH bytes, pointers raw) */
                    unsigned char *model=NULL,*beh=NULL;uintptr_t mvt=0,bvt=0;
                    at+=(size_t)snprintf(out+at,cap-at," beh=");
                    if(agent && join_read(agent+JOIN_AGENT_MODEL,&model,8) && model && join_read(model,&mvt,8) && mvt==(uintptr_t)image+JOIN_STEER_VTABLE &&
                       join_read(model+JOIN_STEER_BEHAVIOURS,&beh,8) && beh && join_read(beh,&bvt,8)) {
                        at+=(size_t)snprintf(out+at,cap-at,"%x:",bvt>=(uintptr_t)image?(uint32_t)(bvt-(uintptr_t)image):0u);
                        for(uint32_t o=0;o<JOIN_WATCH_RAW_BEH;o+=64u) at+=join_watch_hex(out+at,cap-at,beh+o,64);
                    } else if(mvt) at+=(size_t)snprintf(out+at,cap-at,"-m%x",mvt>=(uintptr_t)image?(uint32_t)(mvt-(uintptr_t)image):0u);   /* another model */
                    else at+=(size_t)snprintf(out+at,cap-at,"-");
                }
                at+=(size_t)snprintf(out+at,cap-at,"\r\n");
                if(at+ln+4<cap) {memcpy(out+at,line,ln);at+=ln;at+=(size_t)snprintf(out+at,cap-at,"\r\n");}
                {   /* JOIN_WATCHANIM: the units of the weapon's skeleton (+0x208 -> +0x48: +0x10 a vector of groups {begin, end, cap},
                     * each an array of 0x680-byte units), which 0xe20370 evaluates at the thread time for the bone transform. A unit
                     * (0xe1f840 moves it toward a target at a limited rate): +8 the value, +0x10 the time (double), +0x18 the rate,
                     * +0x1c the target, +0x21 wraps, its virtual base (the vbtable at +0, offset at +4) the keyed curve. Per unit: group,
                     * index, the vbase offset and all 0x680 bytes (at most 8 units). */
                    unsigned char *a208=NULL,*sk=NULL;uintptr_t gv[2]={0,0};
                    if(join_read(level+0x208,&a208,8) && a208 && join_read(a208+0x48,&sk,8) && sk && join_read(sk+0x10,gv,16) &&
                       gv[1]>=gv[0] && (gv[1]-gv[0])%0x18u==0 && (gv[1]-gv[0])/0x18u<=16u) {
                        unsigned shown=0;
                        for(uintptr_t g=gv[0];g<gv[1] && shown<8u;g+=0x18u) {
                            uintptr_t ub[2]={0,0};
                            if(!join_read((void *)g,ub,16) || ub[1]<ub[0] || (ub[1]-ub[0])%0x680u) continue;
                            for(uintptr_t u=ub[0];u<ub[1] && shown<8u;u+=0x680u) {
                                if(at+2u*0x680u+300u>=cap) break;
                                unsigned char *vbt=NULL;int32_t vbo=0;
                                if(!join_read((void *)u,&vbt,8) || !vbt || !join_read(vbt+4,&vbo,4)) vbo=0;
                                at+=(size_t)snprintf(out+at,cap-at,"[X4LAN] NATIVE_GATE JOIN_WATCHANIM side=%c tick=%llu id=%llx group=%u unit=%u vbo=%d bytes=",
                                                     side,(unsigned long long)tick,(unsigned long long)join_watch.portable[i],
                                                     (unsigned)((g-gv[0])/0x18u),(unsigned)((u-ub[0])/0x680u),vbo);
                                for(uint32_t o=0;o<0x680u;o+=64u) at+=join_watch_hex(out+at,cap-at,(unsigned char *)u+o,64);
                                at+=(size_t)snprintf(out+at,cap-at,"\r\n");
                                ++shown;
                            }
                        }
                    }
                }
            }
            if(rva==JOIN_GRAV_SECTOR_VT) break;
            below=level;level=join_watch_live(parent);
        }
    }
    {   /* the transform publications of the watched objects since the last dump */
        LONG n=join_pcf_n;if(n>(LONG)JOIN_PCF_ROWS) n=(LONG)JOIN_PCF_ROWS;
        for(LONG k=0;k<n && at+200<cap;k++) {
            const join_pcf_row *r=&join_pcf_rows[k];
            at+=(size_t)snprintf(out+at,cap-at,"[X4LAN] NATIVE_GATE JOIN_WATCHPCF side=%c tick=%llu at=%llu id=%llx site=%x flags=%x view=%u\r\n",side,
                                 (unsigned long long)tick,(unsigned long long)r->tick,(unsigned long long)join_watch.portable[r->watched],r->site,r->flags,r->view);
        }
        at+=(size_t)snprintf(out+at,cap-at,"[X4LAN] NATIVE_GATE JOIN_WATCHPCF side=%c tick=%llu rows=%ld calls=%lld dropped=%lld sites=%x\r\n",side,(unsigned long long)tick,
                             (long)n,(long long)join_pcf_calls,(long long)join_pcf_dropped,join_pcf_mask);
        InterlockedExchange(&join_pcf_n,0);
    }
    {   /* JOIN_RPRT: the relative transforms handed to watched ships' linear models since the last dump */
        LONG n=join_rprt_n;if(n>(LONG)JOIN_RPRT_ROWS) n=(LONG)JOIN_RPRT_ROWS;
        for(LONG k=join_rprt_shown;k<n && at+700<cap;k++) {
            const join_rprt_row *r=&join_rprt_rows[k];
            at+=(size_t)snprintf(out+at,cap-at,"[X4LAN] NATIVE_GATE JOIN_RPRT side=%c tick=%llu at=%llu id=%llx thread=%u m=",side,(unsigned long long)tick,
                                 (unsigned long long)r->tick,(unsigned long long)join_watch.portable[r->watched],r->thread);
            at+=join_watch_hex(out+at,cap-at,(const unsigned char *)r->m,sizeof(r->m));
            at+=(size_t)snprintf(out+at,cap-at," before=");at+=join_watch_hex(out+at,cap-at,(const unsigned char *)r->before,16);
            at+=(size_t)snprintf(out+at,cap-at," after=");at+=join_watch_hex(out+at,cap-at,(const unsigned char *)r->after,16);
            at+=(size_t)snprintf(out+at,cap-at,"\r\n");
        }
        if(n>join_rprt_shown) join_rprt_shown=n;
        if(join_rprt_on) at+=(size_t)snprintf(out+at,cap-at,"[X4LAN] NATIVE_GATE JOIN_RPRT side=%c tick=%llu installed=%d calls=%lld rows=%ld\r\n",side,
                                              (unsigned long long)tick,join_rprt_installed,(long long)join_rprt_calls,(long)join_rprt_n);
    }
    {   /* JOIN_WATCHACOS: the weapon request checks' aim angles since the last dump (lockstep_portable_math.h) */
        LONG n=pmath_acos_n;if(n>(LONG)PMATH_ACOS_ROWS) n=(LONG)PMATH_ACOS_ROWS;
        for(LONG k=0;k<n && at+160<cap;k++) if(pmath_acos_rows[k].tick+1u>=tick)   /* the steps since the last dump */
            at+=(size_t)snprintf(out+at,cap-at,"[X4LAN] NATIVE_GATE JOIN_WATCHACOS side=%c tick=%llu at=%llu x=%08x r=%08x\r\n",side,
                                 (unsigned long long)tick,(unsigned long long)pmath_acos_rows[k].tick,pmath_acos_rows[k].x,pmath_acos_rows[k].r);
        at+=(size_t)snprintf(out+at,cap-at,"[X4LAN] NATIVE_GATE JOIN_WATCHACOS side=%c tick=%llu rows=%ld dropped=%lld\r\n",side,
                             (unsigned long long)tick,(long)n,(long long)pmath_acos_dropped);
        InterlockedExchange(&pmath_acos_n,0);
    }
    {   /* JOIN_WATCHAIM: the aim points since the last dump (installed with JOIN_WATCHRULE) */
        LONG n=join_aim_n;if(n>(LONG)JOIN_AIM_ROWS) n=(LONG)JOIN_AIM_ROWS;
        for(LONG k=0;k<n && at+400<cap;k++) {
            const join_aim_row *r=&join_aim_rows[k];
            at+=(size_t)snprintf(out+at,cap-at,"[X4LAN] NATIVE_GATE JOIN_WATCHAIM side=%c tick=%llu at=%llu target=%llx weapon=%llx "
                                 "origin=%08x,%08x,%08x aim=%08x,%08x,%08x result=%u rot=",side,(unsigned long long)tick,
                                 (unsigned long long)r->tick,(unsigned long long)join_watch_key(r->target),(unsigned long long)join_watch_key(r->weapon),
                                 r->origin[0],r->origin[1],r->origin[2],r->aim[0],r->aim[1],r->aim[2],r->result);
            for(unsigned q=0;q<12u;q++) at+=(size_t)snprintf(out+at,cap-at,"%s%08x",q?",":"",r->rot[q]);
            at+=(size_t)snprintf(out+at,cap-at,"\r\n");
        }
        at+=(size_t)snprintf(out+at,cap-at,"[X4LAN] NATIVE_GATE JOIN_WATCHAIM side=%c tick=%llu rows=%ld dropped=%lld installed=%d\r\n",side,
                             (unsigned long long)tick,(long)n,(long long)join_aim_dropped,join_aim_installed);
        InterlockedExchange(&join_aim_n,0);
    }
    {   /* JOIN_WATCHRULE: the line-of-fire rulings since the last dump (installed at the first dump) */
        join_rule_install();
        LONG n=join_rule_n;if(n>(LONG)JOIN_RULE_ROWS) n=(LONG)JOIN_RULE_ROWS;
        for(LONG k=0;k<n && at+256<cap;k++) {
            const join_rule_row *r=&join_rule_rows[k];
            at+=(size_t)snprintf(out+at,cap-at,"[X4LAN] NATIVE_GATE JOIN_WATCHRULE side=%c tick=%llu at=%llu hit=%llx target=%llx shooter=%llx flag=%u result=%u\r\n",
                                 side,(unsigned long long)tick,(unsigned long long)r->tick,(unsigned long long)join_watch_key(r->hit),
                                 (unsigned long long)join_watch_key(r->target),(unsigned long long)join_watch_key(r->shooter),r->flag,r->result);
        }
        at+=(size_t)snprintf(out+at,cap-at,"[X4LAN] NATIVE_GATE JOIN_WATCHRULE side=%c tick=%llu rows=%ld calls=%lld dropped=%lld installed=%d\r\n",side,
                             (unsigned long long)tick,(long)n,(long long)join_rule_calls,(long long)join_rule_dropped,join_rule_installed);
        InterlockedExchange(&join_rule_n,0);
    }
    {   /* JOIN_GRAVB (with the watch): the radar worker's mapped bounds maps. The Player's Gravidar update 0x10b34c0 copies the base
         * (+0x6e90) entry of an ID into the write buffer's (+0x6e10 + 0x40 x the selector 0x2f4b99c), extends it by its contacts and posts
         * MappedBoundsChangedEvent (0x10b405e, to the Player) when it differs from the read buffer's (selector 0x38f5260); each update
         * starts by copying the read buffer into the write one (0x10aed3c). A tree map: header +8 root, +0x20 leftmost, +0x28 size;
         * node +0 parent, +8 left, +0x10 right, +0x20 the ID, +0x28 x, y, r (floats). Per map its size and up to 32 entries. */
        unsigned char *w=join_grav_worker();uint32_t rsel=0,wsel=0;
        static const uint32_t maps[3]={0x6e10u,0x6e50u,0x6e90u};
        (void)join_read(image+JOIN_GRAVB_READ,&rsel,4);(void)join_read(image+JOIN_GRAVB_WRITE,&wsel,4);
        for(unsigned m=0;w && m<3u && at+4096<cap;m++) {
            unsigned char *head=w+maps[m],*node=NULL;uint64_t size=0;
            (void)join_read(head+0x28,&size,8);(void)join_read(head+0x20,&node,8);
            at+=(size_t)snprintf(out+at,cap-at,"[X4LAN] NATIVE_GATE JOIN_GRAVB side=%c tick=%llu sel=%u:%u map=%x n=%llu e=",side,
                                 (unsigned long long)tick,rsel,wsel,maps[m],(unsigned long long)size);
            for(uint64_t k=0;k<size && k<32u && node && node!=head && at+96<cap;k++) {
                uint64_t key=0,pk=0;uint32_t v[3]={0,0,0};unsigned klass;
                (void)join_read(node+0x20,&key,8);(void)join_read(node+0x28,v,12);
                pk=key?join_key(key,&klass):0;
                at+=(size_t)snprintf(out+at,cap-at,"%s%llx:%08x,%08x,%08x",k?";":"",(unsigned long long)(pk?pk:(key|(1ull<<62))),v[0],v[1],v[2]);
                node=join_gravb_next(head,node);
            }
            at+=(size_t)snprintf(out+at,cap-at,"\r\n");
        }
    }
    if(side=='s' || side=='k' || side=='t' || side=='u' || side=='v') {
        /* JOIN_REFWATCH: armed at the session's capture of T (s), at the joiner's admission before VIEWK (k) and after T+1 (t), the
         * threads started since every 10 steps */
        if(join_rw_at) {if(tick+1u>=join_rw_at && (tick+1u==join_rw_at || !((tick-join_watch.tick)%10u))) join_rw_arm(1);}
        else if(side=='s' || side=='k' || side=='t' || !((tick-join_watch.tick)%10u)) join_rw_arm(1);
        if(at+65536u<cap) at+=join_rw_rows_text(out+at,cap-at,side,tick);
    }
    if(at) crash_note_write(out,(int)at);
    free(out);
}
/* The saver: the watched objects' runtime IDs from its ID table, then the rows of T. */
static void join_watch_session(const join_tables *t,uint64_t tick) {
    if(!join_watch.n) return;
    for(unsigned i=0;i<join_watch.n;i++) {
        join_watch.runtime[i]=0;
        for(uint32_t k=0;k<t->nids;k++) if(t->ids[k].portable==join_watch.portable[i]) {join_watch.runtime[i]=t->ids[k].runtime;break;}
    }
    join_watch_dump('s',tick);
}
/* Both sides, at the boundaries after T+1 (t), T+2 (u) and, up to native_gate_join_watch_span, after each later step (v). */
#define JOIN_WATCH_SPAN_MAX 600u
static uint32_t join_watch_span=2u;
/* native_gate_join_buildp (diagnostic, both sides, with the watch): every build processor's busy fields after T+1 and T+14.
 * j1-122 at 648: Job_Helper's Process_Ships drew 81 times on the joiner, 77 on the session; its loop runs one ship a draw
 * until no shipyard has a free build processor (find_object_component class.buildprocessor busy=false), and X4's busy test
 * (U::BuildFilterByIsBusy 0x388580) for a processor is +0x260 >= -0.9999999 or +0x504 set or 0x38c100 (the build's progress
 * at the frame's time). Rows JOIN_BUILDP side= tick= id=<portable> at=<+0x260 bits> flag=<+0x504> busy=, then a sum. */
static int join_buildp_on;
#define JOIN_BUILDP_CLASS 10u
/* j1-131: the busy test's third part (0x38c100 with its flag) calls 0x38dd40(processor, time), which walks up to the class-0x6f
 * ancestor and runs 0x4280e0(station, time): the station's builds advanced to the frame's time, its last update at +0xa58
 * (skipped when that is already the time). A row per class-0x6f object with that time. */
#define JOIN_BUILDP_STATION 0x6fu
#define JOIN_BUILDP_LAST 0xa58u
static void join_buildp_dump(char side,uint64_t tick) {
    if(!join_buildp_on || !world_identities) return;
    size_t cap=1u<<20,at=0;char *out=malloc(cap);if(!out) return;
    unsigned rows=0,busy=0;
    for(unsigned i=0;i<WORLD_IDENTITY_CAPACITY && at+256<cap;i++) {
        const uint64_t runtime=world_identities[i].runtime;if(!runtime) continue;
        unsigned char *o=join_object(runtime);uintptr_t vt=0;uint32_t words[8];
        if(!o || !join_read(o,&vt,8) || !world_class_words(vt,words)) continue;
        if(world_class_has(words,JOIN_BUILDP_STATION)) {   /* j1-131: the busy test updates the processor's class-0x6f ancestor */
            uint64_t last=0;(void)join_read(o+JOIN_BUILDP_LAST,&last,8);++rows;
            at+=(size_t)snprintf(out+at,cap-at,"[X4LAN] NATIVE_GATE JOIN_BUILDP side=%c tick=%llu id=%llx station_last=%016llx\r\n",side,
                                 (unsigned long long)tick,(unsigned long long)world_identities[i].saved,(unsigned long long)last);
            continue;
        }
        if(!world_class_has(words,JOIN_BUILDP_CLASS)) continue;
        uint64_t bits=0;unsigned char flag=0;(void)join_read(o+0x260,&bits,8);(void)join_read(o+0x504,&flag,1);
        double d;memcpy(&d,&bits,8);const int b=d>=-0.9999999 || flag;busy+=(unsigned)b;++rows;
        at+=(size_t)snprintf(out+at,cap-at,"[X4LAN] NATIVE_GATE JOIN_BUILDP side=%c tick=%llu id=%llx at=%016llx flag=%u busy=%d\r\n",side,
                             (unsigned long long)tick,(unsigned long long)world_identities[i].saved,(unsigned long long)bits,flag,b);
    }
    at+=(size_t)snprintf(out+at,cap-at,"[X4LAN] NATIVE_GATE JOIN_BUILDP_SUM side=%c tick=%llu rows=%u busy=%u\r\n",side,(unsigned long long)tick,rows,busy);
    crash_note_write(out,(int)at);free(out);
}
/* native_gate_join_refwatch_from (diagnostic, a saver): the JOIN_REFWATCH fields armed from step N on, their objects found by
 * portable ID in the world identity table (snapshot objects only), applied again every 10 steps for new threads until the
 * capture of T arms them through the watch; the rows show at side s. j1-204: the slot pilots' +0x5a0 is set at runtime (7 of
 * 11,734 NPCs on the session: both slot pilots and 5 created ones; none on the joiner). */
static void join_rw_early_probe(uint64_t completed) {
#ifndef JOIN_OWN_PLATFORM
    if(!join_rw_from || !join_rw_nspec || !world_identities || join_watch.armed || completed<join_rw_from || (completed-join_rw_from)%10u) return;
    if(!join_rw_veh_handle) join_rw_veh_handle=AddVectoredExceptionHandler(1,join_rw_veh);
    if(!join_rw_veh_handle) return;
    uint32_t found=0;
    for(unsigned i=0;i<4u;i++) {
        uintptr_t a=0;uint32_t len=4u;
        if(i<join_rw_nspec) {
            const join_rw_spec_t *s=&join_rw_spec[i];unsigned char *o=NULL;len=s->len;
            if(s->portable==~0ull) o=join_jobe_engine();   /* the job engine */
            else if(s->portable==~1ull) o=(unsigned char *)image;   /* the image: an execution breakpoint */
            join_rw_exec[i]=s->portable==~1ull;
            for(unsigned k=0;k<WORLD_IDENTITY_CAPACITY && !o;k++)
                if(world_identities[k].runtime && world_identities[k].saved==s->portable) o=join_object(world_identities[k].runtime);
            for(uint32_t d=0;o && s->portable!=~1ull && d<s->nderef;d++) {unsigned char *p=NULL;o=join_read(o+s->deref[d],&p,8)?p:NULL;}
            if(o) {a=(uintptr_t)(o+s->offset);found|=1u<<i;}
        }
        join_rw_addr[i]=a;join_rw_len[i]=len;
    }
    join_rw_early_found=found;
    join_rw_apply();
#else
    (void)completed;
#endif
}
/* JOIN_SHIPCODE (diagnostic, at native_gate_join_jobcnt_at's step): every created ship's code, the std::string at +0x328 (vt+0x12c0;
 * vt+0x1330 0x6cd820 is its FNV-1a hash, which seeds the job quota 0xefda70's draw: j1-223 at 902, the linked jobs' quotas 2 and 1
 * on the session, 1 and 0 on the joiner after the same first spawn). */
static uint64_t join_jobcnt_at;   /* lockstep_join.h (a tentative definition here) */
static void join_shipcode_dump(uint64_t tick) {
    if(!join_jobcnt_at || tick!=join_jobcnt_at) return;
    static const uint32_t ships[]={0x2b7d738u,0x2b7feb8u,0x2b82440u,0x2b9bff8u,0x2b99a88u};
    const size_t cap=1u<<16;char *out=malloc(cap);size_t at=0;if(!out) return;
    const unsigned total=world_hash_count+world_hash_created_count;
    for(unsigned i=world_hash_count;i<total;i++) {   /* the created items */
        const world_hash_item *item=world_hash_item_at(i);const uint64_t runtime=item->runtime,key=item->saved;if(!runtime) continue;
        unsigned char *o=join_object(runtime);uintptr_t vt=0;if(!o || !join_read(o,&vt,8)) continue;
        unsigned k=0;for(;k<5u;k++) if(vt==(uintptr_t)image+ships[k]) break;
        if(k>=5u) continue;
        uint64_t size=0,capacity=0;char code[64];unsigned char *text=o+0x328;
        if(!join_read(o+0x338,&size,8) || !join_read(o+0x340,&capacity,8) || size>=sizeof(code)) continue;
        if(capacity>=16u) (void)join_read(o+0x328,&text,8);
        if(!text || !join_read(text,code,(size_t)size)) continue;
        code[size]=0;
        if(at+128u>=cap) {crash_note_write(out,(int)at);at=0;}
        at+=(size_t)snprintf(out+at,cap-at,"[X4LAN] NATIVE_GATE JOIN_SHIPCODE tick=%llu id=%llx code=%s""\r\n",(unsigned long long)tick,(unsigned long long)key,code);
    }
    if(at) crash_note_write(out,(int)at);
    free(out);
}
/* refwatch_at past the span: the watched objects again (as the dump finds them), the fields armed at n-1 and every 10 steps after
 * (threads started since), the rows since the last print. */
static void join_rw_dump(uint64_t tick) {
    for(unsigned i=0;i<join_watch.n;i++) join_watch.object[i]=join_watch.runtime[i]?join_object(join_watch.runtime[i]):NULL;
    if(tick+1u==join_rw_at || !((tick+1u-join_rw_at)%10u)) join_rw_arm(1);
    const size_t cap=1u<<18;char *out=malloc(cap);
    if(!out) return;
    const size_t at=join_rw_rows_text(out,cap,'v',tick);
    if(at) crash_note_write(out,(int)at);
    free(out);
}
static void join_watch_probe(uint64_t completed) {
    join_rw_early_probe(completed);
    join_codew_step(completed);   /* JOIN_CODEW: the code counter's changes before JOBCNT's step */
    if(join_jobcnt_at && completed==join_jobcnt_at) {   /* JOIN_RSTORE at JOBCNT's step too (side c) */
        size_t cap=1u<<20;char *o=malloc(cap);if(o) {const size_t at=join_rstore_dump(o,cap,'c',completed);if(at) crash_note_write(o,(int)at);free(o);}
    }
    join_jobcnt_dump(completed);
    join_shipcode_dump(completed);
    if(!join_watch.armed || completed<=join_watch.tick) return;
    const uint64_t k=completed-join_watch.tick;
    if(k==1u || (k==14u && k<=join_watch_span)) join_buildp_dump(k==1u?'t':'v',completed);
    const int rw_open=join_rw_at && completed+1u>=join_rw_at && completed+1u<join_rw_at+JOIN_RW_AT_STEPS;
    if(k<=join_watch_span) join_watch_dump(k==1u?'t':k==2u?'u':'v',completed);
    else if(rw_open) join_rw_dump(completed);
    if(k>=join_watch_span && !rw_open && !(join_rw_at && completed+1u<join_rw_at)) {join_watch.armed=0;join_rw_arm(0);}
}
/* ---- version 13: VIEWK, VIEWKC (report 1.18) ---- */
#define JOIN_VIEW_FLAGS 0x3cu
#define JOIN_VIEW_ATTENTION 0x38u          /* the view's published attention (u32) */
#define JOIN_VIEWF_STATION_VT 0x2b764d8u    /* U::Station */
#define JOIN_VIEWF_ZONE_VT 0x2b66080u       /* U::Zone */
#define JOIN_VIEW_CHILDREN 0x10u
typedef struct {uint64_t portable;unsigned char *object;} join_viewk_cand;
static int join_viewk_cand_cmp(const void *a,const void *b) {
    uint64_t x=((const join_viewk_cand *)a)->portable,y=((const join_viewk_cand *)b)->portable;return x<y?-1:x>y;
}
static void join_capture_viewk(join_tables *t) {
    const uint32_t cur=join_view_index();uint32_t kcap=0,ccap=0,ncand=0,candcap=0;join_viewk_cand *cand=NULL;
    for(uint32_t i=0;i<t->nids;i++) {
        unsigned char *object=join_object(t->ids[i].runtime);uint32_t f0=0,f1=0;
        if(!object || !join_read(object+JOIN_VIEW_SLOTS+JOIN_VIEW_FLAGS,&f0,4) || !join_read(object+JOIN_VIEW_SLOTS+0x80u+JOIN_VIEW_FLAGS,&f1,4) ||
           !((f0|f1)&JOIN_VIEWK_PENDING)) continue;
        join_viewk_cand c={t->ids[i].portable,object};
        if(ncand>=JOIN_VIEWK_MAX/2u || !JOIN_PUSH(cand,ncand,candcap,c)) break;
    }
    if(ncand>1) qsort(cand,ncand,sizeof(*cand),join_viewk_cand_cmp);
    for(uint32_t i=0;i<ncand;i++) {
        for(uint32_t rel=0;rel<2u;rel++) {
            const unsigned char *block=cand[i].object+JOIN_VIEW_SLOTS+(size_t)(cur^rel)*0x80u;uintptr_t begin=0;uint64_t n=0;
            join_viewk_rec r;memset(&r,0,sizeof(r));r.portable=cand[i].portable;r.rel=rel;
            (void)join_read(block+JOIN_VIEW_FLAGS,&r.vflags,4);
            if(!join_grav_vector(block+JOIN_VIEW_CHILDREN,&begin,&n) || n>JOIN_VIEWK_CHILDREN || (n && !join_read((void *)begin,join_grav_ids,(size_t)n*8u)))
                {r.flags=JOIN_VIEWK_UNREAD;n=0;}
            r.count=(uint32_t)n;
            for(uint64_t k=0;k<n;k++) {
                join_viewkc_rec c;memset(&c,0,sizeof(c));uint64_t runtime=0;unsigned klass;
                c.portable=r.portable;c.rel=rel;c.index=(uint32_t)k;
                if(join_grav_ids[k] && join_read((void *)(uintptr_t)(join_grav_ids[k]+8u),&runtime,8) && runtime) c.child=join_key(runtime,&klass);
                if(!c.child) r.flags|=JOIN_VIEWK_UNMAPPED;
                if(!JOIN_PUSH(t->viewkc,t->nviewkc,ccap,c)) {free(cand);return;}
            }
            if(!JOIN_PUSH(t->viewk,t->nviewk,kcap,r)) {free(cand);return;}
        }
    }
    free(cand);
}
static int join_viewf_cmp(const void *a,const void *b) {
    uint64_t x=((const join_viewf_rec *)a)->portable,y=((const join_viewf_rec *)b)->portable;return x<y?-1:x>y;
}
/* SHOOT: the weapon classes (U::Weapon, U::Turret, U::MissileTurret, U::MissileLauncher, U::BombLauncher) and the shoot controllers
 * (U::BasicShootController and its LargeTarget, Mindless, Multiple, Player kinds), primary vtable RVAs. */
static const uint32_t join_shoot_weapon_vt[]={0x2b68278u,0x2b6e910u,0x2b00620u,0x2b02988u,0x2b5fce8u};
static const uint32_t join_shoot_ctl_vt[]={0x2b7fcc0u,0x2b7d088u,0x2b7cf68u,0x2b7cdf8u,0x2b7d1a8u};
#define JOIN_SHOOT_LASTSHOT 0x310u
#define JOIN_SHOOT_WFLAGS 0x30cu
#define JOIN_SHOOT_CTL 0x318u
static const uint32_t join_shoot_ctl_off[5]={0x28u,0x40u,0x48u,0x50u,0x58u};
#define JOIN_SHOOT_AIM 0x60u
#define JOIN_SHOOT_TOL 0x80u           /* the controller's aim cone (float) */
#define JOIN_SHOOT_CFLAGS 0x84u
static uint32_t join_shoot_rva(const unsigned char *p,const uint32_t *set,unsigned n) {
    uintptr_t vt=0;
    if(!p || !join_read(p,&vt,8) || vt<(uintptr_t)image || vt-(uintptr_t)image>=JOIN_FLIGHT_IMAGE) return 0;
    for(unsigned k=0;k<n;k++) if(vt-(uintptr_t)image==set[k]) return set[k];
    return 0;
}
/* A weapon's shot state into r (portable left to the caller): 0 when it is no weapon of the family. *ctlp its controller of the
 * family (has_ctl 1), else NULL and only the last shot time. */
static int join_shoot_read(unsigned char *weapon,join_shoot_rec *r,unsigned char **ctlp) {
    unsigned char *ctl=NULL;join_shoot_rec c;
    memset(r,0,sizeof(*r));*ctlp=NULL;
    if(!join_shoot_rva(weapon,join_shoot_weapon_vt,5u) || !join_read(weapon+JOIN_SHOOT_LASTSHOT,&r->lastshot,8)) return 0;
    c=*r;
    if(!join_read(weapon+JOIN_SHOOT_CTL,&ctl,8) || !ctl || !(c.ctl_vt=join_shoot_rva(ctl,join_shoot_ctl_vt,5u)) ||
       !join_read(weapon+JOIN_SHOOT_WFLAGS,c.wflags,3) || !join_read(ctl+JOIN_SHOOT_AIM,c.aim,sizeof(c.aim)) ||
       !join_read(ctl+JOIN_SHOOT_CFLAGS,c.cflags,2) || !join_read(ctl+JOIN_SHOOT_TOL,&c.tol,4)) return 1;
    for(unsigned k=0;k<5u;k++) if(!join_read(ctl+join_shoot_ctl_off[k],&c.ctl[k],8)) return 1;
    for(unsigned k=0;k<3u;k++) c.wflags[k]=c.wflags[k]!=0;
    for(unsigned k=0;k<2u;k++) c.cflags[k]=c.cflags[k]!=0;
    c.has_ctl=1;*r=c;*ctlp=ctl;return 1;
}
/* The save's four decimals of a time: the joiner's loaded value within this of the session's exact one is the same time. */
#define JOIN_SAVE_ROUNDING 0.00006
static int join_save_rounded(uint64_t mine,uint64_t want) {
    double a,b;memcpy(&a,&mine,8);memcpy(&b,&want,8);
    const double d=a-b;return d<=JOIN_SAVE_ROUNDING && d>=-JOIN_SAVE_ROUNDING;
}
/* A time the save writes exactly: its four decimals read back are the value itself (nothing for the joiner to correct). */
static int join_save_exact(uint64_t bits) {
    double v;char text[64];memcpy(&v,&bits,8);
    if(snprintf(text,sizeof(text),"%.4f",v)<=0) return 0;
    return strtod(text,NULL)==v;
}
/* SHOOT's records whose controller the joiner lacked at the admission (diagnostic: present after T+1, after T+2). */
static struct {uint64_t runtime;uint32_t ctl_vt;} *join_shoot_late;
static uint32_t join_shoot_nlate,join_shoot_latecap;
static void join_shoot_late_probe(unsigned at) {
    if(at>1u) return;
    for(uint32_t i=0;i<join_shoot_nlate;i++) {
        unsigned char *weapon=join_object(join_shoot_late[i].runtime),*ctl=NULL;join_shoot_rec own;
        if(weapon && join_shoot_read(weapon,&own,&ctl) && ctl && own.ctl_vt==join_shoot_late[i].ctl_vt) ++join_rx.shoot_late[at];
    }
}
/* ATTK: the destructibles' primary vtable RVAs (U::Destructible and the 77 classes deriving from it), sorted. */
static const uint32_t join_attk_vt[]={
    0x2a8ce88u,0x2a8f1f0u,0x2a932b8u,0x2a95528u,0x2a97908u,0x2a99cd0u,0x2a9bff0u,0x2a9e2f8u,0x2aa4558u,0x2aa69e8u,0x2aa89d8u,0x2aaca18u,
    0x2aaefb8u,0x2ab1130u,0x2ab3148u,0x2ab5378u,0x2ab7438u,0x2acca90u,0x2acec88u,0x2ad0ff8u,0x2ad58f8u,0x2ad7a88u,0x2adbdc0u,0x2adde00u,
    0x2adfec8u,0x2ae1fc0u,0x2ae50d0u,0x2af5ce0u,0x2af7fa8u,0x2af9e88u,0x2afc1f8u,0x2afe0b8u,0x2b00620u,0x2b02988u,0x2b04ba8u,0x2b06fb0u,
    0x2b0d238u,0x2b0f198u,0x2b11168u,0x2b13b50u,0x2b19760u,0x2b1b848u,0x2b22880u,0x2b24b80u,0x2b288b0u,0x2b2cae8u,0x2b2e9c0u,0x2b36658u,
    0x2b3cf48u,0x2b3f228u,0x2b41108u,0x2b43330u,0x2b4f8a0u,0x2b518c8u,0x2b53918u,0x2b55968u,0x2b57900u,0x2b59950u,0x2b5bb40u,0x2b5dab8u,
    0x2b5fce8u,0x2b63c18u,0x2b68278u,0x2b6a740u,0x2b6e910u,0x2b74548u,0x2b764d8u,0x2b789b0u,0x2b7d738u,0x2b7feb8u,0x2b82440u,0x2b84a98u,
    0x2b99a88u,0x2b9bff8u,0x2ba3240u,0x2c2ff40u,0x2c34050u,0x2c36278u
};
#define JOIN_ATTK_ATTACKER 0x298u
#define JOIN_ATTK_METHOD 0x2a8u
#define JOIN_ATTK_TIMES 0x2b0u   /* attacktime, shipattacktime (+0x2b8), intentionalattacktime (+0x2c0) */
static int join_attk_read(unsigned char *object,join_attk_rec *r) {
    uintptr_t vt=0;uint64_t attacker=0;
    memset(r,0,sizeof(*r));
    if(!object || !join_read(object,&vt,8) || vt<(uintptr_t)image || vt-(uintptr_t)image>=JOIN_FLIGHT_IMAGE) return 0;
    const uint32_t rva=(uint32_t)(vt-(uintptr_t)image);uint32_t lo=0,hi=(uint32_t)(sizeof(join_attk_vt)/sizeof(join_attk_vt[0]));
    while(lo<hi) {uint32_t mid=(lo+hi)/2;if(join_attk_vt[mid]==rva) break;if(join_attk_vt[mid]<rva) lo=mid+1;else hi=mid;}
    if(lo>=hi) return 0;
    if(!join_read(object+JOIN_ATTK_TIMES,r->times,sizeof(r->times)) || !join_read(object+JOIN_ATTK_ATTACKER,&attacker,8) ||
       !join_read(object+JOIN_ATTK_METHOD,&r->method,4)) return 0;
    unsigned klass;r->attacker=attacker?join_key(attacker,&klass):0;
    return 1;
}
/* LIVEF: the classes whose slot 646 is U::Positional::vfunc646 0x34fbb0 itself (primary vtable RVAs, sorted). */
static const uint32_t join_livef_vt[]={
    0x2a8adf8u,0x2a8ce88u,0x2a8f1f0u,0x2a932b8u,0x2a95528u,0x2a97908u,0x2a99cd0u,0x2a9bff0u,0x2a9e2f8u,0x2aa0628u,0x2aa2498u,0x2aa4558u,
    0x2aa69e8u,0x2aa89d8u,0x2aaca18u,0x2aaefb8u,0x2ab1130u,0x2ab3148u,0x2ab5378u,0x2ab7438u,0x2abf5e8u,0x2acca90u,0x2acec88u,0x2ad0ff8u,
    0x2ad3458u,0x2ad58f8u,0x2ad7a88u,0x2ad9ca0u,0x2adbdc0u,0x2adde00u,0x2adfec8u,0x2ae1fc0u,0x2ae50d0u,0x2ae7450u,0x2af5ce0u,0x2af7fa8u,
    0x2af9e88u,0x2afc1f8u,0x2afe0b8u,0x2b00620u,0x2b02988u,0x2b04ba8u,0x2b06fb0u,0x2b091e0u,0x2b0b398u,0x2b0d238u,0x2b0f198u,0x2b11168u,
    0x2b13b50u,0x2b15e38u,0x2b19760u,0x2b1b848u,0x2b1ea48u,0x2b20990u,0x2b22880u,0x2b24b80u,0x2b26a78u,0x2b288b0u,0x2b2ad60u,0x2b2cae8u,
    0x2b2e9c0u,0x2b30a08u,0x2b329a8u,0x2b36658u,0x2b38a48u,0x2b3ac58u,0x2b3cf48u,0x2b3f228u,0x2b41108u,0x2b43330u,0x2b462c8u,0x2b4f8a0u,
    0x2b518c8u,0x2b53918u,0x2b55968u,0x2b57900u,0x2b59950u,0x2b5bb40u,0x2b5dab8u,0x2b5fce8u,0x2b61f30u,0x2b63c18u,0x2b66080u,0x2b68278u,
    0x2b6a740u,0x2b6cc38u,0x2b6e910u,0x2b70ba0u,0x2b74548u,0x2b764d8u,0x2b789b0u,0x2b7afe8u,0x2b7d738u,0x2b7feb8u,0x2b82440u,0x2b84a98u,
    0x2b902a0u,0x2b91fe0u,0x2b93d20u,0x2b99a88u,0x2b9bff8u,0x2ba3240u,0x2c21248u,0x2c23018u,0x2c24dd8u,0x2c26b88u,0x2c2a1a8u,0x2c2cee0u,
    0x2c2ff40u,0x2c321d8u,0x2c34050u,0x2c36278u,0x2c39778u,0x2c3c598u,0x2c40d18u,0x2c42ac8u
};
#define JOIN_LIVEF_FLAGS 0x1e4u
/* An object's bit 0x20 as its setup computes it now (slot 646 replicated by reads: +0x68 0, *(*(+0x60)+0x48)+0x1c0, +0x208): 0 or
 * JOIN_LIVEF_BITS into *fresh, its live flags into *live; 0 when it is none of the classes or unreadable. */
static int join_livef_read(unsigned char *object,uint32_t *fresh,uint32_t *live) {
    uintptr_t vt=0;unsigned char *macro=NULL,*inner=NULL;uint64_t m1c0=0,p208=0;int32_t i68=0;
    if(!object || !join_read(object,&vt,8) || vt<(uintptr_t)image || vt-(uintptr_t)image>=JOIN_FLIGHT_IMAGE) return 0;
    const uint32_t rva=(uint32_t)(vt-(uintptr_t)image);uint32_t lo=0,hi=(uint32_t)(sizeof(join_livef_vt)/sizeof(join_livef_vt[0]));
    while(lo<hi) {uint32_t mid=(lo+hi)/2;if(join_livef_vt[mid]==rva) break;if(join_livef_vt[mid]<rva) lo=mid+1;else hi=mid;}
    if(lo>=hi || !join_read(object+JOIN_LIVEF_FLAGS,live,4) || !join_read(object+0x68,&i68,4) || !join_read(object+0x208,&p208,8) ||
       !join_read(object+0x60,&macro,8)) return 0;
    if(!i68 && macro && join_read(macro+0x48,&inner,8) && inner) (void)join_read(inner+0x1c0,&m1c0,8);
    *fresh=!i68 && m1c0 && p208?0u:JOIN_LIVEF_BITS;
    return 1;
}
static int join_livef_cmp(const void *a,const void *b) {
    uint64_t x=((const join_livef_rec *)a)->portable,y=((const join_livef_rec *)b)->portable;return x<y?-1:x>y;
}
static void join_capture_livef(join_tables *t) {
    uint32_t cap=0;
    for(uint32_t i=0;i<t->nids;i++) {
        uint32_t fresh=0,live=0;
        if(!t->ids[i].portable || !join_livef_read(join_object(t->ids[i].runtime),&fresh,&live) || (live&JOIN_LIVEF_BITS)==fresh) continue;
        join_livef_rec r={t->ids[i].portable,live&JOIN_LIVEF_BITS,~live&JOIN_LIVEF_BITS};
        if(!JOIN_PUSH(t->livef,t->nlivef,cap,r)) return;
    }
    if(t->nlivef>1) qsort(t->livef,t->nlivef,sizeof(*t->livef),join_livef_cmp);
}
/* SPEC: a component's specification entries (at most JOIN_SPEC_MAX; the list +0xd8), their address into *at; the count, -1 none
 * readable. */
#define JOIN_SPEC_LIST 0xd8u
#define JOIN_SPEC_MAX 32u
typedef struct {uint32_t kind,flags,klass,trunc;uint64_t interval,last;} join_spec_entry;
static int join_spec_read(unsigned char *object,join_spec_entry *out,unsigned char **at) {
    unsigned char *list=NULL,*entries;uint64_t cap=0,count=0;
    if(!object || !join_read(object+JOIN_SPEC_LIST,&list,8)) return -1;
    if(!list) return 0;
    if(!join_read(list+0x80,&cap,8) || !join_read(list+0x88,&count,8) || count>cap || count>JOIN_SPEC_MAX) return -1;
    entries=list;
    if(cap!=4u && !join_read(list,&entries,8)) return -1;
    if(count && (!entries || !join_read(entries,out,(size_t)count*sizeof(*out)))) return -1;
    *at=entries;return (int)count;
}
/* SPEC: an entry the joiner lacks is added through AddCacheSpecification 0x3d5180 (it keeps the +0x1e8 counts up the parent chain),
 * then written as the others: only entries without an interval (the engine draws a phase for one with an interval) and not of the
 * one-shot kind 7. [F: j1-106 JOIN_WATCHPCF: the session's BuildCache refreshes 0x8a4b6 every step with 0x4f, the kind 2 entry of its
 * sector 0x13a (truncated at its zone); the joiner's zone passes no flags: j1-99's spec_missing 2, the sector entries that
 * Sector::vfunc701 0x78d870 adds at an attention change, which ATTN's direct write does not run.] */
#define JOIN_SPEC_ADD 0x3d5180u
static void join_spec_add_native(unsigned char *object,const join_spec_rec *r) {
    const uint32_t flags=r->flags;
    ((void (*)(void *,int,const uint32_t *,int,double))(void *)(image+JOIN_SPEC_ADD))(object,(int)r->kind,&flags,(int)r->klass,0.0);
}
static void (*join_spec_add)(unsigned char *,const join_spec_rec *)=join_spec_add_native;   /* tests replace it */
/* An engine component (its vtable in the image): the add calls engine code on it. */
static int join_spec_engine(const unsigned char *object) {
#ifndef JOIN_OWN_PLATFORM
    uintptr_t vt=0;
    return object && join_read(object,&vt,8) && vt>=(uintptr_t)image && vt-(uintptr_t)image<JOIN_FLIGHT_IMAGE;
#else
    (void)object;return 0;   /* the Linux gate: its own entry (not yet) */
#endif
}
static int join_spec_cmp(const void *a,const void *b) {
    const join_spec_rec *x=a,*y=b;
    return x->portable<y->portable?-1:x->portable>y->portable?1:x->kind<y->kind?-1:x->kind>y->kind;
}
static void join_capture_spec(join_tables *t) {
    uint32_t cap=0;join_spec_entry e[JOIN_SPEC_MAX];unsigned char *at=NULL;
    for(uint32_t i=0;i<t->nids;i++) {
        const int n=t->ids[i].portable?join_spec_read(join_object(t->ids[i].runtime),e,&at):0;
        for(int k=0;k<n;k++) {
            int dup=0;
            for(int m=0;m<k;m++) dup|=e[m].kind==e[k].kind;
            if(dup) continue;   /* one per kind (AddCacheSpecification merges): the first */
            join_spec_rec r={t->ids[i].portable,e[k].kind,e[k].flags,e[k].klass,e[k].trunc,e[k].interval,e[k].last};
            if(!JOIN_PUSH(t->spec,t->nspec,cap,r)) return;
        }
    }
    if(t->nspec>1) qsort(t->spec,t->nspec,sizeof(*t->spec),join_spec_cmp);
}
static void join_capture_jobs(join_tables *t);   /* lockstep_join.h: JOBS, by the job DB's layout */
static void join_admit_jobs(const join_tables *s);
static void join_capture_jobg(join_tables *t);   /* lockstep_join.h: JOBG */
static void join_admit_jobg(const join_tables *s);
/* NAVC: an object's navigation controller (+0x200, vtable JOIN_NAV_VTABLE), NULL when it has none. */
#define JOIN_NAVC_CACHE 0x60u
static unsigned char *join_navc_controller(unsigned char *object) {
    unsigned char *c=NULL;uintptr_t vt=0;
    return object && join_read(object+JOIN_CONTROLLER,&c,8) && c && join_read(c,&vt,8) && vt==(uintptr_t)image+JOIN_NAV_VTABLE?c:NULL;
}
static int join_navc_cmp(const void *a,const void *b) {
    uint64_t x=((const join_navc_rec *)a)->portable,y=((const join_navc_rec *)b)->portable;return x<y?-1:x>y;
}
static void join_capture_navc(join_tables *t) {
    uint32_t cap=0;
    for(uint32_t i=0;i<t->nids;i++) {
        unsigned char *c=t->ids[i].portable?join_navc_controller(join_object(t->ids[i].runtime)):NULL;join_navc_rec r;
        if(!c || !join_read(c+JOIN_NAVC_CACHE,r.cache,sizeof(r.cache))) continue;
        r.portable=t->ids[i].portable;
        if(!JOIN_PUSH(t->navc,t->nnavc,cap,r)) return;
    }
    if(t->nnavc>1) qsort(t->navc,t->nnavc,sizeof(*t->navc),join_navc_cmp);
}
static int join_attk_cmp(const void *a,const void *b) {
    uint64_t x=((const join_attk_rec *)a)->portable,y=((const join_attk_rec *)b)->portable;return x<y?-1:x>y;
}
static void join_capture_attk(join_tables *t) {
    uint32_t cap=0;
    for(uint32_t i=0;i<t->nids;i++) {
        join_attk_rec r;
        if(!t->ids[i].portable || !join_attk_read(join_object(t->ids[i].runtime),&r) ||   /* j1-95: 329,857 of 330,224 at -1 */
           (join_save_exact(r.times[0]) && join_save_exact(r.times[1]) && join_save_exact(r.times[2]))) continue;
        r.portable=t->ids[i].portable;
        if(!JOIN_PUSH(t->attk,t->nattk,cap,r)) return;
    }
    if(t->nattk>1) qsort(t->attk,t->nattk,sizeof(*t->attk),join_attk_cmp);
}
static int join_shoot_cmp(const void *a,const void *b) {
    uint64_t x=((const join_shoot_rec *)a)->portable,y=((const join_shoot_rec *)b)->portable;return x<y?-1:x>y;
}
static void join_capture_shoot(join_tables *t) {
    uint32_t cap=0;
    for(uint32_t i=0;i<t->nids;i++) {
        unsigned char *weapon=t->ids[i].portable?join_object(t->ids[i].runtime):NULL,*ctl=NULL;join_shoot_rec r;
        if(!weapon || !join_shoot_read(weapon,&r,&ctl) || (!r.has_ctl && join_save_exact(r.lastshot))) continue;   /* j1-95: 971 of 121,464 */
        r.portable=t->ids[i].portable;
        if(!JOIN_PUSH(t->shoot,t->nshoot,cap,r)) return;
    }
    if(t->nshoot>1) qsort(t->shoot,t->nshoot,sizeof(*t->shoot),join_shoot_cmp);
}
/* IKV (v13, section 62): the skeletons' IK joints. j1-171: the weapon frames the request check aims from differ on the joiner from T+1
 * (rest pose there, the gimbals tracking on the session). The frame slot 0x1ec8 = 0x81c960 takes the bone transform (0xe22b70 ->
 * 0xe20370) of the weapon's skeleton (+0x208 -> +0x48; its +0x10 a vector of groups {begin, end, cap}, each an array of 0x680-byte
 * joints, their vbtable 0x2c450c0 with U::IKValue at +0x678), evaluated at the thread time. A joint moves toward a target at a limited
 * rate (0xe1f840, driven by the weapon's aim 0x81c580 -> 0x7569a0 -> 0xe221a0 -> 0xe21110): +0x08 the value, +0x10 the time (a
 * double), +0x18 the rate, +0x1c the target. j1-175 (JOIN_WATCHANIM, all 0x680 bytes of actor 5's ship's and weapons' joints): the
 * session's two clients equal byte for byte; the joiner's differ only in those four words: the load leaves every joint at value 0,
 * rate 0, the time of the load. IKV carries those 24 bytes (+0x08..+0x1f) of every joint of every object with a skeleton; the admission
 * writes them where the joiner's skeleton has the same joint (same group and index, same vbtable). Status ikv=written:equal:missing:shape
 * (missing: a joiner joint without a record; shape: a skeleton the walk refuses). */
#define JOIN_IKV_CONTAINER 0x208u
#define JOIN_IKV_SKELETON 0x48u
#define JOIN_IKV_GROUP_VEC 0x10u
#define JOIN_IKV_JOINT 0x680u
#define JOIN_IKV_VBT 0x2c450c0u
#define JOIN_IKV_AT 0x08u
static uint64_t join_ikv_written,join_ikv_equal,join_ikv_missing,join_ikv_shape;
/* The joints of an object's skeleton: calls visit(group, joint index, joint) for each; 0 none, 1 walked, -1 refused (a shape the walk
 * does not know: more than JOIN_IKV_GROUPS groups or JOIN_IKV_JOINTS joints in one, a size not a multiple, a joint with another vbtable). */
typedef void (*join_ikv_visit)(void *ctx,uint16_t group,uint16_t joint,unsigned char *at);
/* The last refusal: why (1 the group vector unreadable, 2 its size, 3 a group's joint array (seen: its byte size), 4 a joint's vbtable
 * (seen: its RVA, or its value outside the image)); the admission lists the first few in the status (ikv_refused=). */
static unsigned join_ikv_why;static uintptr_t join_ikv_seen;
static char join_ikv_refused[320]="-";
static int join_ikv_walk(unsigned char *object,join_ikv_visit visit,void *ctx) {
    unsigned char *container=NULL,*skeleton=NULL;uintptr_t gv[2]={0,0};
    if(!object || !join_read(object+JOIN_IKV_CONTAINER,&container,8) || !container || !join_read(container+JOIN_IKV_SKELETON,&skeleton,8) ||
       !skeleton) return 0;
    if(!join_read(skeleton+JOIN_IKV_GROUP_VEC,gv,16)) {join_ikv_why=1;join_ikv_seen=0;return -1;}
    if(gv[0]==gv[1]) return 0;
    if(gv[1]<gv[0] || (gv[1]-gv[0])%0x18u || (gv[1]-gv[0])/0x18u>JOIN_IKV_GROUPS) {join_ikv_why=2;join_ikv_seen=gv[1]-gv[0];return -1;}
    for(int pass=0;pass<2;pass++)   /* the first pass checks the whole skeleton, the second visits */
        for(uintptr_t g=gv[0];g<gv[1];g+=0x18u) {
            uintptr_t jb[2]={0,0};
            if(!join_read((void *)g,jb,16) || jb[1]<jb[0] || (jb[1]-jb[0])%JOIN_IKV_JOINT || (jb[1]-jb[0])/JOIN_IKV_JOINT>JOIN_IKV_JOINTS)
                {join_ikv_why=3;join_ikv_seen=jb[1]-jb[0];return -1;}
            for(uintptr_t j=jb[0];j<jb[1];j+=JOIN_IKV_JOINT) {
                uintptr_t vbt=0;
                if(!pass) {
                    if(!join_read((void *)j,&vbt,8) || vbt!=(uintptr_t)image+JOIN_IKV_VBT) {
                        join_ikv_why=4;join_ikv_seen=vbt>=(uintptr_t)image && vbt-(uintptr_t)image<JOIN_FLIGHT_IMAGE?vbt-(uintptr_t)image:vbt;return -1;
                    }
                    continue;
                }
                visit(ctx,(uint16_t)((g-gv[0])/0x18u),(uint16_t)((j-jb[0])/JOIN_IKV_JOINT),(unsigned char *)j);
            }
        }
    return 1;
}
typedef struct {join_tables *t;uint32_t *cap;uint64_t portable;int full;} join_ikv_capture_ctx;
static void join_ikv_capture_one(void *ctx,uint16_t group,uint16_t joint,unsigned char *at) {
    join_ikv_capture_ctx *c=(join_ikv_capture_ctx *)ctx;join_ikv_rec r;
    if(c->full) return;
    memset(&r,0,sizeof(r));r.portable=c->portable;r.group=group;r.joint=joint;
    if(!join_read(at+JOIN_IKV_AT,r.state,JOIN_IKV_STATE)) return;
    if(!JOIN_PUSH(c->t->ikv,c->t->nikv,*c->cap,r)) c->full=1;
}
static int join_ikv_cmp(const void *a,const void *b) {
    const join_ikv_rec *x=(const join_ikv_rec *)a,*y=(const join_ikv_rec *)b;
    if(x->portable!=y->portable) return x->portable<y->portable?-1:1;
    if(x->group!=y->group) return x->group<y->group?-1:1;
    return x->joint<y->joint?-1:x->joint>y->joint;
}
static void join_capture_ikv(join_tables *t) {
    uint32_t cap=0;join_ikv_capture_ctx c={t,&cap,0,0};
    for(uint32_t i=0;i<t->nids && !c.full;i++) {
        if(!t->ids[i].portable) continue;
        c.portable=t->ids[i].portable;
        (void)join_ikv_walk(join_object(t->ids[i].runtime),join_ikv_capture_one,&c);
    }
    if(t->nikv>1) qsort(t->ikv,t->nikv,sizeof(*t->ikv),join_ikv_cmp);
}
typedef struct {const join_tables *s;uint64_t portable;} join_ikv_admit_ctx;
static void join_ikv_admit_one(void *ctx,uint16_t group,uint16_t joint,unsigned char *at) {
    const join_ikv_admit_ctx *c=(const join_ikv_admit_ctx *)ctx;const join_ikv_rec key={c->portable,group,joint,0,{0}};
    uint32_t lo=0,hi=c->s->nikv;unsigned char own[JOIN_IKV_STATE];
    while(lo<hi) {uint32_t mid=lo+(hi-lo)/2u;if(join_ikv_cmp(&c->s->ikv[mid],&key)<0) lo=mid+1u;else hi=mid;}
    if(lo>=c->s->nikv || join_ikv_cmp(&c->s->ikv[lo],&key)) {++join_ikv_missing;return;}
    if(!join_read(at+JOIN_IKV_AT,own,JOIN_IKV_STATE)) {++join_ikv_missing;return;}
    if(!memcmp(own,c->s->ikv[lo].state,JOIN_IKV_STATE)) {++join_ikv_equal;return;}
    if(join_poke((uintptr_t)(at+JOIN_IKV_AT),c->s->ikv[lo].state,JOIN_IKV_STATE)) ++join_ikv_written; else ++join_ikv_equal;
}
static void join_admit_ikv(const join_tables *s,const join_tables *mine) {
    join_ikv_admit_ctx c={s,0};
    size_t at=0;
    join_ikv_written=join_ikv_equal=join_ikv_missing=join_ikv_shape=0;join_ikv_refused[0]='-';join_ikv_refused[1]=0;
    for(uint32_t i=0;i<mine->nids;i++) {
        if(!mine->ids[i].portable) continue;
        c.portable=mine->ids[i].portable;
        unsigned char *o=join_object(mine->ids[i].runtime);
        if(join_ikv_walk(o,join_ikv_admit_one,&c)>=0) continue;
        if(++join_ikv_shape<=8u && at+64u<sizeof(join_ikv_refused)) {   /* portable/type RVA/why/seen */
            uintptr_t vt=0;
            if(!join_read(o,&vt,8) || vt<(uintptr_t)image || vt-(uintptr_t)image>=JOIN_FLIGHT_IMAGE) vt=(uintptr_t)image;
            at+=(size_t)snprintf(join_ikv_refused+at,sizeof(join_ikv_refused)-at,"%s%llx/%x/%u/%llx",at?",":"",(unsigned long long)c.portable,
                                 (unsigned)(vt-(uintptr_t)image),join_ikv_why,(unsigned long long)join_ikv_seen);
        }
    }
}
/* RCNT (v13, section 63; the admission opt-in, native_gate_join_rcnt): the reference counts of the session's created ships and bays.
 * j1-184: a ship the session created and docked holds two more references (+0x40) on the joiner than on the session, from the load
 * on (j1-191: references the load takes that a live docking does not); j1-192: the joiner never drops them, so such a ship, once
 * destroyed, is never deleted there (the session deletes it in the next step) and its radar contacts linger (the ShipSmall near the
 * second escape pod move apart at 780). The capture writes the count of every object (j1-193: ships and bays were not enough) whose portable ID has the
 * top bit (created on the session). With the opt-in the admission lowers a joiner's count to the session's where it is 1..4 higher
 * (lock xadd, never to zero: the session's count is at least 1); a lower count is kept (adding references only delays a deletion).
 * EXPERIMENT: if a load holder ever released its reference after this, the object would go early (a use after free). Status
 * rcnt=on:lowered:equal:kept:missing. */
static int join_rcnt_on;
static uint64_t join_rcnt_lowered,join_rcnt_equal,join_rcnt_kept,join_rcnt_missing;
/* Every object the session created (j1-193: with ships and bays only, the escape pods' U::Computer pilots still outlived theirs on
 * the joiner): any object whose vtable lies in the image. */
static int join_rcnt_type(unsigned char *o) {
    uintptr_t vt=0;
    return o && join_read(o,&vt,8) && vt>=(uintptr_t)image && vt-(uintptr_t)image<JOIN_FLIGHT_IMAGE;
}
static int join_rcnt_cmp(const void *a,const void *b) {
    uint64_t x=((const join_rcnt_rec *)a)->portable,y=((const join_rcnt_rec *)b)->portable;return x<y?-1:x>y;
}
static void join_capture_rcnt(join_tables *t) {
    uint32_t cap=0;
    for(uint32_t i=0;i<t->nids;i++) {
        if(!(t->ids[i].portable>>63)) continue;
        unsigned char *o=join_object(t->ids[i].runtime);join_rcnt_rec r;
        if(!join_rcnt_type(o)) continue;
        memset(&r,0,sizeof(r));r.portable=t->ids[i].portable;
        if(!join_read(o+0x40,&r.count,4) || !r.count) continue;
        if(!JOIN_PUSH(t->rcnt,t->nrcnt,cap,r)) return;
    }
    if(t->nrcnt>1) qsort(t->rcnt,t->nrcnt,sizeof(*t->rcnt),join_rcnt_cmp);
}
/* ECOS (v13, section 64; the admission opt-in, native_gate_join_ecos): the stations', build storages' and ships' economy schedules.
 * A load sets a container's next economy run from its saved last one: +0xa38 = last (+0xa40) + base + spread - FloatRange(a keyed
 * draw) (Container::Import 0x3f0530 -> 0x430cc0, lockstep_economy_load.h), where the session's came from its own economy update's
 * draw; the package carried neither, so a container whose economy ran during the session runs it at another time on the joiner
 * (UpdateTradeOffers 0x4079e0, the economy update 0x431f90), and its trade offers with it [w1z28/w1z29: a TradeRoutine trader's
 * order steps one tick late on the joiner from 657 (the stamp +0xa18 its trade query 0x89261c writes); the hot join's J1:
 * trade.find.free at 639 draws 4 times on the session and 6 on the joiner]. The capture writes both doubles of every Station,
 * BuildStorage and ship; with the opt-in the admission writes the session's where they differ. Status ecos=on:written:equal:missing. */
static int join_ecos_on;
static uint64_t join_ecos_written,join_ecos_equal,join_ecos_missing;
static const uint32_t join_ecos_vtables[]={0x2b764d8u,0x2aaca18u,0x2b7d738u,0x2b7feb8u,0x2b82440u,0x2b9bff8u,0x2b99a88u};   /* Station, BuildStorage, ShipXS..XL */
static int join_ecos_type(unsigned char *o) {
    uintptr_t vt=0;
    if(!o || !join_read(o,&vt,8) || vt<(uintptr_t)image) return 0;
    for(unsigned i=0;i<sizeof(join_ecos_vtables)/sizeof(*join_ecos_vtables);i++) if(vt==(uintptr_t)image+join_ecos_vtables[i]) return 1;
    return 0;
}
static int join_ecos_cmp(const void *a,const void *b) {
    uint64_t x=((const join_ecos_rec *)a)->portable,y=((const join_ecos_rec *)b)->portable;return x<y?-1:x>y;
}
static void join_capture_ecos(join_tables *t) {
    uint32_t cap=0;
    for(uint32_t i=0;i<t->nids;i++) {
        if(!t->ids[i].portable) continue;
        unsigned char *o=join_object(t->ids[i].runtime);join_ecos_rec r;
        if(!join_ecos_type(o)) continue;
        memset(&r,0,sizeof(r));r.portable=t->ids[i].portable;
        if(!join_read(o+0xa38,&r.next,8) || !join_read(o+0xa40,&r.last,8)) continue;
        if(!JOIN_PUSH(t->ecos,t->necos,cap,r)) return;
    }
    if(t->necos>1) qsort(t->ecos,t->necos,sizeof(*t->ecos),join_ecos_cmp);
    {uint32_t k=0;for(uint32_t i=0;i<t->necos;i++) {if(k && t->ecos[k-1].portable==t->ecos[i].portable) continue;t->ecos[k++]=t->ecos[i];}t->necos=k;}
}
static void join_admit_ecos(const join_tables *s,const join_tables *mine) {
    join_ecos_written=join_ecos_equal=join_ecos_missing=0;
    if(!join_ecos_on) return;
    for(uint32_t i=0;i<s->necos;i++) {
        const join_ecos_rec *r=&s->ecos[i];unsigned char *o=join_object(join_runtime_of(mine,r->portable));
        if(!join_ecos_type(o)) {++join_ecos_missing;continue;}
        int changed=join_poke((uintptr_t)(o+0xa38),&r->next,8);
        changed|=join_poke((uintptr_t)(o+0xa40),&r->last,8);
        if(changed) ++join_ecos_written; else ++join_ecos_equal;
    }
}
/* TRDN (v13, section 66; the admission opt-in, native_gate_join_trdn): the containers' next trade offer update. A container's trade
 * offers are brought up lazily: UpdateTradeOffers 0x4079e0 rebuilds them only when its next update time +0x928 is due (UI_LAZY_ROW
 * state=, lockstep_ui_lazy.h). The save does not hold that time and the load leaves it at -1, so the first trade query on the joiner
 * rebuilds a station's offers the session would not touch until its own time [F: j1-198, UI_LAZY_ROW 636..642: trade.find.free of
 * 0x9561d at 639 asks station 0x39fdf; its +0x928 is 88.42 s on the session (the query returns) and -1 on the joiner (the query
 * rebuilds the offers, the cargo updates follow), and the script finds other offers: 4 draws against 6]. The capture writes +0x928
 * of every Station, BuildStorage and ship (ECOS's classes); with the opt-in the admission writes the session's where it differs.
 * Status trdn=on:written:equal:missing. */
static int join_trdn_on;
static uint64_t join_trdn_written,join_trdn_equal,join_trdn_missing;
static int join_trdn_cmp(const void *a,const void *b) {
    uint64_t x=((const join_trdn_rec *)a)->portable,y=((const join_trdn_rec *)b)->portable;return x<y?-1:x>y;
}
static void join_capture_trdn(join_tables *t) {
    uint32_t cap=0;
    for(uint32_t i=0;i<t->nids;i++) {
        if(!t->ids[i].portable) continue;
        unsigned char *o=join_object(t->ids[i].runtime);join_trdn_rec r;
        if(!join_ecos_type(o)) continue;
        memset(&r,0,sizeof(r));r.portable=t->ids[i].portable;
        if(!join_read(o+0x928,&r.next,8)) continue;
        if(!JOIN_PUSH(t->trdn,t->ntrdn,cap,r)) return;
    }
    if(t->ntrdn>1) qsort(t->trdn,t->ntrdn,sizeof(*t->trdn),join_trdn_cmp);
    {uint32_t k=0;for(uint32_t i=0;i<t->ntrdn;i++) {if(k && t->trdn[k-1].portable==t->trdn[i].portable) continue;t->trdn[k++]=t->trdn[i];}t->ntrdn=k;}
}
static void join_admit_trdn(const join_tables *s,const join_tables *mine) {
    join_trdn_written=join_trdn_equal=join_trdn_missing=0;
    if(!join_trdn_on) return;
    for(uint32_t i=0;i<s->ntrdn;i++) {
        const join_trdn_rec *r=&s->trdn[i];unsigned char *o=join_object(join_runtime_of(mine,r->portable));
        if(!join_ecos_type(o)) {++join_trdn_missing;continue;}
        if(join_poke((uintptr_t)(o+0x928),&r->next,8)) ++join_trdn_written; else ++join_trdn_equal;
    }
}
/* NPCA (v13, section 67; the admission opt-in, native_gate_join_npca): the NPCs' activation. An NPC's active flag +0x370 (NPC
 * vt+0x1588 0x4b76f0 reads it, vt+0x1590 0x4b7700 writes it) is evaluated only when its attention changes (0x3d5660 -> vt+0x15e8
 * 0x6bf900). On the rising edge 0x6bf900 creates its animation state +0x5a0 (0x6bd160 -> 0x3793b0) and 0x6bd830 stamps +0x5b0,
 * +0x5b8 and +0x5c0 with the time and +0x5d8 with the time and a TLS draw. NPC Move 0x6bc420 runs its behaviour block (the +0x5b0
 * stamp, the idle timing against +0x5b8 and +0x5d8, TLS draws) only with an animation state (0x6bc611) [F: j1-203..206: the session
 * activated the slot pilots 0x7a903 and 0x7a8e1 at its snapshot load (stamps 7.083) and keeps the flag; the joiner's load evaluates
 * them at attention 9, where NPC vt+0x1580 0x5559c0 needs attention 11 or the ship's +0x310, so they stay inactive without an
 * animation state (JOIN_NPCSLOT: 7 of 11,734 NPCs hold one on the session, 5 on the joiner; JOIN_REFWATCH in the load: 0x379419 sets
 * the created pilots' from the load's attention pass, never the slot pilots'); the 5 created pilots are active on both sides with
 * the joiner's load time 28.183 as their stamps]. The capture writes every NPC whose flag or animation state is set; with the opt-in
 * the admission creates a missing animation state with X4's own 0x6bd160 (no TLS draw), then writes the flag and +0x5b0 .. +0x5e0.
 * Status npca=on:started:written:equal:missing:failed:index_differs (index: the animation's +0x1f0 after the admission). */
#define JOIN_NPCA_ACTIVE 0x370u
#define JOIN_NPCA_STAMPS 0x5b0u
#define JOIN_NPCA_INDEX 0x1f0u
#define JOIN_NPCA_START 0x6bd160u   /* NPC: its animation state from its character (0x3793b0 into +0x4e0, +0x5a0 = +0x4e0+0xc0) */
typedef void (*join_npca_start_fn)(void *npc);
static void join_npca_start_native(void *npc) {((void (*)(void *))(void *)(image+JOIN_NPCA_START))(npc);}
static join_npca_start_fn join_npca_start=join_npca_start_native;   /* tests replace it */
static int join_npca_on;
static uint64_t join_npca_started,join_npca_written,join_npca_equal,join_npca_missing,join_npca_failed,join_npca_index_differs;
static int join_npca_cmp(const void *a,const void *b) {
    uint64_t x=((const join_npca_rec *)a)->portable,y=((const join_npca_rec *)b)->portable;return x<y?-1:x>y;
}
static unsigned char *join_npca_object(uint64_t runtime) {   /* an NPC (the exact class), else NULL */
    unsigned char *o=join_object(runtime);uintptr_t vt=0;
    return o && join_read(o,&vt,8) && vt==(uintptr_t)image+JOIN_NPC_VT?o:NULL;
}
static void join_capture_npca(join_tables *t) {
    uint32_t cap=0;
    for(uint32_t i=0;i<t->nids;i++) {
        if(!t->ids[i].portable) continue;
        unsigned char *o=join_npca_object(t->ids[i].runtime),*anim=NULL,active=0;join_npca_rec r;uint64_t index=0;
        if(!o || !join_read(o+JOIN_NPCA_ACTIVE,&active,1) || !join_read(o+JOIN_NPC_SLOT,&anim,8) || (!active && !anim)) continue;
        memset(&r,0,sizeof(r));r.portable=t->ids[i].portable;r.flags=(active?1u:0u)|(anim?2u:0u);
        if(anim && join_read(anim+JOIN_NPCA_INDEX,&index,8)) r.index=(uint32_t)index;
        if(!join_read(o+JOIN_NPCA_STAMPS,r.stamps,JOIN_NPCA_BYTES)) continue;
        if(!JOIN_PUSH(t->npca,t->nnpca,cap,r)) return;
    }
    if(t->nnpca>1) qsort(t->npca,t->nnpca,sizeof(*t->npca),join_npca_cmp);
    {uint32_t k=0;for(uint32_t i=0;i<t->nnpca;i++) {if(k && t->npca[k-1].portable==t->npca[i].portable) continue;t->npca[k++]=t->npca[i];}t->nnpca=k;}
}
static void join_admit_npca(const join_tables *s,const join_tables *mine) {
    join_npca_started=join_npca_written=join_npca_equal=join_npca_missing=join_npca_failed=join_npca_index_differs=0;
    if(!join_npca_on) return;
    for(uint32_t i=0;i<s->nnpca;i++) {
        const join_npca_rec *r=&s->npca[i];unsigned char *o=join_npca_object(join_runtime_of(mine,r->portable)),*anim=NULL;
        if(!o) {++join_npca_missing;continue;}
        (void)join_read(o+JOIN_NPC_SLOT,&anim,8);
        if((r->flags&2u) && !anim) {
            join_npca_start(o);++join_npca_started;
            if(!join_read(o+JOIN_NPC_SLOT,&anim,8) || !anim) {++join_npca_failed;continue;}
        }
        const unsigned char active=(r->flags&1u)?1u:0u;
        int changed=join_poke((uintptr_t)(o+JOIN_NPCA_ACTIVE),&active,1);
        changed|=join_poke((uintptr_t)(o+JOIN_NPCA_STAMPS),r->stamps,JOIN_NPCA_BYTES);
        if(changed) ++join_npca_written; else ++join_npca_equal;
        uint64_t index=0;
        if(anim && join_read(anim+JOIN_NPCA_INDEX,&index,8) && (uint32_t)index!=r->index) ++join_npca_index_differs;
    }
}
/* CODE (v13, section 69; the admission opt-in, native_gate_join_code): X4's object codes ("ABC-123", ship and NPC +0x328 / +0x2a0) come
 * from 0x6ea780: a global counter (0x38f0ae8, wrapped at 2^24) advanced for every new code, mixed with the game's code seed (0x3d19860,
 * low 24 bits) and spelled by 0x6ea820. A code seeds more than its name: ship vt+0x1330 0x6cd820 is the FNV-1a hash of it, and the job
 * quota 0xefda70 draws from that seed [F: j1-223 at 902 (a refwatch execution breakpoint on 0xf04817): after the same first spawn the
 * linked jobs' quotas 2 and 1 on the session, 1 and 0 on the joiner: two job ships more and another order]. The load starts the counter
 * at 0 (0x9edb60). The capture writes the counter and the seed; with the opt-in the admission writes the session's counter.
 * Status code=on:written:session counter:own counter:session seed:own seed. */
#define JOIN_CODE_COUNTER 0x38f0ae8u
#define JOIN_CODE_SEED 0x3d19860u
static int join_code_on;
static uint32_t join_code_written,join_code_session_counter,join_code_own_counter,join_code_session_seed,join_code_own_seed;
static void join_capture_code(join_tables *t) {
    uint32_t counter=0,seed=0;uint32_t cap=0;
    if(!join_read(image+JOIN_CODE_COUNTER,&counter,4) || !join_read(image+JOIN_CODE_SEED,&seed,4)) return;
    join_code_rec r={counter>0x1000000u?0x1000000u:counter,seed&0xffffffu};
    (void)JOIN_PUSH(t->code,t->ncode,cap,r);
}
static void join_admit_code(const join_tables *s) {
    join_code_written=0;join_code_own_counter=join_code_own_seed=0;
    if(!s->ncode) return;
    join_code_session_counter=s->code[0].counter;join_code_session_seed=s->code[0].seed;
    (void)join_read(image+JOIN_CODE_COUNTER,&join_code_own_counter,4);(void)join_read(image+JOIN_CODE_SEED,&join_code_own_seed,4);
    join_code_own_seed&=0xffffffu;
    if(join_code_on && join_poke((uintptr_t)(image+JOIN_CODE_COUNTER),&s->code[0].counter,4)) join_code_written=1;
}
/* The in-window resume (lockstep_pilot_join.h pjoin_resume, at T+1) rebuilds the slots' players, seats and modules: objects the
 * session made at its window start, before T, so their codes are already counted in the session's counter of T [w1z168: every ship
 * created after T, from 710 on, had another code on the joiner (173 of 501 created ships; the 328 made before T equal), and the job
 * quota 0xefda70 draws from those codes: the Antigone mining fleet's subordinate jobs built four ships more on the joiner in
 * 1981..2040, 48 objects and from there the 2040/2100 split]. With CODE on, the counter is held over the resume: begin reads it,
 * end puts it back. Status code_resume=the codes the resume drew. */
static uint32_t join_code_resume_draws,join_code_resume_held;
static int join_code_resume_open;
static void join_code_hold(int begin) {
    uint32_t c=0;
    if(!join_code_on || !join_read(image+JOIN_CODE_COUNTER,&c,4)) return;
    if(begin) {join_code_resume_held=c;join_code_resume_open=1;return;}
    if(!join_code_resume_open) return;
    join_code_resume_open=0;join_code_resume_draws=(c-join_code_resume_held)&0xffffffu;
    if(c!=join_code_resume_held) (void)join_poke((uintptr_t)(image+JOIN_CODE_COUNTER),&join_code_resume_held,4);
}
/* ENGR (v13, section 70; the admission opt-in, native_gate_join_engr): every engine's boost ramp [F: w1z43..w1z48, the window's fight
 * at 840: the destroyer 79009's braking distance at its boost refresh of 794 is 1337.98 on the session and 1394.08 on the joiner
 * with every input of 0x6de670 equal but the boost term, which divides by the engine's (0x6d6d80: 79037) +0x2f8: 4.056 against
 * 3.576]. 0x54e3a0..0x54e4b7 integrates that factor toward its target at a macro rate over dt = now - +0x2f0 and stamps +0x2f0 =
 * now; before 794 +0x2f8 is 1.0 on both sides, +0x2f0 37.233 on the session (the ramp's last update) and 38.733 = T on the joiner
 * (the load's stamp), so dt is 9.55 against 8.05 at 794 (1 + 0.32 * dt). The stamp belongs to the unintegrated value, so it is
 * carried (the load-time split: a past event's stamp, not the base time of a value the save writes as of T). The capture writes
 * +0x2f0 and +0x2f8 of every U::Engine; with the opt-in the admission writes the session's where they differ. Status
 * engr=on:written:equal:missing. */
static int join_engr_on;
static uint64_t join_engr_written,join_engr_equal,join_engr_missing;
#define JOIN_ENGR_VTABLE 0x2ad58f8u   /* U::Engine */
static int join_engr_type(unsigned char *o) {
    uintptr_t vt=0;
    return o && join_read(o,&vt,8) && vt==(uintptr_t)image+JOIN_ENGR_VTABLE;
}
static int join_engr_cmp(const void *a,const void *b) {
    uint64_t x=((const join_engr_rec *)a)->portable,y=((const join_engr_rec *)b)->portable;return x<y?-1:x>y;
}
static void join_capture_engr(join_tables *t) {
    uint32_t cap=0;
    for(uint32_t i=0;i<t->nids;i++) {
        if(!t->ids[i].portable) continue;
        unsigned char *o=join_object(t->ids[i].runtime);join_engr_rec r;
        if(!join_engr_type(o)) continue;
        memset(&r,0,sizeof(r));r.portable=t->ids[i].portable;
        if(!join_read(o+0x2f0,&r.ramp_time,8) || !join_read(o+0x2f8,&r.ramp_word,8)) continue;
        if(!JOIN_PUSH(t->engr,t->nengr,cap,r)) return;
    }
    if(t->nengr>1) qsort(t->engr,t->nengr,sizeof(*t->engr),join_engr_cmp);
    {uint32_t k=0;for(uint32_t i=0;i<t->nengr;i++) {if(k && t->engr[k-1].portable==t->engr[i].portable) continue;t->engr[k++]=t->engr[i];}t->nengr=k;}
}
static void join_admit_engr(const join_tables *s,const join_tables *mine) {
    join_engr_written=join_engr_equal=join_engr_missing=0;
    if(!join_engr_on) return;
    for(uint32_t i=0;i<s->nengr;i++) {
        const join_engr_rec *r=&s->engr[i];unsigned char *o=join_object(join_runtime_of(mine,r->portable));
        if(!join_engr_type(o)) {++join_engr_missing;continue;}
        int changed=join_poke((uintptr_t)(o+0x2f0),&r->ramp_time,8);
        changed|=join_poke((uintptr_t)(o+0x2f8),&r->ramp_word,8);
        if(changed) ++join_engr_written; else ++join_engr_equal;
    }
}
/* BNDS (v13, section 72; the admission opt-in, native_gate_join_bnds): every station's bounding structs [F: j1-258 .. j1-266, J1's
 * 1020: ShipMedium 0x82335's AI move.hack.station.storage parks at $target.size / 2 + this.ship.size * 2.5 from the station;
 * station 0x593cc's radius is 3531.08252 on the session and 3531.08276 on the joiner, so its move point of 962 is an ulp apart
 * and its position from 1019]. A station's size (vfunc 0x14b0 = 0x7eb460: station+0xc90) comes from three bounding structs at
 * +0xc90, +0xcc0 and +0xcf0 (0x30 bytes each: centre, extents with w 1, radius +0x20, then three words), which the session grew
 * module by module and the joiner's load rebuilt in one pass: 819 of 855 stations an ulp or more apart in centre, extents or
 * radius (j1-266). The three words after each radius differ between two session clients too (per process) and are not carried.
 * The capture takes the 0x24 bytes of each struct for every object whose vfunc 0x14b0 is 0x7eb460; with the opt-in the admission
 * writes the session's where they differ. Status bnds=on:written:equal:missing.
 * [j1-362: J1's 3780 split is ShipLarge 0x6f1e7's route at 3722, whose two inner nodes the joiner's calculator places 1..4
 * ulps off. Every input the request and the calculator read is equal (the request, its component vector, the 405 transforms
 * of 0x3dde10) except the stations' box at +0xd20, 1..64 ulps apart in centre, extents and radius: the calculator's obstacle
 * volume (0xec0800) takes it for class 0x62.] A station holds five such structs, +0xc60, +0xc90, +0xcc0, +0xcf0 and +0xd20 (the
 * reset 0x7f77d0 fills them from the macro, +0xd20 a copy of +0xc90; the growth paths of Station vfunc978 0x8013f0 widen each,
 * +0xd20 at 0x8026f0), and six floats at +0xd50 (-1 at the reset, widened there too). The record carries all five structs
 * (the first three in their old places) and the six floats. */
/* OMGS (v13, section 73; the admission opt-in, native_gate_join_omgs): each steering ship's two OmegaSwitched controllers
 * [w1z78 .. w1z107: J1's 872 race (and the second pair's 1042) is 176b36's lead pursuit (ChaseBehaviour 0xed68e0 -> 0xed1d10),
 * whose steer call 0xec9fd0 reads a vector through [[agent+0xf80]+0xa0c8] vt+0x3a8 = 0x9db30. That getter recomputes each axis
 * lazily (0x9dba0 -> 0xa8720) from the axis block (+0x50 + i x 0x4f0), four MapCTC tables an axis (+0x1048 + 0x180 each, cached on
 * their key within 1.19e-7) and a reversal gate on a 0.001 threshold: history. At 634 the race joiner's axis 1 had not run its
 * computed part since the reset (sub-object +0x161/+0x162 0 and +0x1a8 0) where the session and the calm joiner had (1, 1, 24); the
 * y it returned at 721 parted by 0.03, and the lead pursuit turned it into 176b36's track]. The object is
 * JPM::OmegaSwitched<JPVec3,double,MapCTC,3,20> (vtable 0x2a24338, built by 0x9e9c0 inside a JPM::Motion at +0x1480, 0x2248 bytes
 * to the Motion's next field); the second one, read on the 0xecb31a path, sits at +0x16398. Its data from +0x10 holds doubles,
 * counters and flags, no pointers (the constructors 0xa5c60, 0xa5df0, 0xa5f00 and 0x9b000 store constants), so the capture takes
 * it raw for every Agent whose flight model is the steering one, and the admission writes it where it differs. Never written, so
 * heap residue on every client: each axis sub-object's (axis +0x190 and +0x340, 0xa5f00) +0x130..+0x13f and its padding, the data
 * block's padding; masked (0 in the record, not written). Status omgs=on:written:equal:missing.
 * One pointer field: each sub-object's std::vector<double> {begin +0x168, end +0x170, capacity +0x178} (zeroed by 0xa5f00; the
 * push_back 0xaf0e0 from the recompute 0xa8720 is the only vector access in the object's 200 vfuncs and the recompute chain)
 * [w1z108: the raw write put the session's heap pointers there and the joiner died at 755 in af405 writing through them]. The
 * record keeps the saver's triple as captured; the admission never writes it raw: a session vector that is empty (every one in
 * w1z108's captures) empties the joiner's own (its end set to its begin); a non-empty one cannot be carried (its elements live in
 * the saver's heap) and the joiner's is left alone. Status omgs_vec=cleared:equal:kept:differs (kept: the same size). */
#define JOIN_OMGS_VTABLE 0x2a24338u   /* JPM::OmegaSwitched<JPVec3,double,MapCTC,3,20> */
#define JOIN_OMGS_HOLDER 0xf80u       /* the Agent's motion block */
static const uint32_t join_omgs_at[2]={0xa0c8u,0x16398u};
typedef struct {uint16_t at,len;} join_omgs_mask_t;
static join_omgs_mask_t join_omgs_mask[64];static unsigned join_omgs_nmask;
#define JOIN_OMGS_VECTORS 6u           /* 3 axes x 2 sub-objects */
#define JOIN_OMGS_VECTOR_AT 0x168u      /* in the sub-object: begin, end, capacity */
static uint16_t join_omgs_vec[JOIN_OMGS_VECTORS];   /* object offsets of the vectors' begin */
static uint64_t join_omgs_vec_cleared,join_omgs_vec_equal,join_omgs_vec_kept,join_omgs_vec_differs;
static void join_omgs_masks(void) {
    if(join_omgs_nmask) return;
    static const uint16_t data_pad[][2]={{0x4c,4},{0xf22,6},{0xf8a,6},{0x1014,4},{0x1045,3}};   /* object offsets */
    for(unsigned k=0;k<sizeof(data_pad)/sizeof(*data_pad);k++) join_omgs_mask[join_omgs_nmask++]=(join_omgs_mask_t){data_pad[k][0],data_pad[k][1]};
    static const uint16_t sub_pad[][2]={{0x130,16},{0x164,4},{0x194,4},{0x1a1,7},{0x1ac,4}};     /* in each 0x1b0-byte sub-object */
    for(unsigned axis=0;axis<3u;axis++) for(unsigned s=0;s<2u;s++) {
        const unsigned base=0x50u+axis*0x4f0u+(s?0x340u:0x190u);
        for(unsigned k=0;k<sizeof(sub_pad)/sizeof(*sub_pad);k++) join_omgs_mask[join_omgs_nmask++]=(join_omgs_mask_t){(uint16_t)(base+sub_pad[k][0]),sub_pad[k][1]};
        join_omgs_vec[axis*2u+s]=(uint16_t)(base+JOIN_OMGS_VECTOR_AT);
    }
}
/* Does [at, at+n) touch a vector triple (never written raw)? */
static int join_omgs_in_vector(unsigned at,unsigned n) {
    for(unsigned v=0;v<JOIN_OMGS_VECTORS;v++) if(at<join_omgs_vec[v]+24u && join_omgs_vec[v]<at+n) return 1;
    return 0;
}
/* The joiner's own vector against the session's size (from the record's triple, the saver's pointers): see OMGS above. */
static int join_omgs_vector(unsigned char *o,const join_omgs_rec *r,unsigned v) {
    uint64_t s[3],j[3];const unsigned at=join_omgs_vec[v];
    memcpy(s,r->data+(at-JOIN_OMGS_FROM),24);
    if(!join_read(o+at,j,24)) return 0;
    const int s_ok=s[0]<=s[1] && s[1]<=s[2] && !((s[1]-s[0])%8u),j_ok=j[0]<=j[1] && j[1]<=j[2] && !((j[1]-j[0])%8u);
    if(!s_ok || !j_ok) {++join_omgs_vec_differs;return 0;}
    const uint64_t ns=(s[1]-s[0])/8u,nj=(j[1]-j[0])/8u;
    if(ns) {if(ns==nj) ++join_omgs_vec_kept; else ++join_omgs_vec_differs;return 0;}
    if(!nj) {++join_omgs_vec_equal;return 0;}
    const int changed=join_poke((uintptr_t)(o+at+8u),&j[0],8);   /* end = begin: empty, the joiner's own storage kept */
    ++join_omgs_vec_cleared;return changed;
}
static unsigned char *join_omgs_object(const unsigned char *agent,unsigned which) {
    unsigned char *holder=NULL,*o=NULL;uintptr_t vt=0;
    if(!agent || which>1u || !join_read(agent+JOIN_OMGS_HOLDER,&holder,8) || !holder || !join_read(holder+join_omgs_at[which],&o,8) || !o ||
       !join_read(o,&vt,8) || vt!=(uintptr_t)image+JOIN_OMGS_VTABLE) return NULL;
    return o;
}
static int join_omgs_on;
static uint64_t join_omgs_written,join_omgs_equal,join_omgs_missing;
static uint32_t join_omgs_cap;   /* the FLIGHT walk's OMGS capacity (reset with each new table) */
static int join_capture_omgs(join_tables *t,const unsigned char *agent,uint64_t portable,uint32_t *cap) {
    join_omgs_masks();
    if(!t->omgs) *cap=0;
    for(unsigned which=0;which<2u;which++) {
        unsigned char *o=join_omgs_object(agent,which);join_omgs_rec r;
        if(!o) continue;
        memset(&r,0,sizeof(r));r.portable=portable;r.which=which;
        if(!join_read(o+JOIN_OMGS_FROM,r.data,JOIN_OMGS_BYTES)) continue;
        for(unsigned k=0;k<join_omgs_nmask;k++) memset(r.data+join_omgs_mask[k].at-JOIN_OMGS_FROM,0,join_omgs_mask[k].len);
        if(!JOIN_PUSH(t->omgs,t->nomgs,*cap,r)) return join_note("memory");
    }
    return 1;
}
static int join_omgs_cmp(const void *a,const void *b) {
    const join_omgs_rec *x=a,*y=b;
    return x->portable<y->portable?-1:x->portable>y->portable?1:x->which<y->which?-1:x->which>y->which;
}
static void join_sort_omgs(join_tables *t) {
    if(t->nomgs>1) qsort(t->omgs,t->nomgs,sizeof(*t->omgs),join_omgs_cmp);
    uint32_t k=0;
    for(uint32_t i=0;i<t->nomgs;i++) {
        if(k && t->omgs[k-1].portable==t->omgs[i].portable && t->omgs[k-1].which==t->omgs[i].which) continue;
        if(k!=i) t->omgs[k]=t->omgs[i];
        ++k;
    }
    t->nomgs=k;
}
/* Write the record's bytes between the masked ranges and the vector triples (sorted by offset), each run only where it differs;
 * then each vector by its size. */
static int join_omgs_write(unsigned char *o,const join_omgs_rec *r) {
    int changed=0;unsigned at=JOIN_OMGS_FROM;
    for(;;) {
        unsigned next=JOIN_OMGS_SIZE,len=0;
        for(unsigned k=0;k<join_omgs_nmask;k++) if(join_omgs_mask[k].at>=at && join_omgs_mask[k].at<next) {next=join_omgs_mask[k].at;len=join_omgs_mask[k].len;}
        for(unsigned v=0;v<JOIN_OMGS_VECTORS;v++) if(join_omgs_vec[v]>=at && join_omgs_vec[v]<next) {next=join_omgs_vec[v];len=24u;}
        if(next>at) {
            if(join_omgs_in_vector(at,next-at)) return -1;   /* a run across a vector: the layout is not the one read */
            changed|=join_poke((uintptr_t)(o+at),r->data+(at-JOIN_OMGS_FROM),next-at);
        }
        if(next>=JOIN_OMGS_SIZE) break;
        at=next+len;
    }
    for(unsigned v=0;v<JOIN_OMGS_VECTORS;v++) changed|=join_omgs_vector(o,r,v);
    return changed;
}
static void join_admit_omgs(const join_tables *s,const join_tables *mine) {
    join_omgs_written=join_omgs_equal=join_omgs_missing=0;
    join_omgs_vec_cleared=join_omgs_vec_equal=join_omgs_vec_kept=join_omgs_vec_differs=0;
    if(!join_omgs_on) return;
    join_omgs_masks();
    for(uint32_t i=0;i<s->nomgs;i++) {
        const join_omgs_rec *r=&s->omgs[i];unsigned char *object=join_object(join_runtime_of(mine,r->portable));
        unsigned char *o=join_omgs_object(object?join_nav_agent(object):NULL,r->which);
        if(!o) {++join_omgs_missing;continue;}
        const int w=join_omgs_write(o,r);
        if(w<0) {++join_omgs_missing;continue;}
        if(w) ++join_omgs_written; else ++join_omgs_equal;
    }
}
/* RESV (v13, section 74; the admission opt-in, native_gate_join_resv): every zone's space reservations [j1-335, w1z151, w1z155:
 * Zone::GetSafePos 0x835b80 adds each reservation's stored box (0x835630 walks the tree at zone+0x3b0 and appends node+0x30 ..
 * +0x9f) to the solver's boxes; on the joiner those boxes differ in their rotation rows (1 .. 8 ulps) and w lanes, the positions
 * equal, and the get_safe_pos points from about 40 s after the join come out 1 .. 3 ulps apart. The session computed them when
 * the ships reserved (0x834f90); the joiner's load imports each node with a zeroed box, identity rows and expiry -1.0
 * (U::Zone::ImportReservation 0x835230), refilled from its own state.] The tree: header zone+0x3b0 (a node: +0 parent, +8 left,
 * +0x10 right; the header's +8 the root), the leftmost node at +0x3d0, the count +0x3d8; a node's key: +0x20 the owner's runtime
 * ID, +0x28 the index; its box +0x30 .. +0x9f, its expiry +0xa0 (0x835860 drops nodes past it). The capture takes every Zone's
 * nodes by the zone's and the owner's portable IDs and the index: the box raw and the expiry; with the opt-in the admission
 * writes the session's where they differ. Status resv=on:written:equal:missing:extra (extra: the joiner's nodes no record
 * matched). */
#define JOIN_RESV_TREE 0x3b0u
#define JOIN_RESV_BEGIN 0x3d0u
#define JOIN_RESV_COUNT 0x3d8u
#define JOIN_RESV_OWNER 0x20u
#define JOIN_RESV_INDEX 0x28u
#define JOIN_RESV_BOX 0x30u
#define JOIN_RESV_EXPIRY 0xa0u
#define JOIN_RESV_MAX 100000u
static int join_resv_on;
static uint64_t join_resv_written,join_resv_equal,join_resv_missing,join_resv_extra;
static int join_resv_zone(unsigned char *o) {uintptr_t vt=0;return o && join_read(o,&vt,8) && vt==(uintptr_t)image+JOIN_ZONE_VTABLE;}
/* The in-order successor as 0x835aa9 .. 0x835aef takes it; NULL on a read failure (the header ends the walk). */
static unsigned char *join_resv_next(unsigned char *node) {
    unsigned char *r=NULL;
    if(!join_read(node+0x10,&r,8)) return NULL;
    if(r) {for(unsigned d=0;d<128u;d++) {unsigned char *l=NULL;if(!join_read(r+8,&l,8)) return NULL;if(!l) return r;r=l;}return NULL;}
    unsigned char *q=node,*p=NULL;
    for(unsigned d=0;d<128u;d++) {
        unsigned char *pl=NULL;
        if(!join_read(q,&p,8) || !p || !join_read(p+8,&pl,8)) return NULL;
        if(pl==q) return p;
        q=p;
    }
    return NULL;
}
/* Each node of a zone's tree in order: visit(node, ctx) returns 0 to stop; the node count, or -1 on a broken walk. */
static long join_resv_walk(unsigned char *zone,int (*visit)(unsigned char *,void *),void *ctx) {
    unsigned char *header=zone+JOIN_RESV_TREE,*node=NULL;uint64_t count=0;long seen=0;
    if(!join_read(zone+JOIN_RESV_BEGIN,&node,8) || !join_read(zone+JOIN_RESV_COUNT,&count,8) || count>JOIN_RESV_MAX) return -1;
    while(node && node!=header) {
        if((uint64_t)seen>=count) return -1;
        ++seen;if(visit && !visit(node,ctx)) return seen;
        node=join_resv_next(node);
    }
    return node==header && (uint64_t)seen==count?seen:-1;
}
typedef struct {join_tables *t;uint64_t zone;uint32_t *cap;int failed;} join_resv_capture_ctx;
static int join_resv_capture_visit(unsigned char *node,void *p) {
    join_resv_capture_ctx *c=(join_resv_capture_ctx*)p;uint64_t owner=0;unsigned klass=0;join_resv_rec r;memset(&r,0,sizeof(r));
    if(!join_read(node+JOIN_RESV_OWNER,&owner,8) || !join_read(node+JOIN_RESV_INDEX,&r.index,8) ||
       !join_read(node+JOIN_RESV_BOX,r.box,JOIN_RESV_BOX_BYTES) || !join_read(node+JOIN_RESV_EXPIRY,&r.expiry,8)) return 1;
    r.zone=c->zone;r.owner=join_key(owner,&klass);
    if(!r.owner) return 1;
    if(!JOIN_PUSH(c->t->resv,c->t->nresv,*c->cap,r)) {c->failed=1;return 0;}
    return 1;
}
static int join_resv_cmp(const void *a,const void *b) {
    const join_resv_rec *x=a,*y=b;
    if(x->zone!=y->zone) return x->zone<y->zone?-1:1;
    if(x->owner!=y->owner) return x->owner<y->owner?-1:1;
    return x->index<y->index?-1:x->index>y->index;
}
static void join_capture_resv(join_tables *t) {
    uint32_t cap=0;
    for(uint32_t i=0;i<t->nids;i++) {
        if(!t->ids[i].portable) continue;
        unsigned char *o=join_object(t->ids[i].runtime);
        if(!join_resv_zone(o)) continue;
        join_resv_capture_ctx c={t,t->ids[i].portable,&cap,0};
        (void)join_resv_walk(o,join_resv_capture_visit,&c);
        if(c.failed) return;
    }
    if(t->nresv>1) qsort(t->resv,t->nresv,sizeof(*t->resv),join_resv_cmp);
    {uint32_t k=0;for(uint32_t i=0;i<t->nresv;i++) {if(k && !join_resv_cmp(&t->resv[k-1],&t->resv[i])) continue;t->resv[k++]=t->resv[i];}t->nresv=k;}
}
typedef struct {uint64_t owner,index;unsigned char *hit;} join_resv_find_ctx;
static int join_resv_find_visit(unsigned char *node,void *p) {
    join_resv_find_ctx *c=(join_resv_find_ctx*)p;uint64_t owner=0,index=0;
    if(join_read(node+JOIN_RESV_OWNER,&owner,8) && join_read(node+JOIN_RESV_INDEX,&index,8) && owner==c->owner && index==c->index) {c->hit=node;return 0;}
    return 1;
}
static void join_admit_resv(const join_tables *s,const join_tables *mine) {
    join_resv_written=join_resv_equal=join_resv_missing=join_resv_extra=0;
    if(!join_resv_on) return;
    uint64_t matched=0,nodes=0;
    for(uint32_t i=0;i<s->nresv;i++) {
        const join_resv_rec *r=&s->resv[i];unsigned char *zone=join_object(join_runtime_of(mine,r->zone));
        join_resv_find_ctx c={join_runtime_of(mine,r->owner),r->index,NULL};
        if(c.owner && join_resv_zone(zone)) (void)join_resv_walk(zone,join_resv_find_visit,&c);
        if(!c.hit) {++join_resv_missing;continue;}
        ++matched;
        int changed=join_poke((uintptr_t)(c.hit+JOIN_RESV_BOX),r->box,JOIN_RESV_BOX_BYTES);
        changed|=join_poke((uintptr_t)(c.hit+JOIN_RESV_EXPIRY),&r->expiry,8);
        if(changed) ++join_resv_written; else ++join_resv_equal;
    }
    for(uint32_t i=0;i<mine->nids;i++) {
        unsigned char *o=mine->ids[i].portable?join_object(mine->ids[i].runtime):NULL;
        if(!join_resv_zone(o)) continue;
        const long n=join_resv_walk(o,NULL,NULL);if(n>0) nodes+=(uint64_t)n;
    }
    join_resv_extra=nodes>matched?nodes-matched:0;
}
/* CONN (v13, section 76; the admission opt-in, native_gate_join_conn) [j1-341 .. j1-349: three minelayers that wait in their
 * jobs' <waiting> lists at T launch 44 ticks late on the joiner. NPC_State_Machines' ENTER_ai_pilot walks a pilot to its post unless
 * $NPC.controlled, and the NPC's test behind it (vt+0x1c50(1), 0x6c71f0) wants the NPC's connection of kind 2 (the save's
 * roomposition) connected to its controllable's con_control. The joiner's load creates both U::Connections, with the session's
 * IDs, but leaves such a waiting ship's pairs unconnected; X4's connect 0x3d87e0 creates the two and sets each one's +0x10 to the
 * other.] A component's connections: *(component+0xc0) (0x3d87e0 creates it), 0x18-byte buckets by kind from +0 to +8, each a
 * std::list (head +0, size +8) whose nodes embed a U::Connection at +0x10 (vtable 0x2bab2e0: +8 its ID, +0x10 the partner
 * connection, +0x18 the owner component, +0x20 its TemplateConnection). The capture takes every U::NPC's connections that have a
 * partner and each partner's own partner, as (owner's portable ID, ID) -> (partner owner's portable ID, partner ID). With the
 * opt-in the admission sets a joiner connection's partner where it is null and the record's partner resolves. Status
 * conn=on:written:equal:missing:conflict:left (conflict: another partner, left untouched; left: the joiner's NPC connections
 * still without a partner afterwards). */
#define JOIN_CONN_VTABLE 0x2bab2e0u       /* U::Connection */
#define JOIN_CONN_NPC_VTABLE 0x2b38a48u   /* U::NPC */
#define JOIN_CONN_CONTAINER 0xc0u
#define JOIN_CONN_BUCKET 0x18u
#define JOIN_CONN_BUCKETS 64u
#define JOIN_CONN_LIST_MAX 4096u
#define JOIN_CONN_NODE 0x10u              /* the node's embedded connection */
#define JOIN_CONN_ID 0x08u
#define JOIN_CONN_PEER 0x10u
#define JOIN_CONN_OWNER 0x18u
static int join_conn_on;
static uint64_t join_conn_written,join_conn_equal,join_conn_missing,join_conn_conflict,join_conn_left;
static int join_conn_is(const unsigned char *c) {uintptr_t vt=0;return c && join_read(c,&vt,8) && vt==(uintptr_t)image+JOIN_CONN_VTABLE;}
static int join_conn_npc(const unsigned char *o) {uintptr_t vt=0;return o && join_read(o,&vt,8) && vt==(uintptr_t)image+JOIN_CONN_NPC_VTABLE;}
/* Each U::Connection of a component: visit(connection, ctx) returns 0 to stop; the count, or -1 on a broken container. */
static long join_conn_walk(const unsigned char *component,int (*visit)(unsigned char *,void *),void *ctx) {
    unsigned char *c=NULL,*b=NULL,*e=NULL;long seen=0;
    if(!component || !join_read(component+JOIN_CONN_CONTAINER,&c,8)) return -1;
    if(!c) return 0;
    if(!join_read(c,&b,8) || !join_read(c+8,&e,8) || !b || e<b || (size_t)(e-b)%JOIN_CONN_BUCKET ||
       (size_t)(e-b)/JOIN_CONN_BUCKET>JOIN_CONN_BUCKETS) return -1;
    for(unsigned char *k=b;k<e;k+=JOIN_CONN_BUCKET) {
        unsigned char *head=NULL,*node=NULL;uint64_t size=0,n=0;
        if(!join_read(k,&head,8) || !join_read(k+8,&size,8) || size>JOIN_CONN_LIST_MAX) return -1;
        if(!size) continue;
        if(!head || !join_read(head,&node,8)) return -1;
        while(node && node!=head) {
            unsigned char *conn=node+JOIN_CONN_NODE;
            if(++n>size || !join_conn_is(conn)) return -1;
            ++seen;if(visit && !visit(conn,ctx)) return seen;
            if(!join_read(node,&node,8)) return -1;
        }
        if(node!=head || n!=size) return -1;
    }
    return seen;
}
/* A connection's owner as a portable ID (0: none) and its ID. */
static uint64_t join_conn_key(const unsigned char *conn,uint64_t *id) {
    unsigned char *owner=NULL;uint64_t runtime=0;unsigned klass=0;*id=0;
    if(!join_conn_is(conn) || !join_read(conn+JOIN_CONN_ID,id,8) || !join_read(conn+JOIN_CONN_OWNER,&owner,8) || !owner ||
       !join_read(owner+8,&runtime,8) || !runtime) return 0;
    return join_key(runtime,&klass);
}
typedef struct {join_tables *t;uint32_t *cap;int failed;} join_conn_capture_ctx;
static int join_conn_record(join_conn_capture_ctx *c,const unsigned char *conn) {
    unsigned char *peer=NULL;join_conn_rec r;memset(&r,0,sizeof(r));
    if(!join_read(conn+JOIN_CONN_PEER,&peer,8) || !peer) return 1;
    r.owner=join_conn_key(conn,&r.id);r.peer_owner=join_conn_key(peer,&r.peer_id);
    if(!r.owner || !r.id || !r.peer_owner || !r.peer_id) return 1;
    if(!JOIN_PUSH(c->t->conn,c->t->nconn,*c->cap,r)) {c->failed=1;return 0;}
    return 1;
}
static int join_conn_capture_visit(unsigned char *conn,void *p) {
    join_conn_capture_ctx *c=(join_conn_capture_ctx*)p;unsigned char *peer=NULL;
    if(!join_conn_record(c,conn)) return 0;
    if(join_read(conn+JOIN_CONN_PEER,&peer,8) && peer && !join_conn_record(c,peer)) return 0;   /* the partner's own partner */
    return 1;
}
static int join_conn_cmp(const void *a,const void *b) {
    const join_conn_rec *x=a,*y=b;
    if(x->owner!=y->owner) return x->owner<y->owner?-1:1;
    return x->id<y->id?-1:x->id>y->id;
}
static void join_capture_conn(join_tables *t) {
    uint32_t cap=0;
    for(uint32_t i=0;i<t->nids;i++) {
        if(!t->ids[i].portable) continue;
        unsigned char *o=join_object(t->ids[i].runtime);
        if(!join_conn_npc(o)) continue;
        join_conn_capture_ctx c={t,&cap,0};
        (void)join_conn_walk(o,join_conn_capture_visit,&c);
        if(c.failed) return;
    }
    if(t->nconn>1) qsort(t->conn,t->nconn,sizeof(*t->conn),join_conn_cmp);
    {uint32_t k=0;for(uint32_t i=0;i<t->nconn;i++) {if(k && !join_conn_cmp(&t->conn[k-1],&t->conn[i])) continue;t->conn[k++]=t->conn[i];}t->nconn=k;}
}
typedef struct {uint64_t id;unsigned char *hit;} join_conn_find_ctx;
static int join_conn_find_visit(unsigned char *conn,void *p) {
    join_conn_find_ctx *c=(join_conn_find_ctx*)p;uint64_t id=0;
    if(join_read(conn+JOIN_CONN_ID,&id,8) && id==c->id) {c->hit=conn;return 0;}
    return 1;
}
static unsigned char *join_conn_find(const join_tables *mine,uint64_t owner,uint64_t id) {
    join_conn_find_ctx c={id,NULL};const uint64_t runtime=join_runtime_of(mine,owner);
    unsigned char *o=runtime?join_object(runtime):NULL;
    if(o) (void)join_conn_walk(o,join_conn_find_visit,&c);
    return c.hit;
}
static int join_conn_left_visit(unsigned char *conn,void *p) {
    unsigned char *peer=NULL;if(join_read(conn+JOIN_CONN_PEER,&peer,8) && !peer) ++*(uint64_t*)p;
    return 1;
}
static void join_admit_conn(const join_tables *s,const join_tables *mine) {
    join_conn_written=join_conn_equal=join_conn_missing=join_conn_conflict=join_conn_left=0;
    if(!join_conn_on) return;
    for(uint32_t i=0;i<s->nconn;i++) {
        const join_conn_rec *r=&s->conn[i];unsigned char *now=NULL;
        unsigned char *a=join_conn_find(mine,r->owner,r->id),*b=a?join_conn_find(mine,r->peer_owner,r->peer_id):NULL;
        if(!a || !b || !join_read(a+JOIN_CONN_PEER,&now,8)) {++join_conn_missing;continue;}
        if(now==b) {++join_conn_equal;continue;}
        if(now) {++join_conn_conflict;continue;}
        if(join_poke((uintptr_t)(a+JOIN_CONN_PEER),&b,8)) ++join_conn_written; else ++join_conn_equal;
    }
    for(uint32_t i=0;i<mine->nids;i++) {
        unsigned char *o=mine->ids[i].portable?join_object(mine->ids[i].runtime):NULL;
        if(join_conn_npc(o)) (void)join_conn_walk(o,join_conn_left_visit,&join_conn_left);
    }
}
/* WARP (v13, section 77; the admission opt-in, native_gate_join_warp): the pending U::WarpEvents' destinations [F: j1-367 .. j1-371,
 * a late J1 join (T=3033): ShipSmall 0x92c2b entered a gate at T, its EnteredGateEvent and WarpEvent due at T+1; the save writes the
 * warp's destination as <offset> with the position in 17 digits and <rotation yaw="180"/>, so the joiner's event, rebuilt from the
 * angle, holds a rotation whose near-zero lanes are float sin(pi) (8.742278e-8) where the session's (the arrival gate's own rotation)
 * are -2.38e-8: the ship arrives turned by an ulp and its motion parts from T+1, the only object the census at T+1 finds apart (j1-371)].
 * U::WarpEvent (vtable 0x2baa8c0, built by 0x9a9d00): +0x18 the zone's runtime ID, +0x20 a 0x40-byte copy of the destination
 * transform, +0x28 a 0x40-byte descriptor, +0x30/+0x31 flags. An event source is its component (the schedule's node +0x28; +0x20 its
 * own event tree: leftmost +0x20, count +0x28; per node the due time +0x20 and the event +0x28). The capture takes the transform of
 * every pending WarpEvent of a keyed source with its place among that source's warps; with the opt-in the admission writes the
 * session's into the joiner's event at the same place of the same source. Status warp=on:written:equal:missing:extra warp_cap=N.
 * j1-372: missing 1. The warp is due at T's game time, so the joiner's boot event hold (native_gate_join_hold_boot_events) had swapped
 * it in the source's tree for a stand-in and kept it (lockstep_join.h join_bootev_items) until the admission's repost: a source's
 * places are its held warps first (they are its earliest, due at or before T), then the warps still in its tree. */
#define JOIN_WARP_VTABLE 0x2baa8c0u
#define JOIN_WARP_XFORM 0x20u
static int join_warp_on;
static uint64_t join_warp_written,join_warp_equal,join_warp_missing,join_warp_extra;
static int join_warp_cmp(const void *a,const void *b) {
    const join_warp_rec *x=a,*y=b;
    if(x->portable!=y->portable) return x->portable<y->portable?-1:1;
    return x->index<y->index?-1:x->index>y->index;
}
/* The place-th WarpEvent pending at a source (its due time into *time), NULL past the last; their count into *n. */
static unsigned char *join_warp_event(unsigned char *source,uint32_t place,uint32_t *n,uint64_t *time) {
    unsigned char *q=NULL,*e=NULL,*hit=NULL;uint64_t count=0;uint32_t seen=0;
    if(n) *n=0;
    if(!source || !join_read(source+0x20,&q,8) || !q || !join_read(q+0x28,&count,8) || !join_read(q+0x20,&e,8)) return NULL;
    for(uint64_t k=0;k<count && k<4096u && e;k++,e=join_evq_next(e)) {
        unsigned char *ev=NULL;uintptr_t v=0;
        if(!join_read(e+0x28,&ev,8) || !ev || !join_read(ev,&v,8)) break;
        if(v!=(uintptr_t)image+JOIN_WARP_VTABLE) continue;
        if(seen==place && !hit) {hit=ev;if(time && !join_read(e+0x20,time,8)) *time=0;}
        ++seen;
    }
    if(n) *n=seen;
    return hit;
}
static void join_capture_warp(join_tables *t) {
    uint64_t count=0;unsigned char *node=NULL;uint32_t cap=0;
    if(!join_read(join_evq_at(JOIN_EVQ_COUNT),&count,8) || !count || !join_read(join_evq_at(JOIN_EVQ_LEFT),&node,8)) return;
    for(uint64_t i=0;i<count && i<200000u && node;i++,node=join_evq_next(node)) {
        unsigned char *src=NULL;uint64_t runtime=0;unsigned klass;uint32_t n=0;
        if(!join_read(node+0x28,&src,8) || !src || !join_read(src+8,&runtime,8)) continue;
        const uint64_t key=runtime?join_key(runtime,&klass):0;
        if(!key) continue;
        (void)join_warp_event(src,0xffffffffu,&n,NULL);
        for(uint32_t k=0;k<n && k<64u;k++) {
            join_warp_rec r;unsigned char *xf=NULL;memset(&r,0,sizeof(r));
            unsigned char *ev=join_warp_event(src,k,NULL,&r.time);
            if(!ev || !join_read(ev+JOIN_WARP_XFORM,&xf,8) || !xf || !join_read(xf,r.xform,sizeof(r.xform))) continue;
            r.portable=key;r.index=k;
            if(!JOIN_PUSH(t->warp,t->nwarp,cap,r)) return;
        }
    }
    if(t->nwarp>1) qsort(t->warp,t->nwarp,sizeof(*t->warp),join_warp_cmp);
    {uint32_t k=0;for(uint32_t i=0;i<t->nwarp;i++) {if(k && !join_warp_cmp(&t->warp[k-1],&t->warp[i])) continue;t->warp[k++]=t->warp[i];}t->nwarp=k;}
}
static unsigned char *join_bootev_warp(unsigned char *source,uint32_t place,uint32_t *n);   /* lockstep_join.h: the held ones */
static void join_admit_warp(const join_tables *s,const join_tables *mine) {
    join_warp_written=join_warp_equal=join_warp_missing=join_warp_extra=0;
    if(!join_warp_on) return;
    for(uint32_t i=0;i<s->nwarp;i++) {
        const join_warp_rec *r=&s->warp[i];
        unsigned char *o=join_object(join_runtime_of(mine,r->portable)),*xf=NULL,now[0x40],*ev=NULL;uint32_t n=0,held=0,tree=0;
        if(o) {
            (void)join_bootev_warp(o,0xffffffffu,&held);(void)join_warp_event(o,0xffffffffu,&tree,NULL);n=held+tree;
            ev=r->index<held?join_bootev_warp(o,r->index,NULL):join_warp_event(o,r->index-held,NULL,NULL);
        }
        if(o && (i+1==s->nwarp || s->warp[i+1].portable!=r->portable) && n>r->index+1u) join_warp_extra+=n-(r->index+1u);   /* the source's last */
        if(!ev || !join_read(ev+JOIN_WARP_XFORM,&xf,8) || !xf || !join_read(xf,now,sizeof(now))) {++join_warp_missing;continue;}
        if(!memcmp(now,r->xform,sizeof(now))) {++join_warp_equal;continue;}
        if(join_poke((uintptr_t)xf,r->xform,sizeof(r->xform))) ++join_warp_written; else ++join_warp_equal;
    }
}
/* LSTN (v13, section 79; opt-in native_gate_join_lstn on the joiner). An event source's listeners for one event type sit in its
 * +0x28 table (JOIN_LSTN above) in registration order, and DeliverEvent hands an event to them in that order. The session built
 * the order over time; the joiner's load registers every script's listeners anew in its own order (j1-386: the carrier's defence
 * NPC 0x123675, type 306 (ObjectSignalled): the escort pilots' AICallStackContexts cf8, c14, cbf, b30, ... on the session, ascending
 * on the joiner; at 3092 the first handler of 'blockingdockapproach cleared' takes the dock slot). The saver records every vector
 * of a keyed source with at least two script listeners (AIScriptContext or AICallStackContext with a keyed owner), each listener
 * in order as (kind, owner portable, script hash); flags UNMAPPED: no key (kind 0: owner the vtable RVA). The joiner keys its own
 * vector the same way and writes the listeners both sides key alike, each key once, into the session's relative order, in the
 * slots they hold (the others stay); status lstn=on:groups:written:equal:missing:partial:dup (partial: the key sets differ, the
 * common ones ordered; dup: a key twice, left). */
#define JOIN_LSTN_TABLE 0x28u
#define JOIN_LSTN_ENTRY 0x28u
#define JOIN_LSTN_MAX 1024u
enum {JOIN_LSTN_UNMAPPED=1u};
static int join_lstn_on;
static uint64_t join_lstn_groups,join_lstn_written,join_lstn_equal,join_lstn_missing,join_lstn_partial,join_lstn_dup;
static int join_lstn_cmp(const void *a,const void *b) {
    const join_lstn_rec *x=a,*y=b;
    if(x->source!=y->source) return x->source<y->source?-1:1;
    if(x->type!=y->type) return x->type<y->type?-1:1;
    return x->index<y->index?-1:x->index>y->index;
}
static int join_lstn_table(unsigned char *o,uintptr_t v[2]) {
    uintptr_t table=0;v[0]=v[1]=0;
    return join_read(o+JOIN_LSTN_TABLE,&table,8) && table && join_read((void *)table,v,16) && v[1]>=v[0] &&
           !((v[1]-v[0])%JOIN_LSTN_ENTRY) && (v[1]-v[0])/JOIN_LSTN_ENTRY<=1024u;
}
static int join_lstn_entry(uintptr_t p,uint32_t *type,uintptr_t lv[2]) {
    return join_read((void *)p,type,4) && join_read((void *)(p+8),lv,16) && lv[1]>=lv[0] && !((lv[1]-lv[0])&7u) &&
           (lv[1]-lv[0])/8u<=JOIN_LSTN_MAX;
}
static unsigned char *join_lstn_buf[JOIN_LSTN_MAX];
static void join_capture_lstn(join_tables *t) {
    const join_engine e=join_lstn_engine();uint32_t cap=0;
    for(uint32_t i=0;i<t->nids;i++) {
        unsigned char *o=t->ids[i].portable?join_object(t->ids[i].runtime):NULL;uintptr_t v[2];
        if(!o || !join_lstn_table(o,v)) continue;
        for(uintptr_t p=v[0];p<v[1];p+=JOIN_LSTN_ENTRY) {
            uint32_t type=0,ai=0;uintptr_t lv[2];
            if(!join_lstn_entry(p,&type,lv) || (lv[1]-lv[0])/8u<2u) continue;
            const uint32_t n=(uint32_t)((lv[1]-lv[0])/8u);
            if(!join_read((void *)lv[0],join_lstn_buf,(size_t)n*8u)) continue;
            static join_lstn_rec rs[JOIN_LSTN_MAX];
            for(uint32_t k=0;k<n;k++) {
                join_lstn_rec *r=&rs[k];memset(r,0,sizeof(*r));
                r->source=t->ids[i].portable;r->type=type;r->index=k;
                r->kind=join_lstn_key(&e,join_lstn_buf[k],&r->owner,&r->script);
                if(!r->kind || !r->owner) r->flags=JOIN_LSTN_UNMAPPED;
                if(r->kind && r->kind<3u && r->owner) ++ai;
            }
            if(ai<2u) continue;
            for(uint32_t k=0;k<n;k++) if(!JOIN_PUSH(t->lstn,t->nlstn,cap,rs[k])) return;
        }
    }
    if(t->nlstn>1) qsort(t->lstn,t->nlstn,sizeof(*t->lstn),join_lstn_cmp);
}
static int join_lstn_same(const join_lstn_rec *a,unsigned kind,uint64_t owner,uint64_t script) {return a->kind==kind && a->owner==owner && a->script==script;}
/* One (source, type) group of the session's records against the joiner's vector of that object: -1 missing, else the slots written. */
static int join_lstn_apply(const join_lstn_rec *g,uint32_t ng,unsigned char *o) {
    const join_engine e=join_lstn_engine();uintptr_t v[2],lv[2]={0,0};int found=0;
    if(!o || !join_lstn_table(o,v)) return -1;
    for(uintptr_t p=v[0];p<v[1];p+=JOIN_LSTN_ENTRY) {uint32_t type=0;if(join_lstn_entry(p,&type,lv) && type==g[0].type) {found=1;break;}}
    if(!found) return -1;
    const uint32_t n=(uint32_t)((lv[1]-lv[0])/8u);
    if(!n || !join_read((void *)lv[0],join_lstn_buf,(size_t)n*8u)) return -1;
    static unsigned kinds[JOIN_LSTN_MAX];static uint64_t owners[JOIN_LSTN_MAX],scripts[JOIN_LSTN_MAX];static int at_session[JOIN_LSTN_MAX];
    for(uint32_t k=0;k<n;k++) {
        kinds[k]=join_lstn_key(&e,join_lstn_buf[k],&owners[k],&scripts[k]);at_session[k]=-1;
        if(!kinds[k] || !owners[k]) continue;
        for(uint32_t q=0;q<ng;q++) if(!g[q].flags && join_lstn_same(&g[q],kinds[k],owners[k],scripts[k])) {at_session[k]=(int)q;break;}
    }
    for(uint32_t a=0;a<ng;a++) for(uint32_t b=a+1u;b<ng;b++)   /* a key twice on either side: left as it is */
        if(!g[a].flags && !g[b].flags && join_lstn_same(&g[a],g[b].kind,g[b].owner,g[b].script)) {++join_lstn_dup;return 0;}
    uint32_t common=0,mapped=0,session_mapped=0;
    for(uint32_t k=0;k<n;k++) {
        if(kinds[k] && owners[k]) ++mapped;
        if(at_session[k]<0) continue;
        for(uint32_t k2=k+1u;k2<n;k2++) if(at_session[k2]==at_session[k]) {++join_lstn_dup;return 0;}
        ++common;
    }
    for(uint32_t q=0;q<ng;q++) if(!g[q].flags) ++session_mapped;
    if(common!=session_mapped || common!=mapped) ++join_lstn_partial;
    if(common<2u) return 0;
    /* the common listeners' slots, in the joiner's order, take them in the session's order */
    static unsigned char *want[JOIN_LSTN_MAX];static uint32_t slots[JOIN_LSTN_MAX];uint32_t ns=0;
    for(uint32_t k=0;k<n;k++) if(at_session[k]>=0) slots[ns++]=k;
    uint32_t w=0;
    for(uint32_t q=0;q<ng && w<ns;q++) for(uint32_t k=0;k<n;k++) if(at_session[k]==(int)q) {want[w++]=join_lstn_buf[k];break;}
    int written=0;
    for(uint32_t s=0;s<ns;s++) {
        if(want[s]==join_lstn_buf[slots[s]]) continue;
        if(join_poke(lv[0]+8u*slots[s],&want[s],8)) ++written;
    }
    return written;
}
static void join_admit_lstn(const join_tables *s,const join_tables *mine) {
    join_lstn_groups=join_lstn_written=join_lstn_equal=join_lstn_missing=join_lstn_partial=join_lstn_dup=0;
    if(!join_lstn_on) return;
    for(uint32_t i=0;i<s->nlstn;) {
        uint32_t j=i+1u;
        while(j<s->nlstn && s->lstn[j].source==s->lstn[i].source && s->lstn[j].type==s->lstn[i].type) ++j;
        ++join_lstn_groups;
        const int w=join_lstn_apply(&s->lstn[i],j-i,join_object(join_runtime_of(mine,s->lstn[i].source)));
        if(w<0) ++join_lstn_missing; else if(w) ++join_lstn_written; else ++join_lstn_equal;
        i=j;
    }
}
/* SRCS (v13, section 81; opt-in native_gate_join_srcs on the joiner; in-window report 57, w1z202, w1z207): a listener keeps the
 * sources it is registered on at +0x10, a {begin, end, cap} of 0x50-byte entries {source, a 0x232-type bit set} sorted by source
 * (0x998720 finds or adds one); 0x999120(table entry, source, listener) appends the listener to a source's type vector and sets
 * the type's bit in the listener's entry for the source, 0x999ef0(source, listener) erases the listener from every type vector of
 * the source and its entry (0x998a80), 0x998b20 unregisters a listener everywhere by that list. (AddSource 0xc9ce30's +0x38 list
 * is empty for every script context, w1z207.) A script that listens on its ship's sector keeps the sector of its start: the
 * session's pilot 0x9327c (move.gate) hears sector 0x26's signal at 3061 after its jump into 0x37, the joiner's load registered
 * it on 0x37 (its signal at 3074). The saver records each keyed script context's keyed sources by portable ID; the joiner moves a
 * context whose sources differ by exactly one (the session's S, its own S'): registers it on S for every type of its S' entry
 * (0x999ae0 with create, 0x999120, as the engine's registrations do: the source's refusal slot first, the flush 0x99abe0) and
 * drops it from S' (0x999ef0), before LSTN orders the vectors. A context two share a key with is left (dup), one that differs
 * otherwise too (unmapped). Status srcs=on:contexts:added (types registered):removed (sources dropped):equal:missing:dup:unmapped
 * srcs_cap=. */
#define JOIN_SRCS_BOOK 0x10u
#define JOIN_SRCS_ENTRY 0x50u
#define JOIN_SRCS_TYPES 0x232u
#define JOIN_SRCS_MAX 256u
#define JOIN_SRCS_GET 0x999ae0u       /* a source's vector of a type (source, type, create) */
#define JOIN_SRCS_APPEND 0x999120u    /* (vector, source, listener) */
#define JOIN_SRCS_DROP 0x999ef0u      /* (source, listener) */
#define JOIN_SRCS_FLUSH 0x99abe0u     /* the pending listener changes, before each registration */
#define JOIN_SRCS_REFUSE 0x11b0u      /* a source's vtable slot: it takes no listener (0x11b8 with the type when it is the listener) */
enum {JOIN_SRCS_UNMAPPED=1u};
typedef struct {uint64_t owner,script;uint32_t kind,dup;unsigned char *ctx;} join_srcs_ctx;
static int join_srcs_on;
static uint64_t join_srcs_contexts,join_srcs_added,join_srcs_removed,join_srcs_equal,join_srcs_missing,join_srcs_dup,join_srcs_unmapped;
static uint32_t join_srcs_capn;
static int join_srcs_register_native(unsigned char *source,uint32_t type,unsigned char *listener) {
    void **vt=*(void ***)source;
    const unsigned char refuse=listener==source?((unsigned char (*)(void *,uint32_t))vt[(JOIN_SRCS_REFUSE+8u)/8u])(source,type):
                                                ((unsigned char (*)(void *))vt[JOIN_SRCS_REFUSE/8u])(source);
    if(refuse) return 0;
    ((void (*)(void))(void *)(image+JOIN_SRCS_FLUSH))();
    void *vector=((void *(*)(void *,uint32_t,unsigned char))(void *)(image+JOIN_SRCS_GET))(source,type,1);
    if(!vector) return 0;
    ((void (*)(void *,void *,void *))(void *)(image+JOIN_SRCS_APPEND))(vector,source,listener);
    return 1;
}
static void join_srcs_drop_native(unsigned char *source,unsigned char *listener) {((void (*)(void *,void *))(void *)(image+JOIN_SRCS_DROP))(source,listener);}
static int (*join_srcs_register)(unsigned char *,uint32_t,unsigned char *)=join_srcs_register_native;   /* tests replace them */
static void (*join_srcs_drop)(unsigned char *,unsigned char *)=join_srcs_drop_native;
static int join_srcs_key_cmp(const join_srcs_ctx *x,const join_srcs_ctx *y) {
    if(x->kind!=y->kind) return x->kind<y->kind?-1:1;
    if(x->owner!=y->owner) return x->owner<y->owner?-1:1;
    return x->script<y->script?-1:x->script>y->script;
}
static int join_srcs_ctx_cmp(const void *a,const void *b) {return join_srcs_key_cmp(a,b);}
/* The listener kinds SRCS records (bit per join_lstn_key kind): the script contexts. The load registers components alike; their
 * sources follow the component tree (and a component per object would make the section large). Tests add components. */
static unsigned join_srcs_kinds=(1u<<1)|(1u<<2);
/* A set of listener pointers (open addressing; a source's vectors repeat a listener once per event type: millions of entries,
 * w1z207 overran JOIN_MAX_RECORDS collecting them whole). */
typedef struct {uintptr_t *slot;uint32_t cap,n;} join_srcs_set;
static uint32_t join_srcs_hash(uintptr_t p,uint32_t cap) {return (uint32_t)(((uint64_t)(p>>4)*0x9e3779b97f4a7c15ull)>>32)&(cap-1u);}
static int join_srcs_set_add(join_srcs_set *s,uintptr_t p) {   /* 1 new, 0 present, -1 memory */
    if((s->n+1u)*2u>s->cap) {
        const uint32_t nc=s->cap?s->cap*2u:65536u;uintptr_t *ns;
        if(nc>(1u<<26) || !(ns=calloc(nc,sizeof(*ns)))) return -1;
        for(uint32_t i=0;i<s->cap;i++) if(s->slot[i]) {uint32_t h=join_srcs_hash(s->slot[i],nc);while(ns[h]) h=(h+1u)&(nc-1u);ns[h]=s->slot[i];}
        free(s->slot);s->slot=ns;s->cap=nc;
    }
    uint32_t h=join_srcs_hash(p,s->cap);
    while(s->slot[h]) {if(s->slot[h]==p) return 0;h=(h+1u)&(s->cap-1u);}
    s->slot[h]=p;++s->n;return 1;
}
/* A source's portable ID (0: not a keyed component). */
static uint64_t join_srcs_portable(unsigned char *x) {
    uint64_t runtime=0;unsigned klass;
    return x && join_read(x+8,&runtime,8) && runtime && join_object(runtime)==x?join_key(runtime,&klass):0;
}
/* A listener's keyed sources from its +0x10 list (at most JOIN_SRCS_MAX; their entries' addresses in at): their count, -1
 * unreadable. */
static int join_srcs_of(unsigned char *ctx,unsigned char **out,uintptr_t *at) {
    uintptr_t book=0,v[2]={0,0};int n=0;
    if(!join_read(ctx+JOIN_SRCS_BOOK,&book,8)) return -1;
    if(!book) return 0;
    if(!join_read((void *)book,v,16) || v[1]<v[0] || (v[1]-v[0])%JOIN_SRCS_ENTRY || (v[1]-v[0])/JOIN_SRCS_ENTRY>4096u) return -1;
    for(uintptr_t p=v[0];p<v[1];p+=JOIN_SRCS_ENTRY) {
        unsigned char *x=NULL;
        if(!join_read((void *)p,&x,8)) return -1;
        if(x==ctx || !join_srcs_portable(x)) continue;   /* itself, or not a keyed component */
        if(n>=(int)JOIN_SRCS_MAX) return -1;
        if(at) at[n]=p;
        out[n++]=x;
    }
    return n;
}
/* Every keyed listener on a keyed source of the table, once, sorted by key; dup: another listener has its key. */
static join_srcs_ctx *join_srcs_listeners(const join_tables *t,uint32_t *count) {
    const join_engine e=join_lstn_engine();join_srcs_set seen={0};join_srcs_ctx *c=NULL;uint32_t n=0,cap=0;*count=0;
    for(uint32_t i=0;i<t->nids;i++) {
        unsigned char *o=t->ids[i].portable?join_object(t->ids[i].runtime):NULL;uintptr_t table=0,v[2]={0,0};
        if(!o || !join_read(o+JOIN_LSTN_TABLE,&table,8) || !table || !join_read((void *)table,v,16) || v[1]<v[0] ||
           (v[1]-v[0])%JOIN_LSTN_ENTRY || (v[1]-v[0])/JOIN_LSTN_ENTRY>4096u) continue;
        for(uintptr_t p=v[0];p<v[1];p+=JOIN_LSTN_ENTRY) {
            uintptr_t lv[2]={0,0};
            if(!join_read((void *)(p+8),lv,16) || lv[1]<lv[0] || (lv[1]-lv[0])&7u || (lv[1]-lv[0])/8u>65536u) continue;
            for(uintptr_t q=lv[0];q<lv[1];q+=8u) {
                uintptr_t l=0;int fresh;
                if(!join_read((void *)q,&l,8) || !l) continue;
                if((fresh=join_srcs_set_add(&seen,l))<0) {free(seen.slot);free(c);return NULL;}
                if(!fresh) continue;
                join_srcs_ctx x;memset(&x,0,sizeof(x));x.ctx=(unsigned char *)l;
                x.kind=join_lstn_key(&e,x.ctx,&x.owner,&x.script);
                if(x.kind>3u || !(join_srcs_kinds>>x.kind&1u) || !x.owner) continue;
                if(!JOIN_PUSH(c,n,cap,x)) {free(seen.slot);free(c);return NULL;}
            }
        }
    }
    free(seen.slot);
    if(n>1) qsort(c,n,sizeof(*c),join_srcs_ctx_cmp);
    for(uint32_t i=1;i<n;i++) if(!join_srcs_key_cmp(&c[i-1],&c[i])) c[i-1].dup=c[i].dup=1u;
    *count=n;return c;
}
static void join_capture_srcs(join_tables *t) {
    uint32_t n=0;join_srcs_ctx *c=join_srcs_listeners(t,&n);static unsigned char *src[JOIN_SRCS_MAX];
    join_srcs_capn=0;
    for(uint32_t i=0;c && i<n;i++) {
        const int ns=c[i].dup?-1:join_srcs_of(c[i].ctx,src,NULL);
        for(int k=0;k<ns;k++) {
            join_srcs_rec r;memset(&r,0,sizeof(r));r.kind=c[i].kind;r.owner=c[i].owner;r.script=c[i].script;r.index=(uint32_t)k;
            r.source=join_srcs_portable(src[k]);
            if(!JOIN_PUSH(t->srcs,t->nsrcs,join_srcs_capn,r)) {free(c);return;}
        }
    }
    free(c);
}
static int join_srcs_find(unsigned char *const *v,int n,const unsigned char *x) {for(int k=0;k<n;k++) if(v[k]==x) return k;return -1;}
static void join_admit_srcs(const join_tables *s,const join_tables *mine) {
    join_srcs_contexts=join_srcs_added=join_srcs_removed=join_srcs_equal=join_srcs_missing=join_srcs_dup=join_srcs_unmapped=0;
    if(!join_srcs_on || !s->nsrcs) return;
    uint32_t n=0;join_srcs_ctx *c=join_srcs_listeners(mine,&n);
    if(!c) return;
    static unsigned char *want[JOIN_SRCS_MAX],*have[JOIN_SRCS_MAX];static uintptr_t have_at[JOIN_SRCS_MAX];
    for(uint32_t i=0;i<s->nsrcs;) {
        const join_srcs_rec *g=&s->srcs[i];uint32_t j=i+1u;
        while(j<s->nsrcs && s->srcs[j].kind==g->kind && s->srcs[j].owner==g->owner && s->srcs[j].script==g->script) ++j;
        ++join_srcs_contexts;
        join_srcs_ctx key;memset(&key,0,sizeof(key));key.kind=g->kind;key.owner=g->owner;key.script=g->script;
        const join_srcs_ctx *f=n?bsearch(&key,c,n,sizeof(*c),join_srcs_ctx_cmp):NULL;
        if(!f) {++join_srcs_missing;i=j;continue;}
        if(f->dup) {++join_srcs_dup;i=j;continue;}
        int nw=0,lost=0;
        for(uint32_t k=i;k<j && nw<(int)JOIN_SRCS_MAX;k++) {
            unsigned char *x=s->srcs[k].flags&JOIN_SRCS_UNMAPPED?NULL:join_object(join_runtime_of(mine,s->srcs[k].source));
            if(x) want[nw++]=x; else lost=1;
        }
        const int nh=join_srcs_of(f->ctx,have,have_at);
        int only_s=-1,only_j=-1,ns=0,nj=0;
        for(int k=0;k<nw;k++) if(join_srcs_find(have,nh<0?0:nh,want[k])<0) {only_s=k;++ns;}
        for(int k=0;k<nh;k++) if(join_srcs_find(want,nw,have[k])<0) {only_j=k;++nj;}
        if(nh<0 || lost) {++join_srcs_unmapped;i=j;continue;}
        if(!ns && !nj) {++join_srcs_equal;i=j;continue;}
        if(ns!=1 || nj!=1) {++join_srcs_unmapped;i=j;continue;}
        uint64_t bits[9];   /* the types of its entry for its own source, before the registrations move the list */
        if(!join_read((void *)(have_at[only_j]+8u),bits,sizeof(bits))) {++join_srcs_unmapped;i=j;continue;}
        unsigned char *to=want[only_s],*from=have[only_j];
        for(uint32_t type=0;type<JOIN_SRCS_TYPES;type++) if(bits[type>>6]>>(type&63u)&1u) join_srcs_added+=(uint64_t)join_srcs_register(to,type,f->ctx);
        join_srcs_drop(from,f->ctx);++join_srcs_removed;
        i=j;
    }
    free(c);
}
/* FOBJ (v13, section 82; opt-in native_gate_join_fobj on the joiner). A region's fields (U::Region +0x288, a vector of field
 * pointers) keep the objects they spawned in a std::list at field +0x90 (sentinel node; node +0 next, +8 prev, +0x10 the object;
 * the size +0x98; the field's constructor 0xedac40 makes the sentinel). The salvage search FindRegionObjectWithYieldInSectorAction
 * (0xbf5f80: 0x7680a0 per region, its fields through 0xedd0c0) finds a recyclable only through those lists, else spawns one
 * (0x7884a0, the field's slot 21). A load leaves every list empty: j1-399, region 0x6b3's fields 36, 42, 43 and 49 hold one object
 * each on the session (f42 and f49 zone 0x21fd6's two recyclables), none on the joiner; at 3093 the tug 0x9c691's search spawns a
 * recyclable on the joiner alone, its object code shifts every later one, and the 3318 job quotas (drawn from codes) part the
 * world by 3360. The saver records every list in order (region portable, field index, place, the object's portable; flags 1:
 * unmapped, object 0); the joiner appends the session's objects to a list it holds empty (nodes from SMem::Allocate 0x14b7140, as
 * std::list's), leaves an equal one, and counts any other (conflict). Status fobj=on:groups:written:equal:missing:conflict:inserted. */
#define JOIN_FOBJ_FIELDS 0x288u
#define JOIN_FOBJ_LIST 0x90u
#define JOIN_FOBJ_SIZE 0x98u
#define JOIN_FOBJ_ALLOC 0x14b7140u
#define JOIN_FOBJ_MAX 4096u
enum {JOIN_FOBJ_UNMAPPED=1u};
static int join_fobj_on;
static uint64_t join_fobj_groups,join_fobj_written,join_fobj_equal,join_fobj_missing,join_fobj_conflict,join_fobj_inserted;
static int join_fobj_cmp(const void *a,const void *b) {
    const join_fobj_rec *x=a,*y=b;
    if(x->region!=y->region) return x->region<y->region?-1:1;
    if(x->field!=y->field) return x->field<y->field?-1:1;
    return x->index<y->index?-1:x->index>y->index;
}
static void *join_fobj_alloc_native(size_t n) {
    return ((void *(*)(size_t,uintptr_t,uintptr_t,uintptr_t,unsigned))(void *)(image+JOIN_FOBJ_ALLOC))(n,0,0,0,0x10);
}
static void *(*join_fobj_alloc)(size_t)=join_fobj_alloc_native;   /* tests replace it */
static uintptr_t join_fobj_region_vt;   /* tests: a synthetic region vtable (0: the image's U::Region) */
static int join_fobj_fields(unsigned char *region,uintptr_t f[2]) {
    uintptr_t vt=0;f[0]=f[1]=0;
    const uintptr_t want=join_fobj_region_vt?join_fobj_region_vt:(uintptr_t)image+JOIN_RGN_VTABLE;
    return region && join_read(region,&vt,8) && vt==want && join_read(region+JOIN_FOBJ_FIELDS,f,16) && f[1]>=f[0] &&
           !((f[1]-f[0])&7u) && (f[1]-f[0])/8u<=4096u;
}
/* A field's list: the objects in order (at most JOIN_FOBJ_MAX), their count; -1 unreadable. */
static int join_fobj_list(unsigned char *field,unsigned char **objs,uint32_t *n,unsigned char **sentinel) {
    unsigned char *sent=NULL,*node=NULL;*n=0;
    if(!field || !join_read(field+JOIN_FOBJ_LIST,&sent,8) || !sent || !join_read(sent,&node,8)) return -1;
    if(sentinel) *sentinel=sent;
    for(;node && node!=sent;) {
        if(*n>=JOIN_FOBJ_MAX) return -1;
        unsigned char *obj=NULL;if(!join_read(node+0x10,&obj,8)) return -1;
        objs[(*n)++]=obj;
        if(!join_read(node,&node,8)) return -1;
    }
    return node?0:-1;
}
static unsigned char *join_fobj_objs[JOIN_FOBJ_MAX];
static void join_capture_fobj(join_tables *t) {
    uint32_t cap=0;
    for(uint32_t i=0;i<t->nids;i++) {
        unsigned char *o=t->ids[i].portable?join_object(t->ids[i].runtime):NULL;uintptr_t f[2];
        if(!o || !join_fobj_fields(o,f)) continue;
        for(uintptr_t q=f[0];q<f[1];q+=8) {
            unsigned char *field=NULL;uint32_t n=0;
            if(!join_read((void *)q,&field,8) || !field || join_fobj_list(field,join_fobj_objs,&n,NULL)<0) continue;
            for(uint32_t k=0;k<n;k++) {
                join_fobj_rec r;memset(&r,0,sizeof(r));uint64_t rt=0;unsigned klass;
                r.region=t->ids[i].portable;r.field=(uint32_t)((q-f[0])/8u);r.index=k;
                if(join_fobj_objs[k] && join_read(join_fobj_objs[k]+8,&rt,8) && rt) r.object=join_key(rt,&klass);
                if(!r.object) r.flags=JOIN_FOBJ_UNMAPPED;
                if(!JOIN_PUSH(t->fobj,t->nfobj,cap,r)) return;
            }
        }
    }
    if(t->nfobj>1) qsort(t->fobj,t->nfobj,sizeof(*t->fobj),join_fobj_cmp);
}
/* The joiner's region: its own table's runtime ID, else the session's (a region its identity table leaves out; the load keeps
 * saved IDs). */
static unsigned char *join_fobj_region(const join_tables *s,const join_tables *mine,uint64_t portable) {
    uint64_t r=join_runtime_of(mine,portable);
    for(uint32_t k=0;!r && k<s->nids;k++) if(s->ids[k].portable==portable) r=s->ids[k].runtime;
    return r?join_object(r):NULL;
}
static unsigned char *join_fobj_want[JOIN_FOBJ_MAX];
static void join_admit_fobj(const join_tables *s,const join_tables *mine) {
    join_fobj_groups=join_fobj_written=join_fobj_equal=join_fobj_missing=join_fobj_conflict=join_fobj_inserted=0;
    if(!join_fobj_on) return;
    for(uint32_t i=0;i<s->nfobj;) {
        uint32_t j=i+1u;
        while(j<s->nfobj && s->fobj[j].region==s->fobj[i].region && s->fobj[j].field==s->fobj[i].field) ++j;
        ++join_fobj_groups;
        const join_fobj_rec *g=&s->fobj[i];const uint32_t ng=j-i;i=j;
        unsigned char *region=join_fobj_region(s,mine,g->region),*field=NULL,*sent=NULL;uintptr_t f[2];uint32_t n=0,m=0;
        if(!join_fobj_fields(region,f) || f[0]+8u*(uintptr_t)g->field>=f[1] || !join_read((void *)(f[0]+8u*(uintptr_t)g->field),&field,8) ||
           join_fobj_list(field,join_fobj_objs,&n,&sent)<0) {++join_fobj_missing;continue;}
        int unmapped=0;
        for(uint32_t k=0;k<ng && m<JOIN_FOBJ_MAX;k++) {
            unsigned char *o=g[k].flags&JOIN_FOBJ_UNMAPPED?NULL:join_object(join_runtime_of(mine,g[k].object));
            if(!o) {unmapped=1;break;}
            join_fobj_want[m++]=o;
        }
        if(unmapped) {++join_fobj_missing;continue;}
        if(n) {
            if(n==m && !memcmp(join_fobj_objs,join_fobj_want,(size_t)n*sizeof(*join_fobj_want))) ++join_fobj_equal;
            else ++join_fobj_conflict;
            continue;
        }
        uint64_t size=0;
        if(!join_read(field+JOIN_FOBJ_SIZE,&size,8) || size) {++join_fobj_conflict;continue;}
        for(uint32_t k=0;k<m;k++) {   /* push_back: node {next, prev, value} before the sentinel */
            unsigned char **node=join_fobj_alloc(0x18u),*last=NULL;
            if(!node || !join_read(sent+8,&last,8) || !last) break;
            node[0]=sent;node[1]=last;node[2]=join_fobj_want[k];
            *(unsigned char **)last=(unsigned char *)node;*(unsigned char **)(sent+8)=(unsigned char *)node;
            ++*(uint64_t *)(field+JOIN_FOBJ_SIZE);++join_fobj_inserted;
        }
        ++join_fobj_written;
    }
}
static int join_bnds_on;
static uint64_t join_bnds_written,join_bnds_equal,join_bnds_missing;
#define JOIN_BNDS_GETTER 0x7eb460u   /* the station's vfunc 0x14b0: lea rax,[rcx+0xc90] */
#define JOIN_BNDS_BYTES 0x24u       /* centre, extents, radius */
#define JOIN_BNDS_TAIL 0xd50u       /* six floats */
static const unsigned join_bnds_at[JOIN_BNDS_STRUCTS]={0xc90u,0xcc0u,0xcf0u,0xc60u,0xd20u};
static int join_bnds_type(unsigned char *o) {
    uintptr_t vt=0,fn=0;
    return o && join_read(o,&vt,8) && vt && join_read((void *)(vt+0x14b0u),&fn,8) && fn==(uintptr_t)image+JOIN_BNDS_GETTER;
}
static int join_bnds_cmp(const void *a,const void *b) {
    uint64_t x=((const join_bnds_rec *)a)->portable,y=((const join_bnds_rec *)b)->portable;return x<y?-1:x>y;
}
static int join_bnds_read(unsigned char *o,uint32_t word[JOIN_BNDS_WORDS]) {
    for(unsigned k=0;k<JOIN_BNDS_STRUCTS;k++) if(!join_read(o+join_bnds_at[k],word+k*9u,JOIN_BNDS_BYTES)) return 0;
    return join_read(o+JOIN_BNDS_TAIL,word+JOIN_BNDS_STRUCTS*9u,4u*JOIN_BNDS_TAIL_WORDS);
}
static void join_capture_bnds(join_tables *t) {
    uint32_t cap=0;
    for(uint32_t i=0;i<t->nids;i++) {
        if(!t->ids[i].portable) continue;
        unsigned char *o=join_object(t->ids[i].runtime);join_bnds_rec r;
        if(!join_bnds_type(o)) continue;
        memset(&r,0,sizeof(r));r.portable=t->ids[i].portable;
        if(!join_bnds_read(o,r.word)) continue;
        if(!JOIN_PUSH(t->bnds,t->nbnds,cap,r)) return;
    }
    if(t->nbnds>1) qsort(t->bnds,t->nbnds,sizeof(*t->bnds),join_bnds_cmp);
    {uint32_t k=0;for(uint32_t i=0;i<t->nbnds;i++) {if(k && t->bnds[k-1].portable==t->bnds[i].portable) continue;t->bnds[k++]=t->bnds[i];}t->nbnds=k;}
}
static void join_admit_bnds(const join_tables *s,const join_tables *mine) {
    join_bnds_written=join_bnds_equal=join_bnds_missing=0;
    if(!join_bnds_on) return;
    for(uint32_t i=0;i<s->nbnds;i++) {
        const join_bnds_rec *r=&s->bnds[i];unsigned char *o=join_object(join_runtime_of(mine,r->portable));
        if(!join_bnds_type(o)) {++join_bnds_missing;continue;}
        int changed=0;
        for(unsigned k=0;k<JOIN_BNDS_STRUCTS;k++) changed|=join_poke((uintptr_t)(o+join_bnds_at[k]),r->word+k*9u,JOIN_BNDS_BYTES);
        changed|=join_poke((uintptr_t)(o+JOIN_BNDS_TAIL),r->word+JOIN_BNDS_STRUCTS*9u,4u*JOIN_BNDS_TAIL_WORDS);
        if(changed) ++join_bnds_written; else ++join_bnds_equal;
    }
}
/* SHGR (v13, section 71; the admission opt-in, native_gate_join_shgr): the shield recharge groups containers keep for their docked
 * ships [F: w1z50/w1z51, the window's census at 1080: 19 docked ships' children differ in health only; WORLD_HEALTH: the ships have
 * no shield group (+0x290 null) until 1059, then one whose changed time +0x70 is 7.083 on the session and 38.733 = T on the
 * joiner, every other field equal; w1z53 (refwatch on +0x290): 0x51f240 (from 0x3f5e5d) gives the ship its parent container's
 * group through 0x4d6270(parent isa 0x23, ship), which first searches the parent's list +0x5d8..+0x5e0 for a group keyed by
 * the ship's runtime ID (+0) and a second key (+8) and returns it]. Those groups were built by each client's own load and
 * stamped then (the session's boot load, the joiner's load at T); a new group would read 0. The capture writes, for every
 * container (isa 0x23) with such a list, each group's value +0x68 and changed time +0x70 by the container's and the ship's
 * portable IDs and the second key; with the opt-in the admission writes the session's where they differ. Status
 * shgr=on:written:equal:missing. */
static int join_shgr_on;
static uint64_t join_shgr_written,join_shgr_equal,join_shgr_missing;
#define JOIN_SHGR_LIST 0x5d8u              /* a container's shield recharge groups: {begin, end} of group pointers */
#define JOIN_SHGR_SOURCE_VT 0x2b19700u     /* U::ShieldRechargeSource, embedded at group +0x90 */
static int join_shgr_list(unsigned char *o,uintptr_t v[2]) {
    v[0]=v[1]=0;
    return o && join_read(o+JOIN_SHGR_LIST,v,16) && v[1]>v[0] && !((v[1]-v[0])%8u) && (v[1]-v[0])/8u<=4096u;
}
static unsigned char *join_shgr_group(uintptr_t at,uint64_t *ship_runtime,uint32_t *key) {
    unsigned char *g=NULL;uintptr_t src=0;
    if(!join_read((void *)at,&g,8) || !g || !join_read(g,ship_runtime,8) || !join_read(g+8,key,4) ||
       !join_read(g+0x90,&src,8) || src!=(uintptr_t)image+JOIN_SHGR_SOURCE_VT) return NULL;
    return g;
}
static int join_shgr_cmp(const void *a,const void *b) {
    const join_shgr_rec *x=a,*y=b;
    if(x->container!=y->container) return x->container<y->container?-1:1;
    if(x->ship!=y->ship) return x->ship<y->ship?-1:1;
    return x->key<y->key?-1:x->key>y->key;
}
static void join_capture_shgr(join_tables *t) {
    uint32_t cap=0;
    for(uint32_t i=0;i<t->nids;i++) {
        if(!t->ids[i].portable) continue;
        unsigned char *o=join_object(t->ids[i].runtime);uintptr_t v[2];
        if(!join_shgr_list(o,v) || join_watch_call(o,0x11e8u,0x23,1)!=1) continue;
        for(uintptr_t q=v[0];q<v[1];q+=8u) {
            uint64_t ship_runtime=0;uint32_t key=0;unsigned klass=0;join_shgr_rec r;
            unsigned char *g=join_shgr_group(q,&ship_runtime,&key);
            if(!g) continue;
            memset(&r,0,sizeof(r));r.container=t->ids[i].portable;r.ship=join_key(ship_runtime,&klass);r.key=key;
            if(!r.ship || !join_read(g+0x68,&r.value,8) || !join_read(g+0x70,&r.changed,8)) continue;
            if(!JOIN_PUSH(t->shgr,t->nshgr,cap,r)) return;
        }
    }
    if(t->nshgr>1) qsort(t->shgr,t->nshgr,sizeof(*t->shgr),join_shgr_cmp);
    {uint32_t k=0;for(uint32_t i=0;i<t->nshgr;i++) {if(k && !join_shgr_cmp(&t->shgr[k-1],&t->shgr[i])) continue;t->shgr[k++]=t->shgr[i];}t->nshgr=k;}
}
static void join_admit_shgr(const join_tables *s,const join_tables *mine) {
    join_shgr_written=join_shgr_equal=join_shgr_missing=0;
    if(!join_shgr_on) return;
    for(uint32_t i=0;i<s->nshgr;i++) {
        const join_shgr_rec *r=&s->shgr[i];unsigned char *o=join_object(join_runtime_of(mine,r->container)),*hit=NULL;
        const uint64_t want=join_runtime_of(mine,r->ship);uintptr_t v[2];
        if(want && join_shgr_list(o,v))
            for(uintptr_t q=v[0];q<v[1] && !hit;q+=8u) {
                uint64_t ship_runtime=0;uint32_t key=0;unsigned char *g=join_shgr_group(q,&ship_runtime,&key);
                if(g && ship_runtime==want && key==r->key) hit=g;
            }
        if(!hit) {++join_shgr_missing;continue;}
        int changed=join_poke((uintptr_t)(hit+0x68),&r->value,8);
        changed|=join_poke((uintptr_t)(hit+0x70),&r->changed,8);
        if(changed) ++join_shgr_written; else ++join_shgr_equal;
    }
}
/* SHPW (v13, section 65; the admission opt-in, native_gate_join_shpw): ship object words a load leaves at defaults [F: w1z28, the
 * hot join's watch on four traders and miners (TradeRoutine, linear flight): the ship objects differ from the load on in +0x3b0..
 * +0x3bf (four floats: the session's 540/40.64/50.80, the joiner's 540 and an equal pair 63.50/63.50), +0xa10/+0xa14 (an int 1 and a
 * float: 19.30 against -1), +0xa18 (the stamp 0x41a910 writes: 7.083 against T), +0xa78 (flags) and +0xc38 (the first field of the
 * sub-object at +0xc30: 0 against 1500); w1z30 with ECOS: the trader 83e03's order still a step late on the joiner from 657]. The
 * capture writes the words of every ship (the five ship classes share the U::Ship layout to +0xc30 and beyond); with the opt-in the
 * admission writes the session's where they differ. Status shpw=on:written:equal:missing. */
static const unsigned short join_shpw_offsets[JOIN_SHPW_WORDS]={0x3b0,0x3b8,0xa10,0xa18,0xa78,0xc38};
static int join_shpw_on;
/* native_gate_join_shpw_words (experiment): which of the six words the admission writes, bit k = join_shpw_offsets[k] (default
 * all). j1-239: fixture 176ad5's +0x3b0 is 626.8 on the session and 208.9 on the joiner from T+1 (the float vfunc 0x1a90 returns;
 * 0x6dad90 reads it in the braking loop of 0x6de670; the engine parameter refresh 0x6dc610 rewrites +0x3ac..+0x3b7), and it
 * parts by one ulp at 1011; mask 1 carries that word alone, without SHPW's stamp +0xa18 (w1z31: carrying the stamp was wrong). */
static unsigned join_shpw_mask=0x3fu;
static uint64_t join_shpw_written,join_shpw_equal,join_shpw_missing;
static const uint32_t join_shpw_vtables[]={0x2b7d738u,0x2b7feb8u,0x2b82440u,0x2b9bff8u,0x2b99a88u};   /* ShipXS, S, M, L, XL */
static int join_shpw_type(unsigned char *o) {
    uintptr_t vt=0;
    if(!o || !join_read(o,&vt,8) || vt<(uintptr_t)image) return 0;
    for(unsigned i=0;i<sizeof(join_shpw_vtables)/sizeof(*join_shpw_vtables);i++) if(vt==(uintptr_t)image+join_shpw_vtables[i]) return 1;
    return 0;
}
static int join_shpw_cmp(const void *a,const void *b) {
    uint64_t x=((const join_shpw_rec *)a)->portable,y=((const join_shpw_rec *)b)->portable;return x<y?-1:x>y;
}
static void join_capture_shpw(join_tables *t) {
    uint32_t cap=0;
    for(uint32_t i=0;i<t->nids;i++) {
        if(!t->ids[i].portable) continue;
        unsigned char *o=join_object(t->ids[i].runtime);join_shpw_rec r;int ok=1;
        if(!join_shpw_type(o)) continue;
        memset(&r,0,sizeof(r));r.portable=t->ids[i].portable;
        for(unsigned k=0;ok && k<JOIN_SHPW_WORDS;k++) ok=join_read(o+join_shpw_offsets[k],&r.word[k],8);
        if(!ok) continue;
        if(!JOIN_PUSH(t->shpw,t->nshpw,cap,r)) return;
    }
    if(t->nshpw>1) qsort(t->shpw,t->nshpw,sizeof(*t->shpw),join_shpw_cmp);
    {uint32_t k=0;for(uint32_t i=0;i<t->nshpw;i++) {if(k && t->shpw[k-1].portable==t->shpw[i].portable) continue;t->shpw[k++]=t->shpw[i];}t->nshpw=k;}
}
static void join_admit_shpw(const join_tables *s,const join_tables *mine) {
    join_shpw_written=join_shpw_equal=join_shpw_missing=0;
    if(!join_shpw_on) return;
    for(uint32_t i=0;i<s->nshpw;i++) {
        const join_shpw_rec *r=&s->shpw[i];unsigned char *o=join_object(join_runtime_of(mine,r->portable));int changed=0;
        if(!join_shpw_type(o)) {++join_shpw_missing;continue;}
        for(unsigned k=0;k<JOIN_SHPW_WORDS;k++) if(join_shpw_mask>>k&1u) changed|=join_poke((uintptr_t)(o+join_shpw_offsets[k]),&r->word[k],8);
        if(changed) ++join_shpw_written; else ++join_shpw_equal;
    }
}
static void join_admit_rcnt(const join_tables *s,const join_tables *mine) {
    join_rcnt_lowered=join_rcnt_equal=join_rcnt_kept=join_rcnt_missing=0;
    if(!join_rcnt_on) return;
    for(uint32_t i=0;i<s->nrcnt;i++) {
        const join_rcnt_rec *r=&s->rcnt[i];unsigned char *o=NULL;
        for(uint32_t k=0;k<mine->nids && !o;k++) if(mine->ids[k].portable==r->portable) o=join_object(mine->ids[k].runtime);
        uint32_t own=0;
        if(!join_rcnt_type(o) || !join_read(o+0x40,&own,4)) {++join_rcnt_missing;continue;}
        if(own==r->count) {++join_rcnt_equal;continue;}
        if(own<r->count || own-r->count>4u) {++join_rcnt_kept;continue;}
        InterlockedExchangeAdd((volatile LONG *)(o+0x40),-(LONG)(own-r->count));
        ++join_rcnt_lowered;
    }
}
static int join_viewp_cmp(const void *a,const void *b) {
    uint64_t x=((const join_viewp_rec *)a)->portable,y=((const join_viewp_rec *)b)->portable;return x<y?-1:x>y;
}
static void join_capture_viewp(join_tables *t) {
    const uint32_t cur=join_view_index();uint32_t cap=0;
    for(uint32_t i=0;i<t->nids;i++) {
        unsigned char *object=join_object(t->ids[i].runtime),*parent=NULL,*p0=NULL,*p1=NULL;uintptr_t pvt=0;join_viewp_rec r;
        if(!object || !t->ids[i].portable || !join_read(object+JOIN_VIEW_SLOTS+(size_t)cur*0x80u,&parent,8) || !parent ||
           !join_read(parent,&pvt,8) || pvt!=(uintptr_t)image+JOIN_ZONE_VTABLE ||
           !join_read(object+JOIN_VIEW_SLOTS+(size_t)cur*0x80u+JOIN_VIEW_POSE,&p0,8) ||
           !join_read(object+JOIN_VIEW_SLOTS+(size_t)(cur^1u)*0x80u+JOIN_VIEW_POSE,&p1,8) || !p0 || !p1) continue;
        unsigned char b1[0x40];
        if(!join_read(p0,r.pose,0x40) || !join_read(p1,b1,0x40) || memcmp(r.pose,b1,0x40)) continue;   /* settled only */
        r.portable=t->ids[i].portable;
        if(!JOIN_PUSH(t->viewp,t->nviewp,cap,r)) return;
    }
    if(t->nviewp>1) qsort(t->viewp,t->nviewp,sizeof(*t->viewp),join_viewp_cmp);
}
static void join_capture_viewf(join_tables *t) {
    const uint32_t cur=join_view_index();uint32_t cap=0;
    for(uint32_t i=0;i<t->nids;i++) {
        unsigned char *object=join_object(t->ids[i].runtime);join_viewf_rec r;memset(&r,0,sizeof(r));
        if(!object || !t->ids[i].portable || !join_read(object+JOIN_VIEW_SLOTS+(size_t)cur*0x80u+JOIN_VIEW_FLAGS,&r.flags[0],4) ||
           !join_read(object+JOIN_VIEW_SLOTS+(size_t)(cur^1u)*0x80u+JOIN_VIEW_FLAGS,&r.flags[1],4) ||
           !join_read(object+JOIN_VIEW_SLOTS+(size_t)cur*0x80u+JOIN_VIEW_ATTENTION,&r.attention[0],4) ||
           !join_read(object+JOIN_VIEW_SLOTS+(size_t)(cur^1u)*0x80u+JOIN_VIEW_ATTENTION,&r.attention[1],4)) continue;
        r.portable=t->ids[i].portable;
        {   /* in flight: the two views differ (flags, attention or the published pose) */
            unsigned char *p0=NULL,*p1=NULL,b0[0x40],b1[0x40];int differ=r.flags[0]!=r.flags[1] || r.attention[0]!=r.attention[1];
            if(!differ && join_read(object+JOIN_VIEW_SLOTS+(size_t)cur*0x80u+JOIN_VIEW_POSE,&p0,8) &&
               join_read(object+JOIN_VIEW_SLOTS+(size_t)(cur^1u)*0x80u+JOIN_VIEW_POSE,&p1,8))
                differ=!p0!=!p1 || (p0 && p1 && (!join_read(p0,b0,0x40) || !join_read(p1,b1,0x40) || memcmp(b0,b1,0x40)));
            if(differ) r.attention[1]|=JOIN_VIEWF_INFLIGHT;
        }
        if(!JOIN_PUSH(t->viewf,t->nviewf,cap,r)) return;
    }
    if(t->nviewf>1) qsort(t->viewf,t->nviewf,sizeof(*t->viewf),join_viewf_cmp);
}
/* By (component, view) each once, view 0/1, known flags, UNREAD without children; VIEWKC exactly each record's children in order, a
 * child 0 exactly under UNMAPPED. */
static int join_viewk_shape(const join_viewk_rec *k,uint32_t nk,const join_viewkc_rec *c,uint32_t nc) {
    uint32_t j=0;
    for(uint32_t i=0;i<nk;i++) {
        const join_viewk_rec *r=&k[i];int zero=0;
        if(!r->portable || r->rel>1u || (r->flags&~3u) || r->count>JOIN_VIEWK_CHILDREN || ((r->flags&JOIN_VIEWK_UNREAD) && r->count) ||
           (i && (k[i-1].portable>r->portable || (k[i-1].portable==r->portable && k[i-1].rel>=r->rel)))) return 0;
        for(uint32_t x=0;x<r->count;x++,j++) {
            if(j>=nc || c[j].portable!=r->portable || c[j].rel!=r->rel || c[j].index!=x) return 0;
            if(!c[j].child) zero=1;
        }
        if(zero!=!!(r->flags&JOIN_VIEWK_UNMAPPED)) return 0;
    }
    return j==nc;
}
static void join_capture_evq_census(double now) {
    join_evq_dump('s',now);
    join_evq_census(now,&join_cap.evq_sources,&join_cap.evq_events,&join_cap.evq_due,&join_cap.evq_fault,join_cap.evq_top,sizeof(join_cap.evq_top));
}
static void join_capture_grav(join_tables *t) {
    unsigned char *w=join_grav_worker();uint32_t scap=0;
    join_grav_cap_units=join_grav_ccap=0;join_cap.grav_unmapped=join_cap.grav_fault=join_cap.grav_passive=0;
    free(join_gravp_objects);join_gravp_objects=NULL;join_gravp_nobjects=join_gravp_ocap=0;
    if(!w) {++join_cap.grav_fault;return;}
    for(unsigned b=0;b<JOIN_GRAV_BUCKETS;b++) {
        int ok;join_grav_lock(w+0x88+b*0x28,1);ok=join_grav_tree(t,w+0x2890+b*0x40);join_grav_lock(w+0x88+b*0x28,0);
        if(!ok) {++join_cap.grav_fault;break;}
    }
    for(unsigned kind=0;kind<2;kind++) {   /* the active sources: components */
        uintptr_t begin=0;uint64_t n=0;
        if(!join_grav_vector(w+0x48+kind*0x18,&begin,&n)) {++join_cap.grav_fault;continue;}
        for(uint64_t i=0;i<n;i++) {
            unsigned char *component=NULL;uint64_t runtime=0;unsigned klass;join_gravs_rec r;memset(&r,0,sizeof(r));
            r.kind=kind;r.index=(uint32_t)i;
            if(join_read((void *)(begin+(uintptr_t)i*8u),&component,8) && component && join_read(component+8,&runtime,8) && runtime)
                r.portable=join_key(runtime,&klass);
            if(!r.portable) r.flags=JOIN_GRAVS_UNMAPPED;
            if(!JOIN_PUSH(t->gravs,t->ngravs,scap,r)) {++join_cap.grav_fault;return;}
        }
    }
    {   /* the field sectors' factor-pass deadlines */
        uintptr_t v[2];uint32_t index=0;
        if(!join_read(w+0x68b8,v,sizeof(v)) || v[1]<v[0] || ((v[1]-v[0])&31u) || (v[1]-v[0])/32u>65536u) {++join_cap.grav_fault;goto sorted;}
        for(uintptr_t p=v[0];p<v[1];p+=32,++index) {
            unsigned char *sector=NULL,field=0;uintptr_t vtable=0;uint64_t runtime=0;unsigned klass;join_gravs_rec r;memset(&r,0,sizeof(r));
            if(!join_read((void *)p,&sector,8) || !sector || !join_read(sector,&vtable,8) || vtable!=(uintptr_t)image+JOIN_GRAV_SECTOR_VT) continue;
            if(!join_read(sector+0x442,&field,1) || !field) continue;
            if(!join_read(sector+0x448,&r.value,8) || !join_read(sector+8,&runtime,8)) {++join_cap.grav_fault;continue;}
            r.kind=JOIN_GRAVS_SECTOR;r.index=index;r.portable=runtime?join_key(runtime,&klass):0;
            if(!r.portable) {r.flags|=JOIN_GRAVS_UNMAPPED;r.value=0;}
            if(join_gdl_seen(runtime)) r.flags|=JOIN_GRAVS_SEEN;
            if(!JOIN_PUSH(t->gravs,t->ngravs,scap,r)) {++join_cap.grav_fault;goto sorted;}
        }
    }
sorted:
    if(ai_sched_armed && ai_sched_admitted && ai_sched_count) {   /* the cursor the next frame expects (ai_sched_frame) */
        join_gravs_rec r;memset(&r,0,sizeof(r));
        r.kind=JOIN_GRAVS_CURSOR;r.index=(uint32_t)ai_sched_count;
        r.value=ai_sched_pending?ai_sched_advance(ai_sched_cursor,ai_sched_count):ai_sched_cursor;
        if(!JOIN_PUSH(t->gravs,t->ngravs,scap,r)) ++join_cap.grav_fault;
    }
    if(t->ngrav) qsort(t->grav,t->ngrav,sizeof(*t->grav),join_grav_cmp);
    if(t->ngravc) qsort(t->gravc,t->ngravc,sizeof(*t->gravc),join_gravc_cmp);
    {   /* one unit per owner: two units with one owner are both dropped with their contacts (counted) */
        uint32_t k=0,kc=0,i=0;
        while(i<t->ngrav) {
            uint32_t j=i+1;while(j<t->ngrav && t->grav[j].portable==t->grav[i].portable) ++j;
            if(j-i==1) t->grav[k++]=t->grav[i];else join_cap.grav_fault+=j-i;
            i=j;
        }
        if(k!=t->ngrav) {
            for(uint32_t c=0;c<t->ngravc;c++) {   /* keep the contacts of kept units (both arrays sorted by portable) */
                const join_grav_rec key={.portable=t->gravc[c].portable};
                if(bsearch(&key,t->grav,k,sizeof(*t->grav),join_grav_cmp)) t->gravc[kc++]=t->gravc[c];
            }
            t->ngrav=k;t->ngravc=kc;
        }
    }
    join_capture_gravn(t,w);   /* version 12 */
    join_capture_gravp(t);
    join_capture_gravq(t,w);
}
/* GRAV by portable ID, kinds and flags known, a passive or uncarried unit without contacts; GRAVC exactly each unit's counted
 * contacts in (list, index) order, a contact 0 only with its flag; GRAVS by kind then index (sources counted from 0), known
 * flags, SEEN and a value only on sectors, portable 0 only with its flag. */
static int join_grav_shape(const join_grav_rec *g,uint32_t ng,const join_gravc_rec *c,uint32_t nc,const join_gravs_rec *s,uint32_t ns) {
    uint32_t at=0;
    for(uint32_t i=0;i<ng;i++) {
        const join_grav_rec *r=&g[i];const int active=r->kind==JOIN_GRAV_ACTIVE_VT;
        if(!r->portable || (i && r->portable<=g[i-1].portable) || (!active && r->kind!=JOIN_GRAV_PASSIVE_VT) || (r->flags&~3u) ||
           !!(r->flags&JOIN_GRAV_ACTIVE)!=active) return 0;
        for(unsigned l=0;l<JOIN_GRAV_LISTS;l++) {
            if((!active || (r->flags&JOIN_GRAV_UNCARRIED)) && r->count[l]) return 0;
            for(uint32_t k=0;k<r->count[l];k++,at++) {
                if(at>=nc) return 0;
                const join_gravc_rec *x=&c[at];
                if(x->portable!=r->portable || x->list!=l || x->index!=k || (x->flags&~1u) || x->reserved || !x->contact!=!!(x->flags&JOIN_GRAVC_UNMAPPED)) return 0;
            }
        }
    }
    if(at!=nc) return 0;
    for(uint32_t i=0;i<ns;i++) {
        const join_gravs_rec *r=&s[i];
        if(r->kind==JOIN_GRAVS_CURSOR) {   /* once, last, portable 0, no flags, a cursor inside the list */
            if(r->portable || r->flags || r->reserved || !r->index || r->value>=r->index || i+1!=ns) return 0;
            if(i && s[i-1].kind==JOIN_GRAVS_CURSOR) return 0;
            continue;
        }
        if(r->kind>JOIN_GRAVS_SECTOR || (r->flags&~3u) || r->reserved || !r->portable!=!!(r->flags&JOIN_GRAVS_UNMAPPED) ||
           ((r->flags&JOIN_GRAVS_SEEN) && r->kind!=JOIN_GRAVS_SECTOR) || ((r->kind!=JOIN_GRAVS_SECTOR || !r->portable) && r->value)) return 0;
        if(i && (r->kind<s[i-1].kind || (r->kind==s[i-1].kind && r->index<=s[i-1].index))) return 0;
        if(r->kind!=JOIN_GRAVS_SECTOR && r->index!=(i && s[i-1].kind==r->kind?s[i-1].index+1u:0u)) return 0;
    }
    return 1;
}
/* ---- version 12: SPLM, SPLME, SPLMSEG (the spline flight control models; report 1.16) ----
 * Layout [F: the constructor 0xeb4f40, the route push 0xebcad0, the deque grow 0x7be0e0 and clear 0xf1c960, the step 0xeb7230]: the
 * deques {proxy, map, map size (a power of two), offset, count} at +0x70 and +0x98, one 0x50-byte entry per block (entry +0 its
 * CompositeSpline<5> or 0, +0x10..+0x3f a transform, +0x40 a word, +0x44/+0x45 two flags); the embedded CompositeSpline<5> at +0x158 and
 * +0x1a8 (each 0x50 bytes as the heap ones: vtable, scale +8, segments +0x10, pairs +0x28, length/min/max +0x40); the request deque +0x428
 * (count +0x448). Not carried (join_splm_skip): pointers (vtable, the owner +0x10, deques, the avoidance cache's contact vector +0xf0 with
 * its time +0x108: j1-38's joiner crashed in the cache refresh 0xeaf660 on the session's pointers; the joiner keeps its own empty cache and
 * time -1, so it refreshes at its first step as the steering models do, the splines' vtables and vectors), the words
 * the constructor leaves unset (+8, +0x18, +0x210, +0x268) and +0x418 (a caller's argument kept for a later SetTarget). */
#define JOIN_SPLM_DEQUE0 0x70u
#define JOIN_SPLM_EMB0 0x158u
#define JOIN_SPLM_REQ_COUNT 0x448u
#define JOIN_SPLM_CS 0x50u
static const struct {unsigned at,len;} join_splm_skip[]={{0,0x20},{0x70,0x50},{0xf0,0x20},{0x158,8},{0x168,0x30},{0x1a8,8},{0x1b8,0x30},
                                                         {0x210,8},{0x268,8},{0x418,8},{0x428,0x28}};
#define JOIN_SPLM_SKIPS (sizeof(join_splm_skip)/sizeof(*join_splm_skip))
static unsigned char join_splm_segs[JOIN_FLIGHT_SEGMENTS*JOIN_FLIGHT_SEGMENT];
static uint64_t join_splm_pairs[JOIN_FLIGHT_PAIRS];
/* A deque at at: count, and for each entry in order its block (out[]). 0 implausible; the joiner's empty one reads n 0. */
static int join_splm_deque(const unsigned char *at,uintptr_t *out,uint32_t cap,uint32_t *n) {
    uint64_t d[5];*n=0;
    if(!join_read(at,d,sizeof(d))) return 0;
    if(!d[4]) return 1;
    if(!d[1] || !d[2] || (d[2]&(d[2]-1)) || d[4]>=d[2] || d[4]>cap) return 0;
    for(uint64_t i=0;i<d[4];i++) {
        uintptr_t block=0;
        if(!join_read((void *)(d[1]+((d[3]+i)&(d[2]-1))*8u),&block,8) || !block) return 0;
        out[i]=block;
    }
    *n=(uint32_t)d[4];return 1;
}
/* A CompositeSpline<5> at cs (heap or embedded): its 0x50 bytes with vtable and vectors 0 into head, its segments (BezierSpline vtables
 * 0) and pairs into the scratch buffers. 0: not that class or more than the record holds. */
static int join_splm_composite(const unsigned char *cs,unsigned char head[JOIN_SPLM_CS],uint64_t *nseg,uint64_t *npair) {
    uintptr_t vtable=0,seg[3],pair[3];uint64_t room=0;
    if(!join_read(cs,head,JOIN_SPLM_CS)) return 0;
    memcpy(&vtable,head,8);
    if(vtable!=(uintptr_t)image+JOIN_FLIGHT_SPLINE_VT || !join_flight_vector(cs+0x10,JOIN_FLIGHT_SEGMENT,seg,nseg,&room) ||
       !join_flight_vector(cs+0x28,8u,pair,npair,&room) || *nseg>JOIN_FLIGHT_SEGMENTS || *npair>JOIN_FLIGHT_PAIRS) return 0;
    if((*nseg && !join_read((void *)seg[0],join_splm_segs,(size_t)*nseg*JOIN_FLIGHT_SEGMENT)) ||
       (*npair && !join_read((void *)pair[0],join_splm_pairs,(size_t)*npair*8u))) return 0;
    for(uint64_t k=0;k<*nseg;k++) {
        uintptr_t bezier;memcpy(&bezier,join_splm_segs+k*JOIN_FLIGHT_SEGMENT+JOIN_FLIGHT_BEZIER_AT,8);
        if(bezier!=(uintptr_t)image+JOIN_FLIGHT_BEZIER_VT) return 0;
        memset(join_splm_segs+k*JOIN_FLIGHT_SEGMENT+JOIN_FLIGHT_BEZIER_AT,0,8);
    }
    memset(head,0,8);memset(head+0x10,0,0x30);
    return 1;
}
static uint32_t join_splm_mcap,join_splm_ecap,join_splm_scap;
/* The scratch segments and pairs of one spline into SPLMSEG (part list*2 + kind, index element << 16 | k). */
static int join_splm_push(join_tables *t,uint64_t portable,unsigned list,uint32_t element,uint64_t nseg,uint64_t npair) {
    for(uint64_t k=0;k<nseg;k++) {
        join_spline_rec g;memset(&g,0,sizeof(g));g.portable=portable;g.part=list*2u;g.index=element<<16|(uint32_t)k;
        memcpy(g.data,join_splm_segs+k*JOIN_FLIGHT_SEGMENT,JOIN_FLIGHT_SEGMENT);
        if(!JOIN_PUSH(t->splmseg,t->nsplmseg,join_splm_scap,g)) return 0;
    }
    for(uint64_t k=0;k*JOIN_FLIGHT_PAIRS_PER<npair;k++) {
        join_spline_rec g;memset(&g,0,sizeof(g));g.portable=portable;g.part=list*2u+1u;g.index=element<<16|(uint32_t)k;
        uint64_t used=npair-k*JOIN_FLIGHT_PAIRS_PER;if(used>JOIN_FLIGHT_PAIRS_PER) used=JOIN_FLIGHT_PAIRS_PER;
        memcpy(g.data,join_splm_pairs+k*JOIN_FLIGHT_PAIRS_PER,(size_t)used*8u);
        if(!JOIN_PUSH(t->splmseg,t->nsplmseg,join_splm_scap,g)) return 0;
    }
    return 1;
}
/* A spline model into SPLM (and its entries and segments). What the record cannot hold is flagged UNCARRIED (no entries, no segments),
 * never refused; 0 only without memory (join_fault). */
static int join_capture_splm(join_tables *t,const unsigned char *model,uint64_t portable) {
    join_splm_rec r;memset(&r,0,sizeof(r));r.portable=portable;
    const uint32_t e0=t->nsplme,s0=t->nsplmseg;uint64_t requests=0;int ok=join_read(model,r.model,JOIN_SPLM_BYTES);
    for(unsigned k=0;k<JOIN_SPLM_SKIPS;k++) memset(r.model+join_splm_skip[k].at,0,join_splm_skip[k].len);
    if(ok && join_read(model+JOIN_SPLM_REQ_COUNT,&requests,8) && requests) {r.flags|=JOIN_SPLM_REQUESTS;r.requests=(uint32_t)(requests>0xffffffffu?0xffffffffu:requests);}
    for(unsigned l=0;l<2 && ok;l++) {   /* the embedded splines */
        unsigned char head[JOIN_SPLM_CS];uint64_t nseg=0,npair=0;
        ok=join_splm_composite(model+JOIN_SPLM_EMB0+l*0x50u,head,&nseg,&npair);
        if(ok) {
            r.segments[l]=(uint32_t)nseg;r.pairs[l]=(uint32_t)npair;
            if(!join_splm_push(t,portable,l,0,nseg,npair)) return join_note("memory");
        }
    }
    for(unsigned l=0;l<2 && ok;l++) {   /* the deques */
        uintptr_t blocks[JOIN_SPLM_ENTRIES];uint32_t n=0;
        ok=join_splm_deque(model+JOIN_SPLM_DEQUE0+l*0x28u,blocks,JOIN_SPLM_ENTRIES,&n);
        for(uint32_t k=0;k<n && ok;k++) {
            join_splme_rec e;memset(&e,0,sizeof(e));uintptr_t cs=0;uint64_t nseg=0,npair=0;
            e.portable=portable;e.list=l;e.index=k;
            ok=join_read((void *)blocks[k],e.entry,0x50);
            if(!ok) break;
            memcpy(&cs,e.entry,8);memset(e.entry,0,16);   /* the spline pointer and the word the push leaves unset (+8: savers differ) */
            if(cs) {
                ok=join_splm_composite((const unsigned char *)cs,e.spline,&nseg,&npair);
                if(!ok) break;
                e.flags=JOIN_SPLME_SPLINE;e.segments=(uint32_t)nseg;e.pairs=(uint32_t)npair;
                if(!join_splm_push(t,portable,2u+l,k,nseg,npair)) return join_note("memory");
            }
            if(!JOIN_PUSH(t->splme,t->nsplme,join_splm_ecap,e)) return join_note("memory");
        }
        if(ok) r.entries[l]=n;
    }
    if(!ok) {   /* a route this record cannot hold: the model's own bytes only */
        r.flags|=JOIN_SPLM_UNCARRIED;memset(r.entries,0,sizeof(r.entries));memset(r.segments,0,sizeof(r.segments));memset(r.pairs,0,sizeof(r.pairs));
        t->nsplme=e0;t->nsplmseg=s0;
    }
    return JOIN_PUSH(t->splm,t->nsplm,join_splm_mcap,r)?1:join_note("memory");
}
static int join_splme_cmp(const void *a,const void *b) {
    const join_splme_rec *x=a,*y=b;
    if(x->portable!=y->portable) return x->portable<y->portable?-1:1;
    if(x->list!=y->list) return x->list<y->list?-1:1;
    return x->index<y->index?-1:x->index>y->index;
}
static int join_splm_cmp(const void *a,const void *b) {
    uint64_t x=((const join_splm_rec *)a)->portable,y=((const join_splm_rec *)b)->portable;return x<y?-1:x>y;
}
static int join_splmseg_cmp(const void *a,const void *b) {
    const join_spline_rec *x=a,*y=b;
    if(x->portable!=y->portable) return x->portable<y->portable?-1:1;
    if(x->part!=y->part) return x->part<y->part?-1:1;
    return x->index<y->index?-1:x->index>y->index;
}
/* SPLM by portable ID, known flags, nothing under an uncarried record; SPLME exactly entries[0] then entries[1] entries per model;
 * SPLMSEG exactly each spline's segments (part list*2) and ceil(pairs/20) pair records (part list*2+1), in (part, index) order, the
 * BezierSpline vtable 0 in each segment, a pair record's unused tail 0. */
static int join_splm_shape(const join_splm_rec *m,uint32_t nm,const join_splme_rec *e,uint32_t ne,const join_spline_rec *g,uint32_t ng) {
    uint32_t ei=0,gi=0;
    for(uint32_t i=0;i<nm;i++) {
        const join_splm_rec *r=&m[i];
        if(!r->portable || (i && r->portable<=m[i-1].portable) || (r->flags&~3u) || r->reserved[0] || r->reserved[1] ||
           !(r->flags&JOIN_SPLM_REQUESTS)!=!r->requests || r->entries[0]>JOIN_SPLM_ENTRIES || r->entries[1]>JOIN_SPLM_ENTRIES) return 0;
        if((r->flags&JOIN_SPLM_UNCARRIED) && (r->entries[0] || r->entries[1] || r->segments[0] || r->segments[1] || r->pairs[0] || r->pairs[1])) return 0;
        uint32_t seg[4][JOIN_SPLM_ENTRIES+1],pair[4][JOIN_SPLM_ENTRIES+1],count[4];
        count[0]=count[1]=1;seg[0][0]=r->segments[0];pair[0][0]=r->pairs[0];seg[1][0]=r->segments[1];pair[1][0]=r->pairs[1];
        for(unsigned l=0;l<2;l++) {
            count[2+l]=r->entries[l];
            for(uint32_t k=0;k<r->entries[l];k++,ei++) {
                if(ei>=ne) return 0;
                const join_splme_rec *x=&e[ei];
                if(x->portable!=r->portable || x->list!=l || x->index!=k || (x->flags&~1u) || x->reserved ||
                   (!(x->flags&JOIN_SPLME_SPLINE) && (x->segments || x->pairs)) || x->segments>JOIN_FLIGHT_SEGMENTS || x->pairs>JOIN_FLIGHT_PAIRS) return 0;
                for(unsigned b=0;b<8;b++) if(x->entry[b]) return 0;
                seg[2+l][k]=x->segments;pair[2+l][k]=x->pairs;
            }
        }
        for(unsigned l=0;l<4;l++) for(unsigned kind=0;kind<2;kind++) for(uint32_t el=0;el<count[l];el++) {
            const uint32_t want=kind?(pair[l][el]+JOIN_FLIGHT_PAIRS_PER-1)/JOIN_FLIGHT_PAIRS_PER:seg[l][el];
            for(uint32_t k=0;k<want;k++,gi++) {
                if(gi>=ng) return 0;
                const join_spline_rec *x=&g[gi];
                if(x->portable!=r->portable || x->part!=l*2u+kind || x->index!=(el<<16|k)) return 0;
                if(!kind) {for(unsigned b=JOIN_FLIGHT_BEZIER_AT;b<JOIN_FLIGHT_BEZIER_AT+8;b++) if(x->data[b]) return 0;}
                else {
                    uint32_t used=pair[l][el]-k*JOIN_FLIGHT_PAIRS_PER;if(used>JOIN_FLIGHT_PAIRS_PER) used=JOIN_FLIGHT_PAIRS_PER;
                    for(unsigned b=used*8u;b<JOIN_FLIGHT_SEGMENT;b++) if(x->data[b]) return 0;
                }
            }
        }
    }
    return ei==ne && gi==ng;
}
static int join_capture_flight(join_tables *t,const unsigned char *controller,uint64_t runtime,uint64_t portable,uint32_t *fcap,uint32_t *scap,
                               uint32_t *acap,uint32_t *rcap,uint32_t *pcap,uint32_t *stcap,uint32_t *stpcap,uint32_t *socap) {
    unsigned char *agent=NULL,*model=NULL,*owner=NULL;uintptr_t vtable=0,spline=0,seg[3],pair[3];uint64_t word=0,nseg=0,npair=0,room=0;
    if(!join_read(controller+0x40,&agent,8) || !agent || !join_read(agent+JOIN_AGENT_MODEL,&model,8))
        return join_note("flight,id=%llx,check=agent",(unsigned long long)portable);
    if(!model) {
        /* A pilot slot ship a player flies (X4's PlayerController or a slot's copy: the Agent has no flight model). The lockstep
         * session's w1z5/w1z6: an in-window joiner's slot ships load their Agents without a solver, with default limits and the
         * load's timestamps, and none of them was carried (this returned here). Its AGENT, PARAM, ROUTE and SOLVER records, paired
         * with a FLIGHT record of kind JOIN_FLIGHT_NOMODEL (flags OTHER; the FLIGHT admission skips it). A nav ship without a
         * model carries nothing, as before. */
        uintptr_t cvt=0;
        if(!join_read(controller,&cvt,8) || pslot_canonical_vtable(cvt)!=(uintptr_t)image+PSLOT_VTABLE) return 1;
        join_agent_rec a;memset(&a,0,sizeof(a));a.portable=portable;
        if(!join_agent_read(agent,&a)) return join_note("agent,id=%llx",(unsigned long long)portable);
        if(!JOIN_PUSH(t->agent,t->nagent,*acap,a) || !join_agtt_push(t,agent,portable)) return join_note("memory");
        if(!join_capture_param(t,agent,portable,pcap)) return 0;
        if(!join_capture_route(t,agent,portable,0,0,rcap)) return 0;
        if(!join_capture_solver(t,agent,runtime,portable,JOIN_FLIGHT_NOMODEL,socap)) return 0;
        join_flight_rec r;memset(&r,0,sizeof(r));r.portable=portable;r.kind=JOIN_FLIGHT_NOMODEL;r.flags=JOIN_FLIGHT_OTHER;
        return JOIN_PUSH(t->flight,t->nflight,*fcap,r)?1:join_note("memory");
    }
    if(!join_read(model,&vtable,8) || vtable<(uintptr_t)image || vtable>=(uintptr_t)image+JOIN_FLIGHT_IMAGE)
        return join_note("flight,id=%llx,check=vtable",(unsigned long long)portable);
    {   /* version 6: the Agent's step inputs, one record per FLIGHT record */
        join_agent_rec a;memset(&a,0,sizeof(a));a.portable=portable;
        if(!join_agent_read(agent,&a)) return join_note("agent,id=%llx",(unsigned long long)portable);
        if(!JOIN_PUSH(t->agent,t->nagent,*acap,a) || !join_agtt_push(t,agent,portable)) return join_note("memory");   /* AGTT with it */
        if(!join_capture_param(t,agent,portable,pcap)) return 0;   /* version 8: its flight parameter slots, one record per AGENT record */
    }
    {   /* version 7: its waypoint entries, the IDs around them and the linear model's SetState word */
        const int linear=vtable==(uintptr_t)image+JOIN_FLIGHT_LINEAR;uint64_t w=0;
        if(linear && !join_read(model+JOIN_FLIGHT_WORD_AT,&w,8)) return join_note("flight,id=%llx,check=word",(unsigned long long)portable);
        if(!join_capture_route(t,agent,portable,linear,w,rcap)) return 0;
    }
    if(!join_capture_solver(t,agent,runtime,portable,(uint32_t)(vtable-(uintptr_t)image),socap)) return 0;   /* version 10 */
    join_flight_rec r;memset(&r,0,sizeof(r));r.portable=portable;r.kind=(uint32_t)(vtable-(uintptr_t)image);
    if(r.kind!=JOIN_FLIGHT_LINEAR) {
        r.flags=JOIN_FLIGHT_OTHER;
        if(r.kind==JOIN_STEER_VTABLE && !join_capture_steer(t,agent,model,portable,stcap,stpcap)) return 0;   /* version 9 */
        if(r.kind==JOIN_STEER_VTABLE && !join_capture_omgs(t,agent,portable,&join_omgs_cap)) return 0;        /* version 13 */
        if(r.kind==JOIN_SPLINE_VTABLE && !join_capture_splm(t,model,portable)) return 0;                      /* version 12 */
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
#include "lockstep_join_body.h"   /* BODY (v13): the destructible objects' physics bodies */
/* BUPD (v13, section 51): every class-0x6f object's last build update. j1-124..j1-132: at 648 the Split faction's Process_Ships
 * found more free build capacity on a joiner (81 draws against 77, the job ships placed the other way, 418 of the 900 census
 * objects after it). Its free-processor test (U::BuildFilterByIsBusy, 0x38c100 with its flag) brings the processor's class-0x6f
 * ancestor to the frame's time: 0x38dd40 -> 0x4280e0(object, time), its last update at +0xa58, the builds advanced over the
 * interval since. The save keeps no such time; a joiner's load sets it to the load time on every object (j1-132: 8,971 of
 * 10,252 other, the session's mostly 7.083, the joiner's 28.183), so the first update after T advances over another interval.
 * j1-131: the same update run at one tick on both sides gives 648 alike and 162 at 900. The saver writes each object's
 * +0xa58, the joiner's admission sets it. Status bupd=written:equal:missing:shape bupd_cap=. */
#define JOIN_BUPD_CLASS 0x6fu
#define JOIN_BUPD_FIELD 0xa58u
static uint64_t join_bupd_written,join_bupd_equal,join_bupd_missing,join_bupd_shape;
static int join_bupd_cmp(const void *a,const void *b) {
    const uint64_t x=((const join_bupd_rec*)a)->portable,y=((const join_bupd_rec*)b)->portable;return x<y?-1:x>y;
}
static int join_bupd_class(uintptr_t vtable) {uint32_t words[8];return world_class_words(vtable,words) && world_class_has(words,JOIN_BUPD_CLASS);}
/* LDC (v13, section 52): every drifting object's U::LinearDecelerationController (+0x200, vtable 0x2c3f5f0; layout in
 * lockstep_wreck_watch.h). Its pose is a closed form of the time it is asked for and so are its velocities (GetVelocity
 * 0xe288e0: v * (1 - tau / D)), from the start transform +0x20, start time +0x60, velocities +0x80/+0x90, duration +0xa0 and
 * flag +0xa8; Scene::Step's pre-sync sets the body from them every step. The save keeps them rounded (start time and pose
 * at three decimals, velocities at six digits; 1.4), so a joiner's wreck drifts on another path: j1-122..j1-138 the fixture
 * wreck (actor 8) parted by a few ulp at 634 with BODY written at T (its body set again from the loaded controller). The
 * saver writes each LDC's fields, the joiner's admission writes them into its loaded LDC (before BODY). Pinned by the
 * GetVelocity layout; on a mismatch nothing is written. Status ldc=written:equal:missing:shape ldc_cap=. */
#define JOIN_LDC_VTABLE 0x2c3f5f0u
#define JOIN_LDC_LAYOUT 0xe288e0u
#define JOIN_LDC_LAYOUT_SIZE 0x11eu
#define JOIN_LDC_LAYOUT_SHA "7c12a422d468ce634fde3a13a78827794a39608d4cea6b0b7daf48174f4af4e5"
static const struct {unsigned at,len;} join_ldc_fields[5]={{0x20u,0x40u},{0x60u,8u},{0x80u,0x20u},{0xa0u,8u},{0xa8u,1u}};
static uint64_t join_ldc_written,join_ldc_equal,join_ldc_missing,join_ldc_shape;
static unsigned char *join_ldc_controller(unsigned char *object) {
    unsigned char *c=NULL;uintptr_t vt=0;
    return object && join_read(object+JOIN_CONTROLLER,&c,8) && c && join_read(c,&vt,8) && vt==(uintptr_t)image+JOIN_LDC_VTABLE?c:NULL;
}
static int join_ldc_read(const unsigned char *c,unsigned char out[JOIN_LDC_STATE]) {
    unsigned o=0;
    for(unsigned k=0;k<5u;k++) {if(!join_read(c+join_ldc_fields[k].at,out+o,join_ldc_fields[k].len)) return 0;o+=join_ldc_fields[k].len;}
    return o==JOIN_LDC_STATE;
}
static int join_ldc_cmp(const void *a,const void *b) {
    const uint64_t x=((const join_ldc_rec*)a)->portable,y=((const join_ldc_rec*)b)->portable;return x<y?-1:x>y;
}
static void join_capture_ldc(join_tables *t) {
    uint32_t cap=0;
    for(uint32_t i=0;i<t->nids;i++) {
        unsigned char *c=t->ids[i].portable?join_ldc_controller(join_object(t->ids[i].runtime)):NULL;join_ldc_rec r;memset(&r,0,sizeof(r));
        if(!c || !join_ldc_read(c,r.state)) continue;
        r.portable=t->ids[i].portable;
        if(!JOIN_PUSH(t->ldc,t->nldc,cap,r)) return;
    }
    if(t->nldc>1) qsort(t->ldc,t->nldc,sizeof(*t->ldc),join_ldc_cmp);
}
static void join_capture_nmesh(join_tables *t);   /* NMESH (lockstep_join.h, at join_admit_rid) */
static uint64_t join_nav_runtime(const join_tables *mine,uint64_t portable,int *ok);   /* NAV's (below) */
static void join_admit_pbnk(const join_tables *s,const join_tables *mine) {
    unsigned read=0,write=0;
    if(!s->npbnk) return;
    if(!join_param_selectors(&read,&write)) {join_pbnk_missing+=s->npbnk;return;}
    for(uint32_t i=0;i<s->npbnk;i++) {
        const join_pbnk_rec *r=&s->pbnk[i];
        unsigned char *object=join_object(join_runtime_of(mine,r->portable)),*agent=object?join_nav_agent(object):NULL;
        if(!agent) {++join_pbnk_missing;continue;}
        int changed=0;
        for(unsigned role=0;role<2;role++) {
            const unsigned bp=role?write:read;const join_nav_bank *b=&r->bank[role];
            const uintptr_t bank=(uintptr_t)agent+0xad0u+(uintptr_t)bp*WORLD_NAV_BANK_BYTES,pend=(uintptr_t)agent+0xed0u+16u*bp;
            for(unsigned v=0;v<6;v++) changed|=join_poke(bank+16u*v,b->xyz+3*v,12);
            for(unsigned k=0;k<2;k++) {
                int ok=!(r->flags>>(3*role+k)&1u);uint64_t want=ok?join_nav_runtime(mine,b->ref[k],&ok):0;
                if(!ok) {++join_pbnk_unmapped;continue;}
                changed|=join_poke(bank+0xb0+8u*k,&want,8);
            }
            changed|=join_poke(bank+0xcc,&b->mode,4);changed|=join_poke(bank+0x150,&b->eta,8);
            changed|=join_poke(bank+0x159,&b->arrived,1);changed|=join_poke(bank+0x15a,&b->notified,1);changed|=join_poke(bank+0x15b,&b->layer,1);
            {
                int ok=!(r->flags>>(3*role+2)&1u);uint64_t want=ok && b->pending_state==WORLD_NAV_SAVED?join_nav_runtime(mine,b->pending_ref,&ok):0;
                if(!ok) ++join_pbnk_unmapped;
                else {changed|=join_poke(pend,&want,8);changed|=join_poke(pend+8,&b->pending_count,8);}
            }
            for(unsigned v=0;v<JOIN_NAVT_VECTORS;v++) changed|=join_poke(bank+JOIN_NAVT_FROM+16u*v,r->tail[role]+3*v,12);
        }
        if(changed) ++join_pbnk_written; else ++join_pbnk_equal;
    }
}
static void join_admit_agcb(const join_tables *s,const join_tables *mine) {
    for(uint32_t i=0;i<s->nagcb;i++) {
        unsigned char *object=join_object(join_runtime_of(mine,s->agcb[i].portable)),*agent=object?join_nav_agent(object):NULL;
        if(!agent) {++join_agcb_missing;continue;}
        unsigned char *controller=NULL;uintptr_t cvt=0;
        const int nav=join_read(object+JOIN_CONTROLLER,&controller,8) && controller && join_read(controller,&cvt,8) &&
                      pslot_canonical_vtable(cvt)==(uintptr_t)image+JOIN_NAV_VTABLE;
        if(nav && !join_agcb_nav_on) continue;   /* a nav ship's block: only with native_gate_join_agcb_nav */
        if(join_poke((uintptr_t)(agent+JOIN_AGCB_AT),s->agcb[i].block,JOIN_AGCB_BYTES)) ++join_agcb_written; else ++join_agcb_equal;
    }
}
/* One record into an Agent through the joiner's read selector: 1 written, 0 equal, -1 an ID it cannot map (left). */
static int join_ares_write(unsigned char *agent,const join_ares_rec *r,const join_tables *mine,uint32_t sel) {
    unsigned char want[2][JOIN_ARES_BANK];memcpy(want,r->bank,sizeof(want));
    for(unsigned role=0;role<2;role++) {
        uint64_t runtime=0;
        if(r->flags>>role&1u) return -1;
        if(r->ref[role] && !(runtime=join_runtime_of(mine,r->ref[role]))) return -1;
        memcpy(want[role],&runtime,8);
    }
    int changed=0;
    for(unsigned role=0;role<2;role++) changed|=join_poke((uintptr_t)(agent+JOIN_ARES_AT+JOIN_ARES_BANK*(role?1u-sel:sel)),want[role],JOIN_ARES_BANK);
    return changed;
}
static void join_admit_ares(const join_tables *s,const join_tables *mine) {
    join_ares_written=join_ares_equal=join_ares_missing=join_ares_unmapped=0;
    if(!join_ares_on) return;
    const uint32_t sel=join_read_selector();if(sel>1u) {join_ares_missing=s->nares;return;}
    for(uint32_t i=0;i<s->nares;i++) {
        const join_ares_rec *r=&s->ares[i];
        unsigned char *object=join_object(join_runtime_of(mine,r->portable)),*agent=object?join_nav_agent(object):NULL;
        if(!agent) {++join_ares_missing;continue;}
        const int w=join_ares_write(agent,r,mine,sel);
        if(w<0) ++join_ares_unmapped; else if(w) ++join_ares_written; else ++join_ares_equal;
    }
}
static void join_admit_agtt(const join_tables *s,const join_tables *mine) {
    for(uint32_t i=0;i<s->nagtt;i++) {
        unsigned char *object=join_object(join_runtime_of(mine,s->agtt[i].portable)),*agent=object?join_nav_agent(object):NULL;
        if(!agent) {++join_agtt_missing;continue;}
        int changed=0;
        for(unsigned k=0;k<JOIN_AGTT_FIELDS;k++) if(join_agtt_at[k]) changed|=join_poke((uintptr_t)(agent+join_agtt_at[k]),&s->agtt[i].v[k],8);
        if(changed) ++join_agtt_written; else ++join_agtt_equal;
    }
}
static void join_capture_noct(join_tables *t);    /* NOCT (lockstep_join.h, at join_admit_rid) */
static void join_admit_noct(const join_tables *s,const join_tables *mine);
static void join_admit_ldc(const join_tables *s,const join_tables *mine) {
    if(!s->nldc) return;
    if(!hash_ok(image+JOIN_LDC_LAYOUT,JOIN_LDC_LAYOUT_SIZE,JOIN_LDC_LAYOUT_SHA)) {join_ldc_shape+=s->nldc;return;}
    for(uint32_t i=0;i<s->nldc;i++) {
        unsigned char *c=join_ldc_controller(join_object(join_runtime_of(mine,s->ldc[i].portable)));
        if(!c) {++join_ldc_missing;continue;}
        int changed=0;unsigned o=0;
        for(unsigned k=0;k<5u;k++) {changed|=join_poke((uintptr_t)(c+join_ldc_fields[k].at),s->ldc[i].state+o,join_ldc_fields[k].len);o+=join_ldc_fields[k].len;}
        if(changed) ++join_ldc_written; else ++join_ldc_equal;
    }
}
static int join_capture_world(join_tables *t,double now) {
    join_agtt_capn=0;join_ares_capn=0;join_undk_capn=0;join_agcb_capn=0;join_pbnk_capn=0;join_pcsv_capn=0;
    uint32_t ntcap=0,bupdcap=0,bodycap=0,hcap=0,pcap=0,scap=0,ncap=0,acap=0,gcap=0,fcap=0,lcap=0,agcap=0,rcap=0,pacap=0,dcap=0,stcap=0,stpcap=0,socap=0,bcap=0;join_t1_reset();
    join_splm_mcap=join_splm_ecap=join_splm_scap=0;
    join_solver_dormant=0;join_cap.solver_over=0;join_cap.solver_mapped=0;join_cap.bullet_fault=0;join_cap.bullet_blocks=0;join_cap.bullet_live=0;join_cap.bullet_unkeyed=0;
    for(uint32_t i=0;i<t->nids;i++) {
        const join_id_rec *id=&t->ids[i];unsigned char *object=join_object(id->runtime);uintptr_t vtable=0;
        if(!object) continue;
        if(!join_read(object,&vtable,8)) return join_note("object,id=%llx",(unsigned long long)id->portable);
        if(vtable==(uintptr_t)(image+JOIN_REGION_VTABLE) && !join_capture_region(t,object,id->portable,&gcap)) return 0;
        unsigned classes=world_class_flags(vtable);
        if(classes&0x80000000u) continue;
        if(join_bupd_class(vtable)) {   /* BUPD: its last build update */
            join_bupd_rec u={id->portable,0};
            if(!join_read(object+JOIN_BUPD_FIELD,&u.last,8)) return join_note("bupd,id=%llx",(unsigned long long)id->portable);
            if(!JOIN_PUSH(t->bupd,t->nbupd,bupdcap,u)) return join_note("memory");
        }
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
            if(!join_capture_body(t,object,id->portable,&bodycap)) return 0;
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
                join_nav_rec r;join_navt_rec tr;if(!join_nav_capture(object,controller,id->runtime,id->portable,&r,&tr)) return 0;
                if(!JOIN_PUSH(t->nav,t->nnav,ncap,r) || !JOIN_PUSH(t->navt,t->nnavt,ntcap,tr)) return join_note("memory");
                if(!join_capture_flight(t,controller,id->runtime,id->portable,&fcap,&lcap,&agcap,&rcap,&pacap,&stcap,&stpcap,&socap)) return 0;   /* versions 5..10 */
                if(!join_capture_agcb(t,controller,id->portable)) return 0;   /* AGCB: a nav ship's Agent block too (native_gate_join_agcb_nav) */
                continue;
            }
            /* A pilot slot ship (X4's PlayerController or a slot's copy): its Agent's sections as a nav ship's (FLIGHT, AGENT, PARAM,
             * ROUTE, SOLVER, STEER), then its pose as before. */
            if(p.kind==PSLOT_VTABLE && !join_capture_flight(t,controller,id->runtime,id->portable,&fcap,&lcap,&agcap,&rcap,&pacap,&stcap,&stpcap,&socap))
                return 0;
            if(p.kind==PSLOT_VTABLE) {   /* PBNK: its Agent's pathing state banks by role */
                unsigned char *agent=NULL;unsigned read=0,write=0;
                if(join_read(controller+0x40,&agent,8) && agent && join_param_selectors(&read,&write)) {
                    join_pbnk_rec *r=calloc(1,sizeof(*r));int ok=r!=NULL;
                    if(r) r->portable=id->portable;
                    for(unsigned role=0;ok && role<2;role++) {
                        const unsigned bp=role?write:read;unsigned char bank[WORLD_NAV_BANK_BYTES];uint64_t pend[2];
                        if(!join_read(agent+0xad0+(size_t)bp*WORLD_NAV_BANK_BYTES,bank,sizeof(bank)) || !join_read(agent+0xed0+16u*bp,pend,16)) {ok=0;break;}
                        join_nav_bank *b=&r->bank[role];uint64_t refs[2];memcpy(refs,bank+0xb0,16);
                        for(unsigned v=0;v<6;v++) memcpy(b->xyz+3*v,bank+16*v,12);
                        for(unsigned k=0;k<2;k++) {b->ref[k]=refs[k]?join_nav_key(refs[k]):0;if(refs[k] && !b->ref[k]) r->flags|=1u<<(3*role+k);}
                        memcpy(&b->eta,bank+0x150,8);memcpy(&b->mode,bank+0xcc,4);b->arrived=bank[0x159];b->notified=bank[0x15a];b->layer=bank[0x15b];
                        b->pending_ref=pend[0]?join_nav_key(pend[0]):0;b->pending_count=pend[1];
                        b->pending_state=(uint8_t)(!pend[0]?WORLD_NAV_NULL:b->pending_ref?WORLD_NAV_SAVED:WORLD_NAV_UNKNOWN);
                        if(b->pending_state==WORLD_NAV_UNKNOWN) r->flags|=1u<<(3*role+2);
                        for(unsigned v=0;v<JOIN_NAVT_VECTORS;v++) memcpy(r->tail[role]+3*v,bank+JOIN_NAVT_FROM+16*v,12);
                    }
                    if(ok && !JOIN_PUSH(t->pbnk,t->npbnk,join_pbnk_capn,*r)) {free(r);return join_note("memory");}
                    free(r);
                }
            }
            if(p.kind==PSLOT_VTABLE && !join_capture_pcsv(t,controller,id->portable)) return 0;   /* PCSV: its own motion solver */
            if(p.kind==PSLOT_VTABLE && !join_capture_agcb(t,controller,id->portable)) return 0;   /* AGCB: its Agent's command and probe block */
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
    if(!join_body_sort(t)) return 0;
    if(t->nbupd) qsort(t->bupd,t->nbupd,sizeof(*t->bupd),join_bupd_cmp);
    if(t->nnavt>1) qsort(t->navt,t->nnavt,sizeof(*t->navt),join_navt_cmp);
    for(uint32_t i=1;i<t->nnavt;i++) if(t->navt[i].portable==t->navt[i-1].portable) return join_note("navt_duplicate,id=%llx",(unsigned long long)t->navt[i].portable);
    for(uint32_t i=1;i<t->nbupd;i++) if(t->bupd[i].portable==t->bupd[i-1].portable) return join_note("bupd_duplicate,id=%llx",(unsigned long long)t->bupd[i].portable);
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
    if(t->nagcb>1) qsort(t->agcb,t->nagcb,sizeof(*t->agcb),join_agcb_cmp);
    if(t->npbnk>1) qsort(t->pbnk,t->npbnk,sizeof(*t->pbnk),join_pbnk_cmp);
    if(t->nares>1) qsort(t->ares,t->nares,sizeof(*t->ares),join_ares_cmp);
    if(t->nundk>1) qsort(t->undk,t->nundk,sizeof(*t->undk),join_undk_cmp);       /* pushed with AGTT: unique portable IDs */
    if(t->nagtt) qsort(t->agtt,t->nagtt,sizeof(*t->agtt),join_agtt_cmp);        /* pushed with AGENT: the same portable IDs (j1-150/151:
                                                                                 * unsorted, the driver's reader refused the package) */
    if(t->nroute) qsort(t->route,t->nroute,sizeof(*t->route),join_route_cmp);   /* each Agent's head and points, keys unique */
    if(t->nparam) qsort(t->param,t->nparam,sizeof(*t->param),join_param_cmp);   /* pushed with AGENT: the same portable IDs */
    if(t->ndock) qsort(t->dock,t->ndock,sizeof(*t->dock),join_dock_cmp);
    for(uint32_t i=1;i<t->ndock;i++) if(t->dock[i].portable==t->dock[i-1].portable)
        return join_note("dock_duplicate,id=%llx",(unsigned long long)t->dock[i].portable);
    if(t->nsteer) qsort(t->steer,t->nsteer,sizeof(*t->steer),join_steer_cmp);          /* pushed with FLIGHT: the same portable IDs */
    if(t->nsteerpt) qsort(t->steerpt,t->nsteerpt,sizeof(*t->steerpt),join_route_cmp);  /* (portable, index): kind and list constant */
    if(t->nsolver) qsort(t->solver,t->nsolver,sizeof(*t->solver),join_solver_cmp);
    join_capture_bullets(t,&bcap);   /* version 10 */
    join_capture_grav(t);            /* version 11 */
    join_capture_jobe(t);            /* version 12 */
    join_capture_evq_census(now);    /* census only */
    join_capture_evqt(t);            /* version 13 */
    join_capture_gravd(t);           /* version 13 */
    join_capture_gravb(t);           /* version 13 */
    join_capture_viewl(t);           /* version 13 */
    join_capture_ikv(t);             /* version 13 */
    join_capture_rcnt(t);            /* version 13 */
    join_capture_ecos(t);            /* version 13 */
    join_capture_shpw(t);            /* version 13 */
    join_capture_trdn(t);            /* version 13 */
    join_capture_npca(t);            /* version 13 */
    join_capture_code(t);            /* version 13 */
    join_capture_engr(t);            /* version 13 */
    join_capture_shgr(t);            /* version 13 */
    join_capture_bnds(t);            /* version 13 */
    join_capture_resv(t);            /* version 13: the zones' space reservations */
    join_capture_conn(t);            /* version 13: the NPCs' connected connections */
    join_capture_warp(t);            /* version 13: the pending warps' destinations */
    join_capture_lstn(t);            /* version 13: the script listeners' order per event source and type */
    join_capture_srcs(t);            /* version 13: the listeners' sources */
    join_capture_fobj(t);            /* version 13: the region fields' spawned-object lists */
    if(t->npcsv>1) qsort(t->pcsv,t->npcsv,sizeof(*t->pcsv),join_solver_cmp);   /* version 13: the PlayerControllers' solvers */
    for(uint32_t i=1;i<t->npcsv;i++) if(t->pcsv[i].portable==t->pcsv[i-1].portable) return join_note("pcsv_duplicate,id=%llx",(unsigned long long)t->pcsv[i].portable);
    join_sort_omgs(t);               /* version 13: OMGS, taken in the FLIGHT walk */
    join_capture_viewk(t);           /* version 13 */
    join_capture_viewf(t);           /* version 13 */
    join_capture_pend(t);            /* version 13 */
    join_capture_viewp(t);           /* version 13 */
    join_capture_shoot(t);           /* version 13 */
    join_capture_attk(t);            /* version 13 */
    join_capture_livef(t);           /* version 13 */
    join_capture_spec(t);            /* version 13 */
    join_capture_navc(t);            /* version 13 */
    join_capture_ldc(t);             /* version 13 */
    join_capture_nmesh(t);           /* version 13 */
    join_capture_noct(t);            /* version 13 */
    join_capture_jobs(t);            /* version 13 */
    join_capture_jobg(t);            /* version 13 */
    join_watch_session(t,t->gate.tick);   /* diagnostic (native_gate_join_watch) */
    if(t->nsplm) qsort(t->splm,t->nsplm,sizeof(*t->splm),join_splm_cmp);            /* version 12 */
    if(t->nsplme) qsort(t->splme,t->nsplme,sizeof(*t->splme),join_splme_cmp);
    if(t->nsplmseg) qsort(t->splmseg,t->nsplmseg,sizeof(*t->splmseg),join_splmseg_cmp);
    if(t->nbullet) qsort(t->bullet,t->nbullet,sizeof(*t->bullet),join_bullet_cmp);
    for(uint32_t i=0;i<t->nbullet;i++) t->bullet[i].index=i && t->bullet[i].zone==t->bullet[i-1].zone?t->bullet[i-1].index+1u:0u;
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
static unsigned join_nav_admit(const join_nav_rec *r,const join_navt_rec *tail,const join_tables *mine,int *problem,char *why_out,
                               size_t why_cap) {
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
        if(tail) {   /* NAVT: the vectors after NAV's six */
            int changed=0;for(unsigned v=0;v<JOIN_NAVT_VECTORS;v++) changed|=join_poke(bank+JOIN_NAVT_FROM+16u*v,tail->xyz[role]+3*v,12);
            if(changed) ++join_navt_written; else ++join_navt_equal;
        }
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
        const join_navt_rec *tail=NULL;
        if(s->version>=13) {   /* NAVT by portable ID (one per NAV record on the saver) */
            join_navt_rec key;key.portable=r->portable;
            tail=s->nnavt?bsearch(&key,s->navt,s->nnavt,sizeof(*s->navt),join_navt_cmp):NULL;
            if(!tail) ++join_navt_missing;
        }
        unsigned fixed=join_nav_admit(r,tail,mine,&problem,why,sizeof(why));
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
/* ATTN level fixes that involve a level of 7 or more (diagnostic, status attn_high=count:id:joiner>session,...). The direct
 * write was chosen for the levels 2, 3, 5 of the first joins, below 7, where vfunc 688/689's per-level work (bodies, per-frame)
 * is off on both sides (report 1.x); a late in-window join (w1z189..w1z191, T=3033) has zone 0x72b at 7 on the session since
 * 2489, and at T+1 only the joiner posts event_object_changed_attention for its destroyer 6f1e7 and the escorts swap their
 * Steering models for fresh Splines. This counts such fixes and keeps the first JOIN_ATTN_HIGH_IDS. */
#define JOIN_ATTN_HIGH 7u
#define JOIN_ATTN_HIGH_IDS 8u
static uint64_t join_attn_high;static unsigned join_attn_high_n;
static struct {uint64_t portable;uint32_t from,to;} join_attn_high_ids[JOIN_ATTN_HIGH_IDS];
static void join_attn_note(uint64_t portable,uint32_t from,uint32_t to) {
    if(from<JOIN_ATTN_HIGH && to<JOIN_ATTN_HIGH) return;
    ++join_attn_high;
    if(join_attn_high_n<JOIN_ATTN_HIGH_IDS) {
        join_attn_high_ids[join_attn_high_n].portable=portable;join_attn_high_ids[join_attn_high_n].from=from;
        join_attn_high_ids[join_attn_high_n].to=to;++join_attn_high_n;
    }
}
static void join_attn_high_text(char *out,size_t cap) {
    size_t at=0;
    if(!join_attn_high_n) {snprintf(out,cap,"-");return;}
    out[0]=0;
    for(unsigned i=0;i<join_attn_high_n && at+40<cap;i++)
        at+=(size_t)snprintf(out+at,cap-at,"%s%016llx:%u>%u",i?",":"",(unsigned long long)join_attn_high_ids[i].portable,
                             join_attn_high_ids[i].from,join_attn_high_ids[i].to);
}
/* ATTN set (opt-in native_gate_join_attn_set; w1z189..w1z192, in-window report 57). SetAttention 0x3d5660(object, level) returns at
 * once only when the level equals +0x1ec and vfunc 689 (0x1588: the object's per-level state, for its level) equals vfunc 688
 * (0x1580: the state the level needs); otherwise it writes +0x1ec, marks the views (0x40), runs the class's handler vfunc 0x15e8 (the
 * per-level work: bodies, children) and posts a ChangedAttentionEvent 0x2ba8160 (and a ChangedAdjacentZoneAttentionEvent
 * 0x2ac1748 when the level crosses 6). ATTN's direct write leaves the state of the load's level: w1z192 attn_high=1305, zone 0x72b
 * and its objects 6>7 (the session's zone at 7 since 2489), and at T+1 the joiner's attention timer runs the whole change, posts the
 * events (only the joiner's destroyer pilot hears event_object_changed_attention) and its escorts replace their Steering models by
 * fresh Splines (29 ships one step apart at T+1). With attn_set, a level ATTN writes where either side is 6 or more and the two
 * states then differ gets the native call at the admission (before FLIGHT, STEER and BODY write over what its handler built).
 * Its events must not reach a listener: PostEvent 0x99a3f0 dispatches an immediate event due now at once (0x99c470), and the AI
 * and MD admissions ran before the world's [w1z193: the calls alone, the escorts re-planned all the same]. So for the calls
 * SetAttention's two PostEvent sites (0x3d57d5, 0x3d584b; children's calls go through them too) are swapped to the boot event
 * hold's thunk slot, idle after the admission's start, whose join_boot_events destroys the event (posted=); and every due event
 * the calls queued otherwise (the schedule's due events before the calls against after) is dropped unless the session's EVQT holds
 * its source and class due at T (join_attn_events_drop; status attn_set=on:calls:posted:dropped:kept:due_before:due_after). */
#define JOIN_SET_ATTENTION 0x3d5660u
#define JOIN_ATTN_NEED_SLOT (0x1580u/8u)
#define JOIN_ATTN_STATE_SLOT (0x1588u/8u)
#define JOIN_ATTN_VISIBLE 6u
static int join_attn_set_on;
static volatile LONG join_attn_post_on;   /* join_boot_events: a swapped PostEvent site's call (the event destroyed) */
static volatile LONG64 join_attn_set_posted;
static uint64_t join_attn_set_calls,join_attn_set_dropped,join_attn_set_kept,join_attn_set_due[2],join_attn_set_refused;
static void join_event_destroy_native(unsigned char *ev) {((void (*)(void *,int))(*(void ***)ev)[0])(ev,1);}
static void (*join_event_destroy)(unsigned char *)=join_event_destroy_native;   /* tests replace it */
static int join_attn_post_swap_native(int on);   /* lockstep_join.h: SetAttention's PostEvent sites to the thunk and back */
static int (*join_attn_post_swap)(int)=join_attn_post_swap_native;   /* tests replace it */
static unsigned char join_attn_flag_native(unsigned char *object,uint32_t level,int need) {
    void **vt=*(void ***)object;
    return ((unsigned char (*)(void *,uint32_t,uint32_t))vt[need?JOIN_ATTN_NEED_SLOT:JOIN_ATTN_STATE_SLOT])(object,level,0);
}
static void join_set_attention_native(unsigned char *object,uint32_t level) {((void (*)(void *,uint32_t))(void *)(image+JOIN_SET_ATTENTION))(object,level);}
static unsigned char (*join_attn_flag)(unsigned char *,uint32_t,int)=join_attn_flag_native;   /* tests replace them */
static void (*join_set_attention)(unsigned char *,uint32_t)=join_set_attention_native;
static uint64_t join_attn_events_drop(int count_only,uint64_t *kept);   /* lockstep_join.h */
/* After ATTN wrote the session's level (was: the joiner's): the native change where the states differ, its posts destroyed; 1 called. */
static int join_attn_reconcile(unsigned char *object,uint32_t was,uint32_t level) {
    if(!join_attn_set_on || level==1u || (was<JOIN_ATTN_VISIBLE && level<JOIN_ATTN_VISIBLE)) return 0;   /* 1: SetAttention computes its own */
    if(join_attn_flag(object,level,0)==join_attn_flag(object,level,1)) return 0;
    if(!InterlockedCompareExchange(&join_attn_post_on,0,0)) {
        if(!join_attn_post_swap(1)) {++join_attn_set_refused;return 0;}
        InterlockedExchange(&join_attn_post_on,1);
    }
    join_set_attention(object,level);++join_attn_set_calls;return 1;
}
static int join_admit_attn(const join_tables *s,const join_tables *mine) {
    unsigned rows=0,samples=0;join_attn_high=0;join_attn_high_n=0;
    join_attn_set_calls=join_attn_set_dropped=join_attn_set_kept=join_attn_set_refused=0;join_attn_set_due[0]=join_attn_set_due[1]=0;
    InterlockedExchange64(&join_attn_set_posted,0);
    if(join_attn_set_on) join_attn_set_due[0]=join_attn_events_drop(1,NULL);
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
            join_attn_note(a->portable,level,a->level);
            memcpy(object+JOIN_ATTENTION,&a->level,4);++join_rx.attn_level_fixed;
            if(samples<JOIN_WORLD_ROWS && join_rx.nrows<JOIN_ROWS) {
                ++samples;snprintf(join_rx.rows[join_rx.nrows++],sizeof(join_rx.rows[0]),"attn side=joiner id=%016llx level=%u/%u vtable=%llx",
                                   (unsigned long long)a->portable,level,a->level,(unsigned long long)(vtable-(uintptr_t)image));
            }
        }
        if((a->flags&JOIN_ATTN_DUE) && due!=a->due) {memcpy(object+JOIN_ATTENTION_DUE,&a->due,8);++join_rx.attn_due_fixed;}
        if(level!=a->level) (void)join_attn_reconcile(object,level,a->level);
        ++join_rx.attn_applied;
    }
    if(join_attn_set_on) {
        if(InterlockedCompareExchange(&join_attn_post_on,0,0)) {(void)join_attn_post_swap(0);InterlockedExchange(&join_attn_post_on,0);}
        if(join_attn_set_calls) join_attn_set_dropped=join_attn_events_drop(0,&join_attn_set_kept);
        join_attn_set_due[1]=join_attn_events_drop(1,NULL);
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
        if(r->kind==JOIN_FLIGHT_NOMODEL && r->flags==JOIN_FLIGHT_OTHER) {++join_rx.flight_other;continue;}   /* a slot ship's Agent: AGENT and the rest */
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
        if(r->flags==JOIN_FLIGHT_OTHER && r->kind==JOIN_SPLINE_VTABLE && s->version>=12) continue; /* SPLM carries it (join_admit_splm) */
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
/* ---- the joiner, version 10 ---- */
typedef void *(*join_solver_prepare_fn)(void *agent);
static void *join_solver_prepare_native(void *agent) {return ((void *(*)(void *,int,int,float))(void *)(image+JOIN_SOLVER_PREPARE))(agent,1,0,0.0f);}
static join_solver_prepare_fn join_solver_prepare=join_solver_prepare_native;   /* tests replace it */
static unsigned char join_solver_buffer[JOIN_SOLVER_BYTES];
/* A record's bytes for the joiner's solver block at `solver` into join_solver_buffer. J1 j1-39: a session heap pointer copied raw
 * (ship 176bf8's solver +0x81c8: a vector {begin, end, capacity} the JPM controllers allocate on demand) crashed the joiner at 1281
 * when it pushed into it. Version 12: the session marked each qword that was an address of its memory (JOIN_SOLVER_HEAP); the
 * joiner keeps its own value from its solver there (its own vector, its own unset bytes) and writes the rest. Before version 12:
 * every qword that looks like a user address (8-aligned, 4 GiB .. 2^47) keeps the joiner's own. Counted (solver_heap). Then the
 * listed pointers absolute for this block and this image. */
static void join_solver_absolute(const join_solver_rec *r,unsigned char *solver,uint32_t version) {
    static unsigned char own[JOIN_SOLVER_BYTES];
    memcpy(join_solver_buffer,r->data,JOIN_SOLVER_BYTES);
    if(join_read(solver,own,JOIN_SOLVER_BYTES))
        for(unsigned at=0;at+8u<=JOIN_SOLVER_BYTES;at+=8) {
            uint64_t q;memcpy(&q,join_solver_buffer+at,8);
            if(version>=12?q==JOIN_SOLVER_HEAP:JOIN_HEAPISH(q)) {memcpy(join_solver_buffer+at,own+at,8);++join_rx.solver_heap;}
        }
    for(uint32_t k=0;k<r->pointers;k++) {
        const uint32_t e=r->pointer[k],at=(e>>1)*8u;uint64_t q;
        memcpy(&q,r->data+at,8);q+=(e&1u)?(uint64_t)(uintptr_t)image:(uint64_t)(uintptr_t)solver;memcpy(join_solver_buffer+at,&q,8);
    }
}
/* SOLVER into the joiner's Agents (boot phase 3: no pass runs; a record that does not fit changes nothing): the Agent's solver, through
 * the engine when it has none yet (solver created), every byte the session's, its pointers made absolute for this object and this image
 * (solver fixed). Counted: missing (no object), shape (no navigation Agent, no solver after the preparation, a record the saver could not
 * normalize), kind (another model class than the session's: the bytes are still written, STEER and FLIGHT count the model itself). */
static int join_admit_solver(const join_tables *s,const join_tables *mine) {
    unsigned rows=0;
    for(uint32_t i=0;i<s->nsolver;i++) {
        const join_solver_rec *r=&s->solver[i];
        uint64_t runtime=join_runtime_of(mine,r->portable);unsigned char *object=join_object(runtime),*agent=object?join_nav_agent(object):NULL;
        unsigned char *solver=NULL,*model=NULL;uintptr_t vtable=0;
        if(!object) {
            ++join_rx.solver_missing;
            join_world_row(&rows,"solver side=session id=%016llx why=missing runtime=%llx",(unsigned long long)r->portable,(unsigned long long)runtime);
            continue;
        }
        if(!agent || (r->flags&JOIN_SOLVER_OVERFLOW) || !join_read(agent+JOIN_AGENT_SOLVER,&solver,8)) {
            ++join_rx.solver_shape;
            join_world_row(&rows,"solver side=joiner id=%016llx why=shape check=%s",(unsigned long long)r->portable,agent?"record":"agent");
            continue;
        }
        if(!solver) {
            if(!join_solver_prepare(agent) || !join_read(agent+JOIN_AGENT_SOLVER,&solver,8) || !solver) {
                ++join_rx.solver_shape;
                join_world_row(&rows,"solver side=joiner id=%016llx why=shape check=prepare",(unsigned long long)r->portable);
                continue;
            }
            ++join_rx.solver_created;
        }
        if(!join_read(agent+JOIN_AGENT_MODEL,&model,8) || !model || !join_read(model,&vtable,8) || vtable!=(uintptr_t)image+r->kind) {
            ++join_rx.solver_kind;
            join_world_row(&rows,"solver side=joiner id=%016llx why=kind kind=%x/%llx",(unsigned long long)r->portable,r->kind,
                           (unsigned long long)(vtable>=(uintptr_t)image?vtable-(uintptr_t)image:0));
        }
        join_solver_absolute(r,solver,s->version);
        if(join_poke((uintptr_t)solver,join_solver_buffer,JOIN_SOLVER_BYTES)) ++join_rx.solver_fixed;
        ++join_rx.solver_applied;
        if(r->flags&JOIN_SOLVER_ACTIVE) join_t1x_add(runtime,r->portable);
    }
    return 1;
}
/* PCSV into the joiner's PlayerControllers (boot phase 3, opt-in): each record's solver block into the controller of the same ship,
 * as SOLVER writes an Agent's (join_solver_absolute). Counted: written / equal, missing (no object or no PlayerController: a slot
 * the resume builds later), shape (an uncarried record or another kind). */
static void join_admit_pcsv(const join_tables *s,const join_tables *mine) {
    join_pcsv_written=join_pcsv_equal=join_pcsv_missing=join_pcsv_shape=0;
    if(!join_pcsv_on) return;
    for(uint32_t i=0;i<s->npcsv;i++) {
        const join_solver_rec *r=&s->pcsv[i];
        unsigned char *controller=join_pcsv_controller(join_object(join_runtime_of(mine,r->portable)));
        if(!controller) {++join_pcsv_missing;continue;}
        if((r->flags&JOIN_SOLVER_OVERFLOW) || r->kind!=PSLOT_VTABLE) {++join_pcsv_shape;continue;}
        join_solver_absolute(r,controller+JOIN_PCSV_AT,s->version);
        if(join_poke((uintptr_t)(controller+JOIN_PCSV_AT),join_solver_buffer,JOIN_SOLVER_BYTES)) ++join_pcsv_written; else ++join_pcsv_equal;
    }
}
/* ---- the joiner, version 11 ----
 * GRAV, GRAVC and GRAVS into the joiner (boot phase 3, after gdl_admit set every active unit's times back to -1; the Gravidar thread
 * idle: gdl_admit checked it, lockstep_gate.c). The joiner's load built every active unit empty, and passive units only as its scans
 * reach their objects (the sector scan 0x10b34c0 makes them through 0x10b2aa0: j1-32 had 262 of the session's 2,638 at T+1), so:
 *   - a passive unit the joiner lacks is made through the engine's own 0x10b2aa0 (it takes the bucket lock and queues the object's
 *     UpdateGravidarAccessEvent as the session's scan did; created), then written as any other;
 *   - under the unit's lock: +0x40..+0x4f and +0x60..+0x67, an active unit's times +0xa8..+0xc7, and its four contact lists in this
 *     process's runtime IDs, sorted (a list that does not fit its buffer gets a new one from the engine's allocator: buffers;
 *     the old one is left to the heap, a few bytes per unit);
 *   - the worker's active sources in the session's order; every field sector's deadline, and, for a sector the session's
 *     native_gate_gravidar_deadlines had seen (SEEN), the joiner's gdl table too, so its first Move keeps the deadline.
 * Counted: applied, fixed (a byte changed), missing (no object or no unit after the creation), kind (another class), shape (an
 * uncarried record, a list pointer the joiner lacks), contacts written, contacts without a joiner object (unmapped), buffers. */
#define JOIN_GRAV_PASSIVE_NEW 0x10b2aa0u
/* The session's Gravidar round-robin cursor into the joiner's thread slot (lockstep_ai_schedule.h: the worker idle under its ready
 * lock, the slot's frame checked, the same sector count), and ai_schedule's own expectation with it, so the frame of T+1 checks and
 * the pass of T+1 starts where the session's does. 1 set, -1 ai_schedule not armed or not admitted, 0 refused. */
static int join_ai_sched_set(uint64_t cursor,uint64_t count) {
    unsigned char *w=ai_sched_worker;uint64_t now=0,n=0;int ok=0;
    if(!ai_sched_armed || !ai_sched_admitted || !w || !ai_sched_slot) return -1;
    EnterCriticalSection((CRITICAL_SECTION*)(w+AI_SCHED_W_READY_LOCK));
    if(*(volatile unsigned char*)(w+AI_SCHED_W_READY)==1 && ai_sched_frame_ok() && read_local((const void*)ai_sched_slot,&now,8) &&
       ai_sched_list(w,&n) && n==count && cursor<count) {*ai_sched_slot=cursor;ok=1;}
    LeaveCriticalSection((CRITICAL_SECTION*)(w+AI_SCHED_W_READY_LOCK));
    if(ok) {ai_sched_cursor=cursor;ai_sched_count=count;ai_sched_pending=0;ai_sched_latched=cursor!=0;}
    return ok;
}
static void *join_grav_passive_native(void *worker,void *object) {return ((void *(*)(void *,void *))(void *)(image+JOIN_GRAV_PASSIVE_NEW))(worker,object);}
static void *(*join_grav_passive_new)(void *,void *)=join_grav_passive_native;   /* tests replace it */
static uint64_t join_grav_out[JOIN_GRAV_CONTACTS_MAX];
/* n 8-byte entries into the vector at at: its own buffer when it has the room, else a new one (capacity n). -1: no memory. */
static int join_grav_put(unsigned char *at,const uint64_t *data,uint32_t n,int *changed) {
    uintptr_t v[3];uint64_t have=0,room=0;int grew=0;
    if(!join_flight_vector(at,8u,v,&have,&room)) return 0;
    if(n>room) {
        unsigned char *p=join_flight_alloc((size_t)n*8u);if(!p) return -1;
        v[0]=(uintptr_t)p;v[2]=v[0]+(uintptr_t)n*8u;
        if(join_poke((uintptr_t)at,&v[0],8)) *changed=1;
        if(join_poke((uintptr_t)at+16,&v[2],8)) *changed=1;
        grew=1;
    }
    if(n && join_poke(v[0],data,(size_t)n*8u)) *changed=1;
    uintptr_t end=v[0]+(uintptr_t)n*8u;
    if(join_poke((uintptr_t)at+8,&end,8)) *changed=1;
    return grew?2:1;
}
static int join_admit_grav(const join_tables *s,const join_tables *mine) {
    unsigned rows=0;unsigned char *w=join_grav_worker();uint32_t at=0;
    if(!w) {
        if(s->ngrav || s->ngravs) {++join_rx.grav_shape;join_world_row(&rows,"grav side=joiner why=worker");}
        return 1;
    }
    for(uint32_t i=0;i<s->ngrav;i++) {
        const join_grav_rec *r=&s->grav[i];const join_gravc_rec *c=s->gravc+at;uint32_t total=0;
        for(unsigned l=0;l<JOIN_GRAV_LISTS;l++) total+=r->count[l];
        at+=total;
        if(r->flags&JOIN_GRAV_UNCARRIED) {
            ++join_rx.grav_shape;join_world_row(&rows,"grav side=session id=%016llx why=uncarried",(unsigned long long)r->portable);continue;
        }
        uint64_t runtime=join_runtime_of(mine,r->portable);unsigned char *object=join_object(runtime),*unit=NULL;uintptr_t vtable=0;
        if(!object) {
            ++join_rx.grav_missing;join_world_row(&rows,"grav side=session id=%016llx why=missing",(unsigned long long)r->portable);continue;
        }
        unsigned char *lock=w+0x88+(runtime&255u)*0x28;
        join_grav_lock(lock,1);unit=join_grav_lookup(w,runtime);join_grav_lock(lock,0);
        if(!unit && r->kind==JOIN_GRAV_PASSIVE_VT) {
            unit=join_grav_passive_new(w,object);   /* takes the bucket lock itself */
            if(unit) ++join_rx.grav_created;
        }
        if(!unit) {
            ++join_rx.grav_missing;join_world_row(&rows,"grav side=joiner id=%016llx why=no_unit",(unsigned long long)r->portable);continue;
        }
        if(!join_read(unit,&vtable,8) || vtable!=(uintptr_t)image+r->kind) {
            ++join_rx.grav_kind;
            join_world_row(&rows,"grav side=joiner id=%016llx why=kind kind=%x/%llx",(unsigned long long)r->portable,r->kind,
                           (unsigned long long)(vtable>=(uintptr_t)image?vtable-(uintptr_t)image:0));
            continue;
        }
        int changed=0,bad=0;
        join_grav_lock(unit+0x18,1);
        if(join_poke((uintptr_t)unit+0x40,r->unit+0x40,0x10)) changed=1;
        /* +0x50..+0x5f: the live view (+0x54) and player view (+0x58) states the radar's apply 0x10abc70 compares with the
         * viewer's flags (+0xb8, +0xb9) and on a change posts ObjectEntered/LeftLiveView and ...PlayerView. j1-153/154: written
         * nowhere before, a joiner's first apply posted ObjectEnteredLiveView for the 15 units the session had in live view
         * and ObjectEnteredPlayerView for the 14 of them in player view (actor 5 among them, its new decision 16 ticks on). */
        if(join_poke((uintptr_t)unit+0x50,r->unit+0x50,0x10)) changed=1;
        if(join_poke((uintptr_t)unit+0x60,r->unit+0x60,0x08)) changed=1;
        {   /* +0x10: the player view's viewer, by portable ID (0: none, or unmapped on the saver) */
            uint64_t key=0,viewer=0;memcpy(&key,r->unit+0x10,8);
            if(key && (viewer=join_runtime_of(mine,key))!=0 && join_poke((uintptr_t)unit+0x10,&viewer,8)) changed=1;
        }
        if(r->flags&JOIN_GRAV_ACTIVE) {
            if(join_poke((uintptr_t)unit+0xa8,r->unit+0xa8,0x20)) changed=1;
            uint32_t k=0;
            for(unsigned l=0;l<JOIN_GRAV_LISTS && !bad;l++) {
                unsigned char *list=NULL;uint32_t n=0;
                if(l<2) list=unit+0x68+l*0x18;
                else if(!join_read(unit+0x98+(l-2)*8u,&list,8)) list=NULL;
                for(uint32_t q=0;q<r->count[l];q++,k++) {
                    uint64_t id=c[k].contact?join_runtime_of(mine,c[k].contact):0;
                    if(!id || !join_object(id)) {++join_rx.grav_unmapped;continue;}
                    join_grav_out[n++]=id;
                }
                if(!list) {if(n) bad=1;continue;}
                if(n>1) qsort(join_grav_out,n,8u,join_u64_cmp);
                int put=join_grav_put(list,join_grav_out,n,&changed);
                if(put<=0) bad=1;
                else {join_rx.grav_contacts+=n;if(put==2) ++join_rx.grav_buffers;}
            }
        }
        join_grav_lock(unit+0x18,0);
        if(bad) {++join_rx.grav_shape;join_world_row(&rows,"grav side=joiner id=%016llx why=list",(unsigned long long)r->portable);continue;}
        ++join_rx.grav_applied;
        if(changed) ++join_rx.grav_fixed;
    }
    {   /* the active sources, in the session's order (components of this process) */
        for(unsigned kind=0;kind<2;kind++) {
            uint32_t n=0;
            for(uint32_t i=0;i<s->ngravs;i++) {
                const join_gravs_rec *r=&s->gravs[i];
                if(r->kind!=kind) continue;
                unsigned char *object=r->portable?join_object(join_runtime_of(mine,r->portable)):NULL;
                if(!object) {++join_rx.gravs_missing;continue;}
                join_grav_out[n++]=(uint64_t)(uintptr_t)object;
                if(n==JOIN_GRAV_CONTACTS_MAX) break;
            }
            int changed=0,put=join_grav_put(w+0x48+kind*0x18,join_grav_out,n,&changed);
            if(put<=0) {++join_rx.grav_shape;join_world_row(&rows,"gravs side=joiner kind=%u why=list",kind);}
            else ++join_rx.gravs_lists;
        }
    }
    for(uint32_t i=0;i<s->ngravs;i++) {   /* the field sectors */
        const join_gravs_rec *r=&s->gravs[i];
        if(r->kind!=JOIN_GRAVS_SECTOR || !r->portable) continue;
        uint64_t runtime=join_runtime_of(mine,r->portable);unsigned char *sector=join_object(runtime);uintptr_t vtable=0;
        if(!sector || !join_read(sector,&vtable,8) || vtable!=(uintptr_t)image+JOIN_GRAV_SECTOR_VT) {
            ++join_rx.gravs_missing;join_world_row(&rows,"gravs side=joiner sector=%016llx why=missing",(unsigned long long)r->portable);continue;
        }
        (void)join_poke((uintptr_t)sector+0x448,&r->value,8);
        if((r->flags&JOIN_GRAVS_SEEN) && gdl_armed && gdl_first_move(runtime)<0) {
            ++join_rx.grav_shape;join_world_row(&rows,"gravs side=joiner sector=%016llx why=gdl_full",(unsigned long long)r->portable);continue;
        }
        ++join_rx.gravs_sectors;
    }
    join_rx.gravs_cursor=2;
    for(uint32_t i=0;i<s->ngravs;i++) {
        const join_gravs_rec *r=&s->gravs[i];
        if(r->kind!=JOIN_GRAVS_CURSOR) continue;
        int set=join_ai_sched_set(r->value,r->index);
        join_rx.gravs_cursor=set>0?1u:set<0?2u:3u;
        if(!set) {++join_rx.grav_shape;join_world_row(&rows,"gravs side=joiner why=cursor value=%llu count=%u joiner_count=%llu",
                                                        (unsigned long long)r->value,r->index,(unsigned long long)ai_sched_count);}
    }
    join_gravn_kept=0;
    if(s->version>=12 && s->ngravn) {   /* version 12: every zone group's neighbour list as the session's, in this process's indices */
        uintptr_t v[2];
        join_grav_lock(w+0x6890,1);
        if(join_read(w+0x68b8,v,sizeof(v)) && v[1]>=v[0] && !((v[1]-v[0])&31u))
            for(uintptr_t p=v[0];p<v[1];p+=32) {
                unsigned char *sector=NULL;uintptr_t begin=0;uint64_t n=0,skey;
                if(!join_gravn_groups(p,&sector,&begin,&n) || !(skey=join_gravn_key(sector)) || n>JOIN_GRAV_CONTACTS_MAX) continue;
                uint32_t lo=0;while(lo<s->ngravn && s->gravn[lo].sector<skey) ++lo;
                if(lo==s->ngravn || s->gravn[lo].sector!=skey) continue;
                uint32_t hi=lo;while(hi<s->ngravn && s->gravn[hi].sector==skey) ++hi;
                /* GRAVN extra: the joiner's groups of this sector the session has no record of, with their own neighbour lists */
                static uint64_t xidx[JOIN_GRAVN_EXTRA_MAX],xlist[JOIN_GRAVN_EXTRA_MAX][64];static uint32_t xn[JOIN_GRAVN_EXTRA_MAX];uint32_t nx=0;
                for(uint64_t i=0;join_gravn_extra_on && i<n && nx<JOIN_GRAVN_EXTRA_MAX;i++) {
                    unsigned char *zone=NULL;uint64_t zkey;uintptr_t nb[3];
                    if(!join_read((void *)(begin+i*JOIN_GRAVN_GROUP),&zone,8) || !(zkey=join_gravn_key(zone))) continue;
                    uint32_t a=lo;while(a<hi && s->gravn[a].zone<zkey) ++a;
                    if(a<hi && s->gravn[a].zone==zkey) continue;
                    if(!join_read((void *)(begin+i*JOIN_GRAVN_GROUP+0x38),nb,24) || nb[1]<nb[0] || ((nb[1]-nb[0])&7u) || (nb[1]-nb[0])/8u>64u) continue;
                    xn[nx]=(uint32_t)((nb[1]-nb[0])/8u);
                    if(xn[nx] && !join_read((void *)nb[0],xlist[nx],(size_t)xn[nx]*8u)) continue;
                    xidx[nx++]=i;
                }
                for(uint64_t i=0;i<n;i++) {
                    unsigned char *zone=NULL;uint64_t zkey;
                    if(!join_read((void *)(begin+i*JOIN_GRAVN_GROUP),&zone,8) || !(zkey=join_gravn_key(zone))) continue;
                    uint32_t a=lo;while(a<hi && s->gravn[a].zone<zkey) ++a;
                    if(a==hi || s->gravn[a].zone!=zkey) {
                        ++join_rx.gravn_missing;
                        join_world_row(&rows,"gravn side=joiner sector=%016llx zone=%016llx why=extra",(unsigned long long)skey,(unsigned long long)zkey);
                        continue;
                    }
                    uint32_t m=0;
                    for(;a<hi && s->gravn[a].zone==zkey;a++) {
                        if(s->gravn[a].flags&JOIN_GRAVN_NONE) continue;
                        uint64_t j=0;
                        for(;j<n;j++) {unsigned char *nz=NULL;if(join_read((void *)(begin+j*JOIN_GRAVN_GROUP),&nz,8) && join_gravn_key(nz)==s->gravn[a].neighbour) break;}
                        if(j==n) {++join_rx.gravn_unfound;continue;}
                        join_grav_out[m++]=j;
                    }
                    for(uint32_t x=0;x<nx;x++) for(uint32_t q=0;q<xn[x];q++) {   /* GRAVN extra: this group is in an extra group's list */
                        if(xlist[x][q]!=i) continue;
                        int dup=0;for(uint32_t r=0;r<m;r++) dup|=join_grav_out[r]==xidx[x];
                        if(!dup && m<64u) {join_grav_out[m++]=xidx[x];++join_gravn_kept;}
                    }
                    if(m>1) qsort(join_grav_out,m,8u,join_u64_cmp);
                    {   /* compare with the group's own list as a set before writing */
                        uintptr_t nb[3];uint64_t own[64];uint32_t on=0;int same=0;
                        if(join_read((void *)(begin+i*JOIN_GRAVN_GROUP+0x38),nb,24) && nb[1]>=nb[0] && (nb[1]-nb[0])/8u==m && m<=64) {
                            on=m;if(!m || join_read((void *)nb[0],own,(size_t)m*8u)) {
                                if(on>1) qsort(own,on,8u,join_u64_cmp);
                                same=!memcmp(own,join_grav_out,(size_t)m*8u);
                            }
                        }
                        ++join_rx.gravn_groups;
                        if(same) continue;
                    }
                    int changed=0,put=join_grav_put((unsigned char *)(begin+i*JOIN_GRAVN_GROUP+0x38),join_grav_out,m,&changed);
                    if(put<=0) {++join_rx.grav_shape;join_world_row(&rows,"gravn side=joiner zone=%016llx why=list",(unsigned long long)zkey);}
                    else ++join_rx.gravn_fixed;
                }
            }
        join_grav_lock(w+0x6890,0);
    }
    if(s->version>=13) join_gravp_settled_build(s,mine);   /* version 13: the settled poses to restore after T+1 and T+2 */
    for(uint32_t i=0;s->version>=13 && i<s->nviewp;i++) {   /* version 13: the zone-held objects' settled poses, both views */
        unsigned char *object=join_object(join_runtime_of(mine,s->viewp[i].portable));
        if(!object) {++join_rx.viewp_missing;continue;}
        for(uint32_t v=0;v<2u;v++) {
            unsigned char *pose=NULL,own[0x40];
            if(!join_read(object+JOIN_VIEW_SLOTS+(size_t)v*0x80u+JOIN_VIEW_POSE,&pose,8) || !pose || !join_read(pose,own,sizeof(own)))
                {++join_rx.viewp_missing;continue;}
            if(!memcmp(own,s->viewp[i].pose,sizeof(own))) ++join_rx.viewp_equal;
            else if(join_poke((uintptr_t)pose,s->viewp[i].pose,0x40)) ++join_rx.viewp_written;
        }
    }
    for(uint32_t i=0;s->version>=13 && i<s->nshoot;i++) {   /* version 13: the weapons' shot state and their controllers' */
        const join_shoot_rec *r=&s->shoot[i];join_shoot_rec own;
        const uint64_t runtime=join_runtime_of(mine,r->portable);
        unsigned char *weapon=join_object(runtime),*ctl=NULL;
        if(!weapon || !join_shoot_read(weapon,&own,&ctl)) {++join_rx.shoot_missing;continue;}
        if(own.lastshot!=r->lastshot) {   /* the same shot's time, the save's four decimals of it */
            if(join_save_rounded(own.lastshot,r->lastshot)) {if(join_poke((uintptr_t)(weapon+JOIN_SHOOT_LASTSHOT),&r->lastshot,8)) ++join_rx.shoot_time;}
            else ++join_rx.shoot_far;
        }
        if(!r->has_ctl) continue;
        if(!ctl) {
            ++join_rx.shoot_missing;
            const __typeof__(*join_shoot_late) e={runtime,r->ctl_vt};(void)JOIN_PUSH(join_shoot_late,join_shoot_nlate,join_shoot_latecap,e);
            continue;
        }
        if(own.ctl_vt!=r->ctl_vt) {++join_rx.shoot_class;continue;}
        own.portable=r->portable;own.lastshot=r->lastshot;
        if(!memcmp(&own,r,sizeof(own))) {++join_rx.shoot_equal;continue;}
        int changed=join_poke((uintptr_t)(weapon+JOIN_SHOOT_WFLAGS),r->wflags,3);   /* each field on its own: join_poke is 0 when equal */
        changed|=join_poke((uintptr_t)(ctl+JOIN_SHOOT_AIM),r->aim,sizeof(r->aim));
        changed|=join_poke((uintptr_t)(ctl+JOIN_SHOOT_CFLAGS),r->cflags,2);
        changed|=join_poke((uintptr_t)(ctl+JOIN_SHOOT_TOL),&r->tol,4);
        for(unsigned k=0;k<5u;k++) changed|=join_poke((uintptr_t)(ctl+join_shoot_ctl_off[k]),&r->ctl[k],8);
        if(changed) ++join_rx.shoot_written; else ++join_rx.shoot_equal;
    }
    if(s->version>=13) join_admit_jobs(s);   /* version 13: the jobs' state +0x48 */
    if(s->version>=13) join_admit_jobg(s);   /* the jobs' own generator words (opt-in) */
    for(uint32_t i=0;s->version>=13 && i<s->nnavc;i++) {   /* version 13: the navigation controllers' cached transform and velocity */
        const join_navc_rec *r=&s->navc[i];
        unsigned char *c=join_navc_controller(join_object(join_runtime_of(mine,r->portable)));
        if(!c) {++join_rx.navc_missing;continue;}
        if(join_poke((uintptr_t)(c+JOIN_NAVC_CACHE),r->cache,sizeof(r->cache))) ++join_rx.navc_written; else ++join_rx.navc_equal;
    }
    for(uint32_t i=0;s->version>=13 && i<s->nspec;) {   /* version 13: the view cache specification entries, per component */
        const uint64_t portable=s->spec[i].portable;uint32_t end=i;
        while(end<s->nspec && s->spec[end].portable==portable) ++end;
        join_spec_entry e[JOIN_SPEC_MAX];unsigned char *at=NULL;int used[JOIN_SPEC_MAX]={0};
        unsigned char *object=join_object(join_runtime_of(mine,portable));
        int n=join_spec_read(object,e,&at);
        if(n>=0 && join_spec_engine(object)) {   /* an entry the joiner lacks (an attention handler the direct ATTN write skipped) */
            int added=0;
            for(uint32_t k=i;k<end;k++) {
                const join_spec_rec *r=&s->spec[k];int m=0;
                while(m<n && e[m].kind!=r->kind) ++m;
                if(m<n || r->interval || r->kind==7u) continue;   /* only without a random phase, never the one-shot kind 7 */
                join_spec_add(object,r);++join_rx.spec_added;added=1;
            }
            if(added) n=join_spec_read(object,e,&at);   /* the list may have grown into the heap */
        }
        for(uint32_t k=i;k<end;k++) {
            const join_spec_rec *r=&s->spec[k];int m=0;
            while(m<n && e[m].kind!=r->kind) ++m;
            if(n<=0 || m>=n) {++join_rx.spec_missing;continue;}
            used[m]=1;
            const join_spec_entry want={r->kind,r->flags,r->klass,r->trunc,r->interval,r->last};
            if(e[m].flags!=r->flags || e[m].klass!=r->klass || e[m].interval!=r->interval) ++join_rx.spec_conflict;
            if(join_poke((uintptr_t)(at+(size_t)m*sizeof(join_spec_entry)),&want,sizeof(want))) ++join_rx.spec_written;
            else ++join_rx.spec_equal;
        }
        for(int m=0;m<n;m++) if(!used[m]) ++join_rx.spec_extra;
        i=end;
    }
    for(uint32_t i=0;s->version>=13 && i<s->nlivef;i++) {   /* version 13: the live flags the session holds from an earlier setup */
        const join_livef_rec *r=&s->livef[i];uint32_t fresh=0,live=0;
        unsigned char *object=join_object(join_runtime_of(mine,r->portable));
        if(!join_livef_read(object,&fresh,&live)) {++join_rx.livef_missing;continue;}
        const uint32_t want=(live|r->set)&~r->clear;
        if(want==live) ++join_rx.livef_equal;
        else if(join_poke((uintptr_t)(object+JOIN_LIVEF_FLAGS),&want,4)) ++join_rx.livef_written;
    }
    for(uint32_t i=0;s->version>=13 && i<s->nattk;i++) {   /* version 13: the attack throttles at full precision */
        const join_attk_rec *r=&s->attk[i];join_attk_rec own;
        unsigned char *object=join_object(join_runtime_of(mine,r->portable));
        if(!join_attk_read(object,&own)) {++join_rx.attk_missing;continue;}
        if(own.attacker!=r->attacker || own.method!=r->method) {++join_rx.attk_conflict;continue;}
        int changed=0,beyond=0;
        for(unsigned k=0;k<3u;k++) {
            if(own.times[k]==r->times[k]) continue;
            if(!join_save_rounded(own.times[k],r->times[k])) {beyond=1;continue;}
            changed|=join_poke((uintptr_t)(object+JOIN_ATTK_TIMES+8u*k),&r->times[k],8);
        }
        if(beyond) ++join_rx.attk_far;
        if(changed) ++join_rx.attk_written; else if(!beyond) ++join_rx.attk_equal;
    }
    if(s->version>=12 && s->ngravp) {   /* version 12: the published poses, both views relative to this process's current one */
        const uint32_t cur=join_view_index();
        for(uint32_t i=0;i<s->ngravp;i++) {
            const join_gravp_rec *r=&s->gravp[i];
            unsigned char *object=join_object(join_runtime_of(mine,r->portable)),*pose=NULL;
            if(!object) {++join_rx.gravp_missing;continue;}
            if(!join_read(object+JOIN_VIEW_SLOTS+(size_t)(cur^r->rel)*0x80u+JOIN_VIEW_POSE,&pose,8)) {++join_rx.gravp_missing;continue;}
            if(!(r->flags&JOIN_GRAVP_PRESENT)) {if(pose) ++join_rx.gravp_extra;continue;}
            if(!pose) {++join_rx.gravp_absent;continue;}
            unsigned char own[0x40];
            if(join_read(pose,own,sizeof(own)) && !memcmp(own,r->pose,sizeof(own))) {++join_rx.gravp_equal;continue;}
            if(join_poke((uintptr_t)pose,r->pose,sizeof(r->pose))) ++join_rx.gravp_fixed;
        }
    }
    if(s->version>=12 && s->ngravq) {   /* version 12: the session's pending has_scanned posts, appended to this worker's list in order */
        unsigned char *sentinel=NULL;uint64_t size=0;
        join_gravq_cs(w+AI_SCHED_W_READY_LOCK,1);
        if(*(volatile unsigned char *)(w+AI_SCHED_W_READY)!=1 || !join_read(w+JOIN_GRAVQ_LIST,&sentinel,8) || !sentinel ||
           !join_read(w+JOIN_GRAVQ_LIST+8u,&size,8)) {
            ++join_rx.gravq_busy;join_world_row(&rows,"gravq side=joiner why=busy");
        } else {
            join_rx.gravq_had=size;
            for(uint32_t i=0;i<s->ngravq;i++) {
                const join_gravq_rec *r=&s->gravq[i];
                if(r->flags&JOIN_GRAVQ_OTHER) {++join_rx.gravq_other;continue;}
                const uint64_t runtime=join_runtime_of(mine,r->portable);
                if(!runtime || !join_object(runtime)) {++join_rx.gravq_unmapped;continue;}
                uint64_t *event=(uint64_t *)join_flight_alloc(0x18),*node=event?(uint64_t *)join_flight_alloc(0x40):NULL;
                if(!node) {++join_rx.gravq_alloc;break;}   /* an event without its node stays allocated (counted, strict refuses) */
                event[0]=(uintptr_t)image+JOIN_GRAVQ_EVENT_VT;event[1]=event[2]=0;
                uint64_t *tail=*(uint64_t **)(sentinel+8);
                memset(node,0,0x40);node[0]=(uintptr_t)sentinel;node[1]=(uintptr_t)tail;node[2]=3u;node[3]=runtime;node[4]=(uintptr_t)event;node[5]=r->time;
                *(uint64_t **)(sentinel+8)=node;tail[0]=(uintptr_t)node;++size;
                ++join_rx.gravq_posted;
            }
            *(uint64_t *)(w+JOIN_GRAVQ_LIST+8u)=size;
        }
        join_gravq_cs(w+AI_SCHED_W_READY_LOCK,0);
    }
    return 1;
}
/* ---- the joiner, version 12 ----
 * SPLM, SPLME and SPLMSEG into the joiner's spline models (boot phase 3: no pass runs). The model must be the session's kind and its
 * deques empty (as the load left them). The model's carried bytes are written; each embedded spline's vectors take the session's
 * segments (this process's BezierSpline vtable) and pairs; each deque is rebuilt as the engine's push builds it: a map of a power of two
 * entries above the count from the engine's allocator (the joiner's own when it has the room), one 0x50-byte block per entry, the
 * entry's CompositeSpline<5> from the engine's allocator with this process's vtable and its own segment and pair vectors (capacity =
 * count, the size class the engine's free derives from it), offset 0. */
static unsigned char *join_splm_alloc(size_t bytes) {unsigned char *p=join_flight_alloc(bytes);if(p) memset(p,0,bytes);return p;}
/* One spline's records (part list*2, list*2+1; element) into the scratch buffers with this process's BezierSpline vtable. */
static void join_splm_gather(const join_spline_rec *g,uint32_t ng,unsigned list,uint32_t element,uint32_t nseg,uint32_t npair) {
    uint32_t k=0;
    for(uint32_t i=0;i<ng;i++) {
        const join_spline_rec *x=&g[i];
        if(x->index>>16!=element) continue;
        if(x->part==list*2u && k<nseg) {
            uintptr_t bezier=(uintptr_t)image+JOIN_FLIGHT_BEZIER_VT;
            memcpy(join_splm_segs+(size_t)(x->index&0xffffu)*JOIN_FLIGHT_SEGMENT,x->data,JOIN_FLIGHT_SEGMENT);
            memcpy(join_splm_segs+(size_t)(x->index&0xffffu)*JOIN_FLIGHT_SEGMENT+JOIN_FLIGHT_BEZIER_AT,&bezier,8);++k;
        } else if(x->part==list*2u+1u) {
            uint32_t at=(x->index&0xffffu)*JOIN_FLIGHT_PAIRS_PER,used=npair>at?npair-at:0;if(used>JOIN_FLIGHT_PAIRS_PER) used=JOIN_FLIGHT_PAIRS_PER;
            memcpy(join_splm_pairs+at,x->data,(size_t)used*8u);
        }
    }
}
/* A heap CompositeSpline<5> for one entry: the session's head, this process's vtable, its vectors from the allocator. NULL: no memory. */
static unsigned char *join_splm_build(const join_splme_rec *e,const join_spline_rec *g,uint32_t ng) {
    unsigned char *cs=join_splm_alloc(JOIN_SPLM_CS),*segs=NULL,*pairs=NULL;
    if(!cs) return NULL;
    join_splm_gather(g,ng,2u+e->list,e->index,e->segments,e->pairs);
    if(e->segments && !(segs=join_splm_alloc((size_t)e->segments*JOIN_FLIGHT_SEGMENT))) return NULL;
    if(e->pairs && !(pairs=join_splm_alloc((size_t)e->pairs*8u))) return NULL;
    memcpy(cs,e->spline,JOIN_SPLM_CS);
    uintptr_t v=(uintptr_t)image+JOIN_FLIGHT_SPLINE_VT;memcpy(cs,&v,8);
    if(segs) {
        memcpy(segs,join_splm_segs,(size_t)e->segments*JOIN_FLIGHT_SEGMENT);
        uintptr_t s3[3]={(uintptr_t)segs,(uintptr_t)segs+(uintptr_t)e->segments*JOIN_FLIGHT_SEGMENT,(uintptr_t)segs+(uintptr_t)e->segments*JOIN_FLIGHT_SEGMENT};
        memcpy(cs+0x10,s3,24);
    }
    if(pairs) {
        memcpy(pairs,join_splm_pairs,(size_t)e->pairs*8u);
        uintptr_t p3[3]={(uintptr_t)pairs,(uintptr_t)pairs+(uintptr_t)e->pairs*8u,(uintptr_t)pairs+(uintptr_t)e->pairs*8u};
        memcpy(cs+0x28,p3,24);
    }
    return cs;
}
static int join_admit_splm(const join_tables *s,const join_tables *mine) {
    unsigned rows=0;uint32_t ei=0,gi=0;
    for(uint32_t i=0;i<s->nsplm;i++) {
        const join_splm_rec *r=&s->splm[i];
        const uint32_t e0=ei,g0=gi;
        while(ei<s->nsplme && s->splme[ei].portable==r->portable) ++ei;
        while(gi<s->nsplmseg && s->splmseg[gi].portable==r->portable) ++gi;
        const join_splme_rec *e=s->splme+e0;const join_spline_rec *g=s->splmseg+g0;const uint32_t ng=gi-g0;
        if(r->flags&(JOIN_SPLM_UNCARRIED|JOIN_SPLM_REQUESTS)) {
            ++join_rx.splm_other;
            join_world_row(&rows,"splm side=session id=%016llx why=%s requests=%u",(unsigned long long)r->portable,
                           (r->flags&JOIN_SPLM_UNCARRIED)?"uncarried":"requests",r->requests);
            if(r->flags&JOIN_SPLM_UNCARRIED) continue;
        }
        uint64_t runtime=join_runtime_of(mine,r->portable);unsigned char *object=join_object(runtime),*controller=NULL,*agent=NULL,*model=NULL;
        uintptr_t nav=0,vtable=0;
        if(!object) {++join_rx.splm_missing;join_world_row(&rows,"splm side=session id=%016llx why=missing",(unsigned long long)r->portable);continue;}
        if(!join_read(object+JOIN_CONTROLLER,&controller,8) || !controller || !join_read(controller,&nav,8) || nav!=(uintptr_t)image+JOIN_NAV_VTABLE ||
           !join_read(controller+0x40,&agent,8) || !agent || !join_read(agent+JOIN_AGENT_MODEL,&model,8) || !model ||
           !join_read(model,&vtable,8) || vtable!=(uintptr_t)image+JOIN_SPLINE_VTABLE) {
            ++join_rx.splm_kind;
            join_world_row(&rows,"splm side=joiner id=%016llx why=kind kind=%llx",(unsigned long long)r->portable,
                           (unsigned long long)(vtable>=(uintptr_t)image?vtable-(uintptr_t)image:0));
            continue;
        }
        uint32_t own[2]={0,0};uintptr_t blocks[JOIN_SPLM_ENTRIES];
        if(!join_splm_deque(model+JOIN_SPLM_DEQUE0,blocks,JOIN_SPLM_ENTRIES,&own[0]) || !join_splm_deque(model+JOIN_SPLM_DEQUE0+0x28,blocks,JOIN_SPLM_ENTRIES,&own[1]) ||
           own[0] || own[1]) {
            ++join_rx.splm_shape;
            join_world_row(&rows,"splm side=joiner id=%016llx why=route entries=%u/%u",(unsigned long long)r->portable,own[0],own[1]);
            continue;
        }
        int changed=0,bad=0;
        {   /* the carried bytes: every run between the skipped ranges (sorted by offset) */
            unsigned at=0;
            for(unsigned k=0;k<=JOIN_SPLM_SKIPS;k++) {
                const unsigned end=k<JOIN_SPLM_SKIPS?join_splm_skip[k].at:JOIN_SPLM_BYTES;
                if(end>at && join_poke((uintptr_t)model+at,r->model+at,end-at)) changed=1;
                if(k<JOIN_SPLM_SKIPS) at=join_splm_skip[k].at+join_splm_skip[k].len;
            }
        }
        for(unsigned l=0;l<2 && !bad;l++) {   /* the embedded splines */
            int allocated=0;unsigned char *cs=model+JOIN_SPLM_EMB0+l*0x50u;
            join_splm_gather(g,ng,l,0,r->segments[l],r->pairs[l]);
            int put=join_flight_put(cs+0x10,JOIN_FLIGHT_SEGMENT,join_splm_segs,r->segments[l],&allocated,&changed);
            if(put>0) put=join_flight_put(cs+0x28,8u,(const unsigned char *)join_splm_pairs,r->pairs[l],&allocated,&changed);
            if(put<=0) {
                if(put<0) ++join_rx.splm_alloc;else ++join_rx.splm_shape;
                join_world_row(&rows,"splm side=joiner id=%016llx why=%s spline=%u",(unsigned long long)r->portable,put<0?"alloc":"spline",l);
                bad=1;
            }
        }
        for(unsigned l=0;l<2 && !bad;l++) {   /* the deques */
            const uint32_t n=r->entries[l];if(!n) continue;
            unsigned char *deque=model+JOIN_SPLM_DEQUE0+l*0x28u;uint64_t d[5];
            if(!join_read(deque,d,sizeof(d))) {bad=1;++join_rx.splm_shape;break;}
            uint64_t size=d[2];unsigned char *map=(unsigned char *)(uintptr_t)d[1];
            if(!map || size<=n || (size&(size-1))) {
                size=8;while(size<=n) size*=2;
                if(!(map=join_splm_alloc((size_t)size*8u))) {bad=1;++join_rx.splm_alloc;break;}
            }
            for(uint32_t k=0;k<n && !bad;k++) {
                const join_splme_rec *x=NULL;
                for(uint32_t q=0;q<ei-e0;q++) if(e[q].list==l && e[q].index==k) {x=&e[q];break;}
                unsigned char *block=NULL,*cs=NULL;
                if(!x || !join_read(map+(size_t)k*8u,&block,8)) {bad=1;++join_rx.splm_shape;break;}
                if(!block && !(block=join_splm_alloc(0x50))) {bad=1;++join_rx.splm_alloc;break;}
                if((x->flags&JOIN_SPLME_SPLINE) && !(cs=join_splm_build(x,g,ng))) {bad=1;++join_rx.splm_alloc;break;}
                unsigned char entry[0x50];memcpy(entry,x->entry,0x50);memcpy(entry,&cs,8);
                (void)join_poke((uintptr_t)block,entry,0x50);
                (void)join_poke((uintptr_t)map+(uintptr_t)k*8u,&block,8);
                ++join_rx.splm_entries;
            }
            if(bad) break;
            uint64_t head[4]={(uint64_t)(uintptr_t)map,size,0,n};
            (void)join_poke((uintptr_t)deque+8,head,sizeof(head));changed=1;
        }
        if(bad) {join_world_row(&rows,"splm side=joiner id=%016llx why=rebuild",(unsigned long long)r->portable);continue;}
        ++join_rx.splm_applied;
        if(changed) ++join_rx.splm_fixed;
        join_t1x_add(runtime,r->portable);   /* the T+1 rows: the state word +0x218 of both sides */
    }
    return 1;
}
/* Every world record (HEALTH, POSE; version 3 SHIELD, NAV, ATTN, KEYS; version 4 REGION, PATH; version 5 FLIGHT) into the joiner's objects (boot
 * phase 3, main thread, the MoveWorker and the pathing thread idle). 0: strict refusal. */
/* The joiner (admission, main thread, no frame running): every source of its own with events of a rebuilt class loses them (the
 * engine's removal per type), and the session's are posted at their times in the session's order. */
static void join_admit_evqt(const join_tables *s,const join_tables *mine) {
    enum {PAIRS=65536};static uintptr_t src[PAIRS];static uint32_t type[PAIRS];uint32_t np=0;
    uint64_t count=0;unsigned char *node=NULL;
    if(join_read(join_evq_at(JOIN_EVQ_COUNT),&count,8) && count && join_read(join_evq_at(JOIN_EVQ_LEFT),&node,8))
        for(uint64_t i=0;i<count && i<200000u && node && np<PAIRS;i++,node=join_evq_next(node)) {
            unsigned char *so=NULL,*q=NULL,*e=NULL;uint64_t n=0;
            if(!join_read(node+0x28,&so,8) || !so || !join_read(so+0x20,&q,8) || !q || !join_read(q+0x28,&n,8) || !join_read(q+0x20,&e,8)) continue;
            for(uint32_t k=0;k<n && k<4096u && e && np<PAIRS;k++,e=join_evq_next(e)) {
                unsigned char *ev=NULL;uintptr_t v=0;
                if(!join_read(e+0x28,&ev,8) || !ev || !join_read(ev,&v,8)) break;
                const uint32_t cls=v>=(uintptr_t)image && v-(uintptr_t)image<JOIN_FLIGHT_IMAGE?(uint32_t)(v-(uintptr_t)image):0u,id=join_evqt_type(cls);
                if(!join_evqt_is_rebuilt(cls) || !id) continue;
                uint32_t j=0;while(j<np && !(src[j]==(uintptr_t)so && type[j]==id)) ++j;
                if(j==np) {src[np]=(uintptr_t)so;type[np]=id;++np;}
            }
        }
    for(uint32_t j=0;j<np;j++) {join_evqt_remove((void *)src[j],(int)type[j],-1.0);++join_rx.evqt_removed;}
    for(uint32_t i=0;i<s->nevqt;i++) {
        const join_evqt_rec *r=&s->evqt[i];
        if(!join_evqt_is_rebuilt(r->cls) || !r->type) {++join_rx.evqt_other;continue;}
        unsigned char *object=(r->flags&JOIN_EVQT_UNMAPPED)?NULL:join_object(join_runtime_of(mine,r->portable));
        if(!object) {++join_rx.evqt_missing;continue;}
        /* the session's source had them removed above when the joiner had any; when it had none, nothing of the type waits there */
        uint64_t *ev=(uint64_t *)join_flight_alloc(0x18);
        if(!ev) {++join_rx.evqt_alloc;continue;}
        ev[0]=(uintptr_t)image+r->cls;ev[1]=0;ev[2]=0;
        double time;memcpy(&time,&r->time,8);
        join_evqt_post(object,ev,time,0);++join_rx.evqt_posted;
    }
}
/* The components whose views keep the joiner's own words: every one the session had in flight and all its ancestors (the
 * publication goes down from them), as sorted object pointers. */
static unsigned char **join_viewf_keep;static uint32_t join_viewf_nkeep,join_viewf_keep_cap;
static int join_viewf_ptr_cmp(const void *a,const void *b) {uintptr_t x=*(const uintptr_t *)a,y=*(const uintptr_t *)b;return x<y?-1:x>y;}
static void join_viewf_keep_build(const join_tables *s,const join_tables *mine) {
    free(join_viewf_keep);join_viewf_keep=NULL;join_viewf_nkeep=join_viewf_keep_cap=0;
    for(uint32_t i=0;i<s->nviewf;i++) {
        if(!(s->viewf[i].attention[1]&JOIN_VIEWF_INFLIGHT)) continue;
        unsigned char *o=join_object(join_runtime_of(mine,s->viewf[i].portable));
        for(unsigned d=0;d<12 && o;d++) {
            unsigned char *p=NULL;
            if(!JOIN_PUSH(join_viewf_keep,join_viewf_nkeep,join_viewf_keep_cap,o)) return;
            if(!join_read(o+JOIN_WATCH_PARENT,&p,8)) break;
            o=join_watch_live(p);
        }
    }
    if(join_viewf_nkeep>1) qsort(join_viewf_keep,join_viewf_nkeep,sizeof(*join_viewf_keep),join_viewf_ptr_cmp);
}
static int join_viewf_kept(unsigned char *o) {
    uint32_t lo=0,hi=join_viewf_nkeep;
    while(lo<hi) {uint32_t mid=lo+(hi-lo)/2u;if((uintptr_t)join_viewf_keep[mid]<(uintptr_t)o) lo=mid+1u;else hi=mid;}
    return lo<join_viewf_nkeep && join_viewf_keep[lo]==o;
}
/* VIEWF's rewrites (diagnostic): the 32 most common (view, joiner's word, session's word), one JOIN_VIEWF_HIST line at the admission. */
#define JOIN_VIEWF_HIST 32u
static struct {uint32_t rel,have,want;uint64_t n;} join_viewf_hist[JOIN_VIEWF_HIST];static unsigned join_viewf_hist_n;static uint64_t join_viewf_hist_other;
static void join_viewf_hist_add(uint32_t rel,uint32_t have,uint32_t want) {
    for(unsigned k=0;k<join_viewf_hist_n;k++)
        if(join_viewf_hist[k].rel==rel && join_viewf_hist[k].have==have && join_viewf_hist[k].want==want) {++join_viewf_hist[k].n;return;}
    if(join_viewf_hist_n<JOIN_VIEWF_HIST) {join_viewf_hist[join_viewf_hist_n].rel=rel;join_viewf_hist[join_viewf_hist_n].have=have;join_viewf_hist[join_viewf_hist_n].want=want;join_viewf_hist[join_viewf_hist_n++].n=1;}
    else ++join_viewf_hist_other;
}
static void join_viewf_hist_note(void) {
    char out[4096];size_t at=(size_t)snprintf(out,sizeof(out),"[X4LAN] NATIVE_GATE JOIN_VIEWF_HIST other=%llu",(unsigned long long)join_viewf_hist_other);
    for(unsigned k=0;k<join_viewf_hist_n && at+64<sizeof(out);k++)
        at+=(size_t)snprintf(out+at,sizeof(out)-at," %u:%x>%x*%llu",join_viewf_hist[k].rel,join_viewf_hist[k].have,join_viewf_hist[k].want,(unsigned long long)join_viewf_hist[k].n);
    at+=(size_t)snprintf(out+at,sizeof(out)-at,"\r\n");crash_note_write(out,(int)at);
}
/* VIEWK: the session's unpublished views (report 1.18): its view flags and, when its children are among the joiner's, its children. */
static uintptr_t join_viewk_want[JOIN_VIEWK_CHILDREN];
static void join_admit_viewk(const join_tables *s,const join_tables *mine) {
    const uint32_t cur=join_view_index();uint32_t j=0;
    for(uint32_t i=0;i<s->nviewk;i++) {
        const join_viewk_rec *r=&s->viewk[i];const join_viewkc_rec *c=s->viewkc+j;j+=r->count;
        unsigned char *object=join_object(join_runtime_of(mine,r->portable));
        if(!object) {++join_rx.viewk_missing;continue;}
        if(r->flags) {++join_rx.viewk_unmapped;continue;}
        unsigned char *block=object+JOIN_VIEW_SLOTS+(size_t)(cur^r->rel)*0x80u;uintptr_t begin=0;uint64_t n=0;uint32_t vf=0;
        if(!join_grav_vector(block+JOIN_VIEW_CHILDREN,&begin,&n) || (n && !join_read((void *)begin,join_grav_ids,(size_t)n*8u)) ||
           !join_read(block+JOIN_VIEW_FLAGS,&vf,4)) {++join_rx.viewk_missing;continue;}
        int ok=1,same=n==r->count;
        for(uint32_t x=0;x<r->count && ok;x++) {
            const uintptr_t want=(uintptr_t)join_object(join_runtime_of(mine,c[x].child));int found=0;
            for(uint64_t y=0;y<n && want;y++) if((uintptr_t)join_grav_ids[y]==want) {found=1;break;}
            if(!found) ok=0;
            else {join_viewk_want[x]=want;if(same && (uintptr_t)join_grav_ids[x]!=want) same=0;}
        }
        if(!ok) {++join_rx.viewk_superset;continue;}
        if(same && vf==r->vflags) {++join_rx.viewk_equal;continue;}
        if(!same) {
            for(uint32_t x=0;x<r->count;x++) (void)join_poke(begin+(uintptr_t)x*8u,&join_viewk_want[x],8);
            const uintptr_t end=begin+(uintptr_t)r->count*8u;(void)join_poke((uintptr_t)(block+JOIN_VIEW_CHILDREN+8u),&end,8);
            ++join_rx.viewk_children;
        }
        if(vf!=r->vflags) {(void)join_poke((uintptr_t)(block+JOIN_VIEW_FLAGS),&r->vflags,4);++join_rx.viewk_flags;}
    }
}
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
    if(s->version>=9) join_admit_steer(s,&mine);
    if(s->version>=13) join_admit_undk(s,&mine);   /* the undocking behaviours (opt-in), after STEER */   /* the steering models FLIGHT holds the kind of; before PATH primes the thread */
    if(s->version>=10) join_admit_solver(s,&mine);  /* the Agents' motion solvers */
    if(s->version>=11) join_admit_grav(s,&mine);    /* the radar */
    if(s->version>=13) join_admit_gravb(s,&mine);   /* the radar's mapped bounds */
    if(s->version>=13) join_admit_viewl(s,&mine);   /* the live and player view flags (opt-in) */
    if(s->version>=13) join_admit_ikv(s,&mine);     /* the skeletons' IK joints (the weapons' gimbals) */
    if(s->version>=13) join_admit_ecos(s,&mine);    /* the economy schedules (opt-in) */
    if(s->version>=13) join_admit_shpw(s,&mine);    /* the ships' words a load leaves at defaults (opt-in) */
    if(s->version>=13) join_admit_trdn(s,&mine);    /* the containers' next trade offer update (opt-in) */
    if(s->version>=13) join_admit_npca(s,&mine);    /* the NPCs' activation (opt-in) */
    if(s->version>=13) join_admit_code(s);          /* the object code counter (opt-in) */
    if(s->version>=13) join_admit_engr(s,&mine);    /* the engines' boost ramps (opt-in) */
    if(s->version>=13) join_admit_shgr(s,&mine);    /* the containers' shield recharge groups (opt-in) */
    if(s->version>=13) join_admit_bnds(s,&mine);    /* the stations' bounding structs (opt-in) */
    if(s->version>=13) join_admit_resv(s,&mine);    /* the zones' space reservations' boxes and expiries (opt-in) */
    if(s->version>=13) join_admit_pcsv(s,&mine);    /* the PlayerControllers' own motion solvers (opt-in) */
    if(s->version>=13) join_admit_conn(s,&mine);    /* the NPCs' connections' partners a waiting ship's load leaves null (opt-in) */
    if(s->version>=13) join_admit_warp(s,&mine);    /* the pending warps' destinations a load rebuilds from angles (opt-in) */
    if(s->version>=13) join_admit_omgs(s,&mine);    /* the steering ships' OmegaSwitched controllers (opt-in) */
    if(s->version>=13) join_admit_rcnt(s,&mine);    /* the created ships' and bays' reference counts (opt-in, experiment) */
    if(s->version>=12) join_admit_splm(s,&mine);    /* the spline models' routes */
    if(s->version>=13) join_admit_evqt(s,&mine);   /* version 13: the pending events of the rebuilt classes, the session's */
    if(s->version>=13 && s->ngravd && !(join_view_skip&8u)) {   /* version 13: every ship's cached parent in both views as the session's (the radar's docked filter) */
        const uint32_t cur=join_view_index();
        for(uint32_t i=0;i<s->ngravd;i++) {
            const join_gravd_rec *r=&s->gravd[i];
            unsigned char *object=join_object(join_runtime_of(&mine,r->portable)),*want=NULL,*have=NULL;
            if(!object) {++join_rx.gravd_missing;continue;}
            if(r->flags&JOIN_GRAVD_UNMAPPED) {++join_rx.gravd_unmapped;continue;}
            if(!(r->flags&JOIN_GRAVD_NONE) && !(want=join_object(join_runtime_of(&mine,r->parent)))) {++join_rx.gravd_missing;continue;}
            unsigned char *slot=object+JOIN_VIEW_SLOTS+(size_t)(cur^r->rel)*0x80u;
            if(!join_read(slot,&have,8)) {++join_rx.gravd_missing;continue;}
            if(have==want) {++join_rx.gravd_equal;continue;}
            if(join_poke((uintptr_t)slot,&want,8)) ++join_rx.gravd_fixed;
        }
    }
    for(unsigned i=0;i<join_watch.n;i++) join_watch.runtime[i]=join_watch_runtime(s,&mine,join_watch.portable[i]);   /* diagnostic */
    join_watch_dump('k',s->gate.tick);   /* the joiner's own views before VIEWK and VIEWF */
    if(s->version>=13 && s->nviewk && !(join_view_skip&2u)) join_admit_viewk(s,&mine);   /* version 13: the views the session left unpublished, as its */
    if(s->version>=13 && s->nviewf) {join_viewf_hist_n=0;join_viewf_keep_build(s,&mine);}
    if(s->version>=13 && s->nviewf && (join_viewf_write || join_viewf_bits) && !(join_view_skip&1u)) {   /* opt-in (diagnostic): every view's flags as the session's (a load leaves its current view pending), or only join_viewf_bits of them */
        const uint32_t cur=join_view_index();
        for(uint32_t i=0;i<s->nviewf;i++) {
            unsigned char *object=join_object(join_runtime_of(&mine,s->viewf[i].portable));
            if(!object) {++join_rx.viewf_missing;continue;}
            if(join_viewf_kept(object)) {++join_rx.viewf_inflight;continue;}   /* in flight, or above one: the joiner's own publication */
            for(uint32_t rel=0;rel<2u;rel++) {
                unsigned char *at=object+JOIN_VIEW_SLOTS+(size_t)(cur^rel)*0x80u+JOIN_VIEW_FLAGS;uint32_t have=0;
                if(!join_read(at,&have,4)) {++join_rx.viewf_missing;continue;}
                uint32_t att=0;unsigned char *aat=object+JOIN_VIEW_SLOTS+(size_t)(cur^rel)*0x80u+JOIN_VIEW_ATTENTION;
                if(join_viewf_write && join_read(aat,&att,4) && att!=s->viewf[i].attention[rel] && join_poke((uintptr_t)aat,&s->viewf[i].attention[rel],4)) ++join_rx.viewf_attention;
                const uint32_t want=join_viewf_write?s->viewf[i].flags[rel]:(have&~join_viewf_bits)|(s->viewf[i].flags[rel]&join_viewf_bits);
                if(have==want) ++join_rx.viewf_equal;
                else {
                    join_viewf_hist_add(rel,have,want);
                    if(join_poke((uintptr_t)at,&want,4)) ++join_rx.viewf_written;
                }
            }
        }
    }
    {double now=0;memcpy(&now,&s->gate.game,8);   /* census: what this load scheduled, against the session's (JOIN_STATUS evq/evq_rx) */
     join_evq_census(now,&join_rx.evq_sources,&join_rx.evq_events,&join_rx.evq_due,&join_rx.evq_fault,join_rx.evq_top,sizeof(join_rx.evq_top));
     join_evq_dump('j',now);}
    if(s->version>=13 && s->nviewf && (join_viewf_write || join_viewf_bits)) join_viewf_hist_note();   /* diagnostic */
    if(s->version>=13) join_gravp_settled_parents();   /* the restore's cached parents, after GRAVD, VIEWK and VIEWF */
    for(unsigned i=0;i<join_watch.n;i++) join_watch.runtime[i]=join_watch_runtime(s,&mine,join_watch.portable[i]);   /* diagnostic */
    join_watch_dump('j',s->gate.tick);
    if(s->version>=13 && s->njobe) {   /* version 13: the job engine's schedule (main thread, phase 3: its update does not run) */
        unsigned char *e=join_jobe_engine();join_jobe_rec own;
        if(!e || !join_jobe_read(e,&own)) ++join_rx.jobe_missing;
        else {
            const join_jobe_rec *r=&s->jobe[0];
            join_rx.jobe_prev_last=own.last;join_rx.jobe_prev_len=own.len;
            memcpy(e+0x10,&r->last,8);e[8]=(r->flags&JOIN_JOBE_ENABLED)?1:0;e[9]=(r->flags&JOIN_JOBE_RUNNING)?1:0;
            *join_jobe_switch()=(r->flags&JOIN_JOBE_GATE)?1:0;
            if(join_jobe_assign(e+0x18,r->cursor,r->len)) ++join_rx.jobe_applied;else ++join_rx.jobe_missing;
        }
    }
    if(s->version>=4) {join_admit_region(s,&mine);join_admit_path(s,strict);}
    if(!join_admit_created(s,&mine)) {join_tables_free(&mine);return 0;}
    if(s->version>=13) join_admit_noct(s,&mine);   /* the nav octree's index table and hidden keys */
    if(s->version>=13) join_admit_agtt(s,&mine);   /* the Agents' times the load resets */
    if(s->version>=13) join_admit_ares(s,&mine);   /* the Agents' result banks (opt-in) */
    if(s->version>=13) join_admit_fobj(s,&mine);   /* the region fields' spawned-object lists (opt-in) */
    if(s->version>=13) join_admit_srcs(s,&mine);   /* the listeners' sources (opt-in), before their order */
    if(s->version>=13) join_admit_lstn(s,&mine);   /* the script listeners' order (opt-in) */
    if(s->version>=13) join_admit_agcb(s,&mine);   /* the pilot slot ships' command and probe blocks */
    if(s->version>=13) join_admit_pbnk(s,&mine);   /* the pilot slot ships' pathing state banks */
    if(s->version>=13) join_admit_specdrop(s,&mine);   /* the load's one-shot view cache specifications, after SPEC and VIEWK (opt-in) */
    if(s->version>=13) join_admit_pend(s,&mine);   /* the pending view cache flags (opt-in) */
    if(s->version>=13) join_admit_ldc(s,&mine);    /* the drift controllers, before the bodies they set */
    if(s->version>=13) join_admit_body(s,&mine);   /* last: the bodies of the objects as admitted */
    if(s->version>=13) for(uint32_t i=0;i<s->nbupd;i++) {   /* BUPD: the session's last build updates */
        unsigned char *object=join_object(join_runtime_of(&mine,s->bupd[i].portable));uintptr_t vtable=0;uint64_t own=0;
        if(!object) {++join_bupd_missing;continue;}
        if(!join_read(object,&vtable,8) || !join_bupd_class(vtable) || !join_read(object+JOIN_BUPD_FIELD,&own,8)) {++join_bupd_shape;continue;}
        if(own==s->bupd[i].last) {++join_bupd_equal;continue;}
        memcpy(object+JOIN_BUPD_FIELD,&s->bupd[i].last,8);++join_bupd_written;
    }
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
    if(strict && (join_rx.solver_missing || join_rx.solver_shape || join_rx.solver_kind))
        return join_fail_rx(JOIN_WHY_MISMATCH,join_rx.solver_missing?"world:solver_missing":join_rx.solver_shape?"world:solver_shape":"world:solver_kind");
    if(strict && (join_rx.grav_missing || join_rx.grav_kind || join_rx.grav_shape || join_rx.gravs_missing))
        return join_fail_rx(JOIN_WHY_MISMATCH,join_rx.grav_missing?"world:grav_missing":join_rx.grav_kind?"world:grav_kind":
                            join_rx.grav_shape?"world:grav_shape":"world:gravs_missing");
    if(strict && (join_rx.splm_missing || join_rx.splm_kind || join_rx.splm_shape || join_rx.splm_alloc || join_rx.splm_other))
        return join_fail_rx(JOIN_WHY_MISMATCH,join_rx.splm_missing?"world:splm_missing":join_rx.splm_kind?"world:splm_kind":
                            join_rx.splm_shape?"world:splm_shape":join_rx.splm_alloc?"world:splm_alloc":"world:splm_other");
    if(strict && (join_rx.gravq_busy || join_rx.gravq_alloc || join_rx.gravq_unmapped || join_rx.gravq_other))
        return join_fail_rx(JOIN_WHY_MISMATCH,join_rx.gravq_busy?"world:gravq_busy":join_rx.gravq_alloc?"world:gravq_alloc":
                            join_rx.gravq_unmapped?"world:gravq_unmapped":"world:gravq_other");
    return 1;
}
