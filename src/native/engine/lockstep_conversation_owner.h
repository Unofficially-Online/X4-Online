/* Conversations of the players in slots 2..N, first form (native_gate_conversation_slots; needs missions per player and
 * the conversation family). Included behind lockstep_mission_owners.h. Report: reports/LOCKSTEP-UI-MISSIONS-20261101.txt
 * section 14.
 *
 * X4 has one conversation manager per process (0x38ee230: its state at +0x1a0, 1 = idle; the current conversation at
 * +0x80), simulation state that every client holds alike, and MD's conversation cues act on "the player". So far only
 * slot 1 could talk: its menu calls were replayed and ran as X4's player on every client; slots 2..N were refused.
 *
 * Here a conversation has an owner slot:
 *   take     the apply of a replayed start (every client, the same tick) takes the conversation for its slot when the
 *            manager is idle and nobody owns one; else the start is refused, the same everywhere ("busy"). One
 *            conversation in the world at a time.
 *   may      a dialog choice, a proceed, a return or a cancel is its owner's alone: another slot's is refused at the
 *            capture on its own client and again at the apply on every client. Without an owner (a conversation MD
 *            started by itself) slot 1 keeps what it could do before.
 *   release  once per tick (the probe's frame): the manager idle again after it left idle ends the ownership; an owner
 *            whose start never left idle loses it after CVO_GRACE ticks.
 *   events   while the owner is a slot k >= 2, the dispatch of the four conversation events to a listener
 *            (U::ConversationStartedEvent, ...NextSectionEvent, ...ReturnedToSectionEvent, ...FinishedEvent: vtable slot 2)
 *            runs inside slot k's mission call scope with the generation open (lockstep_mission_owners.h mpp_call_enter,
 *            mpp_generate): the conditions of MD's conversation cues see P_k, and the instances they make are slot k's,
 *            so their later frames run in slot k's scope by the frame rule. A dispatch that is inside slot k's scope
 *            already only opens the generation; inside another scope it runs as it is (counted).
 * The start itself is the Lua core's: slot k's replayed StartConversationFromMenu signals the galaxy inside slot k's scope
 * with the generation open, and our MD cue ConversationStartSlot (native_gate_ui_replay.xml) runs start_conversation there.
 * Not here: the dialog on the owner's screen only (every client still shows it), a conversation MD starts from a frame
 * of slot k's own cues (no owner), two conversations at once.
 * Fail closed: the four vtables' type and dispatch entries are compared at the arming; a dispatch slot that is not ours
 * any more stops the probe (the status says so). */
#define CVO_EVENTS 4u
#define CVO_DISPATCH_SLOT 2u
#define CVO_GRACE 150u
#define CVO_IDLE 1
static const struct {unsigned vtable,type,dispatch;const char *name;} cvo_events[CVO_EVENTS]={
    {0x2ba7f30u,0x7c7610u,0x9c6df0u,"started"},{0x2ba7e18u,0x7c7ba0u,0x9c7040u,"next_section"},
    {0x2ba7e88u,0x5be920u,0x9c7410u,"returned"},{0x2ba7e50u,0x7c8470u,0x9c7680u,"finished"}};
typedef uint64_t (*cvo_dispatch_fn)(void *,void *,void *,double);   /* (event, listener, source, time) */
static int cvo_armed,cvo_installed;
static unsigned cvo_owner;
static uint64_t cvo_owner_tick;
static int cvo_owner_left_idle;
static cvo_dispatch_fn cvo_original[CVO_EVENTS];
static int (*cvo_state)(int *,int *);   /* the manager's state and whether it holds a conversation (tests replace it) */
static volatile LONG64 cvo_taken,cvo_busy,cvo_released,cvo_expired,cvo_dispatches,cvo_scoped,cvo_inside,cvo_unscoped,cvo_leave_failed,cvo_refused;
static const char *cvo_why="-";
static int cvo_idle(void) {
    int state=-1,current=0;
    return cvo_state && cvo_state(&state,&current) && state==CVO_IDLE && !current;
}
/* Once per tick, the probe's frame: the owner of now (0 nobody). */
static unsigned cvo_frame(uint64_t tick) {
    if(!cvo_armed || !cvo_owner) return 0;
    if(!cvo_idle()) {cvo_owner_left_idle=1;return cvo_owner;}
    if(cvo_owner_left_idle) {cvo_owner=0;cvo_owner_left_idle=0;InterlockedIncrement64(&cvo_released);}
    else if(tick>cvo_owner_tick+CVO_GRACE) {cvo_owner=0;InterlockedIncrement64(&cvo_expired);}
    return cvo_owner;
}
/* The apply of slot's replayed start: 1 the conversation is slot's, 0 busy (the same on every client). */
static int cvo_take(unsigned slot,uint64_t tick) {
    if(!cvo_armed || slot<1 || slot>PILOT_SLOTS) return 0;
    if(cvo_owner || !cvo_idle()) {InterlockedIncrement64(&cvo_busy);return 0;}
    cvo_owner=slot;cvo_owner_tick=tick;cvo_owner_left_idle=0;InterlockedIncrement64(&cvo_taken);return 1;
}
/* The conversation's owner slot (0: nobody, or not armed): whose client shows its picture (lockstep_md_media.h talk). */
static unsigned cvo_owner_now(void) {return cvo_armed?cvo_owner:0;}
/* May slot answer the conversation of now? Its owner; slot 1 also when nobody owns it (a conversation MD started). */
static int cvo_may(unsigned slot) {
    if(!cvo_armed) return slot==1;
    if(cvo_owner?cvo_owner==slot:slot==1) return 1;
    InterlockedIncrement64(&cvo_refused);return 0;
}
/* One conversation event to one listener. */
static uint64_t cvo_dispatch(unsigned i,void *event,void *listener,void *source,double time) {
    const unsigned k=cvo_armed?cvo_owner:0;int entered=0,opened=0;
    InterlockedIncrement64(&cvo_dispatches);
    if(k>=2 && GetCurrentThreadId()==ui_thread) {
        if(pstk_scope_slot==k) {
            InterlockedIncrement64(&cvo_inside);
            if(!mpp_generating) opened=mpp_generate(k);
        } else if(!pstk_scope_slot && mpp_call_enter(k)) {
            entered=1;InterlockedIncrement64(&cvo_scoped);mpp_generate(k);
        } else InterlockedIncrement64(&cvo_unscoped);
    }
    const uint64_t result=cvo_original[i](event,listener,source,time);
    if(entered) {if(!mpp_call_leave(k)) InterlockedIncrement64(&cvo_leave_failed);}
    else if(opened) mpp_generate(0);
    return result;
}
static uint64_t cvo_dispatch_0(void *e,void *l,void *s,double t) {return cvo_dispatch(0,e,l,s,t);}
static uint64_t cvo_dispatch_1(void *e,void *l,void *s,double t) {return cvo_dispatch(1,e,l,s,t);}
static uint64_t cvo_dispatch_2(void *e,void *l,void *s,double t) {return cvo_dispatch(2,e,l,s,t);}
static uint64_t cvo_dispatch_3(void *e,void *l,void *s,double t) {return cvo_dispatch(3,e,l,s,t);}
static const cvo_dispatch_fn cvo_wrappers[CVO_EVENTS]={cvo_dispatch_0,cvo_dispatch_1,cvo_dispatch_2,cvo_dispatch_3};
static void **cvo_slot(const unsigned char *at,unsigned i) {return (void**)(at+cvo_events[i].vtable+8u*CVO_DISPATCH_SLOT);}
/* The four vtables as this module believes them: type at slot 1, dispatch at slot 2 (base: where the image's pointers
 * point, the image itself in the game). */
static int cvo_pins(const unsigned char *at,uintptr_t base) {
    for(unsigned i=0;i<CVO_EVENTS;i++) {
        uintptr_t slots[3];memcpy(slots,at+cvo_events[i].vtable,sizeof(slots));
        if(slots[1]!=base+cvo_events[i].type || slots[2]!=base+cvo_events[i].dispatch) return 0;
    }
    return 1;
}
static int cvo_swap(unsigned char *at,unsigned i,void *expected,void *value) {
    void **p=cvo_slot(at,i);DWORD old,ignored;
    if(!VirtualProtect(p,8,PAGE_READWRITE,&old)) return 0;
    void *actual=InterlockedCompareExchangePointer((void*volatile*)p,value,expected);
    VirtualProtect(p,8,old,&ignored);return actual==expected;
}
static int cvo_intact_at(const unsigned char *at) {
    for(unsigned i=0;i<CVO_EVENTS;i++) if(*cvo_slot(at,i)!=(void*)cvo_wrappers[i]) return 0;
    return 1;
}
static void cvo_restore_at(unsigned char *at) {
    if(!cvo_installed) return;
    for(unsigned i=CVO_EVENTS;i-->0;) cvo_swap(at,i,(void*)cvo_wrappers[i],(void*)cvo_original[i]);
    cvo_installed=0;cvo_owner=0;
}
static int cvo_install_at(unsigned char *at) {
    for(unsigned i=0;i<CVO_EVENTS;i++) {
        cvo_original[i]=(cvo_dispatch_fn)*cvo_slot(at,i);
        if(!cvo_swap(at,i,(void*)cvo_original[i],(void*)cvo_wrappers[i])) {
            while(i-->0) cvo_swap(at,i,(void*)cvo_wrappers[i],(void*)cvo_original[i]);
            return 0;
        }
    }
    cvo_installed=1;return 1;
}
/* ---- the owner's screen only (19 Nov) ----
 * A conversation's dialog and the menus it opens are UI: they showed on every client (crew run of 19 Nov: player 1's
 * hire map stayed open on player 2's screen). They are held back on a client whose slot is not the owner's; the
 * conversation itself (the manager, MD, the events) runs on every client as before.
 *   dialog  the conversation manager's StartDialogEvent reaches the dialog menu through the listener the menu registers
 *           (NotifyOnStartDialog 0xb0ff30: UI::XAnark::AnarkStartDialogEventInterface, vtable 0x2bd2cd0); the event's
 *           dispatch 0x974220 calls the listener's slot 322 (+0xa10) = 0xaf5270, which raises the Lua event
 *           'startDialog' and nothing else. That slot is ours: on a foreign client it returns at once. (StopDialogEvent
 *           goes on to every client: closing a dialog that is not shown does nothing.)
 *   menu    open_conversation_menu (Scripts::OpenMenuAction 0xbf4320, the branch with a conversation) asks the UI with
 *           [[0x38f52b0]]->slot 59 (+0x1d8, call at 0xbf4569) and then marks the manager (+0x1a5 = 1, simulation state).
 *           Slot 59 of that object's vtable is ours: called from that site (return address 0xbf456f) on a foreign client
 *           it returns at once; the mark is made on every client. Every other caller of the slot goes through as it is.
 * Without an owner (a conversation MD started) every client shows it, as before. The menu interface is found at the
 * arming; none there: the menus show everywhere (status menu=absent), the dialog is still the owner's. */
#define CVO_DIALOG_VTABLE 0x2bd2cd0u
#define CVO_DIALOG_SLOT 322u
#define CVO_DIALOG_FN 0xaf5270u
#define CVO_MENU_HOLDER 0x38f52b0u
#define CVO_MENU_SLOT 59u
#define CVO_MENU_CALL 0xbf4569u
#define CVO_MENU_RET 0xbf456fu
#define CVO_IMAGE_SIZE 0x6fa9000u
typedef void (*cvo_dialog_fn)(void *,void *,double);
typedef uint64_t (*cvo_menu_fn)(void *,void *,void *,void *);
static cvo_dialog_fn cvo_dialog_original;
static cvo_menu_fn cvo_menu_original;
static void **cvo_menu_slot_at;
static int cvo_dialog_installed,cvo_menu_installed;
static uintptr_t cvo_menu_vtable_rva;
static volatile LONG64 cvo_dialogs_shown,cvo_dialogs_hidden,cvo_menus_shown,cvo_menus_hidden;
/* This client shows nothing of the conversation of now: it has an owner, and the owner is another slot. */
static int cvo_foreign(void) {return cvo_armed && cvo_owner && cvo_owner!=stats.slot;}
/* The menu slot's verdict for one call: 1 hold it back (called from the conversation branch, a foreign conversation). */
static int cvo_menu_hide(uintptr_t return_address) {return return_address==(uintptr_t)image+CVO_MENU_RET && cvo_foreign();}
static void cvo_dialog_start(void *listener,void *source,double time) {
    if(cvo_foreign()) {InterlockedIncrement64(&cvo_dialogs_hidden);return;}
    InterlockedIncrement64(&cvo_dialogs_shown);cvo_dialog_original(listener,source,time);
}
static uint64_t cvo_menu_open(void *ui,void *name,void *c,void *d) {
    const uintptr_t from=(uintptr_t)__builtin_return_address(0);
    if(cvo_menu_hide(from)) {InterlockedIncrement64(&cvo_menus_hidden);return 0;}
    if(from==(uintptr_t)image+CVO_MENU_RET) InterlockedIncrement64(&cvo_menus_shown);
    return cvo_menu_original(ui,name,c,d);
}
static int cvo_slot_swap(void **p,void *expected,void *value) {
    DWORD old,ignored;
    if(!VirtualProtect(p,8,PAGE_READWRITE,&old)) return 0;
    void *actual=InterlockedCompareExchangePointer((void*volatile*)p,value,expected);
    VirtualProtect(p,8,old,&ignored);return actual==expected;
}
/* The call site of the conversation branch as this module believes it: mov rcx,[rip->0x38f52b0] at 0xbf454f and
 * call [rax+0x1d8] at 0xbf4569; the dialog listener's slot (base: where the image's pointers point). */
static int cvo_screen_pins(const unsigned char *at,uintptr_t base) {
    static const unsigned char call[6]={0xff,0x90,0xd8,0x01,0x00,0x00};
    int32_t rel;memcpy(&rel,at+0xbf4552u,4);
    uintptr_t slot;memcpy(&slot,at+CVO_DIALOG_VTABLE+8u*CVO_DIALOG_SLOT,8);
    return !memcmp(at+CVO_MENU_CALL,call,6) && at[0xbf454fu]==0x48 && at[0xbf4550u]==0x8b && at[0xbf4551u]==0x0d &&
           (uint32_t)(0xbf4556u+rel)==CVO_MENU_HOLDER && slot==base+CVO_DIALOG_FN;
}
/* The menu interface of now: [[holder]] and its vtable inside the image, or NULL. */
static void **cvo_menu_slot_find(void) {
    void *holder=NULL,*ui=NULL,*vtable=NULL;
    if(!read_local(image+CVO_MENU_HOLDER,&holder,8) || !holder || !read_local(holder,&ui,8) || !ui || !read_local(ui,&vtable,8)) return NULL;
    if((uintptr_t)vtable<(uintptr_t)image || (uintptr_t)vtable>=(uintptr_t)image+CVO_IMAGE_SIZE) return NULL;
    void *fn=NULL;void **slot=(void**)vtable+CVO_MENU_SLOT;
    if(!read_local(slot,&fn,8) || (uintptr_t)fn<(uintptr_t)image || (uintptr_t)fn>=(uintptr_t)image+CVO_IMAGE_SIZE) return NULL;
    cvo_menu_vtable_rva=(uintptr_t)vtable-(uintptr_t)image;return slot;
}
static int cvo_screen_install(void) {
    void **dialog=(void**)(image+CVO_DIALOG_VTABLE+8u*CVO_DIALOG_SLOT);
    cvo_dialog_original=(cvo_dialog_fn)*dialog;
    if(!cvo_slot_swap(dialog,(void*)cvo_dialog_original,(void*)cvo_dialog_start)) return 0;
    cvo_dialog_installed=1;
    void **menu=cvo_menu_slot_find();
    if(menu) {
        cvo_menu_original=(cvo_menu_fn)*menu;
        if(cvo_slot_swap(menu,(void*)cvo_menu_original,(void*)cvo_menu_open)) {cvo_menu_slot_at=menu;cvo_menu_installed=1;}
    }
    return 1;
}
static void cvo_screen_restore(void) {
    if(cvo_menu_installed && cvo_slot_swap(cvo_menu_slot_at,(void*)cvo_menu_open,(void*)cvo_menu_original)) cvo_menu_installed=0;
    if(cvo_dialog_installed && cvo_slot_swap((void**)(image+CVO_DIALOG_VTABLE+8u*CVO_DIALOG_SLOT),(void*)cvo_dialog_start,(void*)cvo_dialog_original)) cvo_dialog_installed=0;
}
static int cvo_screen_intact(void) {
    return (!cvo_dialog_installed || *(void**)(image+CVO_DIALOG_VTABLE+8u*CVO_DIALOG_SLOT)==(void*)cvo_dialog_start) &&
           (!cvo_menu_installed || *cvo_menu_slot_at==(void*)cvo_menu_open);
}
static void cvo_restore(void) {cvo_screen_restore();cvo_restore_at(image);cvo_armed=0;}
static int uimis_conv_state(int *state,int *current);
static int cvo_arm(void) {
    if(cvo_armed) return 1;
    if(stats.issued) {cvo_why="late";return 0;}   /* before the first tick, as every module the probe arms */
    if(!mpp_armed) {cvo_why="needs_missions_per_player";return 0;}
    if(!(uirep_families&UIREP_FAMILY_CONVERSATION)) {cvo_why="needs_conversation_family";return 0;}
    if(!cvo_pins(image,(uintptr_t)image)) {cvo_why="pins";return 0;}
    if(!cvo_screen_pins(image,(uintptr_t)image)) {cvo_why="screen_pins";return 0;}
    if(!cvo_install_at(image)) {cvo_why="install";return 0;}
    if(!cvo_screen_install()) {cvo_restore_at(image);cvo_why="screen_install";return 0;}
    cvo_state=uimis_conv_state;cvo_owner=0;cvo_owner_tick=0;cvo_owner_left_idle=0;
    cvo_taken=cvo_busy=cvo_released=cvo_expired=cvo_dispatches=cvo_scoped=cvo_inside=cvo_unscoped=cvo_leave_failed=cvo_refused=0;
    cvo_dialogs_shown=cvo_dialogs_hidden=cvo_menus_shown=cvo_menus_hidden=0;
    cvo_why="-";cvo_armed=1;return 1;
}
/* 1 while the four dispatch slots are ours (asked once per tick with the frame). */
static int cvo_check(void) {
    if(!cvo_armed) return 1;
    if(cvo_installed && cvo_intact_at(image) && cvo_screen_intact()) return 1;
    cvo_why="slot_changed";return 0;
}
/* Hot join: "v1|owner:tick:left_idle". */
static int cvo_join_state(char *out,size_t cap) {
    int n=snprintf(out,cap,"v1|%u:%llu:%d",cvo_owner,(unsigned long long)cvo_owner_tick,cvo_owner_left_idle);
    return n>0 && (size_t)n<cap?n:0;
}
static int cvo_join_restore(const char *text,size_t length) {
    char copy[64];unsigned owner=0,left=0;unsigned long long tick=0;char end=0;
    if(!cvo_armed || length<4 || length>=sizeof(copy)) return 0;
    memcpy(copy,text,length);copy[length]=0;
    if(sscanf(copy,"v1|%u:%llu:%u%c",&owner,&tick,&left,&end)!=3 || owner>PILOT_SLOTS || left>1) return 0;
    cvo_owner=owner;cvo_owner_tick=tick;cvo_owner_left_idle=(int)left;return 1;
}
static int cvo_status(char *out,size_t cap) {
    int state=-1,current=0;if(cvo_state) cvo_state(&state,&current);
    int n=snprintf(out,cap,"armed=%d why=%s owner=%u since=%llu left_idle=%d manager=%d.%d taken=%lld busy=%lld released=%lld expired=%lld refused=%lld "
        "dispatches=%lld scoped=%lld inside=%lld unscoped=%lld leave_failed=%lld screen=%d:%s dialogs=%lld/%lld menus=%lld/%lld menu_vtable=%llx",
        cvo_armed,cvo_why,cvo_owner,(unsigned long long)cvo_owner_tick,cvo_owner_left_idle,
        state,current,(long long)cvo_taken,(long long)cvo_busy,(long long)cvo_released,(long long)cvo_expired,(long long)cvo_refused,
        (long long)cvo_dispatches,(long long)cvo_scoped,(long long)cvo_inside,(long long)cvo_unscoped,(long long)cvo_leave_failed,
        cvo_dialog_installed,cvo_menu_installed?"menu":"menu_absent",(long long)cvo_dialogs_shown,(long long)cvo_dialogs_hidden,
        (long long)cvo_menus_shown,(long long)cvo_menus_hidden,(unsigned long long)cvo_menu_vtable_rva);
    return n>0 && (size_t)n<cap?n:0;
}
