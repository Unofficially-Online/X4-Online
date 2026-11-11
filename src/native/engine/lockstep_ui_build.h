/* UI replay families 'build' and 'assets', DEFAULT OFF (native_gate_ui_replay lists 'build' and/or 'assets'; they need
 * native_gate_ui_guard, whose hooks they use, and the replay's runtime IDs; stop reason 164 like the replay). Report:
 * reports/LOCKSTEP-UI-BUILD-20261101.txt. Lua half: tools/dev/native_gate_ui_build_core.lua.
 *
 *   claims    the guard (lockstep_ui_guard.h) hooks every inventoried entry already. These families claim some of its FFI
 *             entries by name (uibuild_claims): in a local phase a claimed call is encoded into this client's open batch
 *             instead of being refused, the menu gets a predicted answer and X4's function does not run. Simulation, MD,
 *             the probe's own frame and the apply pass untouched (the guard's rule).
 *   batch     every claimed call one client's UI makes between two probe frames (a click handler runs inside one frame),
 *             together with the Lua C globals the Lua core adds (gate.ui_build_add: money inside a commit scope,
 *             SetComponentName, a repair signal), is one batch. An event of another family closes the open batch first
 *             (uirep_push), so every event of a slot stays in call order. A batch goes out as parts (op 0x50, at most
 *             UIBUILD_PART_DATA bytes each) through the replay's queue, all of them or none; every client runs it when its
 *             last part applies, in the probe's frame, canonical, record by record (Lua records by the Lua core).
 *   answers   what the menu reads in the same frame: void nothing; bool true; a call that has a check-only flag answers
 *             with X4's own answer to the same call as a query (PerformCrewExchange2, AssignHiredActor,
 *             TakeShipFromInternalStorage); AddBuildTask6 a token that only SetBuildTaskTransferredMoney takes (in the
 *             same batch it becomes the real build task ID at the apply, and is skipped when X4 answered 0);
 *             ReserveBuildPlot 0 (the map then selects nothing; a PayBuildPlotSize(0) in the same batch takes the new
 *             plot); a PayBuildPlotSize right behind ExtendBuildPlot runs only if that answered true at the apply, and
 *             with the plot's size and centre read at the apply (the menu read them before the extension ran).
 *   slots     coop: every slot (the player faction's wallet and ships are shared). Factions mode: slot 1 only (slot k's
 *             faction account and ownership are not adapted yet). Mods from the inventory: X4 takes the mod's parts
 *             from "the player" (the eleven mod exports read the universe's player singleton), so slot k's record
 *             runs inside slot k's scope (lockstep_mission_owners.h mpp_call_enter, missions per player's call scope:
 *             the singleton is slot k's own player, the parts its own inventory's); without missions per player
 *             slot 1 only. Refused at capture (nothing ran anywhere, a row and the
 *             info line); a batch that breaks the rule anyway is skipped record by record by every client alike.
 *   wire      batch = version | record count | records; record = kind ('N' a claimed call, 'L' a Lua record) | id |
 *             flags (1: runs only if the record before answered nonzero) | length u16 | payload. Strings are kept once
 *             per batch (0xff literal, 0xfe NULL, else the index of an earlier literal). Components are tagged: 0 zero,
 *             1 runtime ID + portable check (UIREP_REF), 2 the answer of an earlier record of the batch.
 *   receipt   UIBUILD_1:tick:batches:hash folds every completed batch and every record's answer; joins the pilot receipt.
 * Fail closed: a claim the guard table does not hold, a pin that differs, a stock struct layout that differs (Lua checks
 * the cdefs), a malformed batch, references that differ, a fault in a replayed call: arming refused or stop 164. */
/* UIBUILD_OP_PART (0x50, a Lua-run op) is defined in lockstep_ui_replay.h, whose section check names it. */
#define UIBUILD_PART_HEAD 3u                               /* seq, index, count */
#define UIBUILD_PART_DATA (UIREP_EVENT_MAX-UIBUILD_PART_HEAD)
#define UIBUILD_PARTS_MAX 48u                              /* of UIREP_QUEUE: room for the slot's other events */
#define UIBUILD_BATCH_MAX (UIBUILD_PARTS_MAX*UIBUILD_PART_DATA)
#define UIBUILD_VERSION 1u
#define UIBUILD_HEAD 2u
#define UIBUILD_RECORD_HEAD 5u
#define UIBUILD_RECORDS_MAX 32u
#define UIBUILD_STRINGS_MAX 250u
#define UIBUILD_STRING_MAX 255u
#define UIBUILD_ARRAY_MAX 255u
#define UIBUILD_ARGS 12u
#define UIBUILD_RING 32u
#define UIBUILD_LUA_MAX 4u                                 /* Lua records 1..4: native_gate_ui_build_core.lua K.LUA */
#define UIBUILD_TOKEN 0x5842554c44000000ULL                /* predicted build task IDs: | batch sequence << 8 | record */
#define UIBUILD_STR_NEW 0xffu
#define UIBUILD_STR_NULL 0xfeu
#define UIBUILD_FLAG_AFTER 1u                              /* 'N': runs only if the record before answered nonzero */
#define UIBUILD_FLAG_SCOPED 2u                             /* 'L': made inside a construction scope (factions: the slot's scope) */
enum {UIBUILD_REF_ZERO=0,UIBUILD_REF_ID=1,UIBUILD_REF_RESULT=2};
enum {UIBUILD_RET_VOID=0,UIBUILD_RET_BOOL,UIBUILD_RET_U64,UIBUILD_RET_REASON,UIBUILD_RET_STRUCT_REASON};
enum {UIBUILD_PRED_VOID=0,UIBUILD_PRED_TRUE,UIBUILD_PRED_CHECK,UIBUILD_PRED_TOKEN,UIBUILD_PRED_BIND};
enum {UIBUILD_SLOTS_ANY=0,UIBUILD_SLOTS_INVENTORY,UIBUILD_SLOTS_INVENTORY_ARG};
#define UIBUILD_FAMILIES (UIREP_FAMILY_BUILD|UIREP_FAMILY_ASSETS)
/* Argument specs, one letter per C argument (the hidden struct pointer counts): h hidden struct pointer, I component, i
 * component or 0, B component or 0 = the plot of this batch's ReserveBuildPlot, t a build task token of this batch, u 64
 * bits, b a byte, s string, z string or NULL, f float in a register, F float on the stack, P UIPosRot*, C Coord3D*,
 * L UILoadout2*, T CrewTransferInfo2*, X AddBuildTask6Container* or NULL, G GenericActor*, N NPCSeed* with its count
 * next (n), R CrewTransferContainer* with its count next (n), k the check-only flag (0 at the apply), Q a holomap: its
 * construction plan, carried whole (lockstep_ui_construct.h; the apply runs the commit there, not X4's entry). */
typedef struct {const char *name;unsigned family;const char *spec;unsigned char ret,pred,slots,arg;const char *after;} uibuild_claim;
static const uibuild_claim uibuild_claims[]={
    /* ship building, upgrade, repair at shipyards, wharfs and equipment docks (menu_ship_configuration.lua buttonConfirm) */
    {"AddBuildTask6",UIREP_FAMILY_BUILD,"IizLuTbzX",UIBUILD_RET_U64,UIBUILD_PRED_TOKEN,UIBUILD_SLOTS_ANY,0,NULL},
    {"SetBuildTaskTransferredMoney",UIREP_FAMILY_BUILD,"tu",UIBUILD_RET_VOID,UIBUILD_PRED_VOID,UIBUILD_SLOTS_ANY,0,NULL},
    /* plots and the construction's settings (menu_map.lua, menu_station_configuration.lua) */
    {"ReserveBuildPlot",UIREP_FAMILY_BUILD,"IzzPFFF",UIBUILD_RET_U64,UIBUILD_PRED_BIND,UIBUILD_SLOTS_ANY,0,NULL},
    {"PayBuildPlotSize",UIREP_FAMILY_BUILD,"BCC",UIBUILD_RET_VOID,UIBUILD_PRED_VOID,UIBUILD_SLOTS_ANY,0,"ExtendBuildPlot"},
    {"ExtendBuildPlot",UIREP_FAMILY_BUILD,"ICCb",UIBUILD_RET_BOOL,UIBUILD_PRED_TRUE,UIBUILD_SLOTS_ANY,0,NULL},
    {"RemoveBuildPlot2",UIREP_FAMILY_BUILD,"Ib",UIBUILD_RET_BOOL,UIBUILD_PRED_TRUE,UIBUILD_SLOTS_ANY,0,NULL},
    {"RemoveBuildStorage",UIREP_FAMILY_BUILD,"I",UIBUILD_RET_BOOL,UIBUILD_PRED_TRUE,UIBUILD_SLOTS_ANY,0,NULL},
    {"SetStationModuleLoadoutLevel",UIREP_FAMILY_BUILD,"If",UIBUILD_RET_VOID,UIBUILD_PRED_VOID,UIBUILD_SLOTS_ANY,0,NULL},
    /* crew and pilots (menu_map.lua crew transfer, hiring, the info submenu) */
    {"PerformCrewExchange2",UIREP_FAMILY_ASSETS,"hIINnNnuubk",UIBUILD_RET_STRUCT_REASON,UIBUILD_PRED_CHECK,UIBUILD_SLOTS_ANY,0,NULL},
    {"AssignHiredActor",UIREP_FAMILY_ASSETS,"GIzzk",UIBUILD_RET_REASON,UIBUILD_PRED_CHECK,UIBUILD_SLOTS_ANY,0,NULL},
    {"ReassignPeople",UIREP_FAMILY_ASSETS,"IRn",UIBUILD_RET_VOID,UIBUILD_PRED_VOID,UIBUILD_SLOTS_ANY,0,NULL},
    {"RemovePerson",UIREP_FAMILY_ASSETS,"Iu",UIBUILD_RET_VOID,UIBUILD_PRED_VOID,UIBUILD_SLOTS_ANY,0,NULL},
    {"ReleasePersonFromCrewTransfer",UIREP_FAMILY_ASSETS,"Iu",UIBUILD_RET_VOID,UIBUILD_PRED_VOID,UIBUILD_SLOTS_ANY,0,NULL},
    /* ships: claim, sell, storage (menu_interactmenu.lua, menu_map.lua, menu_platformundock.lua) */
    {"ClaimShip",UIREP_FAMILY_ASSETS,"II",UIBUILD_RET_BOOL,UIBUILD_PRED_TRUE,UIBUILD_SLOTS_ANY,0,NULL},
    {"SellPlayerShip",UIREP_FAMILY_ASSETS,"II",UIBUILD_RET_VOID,UIBUILD_PRED_VOID,UIBUILD_SLOTS_ANY,0,NULL},
    {"PutShipIntoStorage",UIREP_FAMILY_ASSETS,"II",UIBUILD_RET_VOID,UIBUILD_PRED_VOID,UIBUILD_SLOTS_ANY,0,NULL},
    {"TakeShipFromInternalStorage",UIREP_FAMILY_ASSETS,"Ibk",UIBUILD_RET_BOOL,UIBUILD_PRED_CHECK,UIBUILD_SLOTS_ANY,0,NULL},
    {"RequestShipFromInternalStorage2",UIREP_FAMILY_ASSETS,"Ibi",UIBUILD_RET_BOOL,UIBUILD_PRED_TRUE,UIBUILD_SLOTS_ANY,0,NULL},
    /* equipment mods (menu_ship_configuration.lua): the mod comes from (or goes back to) the player's inventory */
    {"InstallWeaponMod",UIREP_FAMILY_ASSETS,"Is",UIBUILD_RET_BOOL,UIBUILD_PRED_TRUE,UIBUILD_SLOTS_INVENTORY,0,NULL},
    {"InstallEngineMod",UIREP_FAMILY_ASSETS,"Is",UIBUILD_RET_BOOL,UIBUILD_PRED_TRUE,UIBUILD_SLOTS_INVENTORY,0,NULL},
    {"InstallShieldMod",UIREP_FAMILY_ASSETS,"Iizs",UIBUILD_RET_BOOL,UIBUILD_PRED_TRUE,UIBUILD_SLOTS_INVENTORY,0,NULL},
    {"InstallGroupedWeaponMod",UIREP_FAMILY_ASSETS,"Iizs",UIBUILD_RET_BOOL,UIBUILD_PRED_TRUE,UIBUILD_SLOTS_INVENTORY,0,NULL},
    {"InstallShipMod",UIREP_FAMILY_ASSETS,"Is",UIBUILD_RET_BOOL,UIBUILD_PRED_TRUE,UIBUILD_SLOTS_INVENTORY,0,NULL},
    {"InstallPaintMod",UIREP_FAMILY_ASSETS,"Isb",UIBUILD_RET_BOOL,UIBUILD_PRED_TRUE,UIBUILD_SLOTS_INVENTORY_ARG,2,NULL},
    {"DismantleWeaponMod",UIREP_FAMILY_ASSETS,"I",UIBUILD_RET_VOID,UIBUILD_PRED_VOID,UIBUILD_SLOTS_INVENTORY,0,NULL},
    {"DismantleEngineMod",UIREP_FAMILY_ASSETS,"I",UIBUILD_RET_VOID,UIBUILD_PRED_VOID,UIBUILD_SLOTS_INVENTORY,0,NULL},
    {"DismantleShieldMod",UIREP_FAMILY_ASSETS,"Iiz",UIBUILD_RET_VOID,UIBUILD_PRED_VOID,UIBUILD_SLOTS_INVENTORY,0,NULL},
    {"DismantleGroupedWeaponMod",UIREP_FAMILY_ASSETS,"Iiz",UIBUILD_RET_VOID,UIBUILD_PRED_VOID,UIBUILD_SLOTS_INVENTORY,0,NULL},
    {"DismantleShipMod",UIREP_FAMILY_ASSETS,"I",UIBUILD_RET_VOID,UIBUILD_PRED_VOID,UIBUILD_SLOTS_INVENTORY,0,NULL},
    /* station construction (lockstep_ui_construct.h, reports/LOCKSTEP-UI-CONSTRUCT-20261101.txt): the station configuration's
     * plan commit (Q: the holomap's construction plan, carried whole), cancelling a construction (interact menu, user question) */
    {"SetConstructionSequenceFromConstructionMap",UIREP_FAMILY_BUILD,"IQ",UIBUILD_RET_VOID,UIBUILD_PRED_VOID,UIBUILD_SLOTS_ANY,0,NULL},
    {"CancelConstruction",UIREP_FAMILY_BUILD,"Iu",UIBUILD_RET_BOOL,UIBUILD_PRED_TRUE,UIBUILD_SLOTS_ANY,0,NULL},
};
#define UIBUILD_CLAIMS (sizeof(uibuild_claims)/sizeof(*uibuild_claims))
#define UIBUILD_PLAN_CLAIM "SetConstructionSequenceFromConstructionMap"
/* Factions mode, slots 2+: the station construction claims run in the slot's faction scope (the trade family's: X4's
 * player faction = the owner of slot k's ship for the length of the call), so the station, the plot and the money are
 * that faction's. Every other claim stays slot 1's in factions mode. */
static const char *const uibuild_scoped_names[]={UIBUILD_PLAN_CLAIM,"CancelConstruction","SetStationModuleLoadoutLevel","ReserveBuildPlot",
    "ExtendBuildPlot","PayBuildPlotSize","RemoveBuildPlot2","RemoveBuildStorage"};
static int uibuild_claim_scoped(const uibuild_claim *c) {
    for(unsigned i=0;i<sizeof(uibuild_scoped_names)/sizeof(*uibuild_scoped_names);i++) if(!strcmp(c->name,uibuild_scoped_names[i])) return 1;
    return 0;
}
/* The trade family's faction scope and owners (lockstep_ui_trade.h, included after this header). */
static int uitrade_wanted(void);
static int uitrade_scope_enter(unsigned slot);
static int uitrade_scope_leave(void);
static void *uitrade_slot_faction(unsigned slot);
static void *uitrade_owner(void *object);
static void *volatile *uitrade_player_faction(void);
/* Slot k's faction ID (the identity plan's, gate.ui_build): ReserveBuildPlot names its faction by ID. */
#define UIBUILD_FACTION_ID 48u
static char uibuild_faction_id[PILOT_SLOTS][UIBUILD_FACTION_ID+1];
/* The Lua records' families (K.LUA order): TransferPlayerMoneyTo, TransferMoneyToPlayer (inside a build commit scope),
 * SetComponentName (assets), SignalObject 'repairs_initiate' (inside the ship configuration's commit scope). */
static const unsigned uibuild_lua_family[UIBUILD_LUA_MAX+1]={0,UIREP_FAMILY_BUILD,UIREP_FAMILY_BUILD,UIREP_FAMILY_ASSETS,UIREP_FAMILY_BUILD};
/* The stock UI's struct layouts these functions take (the ffi.cdef of menu_ship_configuration.lua and menu_map.lua; the
 * Lua core compares them with ffi.offsetof at the arming). */
typedef struct {const char *ammo,*mode;} uibuild_wsetting;
typedef struct {const char *macro,*type;size_t slot;unsigned char optional;uibuild_wsetting setting;} uibuild_macro2;
typedef struct {const char *macro,*path,*group;uint32_t count;unsigned char optional;uibuild_wsetting setting;} uibuild_group2;
typedef struct {const char *macro;uint32_t amount;unsigned char optional;} uibuild_ammo;
typedef struct {const char *ware;} uibuild_software;
typedef struct {const char *macro;unsigned char optional;} uibuild_vmacro;
typedef struct {const char *role;uint32_t count;unsigned char optional;} uibuild_crewdata;
typedef struct {
    uibuild_macro2 *weapons;uint32_t nweapons;uibuild_macro2 *turrets;uint32_t nturrets;uibuild_macro2 *shields;uint32_t nshields;
    uibuild_macro2 *engines;uint32_t nengines;uibuild_group2 *turretgroups;uint32_t nturretgroups;uibuild_group2 *shieldgroups;uint32_t nshieldgroups;
    uibuild_ammo *ammo;uint32_t nammo;uibuild_ammo *units;uint32_t nunits;uibuild_software *software;uint32_t nsoftware;
    uibuild_vmacro thruster;uint32_t ncrew;uibuild_crewdata *crew;unsigned char hascrewexperience;
} uibuild_loadout2;
typedef struct {const char *role;uint64_t seed;uint32_t amount;int64_t price;} uibuild_xfer2;
typedef struct {uibuild_xfer2 *removed;uint32_t nremoved;uibuild_xfer2 *added;uint32_t nadded;uibuild_xfer2 *transferred;uint32_t ntransferred;} uibuild_xferinfo2;
typedef struct {int32_t id;const char *type;} uibuild_typeid;
typedef struct {uibuild_typeid *blacklists;uint32_t nblacklists;uibuild_typeid *fightrules;uint32_t nfightrules;const char *paint;} uibuild_task6info;
typedef struct {uint64_t entity,personcontrollable,seed;} uibuild_actor;
typedef struct {const char *role;uint64_t seed;uint32_t amount;} uibuild_xfer;
_Static_assert(sizeof(uibuild_macro2)==48 && sizeof(uibuild_group2)==48 && sizeof(uibuild_ammo)==16 && sizeof(uibuild_vmacro)==16,"loadout entries");
_Static_assert(sizeof(uibuild_crewdata)==16 && sizeof(uibuild_loadout2)==184 && __builtin_offsetof(uibuild_loadout2,crew)==168,"UILoadout2");
_Static_assert(sizeof(uibuild_xfer2)==32 && sizeof(uibuild_xferinfo2)==48 && sizeof(uibuild_typeid)==16 && sizeof(uibuild_task6info)==40,"build task");
_Static_assert(sizeof(uibuild_actor)==24 && sizeof(uibuild_xfer)==24,"crew");

typedef uint64_t (*uibuild_call_fn)(uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t);
typedef void (*uibuild_float1_fn)(uint64_t,float);
typedef void *(*uibuild_coord_fn)(void *,uint64_t);   /* Coord3D Get...(UniverseID): the hidden pointer first */
static struct {
    void *(*entry)(unsigned claim);                    /* where a claimed function is (tests: stand-ins) */
    uibuild_coord_fn plot_size,plot_center;           /* GetBuildPlotSize, GetBuildPlotCenterOffset */
} uibuild_fn;
#define UIBUILD_PLOT_SIZE 0x1c9450u
#define UIBUILD_PLOT_CENTER 0x1c9090u
static const struct {unsigned rva,size;const char *sha;} uibuild_pin_table[]={
    {UIBUILD_PLOT_SIZE,0xa4,"6128e55da7880a565acf5cdcf118040db98e9064462b36a03f32c0fdab4cdfdb"},    /* GetBuildPlotSize: hidden pointer, station */
    {UIBUILD_PLOT_CENTER,0x9d,"532c9825a60c284002e7dfa97e90b1cdfb790f3044bfb2efe1255ca7a7dfcc60"},  /* GetBuildPlotCenterOffset */
};
#define UIBUILD_PINS (sizeof(uibuild_pin_table)/sizeof(*uibuild_pin_table))

static int uibuild_armed,uibuild_factions;
static const char *uibuild_arm_why="-";
static unsigned short uibuild_guard_of[UIBUILD_CLAIMS];   /* the guard table's index of each claim */
static const uibuild_claim *uibuild_claim_at[UIGUARD_MAX];
static volatile LONG64 uibuild_captured,uibuild_refused,uibuild_batches_out,uibuild_parts_out,uibuild_dropped,uibuild_parts_in,uibuild_torn,
    uibuild_batches_in,uibuild_records_ran,uibuild_records_skipped,uibuild_lua_records,uibuild_checks;
typedef struct {uint64_t tick;const char *what,*why;} uibuild_refusal;
static uibuild_refusal uibuild_ring[UIBUILD_RING];
static unsigned uibuild_ring_head,uibuild_ring_tail;
/* The plan commit's counts (lockstep_ui_construct.h): captured and refused here, ran / X4 kept a task / skipped at the apply. */
static volatile LONG64 uicons_captured,uicons_refused,uicons_ran,uicons_valid,uicons_skipped,uicons_cid_rewound,uicons_cid_rewinds;
static const char *uicons_last_why="-";

/* ---- the open batch (this client's slot) ---- */
static unsigned char uibuild_open[UIBUILD_BATCH_MAX];
static unsigned uibuild_open_length,uibuild_open_records,uibuild_open_strings;
static unsigned char uibuild_open_claim[UIBUILD_RECORDS_MAX];          /* 1 + claim index, 0 a Lua record */
static unsigned uibuild_open_string_at[UIBUILD_STRINGS_MAX];           /* each literal's place in the pool */
static char uibuild_open_pool[UIBUILD_BATCH_MAX];
static unsigned uibuild_open_pool_length;
static unsigned char uibuild_seq;                                       /* the open batch's sequence */
static int uibuild_flushing;

static void uibuild_note(const char *what,const char *why) {
    InterlockedIncrement64(&uibuild_refused);
    if(!strcmp(what,UIBUILD_PLAN_CLAIM)) {InterlockedIncrement64(&uicons_refused);uicons_last_why=why;}
    if(uibuild_ring_head-uibuild_ring_tail>=UIBUILD_RING) return;
    uibuild_refusal *r=&uibuild_ring[uibuild_ring_head%UIBUILD_RING];r->tick=stats.issued;r->what=what;r->why=why;++uibuild_ring_head;
}
static void uibuild_reset_open(void) {
    uibuild_open_length=uibuild_open_records=uibuild_open_strings=uibuild_open_pool_length=0;memset(uibuild_open_claim,0,sizeof(uibuild_open_claim));
}
/* A record being written: bounds-checked, nothing lands in the open batch until it is whole. */
typedef struct {unsigned char *b;unsigned n,capacity;const char *why;
    unsigned strings;unsigned string_at[UIBUILD_STRINGS_MAX];char *pool;unsigned pool_length;} uibuild_writer;
static unsigned char uibuild_record_buffer[UIBUILD_BATCH_MAX];
static char uibuild_record_pool[UIBUILD_BATCH_MAX];
static void uibuild_put(uibuild_writer *w,const void *data,unsigned n) {
    if(w->why) return;
    if(n>w->capacity-w->n) {w->why="too_long";return;}
    memcpy(w->b+w->n,data,n);w->n+=n;
}
static void uibuild_u8(uibuild_writer *w,unsigned v) {unsigned char b=(unsigned char)v;uibuild_put(w,&b,1);}
static void uibuild_u16(uibuild_writer *w,unsigned v) {unsigned char b[2]={(unsigned char)v,(unsigned char)(v>>8)};uibuild_put(w,b,2);}
static void uibuild_u32(uibuild_writer *w,uint32_t v) {unsigned char b[4];for(unsigned i=0;i<4;i++) b[i]=(unsigned char)(v>>(8*i));uibuild_put(w,b,4);}
static void uibuild_u64(uibuild_writer *w,uint64_t v) {unsigned char b[8];for(unsigned i=0;i<8;i++) b[i]=(unsigned char)(v>>(8*i));uibuild_put(w,b,8);}
/* A C string of the call (read through read_local, a byte at a time where a chunk fails), kept once per batch. */
static int uibuild_read_string(const char *s,char *out,unsigned *length) {
    unsigned n=0;
    while(n<=UIBUILD_STRING_MAX) {
        char chunk[32];unsigned take=32;
        if(!read_local(s+n,chunk,take)) {take=1;if(!read_local(s+n,chunk,1)) return 0;}
        for(unsigned i=0;i<take;i++) {
            if(!chunk[i]) {*length=n;return 1;}
            if(n>=UIBUILD_STRING_MAX) return 0;
            out[n++]=chunk[i];
        }
    }
    return 0;
}
static void uibuild_string(uibuild_writer *w,const char *s,int nullable) {
    if(w->why) return;
    if(!s) {if(nullable) uibuild_u8(w,UIBUILD_STR_NULL);else w->why="string";return;}
    char text[UIBUILD_STRING_MAX+1];unsigned length=0;
    if(!uibuild_read_string(s,text,&length)) {w->why="string";return;}
    for(unsigned i=0;i<w->strings;i++) {
        const char *known=w->pool+w->string_at[i];
        if(strlen(known)==length && !memcmp(known,text,length)) {uibuild_u8(w,i);return;}
    }
    if(w->strings>=UIBUILD_STRINGS_MAX || w->pool_length+length+1>UIBUILD_BATCH_MAX) {w->why="strings";return;}
    w->string_at[w->strings++]=w->pool_length;memcpy(w->pool+w->pool_length,text,length);w->pool[w->pool_length+length]=0;w->pool_length+=length+1;
    uibuild_u8(w,UIBUILD_STR_NEW);uibuild_u8(w,length);uibuild_put(w,text,length);
}
static int uibuild_read(const void *p,void *out,size_t n,uibuild_writer *w) {
    if(w->why) return 0;
    if(!p || !read_local(p,out,n)) {w->why="memory";return 0;}
    return 1;
}
static void uibuild_component(uibuild_writer *w,uint64_t id,int optional) {
    if(!id) {if(optional) uibuild_u8(w,UIBUILD_REF_ZERO);else w->why="unmapped";return;}
    if(!uirep_component(id) || !uirep_check_of(id)) {w->why="unmapped";return;}
    unsigned char ref[UIREP_REF];uirep_put_ref(ref,id);uibuild_u8(w,UIBUILD_REF_ID);uibuild_put(w,ref,UIREP_REF);
}
static unsigned uibuild_count(uint64_t value,uibuild_writer *w) {
    if((uint32_t)value>UIBUILD_ARRAY_MAX) {if(!w->why) w->why="array";return 0;}
    return (uint32_t)value;
}
static void uibuild_macro2_out(uibuild_writer *w,const uibuild_macro2 *array,uint32_t n) {
    uibuild_u16(w,uibuild_count(n,w));
    for(uint32_t i=0;i<n && !w->why;i++) {
        uibuild_macro2 e;if(!uibuild_read(array+i,&e,sizeof(e),w)) return;
        uibuild_string(w,e.macro,1);uibuild_string(w,e.type,1);uibuild_u32(w,(uint32_t)e.slot);uibuild_u8(w,e.optional);
        uibuild_string(w,e.setting.ammo,1);uibuild_string(w,e.setting.mode,1);
        if((uint64_t)e.slot>0xffffffffu) w->why="slot";
    }
}
static void uibuild_group2_out(uibuild_writer *w,const uibuild_group2 *array,uint32_t n) {
    uibuild_u16(w,uibuild_count(n,w));
    for(uint32_t i=0;i<n && !w->why;i++) {
        uibuild_group2 e;if(!uibuild_read(array+i,&e,sizeof(e),w)) return;
        uibuild_string(w,e.macro,1);uibuild_string(w,e.path,1);uibuild_string(w,e.group,1);uibuild_u32(w,e.count);uibuild_u8(w,e.optional);
        uibuild_string(w,e.setting.ammo,1);uibuild_string(w,e.setting.mode,1);
    }
}
static void uibuild_ammo_out(uibuild_writer *w,const uibuild_ammo *array,uint32_t n) {
    uibuild_u16(w,uibuild_count(n,w));
    for(uint32_t i=0;i<n && !w->why;i++) {
        uibuild_ammo e;if(!uibuild_read(array+i,&e,sizeof(e),w)) return;
        uibuild_string(w,e.macro,1);uibuild_u32(w,e.amount);uibuild_u8(w,e.optional);
    }
}
static void uibuild_loadout_out(uibuild_writer *w,const void *p) {
    uibuild_loadout2 l;if(!uibuild_read(p,&l,sizeof(l),w)) return;
    uibuild_macro2_out(w,l.weapons,l.nweapons);uibuild_macro2_out(w,l.turrets,l.nturrets);
    uibuild_macro2_out(w,l.shields,l.nshields);uibuild_macro2_out(w,l.engines,l.nengines);
    uibuild_group2_out(w,l.turretgroups,l.nturretgroups);uibuild_group2_out(w,l.shieldgroups,l.nshieldgroups);
    uibuild_ammo_out(w,l.ammo,l.nammo);uibuild_ammo_out(w,l.units,l.nunits);
    uibuild_u16(w,uibuild_count(l.nsoftware,w));
    for(uint32_t i=0;i<l.nsoftware && !w->why;i++) {uibuild_software e;if(uibuild_read(l.software+i,&e,sizeof(e),w)) uibuild_string(w,e.ware,1);}
    uibuild_string(w,l.thruster.macro,1);uibuild_u8(w,l.thruster.optional);
    uibuild_u16(w,uibuild_count(l.ncrew,w));
    for(uint32_t i=0;i<l.ncrew && !w->why;i++) {
        uibuild_crewdata e;if(!uibuild_read(l.crew+i,&e,sizeof(e),w)) return;
        uibuild_string(w,e.role,1);uibuild_u32(w,e.count);uibuild_u8(w,e.optional);
    }
    uibuild_u8(w,l.hascrewexperience);
}
static void uibuild_xfer2_out(uibuild_writer *w,const uibuild_xfer2 *array,uint32_t n) {
    uibuild_u16(w,uibuild_count(n,w));
    for(uint32_t i=0;i<n && !w->why;i++) {
        uibuild_xfer2 e;if(!uibuild_read(array+i,&e,sizeof(e),w)) return;
        uibuild_string(w,e.role,1);uibuild_u64(w,e.seed);uibuild_u32(w,e.amount);uibuild_u64(w,(uint64_t)e.price);
    }
}
static void uibuild_typeids_out(uibuild_writer *w,const uibuild_typeid *array,uint32_t n) {
    uibuild_u16(w,uibuild_count(n,w));
    for(uint32_t i=0;i<n && !w->why;i++) {
        uibuild_typeid e;if(!uibuild_read(array+i,&e,sizeof(e),w)) return;
        uibuild_u32(w,(uint32_t)e.id);uibuild_string(w,e.type,1);
    }
}
/* A float argument in xmm0..3 as the guard's shared routine saved them, below the integer registers (frame - 0x40 + 16*i). */
static uint32_t uibuild_xmm(const uint64_t *frame,unsigned i) {
    uint32_t v=0;memcpy(&v,(const unsigned char*)frame-0x40+16*i,4);return v;
}
/* The record of one claimed call; returns 0 when it cannot be encoded (w->why). Binding: the token of an earlier
 * AddBuildTask6 of this batch, a 0 plot after ReserveBuildPlot. */
static uint64_t uibuild_tokens[UIBUILD_RECORDS_MAX];
static void uicons_plan_out(uibuild_writer *w,uint64_t container,uint64_t holomap);   /* lockstep_ui_construct.h */
/* ReserveBuildPlot names the plot's faction by ID: in factions mode slot k's "player" is its own faction's ID. */
static const char *uibuild_plot_faction(const uibuild_claim *c,unsigned i,const char *given,uibuild_writer *w) {
    if(strcmp(c->name,"ReserveBuildPlot") || i!=1 || !uibuild_factions || pstk_capture_slot<2 || pstk_capture_slot>PILOT_SLOTS) return given;
    const char *own=uibuild_faction_id[pstk_capture_slot-1];char text[UIBUILD_FACTION_ID+1];unsigned n=0;
    if(!own[0] || !given || !uibuild_read_string(given,text,&n) || n>UIBUILD_FACTION_ID) {w->why="faction";return given;}
    text[n]=0;
    if(!strcmp(text,"player") || !strcmp(text,own)) return own;
    w->why="faction";return given;
}
static int uibuild_encode(const uibuild_claim *c,const uint64_t *frame,uibuild_writer *w) {
    const char *spec=c->spec;
    for(unsigned i=0;spec[i] && !w->why;i++) {
        uint64_t a=uiguard_arg(frame,i);
        switch(spec[i]) {
        case 'h':case 'k':case 'n':break;
        case 'Q':uicons_plan_out(w,uiguard_arg(frame,0),a);break;
        case 'I':uibuild_component(w,a,0);break;
        case 'i':uibuild_component(w,a,1);break;
        case 'B': {
            if(a) {uibuild_component(w,a,0);break;}
            int bound=-1;
            for(unsigned r=uibuild_open_records;r-->0;) {
                unsigned k=uibuild_open_claim[r];
                if(k && uibuild_claims[k-1].pred==UIBUILD_PRED_BIND) {bound=(int)r;break;}
            }
            if(bound<0) uibuild_u8(w,UIBUILD_REF_ZERO);else {uibuild_u8(w,UIBUILD_REF_RESULT);uibuild_u8(w,(unsigned)bound);}
            break;
        }
        case 't': {
            int bound=-1;
            for(unsigned r=0;r<uibuild_open_records;r++) if(uibuild_tokens[r]==a && uibuild_open_claim[r]) bound=(int)r;
            if(bound<0) w->why="token";else {uibuild_u8(w,UIBUILD_REF_RESULT);uibuild_u8(w,(unsigned)bound);}
            break;
        }
        case 'u':uibuild_u64(w,a);break;
        case 'b':uibuild_u8(w,a&0xffu);break;
        case 's':uibuild_string(w,(const char*)(uintptr_t)a,0);break;
        case 'z':uibuild_string(w,uibuild_plot_faction(c,i,(const char*)(uintptr_t)a,w),1);break;
        case 'f':if(i>=4) w->why="float";else uibuild_u32(w,uibuild_xmm(frame,i));break;
        case 'F':uibuild_u32(w,(uint32_t)a);break;
        case 'P': {unsigned char v[24];if(uibuild_read((const void*)(uintptr_t)a,v,24,w)) uibuild_put(w,v,24);break;}
        case 'C': {unsigned char v[12];if(uibuild_read((const void*)(uintptr_t)a,v,12,w)) uibuild_put(w,v,12);break;}
        case 'L':uibuild_loadout_out(w,(const void*)(uintptr_t)a);break;
        case 'T': {
            uibuild_xferinfo2 t;if(!uibuild_read((const void*)(uintptr_t)a,&t,sizeof(t),w)) break;
            uibuild_xfer2_out(w,t.removed,t.nremoved);uibuild_xfer2_out(w,t.added,t.nadded);uibuild_xfer2_out(w,t.transferred,t.ntransferred);
            break;
        }
        case 'X': {
            if(!a) {uibuild_u8(w,0);break;}
            uibuild_task6info x;if(!uibuild_read((const void*)(uintptr_t)a,&x,sizeof(x),w)) break;
            uibuild_u8(w,1);uibuild_typeids_out(w,x.blacklists,x.nblacklists);uibuild_typeids_out(w,x.fightrules,x.nfightrules);uibuild_string(w,x.paint,1);
            break;
        }
        case 'G': {
            uibuild_actor g;if(!uibuild_read((const void*)(uintptr_t)a,&g,sizeof(g),w)) break;
            uibuild_component(w,g.entity,1);uibuild_component(w,g.personcontrollable,1);uibuild_u64(w,g.seed);
            break;
        }
        case 'N': {
            unsigned n=uibuild_count(uiguard_arg(frame,i+1),w);uibuild_u16(w,n);
            for(unsigned e=0;e<n && !w->why;e++) {uint64_t seed=0;if(uibuild_read((const uint64_t*)(uintptr_t)a+e,&seed,8,w)) uibuild_u64(w,seed);}
            break;
        }
        case 'R': {
            unsigned n=uibuild_count(uiguard_arg(frame,i+1),w);uibuild_u16(w,n);
            for(unsigned e=0;e<n && !w->why;e++) {
                uibuild_xfer x;if(!uibuild_read((const uibuild_xfer*)(uintptr_t)a+e,&x,sizeof(x),w)) break;
                uibuild_string(w,x.role,1);uibuild_u64(w,x.seed);uibuild_u32(w,x.amount);
            }
            break;
        }
        default:w->why="spec";break;
        }
    }
    return !w->why;
}
/* A claim that takes from or gives to the player's inventory (a paint mod only when its argument says so). */
static int uibuild_inventory_claim(const uibuild_claim *c,uint64_t arg) {
    return c->slots==UIBUILD_SLOTS_INVENTORY || (c->slots==UIBUILD_SLOTS_INVENTORY_ARG && (arg&0xffu));
}
/* Slot k's own scope around such a record (missions per player's; a test's stand-ins). */
static int mpp_call_enter(unsigned k);static int mpp_call_leave(unsigned k);
static int (*uibuild_own_enter)(unsigned k),(*uibuild_own_leave)(unsigned k);
static volatile LONG64 uibuild_own_scopes;
static int uibuild_slot_refusal(const uibuild_claim *c,unsigned slot,uint64_t arg,const char **why) {
    /* factions mode, slots 2+: only the construction claims, and only with the trade family's faction scope */
    if(slot>=2 && uibuild_factions && !(uibuild_claim_scoped(c) && uitrade_wanted())) {*why="factions";return 1;}
    if(slot>=2 && uibuild_inventory_claim(c,arg) && !mpp_armed) {*why="inventory";return 1;}   /* no scope of its own for the slot */
    return 0;
}
/* Appends a whole record to the open batch, flushing it first when the record does not fit: 0 = refused. */
static int uibuild_flush(const char *reason);
static int uibuild_append(unsigned kind,unsigned id,unsigned flags,const unsigned char *payload,unsigned length,uibuild_writer *strings) {
    if(UIBUILD_RECORD_HEAD+length>UIBUILD_BATCH_MAX-UIBUILD_HEAD) return 0;
    if(uibuild_open_records>=UIBUILD_RECORDS_MAX || uibuild_open_length+UIBUILD_RECORD_HEAD+length>UIBUILD_BATCH_MAX-UIBUILD_HEAD) return -1;
    unsigned char *r=uibuild_open+uibuild_open_length;
    r[0]=(unsigned char)kind;r[1]=(unsigned char)id;r[2]=(unsigned char)flags;r[3]=(unsigned char)length;r[4]=(unsigned char)(length>>8);
    memcpy(r+UIBUILD_RECORD_HEAD,payload,length);uibuild_open_length+=UIBUILD_RECORD_HEAD+length;
    uibuild_open_claim[uibuild_open_records]=(unsigned char)(kind=='N'?id+1:0);uibuild_tokens[uibuild_open_records]=0;++uibuild_open_records;
    if(strings) {
        memcpy(uibuild_open_pool,strings->pool,strings->pool_length);uibuild_open_pool_length=strings->pool_length;
        memcpy(uibuild_open_string_at,strings->string_at,sizeof(unsigned)*strings->strings);uibuild_open_strings=strings->strings;
    }
    return 1;
}
static void uibuild_writer_start(uibuild_writer *w) {
    w->b=uibuild_record_buffer;w->n=0;w->capacity=UIBUILD_BATCH_MAX-UIBUILD_HEAD-UIBUILD_RECORD_HEAD;w->why=NULL;
    w->strings=uibuild_open_strings;memcpy(w->string_at,uibuild_open_string_at,sizeof(unsigned)*uibuild_open_strings);
    w->pool=uibuild_record_pool;memcpy(w->pool,uibuild_open_pool,uibuild_open_pool_length);w->pool_length=uibuild_open_pool_length;
}
/* The claimed call, in a local phase (the guard's rule): 1 = handled, *ret the menu's answer; 0 = the guard decides. */
typedef uint64_t (*uibuild_tramp_fn)(uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t);
static int uibuild_claim_decide(unsigned index,const uint64_t *frame,uint64_t *ret) {
    const uibuild_claim *c=index<UIGUARD_MAX?uibuild_claim_at[index]:NULL;
    if(!c || !uibuild_armed || !(uirep_families&c->family)) return 0;
    const uiguard_entry *e=&uiguard_tab[index];const char *why=NULL;
    uint64_t neutral=0;
    if(c->ret==UIBUILD_RET_REASON) neutral=(uint64_t)(uintptr_t)"x4lan_refused";
    else if(c->ret==UIBUILD_RET_STRUCT_REASON) {
        unsigned char *out=(unsigned char*)(uintptr_t)uiguard_arg(frame,0);
        if(out) {static const char refused[]="x4lan_refused";const char *text=refused;memset(out,0,e->size?e->size:24);memcpy(out,&text,8);}
        neutral=(uint64_t)(uintptr_t)out;
    }
    unsigned slot=pstk_capture_slot;
    if(uibuild_slot_refusal(c,slot,c->slots==UIBUILD_SLOTS_INVENTORY_ARG?uiguard_arg(frame,c->arg):0,&why)) {uibuild_note(c->name,why);*ret=neutral;return 1;}
    /* A plan only for a station of the slot's own faction (coop: the player faction; checked again at the apply). */
    int plan=!strcmp(c->name,UIBUILD_PLAN_CLAIM);
    if(plan) {
        unsigned char *object=uirep_component(uiguard_arg(frame,0));
        void *own=uibuild_factions && slot>=2?uitrade_slot_faction(slot):*uitrade_player_faction();
        if(!object || !own || uitrade_owner(object)!=own) {uibuild_note(c->name,object?"owner":"unmapped");*ret=neutral;return 1;}
    }
    /* What the menu reads at once, decided before anything is queued: the query's answer for a call with a check-only flag. */
    uint64_t answer=neutral;
    if(c->pred==UIBUILD_PRED_CHECK) {
        uint64_t a[UIBUILD_ARGS];
        for(unsigned i=0;i<UIBUILD_ARGS;i++) a[i]=uiguard_arg(frame,i);
        int k=e->checkonly;
        if(k<0 || (unsigned)k>=UIBUILD_ARGS) {uibuild_note(c->name,"checkonly");*ret=neutral;return 1;}
        a[k]=1;
        uibuild_tramp_fn f=(uibuild_tramp_fn)uiguard_tramp[index];
        answer=f(a[0],a[1],a[2],a[3],a[4],a[5],a[6],a[7],a[8],a[9],a[10],a[11]);
        InterlockedIncrement64(&uibuild_checks);
    }
    uibuild_writer w;unsigned flags=0;
    for(int attempt=0;attempt<2;attempt++) {
        uibuild_writer_start(&w);
        if(!uibuild_encode(c,frame,&w)) {uibuild_note(c->name,w.why);*ret=neutral;return 1;}
        flags=0;
        if(c->after && uibuild_open_records) {
            unsigned k=uibuild_open_claim[uibuild_open_records-1];
            if(k && !strcmp(uibuild_claims[k-1].name,c->after)) flags|=UIBUILD_FLAG_AFTER;
        }
        int appended=uibuild_append('N',(unsigned)(c-uibuild_claims),flags,w.b,w.n,&w);
        if(appended>0) break;
        if(appended==0 || attempt || !uibuild_open_records) {uibuild_note(c->name,"too_long");*ret=neutral;return 1;}
        /* the open batch is full: it goes out first, this call starts the next one (a reference into it is lost: refuse) */
        uibuild_flush("full");
        if(strpbrk(c->spec,"tB") || flags) {uibuild_note(c->name,"split");*ret=neutral;return 1;}
    }
    InterlockedIncrement64(&uibuild_captured);
    if(plan) InterlockedIncrement64(&uicons_captured);
    unsigned record=uibuild_open_records-1;
    switch(c->pred) {
    case UIBUILD_PRED_TRUE:*ret=1;break;
    case UIBUILD_PRED_CHECK:*ret=answer;break;
    case UIBUILD_PRED_TOKEN:uibuild_tokens[record]=UIBUILD_TOKEN|((uint64_t)uibuild_seq<<8)|record;*ret=uibuild_tokens[record];break;
    default:*ret=0;break;
    }
    return 1;
}
/* A Lua record from the Lua core (its codec's bytes): 1 = in the open batch. scoped: made inside a construction scope
 * (the money of the budget, a plot, a builder's fee): in factions mode slots 2+ may make those (the slot's faction pays). */
static int uibuild_add_lua(unsigned id,const unsigned char *payload,unsigned length,int scoped,const char **why) {
    if(!uibuild_armed || !id || id>UIBUILD_LUA_MAX || !(uirep_families&uibuild_lua_family[id])) {*why="family";return 0;}
    if(uibuild_factions && pstk_capture_slot>=2 && !(scoped && id<=2 && uitrade_wanted())) {*why="factions";return 0;}
    for(int attempt=0;attempt<2;attempt++) {
        int appended=uibuild_append('L',id,scoped?UIBUILD_FLAG_SCOPED:0,payload,length,NULL);
        if(appended>0) {InterlockedIncrement64(&uibuild_captured);return 1;}
        if(appended==0 || attempt || !uibuild_open_records) break;
        uibuild_flush("full");
    }
    *why="too_long";return 0;
}
/* The open batch goes out as parts, all of them or none (nothing ran anywhere when it is dropped). */
static int uibuild_flush(const char *reason) {
    (void)reason;
    if(!uibuild_open_records || uibuild_flushing) return 0;
    unsigned total=UIBUILD_HEAD+uibuild_open_length,parts=(total+UIBUILD_PART_DATA-1)/UIBUILD_PART_DATA;
    static unsigned char batch[UIBUILD_BATCH_MAX];
    batch[0]=UIBUILD_VERSION;batch[1]=(unsigned char)uibuild_open_records;memcpy(batch+UIBUILD_HEAD,uibuild_open,uibuild_open_length);
    const char *why=NULL;
    if(parts>UIBUILD_PARTS_MAX || total>UIBUILD_BATCH_MAX) why="too_long";
    else if(UIREP_QUEUE-uirep_queue_count<parts) why="queue_full";
    if(why) {uibuild_note("batch",why);InterlockedIncrement64(&uibuild_dropped);uibuild_reset_open();++uibuild_seq;return 0;}
    uibuild_flushing=1;
    unsigned pushed=0;
    for(unsigned p=0;p<parts;p++) {
        unsigned char part[UIREP_EVENT_MAX];unsigned at=p*UIBUILD_PART_DATA,n=total-at<UIBUILD_PART_DATA?total-at:UIBUILD_PART_DATA;
        part[0]=uibuild_seq;part[1]=(unsigned char)p;part[2]=(unsigned char)parts;memcpy(part+UIBUILD_PART_HEAD,batch+at,n);
        if(!uirep_push(UIBUILD_OP_PART,part,UIBUILD_PART_HEAD+n)) break;
        ++pushed;
    }
    uibuild_flushing=0;
    if(pushed!=parts) {uibuild_note("batch",pushed?"torn":"refused");InterlockedIncrement64(&uibuild_dropped);}
    else {InterlockedIncrement64(&uibuild_batches_out);InterlockedExchangeAdd64(&uibuild_parts_out,(LONG64)parts);}
    uibuild_reset_open();++uibuild_seq;
    return pushed==parts;
}
/* uirep_push: an event of another family closes the open batch first (call order across families). */
static void uibuild_before_push(unsigned op) {
    if(op!=UIBUILD_OP_PART && uibuild_open_records && !uibuild_flushing) uibuild_flush("event");
}

/* ---- the wire (every client) ---- */
static int uibuild_event_ok(unsigned slot,unsigned op,const unsigned char *data,unsigned n,const char **why) {
    (void)slot;
    if(op!=UIBUILD_OP_PART || !(uirep_families&UIBUILD_FAMILIES) || n<=UIBUILD_PART_HEAD || !data[2] || data[2]>UIBUILD_PARTS_MAX || data[1]>=data[2]) {*why="ui_build";return 0;}
    return 1;
}
/* ---- apply: reassembly, then the records one by one (the probe's frame, every client) ---- */
enum {UIBUILD_RX_IDLE=0,UIBUILD_RX_PARTS,UIBUILD_RX_READY,UIBUILD_RX_LUA};
typedef struct {
    unsigned char state,seq,count,next;unsigned length;unsigned char buf[UIBUILD_BATCH_MAX];
    unsigned at,record,records;uint64_t results[UIBUILD_RECORDS_MAX];unsigned char ran[UIBUILD_RECORDS_MAX];
    unsigned strings;unsigned string_at[UIBUILD_STRINGS_MAX];char pool[UIBUILD_BATCH_MAX];unsigned pool_length;uint64_t hash;
} uibuild_rx;
static uibuild_rx uibuild_rx_slot[PILOT_SLOTS];
static uint64_t uibuild_frame_tick,uibuild_frame_hash;
static unsigned uibuild_frame_batches;
static __attribute__((aligned(16))) unsigned char uibuild_arena[UIBUILD_BATCH_MAX*4];
static unsigned uibuild_arena_used;
typedef struct {const unsigned char *b;unsigned n,at;const char *why;uibuild_rx *rx;int run;} uibuild_reader;
static unsigned uibuild_g8(uibuild_reader *r) {if(r->why) return 0;if(r->at>=r->n) {r->why="truncated";return 0;}return r->b[r->at++];}
static unsigned uibuild_g16(uibuild_reader *r) {unsigned a=uibuild_g8(r);return a|uibuild_g8(r)<<8;}
static uint32_t uibuild_g32(uibuild_reader *r) {uint32_t v=0;for(unsigned i=0;i<4;i++) v|=(uint32_t)uibuild_g8(r)<<(8*i);return v;}
static uint64_t uibuild_g64(uibuild_reader *r) {uint64_t v=0;for(unsigned i=0;i<8;i++) v|=(uint64_t)uibuild_g8(r)<<(8*i);return v;}
static void uibuild_gbytes(uibuild_reader *r,void *out,unsigned n) {
    if(r->why) return;
    if(n>r->n-r->at) {r->why="truncated";return;}
    if(out) memcpy(out,r->b+r->at,n);
    r->at+=n;
}
static void *uibuild_alloc(uibuild_reader *r,size_t n) {
    size_t need=(n+15)&~(size_t)15;
    if(r->why) return NULL;
    if(!r->run) return uibuild_arena;   /* the check pass only walks */
    if(need>sizeof(uibuild_arena)-uibuild_arena_used) {r->why="arena";return NULL;}
    void *p=uibuild_arena+uibuild_arena_used;uibuild_arena_used+=(unsigned)need;memset(p,0,n);return p;
}
static const char *uibuild_gstring(uibuild_reader *r,int nullable) {
    unsigned tag=uibuild_g8(r);uibuild_rx *x=r->rx;
    if(r->why) return NULL;
    if(tag==UIBUILD_STR_NULL) {if(!nullable) r->why="string";return NULL;}
    if(tag==UIBUILD_STR_NEW) {
        unsigned length=uibuild_g8(r);
        if(r->why || length>r->n-r->at || x->strings>=UIBUILD_STRINGS_MAX || x->pool_length+length+1>sizeof(x->pool)) {if(!r->why) r->why="string";return NULL;}
        x->string_at[x->strings++]=x->pool_length;memcpy(x->pool+x->pool_length,r->b+r->at,length);x->pool[x->pool_length+length]=0;
        const char *s=x->pool+x->pool_length;x->pool_length+=length+1;r->at+=length;return s;
    }
    if(tag>=x->strings) {r->why="string";return NULL;}
    return x->pool+x->string_at[tag];
}
/* A tagged component: 1 resolved (or zero, or an earlier answer), 0 the record cannot run (gone, or what it depends on
 * did not happen; the same on every client), -1 stop (the identities differ). */
static int uibuild_gcomponent(uibuild_reader *r,unsigned record,uint64_t *out,int *gone) {
    unsigned tag=uibuild_g8(r);*out=0;
    if(r->why) return 0;
    if(tag==UIBUILD_REF_ZERO) return 1;
    if(tag==UIBUILD_REF_RESULT) {
        unsigned j=uibuild_g8(r);
        if(!r->why && j>=record) r->why="reference";
        if(r->why || !r->run) return 0;
        *out=r->rx->ran[j]==1?r->rx->results[j]:0;
        if(!*out) *gone=2;
        return 1;
    }
    if(tag!=UIBUILD_REF_ID) {r->why="tag";return 0;}
    unsigned char ref[UIREP_REF];uibuild_gbytes(r,ref,UIREP_REF);
    if(r->why || !r->run) return 0;
    unsigned char *component=NULL;int resolved=uirep_resolve(ref,out,&component);
    if(resolved<0) return -1;
    if(!resolved) {*gone=1;*out=0;}
    return 1;
}
static void *uibuild_gmacro2(uibuild_reader *r,uint32_t *count) {
    unsigned n=uibuild_g16(r);uibuild_macro2 *a=n?uibuild_alloc(r,sizeof(*a)*n):NULL;*count=n;
    for(unsigned i=0;i<n && !r->why;i++) {
        uibuild_macro2 e;memset(&e,0,sizeof(e));
        e.macro=uibuild_gstring(r,1);e.type=uibuild_gstring(r,1);e.slot=uibuild_g32(r);e.optional=(unsigned char)uibuild_g8(r);
        e.setting.ammo=uibuild_gstring(r,1);e.setting.mode=uibuild_gstring(r,1);
        if(r->run && a) a[i]=e;
    }
    return r->run?a:NULL;
}
static void *uibuild_ggroup2(uibuild_reader *r,uint32_t *count) {
    unsigned n=uibuild_g16(r);uibuild_group2 *a=n?uibuild_alloc(r,sizeof(*a)*n):NULL;*count=n;
    for(unsigned i=0;i<n && !r->why;i++) {
        uibuild_group2 e;memset(&e,0,sizeof(e));
        e.macro=uibuild_gstring(r,1);e.path=uibuild_gstring(r,1);e.group=uibuild_gstring(r,1);e.count=uibuild_g32(r);e.optional=(unsigned char)uibuild_g8(r);
        e.setting.ammo=uibuild_gstring(r,1);e.setting.mode=uibuild_gstring(r,1);
        if(r->run && a) a[i]=e;
    }
    return r->run?a:NULL;
}
static void *uibuild_gammo(uibuild_reader *r,uint32_t *count) {
    unsigned n=uibuild_g16(r);uibuild_ammo *a=n?uibuild_alloc(r,sizeof(*a)*n):NULL;*count=n;
    for(unsigned i=0;i<n && !r->why;i++) {
        uibuild_ammo e;memset(&e,0,sizeof(e));
        e.macro=uibuild_gstring(r,1);e.amount=uibuild_g32(r);e.optional=(unsigned char)uibuild_g8(r);
        if(r->run && a) a[i]=e;
    }
    return r->run?a:NULL;
}
static void *uibuild_gxfer2(uibuild_reader *r,uint32_t *count) {
    unsigned n=uibuild_g16(r);uibuild_xfer2 *a=n?uibuild_alloc(r,sizeof(*a)*n):NULL;*count=n;
    for(unsigned i=0;i<n && !r->why;i++) {
        uibuild_xfer2 e;memset(&e,0,sizeof(e));
        e.role=uibuild_gstring(r,1);e.seed=uibuild_g64(r);e.amount=uibuild_g32(r);e.price=(int64_t)uibuild_g64(r);
        if(r->run && a) a[i]=e;
    }
    return r->run?a:NULL;
}
static void *uibuild_gtypeids(uibuild_reader *r,uint32_t *count) {
    unsigned n=uibuild_g16(r);uibuild_typeid *a=n?uibuild_alloc(r,sizeof(*a)*n):NULL;*count=n;
    for(unsigned i=0;i<n && !r->why;i++) {
        uibuild_typeid e;memset(&e,0,sizeof(e));
        e.id=(int32_t)uibuild_g32(r);e.type=uibuild_gstring(r,1);
        if(r->run && a) a[i]=e;
    }
    return r->run?a:NULL;
}
/* The arguments of one claimed call (the check pass walks them, the run builds them): 1, 0 (skip: *gone), -1 stop. */
static int uicons_gplan(uibuild_reader *r);   /* lockstep_ui_construct.h: into uicons_rx_plan */
static int uibuild_decode(const uibuild_claim *c,uibuild_reader *r,unsigned record,uint64_t a[UIBUILD_ARGS],uint32_t *float_bits,int *gone) {
    static __attribute__((aligned(16))) unsigned char hidden[64];
    for(unsigned i=0;c->spec[i] && !r->why;i++) {
        switch(c->spec[i]) {
        case 'h':a[i]=(uint64_t)(uintptr_t)hidden;memset(hidden,0,sizeof(hidden));break;
        case 'Q':uicons_gplan(r);a[i]=0;break;   /* the plan is uicons_rx_plan (the run), the holomap stays this client's */
        case 'k':a[i]=0;break;
        case 'n':break;   /* set by the array in front of it */
        case 'I':case 'i':case 'B':case 't':if(uibuild_gcomponent(r,record,&a[i],gone)<0) return -1;break;
        case 'u':a[i]=uibuild_g64(r);break;
        case 'b':a[i]=uibuild_g8(r);break;
        case 's':a[i]=(uint64_t)(uintptr_t)uibuild_gstring(r,0);break;
        case 'z':a[i]=(uint64_t)(uintptr_t)uibuild_gstring(r,1);break;
        case 'f':*float_bits=uibuild_g32(r);a[i]=*float_bits;break;
        case 'F':a[i]=uibuild_g32(r);break;
        case 'P':{void *p=uibuild_alloc(r,24);uibuild_gbytes(r,r->run?p:NULL,24);a[i]=(uint64_t)(uintptr_t)p;break;}
        case 'C':{void *p=uibuild_alloc(r,12);uibuild_gbytes(r,r->run?p:NULL,12);a[i]=(uint64_t)(uintptr_t)p;break;}
        case 'L': {
            uibuild_loadout2 *l=uibuild_alloc(r,sizeof(*l)),tmp;memset(&tmp,0,sizeof(tmp));
            tmp.weapons=uibuild_gmacro2(r,&tmp.nweapons);tmp.turrets=uibuild_gmacro2(r,&tmp.nturrets);
            tmp.shields=uibuild_gmacro2(r,&tmp.nshields);tmp.engines=uibuild_gmacro2(r,&tmp.nengines);
            tmp.turretgroups=uibuild_ggroup2(r,&tmp.nturretgroups);tmp.shieldgroups=uibuild_ggroup2(r,&tmp.nshieldgroups);
            tmp.ammo=uibuild_gammo(r,&tmp.nammo);tmp.units=uibuild_gammo(r,&tmp.nunits);
            tmp.nsoftware=uibuild_g16(r);tmp.software=tmp.nsoftware?uibuild_alloc(r,sizeof(uibuild_software)*tmp.nsoftware):NULL;
            for(unsigned e=0;e<tmp.nsoftware && !r->why;e++) {const char *ware=uibuild_gstring(r,1);if(r->run && tmp.software) tmp.software[e].ware=ware;}
            tmp.thruster.macro=uibuild_gstring(r,1);tmp.thruster.optional=(unsigned char)uibuild_g8(r);
            tmp.ncrew=uibuild_g16(r);tmp.crew=tmp.ncrew?uibuild_alloc(r,sizeof(uibuild_crewdata)*tmp.ncrew):NULL;
            for(unsigned e=0;e<tmp.ncrew && !r->why;e++) {
                uibuild_crewdata d;memset(&d,0,sizeof(d));d.role=uibuild_gstring(r,1);d.count=uibuild_g32(r);d.optional=(unsigned char)uibuild_g8(r);
                if(r->run && tmp.crew) tmp.crew[e]=d;
            }
            tmp.hascrewexperience=(unsigned char)uibuild_g8(r);
            if(!r->run) {tmp.software=NULL;tmp.crew=NULL;}
            if(r->run && l) *l=tmp;
            a[i]=(uint64_t)(uintptr_t)l;
            break;
        }
        case 'T': {
            uibuild_xferinfo2 *t=uibuild_alloc(r,sizeof(*t)),tmp;memset(&tmp,0,sizeof(tmp));
            tmp.removed=uibuild_gxfer2(r,&tmp.nremoved);tmp.added=uibuild_gxfer2(r,&tmp.nadded);tmp.transferred=uibuild_gxfer2(r,&tmp.ntransferred);
            if(r->run && t) *t=tmp;
            a[i]=(uint64_t)(uintptr_t)t;
            break;
        }
        case 'X': {
            unsigned present=uibuild_g8(r);
            if(r->why || !present) {if(present>1) r->why="tag";a[i]=0;break;}
            uibuild_task6info *x=uibuild_alloc(r,sizeof(*x)),tmp;memset(&tmp,0,sizeof(tmp));
            tmp.blacklists=uibuild_gtypeids(r,&tmp.nblacklists);tmp.fightrules=uibuild_gtypeids(r,&tmp.nfightrules);tmp.paint=uibuild_gstring(r,1);
            if(r->run && x) *x=tmp;
            a[i]=(uint64_t)(uintptr_t)x;
            break;
        }
        case 'G': {
            uibuild_actor *g=uibuild_alloc(r,sizeof(*g)),tmp;memset(&tmp,0,sizeof(tmp));
            if(uibuild_gcomponent(r,record,&tmp.entity,gone)<0 || uibuild_gcomponent(r,record,&tmp.personcontrollable,gone)<0) return -1;
            tmp.seed=uibuild_g64(r);
            if(r->run && g) *g=tmp;
            a[i]=(uint64_t)(uintptr_t)g;
            break;
        }
        case 'N': {
            unsigned n=uibuild_g16(r);uint64_t *seeds=n?uibuild_alloc(r,8*n):NULL;
            if(n>UIBUILD_ARRAY_MAX && !r->why) r->why="array";
            for(unsigned e=0;e<n && !r->why;e++) {uint64_t s=uibuild_g64(r);if(r->run && seeds) seeds[e]=s;}
            a[i]=(uint64_t)(uintptr_t)(r->run?seeds:NULL);if(c->spec[i+1]=='n') a[i+1]=n;else if(!r->why) r->why="spec";
            break;
        }
        case 'R': {
            unsigned n=uibuild_g16(r);uibuild_xfer *x=n?uibuild_alloc(r,sizeof(*x)*n):NULL;
            if(n>UIBUILD_ARRAY_MAX && !r->why) r->why="array";
            for(unsigned e=0;e<n && !r->why;e++) {
                uibuild_xfer d;memset(&d,0,sizeof(d));d.role=uibuild_gstring(r,1);d.seed=uibuild_g64(r);d.amount=uibuild_g32(r);
                if(r->run && x) x[e]=d;
            }
            a[i]=(uint64_t)(uintptr_t)(r->run?x:NULL);if(c->spec[i+1]=='n') a[i+1]=n;else if(!r->why) r->why="spec";
            break;
        }
        default:r->why="spec";break;
        }
    }
    return r->why?-1:1;
}
/* A whole batch, walked without running anything (version, count, every record whole): 0 = malformed (stop). */
static int uibuild_batch_ok(uibuild_rx *x,const char **why) {
    if(x->length<UIBUILD_HEAD || x->buf[0]!=UIBUILD_VERSION || !x->buf[1] || x->buf[1]>UIBUILD_RECORDS_MAX) {*why="ui_build_batch";return 0;}
    unsigned at=UIBUILD_HEAD;x->strings=0;x->pool_length=0;
    for(unsigned record=0;record<x->buf[1];record++) {
        if(x->length-at<UIBUILD_RECORD_HEAD) {*why="ui_build_batch";return 0;}
        unsigned kind=x->buf[at],id=x->buf[at+1],flags=x->buf[at+2],length=x->buf[at+3]|x->buf[at+4]<<8;at+=UIBUILD_RECORD_HEAD;
        if(length>x->length-at || (kind=='N' && flags&~UIBUILD_FLAG_AFTER) || (kind=='L' && flags&~UIBUILD_FLAG_SCOPED)) {*why="ui_build_batch";return 0;}
        if(kind=='L') {if(!id || id>UIBUILD_LUA_MAX || !(uirep_families&uibuild_lua_family[id])) {*why="ui_build_lua";return 0;}}
        else if(kind=='N') {
            if(id>=UIBUILD_CLAIMS || !(uirep_families&uibuild_claims[id].family)) {*why="ui_build_claim";return 0;}
            uibuild_reader r={x->buf+at,length,0,NULL,x,0};uint64_t a[UIBUILD_ARGS]={0};uint32_t bits=0;int gone=0;
            if(uibuild_decode(&uibuild_claims[id],&r,record,a,&bits,&gone)<0 || r.at!=length) {*why="ui_build_record";return 0;}
        } else {*why="ui_build_batch";return 0;}
        at+=length;
    }
    if(at!=x->length) {*why="ui_build_batch";return 0;}
    x->strings=0;x->pool_length=0;
    return 1;
}
/* One part (the probe's frame, every client, in the slot's order): 1 = the batch is whole and checked, 0 = more to come,
 * -1 = dropped (a torn batch: the same parts on every client, so dropped everywhere), -2 = stop (malformed). */
static int uibuild_part(unsigned slot,const unsigned char *data,unsigned n,const char **why) {
    if(slot<1 || slot>PILOT_SLOTS || !uibuild_armed || !uibuild_event_ok(slot,UIBUILD_OP_PART,data,n,why)) {if(!*why) *why="ui_build";return -2;}
    uibuild_rx *x=&uibuild_rx_slot[slot-1];unsigned seq=data[0],index=data[1],count=data[2],length=n-UIBUILD_PART_HEAD;
    InterlockedIncrement64(&uibuild_parts_in);
    if(x->state==UIBUILD_RX_READY || x->state==UIBUILD_RX_LUA) {*why="ui_build_overlap";return -2;}   /* the Lua core runs a batch at once */
    if(index==0) {
        if(x->state==UIBUILD_RX_PARTS) InterlockedIncrement64(&uibuild_torn);
        x->state=UIBUILD_RX_PARTS;x->seq=(unsigned char)seq;x->count=(unsigned char)count;x->next=0;x->length=0;
    } else if(x->state!=UIBUILD_RX_PARTS || seq!=x->seq || index!=x->next || count!=x->count) {
        if(x->state==UIBUILD_RX_PARTS) InterlockedIncrement64(&uibuild_torn);
        x->state=UIBUILD_RX_IDLE;*why="torn";return -1;
    }
    if(length>sizeof(x->buf)-x->length) {*why="ui_build_batch";return -2;}
    memcpy(x->buf+x->length,data+UIBUILD_PART_HEAD,length);x->length+=length;++x->next;
    if(x->next<x->count) return 0;
    if(!uibuild_batch_ok(x,why)) {x->state=UIBUILD_RX_IDLE;return -2;}
    x->state=UIBUILD_RX_READY;x->at=UIBUILD_HEAD;x->record=0;x->records=x->buf[1];x->strings=0;x->pool_length=0;
    memset(x->results,0,sizeof(x->results));memset(x->ran,0,sizeof(x->ran));
    x->hash=pstk_fnv(x->buf,x->length,0x5549425549444231ULL^slot);   /* "UIBUILD1" */
    InterlockedIncrement64(&uibuild_batches_in);
    return 1;
}
static void *uibuild_entry_of(unsigned claim) {
    if(uibuild_fn.entry) return uibuild_fn.entry(claim);
    return pnat_base+uiguard_tab[uibuild_guard_of[claim]].rva;
}
static void uibuild_fold(uibuild_rx *x,uint64_t result,unsigned ran) {
    unsigned char b[9];for(unsigned i=0;i<8;i++) b[i]=(unsigned char)(result>>(8*i));b[8]=(unsigned char)ran;
    x->hash=pstk_fnv(b,9,x->hash);
}
static void uibuild_done(uint64_t tick,uibuild_rx *x) {
    if(uibuild_frame_tick!=tick) {uibuild_frame_tick=tick;uibuild_frame_batches=0;uibuild_frame_hash=0;}
    ++uibuild_frame_batches;uibuild_frame_hash=pstk_fnv((const unsigned char*)&x->hash,8,uibuild_frame_hash^0x5549425549444231ULL);
    x->state=UIBUILD_RX_IDLE;
}
/* The next record of slot `slot`'s ready batch. Returns 1 a claimed call ran or was skipped (what), 2 a Lua record for
 * the Lua core (*lua, *payload, *length, *scope: run it in the slot's faction scope; it answers with uibuild_lua_done),
 * 0 the batch is done, -1 stop (why). */
static uint64_t uicons_apply(unsigned slot,uint64_t container,const char **skip,int *ran);   /* lockstep_ui_construct.h */
static int uibuild_step(unsigned slot,uint64_t tick,char *what,size_t capacity,unsigned *lua,const unsigned char **payload,unsigned *length,const char **why,int *scope) {
    uibuild_rx *x=slot>=1 && slot<=PILOT_SLOTS?&uibuild_rx_slot[slot-1]:NULL;
    if(scope) *scope=0;
    if(!x || x->state!=UIBUILD_RX_READY) {*why="ui_build_step";return -1;}
    if(x->record>=x->records) {snprintf(what,capacity,"batch seq=%u records=%u",x->seq,x->records);uibuild_done(tick,x);return 0;}
    unsigned char *h=x->buf+x->at;unsigned kind=h[0],id=h[1],flags=h[2],n=h[3]|h[4]<<8,record=x->record;
    const unsigned char *data=h+UIBUILD_RECORD_HEAD;x->at+=UIBUILD_RECORD_HEAD+n;
    if(kind=='L') {
        *lua=id;*payload=data;*length=n;x->state=UIBUILD_RX_LUA;
        if(uibuild_factions && slot>=2) {
            if((flags&UIBUILD_FLAG_SCOPED) && id<=2 && uitrade_wanted()) {if(scope) *scope=1;return 2;}   /* the slot's faction pays */
            /* refused at capture; a client that sent one anyway: skipped everywhere */
            x->state=UIBUILD_RX_READY;x->ran[record]=2;uibuild_fold(x,0,2);++x->record;InterlockedIncrement64(&uibuild_records_skipped);
            snprintf(what,capacity,"lua%u skipped=factions",id);return 1;
        }
        return 2;
    }
    const uibuild_claim *c=&uibuild_claims[id];uint64_t a[UIBUILD_ARGS]={0};uint32_t bits=0;int gone=0;
    uibuild_arena_used=0;
    uibuild_reader r={data,n,0,NULL,x,1};
    int decoded=uibuild_decode(c,&r,record,a,&bits,&gone);
    if(decoded<0) {*why=r.why?"ui_build_record":"ui_ref_mismatch";return -1;}
    const char *skip=NULL;
    int plan=!strcmp(c->name,UIBUILD_PLAN_CLAIM),scoped=uibuild_factions && slot>=2;
    if(uibuild_slot_refusal(c,slot,c->slots==UIBUILD_SLOTS_INVENTORY_ARG?a[c->arg]:0,&skip)) {}
    else if(gone==1) skip="gone";
    else if(gone==2) skip="depends";
    else if((flags&UIBUILD_FLAG_AFTER) && (!record || x->ran[record-1]!=1 || !x->results[record-1])) skip="condition";
    else if(!strcmp(c->name,"ReserveBuildPlot")) {   /* the plot's faction: "player", in factions mode slot k's own */
        const char *want=scoped?uibuild_faction_id[slot-1]:"player",*given=(const char*)(uintptr_t)a[1];
        if(!want[0] || !given || strcmp(given,want)) skip="faction";
    }
    /* Factions mode, slots 2+: the call runs with X4's player faction = the slot's (the trade family's scope). */
    if(!skip && scoped) {int s=uitrade_scope_enter(slot);if(s<0) skip="scope";else if(!s) scoped=0;}
    ++x->record;
    if(skip) {
        x->ran[record]=2;uibuild_fold(x,0,2);InterlockedIncrement64(&uibuild_records_skipped);
        if(plan) InterlockedIncrement64(&uicons_skipped);
        snprintf(what,capacity,"%s skipped=%s",c->name,skip);return 1;
    }
    /* The plot's size and centre as they are now: the menu read them before its ExtendBuildPlot ran. */
    if(!strcmp(c->name,"PayBuildPlotSize") && (flags&UIBUILD_FLAG_AFTER) && uibuild_fn.plot_size && uibuild_fn.plot_center) {
        uibuild_fn.plot_size((void*)(uintptr_t)a[1],a[0]);uibuild_fn.plot_center((void*)(uintptr_t)a[2],a[0]);
    }
    void *entry=plan?NULL:uibuild_entry_of(id);uint64_t result=0;int fault=0,plan_ran=0;const char *plan_skip=NULL;
    /* An inventory mod of slots 2+: X4's "the player" is the slot's own for the call. A scope that cannot be entered stops
     * the replay: the record never runs on slot 1's inventory on some clients. */
    const int own=slot>=2 && uibuild_inventory_claim(c,c->slots==UIBUILD_SLOTS_INVENTORY_ARG?a[c->arg]:0);
    if(own) {
        if(!(uibuild_own_enter?uibuild_own_enter:mpp_call_enter)(slot)) {if(scoped) uitrade_scope_leave();*why="ui_build_inventory_scope";return -1;}
        InterlockedIncrement64(&uibuild_own_scopes);
    }
    __try {
        if(plan) result=uicons_apply(slot,a[0],&plan_skip,&plan_ran);   /* X4's commit path with the carried plan */
        else if(strchr(c->spec,'f')) {float v;memcpy(&v,&bits,4);((uibuild_float1_fn)entry)(a[0],v);}
        else result=((uibuild_call_fn)entry)(a[0],a[1],a[2],a[3],a[4],a[5],a[6],a[7],a[8],a[9],a[10],a[11]);
    }
    __except(GetExceptionCode()==EXCEPTION_ACCESS_VIOLATION?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH) {fault=1;}
    if(own && !(uibuild_own_leave?uibuild_own_leave:mpp_call_leave)(slot) && !fault) {*why="ui_build_inventory_scope";return -1;}
    if(scoped && !uitrade_scope_leave() && !fault) {*why="ui_trade_scope";return -1;}
    if(fault) {*why="ui_fault";return -1;}
    if(plan && !plan_ran) {
        x->ran[record]=2;uibuild_fold(x,0,2);InterlockedIncrement64(&uibuild_records_skipped);InterlockedIncrement64(&uicons_skipped);
        snprintf(what,capacity,"%s skipped=%s",c->name,plan_skip?plan_skip:"plan");return 1;
    }
    if(plan) {
        x->results[record]=result;x->ran[record]=1;uibuild_fold(x,result,1);InterlockedIncrement64(&uibuild_records_ran);
        snprintf(what,capacity,"%s task=%u plan=%016llx",c->name,result?1u:0u,(unsigned long long)result);return 1;
    }
    const char *reason=NULL;uint64_t value=1;
    switch(c->ret) {
    case UIBUILD_RET_BOOL:value=result&0xffu;break;
    case UIBUILD_RET_U64:value=result;break;
    case UIBUILD_RET_REASON:reason=(const char*)(uintptr_t)result;value=reason && !reason[0];break;
    case UIBUILD_RET_STRUCT_REASON:memcpy(&reason,(const void*)(uintptr_t)a[0],8);value=reason && !reason[0];break;
    default:value=1;break;
    }
    x->results[record]=value;x->ran[record]=1;uibuild_fold(x,value,1);InterlockedIncrement64(&uibuild_records_ran);
    char text[48]="";
    if(reason) {size_t k=0;for(;reason[k] && k<sizeof(text)-1;k++) text[k]=(char)(reason[k]==' ' || reason[k]==','?'_':reason[k]);text[k]=0;}
    if(c->ret==UIBUILD_RET_REASON || c->ret==UIBUILD_RET_STRUCT_REASON) snprintf(what,capacity,"%s=%s",c->name,text[0]?text:"ok");
    else if(c->ret==UIBUILD_RET_VOID) snprintf(what,capacity,"%s",c->name);
    else snprintf(what,capacity,"%s=%llu",c->name,(unsigned long long)value);
    return 1;
}
static int uibuild_lua_done(unsigned slot,int ok) {
    uibuild_rx *x=slot>=1 && slot<=PILOT_SLOTS?&uibuild_rx_slot[slot-1]:NULL;
    if(!x || x->state!=UIBUILD_RX_LUA) return 0;
    unsigned record=x->record++;x->ran[record]=ok?1:3;x->results[record]=ok!=0;uibuild_fold(x,ok!=0,ok?1:3);
    InterlockedIncrement64(&uibuild_lua_records);x->state=UIBUILD_RX_READY;return 1;
}
static int uicons_cid_on;                           /* lockstep_ui_construct.h: the construction ID counter is kept canonical */
static int uicons_ready;                            /* its pins held, its functions bound */
static volatile uint64_t *uicons_cid_counter(void);
static void uicons_reset(void);
static int uibuild_frame_text(uint64_t tick,char *out,size_t capacity) {
    int n=snprintf(out,capacity,"UIBUILD_1:%llu:%u:%016llx",(unsigned long long)tick,uibuild_frame_tick==tick?uibuild_frame_batches:0u,
        (unsigned long long)(uibuild_frame_tick==tick?uibuild_frame_hash:0));
    if(n>0 && (size_t)n<capacity && uicons_cid_on) n+=snprintf(out+n,capacity-(size_t)n,":C%llx",(unsigned long long)*uicons_cid_counter());
    return n>0 && (size_t)n<capacity?n:0;
}
static int uibuild_refusals_text(char *out,size_t capacity) {
    int n=0;out[0]=0;
    while(uibuild_ring_tail!=uibuild_ring_head) {
        const uibuild_refusal *r=&uibuild_ring[uibuild_ring_tail%UIBUILD_RING];
        int more=snprintf(out+n,capacity-(size_t)n,"%s%llu:%s:%s",n?",":"",(unsigned long long)r->tick,r->what,r->why);
        if(more<0 || (size_t)more>=capacity-(size_t)n) break;
        n+=more;++uibuild_ring_tail;
    }
    return n;
}
static int uibuild_status(char *out,size_t capacity) {
    int n=snprintf(out,capacity,"ui_build=%s families=%x factions=%d claims=%u captured=%lld checks=%lld refused=%lld out=%lld/%lld dropped=%lld open=%u "
        "in=%lld/%lld torn=%lld ran=%lld skipped=%lld lua=%lld",uibuild_armed?"armed":"off",uirep_families&UIBUILD_FAMILIES,uibuild_factions,(unsigned)UIBUILD_CLAIMS,
        (long long)uibuild_captured,(long long)uibuild_checks,(long long)uibuild_refused,(long long)uibuild_batches_out,(long long)uibuild_parts_out,(long long)uibuild_dropped,
        uibuild_open_records,(long long)uibuild_batches_in,(long long)uibuild_parts_in,(long long)uibuild_torn,(long long)uibuild_records_ran,
        (long long)uibuild_records_skipped,(long long)uibuild_lua_records);
    if(n>0 && (size_t)n<capacity)
        n+=snprintf(out+n,capacity-(size_t)n," plan=%lld/%lld/%lld/%lld/%lld why=%s cid=%s/%lld/%lld",(long long)uicons_captured,(long long)uicons_refused,
            (long long)uicons_ran,(long long)uicons_valid,(long long)uicons_skipped,uicons_last_why,uicons_cid_on?"kept":"off",
            (long long)uicons_cid_rewinds,(long long)uicons_cid_rewound);
    return n>0 && (size_t)n<capacity?n:0;
}
/* ---- arming (Lua, after gate.ui_replay and gate.ui_guard, before the first tick) ---- */
static int uibuild_refuse(const char *why) {uibuild_arm_why=why;return 0;}
static int uibuild_pins(void) {
    for(unsigned i=0;i<UIBUILD_PINS;i++) if(!hash_ok(image+uibuild_pin_table[i].rva,uibuild_pin_table[i].size,uibuild_pin_table[i].sha)) {pin_failed_rva=uibuild_pin_table[i].rva;return 0;}
    return 1;
}
static int uibuild_arm_using(int factions) {
    if(uibuild_armed) return uibuild_factions==(factions!=0)?1:uibuild_refuse("rerun");
    if(!uirep_armed || !(uirep_families&UIBUILD_FAMILIES)) return uibuild_refuse("needs_family");
    if(!uiguard_armed) return uibuild_refuse("needs_ui_guard");
    if(uiguard_installed) return uibuild_refuse("after_install");
    static char missing[64];
    for(unsigned c=0;c<UIBUILD_CLAIMS;c++) {
        unsigned found=UIGUARD_MAX;
        for(unsigned i=0;i<uiguard_count && i<UIGUARD_MAX;i++)
            if(uiguard_tab[i].kind==UIGUARD_FFI && !strcmp(uiguard_tab[i].name,uibuild_claims[c].name)) found=i;
        if(found==UIGUARD_MAX) {snprintf(missing,sizeof(missing),"claim:%s",uibuild_claims[c].name);return uibuild_refuse(missing);}
        uibuild_guard_of[c]=(unsigned short)found;
    }
    memset(uibuild_claim_at,0,sizeof(uibuild_claim_at));uiguard_unclaim(uibuild_claim_decide);
    for(unsigned c=0;c<UIBUILD_CLAIMS;c++)
        if(uirep_families&uibuild_claims[c].family) {
            if(!uiguard_claim(uibuild_guard_of[c],uibuild_claim_decide)) {   /* another family's entry: the arming fails, nothing claimed */
                uiguard_unclaim(uibuild_claim_decide);memset(uibuild_claim_at,0,sizeof(uibuild_claim_at));
                snprintf(missing,sizeof(missing),"%s:%s",uiguard_claim_why,uibuild_claims[c].name);return uibuild_refuse(missing);
            }
            uibuild_claim_at[uibuild_guard_of[c]]=&uibuild_claims[c];
        }
    uibuild_factions=factions!=0;uibuild_reset_open();uibuild_seq=0;memset(uibuild_rx_slot,0,sizeof(uibuild_rx_slot));
    uibuild_ring_head=uibuild_ring_tail=0;uibuild_frame_tick=0;uibuild_frame_batches=0;uibuild_frame_hash=0;
    uibuild_captured=uibuild_refused=uibuild_batches_out=uibuild_parts_out=uibuild_dropped=uibuild_parts_in=uibuild_torn=0;
    uibuild_batches_in=uibuild_records_ran=uibuild_records_skipped=uibuild_lua_records=uibuild_checks=0;
    uicons_reset();uicons_cid_on=(uirep_families&UIREP_FAMILY_BUILD) && uicons_ready;
    uibuild_arm_why="-";uibuild_armed=1;return 1;
}
/* Slot k's faction ID, "id1,id2,..." in slot order (letters, digits, '_'): 1, or 0 when malformed (nothing kept). */
static int uibuild_faction_ids(const char *csv) {
    memset(uibuild_faction_id,0,sizeof(uibuild_faction_id));
    for(unsigned slot=0;csv && *csv && slot<PILOT_SLOTS;slot++) {
        const char *end=strchr(csv,',');size_t n=end?(size_t)(end-csv):strlen(csv);
        if(n>UIBUILD_FACTION_ID) {memset(uibuild_faction_id,0,sizeof(uibuild_faction_id));return 0;}
        for(size_t i=0;i<n;i++) {
            char ch=csv[i];
            if(!((ch>='a' && ch<='z') || (ch>='A' && ch<='Z') || (ch>='0' && ch<='9') || ch=='_')) {memset(uibuild_faction_id,0,sizeof(uibuild_faction_id));return 0;}
        }
        memcpy(uibuild_faction_id[slot],csv,n);uibuild_faction_id[slot][n]=0;
        if(!end) break;
        csv=end+1;
    }
    return 1;
}
static int uicons_pins(void);static void uicons_bind_image(void);
static int uibuild_arm(int factions) {
    if(uibuild_armed) return uibuild_arm_using(factions);
    if(stats.issued) return uibuild_refuse("after_first_tick");
    if(GetCurrentThreadId()!=ui_thread) return uibuild_refuse("thread");
    if((uirep_families&UIREP_FAMILY_BUILD) && !uibuild_pins()) {static char pin_why[32];snprintf(pin_why,sizeof(pin_why),"pins_ui_build:%x",pin_failed_rva);return uibuild_refuse(pin_why);}
    /* the station plan commit: every function it calls or mirrors, the layouts it reads (lockstep_ui_construct.h) */
    if((uirep_families&UIREP_FAMILY_BUILD) && !uicons_pins()) {static char pin_why[40];snprintf(pin_why,sizeof(pin_why),"pins_ui_construct:%x",pin_failed_rva);return uibuild_refuse(pin_why);}
    uibuild_fn.entry=NULL;uibuild_fn.plot_size=(uibuild_coord_fn)(void*)(image+UIBUILD_PLOT_SIZE);uibuild_fn.plot_center=(uibuild_coord_fn)(void*)(image+UIBUILD_PLOT_CENTER);
    if(uirep_families&UIREP_FAMILY_BUILD) {uicons_bind_image();uicons_ready=1;}
    return uibuild_arm_using(factions);
}
#include "lockstep_ui_construct.h"
