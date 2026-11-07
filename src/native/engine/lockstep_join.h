/* Hot join, DEFAULT OFF: the join save, its side table and the gate state on a running client, and their
 * restore on a joiner (reports/LOCKSTEP-HOTJOIN-20261101.txt; relay side tools/dev/native_gate_join.py).
 *
 * Why a side table. X4's loader does not rebuild the world that saved: AIDirector_Import re-evaluates the
 * duration of every blocked AI Wait (lockstep_ai_wait_rng.h) and Cue_ResumeAfterLoad 0x1115960 schedules
 * every waiting cue again from the loading clock: state 1 (condition checks) at
 * max(now+1 ms, min(activation, now+checkinterval)), state 2 (pending delay) at activation + a re-drawn
 * delay (lockstep_md_resume_rng.h). The two keyed load modules make two loaders agree with each other, not
 * a loader with a session that has run since. So the running clients write what the loader drops, at the
 * same step boundary as the save, and the joiner puts it back right after its load:
 *
 *  - AI timed wakes. AIDirector *(0x6cf15f0): +0x1c0 the timed wake tree (an intrusive red-black tree:
 *    header node at +0x1c0 with root +0x8, free list +0x10, leftmost +0x20, size +0x28; node parent +0,
 *    left +8, right +0x10, colour +0x18, key double +0x20, context +0x28; equal keys insert to the right,
 *    so ties keep insertion order); +0x200/+0x218 the two wake vectors {begin, end, capacity}, +0x230 the
 *    index RequestWake 0x106f910 appends to. Move 0x106d250 toggles the index each pass and drains the
 *    other vector until a pass finds nothing, so after Move both are empty and only wakes requested later
 *    in the frame wait there (J1 build 177: 0 and 0 at T, as expected). Every AIScriptContext is on the global
 *    intrusive list AI::g_AIScriptContextList: head 0x6cf2808 (the first context), tail 0x6cf2de0, next
 *    +0xb8, prev +0xb0 (appended by the constructor 0x10631b0, unlinked by the destructor 0x1063410).
 *    The director's +0x148 is NOT that list: a short vector of contexts flagged by the import and the
 *    script refresh (AIDirector_RemoveContext 0x106f710 drops a finished one), normally empty; J1 build 177 read
 *    it and captured 0 contexts while the tree held 17,904. Context: +0x120 entity, +0xd8 script (+0x58
 *    version), +0x188 wake mask, +0x18c reason, +0x190 due (-1 none), +0x1a8 its tree node (the header
 *    when none), +0x1b0/+0x1b8 its index in vector 0/1 (-1 none). AIDirector::Move wakes every node
 *    whose key <= the TLS game time, in tree order. Captured: every node in tree order, both vectors in
 *    order, and the key of every context on the list. Restored: the joiner's tree is emptied through the
 *    engine's own erase 0xb4b810 and refilled in the captured order through its insert 0x10c85d0
 *    (SetTimedWake's calls), contexts found by key; the vectors are rewritten in place in the captured order.
 *  - MD scheduler. *(0x6cf1828)+0x40..+0x48: a binary min-heap of 24-byte entries {double time, uint32
 *    seq, uint32 kind 1..5, Cue*} ordered by (time, seq); seq comes from the global counter 0x38f52a4
 *    (MDScheduler_Schedule 0x1122ae0); a cancelled entry has no cue. Pop order depends only on (time,
 *    seq). Captured: every entry's cue key, kind, time and seq, and the counter. Restored: each joiner
 *    entry gets its time and seq back, the array is re-sorted (a sorted array is a valid heap), the
 *    counter is set.
 *  - MD::Cue (vtable 0x2c75170; Cue_CreateInstance 0x1111a40, the static constructors, the MD import
 *    0x11121d0): +0x150 its MD script (a static cue; 0 in an instance), +0x158 its parent (a static cue's
 *    static parent; a nested instance's parent instance), +0x160 its static cue (a static cue: itself),
 *    +0x168 a root instance's creator (the instantiating cue that made it; 0 otherwise), +0x170 its root
 *    instance (a root: itself; a static cue: 0), +0x180 state, +0x190 its sub-cues by static slot (an
 *    instance's mirror its static cue's: FUN_111dbb0), +0x1a8 the root instances it created
 *    (Cue_CheckConditions 0x111c290 and Cue_Force append; Cue_Destroy 0x1113900 moves the last one into a
 *    removed one's place; the save 0x111a260 writes them in order, the import rebuilds them in that
 *    order), +0x1c0 a static cue's every live instance (creation or load order: not portable), +0x1f8
 *    the activation time. A nested instance (a sub-cue of an instance) is in no +0x1a8 list: the old key,
 *    which looked every instance up in its static cue's +0x1a8, refused at the first heap entry (J1 build 177).
 *    A static cue's own name is the std::string at +0xc8 (the XML constructor 0x1111170 copies the name attribute,
 *    the MD import 0x11121d0 the saved one, a library user's sub-cue copy 0x1111dd0 its library cue's, the import's
 *    second pass 0x1118d70 gives a copy saved without a name its library cue's); every static cue of a script has
 *    that script at +0x150 (a copy its parent's), whose name is the std::string at +0x18 (constructors 0x110dff0,
 *    0x110e180). The script's name map (+0x60, "name" -> Cue*) is NOT a usable key: lockstep_cue_name.h walked it in
 *    key order with a budget of 8,192 steps (~2,700 entries), and the large story scripts hold 2,900-4,400 entries
 *    (named cues and libraries plus a "<user>.<sub>" entry per library user's sub-cue copy), so every cue named past
 *    that point read as unnamed: J1 j1-7 refused at heap entry 179 with a static cue four levels below a root
 *    (Start/Setup/... of a Story_* script, every level past the budget). The key does not read the map; its path
 *    (lockstep_cue_name.h cue_path_with) is now every module's cue name too.
 *  - Identities. A save writes runtime IDs; the loader maps them to new ones. The running clients'
 *    portable keys (saved IDs of the boot snapshot, created IDs, lockstep_world_identity.h) are written
 *    for every runtime ID; the joiner's import observer records the session's portable key for each
 *    imported object instead of the save's ID (join_import), so every keyed module draws as the session.
 *  - Gate state: the session tick T (the joiner counts session ticks from T: stats.issued = T at its
 *    admission), game and real clock bits (restored when the loaded clock differs by under 1 ms), the
 *    runtime ID allocator's next ID (native_gate_runtime_ids set mode: the joiner's head skips to it).
 *
 * Context key: (owner class, owner portable ID, script name FNV-1a, script version). Cue key: (name, root,
 * ordinal), name = the static cue's path key: FNV-1a of "<script>.<root>.<...>.<cue>", the script's name and
 * the own name (+0xc8) of every static cue from its root (parent 0) down to it (a nameless one below the root,
 * which the loader does not make, by "#<slot>" in its parent's sub-cue slots). The loader refuses unnamed and
 * duplicate cues, so roots and siblings have distinct names and the path names every static cue. A static
 * cue: root 0, ordinal 0. An instance: name = its static cue's path key, root = the key of its root instance R
 * (+0x170): the key of R's creator C (+0x168; a static C by its path key, an instance C by its own cue key)
 * mixed with R's index among the non-empty entries of C's +0x1a8; ordinal = 0 for R
 * itself, else a hash of the sub-cue slots (+0x190) from R down to the instance through the parents
 * (+0x158). Within one root's tree a static cue has at most one instance, so (name, root, ordinal) names
 * every cue. Keys that repeat with different captured values are ambiguous: strict mode refuses them,
 * report mode counts them. A refusal names its record: the check that failed and the raw fields
 * (join_fault, in the JOIN_RESULT detail and the JOIN_STATUS rx_detail).
 * Pairing at the joiner's admission: (name, root, ordinal, kind, state), NOT the activation (+0x1f8). The save writes
 * a cue's time with six significant digits (time="28.383", "1.06667"), so a loaded activation is the session's rounded
 * (J1 j1-8: md_applied 243 = exactly the heap entries whose activation survives that rounding, md_missing 11,987 = the
 * rest). A paired cue gets the session's activation bits back (md_activation_fixed); one further off than the save's
 * rounding counts md_activation_far (strict refuses). Unpaired entries are classified by the first key part that has
 * no counterpart (md_diff=name:root:ordinal:kind_state) and sampled with every key part (gate.join_unpaired rows: the
 * session's from the package, the joiner's with the static path, creator, created index and sub-cue slots).
 *
 * The join package (<save folder>/mpjoin/j<T>.x4join; format in native_gate_join.py): header, sections
 * GATE, IDS, WAKE, WAKEVEC, MDHEAP, MODULES (the join status of every armed module), CTX, LUA and, from version 2
 * (J1 j1-11), the world side table HEALTH and POSE (lockstep_join_world.h: what the save rounds or omits in the
 * census health and motion domains), each with its SHA-256, and a SHA-256 trailer. The portable digest
 * (client-independent lines of CTX, WAKE, WAKEVEC and MDHEAP without seq, the fixture views, one line per world
 * section) goes into the SAVED record: every saver must announce the same one. A version 1 package is still read
 * (a strict joiner refuses it: package_v1).
 *
 * Windows save at the boundary (v1, a pause of the whole session for the save's length): the gate's
 * FrameTick hook after step T (main thread, MoveWorker joined) calls GameClass::Save 0x9f0dd0(game
 * 0x3d196f0, &file, &name, online 0), as its only caller U::UniverseFrameUpdate does at 0x9fe7ca, with the
 * MoveWorker's TLS clock, view and RNG word copied in for the call; file "mpjoin\j<T>" lands in the save
 * folder's subfolder mpjoin (created first), which the game's save list does not show. No engine save may
 * be pending (countdown 0x3d198a8); the one-time 'gameModified' byte 0x31c20c7 is cleared (counted).
 * Every present client saves at T (the SAVE_AT mask), so the save's own side effects (released cues,
 * pathing results) happen on every client alike. A failed save never stops the session: the client
 * announces FAILED and the relay refuses the join.
 *
 * Joiner: gate.join_load(package, sha, mode) before LoadGame verifies every hash and arms the import
 * mapping; at the admission (boot phase 3) join_admit restores clocks, tree, vectors, heap, counter,
 * allocator head and the world side table (hulls, shield groups, static poses), compares the context keys, and
 * sets the tick base. Strict mode (the default) stops with
 * JOIN_STOP_REASON on any difference; report mode applies what matches and reports every difference
 * (J1 diagnostics). Until the handover tick (gate.join_handover), the joining slot's blob in a permit is
 * the relay's stand-in, not this client's proposal: join_own skips that comparison for those sources.
 * Not covered (the J1 census measures them): attention levels and dues and nav deadlines taken from the loading
 * frames, a flying ship's navigation state (saved velocities rounded), Gravidar round-robin cursors, listener
 * registration order of event-driven cues, the loaded objects' runtime IDs, and every module listed as unaudited in
 * MODULES. */
#define JOIN_STOP_REASON 200u
#define JOIN_AI_DIRECTOR 0x6cf15f0u
#define JOIN_MD_SCHEDULER 0x6cf1828u
#define JOIN_MD_SEQ 0x38f52a4u
#define JOIN_TREE_INSERT 0x10c85d0u
#define JOIN_TREE_ERASE 0xb4b810u
#define JOIN_CTX_VTABLE 0x2c6a270u
#define JOIN_SAVE 0x9f0dd0u
#define JOIN_SAVE_POSSIBLE 0x9f2510u
#define JOIN_SAVE_GAME 0x3d196f0u
#define JOIN_SAVE_COUNTDOWN 0x3d198a8u
#define JOIN_SAVE_MODIFIED 0x31c20c7u
#define JOIN_CTX_LIST 0x6cf2808u        /* AI::g_AIScriptContextList head: the first AIScriptContext */
#define JOIN_CTX_TAIL 0x6cf2de0u        /* its tail: the last one */
#define JOIN_CTX_PREV 0xb0u
#define JOIN_CTX_NEXT 0xb8u
#define JOIN_CUE_VTABLE CUE_PATH_VT     /* MD::Cue::vftable 0x2c75170 (SCRIPT_RNG_CUE_VT) */
#define JOIN_DIR_TREE 0x1c0u
#define JOIN_TREE_ROOT 0x08u
#define JOIN_TREE_FIRST 0x20u
#define JOIN_TREE_SIZE 0x28u
#define JOIN_DIR_VEC 0x200u
#define JOIN_DIR_VEC_INDEX 0x230u
#define JOIN_NODE_KEY 0x20u
#define JOIN_NODE_CTX 0x28u
#define JOIN_CTX_SCRIPT 0xd8u
#define JOIN_CTX_ENTITY 0x120u
#define JOIN_CTX_MASK 0x188u
#define JOIN_CTX_REASON 0x18cu
#define JOIN_CTX_DUE 0x190u
#define JOIN_CTX_NODE 0x1a8u
#define JOIN_CTX_VEC 0x1b0u
#define JOIN_MD_HEAP 0x40u
#define JOIN_MD_RELEASE 0x58u
#define JOIN_MD_QUEUED 0xa0u
/* The static cue's path fields are lockstep_cue_name.h's (one implementation with the modules' cue names). */
#define JOIN_CUE_NAME CUE_PATH_NAME           /* 0xc8, a static cue's own name (std::string) */
#define JOIN_CUE_SCRIPT CUE_PATH_SCRIPT       /* 0x150, a static cue's MD script (0 in an instance) */
#define JOIN_SCRIPT_NAME CUE_PATH_SCRIPT_NAME /* 0x18, the MD script's name (std::string) */
#define JOIN_CUE_PARENT CUE_PATH_PARENT       /* 0x158 */
#define JOIN_CUE_DEF CUE_PATH_DEF             /* 0x160 */
#define JOIN_CUE_CREATOR 0x168u
#define JOIN_CUE_ROOT 0x170u
#define JOIN_CUE_STATE 0x180u
#define JOIN_CUE_CHILDREN CUE_PATH_SLOTS      /* 0x190 */
#define JOIN_CUE_INSTANCES 0x1a8u        /* the root instances this cue created, in the session's (saved) order */
#define JOIN_CUE_ACTIVATION 0x1f8u
#define JOIN_CUE_DEPTH 64u               /* parents from a nested cue up to its root instance, a static cue up to its root */
#define JOIN_CUE_CREATORS 8u             /* creators from a root instance up to a static cue */
#define JOIN_CUE_LIST_MAX 1000000u
#define JOIN_MAX_RECORDS 4000000u
#define JOIN_IDMAP_BITS 21u
enum {JOIN_SEC_GATE=1,JOIN_SEC_IDS,JOIN_SEC_WAKE,JOIN_SEC_WAKEVEC,JOIN_SEC_MDHEAP,JOIN_SEC_MODULES,JOIN_SEC_CTX,JOIN_SEC_LUA,
      JOIN_SEC_HEALTH,JOIN_SEC_POSE,JOIN_SEC_SHIELD,JOIN_SEC_NAV,JOIN_SEC_ATTN,JOIN_SEC_KEYS,JOIN_SEC_PATH,JOIN_SEC_REGION,JOIN_SEC_FLIGHT,JOIN_SEC_SPLINE,
      JOIN_SEC_AGENT,JOIN_SEC_ROUTE,JOIN_SEC_PARAM,JOIN_SEC_DOCK,JOIN_SEC_STEER,JOIN_SEC_STEERPT,JOIN_SEC_SOLVER,JOIN_SEC_BULLET,
      JOIN_SEC_GRAV,JOIN_SEC_GRAVC,JOIN_SEC_GRAVS,JOIN_SEC_SPLM,JOIN_SEC_SPLME,JOIN_SEC_SPLMSEG,JOIN_SEC_GRAVN,JOIN_SEC_GRAVP,JOIN_SEC_GRAVQ,JOIN_SEC_JOBE,JOIN_SEC_EVQT,JOIN_SEC_GRAVD,JOIN_SEC_VIEWK,JOIN_SEC_VIEWKC,JOIN_SEC_VIEWF,JOIN_SEC_VIEWP,JOIN_SEC_SHOOT,JOIN_SEC_ATTK,JOIN_SEC_LIVEF,JOIN_SEC_SPEC,JOIN_SEC_NAVC,JOIN_SEC_JOBS,JOIN_SEC_GONE,JOIN_SEC_BODY,JOIN_SEC_BUPD,JOIN_SEC_LDC,JOIN_SEC_NAVT,JOIN_SEC_NMESH,JOIN_SEC_NOCT,JOIN_SEC_AGTT,JOIN_SEC_AGCB,JOIN_SEC_PBNK,JOIN_SEC_PEND,JOIN_SEC_GRAVB,JOIN_SEC_VIEWL,JOIN_SEC_IKV,JOIN_SEC_RCNT,JOIN_SEC_ECOS,JOIN_SEC_SHPW,JOIN_SEC_TRDN,JOIN_SEC_NPCA,JOIN_SEC_JOBG,JOIN_SEC_CODE,JOIN_SEC_ENGR,JOIN_SEC_SHGR,JOIN_SEC_BNDS,JOIN_SEC_OMGS,JOIN_SEC_RESV,JOIN_SEC_PCSV};
/* Package versions: 1 (sections GATE..LUA), 2 (and the world side table HEALTH, POSE: lockstep_join_world.h), 3 (and SHIELD,
 * NAV, ATTN, KEYS: the shield groups' other census fields, flying ships' navigation state, every object's attention level and
 * due, the gate's first-call key tables; J1 j1-12, report 1.7), 4 (and PATH, REGION: the pathing manager's agent batches and
 * the hazard regions' damage field timers; J1 j1-14, report 1.8), 5 (and FLIGHT, SPLINE: every navigation Agent's flight control
 * model, the linear model's leg and integration clock and its leg spline; J1 j1-15, report 1.9), 6 (and AGENT: the Agent inputs
 * of that step; J1 j1-20, report 1.10), 7 (and ROUTE: the Agents' waypoint and pending waypoint entries, their reference IDs
 * and the model's SetState word; J1 j1-21, report 1.11), 8 (and PARAM, DOCK: the Agents' three flight parameter slots, the
 * turn rates the linear step and the Agent update read among them, and the dock and undock controllers' state; J1 j1-22,
 * report 1.12), 9 (and STEER, STEERPT: the steering flight control models' state, their behaviours and path points; J1 j1-25,
 * report 1.13), 10 (and SOLVER, BULLET: the Agents' motion solvers, and a census of the projectiles in flight at T; J1 j1-27, report 1.14),
 * 11 (and GRAV, GRAVC, GRAVS: the radar, every Gravidar unit's state and contacts, the worker's active sources and the sectors'
 * factor-pass deadlines, which the save writes as an empty <gravidar/>; J1 j1-31, report 1.15), 12 (and SPLM, SPLME, SPLMSEG: the spline
 * flight control models, which the save writes as an empty <flightcontrolmodel type="spline"/>; J1 j1-27, report 1.14, 1.16; and GRAVN:
 * the Gravidar zone groups' neighbour lists, which X4 builds when a zone is added: j1-37).
 * A version 11, 10, 9, 8, 7, 6, 5, 4, 3, 2 or 1 package is still read; a strict joiner refuses it (package_v11 ... package_v1). */
#define JOIN_PACKAGE_VERSION 13u
#define JOIN_SECTIONS(version) ((version)>=13?75u:(version)>=12?35u:(version)>=11?29u:(version)>=10?26u:(version)>=9?24u:(version)>=8?22u:(version)>=7?20u:(version)>=6?19u:(version)>=5?18u:(version)>=4?16u:(version)>=3?14u:\
                                (version)>=2?10u:8u)
/* FAILED/ABORT reasons, shared with native_gate_join.py REASONS and native_gate_join_core.lua. */
enum {JOIN_WHY_NONE,JOIN_WHY_NOT_ARMED,JOIN_WHY_NOT_POSSIBLE,JOIN_WHY_SAVE_FAILED,JOIN_WHY_FILE_MISSING,JOIN_WHY_CAPTURE,
      JOIN_WHY_UNSUPPORTED,JOIN_WHY_PACKAGE_WRITE,JOIN_WHY_BUSY,JOIN_WHY_TIMEOUT,JOIN_WHY_MISMATCH};
enum {JOIN_IDLE,JOIN_ARMED,JOIN_DONE,JOIN_FAILED};
enum {JOIN_STRICT=0,JOIN_REPORT=1};
typedef struct {uint64_t owner,script;uint32_t klass,version;} join_ctx_rec;
typedef struct {uint64_t owner,script,due;uint32_t klass,version,reason,flags;} join_wake_rec;
typedef struct {uint64_t owner,script;uint32_t klass,version,vector,position,mask,pad;} join_vec_rec;
typedef struct {uint64_t name,root,time,activation;uint32_t seq,kind,ordinal,state;} join_md_rec;
typedef struct {uint64_t runtime,portable;uint32_t klass,pad;} join_id_rec;
/* The world side table (package v2, lockstep_join_world.h): a destructible object's hull and its shield group's value and
 * changed time (maximum and generator count: shape), a pure-transform controller's pose lanes. Sorted by portable ID. */
typedef struct {uint64_t portable,hull,value,changed,maximum;uint32_t flags,count;} join_health_rec;
typedef struct {uint64_t portable;uint32_t kind,form;uint32_t xyz[12];} join_pose_rec;
enum {JOIN_HEALTH_SHIELD=1u,JOIN_HEALTH_FULL=2u};
/* Version 3 (lockstep_join_world.h; report 1.7), every section sorted by portable ID (KEYS by table, then key):
 * SHIELD  a destructible object's shield group's other census fields (delay +0x48, rate +0x28, factor +0x50, the modifiers'
 *         three factors and whether it has them +0xa0): the census hashes them with value and changed time.
 * NAV     a navigation controller (vtable 0x2c51158): the controller's refresh time +0xc0 and its cursor +0xc8 as the census
 *         reads it (relation to the read selector 0x38f4ee4: 0 the read bank, 1 the write bank, 2 none), the parameter due
 *         +0xd0 and enable +0xd8, the obstacle probe's due and enable ((+0x48)+0x3e0/+0x3e8), and both pathing state banks by
 *         role (0 the bank the read selector names, 1 the other): six vectors' x, y, z lanes, two references and the pending
 *         entry (Agent+0xed0 + 16 x bank) as portable IDs, the transition count, mode +0xcc, eta +0x150, arrived/notified
 *         +0x159/+0x15a, layer +0x15b. flags: a reference without a portable ID (bit 3 x role + 0/1 the bank's, 2 pending).
 * ATTN    every present object of a known class: its attention level +0x1ec and, for an Object (class 73), the attention
 *         timer's due +0x2e0 (flags 1).
 * KEYS    the gate's insert-only first-call tables, so that the joiner's first admitted call is no first call where the
 *         session's was not: 1 attention_all (lockstep_attention.h), 2 the nav probe init, 3 the nav parameter init
 *         (lockstep_nav_cadence.h), 4 and 5 the snapshot fixture actors' attention and navigation first refresh (actor = the
 *         fixture index + 1, key = its saved ID). */
typedef struct {uint64_t portable,delay;uint32_t rate,factor,mods[3],flags;} join_shield_rec;
enum {JOIN_SHIELD_MODS_PRESENT=1u};
typedef struct {
    uint32_t xyz[18];uint64_t ref[2],eta,pending_ref,pending_count;uint32_t mode;uint8_t arrived,notified,layer,pending_state;
} join_nav_bank;
typedef struct {
    uint64_t portable,refresh,params_due,probe_due;uint32_t relation;uint8_t params_on,probe_on,flags,pad;join_nav_bank bank[2];
} join_nav_rec;
typedef struct {uint64_t portable,due;uint32_t level,flags;} join_attn_rec;
enum {JOIN_ATTN_DUE=1u};
typedef struct {uint64_t key;uint32_t table,actor;} join_key_rec;
enum {JOIN_KEYS_ATTENTION_ALL=1u,JOIN_KEYS_NAV_PROBE,JOIN_KEYS_NAV_PARAMS,JOIN_KEYS_ATTENTION_ACTOR,JOIN_KEYS_NAV_ACTOR,JOIN_KEYS_TABLES};
_Static_assert(sizeof(join_shield_rec)==40 && sizeof(join_nav_bank)==120 && sizeof(join_nav_rec)==280 && sizeof(join_attn_rec)==24 &&
               sizeof(join_key_rec)==16,"join v3 record layout (native_gate_join.py)");
/* Version 4 (lockstep_join_world.h; report 1.8):
 * PATH    the pathing manager (*0x6cf21c8) as RunPass 0xeae3c0 will find it at T+1, sorted by (kind, vector, batch, position):
 *         kind 0 one record per batch vector (0: +0x180, 1: +0x198): batch = its number of batches, value = AssignAgentToBatch
 *                0xeaf2b0's round-robin index (+0x1b8, +0x1c0);
 *         kind 1 one per batch: value = its member count, position = its cursor's index in list order (the count: the cursor
 *                is the list's sentinel), flags = the batch's flag byte (+0: its first 100 agents always get a full update);
 *         kind 2 one per member in list order from the sentinel: portable = the portable ID of its Agent's owner (Agent+0xef0);
 *         kind 3 one per Agent on the new-agent list (+0x170: CreateAgent 0xeac530 appends, the next pass assigns them).
 *         flags 1 on a member or new Agent: its owner has no portable ID (portable 0; the joiner cannot place it).
 * REGION  every hazard region's damage field (U::Region vtable 0x2b2ad60, its field vector +0x288; U::Regions::DamageField
 *         vtable 0x2c4b2f0): the region's portable ID, the field's index in the vector, its last pass time +0x10f0 and its next
 *         pass time +0x10f8 (lockstep_region_rng.h), sorted by (region, field). */
typedef struct {uint64_t portable;uint32_t kind,vector,batch,position,value,flags;} join_path_rec;
enum {JOIN_PATH_KIND_MANAGER=0u,JOIN_PATH_KIND_BATCH=1u,JOIN_PATH_KIND_MEMBER=2u,JOIN_PATH_KIND_PENDING=3u};
enum {JOIN_PATH_LOCAL=1u};
typedef struct {uint64_t region,last,due;uint32_t field,flags;} join_region_rec;
_Static_assert(sizeof(join_path_rec)==32 && sizeof(join_region_rec)==32,"join v4 record layout (native_gate_join.py)");
/* Version 5 (lockstep_join_world.h; report 1.9):
 * FLIGHT  every navigation Agent's flight control model (Agent +0xf10; none: no record), sorted by the owner's portable ID.
 *         kind = the model's vtable RVA. A U::Path::LinearFlightControlModel (0x2c4f610, model type 3; 9,997 of j633's 10,010)
 *         carries what its step (vfunc14 0xeb27c0) integrates from, the bytes of JOIN_FLIGHT_RANGES in that order: the target
 *         state +0x20..+0x5f (SetState, vfunc3 0xeaf620); +0x68..+0xdf, the leg (its flags +0x6c..+0x6f, start position +0x70,
 *         start time +0x80, the time of the last step +0x88: the step's dt is now - +0x88 unless +0x88 is -1, progress +0x90,
 *         the new-leg flag +0x94 and +0x95..+0x97, the speeds +0x98..+0xa7, length +0xa8, the leg's basis +0xb0..+0xdf); the
 *         leg spline's (Math::CompositeSpline<5> at +0xe0) segment scale +0xe8 and length, minimum and maximum +0x120..+0x12b;
 *         the spline flags +0x130..+0x133. segments and pairs: the spline's two vectors' lengths (+0xf0: 0xa0-byte Bezier
 *         segments, +0x108: 8-byte parameter pairs). flags: 1 another model (Steering 0x2c4e9c8, Spline 0x2c4f480, Highway
 *         0x2c4f6f0, MassTraffic 0x2c4f8d0: kind only, no state); 2 the word +0x60 was not 0 (not carried); 4 a spline this
 *         record cannot carry (a segment that is no BezierSpline, more than JOIN_FLIGHT_SEGMENTS or JOIN_FLIGHT_PAIRS).
 * SPLINE  the linear models' spline vectors, sorted by (portable, part, index): part 0 one record per Bezier segment (its 0xa0
 *         bytes; the segment's BezierSpline vtable at +0x10 written as 0), part 1 the parameter pairs, 20 per record (the last
 *         one's unused bytes 0). */
#define JOIN_FLIGHT_STATE 204u
#define JOIN_FLIGHT_SEGMENTS 64u
#define JOIN_FLIGHT_PAIRS 128u
#define JOIN_FLIGHT_SEGMENT 0xa0u
#define JOIN_FLIGHT_PAIRS_PER 20u
enum {JOIN_FLIGHT_OTHER=1u,JOIN_FLIGHT_WORD=2u,JOIN_FLIGHT_UNCARRIED=4u};
typedef struct {uint64_t portable;uint32_t kind,flags,segments,pairs;unsigned char state[JOIN_FLIGHT_STATE];uint32_t reserved;} join_flight_rec;
typedef struct {uint64_t portable;uint32_t part,index;unsigned char data[JOIN_FLIGHT_SEGMENT];} join_spline_rec;
_Static_assert(sizeof(join_flight_rec)==232 && sizeof(join_spline_rec)==176,"join v5 record layout (native_gate_join.py)");
/* Version 6 (lockstep_join_world.h; report 1.10):
 * AGENT   one record per FLIGHT record (same portable IDs, same order): the model's Agent as the linear step and the Agent
 *         update read it around the model. value: the 32-bit words at JOIN_AGENT_VALUE_AT (accelerations +0x340..+0x34c,
 *         angular rates +0x358..+0x360, the speed limits +0x364/+0x36c, the throttle rates +0x38c/+0x390, the arrival distance
 *         +0x4c8, the formation speed +0x4d8, +0x504, the turn rates +0x518..+0x520, the pending factor +0xf34); flag: the bytes
 *         at JOIN_AGENT_FLAG_AT (the modes +0x4d0..+0x4d3, the re-target flag +0xf33, +0xf5e, +0xf5f, +0xf77, +0xf79, +0xf7a;
 *         the last two 0); waypoints and pending: the lengths of the waypoint vectors +0x6a8 and +0x6c0 (0x90-byte entries);
 *         members: the length of the member vector of the group the Agent leads (+0xf18; 0 none); formation: 1 when it follows
 *         one (+0xf20). */
#define JOIN_AGENT_VALUES 18u
#define JOIN_AGENT_FLAGS 12u
typedef struct {
    uint64_t portable;uint32_t value[JOIN_AGENT_VALUES];unsigned char flag[JOIN_AGENT_FLAGS];uint16_t waypoints,pending,members,formation;uint32_t reserved;
} join_agent_rec;
_Static_assert(sizeof(join_agent_rec)==104,"join v6 record layout (native_gate_join.py)");
/* Version 7 (lockstep_join_world.h; report 1.11):
 * ROUTE   per AGENT record (same order) one head, then its Agent's waypoint entries (U::Path::FlightPoint, 0x90 bytes, in the
 *         vector +0x6a8) and pending waypoint entries (+0x6c0), each in vector order: records in (portable, kind, list, index)
 *         order. ref: portable IDs of runtime IDs (0 none; flags bit i: slot i's runtime ID had no portable ID).
 *         head  ref[0] the linear model's SetState word +0x60 (flags JOIN_ROUTE_LINEAR), ref[1..4] the Agent's +0xef8, +0xf08,
 *               +0xf48, +0xf60; data: the two lengths (u16 each), the bytes +0xf5c, +0xf5d, +0xf74, +0xf75, +0xf76, +0xf7b, two 0,
 *               the word +0xf70. flags JOIN_ROUTE_OTHER: an entry the record cannot carry (no FlightPoint, a sub-point no
 *               NavPoint, more than JOIN_ROUTE_POINTS in a vector): no points follow.
 *         point list 0 the waypoints, 1 the pending ones; index its place; ref[0] its reference +0x10, ref[1] its sub-point's
 *               (+0x68, a NavPoint) +0x10; data: +0x20..+0x67 (transform, key +0x60, behaviour +0x64), +0x70..+0x8f (+0x7f, padding,
 *               as 0), the sub-point's +0x20..+0x5f (flags JOIN_ROUTE_SUB; else 0). JOIN_ROUTE_CONN, _SUB_CONN: a connection
 *               pointer +0x18 that is not 0 (not carried). */
#define JOIN_ROUTE_REFS 5u
#define JOIN_ROUTE_DATA 168u
#define JOIN_ROUTE_POINTS 64u
enum {JOIN_ROUTE_HEAD=0u,JOIN_ROUTE_POINT=1u};
enum {JOIN_ROUTE_UNMAPPED=0x1fu,JOIN_ROUTE_SUB=0x20u,JOIN_ROUTE_CONN=0x40u,JOIN_ROUTE_SUB_CONN=0x80u,JOIN_ROUTE_OTHER=0x100u,
      JOIN_ROUTE_LINEAR=0x200u};
typedef struct {
    uint64_t portable;uint32_t kind;uint16_t list,index;uint32_t flags,reserved;uint64_t ref[JOIN_ROUTE_REFS];unsigned char data[JOIN_ROUTE_DATA];
} join_route_rec;
_Static_assert(sizeof(join_route_rec)==232,"join v7 record layout (native_gate_join.py)");
/* Version 8 (lockstep_join_world.h; report 1.12):
 * PARAM   one record per AGENT record (same portable IDs, same order): the Agent's three flight parameter slots (0x1a0 bytes
 *         each) by role: slot 0 the read slot (Agent + 0x1a0 x g_PathingStateBufferIndex), 1 the write slot (Agent + 0x1a0 x
 *         g_PathingStateBufferIndex2), 2 the Agent's own copy +0x340, which the linear step and the Agent update read (AGENT's
 *         words are part of it; the turn rates +0x378..+0x380 are not: J1 j1-22). Each slot block, JOIN_PARAM_SLOT bytes: the
 *         flight parameter object +0x000..+0x13f (built on a cleared buffer, 0x153730), the vector +0x160..+0x16f, the runtime
 *         ID +0x170 as a portable ID (flags bit role: it had none), the refresh time +0x178, +0x180..+0x19b, four 0. +0x140..
 *         +0x15f (the Agent update's scratch, cleared after each update) is not carried.
 * DOCK    every positional object whose controller is a DockMovementController (0x2c3fdc0) or an UndockMovementController
 *         (0x2c3e568), sorted by portable ID: kind its vtable RVA, the start time +0x60, the animation selector (dock +0x120,
 *         undock +0xc0), the option bytes (dock +0x124 and +0x125, undock +0xc4 and 0), the x, y, z lanes of its vectors (dock
 *         14: +0x20..+0x5f and +0x80..+0x11f; undock 8: +0x20..+0x5f and +0x80..+0xbf; the rest 0): what the census hashes of it
 *         (lockstep_dock.h). */
#define JOIN_PARAM_ROLES 3u
#define JOIN_PARAM_SLOT 0x180u
#define JOIN_DOCK_VECTORS 14u
typedef struct {uint64_t portable;uint32_t flags,reserved;unsigned char slot[JOIN_PARAM_ROLES][JOIN_PARAM_SLOT];} join_param_rec;
typedef struct {
    uint64_t portable;uint32_t kind,flags;uint64_t time;uint32_t animation;unsigned char option[2];uint16_t reserved;uint32_t xyz[JOIN_DOCK_VECTORS*3];
} join_dock_rec;
_Static_assert(sizeof(join_param_rec)==1168 && sizeof(join_dock_rec)==200,"join v8 record layout (native_gate_join.py)");
/* Version 9 (lockstep_join_world.h; report 1.13):
 * STEER   every navigation Agent's U::Path::SteeringFlightControlModel (vtable 0x2c4e9c8; FLIGHT holds its kind only), sorted by its
 *         owner's portable ID: model the bytes of JOIN_STEER_RANGES in that order (the x, y, z lanes of the 16-byte vectors, every
 *         byte of the scalar ranges), the rest 0; ref[0] the target's runtime ID +0x60 and ref[1] the ID +0x80 as portable IDs
 *         (flags bit 0, 1: that ID had none); behaviour[0] the active behaviour +0x70, behaviour[1] the default one +0x78, each its
 *         vtable RVA (0 none), state[b] its carried bytes (JOIN_STEER_CLASSES; flags JOIN_STEER_UNCARRIED0 << b: a class this build
 *         does not carry, kind only, state 0); points the length of the path vector +0x360 (0x90-byte U::Path::FlightPoints, in
 *         STEERPT; JOIN_STEER_POINTS: more than JOIN_ROUTE_POINTS or an entry the record cannot carry, none in STEERPT); contacts
 *         the length of the avoidance cache +0x90 (0x80-byte contacts, count only: the cache is a copy of the obstacle publisher's
 *         list, not carried); JOIN_STEER_JOB: an async path job was pending (+0x388, not carried).
 * STEERPT the path points of those models as ROUTE's points (join_route_rec, kind JOIN_ROUTE_POINT, list 2), sorted by (portable,
 *         index). */
#define JOIN_STEER_MODEL 592u
#define JOIN_STEER_STATE 96u
enum {JOIN_STEER_UNMAPPED=3u,JOIN_STEER_UNCARRIED0=4u,JOIN_STEER_UNCARRIED1=8u,JOIN_STEER_POINTS=16u,JOIN_STEER_JOB=32u};
typedef struct {
    uint64_t portable;uint32_t flags,kind;uint64_t ref[2];uint32_t behaviour[2];uint16_t points,contacts;uint32_t reserved;
    unsigned char model[JOIN_STEER_MODEL];unsigned char state[2][JOIN_STEER_STATE];
} join_steer_rec;
_Static_assert(sizeof(join_steer_rec)==832,"join v9 record layout (native_gate_join.py)");
/* Version 10 (lockstep_join_world.h; report 1.14):
 * SOLVER  every navigation Agent's motion solver (Agent +0xf80, in no save; the engine allocates it at the Agent's first steering or
 *         spline step), sorted by its owner's portable ID: kind the Agent's model at T (vtable RVA), data the object's bytes with
 *         every qword that points into the object as its offset and every one that points into the image as its RVA, pointer[] those
 *         qwords in offset order ((offset/8)<<1 | 1 for the image), the rest 0. JOIN_SOLVER_ACTIVE: the model is a steering or spline
 *         model; a linear model's Agent keeps a dormant solver of an earlier fight, at most JOIN_SOLVER_DORMANT of them (the rest
 *         counted in world_cap's last but one field). JOIN_SOLVER_OVERFLOW: more such qwords than the list holds (data raw, not applied).
 *         Only (portable, kind) enter the portable digest in this version: the constructor leaves 24,126 bytes unset, so two savers'
 *         bytes differ there; 'native_gate_join.py compare' names the offsets, and the digest takes the bytes when the mask is known.
 * BULLET  a census of the projectiles in flight at T: the joiner writes nothing. Every live U::Bullet of the component registry (j1-28,
 *         j1-29: bullets are in no zone's children vector), sorted by (zone, position): zone the portable ID of its parent (+0x70; or
 *         JOIN_BULLET_ZONE_RUNTIME | its runtime ID when it has none: temp zones), position its registry slot, shooter the portable ID of the
 *         component +0x2a0 (flags bit 0: it had none or one without a portable ID), index its place among the zone's bullets, motion_kind the movement controller's vtable RVA (flags bit 2: none), macro its macro's name (flags bit 1: not
 *         readable), bullet and motion the object's and the controller's bytes with pointers into themselves or the image
 *         normalized as in SOLVER (a heap pointer stays raw: compare names those offsets). Not in the portable digest. */
#define JOIN_SOLVER_BYTES 0x18f60u
#define JOIN_SOLVER_POINTERS 256u
#define JOIN_SOLVER_DORMANT 256u
#define JOIN_BULLET_BYTES 0x330u
#define JOIN_BULLET_MOTION 0xb0u
#define JOIN_BULLET_MACRO 64u
enum {JOIN_SOLVER_ACTIVE=1u,JOIN_SOLVER_OVERFLOW=2u};
enum {JOIN_BULLET_NO_SHOOTER=1u,JOIN_BULLET_NO_MACRO=2u,JOIN_BULLET_NO_MOTION=4u};
#define JOIN_BULLET_ZONE_RUNTIME (1ull<<63)   /* BULLET zone: a zone without a portable ID, keyed by its runtime ID (j1-28: none found by IDS zones) */
typedef struct {
    uint64_t portable;uint32_t flags,kind,pointers,reserved;uint32_t pointer[JOIN_SOLVER_POINTERS];unsigned char data[JOIN_SOLVER_BYTES];
} join_solver_rec;
typedef struct {
    uint64_t zone,shooter;uint32_t position,index,flags,motion_kind;char macro[JOIN_BULLET_MACRO];
    unsigned char bullet[JOIN_BULLET_BYTES];unsigned char motion[JOIN_BULLET_MOTION];
} join_bullet_rec;
_Static_assert(sizeof(join_solver_rec)==103288 && sizeof(join_bullet_rec)==1088,"join v10 record layout (native_gate_join.py)");
/* Version 11 (lockstep_join_world.h; report 1.15). X4's save writes every radar unit as an empty <gravidar/>: a loaded unit has no
 * contacts and its times are -1 (the constructor 0x10ac0c0), so its first scans post GravidarHasScannedEvent where the running
 * session posts nothing, and the AI interrupts on it run apart from the first tick (J1 j1-31's script rows).
 * GRAV   every Gravidar unit of the table *0x6cf1650 (256 buckets, lock +0x88+b*0x28, tree +0x2890+b*0x40: AI::Gravidar lookup
 *        0x10b2dc0) whose owner (the unit's +8) has a portable ID, sorted by it: kind the vtable RVA (AI::ActiveGravidarUnit 0x2c67610,
 *        0xc8 bytes; AI::GravidarUnit 0x2c67660, 0x68 bytes), count[] the four contact lists' lengths (+0x68 and +0x80 vectors, the
 *        vectors *+0x98 and *+0xa0; active units only), unit the unit's bytes with the vtable, the owner's ID, +0x10, the lock
 *        +0x18..+0x3f and the lists +0x68..+0xa7 as 0 (+0x10, the radar group's object, as its portable ID). The joiner writes
 *        +0x10, +0x40..+0x5f (+0x54 and +0x58 the live and player view states), +0x60..+0x67 and (active) the times
 *        +0xa8..+0xc7 (last scan, next due, last has_scanned post, +0xc0, the flags +0xc4).
 * GRAVC  the contacts, (owner, list, index) in the session's order (its runtime IDs): the contact's portable ID, 0 with
 *        JOIN_GRAVC_UNMAPPED. The joiner writes each list in its own runtime IDs, sorted as the engine keeps them.
 * GRAVS  the worker's active sources (kind 0: +0x48 pending, 1: +0x60 current; components, in order; portable 0 with
 *        JOIN_GRAVS_UNMAPPED) and every field sector's factor-pass deadline (kind 2: the sector of the worker's list +0x68b8 whose
 *        +0x442 is set, value its +0x448 bits; JOIN_GRAVS_SEEN: native_gate_gravidar_deadlines had admitted its first Move, so the
 *        joiner records it as seen and its first Move does not set the deadline to 0, lockstep_gravidar_deadlines.h), and (kind 3,
 *        once, portable 0) the Gravidar thread's round-robin sector cursor as native_gate_ai_schedule expects it at T+1 (value; index
 *        the sector list's length): j1-34, the joiner's cursor 0 scanned other sector windows than the session from T+1 on
 *        (lockstep_ai_schedule.h; the 1 table's "Gravidar round-robin cursor ... NOT covered"). */
#define JOIN_GRAV_BYTES 0xc8u
#define JOIN_GRAV_LISTS 4u
enum {JOIN_GRAV_ACTIVE=1u,JOIN_GRAV_UNCARRIED=2u};
enum {JOIN_GRAVC_UNMAPPED=1u};
enum {JOIN_GRAVS_PENDING=0u,JOIN_GRAVS_CURRENT=1u,JOIN_GRAVS_SECTOR=2u,JOIN_GRAVS_CURSOR=3u};
enum {JOIN_GRAVS_UNMAPPED=1u,JOIN_GRAVS_SEEN=2u};
typedef struct {uint64_t portable;uint32_t kind,flags;uint32_t count[JOIN_GRAV_LISTS];unsigned char unit[JOIN_GRAV_BYTES];} join_grav_rec;
typedef struct {uint64_t portable;uint32_t list,index;uint64_t contact;uint32_t flags,reserved;} join_gravc_rec;
typedef struct {uint64_t portable,value;uint32_t kind,index,flags,reserved;} join_gravs_rec;
_Static_assert(sizeof(join_grav_rec)==232 && sizeof(join_gravc_rec)==32 && sizeof(join_gravs_rec)==32,"join v11 record layout (native_gate_join.py)");
/* Version 12 (lockstep_join_world.h; report 1.16). A U::Path::SplineFlightControlModel (0x2c4f480, 0x4c0 bytes; constructor 0xeb4f40, step
 * vfunc14 0xeb7230) flies a route: two std::deques of 0x50-byte entries (+0x70 the route in flight, +0x98 the provisional one; {proxy, map,
 * map size, offset, count}, one entry per block of the engine's allocator), each entry owning a heap Math::CompositeSpline<5> (0x50 bytes:
 * vtable, scale +8, Bezier segments +0x10 and parameter pairs +0x28 as vectors, length/min/max +0x40), two embedded CompositeSpline<5>
 * (+0x158, +0x1a8) and a request deque +0x428 the pathing calculator answers. The save keeps none of it: the joiner's model has no route
 * and brakes (state +0x218 = 8; j1-27: two scouts lose 1.4% of their speed at T+1, fixture actor 2 stops).
 * SPLM     one record per spline model (FLIGHT names its kind), sorted by portable ID: entries[] the two deques' counts, requests the
 *          request deque's (JOIN_SPLM_REQUESTS: not carried), segments[] and pairs[] the embedded splines', model the object's bytes with
 *          the vtable, +8, the owner +0x10, +0x18, both deques, both splines' vtables and vectors, +0x210, +0x268, +0x418 and the request
 *          deque as 0 (join_splm_skip); JOIN_SPLM_UNCARRIED: a route the record cannot hold (no SPLME, SPLMSEG).
 * SPLME    one record per deque entry, (portable, list 0/1, index): entry the entry's bytes with its spline pointer 0, spline the owned
 *          CompositeSpline<5>'s bytes with vtable and vectors 0 (JOIN_SPLME_SPLINE: the entry owns one), its segment and pair counts.
 * SPLMSEG  the splines' segments and pairs as SPLINE's records (join_spline_rec): part = list*2 + kind (list 0, 1 the embedded splines,
 *          2, 3 the deques' entries; kind 0 a 0xa0-byte segment with its BezierSpline vtable 0, 1 twenty parameter pairs), index =
 *          element << 16 | k, in that order per model. */
#define JOIN_SPLM_BYTES 0x4c0u
#define JOIN_SPLM_ENTRIES 64u
enum {JOIN_SPLM_REQUESTS=1u,JOIN_SPLM_UNCARRIED=2u};
enum {JOIN_SPLME_SPLINE=1u};
typedef struct {
    uint64_t portable;uint32_t flags,entries[2],requests,segments[2],pairs[2],reserved[2];unsigned char model[JOIN_SPLM_BYTES];
} join_splm_rec;
typedef struct {uint64_t portable;uint32_t list,index,flags,segments,pairs,reserved;unsigned char entry[0x50],spline[0x50];} join_splme_rec;
/* GRAVN    the Gravidar zone groups' neighbour lists (lockstep_gravidar_wide.h: each sector entry of the worker's list +0x68b8 holds
 *          one 0x58-byte group per zone, its +0x38 a vector of neighbour group indices that Update 0x10b1810 fills when a zone is
 *          added, from the zone's spatial query; a scan pairs a group's units only with its neighbours'). J1 j1-37: the same unit at
 *          the same tick found other contacts on the joiner (40 km radar, contacts at 26..39 km in zones the session's scan never
 *          gathered): the session's lists grew zone by zone, the joiner's were built at its load. One record per (sector, zone,
 *          neighbour), sorted; flags 1: the zone without a portable key (0), 2: the neighbour without one (0); a group without
 *          neighbours has one record with neighbour 0 and flag 4. */
enum {JOIN_GRAVN_ZONE_UNMAPPED=1u,JOIN_GRAVN_NEIGHBOUR_UNMAPPED=2u,JOIN_GRAVN_NONE=4u};
typedef struct {uint64_t sector,zone,neighbour;uint32_t flags,reserved;} join_gravn_rec;
/* GRAVP    the published component view cache poses the radar reads (J1 j1-37..40: the same unit at the same tick found other contacts
 *          on the joiner; ship 8cecc's scan entry 6.6 km apart). Off the MoveWorker, 0x3dde10 composes each link from the view cache
 *          (object+0xe0+view*0x80; +0x28 a pointer to the published 0x40-byte transform, position then three rows; NULL: the link's
 *          own transform at the thread's time), which an out-of-sector object republishes rarely: the session's are the poses of
 *          their last publication, the joiner's those of its load. For every Gravidar unit's owner and its ancestors below the
 *          sector, both views relative to the current one (g_ComponentViewCacheIndex 0x38f4eb4): rel 0 the current view, 1 the other;
 *          flags 1: a pose was published (pose its bytes). Sorted by (portable, rel). */
enum {JOIN_GRAVP_PRESENT=1u};
typedef struct {uint64_t portable;uint32_t rel,flags;unsigned char pose[0x40];} join_gravp_rec;
/* GRAVQ (report 1.16): the Gravidar worker's private command list at T (+0x69b0, a std::list: the pass of T posted its
 * GravidarHasScannedEvents there and the publish of T+1 splices them into U::g_EventCommandQueue). In list order (index): the source
 * as a portable ID (0 with UNMAPPED), the node's kind (3: post an event), the event's vtable RVA, the post time (bits), the event's two
 * qwords. OTHER: not a has_scanned post the joiner can rebuild (another kind or event, a payload beyond them). */
enum {JOIN_GRAVQ_UNMAPPED=1u,JOIN_GRAVQ_OTHER=2u};
typedef struct {uint64_t portable;uint32_t index,kind,flags,event;uint64_t time,q[2];} join_gravq_rec;
/* JOBE (version 13, report 1.17): the job engine's schedule at T (U::JobEngine *0x6cf2300; its update 0xf0b870 runs when the game time is 5 s past
 * its last run +0x10, processes the untimed job after its round-robin cursor (the job ID std::string at +0x18) and every timed job;
 * the save keeps neither, so a loaded game runs a full pass at once). At most one record: the last run (double bits), flags ENABLED
 * (+8) and RUNNING (+9), the cursor's length and bytes (0 behind them). */
enum {JOIN_JOBE_ENABLED=1u,JOIN_JOBE_RUNNING=2u,JOIN_JOBE_GATE=4u};   /* GATE: the global job switch 0x2f4b6de */
#define JOIN_JOBE_CURSOR 112u
typedef struct {uint64_t last;uint32_t flags,len;char cursor[JOIN_JOBE_CURSOR];} join_jobe_rec;
/* EVQT (version 13, report 1.17): the events waiting in their sources at T (U::EventSource::PostEvent's tree per source, the sources in
 * U::g_EventSourceSchedule). The save keeps them with four decimals, but a load schedules its own: j1-50, 9,838 of 10,011
 * UpdateEngineParametersEvents up to 8 s apart, 1,457 UpdateBuildEvents of the joiner's own. One record per event: the source as a
 * portable ID (0 with UNMAPPED), the class (vtable RVA), its type ID (slot 1), its place among the source's events, the time (bits);
 * sorted by (portable, class, time, place). */
enum {JOIN_EVQT_UNMAPPED=1u};
typedef struct {uint64_t portable;uint32_t cls,type,index,flags;uint64_t time;} join_evqt_rec;
/* GRAVD (version 13, report 1.17): every ship's cached parent in both views of the component view cache (object+0xe0+view*0x80, +0):
 * the radar's gather drops objects whose cached parent is an internal dock (StaticObjectCheckerByIsCachedDockedInternally, Ship slot
 * 1005), and j1-62's joiner added two docked ships at T+1 that the session's scan left out. The ship and its parent as portable IDs,
 * the view relative to the current one; NONE: no cached parent, UNMAPPED: a parent without a portable ID. Sorted by (ship, view). */
enum {JOIN_GRAVD_NONE=1u,JOIN_GRAVD_UNMAPPED=2u};
typedef struct {uint64_t portable,parent;uint32_t rel,flags;} join_gravd_rec;
/* VIEWK, VIEWKC (version 13, report 1.18): the component view cache the session left unpublished. The radar's gather reaches an object
 * only through its ancestors' published children (the view block object+0xe0+view*0x80: children vector +0x10/+0x18/+0x20, flags
 * +0x3c). On the session a docking bay whose ship was built in it after the start keeps its view flag 2 (children changed) pending and
 * its published children empty (j1-65: so its station's radar never sees the ship); the joiner's load publishes every view, and its
 * radar adds the ship at T+1. VIEWK: every component with a pending view flag (1, 2 or 4) in either view, both views relative to the
 * current one: the view flags and the children count; UNMAPPED: a child without a portable ID, UNREAD: the vector unreadable. VIEWKC:
 * the children in the view's order, (component, view, place). The joiner writes the flags and, when the session's children are among
 * its own, the children in the session's order and count (it never adds a pointer the view did not hold). Not in the portable digest. */
#define JOIN_VIEWK_PENDING 0x7u
#define JOIN_VIEWK_CHILDREN 65536u
#define JOIN_VIEWK_MAX 400000u
enum {JOIN_VIEWK_UNMAPPED=1u,JOIN_VIEWK_UNREAD=2u};
typedef struct {uint64_t portable;uint32_t rel,flags,vflags,count;} join_viewk_rec;
typedef struct {uint64_t portable,child;uint32_t rel,index;} join_viewkc_rec;
/* VIEWF (version 13, report 1.18): every component's view flags (view block +0x3c), both views relative to the current one. j1-71: a
 * ship the session has not republished for a while keeps its stale pose in both views with clean flags (0x10058); the joiner's load
 * leaves 0x40000 and 1 pending in its current view (0x40059), so its first frame republishes the live pose (0x91d40: 2.8 km from the
 * session's), and its radar at T+2 gains or loses that contact. j1-74: the view's published attention (+0x38) too: the load's
 * current view holds 1 where the session published 6, and without its pending 0x40 the joiner never republishes it (ship 0x6f256:
 * no per-frame publication, no navigation refresh at T+1). j1-75: writing the attention as well changes nothing there: the session
 * publishes 0x6f256 at T+1 because its two views differ at T (a publication in flight, the second view still to take the pose),
 * and the joiner's own pending bits did that too. j1-77 (settled views only, JOIN_VIEWF_INFLIGHT: the two views differ in flags,
 * attention or pose; the top bit of view 1's attention word) kept 0x6f256's own words and still broke its refresh: its zone 0x72b
 * (attention 6, settled 0x110000) got the session's words. The engine publishes what has high attention every frame and the rest on
 * demand, so the joiner keeps its own words where the session's published attention is JOIN_VIEWF_KEEP or more and writes the
 * session's for the rest (424,253 of 426,203 components at attention 1: the stale poses). j1-78: the 180 refreshes still broke
 * with their ships, zones and sectors kept, so the joiner writes only ships and stations (the radar's contacts) of low attention.
 * j1-79/80: that keeps the refreshes (T+1 4) but not the radar, with zones and sectors as well: the ship's parts' pending bits
 * republish it (j1-72, every component: 0x91d40 republished a frame later). j1-81 (governed by the nearest ship's attention): 900
 * 3,878, partial writes leave parents and children apart. The publication goes down the hierarchy: the joiner keeps its own words
 * on every component the session had in flight and on all its ancestors, and writes the session's everywhere else.
 * Sorted by portable ID; not in the portable digest. */
#define JOIN_VIEWF_INFLIGHT 0x80000000u
#define JOIN_VIEWF_KEEP 3u
typedef struct {uint64_t portable;uint32_t flags[2],attention[2];} join_viewf_rec;
/* VIEWP (version 13, report 1.18): the settled published pose (both views equal) of every component whose cached parent is a zone,
 * beyond GRAVP's radar owners: the session keeps a low-attention object's pose stale, the joiner's load publishes it live (j1-85).
 * The joiner writes it into both views at the admission and restores it after T+1 and T+2 with GRAVP's settled ones. Sorted by
 * portable ID; not in the portable digest. */
typedef struct {uint64_t portable;unsigned char pose[0x40];} join_viewp_rec;
/* SHOOT (version 13, report 1.18): every weapon's shoot controller (weapon +0x318, the U::BasicShootController family) and the weapon's
 * own shot state, none of which the save keeps but its last shot time to four decimals [F: j1-93 JOIN_WATCHRAW of the fixture weapons
 * 0x176b42 and 0x176b44]. The weapon's request check 0x816d20 casts a line of fire at most once a second (controller +0x40 its time,
 * +0x84 clear, +0x85 blocked) and fires only while it is clear; the session's check of 28.149667 was clear, the joiner's own check at
 * its load (28.183) came out blocked, so the joiner held fire until its next check at 29.183 (tick 663: event_weapon_fired 27 ticks
 * after the session's). Doubles as their bits: the weapon's last shot time (+0x310); the controller's time it last allowed a shot
 * (+0x28), of the line-of-fire check (+0x40), the aim refresh interval (+0x48), the aim refresh (+0x50), +0x58; its cached aim
 * (+0x60..+0x7f), its vtable RVA (the joiner's must be the same), the weapon's +0x30c..+0x30e (+0x30d shooting: Weapon::Move runs)
 * and the controller's +0x84, +0x85; has_ctl 0 (a weapon without a controller: its last shot time only, the rest 0). Every weapon of
 * the family with a controller, and those whose last shot time the save's four decimals change; the last shot time is written where
 * the joiner's is the save's four decimals of it. Sorted by portable ID; not in the portable digest. */
/* tol (j1-165): the controller's +0x80, its aim cone (float bits; 0 without a controller). The weapon's request check 0x816d20
 * goes on to its line of fire when the angle to the target (acos of the aim direction's z, 0x8176dd) is within it; the session's
 * controllers made at its boot hold 0x3db2b8c2 (5 degrees), the joiner's made at its load 0x3db2b8c7, five units in the last place
 * more: actor 5's weapon 0x176ba3 checked and fired at 650 on the joiner only. */
typedef struct {uint64_t portable,lastshot,ctl[5];unsigned char aim[0x20];uint32_t ctl_vt;uint8_t wflags[3],cflags[2],has_ctl,pad[2];uint32_t tol;} join_shoot_rec;
/* ATTK (version 13, report 1.18): a destructible's attack throttle at full precision. The poster 0x51ff10 posts an attacked event
 * only through 0x51fd80: another attacker (+0x298 its runtime ID), another method (+0x2a8) or a second after the last (+0x2b0
 * attacktime; +0x2b8 shipattacktime, an attacking ship's; +0x2c0 intentionalattacktime); the save keeps them to four decimals
 * [F: j1-94: 0x82a6b's attacktime 27.349667 saved 27.3497, so the attack of 638 (28.349667) passes on the session and is throttled on
 * the joiner; event_object_attacked of 638 missing, 668 and 698 equal]. The attacker as portable key (0 none or unmapped), the times
 * as their bits, the method. Every destructible (78 classes by vtable) with a time the save's four decimals change (-1 is unset);
 * the joiner writes a time where its own is the save's four decimals of it and the attacker and method agree. Sorted by portable
 * ID; not in the portable digest. */
typedef struct {uint64_t portable,attacker,times[3];uint32_t method,pad;} join_attk_rec;
/* LIVEF (version 13, report 1.18): live publication flags (+0x1e4) the session holds stale. Bit 0x20 is set where Positional slot
 * 646 is false (its +0x68 0, its macro's +0x48 -> +0x1c0 and its +0x208 present: true) at the object's setup (Connect 0x3d7010,
 * Destructible::vfunc8 0x516ae0) and kept until the next; the publication 0x3d46b0 then clears the view's 0x20 after one transform,
 * so an object with the bit clear keeps republishing its transform and one with it set does not. The session keeps the bit of the
 * object's setup, the joiner's load computes it afresh [F: j1-97: 0x8a4b6 (+0x208 0 on both) clear on the session, set on the
 * joiner; the session republishes its moving pose at 636, the joiner keeps the old one]. Per object of the 116 classes whose slot
 * 646 is Positional's own, where the session's bit differs from that computation: the bits to set and to clear. Sorted by
 * portable ID; not in the portable digest. */
typedef struct {uint64_t portable;uint32_t set,clear;} join_livef_rec;
#define JOIN_LIVEF_BITS 0x20u   /* the transform publication's "static" bit */
/* SPEC (version 13, report 1.18): the view cache specification entries (component +0xd8: a small vector of 0x20-byte entries, four
 * inline, +0x80 capacity, +0x88 count; an entry kind +0, flags +4, truncation class +8, truncated +0xc, interval +0x10, last +0x18).
 * BuildCache 0x3d4a80 publishes an entry's flags into its subtree when its interval has passed since last; AddCacheSpecification
 * 0x3d5180 starts an entry with an interval at a random phase (last = now - Random::FloatState(TLS+0x318, interval)), so the
 * joiner's load builds its entries at other phases [F: j1-98: the session republishes the moving 0x8a4b6 every five steps (636,
 * 641, 646), the joiner not at all through 647; its live transform equal (census 660), its published pose the one of T]. Per entry
 * of every component (one per kind): the joiner writes the session's entry into its own of the same kind. Sorted by (portable ID,
 * kind); not in the portable digest. */
typedef struct {uint64_t portable;uint32_t kind,flags,klass,trunc;uint64_t interval,last;} join_spec_rec;
/* NAVC (version 13, report 1.18): a navigation controller's cached transform (+0x60, a mat4: translation +0x60, rotation rows
 * +0x70 +0x80 +0x90) and velocity (+0xa0..+0xbf, SetVelocity 0xea5790), which NAV does not hold. GetTransformImpl 0xea5a90
 * returns the cache (or composes with it) when IsStateRelativeToParent 0xea3e10 is false (the read bank's +0xb0 is not the
 * parent); the joiner's load writes the absolute pose of the save into it through SetTransform 0xea5390 [F: j1-100 JOIN_WATCHRAW
 * mov=: 0x8a4b6's bank +0xb0 0, +0xb8 its zone; its cache (11958.9, -560.8, ..) on the session, the pose of T (4359.0, -689.5,
 * ..) on the joiner, its velocity block zero; the joiner keeps the published pose of T]. Per controller (its ship's portable ID)
 * the 0x60 bytes. Sorted by portable ID; not in the portable digest. */
typedef struct {uint64_t portable;unsigned char cache[0x60];} join_navc_rec;
/* JOBS (version 13, report 1.18): per job of the job DB with a state object *(job+0x528), the state's +0x48 [F: j1-108/109
 * JOIN_JOBRAW: job split_stationminer_m_solid (index 524) holds it 3 on the session, 2 after the joiner's load; its waiting vector
 * (+0x30) and its seven requests (ship, requester, timeout 1800, no time) are equal, and by 660 the session has built its waiting
 * ship 6 (0x1248cf), the joiner its ship 2 (0x124633)]. The job by the FNV-1a hash of its ID (the tree's std::string key, node
 * +0x20) and its place in the tree's order; sorted by place; not in the portable digest. */
/* JOBS: active and waiting, the lengths of the state's ship vectors (+0x18, +0x30; 65535 at most), in the former padding. */
typedef struct {uint64_t hash,value;uint32_t index;uint16_t active,waiting;} join_jobs_rec;
/* GONE (v13, section 49): a census row of the session whose object is gone at T (join_capture_gone). */
typedef struct {uint64_t portable;} join_gone_rec;
/* BODY (v13, section 50): a destructible object's physics body (lockstep_join_body.h): position and rotation, the motion properties'
 * first 92 bytes, the XPhys record's previous and delta transforms; pad 1 when the body moves (motion properties read). */
typedef struct {uint64_t portable;uint32_t which,flags;uint32_t pose[8],motion[23],pad,pre[8],delta[8];} join_body_rec;
/* BUPD (v13, section 51): a class-0x6f object's last build update (+0xa58, a game time; lockstep_join_world.h). */
typedef struct {uint64_t portable,last;} join_bupd_rec;
/* LDC (v13, section 52): a drifting object's LinearDecelerationController fields (start transform 0x40, start time 8,
 * velocities 0x20, duration 8, flag 1; lockstep_join_world.h), zero padding. */
#define JOIN_LDC_STATE 0x71u
typedef struct {uint64_t portable;unsigned char state[JOIN_LDC_STATE],pad[7];} join_ldc_rec;
/* NAVT (v13, section 53): a navigation controller's pathing state banks beyond NAV's six vectors: the x, y, z lanes of the
 * five vectors +0x60..+0xaf of each bank, by role as NAV (0 the bank the read selector names, 1 the other). */
#define JOIN_NAVT_VECTORS 5u
typedef struct {uint64_t portable;uint32_t xyz[2][JOIN_NAVT_VECTORS*3];} join_navt_rec;
/* NMESH (v13, section 54): the nav mesh receipt (lockstep_nav_mesh.h, 'sync'): record 0 the receipt, 1 its build count, then
 * every build term it folded, sorted, each once. None without 'sync'. */
typedef struct {uint64_t v;} join_nmesh_rec;
/* NOCT (v13, section 55): the nav octree's 'agreed' index table (lockstep_nav_octree.h): kind 0 first (a next index, b shown,
 * c agreed, d keys hash), then kind 1 per index not shown, by index (a index, b portable, c assigned tick, d live; flag,
 * agreed, shown), then kind 2 per hidden key (a due, b portable; flag). None without 'agreed'. */
enum {JOIN_NOCT_HEAD=0u,JOIN_NOCT_ENTRY=1u,JOIN_NOCT_HIDDEN=2u};
typedef struct {uint64_t a,b,c,d;uint32_t klass;uint8_t flag,kind,agreed,shown;} join_noct_rec;
static int join_noct_shape(const join_noct_rec *r,uint32_t n);   /* at join_admit_rid */
/* AGTT (v13, section 56): one record per AGENT record (same portable IDs, same order): the Agent's times the loader resets
 * (lockstep_join_world.h join_agtt_at: +0x658, +0xf40, +0xf38; the doubles' bits). */
#define JOIN_AGTT_FIELDS 3u
typedef struct {uint64_t portable,v[JOIN_AGTT_FIELDS];} join_agtt_rec;
/* AGCB (v13, section 57): a pilot slot ship's Agent's command and probe block (+0x6e0, 0x400 bytes, raw), by portable ID. */
#define JOIN_AGCB_AT 0x6e0u
#define JOIN_AGCB_BYTES 0x400u
typedef struct {uint64_t portable;unsigned char block[JOIN_AGCB_BYTES];} join_agcb_rec;
/* PBNK (v13, section 58): a pilot slot ship's Agent's two pathing state banks by role (0 the bank the read selector names):
 * NAV's fields per bank (join_nav_bank) and NAVT's five vectors; flags as NAV's (a reference without a portable ID). */
typedef struct {uint64_t portable;uint32_t flags,pad;join_nav_bank bank[2];uint32_t tail[2][JOIN_NAVT_VECTORS*3];} join_pbnk_rec;
/* PEND (v13, section 59): a component's pending view cache flags (+0x1e0, which BuildCache's child loop passes to
 * PropagateCacheFlags), the nonzero ones by portable ID. */
#define JOIN_PEND_AT 0x1e0u
typedef struct {uint64_t portable;uint32_t word,pad;} join_pend_rec;
/* GRAVB (v13, section 60): the Gravidar's mapped bounds maps by role (0 the read buffer, 1 the write buffer, 2 the base): a
 * sector by portable ID and its x, y, r (float bits). */
#define JOIN_GRAVB_ROLES 3u
typedef struct {uint64_t portable;uint32_t role,v[3];} join_gravb_rec;
/* VIEWL (v13, section 61): an object's live view and player view flags (+0x3c8, +0x3c9: set by its ObjectEntered/Left-
 * LiveView/PlayerView handlers), the objects with either set, by portable ID. */
typedef struct {uint64_t portable;unsigned char live,player,pad[6];} join_viewl_rec;
/* IKV (v13, section 62): the state of an object's skeleton IK joints (+0x208 -> +0x48: groups of 0x680-byte joints, U::IKValue their
 * virtual base at +0x678): per joint its group and index and the bytes +0x08..+0x1f (value, time, rate, target), by portable ID. */
#define JOIN_IKV_STATE 24u
#define JOIN_IKV_GROUPS 16u
#define JOIN_IKV_JOINTS 64u
typedef struct {uint64_t portable;uint16_t group,joint;uint32_t pad;unsigned char state[JOIN_IKV_STATE];} join_ikv_rec;
/* RCNT (v13, section 63): the reference count (+0x40) of every ship and docking bay the session created (portable IDs with the
 * top bit), by portable ID. */
typedef struct {uint64_t portable;uint32_t count,pad;} join_rcnt_rec;
/* ECOS (v13, section 64): every station's, build storage's and ship's economy schedule: its next economy run +0xa38 and its last
 * +0xa40 (lockstep_economy_load.h), by portable ID. */
typedef struct {uint64_t portable;double next,last;} join_ecos_rec;
/* SHPW (v13, section 65): words of every ship's object a load leaves at defaults (JOIN_SHPW_WORDS, lockstep_join_world.h). */
#define JOIN_SHPW_WORDS 6u
typedef struct {uint64_t portable;uint64_t word[JOIN_SHPW_WORDS];} join_shpw_rec;
/* TRDN (v13, section 66): a container's next trade offer update (+0x928, the time UpdateTradeOffers 0x4079e0 checks), by portable ID. */
typedef struct {uint64_t portable;double next;} join_trdn_rec;
/* NPCA (v13, section 67): an active NPC's activation (lockstep_join_world.h): flags (1 the active flag +0x370, 2 an animation state
 * +0x5a0), the animation's index (+0x1f0) and its bytes +0x5b0 .. +0x5e0 (the activation's stamps and its next idle time), by portable ID. */
#define JOIN_NPCA_BYTES 48u
typedef struct {uint64_t portable;uint32_t flags,index;unsigned char stamps[JOIN_NPCA_BYTES];} join_npca_rec;
/* JOBG (v13, section 68): a job's own generator word, *(job+0x528) (the state Generate 0xc841b0 draws its macro choice from and
 * the spawn's loadout key, lockstep_spawn_rng.h), by the FNV-1a hash of the job's ID (JOBS's walk); sorted by hash. */
typedef struct {uint64_t hash,word;} join_jobg_rec;
/* CODE (v13, section 69): X4's object code counter (the global 0x38f0ae8 0x6ea780 advances for every new ship or NPC code) and the
 * game's code seed (0x3d19860, its low 24 bits), at most one record (lockstep_join_world.h). */
typedef struct {uint32_t counter,seed;} join_code_rec;
/* ENGR (v13, section 70): every engine's boost ramp, by portable ID: +0x2f0 the time 0x54e3a0 last integrated the ramp and +0x2f8
 * the ramped boost factor (a float) with the word beside it (lockstep_join_world.h). */
typedef struct {uint64_t portable;double ramp_time;uint64_t ramp_word;} join_engr_rec;
/* SHGR (v13, section 71): the shield recharge groups a container keeps for its docked ships (+0x5d8: groups keyed by the ship's
 * runtime ID and a second key), by the container's and the ship's portable IDs: the group's value +0x68 and changed time +0x70
 * (lockstep_join_world.h). */
typedef struct {uint64_t container,ship;uint32_t key,pad;double value,changed;} join_shgr_rec;
/* BNDS (v13, section 72): every station's three bounding structs (+0xc90, +0xcc0, +0xcf0: centre, extents, radius; the words after
 * the radius are per process and not carried), by portable ID (lockstep_join_world.h). */
#define JOIN_BNDS_WORDS 27u
typedef struct {uint64_t portable;uint32_t word[JOIN_BNDS_WORDS],pad;} join_bnds_rec;
/* RESV (v13, section 74): every zone's space reservations (the tree at zone+0x3b0), by the zone's and the owner's portable IDs
 * and the reservation's index: its box node+0x30 .. +0x9f (raw) and its expiry node+0xa0 (lockstep_join_world.h). */
#define JOIN_RESV_BOX_BYTES 0x70u
typedef struct {uint64_t zone,owner,index;unsigned char box[JOIN_RESV_BOX_BYTES];double expiry;} join_resv_rec;
/* PCSV (v13, section 75): every PlayerController's own motion solver (the controller+0x3b0 .. +0x1930f, built by 0xd47ac0 like an
 * Agent's), by the ship's portable ID, in SOLVER's record (kind PSLOT_VTABLE; lockstep_join_world.h). */
/* OMGS (v13, section 73): each steering ship's two OmegaSwitched controllers (Agent+0xf80's +0xa0c8 and +0x16398), the object
 * from +0x10 to its end, by (portable ID, which) (lockstep_join_world.h). */
#define JOIN_OMGS_FROM 0x10u
#define JOIN_OMGS_SIZE 0x2248u
#define JOIN_OMGS_BYTES (JOIN_OMGS_SIZE-JOIN_OMGS_FROM)
typedef struct {uint64_t portable;uint32_t which,pad;unsigned char data[JOIN_OMGS_BYTES];} join_omgs_rec;
_Static_assert(sizeof(join_splm_rec)==1264 && sizeof(join_splme_rec)==192 && sizeof(join_gravn_rec)==32 && sizeof(join_gravp_rec)==80 &&
               sizeof(join_gravq_rec)==48 && sizeof(join_jobe_rec)==128 && sizeof(join_evqt_rec)==32 &&
               sizeof(join_gravd_rec)==24 && sizeof(join_viewk_rec)==24 && sizeof(join_viewkc_rec)==24 &&
               sizeof(join_viewf_rec)==24 && sizeof(join_viewp_rec)==72 && sizeof(join_shoot_rec)==104 && sizeof(join_attk_rec)==48 && sizeof(join_livef_rec)==16 && sizeof(join_spec_rec)==40 && sizeof(join_navc_rec)==104 && sizeof(join_jobs_rec)==24 && sizeof(join_gone_rec)==8 && sizeof(join_body_rec)==208 && sizeof(join_bupd_rec)==16 && sizeof(join_ldc_rec)==128 && sizeof(join_navt_rec)==128 && sizeof(join_nmesh_rec)==8 && sizeof(join_noct_rec)==40 && sizeof(join_agtt_rec)==32 && sizeof(join_agcb_rec)==1032 && sizeof(join_pbnk_rec)==376 && sizeof(join_pend_rec)==16 && sizeof(join_gravb_rec)==24 && sizeof(join_viewl_rec)==16 && sizeof(join_ikv_rec)==40 && sizeof(join_rcnt_rec)==16 && sizeof(join_ecos_rec)==24 && sizeof(join_shpw_rec)==56 && sizeof(join_trdn_rec)==16 && sizeof(join_npca_rec)==64 && sizeof(join_jobg_rec)==16 && sizeof(join_code_rec)==8 && sizeof(join_engr_rec)==24 && sizeof(join_shgr_rec)==40 && sizeof(join_bnds_rec)==120 && sizeof(join_omgs_rec)==8776 && sizeof(join_resv_rec)==144,
               "join v12 record layout (native_gate_join.py)");
typedef struct {
    uint64_t tick,game,real;uint32_t wire_version,clients,lookahead,tick_hz,slot,md_seq;uint64_t rid_next,created_generation;
    uint32_t created_live,identities,ctx_count,wake_count,vec_index,vec0,vec1,md_count,md_void,md_release,md_queued,unsupported;
    char digest[64];unsigned char portable[16];uint64_t save_us,capture_us;uint32_t ambiguous,flags;
    unsigned char fixture[12];   /* the session's view of each snapshot fixture actor at T (SNAPSHOT_VIEW_*; all 0: none bound) */
    unsigned char flight[12];    /* each fixture actor's flight sample init generation at T (lockstep_flight.h; 255 at most; 0 before) */
    uint64_t cid;                /* X4's construction ID counter at T (0x38f4dd0; 0: not carried) */
    uint64_t unpaused;           /* the unpaused clock's bits at T (0x6b60e48, native_gate_speech_clock's; 0: not carried) */
} join_gate_rec;
_Static_assert(sizeof(join_ctx_rec)==24 && sizeof(join_wake_rec)==40 && sizeof(join_vec_rec)==40 && sizeof(join_md_rec)==48 &&
               sizeof(join_id_rec)==24 && sizeof(join_gate_rec)==256 && sizeof(join_health_rec)==48 && sizeof(join_pose_rec)==64,
               "join record layout (native_gate_join.py)");
/* Everything captured at one boundary or read from one package. */
typedef struct {
    join_gate_rec gate;
    join_ctx_rec *ctx;uint32_t nctx;join_wake_rec *wake;uint32_t nwake;join_vec_rec *vec;uint32_t nvec;
    join_md_rec *md;uint32_t nmd;join_id_rec *ids;uint32_t nids;char *modules;size_t modules_len;uint32_t nmodules;
    char *lua;size_t lua_len;uint32_t nlua;   /* the probe's own session state (native_gate_join_core.lua providers) */
    join_health_rec *health;uint32_t nhealth;join_pose_rec *pose;uint32_t npose;   /* the world side table (version 2) */
    join_shield_rec *shield;uint32_t nshield;join_nav_rec *nav;uint32_t nnav;join_attn_rec *attn;uint32_t nattn;   /* version 3 */
    join_key_rec *keys;uint32_t nkeys;
    join_path_rec *path;uint32_t npath;join_region_rec *region;uint32_t nregion;   /* version 4 */
    join_flight_rec *flight;uint32_t nflight;join_spline_rec *spline;uint32_t nspline;   /* version 5 */
    join_agent_rec *agent;uint32_t nagent;                                             /* version 6 */
    join_route_rec *route;uint32_t nroute;                                             /* version 7 */
    join_param_rec *param;uint32_t nparam;join_dock_rec *dock;uint32_t ndock;          /* version 8 */
    join_steer_rec *steer;uint32_t nsteer;join_route_rec *steerpt;uint32_t nsteerpt;   /* version 9 */
    join_solver_rec *solver;uint32_t nsolver;join_bullet_rec *bullet;uint32_t nbullet; /* version 10 */
    join_grav_rec *grav;uint32_t ngrav;join_gravc_rec *gravc;uint32_t ngravc;join_gravs_rec *gravs;uint32_t ngravs;   /* version 11 */
    join_splm_rec *splm;uint32_t nsplm;join_splme_rec *splme;uint32_t nsplme;join_spline_rec *splmseg;uint32_t nsplmseg;   /* version 12 */
    join_gravn_rec *gravn;uint32_t ngravn;join_gravp_rec *gravp;uint32_t ngravp;join_gravq_rec *gravq;uint32_t ngravq;join_jobe_rec *jobe;uint32_t njobe;join_evqt_rec *evqt;uint32_t nevqt;join_gravd_rec *gravd;uint32_t ngravd;join_viewk_rec *viewk;uint32_t nviewk;join_viewkc_rec *viewkc;uint32_t nviewkc;join_viewf_rec *viewf;uint32_t nviewf;join_viewp_rec *viewp;uint32_t nviewp;join_shoot_rec *shoot;uint32_t nshoot;join_attk_rec *attk;uint32_t nattk;join_livef_rec *livef;uint32_t nlivef;join_spec_rec *spec;uint32_t nspec;join_navc_rec *navc;uint32_t nnavc;join_jobs_rec *jobs;uint32_t njobs;join_gone_rec *gone;uint32_t ngone;join_body_rec *body;uint32_t nbody;join_bupd_rec *bupd;uint32_t nbupd;join_ldc_rec *ldc;uint32_t nldc;join_navt_rec *navt;uint32_t nnavt;join_nmesh_rec *nmesh;uint32_t nnmesh;join_noct_rec *noct;uint32_t nnoct;join_agtt_rec *agtt;uint32_t nagtt;join_agcb_rec *agcb;uint32_t nagcb;join_pbnk_rec *pbnk;uint32_t npbnk;join_pend_rec *pend;uint32_t npend;join_gravb_rec *gravb;uint32_t ngravb;join_viewl_rec *viewl;uint32_t nviewl;join_ikv_rec *ikv;uint32_t nikv;join_rcnt_rec *rcnt;uint32_t nrcnt;join_ecos_rec *ecos;uint32_t necos;join_shpw_rec *shpw;uint32_t nshpw;join_trdn_rec *trdn;uint32_t ntrdn;join_npca_rec *npca;uint32_t nnpca;join_jobg_rec *jobg;uint32_t njobg;join_code_rec *code;uint32_t ncode;join_engr_rec *engr;uint32_t nengr;join_shgr_rec *shgr;uint32_t nshgr;join_bnds_rec *bnds;uint32_t nbnds;join_omgs_rec *omgs;uint32_t nomgs;join_resv_rec *resv;uint32_t nresv;join_solver_rec *pcsv;uint32_t npcsv;
    unsigned char save_sha[32];uint64_t save_bytes;uint32_t flags,version;
} join_tables;
/* The engine as the module sees it; tests build a synthetic one. */
typedef void *(*join_tree_insert_fn)(void *tree,void **out,const double *key,const uintptr_t *ctx);
typedef void *(*join_tree_erase_fn)(void *tree,void **out,void *node);
typedef struct {
    unsigned char *director,*scheduler;uint32_t *md_seq;uintptr_t ctx_vtable,cue_vtable;
    uintptr_t ctx_head,ctx_tail;       /* the addresses of the context list's head and tail pointers */
    join_tree_insert_fn insert;join_tree_erase_fn erase;
} join_engine;
typedef int (*join_reader)(const void *,void *,size_t);
static join_reader join_read=sample_read;
/* Portable key of a runtime ID (tests replace it). */
static uint64_t join_key_native(uint64_t runtime,unsigned *klass) {return world_identity_key(runtime,klass);}
static uint64_t (*join_key)(uint64_t,unsigned *)=join_key_native;
static uint64_t join_script_native(uintptr_t ctx) {char name[96];return ai_script((void *)ctx,name);}
static uint64_t (*join_script)(uintptr_t)=join_script_native;
/* The script's name text (diagnostic rows); 0 unreadable. */
static int join_script_name_native(uintptr_t ctx,char name[96]) {return ai_script((void *)ctx,name)!=0;}
static int (*join_script_name)(uintptr_t,char[96])=join_script_name_native;

/* ---- session side state ---- */
static struct {
    int state,mode;unsigned why,slot_k,mask;uint64_t tick;char name[24];WCHAR folder[MAX_PATH];uintptr_t mw_tls;
    unsigned char package_sha[32],portable[16],save_sha[32];uint64_t save_bytes,package_bytes,save_us,capture_us;
    unsigned modified_cleared,possible;char detail[320];char *lua;size_t lua_len;uint32_t nlua;unsigned char fixture[12];
    uint32_t health,pose,shield,nav,attn,keys,path,region,flight,spline,agent,route,param,dock,steer,steerpt,solver,bullet,grav,gravc,gravs,splm,splme,splmseg,gravn,gravp,gravq,jobe,evqt,gravd,viewk,viewkc,viewf,viewp,shoot,attk,livef,spec,navc,jobs,gone,body,bupd,ldc,navt,nmesh,noct,agtt,agcb,pbnk,pend,gravb,viewl,ikv,rcnt,ecos,shpw,trdn,npca,jobg,code,engr,shgr,bnds,omgs,resv,pcsv;   /* the world side table at the last capture */
    uint32_t solver_over,bullet_fault;                                         /* dormant solvers past the cap, bullets not readable */
    uint32_t solver_mapped;                                                    /* version 12: solver qwords that were addresses of this process */
    uint32_t gravq_global,gravq_busy;                                          /* GRAVQ: entries in the event queue at T (not carried), worker not idle */
    uint32_t evq_sources,evq_events,evq_due,evq_fault;char evq_top[640];        /* census: sources with timed events at T, events, due by T+1, top classes */
    uint32_t grav_unmapped,grav_fault,grav_passive;                          /* GRAV capture: units without a portable owner, faults, passive units */
    uint32_t bullet_blocks;uint64_t bullet_live,bullet_unkeyed;               /* BULLET's registry walk: blocks, live components, bullets keyed by runtime ID */
} join_cap;
/* ---- joiner side state ---- */
#define JOIN_ROWS 40u                    /* unpaired samples kept at the admission (gate.join_unpaired; the probe reads 40) */
#define JOIN_UNMAPPED_SET 4096u          /* runtime IDs of imported objects the package names no portable ID for */
#define JOIN_ROW_SIDE 4u                 /* per kind (md, ctx) and side (session, joiner) */
static struct {
    int loaded,admitted,based,mode,skip_dt;join_tables t;uint64_t handover;
    join_id_rec *idmap;uint32_t idmap_mask;
    volatile LONG64 imported,mapped_saved,mapped_created,unmapped,created_failures,record_failures;
    uint64_t ctx_missing,ctx_extra,ctx_ambiguous,wake_applied,wake_missing,wake_erased,vec_applied,vec_missing,mask_cleared,md_applied,md_missing,md_extra,md_dropped,
        md_ambiguous,md_activation_fixed,md_activation_far,md_diff[4],clock_fixed,rid_skipped;double clock_delta;unsigned why;char detail[320];
    uint64_t health_applied,health_hull_fixed,health_shield_fixed,health_missing,health_shape,health_conflict,pose_applied,pose_fixed,pose_missing,
        pose_shape;
    /* version 3 (lockstep_join_world.h join_admit_world): */
    uint64_t shield_applied,shield_fixed,shield_mods,shield_missing,shield_shape,shield_conflict;
    uint64_t nav_applied,nav_fixed,nav_missing,nav_shape,nav_unmapped,nav_field[10];
    uint64_t attn_applied,attn_level_fixed,attn_due_fixed,attn_missing,attn_shape;
    uint64_t keys_applied[JOIN_KEYS_TABLES],keys_known,keys_unarmed,keys_actor,keys_full;
    uint64_t created_extra,created_extra_loaded;
    /* version 4 (lockstep_join_world.h join_admit_path, join_admit_region): */
    uint64_t path_members,path_pending,path_missing,path_local,path_extra,path_shape,path_primed;
    uint64_t region_applied,region_fixed,region_missing,region_shape;
    /* version 5 (lockstep_join_world.h join_admit_flight): */
    uint64_t flight_applied,flight_fixed,flight_spline,flight_missing,flight_kind,flight_other,flight_shape,flight_alloc,flight_word;
    /* version 6 (lockstep_join_world.h join_admit_agent): per field, the records whose field differed (JOIN_AGENT_FIELDS order) */
    uint64_t agent_applied,agent_fixed,agent_missing,agent_shape,agent_state,agent_field[32];
    /* version 7 (lockstep_join_world.h join_admit_route): */
    uint64_t route_applied,route_rebuilt,route_fixed,route_missing,route_other,route_unmapped,route_shape;
    /* version 8 (lockstep_join_world.h join_admit_param, join_admit_dock): read, write, own: records whose slot of that role
     * differed; turn: records whose own copy's turn rates +0x378..+0x380 differed */
    uint64_t param_applied,param_fixed,param_missing,param_shape,param_unmapped,param_read,param_write,param_own,param_turn;
    uint64_t dock_applied,dock_fixed,dock_missing,dock_kind,dock_shape;
    /* version 9 (join_admit_steer): rebuilt: records whose path vector was refilled; job: the session had an async path job */
    uint64_t steer_applied,steer_fixed,steer_missing,steer_kind,steer_shape,steer_unmapped,steer_other,steer_job,steer_rebuilt;
    /* version 10 (join_admit_solver): created: the Agent had no solver yet; kind: the model is another class than the session's */
    uint64_t solver_applied,solver_fixed,solver_created,solver_missing,solver_shape,solver_kind,solver_heap;
    /* version 11 (join_admit_grav): units applied, written (fixed), not found, another kind, shape (a list that does not fit, no worker),
     * contacts written, contacts without a joiner object, lists rebuilt in a new buffer; sources (lists written, entries missing), sectors */
    uint64_t grav_applied,grav_fixed,grav_missing,grav_kind,grav_shape,grav_contacts,grav_unmapped,grav_buffers,gravs_lists,gravs_missing,gravs_sectors,
             grav_created,gravs_cursor,
             /* version 12 (join_admit_splm): applied, fixed, missing, kind, shape (a joiner route not empty, a list that does not fit),
              * other (a record the saver could not carry or requests pending at T), allocations failed, entries rebuilt */
             splm_applied,splm_fixed,splm_missing,splm_kind,splm_shape,splm_other,splm_alloc,splm_entries,
             /* GRAVN: groups whose neighbour list was rewritten, groups the session named that the joiner lacks, neighbours not found */
             gravn_groups,gravn_fixed,gravn_missing,gravn_unfound,
             /* GRAVP: poses written, already equal, objects not found, the session published one the joiner has not, the reverse */
             gravp_fixed,gravp_equal,gravp_missing,gravp_absent,gravp_extra,
             /* GRAVQ: posts rebuilt, records without a portable source or a joiner object, other records, worker not idle, allocations
              * failed, entries the joiner's list already held */
             gravq_posted,gravq_unmapped,gravq_other,gravq_busy,gravq_alloc,gravq_had,
             /* JOBE: the schedule written, no job engine; the joiner's own last run (bits) and cursor length before it */
             jobe_applied,jobe_missing,jobe_prev_last,jobe_prev_len,jobe_held,   /* held: imports that found the switch on */
             /* EVQT: the joiner's own events of the rebuilt classes removed (per source and type), the session's posted, records of other
              * classes left to the load, records without a portable source or a joiner object, allocations failed */
             evqt_removed,evqt_posted,evqt_other,evqt_missing,evqt_alloc,
             /* GRAVD: cached parents written, already equal, ships or parents not found */
             gravd_fixed,gravd_equal,gravd_missing,gravd_unmapped,
             /* VIEWK: children lists and flags written, views already equal, components not found, unmapped or unread, a session's
              * child not among the joiner's */
             viewk_children,viewk_flags,viewk_equal,viewk_missing,viewk_unmapped,viewk_superset,
             /* VIEWF: flag words written, already equal, components not found, attention words written, in flight (kept) */
             viewf_written,viewf_equal,viewf_missing,viewf_attention,viewf_inflight,
             /* VIEWP: poses written at the admission, already equal, components not found or without a pose */
             viewp_written,viewp_equal,viewp_missing,
             /* SHOOT: controllers written at the admission, already equal, weapons not found or without a controller, controller of
              * another class; last shot times written, beyond the save's rounding; of the controllers missing at the admission, those
              * present after T+1 and after T+2 (same class) */
             shoot_written,shoot_equal,shoot_missing,shoot_class,shoot_time,shoot_far,shoot_late[2],
             /* ATTK: objects with a time written, all equal, not found (or no destructible), attacker or method differ, a time beyond
              * the save's rounding */
             attk_written,attk_equal,attk_missing,attk_conflict,attk_far,
             /* LIVEF: objects whose live flags were written, already equal, not found or of another class */
             livef_written,livef_equal,livef_missing,
             /* SPEC: entries written, already equal, of which flags, class or interval differed; entries the joiner lacks (or its
              * component), entries only the joiner holds */
             spec_written,spec_equal,spec_conflict,spec_missing,spec_extra,spec_added,
             /* NAVC: controllers whose cache was written, already equal, not found */
             navc_written,navc_equal,navc_missing,
             /* JOBS: jobs whose state +0x48 differs (compared only), equal, not found by ID (or without a state object) */
             jobs_written,jobs_equal,jobs_missing,jobs_active_differ,jobs_waiting_differ;
    uint32_t evq_sources,evq_events,evq_due,evq_fault;char evq_top[640];   /* the census at the admission (the joiner's own load) */   /* gravs_cursor: 1 set, 2 no record or no ai_schedule, 3 refused */
    volatile LONG64 unmapped_ids[JOIN_UNMAPPED_SET];volatile LONG64 unmapped_set_full;
    char rows[JOIN_ROWS][400];unsigned nrows;
} join_rx;
static volatile LONG64 join_failures;

static int join_fail_cap(unsigned why,const char *detail) {
    join_cap.state=JOIN_FAILED;join_cap.why=why;snprintf(join_cap.detail,sizeof(join_cap.detail),"%s",detail?detail:"-");
    InterlockedIncrement64(&join_failures);return 0;
}
static int join_fail_rx(unsigned why,const char *detail) {
    join_rx.why=why;snprintf(join_rx.detail,sizeof(join_rx.detail),"%s",detail?detail:"-");InterlockedIncrement64(&join_failures);return 0;
}
/* An unpaired sample row of the admission (gate.join_unpaired). */
static void join_row(const char *format,...) {
    if(join_rx.nrows>=JOIN_ROWS) return;
    va_list list;va_start(list,format);vsnprintf(join_rx.rows[join_rx.nrows],sizeof(join_rx.rows[0]),format,list);va_end(list);++join_rx.nrows;
}
/* ---- small helpers ---- */
static int join_grow(void **items,uint32_t *count,uint32_t *cap,size_t size) {
    if(*count<*cap) return 1;
    uint32_t next=*cap?*cap*2u:256u;if(next>JOIN_MAX_RECORDS) return 0;
    void *p=realloc(*items,(size_t)next*size);if(!p) return 0;
    *items=p;*cap=next;return 1;
}
#define JOIN_PUSH(arr,n,cap,value) (join_grow((void **)&(arr),&(n),&(cap),sizeof(*(arr))) ? ((arr)[(n)++]=(value),1) : 0)
static void join_tables_free(join_tables *t) {
    free(t->ctx);free(t->wake);free(t->vec);free(t->md);free(t->ids);free(t->modules);free(t->lua);free(t->health);free(t->pose);
    free(t->shield);free(t->nav);free(t->attn);free(t->keys);free(t->path);free(t->region);free(t->flight);free(t->spline);free(t->agent);
    free(t->route);free(t->param);free(t->dock);free(t->steer);free(t->steerpt);free(t->solver);free(t->bullet);free(t->grav);free(t->gravc);free(t->gravs);free(t->splm);free(t->splme);free(t->splmseg);free(t->gravn);free(t->gravp);free(t->gravq);free(t->jobe);free(t->evqt);free(t->gravd);free(t->viewk);free(t->viewkc);free(t->viewf);free(t->viewp);free(t->shoot);free(t->attk);free(t->livef);free(t->spec);free(t->navc);free(t->jobs);free(t->gone);free(t->body);free(t->bupd);free(t->ldc);free(t->navt);free(t->nmesh);free(t->noct);free(t->agtt);free(t->agcb);free(t->pbnk);free(t->pend);free(t->gravb);free(t->viewl);free(t->ikv);free(t->rcnt);free(t->ecos);free(t->shpw);free(t->trdn);free(t->npca);free(t->jobg);free(t->code);free(t->engr);free(t->shgr);free(t->bnds);free(t->omgs);free(t->resv);free(t->pcsv);memset(t,0,sizeof(*t));
}
static uint64_t join_bits(double v) {uint64_t b;memcpy(&b,&v,8);return b;}
static double join_double(uint64_t b) {double v;memcpy(&v,&b,8);return v;}
static int join_ctx_cmp(const void *a,const void *b) {
    const join_ctx_rec *x=a,*y=b;
    if(x->klass!=y->klass) return x->klass<y->klass?-1:1;
    if(x->owner!=y->owner) return x->owner<y->owner?-1:1;
    if(x->script!=y->script) return x->script<y->script?-1:1;
    return x->version<y->version?-1:x->version>y->version;
}
static int join_md_order(const void *a,const void *b) {
    const join_md_rec *x=a,*y=b;double s=join_double(x->time),t=join_double(y->time);
    if(s!=t) return s<t?-1:1;
    return x->seq<y->seq?-1:x->seq>y->seq;
}
static int join_md_keycmp(const join_md_rec *x,const join_md_rec *y) {
    if(x->name!=y->name) return x->name<y->name?-1:1;
    if(x->root!=y->root) return x->root<y->root?-1:1;
    if(x->ordinal!=y->ordinal) return x->ordinal<y->ordinal?-1:1;
    if(x->kind!=y->kind) return x->kind<y->kind?-1:1;
    if(x->state!=y->state) return x->state<y->state?-1:1;
    return 0;   /* not the activation: the save rounds it (six significant digits); the admission restores it */
}
static int join_md_keysort(const void *a,const void *b) {
    int c=join_md_keycmp(a,b);if(c) return c;
    return join_md_order(a,b);
}

/* ---- refusal diagnostics ---- */
/* The record a refusal stopped at: the check that failed and the raw fields it read (pointer values, counts), as
 * comma-separated name=value pairs without spaces or '|' (the JOIN_RESULT detail on a saver, the JOIN_STATUS rx_detail
 * on a joiner). J1 build 177 said 'tables:md:22:0/17904/0/0' and no more. */
static char join_fault[256]="-";
static int join_note(const char *format,...) {
    va_list list;va_start(list,format);vsnprintf(join_fault,sizeof(join_fault),format,list);va_end(list);return 0;
}
/* The record's own fields in front of the check its key refused with. */
static int join_note_record(const char *format,...) {
    char inner[sizeof(join_fault)],head[128];memcpy(inner,join_fault,sizeof(inner));
    va_list list;va_start(list,format);vsnprintf(head,sizeof(head),format,list);va_end(list);
    snprintf(join_fault,sizeof(join_fault),"%s,%s",head,inner);return 0;
}
/* More fields after the note. */
static void join_note_more(const char *format,...) {
    size_t used=strlen(join_fault);if(used+1>=sizeof(join_fault)) return;
    va_list list;va_start(list,format);vsnprintf(join_fault+used,sizeof(join_fault)-used,format,list);va_end(list);
}
#define JOIN_P(p) ((unsigned long long)(uintptr_t)(p))

/* ---- keys ---- */
/* 1: key read; 0: not a live context (join_ctx_check names why). A context whose script is unreadable keeps script 0
 * and flags 1. */
static const char *join_ctx_check="-";
static int join_ctx_key(const join_engine *e,uintptr_t ctx,join_ctx_rec *k,unsigned *flags) {
    uintptr_t vtable=0,entity=0,script=0;uint64_t lifetime=0,runtime=0;memset(k,0,sizeof(*k));*flags=0;
    if(!ctx) {join_ctx_check="null";return 0;}
    if(!join_read((void *)ctx,&vtable,8) || vtable!=e->ctx_vtable) {join_ctx_check="vtable";return 0;}
    if(!join_read((void *)(ctx+8),&lifetime,8) || !lifetime) {join_ctx_check="id";return 0;}
    unsigned klass=WORLD_KEY_NONE;
    if(join_read((void *)(ctx+JOIN_CTX_ENTITY),&entity,8) && entity && join_read((void *)(entity+8),&runtime,8) && runtime)
        k->owner=join_key(runtime,&klass);
    k->klass=klass;if(klass==WORLD_KEY_NONE) k->owner=0;
    if(join_read((void *)(ctx+JOIN_CTX_SCRIPT),&script,8) && script) {
        k->script=join_script(ctx);if(!k->script) *flags|=1;
        if(!join_read((void *)(script+0x58),&k->version,4)) *flags|=1;
    }
    return 1;
}
/* The ctx's raw fields after join_ctx_key refused it. */
static void join_note_ctx(const char *where,unsigned index,uintptr_t ctx) {
    uintptr_t vtable=0;uint64_t id=0;
    if(ctx) {(void)join_read((void *)ctx,&vtable,8);(void)join_read((void *)(ctx+8),&id,8);}
    join_note("%s#%u,check=ctx_%s,ctx=%llx,vtable=%llx,id=%llx",where,index,join_ctx_check,JOIN_P(ctx),JOIN_P(vtable),(unsigned long long)id);
}
/* Every AIScriptContext in the order of the global list (head and tail pointers, +0xb8 next, +0xb0 prev; every link is
 * checked both ways). 0 with join_fault set. */
static int join_ctx_list(const join_engine *e,uintptr_t **out,uint32_t *count) {
    uintptr_t cur=0,tail=0,prev=0,*items=NULL;uint32_t n=0,cap=0;*out=NULL;*count=0;
    if(!e->ctx_head || !e->ctx_tail || !join_read((void *)e->ctx_head,&cur,8) || !join_read((void *)e->ctx_tail,&tail,8))
        return join_note("check=list_head,head=%llx,tail=%llx",JOIN_P(e->ctx_head),JOIN_P(e->ctx_tail));
    while(cur) {
        uintptr_t back=0,next=0;
        if(!join_read((void *)(cur+JOIN_CTX_PREV),&back,8) || back!=prev)
            {free(items);return join_note("check=list_prev,index=%u,ctx=%llx,prev=%llx,expected=%llx",n,JOIN_P(cur),JOIN_P(back),JOIN_P(prev));}
        if(!join_read((void *)(cur+JOIN_CTX_NEXT),&next,8)) {free(items);return join_note("check=list_next,index=%u,ctx=%llx",n,JOIN_P(cur));}
        if(!JOIN_PUSH(items,n,cap,cur)) {free(items);return join_note("check=list_length,index=%u,ctx=%llx",n,JOIN_P(cur));}
        prev=cur;cur=next;
    }
    if(prev!=tail) {free(items);return join_note("check=list_tail,count=%u,last=%llx,tail=%llx",n,JOIN_P(prev),JOIN_P(tail));}
    *out=items;*count=n;return 1;
}
/* 8-byte pointer vectors {begin, end} (sub-cue slots, created lists) read once per capture or admission; root keys
 * computed once per root, static cue keys once per static cue (open addressing by address; a full table recomputes). */
typedef struct {uintptr_t at;uintptr_t *items;uint32_t n;} join_vec_cache;
typedef struct {uintptr_t at;uint64_t key;} join_key_cache;
#define JOIN_VECS 64u
#define JOIN_ROOTS 4096u
static join_vec_cache join_vecs[JOIN_VECS];
static unsigned join_vecs_next;
static join_key_cache join_roots[JOIN_ROOTS],join_statics[JOIN_ROOTS];
static void join_keys_reset(void) {
    for(unsigned i=0;i<JOIN_VECS;i++) free(join_vecs[i].items);
    memset(join_vecs,0,sizeof(join_vecs));join_vecs_next=0;memset(join_roots,0,sizeof(join_roots));memset(join_statics,0,sizeof(join_statics));
}
static int join_cache_get(const join_key_cache *cache,uintptr_t at,uint64_t *key) {
    for(unsigned n=0,i=(unsigned)(lockstep_mix(at)%JOIN_ROOTS);n<JOIN_ROOTS;n++,i=(i+1)%JOIN_ROOTS) {
        if(cache[i].at==at) {*key=cache[i].key;return 1;}
        if(!cache[i].at) return 0;
    }
    return 0;
}
static void join_cache_put(join_key_cache *cache,uintptr_t at,uint64_t key) {
    for(unsigned n=0,i=(unsigned)(lockstep_mix(at)%JOIN_ROOTS);n<JOIN_ROOTS;n++,i=(i+1)%JOIN_ROOTS)
        if(!cache[i].at) {cache[i].at=at;cache[i].key=key;return;}
}
static const join_vec_cache *join_vector(uintptr_t at) {
    for(unsigned i=0;i<JOIN_VECS;i++) if(at && join_vecs[i].at==at) return &join_vecs[i];
    uintptr_t begin=0,end=0;
    if(!at || !join_read((void *)at,&begin,8) || !join_read((void *)(at+8),&end,8) || end<begin || (end-begin)%8 ||
       (end-begin)/8>JOIN_CUE_LIST_MAX) return NULL;
    uint32_t n=(uint32_t)((end-begin)/8);uintptr_t *items=n?malloc(n*sizeof(*items)):NULL;
    if(n && (!items || !join_read((void *)begin,items,n*8u))) {free(items);return NULL;}
    join_vec_cache *c=&join_vecs[join_vecs_next++%JOIN_VECS];free(c->items);
    c->at=at;c->items=items;c->n=n;return c;
}
/* item's position in the vector at at: every slot counted (sub-cue slots) or only the non-empty ones (a created list,
 * as the save writes it); -1 absent, -2 unreadable. *count: the vector's length. */
static int64_t join_vector_index(uintptr_t at,uintptr_t item,int dense,uint32_t *count) {
    const join_vec_cache *v=join_vector(at);*count=0;
    if(!v) return -2;
    int64_t seen=0;*count=v->n;
    for(uint32_t i=0;i<v->n;i++) {
        if(v->items[i]==item) return dense?seen:(int64_t)i;
        if(v->items[i]) ++seen;
    }
    return -1;
}
/* A static cue's path key: FNV-1a 64 of "<script>.<root>.<...>.<cue>", its MD script's name (+0x150 -> +0x18) and the
 * own name (+0xc8) of every static cue from its root (parent 0) down to it; a nameless one below the root (which the
 * loader does not make) by "#<slot>" in its parent's sub-cue slots. Every level must be an MD::Cue, a static cue (+0x160
 * itself) of the same script. lockstep_cue_name.h's cue_path_with, which the modules' cue names share; not the script's
 * name map (+0x60), whose walk gave up after 8,192 steps, before the large story scripts' late names (J1 j1-7, report
 * 1.2). 0 with join_fault naming the check (cue_path_text). */
/* Text for a diagnostic row: appended with truncation, ' ' and '|' as '_' (rows are space-separated name=value pairs). */
static size_t join_append(char *out,size_t cap,size_t used,const char *text,size_t n) {
    for(size_t i=0;i<n && used+1<cap;i++) out[used++]=text[i]==' ' || text[i]=='|'?'_':text[i];
    if(cap) out[used<cap?used:cap-1]=0;
    return used;
}
/* The key, and with out the path text "<script>.<root>...<cue>" (diagnostic rows; the cache is used only without it). */
static int join_static_path(const join_engine *e,uintptr_t s,uint64_t *key,char *out,size_t cap) {
    if(!out && join_cache_get(join_statics,s,key)) return 1;
    cue_path p;char text[512];
    if(out && cap) out[0]=0;
    if(!cue_path_with(join_read,e->cue_vtable,s,out?text:NULL,out?sizeof(text):0,&p)) {
        char why[sizeof(join_fault)];cue_path_text(&p,why,sizeof(why));return join_note("%s",why);
    }
    if(out) join_append(out,cap,0,text,strlen(text));
    *key=p.key;if(!out) join_cache_put(join_statics,s,p.key);
    return 1;
}
static int join_static_key(const join_engine *e,uintptr_t s,uint64_t *key) {return join_static_path(e,s,key,NULL,0);}
static int join_root_key(const join_engine *e,uintptr_t root,uint64_t *key,unsigned depth);
/* A cue's key (name, root, ordinal) with its state and activation. 0 with join_fault naming the check and its fields. */
static int join_cue_ident(const join_engine *e,uintptr_t cue,join_md_rec *r,unsigned depth) {
    uintptr_t vtable=0,def=0,root=0;
    if(!join_read((void *)cue,&vtable,8) || vtable!=e->cue_vtable) return join_note("check=cue_vtable,cue=%llx,vtable=%llx",JOIN_P(cue),JOIN_P(vtable));
    if(!join_read((void *)(cue+JOIN_CUE_DEF),&def,8) || !def) return join_note("check=def,cue=%llx,def=%llx",JOIN_P(cue),JOIN_P(def));
    if(!join_read((void *)(cue+JOIN_CUE_STATE),&r->state,4) || !join_read((void *)(cue+JOIN_CUE_ACTIVATION),&r->activation,8))
        return join_note("check=state,cue=%llx",JOIN_P(cue));
    r->name=0;r->root=0;r->ordinal=0;
    if(!join_static_key(e,def,&r->name)) return 0;                  /* an instance: its static cue's */
    if(def==cue) return 1;                                            /* a static cue */
    if(!join_read((void *)(cue+JOIN_CUE_ROOT),&root,8) || !root)
        return join_note("check=root,cue=%llx,def=%llx,root=%llx",JOIN_P(cue),JOIN_P(def),JOIN_P(root));
    uint64_t path=0;unsigned up=0;
    for(uintptr_t cur=cue;cur!=root;) {
        uintptr_t parent=0;uint32_t n=0;
        if(++up>JOIN_CUE_DEPTH || !join_read((void *)(cur+JOIN_CUE_PARENT),&parent,8) || !parent)
            return join_note("check=parent,cue=%llx,at=%llx,parent=%llx,root=%llx,depth=%u",JOIN_P(cue),JOIN_P(cur),JOIN_P(parent),JOIN_P(root),up);
        int64_t slot=join_vector_index(parent+JOIN_CUE_CHILDREN,cur,0,&n);
        if(slot<0) return join_note("check=slot,cue=%llx,at=%llx,parent=%llx,root=%llx,slots=%u,why=%d",JOIN_P(cue),JOIN_P(cur),JOIN_P(parent),
                                    JOIN_P(root),n,(int)slot);
        path=lockstep_mix(path^((uint64_t)slot+1u));cur=parent;
    }
    if(up) r->ordinal=(uint32_t)(path>>32)|1u;
    return join_root_key(e,root,&r->root,depth);
}
/* A root instance: its creator's key mixed with its index among the creator's created instances (+0x1a8). */
static int join_root_key(const join_engine *e,uintptr_t root,uint64_t *key,unsigned depth) {
    if(join_cache_get(join_roots,root,key)) return 1;
    uintptr_t creator=0,cdef=0;uint32_t count=0;uint64_t ck=0;
    if(!join_read((void *)(root+JOIN_CUE_CREATOR),&creator,8) || !creator)
        return join_note("check=creator,root=%llx,creator=%llx",JOIN_P(root),JOIN_P(creator));
    int64_t index=join_vector_index(creator+JOIN_CUE_INSTANCES,root,1,&count);
    if(index<0) return join_note("check=creator_index,root=%llx,creator=%llx,created=%u,why=%d",JOIN_P(root),JOIN_P(creator),count,(int)index);
    if(!join_read((void *)(creator+JOIN_CUE_DEF),&cdef,8) || !cdef)
        return join_note("check=creator_def,root=%llx,creator=%llx,def=%llx",JOIN_P(root),JOIN_P(creator),JOIN_P(cdef));
    if(cdef==creator) {if(!join_static_key(e,creator,&ck)) return 0;}
    else {
        join_md_rec c;memset(&c,0,sizeof(c));
        if(depth>=JOIN_CUE_CREATORS) return join_note("check=creator_depth,root=%llx,creator=%llx,depth=%u",JOIN_P(root),JOIN_P(creator),depth);
        if(!join_cue_ident(e,creator,&c,depth+1)) return 0;
        ck=lockstep_mix(c.name^lockstep_mix(c.root^((uint64_t)c.ordinal<<1|1u)));
    }
    *key=lockstep_mix(ck^lockstep_mix((uint64_t)index+1u))|1u;
    join_cache_put(join_roots,root,*key);   /* after the creator's own keys went in */
    return 1;
}
static int join_cue_key(const join_engine *e,uintptr_t cue,join_md_rec *r) {return join_cue_ident(e,cue,r,0);}

/* The capture site that refused (1..n in source order of ctx, tree walk, wake, vec, md), for the failure detail; every
 * site leaves the raw fields of its record in join_fault. */
static unsigned join_cap_site;
#define JOIN_CAP_FAIL(k) do{join_cap_site=(k);return 0;}while(0)
/* ---- capture (session side; main thread at the gate boundary, MoveWorker joined) ---- */
/* Every context on the global list; one that is no live AIScriptContext refuses (the list holds nothing else). */
static int join_capture_ctx(const join_engine *e,join_tables *t) {
    uintptr_t *list=NULL;uint32_t n=0,cap=0;
    if(!join_ctx_list(e,&list,&n)) JOIN_CAP_FAIL(1);
    for(uint32_t i=0;i<n;i++) {
        join_ctx_rec k;unsigned flags;
        if(!join_ctx_key(e,list[i],&k,&flags)) {join_note_ctx("ctx",i,list[i]);free(list);JOIN_CAP_FAIL(2);}
        if(!JOIN_PUSH(t->ctx,t->nctx,cap,k)) {join_note("ctx#%u,check=memory",i);free(list);JOIN_CAP_FAIL(3);}
    }
    free(list);
    qsort(t->ctx,t->nctx,sizeof(*t->ctx),join_ctx_cmp);
    uint32_t ambiguous=0;for(uint32_t i=1;i<t->nctx;i++) if(!join_ctx_cmp(&t->ctx[i-1],&t->ctx[i])) ++ambiguous;
    t->gate.ctx_count=t->nctx;t->gate.ambiguous=ambiguous;return 1;
}
/* In-order successor in the intrusive tree (AIDirector::Move's walk); header when none. */
static uintptr_t join_tree_next(uintptr_t node,uintptr_t header) {
    uintptr_t right=0,p=0,cur=node;
    if(!join_read((void *)(node+0x10),&right,8)) {join_note("check=tree_right,node=%llx",JOIN_P(node));JOIN_CAP_FAIL(4);}
    if(right) {
        for(uintptr_t left;;) {
            if(!join_read((void *)(right+8),&left,8)) {join_note("check=tree_left,node=%llx,at=%llx",JOIN_P(node),JOIN_P(right));JOIN_CAP_FAIL(5);}
            if(!left) return right;
            right=left;
        }
    }
    for(unsigned guard=0;guard<128;guard++) {
        uintptr_t left=0;
        if(!join_read((void *)cur,&p,8) || !p) {join_note("check=tree_parent,node=%llx,at=%llx,parent=%llx",JOIN_P(node),JOIN_P(cur),JOIN_P(p));JOIN_CAP_FAIL(6);}
        if(p==header) return header;
        if(!join_read((void *)(p+8),&left,8)) {join_note("check=tree_up,node=%llx,parent=%llx",JOIN_P(node),JOIN_P(p));JOIN_CAP_FAIL(7);}
        if(left==cur) return p;
        cur=p;
    }
    join_note("check=tree_depth,node=%llx",JOIN_P(node));JOIN_CAP_FAIL(8);
}
static int join_capture_wake(const join_engine *e,join_tables *t) {
    uintptr_t header=(uintptr_t)(e->director+JOIN_DIR_TREE),node=0;uint64_t size=0;uint32_t cap=0;
    if(!join_read((void *)(header+JOIN_TREE_FIRST),&node,8) || !join_read((void *)(header+JOIN_TREE_SIZE),&size,8) || size>JOIN_MAX_RECORDS)
        {join_note("check=tree_header,header=%llx,first=%llx,size=%llu",JOIN_P(header),JOIN_P(node),(unsigned long long)size);JOIN_CAP_FAIL(9);}
    uint64_t seen=0;
    while(node && node!=header) {
        uint64_t key=0;uintptr_t ctx=0;join_ctx_rec k;unsigned flags=0;join_wake_rec w;memset(&w,0,sizeof(w));
        if(++seen>size || !join_read((void *)(node+JOIN_NODE_KEY),&key,8) || !join_read((void *)(node+JOIN_NODE_CTX),&ctx,8))
            {join_note("wake#%u,check=node,node=%llx,size=%llu",t->nwake,JOIN_P(node),(unsigned long long)size);JOIN_CAP_FAIL(10);}
        if(!ctx || !join_ctx_key(e,ctx,&k,&flags)) {join_note_ctx("wake",t->nwake,ctx);join_note_more(",node=%llx",JOIN_P(node));JOIN_CAP_FAIL(11);}
        uint64_t due=0;uintptr_t back=0;
        if(!join_read((void *)(ctx+JOIN_CTX_REASON),&w.reason,4) || !join_read((void *)(ctx+JOIN_CTX_DUE),&due,8) ||
           !join_read((void *)(ctx+JOIN_CTX_NODE),&back,8)) {join_note("wake#%u,check=ctx_fields,ctx=%llx",t->nwake,JOIN_P(ctx));JOIN_CAP_FAIL(12);}
        w.owner=k.owner;w.script=k.script;w.klass=k.klass;w.version=k.version;w.due=key;
        w.flags=flags|(due!=key?2u:0u)|(back!=node?4u:0u);
        if(!JOIN_PUSH(t->wake,t->nwake,cap,w)) {join_note("wake#%u,check=memory",t->nwake);JOIN_CAP_FAIL(13);}
        node=join_tree_next(node,header);
    }
    if(!node) return 0;                      /* join_tree_next refused: its site (4..8) and note stand */
    if(seen!=size) {join_note("check=tree_count,seen=%llu,size=%llu",(unsigned long long)seen,(unsigned long long)size);JOIN_CAP_FAIL(14);}
    t->gate.wake_count=t->nwake;return 1;
}
static int join_capture_vec(const join_engine *e,join_tables *t) {
    uint32_t cap=0;
    if(!join_read(e->director+JOIN_DIR_VEC_INDEX,&t->gate.vec_index,4)) {join_note("check=vec_index,director=%llx",JOIN_P(e->director));JOIN_CAP_FAIL(15);}
    for(uint32_t v=0;v<2;v++) {
        uintptr_t begin=0,end=0;uint32_t position=0;
        unsigned char *vec=e->director+JOIN_DIR_VEC+v*0x18u;
        if(!join_read(vec,&begin,8) || !join_read(vec+8,&end,8) || end<begin || (end-begin)%8)
            {join_note("vec%u,check=vector,begin=%llx,end=%llx",v,JOIN_P(begin),JOIN_P(end));JOIN_CAP_FAIL(16);}
        for(uintptr_t p=begin;p<end;p+=8) {
            uintptr_t ctx=0;join_ctx_rec k;unsigned flags;join_vec_rec r;memset(&r,0,sizeof(r));unsigned at=(unsigned)((p-begin)/8);
            if(!join_read((void *)p,&ctx,8)) {join_note("vec%u#%u,check=entry,at=%llx",v,at,JOIN_P(p));JOIN_CAP_FAIL(17);}
            if(!ctx) continue;                                       /* a hole left by ClearWake */
            if(!join_ctx_key(e,ctx,&k,&flags)) {join_note_ctx("vec",at,ctx);join_note_more(",vector=%u",v);JOIN_CAP_FAIL(18);}
            if(!join_read((void *)(ctx+JOIN_CTX_MASK),&r.mask,4)) {join_note("vec%u#%u,check=mask,ctx=%llx",v,at,JOIN_P(ctx));JOIN_CAP_FAIL(18);}
            r.owner=k.owner;r.script=k.script;r.klass=k.klass;r.version=k.version;r.vector=v;r.position=position++;
            if(!JOIN_PUSH(t->vec,t->nvec,cap,r)) {join_note("vec%u#%u,check=memory",v,at);JOIN_CAP_FAIL(19);}
        }
        if(v) t->gate.vec1=position;else t->gate.vec0=position;
    }
    return 1;
}
static uint32_t join_vector_count(unsigned char *at) {
    uintptr_t begin=0,end=0;
    if(!join_read(at,&begin,8) || !join_read(at+8,&end,8) || end<begin) return UINT32_MAX;
    return (uint32_t)((end-begin)/8);
}
static int join_capture_md(const join_engine *e,join_tables *t) {
    uintptr_t begin=0,end=0;uint32_t cap=0;
    if(!join_read(e->scheduler+JOIN_MD_HEAP,&begin,8) || !join_read(e->scheduler+JOIN_MD_HEAP+8,&end,8) || end<begin || (end-begin)%24 ||
       !join_read(e->md_seq,&t->gate.md_seq,4)) {join_note("check=md_heap,begin=%llx,end=%llx",JOIN_P(begin),JOIN_P(end));JOIN_CAP_FAIL(20);}
    for(uintptr_t p=begin;p<end;p+=24) {
        unsigned char entry[24];uintptr_t cue;join_md_rec r;memset(&r,0,sizeof(r));unsigned at=(unsigned)((p-begin)/24);
        if(!join_read((void *)p,entry,24)) {join_note("md#%u,check=entry,at=%llx",at,JOIN_P(p));JOIN_CAP_FAIL(21);}
        memcpy(&r.time,entry,8);memcpy(&r.seq,entry+8,4);memcpy(&r.kind,entry+12,4);memcpy(&cue,entry+16,8);
        if(!cue) {++t->gate.md_void;continue;}
        if(!join_cue_key(e,cue,&r)) {join_note_record("md#%u,kind=%u,seq=%u,time=%.17g,cue=%llx",at,r.kind,r.seq,join_double(r.time),JOIN_P(cue));JOIN_CAP_FAIL(22);}
        if(!JOIN_PUSH(t->md,t->nmd,cap,r)) {join_note("md#%u,check=memory",at);JOIN_CAP_FAIL(23);}
    }
    t->gate.md_count=t->nmd;
    t->gate.md_release=join_vector_count(e->scheduler+JOIN_MD_RELEASE);t->gate.md_queued=join_vector_count(e->scheduler+JOIN_MD_QUEUED);
    return 1;
}
static int join_id_cmp(const void *a,const void *b) {
    const join_id_rec *x=a,*y=b;return x->runtime<y->runtime?-1:x->runtime>y->runtime;
}
static int join_capture_ids(join_tables *t) {
    uint32_t cap=0;
    if(world_identities) for(unsigned i=0;i<WORLD_IDENTITY_CAPACITY;i++) {
        const world_identity_entry *e=&world_identities[i];
        if(!e->runtime) continue;
        join_id_rec r={e->runtime,e->saved,WORLD_KEY_SAVED,0};
        if(!JOIN_PUSH(t->ids,t->nids,cap,r)) return 0;
    }
    if(created_enabled() && created_live) {
        AcquireSRWLockShared(&created_lock);int ok=1;
        for(unsigned i=0;i<created_live_count && ok;i++) {
            join_id_rec r={created_live[i].runtime,created_live[i].created,WORLD_KEY_CREATED,0};ok=JOIN_PUSH(t->ids,t->nids,cap,r);
        }
        t->gate.created_live=created_live_count;
        ReleaseSRWLockShared(&created_lock);
        if(!ok) return 0;
    }
    qsort(t->ids,t->nids,sizeof(*t->ids),join_id_cmp);
    for(uint32_t i=1;i<t->nids;i++) if(t->ids[i].runtime==t->ids[i-1].runtime) return 0;
    t->gate.identities=t->nids;t->gate.created_generation=(uint64_t)InterlockedCompareExchange64(&world_created_generation,0,0);
    return 1;
}
/* GONE: the census rows of this session whose object is gone at T. The census keeps a row for every snapshot object and for every
 * created object a capture saw (lockstep_world_hash.h), alive or not; a joiner's rows come from the join save, which holds the
 * living ones only. j1-117 at T+1: 2,360 rows only on the session, every one absent (type 0, parent 0), so the world state of
 * the declared digest parted from T+1 on with every common object equal; the joiner's rows and these give the session's roots. */
static int join_gone_cmp(const void *a,const void *b) {
    const uint64_t x=((const join_gone_rec*)a)->portable,y=((const join_gone_rec*)b)->portable;return x<y?-1:x>y;
}
/* Every census row without an object at T: j1-118's savers wrote 95,166, most of them connections, which the census reads as
 * absent on both sides and a joiner imports (its own rows stay, world_hash_init); a row of an object the map still holds but a
 * joiner does not import is carried as well. */
static int join_capture_gone(join_tables *t,world_hash_find_fn find,void *map) {
    if(!world_hash_items || !map) return 1;   /* no census on this session: nothing to carry */
    uint32_t cap=0;
    for(unsigned i=0;i<world_hash_count+world_hash_created_count;i++) {
        const world_hash_item *item=world_hash_item_at(i);
        if(item->runtime && find(map,item->runtime,4)) continue;
        const join_gone_rec r={item->saved};if(!JOIN_PUSH(t->gone,t->ngone,cap,r)) return 0;
    }
    if(t->ngone) qsort(t->gone,t->ngone,sizeof(*t->gone),join_gone_cmp);
    for(uint32_t i=0;i<t->ngone;i++) if(!t->gone[i].portable || (i && t->gone[i-1].portable==t->gone[i].portable)) return 0;
    return 1;
}
/* lockstep_world_hash.h's first capture on a joiner: the session's rows of gone objects, sorted (0 none). */
static uint32_t join_gone_keys(const uint64_t **keys) {
    if(!join_rx.loaded || join_rx.t.version<13 || !join_rx.t.ngone) return 0;
    *keys=&join_rx.t.gone[0].portable;return join_rx.t.ngone;
}
static int join_fixture_any(const join_gate_rec *g) {for(unsigned i=0;i<12;i++) if(g->fixture[i]) return 1;return 0;}
#include "lockstep_join_world.h"   /* the world side table: HEALTH and POSE (package v2) */
/* The client-independent text (native_gate_join.py portable_text) and its digest. */
static int join_portable(join_tables *t,unsigned char out[16]) {
    lockstep_sha256_ctx c;lockstep_sha256_init(&c);char line[192];
    for(uint32_t i=0;i<t->nctx;i++) {
        const join_ctx_rec *r=&t->ctx[i];
        int n=snprintf(line,sizeof(line),"CTX|%u|%016llx|%016llx|%u\n",r->klass,(unsigned long long)r->owner,(unsigned long long)r->script,r->version);
        lockstep_sha256_update(&c,line,(size_t)n);
    }
    for(uint32_t i=0;i<t->nwake;i++) {
        const join_wake_rec *r=&t->wake[i];
        int n=snprintf(line,sizeof(line),"WAKE|%u|%016llx|%016llx|%u|%016llx|%u\n",r->klass,(unsigned long long)r->owner,
                       (unsigned long long)r->script,r->version,(unsigned long long)r->due,r->reason);
        lockstep_sha256_update(&c,line,(size_t)n);
    }
    for(uint32_t i=0;i<t->nvec;i++) {
        const join_vec_rec *r=&t->vec[i];
        int n=snprintf(line,sizeof(line),"VEC|%u|%u|%u|%016llx|%016llx|%u|%u\n",r->vector,r->position,r->klass,(unsigned long long)r->owner,
                       (unsigned long long)r->script,r->version,r->mask);
        lockstep_sha256_update(&c,line,(size_t)n);
    }
    join_md_rec *sorted=t->nmd?malloc(t->nmd*sizeof(*sorted)):NULL;
    if(t->nmd && !sorted) return 0;
    if(t->nmd) {memcpy(sorted,t->md,t->nmd*sizeof(*sorted));qsort(sorted,t->nmd,sizeof(*sorted),join_md_order);}
    for(uint32_t i=0;i<t->nmd;i++) {
        const join_md_rec *r=&sorted[i];
        int n=snprintf(line,sizeof(line),"MD|%016llx|%016llx|%u|%u|%u|%016llx|%016llx\n",(unsigned long long)r->name,(unsigned long long)r->root,
                       r->ordinal,r->kind,r->state,(unsigned long long)r->activation,(unsigned long long)r->time);
        lockstep_sha256_update(&c,line,(size_t)n);
    }
    free(sorted);
    /* The snapshot fixture's views (a line only when the session bound a fixture: packages without one keep their digest). */
    if(join_fixture_any(&t->gate)) {
        int n=snprintf(line,sizeof(line),"FIXTURE|");
        for(unsigned i=0;i<12;i++) line[n++]=(char)('0'+t->gate.fixture[i]%10u);
        line[n++]='\n';lockstep_sha256_update(&c,line,(size_t)n);
    }
    /* Version 2: the world side table, one line per section (its count and the SHA-256 of its records). */
    if(t->version>=2) {
        unsigned char sha[32];char hex[65];
        lockstep_sha256(t->health?(const void *)t->health:"",(size_t)t->nhealth*sizeof(*t->health),sha);hex32(sha,hex);
        int n=snprintf(line,sizeof(line),"HEALTH|%u|%s\n",t->nhealth,hex);lockstep_sha256_update(&c,line,(size_t)n);
        lockstep_sha256(t->pose?(const void *)t->pose:"",(size_t)t->npose*sizeof(*t->pose),sha);hex32(sha,hex);
        n=snprintf(line,sizeof(line),"POSE|%u|%s\n",t->npose,hex);lockstep_sha256_update(&c,line,(size_t)n);
    }
    /* Version 3: SHIELD, NAV, ATTN, KEYS, the same way. */
    if(t->version>=3) {
        const struct {const char *name;const void *records;size_t bytes;uint32_t count;} v3[]={
            {"SHIELD",t->shield,(size_t)t->nshield*sizeof(*t->shield),t->nshield},{"NAV",t->nav,(size_t)t->nnav*sizeof(*t->nav),t->nnav},
            {"ATTN",t->attn,(size_t)t->nattn*sizeof(*t->attn),t->nattn},{"KEYS",t->keys,(size_t)t->nkeys*sizeof(*t->keys),t->nkeys}};
        for(unsigned i=0;i<4;i++) {
            unsigned char sha[32];char hex[65];lockstep_sha256(v3[i].records?v3[i].records:"",v3[i].bytes,sha);hex32(sha,hex);
            int n=snprintf(line,sizeof(line),"%s|%u|%s\n",v3[i].name,v3[i].count,hex);lockstep_sha256_update(&c,line,(size_t)n);
        }
    }
    /* Version 4: PATH and REGION, the same way. */
    if(t->version>=4) {
        const struct {const char *name;const void *records;size_t bytes;uint32_t count;} v4[]={
            {"PATH",t->path,(size_t)t->npath*sizeof(*t->path),t->npath},{"REGION",t->region,(size_t)t->nregion*sizeof(*t->region),t->nregion}};
        for(unsigned i=0;i<2;i++) {
            unsigned char sha[32];char hex[65];lockstep_sha256(v4[i].records?v4[i].records:"",v4[i].bytes,sha);hex32(sha,hex);
            int n=snprintf(line,sizeof(line),"%s|%u|%s\n",v4[i].name,v4[i].count,hex);lockstep_sha256_update(&c,line,(size_t)n);
        }
    }
    /* Version 5: FLIGHT and SPLINE, the same way. */
    if(t->version>=5) {
        const struct {const char *name;const void *records;size_t bytes;uint32_t count;} v5[]={
            {"FLIGHT",t->flight,(size_t)t->nflight*sizeof(*t->flight),t->nflight},{"SPLINE",t->spline,(size_t)t->nspline*sizeof(*t->spline),t->nspline}};
        for(unsigned i=0;i<2;i++) {
            unsigned char sha[32];char hex[65];lockstep_sha256(v5[i].records?v5[i].records:"",v5[i].bytes,sha);hex32(sha,hex);
            int n=snprintf(line,sizeof(line),"%s|%u|%s\n",v5[i].name,v5[i].count,hex);lockstep_sha256_update(&c,line,(size_t)n);
        }
    }
    /* Version 6: AGENT, the same way. */
    if(t->version>=6) {
        unsigned char sha[32];char hex[65];
        lockstep_sha256(t->agent?(const void *)t->agent:"",(size_t)t->nagent*sizeof(*t->agent),sha);hex32(sha,hex);
        int n=snprintf(line,sizeof(line),"AGENT|%u|%s\n",t->nagent,hex);lockstep_sha256_update(&c,line,(size_t)n);
    }
    /* Version 7: ROUTE, the same way. */
    if(t->version>=7) {
        unsigned char sha[32];char hex[65];
        lockstep_sha256(t->route?(const void *)t->route:"",(size_t)t->nroute*sizeof(*t->route),sha);hex32(sha,hex);
        int n=snprintf(line,sizeof(line),"ROUTE|%u|%s\n",t->nroute,hex);lockstep_sha256_update(&c,line,(size_t)n);
    }
    /* Version 8: PARAM and DOCK, the same way. */
    if(t->version>=8) {
        unsigned char sha[32];char hex[65];
        lockstep_sha256(t->param?(const void *)t->param:"",(size_t)t->nparam*sizeof(*t->param),sha);hex32(sha,hex);
        int n=snprintf(line,sizeof(line),"PARAM|%u|%s\n",t->nparam,hex);lockstep_sha256_update(&c,line,(size_t)n);
        lockstep_sha256(t->dock?(const void *)t->dock:"",(size_t)t->ndock*sizeof(*t->dock),sha);hex32(sha,hex);
        n=snprintf(line,sizeof(line),"DOCK|%u|%s\n",t->ndock,hex);lockstep_sha256_update(&c,line,(size_t)n);
    }
    /* Version 9: STEER and STEERPT, the same way. */
    if(t->version>=9) {
        unsigned char sha[32];char hex[65];
        lockstep_sha256(t->steer?(const void *)t->steer:"",(size_t)t->nsteer*sizeof(*t->steer),sha);hex32(sha,hex);
        int n=snprintf(line,sizeof(line),"STEER|%u|%s\n",t->nsteer,hex);lockstep_sha256_update(&c,line,(size_t)n);
        lockstep_sha256(t->steerpt?(const void *)t->steerpt:"",(size_t)t->nsteerpt*sizeof(*t->steerpt),sha);hex32(sha,hex);
        n=snprintf(line,sizeof(line),"STEERPT|%u|%s\n",t->nsteerpt,hex);lockstep_sha256_update(&c,line,(size_t)n);
    }
    /* Version 10: which Agents hold a solver, and under which model (not its bytes: the structure's comment). BULLET not at all. */
    if(t->version>=10) {
        unsigned char sha[32];char hex[65];lockstep_sha256_ctx ids;lockstep_sha256_init(&ids);
        for(uint32_t i=0;i<t->nsolver;i++) {lockstep_sha256_update(&ids,&t->solver[i].portable,8);lockstep_sha256_update(&ids,&t->solver[i].kind,4);}
        lockstep_sha256_final(&ids,sha);hex32(sha,hex);
        int n=snprintf(line,sizeof(line),"SOLVER|%u|%s\n",t->nsolver,hex);lockstep_sha256_update(&c,line,(size_t)n);
    }
    /* Version 11: which units, of which kind, with how many contacts per list (their bytes, contacts and GRAVS: 'native_gate_join.py
     * compare' first; the digest takes them once two savers are seen to agree on them). */
    if(t->version>=11) {
        unsigned char sha[32];char hex[65];lockstep_sha256_ctx ids;lockstep_sha256_init(&ids);
        for(uint32_t i=0;i<t->ngrav;i++) {
            lockstep_sha256_update(&ids,&t->grav[i].portable,8);lockstep_sha256_update(&ids,&t->grav[i].kind,4);
            lockstep_sha256_update(&ids,t->grav[i].count,sizeof(t->grav[i].count));
        }
        lockstep_sha256_final(&ids,sha);hex32(sha,hex);
        int n=snprintf(line,sizeof(line),"GRAV|%u|%u|%u|%s\n",t->ngrav,t->ngravc,t->ngravs,hex);lockstep_sha256_update(&c,line,(size_t)n);
    }
    /* Version 12: which spline models, how many entries, requests, segments and pairs (their bytes: compare first, as GRAV). */
    if(t->version>=12) {
        unsigned char sha[32];char hex[65];lockstep_sha256_ctx ids;lockstep_sha256_init(&ids);
        for(uint32_t i=0;i<t->nsplm;i++) {
            const join_splm_rec *r=&t->splm[i];
            lockstep_sha256_update(&ids,&r->portable,8);lockstep_sha256_update(&ids,r->entries,8);lockstep_sha256_update(&ids,&r->requests,4);
            lockstep_sha256_update(&ids,r->segments,8);lockstep_sha256_update(&ids,r->pairs,8);
        }
        lockstep_sha256_final(&ids,sha);hex32(sha,hex);
        int n=snprintf(line,sizeof(line),"SPLM|%u|%u|%u|%s\n",t->nsplm,t->nsplme,t->nsplmseg,hex);lockstep_sha256_update(&c,line,(size_t)n);
        lockstep_sha256(t->gravn?(const void *)t->gravn:"",(size_t)t->ngravn*sizeof(*t->gravn),sha);hex32(sha,hex);   /* deterministic: keys only */
        n=snprintf(line,sizeof(line),"GRAVN|%u|%s\n",t->ngravn,hex);lockstep_sha256_update(&c,line,(size_t)n);
        {   /* GRAVP: which objects and views hold a pose (its bytes: compare first) */
            lockstep_sha256_ctx pc;lockstep_sha256_init(&pc);
            for(uint32_t i=0;i<t->ngravp;i++) {lockstep_sha256_update(&pc,&t->gravp[i].portable,8);lockstep_sha256_update(&pc,&t->gravp[i].rel,8);}
            lockstep_sha256_final(&pc,sha);hex32(sha,hex);
        }
        n=snprintf(line,sizeof(line),"GRAVP|%u|%s\n",t->ngravp,hex);lockstep_sha256_update(&c,line,(size_t)n);
        {   /* GRAVQ: which sources, kinds, flags and events in which order (the times and the event's qwords: compare first) */
            lockstep_sha256_ctx qc;lockstep_sha256_init(&qc);
            for(uint32_t i=0;i<t->ngravq;i++) lockstep_sha256_update(&qc,&t->gravq[i],24);
            lockstep_sha256_final(&qc,sha);hex32(sha,hex);
        }
        n=snprintf(line,sizeof(line),"GRAVQ|%u|%s\n",t->ngravq,hex);lockstep_sha256_update(&c,line,(size_t)n);
        if(t->version>=13) {   /* version 13: the job engine's schedule, deterministic: all of it */
            lockstep_sha256(t->jobe?(const void *)t->jobe:"",(size_t)t->njobe*sizeof(*t->jobe),sha);hex32(sha,hex);
            n=snprintf(line,sizeof(line),"JOBE|%u|%s\n",t->njobe,hex);lockstep_sha256_update(&c,line,(size_t)n);
            /* EVQT stays out (j1-51: the two savers' pending events differ, each client's own player objects): compare first */
        }
    }
    unsigned char full[32];lockstep_sha256_final(&c,full);memcpy(out,full,16);return 1;
}
/* MODULES: one line per armed module, name=status (serialized, superseded, unsupported, unaudited). */
static unsigned join_audit(join_tables *t);

/* ---- the package ---- */
typedef struct {unsigned char *data;size_t size,cap;int failed;} join_buf;
static void join_put(join_buf *b,const void *p,size_t n) {
    if(b->failed) return;
    if(b->size+n>b->cap) {
        size_t next=b->cap?b->cap:65536;while(next<b->size+n) next*=2;
        unsigned char *q=realloc(b->data,next);if(!q) {b->failed=1;return;}
        b->data=q;b->cap=next;
    }
    memcpy(b->data+b->size,p,n);b->size+=n;
}
static void join_section(join_buf *b,uint32_t kind,uint32_t count,const void *payload,uint64_t bytes) {
    unsigned char head[48],sha[32];lockstep_sha256(payload?payload:"",(size_t)bytes,sha);
    memcpy(head,&kind,4);memcpy(head+4,&count,4);memcpy(head+8,&bytes,8);memcpy(head+16,sha,32);
    join_put(b,head,48);if(bytes) join_put(b,payload,(size_t)bytes);
}
/* Byte for byte native_gate_join.py write_package; the portable digest goes into the GATE record first. */
static int join_package_build(join_tables *t,join_buf *b) {
    if(!join_portable(t,t->gate.portable)) return 0;
    unsigned char header[96];uint32_t version=JOIN_PACKAGE_VERSION,count=JOIN_SECTIONS(JOIN_PACKAGE_VERSION);uint64_t zero=0;
    if(t->version!=JOIN_PACKAGE_VERSION) return 0;   /* the world side table (v2..v4) is part of every package this build writes */
    memcpy(header,"X4MPJN01",8);memcpy(header+8,&version,4);memcpy(header+12,&count,4);memcpy(header+16,&t->gate.tick,8);
    memcpy(header+24,&t->gate.game,8);memcpy(header+32,&t->gate.real,8);memcpy(header+40,t->save_sha,32);memcpy(header+72,&t->save_bytes,8);
    memcpy(header+80,&t->gate.slot,4);memcpy(header+84,&t->flags,4);memcpy(header+88,&zero,8);
    join_put(b,header,96);
    join_section(b,JOIN_SEC_GATE,1,&t->gate,sizeof(t->gate));
    join_section(b,JOIN_SEC_IDS,t->nids,t->ids,(uint64_t)t->nids*sizeof(*t->ids));
    join_section(b,JOIN_SEC_WAKE,t->nwake,t->wake,(uint64_t)t->nwake*sizeof(*t->wake));
    join_section(b,JOIN_SEC_WAKEVEC,t->nvec,t->vec,(uint64_t)t->nvec*sizeof(*t->vec));
    join_section(b,JOIN_SEC_MDHEAP,t->nmd,t->md,(uint64_t)t->nmd*sizeof(*t->md));
    join_section(b,JOIN_SEC_MODULES,t->nmodules,t->modules,t->modules_len);
    join_section(b,JOIN_SEC_CTX,t->nctx,t->ctx,(uint64_t)t->nctx*sizeof(*t->ctx));
    join_section(b,JOIN_SEC_LUA,t->nlua,t->lua,t->lua_len);
    join_section(b,JOIN_SEC_HEALTH,t->nhealth,t->health,(uint64_t)t->nhealth*sizeof(*t->health));
    join_section(b,JOIN_SEC_POSE,t->npose,t->pose,(uint64_t)t->npose*sizeof(*t->pose));
    join_section(b,JOIN_SEC_SHIELD,t->nshield,t->shield,(uint64_t)t->nshield*sizeof(*t->shield));
    join_section(b,JOIN_SEC_NAV,t->nnav,t->nav,(uint64_t)t->nnav*sizeof(*t->nav));
    join_section(b,JOIN_SEC_ATTN,t->nattn,t->attn,(uint64_t)t->nattn*sizeof(*t->attn));
    join_section(b,JOIN_SEC_KEYS,t->nkeys,t->keys,(uint64_t)t->nkeys*sizeof(*t->keys));
    join_section(b,JOIN_SEC_PATH,t->npath,t->path,(uint64_t)t->npath*sizeof(*t->path));
    join_section(b,JOIN_SEC_REGION,t->nregion,t->region,(uint64_t)t->nregion*sizeof(*t->region));
    join_section(b,JOIN_SEC_FLIGHT,t->nflight,t->flight,(uint64_t)t->nflight*sizeof(*t->flight));
    join_section(b,JOIN_SEC_SPLINE,t->nspline,t->spline,(uint64_t)t->nspline*sizeof(*t->spline));
    join_section(b,JOIN_SEC_AGENT,t->nagent,t->agent,(uint64_t)t->nagent*sizeof(*t->agent));
    join_section(b,JOIN_SEC_ROUTE,t->nroute,t->route,(uint64_t)t->nroute*sizeof(*t->route));
    join_section(b,JOIN_SEC_PARAM,t->nparam,t->param,(uint64_t)t->nparam*sizeof(*t->param));
    join_section(b,JOIN_SEC_DOCK,t->ndock,t->dock,(uint64_t)t->ndock*sizeof(*t->dock));
    join_section(b,JOIN_SEC_STEER,t->nsteer,t->steer,(uint64_t)t->nsteer*sizeof(*t->steer));
    join_section(b,JOIN_SEC_STEERPT,t->nsteerpt,t->steerpt,(uint64_t)t->nsteerpt*sizeof(*t->steerpt));
    join_section(b,JOIN_SEC_SOLVER,t->nsolver,t->solver,(uint64_t)t->nsolver*sizeof(*t->solver));
    join_section(b,JOIN_SEC_BULLET,t->nbullet,t->bullet,(uint64_t)t->nbullet*sizeof(*t->bullet));
    join_section(b,JOIN_SEC_GRAV,t->ngrav,t->grav,(uint64_t)t->ngrav*sizeof(*t->grav));
    join_section(b,JOIN_SEC_GRAVC,t->ngravc,t->gravc,(uint64_t)t->ngravc*sizeof(*t->gravc));
    join_section(b,JOIN_SEC_GRAVS,t->ngravs,t->gravs,(uint64_t)t->ngravs*sizeof(*t->gravs));
    join_section(b,JOIN_SEC_SPLM,t->nsplm,t->splm,(uint64_t)t->nsplm*sizeof(*t->splm));
    join_section(b,JOIN_SEC_SPLME,t->nsplme,t->splme,(uint64_t)t->nsplme*sizeof(*t->splme));
    join_section(b,JOIN_SEC_SPLMSEG,t->nsplmseg,t->splmseg,(uint64_t)t->nsplmseg*sizeof(*t->splmseg));
    join_section(b,JOIN_SEC_GRAVN,t->ngravn,t->gravn,(uint64_t)t->ngravn*sizeof(*t->gravn));
    join_section(b,JOIN_SEC_GRAVP,t->ngravp,t->gravp,(uint64_t)t->ngravp*sizeof(*t->gravp));
    join_section(b,JOIN_SEC_GRAVQ,t->ngravq,t->gravq,(uint64_t)t->ngravq*sizeof(*t->gravq));
    join_section(b,JOIN_SEC_JOBE,t->njobe,t->jobe,(uint64_t)t->njobe*sizeof(*t->jobe));
    join_section(b,JOIN_SEC_EVQT,t->nevqt,t->evqt,(uint64_t)t->nevqt*sizeof(*t->evqt));
    join_section(b,JOIN_SEC_GRAVD,t->ngravd,t->gravd,(uint64_t)t->ngravd*sizeof(*t->gravd));
    join_section(b,JOIN_SEC_VIEWK,t->nviewk,t->viewk,(uint64_t)t->nviewk*sizeof(*t->viewk));
    join_section(b,JOIN_SEC_VIEWKC,t->nviewkc,t->viewkc,(uint64_t)t->nviewkc*sizeof(*t->viewkc));
    join_section(b,JOIN_SEC_VIEWF,t->nviewf,t->viewf,(uint64_t)t->nviewf*sizeof(*t->viewf));
    join_section(b,JOIN_SEC_VIEWP,t->nviewp,t->viewp,(uint64_t)t->nviewp*sizeof(*t->viewp));
    join_section(b,JOIN_SEC_SHOOT,t->nshoot,t->shoot,(uint64_t)t->nshoot*sizeof(*t->shoot));
    join_section(b,JOIN_SEC_ATTK,t->nattk,t->attk,(uint64_t)t->nattk*sizeof(*t->attk));
    join_section(b,JOIN_SEC_LIVEF,t->nlivef,t->livef,(uint64_t)t->nlivef*sizeof(*t->livef));
    join_section(b,JOIN_SEC_SPEC,t->nspec,t->spec,(uint64_t)t->nspec*sizeof(*t->spec));
    join_section(b,JOIN_SEC_NAVC,t->nnavc,t->navc,(uint64_t)t->nnavc*sizeof(*t->navc));
    join_section(b,JOIN_SEC_JOBS,t->njobs,t->jobs,(uint64_t)t->njobs*sizeof(*t->jobs));
    join_section(b,JOIN_SEC_GONE,t->ngone,t->gone,(uint64_t)t->ngone*sizeof(*t->gone));
    join_section(b,JOIN_SEC_BODY,t->nbody,t->body,(uint64_t)t->nbody*sizeof(*t->body));
    join_section(b,JOIN_SEC_BUPD,t->nbupd,t->bupd,(uint64_t)t->nbupd*sizeof(*t->bupd));
    join_section(b,JOIN_SEC_LDC,t->nldc,t->ldc,(uint64_t)t->nldc*sizeof(*t->ldc));
    join_section(b,JOIN_SEC_NAVT,t->nnavt,t->navt,(uint64_t)t->nnavt*sizeof(*t->navt));
    join_section(b,JOIN_SEC_NMESH,t->nnmesh,t->nmesh,(uint64_t)t->nnmesh*sizeof(*t->nmesh));
    join_section(b,JOIN_SEC_NOCT,t->nnoct,t->noct,(uint64_t)t->nnoct*sizeof(*t->noct));
    join_section(b,JOIN_SEC_AGTT,t->nagtt,t->agtt,(uint64_t)t->nagtt*sizeof(*t->agtt));
    join_section(b,JOIN_SEC_AGCB,t->nagcb,t->agcb,(uint64_t)t->nagcb*sizeof(*t->agcb));
    join_section(b,JOIN_SEC_PBNK,t->npbnk,t->pbnk,(uint64_t)t->npbnk*sizeof(*t->pbnk));
    join_section(b,JOIN_SEC_PEND,t->npend,t->pend,(uint64_t)t->npend*sizeof(*t->pend));
    join_section(b,JOIN_SEC_GRAVB,t->ngravb,t->gravb,(uint64_t)t->ngravb*sizeof(*t->gravb));
    join_section(b,JOIN_SEC_VIEWL,t->nviewl,t->viewl,(uint64_t)t->nviewl*sizeof(*t->viewl));
    join_section(b,JOIN_SEC_IKV,t->nikv,t->ikv,(uint64_t)t->nikv*sizeof(*t->ikv));
    join_section(b,JOIN_SEC_RCNT,t->nrcnt,t->rcnt,(uint64_t)t->nrcnt*sizeof(*t->rcnt));
    join_section(b,JOIN_SEC_ECOS,t->necos,t->ecos,(uint64_t)t->necos*sizeof(*t->ecos));
    join_section(b,JOIN_SEC_SHPW,t->nshpw,t->shpw,(uint64_t)t->nshpw*sizeof(*t->shpw));
    join_section(b,JOIN_SEC_TRDN,t->ntrdn,t->trdn,(uint64_t)t->ntrdn*sizeof(*t->trdn));
    join_section(b,JOIN_SEC_NPCA,t->nnpca,t->npca,(uint64_t)t->nnpca*sizeof(*t->npca));
    join_section(b,JOIN_SEC_JOBG,t->njobg,t->jobg,(uint64_t)t->njobg*sizeof(*t->jobg));
    join_section(b,JOIN_SEC_CODE,t->ncode,t->code,(uint64_t)t->ncode*sizeof(*t->code));
    join_section(b,JOIN_SEC_ENGR,t->nengr,t->engr,(uint64_t)t->nengr*sizeof(*t->engr));
    join_section(b,JOIN_SEC_SHGR,t->nshgr,t->shgr,(uint64_t)t->nshgr*sizeof(*t->shgr));
    join_section(b,JOIN_SEC_BNDS,t->nbnds,t->bnds,(uint64_t)t->nbnds*sizeof(*t->bnds));
    join_section(b,JOIN_SEC_OMGS,t->nomgs,t->omgs,(uint64_t)t->nomgs*sizeof(*t->omgs));
    join_section(b,JOIN_SEC_RESV,t->nresv,t->resv,(uint64_t)t->nresv*sizeof(*t->resv));
    join_section(b,JOIN_SEC_PCSV,t->npcsv,t->pcsv,(uint64_t)t->npcsv*sizeof(*t->pcsv));
    unsigned char sha[32];if(!b->failed) {lockstep_sha256(b->data,b->size,sha);join_put(b,sha,32);}
    return !b->failed;
}
static void *join_copy(const unsigned char *p,uint64_t bytes) {
    void *q=malloc(bytes?(size_t)bytes:1);if(q && bytes) memcpy(q,p,(size_t)bytes);return q;
}
/* Every hash, every record shape, the portable digest; 0 with *why set. */
static int join_package_parse(const unsigned char *data,size_t size,join_tables *t,const char **why) {
    memset(t,0,sizeof(*t));*why="package_length";
    if(size<96+32) return 0;
    unsigned char sha[32];lockstep_sha256(data,size-32,sha);*why="package_sha";
    if(memcmp(sha,data+size-32,32)) return 0;
    uint32_t version,count,slot,flags;uint64_t tick,game,real,reserved;*why="package_magic";
    memcpy(&version,data+8,4);memcpy(&count,data+12,4);memcpy(&tick,data+16,8);memcpy(&game,data+24,8);memcpy(&real,data+32,8);
    memcpy(&slot,data+80,4);memcpy(&flags,data+84,4);memcpy(&reserved,data+88,8);
    if(memcmp(data,"X4MPJN01",8) || version<1 || version>JOIN_PACKAGE_VERSION || reserved || count>75) return 0;
    memcpy(t->save_sha,data+40,32);memcpy(&t->save_bytes,data+72,8);t->flags=flags;t->version=version;
    const uint32_t last=version>=13?JOIN_SEC_PCSV:version>=12?JOIN_SEC_GRAVQ:version>=11?JOIN_SEC_GRAVS:version>=10?JOIN_SEC_BULLET:version>=9?JOIN_SEC_STEERPT:version>=8?JOIN_SEC_DOCK:version>=7?JOIN_SEC_ROUTE:version>=6?JOIN_SEC_AGENT:version>=5?JOIN_SEC_SPLINE:version>=4?JOIN_SEC_REGION:version>=3?JOIN_SEC_KEYS:
                        version>=2?JOIN_SEC_POSE:JOIN_SEC_LUA;
    size_t at=96,end=size-32;unsigned __int128 seen=0;   /* 128 bits: section kinds reach 64 (version 13, ECOS) */
    for(uint32_t s=0;s<count;s++) {
        uint32_t kind,n;uint64_t bytes;*why="section_head";
        if(end-at<48) return 0;
        memcpy(&kind,data+at,4);memcpy(&n,data+at+4,4);memcpy(&bytes,data+at+8,8);
        if(kind<JOIN_SEC_GATE || kind>last || (seen>>kind&1u) || bytes>end-at-48 || n>JOIN_MAX_RECORDS) return 0;
        seen|=(unsigned __int128)1<<kind;const unsigned char *payload=data+at+48;lockstep_sha256(payload,(size_t)bytes,sha);
        if(memcmp(sha,data+at+16,32)) {*why="section_sha";return 0;}
        static const size_t sizes[]={0,sizeof(join_gate_rec),sizeof(join_id_rec),sizeof(join_wake_rec),sizeof(join_vec_rec),sizeof(join_md_rec),0,
                                     sizeof(join_ctx_rec),0,sizeof(join_health_rec),sizeof(join_pose_rec),sizeof(join_shield_rec),
                                     sizeof(join_nav_rec),sizeof(join_attn_rec),sizeof(join_key_rec),sizeof(join_path_rec),
                                     sizeof(join_region_rec),sizeof(join_flight_rec),sizeof(join_spline_rec),sizeof(join_agent_rec),
                                     sizeof(join_route_rec),sizeof(join_param_rec),sizeof(join_dock_rec),sizeof(join_steer_rec),
                                     sizeof(join_route_rec),sizeof(join_solver_rec),sizeof(join_bullet_rec),
                                     sizeof(join_grav_rec),sizeof(join_gravc_rec),sizeof(join_gravs_rec),
                                     sizeof(join_splm_rec),sizeof(join_splme_rec),sizeof(join_spline_rec),sizeof(join_gravn_rec),
                                     sizeof(join_gravp_rec),sizeof(join_gravq_rec),
                                     sizeof(join_jobe_rec),sizeof(join_evqt_rec),sizeof(join_gravd_rec),sizeof(join_viewk_rec),
                                     sizeof(join_viewkc_rec),sizeof(join_viewf_rec),sizeof(join_viewp_rec),sizeof(join_shoot_rec),sizeof(join_attk_rec),sizeof(join_livef_rec),sizeof(join_spec_rec),sizeof(join_navc_rec),sizeof(join_jobs_rec),sizeof(join_gone_rec),sizeof(join_body_rec),sizeof(join_bupd_rec),sizeof(join_ldc_rec),sizeof(join_navt_rec),sizeof(join_nmesh_rec),sizeof(join_noct_rec),sizeof(join_agtt_rec),sizeof(join_agcb_rec),sizeof(join_pbnk_rec),sizeof(join_pend_rec),sizeof(join_gravb_rec),sizeof(join_viewl_rec),sizeof(join_ikv_rec),sizeof(join_rcnt_rec),sizeof(join_ecos_rec),sizeof(join_shpw_rec),sizeof(join_trdn_rec),sizeof(join_npca_rec),sizeof(join_jobg_rec),sizeof(join_code_rec),sizeof(join_engr_rec),sizeof(join_shgr_rec),sizeof(join_bnds_rec),sizeof(join_omgs_rec),sizeof(join_resv_rec),sizeof(join_solver_rec)};
        *why="section_shape";
        if(kind==JOIN_SEC_MODULES || kind==JOIN_SEC_LUA) {
            uint32_t lines=0;for(uint64_t i=0;i<bytes;i++) {if(payload[i]=='\n') ++lines;else if(payload[i]<32 || payload[i]>126) return 0;}
            if(lines!=n || (bytes && payload[bytes-1]!='\n')) return 0;
            char *copy=join_copy(payload,bytes);if(!copy) {*why="memory";return 0;}
            if(kind==JOIN_SEC_MODULES) {t->modules=copy;t->modules_len=(size_t)bytes;t->nmodules=n;}
            else {t->lua=copy;t->lua_len=(size_t)bytes;t->nlua=n;}
        } else if(kind==JOIN_SEC_GATE) {
            if(n!=1 || bytes!=sizeof(join_gate_rec)) return 0;
            memcpy(&t->gate,payload,sizeof(t->gate));
            *why="gate_fixture";for(unsigned i=0;i<12;i++) if(t->gate.fixture[i]>=SNAPSHOT_VIEWS) return 0;
        } else {
            if(bytes!=(uint64_t)n*sizes[kind]) return 0;
            void *copy=join_copy(payload,bytes);if(!copy) {*why="memory";return 0;}
            if(kind==JOIN_SEC_IDS) {t->ids=copy;t->nids=n;}
            else if(kind==JOIN_SEC_WAKE) {t->wake=copy;t->nwake=n;}
            else if(kind==JOIN_SEC_WAKEVEC) {t->vec=copy;t->nvec=n;}
            else if(kind==JOIN_SEC_MDHEAP) {t->md=copy;t->nmd=n;}
            else if(kind==JOIN_SEC_HEALTH) {t->health=copy;t->nhealth=n;}
            else if(kind==JOIN_SEC_POSE) {t->pose=copy;t->npose=n;}
            else if(kind==JOIN_SEC_SHIELD) {t->shield=copy;t->nshield=n;}
            else if(kind==JOIN_SEC_NAV) {t->nav=copy;t->nnav=n;}
            else if(kind==JOIN_SEC_ATTN) {t->attn=copy;t->nattn=n;}
            else if(kind==JOIN_SEC_KEYS) {t->keys=copy;t->nkeys=n;}
            else if(kind==JOIN_SEC_PATH) {t->path=copy;t->npath=n;}
            else if(kind==JOIN_SEC_REGION) {t->region=copy;t->nregion=n;}
            else if(kind==JOIN_SEC_FLIGHT) {t->flight=copy;t->nflight=n;}
            else if(kind==JOIN_SEC_SPLINE) {t->spline=copy;t->nspline=n;}
            else if(kind==JOIN_SEC_AGENT) {t->agent=copy;t->nagent=n;}
            else if(kind==JOIN_SEC_ROUTE) {t->route=copy;t->nroute=n;}
            else if(kind==JOIN_SEC_PARAM) {t->param=copy;t->nparam=n;}
            else if(kind==JOIN_SEC_DOCK) {t->dock=copy;t->ndock=n;}
            else if(kind==JOIN_SEC_STEER) {t->steer=copy;t->nsteer=n;}
            else if(kind==JOIN_SEC_STEERPT) {t->steerpt=copy;t->nsteerpt=n;}
            else if(kind==JOIN_SEC_SOLVER) {t->solver=copy;t->nsolver=n;}
            else if(kind==JOIN_SEC_BULLET) {t->bullet=copy;t->nbullet=n;}
            else if(kind==JOIN_SEC_GRAV) {t->grav=copy;t->ngrav=n;}
            else if(kind==JOIN_SEC_GRAVC) {t->gravc=copy;t->ngravc=n;}
            else if(kind==JOIN_SEC_GRAVS) {t->gravs=copy;t->ngravs=n;}
            else if(kind==JOIN_SEC_SPLM) {t->splm=copy;t->nsplm=n;}
            else if(kind==JOIN_SEC_SPLME) {t->splme=copy;t->nsplme=n;}
            else if(kind==JOIN_SEC_SPLMSEG) {t->splmseg=copy;t->nsplmseg=n;}
            else if(kind==JOIN_SEC_GRAVN) {t->gravn=copy;t->ngravn=n;}
            else if(kind==JOIN_SEC_GRAVP) {t->gravp=copy;t->ngravp=n;}
            else if(kind==JOIN_SEC_GRAVQ) {t->gravq=copy;t->ngravq=n;}
            else if(kind==JOIN_SEC_JOBE) {t->jobe=copy;t->njobe=n;}
            else if(kind==JOIN_SEC_EVQT) {t->evqt=copy;t->nevqt=n;}
            else if(kind==JOIN_SEC_GRAVD) {t->gravd=copy;t->ngravd=n;}
            else if(kind==JOIN_SEC_VIEWK) {t->viewk=copy;t->nviewk=n;}
            else if(kind==JOIN_SEC_VIEWKC) {t->viewkc=copy;t->nviewkc=n;}
            else if(kind==JOIN_SEC_VIEWF) {t->viewf=copy;t->nviewf=n;}
            else if(kind==JOIN_SEC_VIEWP) {t->viewp=copy;t->nviewp=n;}
            else if(kind==JOIN_SEC_SHOOT) {t->shoot=copy;t->nshoot=n;}
            else if(kind==JOIN_SEC_ATTK) {t->attk=copy;t->nattk=n;}
            else if(kind==JOIN_SEC_LIVEF) {t->livef=copy;t->nlivef=n;}
            else if(kind==JOIN_SEC_SPEC) {t->spec=copy;t->nspec=n;}
            else if(kind==JOIN_SEC_NAVC) {t->navc=copy;t->nnavc=n;}
            else if(kind==JOIN_SEC_JOBS) {t->jobs=copy;t->njobs=n;}
            else if(kind==JOIN_SEC_GONE) {t->gone=copy;t->ngone=n;}
            else if(kind==JOIN_SEC_BODY) {t->body=copy;t->nbody=n;}
            else if(kind==JOIN_SEC_BUPD) {t->bupd=copy;t->nbupd=n;}
            else if(kind==JOIN_SEC_LDC) {t->ldc=copy;t->nldc=n;}
            else if(kind==JOIN_SEC_NAVT) {t->navt=copy;t->nnavt=n;}
            else if(kind==JOIN_SEC_NMESH) {t->nmesh=copy;t->nnmesh=n;}
            else if(kind==JOIN_SEC_NOCT) {t->noct=copy;t->nnoct=n;}
            else if(kind==JOIN_SEC_AGTT) {t->agtt=copy;t->nagtt=n;}
            else if(kind==JOIN_SEC_AGCB) {t->agcb=copy;t->nagcb=n;}
            else if(kind==JOIN_SEC_PBNK) {t->pbnk=copy;t->npbnk=n;}
            else if(kind==JOIN_SEC_PEND) {t->pend=copy;t->npend=n;}
            else if(kind==JOIN_SEC_GRAVB) {t->gravb=copy;t->ngravb=n;}
            else if(kind==JOIN_SEC_VIEWL) {t->viewl=copy;t->nviewl=n;}
            else if(kind==JOIN_SEC_IKV) {t->ikv=copy;t->nikv=n;}
            else if(kind==JOIN_SEC_RCNT) {t->rcnt=copy;t->nrcnt=n;}
            else if(kind==JOIN_SEC_ECOS) {t->ecos=copy;t->necos=n;}
            else if(kind==JOIN_SEC_SHPW) {t->shpw=copy;t->nshpw=n;}
            else if(kind==JOIN_SEC_TRDN) {t->trdn=copy;t->ntrdn=n;}
            else if(kind==JOIN_SEC_NPCA) {t->npca=copy;t->nnpca=n;}
            else if(kind==JOIN_SEC_JOBG) {t->jobg=copy;t->njobg=n;}
            else if(kind==JOIN_SEC_CODE) {t->code=copy;t->ncode=n;}
            else if(kind==JOIN_SEC_ENGR) {t->engr=copy;t->nengr=n;}
            else if(kind==JOIN_SEC_SHGR) {t->shgr=copy;t->nshgr=n;}
            else if(kind==JOIN_SEC_BNDS) {t->bnds=copy;t->nbnds=n;}
            else if(kind==JOIN_SEC_OMGS) {t->omgs=copy;t->nomgs=n;}
            else if(kind==JOIN_SEC_RESV) {t->resv=copy;t->nresv=n;}
            else if(kind==JOIN_SEC_PCSV) {t->pcsv=copy;t->npcsv=n;}
            else {t->ctx=copy;t->nctx=n;}
        }
        at+=48+(size_t)bytes;
    }
    *why="package_layout";if(at!=end || !(seen>>JOIN_SEC_GATE&1u)) return 0;
    if(version>=2 && (!(seen>>JOIN_SEC_HEALTH&1u) || !(seen>>JOIN_SEC_POSE&1u))) return 0;   /* v2: the world table, even if empty */
    if(version>=3 && ((seen>>JOIN_SEC_SHIELD&15u)!=15u)) return 0;                              /* v3: SHIELD, NAV, ATTN, KEYS */
    if(version>=4 && ((seen>>JOIN_SEC_PATH&3u)!=3u)) return 0;                                  /* v4: PATH, REGION */
    if(version>=5 && ((seen>>JOIN_SEC_FLIGHT&3u)!=3u)) return 0;                                /* v5: FLIGHT, SPLINE */
    if(version>=6 && !(seen>>JOIN_SEC_AGENT&1u)) return 0;                                      /* v6: AGENT */
    if(version>=7 && !(seen>>JOIN_SEC_ROUTE&1u)) return 0;                                      /* v7: ROUTE */
    if(version>=8 && ((seen>>JOIN_SEC_PARAM&3u)!=3u)) return 0;                                 /* v8: PARAM, DOCK */
    if(version>=9 && ((seen>>JOIN_SEC_STEER&3u)!=3u)) return 0;                                 /* v9: STEER, STEERPT */
    if(version>=10 && ((seen>>JOIN_SEC_SOLVER&3u)!=3u)) return 0;                               /* v10: SOLVER, BULLET */
    if(version>=11 && ((seen>>JOIN_SEC_GRAV&7u)!=7u)) return 0;                                 /* v11: GRAV, GRAVC, GRAVS */
    if(version>=12 && ((seen>>JOIN_SEC_SPLM&63u)!=63u)) return 0;                               /* v12: SPLM .. GRAVQ */
    if(version>=13 && ((seen>>JOIN_SEC_JOBE&1099511627775ull)!=1099511627775ull)) return 0;     /* v13: JOBE .. PCSV */
    *why="world_order";   /* the world records sorted by portable ID, each once (the admission's identity lookups rely on it) */
    for(uint32_t i=1;i<t->nhealth;i++) if(t->health[i].portable<=t->health[i-1].portable) return 0;
    for(uint32_t i=1;i<t->npose;i++) if(t->pose[i].portable<=t->pose[i-1].portable) return 0;
    for(uint32_t i=1;i<t->nshield;i++) if(t->shield[i].portable<=t->shield[i-1].portable) return 0;
    for(uint32_t i=1;i<t->nnav;i++) if(t->nav[i].portable<=t->nav[i-1].portable) return 0;
    for(uint32_t i=1;i<t->nattn;i++) if(t->attn[i].portable<=t->attn[i-1].portable) return 0;
    for(uint32_t i=0;i<t->nkeys;i++) {   /* by (table, key), each once; a known table, an actor only in the actor tables */
        const join_key_rec *k=&t->keys[i];
        if(!k->key || !k->table || k->table>=JOIN_KEYS_TABLES ||
           (k->table>=JOIN_KEYS_ATTENTION_ACTOR?!k->actor || k->actor>ACTOR_CAPACITY:k->actor!=0)) {*why="keys_shape";return 0;}
        if(i && (k->table<t->keys[i-1].table || (k->table==t->keys[i-1].table && k->key<=t->keys[i-1].key))) return 0;
    }
    /* Version 4: REGION by (region, field), each once; PATH in (kind, vector, batch, position) order with its structure whole
     * (lockstep_join_world.h join_path_shape). */
    for(uint32_t i=0;i<t->nregion;i++) {
        const join_region_rec *g=&t->region[i];
        if(!g->region || g->flags) {*why="region_shape";return 0;}
        if(i && (g->region<t->region[i-1].region || (g->region==t->region[i-1].region && g->field<=t->region[i-1].field))) return 0;
    }
    if(version>=4 && !join_path_shape(t->path,t->npath)) {*why="path_shape";return 0;}
    /* Version 5: FLIGHT by portable ID, each once; SPLINE by (portable, part, index), exactly the records each FLIGHT names
     * (lockstep_join_world.h join_flight_shape). */
    for(uint32_t i=1;i<t->nflight;i++) if(t->flight[i].portable<=t->flight[i-1].portable) return 0;
    if(!join_flight_shape(t->flight,t->nflight,t->spline,t->nspline)) {*why="flight_shape";return 0;}
    /* Version 6: AGENT exactly one record per FLIGHT record, in its order (lockstep_join_world.h join_agent_shape). */
    if(version>=6 && !join_agent_shape(t->flight,t->nflight,t->agent,t->nagent)) {*why="agent_shape";return 0;}
    /* Version 7: ROUTE one head per AGENT record, in its order, each with exactly the points its lengths name (join_route_shape). */
    if(version>=7 && !join_route_shape(t->agent,t->nagent,t->route,t->nroute)) {*why="route_shape";return 0;}
    /* Version 8: PARAM one record per AGENT record, in its order (join_param_shape); DOCK by portable ID, each once, of a dock or
     * undock controller with its unused fields 0 (join_dock_shape). */
    if(version>=8 && !join_param_shape(t->agent,t->nagent,t->param,t->nparam)) {*why="param_shape";return 0;}
    for(uint32_t i=1;i<t->ndock;i++) if(t->dock[i].portable<=t->dock[i-1].portable) return 0;
    if(!join_dock_shape(t->dock,t->ndock)) {*why="dock_shape";return 0;}
    /* Version 9: STEER by portable ID, one per FLIGHT record of a steering model, in its order; STEERPT exactly the path points
     * each names (join_steer_shape). */
    if(version>=9 && !join_steer_shape(t->flight,t->nflight,t->steer,t->nsteer,t->steerpt,t->nsteerpt)) {*why="steer_shape";return 0;}
    /* Version 10: SOLVER by portable ID, BULLET by (zone, position); each record's own shape. */
    if(version>=10 && !join_solver_shape(t->solver,t->nsolver)) {*why="solver_shape";return 0;}
    if(version>=10 && !join_bullet_shape(t->bullet,t->nbullet)) {*why="bullet_shape";return 0;}
    /* Version 11: GRAV by portable ID, GRAVC exactly the contacts each names, GRAVS by (kind, index). */
    if(version>=11 && !join_grav_shape(t->grav,t->ngrav,t->gravc,t->ngravc,t->gravs,t->ngravs)) {*why="grav_shape";return 0;}
    /* Version 12: SPLM by portable ID, SPLME and SPLMSEG exactly the entries and segments each names. */
    if(version>=12 && !join_splm_shape(t->splm,t->nsplm,t->splme,t->nsplme,t->splmseg,t->nsplmseg)) {*why="splm_shape";return 0;}
    if(version>=12 && !join_gravn_shape(t->gravn,t->ngravn)) {*why="gravn_shape";return 0;}
    if(version>=12 && !join_gravp_shape(t->gravp,t->ngravp)) {*why="gravp_shape";return 0;}
    if(version>=12 && !join_gravq_shape(t->gravq,t->ngravq)) {*why="gravq_shape";return 0;}
    if(version>=13 && !join_jobe_shape(t->jobe,t->njobe)) {*why="jobe_shape";return 0;}
    if(version>=13 && !join_evqt_shape(t->evqt,t->nevqt)) {*why="evqt_shape";return 0;}
    if(version>=13 && !join_gravd_shape(t->gravd,t->ngravd)) {*why="gravd_shape";return 0;}
    if(version>=13 && !join_viewk_shape(t->viewk,t->nviewk,t->viewkc,t->nviewkc)) {*why="viewk_shape";return 0;}
    if(version>=13) for(uint32_t i=0;i<t->nviewf;i++) if(!t->viewf[i].portable || (i && t->viewf[i-1].portable>=t->viewf[i].portable)) {*why="viewf_shape";return 0;}
    if(version>=13) for(uint32_t i=0;i<t->nviewp;i++) if(!t->viewp[i].portable || (i && t->viewp[i-1].portable>=t->viewp[i].portable)) {*why="viewp_shape";return 0;}
    if(version>=13) for(uint32_t i=0;i<t->nshoot;i++) {
        const join_shoot_rec *r=&t->shoot[i];
        if(!r->portable || (i && t->shoot[i-1].portable>=r->portable) || r->has_ctl>1u || !r->ctl_vt!=!r->has_ctl || r->wflags[0]>1u ||
           r->wflags[1]>1u || r->wflags[2]>1u || r->cflags[0]>1u || r->cflags[1]>1u || r->pad[0] || r->pad[1] || (!r->has_ctl && r->tol))
            {*why="shoot_shape";return 0;}
    }
    if(version>=13) for(uint32_t i=0;i<t->nattk;i++) {
        const join_attk_rec *r=&t->attk[i];
        if(!r->portable || (i && t->attk[i-1].portable>=r->portable) || r->pad || !(r->times[0]|r->times[1]|r->times[2])) {*why="attk_shape";return 0;}
    }
    if(version>=13) for(uint32_t i=0;i<t->nlivef;i++) {
        const join_livef_rec *r=&t->livef[i];
        if(!r->portable || (i && t->livef[i-1].portable>=r->portable) || (r->set&r->clear) || ((r->set|r->clear)&~JOIN_LIVEF_BITS) ||
           !(r->set|r->clear)) {*why="livef_shape";return 0;}
    }
    if(version>=13) for(uint32_t i=0;i<t->nspec;i++) {
        const join_spec_rec *r=&t->spec[i],*p=i?&t->spec[i-1]:NULL;
        if(!r->portable || (p && (p->portable>r->portable || (p->portable==r->portable && p->kind>=r->kind)))) {*why="spec_shape";return 0;}
    }
    if(version>=13) for(uint32_t i=0;i<t->nnavc;i++)
        if(!t->navc[i].portable || (i && t->navc[i-1].portable>=t->navc[i].portable)) {*why="navc_shape";return 0;}
    if(version>=13) for(uint32_t i=0;i<t->njobs;i++)
        if(!t->jobs[i].hash || (i && t->jobs[i-1].index>=t->jobs[i].index)) {*why="jobs_shape";return 0;}
    if(version>=13) for(uint32_t i=0;i<t->ngone;i++)
        if(!t->gone[i].portable || (i && t->gone[i-1].portable>=t->gone[i].portable)) {*why="gone_shape";return 0;}
    if(version>=13) for(uint32_t i=0;i<t->nbody;i++) {
        const join_body_rec *r=&t->body[i],*p=i?&t->body[i-1]:NULL;
        if(!r->portable || r->which>1u || r->pad>1u || (p && (p->portable>r->portable || (p->portable==r->portable && p->which>=r->which)))) {*why="body_shape";return 0;}
    }
    if(version>=13) for(uint32_t i=0;i<t->nbupd;i++)
        if(!t->bupd[i].portable || (i && t->bupd[i-1].portable>=t->bupd[i].portable)) {*why="bupd_shape";return 0;}
    if(version>=13) for(uint32_t i=0;i<t->nldc;i++) {
        const join_ldc_rec *r=&t->ldc[i];
        if(!r->portable || (i && t->ldc[i-1].portable>=r->portable)) {*why="ldc_shape";return 0;}
        for(unsigned b=0;b<sizeof(r->pad);b++) if(r->pad[b]) {*why="ldc_shape";return 0;}
    }
    if(version>=13) for(uint32_t i=0;i<t->nnavt;i++)
        if(!t->navt[i].portable || (i && t->navt[i-1].portable>=t->navt[i].portable)) {*why="navt_shape";return 0;}
    if(version>=13 && t->nnmesh==1) {*why="nmesh_shape";return 0;}
    if(version>=13) for(uint32_t i=3;i<t->nnmesh;i++) if(t->nmesh[i-1].v>=t->nmesh[i].v) {*why="nmesh_shape";return 0;}
    if(version>=13 && t->nnoct && !join_noct_shape(t->noct,t->nnoct)) {*why="noct_shape";return 0;}
    if(version>=13 && t->nagtt) {
        if(t->nagtt!=t->nagent) {*why="agtt_shape";return 0;}
        for(uint32_t i=0;i<t->nagtt;i++) if(t->agtt[i].portable!=t->agent[i].portable) {*why="agtt_shape";return 0;}
    }
    if(version>=13) for(uint32_t i=0;i<t->nagcb;i++)
        if(!t->agcb[i].portable || (i && t->agcb[i-1].portable>=t->agcb[i].portable)) {*why="agcb_shape";return 0;}
    if(version>=13) for(uint32_t i=0;i<t->npbnk;i++) {
        const join_pbnk_rec *r=&t->pbnk[i];
        if(!r->portable || r->pad || (r->flags&~63u) || (i && t->pbnk[i-1].portable>=r->portable)) {*why="pbnk_shape";return 0;}
    }
    if(version>=13) for(uint32_t i=0;i<t->npend;i++)
        if(!t->pend[i].portable || !t->pend[i].word || t->pend[i].pad || (i && t->pend[i-1].portable>=t->pend[i].portable)) {*why="pend_shape";return 0;}
    if(version>=13) for(uint32_t i=0;i<t->ngravb;i++) {
        const join_gravb_rec *r=&t->gravb[i],*p=i?&t->gravb[i-1]:NULL;
        if(!r->portable || r->role>=JOIN_GRAVB_ROLES || (p && (p->portable>r->portable || (p->portable==r->portable && p->role>=r->role)))) {*why="gravb_shape";return 0;}
    }
    if(version>=13) for(uint32_t i=0;i<t->nviewl;i++) {
        const join_viewl_rec *r=&t->viewl[i];int pad=0;
        for(unsigned k=0;k<sizeof(r->pad);k++) pad|=r->pad[k];
        if(!r->portable || pad || r->live>1u || r->player>1u || !(r->live|r->player) || (i && t->viewl[i-1].portable>=r->portable)) {*why="viewl_shape";return 0;}
    }
    if(version>=13) for(uint32_t i=0;i<t->nikv;i++) {
        const join_ikv_rec *r=&t->ikv[i],*p=i?&t->ikv[i-1]:NULL;
        if(!r->portable || r->pad || r->group>=JOIN_IKV_GROUPS || r->joint>=JOIN_IKV_JOINTS ||
           (p && (p->portable>r->portable || (p->portable==r->portable && (p->group>r->group || (p->group==r->group && p->joint>=r->joint))))))
            {*why="ikv_shape";return 0;}
    }
    if(version>=13) for(uint32_t i=0;i<t->nrcnt;i++) {
        const join_rcnt_rec *r=&t->rcnt[i];
        if(!(r->portable>>63) || r->pad || !r->count || (i && t->rcnt[i-1].portable>=r->portable)) {*why="rcnt_shape";return 0;}
    }
    if(version>=13) for(uint32_t i=0;i<t->necos;i++) {
        const join_ecos_rec *r=&t->ecos[i];
        if(!r->portable || (i && t->ecos[i-1].portable>=r->portable)) {*why="ecos_shape";return 0;}
    }
    if(version>=13) for(uint32_t i=0;i<t->nshpw;i++) {
        const join_shpw_rec *r=&t->shpw[i];
        if(!r->portable || (i && t->shpw[i-1].portable>=r->portable)) {*why="shpw_shape";return 0;}
    }
    if(version>=13) for(uint32_t i=0;i<t->ntrdn;i++) {
        const join_trdn_rec *r=&t->trdn[i];
        if(!r->portable || (i && t->trdn[i-1].portable>=r->portable)) {*why="trdn_shape";return 0;}
    }
    if(version>=13) for(uint32_t i=0;i<t->nnpca;i++) {
        const join_npca_rec *r=&t->npca[i];
        if(!r->portable || !r->flags || r->flags>3u || (i && t->npca[i-1].portable>=r->portable)) {*why="npca_shape";return 0;}
    }
    if(version>=13) for(uint32_t i=0;i<t->njobg;i++) {
        const join_jobg_rec *r=&t->jobg[i];
        if(!r->hash || (i && t->jobg[i-1].hash>=r->hash)) {*why="jobg_shape";return 0;}
    }
    if(version>=13 && (t->ncode>1u || (t->ncode && (t->code[0].counter>0x1000000u || t->code[0].seed>0xffffffu)))) {*why="code_shape";return 0;}
    if(version>=13) for(uint32_t i=0;i<t->nengr;i++)
        if(!t->engr[i].portable || (i && t->engr[i-1].portable>=t->engr[i].portable)) {*why="engr_shape";return 0;}
    if(version>=13) for(uint32_t i=0;i<t->nshgr;i++) {
        const join_shgr_rec *a=&t->shgr[i],*p=i?&t->shgr[i-1]:NULL;
        if(!a->container || !a->ship || a->pad || (p && (p->container>a->container || (p->container==a->container &&
           (p->ship>a->ship || (p->ship==a->ship && p->key>=a->key)))))) {*why="shgr_shape";return 0;}
    }
    if(version>=13) for(uint32_t i=0;i<t->nbnds;i++)
        if(!t->bnds[i].portable || t->bnds[i].pad || (i && t->bnds[i-1].portable>=t->bnds[i].portable)) {*why="bnds_shape";return 0;}
    if(version>=13) for(uint32_t i=0;i<t->nomgs;i++) {
        const join_omgs_rec *o=&t->omgs[i],*p=i?&t->omgs[i-1]:NULL;
        if(!o->portable || o->which>1u || o->pad || (p && (p->portable>o->portable || (p->portable==o->portable && p->which>=o->which)))) {*why="omgs_shape";return 0;}
    }
    if(version>=13) for(uint32_t i=0;i<t->nresv;i++) {
        const join_resv_rec *a=&t->resv[i],*p=i?&t->resv[i-1]:NULL;
        if(!a->zone || !a->owner || (p && (p->zone>a->zone || (p->zone==a->zone && (p->owner>a->owner || (p->owner==a->owner &&
           p->index>=a->index)))))) {*why="resv_shape";return 0;}
    }
    if(version>=13 && !join_solver_shape(t->pcsv,t->npcsv)) {*why="pcsv_shape";return 0;}
    *why="gate_header";if(t->gate.tick!=tick || t->gate.game!=game || t->gate.real!=real || t->gate.slot!=slot) return 0;
    unsigned char portable[16];*why="portable_digest";
    if(!join_portable(t,portable) || memcmp(portable,t->gate.portable,16)) return 0;
    *why="ok";return 1;
}

/* ---- files (UTF-8 paths from Lua) ---- */
static int join_wide(const char *utf8,WCHAR *out,size_t cap) {
    int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,utf8,-1,out,(int)cap);return n>0 && (size_t)n<cap;
}
static int join_file_sha(const WCHAR *path,unsigned char sha[32],uint64_t *bytes) {
    HANDLE f=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL);
    if(f==INVALID_HANDLE_VALUE) return 0;
    lockstep_sha256_ctx c;lockstep_sha256_init(&c);unsigned char *buffer=malloc(1u<<20);DWORD got=0;int ok=buffer!=NULL;*bytes=0;
    while(ok) {if(!ReadFile(f,buffer,1u<<20,&got,NULL)) {ok=0;break;} if(!got) break;lockstep_sha256_update(&c,buffer,got);*bytes+=got;}
    free(buffer);CloseHandle(f);if(ok) lockstep_sha256_final(&c,sha);return ok;
}
static int join_file_read(const WCHAR *path,unsigned char **data,size_t *size) {
    HANDLE f=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL);
    LARGE_INTEGER s;*data=NULL;
    if(f==INVALID_HANDLE_VALUE) return 0;
    if(!GetFileSizeEx(f,&s) || s.QuadPart<=0 || s.QuadPart>(LONGLONG)1<<31) {CloseHandle(f);return 0;}
    unsigned char *p=malloc((size_t)s.QuadPart);size_t at=0;int ok=p!=NULL;
    while(ok && at<(size_t)s.QuadPart) {DWORD got=0;if(!ReadFile(f,p+at,(DWORD)((size_t)s.QuadPart-at),&got,NULL) || !got) ok=0;at+=got;}
    CloseHandle(f);if(!ok) {free(p);return 0;}
    *data=p;*size=at;return 1;
}
static int join_file_write(const WCHAR *path,const unsigned char *data,size_t size) {
    WCHAR temp[MAX_PATH+8];if(wcslen(path)+5>=MAX_PATH+8) return 0;
    wcscpy(temp,path);wcscat(temp,L".tmp");
    HANDLE f=CreateFileW(temp,GENERIC_WRITE,0,NULL,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);
    if(f==INVALID_HANDLE_VALUE) return 0;
    size_t at=0;int ok=1;
    while(ok && at<size) {DWORD put=0;if(!WriteFile(f,data+at,(DWORD)(size-at>(1u<<30)?(1u<<30):size-at),&put,NULL) || !put) ok=0;at+=put;}
    ok=FlushFileBuffers(f) && ok;CloseHandle(f);
    return ok && MoveFileExW(temp,path,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH);
}

/* ---- the engine save (Windows) ---- */
typedef struct {union {char buf[16];char *ptr;} u;uint64_t size,cap;} join_msvc_string;
static void join_string(join_msvc_string *s,char *storage,const char *text) {
    size_t n=strlen(text);memset(s,0,sizeof(*s));
    if(n<16) {memcpy(s->u.buf,text,n+1);s->cap=15;}
    else {memcpy(storage,text,n+1);s->u.ptr=storage;s->cap=n;}
    s->size=n;
}
typedef unsigned char (*join_save_fn)(void *game,const join_msvc_string *file,const join_msvc_string *name,char online);
typedef int (*join_possible_fn)(void *game,char online);
/* tests replace the call */
static unsigned char join_save_native(const char *file,const char *name) {
    char store_file[64],store_name[64];join_msvc_string f,n;join_string(&f,store_file,file);join_string(&n,store_name,name);
    return ((join_save_fn)(image+JOIN_SAVE))(image+JOIN_SAVE_GAME,&f,&n,0);
}
static unsigned char (*join_save_call)(const char *,const char *)=join_save_native;
/* ---- pins: X4.exe as shipped, and as it runs ----
 * The functions the save and the side table rely on (the save, its only call site and IsSavingPossible; AIDirector::Move,
 * SetTimedWake, ClearWake, RequestWake, the tree's insert and erase, MDScheduler_Update and _Schedule, the heap pop,
 * Cue_CreateInstance, Cue_ResumeAfterLoad; the context list's constructor and destructor, Cue_CheckConditions, Cue_Destroy,
 * the sub-cue instances, the MD cue import and export; the static cue's and the MD script's constructors, the library copy and
 * the import's second pass). The session's own modules patch inside several of them (lockstep_ai.h 106e3f6,
 * lockstep_script_rng_md.h 112208b, lockstep_md_resume_rng.h 1115bad, ...), so the live bytes are not the pins at the save
 * tick: live j1-4 refused both savers with 'pins' while those were installed. Two checks instead:
 *   pristine  each range read from the executable file (the build verify() checked by its SHA-256 at the gate's start)
 *             hashes to its pin; once per process (join_arm and join_load warm it on the UI thread, off the boundary);
 *   live      at the save boundary and at the joiner's admission, every byte of a range that differs from the file lies
 *             inside one of this gate's own hooks: a call or jump with a 32-bit displacement (E8, E9; FF15, FF25 through a
 *             pointer) whose destination is this gate's memory (its DLL, or a private committed page outside X4's image).
 * A refusal names its place: pins:<rva> (a pinned range of the file), pins:file (the executable could not be read),
 * patch:<rva> (a live difference that is no hook of ours). JOIN_STATUS pins= and patched= say what was found. */
#define JOIN_IMAGE_SIZE 0x6fa9000u
typedef struct {unsigned rva,size;const char *sha;} join_pin;
static const join_pin join_pin_table[]={
    {0x9fe7a7,40,"9c2e21b664b8abe8790d57211e7c3be0229791a1e22846e179731353d90c5d9b"},     /* the save's only call (UniverseFrameUpdate) */
    {JOIN_SAVE,0x15b4,"b4befd8f2dfb9400c23b256b455591f78723fd3ab6d4d99220bf71f6ef142198"},
    {JOIN_SAVE_POSSIBLE,0xff,"efdee4badfae42d36a47071a0b7736715f5b229130c4d84c339df51800e41a5d"},
    {0x106d250,0x140e,"360cdc20473b7acde457f95c71540da76295a706b8e7509d9b8466a19e706018"},  /* AIDirector::Move */
    {0x10668b0,153,"bb690671b43c8f40ebaf0bff539cf960d47c384ccdca73a964304ae1df2fd851"},     /* SetTimedWake */
    {0x10669c0,0xb5,"ee0472216b11cfa19adbabf701c6147d6841e62850c6704ba0274b1e890c0a8f"},    /* ClearWake */
    {0x106f910,0x67,"da5cdd3e66f63039820bd06f8bca198ad6bc7f02433361157f54f695af893c91"},    /* RequestWake */
    {JOIN_TREE_INSERT,0x150,"369102d595edfaeca8559e98165bc2388e1255ac94bf53e2e285cf42eb25b407"},
    {JOIN_TREE_ERASE,0x9b,"98acc37fb6f3a7e341940cee2fe4f639036d76d6cb7e2d5927cbe0d54b6fe558"},
    {0x1121f80,0x18a,"1a73fd8bd775643c09f921a2b0e956b23ff8d934f524067466f34b93d3e2b8d1"},   /* MDScheduler_Update */
    {0x1122ae0,0x100,"9acbac02ca1639f3d78a4fd37f0f8638017d139af34120972e1fd763dc58b192"},   /* MDScheduler_Schedule */
    {0x115a6f0,0x150,"d109575979f9971f9eca2a5cb0d5484a06fc9b05c11e2e488f6acf3fe5aebc79"},   /* heap pop */
    {0x1111a40,0x389,"bde488a4463be31fa0a9d096c61c1b5922d2b1e3191a939bf11302905e688ee4"},   /* Cue_CreateInstance */
    {0x1115960,0x2cc,"13f50df874238bf75f8b6ed321847b42ef380e507775f419843ede0aefc93439"},   /* Cue_ResumeAfterLoad */
    /* The layouts the keys read: the context list (constructor appends, destructor unlinks), a cue's created list (Cue_
     * CheckConditions appends, Cue_Destroy moves the last into the hole, the save writes it, the import rebuilds it in
     * order) and the sub-cue slots of an instance (FUN_111dbb0). */
    {0x10631b0,0x1d8,"b5468455eb7534da2330706d124b3e70c1aedf137869208687e6686adfb2917d"},   /* AIScriptContext constructor */
    {0x1063410,0x1dd,"eb81b4738530b6dff8707786035e1bde29f761691381b2ebcf9b92151ed4027e"},   /* its destructor */
    {0x111c290,0x2c8,"5f9346c838fde113044bdca972ea8c18f13dadf33fb48dde50e69664ac5688d1"},   /* Cue_CheckConditions */
    {0x1113900,0x64f,"2df542fc69291a980fcab15044f09a3458ce5bf9f1efed7ec79d5f34d4a63914"},   /* Cue_Destroy */
    {0x111dbb0,0x181,"2936e25735e703b06b9563d494dfe8588e4865a14a87890b575d50e608a6ceb5"},   /* an instance's sub-cue instances */
    CUE_PATH_PIN_IMPORT,                                                                     /* MD cue import */
    {0x111a260,0x1163,"59e960f1280ec1c151182e97eba2c3c8f85e8945e4449aa2f1a4e3cce18f1632"},  /* MD cue export */
    /* The static cue path key (J1 j1-7): a static cue's name (+0xc8), script (+0x150) and parent (+0x158) as the XML
     * constructor, a library user's sub-cue copy and the import's second pass write them; the script's name (+0x18).
     * lockstep_cue_name.h CUE_PATH_PINS, which the modules' cue names check live. */
    CUE_PATH_PIN_XML,                                                                        /* static cue from XML */
    CUE_PATH_PIN_COPY,                                                                       /* library sub-cue copy */
    CUE_PATH_PIN_SECOND,                                                                     /* MD import, second pass */
    CUE_PATH_PIN_SCRIPT_XML,                                                                 /* MD script from XML */
    CUE_PATH_PIN_SCRIPT_SAVE,                                                                /* MD script from a save */
    /* The world side table (j1-11, lockstep_join_world.h): the pure transform getters whose vectors POSE carries (0x10ba20
     * reads controller+0x10..+0x4f, 0x46ff70 +0x20..+0x5f) and the shield read whose fields HEALTH carries (value +0x68,
     * changed time +0x70, the recharge source +0x78: lockstep_state.h shield_canonical_pins). */
    {JOIN_POSE_GETTER_10,0x25,"59916bde73b664f995fc0abd6ec35bd3a27996e03eed8b116f8575cde9c1df04"},
    {JOIN_POSE_GETTER_20,0x2d,"d8d240c4c09c717cd26f198a3dc9917b96cb64649408cf4a0a93259604cbb193"},
    {0x79ddb0,376,"3902e1ea32691f964ebafb2d5d930c34b96a6f4058ffe83de28df3cdc0504a21"},
    /* Version 3 (j1-12, report 1.7): NAV's layout as RefreshFromPathingState reads it (refresh +0xc0, cursor +0xc8 against the
     * read selector 0x38f4ee4, pending +0x50 + 16 x bank, banks +0x30 + 0x1a0 x bank, eta +0x150, arrived/notified +0x159/+0x15a),
     * WaitAndSwapBuffers (the only writer of both selectors); ATTN's: SetAttention's level +0x1ec (its first chunk, which the view
     * points hook through a pointer) and the attention timer's due +0x2e0 (past its first 8 bytes: lockstep_attention.h patches
     * them with a jump and a nop). */
    {0xea2a20,537,"d301bea94b279ddce92ced403bafdd17a90ed2eb83c5cc5944cf2ae600fc4a4f"},   /* RefreshFromPathingState */
    {0xeac170,123,"8487d9a8922f92b790f68759933ac3011a10746f828a634ffc5fcb68ab24bd36"},   /* WaitAndSwapBuffers */
    {0x3d5660,0x64,"8e948a0788bbacca91415cb51e228e73bc416c608d2398b07f05312fb19b56eb"},  /* SetAttention */
    {0x6d1ff8,180,"740c43de64e569c5d869023ebc90074b6a2d97e07590404c989698f488111b2e"},   /* the attention timer */
    /* Version 4 (j1-14, report 1.8): PATH's pathing manager as its own code keeps it: Run's wake (semaphores +0x18/+0x10), the
     * pass time +0x200 and the skip flag +0x208 it tests and clears; the load reset that sets the flag; CreateAgent's append to
     * the new-agent list (+0x170 count, +0x178 sentinel, a 0x18-byte node {prev, next, Agent}); RunPass up to the tempzone
     * module's block at 0xeae860 (the list assigned and cleared, the two vectors rebalanced); RemoveAgent (list or batch);
     * AssignAgentToBatch (vectors +0x180/+0x198, round-robin +0x1b8/+0x1c0); a batch's push before its cursor. REGION's: the
     * Region's field vector +0x288 (Region::Move) and the DamageField's last +0x10f0 and due +0x10f8 (lockstep_region_rng.h). */
    {0xeabf00,610,"ab33c0bdd8e30609ba6f21c60741387dcec42558efd38d3e7749185ef2d937ce"},   /* PathingManager::Run */
    {0xeac1f0,399,"409c903c925c7aede481a550bb03366fa2f72edc6cfd4f761a423b6cdcae596a"},   /* its load reset */
    {0xeac530,582,"7b02058e82f6d8acf92e189f5982eae449af06dc045fcb41be1d182ea374267c"},   /* CreateAgent */
    {0xeae3c0,0x4a0,"920a0a0d1197d92359d52e994cc3fe5936cbc95ec8b6267ac98560adbdd677e2"}, /* RunPass to 0xeae860 */
    {0xeaeee0,965,"80abfe30324640bc8b4df4c40109aa24dca2c8e06d2dbfe01df9e1e9bd5dcd27"},   /* RemoveAgent */
    {0xeaf2b0,216,"c44bc6f94a518ef5613a518587617c01250558926488dac7a80f14a970039e11"},   /* AssignAgentToBatch */
    {0xf16880,153,"766550e3932535daf39462a8745cfe37ce6100816fba549819158e95cf0fffb1"},   /* a batch's push */
    {0x767410,477,"2881619c10d25c81e817fb6a142740b71fe32c99d4ec22ebd43c0a838a9a4ef7"},   /* U::Region::Move */
    {0xee16a0,940,"9717109c4eaf193fe03575f6cf27297cca1600a766d94820327ead9bd275fc79"},   /* DamageField's pass */
    /* Version 5 (j1-15, report 1.9): FLIGHT's linear model as its own code keeps it: the Agent update (the model at +0xf10, a
     * type 3 model not stepped on a cheap update: the predicate 0xe848c0); the linear model's step (dt = now - +0x88, the new
     * leg on +0x94, the spline at +0xe0 and its flag +0x130) and constructor (+0x88 = -1, +0x94 = 1, the spline's vtable);
     * the spline's destructor (its vectors at +0x10 and +0x28), its knots (the pairs, +0x40..+0x48), the segment constructor
     * (a BezierSpline vtable at a segment's +0x10), the two vectors' frees (the allocation size class by capacity) and the
     * allocator the joiner calls for a spline the load left empty. */
    {0xe8f250,10613,"ae0d8b729a39a75f83b1c3d783585f587de1cc2e5eff846620f2f0c482504f52"}, /* the Agent update */
    {0xe848c0,46,"ed0c1d6a7560bbadeea18ce0b32fe3ca943d8b2e6d3980e8230da9799470ad60"},    /* its type 3 predicate */
    {0xeb27c0,5212,"7920e6e4ff99bf0d0c348eeab202b0a21f5e9d69c4005ba75d302eac7ea0c8a4"},  /* LinearFlightControlModel's step */
    {0xeb1a10,735,"6692d3e753b9a9d481c7321f77aae910a1213c4826d562c21e0fdffdae64078d"},   /* its constructor */
    {0x173ab0,132,"67f9f1a1f820bcf16fda4408b9c00fff86c5a36be81bf694068ed85f0326a44f"},   /* CompositeSpline<5>'s destructor */
    {0x31c430,391,"94b7060e713882c16cf9e26c73bdb9c1ec4a91d6095b37038d6a948e002f55d2"},   /* its knots */
    {0x337280,155,"d81cd46b07335a375e6ff448895c715c2a099b7ea5e012d0643c7fbe40160e02"},   /* the segment constructor */
    {0x31c3b0,121,"05e7c4785bf8fcf6400013178fc6740ea514525f2dd620bebe6ba0a0f010b2b9"},   /* the segments' free */
    {0x96c40,90,"5852fe2c1852fbfdc8b2d7146a92a52d62d864aa6f2c3ddc7a644fc0341118ef"},     /* the pairs' free */
    {0x974c0,154,"d8b2ea6264b11514092183c7fb1feb553cd7510eb73bc48cc8e57e60ff3508df"},    /* the allocator */
    /* Version 7 (j1-21, report 1.11): ROUTE's waypoint entries as the engine keeps them: the waypoint follower (the Agent update
     * calls it only while +0xf33 is set; it reads the first entry's reference +0x10, transform, key +0x60 and flags +0x75/+0x76
     * and SetStates the model: target +0x20..+0x5f, word +0x60, +0x6c/+0x6d), the U::Path::FlightPoint copy constructor and
     * assignment (+0x10..+0x67, +0x70..+0x7e and +0x80..+0x8f field by field, the owned NavPoint +0x68 deep, the reference's
     * waypoint registration), its destructor body and scalar destructor, the vector's clear and push_back with its growth
     * (allocation, relocation, the old buffer's destroy and free): the joiner rebuilds both vectors through them. */
    {0xe8c380,5290,"dc87fd5afeef267174e674d1aa60a25e803c6571822ecbc7f4aa91344fbe6f49"},     /* the waypoint follower */
    {0xeaf620,49,"0d2c642e7809f6d293ce4b5bcaee47a8443e49fb4ab7c644df2b8f0a0f475e73"},       /* FlightControlModel::SetState */
    {0xeaa6f0,109,"3585026bc4afdb07fa3d1af3488c353d92cf47ec4da0da825cdab13fb5c33c6d"},      /* FlightPoint copy constructor */
    {0xeaa980,514,"852c64f8a3a26286d79d736f680c0d34ea571479e47d59deb60c017eaa7fabc0"},      /* its assignment */
    {0xeaa760,532,"81929fcf527e7fd9d5e5a205ed338ab015a68cd1ff1b1ccf63b96d00d42857e7"},      /* its destructor body */
    {0xea9fa0,119,"8a7b1a970ecadaf6d7084eab2e78f3092627f654fb702c9f3d7ea3131ad15f9e"},      /* its scalar destructor (vtable slot 0) */
    {0xf169e0,97,"2eab285d2aa89fdd0ab61f4a50896ddb631cb8d2e31c26088049b6d1c8fae697"},       /* the vector's clear */
    {0xe89ed0,64,"b7312b7b79cb534eb7027a6cc05ac040dcef8e828de0101bc9995c975390a39a"},       /* its push_back */
    {0xf23060,372,"f13c17d0897d026e30d99c2b95cc9f6069c6219b625b77aede79b1c167fbfc47"},      /* its growth */
    {0xf29770,215,"a1aee962e3113a15651978cfbb7cebac33656a20612e1c3ee44d1d1b217a11dc"},      /* the relocation */
    {0xf29690,212,"e7064d1e8a8383c9964d84b536df2ffeb4b882e560ae4b23298b41539777de60"},      /* the relocation's tail */
    {0x93e910,224,"f24d80c8c790d067d54fee24d7e7c144efc3374918f4165c14d383b3b04b0721"},      /* the old buffer's destroy and free */
    /* Version 8 (j1-22, report 1.12): PARAM's slots as the engine keeps them: the slot copy (+0x000..+0x19b; the own copy from the
     * read slot), the update's tail that copies the read slot into the own copy when its refresh time +0x178 is newer, the slot
     * reset and the flight parameter object's constructor on a cleared 0x140-byte buffer (+0x000..+0x13f carried whole); DOCK's
     * controllers as the census reads them (lockstep_dock.h: the dock and undock transform evaluators and the reader). */
    {0xe84970,339,"e7de06a26a4d967bd189b2635da0cee6f0b97eba01bc7c84a906ab248f65b9d1"},      /* the flight parameter slot copy */
    {0xe8f020,557,"9e011538ae051f6bd3e6d97a6f9ca4d6c5134114eeba5e209c9d34dbafbaa7f9"},      /* the update's tail (the own copy's refresh) */
    {0x69ef40,170,"913dc723b45c793c63fbf9a4930adfc09243896054e532b50dba26a78b930b8b"},      /* the slot reset */
    {0x153730,396,"86960136fc20a5e424af084da994fb294e68890779b860b698219e6546dc6e84"},      /* the flight parameter object's constructor */
    {0xd6e810,3856,"8912c157e4d54e76b1a72b830f2c08bd43c7e155c32577c8e30bd9d122c1cf75"},     /* the dock transform evaluator */
    {0xd6e150,660,"e64ee51008d97447e33dc24ee6769f6e3a18af68d1f3c6ce597885f5ac54ee55"},      /* the undock transform evaluator */
    {0xd6fdf0,2275,"22d737103007e0e041ec43fd33ba5714a01b80aecc676e52778cc1cff2678cea"},     /* the dock controller's reader */
    /* Version 9 (j1-25, report 1.13): STEER's model and behaviours as their own code keeps them: the steering model's step (it
     * rewrites +0x300..+0x31f from +0x60, +0x80, +0x330, +0x68 and dt at its start, copies the qword pairs +0xb8..+0x108, steps
     * the behaviours +0x70/+0x78, fills the path vector +0x360 from the job +0x388, refreshes the avoidance cache +0x88/+0x90
     * from the obstacle publisher 0xeaf660), its SetState (the target +0x20..+0x5f, +0x120..+0x1c7, the IDs +0x60/+0x80, the
     * target's extents +0x250..+0x270), and the base constructors of the dogfight and gain-distance behaviours (their fields).
     * Not pinned: the model's constructor (lockstep_flight.h patches its entry for the fixture) and the behaviour factory
     * 0xec54b0 (RNG draws the RNG modules may patch); the classes are checked by vtable instead. */
    {0xec7200,10109,"bff420d07fb655ff195560b395af5decc0ab5b2ecf9691481e578eb3282eb796"},    /* the steering model's step */
    {0xec5170,817,"c85544125d12764df08c3c112d7a751fe184f63e122527bbf7e9fa85630ed08e"},      /* its SetState */
    {0xed1560,609,"6e98eec2082a777e49b5daeb190c0468f6cd7659d2e56414b1b38cb5727b3370"},      /* DogfightBehaviour1's constructor */
    {0xed22e0,501,"9c6d738c2ffa3756aa86198ecd53c73d3a6934ccfa683d68ce53b088db5d30e0"},      /* GainDistanceBehaviour's constructor */
    /* Version 10 (j1-27, report 1.14): SOLVER's object as the engine keeps it: the preparation that allocates it (and that the
     * joiner calls: its model getter returns a constant, it writes only into the solver), its constructor and the five subobject
     * constructors (self-pointers and vtables only), the core of its step. BULLET's census: the children walker that shows the
     * vector per view, the zone's create, the bullet's Move and its setup (shooter +0x2a0, controller slot). */
    {0xe95fb0,2048,"c72b49324fed4c46ad78a1b40c100ec7c710e9ae636e6f5cfe6ccd19d258e20d"},     /* the solver's preparation */
    {0xd47ac0,1676,"b6ca142e4a9713bdd14f9f9676bdc1621b6185186d0aec7572d0cf7e014ca396"},     /* the solver's constructor */
    {0xd48150,1304,"a09a28c2fd3af96b704f6e3a8abd3576a0a0b171585a3949578299ae932253d7"},     /* PathControl's */
    {0xd489a0,559,"84313847d310d5f49166c6cf764b41eb0c8eb68eb445c37015ea532ba85d9992"},      /* LinControlA's */
    {0xd48bd0,155,"d101e98f9a583fbcbabef0d916a5fd3ff4c4ea396f64fe6a4914b57b56abaf74"},      /* motion3's */
    {0xd48e60,517,"661d555620ee1925943c15671c56ba532646b0aff399918a6ccee6e72f01d250"},      /* RotControlA's */
    {0xd49070,571,"fc6441660bd9f3192e77882df6a06fe0f25a798bdd93b4f8996e0534b636d2a9"},      /* VelControl's */
    {0x1197970,1635,"9c1b408beba4208821aa973fc2d1e729389872624a533e0105766e0cab09fe9b"},    /* the solver's core */
    {0x10c9910,408,"c04ae79587b778da261dd7ff375943a080a2e40ce40b932b6663294f07ca982c"},     /* the children walker */
    {0x83b500,159,"e1fc6990a8a63c8cc44476ef678b7ea8a1bb5e0275695a10d7da0f2118ceb54e"},      /* the zone's bullet create */
    {0xd4f090,428,"1690c279aeac79136f9234dd4f6144e59e653bb06a3c6dc101d3b4b67b397a5f"},      /* the bullet's Move */
    {0xd50f30,2726,"aea405f11901e0a3de8ff4a80837708e1a829cd2480296ca363243ecf469cec2"},     /* the bullet's setup */
    {0xced70,184,"3209f7be0616de6d0ecc20ce3cfd82fd16d0524c09635d86995210598b0a5f36"},       /* ComponentRegistry::Find: BULLET walks its slots */
    /* Version 11 (report 1.15): the Gravidar lookup the joiner calls for each unit, and the passive unit maker it calls for the ones
     * its scans have not made yet (the bucket lock, the tree node, the unit, the queued UpdateGravidarAccessEvent). */
    {0x10b2dc0,181,"43ced4fa9f55c0b96d47b6f3252a10fe3fcf3c7d6b9d95042f242ff9d9671840"},     /* AI::Gravidar lookup */
    {0x10b2aa0,555,"3a4635c46f9ca1a6aeb62d46cd02ceed6e7a9fa05b437e7cb80b3768928d3248"},    /* its passive unit maker */
    /* Version 12 JOBE (report 1.16): the job engine's update (its last run +0x10, its cursor +0x18) and the std::string assign the joiner calls. */
    {0xf0b870,1337,"43e3076f45f0350110bb54bedb3673c60fcf9f2760332cb9bd8e4b00d70410ce"},    /* U::JobEngine::vfunc1 */
    {0x975a0,266,"f5bb7819b2592ba6d7f6e097700da97dc4e357ea11ca89567919568d85df3e7d"},      /* std::string::assign(const char *, size_t) */
    /* Version 13 EVQT: the post the joiner calls for the session's events and the removal of its own of a type. */
    {0x99a3f0,807,"5ca12a5d17041238645af6d6028c718bb3714cc53e9dd6963769d51827087c0d"},     /* U::EventSource::PostEvent */
    {0x99a870,543,"89f7a46ec199ad14ddf041c5b9ceefba36518a2e87fd3b1565c9f93efcde51b6"},     /* the removal of a source's pending events of a type */
    {0xf0b340,846,"1fee6c342524cc824017ef281edc187327421134ac685329f9547dc2ce5aa5e2"}};    /* JobEngine::Init (the joiner calls it with an empty filter) */
#define JOIN_PINS (sizeof(join_pin_table)/sizeof(*join_pin_table))
static unsigned char *join_pristine[JOIN_PINS];
/* The pinned ranges as they were when the join was armed (SAVE_AT, ~30 ticks before T): every module of the session has
 * installed its patches by then (jumps, calls and the instruction rewrites of exact_reciprocal, travel_snapshot ...).
 * J1 j1-16/j1-17 refused on such rewrites (patch:e918d7, patch:eb28d2). */
static unsigned char *join_armed_bytes[JOIN_PINS];
static unsigned join_armed_kept;
static int join_pins_state;                  /* 0 unchecked, 1 the file's ranges are the pins, -1 not */
static char join_pins_why[24]="-";
static unsigned join_patched;                /* hooks of ours the last live check found */
static char join_patched_text[80]="-";
static const WCHAR *join_exe_override;       /* tests: the executable file to read instead of this process's */
/* rva..rva+size of the executable file, through its own section table. */
static int join_pristine_native(unsigned rva,unsigned size,unsigned char *out) {
    WCHAR path[32768];const WCHAR *name=join_exe_override;
    if(!name) {DWORD n=GetModuleFileNameW(NULL,path,32768);if(!n || n>=32768) return 0;name=path;}
    HANDLE f=CreateFileW(name,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL);
    if(f==INVALID_HANDLE_VALUE) return 0;
    IMAGE_DOS_HEADER dos;IMAGE_NT_HEADERS64 nt;DWORD got=0;int ok=0;LARGE_INTEGER at;
    if(!ReadFile(f,&dos,sizeof(dos),&got,NULL) || got!=sizeof(dos) || dos.e_magic!=IMAGE_DOS_SIGNATURE || dos.e_lfanew<0 || dos.e_lfanew>4096) goto done;
    at.QuadPart=dos.e_lfanew;
    if(!SetFilePointerEx(f,at,NULL,FILE_BEGIN) || !ReadFile(f,&nt,sizeof(nt),&got,NULL) || got!=sizeof(nt) || nt.Signature!=IMAGE_NT_SIGNATURE ||
       nt.OptionalHeader.SizeOfImage!=JOIN_IMAGE_SIZE) goto done;
    at.QuadPart=dos.e_lfanew+(LONGLONG)offsetof(IMAGE_NT_HEADERS64,OptionalHeader)+nt.FileHeader.SizeOfOptionalHeader;
    if(!SetFilePointerEx(f,at,NULL,FILE_BEGIN)) goto done;
    for(unsigned i=0;i<nt.FileHeader.NumberOfSections && i<96;i++) {
        IMAGE_SECTION_HEADER s;
        if(!ReadFile(f,&s,sizeof(s),&got,NULL) || got!=sizeof(s)) goto done;
        if(rva<s.VirtualAddress || (uint64_t)rva+size>(uint64_t)s.VirtualAddress+s.SizeOfRawData) continue;
        at.QuadPart=(LONGLONG)s.PointerToRawData+(rva-s.VirtualAddress);
        ok=SetFilePointerEx(f,at,NULL,FILE_BEGIN) && ReadFile(f,out,size,&got,NULL) && got==size;
        break;
    }
done:
    CloseHandle(f);return ok;
}
static int (*join_pristine_read)(unsigned,unsigned,unsigned char *)=join_pristine_native;
/* This gate's memory: its DLL, or a private committed page outside X4's image (the thunk and near pages); code executable. */
static int join_ours_native(const unsigned char *p,int code) {
    HMODULE mine=NULL,there=NULL;MEMORY_BASIC_INFORMATION m;
    if(!p || (p>=image && p<image+JOIN_IMAGE_SIZE)) return 0;
    const DWORD flags=GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT;
    if(GetModuleHandleExW(flags,(LPCWSTR)(const void *)p,&there))
        return GetModuleHandleExW(flags,(LPCWSTR)(void *)join_ours_native,&mine) && there==mine;
    if(!VirtualQuery(p,&m,sizeof(m)) || m.State!=MEM_COMMIT || m.Type!=MEM_PRIVATE) return 0;
    return !code || (m.Protect&(PAGE_EXECUTE|PAGE_EXECUTE_READ|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY));
}
static int (*join_ours)(const unsigned char *,int)=join_ours_native;
__attribute__((unused)) static void join_pins_reset(void) {   /* tests */
    for(unsigned i=0;i<JOIN_PINS;i++) {free(join_pristine[i]);join_pristine[i]=NULL;free(join_armed_bytes[i]);join_armed_bytes[i]=NULL;}
    join_armed_kept=0;join_pins_state=0;snprintf(join_pins_why,sizeof(join_pins_why),"-");join_patched=0;snprintf(join_patched_text,sizeof(join_patched_text),"-");
}
static int join_pins_pristine(void) {
    if(join_pins_state) return join_pins_state>0;
    for(unsigned i=0;i<JOIN_PINS;i++) {
        const join_pin *pin=&join_pin_table[i];char hex[65];
        if(!join_pristine[i] && !(join_pristine[i]=malloc(pin->size))) {snprintf(join_pins_why,sizeof(join_pins_why),"memory");join_pins_state=-1;return 0;}
        if(!join_pristine_read(pin->rva,pin->size,join_pristine[i])) {snprintf(join_pins_why,sizeof(join_pins_why),"file");join_pins_state=-1;return 0;}
        if(!digest_hex(join_pristine[i],pin->size,hex) || strcmp(hex,pin->sha)) {
            snprintf(join_pins_why,sizeof(join_pins_why),"%x",pin->rva);join_pins_state=-1;return 0;
        }
    }
    join_pins_state=1;return 1;
}
/* A hook of ours over image offset at: its first byte and its end. */
static int join_hook_at(unsigned at,unsigned *start,unsigned *end) {
    for(unsigned back=0;back<6 && back<=at;back++) {
        unsigned s=at-back;const unsigned char *p=image+s;int32_t rel;
        if((p[0]==0xe8 || p[0]==0xe9) && back<5) {
            memcpy(&rel,p+1,4);
            if(join_ours((const unsigned char *)((uintptr_t)p+5+(intptr_t)rel),1)) {*start=s;*end=s+5;return 1;}
        }
        if(p[0]==0xff && (p[1]==0x15 || p[1]==0x25)) {
            const unsigned char *slot,*target=NULL;memcpy(&rel,p+2,4);slot=(const unsigned char *)((uintptr_t)p+6+(intptr_t)rel);
            if(join_ours(slot,0) && read_local(slot,&target,8) && join_ours(target,1)) {*start=s;*end=s+6;return 1;}
        }
    }
    return 0;
}
/* Our data patches (not jumps or calls): a byte inside one is ours when its module reports it installed and the live window
 * holds exactly what the module wrote. lockstep_travel_snapshot.h rewrites a disp32 byte inside the Agent update (the read
 * word at 0xe918d0) and two lea windows; J1 j1-16 refused with patch:e918d7 once v5 pinned the Agent update. */
static int join_data_patch_at(unsigned at,unsigned *start,unsigned *end) {
    if(tsnap_read_installed && at>=TSNAP_READ_WORD && at<TSNAP_READ_WORD+8u && !memcmp(image+TSNAP_READ_WORD,&tsnap_read_patched,8))
        {*start=TSNAP_READ_WORD;*end=TSNAP_READ_WORD+8u;return 1;}
    static const unsigned lea[2]={TSNAP_LEA_A,TSNAP_LEA_B};
    for(unsigned i=0;i<2;i++)
        if(tsnap_lea_installed[i] && at>=lea[i] && at<lea[i]+16u && !memcmp(image+lea[i],&tsnap_lea_patched[i],16))
            {*start=lea[i];*end=lea[i]+16u;return 1;}
    return 0;
}
/* The live pinned ranges at the arming (join_arm): what our modules had patched by then. 0 when memory fails. */
static int join_pins_baseline(void) {
    for(unsigned i=0;i<JOIN_PINS;i++) {
        const join_pin *pin=&join_pin_table[i];
        if(!join_armed_bytes[i] && !(join_armed_bytes[i]=malloc(pin->size))) return 0;
        memcpy(join_armed_bytes[i],image+pin->rva,pin->size);
    }
    return 1;
}
/* Both checks; why = pins:<rva>, pins:file or patch:<rva> on a refusal. */
static int join_pins_check(char *why,size_t capacity) {
    if(!join_pins_pristine()) {snprintf(why,capacity,"pins:%s",join_pins_why);return 0;}
    unsigned found=0;size_t used=0;char text[sizeof(join_patched_text)];text[0]=0;join_armed_kept=0;
    for(unsigned i=0;i<JOIN_PINS;i++) {
        const join_pin *pin=&join_pin_table[i];
        for(unsigned off=0;off<pin->size;) {
            unsigned at=pin->rva+off,start=0,end=0;
            if(image[at]==join_pristine[i][off]) {++off;continue;}
            /* Unchanged since the arming (a patch of ours installed before it, file pins checked): kept, counted. */
            if(join_armed_bytes[i] && image[at]==join_armed_bytes[i][off]) {++join_armed_kept;++off;continue;}
            if(!(join_hook_at(at,&start,&end) || join_data_patch_at(at,&start,&end)) || end<=at) {snprintf(why,capacity,"patch:%x",at);return 0;}
            int n=snprintf(text+used,sizeof(text)-used,"%s%x",found?",":"",start);
            if(n>0 && used+(size_t)n<sizeof(text)) used+=(size_t)n;
            ++found;off=end-pin->rva;
        }
    }
    join_patched=found;snprintf(join_patched_text,sizeof(join_patched_text),"%s",found?text:"-");
    return 1;
}
static join_engine join_engine_native(void) {
    join_engine e;memset(&e,0,sizeof(e));
    e.director=*(unsigned char **)(image+JOIN_AI_DIRECTOR);e.scheduler=*(unsigned char **)(image+JOIN_MD_SCHEDULER);
    e.md_seq=(uint32_t *)(image+JOIN_MD_SEQ);e.ctx_vtable=(uintptr_t)(image+JOIN_CTX_VTABLE);e.cue_vtable=(uintptr_t)(image+JOIN_CUE_VTABLE);
    e.ctx_head=(uintptr_t)(image+JOIN_CTX_LIST);e.ctx_tail=(uintptr_t)(image+JOIN_CTX_TAIL);
    e.insert=(join_tree_insert_fn)(image+JOIN_TREE_INSERT);e.erase=(join_tree_erase_fn)(image+JOIN_TREE_ERASE);
    return e;
}
static join_engine (*join_engine_get)(void)=join_engine_native;
/* JOIN_AIWATCH (diagnostic, with native_gate_join_watch; report 1.18): every AI context whose owner is watched, at the same points
 * as JOIN_WATCH (s, j, t, u): the script, the wake reasons +0x188 and +0x18c, +0x190, the import flags +0x19a and +0x19b, the timed
 * wake node +0x1a8 (the director's tree header: none), the positions in both wake vectors +0x1b0/+0x1b8 (-1: none), the call frame
 * +0x130; and the director's wake vector index +0x230 and both vectors' lengths. j1-82: pilot 0x6ea57's escort handler runs at T+1
 * on both sides, its contexts then run only on the session. */
static void join_job_watch_dump(char side);   /* below the job DB's layout */
static void join_ai_watch_dump(char side,uint64_t tick) {
    if(!join_watch.n) return;
    join_engine e=join_engine_get();uintptr_t *list=NULL;uint32_t n=0;char out[8192];size_t at=0;
    if(!e.director || !join_ctx_list(&e,&list,&n)) return;
    uint32_t idx=0;uintptr_t v[4]={0,0,0,0};
    (void)join_read(e.director+0x230,&idx,4);(void)join_read(e.director+0x200,&v[0],16);(void)join_read(e.director+0x218,&v[2],16);
    at+=(size_t)snprintf(out+at,sizeof(out)-at,"[X4LAN] NATIVE_GATE JOIN_AIWATCH side=%c tick=%llu director idx=%u vec0=%llu vec1=%llu\r\n",side,
                         (unsigned long long)tick,idx,(unsigned long long)((v[1]-v[0])/8u),(unsigned long long)((v[3]-v[2])/8u));
    for(uint32_t i=0;i<n && at+400<sizeof(out);i++) {
        join_ctx_rec k;unsigned flags;
        if(!join_ctx_key(&e,list[i],&k,&flags)) continue;
        unsigned w=0;for(;w<join_watch.n;w++) if(join_watch.portable[w]==k.owner) break;
        if(w==join_watch.n) continue;
        uint32_t r188=0,r18c=0;double t190=0;unsigned char f19a=0,f19b=0;uintptr_t node=0,frame=0;int64_t pos[2]={0,0};
        (void)join_read((void *)(list[i]+0x188),&r188,4);(void)join_read((void *)(list[i]+0x18c),&r18c,4);(void)join_read((void *)(list[i]+0x190),&t190,8);
        (void)join_read((void *)(list[i]+0x19a),&f19a,1);(void)join_read((void *)(list[i]+0x19b),&f19b,1);(void)join_read((void *)(list[i]+0x1a8),&node,8);
        (void)join_read((void *)(list[i]+0x1b0),pos,16);(void)join_read((void *)(list[i]+0x130),&frame,8);
        at+=(size_t)snprintf(out+at,sizeof(out)-at,"[X4LAN] NATIVE_GATE JOIN_AIWATCH side=%c tick=%llu owner=%llx script=%016llx r188=%x r18c=%x t190=%.6f "
                             "f19a=%u f19b=%u timed=%d pos=%lld:%lld frame=%d\r\n",side,(unsigned long long)tick,(unsigned long long)k.owner,
                             (unsigned long long)k.script,r188,r18c,t190,f19a,f19b,node && node!=(uintptr_t)(e.director+JOIN_DIR_TREE),(long long)pos[0],
                             (long long)pos[1],frame!=0);
    }
    free(list);crash_note_write(out,(int)at);
}
static unsigned char *join_tls_block(void) {
    void **slots=*(void ***)((unsigned char *)NtCurrentTeb()+0x58);return slots?(unsigned char *)slots[0]:NULL;
}
#include "lockstep_join_precise.h"   /* the join save at full precision (native_gate_join_precise) */
#include "lockstep_join_ids.h"       /* the joiner's saved objects at the session's runtime IDs (native_gate_join_ids) */
/* The engine save with the MoveWorker's clock, view and RNG word on this thread; 1 saved. */
static int join_save_with_worker_tls(const char *file,const char *name) {
    unsigned char *mine=join_tls_block(),*worker=(unsigned char *)join_cap.mw_tls,saved_time[8],saved_view[4],saved_rng[8];
    int swap=mine && worker && mine!=worker;
    if(swap) {
        memcpy(saved_time,mine+0x300,8);memcpy(saved_view,mine+0x314,4);memcpy(saved_rng,mine+0x318,8);
        memcpy(mine+0x300,worker+0x300,8);memcpy(mine+0x314,worker+0x314,4);memcpy(mine+0x318,worker+0x318,8);
    }
    unsigned char ok=0;
    const int precise=join_precise_on && join_save_call==join_save_native && join_fmt_install();
    if(precise) join_fmt_thread=GetCurrentThreadId();
    __try {ok=join_save_call(file,name);}
    __finally {
        if(swap) {memcpy(mine+0x300,saved_time,8);memcpy(mine+0x314,saved_view,4);memcpy(mine+0x318,saved_rng,8);}
        if(precise) {join_fmt_thread=0;join_fmt_restore();}
    }
    return ok!=0;
}

/* ---- module audit ---- */
typedef struct {const char *name;int armed;const char *status;} join_module;
static unsigned join_audit(join_tables *t) {
    /* status: serialized (in the package), superseded (a load-time draw the package overwrites), stateless (per-tick
     * keys or a flag; no state carries from one tick to the next), unsupported (state the package lacks),
     * unaudited (not classified yet; a strict join refuses it). */
    const join_module modules[]={
        {"world_identities",world_identity_installed,"serialized"},{"created_identities",(int)created_armed,"serialized"},
        {"portable_created",(int)world_portable_created,"stateless"},
        {"runtime_ids",rid_mode!=0,rid_mode==RID_SET?"serialized":"stateless"},
        {"ai_wait_rng",ai_wait_armed,"superseded"},{"md_resume_rng",md_resume_armed,"superseded"},
        {"ai_schedule",ai_sched_armed,"unsupported"},       /* the Gravidar round-robin cursor is not captured */
        {"boot_hold",boot_hold_armed,"unaudited"},{"md_timed_rng",md_timed_armed,"unaudited"},
        {"script_rng",script_rng_armed,"unaudited"},{"job_rng",job_rng_installed,"unaudited"},{"find_order",find_order_armed,"unaudited"},
        {"region_rng",region_rng_armed,"serialized"},   /* per-tick keys; the damage fields' pass times are REGION (v4) */{"spawn_rng",spawn_rng_armed,"unaudited"},{"gravidar_deadlines",gdl_armed,"unaudited"},
        {"speech_time",speech_time_armed,"unaudited"},{"event_order",event_order_armed,"unaudited"},{"event_rng",event_rng_armed,"unaudited"},
        /* Version 3 (report 1.7): attention levels and dues (ATTN), the attention_all and fixture first-call tables (KEYS 1, 4);
         * the navigation parameter and probe deadlines and banks (NAV), the fixture's first refresh and the cadence's
         * parameter and probe first-call tables (KEYS 5, 2, 3). */
        {"attention",attention_installed,"serialized"},
        {"nav",nav_installed,"serialized"},
        {"nav_cadence",nav_cadence_installed,"serialized"},{"nav_player",nav_player_installed,"unaudited"},
        {"nav_octree",noct_installed,"unaudited"},{"nav_mesh",nmesh_installed,"unaudited"},{"mass_traffic",mt_installed,"unaudited"},
        {"character_play",cplay_installed,"stateless"},  /* per-tick keys; the loads make residency alike */
        {"region_camera",rc_installed,"unaudited"},{"view_points",view_installed,"unaudited"},{"hybrid",hyb_armed,"unaudited"},
        {"travel",travel_installed,"unaudited"},{"travel_snapshot",tsnap_installed,"unaudited"},{"path_determinism",path_det_mask!=0,"serialized"},
        {"pilot",pilot_armed,"unaudited"},{"command_order",co_armed,"unaudited"},{"bullet_cadence",bullet_cadence_installed,"unaudited"},
        {"speak_timer",speak_timer_installed,"unaudited"},{"render_rng",render_rng_installed,"unaudited"},
        {"weapon_fire",wfire_installed,"unaudited"},{"engine_rng",erng_installed,"unaudited"},{"build_rng",brng_installed,"unaudited"},
        {"ship_sound",ss_installed,"unaudited"},{"draw_watch",dw_armed||dw_frame_armed,"unaudited"},{"player_timer",pt_installed,"unaudited"},
        {"warp_rng",wr_installed,"unaudited"},{"transition_rows",trr_installed,"unaudited"},{"tempzone_order",tz_installed,"unaudited"},
        {"gravidar_wide",gwide_active,"unaudited"},{"camera_offset",camoff_applied,"unaudited"},{"camera_anchor",camfol_applied,"unaudited"},
        {"ui_cargo",uic_installed,"unaudited"},{"supply_rows",spr_installed,"unaudited"},{"ui_lazy",ul_installed,"unaudited"},
        {"portable_math",pmath_installed,"unaudited"},{"exact_reciprocal",xrcp_installed,"unaudited"},{"fp_env",fpenv_installed,"unaudited"},
        {"decimal",pdec_installed,"unaudited"},{"path_budget",pbud_installed,"serialized"},{"path_transforms",ptf_installed,"unaudited"},
        {"body_pose",bpose_installed,"unaudited"},{"supply_wants",sw_installed,"unaudited"},{"loadout_order",ll_installed,"unaudited"},
        {"order_canon",oc_installed,"unaudited"},{"pick_order",po_installed,"unaudited"},{"macro_arena",ma_installed,"unaudited"},
        {"economy_load",eco_installed,"unaudited"},{"fp_redirect",fr_installed,"unaudited"},{"physics_trace",ptrace_installed,"unaudited"},
        {"body_order",border_installed,"unaudited"},{"shape_order",sorder_installed,"unaudited"},{"radar",radar_installed,"unaudited"},
        {"gravidar",gravidar_installed,"unaudited"},{"interrupt",interrupt_installed||interrupt_event_installed,"unaudited"},
        {"damage",damage_installed,"unaudited"},{"contact",contact_installed,"unaudited"},{"ai",ai_seeded,"unaudited"},
        {"agent",agent_seeded,"unaudited"},{"ship",ship_seeded,"unaudited"},{"weapon",weapon_seeded,"unaudited"},
        {"params",params_installed,"unaudited"},{"velocity",velocity_installed,"unaudited"},{"shape",shape_installed,"unaudited"},
        {"bullet",bullet_installed,"unaudited"},{"death",death_installed,"unaudited"},{"mass",mass_installed,"unaudited"},
        {"flight",flight_installed,"unaudited"},{"spline",spline_installed,"unaudited"},
    };
    free(t->modules);t->modules=NULL;t->modules_len=0;t->nmodules=0;
    join_buf b={0};unsigned bad=0;char line[96];
    for(size_t i=0;i<sizeof(modules)/sizeof(*modules);i++) {
        if(!modules[i].armed) continue;
        int n=snprintf(line,sizeof(line),"%s=%s\n",modules[i].name,modules[i].status);join_put(&b,line,(size_t)n);++t->nmodules;
        if(!strcmp(modules[i].status,"unsupported") || !strcmp(modules[i].status,"unaudited")) ++bad;
    }
    t->modules=(char *)b.data;t->modules_len=b.size;t->gate.unsupported=bad;return bad;
}

/* ---- session side: arm, then the boundary ---- */
/* gate.join_arm(T, k, mask, save folder): on the UI thread (the MoveWorker), at least two ticks ahead. */
static int join_arm(uint64_t tick,unsigned slot_k,unsigned mask,const char *folder,int mode) {
    if(!stats.active || !accept_ui() || join_cap.state==JOIN_ARMED || (boot_mode && boot_phase<3) || tick<=stats.issued+1 || tick>0xffffffffu ||
       !slot_k || slot_k>wire_clients || slot_k==stats.slot || !(mask>>(stats.slot-1)&1u) || wire_version!=3 || peer_on() || wcg.armed) return 0;
#ifdef LOCKSTEP_ADAPTIVE
    if(wire_adaptive) return 0;
#endif
    WCHAR wide[MAX_PATH];
    if(!folder || !join_wide(folder,wide,MAX_PATH-32)) return 0;
    free(join_cap.lua);memset(&join_cap,0,sizeof(join_cap));
    join_cap.tick=tick;join_cap.slot_k=slot_k;join_cap.mask=mask;join_cap.mode=mode;
    snprintf(join_cap.name,sizeof(join_cap.name),"j%llu",(unsigned long long)tick);
    wcscpy(join_cap.folder,wide);size_t n=wcslen(join_cap.folder);
    if(n && join_cap.folder[n-1]!=L'\\' && join_cap.folder[n-1]!=L'/') wcscat(join_cap.folder,L"\\");
    join_cap.mw_tls=(uintptr_t)join_tls_block();join_cap.state=JOIN_ARMED;
    (void)join_pins_pristine();   /* the file read here, on the UI thread; the boundary answers from it (pins:... there) */
    if(!join_pins_baseline()) {join_fail_cap(JOIN_WHY_CAPTURE,"memory");return 1;}   /* refused at once, loudly (JOIN_RESULT) */
    return 1;
}
/* After step T (main thread, the MoveWorker joined): save, capture, write the package. Never stops the session. */
/* Which part of the table capture refused, with how far it got (j1-5 live: 'tables' alone named nothing), then the
 * failing record (join_fault): 'tables:<part>:<site>:<ctx>/<wake>/<vec>/<md>,<record and check>'. */
static char join_tables_why[320]="tables";
static int join_capture_tables(const join_engine *e,join_tables *t) {
    join_keys_reset();join_cap_site=0;snprintf(join_fault,sizeof(join_fault),"-");
    const char *part=!e->director?"director":!e->scheduler?"scheduler":!join_capture_ctx(e,t)?"ctx":!join_capture_wake(e,t)?"wake":
        !join_capture_vec(e,t)?"vec":!join_capture_md(e,t)?"md":NULL;
    if(part) snprintf(join_tables_why,sizeof(join_tables_why),"tables:%s:%u:%u/%u/%u/%u,%s",part,join_cap_site,t->nctx,t->nwake,t->nvec,t->nmd,
                      join_fault);
    join_keys_reset();
    return !part;
}
/* The session's view of each snapshot fixture actor at T (lockstep_snapshot_fixture.h): what its gate binds, so that a
 * joiner binds the same (J1 j1-9: actor 8 a wreck with its drift controller). All 0 without a bound fixture. */
static void join_capture_fixture(join_tables *t,snapshot_find_fn find,void *map) {
    snapshot_session_views(t->gate.fixture,find,map);memcpy(join_cap.fixture,t->gate.fixture,12);
    /* The gate's flight sample generations (j1-120 at T+1: steering=1 on the session's eleven live fixture actors, 0 on the joiner,
     * which registers the loaded model already seeded); a later re-seed draws from actor_seed(.., generation + 1). */
    for(unsigned i=0;i<12;i++) {const uint64_t g=flight_samples[i].init_generation;t->gate.flight[i]=(unsigned char)(g>255u?255u:g);}
}
/* The admission: the session's flight sample generations (the joiner's registration at T + 1 keeps them). */
static void join_admit_flight_generations(void) {
    if(join_rx.t.version<13) return;
    for(unsigned i=0;i<12;i++) flight_samples[i].init_generation=join_rx.t.gate.flight[i];
}
static uint64_t join_cid_get(void);   /* X4's construction ID counter (below, at join_admit_rid) */
static uint64_t join_unpaused_get(void);   /* the unpaused clock (below, at join_admit_cid) */
static void join_gate_fields(join_tables *t,uint64_t tick) {
    t->gate.tick=tick;t->gate.game=join_bits(stats.game);t->gate.real=join_bits(stats.real);t->gate.wire_version=wire_version;
    t->gate.clients=wire_clients;t->gate.lookahead=wire_lookahead;t->gate.tick_hz=gate_tick_hz;t->gate.slot=stats.slot;
    t->gate.rid_next=rid_mode?rid_next_id:0;memcpy(t->gate.digest,digest,64);
    t->gate.cid=join_cid_get();
    t->gate.unpaused=join_unpaused_get();
}
#define JOIN_GRAVP_RESTORE_TICKS 10u   /* the boundaries after T+1 .. T+10 */
/* native_gate_join_shape_refresh (session and joiner, default off; report 1.18, j1-243 .. j1-249): a body's Jolt compound holds
 * its parts' poses as of its last shape refresh (XPhys::Body_UpdateShape 0xbff80 -> 0xc2c00), and the refresh comes at a due
 * time per shape holder (record +0x20, due +0x28; lockstep_shape.h). The session's due times run on from long before T; the
 * joiner's start at its load (its first frame refreshes every fixture holder, shape_initial). So at 1011 a bullet met fixture
 * ship 0x176ad5's moving part as the session last snapshotted it at 890/896 and the joiner at 932/935: the same TemplatePart
 * hit 0.83 units apart (j1-245, j1-247), one ulp of angular velocity, the J1 first mismatch. With this option every client
 * forgets the fixture holders it has seen at the boundary after T+1 (shape_initialized), so at step T+2 every fixture holder
 * on every client refreshes from the same poses with the keyed draw (shape_initial), and their due times run together after.
 * The session learns T from the SAVE_AT record (every client of the session, saver or not), the joiner from its package.
 * Other bodies keep the native refresh path (region camera alignment at each client's start): not covered here. */
static uint64_t join_shape_at;static int join_shape_joiner;static uint32_t join_shape_resets;
static void join_shape_reset(uint64_t completed) {
    unsigned held=0;
    for(unsigned a=0;a<ACTOR_CAPACITY;a++) for(unsigned w=0;w<SHAPE_RECORDS;w++) if(shape_initialized[a][w].holder) ++held;
    memset(shape_initialized,0,sizeof(shape_initialized));++join_shape_resets;
    char row[160];const int n=snprintf(row,sizeof(row),"[X4LAN] NATIVE_GATE JOIN_SHAPE_RESET tick=%llu forgotten=%u resets=%u joiner=%d\r\n",
                                       (unsigned long long)completed,held,join_shape_resets,join_shape_joiner);
    if(n>0) crash_note_write(row,n<(int)sizeof(row)?n:(int)sizeof(row)-1);
}
static void join_boundary(uint64_t completed) {
    if(join_shape_joiner && join_rx.based && completed==join_rx.t.gate.tick+1) join_shape_reset(completed);
    else if(join_shape_at && completed==join_shape_at+1) {join_shape_reset(completed);join_shape_at=0;}
    join_t1_probe(completed,stats.game);   /* both sides: the linear models after step T+1 (lockstep_join_world.h) */
    if(join_watch.armed && (completed==join_watch.tick+1 || completed==join_watch.tick+2)) join_ai_watch_dump(completed==join_watch.tick+1?'t':'u',completed);
    join_watch_probe(completed);           /* both sides: the watched objects after T+1, T+2 (diagnostic) */
    /* version 13: the low-attention published poses back after the load's publication; j1-89: some land after T+2 (the attacks
     * of 638 and 644 between low-attention ships: the batched updates of low attention publish within the first frames) */
    if(join_rx.based && join_rx.t.version>=13 && completed>join_rx.t.gate.tick && completed<=join_rx.t.gate.tick+JOIN_GRAVP_RESTORE_TICKS)
        join_gravp_restore();
    if(join_rx.based && join_rx.t.version>=13 && (completed==join_rx.t.gate.tick+1 || completed==join_rx.t.gate.tick+2))
        join_shoot_late_probe(completed==join_rx.t.gate.tick+1?0u:1u);   /* version 13: SHOOT's controllers missing at the admission */
    if(join_cap.state!=JOIN_ARMED || completed!=join_cap.tick) {
        if(join_cap.state==JOIN_ARMED && completed>join_cap.tick) join_fail_cap(JOIN_WHY_NOT_ARMED,"tick_passed");
        return;
    }
    LARGE_INTEGER t0,t1,t2,f;QueryPerformanceFrequency(&f);QueryPerformanceCounter(&t0);
    join_tables t;memset(&t,0,sizeof(t));join_gate_fields(&t,completed);
    unsigned unsupported=join_audit(&t);
    if(unsupported && join_cap.mode==JOIN_STRICT) {join_tables_free(&t);join_fail_cap(JOIN_WHY_UNSUPPORTED,"strict");return;}
    if(unsupported) t.flags|=1u;
    {char why[48];if(!join_pins_check(why,sizeof(why))) {join_tables_free(&t);join_fail_cap(JOIN_WHY_CAPTURE,why);return;}}
    if(*(volatile int *)(image+JOIN_SAVE_COUNTDOWN)) {join_tables_free(&t);join_fail_cap(JOIN_WHY_BUSY,"engine_save_pending");return;}
    join_cap.possible=(unsigned)((join_possible_fn)(image+JOIN_SAVE_POSSIBLE))(image+JOIN_SAVE_GAME,0);
    if(join_cap.possible!=1) {join_tables_free(&t);join_fail_cap(JOIN_WHY_NOT_POSSIBLE,"is_saving_possible");return;}
    WCHAR dir[MAX_PATH],path[MAX_PATH];
    if(join_cap.lua && join_cap.lua_len) {
        t.lua=malloc(join_cap.lua_len);if(!t.lua) {join_tables_free(&t);join_fail_cap(JOIN_WHY_CAPTURE,"memory");return;}
        memcpy(t.lua,join_cap.lua,join_cap.lua_len);t.lua_len=join_cap.lua_len;t.nlua=join_cap.nlua;
    }
    if(_snwprintf(dir,MAX_PATH,L"%lsmpjoin",join_cap.folder)<0 || (!CreateDirectoryW(dir,NULL) && GetLastError()!=ERROR_ALREADY_EXISTS))
        {join_tables_free(&t);join_fail_cap(JOIN_WHY_PACKAGE_WRITE,"mpjoin_folder");return;}
    if(*(volatile unsigned char *)(image+JOIN_SAVE_MODIFIED)) {*(volatile unsigned char *)(image+JOIN_SAVE_MODIFIED)=0;join_cap.modified_cleared=1;}
    char file[48],title[48];snprintf(file,sizeof(file),"mpjoin\\%s",join_cap.name);snprintf(title,sizeof(title),"MP join %llu",(unsigned long long)completed);
    int saved=join_save_with_worker_tls(file,title);
    QueryPerformanceCounter(&t1);join_cap.save_us=(uint64_t)((t1.QuadPart-t0.QuadPart)*1000000/f.QuadPart);
    if(!saved) {join_tables_free(&t);join_fail_cap(JOIN_WHY_SAVE_FAILED,"engine_save");return;}
    int found=0;
    for(int gz=1;gz>=0 && !found;gz--) {
        if(_snwprintf(path,MAX_PATH,L"%ls\\%hs.%ls",dir,join_cap.name,gz?L"xml.gz":L"xml")<0) break;
        found=join_file_sha(path,t.save_sha,&t.save_bytes);
    }
    if(!found) {join_tables_free(&t);join_fail_cap(JOIN_WHY_FILE_MISSING,"save_file");return;}
    memcpy(join_cap.save_sha,t.save_sha,32);join_cap.save_bytes=t.save_bytes;
    {
        join_engine e=join_engine_get();
        if(!join_capture_tables(&e,&t)) {join_tables_free(&t);join_fail_cap(JOIN_WHY_CAPTURE,join_tables_why);return;}
    }
    if(!join_capture_ids(&t)) {join_tables_free(&t);join_fail_cap(JOIN_WHY_CAPTURE,"identities");return;}
    if(!join_capture_gone(&t,(world_hash_find_fn)(image+0xced70),*(void **)(image+0x6cf1500))) {join_tables_free(&t);join_fail_cap(JOIN_WHY_CAPTURE,"gone");return;}
    join_capture_fixture(&t,(snapshot_find_fn)(image+0xced70),*(void **)(image+0x6cf1500));
    /* The world side table: what the save rounds or omits in the census health and motion domains (lockstep_join_world.h). */
    snprintf(join_fault,sizeof(join_fault),"-");t.version=JOIN_PACKAGE_VERSION;
    if(!join_capture_world(&t,stats.game)) {
        char why[320];snprintf(why,sizeof(why),"world:%s",join_fault);join_tables_free(&t);join_fail_cap(JOIN_WHY_CAPTURE,why);return;
    }
    join_cap.health=t.nhealth;join_cap.pose=t.npose;join_cap.shield=t.nshield;join_cap.nav=t.nnav;join_cap.attn=t.nattn;join_cap.keys=t.nkeys;
    join_cap.path=t.npath;join_cap.region=t.nregion;join_cap.flight=t.nflight;join_cap.spline=t.nspline;join_cap.agent=t.nagent;
    join_cap.route=t.nroute;join_cap.param=t.nparam;join_cap.dock=t.ndock;join_cap.steer=t.nsteer;join_cap.steerpt=t.nsteerpt;
    join_cap.solver=t.nsolver;join_cap.bullet=t.nbullet;join_cap.grav=t.ngrav;join_cap.gravc=t.ngravc;join_cap.gravs=t.ngravs;
    join_cap.splm=t.nsplm;join_cap.splme=t.nsplme;join_cap.splmseg=t.nsplmseg;join_cap.gravn=t.ngravn;join_cap.gravp=t.ngravp;join_cap.gravq=t.ngravq;join_cap.jobe=t.njobe;join_cap.evqt=t.nevqt;join_cap.gravd=t.ngravd;join_cap.viewk=t.nviewk;join_cap.viewkc=t.nviewkc;join_cap.viewf=t.nviewf;join_cap.viewp=t.nviewp;join_cap.shoot=t.nshoot;join_cap.attk=t.nattk;join_cap.livef=t.nlivef;join_cap.spec=t.nspec;join_cap.navc=t.nnavc;join_cap.jobs=t.njobs;join_cap.gone=t.ngone;join_cap.body=t.nbody;join_cap.bupd=t.nbupd;join_cap.ldc=t.nldc;join_cap.navt=t.nnavt;join_cap.nmesh=t.nnmesh;join_cap.noct=t.nnoct;join_cap.agtt=t.nagtt;join_cap.agcb=t.nagcb;join_cap.pbnk=t.npbnk;join_cap.pend=t.npend;join_cap.gravb=t.ngravb;join_cap.viewl=t.nviewl;join_cap.ikv=t.nikv;join_cap.rcnt=t.nrcnt;join_cap.ecos=t.necos;join_cap.shpw=t.nshpw;join_cap.trdn=t.ntrdn;join_cap.npca=t.nnpca;join_cap.jobg=t.njobg;join_cap.code=t.ncode;join_cap.engr=t.nengr;join_cap.shgr=t.nshgr;join_cap.bnds=t.nbnds;join_cap.omgs=t.nomgs;join_cap.resv=t.nresv;join_cap.pcsv=t.npcsv;
    QueryPerformanceCounter(&t2);t.gate.save_us=join_cap.save_us;t.gate.capture_us=(uint64_t)((t2.QuadPart-t1.QuadPart)*1000000/f.QuadPart);
    join_buf b={0};
    if(!join_package_build(&t,&b)) {free(b.data);join_tables_free(&t);join_fail_cap(JOIN_WHY_PACKAGE_WRITE,"build");return;}
    if(_snwprintf(path,MAX_PATH,L"%ls\\%hs.x4join",dir,join_cap.name)<0 || !join_file_write(path,b.data,b.size))
        {free(b.data);join_tables_free(&t);join_fail_cap(JOIN_WHY_PACKAGE_WRITE,"write");return;}
    lockstep_sha256(b.data,b.size,join_cap.package_sha);memcpy(join_cap.portable,t.gate.portable,16);
    join_cap.package_bytes=b.size;join_cap.capture_us=t.gate.capture_us;free(b.data);join_tables_free(&t);
    join_cap.state=JOIN_DONE;join_t1_arm(completed);join_watch_arm(completed);join_ai_watch_dump('s',completed);join_job_watch_dump('s');   /* the capture listed the linear models' targets at T */
}

/* ---- joiner side ---- */
static uint32_t join_idmap_slot(uint64_t runtime) {return (uint32_t)(lockstep_mix(runtime)&join_rx.idmap_mask);}
static int join_idmap_build(void) {
    uint32_t size=1u<<JOIN_IDMAP_BITS;while(size<join_rx.t.nids*2u && size<(1u<<26)) size<<=1;
    join_rx.idmap=VirtualAlloc(NULL,(size_t)size*sizeof(join_id_rec),MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    if(!join_rx.idmap) return 0;
    join_rx.idmap_mask=size-1u;
    for(uint32_t i=0;i<join_rx.t.nids;i++) {
        const join_id_rec *r=&join_rx.t.ids[i];if(!r->runtime) return 0;
        for(uint32_t s=join_idmap_slot(r->runtime),n=0;;s=(s+1u)&join_rx.idmap_mask,n++) {
            if(n>join_rx.idmap_mask) return 0;
            if(!join_rx.idmap[s].runtime) {join_rx.idmap[s]=*r;break;}
            if(join_rx.idmap[s].runtime==r->runtime) return 0;
        }
    }
    return 1;
}
static const join_id_rec *join_idmap_find(uint64_t runtime) {
    if(!join_rx.idmap || !runtime) return NULL;
    for(uint32_t s=join_idmap_slot(runtime),n=0;n<=join_rx.idmap_mask;s=(s+1u)&join_rx.idmap_mask,n++) {
        if(!join_rx.idmap[s].runtime) return NULL;
        if(join_rx.idmap[s].runtime==runtime) return &join_rx.idmap[s];
    }
    return NULL;
}
static int join_import_active(void) {return join_rx.loaded && !join_rx.admitted;}
/* The runtime IDs of imported objects without a portable ID in the package (an insert-only set; the admission's created_extra
 * says which of the joiner's own created identities its load made from the save, loaded=1, and which it made itself). */
static uint32_t join_unmapped_slot(uint64_t runtime) {return (uint32_t)(lockstep_mix(runtime)&(JOIN_UNMAPPED_SET-1u));}
static void join_unmapped_note(uint64_t runtime) {
    if(!runtime) return;
    for(uint32_t s=join_unmapped_slot(runtime),n=0;n<JOIN_UNMAPPED_SET/2;s=(s+1u)&(JOIN_UNMAPPED_SET-1u),n++) {
        LONG64 prior=InterlockedCompareExchange64(&join_rx.unmapped_ids[s],(LONG64)runtime,0);
        if(!prior || (uint64_t)prior==runtime) return;
    }
    InterlockedIncrement64(&join_rx.unmapped_set_full);
}
static int join_unmapped_has(uint64_t runtime) {
    for(uint32_t s=join_unmapped_slot(runtime),n=0;runtime && n<JOIN_UNMAPPED_SET/2;s=(s+1u)&(JOIN_UNMAPPED_SET-1u),n++) {
        uint64_t v=(uint64_t)join_rx.unmapped_ids[s];if(!v) return 0;if(v==runtime) return 1;
    }
    return 0;
}
/* world_identity_observe during the joiner's load: the save's ID (the saver's runtime ID) -> the session's
 * portable key. 0: record *portable as the saved ID; 1: a created identity (entered here); 2: unmapped. */
static void join_jobe_hold(void);
/* Main thread, every boot frame before the admission (lockstep_gate.c). j1-56's load stacks: the joiner's job ships come from
 * JobEngine::Init 0xf0b340, which Modules::Ep1::vfunc1 (0xab1100) calls on the MoveWorker after GameClass::Load (that sets the job
 * switch again after the imports) and LeaveLoadingMode, which parks until the next loading frame releases it. Holding the switch and
 * the engine's running flag off here, before every loading frame runs, keeps them off when Init runs. */
static int join_boot_job_hold;   /* off: j1-57 held Init and the joiner had no job ships of its own, but without Init's bookkeeping its
                                  * first pass (the session's, at ~751) spawned 926 objects the session never had (900: 2,227 against
                                  * 1,773). On when Init's spawns can be held apart from its bookkeeping. */
static void join_boot_frame(void) {
    if(join_boot_job_hold && join_rx.loaded && !join_rx.admitted && join_rx.t.version>=13 && join_rx.t.njobe) join_jobe_hold();
}
static int join_import(uint64_t saved,uint64_t runtime,uint64_t *portable) {
    InterlockedIncrement64(&join_rx.imported);
    if(join_boot_job_hold && join_rx.t.version>=13 && join_rx.t.njobe) join_jobe_hold();   /* (off, see join_boot_frame) */
    const join_id_rec *r=join_idmap_find(saved);
    if(!r) {InterlockedIncrement64(&join_rx.unmapped);join_unmapped_note(runtime);return 2;}
    if(r->klass==WORLD_KEY_SAVED) {*portable=r->portable;InterlockedIncrement64(&join_rx.mapped_saved);return 0;}
    int ok=0;
    if(created_enabled() && created_live && created_by_runtime && created_by_id) {
        AcquireSRWLockExclusive(&created_lock);ok=created_live_add(runtime,r->portable,(uint32_t)((r->portable>>40)&0x7fffffu));ReleaseSRWLockExclusive(&created_lock);
        if(ok) InterlockedIncrement64(&world_created_generation);
    }
    if(ok) InterlockedIncrement64(&join_rx.mapped_created);else InterlockedIncrement64(&join_rx.created_failures);
    return 1;
}
/* Saved-ID keys of this client's own configuration against the package. The import keys every object of the join save by
 * the session's portable ID (join_import: X4 writes runtime IDs into the save, so the save's IDs are not the snapshot's), so
 * a saved ID this client is configured with names the same object as on the session if the session had it at T. The
 * snapshot fixture's twelve must be saved identities of the package (else nothing binds them: SNAPSHOT_BIND_STOP bound=0),
 * and the session must be past tick 4 (every client stops unless all twelve are enrolled by then, so at T they were: the
 * joiner enrolls them at T+1, lockstep_snapshot_fixture.h). Boot binds that only the snapshot's tick 0 can satisfy (the early
 * world trace, the player origin, the transition watch) refuse. 1 with why set. */
static int join_config_refused(const join_tables *t,char *why,size_t cap) {
    if(InterlockedCompareExchange(&world_trace_early,0,0)) {snprintf(why,cap,"boot_bind:world_trace");return 1;}
    if(origin_started) {snprintf(why,cap,"boot_bind:origin");return 1;}
    if(transition_saved_count) {snprintf(why,cap,"boot_bind:transition");return 1;}
    if(!snapshot_configured) return 0;
    if(t->gate.tick<4) {snprintf(why,cap,"fixture_early:%llu",(unsigned long long)t->gate.tick);return 1;}
    for(unsigned a=0;a<12;a++) {
        int found=0;
        for(uint32_t i=0;i<t->nids && !found;i++) found=t->ids[i].klass==WORLD_KEY_SAVED && t->ids[i].portable==snapshot_actors[a].saved;
        if(!found) {snprintf(why,cap,"fixture_unmapped:%016llx",(unsigned long long)snapshot_actors[a].saved);return 1;}
    }
    /* The session's view of each actor at T: the joiner binds what the session's gate saw (lockstep_snapshot_fixture.h). */
    if(!join_fixture_any(&t->gate)) {snprintf(why,cap,"fixture_view_missing");return 1;}
    for(unsigned a=0;a<12;a++) {
        unsigned v=t->gate.fixture[a];
        if(v!=SNAPSHOT_VIEW_NAV && v!=SNAPSHOT_VIEW_DRIFT && v!=SNAPSHOT_VIEW_BARE)
            {snprintf(why,cap,"fixture_view:%u:%s",a+1,snapshot_view_name(v));return 1;}
    }
    return 0;
}
/* gate.join_unpaired(i): the i-th unpaired sample row of the admission (1-based), NULL past the last. */
static const char *join_unpaired_row(unsigned i) {return i>=1 && i<=join_rx.nrows?join_rx.rows[i-1]:NULL;}
/* A version 1 package (a saver before the world side table) carries no HEALTH or POSE, a version 2 one no SHIELD, NAV, ATTN or
 * KEYS: a strict joiner refuses both (the loader's clocks, rotations, navigation banks, attention and first-call tables would
 * stay), a report joiner admits them and counts nothing there. */
static int join_version_refused(const join_tables *t,int mode) {return t->version<JOIN_PACKAGE_VERSION && mode==JOIN_STRICT;}
static const char *join_version_name(uint32_t version) {
    return version<2?"package_v1":version<3?"package_v2":version<4?"package_v3":version<5?"package_v4":version<6?"package_v5":version<7?"package_v6":
           version<8?"package_v7":version<9?"package_v8":version<10?"package_v9":version<11?"package_v10":version<12?"package_v11":"package_v12";
}
/* ---- version 13: the joiner's JobEngine::Init without its initial spawns (report 1.17) ----
 * j1-56's load stacks: the joiner's job ships come from JobEngine::Init 0xf0b340, which the module update calls at 0x9eb8cb with rdx =
 * a job filter (the global 0x3d196f0, IsNewGame's; on a load too, j1-59). Init runs its job pass with the initial flag on for
 * every job of the filter (all when there is none): f03e40 then spawns what is missing now. With the flag off it still adopts the
 * loaded ships into their jobs (j1-57 held all of Init: its bookkeeping missing, the first pass spawned 926 objects) and leaves the
 * spawns to the regular passes, as on the session. On a version 13 joiner, from its package load to its admission, the call at
 * 0x9eb8cb goes through join_job_init (shared thunk slot 64, a rel32 CAS), which passes Init an empty filter instead of none. */
#define JOIN_JOBINIT_SITE 0x9eb8cbu
#define JOIN_JOBINIT_FN 0xf0b340u
#define JOIN_JOBINIT_THUNK 64u
static int32_t join_jobinit_original_rel,join_jobinit_patched_rel;static int join_jobinit_installed;
static volatile LONG64 join_jobinit_calls,join_jobinit_empty;static uint32_t join_jobinit_cleared,join_jobinit_jobs;
/* The job DB (*0x6cf2170): a tree (nodes parent, left, right; the header +0x10, the leftmost +0x30, the count +0x38), each job inside
 * its node at +0x48. f03e40 runs a job's spawn branch with the initial flag off when the job's own flag +0x469 is set (node +0x4b1). */
#define JOIN_JOBDB 0x6cf2170u
#define JOIN_JOBDB_MAX 8192u
static unsigned char *join_jobdb_test;   /* tests: a synthetic DB */
static unsigned char *join_jobdb_node[JOIN_JOBDB_MAX];static unsigned char join_jobdb_flag[JOIN_JOBDB_MAX];
static struct {int *begin,*end;} join_jobinit_none;   /* a filter of no job */
static uint64_t join_jobinit_native(void *engine,void *filter) {return ((uint64_t (*)(void *,void *))(void *)(image+JOIN_JOBINIT_FN))(engine,filter);}
static uint64_t (*join_jobinit_fn)(void *,void *)=join_jobinit_native;   /* tests replace it */
static __attribute__((noinline)) uint64_t join_job_init(void *engine,void *filter) {
    InterlockedIncrement64(&join_jobinit_calls);
    /* j1-59: on a load the filter is not null either (the jobs the load's start lists); every one of them would spawn now */
    if(!(join_rx.loaded && !join_rx.admitted && join_rx.t.version>=13)) return join_jobinit_fn(engine,filter);
    filter=&join_jobinit_none;InterlockedIncrement64(&join_jobinit_empty);
    /* j1-60/61: the empty filter and request deque did not stop the spawns: the jobs with +0x469 spawn in Init's loop anyway. Their
     * flag is off for Init (the adoption runs before the spawn branch) and back after it; the regular passes spawn as the session's. */
    unsigned char *db=join_jobdb_test;uint64_t count=0;uint32_t n=0;
    if(!db) read_local(image+JOIN_JOBDB,&db,8);
    if(db && read_local(db+0x38,&count,8) && count && count<JOIN_JOBDB_MAX) {
        unsigned char *end=db+0x10,*node=NULL;read_local(db+0x30,&node,8);
        for(uint64_t i=0;i<count && node && node!=end && n<JOIN_JOBDB_MAX;i++) {
            join_jobdb_node[n]=node;join_jobdb_flag[n]=node[0x4b1];if(node[0x4b1]) {node[0x4b1]=0;++join_jobinit_cleared;}++n;
            unsigned char *r=*(unsigned char **)(node+0x10);
            if(r) {while(*(unsigned char **)(r+8)) r=*(unsigned char **)(r+8);node=r;continue;}
            unsigned char *p=*(unsigned char **)node;
            while(p && p!=end && *(unsigned char **)(p+0x10)==node) {node=p;p=*(unsigned char **)p;}
            node=p;
        }
    }
    join_jobinit_jobs=n;
    uint64_t result=join_jobinit_fn(engine,filter);
    for(uint32_t i=0;i<n;i++) join_jobdb_node[i][0x4b1]=join_jobdb_flag[i];
    return result;
}
#ifndef JOIN_OWN_PLATFORM
/* The rel32 at site+1 inside one 64-byte line: one locked CAS. */
static int join_jobinit_swap(int32_t expected,int32_t value) {
    unsigned char *rel=image+JOIN_JOBINIT_SITE+1;DWORD old,ignored;
    if(((uintptr_t)rel&63)>60 || !VirtualProtect(rel,4,PAGE_EXECUTE_READWRITE,&old)) return 0;
    int32_t actual=(int32_t)InterlockedCompareExchange((volatile LONG*)rel,(LONG)value,(LONG)expected);
    VirtualProtect(rel,4,old,&ignored);FlushInstructionCache(GetCurrentProcess(),image+JOIN_JOBINIT_SITE,5);
    return actual==expected;
}
static int join_jobinit_install(void) {
    unsigned char call[5];int32_t rel;
    if(join_jobinit_installed) return 1;
    if(!thunk || !thunk_slot_ready(JOIN_JOBINIT_THUNK) || !read_local(image+JOIN_JOBINIT_SITE,call,5) || call[0]!=0xe8) return 0;
    memcpy(&rel,call+1,4);
    if((uintptr_t)(image+JOIN_JOBINIT_SITE+5)+(intptr_t)rel!=(uintptr_t)(image+JOIN_JOBINIT_FN)) return 0;
    intptr_t distance=(intptr_t)(thunk+JOIN_JOBINIT_THUNK)-(intptr_t)(image+JOIN_JOBINIT_SITE+5);
    if(distance<INT32_MIN || distance>INT32_MAX) return 0;
    join_jobinit_original_rel=rel;join_jobinit_patched_rel=(int32_t)distance;
    if(!join_jobinit_swap(join_jobinit_original_rel,join_jobinit_patched_rel)) return 0;
    join_jobinit_installed=1;return 1;
}
static void join_jobinit_restore(void) {
    if(join_jobinit_installed && join_jobinit_swap(join_jobinit_patched_rel,join_jobinit_original_rel)) join_jobinit_installed=0;
}
#else
static int join_jobinit_install(void) {return 1;}   /* the Linux gate: its own site (not yet) */
static void join_jobinit_restore(void) {}
#endif
/* JOIN_JOBWATCH (diagnostic, with native_gate_join_watch; report 1.18): j1-90, the job split_stationminer_m_solid had a different
 * waiting ship built at 780 on the joiner (0x124633 against the session's 0x1248cf). For every job of the job DB, every vector
 * {begin, end, capacity} in the job's first 0x800 bytes whose 8-byte entries hold a watched object (as its pointer or its runtime
 * ID): one row with the job's node, the offset, the entry's place and the vector's length, at s and j. */
/* JOBS: walk the job DB in its tree's order; each job's ID hash and its state object. The node [F: j1-111 JOIN_JOBRAW node=]: +0 .. +0x18
 * the links and color, +0x20 the key's hash (equal on both sides), +0x28 the ID std::string (size +0x38, capacity +0x40; its bytes
 * inline below capacity 16, else at +0x28's pointer), the job at +0x48. The record's hash is FNV-1a of the ID's bytes. */
#define JOIN_JOBS_STATE 0x528u
#define JOIN_JOBS_FIELD 0x48u
typedef void (*join_jobs_visit)(void *ctx,uint32_t index,uint64_t hash,unsigned char *state);
static unsigned char *join_jobs_node;   /* the DB node of the job being visited (the job at +0x48) */
static void join_jobs_walk(join_jobs_visit visit,void *ctx) {
    unsigned char *db=join_jobdb_test;uint64_t count=0;
    if(!db) read_local(image+JOIN_JOBDB,&db,8);
    if(!db || !read_local(db+0x38,&count,8) || !count || count>=JOIN_JOBDB_MAX) return;
    unsigned char *end=db+0x10,*node=NULL;read_local(db+0x30,&node,8);
    for(uint64_t i=0;i<count && node && node!=end;i++) {
        uint64_t size=0,capacity=0;unsigned char *text=node+0x28,*state=NULL;char key[256];
        if(join_read(node+0x38,&size,8) && join_read(node+0x40,&capacity,8) && size && size<sizeof(key)) {
            if(capacity>=16u) (void)join_read(node+0x28,&text,8);
            if(text && join_read(text,key,(size_t)size)) {
                uint64_t h=1469598103934665603ull;
                for(uint64_t k=0;k<size;k++) {h^=(unsigned char)key[k];h*=1099511628211ull;}
                if(join_read(node+0x48+JOIN_JOBS_STATE,&state,8) && state) {join_jobs_node=node;visit(ctx,(uint32_t)i,h,state);join_jobs_node=NULL;}
            }
        }
        unsigned char *r=*(unsigned char **)(node+0x10);
        if(r) {while(*(unsigned char **)(r+8)) r=*(unsigned char **)(r+8);node=r;continue;}
        unsigned char *p=*(unsigned char **)node;
        while(p && p!=end && *(unsigned char **)(p+0x10)==node) {node=p;p=*(unsigned char **)p;}
        node=p;
    }
}
/* A state's ship vector {begin, end} at +0x18 (active) or +0x30 (waiting): its length, 65535 at most. X4's waiting check
 * (StaticShipCheckerByIsWaitingJobShip 0x854cf0) is membership in the waiting one; find_waiting_job_ship, then shuffle_list,
 * in Job_Helper's Process_Ships (j1-122 at 648: 81 draws on the joiner against the session's 77, a list four longer). */
#define JOIN_JOBS_ACTIVE 0x18u
#define JOIN_JOBS_WAITING 0x30u
static uint32_t join_jobs_ships(unsigned char *state,unsigned field) {
    uintptr_t v[2]={0,0};if(!join_read(state+field,v,16) || v[1]<v[0] || (v[1]-v[0])%8u) return 0xffffu;
    const uintptr_t n=(v[1]-v[0])/8u;return n>0xffffu?0xffffu:(uint32_t)n;
}
typedef struct {join_tables *t;uint32_t cap;} join_jobs_capture_ctx;
static void join_jobs_capture_one(void *c,uint32_t index,uint64_t hash,unsigned char *state) {
    join_jobs_capture_ctx *x=c;join_jobs_rec r={hash,0,index,0,0};
    if(!join_read(state+JOIN_JOBS_FIELD,&r.value,8)) return;
    r.active=(uint16_t)join_jobs_ships(state,JOIN_JOBS_ACTIVE);r.waiting=(uint16_t)join_jobs_ships(state,JOIN_JOBS_WAITING);
    (void)JOIN_PUSH(x->t->jobs,x->t->njobs,x->cap,r);
}
static void join_capture_jobs(join_tables *t) {join_jobs_capture_ctx x={t,0};join_jobs_walk(join_jobs_capture_one,&x);}
/* JOBG (v13, section 68; the admission opt-in, native_gate_join_jobg): the jobs' own generator words. The job spawn 0xf06650 hands
 * *(job+0x528) to U::ShipGenerator::Generate 0xc841b0 as its explicit state; the macro choice draws from and advances its first word,
 * and the spawn's loadout key is that word (lockstep_spawn_rng.h). JobDB seeds the word at import (FNV-1a of the job ID xor the
 * session seed) and the save does not hold it, so a joiner's job starts from its seed where the session's has advanced by every
 * spawn since its own import [F: j1-213 SPAWN_RNG_ROW at 818: a job spawns nine Teladi fighters with the same callers on both sides,
 * the joiner's words four steps behind the session's (its fifth the session's first): other ship macros and loadouts, 115 created
 * objects against 113 and the first digest mismatch at 819; the spawns of 809 and 816 equal]. The capture writes every job's word by
 * its ID hash; with the opt-in the admission writes the session's where it differs. Status jobg=on:written:equal:missing. */
static int join_jobg_on;
static uint64_t join_jobg_written,join_jobg_equal,join_jobg_missing;
typedef struct {join_tables *t;uint32_t cap;} join_jobg_capture_ctx;
static void join_jobg_capture_one(void *c,uint32_t index,uint64_t hash,unsigned char *state) {
    (void)index;join_jobg_capture_ctx *x=c;join_jobg_rec r={hash,0};
    if(!join_read(state,&r.word,8)) return;
    (void)JOIN_PUSH(x->t->jobg,x->t->njobg,x->cap,r);
}
static int join_jobg_cmp(const void *a,const void *b) {
    uint64_t x=((const join_jobg_rec *)a)->hash,y=((const join_jobg_rec *)b)->hash;return x<y?-1:x>y;
}
static void join_capture_jobg(join_tables *t) {
    join_jobg_capture_ctx x={t,0};join_jobs_walk(join_jobg_capture_one,&x);
    if(t->njobg>1) qsort(t->jobg,t->njobg,sizeof(*t->jobg),join_jobg_cmp);
    {uint32_t k=0;for(uint32_t i=0;i<t->njobg;i++) {if(!t->jobg[i].hash || (k && t->jobg[k-1].hash==t->jobg[i].hash)) continue;t->jobg[k++]=t->jobg[i];}t->njobg=k;}
}
typedef struct {uint64_t hash;unsigned char *state;} join_jobs_mine;
typedef struct {join_jobs_mine *v;uint32_t n,cap;} join_jobs_admit_ctx;
static void join_jobs_admit_one(void *c,uint32_t index,uint64_t hash,unsigned char *state) {
    (void)index;join_jobs_admit_ctx *x=c;join_jobs_mine e={hash,state};(void)JOIN_PUSH(x->v,x->n,x->cap,e);
}
static void join_admit_jobg(const join_tables *s) {
    join_jobg_written=join_jobg_equal=join_jobg_missing=0;
    if(!join_jobg_on) return;
    join_jobs_admit_ctx x={NULL,0,0};join_jobs_walk(join_jobs_admit_one,&x);
    for(uint32_t i=0;i<s->njobg;i++) {
        unsigned char *state=NULL;
        for(uint32_t k=0;k<x.n;k++) if(x.v[k].hash==s->jobg[i].hash) {state=x.v[k].state;break;}
        if(!state) {++join_jobg_missing;continue;}
        if(join_poke((uintptr_t)state,&s->jobg[i].word,8)) ++join_jobg_written; else ++join_jobg_equal;
    }
    free(x.v);
}
/* native_gate_join_jobw (experiment, default off): the admission writes the session's state +0x48 where it differs (j1-215 after
 * JOBG: 705 of 1,211 jobs differ, a small count on the session; at 902 the joiner's jobs spawn three ships where the session's spawn
 * five). The first 16 differences show as rows jobs side=joiner ... own= session=. */
static int join_jobw_on;static uint64_t join_jobw_written;
/* JOIN_JOBDUMP (diagnostic, native_gate_join_jobdump: up to 8 job ID hashes; with the watch at s, k and t): the job's first 0x600
 * bytes and its state's first 0x1c0 (j1-217: at 902 the session's jobs 0xd0b01187c580022e and 0x6a7d2c0fa238cad0 spawn in this
 * order and two ships more, the joiner's in the other order). */
static uint64_t join_jobdump_hash[8];static unsigned join_jobdump_n;
static int join_jobdump_add(uint64_t hash) {
    for(unsigned i=0;i<join_jobdump_n;i++) if(join_jobdump_hash[i]==hash) return 1;
    if(!hash || join_jobdump_n>=8u) return 0;
    join_jobdump_hash[join_jobdump_n++]=hash;return 1;
}
typedef struct {char side;uint64_t tick;} join_jobdump_ctx;
static void join_jobdump_one(void *c,uint32_t index,uint64_t hash,unsigned char *state) {
    const join_jobdump_ctx *x=c;unsigned k=0;
    for(;k<join_jobdump_n;k++) if(join_jobdump_hash[k]==hash) break;
    if(k>=join_jobdump_n || !join_jobs_node) return;
    static unsigned char job[0x600],st[0x1c0];static const char digits[]="0123456789abcdef";
    memset(job,0,sizeof(job));memset(st,0,sizeof(st));(void)join_read(join_jobs_node+0x48,job,sizeof(job));(void)join_read(state,st,sizeof(st));
    const size_t cap=2u*(sizeof(job)+sizeof(st))+256u;char *out=malloc(cap);size_t at=0;
    if(!out) return;
    at+=(size_t)snprintf(out+at,cap-at,"[X4LAN] NATIVE_GATE JOIN_JOBDUMP side=%c tick=%llu hash=%016llx index=%u job=",x->side,(unsigned long long)x->tick,
                         (unsigned long long)hash,index);
    for(unsigned b=0;b<sizeof(job);b++) {out[at++]=digits[job[b]>>4];out[at++]=digits[job[b]&15u];}
    at+=(size_t)snprintf(out+at,cap-at," state=");
    for(unsigned b=0;b<sizeof(st);b++) {out[at++]=digits[st[b]>>4];out[at++]=digits[st[b]&15u];}
    at+=(size_t)snprintf(out+at,cap-at,"\r\n");
    crash_note_write(out,(int)at);free(out);
}
static void join_jobdump(char side,uint64_t tick) {
    if(!join_jobdump_n) return;
    join_jobdump_ctx x={side,tick};join_jobs_walk(join_jobdump_one,&x);
}
/* JOIN_JOBQ (diagnostic, with native_gate_join_jobdump, at s, k and t): the job engine's spawn request queue, a std::deque of
 * 0x30-byte requests at engine +0x48 (map), +0x50 (map size), +0x58 (offset), +0x60 (size), one request a block; 0xf0bff0 takes ten a
 * pass from its front (j1-219: the job spawns of 902 come from it, five on the session, three on the joiner, two in the other
 * order). A row per request: the job's ID hash (entry +0, the job), its three runtime IDs (+8, +0x18, +0x20) as portable keys,
 * +0x10 and the flag bytes +0x28..+0x2a. */
typedef struct {unsigned char *job;uint64_t hash;} join_jobq_job;
typedef struct {join_jobq_job *v;uint32_t n,cap;} join_jobq_jobs;
static void join_jobq_job_one(void *c,uint32_t index,uint64_t hash,unsigned char *state) {
    (void)index;(void)state;join_jobq_jobs *x=c;if(!join_jobs_node) return;
    join_jobq_job e={join_jobs_node+0x48,hash};(void)JOIN_PUSH(x->v,x->n,x->cap,e);
}
/* JOIN_JOBCNT (diagnostic, native_gate_join_jobcnt_at=N, with the watch): after step N every job with a state whose active or waiting
 * vector is not empty or whose +0x48 is set: its ID hash, the two lengths, +0x48, the last update time +0xd0 and the flag +0x10 (the job
 * update 0xf03e40 requests ships up to its quota minus the active and waiting ships of its linked jobs; j1-221: at 902 the session's
 * update after the first spawn queues three, the joiner's one). */
static uint64_t join_jobcnt_at;
typedef struct {char *out;size_t at,cap;uint64_t tick;} join_jobcnt_ctx;
static void join_jobcnt_one(void *c,uint32_t index,uint64_t hash,unsigned char *state) {
    join_jobcnt_ctx *x=c;uint64_t v48=0,vd0=0;unsigned char f10=0;
    const uint32_t active=join_jobs_ships(state,JOIN_JOBS_ACTIVE),waiting=join_jobs_ships(state,JOIN_JOBS_WAITING);
    (void)join_read(state+0x48,&v48,8);(void)join_read(state+0xd0,&vd0,8);(void)join_read(state+0x10,&f10,1);
    if(!active && !waiting && !v48) return;
    if(x->at+160u>=x->cap) {crash_note_write(x->out,(int)x->at);x->at=0;}
    x->at+=(size_t)snprintf(x->out+x->at,x->cap-x->at,"[X4LAN] NATIVE_GATE JOIN_JOBCNT tick=%llu hash=%016llx index=%u active=%u waiting=%u v48=%llx vd0=%016llx f10=%u" "\r\n",
                          (unsigned long long)x->tick,(unsigned long long)hash,index,active,waiting,(unsigned long long)v48,(unsigned long long)vd0,f10);
}
static void join_jobcnt_dump(uint64_t tick) {
    if(!join_jobcnt_at || tick!=join_jobcnt_at) return;
    join_jobcnt_ctx x={malloc(1u<<16),0,1u<<16,tick};if(!x.out) return;
    join_jobs_walk(join_jobcnt_one,&x);
    if(x.at) crash_note_write(x.out,(int)x.at);
    free(x.out);
}
static void join_jobq_dump(char side,uint64_t tick) {
    if(!join_jobdump_n) return;
    unsigned char *e=join_jobe_engine(),*map=NULL;uint64_t mapsize=0,off=0,size=0;
    if(!e || !join_read(e+0x48,&map,8) || !join_read(e+0x50,&mapsize,8) || !join_read(e+0x58,&off,8) || !join_read(e+0x60,&size,8)) return;
    join_jobq_jobs jobs={NULL,0,0};join_jobs_walk(join_jobq_job_one,&jobs);
    const size_t cap=1u<<16;char *out=malloc(cap);size_t at=0;
    if(!out) {free(jobs.v);return;}
    at+=(size_t)snprintf(out+at,cap-at,"[X4LAN] NATIVE_GATE JOIN_JOBQ side=%c tick=%llu size=%llu mapsize=%llu off=%llu" "\r\n",side,(unsigned long long)tick,
                         (unsigned long long)size,(unsigned long long)mapsize,(unsigned long long)off);
    for(uint64_t i=0;i<size && i<256u && mapsize && !(mapsize&(mapsize-1)) && at+256u<cap;i++) {
        unsigned char *blk=NULL,r[0x30];uint64_t q[6];
        if(!join_read(map+((off+i)&(mapsize-1))*8u,&blk,8) || !blk || !join_read(blk,r,sizeof(r))) break;
        memcpy(q,r,sizeof(q));uint64_t hash=0;unsigned k1=0,k2=0,k3=0;
        for(uint32_t k=0;k<jobs.n;k++) if((uint64_t)(uintptr_t)jobs.v[k].job==q[0]) {hash=jobs.v[k].hash;break;}
        const uint64_t a=q[1]?world_identity_key(q[1],&k1):0,b=q[3]?world_identity_key(q[3],&k2):0,c=q[4]?world_identity_key(q[4],&k3):0;
        at+=(size_t)snprintf(out+at,cap-at,"[X4LAN] NATIVE_GATE JOIN_JOBQ side=%c tick=%llu i=%llu job=%016llx a=%llx b=%llx c=%llx v10=%llx flags=%02x%02x%02x" "\r\n",
                             side,(unsigned long long)tick,(unsigned long long)i,(unsigned long long)hash,(unsigned long long)(a?a:q[1]|(q[1]?1ull<<62:0)),
                             (unsigned long long)(b?b:q[3]|(q[3]?1ull<<62:0)),(unsigned long long)(c?c:q[4]|(q[4]?1ull<<62:0)),(unsigned long long)q[2],r[0x28],r[0x29],r[0x2a]);
    }
    crash_note_write(out,(int)at);free(out);free(jobs.v);
}
static void join_admit_jobs(const join_tables *s) {
    join_jobs_admit_ctx x={NULL,0,0};unsigned rows=0,shown=0;join_jobs_walk(join_jobs_admit_one,&x);join_jobw_written=0;
    for(uint32_t i=0;i<s->njobs;i++) {
        unsigned char *state=NULL;
        for(uint32_t k=0;k<x.n;k++) if(x.v[k].hash==s->jobs[i].hash) {state=x.v[k].state;break;}
        if(!state) {++join_rx.jobs_missing;continue;}
        /* compared only [F: j1-112: written in 411 of 1,211 jobs, the census of every tick unchanged and the job ships still built
         * in the other order]: its meaning is not known and it does not decide the pick */
        uint64_t own=0;
        if(join_read(state+JOIN_JOBS_FIELD,&own,8) && own==s->jobs[i].value) ++join_rx.jobs_equal;
        else {
            ++join_rx.jobs_written;
            if(shown<16u) {++shown;join_world_row(&rows,"jobs side=joiner hash=%016llx index=%u own=%llx session=%llx",(unsigned long long)s->jobs[i].hash,
                                                  s->jobs[i].index,(unsigned long long)own,(unsigned long long)s->jobs[i].value);}
            if(join_jobw_on && join_poke((uintptr_t)(state+JOIN_JOBS_FIELD),&s->jobs[i].value,8)) ++join_jobw_written;
        }
        const uint32_t active=join_jobs_ships(state,JOIN_JOBS_ACTIVE),waiting=join_jobs_ships(state,JOIN_JOBS_WAITING);
        if(active!=s->jobs[i].active) ++join_rx.jobs_active_differ;
        if(waiting!=s->jobs[i].waiting) ++join_rx.jobs_waiting_differ;
        if(active!=s->jobs[i].active || waiting!=s->jobs[i].waiting) {   /* the joiner's waiting ships by portable ID (8 at most) */
            char ships[160];int n=0;uintptr_t v[2]={0,0};ships[0]=0;
            if(join_read(state+JOIN_JOBS_WAITING,v,16)) for(uintptr_t at=v[0];at<v[1] && at<v[0]+64u && n>=0 && n<(int)sizeof(ships)-20;at+=8) {
                unsigned char *ship=NULL;uint64_t runtime=0;unsigned klass=0;
                if(join_read((void*)at,&ship,8) && ship && join_read(ship+8,&runtime,8))
                    n+=snprintf(ships+n,sizeof(ships)-(size_t)n,"%s%llx",n?",":"",(unsigned long long)world_identity_key(runtime,&klass));
            }
            join_world_row(&rows,"jobs side=joiner hash=%016llx index=%u active=%u/%u waiting=%u/%u ships=%s",(unsigned long long)s->jobs[i].hash,
                           s->jobs[i].index,active,s->jobs[i].active,waiting,s->jobs[i].waiting,ships[0]?ships:"-");
        }
    }
    free(x.v);
}
static unsigned char *join_jobraw_seen[8];static unsigned join_jobraw_nseen;
static int join_jobraw_done(unsigned char *state) {
    for(unsigned k=0;k<join_jobraw_nseen;k++) if(join_jobraw_seen[k]==state) return 1;
    if(join_jobraw_nseen<8u) join_jobraw_seen[join_jobraw_nseen++]=state;
    return 0;
}
static void join_jobraw_dump(char side,uint64_t job,unsigned char *state,unsigned char *dbnode) {
    unsigned char s[0x1c0];size_t cap=1u<<15,at=0;char *out=malloc(cap);
    if(!out) return;
    if(!join_read(state,s,sizeof(s))) {free(out);return;}
    at+=(size_t)snprintf(out+at,cap-at,"[X4LAN] NATIVE_GATE JOIN_JOBRAW side=%c job=%llu state=",side,(unsigned long long)job);
    for(unsigned k=0;k<sizeof(s);k++) at+=(size_t)snprintf(out+at,cap-at,"%02x",s[k]);
    {   /* the DB node's first 0x50 bytes (its key before the job at +0x48), and 32 bytes behind each of its pointers */
        unsigned char nb[0x50];
        at+=(size_t)snprintf(out+at,cap-at," node=");
        if(dbnode && join_read(dbnode,nb,sizeof(nb))) {
            for(unsigned k=0;k<sizeof(nb);k++) at+=(size_t)snprintf(out+at,cap-at,"%02x",nb[k]);
            for(unsigned q=0x18;q+8<=0x48;q+=8) {
                unsigned char *p=NULL,tb[32];memcpy(&p,nb+q,8);
                if(!p || !join_read(p,tb,sizeof(tb))) continue;
                at+=(size_t)snprintf(out+at,cap-at," n%x=",q);
                for(unsigned k=0;k<sizeof(tb);k++) at+=(size_t)snprintf(out+at,cap-at,"%02x",tb[k]);
            }
        } else at+=(size_t)snprintf(out+at,cap-at,"-");
    }
    for(unsigned off=0;off+24<=sizeof(s) && at+0x700<cap;off+=8) {
        uintptr_t v[3];memcpy(v,s+off,24);
        if(!v[0] || v[1]<v[0] || v[2]<v[1] || v[1]==v[0] || v[1]-v[0]>0x800u || ((v[1]-v[0])&7u)) continue;
        unsigned char d[0x300];const size_t n=v[1]-v[0]<sizeof(d)?(size_t)(v[1]-v[0]):sizeof(d);
        if(!join_read((void *)v[0],d,n)) continue;
        at+=(size_t)snprintf(out+at,cap-at," v%x=%llu:",off,(unsigned long long)(v[1]-v[0]));
        for(size_t k=0;k<n;k++) at+=(size_t)snprintf(out+at,cap-at,"%02x",d[k]);
    }
    {   /* the hashed container at +0x90..+0xc8 (size +0xb8, buckets +0xc0): its list from the head +0xb0, each node's 0x40 bytes
         * and the qwords of it that are runtime IDs as portable keys (in list order) */
        unsigned char *head=NULL,*node=NULL;
        memcpy(&head,s+0xb0,8);
        at+=(size_t)snprintf(out+at,cap-at," list=");
        if(head && join_read(head,&node,8))
            for(unsigned k=0;k<16u && node && node!=head && at+600<cap;k++) {
                unsigned char nb[0x40];
                if(!join_read(node,nb,sizeof(nb))) break;
                at+=(size_t)snprintf(out+at,cap-at,"[");
                for(unsigned q=0;q<sizeof(nb);q++) at+=(size_t)snprintf(out+at,cap-at,"%02x",nb[q]);
                for(unsigned q=0x10;q+8<=sizeof(nb);q+=8) {
                    uint64_t id;memcpy(&id,nb+q,8);unsigned klass;const uint64_t key=id && id<(1ull<<40)?join_key(id,&klass):0;
                    if(key) at+=(size_t)snprintf(out+at,cap-at," +%x=%llx",q,(unsigned long long)key);
                }
                at+=(size_t)snprintf(out+at,cap-at,"]");
                memcpy(&node,nb,8);
            }
        {   /* the bucket array (+0x98 .. +0xa0) and every node it names, in bucket order: 0x30 bytes, runtime IDs as keys */
            uintptr_t b0=0,b1=0;memcpy(&b0,s+0x98,8);memcpy(&b1,s+0xa0,8);
            at+=(size_t)snprintf(out+at,cap-at," buckets=");
            if(b0 && b1>b0 && b1-b0<=0x400u) {
                uintptr_t bk[128];const unsigned nb=(unsigned)((b1-b0)/8u);
                if(join_read((void *)b0,bk,(size_t)nb*8u)) for(unsigned k=0;k<nb && at+400<cap;k++) {
                    unsigned char e[0x30];
                    if(!bk[k] || !join_read((void *)bk[k],e,sizeof(e))) {at+=(size_t)snprintf(out+at,cap-at,"{%u:-}",k);continue;}
                    at+=(size_t)snprintf(out+at,cap-at,"{%u:%llx:",k,(unsigned long long)(bk[k]&0xffffu));
                    for(unsigned q=0;q<sizeof(e);q++) at+=(size_t)snprintf(out+at,cap-at,"%02x",e[q]);
                    for(unsigned q=0x10;q+8<=sizeof(e);q+=8) {
                        uint64_t id;memcpy(&id,e+q,8);unsigned klass;const uint64_t key=id && id<(1ull<<40)?join_key(id,&klass):0;
                        if(key) at+=(size_t)snprintf(out+at,cap-at," +%x=%llx",q,(unsigned long long)key);
                    }
                    at+=(size_t)snprintf(out+at,cap-at,"}");
                }
            }
        }
        unsigned char *p190=NULL,pb[0x80];memcpy(&p190,s+0x190,8);
        at+=(size_t)snprintf(out+at,cap-at," p190=");
        if(p190 && join_read(p190,pb,sizeof(pb))) {
            for(unsigned q=0;q<sizeof(pb);q++) at+=(size_t)snprintf(out+at,cap-at,"%02x",pb[q]);
            for(unsigned q=0;q+8<=sizeof(pb);q+=8) {
                uint64_t id;memcpy(&id,pb+q,8);unsigned klass;const uint64_t key=id && id<(1ull<<40)?join_key(id,&klass):0;
                if(key) at+=(size_t)snprintf(out+at,cap-at," +%x=%llx",q,(unsigned long long)key);
            }
        } else at+=(size_t)snprintf(out+at,cap-at,"-");
    }
    at+=(size_t)snprintf(out+at,cap-at,"\r\n");
    crash_note_write(out,(int)at);free(out);
}
static void join_job_watch_dump(char side) {
    if(!join_watch.n) return;
    join_jobraw_nseen=0;
    unsigned char *db=join_jobdb_test;uint64_t count=0;char out[8192];size_t at=0;
    if(!db) read_local(image+JOIN_JOBDB,&db,8);
    if(!db || !read_local(db+0x38,&count,8) || !count || count>=JOIN_JOBDB_MAX) return;
    unsigned char *end=db+0x10,*node=NULL;read_local(db+0x30,&node,8);
    uintptr_t want[2*JOIN_WATCH_MAX];unsigned nw=0;
    for(unsigned w=0;w<join_watch.n;w++) {
        unsigned char *o=join_watch.runtime[w]?join_object(join_watch.runtime[w]):NULL;
        want[nw++]=(uintptr_t)o;want[nw++]=(uintptr_t)join_watch.runtime[w];
    }
    for(uint64_t i=0;i<count && node && node!=end && at+300<sizeof(out);i++) {
        unsigned char *job=node+0x48,buf[0x800];
        for(unsigned part=0;part<2u;part++) {   /* the job, then its state object *(job+0x528) (f03e40: its ship IDs at +0x18) */
            unsigned char *base=job;
            if(part && (!join_read(job+0x528,&base,8) || !base)) break;
            if(!join_read(base,buf,sizeof(buf))) continue;
            for(unsigned off=0;off+24<=sizeof(buf);off+=8) {
                uintptr_t v[3];memcpy(v,buf+off,24);
                if(!v[0] || v[1]<v[0] || v[2]<v[1] || ((v[1]-v[0])&7u) || (v[1]-v[0])/8u>4096u) continue;
                uint64_t n=(v[1]-v[0])/8u;uintptr_t e[64];if(!n || n>64u || !join_read((void *)v[0],e,(size_t)n*8u)) continue;
                for(uint64_t k=0;k<n;k++) for(unsigned w=0;w<nw;w++) if(want[w] && e[k]==want[w]) {
                    at+=(size_t)snprintf(out+at,sizeof(out)-at,"[X4LAN] NATIVE_GATE JOIN_JOBWATCH side=%c job=%llu %s+%x place=%llu of=%llu watched=%llx as=%s\r\n",
                                         side,(unsigned long long)i,part?"state":"job",off,(unsigned long long)k,(unsigned long long)n,
                                         (unsigned long long)join_watch.portable[w/2],w&1?"runtime":"pointer");
                    /* JOIN_JOBRAW: the state object that holds a watched ship in its waiting vector (+0x30, j1-92): its first 0x1c0
                     * bytes and every vector in them (up to 0x300 bytes each; the requests: ship, requester, their times) */
                    if(part && off==0x30u && !join_jobraw_done(base)) join_jobraw_dump(side,i,base,node);
                }
            }
        }
        unsigned char *r=*(unsigned char **)(node+0x10);
        if(r) {while(*(unsigned char **)(r+8)) r=*(unsigned char **)(r+8);node=r;continue;}
        unsigned char *p=*(unsigned char **)node;
        while(p && p!=end && *(unsigned char **)(p+0x10)==node) {node=p;p=*(unsigned char **)p;}
        node=p;
    }
    if(at) crash_note_write(out,(int)at);
}
/* ---- the joiner's boot event passes held (native_gate_join_hold_boot_events, report 1.18; default off) ----
 * A load delivers the events due at the load's time before the first gated tick: EventSystem_ProcessDueEvents 0x99bb50 (no arguments,
 * no result) from GameClass::Load at 0x9f0a43 (boot event pass 1) and from UniverseFrameUpdate at 0x9fdf86 in the boot frame (pass 2;
 * every frame's pass after it). The session delivers what was due at T in its pass of T+1; the joiner's boot passes deliver it before
 * the admission, whose AI and MD state overwrite what the handlers did (j1-66: the 24 session-only rows of 634, the order ready and
 * cancelled handlers of five job ships). j1-67 held both passes whole: those rows went, but the load's own events (NPCCreatedEvent
 * 4,096, JoinedFormationEvent 6,748, UpdateGravidarAccessEvent 11,899) reached the admitted MD at T+1 (11,691 joiner-only rows at
 * 634). So the hold is by class: the classes the session had due at T (EVQT, the rebuilt classes apart), computed at the package's
 * load. From then to the admission both calls go through join_boot_events (shared thunk slot 80, a rel32 CAS each): it flushes the
 * command queue, swaps every due event of those classes in its source's tree (node +0x28) for a stand-in of its own (slot 0 frees
 * it, slot 1 a type nobody listens to: the pass destroys it unheard) and keeps the event; then the pass runs. After the admission
 * the kept events go back to their sources at their times through PostEvent: the pass of T+1 delivers them, as the session's. */
#define JOIN_BOOTEV_FN 0x99bb50u
#define JOIN_BOOTEV_THUNK 80u
#define JOIN_BOOTEV_SITES 2u
static const uint32_t join_bootev_site[JOIN_BOOTEV_SITES]={0x9f0a43u,0x9fdf86u};
static int32_t join_bootev_original[JOIN_BOOTEV_SITES],join_bootev_patched[JOIN_BOOTEV_SITES];
static int join_bootev_installed[JOIN_BOOTEV_SITES],join_bootev_on;
static volatile LONG64 join_bootev_calls,join_bootev_held;
static void join_bootev_native(void) {((void (*)(void))(void *)(image+JOIN_BOOTEV_FN))();}
static void (*join_bootev_fn)(void)=join_bootev_native;   /* tests replace it */
#define JOIN_BOOTEV_CLASSES 16u
#define JOIN_BOOTEV_STANDIN_TYPE 0x7ffffff0
#define JOIN_BOOTEV_FLUSH 0x99abe0u          /* U::EventSystem_FlushCommandQueue */
static uint32_t join_bootev_class[JOIN_BOOTEV_CLASSES];static unsigned join_bootev_nclasses;
typedef struct {unsigned char *source,*event;double time;} join_bootev_item;
static join_bootev_item *join_bootev_items;static uint32_t join_bootev_n,join_bootev_cap,join_bootev_reposted;
/* The stand-in: vtable, the count the pass would raise (+8), a context of none (+0x10). */
static void join_bootev_standin_free(void *self,int flags) {(void)flags;free(self);}
static int join_bootev_standin_type(void *self) {(void)self;return JOIN_BOOTEV_STANDIN_TYPE;}
static uint64_t join_bootev_standin_zero(void *self) {(void)self;return 0;}
static void *join_bootev_standin_vt[8]={(void *)join_bootev_standin_free,(void *)join_bootev_standin_type,(void *)join_bootev_standin_zero,
    (void *)join_bootev_standin_zero,(void *)join_bootev_standin_zero,(void *)join_bootev_standin_zero,(void *)join_bootev_standin_zero,
    (void *)join_bootev_standin_zero};
/* The classes the session had due at T: its EVQT records at or before T's game time, the rebuilt classes apart (EVQT reposts them). */
static void join_bootev_classes(void) {
    double now;memcpy(&now,&join_rx.t.gate.game,8);join_bootev_nclasses=0;
    for(uint32_t i=0;i<join_rx.t.nevqt;i++) {
        const join_evqt_rec *r=&join_rx.t.evqt[i];double t;memcpy(&t,&r->time,8);unsigned k=0;
        if(!r->cls || t>now+1e-9 || join_evqt_is_rebuilt(r->cls)) continue;
        while(k<join_bootev_nclasses && join_bootev_class[k]!=r->cls) ++k;
        if(k==join_bootev_nclasses && k<JOIN_BOOTEV_CLASSES) join_bootev_class[join_bootev_nclasses++]=r->cls;
    }
}
static int join_bootev_held_class(uintptr_t vt) {
    for(unsigned k=0;k<join_bootev_nclasses;k++) if(vt==(uintptr_t)image+join_bootev_class[k]) return 1;
    return 0;
}
/* Every due event of a held class swapped for a stand-in and kept. */
static void join_bootev_stash(void) {
    double now;memcpy(&now,&join_rx.t.gate.game,8);uint64_t count=0;unsigned char *node=NULL;
    if(!join_bootev_nclasses || !join_read(join_evq_at(JOIN_EVQ_COUNT),&count,8) || !count || !join_read(join_evq_at(JOIN_EVQ_LEFT),&node,8)) return;
    for(uint64_t i=0;i<count && i<200000u && node;i++,node=join_evq_next(node)) {
        double key=0;unsigned char *src=NULL,*q=NULL,*e=NULL;uint64_t n=0;
        if(!join_read(node+0x20,&key,8) || key>now+1e-9) break;   /* the schedule by its sources' next times */
        if(!join_read(node+0x28,&src,8) || !src || !join_read(src+0x20,&q,8) || !q || !join_read(q+0x28,&n,8) || !join_read(q+0x20,&e,8)) continue;
        for(uint64_t k=0;k<n && k<4096u && e;k++,e=join_evq_next(e)) {
            unsigned char *ev=NULL;uintptr_t vt=0;double t=0;
            if(!join_read(e+0x20,&t,8) || t>now+1e-9) break;
            if(!join_read(e+0x28,&ev,8) || !ev || !join_read(ev,&vt,8) || !join_bootev_held_class(vt)) continue;
            void **standin=calloc(1,0x40);join_bootev_item it={src,ev,t};
            if(!standin || !JOIN_PUSH(join_bootev_items,join_bootev_n,join_bootev_cap,it)) {free(standin);return;}
            standin[0]=(void *)join_bootev_standin_vt;*(void **)(e+0x28)=standin;
        }
    }
}
static __attribute__((noinline)) void join_boot_events(void) {
    InterlockedIncrement64(&join_bootev_calls);
    if(join_rx.loaded && !join_rx.admitted && join_rx.t.version>=13) {
        InterlockedIncrement64(&join_bootev_held);
        if(join_bootev_fn==join_bootev_native) ((void (*)(void))(void *)(image+JOIN_BOOTEV_FLUSH))();   /* the queued posts first */
        join_bootev_stash();
    }
    join_bootev_fn();
}
/* After the admission: the kept events back to their sources at their times (the pass of T+1 delivers them). */
static void join_bootev_repost(void) {
    for(uint32_t i=0;i<join_bootev_n;i++) {join_evqt_post(join_bootev_items[i].source,join_bootev_items[i].event,join_bootev_items[i].time,0);++join_bootev_reposted;}
    free(join_bootev_items);join_bootev_items=NULL;join_bootev_n=join_bootev_cap=0;
}
#ifndef JOIN_OWN_PLATFORM
/* A call's rel32 at site+1 inside one 64-byte line: one locked CAS. */
static int join_rel_swap(uint32_t site,int32_t expected,int32_t value) {
    unsigned char *rel=image+site+1;DWORD old,ignored;
    if(((uintptr_t)rel&63)>60 || !VirtualProtect(rel,4,PAGE_EXECUTE_READWRITE,&old)) return 0;
    int32_t actual=(int32_t)InterlockedCompareExchange((volatile LONG*)rel,(LONG)value,(LONG)expected);
    VirtualProtect(rel,4,old,&ignored);FlushInstructionCache(GetCurrentProcess(),image+site,5);
    return actual==expected;
}
static void join_bootev_restore(void) {
    for(unsigned i=0;i<JOIN_BOOTEV_SITES;i++)
        if(join_bootev_installed[i] && join_rel_swap(join_bootev_site[i],join_bootev_patched[i],join_bootev_original[i])) join_bootev_installed[i]=0;
}
static int join_bootev_install(void) {
    if(!thunk || !thunk_slot_ready(JOIN_BOOTEV_THUNK)) return 0;
    for(unsigned i=0;i<JOIN_BOOTEV_SITES;i++) {
        unsigned char call[5];int32_t rel;const uint32_t site=join_bootev_site[i];
        if(join_bootev_installed[i]) continue;
        if(!read_local(image+site,call,5) || call[0]!=0xe8) {join_bootev_restore();return 0;}
        memcpy(&rel,call+1,4);
        intptr_t distance=(intptr_t)(thunk+JOIN_BOOTEV_THUNK)-(intptr_t)(image+site+5);
        if((uintptr_t)(image+site+5)+(intptr_t)rel!=(uintptr_t)(image+JOIN_BOOTEV_FN) || distance<INT32_MIN || distance>INT32_MAX)
            {join_bootev_restore();return 0;}
        join_bootev_original[i]=rel;join_bootev_patched[i]=(int32_t)distance;
        if(!join_rel_swap(site,join_bootev_original[i],join_bootev_patched[i])) {join_bootev_restore();return 0;}
        join_bootev_installed[i]=1;
    }
    return 1;
}
/* JOIN_WATCHPCF's five call sites of PropagateCacheFlags (diagnostic, with native_gate_join_watch): FlushOrphanCacheData, BuildCache
 * (children, attachments), Connect (vfunc624), UniverseFrameUpdate. */
#define JOIN_PCF_THUNK 96u
#define JOIN_PCF_SITES 5u
static const uint32_t join_pcf_site[JOIN_PCF_SITES]={0x8e7dd9u,0x3d4d0au,0x3d4ea6u,0x3d76aau,0x9fe53cu};
static int32_t join_pcf_original[JOIN_PCF_SITES],join_pcf_patched[JOIN_PCF_SITES];
static int join_pcf_installed[JOIN_PCF_SITES];
static void join_pcf_restore(void) {
    for(unsigned i=0;i<JOIN_PCF_SITES;i++)
        if(join_pcf_installed[i] && join_rel_swap(join_pcf_site[i],join_pcf_patched[i],join_pcf_original[i])) join_pcf_installed[i]=0;
}
/* Each site on its own (a diagnostic: a site whose rel32 crosses a 64-byte line, as UniverseFrameUpdate's 0x9fe53c, the galaxy
 * root's refresh, cannot be swapped by one CAS and is left native); the mask of the sites taken into join_pcf_mask. */
static unsigned join_pcf_mask;
static int join_pcf_install(void) {
    if(!thunk || !thunk_slot_ready(JOIN_PCF_THUNK)) return 0;
    for(unsigned i=0;i<JOIN_PCF_SITES;i++) {
        unsigned char call[5];int32_t rel;const uint32_t site=join_pcf_site[i];
        if(join_pcf_installed[i]) continue;
        if(!read_local(image+site,call,5) || call[0]!=0xe8) continue;
        memcpy(&rel,call+1,4);
        intptr_t distance=(intptr_t)(thunk+JOIN_PCF_THUNK)-(intptr_t)(image+site+5);
        if((uintptr_t)(image+site+5)+(intptr_t)rel!=(uintptr_t)(image+JOIN_PCF_FN) || distance<INT32_MIN || distance>INT32_MAX) continue;
        join_pcf_original[i]=rel;join_pcf_patched[i]=(int32_t)distance;
        if(!join_rel_swap(site,join_pcf_original[i],join_pcf_patched[i])) continue;
        join_pcf_installed[i]=1;join_pcf_mask|=1u<<i;
    }
    return join_pcf_mask!=0;
}
#else
static int join_bootev_install(void) {return 1;}   /* the Linux gate: its own sites (not yet) */
static void join_bootev_restore(void) {}
static unsigned join_pcf_mask;
static int join_pcf_install(void) {return 1;}
static void join_pcf_restore(void) {}
#endif
/* gate.join_load(package, sha, mode): verify and keep the package; before the load request. */
static int join_load(const char *path,const char *sha_hex,int mode) {
    if(!stats.active || !accept_ui() || !boot_mode || boot_phase || stats.issued || join_rx.loaded || !world_identity_installed ||
       wire_version!=3 || peer_on() || wcg.armed) return join_fail_rx(JOIN_WHY_NOT_ARMED,"phase");
#ifdef LOCKSTEP_ADAPTIVE
    if(wire_adaptive) return join_fail_rx(JOIN_WHY_NOT_ARMED,"adaptive");
#endif
    WCHAR wide[MAX_PATH];unsigned char *data=NULL;size_t size=0;const char *why="read";
    if(!path || !sha_hex || !valid_hex(sha_hex,64) || !join_wide(path,wide,MAX_PATH) || !join_file_read(wide,&data,&size)) return join_fail_rx(JOIN_WHY_FILE_MISSING,"package_read");
    unsigned char sha[32];char hex[65];lockstep_sha256(data,size,sha);hex32(sha,hex);
    if(strcmp(hex,sha_hex)) {free(data);return join_fail_rx(JOIN_WHY_MISMATCH,"package_sha_announced");}
    join_tables t;int ok=join_package_parse(data,size,&t,&why);free(data);
    if(!ok) {join_tables_free(&t);return join_fail_rx(JOIN_WHY_MISMATCH,why);}
    if(t.gate.clients!=wire_clients || t.gate.lookahead!=wire_lookahead || t.gate.tick_hz!=gate_tick_hz || t.gate.wire_version!=wire_version)
        {join_tables_free(&t);return join_fail_rx(JOIN_WHY_MISMATCH,"session_shape");}
    if((t.flags&1u) && mode==JOIN_STRICT) {join_tables_free(&t);return join_fail_rx(JOIN_WHY_UNSUPPORTED,"diagnostic_package");}
    if(join_version_refused(&t,mode)) {const char *old=join_version_name(t.version);join_tables_free(&t);return join_fail_rx(JOIN_WHY_MISMATCH,old);}
    if(!join_pins_pristine()) {join_tables_free(&t);char why[48];snprintf(why,sizeof(why),"pins:%s",join_pins_why);return join_fail_rx(JOIN_WHY_CAPTURE,why);}
    {char refused[64];if(join_config_refused(&t,refused,sizeof(refused))) {join_tables_free(&t);return join_fail_rx(JOIN_WHY_MISMATCH,refused);}}
    join_rx.t=t;join_rx.mode=mode;join_rx.handover=UINT64_MAX;
    if(!join_idmap_build()) {join_tables_free(&join_rx.t);return join_fail_rx(JOIN_WHY_MISMATCH,"idmap");}
    if(join_rx.t.version>=13 && !join_jobinit_install()) {join_tables_free(&join_rx.t);return join_fail_rx(JOIN_WHY_CAPTURE,"jobinit_site");}
    if(join_rx.t.version>=13 && join_bootev_on) join_bootev_classes();
    if(join_rx.t.version>=13 && join_bootev_on && join_bootev_nclasses && !join_bootev_install())
        {join_jobinit_restore();join_tables_free(&join_rx.t);return join_fail_rx(JOIN_WHY_CAPTURE,"bootev_site");}
    if(join_rx.t.version>=13 && join_precise_on) {   /* the join save's full-precision numbers, read exactly by the load */
        if(!join_parse_install()) {join_bootev_restore();join_jobinit_restore();join_tables_free(&join_rx.t);return join_fail_rx(JOIN_WHY_CAPTURE,"parse_site");}
        InterlockedExchange(&join_parse_on,1);
    }
    if(join_ids_on && (join_rx.t.version<13 || !join_rx.t.gate.rid_next || !join_ids_arm(join_rx.t.gate.rid_next))) {   /* the session's IDs */
        char why[40];snprintf(why,sizeof(why),"ids:%s",join_ids_stage);
        InterlockedExchange(&join_parse_on,0);join_parse_restore();join_bootev_restore();join_jobinit_restore();join_tables_free(&join_rx.t);
        return join_fail_rx(JOIN_WHY_CAPTURE,why);
    }
    if(join_watch.n) join_rw_load_prepare(&join_rx.t);   /* diagnostic: JOIN_REFWATCH in the load (lockstep_join_world.h) */
    join_rx.loaded=1;return 1;
}
/* gate.join_lua(T, text) in frame T on an armed saver: the probe's providers' state, written with the package. */
static int join_lua_put(uint64_t tick,const char *text,size_t length) {
    if(!accept_ui() || join_cap.state!=JOIN_ARMED || tick!=join_cap.tick || stats.issued!=tick || !text || length>(1u<<20)) return 0;
    uint32_t lines=0;
    for(size_t i=0;i<length;i++) {if(text[i]=='\n') ++lines;else if(text[i]<32 || text[i]>126) return 0;}
    int close=length && text[length-1]!='\n';char *copy=malloc(length+2);if(!copy) return 0;
    memcpy(copy,text,length);if(close) copy[length++]='\n';
    free(join_cap.lua);join_cap.lua=copy;join_cap.lua_len=length;join_cap.nlua=lines+(uint32_t)close;return 1;
}
/* The joiner's own proposal is not in permits sealed before its handover tick (the relay's stand-in is). */
static const char *join_own(uint64_t source,const char *proposal) {
    return join_rx.based && source<join_rx.handover?gate_proposal_any:proposal;
}
static int join_unsealed_ok(uint64_t source) {return join_rx.based && source<join_rx.handover;}
static int join_handover(uint64_t tick) {
    if(!join_rx.based || tick<=join_rx.t.gate.tick || join_rx.handover!=UINT64_MAX) return 0;
    join_rx.handover=tick;return 1;
}
/* Admission helpers (main thread, MoveWorker idle). Each returns 0 only for a strict refusal. */
typedef struct {join_ctx_rec key;uintptr_t ctx;uint32_t count;} join_ctx_slot;
static int join_ctx_slot_cmp(const void *a,const void *b) {return join_ctx_cmp(&((const join_ctx_slot *)a)->key,&((const join_ctx_slot *)b)->key);}
static join_ctx_slot *join_ctx_find(join_ctx_slot *slots,uint32_t n,const join_ctx_rec *key) {
    uint32_t lo=0,hi=n;
    while(lo<hi) {uint32_t mid=(lo+hi)/2;int c=join_ctx_cmp(&slots[mid].key,key);if(!c) {while(mid && !join_ctx_cmp(&slots[mid-1].key,key)) --mid;return &slots[mid];} if(c<0) lo=mid+1;else hi=mid;}
    return NULL;
}
/* The first context of this key not taken yet in this pass (repeated keys are taken in turn); the first one again
 * when every one is taken (more captured entries than contexts: counted by the caller's consistency checks). */
static join_ctx_slot *join_ctx_take(join_ctx_slot *slots,uint32_t n,const join_ctx_rec *key) {
    join_ctx_slot *first=join_ctx_find(slots,n,key),*s=first;
    if(!s) return NULL;
    for(;s<slots+n && !join_ctx_cmp(&s->key,key);s++) if(!s->count) {++s->count;return s;}
    ++first->count;return first;
}
static int join_admit_ai(const join_engine *e) {
    uintptr_t *list=NULL;uint32_t listed=0,n=0,cap=0;join_ctx_slot *slots=NULL;int strict=join_rx.mode==JOIN_STRICT;char why[320];
    /* The joiner's contexts: the global list, as the session's capture read it (join_capture_ctx). */
    if(!join_ctx_list(e,&list,&listed)) {snprintf(why,sizeof(why),"contexts:%s",join_fault);return join_fail_rx(JOIN_WHY_CAPTURE,why);}
    for(uint32_t p=0;p<listed;p++) {
        join_ctx_slot s;unsigned flags;memset(&s,0,sizeof(s));
        if(!join_ctx_key(e,list[p],&s.key,&flags)) {
            join_note_ctx("ctx",p,list[p]);free(list);free(slots);snprintf(why,sizeof(why),"contexts:%s",join_fault);
            return join_fail_rx(JOIN_WHY_CAPTURE,why);
        }
        s.ctx=list[p];if(!JOIN_PUSH(slots,n,cap,s)) {free(list);free(slots);return join_fail_rx(JOIN_WHY_CAPTURE,"memory");}
    }
    free(list);
    qsort(slots,n,sizeof(*slots),join_ctx_slot_cmp);
    /* Context keys: the joiner's multiset against the session's; the first unpaired ones of each side as rows. */
    uint32_t i=0,j=0;unsigned extra_rows=0,missing_rows=0;
    while(i<n || j<join_rx.t.nctx) {
        int c=i==n?1:j==join_rx.t.nctx?-1:join_ctx_cmp(&slots[i].key,&join_rx.t.ctx[j]);
        if(!c) {++i;++j;continue;}
        if(c<0 ? extra_rows++<JOIN_ROW_SIDE : missing_rows++<JOIN_ROW_SIDE) {
            const join_ctx_rec *k=c<0?&slots[i].key:&join_rx.t.ctx[j];char name[96];uintptr_t ctx=c<0?slots[i].ctx:0,entity=0;uint64_t runtime=0;
            if(!ctx) for(uint32_t p=0;p<n && k->script;p++) if(slots[p].key.script==k->script) {ctx=slots[p].ctx;break;}  /* a name for the hash */
            if(!ctx || !join_script_name(ctx,name)) snprintf(name,sizeof(name),"?");
            if(c<0) {
                if(join_read((void *)(ctx+JOIN_CTX_ENTITY),&entity,8) && entity) (void)join_read((void *)(entity+8),&runtime,8);
                join_row("ctx side=joiner class=%u owner=%016llx script=%016llx version=%u name=%s ctx=%llx entity=%llx entity_id=%llx",k->klass,
                         (unsigned long long)k->owner,(unsigned long long)k->script,k->version,name,JOIN_P(ctx),JOIN_P(entity),(unsigned long long)runtime);
            } else join_row("ctx side=session class=%u owner=%016llx script=%016llx version=%u name=%s",k->klass,(unsigned long long)k->owner,
                            (unsigned long long)k->script,k->version,name);
        }
        if(c<0) {++join_rx.ctx_extra;++i;} else {++join_rx.ctx_missing;++j;}
    }
    for(uint32_t k=1;k<n;k++) if(!join_ctx_cmp(&slots[k-1].key,&slots[k].key)) ++join_rx.ctx_ambiguous;
    if(strict && (join_rx.ctx_extra || join_rx.ctx_missing)) {free(slots);return join_fail_rx(JOIN_WHY_MISMATCH,"contexts_differ");}
    /* Timed wakes: empty the tree through the engine's erase, refill in the captured order. */
    uintptr_t header=(uintptr_t)(e->director+JOIN_DIR_TREE),node=0;
    for(unsigned guard=0;guard<JOIN_MAX_RECORDS;guard++) {
        if(!join_read((void *)(header+JOIN_TREE_FIRST),&node,8)) {free(slots);return join_fail_rx(JOIN_WHY_CAPTURE,"tree");}
        if(!node || node==header) break;
        uintptr_t ctx=0;void *next=NULL;
        if(!join_read((void *)(node+JOIN_NODE_CTX),&ctx,8)) {free(slots);return join_fail_rx(JOIN_WHY_CAPTURE,"tree");}
        e->erase((void *)header,&next,(void *)node);
        if(ctx) {double none=-1.0;uint32_t zero=0;memcpy((void *)(ctx+JOIN_CTX_DUE),&none,8);memcpy((void *)(ctx+JOIN_CTX_REASON),&zero,4);memcpy((void *)(ctx+JOIN_CTX_NODE),&header,8);}
        ++join_rx.wake_erased;
    }
    for(uint32_t k=0;k<join_rx.t.nwake;k++) {
        const join_wake_rec *w=&join_rx.t.wake[k];join_ctx_rec key={w->owner,w->script,w->klass,w->version};
        join_ctx_slot *s=join_ctx_take(slots,n,&key);         /* repeated keys: counted ambiguous above */
        if(!s) {++join_rx.wake_missing;continue;}
        double due=join_double(w->due);void *out=NULL;uintptr_t ctx=s->ctx;
        e->insert((void *)header,&out,&due,&ctx);
        memcpy((void *)(ctx+JOIN_CTX_NODE),&out,8);memcpy((void *)(ctx+JOIN_CTX_DUE),&w->due,8);memcpy((void *)(ctx+JOIN_CTX_REASON),&w->reason,4);
        ++join_rx.wake_applied;
    }
    for(uint32_t k=0;k<n;k++) slots[k].count=0;
    /* Wake masks: none outside the wake vectors, as on the session (a context leaves its mask set until Move runs it, and asks for
     * no second place in a vector while it is set). j1-83: two pilots' move.generic contexts queued in the joiner's boot (blocking
     * action done, mask 2) lost their place when the vectors below were emptied but kept the mask, so the escort handler's wake of
     * T+1 never queued them again and they never ran. The session's queued contexts get their masks back below. */
    for(uint32_t k=0;k<n;k++) {
        uint32_t mask=0,zero=0;
        if(join_read((void *)(slots[k].ctx+JOIN_CTX_MASK),&mask,4) && mask) {memcpy((void *)(slots[k].ctx+JOIN_CTX_MASK),&zero,4);++join_rx.mask_cleared;}
    }
    /* Wake vectors: the captured contexts in the captured order, in place. */
    for(uint32_t v=0;v<2;v++) {
        unsigned char *vec=e->director+JOIN_DIR_VEC+v*0x18u;uintptr_t vb=0,ve=0;
        if(!join_read(vec,&vb,8) || !join_read(vec+8,&ve,8) || ve<vb) {free(slots);return join_fail_rx(JOIN_WHY_CAPTURE,"vectors");}
        uint64_t size=(ve-vb)/8,used=0;
        for(uint64_t p=0;p<size;p++) {
            uintptr_t ctx=*(uintptr_t *)(vb+p*8);
            if(ctx) {int64_t none=-1;memcpy((void *)(ctx+JOIN_CTX_VEC+v*8u),&none,8);}
            *(uintptr_t *)(vb+p*8)=0;
        }
        for(uint32_t k=0;k<join_rx.t.nvec;k++) {
            const join_vec_rec *r=&join_rx.t.vec[k];if(r->vector!=v) continue;
            join_ctx_rec key={r->owner,r->script,r->klass,r->version};
            if(used>=size) {++join_rx.vec_missing;continue;}
            join_ctx_slot *s=join_ctx_take(slots,n,&key);
            if(!s) {++join_rx.vec_missing;continue;}
            *(uintptr_t *)(vb+used*8)=s->ctx;int64_t index=(int64_t)used;memcpy((void *)(s->ctx+JOIN_CTX_VEC+v*8u),&index,8);
            memcpy((void *)(s->ctx+JOIN_CTX_MASK),&r->mask,4);++used;++join_rx.vec_applied;
        }
    }
    memcpy(e->director+JOIN_DIR_VEC_INDEX,&join_rx.t.gate.vec_index,4);
    free(slots);
    if(strict && (join_rx.wake_missing || join_rx.vec_missing)) return join_fail_rx(JOIN_WHY_MISMATCH,"wakes_unplaced");
    return 1;
}
/* A joiner's heap entry at the admission: its key (time and seq zeroed for the pairing), the entry as loaded, paired or not. */
typedef struct {join_md_rec key;unsigned char entry[24];uint32_t paired;} join_md_live;
static int join_md_live_cmp(const void *a,const void *b) {return join_md_keysort(&((const join_md_live *)a)->key,&((const join_md_live *)b)->key);}
/* The first fields (1 name, 2 + root, 3 + ordinal) of two keys. */
static int join_md_prefix(const join_md_rec *x,const join_md_rec *y,unsigned fields) {
    if(x->name!=y->name) return x->name<y->name?-1:1;
    if(fields>1 && x->root!=y->root) return x->root<y->root?-1:1;
    if(fields>2 && x->ordinal!=y->ordinal) return x->ordinal<y->ordinal?-1:1;
    return 0;
}
static int join_md_live_has(const join_md_live *live,uint32_t m,const join_md_rec *k,unsigned fields) {
    uint32_t lo=0,hi=m;
    while(lo<hi) {uint32_t mid=lo+(hi-lo)/2;int c=join_md_prefix(&live[mid].key,k,fields);if(!c) return 1;if(c<0) lo=mid+1;else hi=mid;}
    return 0;
}
/* The save rounds a cue's time to six significant digits: a loaded activation within that of the session's. */
static int join_activation_close(double loaded,double session) {
    return isfinite(loaded) && isfinite(session) && fabs(loaded-session)<=5.0001e-6*fabs(session);
}
/* A joiner cue's key parts: the static path; an instance's root creator (its static path, 'inst:' when the creator is an
 * instance), the root's index among the creator's non-empty created entries / the list's length, the sub-cue slots from the
 * root down. Text for the logs (pointer values included; never in a package). */
static void join_cue_detail(const join_engine *e,uintptr_t cue,char *out,size_t cap) {
    uintptr_t def=0,root=0,creator=0,cdef=0;uint64_t k=0;char path[160],cpath[160],slots[64];size_t used=0;
    if(!join_read((void *)(cue+JOIN_CUE_DEF),&def,8) || !def) {snprintf(out,cap,"path=? cue=%llx",JOIN_P(cue));return;}
    if(!join_static_path(e,def,&k,path,sizeof(path))) snprintf(path,sizeof(path),"?");
    if(def==cue || !join_read((void *)(cue+JOIN_CUE_ROOT),&root,8) || !root) {snprintf(out,cap,"path=%s cue=%llx%s",path,JOIN_P(cue),def==cue?"":" root=?");return;}
    unsigned path_slots[16],depth=0;
    for(uintptr_t cur=cue;cur!=root && depth<16;) {
        uintptr_t parent=0;uint32_t count=0;
        if(!join_read((void *)(cur+JOIN_CUE_PARENT),&parent,8) || !parent) break;
        int64_t slot=join_vector_index(parent+JOIN_CUE_CHILDREN,cur,0,&count);
        path_slots[depth++]=slot<0?9999u:(unsigned)slot;cur=parent;
    }
    slots[0]=0;
    for(unsigned i=depth;i-->0;) {char part[16];int n=snprintf(part,sizeof(part),"%s%u",used?".":"",path_slots[i]);used=join_append(slots,sizeof(slots),used,part,(size_t)n);}
    uint32_t count=0;int64_t index=-3;cpath[0]=0;
    if(join_read((void *)(root+JOIN_CUE_CREATOR),&creator,8) && creator) {
        index=join_vector_index(creator+JOIN_CUE_INSTANCES,root,1,&count);
        if(!join_read((void *)(creator+JOIN_CUE_DEF),&cdef,8) || !cdef || !join_static_path(e,cdef,&k,cpath,sizeof(cpath))) snprintf(cpath,sizeof(cpath),"?");
    }
    snprintf(out,cap,"path=%s cue=%llx root_cue=%llx creator=%s%s index=%lld/%u slots=%s",path,JOIN_P(cue),JOIN_P(root),
             creator && cdef && cdef!=creator?"inst:":"",creator?cpath:"none",(long long)index,count,depth?slots:"-");
}
/* A session record's static path: a joiner static cue with that path key, if the key cache holds one. */
static void join_name_text(const join_engine *e,uint64_t name,char *out,size_t cap) {
    snprintf(out,cap,"?");
    for(unsigned i=0;i<JOIN_ROOTS;i++) if(join_statics[i].at && join_statics[i].key==name) {
        uint64_t k=0;if(!join_static_path(e,join_statics[i].at,&k,out,cap) || k!=name) snprintf(out,cap,"?");
        return;
    }
}
static int join_admit_md(const join_engine *e) {
    uintptr_t begin=0,end=0;int strict=join_rx.mode==JOIN_STRICT;
    if(!join_read(e->scheduler+JOIN_MD_HEAP,&begin,8) || !join_read(e->scheduler+JOIN_MD_HEAP+8,&end,8) || end<begin || (end-begin)%24)
        return join_fail_rx(JOIN_WHY_CAPTURE,"md_heap");
    uint32_t n=(uint32_t)((end-begin)/24),m=0,nmd=join_rx.t.nmd;
    join_md_live *live=n?calloc(n,sizeof(*live)):NULL;if(n && !live) return join_fail_rx(JOIN_WHY_CAPTURE,"memory");
    join_keys_reset();
    for(uint32_t i=0;i<n;i++) {
        unsigned char *entry=(unsigned char *)(begin+(uintptr_t)i*24);uintptr_t cue=0;memcpy(&cue,entry+16,8);
        if(!cue) continue;                                          /* a cancelled entry: dropped below */
        memcpy(live[m].entry,entry,24);memcpy(&live[m].key.kind,entry+12,4);
        if(!join_cue_key(e,cue,&live[m].key)) {
            char why[320];double time;uint32_t seq;memcpy(&time,entry,8);memcpy(&seq,entry+8,4);
            join_note_record("md#%u,kind=%u,seq=%u,time=%.17g,cue=%llx",i,live[m].key.kind,seq,time,JOIN_P(cue));
            snprintf(why,sizeof(why),"cue_key:%s",join_fault);free(live);join_keys_reset();return join_fail_rx(JOIN_WHY_CAPTURE,why);
        }
        ++m;
    }
    join_md_rec *want=nmd?malloc(nmd*sizeof(*want)):NULL;unsigned char *taken=nmd?calloc(nmd,1):NULL;
    if(nmd && (!want || !taken)) {free(live);free(want);free(taken);join_keys_reset();return join_fail_rx(JOIN_WHY_CAPTURE,"memory");}
    if(nmd) memcpy(want,join_rx.t.md,nmd*sizeof(*want));
    qsort(want,nmd,sizeof(*want),join_md_keysort);
    /* Pair by key; within one key the captured entries are taken in (time, seq) order. */
    for(uint32_t i=0;i<m;i++) live[i].key.time=0,live[i].key.seq=0;
    qsort(live,m,sizeof(*live),join_md_live_cmp);
    uint32_t i=0,j=0,out;
    while(i<m || j<nmd) {
        int c=i==m?1:j==nmd?-1:join_md_keycmp(&live[i].key,&want[j]);
        if(c<0) {++join_rx.md_extra;++i;continue;}
        if(c>0) {++join_rx.md_missing;++j;continue;}
        if(j+1<nmd && !join_md_keycmp(&want[j],&want[j+1]) && (want[j].time!=want[j+1].time)) ++join_rx.md_ambiguous;
        memcpy(live[i].entry,&want[j].time,8);memcpy(live[i].entry+8,&want[j].seq,4);live[i].key.time=want[j].time;live[i].key.seq=want[j].seq;
        /* The activation as the session had it (the save rounded it); further off than that rounding is a difference. */
        if(live[i].key.activation!=want[j].activation) {
            uintptr_t cue;memcpy(&cue,live[i].entry+16,8);
            if(join_activation_close(join_double(live[i].key.activation),join_double(want[j].activation))) ++join_rx.md_activation_fixed;
            else ++join_rx.md_activation_far;
            memcpy((void *)(cue+JOIN_CUE_ACTIVATION),&want[j].activation,8);live[i].key.activation=want[j].activation;
        }
        live[i].paired=1;taken[j]=1;++join_rx.md_applied;++i;++j;
    }
    /* Each unpaired session entry by the first key part the joiner has no counterpart for: name (no such static cue on the
     * heap), root (the cue's root instance), ordinal (its sub-cue slots), else kind or state. */
    for(uint32_t k=0;k<nmd;k++) {
        if(taken[k]) continue;
        unsigned part=!join_md_live_has(live,m,&want[k],1)?0:!join_md_live_has(live,m,&want[k],2)?1:!join_md_live_has(live,m,&want[k],3)?2:3;
        ++join_rx.md_diff[part];
    }
    /* Samples with every key part: the first unpaired entries of each side in key order (the same names first when a key part
     * differs for many: both lists are sorted by name). */
    {
        unsigned rows=0;char text[160],detail[300];
        for(uint32_t k=0;k<nmd && rows<JOIN_ROW_SIDE;k++) {
            if(taken[k]) continue;
            const join_md_rec *r=&want[k];join_name_text(e,r->name,text,sizeof(text));
            join_row("md side=session name=%016llx root=%016llx ordinal=%08x kind=%u state=%u activation=%.17g time=%.17g seq=%u path=%s",
                     (unsigned long long)r->name,(unsigned long long)r->root,r->ordinal,r->kind,r->state,join_double(r->activation),
                     join_double(r->time),r->seq,text);
            ++rows;
        }
        rows=0;
        for(uint32_t k=0;k<m && rows<JOIN_ROW_SIDE;k++) {
            if(live[k].paired) continue;
            const join_md_rec *r=&live[k].key;double time;uint32_t seq;uintptr_t cue;
            memcpy(&time,live[k].entry,8);memcpy(&seq,live[k].entry+8,4);memcpy(&cue,live[k].entry+16,8);join_cue_detail(e,cue,detail,sizeof(detail));
            join_row("md side=joiner name=%016llx root=%016llx ordinal=%08x kind=%u state=%u activation=%.17g time=%.17g seq=%u %s",
                     (unsigned long long)r->name,(unsigned long long)r->root,r->ordinal,r->kind,r->state,join_double(r->activation),time,seq,detail);
            ++rows;
        }
    }
    join_keys_reset();free(taken);
    if(strict && (join_rx.md_extra || join_rx.md_missing || join_rx.md_ambiguous || join_rx.md_activation_far))
        {free(live);free(want);return join_fail_rx(JOIN_WHY_MISMATCH,join_rx.md_activation_far && !join_rx.md_extra && !join_rx.md_missing?"md_activation_far":"md_heap_differs");}
    /* The heap: matched entries sorted by (time, seq) (a sorted array is a valid min-heap), cancelled entries gone, and in
     * report mode the joiner's unmatched entries gone too (md_dropped): J1 j1-34's four were cues X4 instantiates on every load
     * (Signal_Leaks.Manager.PlayerChangesSpace, the Patch_Userdata cues of Story_Research_Embassy and X4Ep1_Mentor_Subscriptions,
     * Setup_DLC_Timelines.Timelines_Unlock_Message, activated at the joiner's load), which the session ran at its own load long
     * before T; left on the heap they ran at T+1 on the joiner alone. The rows above still name each one. */
    join_md_rec *order=m?malloc(m*sizeof(*order)):NULL;
    if(m && !order) {free(live);free(want);return join_fail_rx(JOIN_WHY_CAPTURE,"memory");}
    uint32_t kept=0;
    for(uint32_t k=0;k<m;k++) {
        if(!live[k].paired) {++join_rx.md_dropped;continue;}
        memcpy(&order[kept].time,live[k].entry,8);memcpy(&order[kept].seq,live[k].entry+8,4);order[kept].name=k;++kept;
    }
    qsort(order,kept,sizeof(*order),join_md_order);
    for(uint32_t k=0;k<kept;k++) memcpy((unsigned char *)begin+(uintptr_t)k*24,live[order[k].name].entry,24);
    out=kept;
    *(uintptr_t *)(e->scheduler+JOIN_MD_HEAP+8)=begin+(uintptr_t)out*24;
    memcpy(e->md_seq,&join_rx.t.gate.md_seq,4);
    free(live);free(want);free(order);return 1;
}
/* rid set mode: the joiner's next ID skips forward to the session's (the run up to it stays unused). */
/* X4's construction ID counter (UICONS_CID_COUNTER 0x38f4dd0 under UICONS_CID_LOCK 0x3d6b940, lockstep_ui_construct.h): the
 * next ID a build item draws. j1-141 (the lockstep session's PILOT_RECEIPT at 660): the joiner's UIBUILD_1 C field 0x5e87 where the
 * session's was 0x5e82, everything else in the receipt equal: the joiner's load drew five more. The session's at T rides the GATE
 * record and the joiner's admission sets it (status cid=session:joiner_before). */
#define JOIN_CID_COUNTER 0x38f4dd0u
#define JOIN_CID_LOCK 0x3d6b940u
static uint64_t join_cid_before;
/* X4 initializes the lock at its start; a zeroed one (never initialized: a mapped image in the native tests) is left alone. */
static int join_cid_ready(const CRITICAL_SECTION *l) {return l->LockCount!=0 || l->DebugInfo || l->SpinCount;}
static uint64_t join_cid_get(void) {
    CRITICAL_SECTION *lock=(CRITICAL_SECTION*)(void*)(image+JOIN_CID_LOCK);uint64_t v;
    if(!join_cid_ready(lock)) return 0;
    EnterCriticalSection(lock);v=*(volatile uint64_t*)(image+JOIN_CID_COUNTER);LeaveCriticalSection(lock);return v;
}
static void join_admit_cid(void) {
    if(join_rx.t.version<13 || !join_rx.t.gate.cid) return;
    CRITICAL_SECTION *lock=(CRITICAL_SECTION*)(void*)(image+JOIN_CID_LOCK);
    if(!join_cid_ready(lock)) return;
    EnterCriticalSection(lock);join_cid_before=*(volatile uint64_t*)(image+JOIN_CID_COUNTER);
    *(volatile uint64_t*)(image+JOIN_CID_COUNTER)=join_rx.t.gate.cid;LeaveCriticalSection(lock);
}
/* The unpaused clock (TimerBase 0x6b60e48 under 0x6b60e10; lockstep_speech_clock.h): NPC speech lines end on it. With
 * native_gate_speech_clock every session client set it to SCLOCK_VALUE at its first gated frame and has added each frame's step
 * since, so at T it holds the same double on all of them; a joiner's own boot left another value, and its speech clock module
 * would set the start again (or refuse, past_value, stop 196). The saver's value at T rides the GATE record; the admission
 * writes it and marks the speech clock set (stage join), so the joiner adds the same steps from T on. Status
 * unpaused=session:joiner_before (0:0 when not carried). */
static uint64_t join_unpaused_before;
static uint64_t join_unpaused_get(void) {
    CRITICAL_SECTION *lock=(CRITICAL_SECTION*)(void*)(image+SCLOCK_LOCK);uint64_t v;
    if(!join_cid_ready(lock)) return 0;
    EnterCriticalSection(lock);v=*(volatile uint64_t*)(image+SCLOCK_CLOCK);LeaveCriticalSection(lock);return v;
}
static void join_admit_unpaused(void) {
    if(join_rx.t.version<13 || !join_rx.t.gate.unpaused) return;
    CRITICAL_SECTION *lock=(CRITICAL_SECTION*)(void*)(image+SCLOCK_LOCK);
    if(!join_cid_ready(lock)) return;
    EnterCriticalSection(lock);join_unpaused_before=*(volatile uint64_t*)(image+SCLOCK_CLOCK);
    *(volatile uint64_t*)(image+SCLOCK_CLOCK)=join_rx.t.gate.unpaused;LeaveCriticalSection(lock);
    if(sclock_armed) {sclock_before_bits=join_unpaused_before;sclock_set=1;sclock_set_tick=stats.issued;sclock_stage="join";}
}
/* NMESH (v13, section 54; lockstep_nav_mesh.h): the saver's nav mesh receipt, its count and the terms it folded; the joiner adopts
 * them and leaves its catch-up builds of those helpers out of its fold up to T + JOIN_NMESH_CATCHUP (status nmesh=adopted:skipped). */
#define JOIN_NMESH_CATCHUP 10u
static int join_nmesh_adopted;
static void join_capture_nmesh(join_tables *t) {
    uint64_t receipt=0,builds=0;const uint64_t *terms=NULL;uint32_t n=0;
    if(!nmesh_join_state(&receipt,&builds,&terms,&n)) return;
    join_nmesh_rec *r=malloc((size_t)(n+2u)*sizeof(*r));
    if(!r) return;
    r[0].v=receipt;r[1].v=builds;for(uint32_t i=0;i<n;i++) r[2u+i].v=terms[i];
    t->nmesh=r;t->nnmesh=n+2u;
}
static void join_admit_nmesh(void) {
    const join_tables *s=&join_rx.t;
    if(s->version<13 || s->nnmesh<2) return;
    join_nmesh_adopted=nmesh_join_adopt(s->nmesh[0].v,s->nmesh[1].v,&s->nmesh[2].v,s->nnmesh-2u,s->gate.tick+JOIN_NMESH_CATCHUP);
}
/* NOCT (v13, section 55): lockstep_nav_octree.h noct_join_export / noct_join_adopt. Status noct=adopted:live:requested:hidden:
 * unbuilt:unmapped noct_cap=. */
static int join_noct_adopted;static uint64_t join_noct_counts[4],join_noct_unmapped;
static int join_noct_shape(const join_noct_rec *r,uint32_t n) {
    if(!n || r[0].kind!=JOIN_NOCT_HEAD || r[0].b>r[0].a) return 0;
    uint32_t i=1;uint64_t want=r[0].b;
    for(;i<n && r[i].kind==JOIN_NOCT_ENTRY;i++) {if(r[i].a!=want || r[i].flag>1u || r[i].d>1u) return 0;++want;}
    if(want!=r[0].a) return 0;
    for(;i<n;i++) if(r[i].kind!=JOIN_NOCT_HIDDEN || r[i].flag>1u) return 0;
    return 1;
}
static void join_capture_noct(join_tables *t) {
    uint64_t head[4];noct_join_entry *e=NULL;noct_join_hide *h=NULL;uint32_t ne=0,nh=0;
    if(!noct_join_export(head,&e,&ne,&h,&nh)) return;
    join_noct_rec *r=calloc((size_t)ne+nh+1u,sizeof(*r));
    if(!r) {free(e);free(h);return;}
    uint32_t n=0;r[n++]=(join_noct_rec){head[0],head[1],head[2],head[3],0,0,JOIN_NOCT_HEAD,0,0};
    for(uint32_t i=0;i<ne;i++) {
        unsigned klass=0;uint64_t portable=e[i].id?join_key(e[i].id,&klass):0;
        r[n++]=(join_noct_rec){e[i].index,portable,e[i].assigned,e[i].live,klass,e[i].flag,JOIN_NOCT_ENTRY,e[i].agreed,e[i].shown};
    }
    for(uint32_t i=0;i<nh;i++) {
        unsigned klass=0;uint64_t portable=h[i].id?join_key(h[i].id,&klass):0;
        if(portable) r[n++]=(join_noct_rec){h[i].due,portable,0,0,klass,h[i].flag,JOIN_NOCT_HIDDEN,0,0};
    }
    free(e);free(h);t->noct=r;t->nnoct=n;
}
static void join_admit_noct(const join_tables *s,const join_tables *mine) {
    if(s->version<13 || !s->nnoct) return;
    uint32_t ne=0,nh=0;
    for(uint32_t i=1;i<s->nnoct;i++) {if(s->noct[i].kind==JOIN_NOCT_ENTRY) ++ne;else ++nh;}
    noct_join_entry *e=calloc(ne?ne:1u,sizeof(*e));noct_join_hide *h=calloc(nh?nh:1u,sizeof(*h));
    void **oe=calloc(ne?ne:1u,sizeof(void *)),**oh=calloc(nh?nh:1u,sizeof(void *));
    if(!e || !h || !oe || !oh) {free(e);free(h);free(oe);free(oh);return;}
    uint32_t je=0,jh=0;
    for(uint32_t i=1;i<s->nnoct;i++) {
        const join_noct_rec *r=&s->noct[i];
        const uint64_t runtime=r->b?join_runtime_of(mine,r->b):0;unsigned char *object=runtime?join_object(runtime):NULL;
        if(r->b && !object) ++join_noct_unmapped;
        if(r->kind==JOIN_NOCT_ENTRY) {e[je]=(noct_join_entry){r->a,runtime,r->c,r->flag,r->agreed,r->shown,(uint8_t)r->d};oe[je++]=object;}
        else {h[jh]=(noct_join_hide){runtime,r->a,r->flag};oh[jh++]=object;}
    }
    const uint64_t head[4]={s->noct[0].a,s->noct[0].b,s->noct[0].c,s->noct[0].d};
    join_noct_adopted=noct_join_adopt(head,e,ne,h,nh,oe,oh,join_noct_counts);
    free(e);free(h);free(oe);free(oh);
}
static int join_admit_rid(void) {
    if(rid_mode!=RID_SET || !join_rx.t.gate.rid_next) {join_ids_restore();return 1;}
    unsigned char *map=rid_map();if(!map) {join_ids_restore();return join_fail_rx(JOIN_WHY_CAPTURE,"rid_map");}
    CRITICAL_SECTION *lock=(CRITICAL_SECTION *)(map+0x10);EnterCriticalSection(lock);
    const int ids=InterlockedExchange(&join_ids_active,0)!=0;   /* lockstep_join_ids.h: the load is over, its list closed */
    rid_counts_t c=rid_counts(map);uint32_t want=(uint32_t)(join_rx.t.gate.rid_next&0x1ffffffu);int ok=1;const char *why="rid";
    if(join_rx.t.gate.rid_next>>25) {ok=0;why="rid_generation";}
    else if(ids) {   /* the saved objects hold their slots below the session's next one: over the run, holes and all */
        ok=join_ids_skip(map,want,&why);
        if(ok) {join_rx.rid_skipped=(uint64_t)join_ids_skipped;rid_next_id=rid_next(map);}
    }
    else if(!c.head || c.head>want) {ok=0;why="rid_head_past";}
    else {
        for(uint32_t slot=c.head;slot<want && ok;slot++) {
            const volatile uint64_t *p=rid_slot(map,slot);
            if(!p || rid_slot_used(map,slot) || (uint32_t)(*p>>32)!=slot+1u) {ok=0;why="rid_run";}
        }
        volatile uint64_t *first=rid_slot(map,want);
        if(ok && (!first || rid_slot_used(map,want) || c.free<want-c.head+1u)) {ok=0;why="rid_target";}
        if(ok) {
            *first&=0xffffffff00000000ULL;*(volatile uint32_t *)(map+RID_F_HEAD)=want;*(volatile uint32_t *)(map+RID_F_FREE)=c.free-(want-c.head);
            join_rx.rid_skipped=want-c.head;rid_next_id=rid_next(map);
        }
    }
    LeaveCriticalSection(lock);
    return ok || join_rx.mode!=JOIN_STRICT?1:join_fail_rx(JOIN_WHY_MISMATCH,why);
}
/* At the admission (boot phase 3, main thread): clocks, AI, MD, allocator, tick base. 0 stops (strict). */
/* Set by a layer that rebuilds or releases objects the join save holds in another form (the lockstep session's pilot layer: a
 * loaded PlayerController): called at the admission on the main thread right before the runtime ID skip, while the joiner's
 * load list still takes every shared allocation (lockstep_join_ids.h); 0 stops the join (before_skip). */
static int (*join_admit_before_skip)(void);
/* Set by the lockstep session's pass layer (region camera, mass traffic, travel snapshot): a joiner's synced pass of T, run at
 * the admission on the main thread once the tick base is T (stats.issued == T), so that T+1 starts from the patched passes of
 * T as the session's does instead of the loading frame's (j1-141..j1-143: REGION_PASS, MASS_TRAFFIC and TRAVEL_SNAPSHOT
 * apart at T+1 only; the 660 census five ships in four zones apart). 0 stops the join (passes). */
static int (*join_admit_passes)(uint64_t tick);
static int join_admit(void) {
    if(!join_rx.loaded || join_rx.admitted) return 1;
    join_rx.admitted=1;join_jobinit_restore();join_bootev_restore();   /* Init and the boot event passes ran in the load (version 13) */
    InterlockedExchange(&join_parse_on,0);join_parse_restore();   /* the load is over: the parser native again */
    if(join_rx.created_failures || InterlockedCompareExchange64(&join_rx.mapped_saved,0,0)+InterlockedCompareExchange64(&join_rx.mapped_created,0,0)<
       InterlockedCompareExchange64(&join_rx.imported,0,0)/2) {
        if(join_rx.mode==JOIN_STRICT) return join_fail_rx(JOIN_WHY_MISMATCH,join_rx.created_failures?"created_entries":"ids_foreign");
    }
    double game=*(double *)(image+GAME_RVA),real=*(double *)(image+REAL_RVA),want=join_double(join_rx.t.gate.game),want_real=join_double(join_rx.t.gate.real);
    join_rx.clock_delta=game-want;
    if(game!=want || real!=want_real) {
        if(fabs(game-want)>1e-3 || !isfinite(real)) return join_fail_rx(JOIN_WHY_MISMATCH,"clock");
        *(double *)(image+GAME_RVA)=want;*(double *)(image+REAL_RVA)=want_real;join_rx.clock_fixed=1;
    }
    /* The joiner has no SAVE_AT: its baseline is now (J1 j1-19 refused patch:eb28d2 here): every patch is this boot's own modules'
     * (installed at its arming), the file pins check the build. A failed baseline refuses. */
    if(!join_pins_pristine() || !join_pins_baseline()) return join_fail_rx(JOIN_WHY_CAPTURE,"pins:baseline");
    {char why[48];if(!join_pins_check(why,sizeof(why))) return join_fail_rx(JOIN_WHY_CAPTURE,why);}
    join_engine e=join_engine_get();
    if(!e.director || !e.scheduler) return join_fail_rx(JOIN_WHY_CAPTURE,"engine");
    if(!join_admit_ai(&e) || !join_admit_md(&e)) return 0;
    if(join_admit_before_skip && !join_admit_before_skip()) return join_fail_rx(JOIN_WHY_CAPTURE,"before_skip");
    if(!join_admit_rid() || !join_admit_world()) return 0;
    join_admit_cid();join_admit_unpaused();join_admit_nmesh();
    join_admit_flight_generations();
    join_bootev_repost();   /* the events of T the boot passes kept (native_gate_join_hold_boot_events) */
    join_ai_watch_dump('j',join_rx.t.gate.tick);join_job_watch_dump('j');   /* diagnostic (native_gate_join_watch) */
    stats.issued=stats.completed=stats.report_tick=join_rx.t.gate.tick;stats.game=want;stats.real=want_real;previous_game=want;
    if(join_admit_passes && !join_admit_passes(join_rx.t.gate.tick)) return join_fail_rx(JOIN_WHY_CAPTURE,"passes");
    join_rx.based=1;join_rx.skip_dt=1;join_t1_arm(join_rx.t.gate.tick);join_watch_arm(join_rx.t.gate.tick);return 1;   /* FLIGHT listed what it wrote */
}
/* The first report after the admission is the loaded world at T: no dt to check against. */
static int join_skip_dt_once(void) {int skip=join_rx.skip_dt;join_rx.skip_dt=0;return skip;}
static uint64_t join_tick_base(void) {return join_rx.based?join_rx.t.gate.tick:0;}
static unsigned snapshot_join_view(unsigned index) {return join_rx.loaded && index<12?join_rx.t.gate.fixture[index]:0;}
/* Twelve view digits ('-' when all 0). */
static const char *join_views_text(const unsigned char views[12],char out[13]) {
    int any=0;for(unsigned i=0;i<12;i++) {out[i]=(char)('0'+views[i]%10u);any|=views[i]!=0;}
    out[12]=0;return any?out:"-";
}
static int join_status(char *out,size_t cap) {
    static const char *const states[]={"idle","armed","done","failed"};char pkg[65],port[33],rx_save[65];hex32(join_cap.package_sha,pkg);
    for(int i=0;i<16;i++) snprintf(port+2*i,3,"%02x",join_cap.portable[i]);
    hex32(join_rx.t.save_sha,rx_save);char fixture[13],rx_fixture[13],nav_fields[256],agent_fields[512];join_nav_fields_text(nav_fields,sizeof(nav_fields));
    join_agent_fields_text(agent_fields,sizeof(agent_fields));
    char ids[160];if(!join_ids_status(ids,sizeof(ids))) snprintf(ids,sizeof(ids),"?");
    int n=snprintf(out,cap,"state=%s tick=%llu k=%u mask=%u why=%u detail=%s save_bytes=%llu package_bytes=%llu save_us=%llu capture_us=%llu"
        " package=%s portable=%s modified_cleared=%u possible=%u joiner=%d mode=%d based=%d handover=%lld imported=%lld saved=%lld"
        " created=%lld unmapped=%lld created_failures=%lld ctx_missing=%llu ctx_extra=%llu ctx_ambiguous=%llu wake_erased=%llu"
        " wake_applied=%llu wake_missing=%llu vec_applied=%llu vec_missing=%llu mask_cleared=%llu md_applied=%llu md_missing=%llu md_extra=%llu"
        " md_ambiguous=%llu md_activation_fixed=%llu md_activation_far=%llu md_diff=%llu:%llu:%llu:%llu unpaired_rows=%u clock_fixed=%llu"
        " clock_delta=%.9g rid_skipped=%llu rx_tick=%llu rx_save_sha=%s rx_why=%u rx_detail=%s failures=%lld pins=%s patched=%u:%s armed_kept=%u"
        " fixture=%s rx_fixture=%s world_cap=%u/%u/%u/%u/%u/%u/%u/%u/%u/%u/%u/%u/%u/%u/%u/%u/%u/%u/%u/%u/%u/%u/%u/%u/%u/%u/%u/%u rx_version=%u health=%llu:%llu:%llu:%llu:%llu:%llu pose=%llu:%llu:%llu:%llu"
        " shield=%llu:%llu:%llu:%llu:%llu:%llu nav=%llu:%llu:%llu:%llu:%llu nav_fields=%s attn=%llu:%llu:%llu:%llu:%llu"
        " keys=%llu:%llu:%llu:%llu:%llu:%llu:%llu:%llu:%llu created_extra=%llu:%llu path=%llu:%llu:%llu:%llu:%llu:%llu:%llu"
        " region=%llu:%llu:%llu:%llu flight=%llu:%llu:%llu:%llu:%llu:%llu:%llu:%llu:%llu agent=%llu:%llu:%llu:%llu:%llu agent_fields=%s"
        " route=%llu:%llu:%llu:%llu:%llu:%llu:%llu param=%llu:%llu:%llu:%llu:%llu:%llu:%llu:%llu:%llu dock=%llu:%llu:%llu:%llu:%llu"
        " steer=%llu:%llu:%llu:%llu:%llu:%llu:%llu:%llu:%llu t1=%u:%llu:%llu:%llu:%llu:%llu:%llu"
        " solver=%llu:%llu:%llu:%llu:%llu:%llu bullet_walk=%u:%llu:%llu grav=%llu:%llu:%llu:%llu:%llu:%llu:%llu:%llu:%llu gravs=%llu:%llu:%llu:%llu md_dropped=%llu"
        " grav_cap=%u:%u:%u splm=%llu:%llu:%llu:%llu:%llu:%llu:%llu:%llu gravn=%llu:%llu:%llu:%llu solver_heap=%llu gravp=%llu:%llu:%llu:%llu:%llu solver_mapped=%u gravq=%llu:%llu:%llu:%llu:%llu:%llu gravq_cap=%u:%u:%u jobe=%llu:%llu:%016llx:%llu:%llu jobinit=%lld:%lld:%d:%u:%u bootev=%lld:%lld:%d:%d:%u:%u:%u jobe_cap=%u evq=%u:%u:%u:%u:%s evq_rx=%u:%u:%u:%u:%s evqt=%llu:%llu:%llu:%llu:%llu evqt_cap=%u gravd=%llu:%llu:%llu:%llu gravd_cap=%u gravp_restore=%llu:%llu:%llu:%u gravp_moved=%llu viewp=%llu:%llu:%llu viewp_cap=%u shoot=%llu:%llu:%llu:%llu shoot_cap=%u shoot_time=%llu:%llu shoot_late=%llu:%llu attk=%llu:%llu:%llu:%llu:%llu attk_cap=%u livef=%llu:%llu:%llu livef_cap=%u spec=%llu:%llu:%llu:%llu:%llu spec_added=%llu spec_cap=%u navc=%llu:%llu:%llu navc_cap=%u jobs=%llu:%llu:%llu:%llu:%llu jobs_cap=%u precise=%lld:%lld:%lld:%lld:%lld:%lld ids=%s gone=%u:%u:%u:%u body=%llu:%llu:%llu:%llu:%llu:%llu:%llu body_cap=%u bupd=%llu:%llu:%llu:%llu bupd_cap=%u ldc=%llu:%llu:%llu:%llu ldc_cap=%u navt=%llu:%llu:%llu navt_cap=%u cid=%llx:%llx unpaused=%llx:%llx nmesh=%d:%llu nmesh_cap=%u noct=%d:%llu:%llu:%llu:%llu:%llu noct_cap=%u agtt=%llu:%llu:%llu agtt_cap=%u agcb=%llu:%llu:%llu agcb_cap=%u pbnk=%llu:%llu:%llu:%llu pbnk_cap=%u pend=%d:%llu:%llu:%llu:%llu pend_cap=%u gravb=%llu:%llu:%llu:%llu gravb_cap=%u viewl=%d:%llu:%llu:%llu:%llu:%llu viewl_cap=%u ikv=%llu:%llu:%llu:%llu ikv_cap=%u ikv_refused=%s rcnt=%d:%llu:%llu:%llu:%llu rcnt_cap=%u ecos=%d:%llu:%llu:%llu ecos_cap=%u shpw=%d:%llu:%llu:%llu shpw_cap=%u trdn=%d:%llu:%llu:%llu trdn_cap=%u npca=%d:%llu:%llu:%llu:%llu:%llu:%llu npca_cap=%u jobg=%d:%llu:%llu:%llu jobg_cap=%u code=%d:%u:%x:%x:%x:%x code_cap=%u engr=%d:%llu:%llu:%llu engr_cap=%u shgr=%d:%llu:%llu:%llu shgr_cap=%u bnds=%d:%llu:%llu:%llu bnds_cap=%u omgs=%d:%llu:%llu:%llu omgs_cap=%u omgs_vec=%llu:%llu:%llu:%llu resv=%d:%llu:%llu:%llu:%llu resv_cap=%u pcsv=%d:%llu:%llu:%llu:%llu pcsv_cap=%u gravn_kept=%d:%llu jobw=%d:%llu viewk=%llu:%llu:%llu:%llu:%llu:%llu viewk_cap=%u:%u viewf=%llu:%llu:%llu:%llu:%llu viewf_cap=%u",
        states[join_cap.state],(unsigned long long)join_cap.tick,join_cap.slot_k,join_cap.mask,join_cap.why,join_cap.detail[0]?join_cap.detail:"-",
        (unsigned long long)join_cap.save_bytes,(unsigned long long)join_cap.package_bytes,(unsigned long long)join_cap.save_us,
        (unsigned long long)join_cap.capture_us,pkg,port,join_cap.modified_cleared,join_cap.possible,join_rx.loaded,join_rx.mode,join_rx.based,
        join_rx.handover==UINT64_MAX?-1LL:(long long)join_rx.handover,(long long)join_rx.imported,(long long)join_rx.mapped_saved,
        (long long)join_rx.mapped_created,(long long)join_rx.unmapped,(long long)join_rx.created_failures,
        (unsigned long long)join_rx.ctx_missing,(unsigned long long)join_rx.ctx_extra,(unsigned long long)join_rx.ctx_ambiguous,
        (unsigned long long)join_rx.wake_erased,(unsigned long long)join_rx.wake_applied,(unsigned long long)join_rx.wake_missing,
        (unsigned long long)join_rx.vec_applied,(unsigned long long)join_rx.vec_missing,(unsigned long long)join_rx.mask_cleared,
        (unsigned long long)join_rx.md_applied,
        (unsigned long long)join_rx.md_missing,(unsigned long long)join_rx.md_extra,(unsigned long long)join_rx.md_ambiguous,
        (unsigned long long)join_rx.md_activation_fixed,(unsigned long long)join_rx.md_activation_far,(unsigned long long)join_rx.md_diff[0],
        (unsigned long long)join_rx.md_diff[1],(unsigned long long)join_rx.md_diff[2],(unsigned long long)join_rx.md_diff[3],join_rx.nrows,
        (unsigned long long)join_rx.clock_fixed,join_rx.clock_delta,(unsigned long long)join_rx.rid_skipped,
        (unsigned long long)join_rx.t.gate.tick,join_rx.loaded?rx_save:"-",join_rx.why,
        join_rx.detail[0]?join_rx.detail:"-",(long long)join_failures,join_pins_state>0?"ok":join_pins_state<0?join_pins_why:"unchecked",join_patched,
        join_patched_text,join_armed_kept,join_views_text(join_cap.fixture,fixture),join_views_text(join_rx.t.gate.fixture,rx_fixture),join_cap.health,join_cap.pose,
        join_cap.shield,join_cap.nav,join_cap.attn,join_cap.keys,join_cap.path,join_cap.region,join_cap.flight,join_cap.spline,join_cap.agent,join_cap.route,
        join_cap.param,join_cap.dock,join_cap.steer,join_cap.steerpt,join_cap.solver,join_cap.bullet,join_cap.solver_over,join_cap.bullet_fault,join_cap.grav,join_cap.gravc,join_cap.gravs,join_cap.splm,join_cap.splme,join_cap.splmseg,join_cap.gravn,join_cap.gravp,
        join_rx.t.version,(unsigned long long)join_rx.health_applied,(unsigned long long)join_rx.health_hull_fixed,
        (unsigned long long)join_rx.health_shield_fixed,(unsigned long long)join_rx.health_missing,(unsigned long long)join_rx.health_shape,
        (unsigned long long)join_rx.health_conflict,(unsigned long long)join_rx.pose_applied,(unsigned long long)join_rx.pose_fixed,
        (unsigned long long)join_rx.pose_missing,(unsigned long long)join_rx.pose_shape,
        (unsigned long long)join_rx.shield_applied,(unsigned long long)join_rx.shield_fixed,(unsigned long long)join_rx.shield_mods,
        (unsigned long long)join_rx.shield_missing,(unsigned long long)join_rx.shield_shape,(unsigned long long)join_rx.shield_conflict,
        (unsigned long long)join_rx.nav_applied,(unsigned long long)join_rx.nav_fixed,(unsigned long long)join_rx.nav_missing,
        (unsigned long long)join_rx.nav_shape,(unsigned long long)join_rx.nav_unmapped,nav_fields,
        (unsigned long long)join_rx.attn_applied,(unsigned long long)join_rx.attn_level_fixed,(unsigned long long)join_rx.attn_due_fixed,
        (unsigned long long)join_rx.attn_missing,(unsigned long long)join_rx.attn_shape,
        (unsigned long long)join_rx.keys_applied[JOIN_KEYS_ATTENTION_ALL],(unsigned long long)join_rx.keys_applied[JOIN_KEYS_NAV_PROBE],
        (unsigned long long)join_rx.keys_applied[JOIN_KEYS_NAV_PARAMS],(unsigned long long)join_rx.keys_applied[JOIN_KEYS_ATTENTION_ACTOR],
        (unsigned long long)join_rx.keys_applied[JOIN_KEYS_NAV_ACTOR],(unsigned long long)join_rx.keys_known,(unsigned long long)join_rx.keys_unarmed,
        (unsigned long long)join_rx.keys_actor,(unsigned long long)join_rx.keys_full,
        (unsigned long long)join_rx.created_extra,(unsigned long long)join_rx.created_extra_loaded,
        (unsigned long long)join_rx.path_members,(unsigned long long)join_rx.path_pending,(unsigned long long)join_rx.path_missing,
        (unsigned long long)join_rx.path_local,(unsigned long long)join_rx.path_extra,(unsigned long long)join_rx.path_shape,
        (unsigned long long)join_rx.path_primed,(unsigned long long)join_rx.region_applied,(unsigned long long)join_rx.region_fixed,
        (unsigned long long)join_rx.region_missing,(unsigned long long)join_rx.region_shape,
        (unsigned long long)join_rx.flight_applied,(unsigned long long)join_rx.flight_fixed,(unsigned long long)join_rx.flight_spline,
        (unsigned long long)join_rx.flight_missing,(unsigned long long)join_rx.flight_kind,(unsigned long long)join_rx.flight_other,
        (unsigned long long)join_rx.flight_shape,(unsigned long long)join_rx.flight_alloc,(unsigned long long)join_rx.flight_word,
        (unsigned long long)join_rx.agent_applied,(unsigned long long)join_rx.agent_fixed,(unsigned long long)join_rx.agent_missing,
        (unsigned long long)join_rx.agent_shape,(unsigned long long)join_rx.agent_state,agent_fields,
        (unsigned long long)join_rx.route_applied,(unsigned long long)join_rx.route_rebuilt,(unsigned long long)join_rx.route_fixed,
        (unsigned long long)join_rx.route_missing,(unsigned long long)join_rx.route_other,(unsigned long long)join_rx.route_unmapped,
        (unsigned long long)join_rx.route_shape,
        (unsigned long long)join_rx.param_applied,(unsigned long long)join_rx.param_fixed,(unsigned long long)join_rx.param_missing,
        (unsigned long long)join_rx.param_shape,(unsigned long long)join_rx.param_unmapped,(unsigned long long)join_rx.param_read,
        (unsigned long long)join_rx.param_write,(unsigned long long)join_rx.param_own,(unsigned long long)join_rx.param_turn,
        (unsigned long long)join_rx.dock_applied,(unsigned long long)join_rx.dock_fixed,(unsigned long long)join_rx.dock_missing,
        (unsigned long long)join_rx.dock_kind,(unsigned long long)join_rx.dock_shape,
        (unsigned long long)join_rx.steer_applied,(unsigned long long)join_rx.steer_fixed,(unsigned long long)join_rx.steer_missing,
        (unsigned long long)join_rx.steer_kind,(unsigned long long)join_rx.steer_shape,(unsigned long long)join_rx.steer_unmapped,
        (unsigned long long)join_rx.steer_other,(unsigned long long)join_rx.steer_job,(unsigned long long)join_rx.steer_rebuilt,
        join_t1.listed,(unsigned long long)join_t1.probed,(unsigned long long)join_t1.stepped,(unsigned long long)join_t1.newleg,
        (unsigned long long)join_t1.pending,(unsigned long long)join_t1.retarget,(unsigned long long)join_t1.missing,
        (unsigned long long)join_rx.solver_applied,(unsigned long long)join_rx.solver_fixed,(unsigned long long)join_rx.solver_created,
        (unsigned long long)join_rx.solver_missing,(unsigned long long)join_rx.solver_shape,(unsigned long long)join_rx.solver_kind,
        join_cap.bullet_blocks,(unsigned long long)join_cap.bullet_live,(unsigned long long)join_cap.bullet_unkeyed,
        (unsigned long long)join_rx.grav_applied,(unsigned long long)join_rx.grav_fixed,(unsigned long long)join_rx.grav_missing,
        (unsigned long long)join_rx.grav_kind,(unsigned long long)join_rx.grav_shape,(unsigned long long)join_rx.grav_contacts,
        (unsigned long long)join_rx.grav_unmapped,(unsigned long long)join_rx.grav_buffers,(unsigned long long)join_rx.grav_created,
        (unsigned long long)join_rx.gravs_lists,
        (unsigned long long)join_rx.gravs_missing,(unsigned long long)join_rx.gravs_sectors,(unsigned long long)join_rx.gravs_cursor,(unsigned long long)join_rx.md_dropped,join_cap.grav_unmapped,join_cap.grav_fault,join_cap.grav_passive,
        (unsigned long long)join_rx.splm_applied,(unsigned long long)join_rx.splm_fixed,(unsigned long long)join_rx.splm_missing,
        (unsigned long long)join_rx.splm_kind,(unsigned long long)join_rx.splm_shape,(unsigned long long)join_rx.splm_other,
        (unsigned long long)join_rx.splm_alloc,(unsigned long long)join_rx.splm_entries,(unsigned long long)join_rx.gravn_groups,
        (unsigned long long)join_rx.gravn_fixed,(unsigned long long)join_rx.gravn_missing,(unsigned long long)join_rx.gravn_unfound,
        (unsigned long long)join_rx.solver_heap,(unsigned long long)join_rx.gravp_fixed,(unsigned long long)join_rx.gravp_equal,
        (unsigned long long)join_rx.gravp_missing,(unsigned long long)join_rx.gravp_absent,(unsigned long long)join_rx.gravp_extra,
        join_cap.solver_mapped,(unsigned long long)join_rx.gravq_posted,(unsigned long long)join_rx.gravq_unmapped,
        (unsigned long long)join_rx.gravq_other,(unsigned long long)join_rx.gravq_busy,(unsigned long long)join_rx.gravq_alloc,
        (unsigned long long)join_rx.gravq_had,join_cap.gravq,join_cap.gravq_global,join_cap.gravq_busy,
        (unsigned long long)join_rx.jobe_applied,(unsigned long long)join_rx.jobe_missing,(unsigned long long)join_rx.jobe_prev_last,
        (unsigned long long)join_rx.jobe_prev_len,(unsigned long long)join_rx.jobe_held,(long long)join_jobinit_calls,(long long)join_jobinit_empty,
        join_jobinit_installed,join_jobinit_jobs,join_jobinit_cleared,(long long)join_bootev_calls,(long long)join_bootev_held,join_bootev_on,
        join_bootev_installed[0]+join_bootev_installed[1],join_bootev_nclasses,join_bootev_n+join_bootev_reposted,join_bootev_reposted,join_cap.jobe,
        join_cap.evq_sources,join_cap.evq_events,join_cap.evq_due,join_cap.evq_fault,join_cap.evq_top[0]?join_cap.evq_top:"-",
        join_rx.evq_sources,join_rx.evq_events,join_rx.evq_due,join_rx.evq_fault,join_rx.evq_top[0]?join_rx.evq_top:"-",
        (unsigned long long)join_rx.evqt_removed,(unsigned long long)join_rx.evqt_posted,(unsigned long long)join_rx.evqt_other,
        (unsigned long long)join_rx.evqt_missing,(unsigned long long)join_rx.evqt_alloc,join_cap.evqt,
        (unsigned long long)join_rx.gravd_fixed,(unsigned long long)join_rx.gravd_equal,(unsigned long long)join_rx.gravd_missing,
        (unsigned long long)join_rx.gravd_unmapped,join_cap.gravd,(unsigned long long)join_gravp_restored,(unsigned long long)join_gravp_checked,
        (unsigned long long)join_gravp_gone,join_gravp_nset,(unsigned long long)join_gravp_moved,(unsigned long long)join_rx.viewp_written,(unsigned long long)join_rx.viewp_equal,
        (unsigned long long)join_rx.viewp_missing,join_cap.viewp,(unsigned long long)join_rx.shoot_written,(unsigned long long)join_rx.shoot_equal,
        (unsigned long long)join_rx.shoot_missing,(unsigned long long)join_rx.shoot_class,join_cap.shoot,
        (unsigned long long)join_rx.shoot_time,(unsigned long long)join_rx.shoot_far,(unsigned long long)join_rx.shoot_late[0],
        (unsigned long long)join_rx.shoot_late[1],(unsigned long long)join_rx.attk_written,(unsigned long long)join_rx.attk_equal,
        (unsigned long long)join_rx.attk_missing,(unsigned long long)join_rx.attk_conflict,(unsigned long long)join_rx.attk_far,join_cap.attk,
        (unsigned long long)join_rx.livef_written,(unsigned long long)join_rx.livef_equal,(unsigned long long)join_rx.livef_missing,join_cap.livef,
        (unsigned long long)join_rx.spec_written,(unsigned long long)join_rx.spec_equal,(unsigned long long)join_rx.spec_conflict,
        (unsigned long long)join_rx.spec_missing,(unsigned long long)join_rx.spec_extra,(unsigned long long)join_rx.spec_added,join_cap.spec,
        (unsigned long long)join_rx.navc_written,(unsigned long long)join_rx.navc_equal,(unsigned long long)join_rx.navc_missing,join_cap.navc,
        (unsigned long long)join_rx.jobs_written,(unsigned long long)join_rx.jobs_equal,(unsigned long long)join_rx.jobs_missing,
        (unsigned long long)join_rx.jobs_active_differ,(unsigned long long)join_rx.jobs_waiting_differ,join_cap.jobs,
        (long long)join_fmt_calls,(long long)(join_fmt_raised+join_fmtf_raised),(long long)join_parse_calls,(long long)join_parse_long,(long long)join_parse_exact,
        (long long)join_parse_far,ids,join_cap.gone,join_rx.t.ngone,world_hash_gone_added,world_hash_gone_dupes,
        (unsigned long long)join_body_written,(unsigned long long)join_body_equal,(unsigned long long)join_body_pose,(unsigned long long)join_body_motion,
        (unsigned long long)join_body_record,(unsigned long long)join_body_missing,(unsigned long long)join_body_shape,join_cap.body,
        (unsigned long long)join_bupd_written,(unsigned long long)join_bupd_equal,(unsigned long long)join_bupd_missing,(unsigned long long)join_bupd_shape,join_cap.bupd,
        (unsigned long long)join_ldc_written,(unsigned long long)join_ldc_equal,(unsigned long long)join_ldc_missing,(unsigned long long)join_ldc_shape,join_cap.ldc,
        (unsigned long long)join_navt_written,(unsigned long long)join_navt_equal,(unsigned long long)join_navt_missing,join_cap.navt,
        (unsigned long long)join_rx.t.gate.cid,(unsigned long long)join_cid_before,
        (unsigned long long)join_rx.t.gate.unpaused,(unsigned long long)join_unpaused_before,join_nmesh_adopted,(unsigned long long)nmesh_skipped,join_cap.nmesh,
        join_noct_adopted,(unsigned long long)join_noct_counts[0],(unsigned long long)join_noct_counts[1],(unsigned long long)join_noct_counts[2],
        (unsigned long long)join_noct_counts[3],(unsigned long long)join_noct_unmapped,join_cap.noct,
        (unsigned long long)join_agtt_written,(unsigned long long)join_agtt_equal,(unsigned long long)join_agtt_missing,join_cap.agtt,
        (unsigned long long)join_agcb_written,(unsigned long long)join_agcb_equal,(unsigned long long)join_agcb_missing,join_cap.agcb,
        (unsigned long long)join_pbnk_written,(unsigned long long)join_pbnk_equal,(unsigned long long)join_pbnk_missing,(unsigned long long)join_pbnk_unmapped,join_cap.pbnk,
        join_pend_on,(unsigned long long)join_pend_written,(unsigned long long)join_pend_equal,(unsigned long long)join_pend_unposed,(unsigned long long)join_pend_missing,join_cap.pend,
        (unsigned long long)join_gravb_written,(unsigned long long)join_gravb_equal,(unsigned long long)join_gravb_missing,(unsigned long long)join_gravb_unmapped,join_cap.gravb,
        join_viewl_on,(unsigned long long)join_viewl_entered,(unsigned long long)join_viewl_left,(unsigned long long)join_viewl_equal,(unsigned long long)join_viewl_other,(unsigned long long)join_viewl_missing,join_cap.viewl,
        (unsigned long long)join_ikv_written,(unsigned long long)join_ikv_equal,(unsigned long long)join_ikv_missing,(unsigned long long)join_ikv_shape,join_cap.ikv,join_ikv_refused,
        join_rcnt_on,(unsigned long long)join_rcnt_lowered,(unsigned long long)join_rcnt_equal,(unsigned long long)join_rcnt_kept,(unsigned long long)join_rcnt_missing,join_cap.rcnt,
        join_ecos_on,(unsigned long long)join_ecos_written,(unsigned long long)join_ecos_equal,(unsigned long long)join_ecos_missing,join_cap.ecos,
        join_shpw_on,(unsigned long long)join_shpw_written,(unsigned long long)join_shpw_equal,(unsigned long long)join_shpw_missing,join_cap.shpw,
        join_trdn_on,(unsigned long long)join_trdn_written,(unsigned long long)join_trdn_equal,(unsigned long long)join_trdn_missing,join_cap.trdn,
        join_npca_on,(unsigned long long)join_npca_started,(unsigned long long)join_npca_written,(unsigned long long)join_npca_equal,
        (unsigned long long)join_npca_missing,(unsigned long long)join_npca_failed,(unsigned long long)join_npca_index_differs,join_cap.npca,
        join_jobg_on,(unsigned long long)join_jobg_written,(unsigned long long)join_jobg_equal,(unsigned long long)join_jobg_missing,join_cap.jobg,
        join_code_on,join_code_written,join_code_session_counter,join_code_own_counter,join_code_session_seed,join_code_own_seed,join_cap.code,
        join_engr_on,(unsigned long long)join_engr_written,(unsigned long long)join_engr_equal,(unsigned long long)join_engr_missing,
        join_cap.engr,
        join_shgr_on,(unsigned long long)join_shgr_written,(unsigned long long)join_shgr_equal,(unsigned long long)join_shgr_missing,
        join_cap.shgr,
        join_bnds_on,(unsigned long long)join_bnds_written,(unsigned long long)join_bnds_equal,(unsigned long long)join_bnds_missing,
        join_cap.bnds,
        join_omgs_on,(unsigned long long)join_omgs_written,(unsigned long long)join_omgs_equal,(unsigned long long)join_omgs_missing,
        join_cap.omgs,
        (unsigned long long)join_omgs_vec_cleared,(unsigned long long)join_omgs_vec_equal,(unsigned long long)join_omgs_vec_kept,
        (unsigned long long)join_omgs_vec_differs,
        join_resv_on,(unsigned long long)join_resv_written,(unsigned long long)join_resv_equal,(unsigned long long)join_resv_missing,
        (unsigned long long)join_resv_extra,join_cap.resv,
        join_pcsv_on,(unsigned long long)join_pcsv_written,(unsigned long long)join_pcsv_equal,(unsigned long long)join_pcsv_missing,
        (unsigned long long)join_pcsv_shape,join_cap.pcsv,
        join_gravn_extra_on,(unsigned long long)join_gravn_kept,
        join_jobw_on,(unsigned long long)join_jobw_written,
        (unsigned long long)join_rx.viewk_children,(unsigned long long)join_rx.viewk_flags,(unsigned long long)join_rx.viewk_equal,
        (unsigned long long)join_rx.viewk_missing,(unsigned long long)join_rx.viewk_unmapped,(unsigned long long)join_rx.viewk_superset,
        join_cap.viewk,join_cap.viewkc,(unsigned long long)join_rx.viewf_written,(unsigned long long)join_rx.viewf_equal,
        (unsigned long long)join_rx.viewf_missing,(unsigned long long)join_rx.viewf_attention,(unsigned long long)join_rx.viewf_inflight,
        join_cap.viewf);
    return n>0 && (size_t)n<cap?n:0;
}
