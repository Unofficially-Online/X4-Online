/* Gun aim per player (native_gate_pilot_aim; a session default with native_gate_pilot_fire = 'record' and stock slots).
 * Report from play, 9 Nov: the guns of every player's ship shot straight ahead, whatever the cursor pointed at. The fire
 * lease (lockstep_pilot_fire.h) puts a MindlessShootController on each gun of a slot's active group: its trigger is the
 * record's, but the class has no target and its aim (vtable slot 21, 0x7e72b0, asked by Weapon::Move at 8141c2) answers
 * false, so the gimbals stay at rest.
 * What X4 does for its own player (PlayerShootController slot 21, 0x7e8650, read 9 Nov): with manual aim (slot 15,
 * 0x7e8e60: the input object's mouse steering flag and two options, nothing of the controller) the answer is one point,
 * written to both outputs in the frame of the component Weapon::Move passes (argument 4), and true: the camera pick
 * (player +0x6c60 in the frame of its class 0x6d ancestor) or, without a pick, the cursor ray 719750. Without manual aim
 * it is the aim assist on the player's target, or false.
 * Here: the manual aim and the aim assist, for every stock slot.
 *   capture   the slot's own client samples the point as lockstep_pilot_stock.h does for slot 1 (pstk_aim_sample: the
 *             pick or the cursor ray, as a point in the frame of the player's ship), slots 2..N inside their presentation
 *             scope (the singleton, the pick's fields and the camera are the slot's there; the capture runs in the
 *             probe's frame, where the scope is paused: it is opened for the sample and paused again). X4's own manual
 *             aim predicate, asked on that client, goes into the record's steering byte as bit 1 (PAIM_STEER_MANUAL): the
 *             options it reads are that player's own. Bit 2 (PAIM_STEER_ASSIST) is X4's aim assist decision as far as it
 *             is the player's (7e86fc: the option's mode, 0x2f4b9c4 or with 3 there 0x2f4b9c0, is 1, or 2 without mouse
 *             steering). The focus rule holds (no aim without the focus).
 *   record    the stock record's aim point and steering byte (bytes 16..28), now for every slot (before: slot 1's only,
 *             a slot record with an aim was refused). Every client arms this or none does: the stack is the session's.
 *   replay    Weapon::Move (8132d0) asks its controller three things, and the Mindless vtable's three entries are ours on
 *             every client (aim1, the first live run, had only slot 21: it was never called, 0 calls in 900 ticks with
 *             both slots firing):
 *               slot 15 (+0x78, 8140b1) "manual aim?": Mindless answers no (0xb38c0), and without a target in range the
 *                 aim is then not asked at all (8140dd). Ours answers yes for a controller the fire lease holds (found by
 *                 its pointer; not one the simulation replaced), on the frame thread, when this tick's record of its
 *                 slot carries an aim and the manual bit;
 *               slot 21 (+0xa8, 8141c2) the aim: for the same controllers, the record's point taken from the slot ship's
 *                 frame to the asked frame (3dde10), in both outputs, true;
 *               slot 17 (+0x88) the target: see assist.
 *             Anything else is X4's own answer (no, false: at rest), also for every other Mindless controller (the AI's
 *             <shoot>). The inputs are the record and the simulation's transforms: the same on every client. Slot 15 is
 *             asked nowhere else in Weapon::Move or the shoot decision (816d20).
 *   assist    (gate.pilot_aim(1,1); native_gate_pilot_aim_assist) without the manual bit, with the assist bit and a target
 *             of the slot (its player's soft target, +0x6dd0, the one the missiles take, replayed by the UI replay's
 *             target family; not its own ship): slot 17 answers that target for the slot's leased controllers, for the
 *             whole tick. X4 then does what it does for a targeted controller: the range gate before the aim (813f11,
 *             8140dd), its own aim on a target with the lead (7e72b0 takes the target from slot 17 through slot 18,
 *             7e5740) and the part after it (8141ef). The shoot decision (816d20) reads slot 17 too (817120): with a
 *             target it runs its checks on the target before the Mindless "shoot without a target" answer (slot 28)
 *             fires the gun, so a held trigger may not fire where X4's own player gun would (its slot 16 says yes and
 *             skips them). Open: read those checks, or answer slot 16 as well.
 * Not here: X4's own narrowing of the assist (the target class flag 7ad890 +0x318, the range hysteresis of 7e8a42),
 * fixed guns' convergence, turrets.
 * Live (aim1, third pass, 9 Nov: x4ls play, two X4 on one PC, 900 ticks, a scripted manual aim at (+-300,100,1000) with the
 * trigger from 150 to 300, then each slot's target on the other's ship and the assist bit from 450 to 650): 0 desyncs, 0
 * errors. Slot 15 answered yes 1122 times and slot 21 gave 1122 points, the same counts on both clients. The weapons'
 * gimbal joints (gate.ik_joints, every 30 ticks, 84 rows equal on both clients): at rest before; from 180 to 300 slot 1's
 * guns at +0.1745 rad (the gimbal's 10 degree limit: the point is 16.7 degrees out) and 0.096 rad (5.5 of the point's 5.7
 * degrees), slot 2's mirrored; at rest again by 330. Client 2's window had the focus: outside the script its record
 * carried its own cursor's aim, sampled in its presentation scope (411 samples, 2 without a scope), and slot 2's guns
 * followed it on both clients. The assist: slot 17 answered the target 1176 times, the guns kept firing as without one
 * (the same fire receipts as a run without the answer), but the aim was never asked (0 calls): the ships were out of the
 * guns' range, where X4 leaves the gimbals at rest too. So the assist is not shown working yet and stays off by default.
 * Stop: the vtable entries go back (paim_restore, with the other pilot hooks). Status: gate.pilot_aim_status.
 * Runs without people: gate.pilot_aim_script(1,x,y,z,fire) makes this client's own record carry that point (its ship's
 * frame) as a manual aim, with the primary trigger when fire is 1, whatever the focus; (2,...) carries the assist bit
 * instead; (0) ends it. */
#define PAIM_VTABLE_SLOT 0xa8u          /* slot 21: the aim */
#define PAIM_MINDLESS_AIM 0x7e72b0u
#define PAIM_MANUAL 0x7e8e60u           /* PlayerShootController slot 15: X4's manual aim predicate */
#define PAIM_MANUAL_SLOT 0x78u          /* slot 15: manual aim? */
#define PAIM_NOT_MANUAL 0xb38c0u        /* Mindless: no */
#define PAIM_TARGET_SLOT 0x88u          /* slot 17: the controller's target */
#define PAIM_NO_TARGET 0xa3a20u         /* Mindless: none */
#define PAIM_TARGET_VIA 0x7e5740u       /* slot 18 of every class: slot 17's answer */
#define PAIM_ASSIST_MODE 0x2f4b9c4u     /* the aim assist option as 7e86fc reads it; 3: the value at PAIM_ASSIST_OTHER */
#define PAIM_ASSIST_OTHER 0x2f4b9c0u
static const struct {unsigned rva,size;const char *sha;} paim_pins_list[]={
    {PAIM_MINDLESS_AIM,429u,"7db8ca6a7f7bb3e70f130b12451f3e553fcb1c4e186010dab024c216c236aded"},
    {PAIM_MANUAL,40u,"f98e25330df0d4fd58bc9216ee921abea58906f1d77f560b7231e580dadb7f2f"},
    {PAIM_NO_TARGET,11u,"7e90c224728a8e94ca0a7381c44b1d32267555c755fab186741292f999f25446"},
    {PAIM_NOT_MANUAL,3u,"01cb47d078b4841b8408ec4fe278efa83115c6f6e101972987507d2b2b57dcf0"},
    {PAIM_TARGET_VIA,27u,"3629c7bb772dbc62f9d54335b3cb235c05b4dab2b3810cac75ea24f905506cd2"}};
/* The Mindless vtable's entries this takes: the slot, X4's function there, ours. */
typedef unsigned char (*paim_manual_fn)(void *);
typedef void **(*paim_target_fn)(void *,void **,void *);
static unsigned char paim_aim(void *,float *,float *,void *,void *,void *,float,float,unsigned char,unsigned char,unsigned char,unsigned char,
                              unsigned char);
static unsigned char paim_manual_get(void *controller);
static void **paim_target_get(void *controller,void **out,void *weapon);
static const struct {unsigned slot,target;void *ours;const char *why;} paim_entries[]={
    {PAIM_MANUAL_SLOT,PAIM_NOT_MANUAL,(void*)paim_manual_get,"vtable_manual"},
    {PAIM_TARGET_SLOT,PAIM_NO_TARGET,(void*)paim_target_get,"vtable_target"},
    {PAIM_VTABLE_SLOT,PAIM_MINDLESS_AIM,(void*)paim_aim,"vtable_aim"}};
#define PAIM_ENTRIES (sizeof(paim_entries)/sizeof(*paim_entries))
static int paim_entry_installed[PAIM_ENTRIES],paim_pins_checked,paim_assist_on;
static pstk_aim_fn paim_original;
static paim_manual_fn paim_manual_original;
static paim_target_fn paim_target_original;
static paim_manual_fn paim_manual_override;   /* tests */
static int paim_assist_override;              /* tests: 1 wanted, -1 not */
static unsigned char *(*paim_target_override)(unsigned slot);   /* tests */
static volatile LONG64 paim_calls,paim_aimed,paim_rest,paim_other,paim_foreign,paim_samples,paim_sample_failures,paim_manual_samples,
    paim_no_scope,paim_manual_calls,paim_manual_answers,paim_target_calls,paim_target_answers,paim_assist_samples,paim_assisted,
    paim_assist_missed;
static const char *paim_why="-";
static int paim_script_on;
static float paim_script_point[3];

static int paim_pins(void) {
    if(!paim_pins_checked) {
        paim_pins_checked=1;
        for(unsigned i=0;i<sizeof(paim_pins_list)/sizeof(*paim_pins_list);i++)
            if(!hash_ok(image+paim_pins_list[i].rva,paim_pins_list[i].size,paim_pins_list[i].sha)) paim_pins_checked=-1;
        if(!pfire_pins() || !pstk_fn.transform) paim_pins_checked=-1;
    }
    return paim_pins_checked>0;
}
/* The player's side of X4's aim assist decision (7e86fc..7e8728), on the capturing client. */
static int paim_assist_wanted(unsigned char steering) {
    uint32_t mode=0,other=0;
    if(paim_assist_override) return paim_assist_override>0;
    if(!read_local(pnat_base+PAIM_ASSIST_MODE,&mode,4) || !read_local(pnat_base+PAIM_ASSIST_OTHER,&other,4)) return 0;
    if(mode==3u) mode=other;
    return mode==1u || (mode==2u && !steering);
}
/* The capturing client's aim of this frame for its slot's record: 1 sampled, 0 not readable, -1 no focus. */
static int paim_take(unsigned slot,float aim[3],unsigned char *steer) {
    if(!paim_armed) return slot==1?pstk_aim_take(aim,steer):0;
    if(paim_script_on) {
        memcpy(aim,paim_script_point,12);*steer=paim_script_on==2?PAIM_STEER_ASSIST:1u|PAIM_STEER_MANUAL;
        InterlockedIncrement64(&paim_samples);InterlockedIncrement64(paim_script_on==2?&paim_assist_samples:&paim_manual_samples);return 1;
    }
    if(!pstk_focused()) {InterlockedIncrement64(&pstk_aim_unfocused);return -1;}
    int resumed=0;
    if(slot>=2 && !pres_open) {   /* the probe's frame: the slot's presentation scope for the sample */
        if(pres_slot!=slot || !(resumed=pres_resume())) {InterlockedIncrement64(&paim_no_scope);return 0;}
    }
    const int ok=pstk_aim_sample(aim,steer);
    if(resumed) pres_pause();
    if(!ok) {InterlockedIncrement64(&paim_sample_failures);return 0;}
    const paim_manual_fn manual=paim_manual_override?paim_manual_override:(paim_manual_fn)(void*)(image+PAIM_MANUAL);
    if(manual(NULL)) {*steer|=PAIM_STEER_MANUAL;InterlockedIncrement64(&paim_manual_samples);}
    else if(paim_assist_wanted(*steer&1u)) {*steer|=PAIM_STEER_ASSIST;InterlockedIncrement64(&paim_assist_samples);}
    InterlockedIncrement64(&paim_samples);
    return 1;
}
static int paim_lease_of(const void *controller,unsigned *slot) {
    for(unsigned s=0;s<pilot_slot_count && s<PILOT_SLOTS;s++) {
        const pfire_slot *f=&pfire_slots[s];
        for(unsigned g=0;f->leased && g<f->count && g<PFIRE_GUNS;g++)
            if(f->guns[g].controller==controller && !f->guns[g].foreign) {*slot=s+1;return 1;}
    }
    return 0;
}
/* This tick's replayed aim of a slot: the point in its ship's frame and the steering byte. */
static int paim_record(unsigned slot,float p[3],unsigned char *steer) {
    if(slot==1) {
        if(!pstk_aim_tick || pstk_aim_tick!=stats.issued || !pstk_aim_valid_replay) return 0;
        memcpy(p,pstk_aim_replay,12);*steer=pstk_aim_steer_replay;
    } else {
        const pstk_slot_frame *f=&pstk_slot_frames[slot-1];
        if(!f->tick || f->tick!=stats.issued || !(f->r.flags&PSTK_FLAG_AIM)) return 0;
        memcpy(p,f->r.aim,12);*steer=f->r.steer;
    }
    return 1;
}
/* One of our leased controllers asked on the frame thread, with this tick's aim of its slot: the slot, or 0. */
static unsigned paim_asked(const void *controller,float p[3],unsigned char *steer) {
    unsigned slot=0;
    if(!paim_armed || GetCurrentThreadId()!=ui_thread || !paim_lease_of(controller,&slot) || !paim_record(slot,p,steer)) return 0;
    return slot;
}
/* Slot 15: manual aim? */
static unsigned char paim_manual_get(void *controller) {
    float p[3];unsigned char steer=0;
    InterlockedIncrement64(&paim_manual_calls);
    if(paim_asked(controller,p,&steer) && (steer&PAIM_STEER_MANUAL)) {InterlockedIncrement64(&paim_manual_answers);return 1;}
    return paim_manual_original(controller);
}
static unsigned char *paim_target_of(unsigned slot) {
    if(paim_target_override) return paim_target_override(slot);
    const uint64_t target=pfire_slot_target(slot);
    return target?(unsigned char*)pfire_find(target):NULL;
}
/* Slot 17: the slot's target while its record asks for the aim assist; X4's answer (none) otherwise. */
static void **paim_target_get(void *controller,void **out,void *weapon) {
    float p[3];unsigned char steer=0;
    InterlockedIncrement64(&paim_target_calls);
    const unsigned slot=paim_assist_on && out?paim_asked(controller,p,&steer):0;
    if(slot && (steer&(PAIM_STEER_MANUAL|PAIM_STEER_ASSIST))==PAIM_STEER_ASSIST) {
        unsigned char *target=paim_target_of(slot);
        if(target && target!=pilot_slots[slot-1].object) {*out=target;InterlockedIncrement64(&paim_target_answers);return out;}
    }
    return paim_target_original(controller,out,weapon);
}
/* Slot 21: the aim. */
static unsigned char paim_aim(void *controller,float *first,float *second,void *space,void *weapon,void *transform,float a7,float a8,
                              unsigned char a9,unsigned char a10,unsigned char a11,unsigned char a12,unsigned char a13) {
    unsigned slot=0;float p[3],t[16],q[4]={0.0f,0.0f,0.0f,0.0f};unsigned char steer=0;
    InterlockedIncrement64(&paim_calls);
    if(!paim_armed || !paim_lease_of(controller,&slot)) {
        InterlockedIncrement64(&paim_other);return paim_original(controller,first,second,space,weapon,transform,a7,a8,a9,a10,a11,a12,a13);
    }
    if(GetCurrentThreadId()!=ui_thread) {
        InterlockedIncrement64(&paim_foreign);return paim_original(controller,first,second,space,weapon,transform,a7,a8,a9,a10,a11,a12,a13);
    }
    unsigned char *ship=pilot_slots[slot-1].object;
    if(!first || !second || !space || !ship || !paim_record(slot,p,&steer) || !(steer&(PAIM_STEER_MANUAL|PAIM_STEER_ASSIST))) {
        InterlockedIncrement64(&paim_rest);return paim_original(controller,first,second,space,weapon,transform,a7,a8,a9,a10,a11,a12,a13);
    }
    if(!(steer&PAIM_STEER_MANUAL)) {   /* the aim assist: X4's own aim; it takes the slot's target from slot 17 */
        const unsigned char result=paim_original(controller,first,second,space,weapon,transform,a7,a8,a9,a10,a11,a12,a13);
        InterlockedIncrement64(result?&paim_assisted:&paim_assist_missed);
        return result;
    }
    pstk_identity(t);pstk_fn.transform(ship,t,space,0);pstk_apply(t,p,q);
    if(!isfinite(q[0]) || !isfinite(q[1]) || !isfinite(q[2])) {
        InterlockedIncrement64(&paim_rest);return paim_original(controller,first,second,space,weapon,transform,a7,a8,a9,a10,a11,a12,a13);
    }
    memcpy(first,q,16);memcpy(second,q,16);InterlockedIncrement64(&paim_aimed);
    return 1;
}
static void **paim_entry(unsigned i) {return (void**)(pnat_base+PFIRE_MINDLESS_VTABLE+paim_entries[i].slot);}
static void paim_uninstall(void) {
    for(unsigned i=0;i<PAIM_ENTRIES;i++)
        if(paim_entry_installed[i] && pnat_swap8(paim_entry(i),paim_entries[i].ours,(void*)(image+paim_entries[i].target),1)) paim_entry_installed[i]=0;
}
static int paim_arm(int assist) {
    if(paim_armed) return (assist!=0)==paim_assist_on;
    if(GetCurrentThreadId()!=ui_thread) {paim_why="thread";return 0;}
    if(!pfire_armed || pfire_mode!=PFIRE_RECORD) {paim_why="fire_record";return 0;}
    if(!paim_pins()) {paim_why="pins";return 0;}
    paim_manual_original=(paim_manual_fn)(void*)(image+PAIM_NOT_MANUAL);
    paim_target_original=(paim_target_fn)(void*)(image+PAIM_NO_TARGET);
    paim_original=(pstk_aim_fn)(void*)(image+PAIM_MINDLESS_AIM);
    for(unsigned i=0;i<PAIM_ENTRIES;i++) {
        if(!pnat_swap8(paim_entry(i),(void*)(image+paim_entries[i].target),paim_entries[i].ours,1)) {paim_uninstall();paim_why=paim_entries[i].why;return 0;}
        paim_entry_installed[i]=1;
    }
    paim_assist_on=assist!=0;paim_armed=1;paim_why="armed";return 1;
}
static void paim_restore(void) {
    paim_uninstall();
    paim_armed=0;paim_assist_on=0;paim_script_on=paim_script_fire=0;
}
static int paim_status(char *out,size_t cap) {
    return snprintf(out,cap,"armed=%d why=%s installed=%d%d%d assist_on=%d aim=%lld/%lld/%lld/%lld/%lld manual=%lld/%lld target=%lld/%lld "
        "assist=%lld/%lld samples=%lld/%lld/%lld/%lld no_scope=%lld script=%d",paim_armed,paim_why,paim_entry_installed[0],paim_entry_installed[1],
        paim_entry_installed[2],paim_assist_on,(long long)paim_calls,(long long)paim_aimed,(long long)paim_rest,(long long)paim_other,
        (long long)paim_foreign,(long long)paim_manual_calls,(long long)paim_manual_answers,(long long)paim_target_calls,
        (long long)paim_target_answers,(long long)paim_assisted,(long long)paim_assist_missed,(long long)paim_samples,
        (long long)paim_manual_samples,(long long)paim_assist_samples,(long long)paim_sample_failures,(long long)paim_no_scope,
        paim_script_on+8*paim_script_fire);
}
/* gate.pilot_aim(1[,assist]): arm; assist 1 = the aim assist on the slot's target as well. */
static int lua_pilot_aim(lua_State *L) {
    const int ok=accept_ui() && p_lua_tonumber(L,1)==1.0 && paim_arm(p_lua_tonumber(L,2)==1.0);
    p_lua_pushnumber(L,ok);p_lua_pushlstring(L,paim_why,strlen(paim_why));return 2;
}
static int lua_pilot_aim_script(lua_State *L) {
    const double on=p_lua_tonumber(L,1),x=p_lua_tonumber(L,2),y=p_lua_tonumber(L,3),z=p_lua_tonumber(L,4),fire=p_lua_tonumber(L,5);
    const int ok=accept_ui() && paim_armed && (on==0.0 || ((on==1.0 || on==2.0) && isfinite(x) && isfinite(y) && isfinite(z)));
    if(ok) {   /* 1: a manual aim at the point; 2: the aim assist on the slot's target */
        paim_script_on=(int)on;paim_script_fire=paim_script_on && fire==1.0;
        paim_script_point[0]=(float)x;paim_script_point[1]=(float)y;paim_script_point[2]=(float)z;
    }
    p_lua_pushnumber(L,ok);return 1;
}
static int lua_pilot_aim_status(lua_State *L) {
    char out[512];int n=paim_status(out,sizeof(out));
    p_lua_pushlstring(L,out,(size_t)(n>0?n:0));return 1;
}
