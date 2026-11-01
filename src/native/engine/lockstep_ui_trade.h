/* UI replay, family 'trade' (bit 1024 of native_gate_ui_replay; DEFAULT OFF, stop reason 164 as the replay). The trade
 * menus' world changes become events of the client's slot and run on every client at the same tick, for slot 1 and
 * slots 2..N, charging the slot's own account. Report: reports/LOCKSTEP-UI-TRADE-20261101.txt.
 *
 *   money     [F] X4 keeps the player's money on the player FACTION, not on the player entity: GetPlayerMoney, AddPlayerMoney
 *             and TransferPlayerMoneyTo/TransferMoneyToPlayer read g_PlayerFaction (0x38f4bd0), its account behind +0x280,
 *             +0x2e0 (a handle 0x3178a0 resolves; the null account 0x38f4bd8 when there is none), balance in cents at the
 *             account's vfunc +0x10. A container has its own account handle at +0x700 (+0x708 own). So coop (every slot
 *             `player`) shares one wallet, and in factions mode x4lan_playerk has its own account.
 *   faction scope  slot k's events run with g_PlayerFaction = the owner of slot k's ship (U::Object::GetTrueOwner, vfunc
 *             708): everything the stock function does "for the player" (whose money pays, whose ownership counts) is
 *             slot k's faction for the length of the call, on every client at the same tick. Coop and slot 1 (`player`):
 *             nothing changes. The scope opens only inside an apply (uirep_replaying) on the frame thread, with no slot or
 *             presentation scope open; it is checked at its close and a pass never sees it open (ui_trade_scope_open).
 *   ops       UITRADE_OP_LUA (Lua-run): a Lua C global the core wraps (native_gate_ui_replay_core.lua X4LAN_UI_TRADE.GLOBALS:
 *             AddTradeToShipQueue with a portable trade offer reference, TransferPlayerMoneyTo, TransferMoneyToPlayer, the
 *             price and stock overrides, the budgets, the docked trade's SignalObject, the trade menu's virtual cargo mode); UITRADE_OP_FFI (Lua-run, applied
 *             here through gate.ui_trade_run): an FFI export of the trade group hooked below, its arguments by spec. Both in
 *             call order in the probe's frame, slot order, after the authority check in Lua (the subject belongs to the
 *             slot's faction and is no other slot's ship).
 *   hooks     the FFI exports below, at their entries with the UI guard's prefixes and pins (the generated table, looked up
 *             by name, covered by this family there: the guard leaves them alone while it is armed). Captured in the
 *             local phases (not the probe's own frame), passed through everywhere else; the apply calls the trampoline.
 *   receipt   UITRADE_1:<each slot faction's balance>, joined by the Lua half's rows (ship accounts, trade loop flags):
 *             a trade that took on one client only is a digest mismatch at that tick. */
#define UITRADE_LUA_GLOBALS 11u          /* X4LAN_UI_TRADE.GLOBALS in the Lua core */
#define UITRADE_PLAYER_FACTION 0x38f4bd0u
#define UITRADE_NULL_ACCOUNT 0x38f4bd8u   /* the dummy account (never read here: its balance getter draws) */
#define UITRADE_OWNER_SLOT 708u           /* U::Object::GetTrueOwner (+0x1620): the owner faction, 0x352b10 */
#define UITRADE_GET_TRUE_OWNER_PIN 0x352b10u
#define UITRADE_FACTION_DATA 0x280u
#define UITRADE_FACTION_ACCOUNT 0x2e0u
#define UITRADE_ACCOUNT_OF 0x3178a0u      /* a handle's object */
#define UITRADE_BALANCE_SLOT 2u           /* AccountClass vfunc +0x10: the balance in cents */
#define UITRADE_STUB 0x20u
#define UITRADE_ARGS 5u
enum {UITRADE_H_TRADE_RULE=0,UITRADE_H_BUYABLE,UITRADE_H_SELLABLE,UITRADE_H_BUY_LIMIT,UITRADE_H_SELL_LIMIT,UITRADE_H_CLEAR_BUY_LIMIT,
    UITRADE_H_CLEAR_SELL_LIMIT,UITRADE_H_ADD_WARE,UITRADE_H_REMOVE_WARE,UITRADE_H_PRICE_FACTOR,UITRADE_H_BUILD_PRICE_FACTOR,UITRADE_H_BUILD_METHOD,
    UITRADE_H_WORKFORCE,UITRADE_H_SUPPLY_MANUAL,UITRADE_H_LOOP_RESERVE,UITRADE_H_LOOP_RESERVE_CLEAR,UITRADE_H_PRODUCTION_OFFERS,UITRADE_HOOKS};
/* The FFI exports (names as ffi.cdef and the guard table know them) and their arguments: I a UniverseID, the subject
 * (always the first), u a TradeRuleID (int32), s a string, z a string or NULL, b a bool, i an int32, f a float (xmm1). */
static const struct {const char *name,*spec;} uitrade_defs[UITRADE_HOOKS]={
    {"SetContainerTradeRule","Iuszb"},{"SetContainerWareIsBuyable","Isb"},{"SetContainerWareIsSellable","Isb"},
    {"SetContainerBuyLimitOverride","Isi"},{"SetContainerSellLimitOverride","Isi"},{"ClearContainerBuyLimitOverride","Is"},
    {"ClearContainerSellLimitOverride","Is"},{"AddTradeWare","Is"},{"RemoveTradeWare","Is"},{"SetContainerGlobalPriceFactor","If"},
    {"SetContainerBuildPriceFactor","If"},{"SetContainerBuildMethod","Is"},{"SetContainerWorkforceFillCapacity","Ib"},
    {"SetSupplyManual","Isb"},{"SetShipTradeLoopCargoReservationOverride","Ib"},{"RemoveShipTradeLoopCargoReservationOverride","I"},
    {"UpdateProductionTradeOffers","I"}};
/* The Lua C globals the Lua half wraps by name: the guard leaves them to this family too (their pins are checked here). */
static const char *const uitrade_lua_names[]={"AddTradeToShipQueue","TransferPlayerMoneyTo","TransferMoneyToPlayer","SetContainerWarePriceOverride",
    "ClearContainerWarePriceOverride","SetContainerStockLimitOverride","ClearContainerStockLimitOverride","SetMinBudget","SetMaxBudget","SetVirtualCargoMode"};
#define UITRADE_LUA_NAMES (sizeof(uitrade_lua_names)/sizeof(*uitrade_lua_names))
/* What the money model and the faction scope rely on (build 9.00): GetPlayerMoney's account walk, the handle resolver,
 * TransferPlayerMoneyTo's cores reading g_PlayerFaction, the faction's AddMoney, AddPlayerMoney, GetTrueOwner. */
typedef struct {unsigned rva,size;const char *sha;} uitrade_pin;
static const uitrade_pin uitrade_pin_table[]={
    {0x288080u,0x95,"777ca5a7bf111c37735f9af2d912859832a185d91e6c16119c9c471ea7d662a3"},  /* Lua GetPlayerMoney */
    {UITRADE_ACCOUNT_OF,0xb3,"7cb7fe44dd2e4c1730bcdf14b1cf9f67c8d600d4764d6a2762b92b41d302c2b0"},
    {0x9f9be0u,0x44,"fef31de40ad7b53f0ccccab49cbfa63a6af371bd9cbe182122ce6606268e2771"},   /* player faction -> container */
    {0x9f9c30u,0x3e,"2bd70ef42d4a39f7fccdb671e1ff2f84fdc91a22af89c9a86ee74d4e2e26ce15"},   /* the same with a reason */
    {0x9dc550u,0x9a,"d3ea64a723c5c5da1769beec18bb628842dfc3c8fc822fa62a794bf305454b14"},   /* FactionClass::AddMoney */
    {0x1762d0u,0x2d,"111ec3e7e466d9d89336185fe1728fc75a2334c208e60098117590858e911586"},   /* AddPlayerMoney */
    {UITRADE_GET_TRUE_OWNER_PIN,0x8,"7900022f876803a52ef0ebfca5f0f9ae79603c127b6ad4a32bc834758e27be53"},    /* GetTrueOwner: +0x348 */
};
#define UITRADE_PIN_COUNT (sizeof(uitrade_pin_table)/sizeof(*uitrade_pin_table))
#define UITRADE_SHIP_VTABLE 0x2b84a98u    /* U::Ship: its slot 708 is GetTrueOwner (Station, Container, Object alike) */
#define UITRADE_GET_TRUE_OWNER 0x352b10u
static const uitrade_pin *uitrade_pin_tab=uitrade_pin_table;   /* tests: stand-ins without the money walk */
static unsigned uitrade_pin_count=UITRADE_PIN_COUNT;
static const uiguard_entry *uitrade_entry[UITRADE_HOOKS];
static int uitrade_installed,uitrade_restored=1,uitrade_pins_checked;
static const char *uitrade_stage="off",*uitrade_why_text="-";
static unsigned char *uitrade_page;
static unsigned char uitrade_patched[UITRADE_HOOKS][5];
static void *uitrade_tramp[UITRADE_HOOKS];       /* the trampolines (tests: stand-ins) */
static void *(*uitrade_owner_fn)(void *object);  /* tests: the owner */
static void *(*uitrade_account_fn)(void *handle);/* tests: the resolver */
static volatile LONG64 uitrade_captured,uitrade_passed,uitrade_refused,uitrade_runs,uitrade_skipped,uitrade_scopes,uitrade_scope_same,uitrade_scope_refused;
static int uitrade_scope_open;
static void *uitrade_scope_saved,*uitrade_scope_faction;
static int uitrade_wanted(void) {return uirep_armed && (uirep_families&UIREP_FAMILY_TRADE);}
static int uitrade_fail(const char *why) {uitrade_stage="failed";if(!strcmp(uitrade_why_text,"-")) uitrade_why_text=why;return 0;}

/* ---- pins: the guard table's entries by name (FFI and Lua, group trade, covered by this family), and the money walk ---- */
static const uiguard_entry *uitrade_lookup(const char *name,unsigned char kind) {
    for(unsigned i=0;i<uiguard_count;i++) if(uiguard_tab[i].kind==kind && !strcmp(uiguard_tab[i].name,name)) return &uiguard_tab[i];
    return NULL;
}
static int uitrade_entry_ok(const uiguard_entry *e) {
    return e && e->group==UIGUARD_G_TRADE && (e->covered&UIREP_FAMILY_TRADE) && e->prefix>=5 && e->prefix<=16 && !e->tail && !e->rip[0] && !e->rip[1] &&
        !e->jcc_len && !memcmp(image+e->rva,e->bytes,e->prefix) && hash_ok(image+e->rva,e->pin,e->sha);
}
static int uitrade_pins(void) {
    if(!uitrade_pins_checked) {
        int ok=1;
        for(unsigned i=0;ok && i<UITRADE_HOOKS;i++) {
            const uiguard_entry *e=uitrade_lookup(uitrade_defs[i].name,UIGUARD_FFI);
            if(!(ok=uitrade_entry_ok(e))) pin_failed_rva=e?e->rva:0;
            uitrade_entry[i]=ok?e:NULL;
        }
        for(unsigned i=0;ok && i<UITRADE_LUA_NAMES;i++) {
            const uiguard_entry *e=uitrade_lookup(uitrade_lua_names[i],UIGUARD_LUA);
            if(!(ok=uitrade_entry_ok(e))) pin_failed_rva=e?e->rva:0;
        }
        for(unsigned i=0;ok && i<uitrade_pin_count;i++)
            if(!(ok=hash_ok(image+uitrade_pin_tab[i].rva,uitrade_pin_tab[i].size,uitrade_pin_tab[i].sha))) pin_failed_rva=uitrade_pin_tab[i].rva;
        /* The owner every slot ship answers: the ship's vtable slot 708 is the pinned GetTrueOwner (the vtable is relocated). */
        if(ok && uitrade_pin_count) {
            unsigned char *slot=NULL;memcpy(&slot,image+UITRADE_SHIP_VTABLE+8u*UITRADE_OWNER_SLOT,8);
            if(!(ok=slot==image+UITRADE_GET_TRUE_OWNER)) pin_failed_rva=UITRADE_SHIP_VTABLE;
        }
        uitrade_pins_checked=ok?1:-1;
    }
    return uitrade_pins_checked==1;
}

/* ---- factions, accounts ---- */
static void *uitrade_owner(void *object) {
    if(uitrade_owner_fn) return uitrade_owner_fn(object);
    return ((void*(*)(void*))(*(void***)object)[UITRADE_OWNER_SLOT])(object);
}
/* Slot k's faction: the owner of its ship now (the same on every client); NULL without a ship. */
static void *uitrade_slot_faction(unsigned slot) {
    if(slot<1 || slot>pilot_slot_count || slot>PILOT_SLOTS || !pilot_slots[slot-1].runtime) return NULL;
    unsigned char *ship=uirep_component(pilot_slots[slot-1].runtime);
    return ship?uitrade_owner(ship):NULL;
}
/* A faction's balance in cents, as GetPlayerMoney reads the player's: 1, or 0 without a faction, -1 without an account. Not
 * the dummy account GetPlayerMoney falls back to (0x38f4bd8): U::DummyAccount's balance getter (0x96e0a0) draws from the
 * thread's RNG, and a read must not draw. */
static int uitrade_balance(void *faction,int64_t *cents) {
    unsigned char *data=NULL;
    if(!faction || !read_local((unsigned char*)faction+UITRADE_FACTION_DATA,&data,8) || !data) return 0;
    void *handle=data+UITRADE_FACTION_ACCOUNT;
    void *account=uitrade_account_fn?uitrade_account_fn(handle):((void*(*)(void*))(void*)(pnat_base+UITRADE_ACCOUNT_OF))(handle);
    if(!account) return -1;
    *cents=((int64_t(*)(void*))(*(void***)account)[UITRADE_BALANCE_SLOT])(account);
    return 1;
}

/* ---- the faction scope ---- */
static void *volatile *uitrade_player_faction(void) {return (void*volatile*)(pnat_base+UITRADE_PLAYER_FACTION);}
/* 1 opened (g_PlayerFaction = slot k's faction), 0 nothing to do (it is already), -1 refused: not inside an apply on the frame
 * thread, a scope open, no faction (no ship). */
static int uitrade_scope_enter(unsigned slot) {
    if(uitrade_scope_open || GetCurrentThreadId()!=ui_thread || !InterlockedCompareExchange(&uirep_replaying,0,0) || pres_open || pstk_scope_slot) {
        InterlockedIncrement64(&uitrade_scope_refused);return -1;
    }
    void *faction=uitrade_slot_faction(slot),*now=*uitrade_player_faction();
    if(!faction || !now) {InterlockedIncrement64(&uitrade_scope_refused);return -1;}
    if(faction==now) {InterlockedIncrement64(&uitrade_scope_same);return 0;}
    uitrade_scope_saved=now;uitrade_scope_faction=faction;*uitrade_player_faction()=faction;uitrade_scope_open=1;
    InterlockedIncrement64(&uitrade_scopes);return 1;
}
/* 1 the scope closed as it was opened; 0 something changed g_PlayerFaction inside it (it is put back all the same). */
static int uitrade_scope_leave(void) {
    if(!uitrade_scope_open) return 1;
    int ok=*uitrade_player_faction()==uitrade_scope_faction;
    *uitrade_player_faction()=uitrade_scope_saved;uitrade_scope_open=0;uitrade_scope_saved=uitrade_scope_faction=NULL;
    return ok;
}

/* ---- capture ---- */
static int uitrade_capture_now(void) {return uirep_capture_now() && !InterlockedCompareExchange(&rid_probe_frame,0,0);}
static int uitrade_has_float(unsigned hook) {return strchr(uitrade_defs[hook].spec,'f')!=NULL;}
static uint64_t uitrade_forward(unsigned hook,const uint64_t v[UITRADE_ARGS],float f) {
    if(uitrade_has_float(hook)) {((void(*)(uint64_t,float))uitrade_tramp[hook])(v[0],f);return 0;}
    return ((uint64_t(*)(uint64_t,uint64_t,uint64_t,uint64_t,uint64_t))uitrade_tramp[hook])(v[0],v[1],v[2],v[3],v[4]);
}
static void uitrade_put32(unsigned char *out,uint32_t v) {for(unsigned b=0;b<4;b++) out[b]=(unsigned char)(v>>(8*b));}
static uint32_t uitrade_get32(const unsigned char *in) {uint32_t v=0;for(unsigned b=0;b<4;b++) v|=(uint32_t)in[b]<<(8*b);return v;}
/* A hooked export was called. Not this client's UI: X4's own (simulation, the probe, an apply). Else one event, and the
 * function does not run here (its effect arrives with the apply, on every client). Every one of them is void. */
static uint64_t uitrade_hooked(unsigned hook,const uint64_t v[UITRADE_ARGS],float f) {
    if(!uitrade_capture_now()) {InterlockedIncrement64(&uitrade_passed);return uitrade_forward(hook,v,f);}
    unsigned char data[UIREP_EVENT_MAX];unsigned n=0;const char *why=NULL;
    data[n++]=(unsigned char)hook;
    for(unsigned i=0;uitrade_defs[hook].spec[i] && !why;i++) {
        uint64_t a=i<UITRADE_ARGS?v[i]:0;
        switch(uitrade_defs[hook].spec[i]) {
        case 'I': if(!a || !uirep_component(a) || !uirep_check_of(a)) why="unmapped";else {uirep_put_ref(data+n,a);n+=UIREP_REF;} break;
        case 'u':case 'i': uitrade_put32(data+n,(uint32_t)a);n+=4;break;
        case 'b': data[n++]=(unsigned char)(a&0xff)!=0;break;
        case 'f': {uint32_t bits=0;memcpy(&bits,&f,4);uitrade_put32(data+n,bits);n+=4;break;}
        case 's':case 'z': {
            const char *s=(const char*)(uintptr_t)a;size_t len=s?strnlen(s,UIREP_STRING_MAX+1):0;
            if(!s && uitrade_defs[hook].spec[i]=='z') {data[n++]=0xff;break;}
            if(!s || len>UIREP_STRING_MAX) why="string";else {data[n++]=(unsigned char)len;memcpy(data+n,s,len);n+=(unsigned)len;}
            break;
        }
        default:why="spec";break;
        }
    }
    if(why) {InterlockedIncrement64(&uitrade_refused);uirep_note_refusal(uitrade_defs[hook].name,why);return 0;}
    if(uirep_push(UITRADE_OP_FFI,data,n)) InterlockedIncrement64(&uitrade_captured);
    return 0;
}
#define UITRADE_W(n,h) static uint64_t uitrade_w_##n(uint64_t a,uint64_t b,uint64_t c,uint64_t d,uint64_t e) { \
    uint64_t v[UITRADE_ARGS]={a,b,c,d,e};return uitrade_hooked(h,v,0.0f);}
#define UITRADE_WF(n,h) static void uitrade_w_##n(uint64_t a,float f) {uint64_t v[UITRADE_ARGS]={a,0,0,0,0};uitrade_hooked(h,v,f);}
UITRADE_W(trade_rule,UITRADE_H_TRADE_RULE)
UITRADE_W(buyable,UITRADE_H_BUYABLE)
UITRADE_W(sellable,UITRADE_H_SELLABLE)
UITRADE_W(buy_limit,UITRADE_H_BUY_LIMIT)
UITRADE_W(sell_limit,UITRADE_H_SELL_LIMIT)
UITRADE_W(clear_buy_limit,UITRADE_H_CLEAR_BUY_LIMIT)
UITRADE_W(clear_sell_limit,UITRADE_H_CLEAR_SELL_LIMIT)
UITRADE_W(add_ware,UITRADE_H_ADD_WARE)
UITRADE_W(remove_ware,UITRADE_H_REMOVE_WARE)
UITRADE_WF(price_factor,UITRADE_H_PRICE_FACTOR)
UITRADE_WF(build_price_factor,UITRADE_H_BUILD_PRICE_FACTOR)
UITRADE_W(build_method,UITRADE_H_BUILD_METHOD)
UITRADE_W(workforce,UITRADE_H_WORKFORCE)
UITRADE_W(supply_manual,UITRADE_H_SUPPLY_MANUAL)
UITRADE_W(loop_reserve,UITRADE_H_LOOP_RESERVE)
UITRADE_W(loop_reserve_clear,UITRADE_H_LOOP_RESERVE_CLEAR)
UITRADE_W(production_offers,UITRADE_H_PRODUCTION_OFFERS)
static void *const uitrade_wrappers[UITRADE_HOOKS]={(void*)uitrade_w_trade_rule,(void*)uitrade_w_buyable,(void*)uitrade_w_sellable,
    (void*)uitrade_w_buy_limit,(void*)uitrade_w_sell_limit,(void*)uitrade_w_clear_buy_limit,(void*)uitrade_w_clear_sell_limit,(void*)uitrade_w_add_ware,
    (void*)uitrade_w_remove_ware,(void*)uitrade_w_price_factor,(void*)uitrade_w_build_price_factor,(void*)uitrade_w_build_method,(void*)uitrade_w_workforce,
    (void*)uitrade_w_supply_manual,(void*)uitrade_w_loop_reserve,(void*)uitrade_w_loop_reserve_clear,(void*)uitrade_w_production_offers};

/* ---- the events on the wire ---- */
/* The bytes an FFI event's arguments take; 0 = not a valid event. */
static unsigned uitrade_length(const unsigned char *data,unsigned n) {
    if(!n || data[0]>=UITRADE_HOOKS) return 0;
    unsigned at=1;const char *spec=uitrade_defs[data[0]].spec;
    for(unsigned i=0;spec[i];i++) {
        unsigned need=spec[i]=='I'?UIREP_REF:spec[i]=='b'?1:(spec[i]=='u' || spec[i]=='i' || spec[i]=='f')?4:0;
        if(spec[i]=='s' || spec[i]=='z') {
            if(at>=n) return 0;
            if(spec[i]=='z' && data[at]==0xff) need=1;
            else if(data[at]>UIREP_STRING_MAX) return 0;
            else need=1u+data[at];
        }
        if(need>n-at) return 0;
        at+=need;
        if(spec[i]=='b' && data[at-1]>1) return 0;
    }
    return at;
}
/* A section's trade event (lockstep_ui_replay.h uirep_event_ok). Any slot: each one's own faction pays. */
static int uitrade_event_ok(unsigned slot,unsigned op,const unsigned char *data,unsigned n,const char **why) {
    (void)slot;
    if(!(uirep_families&UIREP_FAMILY_TRADE)) {*why="ui_trade";return 0;}
    if(op==UITRADE_OP_LUA) {
        if(n<2 || !data[0] || data[0]>UITRADE_LUA_GLOBALS) {*why="ui_trade";return 0;}
        return 1;
    }
    if(uitrade_length(data,n)!=n) {*why="ui_trade";return 0;}
    return 1;
}
/* An FFI event's subject (its first argument): 1 resolved, 0 gone (the same everywhere), -1 the identities differ. */
static int uitrade_subject(const unsigned char *data,unsigned n,uint64_t *runtime) {
    unsigned char *component=NULL;
    if(uitrade_length(data,n)!=n) return -1;
    return uirep_resolve(data+1,runtime,&component);
}
/* One FFI event of slot `slot` (the probe's frame, every client, after the Lua half's authority check): 1 = X4's function ran
 * in the slot's faction scope, 0 = nothing ran, the same everywhere (the subject is gone, the slot has no faction),
 * -1 = stop (decode, the identities differ, a fault, the scope changed under the call). */
static int uitrade_run(unsigned slot,const unsigned char *data,unsigned n,char *what,size_t capacity) {
    static char strings[UITRADE_ARGS][UIREP_STRING_MAX+1];
    uint64_t v[UITRADE_ARGS]={0},runtime=0;float f=0.0f;
    if(slot<1 || slot>PILOT_SLOTS || uitrade_length(data,n)!=n) {snprintf(what,capacity,"decode");return -1;}
    const char *spec=uitrade_defs[data[0]].spec,*name=uitrade_defs[data[0]].name;unsigned at=1;
    for(unsigned i=0;spec[i] && i<UITRADE_ARGS;i++) {
        switch(spec[i]) {
        case 'I': {
            unsigned char *component=NULL;int resolved=uirep_resolve(data+at,&runtime,&component);at+=UIREP_REF;
            if(resolved<0) {snprintf(what,capacity,"ref_mismatch");return -1;}
            if(!resolved) {InterlockedIncrement64(&uitrade_skipped);snprintf(what,capacity,"%s gone",name);return 0;}
            v[i]=runtime;break;
        }
        case 'u':case 'i': v[i]=(uint64_t)(int64_t)(int32_t)uitrade_get32(data+at);at+=4;break;
        case 'b': v[i]=data[at++];break;
        case 'f': {uint32_t bits=uitrade_get32(data+at);memcpy(&f,&bits,4);at+=4;break;}
        case 's':case 'z':
            if(spec[i]=='z' && data[at]==0xff) {v[i]=0;at++;break;}
            {unsigned len=data[at++];memcpy(strings[i],data+at,len);strings[i][len]=0;at+=len;v[i]=(uint64_t)(uintptr_t)strings[i];}
            break;
        default:break;
        }
    }
    int scope=uitrade_scope_enter(slot);
    if(scope<0) {InterlockedIncrement64(&uitrade_skipped);snprintf(what,capacity,"%s no_faction",name);return 0;}
    int fault=0;
    __try {uitrade_forward(data[0],v,f);}
    __except(GetExceptionCode()==EXCEPTION_ACCESS_VIOLATION?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH) {fault=1;}
    int left=uitrade_scope_leave();
    if(fault) {snprintf(what,capacity,"fault");return -1;}
    if(!left) {snprintf(what,capacity,"scope");return -1;}
    InterlockedIncrement64(&uitrade_runs);
    snprintf(what,capacity,"%s%s",name,scope?" scope":"");
    return 1;
}

/* ---- install (with the replay's pass) ---- */
static unsigned char *uitrade_stride(unsigned i) {return uitrade_page+UIREP_STRIDE*i;}
static void uitrade_uninstall(void) {
    for(unsigned i=UITRADE_HOOKS;i-->0;) {
        const uiguard_entry *e=uitrade_entry[i];
        if(!e || !uitrade_page || memcmp(pnat_base+e->rva,uitrade_patched[i],5)) continue;
        pview_cas(e->rva,uitrade_patched[i],e->bytes,5);
    }
    int clean=1;
    for(unsigned i=0;i<UITRADE_HOOKS;i++) if(uitrade_entry[i] && uitrade_page && memcmp(pnat_base+uitrade_entry[i]->rva,uitrade_entry[i]->bytes,5)) clean=0;
    uitrade_restored=clean;
    if(clean) uitrade_installed=0;
}
static int uitrade_install(void) {
    if(uitrade_installed) return 1;
    for(unsigned i=0;i<UITRADE_HOOKS;i++) if(!uitrade_entry[i] || memcmp(pnat_base+uitrade_entry[i]->rva,uitrade_entry[i]->bytes,uitrade_entry[i]->prefix)) return 0;
    if(!uitrade_page && !(uitrade_page=uirep_alloc_page())) return 0;
    DWORD old,ignored;
    if(!VirtualProtect(uitrade_page,UIREP_PAGE,PAGE_READWRITE,&old)) return 0;
    memset(uitrade_page,0xcc,UIREP_PAGE);
    for(unsigned i=0;i<UITRADE_HOOKS;i++) {
        const uiguard_entry *e=uitrade_entry[i];unsigned char *p=uitrade_stride(i);
        memcpy(p,e->bytes,e->prefix);pview_abs_jump(p+e->prefix,pnat_base+e->rva+e->prefix);
        pview_abs_jump(p+UITRADE_STUB,uitrade_wrappers[i]);
    }
    if(!VirtualProtect(uitrade_page,UIREP_PAGE,PAGE_EXECUTE_READ,&ignored)) return 0;
    FlushInstructionCache(GetCurrentProcess(),uitrade_page,UIREP_PAGE);
    for(unsigned i=0;i<UITRADE_HOOKS;i++) {
        if(!uitrade_tramp[i]) uitrade_tramp[i]=uitrade_stride(i);
        int64_t rel=(intptr_t)(uitrade_stride(i)+UITRADE_STUB)-(intptr_t)(pnat_base+uitrade_entry[i]->rva+5);
        if(rel<INT32_MIN || rel>INT32_MAX) return 0;
        int32_t r32=(int32_t)rel;uitrade_patched[i][0]=0xe9;memcpy(uitrade_patched[i]+1,&r32,4);
    }
    uitrade_restored=0;uitrade_installed=1;
    for(unsigned i=0;i<UITRADE_HOOKS;i++)
        if(!pview_cas(uitrade_entry[i]->rva,uitrade_entry[i]->bytes,uitrade_patched[i],5)) {uitrade_uninstall();uitrade_installed=0;return 0;}
    return 1;
}
static int uitrade_intact(void) {
    if(!uitrade_installed || !uitrade_page) return 0;
    for(unsigned i=0;i<UITRADE_HOOKS;i++) if(memcmp(pnat_base+uitrade_entry[i]->rva,uitrade_patched[i],5)) return 0;
    return 1;
}
/* The replay's pass (uirep_guard_pass): 0 = stop with uitrade_why_text. */
static int uitrade_pass(uint64_t tick) {
    if(!uitrade_wanted()) return 1;
    if(uitrade_scope_open) {uitrade_scope_leave();return uitrade_fail("ui_trade_scope_open");}
    if(tick>pilot_stop) {
        if(uitrade_installed) {uitrade_uninstall();if(!uitrade_restored) return uitrade_fail("ui_trade_restore");uitrade_stage="stopped";}
        return 1;
    }
    if(!uitrade_installed) {
        if(!uitrade_pins()) return uitrade_fail("ui_trade_pins");
        if(!uitrade_install()) return uitrade_fail("ui_trade_install");
        uitrade_stage="live";return 1;
    }
    if(!uitrade_intact()) return uitrade_fail("ui_trade_hook_lost");
    return 1;
}
static void uitrade_restore(void) {
    if(uitrade_scope_open) uitrade_scope_leave();
    if(uitrade_installed) uitrade_uninstall();
    if(uitrade_installed) uitrade_stage="restore_refused";
}

/* ---- rows ---- */
/* UITRADE_1:<slot 1 faction balance>,<slot 2>,... in cents, x without a faction, n without an account: every client reads the
 * same world. */
static int uitrade_frame_text(char *out,size_t capacity) {
    int n=snprintf(out,capacity,"UITRADE_1:");
    for(unsigned k=1;n>0 && (size_t)n<capacity && k<=pilot_slot_count && k<=PILOT_SLOTS;k++) {
        int64_t cents=0;void *faction=uitrade_slot_faction(k);int more,b=faction?uitrade_balance(faction,&cents):0;
        if(b>0) more=snprintf(out+n,capacity-(size_t)n,"%s%lld",k>1?",":"",(long long)cents);
        else more=snprintf(out+n,capacity-(size_t)n,"%s%s",k>1?",":"",b<0?"n":"x");
        if(more<0 || (size_t)more>=capacity-(size_t)n) return 0;
        n+=more;
    }
    return n>0 && (size_t)n<capacity?n:0;
}
static int uitrade_status(char *out,size_t capacity) {
    int n=snprintf(out,capacity,"ui_trade=%s why=%s hooks=%u installed=%d restored=%d captured=%lld refused=%lld passed=%lld runs=%lld skipped=%lld "
        "scopes=%lld/%lld/%lld",uitrade_stage,uitrade_why_text,(unsigned)UITRADE_HOOKS,uitrade_installed,uitrade_restored,(long long)uitrade_captured,
        (long long)uitrade_refused,(long long)uitrade_passed,(long long)uitrade_runs,(long long)uitrade_skipped,(long long)uitrade_scopes,
        (long long)uitrade_scope_same,(long long)uitrade_scope_refused);
    return n>0 && (size_t)n<capacity?n:0;
}
