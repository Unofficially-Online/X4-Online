/* UI guard, DEFAULT OFF (native_gate_ui_guard; needs native_gate_ui_replay armed, stop reason 164 like the replay).
 * Containment for what the UI replay does not take yet: every world-changing stock UI entry point of the inventory
 * (tools/dev/ui_guard_inventory.py, generated into lockstep_ui_guard_table.h against the pinned X4.exe: FFI exports and
 * the Lua C functions the engine registers as globals, P0 left-overs to P3) is refused while a person's UI runs it on
 * one client, unless an armed replay family captures it below the entry. Report: reports/LOCKSTEP-UI-GUARD-20261101.txt.
 *
 *   when      in the local phases only, the replay's own rule (uirep_local_phase: slot 6, render slot 10, the slot 4
 *             dispatch of presentation ids, an open presentation scope), and not in the probe's own frame
 *             (rid_probe_frame, gate.runtime_ids_probe), nor while the replay applies (uirep_replaying). Simulation code
 *             (MD, AI, jobs) calling the same functions passes untouched. A call with its check-only flag set is a query
 *             and passes.
 *   refusal   the function does not run: it returns zero (false, 0, NULL, 0.0), an empty string for const char*, a
 *             zeroed struct with empty strings for a struct returned through the hidden pointer, no Lua results for a
 *             Lua C function. Counted per entry (status), each refusal queued for the probe's log (UI_GUARD_REFUSED) and
 *             its info line.
 *   hook      a five-byte jump at the entry (one CAS in its 16-byte block) to a stub that loads the entry's index and
 *             jumps to one shared routine: it saves the argument registers and xmm0..3, asks uiguard_decide, restores
 *             them and either jumps to the entry's trampoline (the prefix copied with its RIP-relative displacements moved
 *             and a conditional branch pointed at an absolute jump to its target, then a jump behind the prefix; a tail
 *             jump goes to its target) or returns the refusal value. Nothing of ours is on the stack while X4's
 *             function runs.
 *   named     a row of AddUITriggeredEvent names its screen/control, one of SignalObject its signal (read from the Lua
 *             call's stack the way the stock function reads them: lua_tolstring of arguments 1 and 2, resp. 2).
 *   UI-only   AddUITriggeredEvent of a (screen, control) in uiguard_ui_only runs where it was made: no MD cue of the base game
 *             or a DLC listens for it (tools/dev/ui_guard_inventory.py UI_ONLY_EVENTS); counted (allowed=) and logged.
 *   lifetime  armed after the replay (gate.ui_guard), pins of every selected entry checked first (prefix bytes and a
 *             SHA-256 of the function's first bytes; any mismatch refuses the arming), installed with the replay at its
 *             first pass, intact checked every pass (a lost patch stops with 164), removed behind the window. */
enum {UIGUARD_FFI=0,UIGUARD_LUA=1};
enum {UIGUARD_G_FLIGHT=0,UIGUARD_G_ORDERS,UIGUARD_G_FLEET,UIGUARD_G_TRADE,UIGUARD_G_FACTION,UIGUARD_G_ASSETS,UIGUARD_G_BUILD,
    UIGUARD_G_CONFLICT,UIGUARD_G_MISSIONS,UIGUARD_G_EVENTS,UIGUARD_G_CONVERSATION,UIGUARD_G_SESSION,UIGUARD_G_CHEAT,UIGUARD_G_EDITOR,UIGUARD_GROUPS};
#define UIGUARD_ALL_GROUPS ((1u<<UIGUARD_GROUPS)-1u)
static const char *const uiguard_group_names[UIGUARD_GROUPS]={"flight","orders","fleet","trade","faction","assets","build","conflict",
    "missions","events","conversation","session","cheat","editor"};
enum {UIGUARD_RET_ZERO=0,UIGUARD_RET_STRING=1,UIGUARD_RET_STRUCT=2};
typedef struct {
    const char *name;unsigned rva;unsigned char kind,group;unsigned short covered;
    unsigned char ret,size,strings;signed char checkonly;     /* refusal value; the check-only argument, -1 none */
    unsigned char prefix;unsigned tail;unsigned char bytes[16]; /* the entry's first bytes; a tail jump's target */
    unsigned char rip[2];unsigned char jcc_at,jcc_len;unsigned jcc_target;   /* how the prefix moves */
    unsigned pin;const char *sha;
} uiguard_entry;
#include "lockstep_ui_guard_table.h"
#define UIGUARD_MAX 320u
#define UIGUARD_STRIDE 0x40u
#define UIGUARD_STUB 0x30u          /* in a stride: mov r10d,index; jmp shared */
#define UIGUARD_SHARED 0x100u       /* the shared routine at the start of the region */
#define UIGUARD_RING 64u
/* Rows name what a person's events call asked (the Lua C functions AddUITriggeredEvent and SignalObject). */
enum {UIGUARD_ARGS_NONE=0,UIGUARD_ARGS_UI_EVENT=1,UIGUARD_ARGS_SIGNAL=2};
#define UIGUARD_ARG_TEXT 20u
#define UIGUARD_ARGS_TEXT (2u*UIGUARD_ARG_TEXT+2u)
_Static_assert(UIGUARD_ENTRIES<=UIGUARD_MAX,"ui guard table larger than its arrays");
static const uiguard_entry *uiguard_tab=uiguard_table;
static unsigned uiguard_count=UIGUARD_ENTRIES;
static int uiguard_installed,uiguard_restored=1,uiguard_live,uiguard_pins_checked;
static unsigned uiguard_groups;
static const char *uiguard_stage="off",*uiguard_why_text="-",*uiguard_arm_why="-";
static unsigned char *uiguard_region;static size_t uiguard_region_size;
static void *uiguard_tramp[UIGUARD_MAX];
static unsigned char uiguard_patched[UIGUARD_MAX][5],uiguard_on[UIGUARD_MAX];
static volatile LONG64 uiguard_refused_by[UIGUARD_MAX],uiguard_passed,uiguard_queries,uiguard_refused,uiguard_ring_lost;
static volatile uint64_t uiguard_ret;
static const char uiguard_empty[1]="";
typedef struct {uint64_t tick;unsigned index;unsigned char ui_only;char args[UIGUARD_ARGS_TEXT];} uiguard_refusal;
static uiguard_refusal uiguard_ring[UIGUARD_RING];
static unsigned uiguard_ring_head,uiguard_ring_tail;

static int uiguard_selected(const uiguard_entry *e) {return (uiguard_groups>>e->group)&1u;}
/* Claims: replay families that capture in this hook (lockstep_ui_build.h: build, assets; lockstep_ui_missions.h:
 * missions, conversation; docs/LOCKSTEP-REGISTRY.txt section 3). Dispatch by entry: each entry holds at most one family's
 * handler. A claimed entry is installed whatever the groups say; in a local phase its handler answers first (1: handled,
 * its value returned), otherwise the guard's own rule. A family claims at its arming, before the install, and never an
 * entry another family holds (claim_taken) or one an armed family covers with its own hook (claim_covered: two patches
 * at one entry). */
typedef int (*uiguard_claim_handler)(unsigned index,const uint64_t *frame,uint64_t *ret);
static unsigned char uiguard_claimed[UIGUARD_MAX];
static uiguard_claim_handler uiguard_claim_by[UIGUARD_MAX];
static const char *uiguard_claim_why="-";
/* 1: entry index is fn's (now or already); 0 and uiguard_claim_why: refused. */
static int uiguard_claim(unsigned index,uiguard_claim_handler fn) {
    if(!fn || index>=uiguard_count || index>=UIGUARD_MAX) {uiguard_claim_why="claim_index";return 0;}
    if(uiguard_claim_by[index] && uiguard_claim_by[index]!=fn) {uiguard_claim_why="claim_taken";return 0;}
    if(uirep_families&uiguard_tab[index].covered) {uiguard_claim_why="claim_covered";return 0;}
    uiguard_claim_by[index]=fn;uiguard_claimed[index]=1;return 1;
}
/* Every entry fn holds back to the guard (NULL: every claim). */
static void uiguard_unclaim(uiguard_claim_handler fn) {
    for(unsigned i=0;i<UIGUARD_MAX;i++) if(!fn || uiguard_claim_by[i]==fn) {uiguard_claim_by[i]=NULL;uiguard_claimed[i]=0;}
}
/* Installed: selected, and no armed replay family captures it below the entry (those hooks share some entries); or claimed. */
static int uiguard_wanted(const uiguard_entry *e) {
    unsigned i=(unsigned)(e-uiguard_tab);
    return (uiguard_selected(e) && !(uirep_families&e->covered)) || (i<UIGUARD_MAX && uiguard_claimed[i]);
}
static int uiguard_pins(void) {
    if(!uiguard_pins_checked) {
        int ok=1;
        for(unsigned i=0;ok && i<uiguard_count;i++) {
            const uiguard_entry *e=&uiguard_tab[i];
            if(!(ok=e->prefix>=5 && e->prefix<=16 && !memcmp(image+e->rva,e->bytes,e->prefix) && hash_ok(image+e->rva,e->pin,e->sha))) pin_failed_rva=e->rva;
        }
        uiguard_pins_checked=ok?1:-1;
    }
    return uiguard_pins_checked==1;
}
static uint64_t uiguard_arg(const uint64_t *frame,unsigned i);
/* UI-only AddUITriggeredEvent calls (tools/dev/ui_guard_inventory.py UI_ONLY_EVENTS, native_gate_ui_replay_core.lua
 * K.UI_ONLY): screen, control. TopLevelMenu '' is Helper's show of the HUD's top level menu: every client at the first game
 * plan change after a load (menu_toplevel.lua), after an undock, and whenever a menu closes in flight (helper.lua closeMenu). */
static const struct {const char *screen,*control;} uiguard_ui_only[]={{"TopLevelMenu",""}};
#define UIGUARD_UI_ONLY (sizeof(uiguard_ui_only)/sizeof(*uiguard_ui_only))
static const char *(*uiguard_lua_string)(void *L,int index,size_t *length);   /* lua_tolstring, set at the arming */
static unsigned char uiguard_args_kind[UIGUARD_MAX];
static volatile LONG64 uiguard_allowed;
static void uiguard_kinds(void) {
    for(unsigned i=0;i<uiguard_count && i<UIGUARD_MAX;i++) {
        const uiguard_entry *e=&uiguard_tab[i];
        uiguard_args_kind[i]=e->kind!=UIGUARD_LUA?UIGUARD_ARGS_NONE:!strcmp(e->name,"AddUITriggeredEvent")?UIGUARD_ARGS_UI_EVENT:
            !strcmp(e->name,"SignalObject")?UIGUARD_ARGS_SIGNAL:UIGUARD_ARGS_NONE;
    }
}
/* A Lua argument as the rows show it: letters, digits and _ . - as they are, anything else '?', UIGUARD_ARG_TEXT at most. */
static void uiguard_arg_text(const char *s,size_t length,char *out) {
    unsigned n=0;
    for(size_t i=0;s && i<length && n<UIGUARD_ARG_TEXT;i++) {
        unsigned char c=(unsigned char)s[i];
        out[n++]=(char)((c>='a' && c<='z') || (c>='A' && c<='Z') || (c>='0' && c<='9') || c=='_' || c=='.' || c=='-'?c:'?');
    }
    out[n]=0;
}
/* The call's arguments into out ("screen/control" or "signal"); 1 when it is a UI-only AddUITriggeredEvent. */
static int uiguard_args_of(unsigned kind,void *L,char out[UIGUARD_ARGS_TEXT]) {
    size_t n1=0,n2=0;const char *a=NULL,*b=NULL;char ta[UIGUARD_ARG_TEXT+1],tb[UIGUARD_ARG_TEXT+1];
    out[0]='-';out[1]=0;
    if(!kind || !L || !uiguard_lua_string) return 0;
    if(kind==UIGUARD_ARGS_UI_EVENT) a=uiguard_lua_string(L,1,&n1);
    b=uiguard_lua_string(L,2,&n2);
    uiguard_arg_text(a,a?n1:0,ta);uiguard_arg_text(b,b?n2:0,tb);
    if(kind==UIGUARD_ARGS_UI_EVENT) snprintf(out,UIGUARD_ARGS_TEXT,"%s/%s",ta,tb);else snprintf(out,UIGUARD_ARGS_TEXT,"%s",tb);
    if(kind!=UIGUARD_ARGS_UI_EVENT || !a || !b) return 0;
    for(unsigned i=0;i<UIGUARD_UI_ONLY;i++)
        if(strlen(uiguard_ui_only[i].screen)==n1 && !memcmp(a,uiguard_ui_only[i].screen,n1) && strlen(uiguard_ui_only[i].control)==n2 &&
           !memcmp(b,uiguard_ui_only[i].control,n2)) return 1;
    return 0;
}
/* Argument i of the hooked call as the shared routine saved it: frame = r10, r9, r8, rdx, rcx, return address, home
 * space, then the stack arguments. */
static uint64_t uiguard_arg(const uint64_t *frame,unsigned i) {
    static const unsigned slot[4]={4,3,2,1};
    return i<4?frame[slot[i]]:frame[6+i];
}
/* The shared routine's question, any thread: 0 = run X4's function, 1 = refused (uiguard_ret is the answer). */
static int uiguard_decide(uint64_t index,const uint64_t *frame) {
    if(index>=uiguard_count) return 0;
    const uiguard_entry *e=&uiguard_tab[index];
    if(!uiguard_live || uirep_stopped || InterlockedCompareExchange(&uirep_replaying,0,0) || InterlockedCompareExchange(&rid_probe_frame,0,0) ||
       !uirep_local_phase()) {InterlockedIncrement64(&uiguard_passed);return 0;}
    if(e->checkonly>=0 && (uiguard_arg(frame,(unsigned)e->checkonly)&0xffu)) {InterlockedIncrement64(&uiguard_queries);return 0;}
    {uiguard_claim_handler claim=index<UIGUARD_MAX && uiguard_claimed[index]?uiguard_claim_by[index]:NULL;uint64_t v=0;
     if(claim && claim((unsigned)index,frame,&v)) {uiguard_ret=v;return 1;}}
    if(!uiguard_selected(e)) {InterlockedIncrement64(&uiguard_passed);return 0;}   /* installed for a claim only */
    char args[UIGUARD_ARGS_TEXT];
    int ui_only=uiguard_args_of(uiguard_args_kind[index],(void*)(uintptr_t)uiguard_arg(frame,0),args);
    if(ui_only) {   /* runs here, as X4's own; one row */
        InterlockedIncrement64(&uiguard_allowed);
        if(uiguard_ring_head-uiguard_ring_tail<UIGUARD_RING) {
            uiguard_refusal *r=&uiguard_ring[uiguard_ring_head%UIGUARD_RING];r->tick=stats.issued;r->index=(unsigned)index;r->ui_only=1;
            memcpy(r->args,args,sizeof(args));++uiguard_ring_head;
        } else InterlockedIncrement64(&uiguard_ring_lost);
        return 0;
    }
    uint64_t value=0;
    if(e->ret==UIGUARD_RET_STRING) value=(uint64_t)(uintptr_t)uiguard_empty;
    else if(e->ret==UIGUARD_RET_STRUCT) {
        unsigned char *out=(unsigned char*)(uintptr_t)uiguard_arg(frame,0);
        if(out) {
            memset(out,0,e->size);
            for(unsigned s=0;s<8;s++) if((e->strings>>s)&1u) {const char *empty=uiguard_empty;memcpy(out+8*s,&empty,8);}
        }
        value=(uint64_t)(uintptr_t)out;
    }
    uiguard_ret=value;
    InterlockedIncrement64(&uiguard_refused);InterlockedIncrement64(&uiguard_refused_by[index]);
    if(uiguard_ring_head-uiguard_ring_tail<UIGUARD_RING) {
        uiguard_refusal *r=&uiguard_ring[uiguard_ring_head%UIGUARD_RING];r->tick=stats.issued;r->index=(unsigned)index;r->ui_only=0;
        memcpy(r->args,args,sizeof(args));++uiguard_ring_head;
    } else InterlockedIncrement64(&uiguard_ring_lost);
    return 1;
}

/* ---- code ---- */
/* push rcx,rdx,r8,r9,r10; sub rsp,0x60; xmm0..3 to [rsp+0x20..]; rcx = r10 (index), rdx = the saved block; call decide;
 * restore; nonzero: rax = xmm0 = uiguard_ret, ret; zero: jmp [uiguard_tramp + r10*8]. rsp is 16-aligned at the call. */
static unsigned uiguard_write_shared(unsigned char *p) {
    static const unsigned char save[]={0x51,0x52,0x41,0x50,0x41,0x51,0x41,0x52,0x48,0x83,0xec,0x60,
        0xf3,0x0f,0x7f,0x44,0x24,0x20,0xf3,0x0f,0x7f,0x4c,0x24,0x30,0xf3,0x0f,0x7f,0x54,0x24,0x40,0xf3,0x0f,0x7f,0x5c,0x24,0x50,
        0x4c,0x89,0xd1,0x48,0x8d,0x54,0x24,0x60};
    static const unsigned char restore[]={0xff,0xd0,0x41,0x89,0xc3,
        0xf3,0x0f,0x6f,0x44,0x24,0x20,0xf3,0x0f,0x6f,0x4c,0x24,0x30,0xf3,0x0f,0x6f,0x54,0x24,0x40,0xf3,0x0f,0x6f,0x5c,0x24,0x50,
        0x48,0x83,0xc4,0x60,0x41,0x5a,0x41,0x59,0x41,0x58,0x5a,0x59,0x45,0x85,0xdb,0x75,0x0e,0x49,0xbb};
    static const unsigned char jump[]={0x43,0xff,0x24,0xd3};
    static const unsigned char refuse[]={0x48,0x8b,0x00,0x66,0x48,0x0f,0x6e,0xc0,0xc3};
    unsigned n=0;void *table=uiguard_tramp,*ret=(void*)&uiguard_ret;
    memcpy(p+n,save,sizeof(save));n+=sizeof(save);
    n+=pview_mov_rax(p+n,(void*)uiguard_decide);
    memcpy(p+n,restore,sizeof(restore));n+=sizeof(restore);memcpy(p+n,&table,8);n+=8;
    memcpy(p+n,jump,sizeof(jump));n+=sizeof(jump);
    n+=pview_mov_rax(p+n,ret);memcpy(p+n,refuse,sizeof(refuse));n+=sizeof(refuse);
    return n;
}
static unsigned char *uiguard_stride(unsigned i) {return uiguard_region+UIGUARD_SHARED+UIGUARD_STRIDE*i;}
/* The trampoline (the prefix, moved) and the stub of entry i. 0: a displacement does not reach from the copy. */
static int uiguard_write_entry(unsigned i,unsigned char *p) {
    const uiguard_entry *e=&uiguard_tab[i];
    memset(p,0xcc,UIGUARD_STRIDE);
    if(e->tail) pview_abs_jump(p,pnat_base+e->tail);
    else {
        memcpy(p,e->bytes,e->prefix);
        for(unsigned r=0;r<2;r++) {
            if(!e->rip[r]) continue;
            if(e->rip[r]+4u>e->prefix) return 0;
            int32_t old=0;memcpy(&old,p+e->rip[r],4);
            int64_t moved=(int64_t)old+((int64_t)(intptr_t)(pnat_base+e->rva)-(int64_t)(intptr_t)p);
            if(moved<INT32_MIN || moved>INT32_MAX) return 0;
            int32_t value=(int32_t)moved;memcpy(p+e->rip[r],&value,4);
        }
        pview_abs_jump(p+e->prefix,pnat_base+e->rva+e->prefix);
        if(e->jcc_len) {   /* the branch goes to an absolute jump to its target, behind the jump back */
            unsigned target=e->prefix+14u;
            if(e->jcc_at+e->jcc_len>e->prefix || (e->jcc_len!=2 && e->jcc_len!=6)) return 0;
            if(e->jcc_len==2) p[e->jcc_at+1]=(unsigned char)(int8_t)(target-(e->jcc_at+2u));
            else {int32_t rel=(int32_t)(target-(e->jcc_at+6u));memcpy(p+e->jcc_at+2,&rel,4);}
            pview_abs_jump(p+target,pnat_base+e->jcc_target);
        }
    }
    unsigned char *stub=p+UIGUARD_STUB;uint32_t index=i;int32_t rel=(int32_t)((intptr_t)uiguard_region-(intptr_t)(stub+11));
    stub[0]=0x41;stub[1]=0xba;memcpy(stub+2,&index,4);stub[6]=0xe9;memcpy(stub+7,&rel,4);
    return 1;
}
static size_t uiguard_size_for(unsigned count) {return (UIGUARD_SHARED+(size_t)count*UIGUARD_STRIDE+0xfffu)&~(size_t)0xfffu;}
/* Near the image: every entry reaches its stub with a rel32 jump. */
static unsigned char *uiguard_alloc(size_t size) {
    unsigned lo=0xffffffffu,hi=0;
    for(unsigned i=0;i<uiguard_count;i++) {if(uiguard_tab[i].rva<lo) lo=uiguard_tab[i].rva;if(uiguard_tab[i].rva>hi) hi=uiguard_tab[i].rva;}
    if(!uiguard_count) return NULL;
    uintptr_t at=(uintptr_t)(pnat_base+0x800000u)&~(uintptr_t)65535;
    for(uintptr_t distance=65536;distance<0x70000000;distance+=65536)
        for(int sign=-1;sign<=1;sign+=2) {
            unsigned char *p=VirtualAlloc((void*)(sign<0?at-distance:at+distance),size,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
            if(!p) continue;
            intptr_t a=(intptr_t)p-(intptr_t)(pnat_base+lo),b=(intptr_t)(p+size)-(intptr_t)(pnat_base+hi);
            if(a>INT32_MIN+0x10000 && a<INT32_MAX-0x10000 && b>INT32_MIN+0x10000 && b<INT32_MAX-0x10000) return p;
            VirtualFree(p,0,MEM_RELEASE);
        }
    return NULL;
}
static void uiguard_uninstall(void) {
    for(unsigned i=uiguard_count;i-->0;) {
        if(!uiguard_on[i] || memcmp(pnat_base+uiguard_tab[i].rva,uiguard_patched[i],5)) continue;
        if(pview_cas(uiguard_tab[i].rva,uiguard_patched[i],uiguard_tab[i].bytes,5)) uiguard_on[i]=0;
    }
    int clean=1;
    for(unsigned i=0;i<uiguard_count;i++) if(uiguard_on[i] || memcmp(pnat_base+uiguard_tab[i].rva,uiguard_tab[i].bytes,5)) {if(uiguard_wanted(&uiguard_tab[i])) clean=0;}
    uiguard_restored=clean;uiguard_live=0;
    if(clean) uiguard_installed=0;
}
static int uiguard_install(void) {
    if(uiguard_installed) return 1;
    for(unsigned i=0;i<uiguard_count;i++) if(uiguard_wanted(&uiguard_tab[i]) && memcmp(pnat_base+uiguard_tab[i].rva,uiguard_tab[i].bytes,uiguard_tab[i].prefix)) return 0;
    size_t size=uiguard_size_for(uiguard_count);
    if(!uiguard_region) {if(!(uiguard_region=uiguard_alloc(size))) return 0;uiguard_region_size=size;}
    DWORD old,ignored;
    if(!VirtualProtect(uiguard_region,uiguard_region_size,PAGE_READWRITE,&old)) return 0;
    memset(uiguard_region,0xcc,uiguard_region_size);
    unsigned shared=uiguard_write_shared(uiguard_region);int ok=shared<=UIGUARD_SHARED;
    for(unsigned i=0;ok && i<uiguard_count;i++) ok=uiguard_write_entry(i,uiguard_stride(i));
    if(!VirtualProtect(uiguard_region,uiguard_region_size,PAGE_EXECUTE_READ,&ignored) || !ok) return 0;
    FlushInstructionCache(GetCurrentProcess(),uiguard_region,uiguard_region_size);
    for(unsigned i=0;i<uiguard_count;i++) {
        uiguard_tramp[i]=uiguard_stride(i);
        int32_t rel=(int32_t)((intptr_t)(uiguard_stride(i)+UIGUARD_STUB)-(intptr_t)(pnat_base+uiguard_tab[i].rva+5));
        uiguard_patched[i][0]=0xe9;memcpy(uiguard_patched[i]+1,&rel,4);
    }
    uiguard_restored=0;uiguard_installed=1;uiguard_live=1;
    for(unsigned i=0;i<uiguard_count;i++) {
        if(!uiguard_wanted(&uiguard_tab[i])) continue;
        if(!pview_cas(uiguard_tab[i].rva,uiguard_tab[i].bytes,uiguard_patched[i],5)) {uiguard_uninstall();uiguard_installed=0;uiguard_live=0;return 0;}
        uiguard_on[i]=1;
    }
    return 1;
}
static int uiguard_intact(void) {
    if(!uiguard_installed || !uiguard_region) return 0;
    for(unsigned i=0;i<uiguard_count;i++) if(uiguard_on[i] && memcmp(pnat_base+uiguard_tab[i].rva,uiguard_patched[i],5)) return 0;
    return 1;
}
static unsigned uiguard_installed_count(void) {unsigned n=0;for(unsigned i=0;i<uiguard_count;i++) n+=uiguard_on[i];return n;}
static int uiguard_fail(const char *why) {uiguard_stage="failed";if(!strcmp(uiguard_why_text,"-")) uiguard_why_text=why;return 0;}
/* The replay's pass (uirep_pass) asks the guard after its own part: 0 = stop (uirep_fail with uiguard_why_text). */
static int uiguard_pass(uint64_t tick) {
    if(!uiguard_armed) return 1;
    if(tick>pilot_stop) {
        uiguard_live=0;
        if(uiguard_installed) {uiguard_uninstall();if(!uiguard_restored) return uiguard_fail("ui_guard_restore");uiguard_stage="stopped";}
        return 1;
    }
    if(!uiguard_installed) {
        if(!uiguard_install()) return uiguard_fail("ui_guard_install");
        uiguard_stage="live";return 1;
    }
    if(!uiguard_intact()) return uiguard_fail("ui_guard_hook_lost");
    return 1;
}
static void uiguard_restore(void) {
    if(!uiguard_armed) return;
    uiguard_live=0;
    if(uiguard_installed) uiguard_uninstall();
    if(uiguard_installed) uiguard_stage="restore_refused";
}
/* Arming (Lua, after gate.ui_replay, before the first admitted tick; an identical rerun after a reload answers 1). */
static int uiguard_refuse(const char *why) {uiguard_arm_why=why;return 0;}
static int uiguard_arm_using(unsigned groups) {
    if(uiguard_armed) return uiguard_groups==groups?1:uiguard_refuse("rerun");
    if(!groups || (groups&~UIGUARD_ALL_GROUPS)) return uiguard_refuse("groups");
    if(!uirep_armed) return uiguard_refuse("needs_ui_replay");
    uiguard_groups=groups;uiguard_installed=0;uiguard_restored=1;uiguard_live=0;uiguard_stage="armed";uiguard_why_text="-";uiguard_arm_why="-";
    memset(uiguard_on,0,sizeof(uiguard_on));memset((void*)uiguard_refused_by,0,sizeof(uiguard_refused_by));uiguard_kinds();
    uiguard_passed=uiguard_queries=uiguard_refused=uiguard_ring_lost=uiguard_allowed=0;uiguard_ring_head=uiguard_ring_tail=0;
    uiguard_armed=1;return 1;
}
static int uiguard_arm(unsigned groups) {
    if(uiguard_armed) return uiguard_arm_using(groups);
    if(stats.issued) return uiguard_refuse("after_first_tick");
    if(GetCurrentThreadId()!=ui_thread) return uiguard_refuse("thread");
    if(!uiguard_pins()) {static char pin_why[32];snprintf(pin_why,sizeof(pin_why),"pins_ui_guard:%x",pin_failed_rva);return uiguard_refuse(pin_why);}
    return uiguard_arm_using(groups);
}
/* "tick:name:group:verdict:args,..." since the last call: verdict refused or ui_only (an allowed UI-only call), args '-' or
 * the events call's screen/control or signal (no ',' or ':' in them). */
static int uiguard_refusals_text(char *out,size_t capacity) {
    int n=0;out[0]=0;
    while(uiguard_ring_tail!=uiguard_ring_head) {
        const uiguard_refusal *r=&uiguard_ring[uiguard_ring_tail%UIGUARD_RING];const uiguard_entry *e=&uiguard_tab[r->index];
        int more=snprintf(out+n,capacity-(size_t)n,"%s%llu:%s:%s:%s:%s",n?",":"",(unsigned long long)r->tick,e->name,uiguard_group_names[e->group],
            r->ui_only?"ui_only":"refused",r->args[0]?r->args:"-");
        if(more<0 || (size_t)more>=capacity-(size_t)n) break;
        n+=more;++uiguard_ring_tail;
    }
    return n;
}
/* The status: counts, and the refused entries by name, most refused first (as many as fit). */
static int uiguard_status(char *out,size_t capacity) {
    int n=snprintf(out,capacity,"ui_guard=%s why=%s groups=%x entries=%u installed=%u passed=%lld queries=%lld allowed=%lld refused=%lld lost=%lld by=",uiguard_stage,
        uiguard_why_text,uiguard_groups,uiguard_count,uiguard_installed_count(),(long long)uiguard_passed,(long long)uiguard_queries,(long long)uiguard_allowed,
        (long long)uiguard_refused,(long long)uiguard_ring_lost);
    if(n<0 || (size_t)n>=capacity) return 0;
    unsigned char listed[UIGUARD_MAX];memset(listed,0,sizeof(listed));int any=0;
    for(;;) {
        unsigned best=UIGUARD_MAX;LONG64 most=0;
        for(unsigned i=0;i<uiguard_count;i++) if(!listed[i] && uiguard_refused_by[i]>most) {most=uiguard_refused_by[i];best=i;}
        if(best==UIGUARD_MAX) break;
        listed[best]=1;
        int more=snprintf(out+n,capacity-(size_t)n,"%s%s:%lld",any?",":"",uiguard_tab[best].name,(long long)most);
        if(more<0 || (size_t)more>=capacity-(size_t)n) break;
        n+=more;any=1;
    }
    if(!any && (size_t)n+1<capacity) {out[n++]='-';out[n]=0;}
    return n;
}
