/* A library per faction: research and blueprints, ON BY DEFAULT in sessions (native_gate_research_per_faction; needs
 * the stock slots, whose players it uses, and the trade family's faction scope). User decision, 16 Nov 2026: in coop the
 * group shares one headquarters and what it researched; in factions mode each player's faction has its own headquarters
 * and its research is that faction's alone. Blueprints go with it: X4's research add itself gives the blueprints a
 * research unlocks (0x7220f0 calls the blueprint add), so the two lists have one owner.
 *
 *   where   [F 9.00] X4 keeps what "the player" knows in two vectors of U::Player, side by side: blueprints at +0x7320
 *           ({begin, end, capacity}, 0x40-byte entries, the ware first; Player's find 0x721c00 walks it, 0x720e30 adds)
 *           and research at +0x7338 (0x38-byte entries, the ware first; Player::HasResearched 0x7224a0 walks it,
 *           0x154840 adds: the research module's finish, MD's add_research, the AddResearch export). Player's import
 *           and export read and write them as <blueprints> and <research> of that player's component. Every stock slot
 *           k has its own U::Player P_k (lockstep_pilot_slots.h), saved with the session, so P_k's vectors are where
 *           slot k's faction's library is.
 *   lists   X4 code reaches the vectors through whichever player it has: the universe's singleton (P_k inside slot k's
 *           scope, X4's own player P_1 everywhere else: client k's menus and a replayed click run on P_1, which takes
 *           slot k's identity for the call). The right library is put there for the length of that scope by exchanging
 *           the 48 bytes of the two pairs of vectors (P_1 <-> P_k), and exchanged back when it closes; nothing is
 *           copied, a list that grows inside the scope goes back to its owner as it is:
 *             slot k has its own faction (factions mode): in client k's presentation scope (its menus read) and in a
 *               replayed call's faction scope (lockstep_ui_trade.h: its StartResearch, its LearnBlueprint) P_1 shows
 *               P_k's library; slot k's own scope (its missions, its plot's add_research) has P_k with its own already.
 *             slot k is of X4's player faction (coop): in slot k's own scope P_k shows P_1's library, so its missions
 *               read and add to the one shared library; menus and replayed calls have P_1 with it already.
 *   module  a production module's update (0x761720) asks four times for the module's owner (vtable +0x1620) and goes on
 *           only when that is g_PlayerFaction, each time with the universe's player: it posts U::ProductionStartedEvent
 *           on it (0x7624bf), asks it whether the item is known (0x762592), adds a finished research to it (0x762aac:
 *           another owner's research is dropped) and posts U::ProductionFinishedEvent on it (0x762e9f); an item taken
 *           out of its queue (0x7639d0) asks the same way and posts U::ProductionCancelledEvent (0x763d3f). MD's
 *           event_player_production_started / _finished / _cancelled and, from the add, event_player_research_unlocked,
 *           which the headquarters plot waits for. For a module of a slot's own faction the owner call answers g_PlayerFaction
 *           and the call that follows is given that slot's player P_k in place of the universe's: the events are P_k's
 *           (X4's delivery brings a slot's player's events to its own missions inside its scope,
 *           lockstep_pilot_death.h), the question reads its library, and the add runs on P_k inside slot k's scope
 *           where one can be opened, as an add_research of its own mission would. X4's own add, events and notice;
 *           only the player differs. (16 Nov's first form exchanged the lists and added on P_1: the right list, but
 *           the unlock event was player 1's, heard by player 1's plot and not by the owner's.)
 *   receipt RSCH_2:<slot>=<research count>.<hash>/<blueprint count>.<hash>;... (the wares in order) once any slot from 2
 *           on has anything (none before: a session without a library per faction has no part); joins the pilot receipt
 *           through the inventory family.
 * Fail closed: the code regions are pinned by SHA-256 and the ten call sites by their bytes; a refused arming leaves X4
 * as it is and the status says why (the probe stops the session: research would silently go to nobody). */
#define RSCH_LIBRARY 0x7320u               /* the blueprints' vector, then the research's */
#define RSCH_LIBRARY_SIZE 48u
#define RSCH_BLUEPRINTS 0x7320u
#define RSCH_BLUEPRINT_ENTRY 0x40u
#define RSCH_LIST 0x7338u
#define RSCH_ENTRY 0x38u
#define RSCH_ENTRIES_MAX 16384u
#define RSCH_REGION 0x76241du              /* the four owner questions and what follows each */
#define RSCH_REGION_SIZE 2700u
#define RSCH_REGION_SHA "feb5eac2726bf2bd3ad79eddd0803f709cf258e166c3810e2464efc96b9c6860"
#define RSCH_CANCEL_REGION 0x763ca9u       /* the owner question of a cancelled item and its notice */
#define RSCH_CANCEL_REGION_SIZE 160u
#define RSCH_CANCEL_REGION_SHA "07e4beebbda8f6ef0b874317d52edc523d368c86fb2e4854357782cb1dfb045b"
#define RSCH_ADD 0x154840u                 /* (player, ware, time x3) */
#define RSCH_ADD_SIZE 105u
#define RSCH_ADD_SHA "95e840fa226e9185718ab74929238ce4730f16f86cd3aa0a200e45448e32cef3"
#define RSCH_HAS 0x7224a0u                 /* pins the research list's place and its entry size */
#define RSCH_HAS_SIZE 77u
#define RSCH_HAS_SHA "d53a98ef1a96b0fb1e028a4770b56cad382cdf6889b4830c60fd60f5fab2ea5c"
#define RSCH_FIND_BLUEPRINT 0x721c00u      /* pins the blueprint list's place and its entry size */
#define RSCH_FIND_BLUEPRINT_SIZE 104u
#define RSCH_FIND_BLUEPRINT_SHA "420e6e51bc90fa8f65a227bcae486604fe824ecf7dfb5517f2952c761ccc9493"
#define RSCH_KNOWS 0x7224f0u               /* (player, key): the question of 0x762592 */
#define RSCH_KNOWS_SIZE 80u
#define RSCH_KNOWS_SHA "2416b1251318405fe1ad4d8fada62c2ddf03f8fa92b3d1bd5a2364fc92fb1caa"
#define RSCH_POST 0x99a3f0u                /* EventSource::PostEvent(source, event, time, immediate) */
#define RSCH_POST_SIZE 807u
#define RSCH_POST_SHA "5ca12a5d17041238645af6d6028c718bb3714cc53e9dd6963769d51827087c0d"
#define RSCH_OWNER_VSLOT (0x1620u/8u)
#define RSCH_STUB 32u
enum {RSCH_UNKNOWN=0,RSCH_OWN,RSCH_SHARED};
enum {RSCH_USE_POST=0,RSCH_USE_KNOWS,RSCH_USE_ADD,RSCH_USES};
#define RSCH_SITES 5u
/* owner: call qword ptr [rax+0x1620]; use: the call that takes the universe's player after it */
static const struct {unsigned owner,use,target,kind;} rsch_sites[RSCH_SITES]={
    {0x762423u,0x7624bfu,RSCH_POST,RSCH_USE_POST},     /* production started */
    {0x762525u,0x762592u,RSCH_KNOWS,RSCH_USE_KNOWS},   /* is the item known */
    {0x762a4bu,0x762aacu,RSCH_ADD,RSCH_USE_ADD},       /* a finished research */
    {0x762e05u,0x762e9fu,RSCH_POST,RSCH_USE_POST},     /* production finished */
    {0x763cafu,0x763d3fu,RSCH_POST,RSCH_USE_POST}};    /* production cancelled */

typedef void (*rsch_add_fn)(void *,void *,double,double,double);
typedef uint64_t (*rsch_post_fn)(void *,void *,double,unsigned char);
typedef uint64_t (*rsch_knows_fn)(void *,void *);
static int rsch_armed,rsch_installed;
static const char *rsch_why="-";
static unsigned char *rsch_page;
static unsigned char rsch_owner_original[RSCH_SITES][6],rsch_owner_patched[RSCH_SITES][6],rsch_use_original[RSCH_SITES][5],
                     rsch_use_patched[RSCH_SITES][5];
static unsigned rsch_open;                 /* the slot whose library and P_1's are exchanged now, 0 none */
static unsigned rsch_pending;              /* between an owner question and the call after it: the slot whose module it is */
static unsigned char rsch_kind[PILOT_SLOTS+1];   /* the last known: a slot without a ship has no faction to ask */
static volatile LONG64 rsch_exchanges,rsch_nested,rsch_finishes,rsch_credited,rsch_told,rsch_asked,rsch_unscoped,rsch_lost,
                       rsch_offthread,rsch_private;
/* ---- A faction's library notices stay that faction's ----
 * The scripts nobody owns hear every player (lockstep_pilot_death.h pvehk_forward): what slot k's player is told is
 * also handed to player 1's listeners of the world's scripts. For the notices about a library that is slot k's
 * faction's alone that is wrong: the stock research stories, the diplomacy script, the notifications and the tutorials
 * take "the player researched X" or "the player got blueprint Y" as the one player faction's and act on player 1's
 * headquarters and list (the embassy room, a story's next step, a notice on player 1's screen). So an event of these
 * five types on the player of a slot with an own faction goes to that slot's own cues only. In coop nothing changes:
 * the library is one and its notices are posted on X4's own player. The types are read from X4's own getters (slot 1
 * of each event class's vtable, `mov eax, type; ret`), checked at arm; a getter that reads otherwise leaves the
 * forwarding as it was. Status private=<n>. */
#define RSCH_NOTICES 5u
static const struct {unsigned getter,type;} rsch_notices[RSCH_NOTICES]={
    {0x9745c0u,0x164u},     /* U::PlayerResearchAddedEvent (research unlocked) */
    {0x974020u,0x175u},     /* production started */
    {0x974030u,0x172u},     /* production finished */
    {0x974060u,0x171u},     /* production cancelled */
    {0x9745d0u,0x151u}};    /* U::PlayerBlueprintAddedEvent */
static int rsch_notices_ok;
static int rsch_notices_check(const unsigned char *image) {
    for(unsigned i=0;i<RSCH_NOTICES;i++) {
        unsigned char code[6];const unsigned type=rsch_notices[i].type;
        const unsigned char want[6]={0xb8,(unsigned char)type,(unsigned char)(type>>8),(unsigned char)(type>>16),(unsigned char)(type>>24),0xc3};
        if(!image || !read_local(image+rsch_notices[i].getter,code,6) || memcmp(code,want,6)) return 0;
    }
    return 1;
}
static unsigned rsch_last_slot;static char rsch_last_ware[48]="-";
/* tests replace these: the players, a slot's faction, X4's own player faction, slot k's scope */
static unsigned char *rsch_player_native(unsigned k) {return k==1?pslot_p1:k>=2 && k<=PILOT_SLOTS?pslots[k].player:NULL;}
static void *rsch_faction_native(unsigned k) {return uitrade_slot_faction(k);}
static void *rsch_canonical_native(void) {
    if(uitrade_scope_open) return uitrade_scope_saved;      /* a replayed call's faction scope holds a slot's now */
    if(mpp_faction_open) return mpp_faction_saved;          /* a mission scope too */
    return pnat_base?*uitrade_player_faction():NULL;
}
static int rsch_scope_enter_native(unsigned k) {return !pstk_scope_slot && pslot_enter(k);}
static void rsch_scope_leave_native(unsigned k) {if(pstk_scope_slot==k) (void)pslot_leave(k);}
static unsigned char *(*rsch_player)(unsigned)=rsch_player_native;
static void *(*rsch_faction)(unsigned)=rsch_faction_native;
static void *(*rsch_canonical)(void)=rsch_canonical_native;
static int (*rsch_scope_enter)(unsigned)=rsch_scope_enter_native;
static void (*rsch_scope_leave)(unsigned)=rsch_scope_leave_native;

static unsigned rsch_kind_of(unsigned k) {
    if(k<2 || k>PILOT_SLOTS) return RSCH_UNKNOWN;
    void *faction=rsch_faction(k),*canonical=rsch_canonical();
    if(faction && canonical) rsch_kind[k]=faction!=canonical?RSCH_OWN:RSCH_SHARED;
    return rsch_kind[k];
}
/* pvehk_forward asks: an event of this type on slot k's player, inside slot k's scope. 1 = slot k's own cues only. */
static int rsch_event_private(unsigned k,unsigned type) {
    if(!rsch_armed || !rsch_notices_ok || rsch_kind_of(k)!=RSCH_OWN) return 0;
    for(unsigned i=0;i<RSCH_NOTICES;i++) if(rsch_notices[i].type==type) {InterlockedIncrement64(&rsch_private);return 1;}
    return 0;
}
static void rsch_exchange(unsigned char *a,unsigned char *b) {
    unsigned char held[RSCH_LIBRARY_SIZE];
    memcpy(held,a+RSCH_LIBRARY,RSCH_LIBRARY_SIZE);memcpy(a+RSCH_LIBRARY,b+RSCH_LIBRARY,RSCH_LIBRARY_SIZE);
    memcpy(b+RSCH_LIBRARY,held,RSCH_LIBRARY_SIZE);
    InterlockedIncrement64(&rsch_exchanges);
}
/* P_1 and P_k exchange libraries until rsch_close(k). 1: this call did it. */
static int rsch_open_for(unsigned k) {
    unsigned char *p1=rsch_player(1),*pk=rsch_player(k);
    if(!rsch_armed || k<2 || !p1 || !pk || p1==pk) return 0;
    if(rsch_open) {InterlockedIncrement64(&rsch_nested);return 0;}
    rsch_exchange(p1,pk);rsch_open=k;return 1;
}
static void rsch_close(unsigned k) {
    unsigned char *p1=rsch_player(1),*pk=rsch_player(k);
    if(!rsch_open || rsch_open!=k) return;
    if(p1 && pk) rsch_exchange(p1,pk);
    rsch_open=0;
}
/* Slot k's own scope (lockstep_pilot_slots.h pslot_enter / pslot_leave): the singleton is P_k. */
static void rsch_slot_enter(unsigned k) {if(rsch_armed && rsch_kind_of(k)==RSCH_SHARED) (void)rsch_open_for(k);}
static void rsch_slot_leave(unsigned k) {if(rsch_armed) rsch_close(k);}
/* A scope in which X4's own player stands for slot k: client k's presentation scope (lockstep_pilot_present.h) and a
 * replayed call's faction scope (lockstep_ui_trade.h). */
static void rsch_view_enter(unsigned k) {if(rsch_armed && rsch_kind_of(k)==RSCH_OWN) (void)rsch_open_for(k);}
static void rsch_view_leave(unsigned k) {if(rsch_armed) rsch_close(k);}

/* ---- a production module's update ---- */
/* The owner question. owner: the module's; now: g_PlayerFaction as it stands (inside a slot's faction scope that is the
 * slot's faction, and X4 goes on by itself). A module of a slot's own faction is that slot's whatever stands there. */
static void *rsch_finish_owner(void *owner,void *now) {
    rsch_pending=0;InterlockedIncrement64(&rsch_finishes);
    if(!rsch_armed || !owner) return owner;
    if(GetCurrentThreadId()!=ui_thread) {if(owner!=now) InterlockedIncrement64(&rsch_offthread);return owner;}
    for(unsigned k=2;k<=PILOT_SLOTS;k++)
        if(rsch_player(k) && rsch_faction(k)==owner && rsch_kind_of(k)==RSCH_OWN) {rsch_pending=k;return now;}
    return owner;   /* X4's own player faction (coop: every slot's), or another faction's module: as X4 has it */
}
static void *rsch_owner_hook(void *module) {
    void *owner=((void*(*)(void*))(*(void***)module)[RSCH_OWNER_VSLOT])(module);
    return rsch_finish_owner(owner,*uitrade_player_faction());
}
static unsigned rsch_take(void) {
    const unsigned k=rsch_pending;rsch_pending=0;
    return k && rsch_player(k)?k:0;
}
/* A production event: the slot's player is its source. */
static uint64_t rsch_use_post(rsch_post_fn post,void *source,void *event,double time,unsigned char immediate) {
    const unsigned k=rsch_take();
    if(k) {source=rsch_player(k);InterlockedIncrement64(&rsch_told);}
    return post(source,event,time,immediate);
}
/* The question about an item: asked of the player that holds the slot's library now. */
static uint64_t rsch_use_knows(rsch_knows_fn knows,void *player,void *key) {
    const unsigned k=rsch_take();
    if(k) {player=rsch_open==k?(void*)rsch_player(1):(void*)rsch_player(k);InterlockedIncrement64(&rsch_asked);}
    return knows(player,key);
}
/* A finished research: added on the slot's player with its own library in it, inside its scope where one opens. */
static void rsch_finish_add(rsch_add_fn add,void *player,void *ware,double a,double b,double c) {
    const unsigned k=rsch_take();
    if(!k) {add(player,ware,a,b,c);return;}
    const int reopen=rsch_open==k;
    if(reopen) rsch_close(k);                           /* its library goes back to P_k for the add */
    const int scoped=!reopen && !rsch_open && rsch_scope_enter(k);   /* never inside a call's or a menu's scope */
    if(!scoped) InterlockedIncrement64(&rsch_unscoped);
    add(rsch_player(k),ware,a,b,c);
    if(scoped) rsch_scope_leave(k);
    if(reopen && !rsch_open_for(k)) InterlockedIncrement64(&rsch_lost);
    rsch_last_slot=k;sw_ware_id_cap(ware,rsch_last_ware,sizeof(rsch_last_ware));InterlockedIncrement64(&rsch_credited);
}
static uint64_t rsch_post_hook(void *source,void *event,double time,unsigned char immediate) {
    return rsch_use_post((rsch_post_fn)(void*)(image+RSCH_POST),source,event,time,immediate);
}
static uint64_t rsch_knows_hook(void *player,void *key) {
    return rsch_use_knows((rsch_knows_fn)(void*)(image+RSCH_KNOWS),player,key);
}
static void rsch_add_hook(void *player,void *ware,double a,double b,double c) {
    rsch_finish_add((rsch_add_fn)(void*)(image+RSCH_ADD),player,ware,a,b,c);
}

/* ---- reading a list (the probe's frame: no scope is open) ---- */
static int rsch_read_at(unsigned k,unsigned list,unsigned entry,unsigned *count,uint64_t *hash,char *names,size_t capacity) {
    unsigned char *player=rsch_player(k),*vector[3];size_t used=0;
    if(names && capacity) names[0]=0;
    *count=0;*hash=1469598103934665603ULL;
    if(!player || rsch_open || !read_local(player+list,vector,sizeof(vector))) return 0;
    if(!vector[0] && !vector[1]) return 1;
    if(vector[1]<vector[0] || (size_t)(vector[1]-vector[0])%entry || (size_t)(vector[1]-vector[0])/entry>RSCH_ENTRIES_MAX) return 0;
    for(unsigned char *e=vector[0];e<vector[1];e+=entry) {
        void *ware=NULL;char id[48];
        if(!read_local(e,&ware,8)) return 0;
        sw_ware_id_cap(ware,id,sizeof(id));
        for(const char *c=id;*c;c++) *hash=(*hash^(unsigned char)*c)*1099511628211ULL;
        *hash=(*hash^0x2cu)*1099511628211ULL;
        if(names && used+strlen(id)+2<capacity) used+=(size_t)snprintf(names+used,capacity-used,"%s%s",used?",":"",id);
        ++*count;
    }
    return 1;
}
static int rsch_read(unsigned k,unsigned *count,uint64_t *hash,char *names,size_t capacity) {
    return rsch_read_at(k,RSCH_LIST,RSCH_ENTRY,count,hash,names,capacity);
}
static int rsch_read_blueprints(unsigned k,unsigned *count,uint64_t *hash,char *names,size_t capacity) {
    return rsch_read_at(k,RSCH_BLUEPRINTS,RSCH_BLUEPRINT_ENTRY,count,hash,names,capacity);
}
static size_t rsch_receipt(char *out,size_t capacity) {
    size_t used=0;unsigned any=0;
    if(!rsch_armed || !capacity) return 0;
    out[0]=0;
    for(unsigned k=2;k<=PILOT_SLOTS && !any;k++) {
        unsigned n=0,b=0;uint64_t h;
        if(rsch_player(k) && ((rsch_read(k,&n,&h,NULL,0) && n) || (rsch_read_blueprints(k,&b,&h,NULL,0) && b))) any=1;
    }
    if(!any) return 0;
    for(unsigned k=1;k<=PILOT_SLOTS;k++) {
        unsigned n,b;uint64_t h,bh;
        if(!rsch_player(k)) continue;
        int w=rsch_read(k,&n,&h,NULL,0) && rsch_read_blueprints(k,&b,&bh,NULL,0)
            ?snprintf(out+used,capacity-used,"%s%u=%u.%016llx/%u.%016llx",used?";":"RSCH_2:",k,n,(unsigned long long)h,b,(unsigned long long)bh)
            :snprintf(out+used,capacity-used,"%s%u=unreadable",used?";":"RSCH_2:",k);
        if(w<=0 || (size_t)w>=capacity-used) return 0;
        used+=(size_t)w;
    }
    return used;
}
static size_t rsch_status(char *out,size_t capacity) {
    char kinds[PILOT_SLOTS+2];unsigned n=0;
    for(unsigned k=2;k<=PILOT_SLOTS;k++) if(rsch_player(k)) kinds[n++]=rsch_kind[k]==RSCH_OWN?'o':rsch_kind[k]==RSCH_SHARED?'s':'?';
    kinds[n]=0;
    int w=snprintf(out,capacity,"research=%s why=%s sites=%d kinds=%s exchanges=%lld nested=%lld finishes=%lld credited=%lld told=%lld "
        "asked=%lld unscoped=%lld lost=%lld offthread=%lld private=%s%lld open=%u last=%u:%s",
        rsch_armed?"armed":"off",rsch_why,rsch_installed?(int)(2u*RSCH_SITES):0,n?kinds:"-",(long long)rsch_exchanges,(long long)rsch_nested,
        (long long)rsch_finishes,(long long)rsch_credited,(long long)rsch_told,(long long)rsch_asked,(long long)rsch_unscoped,
        (long long)rsch_lost,(long long)rsch_offthread,rsch_notices_ok?"":"off/",(long long)rsch_private,rsch_open,rsch_last_slot,
        rsch_last_ware);
    return w>0 && (size_t)w<capacity?(size_t)w:0;
}

/* ---- the ten sites ---- */
static int rsch_pins(void) {
    static const unsigned char owner_call[6]={0xff,0x90,0x20,0x16,0x00,0x00};
    if(!hash_ok(image+RSCH_REGION,RSCH_REGION_SIZE,RSCH_REGION_SHA) ||
       !hash_ok(image+RSCH_CANCEL_REGION,RSCH_CANCEL_REGION_SIZE,RSCH_CANCEL_REGION_SHA) || !hash_ok(image+RSCH_ADD,RSCH_ADD_SIZE,RSCH_ADD_SHA) ||
       !hash_ok(image+RSCH_HAS,RSCH_HAS_SIZE,RSCH_HAS_SHA) ||
       !hash_ok(image+RSCH_FIND_BLUEPRINT,RSCH_FIND_BLUEPRINT_SIZE,RSCH_FIND_BLUEPRINT_SHA) ||
       !hash_ok(image+RSCH_KNOWS,RSCH_KNOWS_SIZE,RSCH_KNOWS_SHA) || !hash_ok(image+RSCH_POST,RSCH_POST_SIZE,RSCH_POST_SHA)) return 0;
    for(unsigned i=0;i<RSCH_SITES;i++) {
        unsigned char bytes[5];int32_t rel;
        const int first=rsch_sites[i].owner>=RSCH_REGION && rsch_sites[i].use+5u<=RSCH_REGION+RSCH_REGION_SIZE;
        const int second=rsch_sites[i].owner>=RSCH_CANCEL_REGION && rsch_sites[i].use+5u<=RSCH_CANCEL_REGION+RSCH_CANCEL_REGION_SIZE;
        if((!first && !second) ||
           memcmp(image+rsch_sites[i].owner,owner_call,6) || !read_local(image+rsch_sites[i].use,bytes,5) || bytes[0]!=0xe8) return 0;
        memcpy(&rel,bytes+1,4);
        if(rsch_sites[i].use+5u+(uint32_t)rel!=rsch_sites[i].target) return 0;
    }
    return 1;
}
static int rsch_write(unsigned site,const unsigned char *bytes,unsigned size) {
    unsigned char *at=image+site;DWORD old,ignored;
    if(!VirtualProtect(at,size,PAGE_EXECUTE_READWRITE,&old)) return 0;
    memcpy(at,bytes,size);VirtualProtect(at,size,old,&ignored);FlushInstructionCache(GetCurrentProcess(),at,size);return 1;
}
/* The sites that carry our calls go back, the owner questions first (a use site without its question only passes on). */
static int rsch_uninstall(void) {
    int ok=1;
    if(!rsch_installed) return 1;
    for(unsigned i=0;i<RSCH_SITES;i++)
        if(!memcmp(image+rsch_sites[i].owner,rsch_owner_patched[i],6) && !rsch_write(rsch_sites[i].owner,rsch_owner_original[i],6)) ok=0;
    for(unsigned i=0;i<RSCH_SITES;i++) if(!memcmp(image+rsch_sites[i].owner,rsch_owner_patched[i],6)) ok=0;
    if(!ok) return 0;
    rsch_pending=0;
    for(unsigned i=0;i<RSCH_SITES;i++)
        if(!memcmp(image+rsch_sites[i].use,rsch_use_patched[i],5) && !rsch_write(rsch_sites[i].use,rsch_use_original[i],5)) ok=0;
    if(ok) rsch_installed=0;
    return ok;
}
/* On the frame thread (the simulation runs on it: no call is inside the region now). */
static int rsch_install(void) {
    DWORD old,ignored;
    static void *const hooks[RSCH_USES]={(void*)rsch_post_hook,(void*)rsch_knows_hook,(void*)rsch_add_hook};
    if(rsch_installed) return 1;
    if(GetCurrentThreadId()!=ui_thread) {rsch_why="thread";return 0;}
    if(!rsch_pins()) {rsch_why="pins";return 0;}
    if(!rsch_page) {
        const uintptr_t at=(uintptr_t)(image+RSCH_REGION)&~(uintptr_t)65535;
        for(uintptr_t distance=65536;distance<0x70000000 && !rsch_page;distance+=65536)
            for(int sign=-1;sign<=1 && !rsch_page;sign+=2) {
                unsigned char *p=VirtualAlloc((void*)(sign<0?at-distance:at+distance),0x1000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
                if(!p) continue;
                const intptr_t d=(intptr_t)p-(intptr_t)(image+RSCH_REGION);
                if(d>INT32_MIN+0x1000000 && d<INT32_MAX-0x1000000) rsch_page=p;else VirtualFree(p,0,MEM_RELEASE);
            }
        if(!rsch_page) {rsch_why="page";return 0;}
    }
    if(!VirtualProtect(rsch_page,0x1000,PAGE_READWRITE,&old)) {rsch_why="page_protect";return 0;}
    memset(rsch_page,0xcc,(1u+RSCH_USES)*RSCH_STUB);
    pview_abs_jump(rsch_page,(void*)rsch_owner_hook);
    for(unsigned u=0;u<RSCH_USES;u++) pview_abs_jump(rsch_page+(1u+u)*RSCH_STUB,hooks[u]);
    if(!VirtualProtect(rsch_page,0x1000,PAGE_EXECUTE_READ,&ignored)) {rsch_why="page_protect";return 0;}
    FlushInstructionCache(GetCurrentProcess(),rsch_page,0x1000);
    for(unsigned i=0;i<RSCH_SITES;i++) {
        const int32_t owner=(int32_t)((intptr_t)rsch_page-(intptr_t)(image+rsch_sites[i].owner+5));
        const int32_t use=(int32_t)((intptr_t)(rsch_page+(1u+rsch_sites[i].kind)*RSCH_STUB)-(intptr_t)(image+rsch_sites[i].use+5));
        memcpy(rsch_owner_original[i],image+rsch_sites[i].owner,6);memcpy(rsch_use_original[i],image+rsch_sites[i].use,5);
        rsch_owner_patched[i][0]=0xe8;memcpy(rsch_owner_patched[i]+1,&owner,4);rsch_owner_patched[i][5]=0x90;
        rsch_use_patched[i][0]=0xe8;memcpy(rsch_use_patched[i]+1,&use,4);
    }
    /* the use sites first: with only an owner question in, a slot's module would reach X4's call with X4's own player */
    rsch_installed=1;
    for(unsigned i=0;i<RSCH_SITES;i++) if(!rsch_write(rsch_sites[i].use,rsch_use_patched[i],5)) {rsch_why="site_write";(void)rsch_uninstall();return 0;}
    for(unsigned i=0;i<RSCH_SITES;i++) if(!rsch_write(rsch_sites[i].owner,rsch_owner_patched[i],6)) {rsch_why="site_write";(void)rsch_uninstall();return 0;}
    return 1;
}
/* gate.research_per_faction(1): before the first tick, after the stock slots armed. 1 = armed. */
static int rsch_arm(void) {
    if(rsch_armed) return 1;
    if(!pnat_base) {rsch_why="no_image";return 0;}
    if(!rsch_install()) return 0;
    rsch_notices_ok=rsch_notices_check(pnat_base);
    rsch_armed=1;rsch_why="-";return 1;
}
/* The session's end (uirep_restore): the calls go back; a library still exchanged goes back first. */
static void rsch_restore(void) {
    if(rsch_open) rsch_close(rsch_open);
    rsch_pending=0;
    if(rsch_armed && rsch_uninstall()) rsch_armed=0;
}
