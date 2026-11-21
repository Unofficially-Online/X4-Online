/* A player's own faction as "the player faction" on its own client (native_gate_faction_present, a session default;
 * factions mode, slots 2..N; needs the presentation scope, lockstep_pilot_present.h, and the trade family's owner walk).
 *
 * What was wrong (run fa1, 19 Nov 2026, two clients, a faction per player, each with a station of its own): player 2's
 * map listed player 1's station and ships under "Property Owned" and drew its own station as a foreign one. X4's menus
 * and HUD ask for the faction named "player" (193 places in the stock UI's Lua: GetContainedStationsByOwner("player"),
 * faction == "player", ...) and for "is this the player's" (g_PlayerFaction), and both are player 1's faction on
 * every client. The presentation scope gives client k slot k's ship and player; this gives it slot k's faction.
 *
 * X4 keeps its factions in one tree, ordered by the FNV hash of the faction's id; the faction object lies inside its
 * node and carries the hash and the id text itself:
 *   table      [image + 0x6cf15f8]; the tree's anchor at table + 0x10 (a node's first three words: its own address as
 *              the root's parent, the root at +8), the leftmost node at anchor + 0x20
 *   node       +0 parent, +8 left, +0x10 right, +0x18 colour, +0x20 key (the hash), +0x30 the faction
 *   faction    +0 vtable, +8 the hash again, +0x10 the id (a string of 0x20 bytes; both ids here are short ones, held
 *              inside it)
 * (the lookup is inlined wherever a faction is named: GetNumAllFactionShips 0x198910 from 0x198977 on; the id is read
 * at +0x10 in 0xafe073.) Read from a running client: 148 factions, "player" key 71c3ba713fbe3c2a, "x4lan_player2" key
 * 9a39ab21d5e005a0, each key equal to the hash inside its faction.
 *
 * For the length of a presentation scope of a slot whose faction is not the player faction, the two factions change
 * places: the two nodes exchange their place in the tree and their keys, the two factions their hash and id, and
 * g_PlayerFaction names the slot's. Inside the scope "player" finds the slot's faction, the slot's faction says its id
 * is "player", and player 1's faction is the one named x4lan_player<k>. At the scope's exit the same exchange again
 * puts every byte back, before any simulation code runs. The probe's own frame runs outside the scope (the presentation
 * pause), so every replayed call and every gate module sees the canonical names, the same on every client.
 * A world-changing call a menu makes inside the scope is captured, not run; where it names its own faction it says
 * "player", which the families already send as the slot's own (ReserveBuildPlot, the faction family's tokens). Not
 * covered: a captured call that names player 1's faction by id from client k (a trade rule that lists it): it would
 * carry the exchanged id.
 * Refused (nothing changes, refused= counts): no faction for the slot, the player faction is not named "player", a
 * node that the tree does not find under its key. swaps= counts the scopes, changed= a scope at whose exit
 * g_PlayerFaction was not the slot's any more, broken= an exit after which the tree did not find the player faction.
 * Other threads: the exchange is a few pointer writes on the frame thread, inside the UI's, the render's and the
 * pick's part of the frame; a faction lookup by id from another thread in that moment would be unsafe. None is known
 * (ids are named by scripts, the UI and the loader, all of the frame thread).
 *
 * The map stays the session's one map. X4 keeps "known to" per faction: an object or a space holds the set of the
 * factions that know it, and "is it known to the player" asks that set for g_PlayerFaction (IsKnownToPlayer 0x1b3290 ->
 * virtual +0x1798 -> 0x4b73b0(set, faction): a flag for all, the owner, else a small list of factions). Only X4's
 * player faction ever discovers anything, so with the exchange alone player 2's map named every sector "Unknown Sector"
 * and every station but its own "Unknown Station" (run fa2). 0x4b73b0 has eleven callers: the three virtual "known
 * to" functions (objects 0x6d3d50, spaces 0x7ed7c0, 0x554f00) and eight direct ones, among them a space's name
 * (0x7edc60: the name, or "Unknown Sector"; with the three virtual ones alone the sectors stayed unknown, run fa3).
 * Its entry goes through here (its first two instructions on a trampoline, X4's own code does the test), and inside
 * the scope a question about the slot's own faction is asked about the simulation's player faction as well: known
 * when either knows it
 * (the slot's own things are its own, the rest is what the session has discovered). known= counts the questions
 * answered by the shared knowledge.
 *
 * What a player knows of its own property. "Is this piece of information unlocked for the player" (the export
 * IsInfoUnlockedForPlayer and eleven more callers -> 0x643ef0(manager, kind, component)) compares the kind's threshold
 * with the object's revealed share, and a station keeps that share as one number (+0xd68, virtual +0x1940 with -1),
 * which X4 sets to 100 for the player faction's own. A station of x4lan_player<k> has 0 there: its owner's own
 * "Logical Station Overview" showed "???" in every node (run fa2), and so it shows on player 1's client. The entry of
 * 0x643ef0 goes through here: an object whose owner is a player's own faction (x4lan_player<k> by its id, or the
 * slot's own while the exchange holds its id) is unlocked, for every kind, on every client and in every part of the
 * frame alike (the answer does not depend on whose client asks, so a script that asks gets the same everywhere).
 * Players see each other's stations in full, as they do in coop. info= counts those answers.
 *
 * The simulation builds for a player's own faction as it builds for player 1's. X4's build processing asks what "the
 * player" may build (the blueprints of X4's one player, for X4's player faction; any other faction is an NPC faction,
 * which builds what its ware lists hold). Run tf2 (19 Nov): player 2's "Confirm module changes" ran on both clients,
 * X4 made a valid build task, and the station had no planned module at the next reading: the processor dropped the
 * plan of a faction that is no player's. The simulation's own call of "bring this container's builds up to a time"
 * (0x4280e0, the entry lockstep_ui_lazy.h already holds) for a container whose owner is slot k's own faction runs with
 * that faction as g_PlayerFaction and slot k's library in X4's player (the research module's exchange), and both go
 * back when it returns. The same on every client: it depends on the container's owner alone. sim= counts them.
 *
 * A player's own faction has people. X4 picks the character of a new person (a station's or a build storage's manager,
 * a ship's crew, a hired hand) from the definitions in libraries/characters.xml whose category lists the person's
 * faction; the stock lists name "faction.player" and never a faction of this session. The root cause of the dropped
 * plan above (run tf7, 19 Nov, both clients): at player 2's plot X4 logged "Failed to create 'manager' of
 * 'buildstorage_gen_base_01_macro' as no character definition was found with tags 'manager' or 'officer'", so the
 * build storage had no manager, nothing ran its script build.buildstorage, and the build task nobody processed was
 * dropped. The lookup 0x98c5c0(definitions, tags, ?, factions, flag, ?) (six callers: the control entity's creation
 * 0x44e760 twice, MD's people, the crew generators) takes the faction list as a small vector (inline capacity 2:
 * +0x10 capacity, +0x18 size, the same layout as the "known to" set). Its entry goes through here: a list of one
 * faction that is a player's own (x4lan_player<k>, or the slot's own while the exchange holds its id) is asked as the
 * simulation's player faction, so a player's faction draws from exactly the player faction's people (the DLCs' too).
 * The answer depends on the faction asked about alone, on every client and every thread. cast= counts them. */
#define FPRES_TABLE 0x6cf15f8u
#define FPRES_ANCHOR 0x10u
#define FPRES_BEGIN 0x20u            /* anchor: the leftmost node */
#define FPRES_N_PARENT 0x0u
#define FPRES_N_LEFT 0x8u
#define FPRES_N_RIGHT 0x10u
#define FPRES_N_KEY 0x20u
#define FPRES_N_FACTION 0x30u
#define FPRES_F_HASH 0x8u
#define FPRES_F_ID 0x10u
#define FPRES_F_ID_SIZE 0x20u
#define FPRES_DEPTH 64u
typedef struct {uint32_t rva;uint8_t size;uint8_t bytes[9];} fpres_pin;
/* The inlined lookup of GetNumAllFactionShips (table, anchor, root, key, left, right, the faction in the node) and the
 * id read of 0xafe073 (a short string's size test at +0x28, its text at +0x10). */
static const fpres_pin fpres_pin_table[]={
    {0x198977u,3,{0x48,0x8b,0x15}},{0x19897eu,4,{0x48,0x83,0xc2,0x10}},{0x198985u,4,{0x48,0x8b,0x42,0x08}},
    {0x198990u,4,{0x4c,0x39,0x48,0x20}},{0x198999u,4,{0x48,0x8b,0x40,0x08}},{0x19899fu,4,{0x48,0x8b,0x40,0x10}},
    {0x1989b9u,4,{0x4c,0x8d,0x41,0x30}},{0xafe073u,9,{0x48,0x83,0x7a,0x28,0x10,0x48,0x8d,0x42,0x10}}};
#define FPRES_PINS (sizeof(fpres_pin_table)/sizeof(*fpres_pin_table))
#define FPRES_KNOWN 0x4b73b0u            /* (the set of an object or a space, a faction): known to it */
#define FPRES_KNOWN_SIZE 0x64u
#define FPRES_KNOWN_SHA "3ccec8825ab4042f6a2fe0df55d796be29d56682ae50973987440a0cab16eb55"
#define FPRES_KNOWN_YES 0x4b7411u        /* its "mov al,1; ret", where the first test jumps */
#define FPRES_KNOWN_STUB 0x580u          /* on the slots' near page: free between the animation listeners' (to 0x553) and 0x900 */
#define FPRES_KNOWN_TRAMPOLINE 0x5e0u    /* cmp byte [rcx+0x72],0; jne <yes> (as a near jump); then on at +6 */
static const unsigned char fpres_known_head[6]={0x80,0x79,0x72,0x00,0x75,0x5b};   /* cmp byte [rcx+0x72],0; jne +0x5b */
static unsigned char fpres_known_patched[5];
static int fpres_known_installed;
static volatile LONG64 fpres_known_shared;
typedef unsigned char (*fpres_known_fn)(void *set,void *faction);
static fpres_known_fn fpres_known_test;   /* tests: X4's function */
#define FPRES_INFO 0x643ef0u             /* (the info manager, the kind, a component): unlocked for the player */
#define FPRES_INFO_SIZE 0xb5u
#define FPRES_INFO_SHA "65f0973c25788c0a6e9bbfb629e2e6847128993e318597862b06666528b54844"
#define FPRES_INFO_STUB 0x5a0u           /* on the slots' near page, after the "known to" stub */
#define FPRES_INFO_TRAMPOLINE 0x5c0u     /* the function's first two instructions, then on at +6 */
static const unsigned char fpres_info_head[6]={0x40,0x53,0x48,0x83,0xec,0x20};   /* push rbx; sub rsp,0x20 */
static unsigned char fpres_info_patched[5];
static int fpres_info_installed;
static volatile LONG64 fpres_info_own;
typedef unsigned char (*fpres_info_fn)(void *manager,int kind,unsigned char *component);
static fpres_info_fn fpres_info_test;     /* tests: X4's function */
static void *(*fpres_owner_test)(void *component);
#define FPRES_CAST 0x98c5c0u             /* the character definition for (tags, factions, ...) or NULL */
#define FPRES_CAST_SIZE 0x141u
#define FPRES_CAST_SHA "b54d1e77690836d58b93d9f3f3212037cee82180638521a981b855b3b8f2be2b"
#define FPRES_CAST_STUB 0x600u           /* on the slots' near page, after the "known to" trampoline (to 0x5f8) */
#define FPRES_CAST_TRAMPOLINE 0x620u     /* its first instruction, then on at +5 */
static const unsigned char fpres_cast_head[5]={0x48,0x89,0x5c,0x24,0x08};   /* mov [rsp+8],rbx */
static unsigned char fpres_cast_patched[5];
static int fpres_cast_installed;
static volatile LONG64 fpres_cast_player;
typedef void *(*fpres_cast_fn)(void *definitions,void *tags,void *a3,unsigned char *factions,unsigned char flag,void *a6);
static fpres_cast_fn fpres_cast_test;     /* tests: X4's function */
static int fpres_armed;
static unsigned char *fpres_own,*fpres_anchor_open;
static void *volatile *fpres_cell_open;
static const char *fpres_why="-";
static volatile LONG64 fpres_swaps,fpres_refused,fpres_changed,fpres_broken,fpres_sim_scopes;
static unsigned fpres_slot_last;
/* Test hooks: the tree's anchor, g_PlayerFaction's cell, a slot's faction. */
static unsigned char *(*fpres_anchor_test)(void);
static void *volatile *(*fpres_cell_test)(void);
static void *(*fpres_slot_faction_test)(unsigned slot);

/* The entry of 0x4b73b0: known to the faction asked about (X4's own test, on the trampoline); inside the scope the
 * slot's own faction also knows what the simulation's player faction knows. */
static __attribute__((noinline)) unsigned char fpres_known_to(void *set,void *faction) {
    const fpres_known_fn original=fpres_known_test?fpres_known_test:(fpres_known_fn)(void*)(pslot_page+FPRES_KNOWN_TRAMPOLINE);
    unsigned char known=original(set,faction);
    if(!known && fpres_open && faction==(void*)fpres_own && GetCurrentThreadId()==ui_thread && original(set,fpres_canonical)) {
        InterlockedIncrement64(&fpres_known_shared);known=1;
    }
    return known;
}
/* X4's player faction as the simulation knows it, whatever scope holds another in g_PlayerFaction now. */
static void *fpres_true_player(void) {
    if(fpres_open) return fpres_canonical;
    if(fpres_sim_slot) return fpres_sim_saved;
    if(uitrade_scope_open) return uitrade_scope_saved;
    if(mpp_faction_open) return mpp_faction_saved;
    if(uifac_view_open) return uifac_view_saved;
    void *volatile *cell=fpres_cell_test?fpres_cell_test():uitrade_player_faction();
    return cell?*cell:NULL;
}
/* A player's own faction of this session, by its id (x4lan_player<k>: the session's pool factions). */
static int fpres_pool_faction(const unsigned char *faction) {
    uint64_t size=0,capacity=0;
    memcpy(&size,faction+FPRES_F_ID+0x10,8);memcpy(&capacity,faction+FPRES_F_ID+0x18,8);
    return capacity<16 && size>12 && size<16 && !memcmp(faction+FPRES_F_ID,"x4lan_player",12);
}
/* The entry of 0x643ef0: a player's own faction's property is unlocked; everything else is X4's answer. */
static __attribute__((noinline)) unsigned char fpres_info(void *manager,int kind,unsigned char *component) {
    if(component) {
        unsigned char *owner=fpres_owner_test?fpres_owner_test(component):uitrade_owner(component);
        if(owner && ((fpres_open && owner==fpres_own) || fpres_pool_faction(owner))) {InterlockedIncrement64(&fpres_info_own);return 1;}
    }
    return (fpres_info_test?fpres_info_test:(fpres_info_fn)(void*)(pslot_page+FPRES_INFO_TRAMPOLINE))(manager,kind,component);
}
/* Around the simulation's update of a container: its owner as X4's player faction when that is a slot's own
 * faction. 1 opened (fpres_sim_leave closes it). Not inside a presentation scope, a replayed call's faction scope or
 * a slot's scope (each has set the player faction for its own reason), and on the frame thread only. */
static int fpres_sim_enter(void *container) {
    if(!fpres_armed || fpres_open || fpres_sim_slot || !container || uitrade_scope_open || pstk_scope_slot || GetCurrentThreadId()!=ui_thread) return 0;
    void *volatile *cell=fpres_cell_test?fpres_cell_test():uitrade_player_faction();void *now=cell?*cell:NULL;
    unsigned char *owner=fpres_owner_test?fpres_owner_test(container):uitrade_owner(container);
    if(!now || !owner || owner==now || !fpres_pool_faction(owner)) return 0;
    for(unsigned k=2;k<=PILOT_SLOTS;k++) {
        if((fpres_slot_faction_test?fpres_slot_faction_test(k):uitrade_slot_faction(k))!=(void*)owner) continue;
        rsch_view_enter(k);   /* slot k's library in X4's player: asked while the player faction is still the simulation's */
        fpres_sim_saved=now;*cell=owner;fpres_sim_slot=k;
        InterlockedIncrement64(&fpres_sim_scopes);
        return 1;
    }
    return 0;
}
static void fpres_sim_leave(void) {
    if(!fpres_sim_slot) return;
    void *volatile *cell=fpres_cell_test?fpres_cell_test():uitrade_player_faction();
    const unsigned k=fpres_sim_slot;
    *cell=fpres_sim_saved;fpres_sim_slot=0;fpres_sim_saved=NULL;
    rsch_view_leave(k);
}
/* The entry of the character lookup: one faction that is a player's own is asked as the player faction. */
static __attribute__((noinline)) void *fpres_cast(void *definitions,void *tags,void *a3,unsigned char *factions,unsigned char flag,void *a6) {
    const fpres_cast_fn original=fpres_cast_test?fpres_cast_test:(fpres_cast_fn)(void*)(pslot_page+FPRES_CAST_TRAMPOLINE);
    if(fpres_armed && factions) {
        uint64_t capacity=0,size=0;void *data=NULL,*faction=NULL;
        memcpy(&capacity,factions+0x10,8);memcpy(&size,factions+0x18,8);
        if(size==1) {
            if(capacity==2) data=factions;else memcpy(&data,factions,8);
            if(data) memcpy(&faction,data,8);
            void *player=fpres_true_player();
            if(faction && player && faction!=player && (fpres_pool_faction(faction) || (fpres_open && faction==(void*)fpres_own))) {
                unsigned char asked[0x20]={0};uint64_t two=2,one=1;
                memcpy(asked,&player,8);memcpy(asked+0x10,&two,8);memcpy(asked+0x18,&one,8);
                InterlockedIncrement64(&fpres_cast_player);
                return original(definitions,tags,a3,asked,flag,a6);
            }
        }
    }
    return original(definitions,tags,a3,factions,flag,a6);
}
static void *fpres_get(const unsigned char *at) {void *v;memcpy(&v,at,8);return v;}
static void fpres_put(unsigned char *at,void *v) {memcpy(at,&v,8);}
static uint64_t fpres_key(const unsigned char *at) {uint64_t v;memcpy(&v,at,8);return v;}
static void fpres_trade(unsigned char *a,unsigned char *b,size_t n) {
    unsigned char t[FPRES_F_ID_SIZE];
    memcpy(t,a,n);memcpy(a,b,n);memcpy(b,t,n);
}
/* X4's hash of a faction id (0x19894b: 0x811c9dc5 in a 64-bit word, times 0x1000193, xor the sign-extended byte). */
static uint64_t fpres_hash(const char *id) {
    uint64_t h=0x811c9dc5u;
    for(;*id;id++) {h*=0x1000193u;h^=(uint64_t)(int64_t)(signed char)*id;}
    return h;
}
/* The node the tree finds under a key, as the inlined lookup walks; NULL when there is none. */
static unsigned char *fpres_find(const unsigned char *anchor,uint64_t key) {
    unsigned char *n=fpres_get(anchor+FPRES_N_LEFT);
    for(unsigned depth=0;n && depth<FPRES_DEPTH;depth++) {
        uint64_t k=fpres_key(n+FPRES_N_KEY);
        if(k==key) return n;
        n=fpres_get(n+(k<key?FPRES_N_RIGHT:FPRES_N_LEFT));
    }
    return NULL;
}
/* A faction whose node the tree finds under the hash the faction itself holds. */
static int fpres_listed(const unsigned char *anchor,const unsigned char *faction) {
    const unsigned char *node=faction-FPRES_N_FACTION;
    uint64_t key=fpres_key(node+FPRES_N_KEY);
    return key==fpres_key(faction+FPRES_F_HASH) && fpres_find(anchor,key)==node;
}
/* A faction with this short id. */
static int fpres_named(const unsigned char *faction,const char *id) {
    size_t n=strlen(id);uint64_t size=fpres_key(faction+FPRES_F_ID+0x10),capacity=fpres_key(faction+FPRES_F_ID+0x18);
    return n<16 && size==n && capacity<16 && !memcmp(faction+FPRES_F_ID,id,n) && fpres_key(faction+FPRES_F_HASH)==fpres_hash(id);
}
/* Where a node's parent names the node that stood in its place before (the anchor is the root's parent, its "left" the
 * root); NULL when the parent is the other node of the exchange (its links are right already). */
static unsigned char *fpres_parent_link(unsigned char *n,unsigned char *other) {
    unsigned char *p=fpres_get(n+FPRES_N_PARENT);
    if(!p || p==other) return NULL;
    if(fpres_get(p+FPRES_N_LEFT)==other) return p+FPRES_N_LEFT;
    if(fpres_get(p+FPRES_N_RIGHT)==other) return p+FPRES_N_RIGHT;
    return NULL;
}
/* Two nodes change places in the tree and take each other's key; their factions take each other's hash and id. The
 * tree's order holds (each key sits where it sat). The same call again undoes it. */
static void fpres_exchange(unsigned char *anchor,unsigned char *a,unsigned char *b) {
    static const unsigned links[3]={FPRES_N_PARENT,FPRES_N_LEFT,FPRES_N_RIGHT};
    unsigned char *pair[2]={a,b};
    fpres_trade(a,b,FPRES_N_KEY);   /* parent, left, right, colour */
    /* a link to itself: the other node was its neighbour and now stands where it stood */
    for(unsigned i=0;i<2;i++) for(unsigned l=0;l<3;l++) if(fpres_get(pair[i]+links[l])==pair[i]) fpres_put(pair[i]+links[l],pair[1-i]);
    unsigned char *up[2]={fpres_parent_link(a,b),fpres_parent_link(b,a)};   /* both found before either is written */
    for(unsigned i=0;i<2;i++) {
        unsigned char *n=pair[i],*left=fpres_get(n+FPRES_N_LEFT),*right=fpres_get(n+FPRES_N_RIGHT);
        if(up[i]) fpres_put(up[i],n);
        if(left) fpres_put(left+FPRES_N_PARENT,n);
        if(right) fpres_put(right+FPRES_N_PARENT,n);
    }
    {   unsigned char *begin=fpres_get(anchor+FPRES_BEGIN);
        if(begin==a) fpres_put(anchor+FPRES_BEGIN,b);else if(begin==b) fpres_put(anchor+FPRES_BEGIN,a);
    }
    fpres_trade(a+FPRES_N_KEY,b+FPRES_N_KEY,8);
    fpres_trade(a+FPRES_N_FACTION+FPRES_F_HASH,b+FPRES_N_FACTION+FPRES_F_HASH,8);
    fpres_trade(a+FPRES_N_FACTION+FPRES_F_ID,b+FPRES_N_FACTION+FPRES_F_ID,FPRES_F_ID_SIZE);
}
static unsigned char *fpres_anchor(void) {
    if(fpres_anchor_test) return fpres_anchor_test();
    unsigned char *table=NULL;
    if(!pnat_base || !read_local(pnat_base+FPRES_TABLE,&table,8) || !table) return NULL;
    return table+FPRES_ANCHOR;
}
static void *volatile *fpres_cell(void) {return fpres_cell_test?fpres_cell_test():uitrade_player_faction();}
/* In pres_enter, last: the slot's faction in the player faction's place. 1 when it is (fpres_open). */
static int fpres_enter(unsigned slot) {
    if(!fpres_armed || fpres_open) return 0;
    void *volatile *cell=fpres_cell();
    unsigned char *own=fpres_slot_faction_test?fpres_slot_faction_test(slot):uitrade_slot_faction(slot),*player=cell?*cell:NULL;
    if(!own || !player) {fpres_why="no_faction";InterlockedIncrement64(&fpres_refused);return 0;}
    if(own==player) return 0;   /* coop, or slot 1: its faction is the player faction */
    unsigned char *anchor=fpres_anchor();
    if(!anchor) {fpres_why="no_table";InterlockedIncrement64(&fpres_refused);return 0;}
    if(!fpres_named(player,"player")) {fpres_why="player_name";InterlockedIncrement64(&fpres_refused);return 0;}
    if(!fpres_listed(anchor,player) || !fpres_listed(anchor,own)) {fpres_why="not_listed";InterlockedIncrement64(&fpres_refused);return 0;}
    fpres_exchange(anchor,player-FPRES_N_FACTION,own-FPRES_N_FACTION);
    fpres_canonical=player;fpres_own=own;fpres_anchor_open=anchor;fpres_cell_open=cell;*cell=own;fpres_open=1;fpres_slot_last=slot;
    InterlockedIncrement64(&fpres_swaps);
    return 1;
}
/* In pres_leave, first: every byte back. */
static void fpres_leave(void) {
    if(!fpres_open) return;
    if(*fpres_cell_open!=fpres_own) InterlockedIncrement64(&fpres_changed);
    fpres_exchange(fpres_anchor_open,(unsigned char*)fpres_canonical-FPRES_N_FACTION,fpres_own-FPRES_N_FACTION);
    *fpres_cell_open=fpres_canonical;
    if(!fpres_named(fpres_canonical,"player") || !fpres_listed(fpres_anchor_open,fpres_canonical) || !fpres_listed(fpres_anchor_open,fpres_own)) {
        fpres_why="broken";InterlockedIncrement64(&fpres_broken);
    }
    fpres_open=0;fpres_canonical=NULL;fpres_own=NULL;fpres_anchor_open=NULL;fpres_cell_open=NULL;
}
/* The pinned bytes of this build, and the table's address in the lookup. */
static int fpres_pins(void) {
    int32_t rel=0;
    if(!pnat_base) return 0;
    for(unsigned i=0;i<FPRES_PINS;i++) if(memcmp(pnat_base+fpres_pin_table[i].rva,fpres_pin_table[i].bytes,fpres_pin_table[i].size)) return 0;
    memcpy(&rel,pnat_base+0x19897au,4);
    return 0x19897eu+(uint32_t)rel==FPRES_TABLE;
}
/* The entry of "known to" through the near page's stub; its first two instructions on a trampoline, the short jump
 * of the second as a near one to the same place. */
static int fpres_known_install(void) {
    DWORD old,ignored;int32_t rel=0;
    if(fpres_known_installed) return 1;
    if(!pslot_page && !(pslot_page=pslot_alloc_page())) {fpres_why="page";return 0;}
    if(!hash_ok(pnat_base+FPRES_KNOWN,FPRES_KNOWN_SIZE,FPRES_KNOWN_SHA) || memcmp(pnat_base+FPRES_KNOWN,fpres_known_head,sizeof(fpres_known_head))) {
        fpres_why="known_pin";return 0;
    }
    if(!VirtualProtect(pslot_page,PVIEW_PAGE,PAGE_READWRITE,&old)) {fpres_why="protect";return 0;}
    pview_abs_jump(pslot_page+FPRES_KNOWN_STUB,(void*)fpres_known_to);
    {   unsigned char *t=pslot_page+FPRES_KNOWN_TRAMPOLINE;
        memcpy(t,fpres_known_head,4);t[4]=0x0f;t[5]=0x85;
        rel=(int32_t)((intptr_t)(pnat_base+FPRES_KNOWN_YES)-(intptr_t)(t+10));
        memcpy(t+6,&rel,4);
        pview_abs_jump(t+10,pnat_base+FPRES_KNOWN+sizeof(fpres_known_head));
    }
    if(!VirtualProtect(pslot_page,PVIEW_PAGE,PAGE_EXECUTE_READ,&ignored)) {fpres_why="protect";return 0;}
    FlushInstructionCache(GetCurrentProcess(),pslot_page,PVIEW_PAGE);
    rel=(int32_t)((intptr_t)(pslot_page+FPRES_KNOWN_STUB)-(intptr_t)(pnat_base+FPRES_KNOWN+5));
    fpres_known_patched[0]=0xe9;memcpy(fpres_known_patched+1,&rel,4);
    if(!pview_cas(FPRES_KNOWN,fpres_known_head,fpres_known_patched,5)) {fpres_why="known_entry";return 0;}
    fpres_known_installed=1;
    return 1;
}
/* The entry of the character lookup through the near page's stub, its first instruction on a trampoline. */
static int fpres_cast_install(void) {
    DWORD old,ignored;int32_t rel=0;
    if(fpres_cast_installed) return 1;
    if(!pslot_page && !(pslot_page=pslot_alloc_page())) {fpres_why="page";return 0;}
    if(!hash_ok(pnat_base+FPRES_CAST,FPRES_CAST_SIZE,FPRES_CAST_SHA) || memcmp(pnat_base+FPRES_CAST,fpres_cast_head,sizeof(fpres_cast_head))) {
        fpres_why="cast_pin";return 0;
    }
    if(!VirtualProtect(pslot_page,PVIEW_PAGE,PAGE_READWRITE,&old)) {fpres_why="protect";return 0;}
    pview_abs_jump(pslot_page+FPRES_CAST_STUB,(void*)fpres_cast);
    memcpy(pslot_page+FPRES_CAST_TRAMPOLINE,fpres_cast_head,sizeof(fpres_cast_head));
    pview_abs_jump(pslot_page+FPRES_CAST_TRAMPOLINE+sizeof(fpres_cast_head),pnat_base+FPRES_CAST+sizeof(fpres_cast_head));
    if(!VirtualProtect(pslot_page,PVIEW_PAGE,PAGE_EXECUTE_READ,&ignored)) {fpres_why="protect";return 0;}
    FlushInstructionCache(GetCurrentProcess(),pslot_page,PVIEW_PAGE);
    rel=(int32_t)((intptr_t)(pslot_page+FPRES_CAST_STUB)-(intptr_t)(pnat_base+FPRES_CAST+5));
    fpres_cast_patched[0]=0xe9;memcpy(fpres_cast_patched+1,&rel,4);
    if(!pview_cas(FPRES_CAST,fpres_cast_head,fpres_cast_patched,5)) {fpres_why="cast_entry";return 0;}
    fpres_cast_installed=1;
    return 1;
}
/* The entry of the info test through the near page's stub, its first two instructions on a trampoline. */
static int fpres_info_install(void) {
    DWORD old,ignored;int32_t rel=0;
    if(fpres_info_installed) return 1;
    if(!pslot_page && !(pslot_page=pslot_alloc_page())) {fpres_why="page";return 0;}
    if(!hash_ok(pnat_base+FPRES_INFO,FPRES_INFO_SIZE,FPRES_INFO_SHA) || memcmp(pnat_base+FPRES_INFO,fpres_info_head,sizeof(fpres_info_head))) {
        fpres_why="info_pin";return 0;
    }
    if(!VirtualProtect(pslot_page,PVIEW_PAGE,PAGE_READWRITE,&old)) {fpres_why="protect";return 0;}
    pview_abs_jump(pslot_page+FPRES_INFO_STUB,(void*)fpres_info);
    memcpy(pslot_page+FPRES_INFO_TRAMPOLINE,fpres_info_head,sizeof(fpres_info_head));
    pview_abs_jump(pslot_page+FPRES_INFO_TRAMPOLINE+sizeof(fpres_info_head),pnat_base+FPRES_INFO+sizeof(fpres_info_head));
    if(!VirtualProtect(pslot_page,PVIEW_PAGE,PAGE_EXECUTE_READ,&ignored)) {fpres_why="protect";return 0;}
    FlushInstructionCache(GetCurrentProcess(),pslot_page,PVIEW_PAGE);
    rel=(int32_t)((intptr_t)(pslot_page+FPRES_INFO_STUB)-(intptr_t)(pnat_base+FPRES_INFO+5));
    fpres_info_patched[0]=0xe9;memcpy(fpres_info_patched+1,&rel,4);
    if(!pview_cas(FPRES_INFO,fpres_info_head,fpres_info_patched,5)) {fpres_why="info_entry";return 0;}
    fpres_info_installed=1;
    return 1;
}
static void fpres_restore(void) {
    if(fpres_cast_installed && pview_cas(FPRES_CAST,fpres_cast_patched,fpres_cast_head,5)) fpres_cast_installed=0;
    if(fpres_info_installed && pview_cas(FPRES_INFO,fpres_info_patched,fpres_info_head,5)) fpres_info_installed=0;
    if(fpres_known_installed && pview_cas(FPRES_KNOWN,fpres_known_patched,fpres_known_head,5)) fpres_known_installed=0;
}
static int fpres_arm(void) {
    if(fpres_armed) return 1;
    if(!fpres_pins()) {fpres_why="pins";return 0;}
    fpres_swaps=fpres_refused=fpres_changed=fpres_broken=fpres_known_shared=fpres_info_own=fpres_sim_scopes=fpres_cast_player=0;fpres_why="-";fpres_slot_last=0;
    if(!fpres_known_install() || !fpres_info_install() || !fpres_cast_install()) {fpres_restore();return 0;}
    fpres_armed=1;
    return 1;
}
static int fpres_status(char *out,size_t capacity) {
    int n=snprintf(out,capacity,"armed=%d open=%d slot=%u swaps=%lld refused=%lld changed=%lld broken=%lld known=%lld info=%lld sim=%lld cast=%lld why=%s",fpres_armed,fpres_open,fpres_slot_last,
        (long long)fpres_swaps,(long long)fpres_refused,(long long)fpres_changed,(long long)fpres_broken,(long long)fpres_known_shared,(long long)fpres_info_own,
        (long long)fpres_sim_scopes,(long long)fpres_cast_player,fpres_why);
    return n>0 && (size_t)n<capacity?n:0;
}
