/* The region object search's camera test from the player's ship on the Linux X4 9.00 executable
 * (native_gate_mining_camera = 'view'): the Linux side of lockstep_region_camera.h's mining group, whose distance core
 * (lockstep_mining_near.h) both gates share. Included by gate_linux.c only, after gate_warp_exit.h.
 *
 * The search 0x1f47b20 (Windows 0x7884a0; AI salvage and mining through find_recyclable_with_yield_in_sector and
 * 0x7880c0's twin) returns nothing when the region's cluster (0x127c340 of the sector rbx) is the player object's
 * (g_Universe 0x3db6948 +0x238, its cluster from 0xc7e2b0 into r15, held: 0xa14540 releases r15+0x38) and the camera
 * (0x1dccd30, its transform 0x1925bf0) is within 50 km of the requested position (0x1a4a2a0 against 2.5e9): each
 * process's camera decided which region objects the AI got. W30 against lx20: at tick 2664 a salvage ship's search in
 * sector 0x327 (cluster 0x323) got "far" on Windows and spawned a Recyclable, "near" on Linux and retried 300 ticks
 * later; Windows answers "far" with and without its stand-in (W34 = W30), Linux's camera block "near".
 *
 * Here the camera block 0x1f47f40 (16-aligned, entered only from 0x1f47b9e) becomes a jmp rel32 to a thunk near the
 * image that calls lxmc_near(cluster [rbp-0x178], position [rbp-0x168], frame r12), then continues as the native
 * block would: near -> 0x1f47fc5, far -> 0x1f48130, both with rdi = r15+0x38 (the release). Only stack slots and
 * callee-saved registers are live there (the native block calls out too), and rsp is 16-aligned in the body.
 * lxmc_near is rc_mining_near without the hybrid owner (the Linux gate has no hybrid sectors): near when the latched
 * point is in that cluster and within 50 km, from GetTransformRelativeTo (0x18a0090) at the thread's game time in the
 * cluster, through the shared rc_mining_dist2. The latch (region_camera_frame, every admitted tick) is
 * rc_shape_latch's without view points: the player object's class-0x49 ancestor, else the player object (the same on
 * every client of a same-save census; the view points port replaces it). Pin mining_search: the whole search, read
 * with the site's 5 bytes as the original. Before the first admitted frame, on the MoveWorker; the per-tick check
 * (stop reason 166, as on Windows) and the restore are this file's. */
#include "lockstep_mining_near.h"
#define LXMC_STOP_REASON 166
#define LXMC_LINE (LX_MC_SITE_VA&~15u)
#define LXMC_STUB_BYTES 72u
_Static_assert((LX_MC_SITE_VA&15u)==0,"the site starts a 16-byte line");
static const unsigned char lxmc_original[5]={0x66,0x0f,0xef,0xc0,0x0f}; /* pxor xmm0,xmm0; the first byte of movaps */
static unsigned char *lxmc_page;
static int lxmc_installed,lxmc_restored;
static LONG64 lxmc_failures;
static const char *lxmc_stage="idle";
static volatile LONG64 lxmc_calls,lxmc_near_count,lxmc_foreign,lxmc_latches;
static void *volatile lxmc_point,*volatile lxmc_cluster;
typedef char (*lxmc_is_class_fn)(void *,int);
typedef void (*lxmc_gtr_fn)(void *,float *,double,void *,unsigned char);
static lxmc_gtr_fn lxmc_gtr_override; /* tests only */
static const lx_site *lxmc_site(const char *name) {
    for(unsigned i=0;i<gate_site_count;i++) if(!strcmp(gate_sites[i].name,name)) return &gate_sites[i];
    return NULL;
}
/* The object or its nearest +0x70 ancestor of the class (IsClass, vtable +0x11e8 as on Windows), at most 17 up. */
static void *lxmc_ancestor(void *object,int klass) {
    unsigned char *p=object;
    for(unsigned depth=0;p && depth<17;depth++) {
        void **vtable=NULL;lxmc_is_class_fn is_class=NULL;
        if(!gate_read_local(p,&vtable,8) || !vtable || !gate_read_local((unsigned char*)vtable+0x11e8,&is_class,8) || !is_class) return NULL;
        if(is_class(p,klass)) return p;
        if(!gate_read_local(p+0x70,&p,8)) return NULL;
    }
    return NULL;
}
static int lxmc_is_below(unsigned char *frame,const void *ancestor) {
    for(unsigned depth=0;frame && depth<17;depth++) {if(frame==ancestor) return 1;if(!gate_read_local(frame+0x70,&frame,8)) return 0;}
    return 0;
}
static void lxmc_gtr(void *self,float out[16],double time,void *ancestor) {
    static const float identity[16]={0,0,0,0, 1,0,0,0, 0,1,0,0, 0,0,1,0};
    memcpy(out,identity,sizeof(identity));
    (lxmc_gtr_override?lxmc_gtr_override:(lxmc_gtr_fn)(void*)LX(LX_WR_GTRT_VA))(self,out,time,ancestor,0);
}
/* Every admitted tick: the player object's ship (or the player object) and its cluster. */
static void lxmc_latch(void) {
    unsigned char *universe=NULL;void *player=NULL,*parent=NULL,*point=NULL,*cluster=NULL;
    if(gate_read_local(LX(LX_UNIVERSE_VA),&universe,8) && universe && gate_read_local(universe+0x238,&player,8) && player &&
       gate_read_local((unsigned char*)player+0x70,&parent,8)) {
        point=parent?lxmc_ancestor(parent,0x49):NULL;if(!point) point=player;
        cluster=lxmc_ancestor(point,0xf);
    }
    lxmc_point=point;lxmc_cluster=cluster;InterlockedIncrement64(&lxmc_latches);
}
static int lxmc_near(void *cluster,const float *pos,void *frame) {
    InterlockedIncrement64(&lxmc_calls);float best=-1.0f;
    if(cluster && pos && frame) {
        if(!lxmc_is_below(frame,cluster)) InterlockedIncrement64(&lxmc_foreign);
        else {
            void *point=lxmc_point;
            if(point && lxmc_cluster==cluster) {
                double now=0,*time=gate_tls_time(gate_tls_self());if(time) memcpy(&now,time,8);
                float f[16] __attribute__((aligned(16))),v[16] __attribute__((aligned(16)));
                lxmc_gtr(frame,f,now,cluster);lxmc_gtr(point,v,now,cluster);
                best=rc_mining_dist2(f,v,pos);
            }
        }
    }
    const int is_near=rc_mining_is_near(best);
    if(is_near) InterlockedIncrement64(&lxmc_near_count);
    return is_near;
}
/* mov rdi,[rbp-0x178]; mov rsi,[rbp-0x168]; mov rdx,r12; movabs rax,lxmc_near; call rax; lea rdi,[r15+0x38];
 * test eax,eax; je far; jmp [rip] near; far: jmp [rip] far. */
static void lxmc_stub_bytes(unsigned char out[LXMC_STUB_BYTES],const void *fn,const void *near_to,const void *far_to) {
    static const unsigned char head[]={0x48,0x8b,0xbd,0x88,0xfe,0xff,0xff, 0x48,0x8b,0xb5,0x98,0xfe,0xff,0xff, 0x4c,0x89,0xe2, 0x48,0xb8};
    static const unsigned char mid[]={0xff,0xd0, 0x49,0x8d,0x7f,0x38, 0x85,0xc0, 0x74,0x0e, 0xff,0x25,0,0,0,0};
    static const unsigned char jump[]={0xff,0x25,0,0,0,0};
    unsigned n=0;memset(out,0xcc,LXMC_STUB_BYTES);
    memcpy(out+n,head,sizeof(head));n+=sizeof(head);memcpy(out+n,&fn,8);n+=8;
    memcpy(out+n,mid,sizeof(mid));n+=sizeof(mid);memcpy(out+n,&near_to,8);n+=8;
    memcpy(out+n,jump,sizeof(jump));n+=sizeof(jump);memcpy(out+n,&far_to,8);
}
static void lxmc_entry(unsigned char out[5],const unsigned char *to) {
    int32_t d=(int32_t)((intptr_t)to-(intptr_t)(LX(LX_MC_SITE_VA)+5));out[0]=0xe9;memcpy(out+1,&d,4);
}
/* 0 native, 1 our jmp to the thunk, -1 anything else. */
static int lxmc_state(void) {
    unsigned char b[5],want[5];
    if(!gate_read_local(LX(LX_MC_SITE_VA),b,5)) return -1;
    if(!memcmp(b,lxmc_original,5)) return 0;
    if(lxmc_page) {lxmc_entry(want,lxmc_page);if(!memcmp(b,want,5)) return 1;}
    return -1;
}
static int lxmc_pins(void) {
    const lx_site *s=lxmc_site("mining_search");static unsigned char copy[0x800];
    if(!s || s->length>sizeof(copy) || s->va!=LX_MC_FUNC_VA || LX_MC_SITE_VA+5>s->va+s->length || lxmc_state()<0 ||
       !gate_read_local(LX(s->va),copy,s->length)) return 0;
    memcpy(copy+(LX_MC_SITE_VA-s->va),lxmc_original,5);
    return hash_ok(copy,s->length,s->sha256);
}
static int lxmc_swap(const unsigned char from[5],const unsigned char to[5]) {
    unsigned char a[16],b[16];unsigned __int128 expected,desired;
    memcpy(a,LX(LXMC_LINE),16);memcpy(b,a,16);memcpy(a,from,5);memcpy(b,to,5);
    memcpy(&expected,a,16);memcpy(&desired,b,16);
    return gate_code_swap128(LX(LXMC_LINE),expected,desired);
}
static int lxmc_install(void) {
    if(!lxmc_page) lxmc_page=gate_near_page(LX(LX_MC_SITE_VA));
    if(!lxmc_page) return 0;
    intptr_t d=(intptr_t)lxmc_page-(intptr_t)(LX(LX_MC_SITE_VA)+5);if(d<INT32_MIN || d>INT32_MAX) return 0;
    if(mprotect(lxmc_page,GATE_PAGE,PROT_READ|PROT_WRITE)) return 0;
    memset(lxmc_page,0xcc,GATE_PAGE);lxmc_stub_bytes(lxmc_page,(void*)lxmc_near,LX(LX_MC_NEAR_VA),LX(LX_MC_FAR_VA));
    if(mprotect(lxmc_page,GATE_PAGE,PROT_READ|PROT_EXEC)) return 0;
    unsigned char to[5];lxmc_entry(to,lxmc_page);
    return lxmc_swap(lxmc_original,to);
}
static int lxmc_owned(void) {
    if(!lxmc_installed) return 1;
    unsigned char want[LXMC_STUB_BYTES];lxmc_stub_bytes(want,(void*)lxmc_near,LX(LX_MC_NEAR_VA),LX(LX_MC_FAR_VA));
    return lxmc_state()==1 && !memcmp(lxmc_page,want,LXMC_STUB_BYTES);
}
/* Before the first admitted frame, on the MoveWorker; 'mining' is the one region camera group of the Linux gate. */
static int lxmc_arm(void) {
    if(lxmc_installed) return stats.active; /* Identical rerun after a loading-UI reload. */
    if(!stats.active || (boot_mode && boot_phase) || stats.issued || GetCurrentThreadId()!=ui_thread) return 0;
    if(lxmc_state()!=0) {lxmc_stage="entry";return 0;}
    if(!lxmc_pins() || !site_ok(lxmc_site("warp_gtrt"))) {lxmc_stage="pins";return 0;}
    lxmc_failures=0;lxmc_calls=lxmc_near_count=lxmc_foreign=lxmc_latches=0;lxmc_point=lxmc_cluster=NULL;
    if(!lxmc_install()) {lxmc_stage="install";return 0;}
    lxmc_installed=1;lxmc_restored=0;lxmc_stage="installed";return 1;
}
/* Main-thread tick check; any failure stops the gate with reason 166. */
static int lxmc_check(void) {
    const char *why=!lxmc_owned()?"entry_changed":!lxmc_pins()?"block_changed":NULL;
    if(why) {lxmc_stage=why;InterlockedIncrement64(&lxmc_failures);return 0;}
    return 1;
}
static void lxmc_restore(void) {
    if(!lxmc_installed) return;
    int state=lxmc_state();
    if(state==1) {unsigned char from[5];lxmc_entry(from,lxmc_page);if(!lxmc_swap(from,lxmc_original)) return;}
    else if(state!=0) return; /* a foreign value stays */
    lxmc_installed=0;lxmc_restored=1; /* the thunk page stays: a thread may still be inside it */
}
static int lxmc_status(char *out,size_t size) {
    int n=snprintf(out,size,"tick=%llu installed=%d restored=%d groups=mining mining=%lld/%lld/%lld latches=%lld latched=%d "
        "failures=%lld state=%d pin=%d stage=%s",(unsigned long long)stats.issued,lxmc_installed,lxmc_restored,(long long)lxmc_calls,
        (long long)lxmc_near_count,(long long)lxmc_foreign,(long long)lxmc_latches,lxmc_point!=NULL,(long long)lxmc_failures,lxmc_state(),
        lxmc_pins(),lxmc_stage);
    return n>0 && (size_t)n<size?n:0;
}
/* The groups of the Linux gate: mining (here) and shape_refresh (gate_shape.h). */
static int lxsh_rc_arm(void);
static void lxsh_latch(void);
static int lxsh_rc_status(char *out,size_t size);
static int lxsh_rc_armed;
static int lua_region_camera(lua_State *L) {
    size_t length=0;const char *group=p_lua_tolstring(L,1,&length);
    const int ok=group && ((length==6 && !memcmp(group,"mining",6) && lxmc_arm()) || (length==13 && !memcmp(group,"shape_refresh",13) && lxsh_rc_arm()));
    p_lua_pushnumber(L,ok);return 1;
}
static int lua_region_camera_frame(lua_State *L) {
    (void)L;int ok=accept_ui() && (lxmc_installed || lxsh_rc_armed);
    if(ok && lxmc_installed) lxmc_latch();
    if(ok && lxsh_rc_armed) lxsh_latch();
    p_lua_pushnumber(L,ok);return 1;
}
static int lua_region_camera_status(lua_State *L) {
    char out[768];int n=accept_ui()?lxmc_status(out,sizeof(out)):0;
    if(n && lxsh_rc_armed) {out[n++]=' ';const int k=lxsh_rc_status(out+n,sizeof(out)-(size_t)n);n=k?n+k:n-1;}
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
