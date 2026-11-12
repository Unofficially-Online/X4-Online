/* Component slot player actions (the interact menu's PerformCompSlotPlayerAction), captured and replayed.
 * Part of native_gate_ui_replay with native_gate_ui_compslot (a session default since the seat click and the transporter
 * terminal ran live with real clicks, 12 Nov; stop reason 164 through the replay). Package 1 "suit and seat"; the class
 * table is shared with package 3 (on-foot clicks).
 *
 * What X4 does [F, pinned 9.00]. The export PerformCompSlotPlayerAction 0x1b5450(UIComponentSlot *slot, uint32_t index)
 * (slot = {UniverseID component; const char *connection}) finds the component (0xced70, mode 4), resolves the connection
 * by the hash of its name and asks the component for its action container (vtable +0x1338; Ship 0x7d84d0). A container
 * is U::SingleActionContainer / InstantActionContainer / MainTypeInstantActionContainer (one action at +0x10, perform
 * 0x3508e0) or U::MultiActionContainer (a vector of actions +0x10..+0x18, perform 0x350d60); the export calls
 * container->perform(index) (vtable +0x90), which runs the action's own execute (the action's vtable +0x10), and
 * destroys the container. The actions are U::Action classes, most of them U::TimedAction<5>: U::CycleAirlockAction
 * (vtable 0x2b45758: its execute posts U::AirlockCycledEvent, whose handler in the room 0x773340 puts the player into a
 * spacesuit through 0x70efd0, the eject of lockstep_pilot_death.h), U::ControlPositionAction (0x2b45548: take a control
 * position, the seat), U::ActivateTransporterRoomAction (0x2b45a38), U::TeleportLuaAction (0x2b84840), the consoles, the
 * crates, the doors and more. The menu (menu_interactmenu.lua) lists them with GetCompSlotPlayerActions and calls the
 * export for the one clicked. Until now the guard refused the export (group flight), so nothing on foot could be used.
 *
 * Here:
 *   claim     the export's guard entry is this module's (uiguard_claim, as the docked menu's GetUp is the foot module's).
 *   class     two entry hooks sit on the containers' perform functions. Outside a call of ours they pass (X4's own
 *             callers). In a PEEK they note the class of the action asked for (its vtable's RVA) and answer 0 without
 *             running it; in an APPLY they run X4's perform when the class is the one the event names.
 *   capture   in a local phase on the capturing client: the export runs once as a PEEK through the guard's trampoline
 *             (the container is made and destroyed, nothing is performed), the class is looked up in the verdict table
 *             for this client's slot; allowed: one native event (op 0x08: the component's reference, the action's index,
 *             its class, the connection's name) and the menu gets true; else the guard's refusal with its info line.
 *   apply     every client at capture + delay, slot 1 canonical, slot k in its scope (the replay's native events): the
 *             reference resolved (gone: stale), the export through the trampoline as an APPLY. An action that is no
 *             longer there, or is of another class now, performs nothing on any client (stale).
 *   verdict   uics_verdicts[]: per class which slots may use it; a class goes in with the handling it needs. Slot 1: the
 *             airlock and the control position (X4's own player; lockstep_pilot_death.h adopts the vehicle X4 puts it
 *             into). Every other class and every other slot is refused, its class named in the status (last=).
 *   seat      slots 2..N, walking: the control position of a seat in the slot's own ship is not performed (X4 would seat
 *             its own player, slot 1's). The click becomes the slot's GET_UP in its stock record, which is its sit-down
 *             while it walks (lockstep_pilot_foot_slots.h pfslot_get_up); no event of this module. A seat of another
 *             ship stays refused.
 *   airlock   slots 2..N, walking: the airlock of the slot's own ship is captured as an event of this module and at the
 *             apply never performed (X4's event would eject its own player): the death module takes the slot out into a
 *             spacesuit of its own (lockstep_pilot_death.h pvehk_*, needs native_gate_pilot_death_suit).
 *   instant   the same actions without the menu. On foot the target system (targetsystem.lua startSofttargetInteraction)
 *             shows a target element for what the crosshair is on (a seat: "Take Command of Ship") and, for an element
 *             with an instant action, a click or the interact key calls the Lua global PerformAction(messageID, n)
 *             [F b10b70: the message of the id (ad6ec0), its action container at +0x90, perform(n-1) through the same
 *             vtable slot +0x90]. The guard refused it (group flight): in the plain game one click on the pilot's seat
 *             sits the player down, in a session nothing happened (the user's session of 11 Nov 22:25; a real click
 *             in a test window, `refused by=PerformAction`). Its guard entry is claimed too: the message's component
 *             and connection come from X4's own GetMessageDetails3, the class from the container read in place
 *             (nothing is called that performs), and from there the capture is the menu's: slot k's own seat is
 *             its sit-down, an allowed class becomes the same event (the action's index as the container takes it).
 *   terminal  the transporter room's terminal (U::ActivateTransporterRoomAction, an instant action; in a small ship the
 *             keypad at the back of the cockpit: "Use Transporter Room"). Its execute (35d5c0) only opens the menu
 *             TransporterMenu for the room (UI, this client's), so for a walking slot at a terminal of its own ship X4's
 *             own PerformAction runs (through the guard's trampoline, in the presentation scope the UI is in; no event).
 *             The menu lists the room's targets (GetValidTransporterTargets2); every way out of it ends in the export
 *             TransportPlayerToTarget(UIComponentSlot) [F 1bcc70], which moves X4's own player and is refused by the
 *             guard. Its entry is claimed: the target "Use Spacesuit" is a zone (the menu's own test, class 0x6d),
 *             and for a walking slot 2..N that may leave it becomes this module's event with the airlock's class on
 *             the slot's ship: at the apply the slot goes out into its own spacesuit as through the airlock
 *             (lockstep_pilot_death.h pvehk_*). Any other target, and slot 1 (X4's own transport is not replayed yet),
 *             stays refused.
 * Status UI_REPLAY rows: compslot=on:claimed:captured:refused:applied:stale:passed last=<class RVA of the last peek>
 * seat=<clicks of slots 2..N on their own seat> instant=<PerformAction calls decided here>
 * terminal=<claimed>:<menus opened by X4's own>:<spacesuit asked through the menu>. */
#define UICS_SINGLE_PERFORM 0x3508e0u
#define UICS_MULTI_PERFORM 0x350d60u
#define UICS_NAME_MAX 60u
#define UICS_MESSAGE 0xad6ec0u            /* the UI message of an id (a leaf; PerformAction's and GetMessageDetails3's lookup) */
#define UICS_MESSAGE_SIZE 0xa8u
#define UICS_MESSAGE_SHA "702af72b68385c061cd08d4123bc8738bf5fc8e6cf2a962b8fb9c194dc125be2"
#define UICS_MESSAGE_CONTAINER 0x90u      /* the message's action container */
#define UICS_DETAILS 0xb03b70u            /* the export GetMessageDetails3(out, id): poi, component, type, connection, flags */
#define UICS_DETAILS_SIZE 0x58u
#define UICS_DETAILS_SHA "05ab4aa71add2181c1018dce0f59aa5190f636ce859a347561a5edc8a79af98c"
#define UICS_CONTAINER_PERFORM 0x90u      /* a container's vtable: perform(index) */
typedef struct {uint64_t poi,component;const char *type,*connection;unsigned char flags[8];} uics_message_details;
enum {UICS_PASS=0,UICS_PEEK=1,UICS_APPLY=2};
enum {UICS_SLOT_ONE=1u,UICS_SLOT_OTHERS=2u};
typedef struct {uint32_t klass;unsigned slots;const char *name;} uics_verdict_row;
/* The classes a slot may use; a row is added with the handling its class needs.
 *   CycleAirlockAction     slot 1: X4's own eject into a spacesuit; the walking pass adopts the suit as the slot's ship and
 *                          takes the seat X4 gives after the suit (lockstep_pilot_death.h pveh_*). Slots 2..N: their own
 *                          ship's, while they walk (pvehk_*; uics_slot_airlock).
 *   ControlPositionAction  slot 1: X4 seats its own player; its own ship's seat was adopted before, another ship's is now. */
#define UICS_CLASS_AIRLOCK 0x2b45758u
#define UICS_CLASS_CONTROL 0x2b45548u
#define UICS_CLASS_TRANSPORTER 0x2b45a38u /* U::ActivateTransporterRoomAction: its execute opens the transporter menu */
#define UICS_ZONE_CLASS 0x6d              /* a zone: the transporter menu's spacesuit target (IsClass, vtable +0x11e8) */
#define UICS_TRANSPORT_NAME "transporter" /* the event's connection name for a spacesuit asked through the menu */
static const uics_verdict_row uics_verdicts[]={
    {UICS_CLASS_AIRLOCK,UICS_SLOT_ONE|UICS_SLOT_OTHERS,"CycleAirlockAction"},
    {UICS_CLASS_CONTROL,UICS_SLOT_ONE,"ControlPositionAction"}};
#define UICS_VERDICTS (sizeof(uics_verdicts)/sizeof(*uics_verdicts))
typedef struct {uint64_t component;const char *connection;} uics_slot_arg;
typedef unsigned char (*uics_export_fn)(const uics_slot_arg *,uint32_t);
typedef unsigned char (*uics_perform_fn)(void *,unsigned);
static int uics_on,uics_armed;
static unsigned uics_index=UINT_MAX;
static volatile LONG uics_mode;
static DWORD uics_thread;
static uint32_t uics_seen,uics_expected;
static int uics_ran;
static volatile LONG64 uics_captured,uics_refused,uics_applied,uics_stale,uics_passed,uics_peeks,uics_faults;
static uint32_t uics_last;
static volatile LONG64 uics_seat_clicks,uics_instant,uics_menus,uics_transports;
static unsigned uics_transport_index=UINT_MAX;   /* the guard's entry of TransportPlayerToTarget, when claimed */
static int (*uics_lua_original)(void *L);         /* tests: in place of the guard's trampoline of PerformAction */
static int (*uics_zone_test)(unsigned char *);    /* tests: in place of X4's class test */
static unsigned uics_action_index=UINT_MAX;   /* the guard's entry of the Lua PerformAction, when claimed */
static unsigned char *(*uics_message_fn)(uint32_t);                                  /* ad6ec0, or a test's */
static uics_message_details *(*uics_details_fn)(uics_message_details *,uint32_t);   /* b03b70, or a test's */
static double (*uics_lua_number)(void *L,int index);                                /* lua_tonumber, set at the arming */
static uics_export_fn uics_export_test;        /* tests: in place of the guard's trampoline */
static uics_perform_fn uics_perform_test[2];   /* tests: in place of the hooks' trampolines */
static int uics_hooks_wanted(void) {return uics_on;}
static unsigned uics_verdict(unsigned slot,uint32_t klass) {
    if(!klass) return 0;
    for(unsigned i=0;i<UICS_VERDICTS;i++)
        if(uics_verdicts[i].klass==klass) return uics_verdicts[i].slots&(slot==1?UICS_SLOT_ONE:UICS_SLOT_OTHERS);
    return 0;
}
/* component is ship or inside it (a room, a cockpit, a control position: the parent chain +0x70). */
static int uics_under(const unsigned char *component,const unsigned char *ship) {
    for(unsigned depth=0;component && ship && depth<8;depth++) {
        if(component==ship) return 1;
        if(!read_local(component+0x70,&component,8)) return 0;
    }
    return 0;
}
/* Slot k's click is on a seat of its own ship while it walks: its sit-down (header, seat). */
static int uics_slot_seat(unsigned k,uint32_t klass,const unsigned char *component) {
    if(klass!=UICS_CLASS_CONTROL || k<2 || k>PILOT_SLOTS || !pfslot_own_walking()) return 0;
    return uics_under(component,pslots[k].ship);
}
/* Slot k's click is on an airlock of its own ship while it walks and may leave (header, airlock). */
static int uics_slot_airlock(unsigned k,uint32_t klass,const unsigned char *component) {
    if(klass!=UICS_CLASS_AIRLOCK || k<2 || k>PILOT_SLOTS || !pvehk_may(k)) return 0;
    return uics_under(component,pslots[k].ship);
}
/* The class of the action a container would perform for index: its vtable's RVA; 0 unreadable or none. */
static uint32_t uics_class(unsigned char *container,unsigned index,int multi) {
    unsigned char *action=NULL;void *vtable=NULL;
    if(!container) return 0;
    if(multi) {
        unsigned char **begin=NULL,**end=NULL;
        if(!read_local(container+0x10,&begin,8) || !read_local(container+0x18,&end,8) || !begin || end<begin ||
           index>=(size_t)(end-begin) || !read_local(begin+index,&action,8)) return 0;
    } else if(index || !read_local(container+0x10,&action,8)) return 0;
    if(!action || !read_local(action,&vtable,8) || !pnat_base || (unsigned char *)vtable<pnat_base ||
       (uintptr_t)((unsigned char *)vtable-pnat_base)>0xffffffffu) return 0;
    return (uint32_t)((unsigned char *)vtable-pnat_base);
}
static unsigned char uics_perform(unsigned which,unsigned char *container,unsigned index) {
    uics_perform_fn original=uics_perform_test[which]?uics_perform_test[which]:
        (uics_perform_fn)(void *)uirep_stride(which?UIREP_H_CS_MULTI:UIREP_H_CS_SINGLE);
    const LONG mode=InterlockedCompareExchange(&uics_mode,0,0);
    if(mode==UICS_PASS || GetCurrentThreadId()!=uics_thread) {InterlockedIncrement64(&uics_passed);return original(container,index);}
    const uint32_t klass=uics_class(container,index,(int)which);
    uics_seen=klass;
    if(mode==UICS_PEEK) return 0;                       /* asked, not done */
    if(!klass || klass!=uics_expected) return 0;        /* another action now: nothing on any client */
    uics_ran=1;return original(container,index);
}
static unsigned char uics_perform_single(void *container,unsigned index) {return uics_perform(0,container,index);}
static unsigned char uics_perform_multi(void *container,unsigned index) {return uics_perform(1,container,index);}
/* The export through the guard's trampoline in one mode; 0 with a fault counted. */
static int uics_call(LONG mode,uint64_t component,const char *connection,uint32_t index,uint32_t expected) {
    uics_export_fn f=uics_export_test?uics_export_test:(uics_index<UIGUARD_MAX?(uics_export_fn)uiguard_tramp[uics_index]:NULL);
    const uics_slot_arg slot={component,connection};int fault=0;
    if(!f) return 0;
    uics_thread=GetCurrentThreadId();uics_seen=0;uics_expected=expected;uics_ran=0;
    InterlockedExchange(&uics_mode,mode);
    __try {(void)f(&slot,index);}
    __except(GetExceptionCode()==EXCEPTION_ACCESS_VIOLATION?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH) {fault=1;}
    InterlockedExchange(&uics_mode,UICS_PASS);
    if(fault) InterlockedIncrement64(&uics_faults);
    return !fault;
}
/* The guard's refusal (its row and info line) where its group is armed, else refused here. */
static int uics_refuse(unsigned index,uint64_t *ret) {
    InterlockedIncrement64(&uics_refused);*ret=0;
    return index<uiguard_count && uiguard_selected(&uiguard_tab[index])?0:1;
}
/* What a peeked action of this client's slot becomes: slot k's own seat its sit-down, an allowed class the event. `ok` is
 * the caller's answer for "done" (the export's true; the Lua function's no results). */
static int uics_capture(unsigned index,uint64_t *ret,uint64_t ok,uint64_t component_id,const char *name,size_t length,unsigned action,uint32_t klass) {
    uics_last=klass;
    if(pstk_capture_slot>=2 && uics_slot_seat(pstk_capture_slot,klass,uirep_component(component_id))) {
        if(!pnat_is_live() || !pstk_record_event(0u,PFOOT_GET_UP,0u)) return uics_refuse(index,ret);
        InterlockedIncrement64(&uics_seat_clicks);*ret=ok;return 1;
    }
    if(!uics_verdict(pstk_capture_slot,klass)) return uics_refuse(index,ret);
    if(pstk_capture_slot>=2 && !uics_slot_airlock(pstk_capture_slot,klass,uirep_component(component_id))) return uics_refuse(index,ret);
    if(!length || length>UICS_NAME_MAX || action>255u) return uics_refuse(index,ret);
    unsigned char data[UIREP_REF+1+4+1+UICS_NAME_MAX];unsigned n=0;
    uirep_put_ref(data,component_id);n=UIREP_REF;
    data[n++]=(unsigned char)action;
    for(unsigned i=0;i<4;i++) data[n++]=(unsigned char)(klass>>(8*i));
    data[n++]=(unsigned char)length;memcpy(data+n,name,length);n+=(unsigned)length;
    if(!uirep_push(UICS_OP,data,n)) return uics_refuse(index,ret);
    InterlockedIncrement64(&uics_captured);*ret=ok;return 1;
}
/* A connection's name out of X4's memory: its length, 0 unreadable, empty or longer than an event carries. */
static size_t uics_name(const char *from,char name[UICS_NAME_MAX+1]) {
    size_t length=0;
    for(;from && length<=UICS_NAME_MAX;length++) {
        char c=0;
        if(!read_local(from+length,&c,1)) return 0;
        name[length]=c;
        if(!c) return length;
    }
    return 0;
}
/* Slot k stands at a transporter terminal of its own ship while it walks (header, terminal). */
static int uics_slot_terminal(unsigned k,uint32_t klass,const unsigned char *component) {
    if(klass!=UICS_CLASS_TRANSPORTER || k<2 || k>PILOT_SLOTS || !pfslot_own_walking()) return 0;
    return uics_under(component,pslots[k].ship);
}
static int uics_is_zone(unsigned char *component) {
    int zone=0;
    if(!component) return 0;
    if(uics_zone_test) return uics_zone_test(component);
    __try {zone=pfslot_is(component,UICS_ZONE_CLASS,0x11e8u);}
    __except(GetExceptionCode()==EXCEPTION_ACCESS_VIOLATION?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH) {zone=0;InterlockedIncrement64(&uics_faults);}
    return zone;
}
/* single 0, multi 1 by the container's own perform (its vtable +0x90); -1 another kind of container. */
static int uics_container_kind(unsigned char *container) {
    unsigned char *vtable=NULL,*perform=NULL;
    if(!container || !pnat_base || !read_local(container,&vtable,8) || !vtable || !read_local(vtable+UICS_CONTAINER_PERFORM,&perform,8)) return -1;
    return perform==pnat_base+UICS_SINGLE_PERFORM?0:perform==pnat_base+UICS_MULTI_PERFORM?1:-1;
}
/* The Lua global PerformAction(messageID, n) of the target system (header, instant): frame argument 0 is the lua_State. */
static int uics_action_decide(unsigned index,const uint64_t *frame,uint64_t *ret) {
    *ret=0;
    if(!uics_armed || index!=uics_action_index) return 0;
    void *L=(void *)(uintptr_t)uiguard_arg(frame,0);char name[UICS_NAME_MAX+1];
    InterlockedIncrement64(&uics_instant);
    if(!uirep_capture_now() || !pstk_capturing || !L || !uics_lua_number || !uics_message_fn || !uics_details_fn) return uics_refuse(index,ret);
    const double id=uics_lua_number(L,1),number=uics_lua_number(L,2);
    if(!(id>=1.0 && id<4294967296.0) || !(number>=1.0 && number<=256.0)) return uics_refuse(index,ret);
    unsigned char *message=NULL,*container=NULL;uics_message_details details;int fault=0;
    memset(&details,0,sizeof(details));
    __try {message=uics_message_fn((uint32_t)id);if(message) (void)uics_details_fn(&details,(uint32_t)id);}
    __except(GetExceptionCode()==EXCEPTION_ACCESS_VIOLATION?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH) {fault=1;}
    if(fault) {InterlockedIncrement64(&uics_faults);return uics_refuse(index,ret);}
    if(!message || !details.component || !read_local(message+UICS_MESSAGE_CONTAINER,&container,8) || !container) return uics_refuse(index,ret);
    const int kind=uics_container_kind(container);const unsigned action=(unsigned)number-1u;
    if(kind<0) return uics_refuse(index,ret);
    InterlockedIncrement64(&uics_peeks);
    const uint32_t klass=uics_class(container,action,kind);
    if(uics_slot_terminal(pstk_capture_slot,klass,uirep_component(details.component))) {   /* the menu: X4's own, this client's UI */
        int (*original)(void *)=uics_lua_original?uics_lua_original:(index<UIGUARD_MAX?(int(*)(void *))uiguard_tramp[index]:NULL);int results=0;
        uics_last=klass;
        if(!original) return uics_refuse(index,ret);
        __try {results=original(L);}
        __except(GetExceptionCode()==EXCEPTION_ACCESS_VIOLATION?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH) {fault=1;}
        if(fault) {InterlockedIncrement64(&uics_faults);return uics_refuse(index,ret);}
        InterlockedIncrement64(&uics_menus);*ret=(uint64_t)(int64_t)results;return 1;
    }
    return uics_capture(index,ret,0,details.component,name,uics_name(details.connection,name),action,klass);
}
/* The transporter menu's TransportPlayerToTarget(UIComponentSlot target) (header, terminal): the struct by reference. */
static int uics_transport_decide(unsigned index,const uint64_t *frame,uint64_t *ret) {
    *ret=0;
    if(!uics_armed || index!=uics_transport_index) return 0;
    uics_slot_arg target={0,NULL};const unsigned k=pstk_capture_slot;
    const unsigned char *arg=(const unsigned char *)(uintptr_t)uiguard_arg(frame,0);
    if(!uirep_capture_now() || !pstk_capturing || !arg || !read_local(arg,&target,sizeof(target)) || !target.component) return uics_refuse(index,ret);
    if(k<2 || k>PILOT_SLOTS || !pfslot_own_walking() || !pvehk_may(k) || !pslots[k].runtime || !uics_is_zone(uirep_component(target.component)))
        return uics_refuse(index,ret);
    InterlockedIncrement64(&uics_transports);
    return uics_capture(index,ret,0,pslots[k].runtime,UICS_TRANSPORT_NAME,sizeof(UICS_TRANSPORT_NAME)-1,0,UICS_CLASS_AIRLOCK);
}
static int uics_claim_decide(unsigned index,const uint64_t *frame,uint64_t *ret) {
    *ret=0;
    if(!uics_armed || index!=uics_index) return 0;
    uics_slot_arg slot={0,NULL};char name[UICS_NAME_MAX+1];size_t length=0;
    const unsigned char *arg=(const unsigned char *)(uintptr_t)uiguard_arg(frame,0);const uint64_t action=uiguard_arg(frame,1)&0xffffffffu;
    if(!uirep_capture_now() || !pstk_capturing || !arg || !read_local(arg,&slot,sizeof(slot)) || !slot.component || !slot.connection || action>255u)
        return uics_refuse(index,ret);
    for(;length<=UICS_NAME_MAX;length++) {
        char c=0;
        if(!read_local(slot.connection+length,&c,1)) return uics_refuse(index,ret);
        name[length]=c;
        if(!c) break;
    }
    if(!length || length>UICS_NAME_MAX) return uics_refuse(index,ret);
    /* The class, asked of X4 itself: the export as a peek (nothing is performed). */
    InterlockedIncrement64(&uics_peeks);
    if(!uics_call(UICS_PEEK,slot.component,name,(uint32_t)action,0)) return uics_refuse(index,ret);
    return uics_capture(index,ret,1,slot.component,name,length,(unsigned)action,uics_seen);
}
static int uics_event_ok(unsigned slot,unsigned op,const unsigned char *data,unsigned n,const char **why) {
    (void)op;
    if(!uics_armed || n<UIREP_REF+1+4+1+1u || data[UIREP_REF+5]<1 || data[UIREP_REF+5]>UICS_NAME_MAX || n!=UIREP_REF+6u+data[UIREP_REF+5]) {
        *why="ui_compslot";return 0;
    }
    uint32_t klass=0;for(unsigned i=0;i<4;i++) klass|=(uint32_t)data[UIREP_REF+1+i]<<(8*i);
    if(!uics_verdict(slot,klass)) {*why="ui_compslot_class";return 0;}   /* a class this build does not let the slot use */
    return 1;
}
/* The apply: slot 1 canonical, slot k in its scope (uirep_run_native). */
static int uics_run(unsigned slot,const unsigned char *data,const char **why) {
    uint64_t runtime=0;unsigned char *component=NULL;char name[UICS_NAME_MAX+1];uint32_t klass=0;
    const int resolved=uirep_resolve(data,&runtime,&component);
    if(resolved<0) {*why="ui_ref_mismatch";return 0;}
    if(!resolved) {InterlockedIncrement64(&uics_stale);return 1;}
    for(unsigned i=0;i<4;i++) klass|=(uint32_t)data[UIREP_REF+1+i]<<(8*i);
    const unsigned length=data[UIREP_REF+5];
    memcpy(name,data+UIREP_REF+6,length);name[length]=0;
    if(slot>=2) {   /* never X4's action for another player than its own: the slot's way out is the death module's */
        InterlockedIncrement64(uics_slot_airlock(slot,klass,component) && pvehk_ask(slot,stats.issued)?&uics_applied:&uics_stale);
        return 1;
    }
    if(!uics_call(UICS_APPLY,runtime,name,data[UIREP_REF],klass)) {*why="ui_compslot_fault";return 0;}
    InterlockedIncrement64(uics_ran?&uics_applied:&uics_stale);
    /* Slot 1's player is about to be in another vehicle: the walking pass may adopt it (lockstep_pilot_death.h). */
    if(uics_ran && slot==1 && (klass==UICS_CLASS_AIRLOCK || klass==UICS_CLASS_CONTROL)) pveh_allow(stats.issued);
    return 1;
}
/* At the arming (gate.ui_compslot, after the replay and the guard's table are there, before the first tick). */
static int uics_arm(void) {
    if(uics_armed) return 1;
    if(!uirep_armed) return uirep_refuse("compslot_needs_replay");
    uics_index=UINT_MAX;
    for(unsigned i=0;i<uiguard_count && i<UIGUARD_MAX;i++)
        if(uiguard_tab[i].kind==UIGUARD_FFI && uiguard_tab[i].name && !strcmp(uiguard_tab[i].name,"PerformCompSlotPlayerAction")) {uics_index=i;break;}
    if(uics_index==UINT_MAX) return uirep_refuse("compslot_guard_entry");
    if(!uiguard_claim(uics_index,uics_claim_decide)) {uics_index=UINT_MAX;return uirep_refuse(uiguard_claim_why);}
    /* The target system's instant action (header): with its two functions pinned; without them the guard keeps refusing it. */
    uics_action_index=UINT_MAX;
    if(pnat_base && hash_ok(pnat_base+UICS_MESSAGE,UICS_MESSAGE_SIZE,UICS_MESSAGE_SHA) && hash_ok(pnat_base+UICS_DETAILS,UICS_DETAILS_SIZE,UICS_DETAILS_SHA)) {
        uics_message_fn=(unsigned char *(*)(uint32_t))(void *)(pnat_base+UICS_MESSAGE);
        uics_details_fn=(uics_message_details *(*)(uics_message_details *,uint32_t))(void *)(pnat_base+UICS_DETAILS);
        for(unsigned i=0;i<uiguard_count && i<UIGUARD_MAX;i++)
            if(uiguard_tab[i].kind==UIGUARD_LUA && uiguard_tab[i].name && !strcmp(uiguard_tab[i].name,"PerformAction")) {uics_action_index=i;break;}
        if(uics_action_index!=UINT_MAX && !uiguard_claim(uics_action_index,uics_action_decide)) uics_action_index=UINT_MAX;
    }
    /* The transporter menu's way out (header, terminal). */
    uics_transport_index=UINT_MAX;
    for(unsigned i=0;i<uiguard_count && i<UIGUARD_MAX;i++)
        if(uiguard_tab[i].kind==UIGUARD_FFI && uiguard_tab[i].name && !strcmp(uiguard_tab[i].name,"TransportPlayerToTarget")) {uics_transport_index=i;break;}
    if(uics_transport_index!=UINT_MAX && !uiguard_claim(uics_transport_index,uics_transport_decide)) uics_transport_index=UINT_MAX;
    uics_menus=uics_transports=0;
    uics_captured=uics_refused=uics_applied=uics_stale=uics_passed=uics_peeks=uics_faults=0;uics_last=0;uics_seat_clicks=0;uics_instant=0;
    InterlockedExchange(&uics_mode,UICS_PASS);uics_on=1;uics_armed=1;return 1;
}
static int uics_status(char *out,size_t capacity) {
    int n=snprintf(out,capacity," compslot=%d:%d:%lld:%lld:%lld:%lld:%lld last=%x seat=%lld instant=%d:%lld terminal=%d:%lld:%lld",uics_on,
        uics_index<UIGUARD_MAX && uiguard_claimed[uics_index] && uiguard_claim_by[uics_index]==uics_claim_decide,(long long)uics_captured,
        (long long)uics_refused,(long long)uics_applied,(long long)uics_stale,(long long)uics_passed,uics_last,(long long)uics_seat_clicks,
        uics_action_index<UIGUARD_MAX && uiguard_claimed[uics_action_index] && uiguard_claim_by[uics_action_index]==uics_action_decide,(long long)uics_instant,
        uics_transport_index<UIGUARD_MAX && uiguard_claimed[uics_transport_index] && uiguard_claim_by[uics_transport_index]==uics_transport_decide,
        (long long)uics_menus,(long long)uics_transports);
    return n>0 && (size_t)n<capacity?n:0;
}
