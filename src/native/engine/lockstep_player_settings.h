/* Player settings per slot, DEFAULT OFF (native_gate_player_settings_per_slot = true; needs the UI replay's faction family,
 * lockstep_ui_faction.h; stop reason 164 like the replay, whys player_settings_*). Factions mode: every slot k whose faction
 * (the owner of its ship, x4lan_playerk) is not the player faction has its own values of the eight settings X4 keeps on its
 * single player object or reads through it, changes them through X4's own setters and sees them in its menus; any number of
 * slots (PILOT_SLOTS). Coop and slot 1: nothing changes (every slot is `player`, X4's own player holds the values).
 * Report: reports/LOCKSTEP-UI-FACTION-20261101.txt section 13.
 *
 *   storage   [F 9.00] U::Player (Universe+0x230): +0x6bb8 paint theme, +0x6bc0 the last valid one, +0x6bc8 clothing theme,
 *             +0x6bd0 the last valid one (theme library objects; 0x70a720 / 0x70a910 set them); +0x8a6a8 global loadout
 *             level, +0x8a6ac quantity, +0x8a6b0 quality (floats), +0x8a6b4 ships wait, +0x8a6b5 taxi waits; +0x8a6d0 the
 *             illegal wares (a vector of ware pointers: 4 inline, capacity +0x20, size +0x28; push_back 0xcb8c0); +0x8a720
 *             trade loop cargo reservation. The logo is the owner FACTION's (faction data +0x280, +0x578 file, +0x598
 *             personal): SetPlayerLogo writes the faction the player's vfunc 706 answers (+0x2c8 the cover, else +0x2c0).
 *             Not a slot's: +0x8a6ac global loadout quantity and +0x8a6b0 quality. X4's player's constructor writes the
 *             level and the waits (0x6faffe, 0x6fb004) and never these two, the load (0x6fdb79, 0x6fdba8/0x6fdbc0) and the
 *             save (0x701757, 0x7017cc/0x7017f5) skip them; only their setters (0x1dcf93/0x1dcfc3, called by no stock Lua
 *             or script) write them, so they hold heap bytes of each process (census f3set: client 2's "a3b25632", client
 *             1's zero, ;P2 differed from tick 1). They stay X4's: not in a slot's table, never exchanged, not in the
 *             receipt or the rows.
 *             Slot k's values live in a side table here (pss_slots), not on P_k (lockstep_pilot_slots.h): no reader ever
 *             reads P_k's copy (the simulation's readers resolve the owner faction, never the singleton P_k), P_k exists
 *             only while slot k is seated and is built anew per seat, and the ware vector owns X4 heap memory. A slot's
 *             table is filled once, when its faction is first known, from X4's player's own fields (the save's values, also
 *             while a scope holds another slot's in them; the vector copied through X4's own push_back), the same on every
 *             client whichever slot it presents.
 *   scopes    the canonical player's settings fields hold slot k's values for the length of
 *               apply    the faction family's replayed entity setter of slot k (uifac_run, every client, inside slot k's
 *                        faction scope), with the cover +0x2c8 = slot k's faction (SetPlayerLogo writes slot k's faction's
 *                        logo); the result goes back into slot k's table;
 *               view     the faction view (client k's player information menu, uifac_view_enter), with the cover;
 *               present  the presentation scope (lockstep_pilot_present.h, client k's whole UI, every frame), no cover.
 *             Every byte is back at the scope's end (a UI scope that changed one is counted, refused=, and discarded).
 *   readers   [F] the simulation reads the settings per owner faction through three functions, replaced or continued at
 *             their entries (an E9 jump to a stub on a near page, CAS, pinned whole bodies):
 *               0x6d83b0  paint theme of (object, faction): build processor 0x393d70, ShipGenerator InitializeShip
 *                         0xc7e820, the setter's repaint 0x70a720, gamestart, ventures; replaced in C;
 *               0x6bf530  clothing theme of (NPC, faction): NPC vfunc 179, NPC generator 0xc76bc0, the setter's 0x70a910;
 *                         the theme chosen in C, X4's body continues (0x6bf5ad, the default at 0x6bf59d);
 *               0x8fa710  is ware illegal to (ware, faction, licence holder): every legality check (scans, trade filters,
 *                         MD/AI ware lists, Lua IsWareIllegalTo, WareFilterByLegality); replaced in C.
 *             Each resolves the faction: slot k's (k >= 2, its table in use) answers slot k's value (the live field while
 *             slot k's scope is open); the player faction while a scope is open answers X4's player's saved value;
 *             everything else exactly as X4 does. The same world gives the same answer on every client.
 *             Not readers of slot k's ships [F scripts]: ships/taxi wait (move.undock, order.wait.signal, orders.base) and
 *             the global loadout level (interrupt.restock, player.loadoutlevel) are read only for `isplayerowned` ships
 *             through the `player` keyword (Scripts::EvaluateKeywordProperty 0xcd5910 -> 0x1548b0..0x1548f0); the trade
 *             loop cargo reservation (+0x8a720, the per-ship overrides +0x8a730) only by GetShipTradeLoopCargoReservation-
 *             Setting (the map's menus). Those are stored, replayed and shown per slot.
 *   menus     in the view and the presentation scope the stock getters read slot k's fields (GetPlayerPaintTheme,
 *             GetPlayerClothingTheme, ShouldPlayer*WaitForPlayer, GetPlayerGlobalLoadoutLevel, GetPlayerGlobalTradeLoop-
 *             CargoReservationSetting, GetWareData "playerillegal"; in the view GetCurrentPlayerLogo through the cover),
 *             and four getters by faction ID answer slot k's faction for "player" (an entry stub rewrites the argument:
 *             GetDefaultResponseToSignalForFaction2, HasDefaultResponseToSignalForFaction, GetAskToSignalForFaction,
 *             GetFactionDefaultWeaponMode). The build method: GetPlayerBuildMethod reads g_PlayerFaction [F 0x1d73b0], which
 *             the view makes slot k's.
 *   receipt   ;P<k>:<ships wait><taxi waits><reserve>.<level>.<wares>.<paint>.<clothing>.<logo> per slot 2..N (float
 *             bits, FNV-1a 32 of the theme IDs and the logo file; x = not in use) joins UIFAC_1; the family's ;w (X4's player)
 *             reads X4's own values while a scope holds a slot's.
 * Fail closed: a pin that differs (the readers' bodies, the setters and getters whose fields the scopes exchange, the owner
 * vfunc, the Player vtable slot), an entry not as pinned at the install, a patch lost (stop 164 player_settings_hook_lost),
 * a scope left open at a pass or changed inside an apply (player_settings_scope*). */
#define PSS_COVER 0x2c8u                     /* Entity: the cover faction (vfunc 705 HasCover, 706 GetOwner, 707 GetCover) */
#define PSS_PLAYER_VTABLE 0x2b329a8u         /* U::Player */
#define PSS_OWNER_SLOT 706u
#define PSS_OWNER_FN 0x4b7680u
#define PSS_PAINT_FN 0x6d83b0u
#define PSS_CLOTHING_FN 0x6bf530u
#define PSS_CLOTHING_GO 0x6bf5adu            /* r9 = the theme, rbx the faction, rdi the NPC, the frame set up */
#define PSS_CLOTHING_DEFAULT 0x6bf59du       /* the NPC macro's default theme */
#define PSS_CLOTHING_STOCK 0x6bf535u         /* behind the first instruction */
#define PSS_ILLEGAL_FN 0x8fa710u
#define PSS_MACRO_PROPS 0x7a93f0u            /* (macro) -> its properties, +0x7c0 the default paint theme */
#define PSS_HAS_LICENCE 0x9dd220u            /* U::FactionClass::HasLicence(holder, type, faction) */
#define PSS_PUSH_BACK 0xcb8c0u               /* the ware vector's push_back(vector, &ware) */
#define PSS_THEME_ID 0x10u                   /* a theme: its ID (std::string) */
#define PSS_FACTION_DATA 0x280u
#define PSS_LOGO_FILE 0x578u                 /* faction data: the logo file (std::string), +0x598 personal */
#define PSS_LOGO_PERSONAL 0x598u
#define PSS_WARE_LIST 0x148u                 /* a ware: the factions it is illegal to (vector, 2 inline, capacity +0x10, size +0x18) */
#define PSS_WARE_FLAGS 0x140u
#define PSS_WARE_LICENCE 0x168u
#define PSS_STUB 0x80u
#define PSS_PAGE 0x1000u
#define PSS_DEPTH 4u
#define PSS_ID_MAX 64u
/* The canonical player's fields a slot owns: where on U::Player, how many bytes, where in a slot's block. */
enum {PSS_R_THEMES=0,PSS_R_LEVEL,PSS_R_WAITS,PSS_R_WARES,PSS_R_RESERVE,PSS_REGIONS};
static const struct {unsigned offset,size,at;const char *name;} pss_regions[PSS_REGIONS]={
    {0x6bb8u,0x20u,0x00u,"themes"},          /* paint, its last valid, clothing, its last valid */
    {0x8a6a8u,0x04u,0x20u,"loadout_level"},  /* not quantity +0x8a6ac, quality +0x8a6b0 (above: no one's, X4's own) */
    {0x8a6b4u,0x02u,0x2cu,"waits"},          /* ships wait, taxi waits */
    {0x8a6d0u,0x30u,0x30u,"illegal_wares"},
    {0x8a720u,0x01u,0x60u,"loop_reserve"}};
#define PSS_BLOCK 0x70u
#define PSS_AT_PAINT 0x00u
#define PSS_AT_CLOTHING 0x10u
#define PSS_AT_LEVEL 0x20u
#define PSS_AT_WAITS 0x2cu
#define PSS_AT_WARES 0x30u
#define PSS_AT_RESERVE 0x60u
/* The four getters by faction ID whose "player" a menu of slot k means as slot k's faction: the argument rewritten, then
 * the entry's first instructions (copied, no RIP-relative operand) and a jump behind them. */
#define PSS_ARGS 4u
static const struct {unsigned rva;unsigned char arg,prologue;unsigned char bytes[8];const char *name;} pss_args[PSS_ARGS]={
    {0x18c7e0u,1,5,{0x48,0x89,0x6c,0x24,0x20},"GetDefaultResponseToSignalForFaction2"},
    {0x1b0b50u,1,7,{0x40,0x53,0x55,0x48,0x83,0xec,0x28},"HasDefaultResponseToSignalForFaction"},
    {0x186cd0u,1,6,{0x40,0x57,0x48,0x83,0xec,0x20},"GetAskToSignalForFaction"},
    {0x18e9d0u,0,7,{0x48,0x83,0xec,0x28,0x4c,0x8b,0xc9},"GetFactionDefaultWeaponMode"}};
/* Every patched entry: its first five bytes as pinned. */
enum {PSS_H_PAINT=0,PSS_H_CLOTHING,PSS_H_ILLEGAL,PSS_H_ARG0,PSS_HOOKS=PSS_H_ARG0+PSS_ARGS};
static const unsigned char pss_paint_orig[5]={0x48,0x83,0xec,0x28,0x48},pss_mov_rbx[5]={0x48,0x89,0x5c,0x24,0x08};
typedef struct {unsigned rva,size;const char *sha;} pss_pin;
static const pss_pin pss_pin_table[]={
    {0x6d83b0u,0x7c,"1a6ecb8b7edebf2ba4032ca0c64931eb964e03cf84bfe59d7a02629fca7534e0"},    /* paint theme of (object, faction): replaced */
    {0x8fa710u,0xfd,"7007c8989d0fcbcbfbc68a9d066c3131bca9246dcd82fa063c998ef0cf0b577d"},    /* illegal to (ware, faction, holder): replaced */
    {0x6bf530u,0x155,"d3596379a67be143097c2f915e32bc4fe719221bfaaab0f4be95ef411be4ecbf"},   /* clothing theme of (NPC, faction): continued */
    {0x18c7e0u,0x20,"7e7bbc579113a80e96696e53ba411fa388c15b1d360ff6755362a68ee1807a95"},    /* GetDefaultResponseToSignalForFaction2 */
    {0x1b0b50u,0x20,"2373cae1092ce8f98ddd27548ad3b2b9f152955c03fb715bba0e55504a35b061"},    /* HasDefaultResponseToSignalForFaction */
    {0x186cd0u,0x20,"ccf1aefb20c0a8f85a038dd7143ce846d40a476bb4ca97be66b5f0c0ec0b3fa4"},    /* GetAskToSignalForFaction */
    {0x18e9d0u,0x20,"5097373f06dce5315f8b786e0ba6db67ad14fedb3a1096b266f84b9e365bbcd7"},    /* GetFactionDefaultWeaponMode */
    {0x1b9810u,0x16a,"3cfa4c70e50def5ec12dbb9b0b022671194f2987fe89a3002f112573b866ec75"},   /* SetPlayerPaintTheme -> 0x70a720 */
    {0x1b9280u,0x16a,"d0d89c38e896606255da6244955123f3bf53dd2a3f4c455e3be5cd1a803a1cc4"},   /* SetPlayerClothingTheme -> 0x70a910 */
    {0x1b95c0u,0x102,"952aee02827ce1ccbdd992b2260f50d2bdc395672d56e2e6e6a6261c141765c2"},   /* SetPlayerIllegalWare: +0x8a6d0 */
    {0x1b96d0u,0x13c,"145d67c07aab361cd12f80975bc3ab515c03f58b1f3f2e741445f2e355b401ed"},   /* SetPlayerLogo: vfunc 706's faction */
    {0x70a720u,0x1ec,"8748021ae3010f3e111d7f428958964ccdfbb520f5144754ed524d10d3513375"},   /* +0x6bb8/+0x6bc0, repaints g_PlayerFaction's */
    {0x70a910u,0xe2,"4e37877b880661c560241a28be4c1d979ab9426ea673815436d04dab13f5104c"},    /* +0x6bc8/+0x6bd0, re-dresses g_PlayerFaction's */
    {0x154900u,0x5b,"6bca745d461be47da4b1922cec94666cfcc9ef36e7ee3a15a7a1a19f4b81deaf"},    /* the player's illegal ware test */
    {0x1a48c0u,0x46,"8142664caffa55e2bf0c153695134f76fddafb37264ea78b4aaed587493f6ddf"},    /* GetPlayerPaintTheme: +0x6bb8, ID at +0x10 */
    {0x1a4200u,0x46,"30b7eb613cae4984ad0532d7643767f454f1ef367daa895a97560c43276a8bb3"},    /* GetPlayerClothingTheme: +0x6bc8 */
    {0x18bda0u,0xfa,"ab919154dd467c7e34ab1ebdc9bca944e5c236869eec0e438600bf0a92147620"},    /* GetCurrentPlayerLogo: vfunc 706, +0x280 +0x578 */
    {0xcb8c0u,0x65,"b2f9352e8770022e5f4f53684e661cddb5b653ecf1ef4987d5d94d27acc1dee9"},     /* the vector's push_back */
    {0x4b7680u,0x14,"c90162d308a995551c3a6fbf4039d0df51ff24b81e7e4328eecf56d012db4dbd"},    /* vfunc 706: +0x2c8 else +0x2c0 */
    {0x1dcf70u,0x2c,"ebd09cdec7a3756b9f3682384c472394c3ecf870c48aa544e960b486047799c4"},    /* SetPlayerGlobalLoadoutQuantity: +0x8a6ac */
    {0x1d7910u,0x30,"d068b2b20aae7887d9dff7b24a4aa9e39a475e70b8993382a8357865d5878d02"}};   /* GetPlayerGlobalLoadoutQuantity: +0x8a6ac */
#define PSS_PIN_COUNT (sizeof(pss_pin_table)/sizeof(*pss_pin_table))
static const pss_pin *pss_pin_tab=pss_pin_table;   /* tests: none */
static unsigned pss_pin_count=PSS_PIN_COUNT;
typedef struct {
    unsigned char *(*macro_props)(void *);                     /* 0x7a93f0 */
    unsigned char (*has_licence)(void *,uint32_t,void *);      /* 0x9dd220 */
    void (*push_back)(void *,void *const *);                   /* 0xcb8c0 */
} pss_fns;
static pss_fns pss_fn;
typedef struct {
    __attribute__((aligned(16))) unsigned char values[PSS_BLOCK];
    void *volatile faction;          /* the owner of the slot's ship (the pass, every client the same); NULL none */
    volatile int used;               /* its faction is known and not the player faction: readers answer the table */
    int filled;                      /* its table holds values */
} pss_slot_t;
static pss_slot_t pss_slots[PILOT_SLOTS+1];
static int pss_armed,pss_installed,pss_pins_checked,pss_restored=1;
static unsigned pss_done;            /* bit per hook: patched */
static unsigned char *pss_page,pss_patched[PSS_HOOKS][5];
static const char *pss_stage="off",*pss_why="-",*pss_arm_why="-",*pss_refused_region="-";
static char pss_own_id[PSS_ID_MAX+1];   /* this client's slot faction's ID (the Lua half), for the getters' "player" */
/* The swap of the canonical player's fields (UI thread only; a stack: the view inside the presentation). */
enum {PSS_K_APPLY=1,PSS_K_VIEW=2,PSS_K_PRESENT=3};
static volatile unsigned pss_swap_slot;
static unsigned pss_swap_depth,pss_present_on;
static unsigned char pss_swap_kind[PSS_DEPTH],pss_swap_covered[PSS_DEPTH];
static unsigned char *pss_swap_player;
static __attribute__((aligned(16))) unsigned char pss_saved[PSS_BLOCK];
static void *pss_saved_cover,*pss_cover_faction;
static int pss_cover_on;
static volatile LONG64 pss_swaps[4],pss_refused,pss_reads_slot[3],pss_reads_player[3],pss_reads_stock[3],pss_rewrites,pss_fills,pss_fill_wares,
    pss_swap_refused;
static void pss_present_hook(int enter);

/* ---- the world, read only ---- */
static unsigned char *pss_player(void) {return pnat_player();}
static void *pss_load_ptr(const unsigned char *at) {void *p=NULL;memcpy(&p,at,8);return p;}
/* X4's player faction as the simulation knows it, also while a faction scope or the view has swapped g_PlayerFaction. */
static void *pss_true_player_faction(void) {
    if(uitrade_scope_open) return uitrade_scope_saved;
    if(uifac_view_open) return uifac_view_saved;
    return *uitrade_player_faction();
}
/* `faction` is X4's player faction for a reader: g_PlayerFaction (X4's own test), or, while a scope holds a slot's values
 * (and the faction scope or the view may have swapped g_PlayerFaction), the true player faction. */
static int pss_is_player(void *faction) {
    return faction==*uitrade_player_faction() || (pss_swap_slot && faction==pss_true_player_faction());
}
/* Slot k (2..N) whose table answers for `faction`, or 0. */
static unsigned pss_slot_of(void *faction) {
    if(!faction) return 0;
    for(unsigned k=2;k<=PILOT_SLOTS;k++) if(pss_slots[k].used && pss_slots[k].faction==faction) return k;
    return 0;
}
/* Slot k's bytes of a region as they are now: the canonical player's field while slot k's scope holds it, else its table. */
static const unsigned char *pss_live(unsigned k,unsigned at) {
    if(pss_swap_slot==k && pss_swap_player) {
        for(unsigned r=0;r<PSS_REGIONS;r++)
            if(at>=pss_regions[r].at && at<pss_regions[r].at+pss_regions[r].size) return pss_swap_player+pss_regions[r].offset+(at-pss_regions[r].at);
    }
    return pss_slots[k].values+at;
}
/* X4's player's own bytes (saved while a scope holds slot k's), NULL without a player. */
static const unsigned char *pss_canonical(unsigned at) {
    if(pss_swap_slot && pss_swap_player) return pss_saved+at;
    unsigned char *p=pss_player();
    if(!p) return NULL;
    for(unsigned r=0;r<PSS_REGIONS;r++)
        if(at>=pss_regions[r].at && at<pss_regions[r].at+pss_regions[r].size) return p+pss_regions[r].offset+(at-pss_regions[r].at);
    return NULL;
}
/* A vector of pointers with `inline_cap` inline (data at +0 or inline, capacity at cap_at, size at size_at): holds v. */
static int pss_vec_has(const unsigned char *vec,unsigned inline_cap,unsigned cap_at,unsigned size_at,const void *v) {
    uint64_t cap=0,size=0;memcpy(&cap,vec+cap_at,8);memcpy(&size,vec+size_at,8);
    const unsigned char *data=cap==inline_cap?vec:(const unsigned char*)pss_load_ptr(vec);
    for(uint64_t i=0;i<size && data;i++) if(pss_load_ptr(data+8*i)==v) return 1;
    return 0;
}
static uint64_t pss_vec_size(const unsigned char *vec) {uint64_t size=0;memcpy(&size,vec+0x28,8);return size;}
/* A std::string (MSVC: 16 inline, size +0x10, capacity +0x18): up to cap-1 bytes, 1, or 0 when unreadable. */
static int pss_string(const unsigned char *s,char *out,size_t cap) {
    uint64_t size=0,capacity=0;const unsigned char *data=s;out[0]=0;
    if(!s || !read_local(s+0x10,&size,8) || !read_local(s+0x18,&capacity,8)) return 0;
    if(capacity>=16 && !read_local(s,&data,8)) return 0;
    size_t n=size<cap-1?(size_t)size:cap-1;
    if(n && (!data || !read_local(data,out,n))) {out[0]=0;return 0;}
    out[n]=0;return 1;
}
static uint32_t pss_fnv32(const char *s) {uint32_t h=0x811c9dc5u;for(;*s;s++) {h^=(unsigned char)*s;h*=0x01000193u;}return h;}
/* A theme's ID ("" none). */
static void pss_theme_id(const unsigned char *theme,char *out,size_t cap) {out[0]=0;if(theme) pss_string(theme+PSS_THEME_ID,out,cap);}
/* A faction's logo file ("" none or unreadable) and whether it is personal. */
static void pss_logo(void *faction,char *out,size_t cap,unsigned *personal) {
    unsigned char *data=NULL,flag=0;out[0]=0;*personal=0;
    if(!faction || !read_local((unsigned char*)faction+PSS_FACTION_DATA,&data,8) || !data) return;
    pss_string(data+PSS_LOGO_FILE,out,cap);
    if(read_local(data+PSS_LOGO_PERSONAL,&flag,1)) *personal=flag!=0;
}

/* ---- the readers (any thread; the same world gives the same answer everywhere) ---- */
/* 0x6d83b0: the paint theme `object` gets for `faction` (NULL none). */
static void *pss_paint_for(unsigned char *object,void *faction) {
    unsigned k=pss_slot_of(faction);void *theme=NULL;
    if(k) {InterlockedIncrement64(&pss_reads_slot[0]);theme=pss_load_ptr(pss_live(k,PSS_AT_PAINT));if(theme) return theme;}
    else if(pss_is_player(faction)) {
        const unsigned char *own=pss_canonical(PSS_AT_PAINT);
        if(pss_swap_slot) InterlockedIncrement64(&pss_reads_player[0]);else InterlockedIncrement64(&pss_reads_stock[0]);
        if(own && (theme=pss_load_ptr(own))) return theme;
    } else InterlockedIncrement64(&pss_reads_stock[0]);
    unsigned char *props=pss_fn.macro_props(pss_load_ptr(object+0x60));
    return pss_load_ptr(props+0x7c0);
}
/* 0x6bf530's choice: 0 X4's own path, 1 the NPC macro's default theme, else the theme. */
static uintptr_t pss_clothing_for(void *npc,void *faction) {
    (void)npc;
    unsigned k=pss_armed?pss_slot_of(faction):0;void *theme;
    if(k) {InterlockedIncrement64(&pss_reads_slot[1]);theme=pss_load_ptr(pss_live(k,PSS_AT_CLOTHING));return theme?(uintptr_t)theme:1u;}
    if(pss_swap_slot && pss_is_player(faction)) {
        const unsigned char *own=pss_canonical(PSS_AT_CLOTHING);
        InterlockedIncrement64(&pss_reads_player[1]);theme=own?pss_load_ptr(own):NULL;return theme?(uintptr_t)theme:1u;
    }
    InterlockedIncrement64(&pss_reads_stock[1]);
    return 0;
}
/* 0x8fa710: ware illegal to `faction`, a licence `holder` may make it legal. */
static uint64_t pss_illegal_to(unsigned char *ware,void *faction,void *holder) {
    int illegal;
    if(!faction) illegal=pss_load_ptr(ware+PSS_WARE_LIST+0x18)!=NULL;
    else {
        unsigned k=pss_slot_of(faction);
        if(k) {InterlockedIncrement64(&pss_reads_slot[2]);illegal=pss_vec_has(pss_live(k,PSS_AT_WARES),4,0x20,0x28,ware);}
        else if(pss_is_player(faction)) {
            const unsigned char *own=pss_canonical(PSS_AT_WARES);
            if(pss_swap_slot) InterlockedIncrement64(&pss_reads_player[2]);else InterlockedIncrement64(&pss_reads_stock[2]);
            illegal=own?pss_vec_has(own,4,0x20,0x28,ware):0;
        } else {InterlockedIncrement64(&pss_reads_stock[2]);illegal=pss_vec_has(ware+PSS_WARE_LIST,2,0x10,0x18,faction);}
    }
    if(!(ware[PSS_WARE_FLAGS]&2) && !illegal) return 0;
    uint32_t type=0;memcpy(&type,ware+PSS_WARE_LICENCE,4);
    if(!type || !holder) return 1;
    return pss_fn.has_licence(holder,type,faction)?0:1;
}
/* The getters by faction ID: "player" in a menu of this client's slot (the view or the presentation open on the UI thread)
 * is this slot's faction. */
static const char *pss_faction_arg(const char *id) {
    char head[7];
    if(!pss_armed || !id || !pss_swap_slot || pss_swap_kind[0]==PSS_K_APPLY || !pss_own_id[0] || GetCurrentThreadId()!=ui_thread) return id;
    if(!read_local(id,head,7) || memcmp(head,"player",7)) return id;
    InterlockedIncrement64(&pss_rewrites);return pss_own_id;
}

/* ---- the stubs (a near page; nothing of ours stays on the stack while X4's code runs) ---- */
static unsigned pss_hook_rva(unsigned h) {return h==PSS_H_PAINT?PSS_PAINT_FN:h==PSS_H_CLOTHING?PSS_CLOTHING_FN:h==PSS_H_ILLEGAL?PSS_ILLEGAL_FN:pss_args[h-PSS_H_ARG0].rva;}
static const unsigned char *pss_hook_orig(unsigned h) {return h==PSS_H_PAINT?pss_paint_orig:h>=PSS_H_ARG0?pss_args[h-PSS_H_ARG0].bytes:pss_mov_rbx;}
static unsigned pss_put(unsigned char *p,unsigned n,const void *b,unsigned k) {memcpy(p+n,b,k);return n+k;}
static unsigned pss_abs(unsigned char *p,unsigned n,unsigned char op,unsigned slot) {   /* call/jmp [rip+d] to the address at p+slot */
    p[n++]=0xff;p[n++]=op;int32_t d=(int32_t)(slot-(n+4));memcpy(p+n,&d,4);return n+4;
}
static void pss_write_stub(unsigned char *p,unsigned h) {
    static const unsigned char save_xmm[]={0x0f,0x11,0x44,0x24,0x20,0x0f,0x11,0x4c,0x24,0x30,0x0f,0x11,0x54,0x24,0x40,0x0f,0x11,0x5c,0x24,0x50};
    static const unsigned char load_xmm[]={0x0f,0x10,0x44,0x24,0x20,0x0f,0x10,0x4c,0x24,0x30,0x0f,0x10,0x54,0x24,0x40,0x0f,0x10,0x5c,0x24,0x50};
    unsigned n=0;void *a[3]={NULL,NULL,NULL};
    memset(p,0xcc,PSS_STUB);
    if(h==PSS_H_PAINT || h==PSS_H_ILLEGAL) {
        /* The whole function in C: r9..r11 and xmm0..5 kept as the native ones leave them. */
        static const unsigned char head[]={0x41,0x51,0x41,0x52,0x41,0x53,0x48,0x81,0xec,0x80,0,0,0,
            0x0f,0x11,0x64,0x24,0x60,0x0f,0x11,0x6c,0x24,0x70};
        static const unsigned char tail[]={0x0f,0x10,0x64,0x24,0x60,0x0f,0x10,0x6c,0x24,0x70,0x48,0x81,0xc4,0x80,0,0,0,0x41,0x5b,0x41,0x5a,0x41,0x59,0xc3};
        n=pss_put(p,n,head,sizeof(head));n=pss_put(p,n,save_xmm,sizeof(save_xmm));
        n=pss_abs(p,n,0x15,0x68);
        n=pss_put(p,n,load_xmm,sizeof(load_xmm));n=pss_put(p,n,tail,sizeof(tail));
        a[0]=h==PSS_H_PAINT?(void*)pss_paint_for:(void*)pss_illegal_to;memcpy(p+0x68,&a[0],8);
    } else if(h==PSS_H_CLOTHING) {
        /* The choice in C, then X4's own frame and body: its theme path, its default path, or X4's own from the start. */
        static const unsigned char call_head[]={0x51,0x52,0x41,0x50,0x41,0x51,0x48,0x83,0xec,0x28};
        static const unsigned char call_tail[]={0x48,0x83,0xc4,0x28,0x41,0x59,0x41,0x58,0x5a,0x59,0x48,0x85,0xc0,0x74,0x25,
            0x48,0x89,0x5c,0x24,0x08,0x57,0x48,0x83,0xec,0x20,0x48,0x8b,0xda,0x48,0x8b,0xf9,0x48,0x83,0xf8,0x01,0x74,0x09,0x4c,0x8b,0xc8};
        n=pss_put(p,n,call_head,sizeof(call_head));n=pss_abs(p,n,0x15,0x60);
        n=pss_put(p,n,call_tail,sizeof(call_tail));
        n=pss_abs(p,n,0x25,0x68);                                   /* theme: 0x6bf5ad */
        n=pss_abs(p,n,0x25,0x70);                                   /* default: 0x6bf59d */
        n=pss_put(p,n,pss_mov_rbx,5);n=pss_abs(p,n,0x25,0x78);      /* X4's own: 0x6bf535 */
        a[0]=(void*)pss_clothing_for;memcpy(p+0x60,&a[0],8);
        a[0]=pnat_base+PSS_CLOTHING_GO;a[1]=pnat_base+PSS_CLOTHING_DEFAULT;a[2]=pnat_base+PSS_CLOTHING_STOCK;memcpy(p+0x68,a,24);
    } else {
        /* The faction ID argument rewritten in its saved register, then the entry's own first instructions. */
        unsigned i=h-PSS_H_ARG0;unsigned char off=(unsigned char)(0x68+(pss_args[i].arg?0x10:0x18));
        static const unsigned char head[]={0x51,0x52,0x41,0x50,0x41,0x51,0x48,0x83,0xec,0x68};
        static const unsigned char tail[]={0x48,0x83,0xc4,0x68,0x41,0x59,0x41,0x58,0x5a,0x59};
        n=pss_put(p,n,head,sizeof(head));n=pss_put(p,n,save_xmm,sizeof(save_xmm));
        unsigned char load[]={0x48,0x8b,0x8c,0x24,off,0,0,0},store[]={0x48,0x89,0x84,0x24,off,0,0,0};
        n=pss_put(p,n,load,8);n=pss_abs(p,n,0x15,0x70);n=pss_put(p,n,store,8);
        n=pss_put(p,n,load_xmm,sizeof(load_xmm));n=pss_put(p,n,tail,sizeof(tail));
        n=pss_put(p,n,pss_args[i].bytes,pss_args[i].prologue);n=pss_abs(p,n,0x25,0x78);
        a[0]=(void*)pss_faction_arg;a[1]=pnat_base+pss_args[i].rva+pss_args[i].prologue;memcpy(p+0x70,a,16);
    }
}
static int32_t pss_rel(unsigned h) {return (int32_t)((intptr_t)(pss_page+PSS_STUB*h)-(intptr_t)(pnat_base+pss_hook_rva(h)+5));}
/* 0 X4's bytes, 1 ours, -1 anything else. */
static int pss_state_of(unsigned h) {
    unsigned char now[5];if(!read_local(pnat_base+pss_hook_rva(h),now,5)) return -1;
    if(!memcmp(now,pss_hook_orig(h),5)) return 0;
    if(pss_page && !memcmp(now,pss_patched[h],5)) return 1;
    return -1;
}
static unsigned char *pss_alloc_page(void) {
    uintptr_t at=(uintptr_t)(pnat_base+0x500000u)&~(uintptr_t)65535;
    for(uintptr_t distance=65536;distance<0x70000000;distance+=65536)
        for(int sign=-1;sign<=1;sign+=2) {
            uintptr_t candidate=sign<0?at-distance:at+distance;
            unsigned char *p=VirtualAlloc((void*)candidate,PSS_PAGE,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
            if(!p) continue;
            int ok=1;
            for(unsigned h=0;h<PSS_HOOKS && ok;h++) {intptr_t d=(intptr_t)p-(intptr_t)(pnat_base+pss_hook_rva(h)+5);ok=d>INT32_MIN+0x10000 && d<INT32_MAX-0x10000;}
            if(ok) return p;
            VirtualFree(p,0,MEM_RELEASE);
        }
    return NULL;
}
static void pss_uninstall(void) {
    for(unsigned h=0;h<PSS_HOOKS;h++) {
        if(!((pss_done>>h)&1u)) continue;
        if(pss_state_of(h)==1 && pview_cas(pss_hook_rva(h),pss_patched[h],pss_hook_orig(h),5)) pss_done&=~(1u<<h);
    }
    pss_restored=!pss_done;if(!pss_done) pss_installed=0;
}
static int pss_install(void) {
    if(!pss_page) pss_page=pss_alloc_page();
    if(!pss_page) return 0;
    for(unsigned h=0;h<PSS_HOOKS;h++) if(pss_state_of(h)!=0) return 0;
    DWORD old,ignored;if(!VirtualProtect(pss_page,PSS_PAGE,PAGE_READWRITE,&old)) return 0;
    for(unsigned h=0;h<PSS_HOOKS;h++) {
        pss_write_stub(pss_page+PSS_STUB*h,h);
        int32_t d=pss_rel(h);pss_patched[h][0]=0xe9;memcpy(pss_patched[h]+1,&d,4);
    }
    if(!VirtualProtect(pss_page,PSS_PAGE,PAGE_EXECUTE_READ,&ignored)) return 0;
    FlushInstructionCache(GetCurrentProcess(),pss_page,PSS_PAGE);
    pss_restored=0;pss_installed=1;
    for(unsigned h=0;h<PSS_HOOKS;h++) {
        if(!pview_cas(pss_hook_rva(h),pss_hook_orig(h),pss_patched[h],5)) {pss_uninstall();return 0;}
        pss_done|=1u<<h;
    }
    return 1;
}
static int pss_intact(void) {
    for(unsigned h=0;h<PSS_HOOKS;h++) {
        unsigned char want[PSS_STUB];pss_write_stub(want,h);
        if(pss_state_of(h)!=1 || memcmp(pss_page+PSS_STUB*h,want,PSS_STUB)) return 0;
    }
    return 1;
}

/* ---- the swap (UI thread): the canonical player's fields hold slot k's values ---- */
static int pss_slot_ready(unsigned slot) {return slot>=2 && slot<=PILOT_SLOTS && pss_slots[slot].used && pss_slots[slot].filled;}
/* 1 entered, 0 refused (counted): another slot's scope is open, an apply inside anything, nothing to hold. */
static int pss_swap_enter(unsigned slot,unsigned kind,int cover) {
    unsigned char *p=pss_swap_depth?pss_swap_player:pss_player();
    if(!pss_armed || !p || !pss_slot_ready(slot) || pss_swap_depth>=PSS_DEPTH || GetCurrentThreadId()!=ui_thread ||
       (pss_swap_depth && (pss_swap_slot!=slot || kind==PSS_K_APPLY || pss_swap_kind[0]==PSS_K_APPLY))) {
        InterlockedIncrement64(&pss_swap_refused);return 0;
    }
    if(!pss_swap_depth) {
        for(unsigned r=0;r<PSS_REGIONS;r++) {
            memcpy(pss_saved+pss_regions[r].at,p+pss_regions[r].offset,pss_regions[r].size);
            memcpy(p+pss_regions[r].offset,pss_slots[slot].values+pss_regions[r].at,pss_regions[r].size);
        }
        pss_swap_player=p;pss_swap_slot=slot;
    }
    unsigned char covered=0;
    if(cover && !pss_cover_on) {
        pss_saved_cover=pss_load_ptr(p+PSS_COVER);pss_cover_faction=pss_slots[slot].faction;
        memcpy(p+PSS_COVER,&pss_cover_faction,8);pss_cover_on=1;covered=1;
    }
    pss_swap_kind[pss_swap_depth]=(unsigned char)kind;pss_swap_covered[pss_swap_depth]=covered;++pss_swap_depth;
    InterlockedIncrement64(&pss_swaps[kind]);
    return 1;
}
/* 1 left as entered; 0 the innermost scope was another kind, or the cover changed inside (everything is put back). A
 * committed apply's values go into the slot's table; a UI scope's changes are refused (counted) and discarded. */
static int pss_swap_close(unsigned kind,int commit) {
    if(!pss_swap_depth) return 0;
    unsigned char *p=pss_swap_player;int ok=pss_swap_kind[pss_swap_depth-1]==kind;
    --pss_swap_depth;
    if(pss_swap_covered[pss_swap_depth]) {
        if(pss_load_ptr(p+PSS_COVER)!=pss_cover_faction) ok=0;
        memcpy(p+PSS_COVER,&pss_saved_cover,8);pss_cover_on=0;pss_saved_cover=pss_cover_faction=NULL;
    }
    if(!pss_swap_depth) {
        pss_slot_t *s=&pss_slots[pss_swap_slot];
        for(unsigned r=0;r<PSS_REGIONS;r++) {
            unsigned char *field=p+pss_regions[r].offset,*mine=s->values+pss_regions[r].at;
            /* The ware vector always: X4 may have moved its heap block, the table owns what the field holds now. */
            if(kind==PSS_K_APPLY && ((ok && commit) || r==PSS_R_WARES)) memcpy(mine,field,pss_regions[r].size);
            else if(kind==PSS_K_APPLY) {}   /* a fault or a changed cover: the session stops, the table stays */
            else if(memcmp(mine,field,pss_regions[r].size)) {InterlockedIncrement64(&pss_refused);pss_refused_region=pss_regions[r].name;}
            memcpy(field,pss_saved+pss_regions[r].at,pss_regions[r].size);
        }
        pss_swap_slot=0;pss_swap_player=NULL;
    }
    return ok;
}
/* Everything back at once (a pass that finds a scope open, a stop). */
static void pss_swap_unwind(void) {
    while(pss_swap_depth) pss_swap_close(pss_swap_kind[pss_swap_depth-1],0);
    pss_present_on=0;
}
/* The presentation scope's part (lockstep_pilot_present.h pres_enter / pres_leave). */
static void pss_present_hook(int enter) {
    if(enter) {if(!pss_present_on && pss_slot_ready(pres_slot)) pss_present_on=pss_swap_enter(pres_slot,PSS_K_PRESENT,0);}
    else if(pss_present_on) {pss_present_on=0;if(!pss_swap_close(PSS_K_PRESENT,0)) {pss_why="player_settings_present";uirep_async_fail("player_settings_present");}}
}
/* The faction family's apply and view (lockstep_ui_faction.h). */
static int pss_apply_enter(unsigned slot) {return pss_swap_enter(slot,PSS_K_APPLY,1);}
static int pss_apply_leave(int commit) {return pss_swap_close(PSS_K_APPLY,commit);}
static int pss_view_enter(unsigned slot) {return pss_armed && pss_slot_ready(slot)?pss_swap_enter(slot,PSS_K_VIEW,1):0;}
static int pss_view_leave(void) {return pss_swap_close(PSS_K_VIEW,0);}

/* ---- slots: their factions, their tables (the pass, every client the same) ---- */
/* Slot k's table from X4's player's own fields (the saved bytes while a scope holds a slot's values in them): the regions
 * as they are, the ware vector rebuilt through X4's own push_back; nothing else of the block (zero). */
static int pss_fill(unsigned k) {
    pss_slot_t *s=&pss_slots[k];
    if(!pss_player()) return 0;
    memset(s->values,0,PSS_BLOCK);
    for(unsigned r=0;r<PSS_REGIONS;r++) if(r!=PSS_R_WARES) memcpy(s->values+pss_regions[r].at,pss_canonical(pss_regions[r].at),pss_regions[r].size);
    unsigned char *vec=s->values+PSS_AT_WARES;uint64_t four=4;memcpy(vec+0x20,&four,8);   /* empty, inline */
    const unsigned char *own=pss_canonical(PSS_AT_WARES);uint64_t cap=0,size=pss_vec_size(own);memcpy(&cap,own+0x20,8);
    const unsigned char *data=cap==4?own:(const unsigned char*)pss_load_ptr(own);
    for(uint64_t i=0;i<size && data;i++) {void *ware=pss_load_ptr(data+8*i);pss_fn.push_back(vec,&ware);InterlockedIncrement64(&pss_fill_wares);}
    s->filled=1;InterlockedIncrement64(&pss_fills);
    return 1;
}
static void pss_refresh(void) {
    void *player=pss_true_player_faction();
    for(unsigned k=2;k<=PILOT_SLOTS;k++) {
        pss_slot_t *s=&pss_slots[k];
        void *f=k<=pilot_slot_count?uitrade_slot_faction(k):NULL;
        s->faction=f;
        if(f && f!=player && !s->filled) pss_fill(k);
        s->used=f && f!=player && s->filled;
    }
}

/* ---- receipt, state, status ---- */
/* ;P<k>:<flags>.<level>.<wares>.<paint>.<clothing>.<logo>,.. for slots 2..N ('x' not in use): a slot's own values only. */
static int pss_frame_text(char *out,size_t capacity) {
    int n=snprintf(out,capacity,";P");
    for(unsigned k=2;k<=pilot_slot_count && k<=PILOT_SLOTS && n>0 && (size_t)n<capacity;k++) {
        pss_slot_t *s=&pss_slots[k];
        if(!s->used) {n+=snprintf(out+n,capacity-(size_t)n,"%s%u:x",k>2?",":"",k);continue;}
        const unsigned char *v=s->values;uint32_t level;memcpy(&level,v+PSS_AT_LEVEL,4);
        char paint[PSS_ID_MAX],cloth[PSS_ID_MAX],logo[160];unsigned personal=0;
        pss_theme_id(pss_load_ptr(v+PSS_AT_PAINT),paint,sizeof(paint));pss_theme_id(pss_load_ptr(v+PSS_AT_CLOTHING),cloth,sizeof(cloth));
        pss_logo(s->faction,logo,sizeof(logo),&personal);
        n+=snprintf(out+n,capacity-(size_t)n,"%s%u:%u%u%u.%08x.%llu.%08x.%08x.%08x%u",k>2?",":"",k,v[PSS_AT_WAITS]!=0,v[PSS_AT_WAITS+1]!=0,
            v[PSS_AT_RESERVE]!=0,(unsigned)level,(unsigned long long)pss_vec_size(v+PSS_AT_WARES),
            paint[0]?(unsigned)pss_fnv32(paint):0u,cloth[0]?(unsigned)pss_fnv32(cloth):0u,logo[0]?(unsigned)pss_fnv32(logo):0u,personal);
    }
    return n>0 && (size_t)n<capacity?n:0;
}
/* The faction family's ;w (X4's player's waits, cargo reservation and level) while a scope holds a slot's values in X4's
 * player's fields (client k's presentation, its view): X4's own, saved. 1 answered, 0 no scope (the fields are X4's). */
static int pss_entity_own(unsigned char flags[3],uint32_t *loadout) {
    if(!pss_armed || !pss_swap_slot || !pss_swap_player) return 0;
    flags[0]=pss_saved[PSS_AT_WAITS];flags[1]=pss_saved[PSS_AT_WAITS+1];flags[2]=pss_saved[PSS_AT_RESERVE];memcpy(loadout,pss_saved+PSS_AT_LEVEL,4);
    return 1;
}
/* Readable, per slot: slot 1 X4's player's own values, slots 2..N their tables. */
static int pss_state_text(char *out,size_t capacity) {
    int n=snprintf(out,capacity,"%s","");
    for(unsigned k=1;k<=pilot_slot_count && k<=PILOT_SLOTS && n>=0 && (size_t)n<capacity;k++) {
        const unsigned char *v;unsigned char own[PSS_BLOCK];void *faction;
        if(k==1) {
            if(!pss_player()) {n+=snprintf(out+n,capacity-(size_t)n,"%sslot=1 none",k>1?" ":"");continue;}
            memset(own,0,sizeof(own));
            for(unsigned r=0;r<PSS_REGIONS;r++) memcpy(own+pss_regions[r].at,pss_canonical(pss_regions[r].at),pss_regions[r].size);
            v=own;faction=pss_true_player_faction();
        } else {
            if(!pss_slots[k].used) {n+=snprintf(out+n,capacity-(size_t)n," slot=%u x",k);continue;}
            v=pss_slots[k].values;faction=pss_slots[k].faction;
        }
        float level;memcpy(&level,v+PSS_AT_LEVEL,4);
        char paint[PSS_ID_MAX],cloth[PSS_ID_MAX],logo[96];unsigned personal=0;
        pss_theme_id(pss_load_ptr(v+PSS_AT_PAINT),paint,sizeof(paint));pss_theme_id(pss_load_ptr(v+PSS_AT_CLOTHING),cloth,sizeof(cloth));
        pss_logo(faction,logo,sizeof(logo),&personal);
        n+=snprintf(out+n,capacity-(size_t)n,"%sslot=%u wait=%u/%u reserve=%u loadout=%.3f illegal=%llu paint=%s clothing=%s logo=%s%s",k>1?" ":"",k,
            v[PSS_AT_WAITS]!=0,v[PSS_AT_WAITS+1]!=0,v[PSS_AT_RESERVE]!=0,(double)level,(unsigned long long)pss_vec_size(v+PSS_AT_WARES),
            paint[0]?paint:"-",cloth[0]?cloth:"-",logo[0]?logo:"-",personal?"/personal":"");
    }
    return n>0 && (size_t)n<capacity?n:0;
}
/* Slot k's paint and clothing theme IDs (the spawn check's expected values; k=1 X4's player). */
static void pss_themes_of(unsigned k,char *paint,char *cloth,size_t cap) {
    paint[0]=cloth[0]=0;
    const unsigned char *pv=NULL,*cv=NULL;
    if(k==1) {pv=pss_canonical(PSS_AT_PAINT);cv=pss_canonical(PSS_AT_CLOTHING);}
    else if(k<=PILOT_SLOTS && pss_slots[k].used) {pv=pss_slots[k].values+PSS_AT_PAINT;cv=pss_slots[k].values+PSS_AT_CLOTHING;}
    if(pv) pss_theme_id(pss_load_ptr(pv),paint,cap);
    if(cv) pss_theme_id(pss_load_ptr(cv),cloth,cap);
}
static int pss_status(char *out,size_t capacity) {
    unsigned used=0,filled=0;for(unsigned k=2;k<=PILOT_SLOTS;k++) {used+=pss_slots[k].used!=0;filled+=pss_slots[k].filled!=0;}
    int n=snprintf(out,capacity,"player_settings=%s why=%s slots=%u/%u fills=%lld/%lld swaps=%lld/%lld/%lld refused=%lld/%s swap_refused=%lld "
        "reads=paint:%lld/%lld/%lld,clothing:%lld/%lld/%lld,illegal:%lld/%lld/%lld rewrites=%lld own=%s installed=%d restored=%d",pss_stage,pss_why,used,filled,
        (long long)pss_fills,(long long)pss_fill_wares,(long long)pss_swaps[PSS_K_APPLY],(long long)pss_swaps[PSS_K_VIEW],(long long)pss_swaps[PSS_K_PRESENT],
        (long long)pss_refused,pss_refused_region,(long long)pss_swap_refused,
        (long long)pss_reads_slot[0],(long long)pss_reads_player[0],(long long)pss_reads_stock[0],(long long)pss_reads_slot[1],(long long)pss_reads_player[1],
        (long long)pss_reads_stock[1],(long long)pss_reads_slot[2],(long long)pss_reads_player[2],(long long)pss_reads_stock[2],(long long)pss_rewrites,
        pss_own_id[0]?pss_own_id:"-",pss_installed,pss_restored);
    return n>0 && (size_t)n<capacity?n:0;
}

/* ---- pins, arming, the pass, the stop ---- */
static int pss_pins(void) {
    if(!pss_pins_checked) {
        int ok=1;
        for(unsigned i=0;ok && i<pss_pin_count;i++)
            if(!(ok=hash_ok(image+pss_pin_tab[i].rva,pss_pin_tab[i].size,pss_pin_tab[i].sha))) pin_failed_rva=pss_pin_tab[i].rva;
        /* The canonical player's vtable slot 706 is the pinned owner function (SetPlayerLogo, GetCurrentPlayerLogo call it). */
        if(ok && pss_pin_count) {
            unsigned char *slot=NULL;memcpy(&slot,image+PSS_PLAYER_VTABLE+8u*PSS_OWNER_SLOT,8);
            if(!(ok=slot==image+PSS_OWNER_FN)) pin_failed_rva=PSS_PLAYER_VTABLE;
        }
        pss_pins_checked=ok?1:-1;
    }
    return pss_pins_checked==1;
}
static int pss_refuse(const char *why) {pss_arm_why=why;return 0;}
static void pss_use_image(void) {
    pss_fn.macro_props=(unsigned char*(*)(void*))(void*)(image+PSS_MACRO_PROPS);
    pss_fn.has_licence=(unsigned char(*)(void*,uint32_t,void*))(void*)(image+PSS_HAS_LICENCE);
    pss_fn.push_back=(void(*)(void*,void*const*))(void*)(image+PSS_PUSH_BACK);
}
/* After gate.ui_faction, before the first tick (UI thread): 1, or 0 and why. The hooks go in at the replay's first pass. */
static int pss_arm_using(void) {
    if(pss_armed) return 1;
    if(!uifac_armed) return pss_refuse("needs_ui_faction");
    if(!pss_fn.macro_props || !pss_fn.has_licence || !pss_fn.push_back) return pss_refuse("functions");
    for(unsigned h=0;h<PSS_HOOKS;h++) if(pss_state_of(h)!=0) return pss_refuse("entry_changed");
    memset(pss_slots,0,sizeof(pss_slots));pss_own_id[0]=0;
    pss_swap_slot=0;pss_swap_depth=0;pss_swap_player=NULL;pss_cover_on=0;pss_present_on=0;pss_done=0;pss_installed=0;pss_restored=1;
    memset((void*)pss_swaps,0,sizeof(pss_swaps));memset((void*)pss_reads_slot,0,sizeof(pss_reads_slot));memset((void*)pss_reads_player,0,sizeof(pss_reads_player));
    memset((void*)pss_reads_stock,0,sizeof(pss_reads_stock));pss_refused=pss_rewrites=pss_fills=pss_fill_wares=pss_swap_refused=0;
    pss_stage="armed";pss_why="-";pss_arm_why="-";pss_refused_region="-";
    pss_armed=1;pres_settings_hook=pss_present_hook;
    pss_refresh();
    return 1;
}
static int pss_arm(void) {
    if(pss_armed) return pss_arm_using();
    if(stats.issued) return pss_refuse("after_first_tick");
    if(GetCurrentThreadId()!=ui_thread) return pss_refuse("thread");
    static char pin_why[40];
    if(!pss_pins()) {snprintf(pin_why,sizeof(pin_why),"pins_player_settings:%x",pin_failed_rva);return pss_refuse(pin_why);}
    pss_use_image();
    return pss_arm_using();
}
static int pss_fail(const char *why) {pss_stage="failed";if(!strcmp(pss_why,"-")) pss_why=why;return 0;}
/* The faction family's pass (every tick, frame thread, between frames): 0 stops (uifac_why = pss_why). */
static int pss_pass(uint64_t tick) {
    if(!pss_armed) return 1;
    if(pss_swap_depth) {pss_swap_unwind();return pss_fail("player_settings_scope_open");}
    if(tick>pilot_stop) {
        if(pss_installed) {pss_uninstall();if(!pss_restored) return pss_fail("player_settings_restore");pss_stage="stopped";}
        pres_settings_hook=NULL;return 1;
    }
    if(!pss_installed) {
        if(!pss_install()) return pss_fail("player_settings_install");
        pss_stage="live";
    } else if(!pss_intact()) return pss_fail("player_settings_hook_lost");
    pss_refresh();
    return 1;
}
static void pss_restore(void) {
    if(!pss_armed) return;
    pres_settings_hook=NULL;
    if(GetCurrentThreadId()==ui_thread) pss_swap_unwind();
    if(pss_installed) pss_uninstall();
    if(pss_installed) pss_stage="restore_refused";else if(strcmp(pss_stage,"failed")) pss_stage="stopped";
}
