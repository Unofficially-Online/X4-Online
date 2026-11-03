/* UI replay family 'faction', DEFAULT OFF (native_gate_ui_replay lists 'faction', bit 32768; it needs native_gate_ui_guard,
 * whose hooks it claims; stop reason 164 like the replay). The player faction's settings for every slot, coop and factions
 * mode. Report: reports/LOCKSTEP-UI-FACTION-20261101.txt. Lua half: X4LAN_UI_FACTION in native_gate_ui_replay_core.lua.
 *
 *   claims    the guard (lockstep_ui_guard.h) hooks every inventoried entry. This family claims 33 of its FFI entries by name
 *             (uifac_claims: the faction group, the per-controllable rules and responses of the fleet group, the trade
 *             rules, supply overrides and module pause of the trade group): in a local phase the call becomes one record of
 *             this client's slot, the menu gets a predicted answer, X4's function does not run here. Everything else (MD,
 *             AI, the probe's own frame, an apply) passes as the guard lets it.
 *   records   one claimed call = one record (claim | token | arguments by spec), sent as parts (op 0x5a, all or none) through
 *             the replay's queue; every client reassembles it per slot and runs it at capture tick + delay in the probe's
 *             frame, slot order and call order, through the entry (the guard passes an apply, the trampoline runs X4's).
 *   rule IDs  [F 9.00] blacklists, fight rules and trade rules live in three world managers (0x6cf1850, 0x6cf1878,
 *             0x6cf17d8): a create takes ++counter (the manager's first dword) as the new ID and the rule's owner is
 *             g_PlayerFaction at the call (0x371370, CreateFightRule 0x179285, 0x807760). The same creates in the same order
 *             give the same IDs on every client. The capturing menu reads the ID at once (menu_playerinfo.lua: the defaults
 *             right behind the create, the edited rule keeps it): it gets a token (0x7f000000 | slot << 16 | sequence), and
 *             every client keeps token -> X4's ID when the create runs; any later record of that slot naming the token
 *             (the defaults in the same click, an update, a removal, a controllable's rule) runs with the real ID. A token
 *             nobody created is skipped (token), one whose create answered 0 too (depends): the same everywhere.
 *   faction   coop: every slot is `player`, the shared player faction's settings change for everyone. Factions mode:
 *             each slot's records run in its faction scope (lockstep_ui_trade.h uitrade_scope_enter: g_PlayerFaction = the
 *             owner of the slot's ship for the call), so what X4 does "for the player" (the rule's owner, the faction's
 *             defaults and name) is x4lan_playerk's; a faction ID argument "player" (and "player" in a rule's faction list)
 *             becomes the slot's faction ID, another faction's ID is refused (not_owner).
 *   authority a rule named by ID must be the slot faction's (else not_owner; a rule that is gone: gone); a component subject
 *             (controllable, container, module) is checked by the Lua half (the slot faction's, no other slot's ship).
 *   entity    seven settings live on the player entity (+0x8a6a8..+0x8a720: ships wait, taxi waits, global loadout level,
 *             trade loop cargo reservation, illegal wares; logo, paint and clothing theme through the entity): the
 *             canonical apply changes X4's own player, the one the simulation reads. Coop: every slot; factions mode: only a
 *             slot whose faction is the player faction (slot 1), else refused at capture and skipped at the apply
 *             (player_entity) - unless native_gate_player_settings_per_slot holds slot k's own values
 *             (lockstep_player_settings.h): then slot k's record runs with X4's player holding slot k's values (and its
 *             cover = slot k's faction, for the logo) and the result goes back into slot k's table; the view shows them.
 *   receipt   UIFAC_1:<each manager's counter and size>;<each slot faction's rule counts>;<the player entity's settings>;
 *             <hash over every record run or skipped>: joins the pilot receipt, so a rule or setting that took on one client
 *             only is a digest mismatch at that tick.
 * Fail closed: a claim the guard table does not hold or another family holds, a pin that differs (the managers' code, the
 * entity setters, the trade family's scope pins), a malformed record, references that differ, a fault in a replayed call. */
/* UIFAC_OP_PART (0x5a, a Lua-run op) is defined in lockstep_ui_replay.h, whose section check names it. */
#define UIFAC_PART_HEAD 3u                                /* seq, index, count */
#define UIFAC_PART_DATA (UIREP_EVENT_MAX-UIFAC_PART_HEAD)
#define UIFAC_PARTS_MAX 24u
#define UIFAC_RECORD_MAX (UIFAC_PARTS_MAX*UIFAC_PART_DATA)
#define UIFAC_STRING_MAX 254u                             /* 0xff tags NULL */
#define UIFAC_STR_NULL 0xffu
#define UIFAC_ARRAY_MAX 255u
#define UIFAC_ARGS 6u
#define UIFAC_TOKEN 0x7f000000u
#define UIFAC_TOKEN_MASK 0xff000000u
#define UIFAC_TOKENS 256u
#define UIFAC_RING 32u
#define UIFAC_TREE_MAX 8192u
#define UIFAC_DEPTH_MAX 96u
/* The rule managers (pointers to world singletons) and their layout [F: the listers 0x371280 / 0x807670, the lookups of
 * RemoveBlacklist / RemoveFightRule / RemoveTradeRule]: +0 the ID counter, +0x10 the tree's header (+0x18 root, +0x30
 * leftmost, +0x38 count); a node: +0 parent, +8 left, +0x10 right, +0x20 the ID, +0x28 the rule; a rule: +0 its owner faction. */
enum {UIFAC_K_NONE=0,UIFAC_K_BLACKLIST,UIFAC_K_FIGHT,UIFAC_K_TRADE,UIFAC_KINDS};
static const unsigned uifac_manager_rva[UIFAC_KINDS]={0,0x6cf1850u,0x6cf1878u,0x6cf17d8u};
static const char uifac_kind_letter[UIFAC_KINDS]={'-','b','f','t'};
#define UIFAC_M_HEADER 0x10u
#define UIFAC_M_ROOT 0x18u
#define UIFAC_M_LEFTMOST 0x30u
#define UIFAC_M_SIZE 0x38u
#define UIFAC_N_LEFT 0x8u
#define UIFAC_N_RIGHT 0x10u
#define UIFAC_N_KEY 0x20u
#define UIFAC_N_VALUE 0x28u
/* The player entity's settings [F: the setters 0x1f0e70, 0x1b9980, 0x1dcf40, 0x1b99b0 write [[0x3d198f8]+0x230] + these]. */
#define UIFAC_GAME 0x3d198f8u
#define UIFAC_GAME_PLAYER 0x230u
#define UIFAC_E_SHIPS_WAIT 0x8a6b4u
#define UIFAC_E_TAXI_WAITS 0x8a6b5u
#define UIFAC_E_LOOP_RESERVE 0x8a720u
#define UIFAC_E_LOADOUT 0x8a6a8u
enum {UIFAC_RET_VOID=0,UIFAC_RET_BOOL,UIFAC_RET_ID};
enum {UIFAC_PRED_VOID=0,UIFAC_PRED_TRUE,UIFAC_PRED_TOKEN};
/* Argument specs, one letter per C argument: I a component (the subject when `subject` names it), r a rule ID of the claim's
 * kind (int32; a token, -1 or 0 pass as they are), s a string, F a faction ID string ("player" = the slot's faction),
 * b a bool, f a float in xmm(i), B BlacklistInfo2*, G FightRuleInfo*, T TradeRuleInfo*, L UILogo*, O SupplyOverride* with
 * its count next (n). A struct the stock prototype takes by value arrives by pointer (Win64: larger than 8 bytes). */
typedef struct {const char *name,*spec;unsigned char ret,pred,kind,entity;signed char subject;} uifac_claim;
static const uifac_claim uifac_claims[]={
    /* blacklists (menu_playerinfo.lua buttonBlacklistConfirm / Remove; menu_map.lua the behaviour panel) */
    {"CreateBlacklist2","B",UIFAC_RET_ID,UIFAC_PRED_TOKEN,UIFAC_K_BLACKLIST,0,-1},
    {"UpdateBlacklist2","B",UIFAC_RET_VOID,UIFAC_PRED_VOID,UIFAC_K_BLACKLIST,0,-1},
    {"RemoveBlacklist","r",UIFAC_RET_VOID,UIFAC_PRED_VOID,UIFAC_K_BLACKLIST,0,-1},
    {"SetPlayerBlacklistDefault","rssb",UIFAC_RET_VOID,UIFAC_PRED_VOID,UIFAC_K_BLACKLIST,0,-1},
    {"SetControllableBlacklist","Irsb",UIFAC_RET_VOID,UIFAC_PRED_VOID,UIFAC_K_BLACKLIST,0,0},
    /* fight rules */
    {"CreateFightRule","G",UIFAC_RET_ID,UIFAC_PRED_TOKEN,UIFAC_K_FIGHT,0,-1},
    {"UpdateFightRule","G",UIFAC_RET_VOID,UIFAC_PRED_VOID,UIFAC_K_FIGHT,0,-1},
    {"RemoveFightRule","r",UIFAC_RET_VOID,UIFAC_PRED_VOID,UIFAC_K_FIGHT,0,-1},
    {"SetPlayerFightRuleDefault","rsb",UIFAC_RET_VOID,UIFAC_PRED_VOID,UIFAC_K_FIGHT,0,-1},
    {"SetControllableFightRule","Irsb",UIFAC_RET_VOID,UIFAC_PRED_VOID,UIFAC_K_FIGHT,0,0},
    /* trade rules (the trade family applies existing ones to stations: SetContainerTradeRule) */
    {"CreateTradeRule","T",UIFAC_RET_ID,UIFAC_PRED_TOKEN,UIFAC_K_TRADE,0,-1},
    {"UpdateTradeRule","T",UIFAC_RET_VOID,UIFAC_PRED_VOID,UIFAC_K_TRADE,0,-1},
    {"RemoveTradeRule","r",UIFAC_RET_VOID,UIFAC_PRED_VOID,UIFAC_K_TRADE,0,-1},
    {"SetPlayerTradeRuleDefault","rsb",UIFAC_RET_VOID,UIFAC_PRED_VOID,UIFAC_K_TRADE,0,-1},
    /* default responses to signals: the faction's (faction ID, purpose) and a controllable's override */
    {"SetDefaultResponseToSignalForFaction2","sbsFs",UIFAC_RET_BOOL,UIFAC_PRED_TRUE,UIFAC_K_NONE,0,-1},
    {"ResetDefaultResponseToSignalForFaction","sFs",UIFAC_RET_VOID,UIFAC_PRED_VOID,UIFAC_K_NONE,0,-1},
    {"SetDefaultResponseToSignalForControllable","sbsI",UIFAC_RET_BOOL,UIFAC_PRED_TRUE,UIFAC_K_NONE,0,3},
    {"ResetResponseToSignalForControllable","sI",UIFAC_RET_BOOL,UIFAC_PRED_TRUE,UIFAC_K_NONE,0,1},
    /* faction-wide settings of the player information menu: by faction ID, or g_PlayerFaction's */
    {"SetFactionBuildMethod","Fs",UIFAC_RET_VOID,UIFAC_PRED_VOID,UIFAC_K_NONE,0,-1},
    {"SetFactionDefaultWeaponMode","Fs",UIFAC_RET_VOID,UIFAC_PRED_VOID,UIFAC_K_NONE,0,-1},
    {"SetPlayerFactionName","s",UIFAC_RET_VOID,UIFAC_PRED_VOID,UIFAC_K_NONE,0,-1},
    /* the player entity's settings (coop every slot; factions mode the player faction's slot only) */
    {"SetPlayerShipsWaitForPlayer","b",UIFAC_RET_VOID,UIFAC_PRED_VOID,UIFAC_K_NONE,1,-1},
    {"SetPlayerTaxiWaitsForPlayer","b",UIFAC_RET_VOID,UIFAC_PRED_VOID,UIFAC_K_NONE,1,-1},
    {"SetPlayerGlobalLoadoutLevel","f",UIFAC_RET_VOID,UIFAC_PRED_VOID,UIFAC_K_NONE,1,-1},
    {"SetPlayerTradeLoopCargoReservationSetting","b",UIFAC_RET_VOID,UIFAC_PRED_VOID,UIFAC_K_NONE,1,-1},
    {"SetPlayerIllegalWare","sb",UIFAC_RET_VOID,UIFAC_PRED_VOID,UIFAC_K_NONE,1,-1},
    {"SetPlayerLogo","L",UIFAC_RET_VOID,UIFAC_PRED_VOID,UIFAC_K_NONE,1,-1},
    {"SetPlayerPaintTheme","s",UIFAC_RET_VOID,UIFAC_PRED_VOID,UIFAC_K_NONE,1,-1},
    {"SetPlayerClothingTheme","s",UIFAC_RET_VOID,UIFAC_PRED_VOID,UIFAC_K_NONE,1,-1},
    /* station settings the trade family left refused (menu_station_overview.lua) and the behaviour panel's resupply level */
    {"UpdateSupplyOverrides","IOn",UIFAC_RET_VOID,UIFAC_PRED_VOID,UIFAC_K_NONE,0,0},
    {"PauseProcessingModule","Ib",UIFAC_RET_VOID,UIFAC_PRED_VOID,UIFAC_K_NONE,0,0},
    {"PauseProductionModule","Ib",UIFAC_RET_VOID,UIFAC_PRED_VOID,UIFAC_K_NONE,0,0},
    {"SetDefensibleLoadoutLevel","If",UIFAC_RET_VOID,UIFAC_PRED_VOID,UIFAC_K_NONE,0,0},
};
#define UIFAC_CLAIMS (sizeof(uifac_claims)/sizeof(*uifac_claims))
/* The stock struct layouts (menu_playerinfo.lua, helper.lua, menu_station_overview.lua ffi.cdef; the Lua half compares them
 * with ffi.offsetof at the arming). */
typedef struct {uint32_t id;const char *type,*name;unsigned char usemacrowhitelist;uint32_t nummacros;const char **macros;
    unsigned char usefactionwhitelist;uint32_t numfactions;const char **factions;const char *relation;unsigned char hazardous;} uifac_blacklist;
typedef struct {const char *faction,*civilian,*military;} uifac_fsetting;
typedef struct {int32_t id;const char *name;uint32_t numfactions;uifac_fsetting *factions;} uifac_fightrule;
typedef struct {uint32_t id;const char *name;uint32_t numfactions;const char **factions;unsigned char iswhitelist;} uifac_traderule;
typedef struct {const char *file,*icon;unsigned char ispersonal;} uifac_logo;
typedef struct {const char *macro;int32_t amount;} uifac_supply;
_Static_assert(sizeof(uifac_blacklist)==72 && __builtin_offsetof(uifac_blacklist,nummacros)==28 && __builtin_offsetof(uifac_blacklist,factions)==48 &&
    __builtin_offsetof(uifac_blacklist,hazardous)==64,"BlacklistInfo2");
_Static_assert(sizeof(uifac_fsetting)==24 && sizeof(uifac_fightrule)==32 && __builtin_offsetof(uifac_fightrule,factions)==24,"FightRuleInfo");
_Static_assert(sizeof(uifac_traderule)==40 && __builtin_offsetof(uifac_traderule,iswhitelist)==32,"TradeRuleInfo");
_Static_assert(sizeof(uifac_logo)==24 && sizeof(uifac_supply)==16,"UILogo, SupplyOverride");
/* What the managers' layout, the creates' counters and owners and the entity's settings rely on (build 9.00). */
typedef struct {unsigned rva,size;const char *sha;} uifac_pin;
static const uifac_pin uifac_pin_table[]={
    {0x371280u,0xef,"c3be85141dfcc583226d9a7f2d899be4ef7a3296f26c8f8366f8fd9d82bcef50"},   /* blacklist/fight rule lister: +0x30 +0x38 +0x28 [rule] */
    {0x807670u,0xe9,"f555920bd25d3ff5583524e70b38a56e354ac104d187911f9278e8c19858fa2e"},   /* trade rule lister */
    {0x371370u,0x180,"e41961553b67efcf39be3ce09f4e3e90815aa7ca01522e2a487b9265f8cabfe3"},  /* blacklist create: inc [manager], insert */
    {0x370650u,0x40,"678fa7d3394ea24351514f954704da92bd3513bb58797e81cff0ceee1ffc24f4"},   /* blacklist ctor: +0 owner */
    {0x179279u,0x3d,"0112b3a0455b647f9c13c17079bf394ae85184db0570f2bae78b7d511dd79e6f"},   /* CreateFightRule: inc [manager], +0 g_PlayerFaction */
    {0x807760u,0x17a,"6e48da6d49d2a250488eb46a15b7461e9a7271a68e66c4fa0f1149ebba93cc97"},  /* trade rule create: inc [manager], +0 owner */
    {0x1f0e70u,0x2d,"b2cd532721ef3290ab8e471e5ad32bf68694f39976a7fe6b062843edd7cd4273"},   /* SetPlayerShipsWaitForPlayer: +0x8a6b4 */
    {0x1b9980u,0x2d,"b850380e6b515d8290b448be2cc37683f34f2db1f2172d4a851f700e4ab39230"},   /* SetPlayerTaxiWaitsForPlayer: +0x8a6b5 */
    {0x1dcf40u,0x2c,"5b10946d5bb1cbebf98cd166aaa30e2d054f11130bfb517aff3d3888bb5cc56e"},   /* SetPlayerGlobalLoadoutLevel: +0x8a6a8 */
    {0x1b99b0u,0x2d,"4b29fe0d380c7717b3553f6b53b3a7713d241b7f41101fd1ec49ce0ee15bfa21"},   /* SetPlayerTradeLoopCargoReservationSetting: +0x8a720 */
};
#define UIFAC_PIN_COUNT (sizeof(uifac_pin_table)/sizeof(*uifac_pin_table))
static const uifac_pin *uifac_pin_tab=uifac_pin_table;   /* tests: none */
/* Player settings per slot (lockstep_player_settings.h, included at the end; native_gate_player_settings_per_slot). */
static int pss_armed;
static const char *pss_why;
static int pss_slot_ready(unsigned slot);
static int pss_apply_enter(unsigned slot);
static int pss_apply_leave(int commit);
static int pss_view_enter(unsigned slot);
static int pss_view_leave(void);
static int pss_pass(uint64_t tick);
static int pss_frame_text(char *out,size_t capacity);
static int pss_entity_own(unsigned char flags[3],uint32_t *loadout);
static unsigned uifac_pin_count=UIFAC_PIN_COUNT;
typedef uint64_t (*uifac_call_fn)(uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t);
static struct {void *(*entry)(unsigned claim);} uifac_fn;   /* where a claimed function is (tests: stand-ins) */
static int uifac_armed,uifac_pins_checked;
static const char *uifac_why="-",*uifac_arm_why="-";
static const uifac_claim *uifac_claim_at[UIGUARD_MAX];
static unsigned short uifac_guard_of[UIFAC_CLAIMS];
static unsigned char uifac_seq;
static uint32_t uifac_token_seq;
static volatile LONG64 uifac_captured,uifac_refused,uifac_records_out,uifac_parts_out,uifac_dropped,uifac_records_in,uifac_parts_in,uifac_torn,
    uifac_ran,uifac_skipped,uifac_scopes,uifac_scope_same,uifac_tokens_mapped,uifac_tokens_unknown;
static uint64_t uifac_hash;
typedef struct {uint64_t tick;const char *what,*why;} uifac_refusal;
static uifac_refusal uifac_ring[UIFAC_RING];
static unsigned uifac_ring_head,uifac_ring_tail;
static void uifac_note(const char *what,const char *why) {
    InterlockedIncrement64(&uifac_refused);
    if(uifac_ring_head-uifac_ring_tail>=UIFAC_RING) return;
    uifac_refusal *r=&uifac_ring[uifac_ring_head%UIFAC_RING];r->tick=stats.issued;r->what=what;r->why=why;++uifac_ring_head;
}

/* ---- the world, read only ---- */
static unsigned char *uifac_manager(unsigned kind) {
    unsigned char *m=NULL;
    return kind>=1 && kind<UIFAC_KINDS && read_local(pnat_base+uifac_manager_rva[kind],&m,8)?m:NULL;
}
static int uifac_ptr(const unsigned char *at,unsigned char **out) {*out=NULL;return read_local(at,out,8);}
/* The rule `id` of a manager as X4's own lookups find it (RemoveBlacklist 0x1b62e0): 1 and its owner, 0 none, -1 unreadable. */
static int uifac_rule_owner(unsigned kind,int32_t id,void **owner) {
    unsigned char *m=uifac_manager(kind),*node=NULL,*cand,*value=NULL;int32_t key=0;*owner=NULL;
    if(!m || !uifac_ptr(m+UIFAC_M_ROOT,&node)) return -1;
    cand=m+UIFAC_M_HEADER;
    for(unsigned depth=0;node;depth++) {
        if(depth>UIFAC_DEPTH_MAX || !read_local(node+UIFAC_N_KEY,&key,4)) return -1;
        if(key<id) {if(!uifac_ptr(node+UIFAC_N_RIGHT,&node)) return -1;}
        else {cand=node;if(!uifac_ptr(node+UIFAC_N_LEFT,&node)) return -1;}
    }
    if(cand==m+UIFAC_M_HEADER) return 0;
    if(!read_local(cand+UIFAC_N_KEY,&key,4)) return -1;
    if(id<key) return 0;
    if(!uifac_ptr(cand+UIFAC_N_VALUE,&value)) return -1;
    if(!value) return 0;
    return uifac_ptr(value,(unsigned char**)owner)?1:-1;
}
/* A manager's counter and size, and how many of its rules each slot's faction owns (the lister's in-order walk, 0x371280).
 * 1, or 0 when it cannot be read. */
static int uifac_walk(unsigned kind,void *const factions[PILOT_SLOTS],unsigned counts[PILOT_SLOTS],int32_t *counter,uint64_t *size) {
    unsigned char *m=uifac_manager(kind),*header,*node=NULL;
    memset(counts,0,sizeof(unsigned)*PILOT_SLOTS);*counter=0;*size=0;
    if(!m || !read_local(m,counter,4) || !read_local(m+UIFAC_M_SIZE,size,8) || !uifac_ptr(m+UIFAC_M_LEFTMOST,&node)) return 0;
    header=m+UIFAC_M_HEADER;
    for(unsigned steps=0;node && node!=header;steps++) {
        unsigned char *value=NULL,*owner=NULL,*next=NULL;
        if(steps>UIFAC_TREE_MAX || steps>*size || !uifac_ptr(node+UIFAC_N_VALUE,&value)) return 0;
        if(value && !uifac_ptr(value,&owner)) return 0;
        for(unsigned s=0;s<PILOT_SLOTS;s++) if(factions[s] && owner==(unsigned char*)factions[s]) ++counts[s];
        if(!uifac_ptr(node+UIFAC_N_RIGHT,&next)) return 0;
        if(next) {
            node=next;
            for(unsigned d=0;;d++) {unsigned char *left=NULL;if(d>UIFAC_DEPTH_MAX || !uifac_ptr(node+UIFAC_N_LEFT,&left)) return 0;if(!left) break;node=left;}
        } else {
            unsigned char *parent=NULL,*child=node,*left=NULL;
            for(unsigned d=0;;d++) {
                if(d>UIFAC_DEPTH_MAX || !uifac_ptr(child,&parent) || !parent || !uifac_ptr(parent+UIFAC_N_LEFT,&left)) return 0;
                if(left==child) break;
                child=parent;
            }
            node=parent;
        }
    }
    return 1;
}
/* X4's own player (the canonical one, the simulation's): its settings; 0 without one. */
static int uifac_entity(unsigned char flags[3],uint32_t *loadout) {
    unsigned char *game=NULL,*player=NULL;
    if(pss_entity_own(flags,loadout)) return 1;   /* a settings scope holds a slot's values in the fields: X4's own, saved */
    if(!uifac_ptr(pnat_base+UIFAC_GAME,&game) || !game || !uifac_ptr(game+UIFAC_GAME_PLAYER,&player) || !player) return 0;
    return read_local(player+UIFAC_E_SHIPS_WAIT,&flags[0],1) && read_local(player+UIFAC_E_TAXI_WAITS,&flags[1],1) &&
        read_local(player+UIFAC_E_LOOP_RESERVE,&flags[2],1) && read_local(player+UIFAC_E_LOADOUT,loadout,4);
}
/* The faction view (factions mode, this client's own menus only): g_PlayerFaction = this client's slot faction for the length
 * of a stock menu function the Lua half wraps (the player information menu, the rule lists of Helper), so the stock queries
 * (the rule lists and their defaults, the faction's name, its money) answer the slot's own faction. Only on the UI thread in
 * a local phase, never during an apply or a trade scope; a pass never finds it open. */
static int uifac_view_open,uifac_view_settings;
static void *uifac_view_saved,*uifac_view_faction;
static volatile LONG64 uifac_views,uifac_views_nested,uifac_views_refused;
/* X4's player faction as the simulation knows it (the view's saved one while the view is open). */
static void *uifac_player_faction(void) {return uifac_view_open?uifac_view_saved:*uitrade_player_faction();}
/* Slot k's faction against X4's player faction: 0 the same (coop, slot 1), 1 another (factions mode), -1 none (no ship). */
static int uifac_slot_relation(unsigned slot,void **faction) {
    *faction=uitrade_slot_faction(slot);
    if(!*faction) return -1;
    return *faction==uifac_player_faction()?0:1;
}
/* 1 opened, 2 open already (a nested wrapped call: the outer one closes it), 0 not needed (this slot's faction is the player
 * faction), -1 refused (not a local phase of this client's UI, an apply, a scope). */
static int uifac_view_enter(void) {
    if(uifac_view_open && GetCurrentThreadId()==ui_thread) {InterlockedIncrement64(&uifac_views_nested);return 2;}
    if(!uifac_armed || uitrade_scope_open || GetCurrentThreadId()!=ui_thread || InterlockedCompareExchange(&uirep_replaying,0,0) || uirep_stopped ||
       pstk_scope_slot || !uirep_local_phase()) {InterlockedIncrement64(&uifac_views_refused);return -1;}
    void *faction=NULL;int relation=uifac_slot_relation(pstk_capture_slot,&faction);
    if(relation<0) {InterlockedIncrement64(&uifac_views_refused);return -1;}
    if(!relation) return 0;
    uifac_view_saved=*uitrade_player_faction();uifac_view_faction=faction;*uitrade_player_faction()=faction;uifac_view_open=1;
    uifac_view_settings=pss_armed && pss_view_enter(pstk_capture_slot);   /* the slot's own settings in X4's player's fields */
    InterlockedIncrement64(&uifac_views);return 1;
}
/* 1 closed as it was opened (or nothing open); 0 g_PlayerFaction changed inside (put back all the same: the replay stops). */
static int uifac_view_leave(void) {
    if(!uifac_view_open) return 1;
    int held=uifac_view_settings?pss_view_leave():1;uifac_view_settings=0;
    int ok=*uitrade_player_faction()==uifac_view_faction && held;
    *uitrade_player_faction()=uifac_view_saved;uifac_view_open=0;uifac_view_saved=uifac_view_faction=NULL;
    return ok;
}

/* ---- tokens: what a create answered the menu, and X4's ID for it (every client the same) ---- */
typedef struct {uint32_t token;int32_t id;unsigned char kind;} uifac_mapping;
static uifac_mapping uifac_map[UIFAC_TOKENS];
static unsigned uifac_map_count,uifac_map_next;
static int uifac_is_token(int32_t v) {return ((uint32_t)v&UIFAC_TOKEN_MASK)==UIFAC_TOKEN;}
static void uifac_map_put(unsigned kind,uint32_t token,int32_t id) {
    uifac_mapping *e=&uifac_map[uifac_map_next];e->token=token;e->id=id;e->kind=(unsigned char)kind;
    uifac_map_next=(uifac_map_next+1)%UIFAC_TOKENS;if(uifac_map_count<UIFAC_TOKENS) ++uifac_map_count;
    InterlockedIncrement64(&uifac_tokens_mapped);
}
static int uifac_map_find(unsigned kind,uint32_t token,int32_t *id) {
    for(unsigned i=0;i<uifac_map_count;i++) if(uifac_map[i].token==token && uifac_map[i].kind==kind) {*id=uifac_map[i].id;return 1;}
    return 0;
}

/* ---- capture (the guard's hook, a local phase of this client's UI) ---- */
typedef struct {unsigned char *b;unsigned n,cap;const char *why;} uifac_writer;
static void uifac_put(uifac_writer *w,const void *p,unsigned k) {
    if(w->why) return;
    if(k>w->cap-w->n) {w->why="too_long";return;}
    memcpy(w->b+w->n,p,k);w->n+=k;
}
static void uifac_put8(uifac_writer *w,unsigned v) {unsigned char b=(unsigned char)v;uifac_put(w,&b,1);}
static void uifac_put32(uifac_writer *w,uint32_t v) {unsigned char b[4];for(unsigned i=0;i<4;i++) b[i]=(unsigned char)(v>>(8*i));uifac_put(w,b,4);}
static int uifac_read(const void *from,void *to,size_t n,uifac_writer *w) {
    if(w->why) return 0;
    if(!from || !read_local(from,to,n)) {w->why="memory";return 0;}
    return 1;
}
/* A C string from the menu's memory (copied with guarded reads): 1, or 0 when unreadable or longer than UIFAC_STRING_MAX. */
static int uifac_cstr(const char *s,char *out,unsigned *length) {
    unsigned n=0;
    while(s) {
        unsigned char chunk[32];unsigned want=32;
        if(!read_local(s+n,chunk,want)) {if(!read_local(s+n,chunk,1)) return 0;want=1;}
        for(unsigned i=0;i<want;i++) {
            if(!chunk[i]) {out[n]=0;*length=n;return 1;}
            if(n>=UIFAC_STRING_MAX) return 0;
            out[n++]=(char)chunk[i];
        }
    }
    return 0;
}
static void uifac_string(uifac_writer *w,const char *s,int nullable) {
    char text[UIFAC_STRING_MAX+1];unsigned length=0;
    if(w->why) return;
    if(!s) {if(nullable) uifac_put8(w,UIFAC_STR_NULL);else w->why="string";return;}
    if(!uifac_cstr(s,text,&length)) {w->why="string";return;}
    uifac_put8(w,length);uifac_put(w,text,length);
}
static unsigned uifac_count(uint64_t v,uifac_writer *w) {
    if(v>UIFAC_ARRAY_MAX) {if(!w->why) w->why="array";return 0;}
    return (unsigned)v;
}
static void uifac_strings(uifac_writer *w,const char *const *list,uint64_t count) {
    unsigned n=uifac_count(count,w);
    if(n && !list && !w->why) w->why="array";
    uifac_put8(w,n);
    for(unsigned i=0;i<n && !w->why;i++) {const char *s=NULL;if(uifac_read(list+i,&s,8,w)) uifac_string(w,s,0);}
}
/* A float argument in xmm0..3 as the guard's shared routine saved them (frame - 0x40 + 16*i). */
static uint32_t uifac_xmm(const uint64_t *frame,unsigned i) {uint32_t v=0;memcpy(&v,(const unsigned char*)frame-0x40+16*i,4);return v;}
static int uifac_encode(const uifac_claim *c,const uint64_t *frame,uifac_writer *w) {
    for(unsigned i=0;c->spec[i] && !w->why;i++) {
        uint64_t a=uiguard_arg(frame,i);
        switch(c->spec[i]) {
        case 'I':
            if(!a || !uirep_component(a) || !uirep_check_of(a)) w->why="unmapped";
            else {unsigned char ref[UIREP_REF];uirep_put_ref(ref,a);uifac_put(w,ref,UIREP_REF);}
            break;
        case 'r':uifac_put32(w,(uint32_t)a);break;
        case 's':case 'F':uifac_string(w,(const char*)(uintptr_t)a,0);break;
        case 'b':uifac_put8(w,(a&0xffu)!=0);break;
        case 'f':if(i>=4) w->why="float";else uifac_put32(w,uifac_xmm(frame,i));break;
        case 'n':break;
        case 'B': {
            uifac_blacklist b;if(!uifac_read((const void*)(uintptr_t)a,&b,sizeof(b),w)) break;
            uifac_put32(w,b.id);uifac_string(w,b.type,0);uifac_string(w,b.name,0);uifac_put8(w,b.usemacrowhitelist!=0);
            uifac_strings(w,b.macros,b.nummacros);uifac_put8(w,b.usefactionwhitelist!=0);uifac_strings(w,b.factions,b.numfactions);
            uifac_string(w,b.relation,1);uifac_put8(w,b.hazardous!=0);
            break;
        }
        case 'G': {
            uifac_fightrule g;if(!uifac_read((const void*)(uintptr_t)a,&g,sizeof(g),w)) break;
            unsigned n=uifac_count(g.numfactions,w);
            if(n && !g.factions && !w->why) w->why="array";
            uifac_put32(w,(uint32_t)g.id);uifac_string(w,g.name,0);uifac_put8(w,n);
            for(unsigned e=0;e<n && !w->why;e++) {
                uifac_fsetting s;if(!uifac_read(g.factions+e,&s,sizeof(s),w)) break;
                uifac_string(w,s.faction,0);uifac_string(w,s.civilian,1);uifac_string(w,s.military,1);
            }
            break;
        }
        case 'T': {
            uifac_traderule t;if(!uifac_read((const void*)(uintptr_t)a,&t,sizeof(t),w)) break;
            uifac_put32(w,t.id);uifac_string(w,t.name,0);uifac_strings(w,t.factions,t.numfactions);uifac_put8(w,t.iswhitelist!=0);
            break;
        }
        case 'L': {
            uifac_logo l;if(!uifac_read((const void*)(uintptr_t)a,&l,sizeof(l),w)) break;
            uifac_string(w,l.file,1);uifac_string(w,l.icon,1);uifac_put8(w,l.ispersonal!=0);
            break;
        }
        case 'O': {
            unsigned n=uifac_count(uiguard_arg(frame,i+1),w);
            if(n && !a && !w->why) w->why="array";
            uifac_put8(w,n);
            for(unsigned e=0;e<n && !w->why;e++) {
                uifac_supply s;if(!uifac_read((const uifac_supply*)(uintptr_t)a+e,&s,sizeof(s),w)) break;
                uifac_string(w,s.macro,1);uifac_put32(w,(uint32_t)s.amount);
            }
            break;
        }
        default:w->why="spec";break;
        }
    }
    return !w->why;
}
/* A whole record goes out as parts, all of them or none: 1, or 0 and why (nothing ran anywhere). */
static int uifac_send(const unsigned char *record,unsigned n,const char **why) {
    unsigned parts=(n+UIFAC_PART_DATA-1)/UIFAC_PART_DATA,pushed=0;
    if(!n || parts>UIFAC_PARTS_MAX) {*why="too_long";return 0;}
    if(UIREP_QUEUE-uirep_queue_count<parts) {*why="queue_full";return 0;}
    for(unsigned p=0;p<parts;p++) {
        unsigned char part[UIREP_EVENT_MAX];unsigned at=p*UIFAC_PART_DATA,k=n-at<UIFAC_PART_DATA?n-at:UIFAC_PART_DATA;
        part[0]=uifac_seq;part[1]=(unsigned char)p;part[2]=(unsigned char)parts;memcpy(part+UIFAC_PART_HEAD,record+at,k);
        if(!uirep_push(UIFAC_OP_PART,part,UIFAC_PART_HEAD+k)) break;
        ++pushed;
    }
    ++uifac_seq;
    if(pushed!=parts) {InterlockedIncrement64(&uifac_dropped);*why=pushed?"torn":"refused";return 0;}
    InterlockedIncrement64(&uifac_records_out);InterlockedExchangeAdd64(&uifac_parts_out,(LONG64)parts);
    return 1;
}
/* The claimed call in a local phase (the guard's rule): 1 = handled (*ret the menu's answer); the function never runs here.
 * Refusals answer the neutral value (0) and are rows of this client (UI_FACTION_REFUSED) with the info line. */
static int uifac_claim_decide(unsigned index,const uint64_t *frame,uint64_t *ret) {
    const uifac_claim *c=index<UIGUARD_MAX?uifac_claim_at[index]:NULL;
    if(!c || !uifac_armed || !(uirep_families&UIREP_FAMILY_FACTION)) return 0;
    unsigned slot=pstk_capture_slot;void *faction=NULL;*ret=0;
    if(slot<1 || slot>PILOT_SLOTS) {uifac_note(c->name,"slot");return 1;}
    if(c->entity) {
        int relation=uifac_slot_relation(slot,&faction);
        if(relation<0 || (relation && !pss_slot_ready(slot))) {uifac_note(c->name,relation<0?"no_faction":"player_entity");return 1;}
    }
    static unsigned char record[UIFAC_RECORD_MAX];
    uifac_writer w={record,0,sizeof(record),NULL};
    uint32_t token=0;
    uifac_put8(&w,(unsigned)(c-uifac_claims));
    if(c->pred==UIFAC_PRED_TOKEN) {
        token=UIFAC_TOKEN|((uint32_t)slot<<16)|((uifac_token_seq%0xffffu)+1u);
        uifac_put32(&w,token);
    }
    const char *why=NULL;
    if(!uifac_encode(c,frame,&w)) {uifac_note(c->name,w.why);return 1;}
    if(!uifac_send(record,w.n,&why)) {uifac_note(c->name,why);return 1;}
    if(token) ++uifac_token_seq;
    InterlockedIncrement64(&uifac_captured);
    *ret=c->pred==UIFAC_PRED_TOKEN?token:c->pred==UIFAC_PRED_TRUE?1:0;
    return 1;
}

/* ---- the wire (every client) ---- */
static int uifac_event_ok(unsigned slot,unsigned op,const unsigned char *data,unsigned n,const char **why) {
    (void)slot;
    if(op!=UIFAC_OP_PART || !(uirep_families&UIREP_FAMILY_FACTION) || n<=UIFAC_PART_HEAD || !data[2] || data[2]>UIFAC_PARTS_MAX || data[1]>=data[2]) {
        *why="ui_faction";return 0;
    }
    return 1;
}
/* Reading a record: the walk (run 0) checks it whole without touching the world; the run (run 1) builds the arguments in the
 * arena, strings NUL-terminated, "player" in a faction list as `self` (the slot's faction ID). */
typedef struct {const unsigned char *b;unsigned n,at;const char *why;int run;const char *self;} uifac_reader;
static __attribute__((aligned(16))) unsigned char uifac_arena[4*UIFAC_RECORD_MAX];
static unsigned uifac_arena_used;
static unsigned uifac_g8(uifac_reader *r) {if(r->why) return 0;if(r->at>=r->n) {r->why="truncated";return 0;}return r->b[r->at++];}
static uint32_t uifac_g32(uifac_reader *r) {uint32_t v=0;for(unsigned i=0;i<4;i++) v|=(uint32_t)uifac_g8(r)<<(8*i);return v;}
static void *uifac_alloc(uifac_reader *r,size_t n) {
    size_t need=(n+15)&~(size_t)15;
    if(r->why || !r->run) return NULL;
    if(need>sizeof(uifac_arena)-uifac_arena_used) {r->why="arena";return NULL;}
    void *p=uifac_arena+uifac_arena_used;uifac_arena_used+=(unsigned)need;memset(p,0,n);return p;
}
static const char *uifac_gstring(uifac_reader *r,int nullable,int faction) {
    unsigned length=uifac_g8(r);
    if(r->why) return NULL;
    if(length==UIFAC_STR_NULL) {if(!nullable) r->why="string";return NULL;}
    if(length>r->n-r->at) {r->why="truncated";return NULL;}
    const unsigned char *s=r->b+r->at;r->at+=length;
    if(!r->run) return NULL;
    if(faction && r->self && length==6 && !memcmp(s,"player",6)) return r->self;
    char *out=uifac_alloc(r,length+1);
    if(out) {memcpy(out,s,length);out[length]=0;}
    return out;
}
static const char **uifac_gstrings(uifac_reader *r,uint32_t *count,int faction) {
    unsigned n=uifac_g8(r);*count=n;
    const char **list=n?uifac_alloc(r,sizeof(char*)*n):NULL;
    for(unsigned i=0;i<n && !r->why;i++) {const char *s=uifac_gstring(r,0,faction);if(list) list[i]=s;}
    return list;
}
/* The arguments of claim c (a[i] by spec; *subject_at: the subject's reference in the record, or -1). */
static int uifac_decode(const uifac_claim *c,uifac_reader *r,uint64_t a[UIFAC_ARGS],uint32_t *float_bits,int *subject_at) {
    *subject_at=-1;
    for(unsigned i=0;c->spec[i] && i<UIFAC_ARGS && !r->why;i++) {
        switch(c->spec[i]) {
        case 'I':
            if(c->subject==(signed char)i) *subject_at=(int)r->at;
            if(UIREP_REF>r->n-r->at) {r->why="truncated";break;}
            {uint32_t check=0;a[i]=uirep_get_ref(r->b+r->at,&check);}
            r->at+=UIREP_REF;
            break;
        case 'r':a[i]=(uint64_t)(int64_t)(int32_t)uifac_g32(r);break;
        case 's':a[i]=(uint64_t)(uintptr_t)uifac_gstring(r,0,0);break;
        case 'F':a[i]=(uint64_t)(uintptr_t)uifac_gstring(r,0,0);break;   /* checked and rewritten by the run */
        case 'b': {unsigned v=uifac_g8(r);if(v>1 && !r->why) r->why="bool";a[i]=v;break;}
        case 'f':*float_bits=uifac_g32(r);a[i]=*float_bits;break;
        case 'n':break;
        case 'B': {
            uifac_blacklist *b=uifac_alloc(r,sizeof(*b)),t;memset(&t,0,sizeof(t));
            t.id=uifac_g32(r);t.type=uifac_gstring(r,0,0);t.name=uifac_gstring(r,0,0);t.usemacrowhitelist=(unsigned char)uifac_g8(r);
            t.macros=uifac_gstrings(r,&t.nummacros,0);t.usefactionwhitelist=(unsigned char)uifac_g8(r);t.factions=uifac_gstrings(r,&t.numfactions,1);
            t.relation=uifac_gstring(r,1,0);t.hazardous=(unsigned char)uifac_g8(r);
            if(!r->why && (t.usemacrowhitelist>1 || t.usefactionwhitelist>1 || t.hazardous>1)) r->why="bool";
            if(b) *b=t;
            a[i]=(uint64_t)(uintptr_t)b;
            break;
        }
        case 'G': {
            uifac_fightrule *g=uifac_alloc(r,sizeof(*g)),t;memset(&t,0,sizeof(t));
            t.id=(int32_t)uifac_g32(r);t.name=uifac_gstring(r,0,0);t.numfactions=uifac_g8(r);
            t.factions=t.numfactions?uifac_alloc(r,sizeof(uifac_fsetting)*t.numfactions):NULL;
            for(unsigned e=0;e<t.numfactions && !r->why;e++) {
                uifac_fsetting s;s.faction=uifac_gstring(r,0,1);s.civilian=uifac_gstring(r,1,0);s.military=uifac_gstring(r,1,0);
                if(t.factions) t.factions[e]=s;
            }
            if(g) *g=t;
            a[i]=(uint64_t)(uintptr_t)g;
            break;
        }
        case 'T': {
            uifac_traderule *t=uifac_alloc(r,sizeof(*t)),v;memset(&v,0,sizeof(v));
            v.id=uifac_g32(r);v.name=uifac_gstring(r,0,0);v.factions=uifac_gstrings(r,&v.numfactions,1);v.iswhitelist=(unsigned char)uifac_g8(r);
            if(!r->why && v.iswhitelist>1) r->why="bool";
            if(t) *t=v;
            a[i]=(uint64_t)(uintptr_t)t;
            break;
        }
        case 'L': {
            uifac_logo *l=uifac_alloc(r,sizeof(*l)),v;memset(&v,0,sizeof(v));
            v.file=uifac_gstring(r,1,0);v.icon=uifac_gstring(r,1,0);v.ispersonal=(unsigned char)uifac_g8(r);
            if(!r->why && v.ispersonal>1) r->why="bool";
            if(l) *l=v;
            a[i]=(uint64_t)(uintptr_t)l;
            break;
        }
        case 'O': {
            unsigned n=uifac_g8(r);uifac_supply *s=n?uifac_alloc(r,sizeof(*s)*n):NULL;
            for(unsigned e=0;e<n && !r->why;e++) {uifac_supply v;v.macro=uifac_gstring(r,1,0);v.amount=(int32_t)uifac_g32(r);if(s) s[e]=v;}
            a[i]=(uint64_t)(uintptr_t)s;
            if(c->spec[i+1]=='n' && i+1<UIFAC_ARGS) a[i+1]=n;else if(!r->why) r->why="spec";
            break;
        }
        default:r->why="spec";break;
        }
    }
    return r->why?-1:1;
}
/* One record of slot k, as it reassembles (the probe's frame, every client, in the slot's order). */
enum {UIFAC_RX_IDLE=0,UIFAC_RX_PARTS,UIFAC_RX_READY};
typedef struct {
    unsigned char state,seq,count,next;unsigned length,claim;int subject_at;uint32_t token;unsigned char buf[UIFAC_RECORD_MAX];
} uifac_rx;
static uifac_rx uifac_rx_slot[PILOT_SLOTS];
/* The record's head and whole walk: 1 well-formed (claim, token, subject), 0 malformed. */
static int uifac_record_ok(uifac_rx *x,unsigned slot) {
    if(!x->length || x->buf[0]>=UIFAC_CLAIMS) return 0;
    const uifac_claim *c=&uifac_claims[x->buf[0]];
    uifac_reader r={x->buf,x->length,1,NULL,0,NULL};
    x->claim=x->buf[0];x->token=0;
    if(c->pred==UIFAC_PRED_TOKEN) {
        x->token=uifac_g32(&r);
        if(r.why || !uifac_is_token((int32_t)x->token) || ((x->token>>16)&0xffu)!=slot || !(x->token&0xffffu)) return 0;
    }
    uint64_t a[UIFAC_ARGS]={0};uint32_t bits=0;
    if(uifac_decode(c,&r,a,&bits,&x->subject_at)<0 || r.at!=x->length) return 0;
    return 1;
}
/* One part at its apply: 1 the record is whole and well-formed, 0 more to come, -1 dropped (torn: the same parts on every
 * client, so dropped everywhere), -2 stop (malformed, a record the Lua half did not run). */
static int uifac_part(unsigned slot,const unsigned char *data,unsigned n,const char **why) {
    *why=NULL;
    if(slot<1 || slot>PILOT_SLOTS || !uifac_armed || !uifac_event_ok(slot,UIFAC_OP_PART,data,n,why)) {if(!*why) *why="ui_faction";return -2;}
    uifac_rx *x=&uifac_rx_slot[slot-1];unsigned seq=data[0],index=data[1],count=data[2],length=n-UIFAC_PART_HEAD;
    InterlockedIncrement64(&uifac_parts_in);
    if(x->state==UIFAC_RX_READY) {*why="ui_faction_overlap";return -2;}
    if(index==0) {
        if(x->state==UIFAC_RX_PARTS) InterlockedIncrement64(&uifac_torn);
        x->state=UIFAC_RX_PARTS;x->seq=(unsigned char)seq;x->count=(unsigned char)count;x->next=0;x->length=0;
    } else if(x->state!=UIFAC_RX_PARTS || seq!=x->seq || index!=x->next || count!=x->count) {
        if(x->state==UIFAC_RX_PARTS) InterlockedIncrement64(&uifac_torn);
        x->state=UIFAC_RX_IDLE;*why="torn";return -1;
    }
    if(length>sizeof(x->buf)-x->length) {x->state=UIFAC_RX_IDLE;*why="ui_faction_record";return -2;}
    memcpy(x->buf+x->length,data+UIFAC_PART_HEAD,length);x->length+=length;++x->next;
    if(x->next<x->count) return 0;
    if(!uifac_record_ok(x,slot)) {x->state=UIFAC_RX_IDLE;*why="ui_faction_record";return -2;}
    x->state=UIFAC_RX_READY;InterlockedIncrement64(&uifac_records_in);
    return 1;
}
/* The ready record's claim and subject: 1 resolved (*runtime), 0 it names none, 2 gone (the same everywhere), -1 the
 * identities differ (stop). */
static int uifac_ready_subject(unsigned slot,const char **name,uint64_t *runtime) {
    uifac_rx *x=slot>=1 && slot<=PILOT_SLOTS?&uifac_rx_slot[slot-1]:NULL;
    *runtime=0;*name="?";
    if(!x || x->state!=UIFAC_RX_READY) return -1;
    *name=uifac_claims[x->claim].name;
    if(x->subject_at<0) return 0;
    unsigned char *component=NULL;int r=uirep_resolve(x->buf+x->subject_at,runtime,&component);
    return r<0?-1:r?1:2;
}
static void uifac_fold(unsigned slot,unsigned claim,unsigned ran,int32_t result,const char *why) {
    unsigned char b[8];b[0]=(unsigned char)slot;b[1]=(unsigned char)claim;b[2]=(unsigned char)ran;b[3]=0;
    for(unsigned i=0;i<4;i++) b[4+i]=(unsigned char)((uint32_t)result>>(8*i));
    uifac_hash=pstk_fnv(b,8,uifac_hash^0x5549464143543131ULL);   /* "UIFACT11" */
    if(why) uifac_hash=pstk_fnv((const unsigned char*)why,strlen(why),uifac_hash);
}
static void *uifac_entry_of(unsigned claim) {
    if(uifac_fn.entry) return uifac_fn.entry(claim);
    return pnat_base+uiguard_tab[uifac_guard_of[claim]].rva;
}
/* A rule ID of a record: a token becomes X4's ID; a positive ID must name a rule of `faction`. 1, or 0 and why (skip). */
static int uifac_rule_ok(unsigned kind,int32_t *id,void *faction,const char **why) {
    int32_t v=*id;
    if(uifac_is_token(v)) {
        int32_t real=0;
        if(!uifac_map_find(kind,(uint32_t)v,&real)) {InterlockedIncrement64(&uifac_tokens_unknown);*why="token";return 0;}
        if(!real) {*why="depends";return 0;}
        v=real;*id=v;
    }
    if(v<=0) return 1;   /* none named (-1: the controllable's own setting off), X4 answers it as it does */
    void *owner=NULL;int found=uifac_rule_owner(kind,v,&owner);
    if(found<0) {*why="rules_unreadable";return -1;}
    if(!found) {*why="gone";return 0;}
    if(owner!=faction) {*why="not_owner";return 0;}
    return 1;
}
/* Slot k's ready record (the probe's frame, every client, during the apply, after the Lua half's subject check): `self` the
 * slot faction's ID as the Lua half reads it (GetComponentData of the slot ship's owner), `refused` the Lua half's verdict
 * or NULL. 1 = X4's function ran (what), 0 = skipped, the same everywhere (what), -1 = stop (what is the why). */
static int uifac_run(unsigned slot,const char *self,const char *refused,char *what,size_t capacity) {
    uifac_rx *x=slot>=1 && slot<=PILOT_SLOTS?&uifac_rx_slot[slot-1]:NULL;
    snprintf(what,capacity,"ui_faction_run");
    if(!x || x->state!=UIFAC_RX_READY) return -1;
    const uifac_claim *c=&uifac_claims[x->claim];
    x->state=UIFAC_RX_IDLE;
    if(uifac_view_open) {snprintf(what,capacity,"ui_faction_view_open");return -1;}   /* an apply inside a menu's view: never */
    const char *skip=refused;void *faction=NULL;
    uint64_t a[UIFAC_ARGS]={0};uint32_t bits=0;int subject_at=-1;
    uifac_arena_used=0;
    if(!skip && (!self || !self[0] || strlen(self)>UIFAC_STRING_MAX)) skip="no_faction";
    if(!skip) {
        uifac_reader r={x->buf,x->length,1+(c->pred==UIFAC_PRED_TOKEN?4u:0u),NULL,1,self};
        if(uifac_decode(c,&r,a,&bits,&subject_at)<0) {snprintf(what,capacity,"ui_faction_record");return -1;}
    }
    /* The component (every claim's only 'I' is its subject, which the Lua half checked): gone is the same everywhere, a check
     * that differs stops. */
    if(!skip && subject_at>=0) {
        unsigned char *component=NULL;uint64_t runtime=0;
        int resolved=uirep_resolve(x->buf+subject_at,&runtime,&component);
        if(resolved<0) {snprintf(what,capacity,"ui_ref_mismatch");return -1;}
        if(!resolved) skip="gone";
    }
    int relation=0;
    if(!skip) {
        relation=uifac_slot_relation(slot,&faction);
        if(relation<0) skip="no_faction";
        else if(relation==0 && strcmp(self,"player")) {snprintf(what,capacity,"ui_faction_view");return -1;}   /* the two readings differ */
        else if(relation==1 && !strcmp(self,"player")) {snprintf(what,capacity,"ui_faction_view");return -1;}
    }
    if(!skip && c->entity && relation && !pss_slot_ready(slot)) skip="player_entity";
    /* Faction ID arguments: "player" is the slot's faction; another faction's settings are not the slot's to change. */
    for(unsigned i=0;!skip && c->spec[i] && i<UIFAC_ARGS;i++) {
        if(c->spec[i]!='F') continue;
        const char *s=(const char*)(uintptr_t)a[i];
        if(s && !strcmp(s,"player")) a[i]=(uint64_t)(uintptr_t)self;
        else if(!s || strcmp(s,self)) skip="not_owner";
    }
    /* Rule IDs: tokens become X4's IDs, a named rule must be the slot faction's; a create's own ID field is X4's to give. */
    for(unsigned i=0;!skip && c->spec[i] && i<UIFAC_ARGS;i++) {
        int32_t *id=NULL,arg=0;
        switch(c->spec[i]) {
        case 'r':arg=(int32_t)a[i];id=&arg;break;
        case 'B':id=(int32_t*)(void*)&((uifac_blacklist*)(uintptr_t)a[i])->id;break;
        case 'G':id=&((uifac_fightrule*)(uintptr_t)a[i])->id;break;
        case 'T':id=(int32_t*)(void*)&((uifac_traderule*)(uintptr_t)a[i])->id;break;
        default:break;
        }
        if(!id) continue;
        if(c->pred==UIFAC_PRED_TOKEN) {*id=0;continue;}
        const char *why=NULL;int ok=uifac_rule_ok(c->kind,id,faction,&why);
        if(ok<0) {snprintf(what,capacity,"ui_faction_%s",why);return -1;}
        if(!ok) skip=why;
        if(c->spec[i]=='r') a[i]=(uint64_t)(int64_t)arg;
    }
    if(skip) {
        if(c->pred==UIFAC_PRED_TOKEN && x->token) uifac_map_put(c->kind,x->token,0);   /* its dependants are skipped too */
        uifac_fold(slot,x->claim,2,0,skip);InterlockedIncrement64(&uifac_skipped);
        snprintf(what,capacity,"%s skipped=%s",c->name,skip);return 0;
    }
    int scope=uitrade_scope_enter(slot);
    if(scope<0) {
        if(c->pred==UIFAC_PRED_TOKEN && x->token) uifac_map_put(c->kind,x->token,0);
        uifac_fold(slot,x->claim,2,0,"no_scope");InterlockedIncrement64(&uifac_skipped);
        snprintf(what,capacity,"%s skipped=no_scope",c->name);return 0;
    }
    if(scope) InterlockedIncrement64(&uifac_scopes);else InterlockedIncrement64(&uifac_scope_same);
    /* Slot k's own player settings (factions mode, lockstep_player_settings.h): X4's player holds slot k's values for the call. */
    int settings=c->entity && relation;
    if(settings && !pss_apply_enter(slot)) {uitrade_scope_leave();snprintf(what,capacity,"player_settings_scope");return -1;}
    void *entry=uifac_entry_of(x->claim);uint64_t result=0;int fault=0;float f;memcpy(&f,&bits,4);
    __try {
        if(c->spec[0]=='f') ((void(*)(float))entry)(f);
        else if(c->spec[1]=='f') ((void(*)(uint64_t,float))entry)(a[0],f);
        else result=((uifac_call_fn)entry)(a[0],a[1],a[2],a[3],a[4],a[5]);
    }
    __except(GetExceptionCode()==EXCEPTION_ACCESS_VIOLATION?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH) {fault=1;}
    int held=settings?pss_apply_leave(!fault):1;
    int left=uitrade_scope_leave();
    if(fault) {snprintf(what,capacity,"ui_fault");return -1;}
    if(!left) {snprintf(what,capacity,"ui_faction_scope");return -1;}
    if(!held) {snprintf(what,capacity,"player_settings_scope");return -1;}
    int32_t value=c->ret==UIFAC_RET_ID?(int32_t)(uint32_t)result:c->ret==UIFAC_RET_BOOL?(int32_t)(result&0xffu):0;
    if(c->pred==UIFAC_PRED_TOKEN) uifac_map_put(c->kind,x->token,value);
    uifac_fold(slot,x->claim,1,value,NULL);InterlockedIncrement64(&uifac_ran);
    if(c->ret==UIFAC_RET_VOID) snprintf(what,capacity,"%s%s%s",c->name,scope?" scope":"",settings?" settings":"");
    else snprintf(what,capacity,"%s%s%s=%d",c->name,scope?" scope":"",settings?" settings":"",(int)value);
    return 1;
}

/* ---- receipt, rows, status ---- */
/* UIFAC_1:b<counter>.<size>,f..,t..;<slot 1 b.f.t>,<slot 2 ..>;w<ships wait><taxi waits><loop reserve>.<loadout bits>;h<hash>
 * ('x' a slot without a faction, '?' a manager that cannot be read, 'w-' no player): every client reads the same world. */
static int uifac_frame_text(char *out,size_t capacity,int with_hash) {
    void *factions[PILOT_SLOTS]={NULL};unsigned counts[UIFAC_KINDS][PILOT_SLOTS];int ok[UIFAC_KINDS]={0};
    for(unsigned s=1;s<=pilot_slot_count && s<=PILOT_SLOTS;s++) factions[s-1]=uitrade_slot_faction(s);
    int n=snprintf(out,capacity,"UIFAC_1:");
    for(unsigned k=1;k<UIFAC_KINDS && n>0 && (size_t)n<capacity;k++) {
        int32_t counter=0;uint64_t size=0;
        ok[k]=uifac_walk(k,factions,counts[k],&counter,&size);
        n+=ok[k]?snprintf(out+n,capacity-(size_t)n,"%s%c%d.%llu",k>1?",":"",uifac_kind_letter[k],(int)counter,(unsigned long long)size):
            snprintf(out+n,capacity-(size_t)n,"%s%c?",k>1?",":"",uifac_kind_letter[k]);
    }
    for(unsigned s=0;s<pilot_slot_count && s<PILOT_SLOTS && n>0 && (size_t)n<capacity;s++) {
        n+=snprintf(out+n,capacity-(size_t)n,"%s",s?",":";");
        if(!factions[s]) {n+=snprintf(out+n,capacity-(size_t)n,"x");continue;}
        for(unsigned k=1;k<UIFAC_KINDS && n>0 && (size_t)n<capacity;k++)
            n+=ok[k]?snprintf(out+n,capacity-(size_t)n,"%s%u",k>1?".":"",counts[k][s]):snprintf(out+n,capacity-(size_t)n,"%s?",k>1?".":"");
    }
    unsigned char flags[3]={0};uint32_t loadout=0;
    if(n>0 && (size_t)n<capacity)
        n+=uifac_entity(flags,&loadout)?snprintf(out+n,capacity-(size_t)n,";w%u%u%u.%08x",flags[0]!=0,flags[1]!=0,flags[2]!=0,(unsigned)loadout):
            snprintf(out+n,capacity-(size_t)n,";w-");
    if(pss_armed && n>0 && (size_t)n<capacity) {int more=pss_frame_text(out+n,capacity-(size_t)n);if(!more) return 0;n+=more;}
    if(with_hash && n>0 && (size_t)n<capacity) n+=snprintf(out+n,capacity-(size_t)n,";h%016llx",(unsigned long long)uifac_hash);
    return n>0 && (size_t)n<capacity?n:0;
}
static int uifac_refusals_text(char *out,size_t capacity) {
    int n=0;out[0]=0;
    while(uifac_ring_tail!=uifac_ring_head) {
        const uifac_refusal *r=&uifac_ring[uifac_ring_tail%UIFAC_RING];
        int more=snprintf(out+n,capacity-(size_t)n,"%s%llu:%s:%s",n?",":"",(unsigned long long)r->tick,r->what,r->why);
        if(more<0 || (size_t)more>=capacity-(size_t)n) break;
        n+=more;++uifac_ring_tail;
    }
    return n;
}
static int uifac_status(char *out,size_t capacity) {
    int n=snprintf(out,capacity,"ui_faction=%s why=%s claims=%u captured=%lld refused=%lld out=%lld/%lld dropped=%lld in=%lld/%lld torn=%lld ran=%lld "
        "skipped=%lld scopes=%lld/%lld tokens=%lld/%lld views=%lld/%lld/%lld",uifac_armed?"armed":"off",uifac_why,(unsigned)UIFAC_CLAIMS,(long long)uifac_captured,
        (long long)uifac_refused,(long long)uifac_records_out,(long long)uifac_parts_out,(long long)uifac_dropped,(long long)uifac_records_in,
        (long long)uifac_parts_in,(long long)uifac_torn,(long long)uifac_ran,(long long)uifac_skipped,(long long)uifac_scopes,(long long)uifac_scope_same,
        (long long)uifac_tokens_mapped,(long long)uifac_tokens_unknown,(long long)uifac_views,(long long)uifac_views_nested,(long long)uifac_views_refused);
    return n>0 && (size_t)n<capacity?n:0;
}

/* ---- arming, the replay's first pass, its every pass ---- */
static int uifac_pins(void) {
    if(!uifac_pins_checked) {
        int ok=1;
        for(unsigned i=0;ok && i<uifac_pin_count;i++)
            if(!(ok=hash_ok(image+uifac_pin_tab[i].rva,uifac_pin_tab[i].size,uifac_pin_tab[i].sha))) pin_failed_rva=uifac_pin_tab[i].rva;
        uifac_pins_checked=ok?1:-1;
    }
    return uifac_pins_checked==1;
}
static int uifac_refuse(const char *why) {uifac_arm_why=why;return 0;}
/* After gate.ui_replay (with the family) and gate.ui_guard, before the guard installs: the claims (1, or 0 and why). */
static int uifac_arm_using(void) {
    if(uifac_armed) return 1;
    if(!uirep_armed || !(uirep_families&UIREP_FAMILY_FACTION)) return uifac_refuse("needs_family");
    if(!uiguard_armed) return uifac_refuse("needs_ui_guard");
    if(uiguard_installed) return uifac_refuse("after_install");
    static char missing[80];
    for(unsigned c=0;c<UIFAC_CLAIMS;c++) {
        unsigned found=UIGUARD_MAX;
        for(unsigned i=0;i<uiguard_count && i<UIGUARD_MAX;i++)
            if(uiguard_tab[i].kind==UIGUARD_FFI && !strcmp(uiguard_tab[i].name,uifac_claims[c].name)) found=i;
        if(found==UIGUARD_MAX) {snprintf(missing,sizeof(missing),"claim:%s",uifac_claims[c].name);return uifac_refuse(missing);}
        uifac_guard_of[c]=(unsigned short)found;
    }
    memset(uifac_claim_at,0,sizeof(uifac_claim_at));uiguard_unclaim(uifac_claim_decide);
    for(unsigned c=0;c<UIFAC_CLAIMS;c++) {
        if(!uiguard_claim(uifac_guard_of[c],uifac_claim_decide)) {   /* another family's entry: nothing claimed */
            uiguard_unclaim(uifac_claim_decide);memset(uifac_claim_at,0,sizeof(uifac_claim_at));
            snprintf(missing,sizeof(missing),"%s:%s",uiguard_claim_why,uifac_claims[c].name);return uifac_refuse(missing);
        }
        uifac_claim_at[uifac_guard_of[c]]=&uifac_claims[c];
    }
    memset(uifac_rx_slot,0,sizeof(uifac_rx_slot));memset(uifac_map,0,sizeof(uifac_map));uifac_map_count=uifac_map_next=0;
    uifac_seq=0;uifac_token_seq=0;uifac_hash=0;uifac_ring_head=uifac_ring_tail=0;
    uifac_captured=uifac_refused=uifac_records_out=uifac_parts_out=uifac_dropped=uifac_records_in=uifac_parts_in=uifac_torn=0;
    uifac_ran=uifac_skipped=uifac_scopes=uifac_scope_same=uifac_tokens_mapped=uifac_tokens_unknown=0;
    uifac_views=uifac_views_nested=uifac_views_refused=0;uifac_view_open=0;
    uifac_why="-";uifac_arm_why="-";uifac_armed=1;return 1;
}
static int uifac_arm(void) {
    if(uifac_armed) return uifac_arm_using();
    if(stats.issued) return uifac_refuse("after_first_tick");
    if(GetCurrentThreadId()!=ui_thread) return uifac_refuse("thread");
    static char pin_why[40];
    if(!uifac_pins()) {snprintf(pin_why,sizeof(pin_why),"pins_ui_faction:%x",pin_failed_rva);return uifac_refuse(pin_why);}
    if(!uitrade_pins()) {snprintf(pin_why,sizeof(pin_why),"pins_ui_trade:%x",pin_failed_rva);return uifac_refuse(pin_why);}   /* the scope, the owner */
    uifac_fn.entry=NULL;
    return uifac_arm_using();
}
/* The replay's first pass (fail closed): the family named but never armed would leave its entries to the guard's groups. */
static int uifac_ready(void) {
    if(!(uirep_families&UIREP_FAMILY_FACTION)) return 1;
    if(!uifac_armed) {uifac_why="ui_faction_not_armed";return 0;}
    if(!uiguard_armed) {uifac_why="ui_faction_need_guard";return 0;}
    return 1;
}
/* Every pass: neither a faction scope nor the view is open between frames (the trade family checks the scope too when armed). */
static int uifac_pass(uint64_t tick) {
    (void)tick;
    if(!uifac_armed) return 1;
    if(uifac_view_open) {uifac_view_leave();uifac_why="ui_faction_view_open";return 0;}
    if(uitrade_scope_open) {uitrade_scope_leave();uifac_why="ui_faction_scope_open";return 0;}
    if(!pss_pass(tick)) {uifac_why=pss_why;return 0;}
    return 1;
}
#include "lockstep_player_settings.h"   /* native_gate_player_settings_per_slot: slot k's own player settings */
