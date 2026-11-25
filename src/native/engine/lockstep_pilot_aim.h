/* Gun aim per player (native_gate_pilot_aim; a session default with native_gate_pilot_fire = 'record' and stock slots).
 * Report from play, 9 Nov: the guns of every player's ship shot straight ahead, whatever the cursor pointed at. The fire
 * lease (lockstep_pilot_fire.h) puts a MindlessShootController on each gun of a slot's active group: its trigger is the
 * record's, but the class has no target and its aim (vtable slot 21, 0x7e72b0, asked by Weapon::Move at 8141c2) answers
 * false, so the gimbals stay at rest.
 * What X4 does for its own player (PlayerShootController slot 21, 0x7e8650, read 9 Nov and again 24 Nov): the target is
 * slot 17's (+0x10, the player's target); the aim assist is on when the option's mode (0x2f4b9c4, or with 3 there
 * 0x2f4b9c0) is 1, or 2 without mouse steering, and the target's class allows it (7ad890(macro)+0x318). With manual aim
 * (slot 15, 0x7e8e60: the input object's mouse steering flag and two options, nothing of the controller) the point is the
 * camera pick (player +0x6c60 in the frame of its class 0x6d ancestor) or, without a pick, the cursor ray 719750 (100 km
 * out); with the aim assist as well, the guns LOCK onto the target when that point lies in a cone around it: the cosine
 * between the directions from the player to the point and to the target's aim point nearest the gun (520fc0) above
 * config+0xc8 (parameters.xml playerflight/aimassist angle; config = *0x6cf1538). Otherwise the point, in both outputs of
 * the frame Weapon::Move passes (argument 4), and true. Without manual aim: the lock with the aim assist, else false. The
 * lock (7e8a42; Weapon::Move passes a13 = 1, so it is decided at every aim): set unless the target's vtable +0x1bf0 says
 * yes, it has no aim targets (59e100(macro)+0x760) and it is closer than config+0x294 (weapons/player/autoaim
 * mindistance); kept in +0xb8 (slot 34, the HUD's aim assist icon); then 7e72b0, X4's aim on a target with the lead,
 * else false. After a true aim Weapon::Move asks slot 18 (8141ef) for the target once more: its velocity goes into the
 * gimbal's turn.
 * Here: the same for every stock slot, slot k with its own player P_k, its own target and its own player's option.
 *   capture   the slot's own client samples the point as lockstep_pilot_stock.h does for slot 1 (pstk_aim_sample: the
 *             pick or the cursor ray, as a point in the frame of the player's ship), slots 2..N inside their presentation
 *             scope (the singleton, the pick's fields and the camera are the slot's there; the capture runs in the
 *             probe's frame, where the scope is paused: it is opened for the sample and paused again). X4's own manual
 *             aim predicate, asked on that client, goes into the record's steering byte as bit 1 (PAIM_STEER_MANUAL): the
 *             options it reads are that player's own. Bit 2 (PAIM_STEER_ASSIST) is X4's aim assist decision as far as it
 *             is the player's (paim_assist_rule: mode 1, or 2 without mouse steering), with or without the manual bit
 *             (before 24 Nov only without it: with mouse steering nobody's guns ever locked, user report: no lock
 *             onto the target ship). The focus rule holds (no aim without the focus). The track (paim_track, below) may
 *             turn a manual point near the slot's target first.
 *   record    the stock record's aim point and steering byte (bytes 16..28), for every slot. Every client arms this or
 *             none does: the stack is the session's. The steering byte's meaning changed on 24 Nov (PSTK_VERSION 4).
 *   replay    Weapon::Move (8132d0) asks its controller five things, and the Mindless vtable's five entries are ours on
 *             every client (aim1, the first live run, had only slot 21: it was never called, 0 calls in 900 ticks with
 *             both slots firing):
 *               slot 15 (+0x78, 8140b1) "manual aim?": Mindless answers no (0xb38c0), and without a target in range the
 *                 aim is then not asked at all (8140dd). Ours answers yes for a controller the fire lease holds (found by
 *                 its pointer; not one the simulation replaced), on the frame thread, when this tick's record of its
 *                 slot carries an aim and the manual bit, or the assist bit with an assist target in the gun's range;
 *               slot 21 (+0xa8, 8141c2) the aim, for the same controllers: X4's order (paim_aim). Manual: the record's
 *                 point taken from the slot ship's frame to the asked frame (3dde10); with the assist bit and the slot's
 *                 assist target, the cone (paim_cone: from P_k, uirep_slot_player, to the point and to 520fc0's point,
 *                 exact roots where X4 uses rsqrtss, which differs between processors; the cosine is the same in every
 *                 frame, so both are taken in the asked one) and, inside it, the lock (paim_lock); outside it the
 *                 point. Assist without manual aim: the lock on a target in the gun's range;
 *               slot 17 (+0x88) the target: see assist;
 *               slot 18 (+0x90, 8141ef) the target for the gimbal's turn: the slot's target, asked from that one call
 *                 site (its return address image+0x8141f5) right after one of our aims answered true for that
 *                 controller (paim_via_answer), else X4's answer (7e5740: slot 17's, none). Never at the range gate
 *                 (813f11) or the shoot decision (8175ed, 817120), which DELETES a Mindless controller with a far target;
 *               slot 34 (+0x110) locked?: whether this gun's aim locked at this tick or the one before (paim_locks), for
 *                 GetAimAssistState (0xb02450), the one caller through a gun (a sweep of every +0x110 call: 54 sites,
 *                 this the only one after a load of +0x318), the HUD's assist_aim icon: presentation only;
 *               the lock's call of X4's aim passes a10 = 0 as X4's player does (no evasion factor on the lead: paim_lock);
 *               slot 16 (+0x80) "a player's controller?", slots 11 and 13 (+0x58, +0x68) the dot's distances, and
 *                 U::Weapon's +0x2088 "draw this gun's dot?": the guns' aim dots and the HUD's lead marker of this client's
 *                 own guns, presentation only (below "The dots", rk8 report of 24 Nov).
 *             Anything else is X4's own answer (no, false: at rest), also for every other Mindless controller (the AI's
 *             <shoot>). The inputs are the record and the simulation's transforms: the same on every client.
 *   assist    (gate.pilot_aim(1,1); native_gate_pilot_aim_assist) the slot's target is its player's soft target (+0x6dd0,
 *             the one the missiles take, replayed by the UI replay's target family; not its own ship). X4's aim on it,
 *             0x7e72b0, the function every targeted controller answers with (the aim point with the lead; it refreshes
 *             its aim offset in the base class fields at +0x48..+0x7f), takes the target from slot 17, so slot 17
 *             answers the slot's target for that one call, on that thread, for that controller, and none at any other
 *             time. Why not the target for the whole tick (the second pass of aim1 had that): the shoot decision (816d20)
 *             reads slot 17 too (817120), and for a controller whose slot 16 says no (Mindless) a target in another
 *             sector or of the wrong kind makes it DELETE the controller (81725a: 819a90, the destructor, weapon+0x318
 *             cleared). X4's player class says yes in slot 16 and skips that. With the target shown to the aim alone the
 *             trigger stays the lease's: the gun fires when the record says so.
 *             X4's range gate before the aim without manual aim (813f45..8140dd) is kept by asking X4 the same two things
 *             for the gun: the squared distance 0x3e1930(target, gun, 0, the frame's game time, 1) must be below the
 *             square of the gun's range (its vtable +0x1ee8) times the factor at *0x6cf1538 +0x190. Without it a far
 *             target (a station across the sector) would pin the gimbals at their limit toward it (aim2: no gate yet,
 *             the other slot's ship out of range: the guns sat at both 10 degree limits for the whole assist). With
 *             manual aim X4 skips that gate (8140b6) and so does the cone.
 * Not here: fixed guns' convergence, turrets.
 * Live (aim1, third pass, 9 Nov: x4ls play, two X4 on one PC, 900 ticks, a scripted manual aim at (+-300,100,1000) with the
 * trigger from 150 to 300, then each slot's target on the other's ship and the assist bit from 450 to 650): 0 desyncs, 0
 * errors. Slot 15 answered yes 1122 times and slot 21 gave 1122 points, the same counts on both clients. The weapons'
 * gimbal joints (gate.ik_joints, every 30 ticks, 84 rows equal on both clients): at rest before; from 180 to 300 slot 1's
 * guns at +0.1745 rad (the gimbal's 10 degree limit: the point is 16.7 degrees out) and 0.096 rad (5.5 of the point's 5.7
 * degrees), slot 2's mirrored; at rest again by 330. Client 2's window had the focus: outside the script its record
 * carried its own cursor's aim, sampled in its presentation scope (411 samples, 2 without a scope), and slot 2's guns
 * followed it on both clients. The assist: slot 17 answered the target 1176 times, the guns kept firing as without one
 * (the same fire receipts as a run without the answer), but the aim was never asked (0 calls): the ships were out of the
 * guns' range, where X4 leaves the gimbals at rest too. That pass answered the target for the whole tick; the form above
 * (the target for the aim call alone, slot 15 yes) came after it.
 * The assist, live (the same plan, 900 ticks each, 0 desyncs and 0 errors each):
 *   aim2 (that form, no range gate yet): X4's aim on the target answered true 588 times on both clients (4 guns, 147
 *     ticks); slot 1's guns went from rest to the mount's limits (+0.1745 and -0.1745 rad: the other ship is far off the
 *     nose) for the assist's ticks and back to rest after them; the guns fired as before; joints equal on both clients.
 *   aim3 (with the range gate, the assist a session default): the target was 13,823 m away against a reach of 10,080 m,
 *     the gate refused 588 times on both clients and the guns stayed at rest through the assist; the manual part as in
 *     aim1.
 *   aim4 (the same build, 1700 ticks: both slots fly to each other by autopilot from tick 100, target each other at
 *     440, the assist bit and the trigger from 700 to 1500): the target 1,932 m away against the reach of 10,080 m at
 *     tick 1200; X4's aim answered true 3188 times, 0 false, 0 refusals by the gate, the same counts on both clients.
 *     Slot 1's guns: from rest to -0.1333 rad on one joint (inside the mount's limit, moving by a few 1e-6 rad per
 *     reading: tracking) and the limit on the other, at rest again by 1560; slot 2's at a limit. 124 joint rows equal
 *     on both clients.
 * 24 Nov (diagnosis: player 2's 10,290 samples of a fight all manual, 0 assist, slot 17 never answered): the assist bit
 * with mouse steering, the cone, the class flag, the lock's minimum distance, slot 18 and slot 34; C tests only so far.
 * rk8 (24 Nov, player 2 live: the guns still did not point at the lead marker, and the small aim dots of locked guns
 * were missing): the lock worked in the simulation (client
 * 2 at 23400: 12,907 cone hits, X4's aim on the target true 12,865 times) but nothing showed it: X4's per-gun aim dots
 * ask the controller's slot 16, which Mindless says no to, so they were never drawn, for anyone. And the lead was the AI's:
 * the lock's call and the HUD's lead marker both passed a10 = 1 (Mindless is no player's controller), so X4's lead scaled
 * the time of flight by the target's evasion (7d5fc0), which the HUD's call also wrote into the target on that client
 * alone. Now: the dots of this client's own guns, X4's player lead in the lock (a10 = 0) and in the HUD's marker (slot
 * 16's yes there). Live (aim2 live1, play --solo 2, 2100 ticks, the script's aim at slot 1's ship 688 m ahead): at 1800
 * dot=16506/16506/49518/0 (every ask on the frame thread, X4's predicate yes each time, three distances per draw),
 * lead=1505, s2=900/2880/3171; on screen the locked guns' dots merged in the middle of the lead mark with the aim assist
 * icon lit, at rest the outer guns' dots sat beside it with the icon dim; the gimbals at 0.0143 rad while locked (the
 * guns converging on the lead), at the 0.1745 limit in the cursor window; no stop.
 * Stop: the vtable entries go back (paim_restore, with the other pilot hooks). Status: gate.pilot_aim_status.
 * Runs without people: gate.pilot_aim_script(1,x,y,z,fire) makes this client's own record carry that point (its ship's
 * frame) as a manual aim, with the assist bit as X4's option gives it with mouse steering, and the primary trigger when
 * fire is 1, whatever the focus; (2,...) carries the assist bit alone; (3,...) the manual aim without the assist bit;
 * (0) ends it. */
#define PAIM_VTABLE_SLOT 0xa8u          /* slot 21: the aim */
#define PAIM_MINDLESS_AIM 0x7e72b0u
#define PAIM_MANUAL 0x7e8e60u           /* PlayerShootController slot 15: X4's manual aim predicate */
#define PAIM_MANUAL_SLOT 0x78u          /* slot 15: manual aim? */
#define PAIM_NOT_MANUAL 0xb38c0u        /* Mindless: no */
#define PAIM_TARGET_SLOT 0x88u          /* slot 17: the controller's target */
#define PAIM_NO_TARGET 0xa3a20u         /* Mindless: none */
#define PAIM_TARGET_VIA 0x7e5740u       /* slot 18 of every class: slot 17's answer */
#define PAIM_VIA_SLOT 0x90u             /* slot 18 */
#define PAIM_TURN_RETURN 0x8141f5u      /* Weapon::Move after its slot 18 call at 8141ef: the target of the gimbal's turn */
#define PAIM_LOCK_SLOT 0x110u           /* slot 34: locked? (PlayerShootController 7e5c00: +0xb8; Mindless 0xb38c0: no) */
#define PAIM_ASSIST_MODE 0x2f4b9c4u     /* the aim assist option as 7e86fc reads it; 3: the value at PAIM_ASSIST_OTHER */
#define PAIM_ASSIST_OTHER 0x2f4b9c0u
#define PAIM_DISTANCE 0x3e1930u         /* squared distance: target, gun, 0, game time, 1 (Weapon::Move 813f5c), 2 (7e8ac0) */
#define PAIM_RANGE_SLOT 0x1ee8u         /* the gun's range (its own vtable; Weapon::Move 8140c0) */
#define PAIM_RANGE_SCALE 0x6cf1538u     /* the config: *this +0x190 the factor on the range (8140cd), +0xc8, +0x294 */
#define PAIM_PLAYER_AIM 0x7e8650u       /* PlayerShootController slot 21: the rules taken over here (pinned, never called) */
#define PAIM_AIM_POINT 0x520fc0u        /* (target, gun): the target's aim point nearest the gun or its centre, target frame */
#define PAIM_CLASS_OF 0x7ad890u         /* a macro's class 0x4d data: +0x318 the aim assist flag (7e8738) */
#define PAIM_MACRO_OF 0x59e100u         /* a macro's class 0x24 data: +0x760 its aim targets (7e8a75) */
#define PAIM_ASSIST_STATE 0xb02450u     /* GetAimAssistState: slot 34's one caller through a gun (the HUD icon) */
#define PAIM_CLASS_FLAG 0x318u
#define PAIM_AIM_TARGETS 0x760u
#define PAIM_LARGE_SLOT 0x1bf0u         /* the target's vtable: the kind the minimum distance applies to (7e8a62) */
#define PAIM_CONE 0xc8u                 /* config: the cone's cosine */
#define PAIM_MIN_DISTANCE 0x294u        /* config: the lock's minimum distance */
#define PAIM_EPSILON 1e-27f             /* 7e8953: added to each squared length before the root */
#define PAIM_LOCK_FRESH 2u              /* slot 34: locked at this tick or the one before */
/* The guns' aim dots and X4's player lead (rk8 report of 24 Nov, below "The dots"). */
#define PAIM_PLAYER_SLOT 0x80u          /* slot 16: a player's controller? (PlayerShootController 9c980 yes, Mindless b38c0 no) */
#define PAIM_YES 0x9c980u
#define PAIM_REACH_SLOT 0x58u           /* slot 11: the barrel ray's distance (Player 7e5be0: +0xa8; Mindless 1538d0: 1) */
#define PAIM_TARGET_DISTANCE_SLOT 0x68u /* slot 13: the target's distance (Player 7e5bf0: +0xac; Mindless 1538d0: 1) */
#define PAIM_ONE 0x1538d0u              /* 1.0f */
#define PAIM_DOT_SLOT 0x2088u           /* U::Weapon's vtable: draw this gun's aim dot? 81bb50 */
#define PAIM_DOT_ASKED 0x81bb50u
#define PAIM_DOT_ASK_RETURN 0x81bb6eu   /* 81bb50 after its slot 16 call at 81bb68 */
#define PAIM_DOT_RENDER 0x81b92au       /* Weapon vfunc 693 (81b900) after its 0x2088 call at 81b924 */
#define PAIM_DOT_RENDER2 0x81bae5u      /* Weapon vfunc 695 (81b960) after its 0x2088 call at 81badf */
#define PAIM_DOT_TARGET_RETURN 0x81d786u/* 81d700 (the dot's place) after slot 13 at 81d783 */
#define PAIM_DOT_REACH_RETURN 0x81d796u /* ...after slot 11 at 81d793 */
#define PAIM_DOT_COLOUR_RETURN 0x81bfc9u/* 81bbc0 (the dot's draw) after slot 13 at 81bfc6: in or out of range */
#define PAIM_HUD_LEAD_RETURN 0x815636u  /* 815330 (the HUD's lead marker) after its slot 16 call at 815630 */
#define PAIM_DOT_FAR 1.8446743e19f      /* X4's +0xac without a target: the root of FLT_MAX (Weapon::Move 813eea, 81471f) */
static const struct {unsigned rva,size;const char *sha;} paim_pins_list[]={
    {PAIM_MINDLESS_AIM,429u,"7db8ca6a7f7bb3e70f130b12451f3e553fcb1c4e186010dab024c216c236aded"},
    {PAIM_MANUAL,40u,"f98e25330df0d4fd58bc9216ee921abea58906f1d77f560b7231e580dadb7f2f"},
    {PAIM_NO_TARGET,11u,"7e90c224728a8e94ca0a7381c44b1d32267555c755fab186741292f999f25446"},
    {PAIM_NOT_MANUAL,3u,"01cb47d078b4841b8408ec4fe278efa83115c6f6e101972987507d2b2b57dcf0"},
    {PAIM_DISTANCE,96u,"c7854f4aa35006c61ed129716bdf5f287636210944ee50aebd7e16d12e976e24"},
    {PAIM_TARGET_VIA,27u,"3629c7bb772dbc62f9d54335b3cb235c05b4dab2b3810cac75ea24f905506cd2"},
    {PAIM_PLAYER_AIM,1379u,"f20d8c3f389550961e8198ce56ee827dd4c612acdfd13ce6c5dbfd52f73893dd"},
    {PAIM_AIM_POINT,283u,"9260b3cb196bc84cfebdeaca9ebc0ef64d2b8d5983edf242f79ad49f29112eab"},
    {PAIM_CLASS_OF,193u,"d1d7f1a6fc755432e9c70f5f963cba12008a85fd9b25d3d5dbec9cb78f0ee958"},
    {PAIM_MACRO_OF,193u,"f2793ef7f9216e6315138ed54b8105fef03181728f7b7155e858bfe428efd81a"},
    {PAIM_ASSIST_STATE,312u,"b9b6e5d0b83f22e47814450466769ed2d7eaba07e7977aeeffff581df3e6d6d2"},
    {PAIM_DOT_ASKED,108u,"82bf6a4b51b01ca86081f1d577f2f5e28b432434305316f6fe1229e7724def36"},   /* the dot's predicate */
    {0x81b900u,95u,"e35d3b397b6731c7352123675cb5ec598f6487652ebf0556fbda446a5111c270"},       /* Weapon vfunc 693: it asks, draws */
    {0x81b960u,485u,"244fff2b1c13d38d51c4fdef0c27d60715790b8ef77d0f47f522846b5ae682ae"},      /* Weapon vfunc 695: the same */
    {0x81bbc0u,1914u,"d3ee988baf59209d3316aa26fccb8463b6d0b6429c513ff76e8a76c5253d3606"},     /* the dot's draw */
    {0x81d700u,439u,"32d5e4f1b786eb4921bec9b49a169739cee5c73b12e925bc01ecb4b851a00fd1"},      /* the dot's place */
    {0x815330u,1232u,"0f96acd4c9e658a053f9ed1df35b93071bb994cfe481a3282beea6b81bb3e626"},     /* the HUD's lead marker */
    {0x7e7460u,2088u,"8615260430dd2227b380a3a6d399ef414078f32332c81014dbff37f9fb624243"}};    /* X4's lead (a10: the evasion) */
/* The vtable entries this takes: the vtable, the slot, X4's function there, ours. All but the last are the Mindless
 * vtable's (PFIRE_MINDLESS_VTABLE), every MindlessShootController's; the last is U::Weapon's (every gun's). */
typedef unsigned char (*paim_manual_fn)(void *);
typedef void **(*paim_target_fn)(void *,void **,void *);
typedef float (*paim_float_fn)(void *);
static unsigned char paim_aim(void *,float *,float *,void *,void *,void *,float,float,unsigned char,unsigned char,unsigned char,unsigned char,
                              unsigned char);
static unsigned char paim_manual_get(void *controller);
static void **paim_target_get(void *controller,void **out,void *weapon);
static void **paim_via_get(void *controller,void **out,void *weapon);
static unsigned char paim_locked_get(void *controller);
static unsigned char paim_player_get(void *controller);
static float paim_reach_get(void *controller);
static float paim_target_distance_get(void *controller);
static unsigned char paim_dot_get(void *weapon);
static const struct {unsigned vtable,slot,target;void *ours;const char *why;} paim_entries[]={
    {PFIRE_MINDLESS_VTABLE,PAIM_MANUAL_SLOT,PAIM_NOT_MANUAL,(void*)paim_manual_get,"vtable_manual"},
    {PFIRE_MINDLESS_VTABLE,PAIM_TARGET_SLOT,PAIM_NO_TARGET,(void*)paim_target_get,"vtable_target"},
    {PFIRE_MINDLESS_VTABLE,PAIM_VTABLE_SLOT,PAIM_MINDLESS_AIM,(void*)paim_aim,"vtable_aim"},
    {PFIRE_MINDLESS_VTABLE,PAIM_VIA_SLOT,PAIM_TARGET_VIA,(void*)paim_via_get,"vtable_via"},
    {PFIRE_MINDLESS_VTABLE,PAIM_LOCK_SLOT,PAIM_NOT_MANUAL,(void*)paim_locked_get,"vtable_lock"},
    {PFIRE_MINDLESS_VTABLE,PAIM_PLAYER_SLOT,PAIM_NOT_MANUAL,(void*)paim_player_get,"vtable_player"},
    {PFIRE_MINDLESS_VTABLE,PAIM_REACH_SLOT,PAIM_ONE,(void*)paim_reach_get,"vtable_reach"},
    {PFIRE_MINDLESS_VTABLE,PAIM_TARGET_DISTANCE_SLOT,PAIM_ONE,(void*)paim_target_distance_get,"vtable_distance"},
    {PFIRE_WEAPON_VTABLE,PAIM_DOT_SLOT,PAIM_DOT_ASKED,(void*)paim_dot_get,"vtable_dot"}};
#define PAIM_ENTRIES (sizeof(paim_entries)/sizeof(*paim_entries))
static int paim_entry_installed[PAIM_ENTRIES],paim_pins_checked,paim_assist_on;
static pstk_aim_fn paim_original;
static paim_manual_fn paim_manual_original,paim_lock_original,paim_player_original,paim_dot_original;
static paim_target_fn paim_target_original,paim_via_original;
static paim_float_fn paim_reach_original,paim_distance_original;
/* The dots (frame thread): the controller whose X4 predicate is asked for its gun's dot right now; counters. */
static void *paim_dot_for;
static volatile LONG64 paim_dot_asks,paim_dot_shown,paim_dot_distances,paim_dot_elsewhere,paim_hud_leads;
static unsigned (*paim_own_slot_override)(void);   /* tests */
static float (*paim_gun_range_override)(void *);   /* tests */
static paim_manual_fn paim_manual_override;   /* tests */
static int paim_assist_override;              /* tests: 1 wanted, -1 not */
static unsigned char *(*paim_target_override)(unsigned slot);   /* tests */
static int paim_range_override;               /* tests: 1 in range, -1 out */
typedef float (*paim_distance_fn)(void *,void *,void *,double,int);
typedef float (*paim_range_fn)(void *,unsigned);
/* X4's functions the cone and the lock ask (set at arming; tests put stand-ins). */
typedef struct {
    const float *(*point)(void *,void *);     /* 520fc0(target, gun) */
    unsigned char *(*class_of)(void *);       /* 7ad890(macro) */
    unsigned char *(*macro_of)(void *);       /* 59e100(macro) */
    paim_distance_fn distance;                /* 3e1930 */
} paim_fns;
static paim_fns paim_fn;
typedef unsigned char (*paim_large_fn)(void *);
static volatile LONG64 paim_out_of_range;
static float paim_last_distance2,paim_last_reach;   /* the last gate's two sides (status) */
static float paim_last_cosine=-2.0f,paim_last_cone,paim_last_min_distance;
static void *paim_target,*paim_target_for;    /* the assist's one aim call: the slot's target for that controller */
static void *paim_turn_for,*paim_turn_target; /* the gimbal turn after one of our aims: that controller, the slot's target */
/* Slot 34 (the HUD): each leased gun's last aim, by slot and gun index; the controller is checked on reading. */
typedef struct {const void *controller;uint64_t tick;unsigned char locked;} paim_lock_state;
static paim_lock_state paim_locks[PILOT_SLOTS][PFIRE_GUNS];
static volatile LONG64 paim_calls,paim_aimed,paim_rest,paim_other,paim_foreign,paim_samples,paim_sample_failures,paim_manual_samples,
    paim_no_scope,paim_manual_calls,paim_manual_answers,paim_target_calls,paim_target_answers,paim_assist_samples,paim_assisted,
    paim_assist_missed,paim_cone_hits,paim_cone_misses,paim_cone_unread,paim_class_refused,paim_lock_refused,paim_config_unread,
    paim_via_calls,paim_via_answers,paim_lock_calls,paim_lock_answers,paim_tracked,paim_track_far,paim_track_fresh;
/* Per slot (frame thread): manual points given, cone hits, X4's aims on the target that answered true. */
static uint64_t paim_slot_points[PILOT_SLOTS],paim_slot_cones[PILOT_SLOTS],paim_slot_locks[PILOT_SLOTS];
static const char *paim_why="-";
static int paim_script_on;
static float paim_script_point[3];
/* 1 while the presentation of this client's own guns (lockstep_pilot_aim_present.h) asks the rules below: the counters and the
 * status's last values are the simulation's and stay the same on every client. */
static int paim_quiet;
static void paimp_note_sample(unsigned slot,const float aim[3],unsigned char steer);   /* lockstep_pilot_aim_present.h */
static unsigned char paimp_locked_answer(void *controller,uintptr_t from,int *answered);
static void paimp_restore(void);

static int paim_pins(void) {
    if(!paim_pins_checked) {
        paim_pins_checked=1;
        for(unsigned i=0;i<sizeof(paim_pins_list)/sizeof(*paim_pins_list);i++)
            if(!hash_ok(image+paim_pins_list[i].rva,paim_pins_list[i].size,paim_pins_list[i].sha)) paim_pins_checked=-1;
        if(!pfire_pins() || !pstk_fn.transform) paim_pins_checked=-1;
    }
    return paim_pins_checked>0;
}
/* The player's side of X4's aim assist decision (7e86fc..7e8728): the option's mode (3: the other word's), mouse steering. */
static int paim_assist_rule(uint32_t mode,uint32_t other,unsigned char steering) {
    if(mode==3u) mode=other;
    return mode==1u || (mode==2u && !steering);
}
static int paim_assist_wanted(unsigned char steering) {
    uint32_t mode=0,other=0;
    if(paim_assist_override) return paim_assist_override>0;
    if(!read_local(pnat_base+PAIM_ASSIST_MODE,&mode,4) || !read_local(pnat_base+PAIM_ASSIST_OTHER,&other,4)) return 0;
    return paim_assist_rule(mode,other,steering);
}
static unsigned char *paim_gun_at(const void *controller,unsigned *slot,unsigned *index) {
    for(unsigned s=0;s<pilot_slot_count && s<PILOT_SLOTS;s++) {
        const pfire_slot *f=&pfire_slots[s];
        for(unsigned g=0;f->leased && g<f->count && g<PFIRE_GUNS;g++)
            if(f->guns[g].controller==controller && !f->guns[g].foreign) {*slot=s+1;*index=g;return f->guns[g].object;}
    }
    return NULL;
}
static unsigned char *paim_gun_of(const void *controller,unsigned *slot) {unsigned index=0;return paim_gun_at(controller,slot,&index);}
static int paim_lease_of(const void *controller,unsigned *slot) {return paim_gun_of(controller,slot)!=NULL;}
/* The config X4's aim reads (*0x6cf1538): the cone's cosine and the lock's minimum distance. */
static int paim_config(float *cone,float *min_distance) {
    unsigned char *config=NULL;
    if(!read_local(pnat_base+PAIM_RANGE_SCALE,&config,8) || !config || !read_local(config+PAIM_CONE,cone,4) ||
       !read_local(config+PAIM_MIN_DISTANCE,min_distance,4) || !isfinite(*cone) || !isfinite(*min_distance)) {
        if(!paim_quiet) InterlockedIncrement64(&paim_config_unread);
        return 0;
    }
    if(!paim_quiet) {paim_last_cone=*cone;paim_last_min_distance=*min_distance;}
    return 1;
}
/* X4's range gate for a targeted gun (Weapon::Move 813f45..8140dd), asked the same way. */
static int paim_in_range(unsigned char *target,unsigned char *gun) {
    unsigned char *vtable=NULL,*scale=NULL;paim_range_fn range=NULL;float factor=0.0f;double now=0.0;
    if(paim_range_override) return paim_range_override>0;
    if(!gun || !paim_fn.distance || !read_local(gun,&vtable,8) || !vtable || !read_local(vtable+PAIM_RANGE_SLOT,&range,8) || !range ||
       !read_local(pnat_base+PAIM_RANGE_SCALE,&scale,8) || !scale || !read_local(scale+0x190,&factor,4) || !pfire_now(&now)) return 0;
    const float distance2=paim_fn.distance(target,gun,NULL,now,1);
    const float reach=range(gun,0)*factor;
    if(!paim_quiet) {paim_last_distance2=distance2;paim_last_reach=reach;}
    return isfinite(distance2) && isfinite(reach) && distance2<reach*reach;
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
/* The slot's target: its player's soft target, a live object, not the slot's own ship. */
static unsigned char *paim_slot_target(unsigned slot) {
    unsigned char *target=NULL;
    if(slot<1 || slot>PILOT_SLOTS) return NULL;
    if(paim_target_override) target=paim_target_override(slot);
    else {const uint64_t id=pfire_slot_target(slot);target=id?(unsigned char*)pfire_find(id):NULL;}
    return target && target!=pilot_slots[slot-1].object?target:NULL;
}
/* X4's aim assist flag of the target's class (7e8734..7e8744: 7ad890(macro) +0x318). */
static int paim_class_ok(unsigned char *target) {
    unsigned char *macro=NULL,*data=NULL,flag=0;
    if(!paim_fn.class_of || !read_local(target+0x60,&macro,8) || !macro || !(data=paim_fn.class_of(macro))) return 0;
    return read_local(data+PAIM_CLASS_FLAG,&flag,1) && flag;
}
/* The slot's target for the aim assist of one of its guns: armed, X4's class flag, and in the gun's range where X4 asks for
 * that (ranged: Weapon::Move's gate without manual aim; X4's manual aim has none). */
static unsigned char *paim_assist_target(unsigned slot,unsigned char *gun,int ranged) {
    if(!paim_assist_on) return NULL;
    unsigned char *target=paim_slot_target(slot);
    if(!target) return NULL;
    if(!paim_class_ok(target)) {if(!paim_quiet) InterlockedIncrement64(&paim_class_refused);return NULL;}
    if(ranged && !paim_in_range(target,gun)) {if(!paim_quiet) InterlockedIncrement64(&paim_out_of_range);return NULL;}
    return target;
}
static float paim_dot(const float a[3],const float b[3]) {return a[0]*b[0]+(a[1]*b[1]+a[2]*b[2]);}   /* X4's order: x + (y + z) */
/* X4's cone (7e8883..7e8a15): the cosine between the directions from the slot's player P_k to the point q and to the target's
 * aim point nearest the gun, both in the asked frame (space), each normalized with an exact root after adding PAIM_EPSILON
 * to its squared length, clamped to [-1, 1] (a NaN to -1, as minss/maxss do), above the config's cone. */
static int paim_cone(unsigned slot,const float q[3],unsigned char *target,void *gun,void *space,float cone,float *cosine) {
    const unsigned char *player=uirep_slot_player(slot);const float *point=NULL;float t[16],o[16],at[3],a[3],b[3];
    if(!player || !paim_fn.point || !pstk_fn.transform || !(point=paim_fn.point(target,gun))) {if(!paim_quiet) InterlockedIncrement64(&paim_cone_unread);return 0;}
    pstk_identity(t);pstk_fn.transform(target,t,space,0);pstk_apply(t,point,at);
    pstk_identity(o);pstk_fn.transform((void*)player,o,space,0);
    for(unsigned i=0;i<3;i++) {a[i]=q[i]-o[i];b[i]=at[i]-o[i];}
    const float na=1.0f/sqrtf(paim_dot(a,a)+PAIM_EPSILON),nb=1.0f/sqrtf(paim_dot(b,b)+PAIM_EPSILON);
    for(unsigned i=0;i<3;i++) {a[i]*=na;b[i]*=nb;}
    float c=paim_dot(a,b);
    c=c>1.0f?1.0f:c;
    c=c>-1.0f?c:-1.0f;
    *cosine=c;
    if(!paim_quiet) paim_last_cosine=c;
    return c>cone;
}
/* X4's lock (7e8a42..7e8ae3): yes, unless the target's vtable +0x1bf0 says yes, it has no aim targets and its squared
 * distance 3e1930(target, gun, 0, now, 2) is below the square of the minimum distance (a NaN: no). */
static int paim_lock_ok(unsigned char *target,void *gun,float min_distance) {
    unsigned char *vtable=NULL,*macro=NULL,*data=NULL;paim_large_fn large=NULL;void *aims=NULL;double now=0.0;
    if(!read_local(target,&vtable,8) || !vtable || !read_local(vtable+PAIM_LARGE_SLOT,&large,8) || !large) return 0;
    if(!large(target)) return 1;
    if(!paim_fn.macro_of || !read_local(target+0x60,&macro,8) || !macro || !(data=paim_fn.macro_of(macro)) ||
       !read_local(data+PAIM_AIM_TARGETS,&aims,8)) return 0;
    if(aims) return 1;
    if(!paim_fn.distance || !pfire_now(&now)) return 0;
    return paim_fn.distance(target,gun,NULL,now,2)>=min_distance*min_distance;
}
/* X4's lock path (7e8a42..7e8b6b) for one of our controllers: the lock decided (kept for slot 34), then X4's aim on the
 * target with the lead, slot 17 answering the target for that call; without the lock and without a12 (U::Weapon's vtable
 * +0x1f50: no) X4's false, outputs untouched. The lead as X4's player gets it: a10 = 0 (Weapon::Move passes "not a player's
 * controller", slot 16, which is X4's player's no; with it X4's lead 7e7460 scales the time of flight by the target's
 * evasion, 7d5fc0, which also writes the target's +0xafc/+0xb00). X4's other difference between the two classes there,
 * slot 20 (Mindless yes: wait +0x48 s before the target's motion is read again; the player's no), is none for a lease:
 * 0x819480 builds its Mindless with +0x48 = 0 (7e5620), so the motion is read at every aim as for X4's player. */
static unsigned char paim_lock(paim_lock_state *lock,unsigned slot,void *controller,unsigned char *target,void *gun,float *first,
                               float *second,void *space,void *weapon,void *transform,float a7,float a8,unsigned char a9,
                               unsigned char a10,unsigned char a11,unsigned char a12,unsigned char a13) {
    float cone=0.0f,min_distance=0.0f;
    const int locked=paim_config(&cone,&min_distance) && paim_lock_ok(target,gun,min_distance);
    lock->locked=(unsigned char)locked;
    if(!locked) InterlockedIncrement64(&paim_lock_refused);
    if(!locked && !a12) return 0;
    (void)a10;
    paim_target=target;paim_target_for=controller;
    const unsigned char result=paim_original(controller,first,second,space,weapon,transform,a7,a8,a9,0,a11,a12,a13);
    paim_target_for=NULL;paim_target=NULL;
    InterlockedIncrement64(result?&paim_assisted:&paim_assist_missed);
    if(result) ++paim_slot_locks[slot-1];
    return result;
}
/* An aim of ours answered true: Weapon::Move asks slot 18 next (8141ef), for the gimbal's turn. */
static unsigned char paim_turn(unsigned slot,void *controller,unsigned char result) {
    if(result) {paim_turn_for=controller;paim_turn_target=paim_slot_target(slot);}
    return result;
}
/* Slot 15: manual aim? Yes with the record's manual bit, and with its assist bit while the slot has an assist target in
 * the gun's range (the aim is then asked whatever the range). */
static unsigned char paim_manual_get(void *controller) {
    float p[3];unsigned char steer=0;
    InterlockedIncrement64(&paim_manual_calls);
    unsigned slot=paim_asked(controller,p,&steer);
    if(slot && ((steer&PAIM_STEER_MANUAL) || ((steer&PAIM_STEER_ASSIST) && paim_assist_target(slot,paim_gun_of(controller,&slot),1)))) {
        InterlockedIncrement64(&paim_manual_answers);return 1;
    }
    return paim_manual_original(controller);
}
/* Slot 17: the slot's target inside the assist's own aim call; X4's answer (none) at any other time. */
static void **paim_target_get(void *controller,void **out,void *weapon) {
    InterlockedIncrement64(&paim_target_calls);
    if(controller && out && controller==paim_target_for && GetCurrentThreadId()==ui_thread) {
        *out=paim_target;InterlockedIncrement64(&paim_target_answers);return out;
    }
    return paim_target_original(controller,out,weapon);
}
/* Slot 18: the slot's target at the gimbal's turn (from = the caller's return address), once, for the controller whose aim
 * just answered true; X4's answer (slot 17's: none) at every other call. */
static void **paim_via_answer(void *controller,void **out,void *weapon,uintptr_t from) {
    InterlockedIncrement64(&paim_via_calls);
    if(controller && out && controller==paim_turn_for && from==(uintptr_t)(image+PAIM_TURN_RETURN) && GetCurrentThreadId()==ui_thread) {
        void *target=paim_turn_target;paim_turn_for=NULL;paim_turn_target=NULL;
        if(target) {*out=target;InterlockedIncrement64(&paim_via_answers);return out;}
    }
    return paim_via_original(controller,out,weapon);
}
__attribute__((noinline)) static void **paim_via_get(void *controller,void **out,void *weapon) {
    return paim_via_answer(controller,out,weapon,(uintptr_t)__builtin_return_address(0));
}
/* Slot 34: locked? For one of our guns whether its aim locked at this tick or the one before (X4 keeps its flag until the
 * next aim); X4's answer (no) otherwise. */
static unsigned char paim_locked_answer(void *controller,uintptr_t from) {
    unsigned slot=0,index=0;int answered=0;
    InterlockedIncrement64(&paim_lock_calls);
    const unsigned char shown=paimp_locked_answer(controller,from,&answered);   /* this client's own guns, as it sees them */
    if(answered) return shown;
    if(paim_armed && paim_gun_at(controller,&slot,&index)) {
        const paim_lock_state *l=&paim_locks[slot-1][index];const uint64_t now=stats.issued;
        if(l->controller==controller && l->locked && l->tick<=now && now-l->tick<PAIM_LOCK_FRESH) {InterlockedIncrement64(&paim_lock_answers);return 1;}
    }
    return paim_lock_original(controller);
}
__attribute__((noinline)) static unsigned char paim_locked_get(void *controller) {
    return paim_locked_answer(controller,(uintptr_t)__builtin_return_address(0));
}
/* The dots. X4 draws a small dot for each of its player's guns where that gun points: along its barrel (the gimbal as it
 * is), as far as the nearer of the barrel ray's hit and the target (81d700: min(slot 11, slot 13)), in the in-range colour
 * while the target is in the gun's range (81bbc0: slot 13 against vtable +0x1ee8). With the aim assist locked the dots sit
 * on the lead marker: the aim dots missing in the user report of 24 Nov. The render asks each gun
 * (Weapon vfuncs 693/695 through U::Weapon +0x2088, 81bb50), and X4's predicate wants the controller's slot 16 to say
 * "a player's" (with the gun intact and shown and the HUD's options): a leased gun's Mindless says no, so in a session
 * no player ever had dots, slot 1 neither. X4's PlayerShootController keeps the two distances (+0xa8 from Weapon::Move's
 * ray along the barrel, +0xac the target's distance, both set only for a player's controller, slot 9), Mindless answers
 * 1 m for both. Here, presentation alone, on the frame thread, for a leased gun of this client's own slot:
 *   U::Weapon +0x2088 asked by the render: X4's predicate, slot 16 saying yes for that controller in that one call;
 *   slot 13 at the dot's place and colour: the target's distance from the gun now (X4's 3e1930 mode 1, as Weapon::Move
 *     asks it for the player's target), the root of FLT_MAX without a target (X4's own value then);
 *   slot 11 at the dot's place: the gun's range, the barrel ray's distance when it hits nothing: the ray (a physics query)
 *     is not cast outside the simulation, so a dot whose barrel meets something nearer than the target stays at the
 *     target's distance.
 * Not here: X4's pulse when the target comes into range (81bdf1: only for its player's class, type 6, reading +0xb0,
 * which a Mindless controller does not have). Every other ask is X4's own: Weapon::Move's (the simulation), another
 * slot's gun (as X4's other ships' guns: no dot), the AI's.
 * Slot 16 also says yes at the HUD's lead marker (815330, the UI's alone: GetRelativeAimOffset/ScreenPosition through the
 * player ship's vfunc 0x2210 on its active group's first gun) for a leased gun: with no it took the AI's lead (the target's
 * evasion, paim_lock) and wrote the target's evasion clock on this client alone, from the presentation. */
static unsigned paim_own_slot(void) {return paim_own_slot_override?paim_own_slot_override():pstk_capture_slot;}
/* One of our leased guns of this client's own slot, by its controller: the gun, or NULL. */
static unsigned char *paim_own_gun(const void *controller) {
    unsigned slot=0,index=0;unsigned char *gun=paim_armed && controller?paim_gun_at(controller,&slot,&index):NULL;
    return gun && slot==paim_own_slot()?gun:NULL;
}
static float paim_gun_range(void *gun) {
    unsigned char *vtable=NULL;paim_range_fn range=NULL;
    if(paim_gun_range_override) return paim_gun_range_override(gun);
    if(!gun || !read_local(gun,&vtable,8) || !vtable || !read_local(vtable+PAIM_RANGE_SLOT,&range,8) || !range) return 1.0f;
    return range(gun,0);
}
/* Slot 16: yes for this client's own gun inside X4's dot predicate, and for a leased gun at the HUD's lead marker. */
static unsigned char paim_player_answer(void *controller,uintptr_t from) {
    if(controller && GetCurrentThreadId()==ui_thread) {
        if(controller==paim_dot_for && from==(uintptr_t)(image+PAIM_DOT_ASK_RETURN)) return 1;
        unsigned slot=0;
        if(from==(uintptr_t)(image+PAIM_HUD_LEAD_RETURN) && paim_armed && paim_lease_of(controller,&slot)) {
            InterlockedIncrement64(&paim_hud_leads);return 1;
        }
    }
    return paim_player_original(controller);
}
__attribute__((noinline)) static unsigned char paim_player_get(void *controller) {
    return paim_player_answer(controller,(uintptr_t)__builtin_return_address(0));
}
/* Slots 13 (target_distance) and 11 at the dot's place and colour for this client's own gun; X4's 1 m elsewhere. */
static float paim_distance_answer(void *controller,int target_distance,uintptr_t from) {
    unsigned char *gun=NULL;
    const int site=target_distance?from==(uintptr_t)(image+PAIM_DOT_TARGET_RETURN) || from==(uintptr_t)(image+PAIM_DOT_COLOUR_RETURN):
                                   from==(uintptr_t)(image+PAIM_DOT_REACH_RETURN);
    if(!site || GetCurrentThreadId()!=ui_thread || !(gun=paim_own_gun(controller)))
        return (target_distance?paim_distance_original:paim_reach_original)(controller);
    InterlockedIncrement64(&paim_dot_distances);
    if(!target_distance) return paim_gun_range(gun);
    unsigned char *target=paim_slot_target(paim_own_slot());double now=0.0;
    if(!target || !paim_fn.distance || !pfire_now(&now)) return PAIM_DOT_FAR;
    const float d2=paim_fn.distance(target,gun,NULL,now,1);
    return isfinite(d2) && d2>=0.0f?sqrtf(d2):PAIM_DOT_FAR;
}
__attribute__((noinline)) static float paim_reach_get(void *controller) {
    return paim_distance_answer(controller,0,(uintptr_t)__builtin_return_address(0));
}
__attribute__((noinline)) static float paim_target_distance_get(void *controller) {
    return paim_distance_answer(controller,1,(uintptr_t)__builtin_return_address(0));
}
/* U::Weapon +0x2088 asked by the render for this client's own leased gun: X4's predicate with slot 16's yes. */
static unsigned char paim_dot_answer(void *weapon,uintptr_t from) {
    if(weapon && (from==(uintptr_t)(image+PAIM_DOT_RENDER) || from==(uintptr_t)(image+PAIM_DOT_RENDER2))) {
        unsigned char *controller=NULL;
        if(GetCurrentThreadId()!=ui_thread) InterlockedIncrement64(&paim_dot_elsewhere);
        else if(read_local((unsigned char*)weapon+PFIRE_CONTROLLER,&controller,8) && controller && paim_own_gun(controller)==weapon) {
            InterlockedIncrement64(&paim_dot_asks);
            paim_dot_for=controller;
            const unsigned char shown=paim_dot_original(weapon);
            paim_dot_for=NULL;
            if(shown) InterlockedIncrement64(&paim_dot_shown);
            return shown;
        }
    }
    return paim_dot_original(weapon);
}
__attribute__((noinline)) static unsigned char paim_dot_get(void *weapon) {
    return paim_dot_answer(weapon,(uintptr_t)__builtin_return_address(0));
}
/* Slot 21: the aim, in X4's order (7e8650). */
static unsigned char paim_aim(void *controller,float *first,float *second,void *space,void *weapon,void *transform,float a7,float a8,
                              unsigned char a9,unsigned char a10,unsigned char a11,unsigned char a12,unsigned char a13) {
    unsigned slot=0,index=0;float p[3],t[16],q[4]={0.0f,0.0f,0.0f,0.0f};unsigned char steer=0,*gun=NULL;
    InterlockedIncrement64(&paim_calls);
    paim_turn_for=NULL;paim_turn_target=NULL;
    if(!paim_armed || !(gun=paim_gun_at(controller,&slot,&index))) {
        InterlockedIncrement64(&paim_other);return paim_original(controller,first,second,space,weapon,transform,a7,a8,a9,a10,a11,a12,a13);
    }
    if(GetCurrentThreadId()!=ui_thread) {
        InterlockedIncrement64(&paim_foreign);return paim_original(controller,first,second,space,weapon,transform,a7,a8,a9,a10,a11,a12,a13);
    }
    paim_lock_state *lock=&paim_locks[slot-1][index];
    lock->controller=controller;lock->tick=stats.issued;lock->locked=0;   /* X4 clears its lock at each aim (a13) */
    void *const asked=weapon?weapon:gun;
    unsigned char *ship=pilot_slots[slot-1].object;
    if(!first || !second || !space || !ship || !paim_record(slot,p,&steer) || !(steer&(PAIM_STEER_MANUAL|PAIM_STEER_ASSIST))) {
        InterlockedIncrement64(&paim_rest);return paim_original(controller,first,second,space,weapon,transform,a7,a8,a9,a10,a11,a12,a13);
    }
    if(!(steer&PAIM_STEER_MANUAL)) {   /* the aim assist without manual aim: the lock on a target in the gun's range */
        unsigned char *target=paim_assist_target(slot,gun,1);
        if(!target) {
            InterlockedIncrement64(&paim_rest);return paim_original(controller,first,second,space,weapon,transform,a7,a8,a9,a10,a11,a12,a13);
        }
        return paim_turn(slot,controller,paim_lock(lock,slot,controller,target,asked,first,second,space,weapon,transform,a7,a8,a9,a10,a11,a12,a13));
    }
    pstk_identity(t);pstk_fn.transform(ship,t,space,0);pstk_apply(t,p,q);
    if(!isfinite(q[0]) || !isfinite(q[1]) || !isfinite(q[2])) {
        InterlockedIncrement64(&paim_rest);return paim_original(controller,first,second,space,weapon,transform,a7,a8,a9,a10,a11,a12,a13);
    }
    if(steer&PAIM_STEER_ASSIST) {   /* manual aim with the aim assist: the lock while the point is in the cone */
        unsigned char *target=paim_assist_target(slot,gun,0);float cone=0.0f,min_distance=0.0f,cosine=0.0f;
        if(target && paim_config(&cone,&min_distance)) {
            if(paim_cone(slot,q,target,asked,space,cone,&cosine)) {
                InterlockedIncrement64(&paim_cone_hits);++paim_slot_cones[slot-1];
                return paim_turn(slot,controller,paim_lock(lock,slot,controller,target,asked,first,second,space,weapon,transform,a7,a8,a9,a10,a11,a12,a13));
            }
            InterlockedIncrement64(&paim_cone_misses);
        }
    }
    memcpy(first,q,16);memcpy(second,q,16);InterlockedIncrement64(&paim_aimed);++paim_slot_points[slot-1];
    return paim_turn(slot,controller,1);
}
/* The track (capturing client, manual aim, not scripted): the record's point is applied pilot_delay ticks later, on the
 * simulated ships of that tick, while the player saw the world of the capture tick: its own ship drawn ahead by the
 * own-ship prediction (h_eff of its slot, lockstep_own_predict.h) and every other object at the capture tick (another
 * player's ship ahead by its h_eff with the prediction of the other slots). A point aimed at or near a moving target (the
 * slot's, inside X4's cone around it as seen) would trail it by the target's motion over the delay (24 Nov: about 1.7
 * degrees on a ship crossing at 300 m/s 1 km out). So the point is turned, about the ship, by the rotation that takes
 * the target's direction as drawn into its direction at the apply tick, both from the motion of the last tick (the
 * target's and the own ship's positions in the zone of the player, one tick apart). Only the record changes: the replay
 * is the same on every client whatever this computes. */
typedef struct {uint64_t tick;unsigned slot;const void *target,*zone,*ship;float own[3],at[3];} paim_track_state;
static paim_track_state paim_track_last;
/* v turned by the rotation that takes the direction of a into the direction of b (both nonzero). */
static void paim_turn_by(const float a[3],const float b[3],const float v[3],float out[3]) {
    const float na=1.0f/sqrtf(paim_dot(a,a)),nb=1.0f/sqrtf(paim_dot(b,b));float x[3],y[3],k[3];
    for(unsigned i=0;i<3;i++) {x[i]=a[i]*na;y[i]=b[i]*nb;}
    k[0]=x[1]*y[2]-x[2]*y[1];k[1]=x[2]*y[0]-x[0]*y[2];k[2]=x[0]*y[1]-x[1]*y[0];   /* sin * axis */
    const float s2=paim_dot(k,k),c=paim_dot(x,y);
    if(!(s2>1e-14f) || !(c>0.0f)) {memcpy(out,v,12);return;}
    const float kv[3]={k[1]*v[2]-k[2]*v[1],k[2]*v[0]-k[0]*v[2],k[0]*v[1]-k[1]*v[0]},f=(1.0f-c)/s2,kd=paim_dot(k,v);
    for(unsigned i=0;i<3;i++) out[i]=v[i]*c+kv[i]+k[i]*kd*f;   /* Rodrigues: v cos + (k x v) + k (k.v)(1 - cos)/sin^2 */
}
/* The display lead (ticks) of a slot's ship on this client: the own-ship prediction's h_eff, 0 without it. */
static double paim_drawn_ahead(unsigned slot,int own) {
    if(!opred_armed || opred_mode!=OPRED_PRESENT || slot<1 || slot>PILOT_SLOTS || (!own && !opred_others)) return 0.0;
    const opred_slot *o=&opred_slots[slot-1];
    return o->enabled && isfinite(o->h_eff) && o->h_eff>0.0?o->h_eff:0.0;
}
static unsigned paim_slot_of_ship(const void *object) {
    for(unsigned s=0;s<pilot_slot_count && s<PILOT_SLOTS;s++) if(object && pilot_slots[s].object==object) return s+1;
    return 0;
}
static void paim_track(unsigned slot,float aim[3]) {
    unsigned char *ship=slot>=1 && slot<=PILOT_SLOTS?pilot_slots[slot-1].object:NULL,*target=paim_slot_target(slot),*zone=NULL;
    const unsigned char *player=uirep_slot_player(slot);float s[16],o[16],cone=0.0f,min_distance=0.0f;
    if(!ship || !target || !player || !pstk_fn.zone || !pstk_fn.transform || !(zone=pstk_fn.zone((unsigned char*)player))) {paim_track_last.tick=0;return;}
    pstk_identity(s);pstk_fn.transform(ship,s,zone,0);pstk_identity(o);pstk_fn.transform(target,o,zone,0);
    paim_track_state now={stats.issued,slot,target,zone,ship,{s[0],s[1],s[2]},{o[0],o[1],o[2]}};
    const paim_track_state last=paim_track_last;paim_track_last=now;
    if(!last.tick || last.tick+1!=now.tick || last.slot!=slot || last.target!=target || last.zone!=zone || last.ship!=ship) {InterlockedIncrement64(&paim_track_fresh);return;}
    const double d=(double)pilot_delay,own=paim_drawn_ahead(slot,1),other=paim_drawn_ahead(paim_slot_of_ship(target),0);
    float drawn[3],then[3],w[3],ds[3],dt[3],a[3],b[3],v[3],r[3];
    for(unsigned i=0;i<3;i++) {
        ds[i]=now.own[i]-last.own[i];dt[i]=now.at[i]-last.at[i];
        drawn[i]=(float)((double)now.at[i]+other*dt[i]-((double)now.own[i]+own*ds[i]));   /* the target as drawn, from the ship as drawn */
        then[i]=(float)((double)now.at[i]+d*dt[i]-((double)now.own[i]+d*ds[i]));          /* ...at the apply tick */
    }
    pstk_apply(s,aim,w);
    for(unsigned i=0;i<3;i++) v[i]=w[i]-s[i];   /* the point from the ship, zone axes */
    if(!(paim_dot(drawn,drawn)>1.0f) || !(paim_dot(then,then)>1.0f) || !(paim_dot(v,v)>1.0f) || !paim_config(&cone,&min_distance)) return;
    const float nv=1.0f/sqrtf(paim_dot(v,v)),nd=1.0f/sqrtf(paim_dot(drawn,drawn));
    for(unsigned i=0;i<3;i++) {a[i]=v[i]*nv;b[i]=drawn[i]*nd;}
    if(!(paim_dot(a,b)>cone)) {InterlockedIncrement64(&paim_track_far);return;}
    paim_turn_by(drawn,then,v,r);
    /* back into the ship's frame: its axes are orthonormal, so the transpose */
    const float x=r[0]*s[4]+r[1]*s[5]+r[2]*s[6],y=r[0]*s[8]+r[1]*s[9]+r[2]*s[10],z=r[0]*s[12]+r[1]*s[13]+r[2]*s[14];
    if(!isfinite(x) || !isfinite(y) || !isfinite(z)) return;
    aim[0]=x;aim[1]=y;aim[2]=z;InterlockedIncrement64(&paim_tracked);
}
/* The capturing client's aim of this frame for its slot's record: 1 sampled, 0 not readable, -1 no focus. */
static int paim_take(unsigned slot,float aim[3],unsigned char *steer) {
    if(!paim_armed) return slot==1?pstk_aim_take(aim,steer):0;
    if(paim_script_on) {
        memcpy(aim,paim_script_point,12);
        *steer=paim_script_on==2?PAIM_STEER_ASSIST:1u|PAIM_STEER_MANUAL;
        if(paim_script_on==1 && paim_assist_wanted(1)) *steer|=PAIM_STEER_ASSIST;   /* as a player with mouse steering */
        InterlockedIncrement64(&paim_samples);
        if(*steer&PAIM_STEER_MANUAL) InterlockedIncrement64(&paim_manual_samples);
        if(*steer&PAIM_STEER_ASSIST) InterlockedIncrement64(&paim_assist_samples);
        paimp_note_sample(slot,aim,*steer);
        return 1;
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
    if(paim_assist_wanted(*steer&1u)) {*steer|=PAIM_STEER_ASSIST;InterlockedIncrement64(&paim_assist_samples);}
    paimp_note_sample(slot,aim,*steer);   /* as sampled: the presentation shows it now, the record carries it pilot_delay ticks */
    if(*steer&PAIM_STEER_MANUAL) paim_track(slot,aim);
    InterlockedIncrement64(&paim_samples);
    return 1;
}
static void **paim_entry(unsigned i) {return (void**)(pnat_base+paim_entries[i].vtable+paim_entries[i].slot);}
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
    paim_lock_original=(paim_manual_fn)(void*)(image+PAIM_NOT_MANUAL);
    paim_target_original=(paim_target_fn)(void*)(image+PAIM_NO_TARGET);
    paim_via_original=(paim_target_fn)(void*)(image+PAIM_TARGET_VIA);
    paim_player_original=(paim_manual_fn)(void*)(image+PAIM_NOT_MANUAL);
    paim_reach_original=(paim_float_fn)(void*)(image+PAIM_ONE);
    paim_distance_original=(paim_float_fn)(void*)(image+PAIM_ONE);
    paim_dot_original=(paim_manual_fn)(void*)(image+PAIM_DOT_ASKED);
    paim_original=(pstk_aim_fn)(void*)(image+PAIM_MINDLESS_AIM);
    paim_fn.point=(const float*(*)(void*,void*))(void*)(image+PAIM_AIM_POINT);
    paim_fn.class_of=(unsigned char*(*)(void*))(void*)(image+PAIM_CLASS_OF);
    paim_fn.macro_of=(unsigned char*(*)(void*))(void*)(image+PAIM_MACRO_OF);
    paim_fn.distance=(paim_distance_fn)(void*)(image+PAIM_DISTANCE);
    memset(paim_locks,0,sizeof(paim_locks));paim_track_last.tick=0;
    for(unsigned i=0;i<PAIM_ENTRIES;i++) {
        if(!pnat_swap8(paim_entry(i),(void*)(image+paim_entries[i].target),paim_entries[i].ours,1)) {paim_uninstall();paim_why=paim_entries[i].why;return 0;}
        paim_entry_installed[i]=1;
    }
    paim_assist_on=assist!=0;paim_armed=1;paim_why="armed";return 1;
}
static void paim_restore(void) {
    paimp_restore();
    paim_uninstall();
    paim_target_for=NULL;paim_target=NULL;paim_turn_for=NULL;paim_turn_target=NULL;paim_dot_for=NULL;
    paim_armed=0;paim_assist_on=0;paim_script_on=paim_script_fire=0;
}
static int paim_status(char *out,size_t cap) {
    char installed[PAIM_ENTRIES+1];
    for(unsigned i=0;i<PAIM_ENTRIES;i++) installed[i]=paim_entry_installed[i]?'1':'0';
    installed[PAIM_ENTRIES]=0;
    int n=snprintf(out,cap,"armed=%d why=%s installed=%s assist_on=%d aim=%lld/%lld/%lld/%lld/%lld manual=%lld/%lld target=%lld/%lld "
        "assist=%lld/%lld/%lld gate=%.0f/%.0f samples=%lld/%lld/%lld/%lld no_scope=%lld script=%d cone=%lld/%lld/%lld cos=%.4f cfg=%.4f/%.0f "
        "lock=%lld/%lld/%lld class=%lld turn=%lld/%lld track=%lld/%lld/%lld config_unread=%lld dot=%lld/%lld/%lld/%lld lead=%lld own=%u",
        paim_armed,paim_why,installed,paim_assist_on,
        (long long)paim_calls,(long long)paim_aimed,(long long)paim_rest,(long long)paim_other,
        (long long)paim_foreign,(long long)paim_manual_calls,(long long)paim_manual_answers,(long long)paim_target_calls,
        (long long)paim_target_answers,(long long)paim_assisted,(long long)paim_assist_missed,(long long)paim_out_of_range,
        (double)sqrtf(paim_last_distance2>0.0f?paim_last_distance2:0.0f),(double)paim_last_reach,(long long)paim_samples,
        (long long)paim_manual_samples,(long long)paim_assist_samples,(long long)paim_sample_failures,(long long)paim_no_scope,
        paim_script_on+8*paim_script_fire,(long long)paim_cone_hits,(long long)paim_cone_misses,(long long)paim_cone_unread,
        (double)paim_last_cosine,(double)paim_last_cone,(double)paim_last_min_distance,(long long)paim_lock_refused,
        (long long)paim_lock_answers,(long long)paim_lock_calls,(long long)paim_class_refused,(long long)paim_via_answers,
        (long long)paim_via_calls,(long long)paim_tracked,(long long)paim_track_far,(long long)paim_track_fresh,(long long)paim_config_unread,
        (long long)paim_dot_asks,(long long)paim_dot_shown,(long long)paim_dot_distances,(long long)paim_dot_elsewhere,(long long)paim_hud_leads,
        paim_own_slot());
    /* per slot with any aim: s<k>=manual points/cone hits/X4's aims on the target */
    for(unsigned s=0;n>0 && (size_t)n<cap && s<PILOT_SLOTS;s++)
        if(paim_slot_points[s] || paim_slot_cones[s] || paim_slot_locks[s])
            n+=snprintf(out+n,cap-(size_t)n," s%u=%llu/%llu/%llu",s+1,(unsigned long long)paim_slot_points[s],(unsigned long long)paim_slot_cones[s],
                        (unsigned long long)paim_slot_locks[s]);
    return n;
}
/* gate.pilot_aim(1[,assist]): arm; assist 1 = the aim assist on the slot's target as well. */
static int lua_pilot_aim(lua_State *L) {
    const int ok=accept_ui() && p_lua_tonumber(L,1)==1.0 && paim_arm(p_lua_tonumber(L,2)==1.0);
    p_lua_pushnumber(L,ok);p_lua_pushlstring(L,paim_why,strlen(paim_why));return 2;
}
static int lua_pilot_aim_script(lua_State *L) {
    const double on=p_lua_tonumber(L,1),x=p_lua_tonumber(L,2),y=p_lua_tonumber(L,3),z=p_lua_tonumber(L,4),fire=p_lua_tonumber(L,5);
    const int ok=accept_ui() && paim_armed && (on==0.0 || ((on==1.0 || on==2.0 || on==3.0) && isfinite(x) && isfinite(y) && isfinite(z)));
    if(ok) {   /* 1: a manual aim at the point (with the assist bit as X4's option gives it); 2: the aim assist alone; 3: manual only */
        paim_script_on=(int)on;paim_script_fire=paim_script_on && fire==1.0;
        paim_script_point[0]=(float)x;paim_script_point[1]=(float)y;paim_script_point[2]=(float)z;
    }
    p_lua_pushnumber(L,ok);return 1;
}
static int lua_pilot_aim_status(lua_State *L) {
    char out[1536];int n=paim_status(out,sizeof(out));
    if(n>(int)sizeof(out)-1) n=(int)sizeof(out)-1;
    p_lua_pushlstring(L,out,(size_t)(n>0?n:0));return 1;
}
