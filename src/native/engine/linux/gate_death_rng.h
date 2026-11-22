/* native_gate_death_rng = 'all' on the Linux gate (lockstep_death_all.h; Windows: lockstep_death.h, armed the same way,
 * gate.death_all()): a ship's death draws (its wreck's drift) come from a word seeded from the gate tick, the ship's
 * portable key and the kill reason, never from the delivering thread's simulation word. Included by gate_linux.c after
 * gate_path.h (world_identity_key) and gate_sound_rng.h.
 *
 * Linux site: the KillEvent listener of every ship class, U::Ship 0x12adf40 (Windows 0x7cd610), in slot 9 of the S, M,
 * L and XL vtables (address points 0x3d21748, 0x3d23cd0, 0x3d41418, 0x3d3ee90; Windows slot 8: two destructors here).
 * U::KillEvent::Dispatch 0x16b1920 tail-calls it as (listener, source, reason = event+0x18, killer, time in xmm0). The
 * six Random::FloatState draws (0x12ae1d7..0x12ae254, fs:-0x2720) fill the wreck's two velocity vectors in Windows'
 * order (0x7d3bf0: angular (d6,d5,d4), linear (-d2,d3,-d1)), so the same word gives the same wreck. The four slots are
 * pointer swaps (expected: the listener), the listener's body is pinned (death_rng_listener). Armed before the first
 * admitted frame; every gate tick re-checks the slots and the pin (stop reason 218); stop restores. */
#define LXD_STOP_REASON 218u
#define LXD_LISTENER_VA 0x12adf40u
#define LXD_SLOTS 4u
typedef void (*lxd_fn)(void *,void *,unsigned,void *,double);
static const uint32_t lxd_slots[LXD_SLOTS]={0x3d21748u+0x48u,0x3d23cd0u+0x48u,0x3d41418u+0x48u,0x3d3ee90u+0x48u};
static const char *const lxd_pin_names[]={"death_rng_listener"};
static unsigned lxd_swapped;
static int lxd_installed,lxd_restored;
static const char *lxd_stage="idle";
static volatile int64_t lxd_keyed,lxd_unkeyed,lxd_calls;
static lxd_fn lxd_override; /* tests */
static void lxd_listener(void *self,void *source,unsigned reason,void *killer,double time) {
    const lxd_fn original=lxd_override?lxd_override:(lxd_fn)(void *)LX(LXD_LISTENER_VA);
    __atomic_add_fetch(&lxd_calls,1,__ATOMIC_RELAXED);
    uint64_t runtime=0,key=0;unsigned klass;uint64_t *word=gate_tls_rng(gate_tls_self());
    if(stats.active && boot_phase==3 && source==self && GetCurrentThreadId()==ui_thread && isfinite(time) && word &&
       gate_read_local((unsigned char*)self+8,&runtime,8)) key=world_identity_key(runtime,&klass);
    if(!key) {__atomic_add_fetch(&lxd_unkeyed,1,__ATOMIC_RELAXED);original(self,source,reason,killer,time);return;}
    const uint64_t saved=*word;
    {uint64_t clock=0;unsigned char *tls=gate_tls_self();if(tls) gate_read_local(tls+GATE_TLS_LINUX_TIME,&clock,8);
     death_all_note(stats.issued,key,reason,time,clock);}
    *word=death_all_seed(stats.issued,key,reason);
    original(self,source,reason,killer,time);
    *word=saved;__atomic_add_fetch(&lxd_keyed,1,__ATOMIC_RELAXED);
}
static int lxd_pins(void) {
    for(unsigned i=0;i<sizeof(lxd_pin_names)/sizeof(*lxd_pin_names);i++) if(!site_ok(site_named(lxd_pin_names[i]))) return 0;
    return 1;
}
/* 0 native, 1 ours, -1 anything else. */
static int lxd_state(void) {
    unsigned native=0,ours=0;
    for(unsigned i=0;i<LXD_SLOTS;i++) {
        uint64_t v;if(!gate_read_local(LX(lxd_slots[i]),&v,8)) return -1;
        native+=v==(uint64_t)(uintptr_t)LX(LXD_LISTENER_VA);ours+=v==(uint64_t)(uintptr_t)lxd_listener;
    }
    return native==LXD_SLOTS?0:ours==LXD_SLOTS?1:-1;
}
static void lxd_unswap(void) {
    for(unsigned i=0;i<LXD_SLOTS;i++)
        if((lxd_swapped&(1u<<i)) && gate_data_swap64(LX(lxd_slots[i]),(uint64_t)(uintptr_t)lxd_listener,(uint64_t)(uintptr_t)LX(LXD_LISTENER_VA)))
            lxd_swapped&=~(1u<<i);
}
static int lxd_arm(void) {
    if(lxd_installed) return stats.active;
    if(!stats.active || (boot_mode && boot_phase) || stats.issued || GetCurrentThreadId()!=ui_thread) {lxd_stage="guard";return 0;}
    if(!lxd_pins() || lxd_state()!=0) {lxd_stage="pins";return 0;}
    for(unsigned i=0;i<LXD_SLOTS;i++) {
        if(!gate_data_swap64(LX(lxd_slots[i]),(uint64_t)(uintptr_t)LX(LXD_LISTENER_VA),(uint64_t)(uintptr_t)lxd_listener)) {
            lxd_unswap();lxd_stage="swap";return 0;
        }
        lxd_swapped|=1u<<i;
    }
    lxd_installed=1;lxd_restored=0;lxd_stage="armed";return 1;
}
static int lxd_check(void) {
    if(!lxd_installed) return 1;
    const char *why=lxd_state()!=1?"slot_changed":!lxd_pins()?"block_changed":NULL;
    if(why) {lxd_stage=why;return 0;}
    return 1;
}
static void lxwc_restore(void);
static void lxd_restore(void) {
    lxwc_restore();
    if(!lxd_installed) return;
    lxd_unswap();
    if(!lxd_swapped) {lxd_installed=0;lxd_restored=1;lxd_stage="restored";}
}
static int lxd_status(char *out,size_t capacity) {
    const int n=snprintf(out,capacity,"all=%d slots=%x keyed=%lld unkeyed=%lld calls=%lld stage=%s restored=%d",
        lxd_installed,lxd_swapped,(long long)lxd_keyed,(long long)lxd_unkeyed,(long long)lxd_calls,lxd_stage,lxd_restored);
    return n>0 && (size_t)n<capacity?n:0;
}
static int lua_death_all(lua_State *L) {p_lua_pushnumber(L,accept_ui() && lxd_arm());return 1;}
static int lua_death_all_row(lua_State *L) {
    double index=p_lua_tonumber(L,1);char out[256];int n=0;
    if(accept_ui() && isfinite(index) && index>=0 && index<9007199254740992.0 && floor(index)==index) n=death_all_row((uint64_t)index,out,sizeof(out));
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);
    return 1;
}
static int lua_death_all_status(lua_State *L) {
    char out[256];int n=accept_ui()?lxd_status(out,sizeof(out)):0;
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);
    return 1;
}
/* Wreck clock rows (lockstep_death_all.h): the pose function U::LinearDecelerationController 0x14ddcd0(this, out, flag,
 * time) gets an entry detour, since GCC calls it directly from its callers (0x11ae016, 0x11c9478, 0x188de92, two tail
 * jumps) besides slot 39 of the vtable 0x3affcb8: its first three pushes (6 bytes, one 16-byte line) become a jmp to a
 * page stub, the trampoline runs them and jumps back to 0x14ddcd6; Windows swaps slot 6. */
#define LXWC_POSE_VA 0x14ddcd0u
#define LXWC_STOLEN 6u
typedef uint64_t (*lxwc_pose_fn)(void *,void *,uint64_t,uint64_t,double);
static const unsigned char lxwc_prologue[16]={0x41,0x56,0x41,0x55,0x41,0x54,0x55,0x48,0x89,0xfd,0x53,0x48,0x89,0xf3,0x48,0x81};
static unsigned char *lxwc_page;
static unsigned __int128 lxwc_original,lxwc_patched;
static int lxwc_installed;
static uint64_t lxwc_pose(void *self,void *out,uint64_t flag,uint64_t extra,double time) {
    uint32_t chain[4]={0,0,0,0};void *frames[8];const int got=backtrace(frames,8);unsigned k=0;
    const uintptr_t low=(uintptr_t)image,high=low+(LX_TEXT_END_VA-LX_IMAGE_BASE);
    for(int i=1;i<got && k<4;i++) if((uintptr_t)frames[i]>=low && (uintptr_t)frames[i]<high) chain[k++]=(uint32_t)((uintptr_t)frames[i]-low);
    uint64_t cur=0,prev=0;unsigned char *tls=gate_tls_self();
    if(tls) {gate_read_local(tls+GATE_TLS_LINUX_TIME,&cur,8);gate_read_local(tls+GATE_TLS_LINUX_PREVIOUS_TIME,&prev,8);}
    const DWORD thread=GetCurrentThreadId();
    wreck_clock_note(self,time,cur,prev,thread==ui_thread?0u:thread==main_thread?1u:2u,chain);
    return ((lxwc_pose_fn)(void *)(lxwc_page+0x20))(self,out,flag,extra,time);
}
static int lxwc_arm(uint64_t first,uint64_t last) {
    if(lxwc_installed) return first==wreck_clock_first && last==wreck_clock_last;
    if(!first || last<first || stats.issued || GetCurrentThreadId()!=ui_thread) return 0;
    unsigned char *at=LX(LXWC_POSE_VA);
    if(((uintptr_t)at&15) || memcmp(at,lxwc_prologue,sizeof(lxwc_prologue))) return 0;
    if(!lxwc_page) lxwc_page=gate_near_page(at);
    if(!lxwc_page || mprotect(lxwc_page,GATE_PAGE,PROT_READ|PROT_WRITE)) return 0;
    memset(lxwc_page,0xcc,GATE_PAGE);
    lxw_jump_write(lxwc_page,(void*)lxwc_pose);                       /* entry: jmp [rip] -> the recorder */
    memcpy(lxwc_page+0x20,at,LXWC_STOLEN);lxw_jump_write(lxwc_page+0x20+LXWC_STOLEN,at+LXWC_STOLEN); /* trampoline */
    if(mprotect(lxwc_page,GATE_PAGE,PROT_READ|PROT_EXEC)) return 0;
    memcpy(&lxwc_original,at,16);lxwc_patched=lxwc_original;
    {unsigned char jmp[5];const int32_t rel=(int32_t)((intptr_t)lxwc_page-(intptr_t)(at+5));jmp[0]=0xe9;memcpy(jmp+1,&rel,4);
     memcpy((unsigned char*)&lxwc_patched,jmp,5);}
    wreck_clock_first=first;wreck_clock_last=last;
    if(!gate_code_swap128(at,lxwc_original,lxwc_patched)) {wreck_clock_last=0;return 0;}
    lxwc_installed=1;return 1;
}
static void lxwc_restore(void) {
    if(lxwc_installed && gate_code_swap128(LX(LXWC_POSE_VA),lxwc_patched,lxwc_original)) lxwc_installed=0;
}
/* view_slots(): the view cache slot indexes, 'current' 0x3e77430 and 'published' (view 0) 0x3e7742c, as 0x189f5f0
 * chooses them (Windows 0x38f4ee0 / 0x38f4eb4; diagnostic, read-only). */
static int lua_view_slots(lua_State *L) {
    char out[64];uint32_t cur=0,pub=0;int n=0;
    if(accept_ui() && gate_read_local(LX(0x3e77430u),&cur,4) && gate_read_local(LX(0x3e7742cu),&pub,4)) n=snprintf(out,sizeof(out),"cur=%u pub=%u",cur,pub);
    if(n>0) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);
    return 1;
}
static int lua_wreck_clock(lua_State *L) {
    const double a=p_lua_tonumber(L,1),b=p_lua_tonumber(L,2);
    p_lua_pushnumber(L,accept_ui() && isfinite(a) && isfinite(b) && a>=1 && b>=a && b<1e9 && lxwc_arm((uint64_t)a,(uint64_t)b));return 1;
}
static int lua_wreck_clock_row(lua_State *L) {
    double index=p_lua_tonumber(L,1);char out[300];int n=0;
    if(accept_ui() && isfinite(index) && index>=0 && index<9007199254740992.0 && floor(index)==index) n=wreck_clock_row((uint64_t)index,out,sizeof(out));
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);
    return 1;
}
