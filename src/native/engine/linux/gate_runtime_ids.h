/* The same runtime component IDs on every client, set mode, on the Linux X4 9.00 executable: the
 * Linux twin of lockstep_runtime_ids.h, whose free list work, map digest and status are shared.
 * Included by gate_linux.c only. Why IDs differ and what set mode does are in that header.
 *
 * Linux sites: the map pointer is at 0x3daa860, the same structure as Windows with every field 8
 * bytes later because X4's mutex at +0x10 is 0x30 bytes (a critical section 0x28): inserts +0x40,
 * live +0x48, free +0x4c, generation +0x54, head +0x58, tail +0x5c, blocks +0x60. A slot's bucket
 * is unchanged. Remove 0xee3850 (rdi map, rsi ID, al removed) holds the only append to the free
 * list; EnsureFreeSlots is inlined into AssignNewID 0xeed870, which the gate does not touch.
 *
 * The table of who allocates (native_gate_runtime_ids_callers, default off) has its own hook here:
 * AssignNewID's entry, rdi the object, rax the ID. Before the seal it notes the return address only,
 * but for the few hundred components made after the import, where clients part (vps-linux-17: 375
 * against 372 and 370, the only load row that differs); after it the stack, through glibc's backtrace
 * and X4's .eh_frame. The rows are those of Windows, with
 * Linux addresses, so census_runtime_callers.py compares two clients of either platform. It costs one
 * unwind per allocation in play, about 16 a tick, and it only reports.
 *
 * The local lists for what only the view asks for (native_gate_runtime_ids_local): the shared
 * header's lists, build and assignment, with this executable's route table (lx_rid_sites) on the
 * hook's stack. Every Windows row has its Linux rows, matched by RTTI vtables, log strings and the
 * pair table, then pinned: GCC inlines the constructors of U::UIElement, HoloMap, CutsceneAnchor,
 * EffectObject, FogVolume, LensFlare and Stardust into the two component factories (0x119bd70,
 * 0x11a2560), so their rows are the Positional calls there, the one before each class's vtable store;
 * the three animation listeners and the message configuration listener are inlined where they are
 * made, so each row is the AssignNewID call before the vtable store. Slot 6 (CUIController, through
 * the secondary vtable's thunk) and slot 10 (GameRender) are their whole bodies, as on Windows; slot
 * 6 inlines what Windows calls. The optional camera setter has a thread-local
 * scope; other presentation scopes, UI dispatch and client guidance remain unported. */
#include <execinfo.h>
#include "gate_player_camera_id_scope.h"
#define RID_OWN_PLATFORM
#define RID_MAP (LX_ID_MAP_VA-LX_IMAGE_BASE)
#define RID_REMOVE_ENTRY (LX_ID_REMOVE_VA-LX_IMAGE_BASE)
#define RID_REMOVE_THUNK 0x300u  /* the relocated prologue, then jmp [rip] -> Remove+6 */
#define RID_F_INSERTS 0x40
#define RID_F_LIVE 0x48
#define RID_F_FREE 0x4c
#define RID_F_GENERATION 0x54
#define RID_F_HEAD 0x58
#define RID_F_TAIL 0x5c
#define RID_F_BLOCKS 0x60
#define RID_F_BLOCK1 0x68
#define LXI_THUNK_HOOK 0x340u    /* jmp [rip] -> rid_remove */
#define RID_ASSIGN_THUNK 0x380u  /* the relocated prologue, then jmp [rip] -> AssignNewID+6 */
#define LXI_ASSIGN_HOOK 0x3c0u   /* jmp [rip] -> rid_assign */
#define CRITICAL_SECTION void
#define EnterCriticalSection(mutex) lxg_lock(mutex)
#define LeaveCriticalSection(mutex) lxg_unlock(mutex)
#define LONG long /* the status prints no active word; rid_local_on stays this type */
/* The shared local lists call these; this file defines them for this executable. */
static int rid_local_pins(void);
static void *rid_allocate(size_t size);
static created_assign_fn rid_assign_original(void);
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wunused-function"
#include "lockstep_runtime_ids.h"
#pragma GCC diagnostic pop
#undef CRITICAL_SECTION
#undef EnterCriticalSection
#undef LeaveCriticalSection
#undef LONG
static int rid_pins(void) {return 1;} /* the gate's site pins, checked at start */
static int rid_swap(uint64_t expected,uint64_t value) {return gate_code_swap64(LX(LX_ID_REMOVE_VA),expected,value);}
static int rid_callers_on; /* the table of who allocates; the hook also serves the created identities */
static uint64_t rid_assign_patched_word;
/* The route table: return addresses in this image (VA - LX_IMAGE_BASE, as rid_frames_of reports them).
 * A call row's target is checked at arming; a body's bytes are lx_sites pins (rid_ui_*, rid_view_*). */
#define LXR(va) ((uint32_t)((va)-LX_IMAGE_BASE))
#define LX_RID_CALL(ret,target,route) {LXR(ret),LXR(ret),LXR(target),route,0}
#define LX_RID_BODY(start,end,route,ui_move) {LXR(start),LXR(end)-1u,0,route,ui_move}
#define LX_RID_POS_A 0x1316e40u /* U::Positional's constructors (Windows 0x7429c0, 0x7426d0) */
#define LX_RID_POS_B 0x13215c0u
#define LX_RID_SPHERE 0xeedd70u /* the query sphere, a JPH::SphereShape in an IDBase wrapper (Windows 0xb82d0) */
static const rid_site_t lx_rid_sites[]={
    /* U::UIElement in both factories (Windows: both constructors' Positional calls); U::UIElement::CreateInstance (the
     * log string; it calls factory 0x119bd70 itself). */
    LX_RID_CALL(0x119c625u,LX_RID_POS_A,RID_ROUTE_UI),LX_RID_CALL(0x11a58bdu,LX_RID_POS_B,RID_ROUTE_UI),LX_RID_BODY(0xb08820u,0xb08aa0u,RID_ROUTE_UI,0),
    /* U::HoloMap (Windows: its constructors' bodies; only their CutsceneAnchor base reaches AssignNewID), U::CutsceneAnchor. */
    LX_RID_CALL(0x119e108u,LX_RID_POS_A,RID_ROUTE_UI),LX_RID_CALL(0x11a3760u,LX_RID_POS_B,RID_ROUTE_UI),
    LX_RID_CALL(0x119f4b4u,LX_RID_POS_A,RID_ROUTE_UI),LX_RID_CALL(0x11a2e6eu,LX_RID_POS_B,RID_ROUTE_UI),
    /* Lua::NotifyOn*, in the Windows order. */
    LX_RID_CALL(0xa71953u,LX_ID_ASSIGN_VA,RID_ROUTE_UI),LX_RID_CALL(0xa71ae3u,LX_ID_ASSIGN_VA,RID_ROUTE_UI),LX_RID_CALL(0xa71cc9u,LX_ID_ASSIGN_VA,RID_ROUTE_UI),
    LX_RID_CALL(0xa71e99u,LX_ID_ASSIGN_VA,RID_ROUTE_UI),LX_RID_CALL(0xa72033u,LX_ID_ASSIGN_VA,RID_ROUTE_UI),LX_RID_CALL(0xa721a3u,LX_ID_ASSIGN_VA,RID_ROUTE_UI),
    LX_RID_CALL(0xa72323u,LX_ID_ASSIGN_VA,RID_ROUTE_UI),LX_RID_CALL(0xa724a3u,LX_ID_ASSIGN_VA,RID_ROUTE_UI),LX_RID_CALL(0xa72663u,LX_ID_ASSIGN_VA,RID_ROUTE_UI),
    LX_RID_CALL(0xa72823u,LX_ID_ASSIGN_VA,RID_ROUTE_UI),LX_RID_CALL(0xa729e3u,LX_ID_ASSIGN_VA,RID_ROUTE_UI),LX_RID_CALL(0xa72b63u,LX_ID_ASSIGN_VA,RID_ROUTE_UI),
    LX_RID_CALL(0xa72ce3u,LX_ID_ASSIGN_VA,RID_ROUTE_UI),LX_RID_CALL(0xa72e73u,LX_ID_ASSIGN_VA,RID_ROUTE_UI),LX_RID_CALL(0xa72fe3u,LX_ID_ASSIGN_VA,RID_ROUTE_UI),
    LX_RID_CALL(0xa73163u,LX_ID_ASSIGN_VA,RID_ROUTE_UI),LX_RID_CALL(0xa73b93u,LX_ID_ASSIGN_VA,RID_ROUTE_UI),LX_RID_CALL(0xa73d23u,LX_ID_ASSIGN_VA,RID_ROUTE_UI),
    LX_RID_CALL(0xa73ea3u,LX_ID_ASSIGN_VA,RID_ROUTE_UI),LX_RID_CALL(0xa74033u,LX_ID_ASSIGN_VA,RID_ROUTE_UI),LX_RID_CALL(0xa741a3u,LX_ID_ASSIGN_VA,RID_ROUTE_UI),
    LX_RID_CALL(0xa74323u,LX_ID_ASSIGN_VA,RID_ROUTE_UI),LX_RID_CALL(0xa744e3u,LX_ID_ASSIGN_VA,RID_ROUTE_UI),LX_RID_CALL(0xa746a3u,LX_ID_ASSIGN_VA,RID_ROUTE_UI),
    LX_RID_CALL(0xa7483bu,LX_ID_ASSIGN_VA,RID_ROUTE_UI),LX_RID_CALL(0xa749e3u,LX_ID_ASSIGN_VA,RID_ROUTE_UI),LX_RID_CALL(0xa74ba3u,LX_ID_ASSIGN_VA,RID_ROUTE_UI),
    LX_RID_CALL(0xa74d7du,LX_ID_ASSIGN_VA,RID_ROUTE_UI),
    /* The cockpit animation listeners (U::Animation::CockpitStateListener, CockpitActivationStateListener,
     * DockingBayFenceStateListener), each made in two places; Modules::StartMenu. */
    LX_RID_CALL(0x12acd29u,LX_ID_ASSIGN_VA,RID_ROUTE_UI),LX_RID_CALL(0x179bf15u,LX_ID_ASSIGN_VA,RID_ROUTE_UI),LX_RID_CALL(0x1c9a6a3u,LX_ID_ASSIGN_VA,RID_ROUTE_UI),
    LX_RID_CALL(0x1c9a7f3u,LX_ID_ASSIGN_VA,RID_ROUTE_UI),LX_RID_CALL(0x177fdb9u,LX_ID_ASSIGN_VA,RID_ROUTE_UI),LX_RID_CALL(0x177ff09u,LX_ID_ASSIGN_VA,RID_ROUTE_UI),
    LX_RID_CALL(0xf4ea97u,LX_ID_ASSIGN_VA,RID_ROUTE_UI),
    /* Slot 6 (UI::CUIController, the implementation behind the secondary vtable's thunk 0xa77e90; not the probe's frame)
     * and slot 10 (X4::GameRender). */
    LX_RID_BODY(0xa758a0u,0xa77e90u,RID_ROUTE_UI,1),LX_RID_BODY(0xfb0180u,0xfb01f0u,RID_ROUTE_UI,0),
    /* UI::ICom: a message (0xc8c400) and what its start makes, CMessageManager::CreateSource (its log string), the
     * message configuration listener (inlined in 0xf4ca20). */
    LX_RID_CALL(0xc8c5a8u,LX_ID_ASSIGN_VA,RID_ROUTE_VIEW),LX_RID_BODY(0xc78bc0u,0xc78d90u,RID_ROUTE_VIEW,0),LX_RID_BODY(0xa70cd0u,0xa718c0u,RID_ROUTE_VIEW,0),
    LX_RID_CALL(0xf4dad0u,LX_ID_ASSIGN_VA,RID_ROUTE_VIEW),
    /* EffectObject, FogVolume, LensFlare, Stardust in both factories; EffectObjectCreator. */
    LX_RID_CALL(0x119e50fu,LX_RID_POS_A,RID_ROUTE_VIEW),LX_RID_CALL(0x11a35f5u,LX_RID_POS_B,RID_ROUTE_VIEW),LX_RID_CALL(0x119dcd1u,LX_RID_POS_A,RID_ROUTE_VIEW),
    LX_RID_CALL(0x11a3d89u,LX_RID_POS_B,RID_ROUTE_VIEW),LX_RID_CALL(0x119f6d9u,LX_RID_POS_A,RID_ROUTE_VIEW),LX_RID_CALL(0x11a507au,LX_RID_POS_B,RID_ROUTE_VIEW),
    LX_RID_CALL(0x119c4c8u,LX_RID_POS_A,RID_ROUTE_VIEW),LX_RID_CALL(0x11a572eu,LX_RID_POS_B,RID_ROUTE_VIEW),
    LX_RID_CALL(0x1796d53u,LX_ID_ASSIGN_VA,RID_ROUTE_VIEW),LX_RID_CALL(0x178460bu,LX_ID_ASSIGN_VA,RID_ROUTE_VIEW),
    /* AI::NavSplineCalculator's sweep test (0x10b0b30, the only maker of the sphere that sweeps it) and the player
     * entity's two query spheres (vfunc674 0x138e6a0: +0x8a510 radius +0x4ed4, +0x8a518 radius +0x4ed8; Windows
     * +0x8a520/+0x4f54 and +0x8a528/+0x4f58). */
    LX_RID_CALL(0x10b0cd7u,LX_RID_SPHERE,RID_ROUTE_VIEW),LX_RID_CALL(0x13927d7u,LX_RID_SPHERE,RID_ROUTE_VIEW),LX_RID_CALL(0x1392be5u,LX_RID_SPHERE,RID_ROUTE_VIEW),
    /* The camera-mode event creates another query sphere at receiver+0x6bc8.
     * 0x136fea0 creates it only for camera modes 2..5; 0x136f950 passes it to
     * the scene query at 0xedfd60. It is a view-owned XPhys::Shape wrapping a
     * JPH::SphereShape, not a shared world entity. This event runs after the
     * synchronous camera setter's scope, so it needs its own exact call pin.
     * Do not classify the common sphere constructor or other callers. */
    LX_RID_CALL(0x136ffbdu,LX_RID_SPHERE,RID_ROUTE_VIEW)};
#define LX_RID_SITES (sizeof(lx_rid_sites)/sizeof(*lx_rid_sites))
static created_assign_fn lxr_assign_native; /* tests only: replaces the thunk's original */
/* Every call row checked: the table's return addresses are this build's (the bodies are start pins). */
static int rid_local_pins(void) {
    for(unsigned i=0;i<LX_RID_SITES;i++) {
        const rid_site_t *s=&lx_rid_sites[i];if(!s->target) continue;
        int32_t rel;memcpy(&rel,image+s->lo-4u,4);
        if(image[s->lo-5u]!=0xe8 || (uint32_t)((int64_t)s->lo+rel)!=s->target) return 0;
    }
    return 1;
}
/* What AssignNewID's inlined EnsureFreeSlots allocates a block with: (size, alignment). */
static void *rid_allocate(size_t size) {
    typedef void *(*allocate_fn)(size_t,size_t);
    return rid_allocate_native?rid_allocate_native(size):((allocate_fn)(void *)LX(LX_RID_ALLOCATE_VA))(size,0x10);
}
static created_assign_fn rid_assign_original(void) {return lxr_assign_native?lxr_assign_native:(created_assign_fn)(void *)(thunk+RID_ASSIGN_THUNK);}
/* Which list a stack belongs to: the table, ui over view; off or before the seal, the shared one. */
static unsigned lxr_route_of(const uint32_t *frames,unsigned count) {
    if(!InterlockedCompareExchange(&rid_local_on,0,0) || !rid_sealed) return RID_ROUTE_SHARED;
    if(player_camera_id_scope_active()) return RID_ROUTE_UI;
    const int probe=InterlockedCompareExchange(&rid_probe_frame,0,0)!=0;unsigned route=RID_ROUTE_SHARED;
    for(unsigned i=0;i<count;i++) for(unsigned s=0;s<LX_RID_SITES;s++) {
        const rid_site_t *site=&lx_rid_sites[s];
        if(frames[i]<site->lo || frames[i]>site->hi || (site->ui_move && probe)) continue;
        if(site->route==RID_ROUTE_UI) return RID_ROUTE_UI;
        route=site->route;
    }
    return route;
}
/* Opt-in synchronous camera setter: the existing UI list is used only on
 * this thread. The owned allocator hooks must remain installed throughout. */
static int player_camera_rid_ready(void) {
    return rid_mode == RID_SET && rid_sealed && rid_local_built && rid_installed && rid_assign_installed &&
        created_installed && InterlockedCompareExchange(&rid_active, 0, 0) &&
        InterlockedCompareExchange(&rid_local_on, 0, 0) &&
        !InterlockedCompareExchange64(&rid_failures, 0, 0) && rid_owned() &&
        !player_camera_id_scope_active();
}
static int player_camera_rid_begin(player_camera_id_token *token) {
    return player_camera_rid_ready() && player_camera_id_scope_enter(token);
}
static int player_camera_rid_end(player_camera_id_token *token) {
    if (!player_camera_id_scope_leave(token)) return 0;
    return player_camera_rid_ready();
}

/* The X4 return addresses of an allocation, innermost first: the direct caller before the seal, the stack after it and for components. */
static unsigned rid_frames_of(void *caller,uint32_t *frames) {
    const uintptr_t low=(uintptr_t)image,high=low+(LX_TEXT_END_VA-LX_IMAGE_BASE);unsigned n=0;
    if(rid_sealed || (uintptr_t)caller==(uintptr_t)LX(LX_ID_COMPONENT_RETURN_VA)) {
        void *stack[RID_STACK];const int got=backtrace(stack,RID_STACK);
        for(int i=0;i<got;i++) if((uintptr_t)stack[i]>=low && (uintptr_t)stack[i]<high) frames[n++]=(uint32_t)((uintptr_t)stack[i]-low);
    }
    if(!n && (uintptr_t)caller>=low && (uintptr_t)caller<high) frames[n++]=(uint32_t)((uintptr_t)caller-low);
    return n;
}
/* The thunk jumps, so the return address is AssignNewID's caller. With the local lists, after the seal, the
 * stack picks the list (lxr_route_of). */
static __attribute__((noinline)) uint64_t rid_assign(void *object) {
    const created_assign_fn original=rid_assign_original();
    const int local=InterlockedCompareExchange(&rid_local_on,0,0) && rid_sealed;
    if(!rid_callers_on && !local) return created_assign_execute(original,object);
    void *caller=__builtin_return_address(0);uint32_t frames[RID_STACK];
    const unsigned count=rid_frames_of(caller,frames),route=lxr_route_of(frames,count);
    const DWORD thread=GetCurrentThreadId();
    const created_assign_fn assign=route==RID_ROUTE_UI?rid_assign_ui:route==RID_ROUTE_VIEW?rid_assign_view:original;
    const uint64_t result=created_assign_execute(assign,object);
    if(rid_callers_on) rid_note_caller(frames,count,route,thread==ui_thread?0u:thread==main_thread?1u:2u);
    return result;
}
static void lxi_thunk_write(unsigned char *page) {
    lxw_jump_write(page+LXI_THUNK_HOOK,(void*)rid_remove);
    unsigned char *p=page+RID_REMOVE_THUNK;memcpy(p,"\x41\x57\x41\x56\x41\x55",LX_ID_REMOVE_PROLOGUE); /* push r15; push r14; push r13 */
    lxw_jump_write(p+LX_ID_REMOVE_PROLOGUE,LX(LX_ID_REMOVE_VA)+LX_ID_REMOVE_PROLOGUE);
    lxw_jump_write(page+LXI_ASSIGN_HOOK,(void*)rid_assign);
    p=page+RID_ASSIGN_THUNK;memcpy(p,"\x41\x56\x41\x55\x41\x54",LX_ID_ASSIGN_PROLOGUE); /* push r14; push r13; push r12 */
    lxw_jump_write(p+LX_ID_ASSIGN_PROLOGUE,LX(LX_ID_ASSIGN_VA)+LX_ID_ASSIGN_PROLOGUE);
}
static int rid_install(void) {
    unsigned char *entry=LX(LX_ID_REMOVE_VA);
    if(!thunk) {rid_stage="thunk";return 0;}
    if(rd64(entry)!=LX_ID_REMOVE_ENTRY_ORIGINAL) {rid_stage="entry";return 0;}
    rid_original_word=LX_ID_REMOVE_ENTRY_ORIGINAL;
    if(!lxw_patch(entry,rid_original_word,LXI_THUNK_HOOK,&rid_patched_word)) {rid_stage="swap";return 0;}
    rid_installed=1;return 1;
}
static int rid_owned(void) {
    return (!rid_installed || rd64(LX(LX_ID_REMOVE_VA))==rid_patched_word) && (!rid_assign_installed || rd64(LX(LX_ID_ASSIGN_VA))==rid_assign_patched_word);
}
/* AssignNewID's entry, once, for the table of who allocates and the created identities. */
static int rid_assign_install(void) {
    if(rid_assign_installed) return 1;
    if(rd64(LX(LX_ID_ASSIGN_VA))!=LX_ID_ASSIGN_ENTRY_ORIGINAL) return 0;
    if(!lxw_patch(LX(LX_ID_ASSIGN_VA),LX_ID_ASSIGN_ENTRY_ORIGINAL,LXI_ASSIGN_HOOK,&rid_assign_patched_word)) return 0;
    rid_assign_installed=1;return 1;
}
/* After a set rid_arm, before the load. The first backtrace loads libgcc_s, so that happens here and not on some X4 thread. */
static int rid_callers_arm(void) {
    if(rid_callers_on) return rid_mode==RID_SET;
    if(rid_mode!=RID_SET || !stats.active || rid_sealed || GetCurrentThreadId()!=ui_thread || !thunk) {rid_stage="callers_phase";return 0;}
    void *first[2];backtrace(first,2);
    if(!rid_assign_install()) {rid_stage="callers_entry";return 0;}
    rid_callers_on=1;return 1;
}
/* With rid_restore: the entry back; a call already in the hook finishes through the thunk, which stays. */
static void rid_callers_restore(void) {
    if(rid_assign_installed && gate_code_swap64(LX(LX_ID_ASSIGN_VA),rid_assign_patched_word,LX_ID_ASSIGN_ENTRY_ORIGINAL)) rid_assign_installed=0;
    if(rid_assign_installed) rid_restored=0;
}
static int lua_runtime_ids_callers(lua_State *L) {p_lua_pushnumber(L,rid_callers_arm());return 1;}
/* runtime_ids_local(): right after a set runtime_ids, with the created identities; the view's allocations on their own lists. */
static int lua_runtime_ids_local(lua_State *L) {p_lua_pushnumber(L,accept_ui() && rid_local_arm());return 1;}
/* runtime_ids_probe(1|0): around the probe's own frame, which runs in slot 6 and is not the view. */
static int lua_runtime_ids_probe(lua_State *L) {
    if(accept_ui()) InterlockedExchange(&rid_probe_frame,p_lua_tonumber(L,1)!=0?1L:0L);
    return 0;
}
static int lua_runtime_ids(lua_State *L) {
    double base=p_lua_tonumber(L,1);
    p_lua_pushnumber(L,isfinite(base) && base>=0 && base<=RID_BASE_MAX && floor(base)==base && rid_arm((uint32_t)base));return 1;
}
static int lua_runtime_ids_status(lua_State *L) {
    char out[900];int n=accept_ui()?rid_status(out,sizeof(out)):0;
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);
    return 1;
}
/* The ID the next allocation would get, for native_gate_runtime_ids_barrier: not ported, so never read. */
static int lua_runtime_ids_word(lua_State *L) {
    char out[32];int n=accept_ui()?snprintf(out,sizeof(out),"%016llx",(unsigned long long)rid_next_id):0;
    if(n>0) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);
    return 1;
}
static int lua_runtime_ids_map_row(lua_State *L) {
    char out[640];int n=accept_ui()?rid_map_row(out,sizeof(out)):0;
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);
    return 1;
}
static int lua_runtime_ids_caller_row(lua_State *L) {
    double index=p_lua_tonumber(L,1);char out[256];
    int n=accept_ui() && isfinite(index) && index>=0 && index<RID_CALLERS && floor(index)==index?rid_caller_row((unsigned)index,out,sizeof(out)):0;
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);
    return 1;
}
