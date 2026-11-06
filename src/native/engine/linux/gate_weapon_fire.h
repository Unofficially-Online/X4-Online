/* Every weapon shot on its own keyed stream on the Linux X4 9.00 executable: the Linux twin of
 * lockstep_weapon_fire.h (native_gate_weapon_fire_rng), whose domain, per-tick ordinals, counters
 * and status are shared, so a shot draws the same seed on both executables. Included by gate_linux.c only.
 *
 * Linux sites: the shot (Windows U::Weapon::vfunc998 0x8185b0) is 0x1138c70, in slot +0x1fc0
 * (Windows +0x1f30) of U::Weapon 0x3c6a440 and U::Turret 0x3d38dd8 (gate_world_classes.h). As on
 * Windows, U::MissileTurret's slot holds another function. Its only caller is the fire loop
 * 0x1d698b0 (Windows 0x817d20), which loads the slot at 0x1d69a37 and calls it at 0x1d69a75 once
 * per shot; the other calls through +0x1fc0 in the image are another hierarchy's (one argument,
 * a bool result). System V signature, read off the fire loop and the shot's prologue: weapon rdi,
 * a2 rsi, a3 rdx, the shot index rcx, two bools r8d/r9d, the base time xmm0, a float xmm1 and four
 * stack qwords (the fire loop pushes them), the shot time back in xmm0. The Windows twelve
 * integer-class arguments are the same values in MSVC's order. The vtables sit in RELRO: their
 * slots are swapped with the page writable for the moment (gate_data_swap64).
 *
 * Windows restores the word in a __finally; X4's exceptions end the process here, so nothing
 * resumes on that word. Pins: the shot's head and the fire loop's slot call (lx_sites
 * wfire_shot_head, wfire_fire_loop_call), checked at the gate's start and on every gate tick. */
#define WFIRE_OWN_PLATFORM
#include "lockstep_weapon_fire.h"
#define WFIRE_ENTRIES 2u
static const uint32_t wfire_entries[WFIRE_ENTRIES]={LX_WEAPON_VTABLE+LX_WFIRE_SLOT,LX_TURRET_VTABLE+LX_WFIRE_SLOT};
typedef double (*wfire_fn)(void *,void *,void *,uint64_t,uint32_t,uint32_t,double,float,uint64_t,uint64_t,uint64_t,uint64_t);
/* One shot: the keyed scope around the original, as the Windows wfire_run (testable with any original and word). */
static double wfire_run(wfire_fn original,uint64_t *rng,void *weapon,void *a2,void *a3,uint64_t shot,uint32_t a5,uint32_t a6,
                        double base,float a8,uint64_t s1,uint64_t s2,uint64_t s3,uint64_t s4) {
    if(wfire_tick!=stats.issued) {wfire_tick=stats.issued;wfire_frame_keyed=0;}
    uint64_t runtime=0,key=0;unsigned klass=WORLD_KEY_NONE;
    if(read_local((unsigned char*)weapon+8,&runtime,8) && runtime) key=world_identity_key(runtime,&klass);
    if(!key || !rng) {++wfire_unmapped;return original(weapon,a2,a3,shot,a5,a6,base,a8,s1,s2,s3,s4);}
    uint64_t ordinal=wfire_ordinal(key),saved=*rng;
    ++wfire_keyed;++wfire_frame_keyed;
    *rng=lockstep_seed(WFIRE_DOMAIN,stats.issued,key,shot,ordinal,klass);
    double result=original(weapon,a2,a3,shot,a5,a6,base,a8,s1,s2,s3,s4);
    *rng=saved;
    return result;
}
static double wfire_shot(void *weapon,void *a2,void *a3,uint64_t shot,uint32_t a5,uint32_t a6,double base,float a8,
                         uint64_t s1,uint64_t s2,uint64_t s3,uint64_t s4) {
    wfire_fn original=(wfire_fn)(void *)LX(LX_WFIRE_SHOT_VA);
    if(!(stats.active && boot_phase==3 && GetCurrentThreadId()==ui_thread)) {++wfire_native;return original(weapon,a2,a3,shot,a5,a6,base,a8,s1,s2,s3,s4);}
    return wfire_run(original,gate_tls_rng(gate_tls_self()),weapon,a2,a3,shot,a5,a6,base,a8,s1,s2,s3,s4);
}
static const lx_site *wfire_site(const char *name) {
    for(unsigned i=0;i<gate_site_count;i++) if(!strcmp(gate_sites[i].name,name)) return &gate_sites[i];
    return NULL;
}
static int wfire_pins(void) {return site_ok(wfire_site("wfire_shot_head")) && site_ok(wfire_site("wfire_fire_loop_call"));}
static void *wfire_slot(unsigned i) {void *value=NULL;return gate_read_local(LX(wfire_entries[i]),&value,8)?value:NULL;}
static int wfire_swap(unsigned i,void *expected,void *value) {
    return gate_data_swap64(LX(wfire_entries[i]),(uint64_t)(uintptr_t)expected,(uint64_t)(uintptr_t)value);
}
/* Before the first admitted frame, on the MoveWorker; needs the world identities. */
static int wfire_arm(void) {
    void *native=LX(LX_WFIRE_SHOT_VA);
    if(wfire_installed) return stats.active!=0;
    if(!stats.active || !boot_mode || boot_phase || stats.issued || GetCurrentThreadId()!=ui_thread) return 0;
    if(!world_identity_installed) {wfire_stage="needs";return 0;}
    if(!wfire_pins()) {wfire_stage="pins";return 0;}
    for(unsigned i=0;i<WFIRE_ENTRIES;i++) if(wfire_slot(i)!=native) {wfire_stage="entry";return 0;}
    memset(wfire_ordinals,0,sizeof(wfire_ordinals));
    wfire_keyed=wfire_native=wfire_unmapped=wfire_frame_keyed=wfire_table_full=0;wfire_tick=UINT64_MAX;wfire_failures=0;wfire_restored=0;
    for(unsigned i=0;i<WFIRE_ENTRIES;i++) {
        if(!wfire_swap(i,native,(void*)wfire_shot)) {
            for(unsigned j=0;j<i;j++) wfire_swap(j,(void*)wfire_shot,native);
            wfire_swapped=0;wfire_stage="cas";return 0;
        }
        wfire_swapped|=1u<<i;
    }
    wfire_installed=1;wfire_stage="armed";return 1;
}
static int wfire_owned(void) {
    for(unsigned i=0;i<WFIRE_ENTRIES;i++) if((wfire_swapped&(1u<<i)) && wfire_slot(i)!=(void*)wfire_shot) return 0;
    return 1;
}
/* Main-thread tick check; any failure stops the gate with reason 132 (WFIRE_STOP_REASON). */
static int wfire_check(void) {
    if(InterlockedCompareExchange64(&wfire_failures,0,0)) return 0;
    const char *why=!wfire_owned()?"entry_changed":!wfire_pins()?"block_changed":wfire_table_full?"ordinal_table":NULL;
    if(why) {wfire_stage=why;InterlockedIncrement64(&wfire_failures);return 0;}
    return 1;
}
static void wfire_restore(void) {
    if(!wfire_installed) return;
    for(unsigned i=0;i<WFIRE_ENTRIES;i++) if((wfire_swapped&(1u<<i)) && wfire_swap(i,(void*)wfire_shot,LX(LX_WFIRE_SHOT_VA))) wfire_swapped&=~(1u<<i);
    if(!wfire_swapped) {wfire_installed=0;wfire_restored=1;}
}
static int lua_weapon_fire_rng(lua_State *L) {p_lua_pushnumber(L,wfire_arm());return 1;}
static int lua_weapon_fire_rng_status(lua_State *L) {
    char out[512];int n=accept_ui()?wfire_status(out,sizeof(out)):0;
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);
    return 1;
}
