/* UI replay family 'inventory' (bit 524288, native_gate_ui_replay lists it; stop reason 164 as the replay), native part.
 * The family is Lua (tools/dev/native_gate_ui_inventory_core.lua): a click on a stock button becomes one Lua-run event of
 * the client's slot (UIINV_OP_CLICK: kind, then the Lua codec's arguments) and runs on every client, the inventory kinds
 * inside that slot's scope, where X4's "the player" is the slot's own player entity and so the inventory its own.
 *
 * What Lua cannot capture is here, as claims on the UI guard's entries (lockstep_ui_guard.h, docs/LOCKSTEP-REGISTRY.txt
 * section 3):
 *   drop      the inventory menu's Drop calls the FFI export DropInventory(entity, lockbox, wares, count)
 *             (menu_playerinfo.lua buttonInventoryDrop and its drop-all). In a local phase of the capturing client the call
 *             becomes a kind 3 event of its slot in the codec's own bytes (count; the lockbox macro or nil; then ware,
 *             amount pairs), the menu gets true and X4's function does not run here. The Lua half checks the amounts
 *             against the slot's inventory at the apply and calls the export for the slot's player inside its scope.
 *   recorder  the exports the stock buttons of the Lua half's scopes call (uiinv_calls: AddPlayerMoney, LearnBlueprint,
 *             StartResearch, ClearProductionItems, the terraforming start and abort, all void; and
 *             CancelPlayerInvolvedTradeDeal, whose check-only form the guard passes as a query). While a scope is open on
 *             this client (gate.ui_inventory_scope(1), the UI thread) a call does not run: it is a row of the record,
 *             "name<tab>argument...<newline>" (a UniverseID and an int64 in decimal, a string as it is), which the scope's
 *             end (gate.ui_inventory_scope(0)) hands to Lua; the click's event carries it. With no scope open the claim
 *             declines and the guard's own rule applies (refused, as before the family).
 * The guard passes an apply, so the Lua half calls the exports themselves there. Slots 2..N need missions per player, whose
 * call scope the slot's scope is; without it, with a call that does not fit, or for another entity's inventory the call
 * is refused here (a row, the info line through the replay's refusals) and nothing runs. */
#define UIINV_KIND_DROP 3u
#define UIINV_WARE_MAX 64u          /* the codec's STRING_MAX */
#define UIINV_DROP_WARES 15u        /* 1 + 2 * wares values stay inside the codec's ITEMS_MAX (32) */
#define UIINV_TAG_NIL 0u            /* native_gate_ui_replay_core.lua TAG */
#define UIINV_TAG_NUMBER 3u
#define UIINV_TAG_STRING 4u
typedef struct {const char *ware;int32_t amount;} uiinv_ware_amount;   /* the stock UIWareAmount (16 bytes) */
_Static_assert(sizeof(uiinv_ware_amount)==16,"UIWareAmount");
/* The recorded exports and their arguments (native_gate_ui_inventory_core.lua K.FFI): I a UniverseID, s a string, q an int64,
 * Q a 64-bit ID that is no component (a trade deal's: this client's own, the Lua half makes it portable), b a bool. */
#define UIINV_CALLS 7u
static const struct {const char *name,*spec;} uiinv_calls[UIINV_CALLS]={
    {"AddPlayerMoney","q"},{"LearnBlueprint","s"},{"StartResearch","sI"},{"ClearProductionItems","I"},
    {"StartTerraformingProject","Is"},{"AbortActiveTerraformingProject","I"},{"CancelPlayerInvolvedTradeDeal","IQb"}};
/* What the menu is answered while a call is recorded: nothing for the void ones, "done" for the cancelled deal (the menu
 * then hides the row until its next refresh, as after X4's own true). */
static const unsigned char uiinv_call_answer[UIINV_CALLS]={0,0,0,0,0,0,1};
#define UIINV_RECORD_MAX 2048u
static unsigned uiinv_drop_index=UINT_MAX,uiinv_call_index[UIINV_CALLS];
static int uiinv_armed,uiinv_scope_open,uiinv_record_lost;
static char uiinv_record[UIINV_RECORD_MAX];
static unsigned uiinv_record_length;
static const char *uiinv_why="-";
static volatile LONG64 uiinv_drops_captured,uiinv_drops_refused,uiinv_scopes,uiinv_recorded,uiinv_lost;

/* The codec's string and number (a double, as Lua writes it) at out+n: the new length, 0 when it does not fit. */
static unsigned uiinv_put_string(unsigned char *out,unsigned n,const char *text,unsigned length) {
    if(length>UIINV_WARE_MAX || n+2u+length>UIREP_EVENT_MAX) return 0;
    out[n]=UIINV_TAG_STRING;out[n+1]=(unsigned char)length;memcpy(out+n+2,text,length);return n+2u+length;
}
static unsigned uiinv_put_number(unsigned char *out,unsigned n,double value) {
    if(n+9u>UIREP_EVENT_MAX) return 0;
    out[n]=UIINV_TAG_NUMBER;memcpy(out+n+1,&value,8);return n+9u;
}
/* DropInventory's arguments as a kind 3 event: its length, 0 and *why when the call is not one this family carries. */
static unsigned uiinv_drop_event(const char *lockbox,const uiinv_ware_amount *wares,uint64_t count,unsigned char *out,const char **why) {
    char text[UIFAC_STRING_MAX+1];unsigned length=0,n=2;
    if(!count || count>UIINV_DROP_WARES || !wares) {*why="wares";return 0;}
    out[0]=UIINV_KIND_DROP;out[1]=(unsigned char)(1u+2u*(unsigned)count);
    if(!lockbox) out[n++]=UIINV_TAG_NIL;
    else if(!uifac_cstr(lockbox,text,&length) || !length || !(n=uiinv_put_string(out,n,text,length))) {*why="lockbox";return 0;}
    for(unsigned i=0;i<(unsigned)count;i++) {
        uiinv_ware_amount w;
        if(!read_local(&wares[i],&w,sizeof(w)) || !w.ware || !uifac_cstr(w.ware,text,&length) || !length) {*why="ware";return 0;}
        if(w.amount<1) {*why="amount";return 0;}
        if(!(n=uiinv_put_string(out,n,text,length)) || !(n=uiinv_put_number(out,n,(double)w.amount))) {*why="too_long";return 0;}
    }
    return n;
}
/* One call of a recorded export into the open scope's record: 1 = a row was added, 0 = the record is lost (a string the
 * rows cannot carry, no room): the scope's end says so and the click is refused whole. */
static int uiinv_record_call(unsigned c,const uint64_t *frame) {
    char row[160],text[UIFAC_STRING_MAX+1];int n=snprintf(row,sizeof(row),"%s",uiinv_calls[c].name);
    for(unsigned i=0;uiinv_calls[c].spec[i] && n>0;i++) {
        const uint64_t v=uiguard_arg(frame,i);unsigned length=0;int more=0;
        if((size_t)n+24u>=sizeof(row)) return 0;
        switch(uiinv_calls[c].spec[i]) {
        case 'q': more=snprintf(row+n,sizeof(row)-(size_t)n,"\t%lld",(long long)(int64_t)v);break;
        case 'I':case 'Q': more=snprintf(row+n,sizeof(row)-(size_t)n,"\t%llu",(unsigned long long)v);break;
        case 'b': more=snprintf(row+n,sizeof(row)-(size_t)n,"\t%u",(unsigned)(v&0xffu)?1u:0u);break;
        default:
            if(!v || !uifac_cstr((const char*)(uintptr_t)v,text,&length) || !length || length>UIINV_WARE_MAX) return 0;
            for(unsigned k=0;k<length;k++) if((unsigned char)text[k]<0x20) return 0;   /* a tab or a newline would break the rows */
            more=snprintf(row+n,sizeof(row)-(size_t)n,"\t%s",text);
        }
        if(more<=0 || (size_t)(n+more)>=sizeof(row)) return 0;
        n+=more;
    }
    if(n<=0 || uiinv_record_length+(unsigned)n+1u>=UIINV_RECORD_MAX) return 0;
    memcpy(uiinv_record+uiinv_record_length,row,(size_t)n);uiinv_record_length+=(unsigned)n;uiinv_record[uiinv_record_length++]='\n';
    uiinv_record[uiinv_record_length]=0;
    return 1;
}
/* gate.ui_inventory_scope(1): the scope of a click opens on this client (1, or 0 when the family cannot capture now);
 * (0): it closes, the record's rows (*lost: a call did not fit, the click must be refused). */
static int uiinv_scope_begin(void) {
    uiinv_scope_open=0;uiinv_record_length=0;uiinv_record[0]=0;uiinv_record_lost=0;
    if(!uiinv_armed || GetCurrentThreadId()!=ui_thread || !pnat_is_live() || !pstk_capturing) return 0;
    uiinv_scope_open=1;InterlockedIncrement64(&uiinv_scopes);return 1;
}
static const char *uiinv_scope_end(int *lost) {
    uiinv_scope_open=0;*lost=uiinv_record_lost;
    if(uiinv_record_lost) InterlockedIncrement64(&uiinv_lost);
    return uiinv_record;
}
/* The guard's question for a claimed entry, in a local phase: 1 = answered here (*ret is the menu's answer). */
static int uiinv_claim_decide(unsigned index,const uint64_t *frame,uint64_t *ret) {
    *ret=0;
    if(!uiinv_armed) return 0;
    for(unsigned c=0;c<UIINV_CALLS;c++) {
        if(index!=uiinv_call_index[c]) continue;
        if(!uiinv_scope_open || GetCurrentThreadId()!=ui_thread) return 0;   /* no click of this family: the guard's own rule */
        if(uiinv_record_call(c,frame)) {InterlockedIncrement64(&uiinv_recorded);*ret=uiinv_call_answer[c];} else uiinv_record_lost=1;
        return 1;                                                           /* nothing runs here */
    }
    if(index!=uiinv_drop_index) return 0;
    unsigned char data[UIREP_EVENT_MAX];const char *why="not_capturing";unsigned n=0;
    const uint64_t entity=uiguard_arg(frame,0);
    /* A person's click on this client: the entity is the player the menus see (the presentation scope's singleton). */
    const unsigned char *player=NULL;uint64_t own=0;
    unsigned char *universe=NULL;
    if(read_local(pnat_base+PNAT_UNIVERSE,&universe,8) && universe && read_local(universe+0x230,(void*)&player,8) && player) read_local(player+UIREP_ID,&own,8);
    if(!pnat_is_live() || !pstk_capturing) why="not_capturing";
    else if(pstk_capture_slot!=1 && !mpp_armed) why="needs_missions_per_player";
    else if(!own || entity!=own) why="not_the_player";
    else n=uiinv_drop_event((const char*)(uintptr_t)uiguard_arg(frame,1),(const uiinv_ware_amount*)(uintptr_t)uiguard_arg(frame,2),
                            uiguard_arg(frame,3)&0xffffffffu,data,&why);
    if(n && uirep_push(UIINV_OP_CLICK,data,n)) {InterlockedIncrement64(&uiinv_drops_captured);*ret=1;return 1;}
    InterlockedIncrement64(&uiinv_drops_refused);
    if(!n) uirep_note_refusal("DropInventory",why);   /* a refused push noted itself */
    return 1;                                         /* refused here: X4's DropInventory does not run on one client */
}
static unsigned uiinv_entry_of(const char *name) {
    for(unsigned i=0;i<uiguard_count && i<UIGUARD_MAX;i++)
        if(uiguard_tab[i].kind==UIGUARD_FFI && uiguard_tab[i].name && !strcmp(uiguard_tab[i].name,name)) return i;
    return UINT_MAX;
}
static int uiinv_arm_refuse(const char *name,const char *why) {
    static char text[96];snprintf(text,sizeof(text),"ui_inventory_%s:%s",why,name);uiinv_why=text;
    uiguard_unclaim(uiinv_claim_decide);uiinv_drop_index=UINT_MAX;
    for(unsigned c=0;c<UIINV_CALLS;c++) uiinv_call_index[c]=UINT_MAX;
    return 0;
}
/* gate.ui_inventory(): after gate.ui_replay (with the family) and gate.ui_guard; 1, or 0 and uiinv_why. */
static int uiinv_arm(void) {
    uiinv_armed=0;uiinv_scope_open=0;uiinv_drop_index=UINT_MAX;uiguard_unclaim(uiinv_claim_decide);
    for(unsigned c=0;c<UIINV_CALLS;c++) uiinv_call_index[c]=UINT_MAX;
    if(!uirep_armed || !(uirep_families&UIREP_FAMILY_INVENTORY)) {uiinv_why="needs_family";return 0;}
    if(!uiguard_armed) {uiinv_why="needs_ui_guard";return 0;}
    if((uiinv_drop_index=uiinv_entry_of("DropInventory"))==UINT_MAX) return uiinv_arm_refuse("DropInventory","guard_entry");
    if(!uiguard_claim(uiinv_drop_index,uiinv_claim_decide)) return uiinv_arm_refuse("DropInventory",uiguard_claim_why);
    for(unsigned c=0;c<UIINV_CALLS;c++) {
        if((uiinv_call_index[c]=uiinv_entry_of(uiinv_calls[c].name))==UINT_MAX) return uiinv_arm_refuse(uiinv_calls[c].name,"guard_entry");
        if(!uiguard_claim(uiinv_call_index[c],uiinv_claim_decide)) return uiinv_arm_refuse(uiinv_calls[c].name,uiguard_claim_why);
    }
    uiinv_drops_captured=uiinv_drops_refused=uiinv_scopes=uiinv_recorded=uiinv_lost=0;uiinv_armed=1;uiinv_why="-";return 1;
}
static int uiinv_status(char *out,size_t capacity) {
    return snprintf(out,capacity,"ui_inventory=%s claims=%u drops=%lld/%lld scopes=%lld recorded=%lld lost=%lld",uiinv_armed?"armed":"off",
        uiinv_armed?UIINV_CALLS+1u:0u,(long long)uiinv_drops_captured,(long long)uiinv_drops_refused,(long long)uiinv_scopes,
        (long long)uiinv_recorded,(long long)uiinv_lost);
}
