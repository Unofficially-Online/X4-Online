/* native_gate_body_step_diag (Linux gate only, diagnostic, default off): the fixture ships' two physics bodies just
 * before and just after Jolt's step, logged, never hashed. Included by gate_linux.c after gate_battle.h.
 *
 * The frame-end body text (gate.body) shows a body after the step, with the step's force and torque already cleared;
 * a split that first appears there (the battle's tick 28 against Windows: the steering bank equal on both, the body's
 * velocities a few ulps apart) can come from what X4 wrote into the body before the step or from the step itself. This
 * hook keeps both sides: JPH::PhysicsSystem::Update (0x1b1ce30, called once per scene update from 0xeecacb) gets a
 * wrapper, and for every fixture ship whose record belongs to that system it copies each body's first 0x80 bytes
 * (pose, rotation, shape, motion pointer, user data, meta) and its MotionProperties (0x80: velocities, inverse inertia,
 * its rotation, the accumulated force and torque, inverse mass, damping, limits, flags) before the call and after it,
 * with the step's delta time and collision steps. gate.body_step(actor, which) returns the last pair and how many
 * steps ran since the previous read.
 *
 * Update's first 16 bytes (push r15; push r14; push r13; mov r13,rcx; push r12; push rbp; push rbx; mov rbx,rdi:
 * whole instructions, nothing RIP-relative) become jmp [rip] -> lxbs_update and two int3 in one aligned 16-byte
 * compare-exchange; the original runs through a trampoline (the 16 bytes, then jmp [rip] -> 0x1b1ce40). Pinned
 * (physics_update); the per-tick check (stop reason 205) and the restore are this file's. Windows needs nothing. */
#define LXBS_STOP_REASON 205u
#define LXBS_UPDATE_VA 0x1b1ce30u
typedef uint32_t (*lxbs_update_fn)(void *system,float dt,int steps,void *temp,void *jobs);
static const unsigned char lxbs_prologue[16]={0x41,0x57,0x41,0x56,0x41,0x55,0x49,0x89,0xcd,0x41,0x54,0x55,0x53,0x48,0x89,0xfb};
static unsigned char lxbs_original[16],*lxbs_page;
static int lxbs_installed,lxbs_restored;
static volatile int64_t lxbs_updates,lxbs_captured,lxbs_failures;
static const char *lxbs_stage="idle";
static lxbs_update_fn lxbs_original_override; /* tests */
typedef struct {uint32_t steps,dt,collision;int valid;uint64_t seq[2];unsigned char body[2][0x80],motion[2][0x80];} lxbs_sample;
static lxbs_sample lxbs_samples[LXB_ACTORS][2];
/* One order for the step captures here and the velocity rows (gate_velocity.h): every capture takes the next number. */
static volatile uint64_t lx_phys_seq;
/* A fixture ship's body and its motion properties, through its record (none when unbound or unreadable). */
static unsigned char *lxbs_find(unsigned actor,unsigned which,unsigned char **motion_out) {
    if(actor<1 || actor>LXB_ACTORS || which>1 || lxb_bound!=LXB_ACTORS) return NULL;
    const lxb_actor *a=&lxb_actors[actor-1];unsigned slot=lxb_body_slot[actor-1];
    unsigned char *record=NULL,*xbody=NULL,*scene=NULL,*system=NULL,*iface=NULL,*manager=NULL,*bodies=NULL,*body=NULL,*motion=NULL;
    void *owner=NULL;uint32_t id=0;uint64_t count=0;
    if(!a->object || !slot || !gate_read_local((unsigned char*)a->object+slot+which*8u,&record,8) || !record ||
       !gate_read_local(record,&owner,8) || owner!=a->object || !gate_read_local(record+0x18,&scene,8) || !scene ||
       !gate_read_local(scene+0x20,&system,8) || !system || !gate_read_local(record+0x10,&xbody,8) || !xbody ||
       !gate_read_local(xbody+8,&id,4) || id==0xffffffffu || !gate_read_local(system+LXB_PHYS_LOCK_IFACE,&iface,8) || !iface ||
       !gate_read_local(iface+8,&manager,8) || !manager || !gate_read_local(manager,&count,8) || (id&0x7fffffu)>=count || count>0x800000u ||
       !gate_read_local(manager+0x10,&bodies,8) || !bodies || !gate_read_local(bodies+(id&0x7fffffu)*8u,&body,8) || !body ||
       ((uintptr_t)body&1u) || !gate_read_local(body+0x48,&motion,8) || !motion) return NULL;
    if(motion_out) *motion_out=motion;
    return body;
}
/* One phase (0 before, 1 after) of every fixture ship body that belongs to this physics system. */
static void lxbs_capture(void *system,unsigned phase,float dt,int collision) {
    if(lxb_bound!=LXB_ACTORS) return;
    for(unsigned actor=1;actor<=LXB_ACTORS;actor++) {
        const lxb_actor *a=&lxb_actors[actor-1];unsigned slot=lxb_body_slot[actor-1];
        if(!a->object || !slot) continue;
        for(unsigned which=0;which<2;which++) {
            lxbs_sample *s=&lxbs_samples[actor-1][which];
            unsigned char *record=NULL,*xbody=NULL,*scene=NULL,*owner_system=NULL,*iface=NULL,*manager=NULL,*bodies=NULL,*body=NULL,*motion=NULL;
            void *owner=NULL;uint32_t id=0;uint64_t count=0;
            if(!gate_read_local((unsigned char*)a->object+slot+which*8u,&record,8) || !record) continue;
            if(!gate_read_local(record,&owner,8) || owner!=a->object || !gate_read_local(record+0x18,&scene,8) || !scene ||
               !gate_read_local(scene+0x20,&owner_system,8) || owner_system!=(unsigned char*)system) continue;
            if(!gate_read_local(record+0x10,&xbody,8) || !xbody || !gate_read_local(xbody+8,&id,4) || id==0xffffffffu ||
               !gate_read_local(owner_system+LXB_PHYS_LOCK_IFACE,&iface,8) || !iface || !gate_read_local(iface+8,&manager,8) || !manager ||
               !gate_read_local(manager,&count,8) || (id&0x7fffffu)>=count || count>0x800000u ||
               !gate_read_local(manager+0x10,&bodies,8) || !bodies || !gate_read_local(bodies+(id&0x7fffffu)*8u,&body,8) || !body ||
               ((uintptr_t)body&1u) || !gate_read_local(body+0x48,&motion,8) || !motion ||
               !gate_read_local(body,s->body[phase],0x80) || !gate_read_local(motion,s->motion[phase],0x80)) {
                __atomic_add_fetch(&lxbs_failures,1,__ATOMIC_SEQ_CST);continue;
            }
            s->seq[phase]=__atomic_add_fetch(&lx_phys_seq,1,__ATOMIC_SEQ_CST);
            if(phase==0) {s->valid=0;memcpy(&s->dt,&dt,4);s->collision=(uint32_t)collision;}
            else {s->valid=1;s->steps++;__atomic_add_fetch(&lxbs_captured,1,__ATOMIC_SEQ_CST);}
        }
    }
}
static uint32_t lxbs_update(void *system,float dt,int steps,void *temp,void *jobs) {
    lxbs_update_fn original=lxbs_original_override?lxbs_original_override:(lxbs_update_fn)(void*)lxbs_page;
    __atomic_add_fetch(&lxbs_updates,1,__ATOMIC_SEQ_CST);
    lxbs_capture(system,0,dt,steps);
    uint32_t result=original(system,dt,steps,temp,jobs);
    lxbs_capture(system,1,dt,steps);
    return result;
}
static void lxbs_entry(unsigned char out[16]) {
    const void *to=(const void*)lxbs_update;
    out[0]=0xff;out[1]=0x25;memset(out+2,0,4);memcpy(out+6,&to,8);out[14]=0xcc;out[15]=0xcc;
}
static int lxbs_state(void) { /* 0 native, 1 ours, -1 anything else */
    unsigned char b[16],want[16];
    if(!gate_read_local(LX(LXBS_UPDATE_VA),b,16)) return -1;
    if(!memcmp(b,lxbs_prologue,16)) return 0;
    if(lxbs_page) {lxbs_entry(want);if(!memcmp(b,want,16)) return 1;}
    return -1;
}
static int lxbs_pins(void) {
    const lx_site *u=site_named("physics_update");unsigned char copy[0x40];
    if(!u || u->va!=LXBS_UPDATE_VA || u->length>sizeof(copy) || lxbs_state()<0 || !gate_read_local(LX(u->va),copy,u->length)) return 0;
    if(lxbs_installed) memcpy(copy,lxbs_original,16);
    return hash_ok(copy,u->length,u->sha256);
}
static int lxbs_swap(const unsigned char from[16],const unsigned char to[16]) {
    unsigned __int128 expected,desired;memcpy(&expected,from,16);memcpy(&desired,to,16);
    return gate_code_swap128(LX(LXBS_UPDATE_VA),expected,desired);
}
static int lxbs_install(void) {
    if(!gate_read_local(LX(LXBS_UPDATE_VA),lxbs_original,16) || memcmp(lxbs_original,lxbs_prologue,16)) return 0;
    if(!lxbs_page) lxbs_page=gate_near_page(LX(LXBS_UPDATE_VA));
    if(!lxbs_page || mprotect(lxbs_page,GATE_PAGE,PROT_READ|PROT_WRITE)) return 0;
    memset(lxbs_page,0xcc,GATE_PAGE);memcpy(lxbs_page,lxbs_original,16);
    {const void *back=LX(LXBS_UPDATE_VA+16u);lxbs_page[16]=0xff;lxbs_page[17]=0x25;memset(lxbs_page+18,0,4);memcpy(lxbs_page+22,&back,8);}
    if(mprotect(lxbs_page,GATE_PAGE,PROT_READ|PROT_EXEC)) return 0;
    unsigned char to[16];lxbs_entry(to);
    return lxbs_swap(lxbs_original,to);
}
/* Before the snapshot load, on the MoveWorker (the capture waits for the fixture's binding). */
static int lxbs_arm(void) {
    if(lxbs_installed) return stats.active;
    if(!stats.active || (boot_mode && boot_phase) || stats.issued || GetCurrentThreadId()!=ui_thread) {lxbs_stage="guard";return 0;}
    if(lxbs_state()!=0) {lxbs_stage="entry";return 0;}
    if(!lxbs_pins()) {lxbs_stage="pins";return 0;}
    lxbs_updates=lxbs_captured=lxbs_failures=0;memset(lxbs_samples,0,sizeof(lxbs_samples));
    if(!lxbs_install()) {lxbs_stage="install";return 0;}
    lxbs_installed=1;lxbs_restored=0;lxbs_stage="armed";return 1;
}
/* Main-thread tick check: the entry and the pinned block; any failure stops the gate with reason 205. */
static int lxbs_check(void) {
    const char *why=lxbs_state()!=1?"entry_changed":!lxbs_pins()?"block_changed":NULL;
    if(why) {lxbs_stage=why;return 0;}
    return 1;
}
static void lxbs_restore(void) {
    if(!lxbs_installed) return;
    int state=lxbs_state();
    if(state==1) {unsigned char from[16];lxbs_entry(from);if(!lxbs_swap(from,lxbs_original)) return;}
    else if(state!=0) return; /* a foreign value stays */
    lxbs_installed=0;lxbs_restored=1; /* the trampoline stays: a thread may still be inside it */
}
static int lxbs_status(char *out,size_t size) {
    int n=snprintf(out,size,"tick=%llu installed=%d restored=%d updates=%lld captured=%lld failures=%lld state=%d pin=%d stage=%s",
        (unsigned long long)stats.issued,lxbs_installed,lxbs_restored,(long long)lxbs_updates,(long long)lxbs_captured,
        (long long)lxbs_failures,lxbs_state(),lxbs_pins(),lxbs_stage);
    return n>0 && (size_t)n<size?n:0;
}
/* "steps=N dt=XXXXXXXX collision=C body0=<32 words> motion0=<32 words> body1=... motion1=..." (0 before the step, 1
 * after); the step count since the previous read is cleared. */
static int lxbs_text(unsigned actor,unsigned which,char *out,size_t cap) {
    if(actor<1 || actor>LXB_ACTORS || which>1 || cap<2200) return 0;
    lxbs_sample *s=&lxbs_samples[actor-1][which];
    if(!s->valid) return 0;
    int n=snprintf(out,cap,"steps=%u dt=%08x collision=%u seq0=%llu seq1=%llu",s->steps,s->dt,s->collision,
        (unsigned long long)s->seq[0],(unsigned long long)s->seq[1]);
    for(unsigned phase=0;phase<2;phase++) {
        uint32_t w[32];
        memcpy(w,s->body[phase],0x80);n+=snprintf(out+n,cap-(size_t)n," body%u=",phase);n+=lxb_hex_words(out+n,cap-(size_t)n,w,32);
        memcpy(w,s->motion[phase],0x80);n+=snprintf(out+n,cap-(size_t)n," motion%u=",phase);n+=lxb_hex_words(out+n,cap-(size_t)n,w,32);
    }
    s->steps=0;
    return n>0 && (size_t)n<cap?n:0;
}
static int lua_body_step_diag(lua_State *L) {p_lua_pushnumber(L,lxbs_arm());return 1;}
static int lua_body_step_status(lua_State *L) {
    char out[256];int n=accept_ui()?lxbs_status(out,sizeof(out)):0;
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
static int lua_body_step(lua_State *L) {
    double actor=p_lua_tonumber(L,1),which=p_lua_tonumber(L,2);char out[2400];int n=0;
    if(accept_ui() && lxbs_installed && isfinite(actor) && floor(actor)==actor && isfinite(which) && floor(which)==which && actor>=1 && actor<=LXB_ACTORS &&
       which>=0 && which<=1) n=lxbs_text((unsigned)actor,(unsigned)which,out,sizeof(out));
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
