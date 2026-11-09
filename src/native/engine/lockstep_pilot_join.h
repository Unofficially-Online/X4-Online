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
    return snprintf(out,capacity," join_park=%llu/%llu parks=%lld bodies=%x parked=%d adopted=%x",(unsigned long long)pjoin_park_tick,
                    (unsigned long long)pjoin_last_park,(long long)pjoin_parks,pfbody_mask,pfbody_parked,pjoin_adopted);
}

/* ---- the joiner's side (W3 R1, R2; reports/LOCKSTEP-JOIN-IN-WINDOW-20261104.txt section 8) ---- */
static int join_ids_replay(const uint64_t *ids,unsigned n);   /* lockstep_join_ids.h (the hot join's allocator) */
static unsigned join_ids_replay_n,join_ids_replay_done,join_ids_replay_other,join_ids_replay_wrong;   /* its counters (tentative) */
static char pjoin_why_text[192];
static uint64_t pjoin_replay_got(unsigned i);   /* lockstep_gate.c: join_ids_replay_got[i] of the last replay */
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
static int pjoin_parse_players(const char *text) {
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
__attribute__((unused)) static int pjoin_created_keys(unsigned k,unsigned n) {
    const pjoin_slot_rx *r=&pjoin_rx[k];
    if(!created_enabled()) return 1;
    int ok=1;AcquireSRWLockExclusive(&created_lock);
    for(unsigned i=0;ok && i<n && i<r->n;i++) {
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
static int pjoin_rebuild_slot(unsigned k) {
    const pjoin_slot_rx *r=&pjoin_rx[k];
    if(pslots[k].player) {pjoin_resume_why="rebuild_player_exists";return 0;}
    /* The loaded ship has its PlayerController already (w1s): the seat adopts it, so only the construct's IDs are replayed;
     * the session's take() allocation stays an unused slot here (set mode never hands a slot out twice). */
    unsigned char *ship=pilot_slots[k-1].resolved?pilot_slots[k-1].object:NULL;
    const int adopt=ship && pslot_controller_has(ship,PSLOT_CONTROLLER_CLASS,NULL)==1;
    const unsigned n=adopt?r->range[PJOIN_CONSTRUCT][1]-r->range[PJOIN_CONSTRUCT][0]:r->n;
    if(!n || n>r->n) {pjoin_resume_why="rebuild_count";return 0;}
    if(!join_ids_replay(r->ids,n)) {pjoin_resume_why="rebuild_ids_arm";return 0;}
    pslot_adopt=adopt;
    const int built=pslot_construct(k) && pslot_seat(k);
    pslot_adopt=0;
    const int placed=join_ids_replay_end();
    if(!built) {pjoin_resume_why=pslot_why;return 0;}
    if(!placed) {
        snprintf(pjoin_why_text,sizeof(pjoin_why_text),"rebuild_ids:slot%u:adopt%d:n%u:done%u:other%u:wrong%u:got%llx,%llx",k,adopt,
                 join_ids_replay_n,join_ids_replay_done,join_ids_replay_other,join_ids_replay_wrong,
                 (unsigned long long)pjoin_replay_got(0),(unsigned long long)pjoin_replay_got(1));
        pjoin_resume_why=pjoin_why_text;return 0;
    }
    if(adopt && !(pjoin_adopted>>(k-1)&1u)) {pjoin_resume_why="rebuild_adopt";return 0;}
    if(!pjoin_created_keys(k,n)) {pjoin_resume_why="rebuild_created";return 0;}
    return 1;
}

/* ---- the pilot tables a joiner needs at T+1 (W3 R3, R4; 'pilot_state') ----
 * Named blobs, each the raw bytes of a table of this build (both sides run the same build; the package's build pins say
 * so): "v1|<name>=<hex>|...". Ring entries only for apply ticks after T (captured before T, in rows the joiner's catch-up
 * permits do not carry). Pointers never travel: the slots' frames hold portable targets; a seat's store is X4's own control
 * banks (lockstep_virtual_players.h vpl_regions: values, the autopilot target compared live first). */
typedef struct {const char *name;void *data;size_t size;} pjoin_blob;
static unsigned char pjoin_seat_flags[PILOT_SLOTS+1][16];   /* authority, assist[2], agent_mode, no_highway, throttle, length */
static void pjoin_seat_pack(unsigned k) {
    unsigned char *f=pjoin_seat_flags[k];const pslot_slot *s=&pslots[k];memset(f,0,16);
    f[0]=s->authority;f[1]=s->assist[0];f[2]=s->assist[1];f[3]=s->agent_mode;f[4]=s->no_highway;
    memcpy(f+5,&s->throttle,4);memcpy(f+9,&s->length,4);
}
/* P_k's own fields that the resume's rebuild (vps_make) starts at their defaults (w1z8's dump at T+1): +0x6a08 (a flag at
 * +0x6a0c), +0x6e48 and +0x6e50 (two times; -1.0 on a new player). Values only, written into the rebuilt P_k after its seat. */
static const unsigned pjoin_pk_offsets[3]={0x6a08u,0x6e48u,0x6e50u};
static uint64_t pjoin_pk_fields[PILOT_SLOTS+1][3];
static void pjoin_pk_pack(void) {
    memset(pjoin_pk_fields,0,sizeof(pjoin_pk_fields));
    for(unsigned k=2;k<=PILOT_SLOTS;k++)
        for(unsigned i=0;pslots[k].player && i<3;i++) if(!read_local(pslots[k].player+pjoin_pk_offsets[i],&pjoin_pk_fields[k][i],8)) pjoin_pk_fields[k][i]=0;
}
static void pjoin_pk_unpack(unsigned k) {
    for(unsigned i=0;pslots[k].player && i<3;i++) memcpy(pslots[k].player+pjoin_pk_offsets[i],&pjoin_pk_fields[k][i],8);
}
/* The slots' PlayerControllers' own values that a load recreates from <movement class="player"> without (w1z12's dump at T+1,
 * equal on the session's two clients): the times +0xc0/+0xd0/+0xe8 and their flags +0xc8/+0xd8/+0xe0 (the load's game time on
 * a joiner), +0x58, +0x2b8, +0x2f8, +0x3a8, the three doubles +0x3b8/+0x3f0/+0x5c8 (1.035/1.325/1.365 on a joiner), the cached
 * transform rows +0x220..+0x278 and the double positions +0x448..+0x4a0, +0x550..+0x578, +0x5f0..+0x618 (text precision on a
 * joiner). Not the listener ID +0x18 (the runtime ID registry owns it) nor the counters and indices (+0x670, +0x718, +0x788).
 * Values only, slot 1's (X4's own controller) and each seated slot k's, written at the resume once the controllers exist.
 * w1z20 (the hot join's watch on the slot ships' controllers, the session at T against the joiner before the resume) adds the
 * current transform (+0x60 the position, +0x70/+0x80/+0x90 the rows) and its copy +0x100..+0x13f: the joiner holds the load's
 * (exact rows where the session's carry 2^-24 residues, slot 2's position a step behind), and step T+1 moves them into the
 * previous rows +0x220.. that this blob writes, so at T+1 those were the load's again (w1z18: +0x250..+0x268 apart; 176c95, 580 m
 * from slot 1's axis-aligned ship, steered to the other side). Also the last update's time +0xf0 (slot 2: T on the session, -1
 * on the joiner), +0x1c0/+0x1c8, and +0x3e8 beside the elapsed times +0x3f0/+0x5c8, which step T+1 set back to one step's
 * dt on the joiner (w1z18: 0.05 against 31.70 and 16.95). */
static const unsigned short pjoin_pctl_offsets[]={0x58,0x60,0x68,0x70,0x78,0x80,0x88,0x90,0x98,0xc0,0xc8,0xd0,0xd8,0xe0,0xe8,0xf0,
    0x100,0x108,0x110,0x118,0x120,0x128,0x130,0x138,0x1c0,0x1c8,0x220,0x228,0x230,0x238,0x240,0x248,0x250,0x258,0x260,
    0x268,0x270,0x278,0x2b8,0x2f8,0x320,0x328,0x330,0x338,0x340,0x348,0x350,0x358,0x3a8,0x3b8,0x3c0,0x3e8,0x3f0,0x3f8,0x400,0x408,
    0x410,0x418,0x420,0x428,0x430,0x438,0x440,0x448,0x450,0x458,0x460,0x468,0x470,0x478,0x480,0x488,0x490,0x498,0x4a0,0x4d8,0x4e0,
    0x530,0x538,0x540,0x548,0x550,0x558,0x560,0x568,0x570,0x578,0x5c8,0x5d0,0x5d8,0x5e0,0x5e8,0x5f0,0x5f8,0x600,0x608,0x610,0x618,
    0x6f8,0x700,0x708,0x710};
/* +0x438, +0x540, +0x5e0: -0.0 on the session, +0.0 after a load (w1z21). w1z22 (the resume's snapshots against the session's
 * controller at T): a third transform copy +0x320..+0x35f; three double quaternions +0x428..+0x440, +0x530..+0x548,
 * +0x5d0..+0x5e8 (the session's with their history's residues, (0,0,0,1) after a load: the source of the rows' 2^-24); the
 * step's dt +0x3c0; +0x3f8..+0x408; the vector +0x410..+0x420; +0x4d8/+0x4e0; the four factors +0x6f8..+0x710 (0.8, 0.8,
 * 0.2, 0.8 on the session, -1 after a load). Not the container +0x2b0..+0x318 (slot 2: four heap entries on the session,
 * empty after a load) nor the counters +0x670, +0x718, +0x788. */
#define PJOIN_PCTL_WORDS (sizeof(pjoin_pctl_offsets)/sizeof(*pjoin_pctl_offsets))
static uint64_t pjoin_pctl[PILOT_SLOTS+1][PJOIN_PCTL_WORDS+1];   /* [k][0]: 1 when carried */
static unsigned char *pjoin_pctl_of(unsigned k) {return k==1?pnat_controller:(k<=PILOT_SLOTS?pslots[k].controller:NULL);}
static void pjoin_pctl_pack(void) {
    memset(pjoin_pctl,0,sizeof(pjoin_pctl));
    for(unsigned k=1;k<=PILOT_SLOTS;k++) {
        unsigned char *c=pjoin_pctl_of(k);int ok=c!=NULL;
        for(unsigned i=0;ok && i<PJOIN_PCTL_WORDS;i++) ok=read_local(c+pjoin_pctl_offsets[i],&pjoin_pctl[k][i+1],8);
        if(ok) pjoin_pctl[k][0]=1;else memset(pjoin_pctl[k],0,sizeof(pjoin_pctl[k]));
    }
}
/* Diagnostic (gate.pilot_dump which 5 and 6, native_gate_pilot_dump_tick): each slot controller's first PJOIN_SNAP bytes just
 * before and just after the resume writes this blob, to set against the session's controller at T. */
#define PJOIN_SNAP 0x800u
static unsigned char pjoin_snap[2][PILOT_SLOTS+1][PJOIN_SNAP];
static int pjoin_snap_ok[2][PILOT_SLOTS+1];
/* w1z23: slot 1's controller (X4's own, loaded with its containers) equals the session's after step T+1 with every word above; a
 * slot k's (C_k, made by the resume's rebuild with empty containers +0x2b0..+0x318) runs a first update in step T+1 that builds
 * them, and with the words w1z22 added its transforms came out 4 km from the session's (w1z21, without them: ULPs). Those words
 * go to slot 1 only. */
/* w1z62/w1z63 (in-window 40): the window's fixture race starts from slot 2's ship 0x7a8fe a few ULPs off from T+1 (its cached
 * transform and pathing state); the fixture ships' avoidance sees it. Of the slot-1-only words above, the three double
 * quaternions (+0x428..+0x447, +0x530..+0x54f, +0x5d0..+0x5ef: the session's with their history's residues, (0,0,0,1) after
 * a load, the source of the cached rows' 2^-24) are the ULP candidates; w1z22's 4 km came with all the words together.
 * native_gate_join_slotk_quats lets those quaternion words through for slot k as well. */
static int pjoin_pctl_quat(unsigned short at) {return (at>=0x428 && at<0x448) || (at>=0x530 && at<0x550) || (at>=0x5d0 && at<0x5f0);}
/* native_gate_join_slotk_words = mask (experiment): the slot-1-only word groups slot k takes as well. w1z64 (the quaternions): slot
 * 2's rotation equal after T+1, its position and velocity still ULPs apart. Bits: 1 the three double quaternions, 2 the step's
 * dt +0x3c0, 4 +0x3f8..+0x420, 8 +0x4d8/+0x4e0, 16 the factors +0x6f8..+0x710, 32 the transform copy +0x320..+0x358. */
static unsigned pjoin_pctl_group(unsigned short at) {
    if(pjoin_pctl_quat(at)) return 1u;
    if(at==0x3c0) return 2u;
    if(at>=0x3f8 && at<=0x420) return 4u;
    if(at==0x4d8 || at==0x4e0) return 8u;
    if(at>=0x6f8 && at<=0x710) return 16u;
    if(at>=0x320 && at<=0x358) return 32u;
    return 0u;
}
static int pjoin_pctl_slot1_only(unsigned short at) {
    if(pjoin_slotk_quats && pjoin_pctl_quat(at)) return 0;
    if(pjoin_slotk_mask & pjoin_pctl_group(at)) return 0;
    return (at>=0x320 && at<=0x358) || at==0x3c0 || (at>=0x3f8 && at<=0x430) || at==0x440 || at==0x4d8 || at==0x4e0 ||
           at==0x530 || at==0x538 || at==0x548 || at==0x5d0 || at==0x5d8 || at==0x5e8 || (at>=0x6f8 && at<=0x710);
}
static void pjoin_pctl_unpack(unsigned k) {
    unsigned char *c=pjoin_pctl_of(k);
    if(!c || !pjoin_pctl[k][0]) return;
    if(k<=PILOT_SLOTS) pjoin_snap_ok[0][k]=read_local(c,pjoin_snap[0][k],PJOIN_SNAP);
    for(unsigned i=0;i<PJOIN_PCTL_WORDS;i++)
        if(k==1 || !pjoin_pctl_slot1_only(pjoin_pctl_offsets[i])) memcpy(c+pjoin_pctl_offsets[i],&pjoin_pctl[k][i+1],8);
    if(k<=PILOT_SLOTS) pjoin_snap_ok[1][k]=read_local(c,pjoin_snap[1][k],PJOIN_SNAP);
}
static void pjoin_seat_unpack(unsigned k) {
    const unsigned char *f=pjoin_seat_flags[k];pslot_slot *s=&pslots[k];
    s->authority=f[0];s->assist[0]=f[1];s->assist[1]=f[2];s->agent_mode=f[3];s->no_highway=f[4];
    memcpy(&s->throttle,f+5,4);memcpy(&s->length,f+9,4);
}
static uint64_t pjoin_stock_scalars[16];
static void pjoin_stock_pack(void) {
    uint64_t *v=pjoin_stock_scalars;memset(pjoin_stock_scalars,0,sizeof(pjoin_stock_scalars));
    v[0]=pstk_flight;v[1]=pstk_flight_known;v[2]=pstk_cursor_tick;v[3]=pstk_frame_tick;v[4]=pstk_frame_hash;v[5]=pstk_aim_tick;
    v[6]=(uint64_t)pstk_aim_steer_replay|((uint64_t)(pstk_aim_valid_replay!=0)<<8);v[7]=pstk_capture_start;v[8]=pstk_first_apply;
}
static void pjoin_stock_unpack(void) {
    const uint64_t *v=pjoin_stock_scalars;
    pstk_flight=(unsigned)v[0];pstk_flight_known=(unsigned)v[1];pstk_cursor_tick=v[2];pstk_frame_tick=v[3];pstk_frame_hash=v[4];
    pstk_aim_tick=v[5];pstk_aim_steer_replay=(unsigned char)v[6];pstk_aim_valid_replay=(int)((v[6]>>8)&1u);
    pstk_capture_start=v[7];pstk_first_apply=v[8];
}
static pstk_staged pjoin_pstk_ring[PILOT_SLOTS][PSTK_RING];
static pilot_staged pjoin_pilot_ring[PILOT_RING];
/* The blobs, in a fixed order. The rings are copies filtered to apply ticks after T (pjoin_rings_pack). */
static pjoin_blob pjoin_blobs[]={
    {"stock",pjoin_stock_scalars,sizeof(pjoin_stock_scalars)},
    {"frames",pstk_slot_frames,sizeof(pstk_slot_frames)},
    {"held_states",pstk_held_states,sizeof(pstk_held_states)},{"held_ranges",pstk_held_ranges,sizeof(pstk_held_ranges)},
    {"fire_tick",pstk_fire_tick,sizeof(pstk_fire_tick)},{"fire_on",pstk_fire_on,sizeof(pstk_fire_on)},{"fire2_on",pstk_fire2_on,sizeof(pstk_fire2_on)},
    {"aim",pstk_aim_replay,sizeof(pstk_aim_replay)},{"cursor",pstk_cursor_replay,sizeof(pstk_cursor_replay)},
    {"view_state",(void*)pview_state,sizeof(pview_state)},{"view_target",pview_target_replay,sizeof(pview_target_replay)},
    {"view_tick",pview_target_tick,sizeof(pview_target_tick)},
    {"seat_flags",pjoin_seat_flags,sizeof(pjoin_seat_flags)},{"pk_fields",pjoin_pk_fields,sizeof(pjoin_pk_fields)},
    {"pctl",pjoin_pctl,sizeof(pjoin_pctl)}};
#define PJOIN_BLOBS (sizeof(pjoin_blobs)/sizeof(*pjoin_blobs))
static void pjoin_rings_pack(uint64_t t) {
    memset(pjoin_pstk_ring,0,sizeof(pjoin_pstk_ring));memset(pjoin_pilot_ring,0,sizeof(pjoin_pilot_ring));
    for(unsigned k=0;k<PILOT_SLOTS;k++) for(unsigned i=0;i<PSTK_RING;i++)
        if(pstk_ring[k][i].present && pstk_ring[k][i].tick>t) pjoin_pstk_ring[k][i]=pstk_ring[k][i];
    for(unsigned i=0;i<PILOT_RING;i++) if(pilot_ring[i].present && pilot_ring[i].tick>t) pjoin_pilot_ring[i]=pilot_ring[i];
}
/* Every byte as two hex digits; -1 when it does not fit. */
static int pjoin_hex_raw(char *out,size_t capacity,int n,const unsigned char *data,size_t size) {
    static const char hex[]="0123456789abcdef";
    if(n<0 || (size_t)n+2*size>=capacity) return -1;
    for(size_t i=0;i<size;i++) {out[n++]=hex[data[i]>>4];out[n++]=hex[data[i]&15];}
    out[n]=0;return n;
}
/* Blob bytes that are all zero are written as "<name>=0" (most rings and frames are sparse). */
static int pjoin_hex_put(char *out,size_t capacity,int n,const unsigned char *data,size_t size) {
    size_t used=0;while(used<size && !data[used]) used++;
    if(used==size) return n+snprintf(out+n,capacity-(size_t)n,"0");
    return pjoin_hex_raw(out,capacity,n,data,size);
}
/* lockstep_pilot_join_modules.h (R5, after lockstep_ui_replay.h): the per-slot modules' state at T. */
static int pjoin_m5_put(char *out,size_t capacity,int n,uint64_t t);
static void pjoin_m5_reset(void);
static int pjoin_m5_get(const char *name,size_t length,const char *value,size_t chars);
static int pjoin_m5_complete(uint64_t t);
static int pjoin_m5_check(void);
static void pjoin_m5_apply(void);
/* A seat's store holds X4's control banks (pslot_store_init: the image-base regions of PSLOT_CLASSES, in table order); a region
 * marked pointer (environment_zone: the zone d95180 compares) is a process address, which travels as the zone's portable ID
 * (w1v: the session's raw pointer crashed the joiner's d9a478). to_portable 1: pointer -> portable (the saver); 0: back to the
 * joiner's object (its find). 0 on a pointer without a portable ID or a portable ID without an object here. */
static int pjoin_store_pointers(unsigned char *store,unsigned length,int to_portable) {
    unsigned n=0;
    for(unsigned i=0;i<VPL_REGION_COUNT;i++) {
        const vpl_region *r=&vpl_regions[i];
        if(r->base!=VPL_BASE_IMAGE || !(PSLOT_CLASSES>>r->cls&1u)) continue;
        if(n+r->size>length) return 0;
        if(r->pointer && r->size==8) {
            uint64_t v=0;memcpy(&v,store+n,8);
            if(v) {
                if(to_portable) {
                    uint64_t runtime=0;
                    if(!read_local((unsigned char*)(uintptr_t)v+8,&runtime,8) || !(v=world_identity_portable(runtime))) return 0;
                } else {
                    const uint64_t runtime=(v&CREATED_BIT)?created_runtime_of(v):pilot_reverse(v);
                    void *object=runtime && pdeath_find?pdeath_find(pdeath_map,runtime,4):NULL;
                    if(!object) return 0;
                    v=(uint64_t)(uintptr_t)object;
                }
            }
            memcpy(store+n,&v,8);
        }
        n+=r->size;
    }
    return n==length;
}
/* gate.pilot_join_state() on a saver in frame T: every blob (the stores per seated slot after them). 0 when it does not fit. */
static int pjoin_tables_state(char *out,size_t capacity,uint64_t t) {
    pjoin_stock_pack();pjoin_rings_pack(t);
    for(unsigned k=2;k<=PILOT_SLOTS;k++) pjoin_seat_pack(k);
    pjoin_pk_pack();pjoin_pctl_pack();
    int n=snprintf(out,capacity,"v1/t%llx",(unsigned long long)t);
    for(unsigned b=0;n>0 && (size_t)n<capacity && b<PJOIN_BLOBS;b++) {
        n+=snprintf(out+n,capacity-(size_t)n,"|%s=",pjoin_blobs[b].name);
        if(n<=0 || (size_t)n>=capacity || (n=pjoin_hex_put(out,capacity,n,pjoin_blobs[b].data,pjoin_blobs[b].size))<0) return 0;
    }
    /* The rings sparsely: one entry per present index (srK_I for slot K's stock ring, prI for the pilot ring). */
    for(unsigned k=0;n>0 && (size_t)n<capacity && k<PILOT_SLOTS;k++) for(unsigned i=0;n>0 && (size_t)n<capacity && i<PSTK_RING;i++) {
        if(!pjoin_pstk_ring[k][i].present) continue;
        n+=snprintf(out+n,capacity-(size_t)n,"|sr%u_%u=",k,i);
        if(n<=0 || (size_t)n>=capacity || (n=pjoin_hex_put(out,capacity,n,(const unsigned char*)&pjoin_pstk_ring[k][i],sizeof(pstk_staged)))<0) return 0;
    }
    for(unsigned i=0;n>0 && (size_t)n<capacity && i<PILOT_RING;i++) {
        if(!pjoin_pilot_ring[i].present) continue;
        n+=snprintf(out+n,capacity-(size_t)n,"|pr%u=",i);
        if(n<=0 || (size_t)n>=capacity || (n=pjoin_hex_put(out,capacity,n,(const unsigned char*)&pjoin_pilot_ring[i],sizeof(pilot_staged)))<0) return 0;
    }
    for(unsigned k=2;n>0 && (size_t)n<capacity && k<=PILOT_SLOTS;k++) {
        if(!(pslot_mask>>(k-1)&1u) || !pslots[k].player || !pslots[k].length || pslots[k].length>VPL_BLOCK_MAX) continue;
        static unsigned char portable[VPL_BLOCK_MAX];memcpy(portable,pslots[k].store,pslots[k].length);
        if(!pjoin_store_pointers(portable,pslots[k].length,1)) return 0;
        n+=snprintf(out+n,capacity-(size_t)n,"|store%u=",k);
        if(n<=0 || (size_t)n>=capacity || (n=pjoin_hex_put(out,capacity,n,portable,pslots[k].length))<0) return 0;
    }
    for(unsigned k=2;pslot_first_stores && n>0 && (size_t)n<capacity && k<=PILOT_SLOTS;k++) {   /* native_gate_pilot_first_stores */
        if(!(pslot_mask>>(k-1)&1u) || !pslots[k].first_length || pslots[k].first_length>VPL_BLOCK_MAX) continue;
        static unsigned char portable[VPL_BLOCK_MAX];memcpy(portable,pslots[k].first,pslots[k].first_length);
        if(!pjoin_store_pointers(portable,pslots[k].first_length,1)) return 0;
        n+=snprintf(out+n,capacity-(size_t)n,"|first%u=",k);
        if(n<=0 || (size_t)n>=capacity || (n=pjoin_hex_put(out,capacity,n,portable,pslots[k].first_length))<0) return 0;
    }
    if(n>0 && (size_t)n<capacity && (n=pjoin_m5_put(out,capacity,n,t))<0) return 0;   /* R5 */
    return n>0 && (size_t)n<capacity?n:0;
}
__attribute__((unused)) static int pjoin_hex_get(const char *hex,size_t chars,unsigned char *data,size_t size) {
    if(chars==1 && hex[0]=='0') {memset(data,0,size);return 1;}
    if(chars!=2*size) return 0;
    for(size_t i=0;i<size;i++) {
        unsigned v=0;
        for(int j=0;j<2;j++) {char c=hex[2*i+j];v<<=4;if(c>='0'&&c<='9') v|=(unsigned)(c-'0');else if(c>='a'&&c<='f') v|=(unsigned)(c-'a'+10);else return 0;}
        data[i]=(unsigned char)v;
    }
    return 1;
}
/* A joiner (R3, R4): the text back into staging; applied by pjoin_tables_apply once the seats exist. 1 parsed, every blob
 * present once with its exact size; 0 refused (pjoin_resume_why). */
static uint64_t pjoin_tables_t;
static unsigned char pjoin_store_rx[PILOT_SLOTS+1][VPL_BLOCK_MAX],pjoin_first_rx[PILOT_SLOTS+1][VPL_BLOCK_MAX];
static unsigned pjoin_store_rx_len[PILOT_SLOTS+1],pjoin_first_rx_len[PILOT_SLOTS+1];
static int pjoin_tables_parse(const char *text) {
    unsigned long long t=0;int used=0,r5=0;unsigned seen=0;
    memset(pjoin_store_rx_len,0,sizeof(pjoin_store_rx_len));memset(pjoin_first_rx_len,0,sizeof(pjoin_first_rx_len));
    memset(pjoin_pstk_ring,0,sizeof(pjoin_pstk_ring));memset(pjoin_pilot_ring,0,sizeof(pjoin_pilot_ring));
    pjoin_m5_reset();
    if(!text || sscanf(text,"v1/t%llx%n",&t,&used)!=1) {pjoin_resume_why="state_head";return 0;}
    text+=used;
    while(*text=='|') {
        const char *name=++text,*eq=strchr(text,'=');
        if(!eq) {pjoin_resume_why="state_entry";return 0;}
        const char *value=eq+1,*end=strchr(value,'|');if(!end) end=value+strlen(value);
        unsigned b=0;
        for(;b<PJOIN_BLOBS;b++) if((size_t)(eq-name)==strlen(pjoin_blobs[b].name) && !memcmp(name,pjoin_blobs[b].name,(size_t)(eq-name))) break;
        if(b<PJOIN_BLOBS) {
            if(seen>>b&1u || !pjoin_hex_get(value,(size_t)(end-value),pjoin_blobs[b].data,pjoin_blobs[b].size)) {pjoin_resume_why="state_blob";return 0;}
            seen|=1u<<b;
        } else if(name[0]=='s' && name[1]=='r') {
            unsigned k=0,i=0;
            if(sscanf(name,"sr%u_%u=",&k,&i)!=2 || k>=PILOT_SLOTS || i>=PSTK_RING || pjoin_pstk_ring[k][i].present ||
               !pjoin_hex_get(value,(size_t)(end-value),(unsigned char*)&pjoin_pstk_ring[k][i],sizeof(pstk_staged)) ||
               !pjoin_pstk_ring[k][i].present) {pjoin_resume_why="state_ring";return 0;}
        } else if(name[0]=='p' && name[1]=='r') {
            unsigned i=0;
            if(sscanf(name,"pr%u=",&i)!=1 || i>=PILOT_RING || pjoin_pilot_ring[i].present ||
               !pjoin_hex_get(value,(size_t)(end-value),(unsigned char*)&pjoin_pilot_ring[i],sizeof(pilot_staged)) ||
               !pjoin_pilot_ring[i].present) {pjoin_resume_why="state_ring";return 0;}
        } else if((r5=pjoin_m5_get(name,(size_t)(eq-name),value,(size_t)(end-value)))>=0) {
            if(!r5) return 0;
        } else if(!strncmp(name,"first",5)) {   /* native_gate_pilot_first_stores: a slot's first store */
            unsigned k=0;
            if(sscanf(name,"first%u=",&k)!=1 || k<2 || k>PILOT_SLOTS || pjoin_first_rx_len[k] || (end-value)%2 || (size_t)(end-value)/2>VPL_BLOCK_MAX ||
               !(end-value) || !pjoin_hex_get(value,(size_t)(end-value),pjoin_first_rx[k],(size_t)(end-value)/2)) {pjoin_resume_why="state_first";return 0;}
            pjoin_first_rx_len[k]=(unsigned)(end-value)/2;
        } else {
            unsigned k=0;
            if(sscanf(name,"store%u=",&k)!=1 || k<2 || k>PILOT_SLOTS || pjoin_store_rx_len[k] || (end-value)%2 || (size_t)(end-value)/2>VPL_BLOCK_MAX ||
               !(end-value) || !pjoin_hex_get(value,(size_t)(end-value),pjoin_store_rx[k],(size_t)(end-value)/2)) {pjoin_resume_why="state_store";return 0;}
            pjoin_store_rx_len[k]=(unsigned)(end-value)/2;
        }
        text=end;
    }
    if(*text || seen!=(1u<<PJOIN_BLOBS)-1u) {pjoin_resume_why="state_missing";return 0;}
    if(!pjoin_m5_complete(t)) return 0;
    pjoin_tables_t=t;return 1;
}

/* ---- the resume (W3 R1-R5; diagnostic: native_gate_join_in_window) ----
 * A joiner's first pass at T+1 inside a running window, after the slots' resolve and before the per-slot passes: the
 * package's pilot_players and pilot_state, R5's checks (lockstep_pilot_join_modules.h: what is not carried yet refuses
 * before anything changes), the slots' players and seats rebuilt with the session's IDs (R1) and their stores and flags,
 * the slots' hook (its update of frame T is in the save: the check's last tick is T), the body guard parked as the
 * session's after its park at T (R2), the native layer installed and the stock path's state (R3), the ring entries in
 * flight (R4), the per-slot modules' state (R5: death records, leased guns, the UI replay's staged sections and order
 * aliases, the hit claims in flight). Not covered yet (R6): the window's Lua state; a joiner runs on with it as its own
 * arming left it, which is why this is a diagnostic. */
static char pjoin_state_text[1u<<18];
static void join_code_hold(int begin);   /* lockstep_join_world.h: the code counter held over the resume (CODE) */
static int pjoin_resume(uint64_t tick,unsigned char *controller) {
    char players[4096];const uint64_t base=join_tick_base();
    if(!join_package_line("pilot_players",players,sizeof(players)) || !pjoin_parse_players(players)) {
        if(!strcmp(pjoin_resume_why,"-")) pjoin_resume_why="no_players";return 0;
    }
    if(!join_package_line("pilot_state",pjoin_state_text,sizeof(pjoin_state_text)) || !pjoin_tables_parse(pjoin_state_text)) {
        if(!strcmp(pjoin_resume_why,"-")) pjoin_resume_why="no_state";return 0;
    }
    if(pjoin_tables_t!=base || tick!=base+1) {pjoin_resume_why="state_tick";return 0;}
    if(pjoin_rx_slots!=pslot_mask) {pjoin_resume_why="slot_mask";return 0;}
    if(!pjoin_m5_check()) return 0;   /* R5's refusals, before anything changes */
    join_code_hold(1);   /* the session made what follows before T: its codes are in the counter already */
    /* R1 */
    for(unsigned k=2;k<=PILOT_SLOTS;k++) {
        if(!(pjoin_rx_mask>>(k-1)&1u)) continue;
        if(!pjoin_rebuild_slot(k)) return 0;
        if(pjoin_store_rx_len[k]) {
            if(pjoin_store_rx_len[k]>VPL_BLOCK_MAX) {pjoin_resume_why="store_size";return 0;}
            memcpy(pslots[k].store,pjoin_store_rx[k],pjoin_store_rx_len[k]);
            if(!pjoin_store_pointers(pslots[k].store,pjoin_store_rx_len[k],0)) {pjoin_resume_why="store_pointer";return 0;}
        }
        pjoin_seat_unpack(k);pjoin_pk_unpack(k);pjoin_pctl_unpack(k);
        if(pslots[k].length!=pjoin_store_rx_len[k]) {pjoin_resume_why="store_length";return 0;}
    }
    /* The session's first stores (native_gate_pilot_first_stores), absent slots' too: a late seat or a respawn here starts
     * from the block every session client took, not from this client's canonical one. */
    for(unsigned k=2;k<=PILOT_SLOTS;k++) {
        if(!pjoin_first_rx_len[k]) continue;
        if(!(pslot_mask>>(k-1)&1u)) {pjoin_resume_why="first_slot";return 0;}
        memcpy(pslots[k].first,pjoin_first_rx[k],pjoin_first_rx_len[k]);
        if(!pjoin_store_pointers(pslots[k].first,pjoin_first_rx_len[k],0)) {pjoin_resume_why="first_pointer";return 0;}
        pslots[k].first_length=pjoin_first_rx_len[k];
    }
    /* The slots' frame hook as the session's take made it: pslot_seat_all installs it for every armed session, seated
     * slots or not, so a joiner whose session had no other present slot (pjoin_rx_mask 0, hj1) needs it too (hook_lost). */
    if(pslot_mask && !pslot_p1) {   /* no slot rebuilt: the canonical player from the singleton, as take-2 latches it with every
                                    * slot absent (pslot_construct_all; hj1: singleton_changed at the first check) */
        unsigned char *universe=NULL,*p1=NULL;
        if(!read_local(pnat_base+VPL_UNIVERSE,&universe,8) || !universe || !read_local(universe+VPS_SINGLETON,&p1,8) || !p1)
            {pjoin_resume_why="p1_unknown";return 0;}
        pslot_p1=p1;
    }
    if(pslot_mask && !pslot_hook_install()) {pjoin_resume_why="slots_hook";return 0;}
    pslot_update_tick=base;pslot_stage="live";
    /* R2 */
    if(pfbody_armed) {
        /* The body kill stub lives on the slots' near page, which R1's rebuild makes. A session whose only present slot is 1
         * rebuilds none (pjoin_rx_mask 0: a 2-slot x4ls play session joined in its open slot 2, hj1), so make it here. */
        if(!pslot_page && !(pslot_page=pslot_alloc_page())) {pjoin_resume_why="slot_page";return 0;}
        if(!pfbody_kill_install()) {pjoin_resume_why="body_kill_install";return 0;}
        pfbody_mask=0;pfbody_parked=1;pfbody_seated_since=0;memset(pfbody_back,0,sizeof(pfbody_back));pfbody_stage="parked";
    }
    /* R3 */
    if(pnat_armed && !pnat_install(controller)) {pjoin_resume_why="native_install";return 0;}
    pjoin_stock_unpack();
    pjoin_pctl_unpack(1);   /* X4's own controller, now pnat_controller */
    pnat_stage="live";
    /* R4: the carried ring entries where the joiner's own catch-up has not staged the same tick yet */
    for(unsigned k=0;k<PILOT_SLOTS;k++) for(unsigned i=0;i<PSTK_RING;i++)
        if(pjoin_pstk_ring[k][i].present && !(pstk_ring[k][i].present && pstk_ring[k][i].tick==pjoin_pstk_ring[k][i].tick)) pstk_ring[k][i]=pjoin_pstk_ring[k][i];
    for(unsigned i=0;i<PILOT_RING;i++)
        if(pjoin_pilot_ring[i].present && !(pilot_ring[i].present && pilot_ring[i].tick==pjoin_pilot_ring[i].tick)) pilot_ring[i]=pjoin_pilot_ring[i];
    /* R5 */
    pjoin_m5_apply();
    join_code_hold(0);
    pjoin_resume_why="resumed";
    return 1;
}

