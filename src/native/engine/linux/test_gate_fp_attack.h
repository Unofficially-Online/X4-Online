/* FR29b (gate_fp_attack.h) on the synthetic image: every listed call site holds a call to its callee and each callee a
 * jmp [rip] to a stand-in; arm refusals, the forty-eight rel32 swaps to the thunk's stubs, the scenario of
 * test_fp_attack_scenario.h with the sub-calls made through their real sites (sub rsp,8; call; add rsp,8; ret at the
 * site - 4, so the hooks see the site + 5 as their return address), the rows both gates print, the window and the watch
 * list, ownership and the exact restoration. Included by test_gate_linux.c. */
#include "test_fp_attack_scenario.h"
typedef float (*tlfa_dist_site)(void *,void *,uint64_t,uint64_t,double);
typedef float (*tlfa_rng_site)(float);
typedef uint64_t (*tlfa_dps_site)(void *,void *,uint64_t,uint64_t);
typedef uint64_t (*tlfa_add_site)(void *,void *);
typedef uint64_t (*tlfa_apply_site)(void *,void *,void *,uint64_t,uint64_t,uint64_t);
static void *tlfa_site(uint32_t va) {return (void*)(LX(va)-4);}
static uint64_t tlfa_wl(void *attacker,void *out1,void *out2,void *list4,void *list5,uint64_t a6,double a7,double a8,double a14,double a15,
                        uint64_t target,uint64_t a10,uint64_t a11,uint64_t a12,uint64_t a13,uint64_t a17,uint64_t a18) {
    ++tfa_native[FA_WL];
    assert(attacker==tfa_attacker && (void*)(uintptr_t)target==tfa_target && !list4 && !list5 && a6==0 && a7==-1.0);
    float f;uint64_t d;memcpy(&d,&a8,8);uint32_t lo=(uint32_t)d;memcpy(&f,&lo,4);assert(f==1204.5f);
    memcpy(&d,&a14,8);lo=(uint32_t)d;memcpy(&f,&lo,4);assert(f==150.0f);memcpy(&d,&a15,8);lo=(uint32_t)d;memcpy(&f,&lo,4);assert(f==30.0f);
    assert(a10==1 && a11==0 && a12==1 && a13==0 && a17==0 && a18==1 && out2);
    (void)((tlfa_rng_site)tlfa_site(0x1debbf2u))(150.0f);
    tfa_damage local;tfa_damage_init(&local);
    (void)((tlfa_dps_site)tlfa_site(0x1dec155u))(tfa_weapon,&local,0,1);
    tfa_scale(&local,0.5);
    (void)((tlfa_add_site)tlfa_site(0x1dec25au))(out1,&local);
    return 0;
}
static uint64_t tlfa_gas(void *attacker,void *out1,void *out2,void *list4,void *list5,void *target,uint64_t b7,uint64_t b8,uint64_t b9,uint64_t i10) {
    ++tfa_native[FA_GAS];
    assert(b7==1 && b8==0 && b9==1 && i10==1 && !list4 && !list5);
    const float d=((tlfa_dist_site)tlfa_site(0x1d1bc01u))(target,attacker,0,2,tfa_time);assert(d==1234.5f);
    if(attacker!=tfa_attacker) return 0;
    tfa_damage wl1,wl2;tfa_damage_init(&wl1);tfa_damage_init(&wl2);
    double a7=-1.0,a8=0.0,a14=0.0,a15=0.0;const float fa8=d-30.0f,f150=150.0f,f30=30.0f;
    memcpy(&a8,&fa8,4);memcpy(&a14,&f150,4);memcpy(&a15,&f30,4);
    ((fa_l17_fn)(void*)fa_stub(FA_WL))(attacker,&wl1,&wl2,NULL,NULL,0,a7,a8,a14,a15,(uint64_t)(uintptr_t)target,1,0,1,0,0,1);
    (void)((tlfa_add_site)tlfa_site(0x1d1c5aau))(out1,&wl1);
    (void)out2;return 0;
}
static float tlfa_dist(void *a,void *b,uint64_t c3,uint64_t mode,double time) {
    ++tfa_native[FA_DIST];assert(c3==0 && mode==2 && time==tfa_time && (a==tfa_target || a==tfa_other) && b);return 1234.5f;
}
static float tlfa_rng(float bound) {return tfa_rng_body(&tfa_word,bound);}
static uint64_t tlfa_dps(void *weapon,void *out,uint64_t a3,uint64_t flag) {assert(a3==0 && flag==1);tfa_dps_body(weapon,(tfa_damage*)out);return 0;}
static uint64_t tlfa_add(void *acc,void *addend) {tfa_add_body((tfa_damage*)acc,(const tfa_damage*)addend);return (uint64_t)(uintptr_t)acc;}
static uint64_t tlfa_apply(void *target,void *attacker,void *damage,uint64_t p4,uint64_t p5,uint64_t flag) {
    ++tfa_native[FA_APPLY];assert(target==tfa_target && attacker==tfa_attacker && damage && !p4 && !p5 && flag==1);return 0x101; /* al = 1 */
}
static void tlfa_call_code(uint32_t site_va,uint32_t target_va) { /* the e8 rel32 alone */
    unsigned char call[5]={0xe8};const int32_t rel=(int32_t)((intptr_t)LX(target_va)-(intptr_t)(LX(site_va)+5));memcpy(call+1,&rel,4);
    tl_write(LX(site_va),call,5);
}
static void test_fa_linux(void) {
    for(unsigned i=0;i<FA_L_SITES;i++) {
        assert(((fa_l_sites[i].site+1u)&15u)<=12u && fa_l_sites[i].kind>=FA_GAS && fa_l_sites[i].kind<FA_KINDS);
        for(unsigned j=0;j<i;j++) assert(fa_l_sites[j].site+5u<=fa_l_sites[i].site || fa_l_sites[i].site+5u<=fa_l_sites[j].site);
        tlfa_call_code(fa_l_sites[i].site,fa_l_sites[i].target);
    }
    static const uint32_t executed[6]={0x1d1bc01u,0x1debbf2u,0x1dec155u,0x1dec25au,0x1d1c5aau,0xdfc994u};
    for(unsigned k=0;k<6;k++) for(unsigned i=0;i<FA_L_SITES;i++) if(fa_l_sites[i].site==executed[k]) tlf_site_code(executed[k],fa_l_sites[i].target);
    tlf_jump_code(0x1d1b9d0u,(void*)tlfa_gas);tlf_jump_code(0x1deb960u,(void*)tlfa_wl);tlf_jump_code(0xf05ff0u,(void*)tlfa_dps);
    tlf_jump_code(0xf05be0u,(void*)tlfa_add);tlf_jump_code(0x1a491c0u,(void*)tlfa_rng);tlf_jump_code(0x1dea820u,(void*)tlfa_dist);
    tlf_jump_code(0x17f8d90u,(void*)tlfa_apply);
    for(unsigned i=0;i<FA_L_SITES;i++) assert(fa_site_state(i)==0);
    reset_session();stats.active=1;boot_mode=1;boot_phase=0;stats.issued=0;ui_thread=GetCurrentThreadId();
    thunk=make_thunk();assert(thunk);
    for(unsigned k=FA_GAS;k<FA_KINDS;k++) {void *t;memcpy(&t,thunk+LXF_FA_STUB+0x20u*(k-1u)+6,8);assert(!memcmp(thunk+LXF_FA_STUB+0x20u*(k-1u),"\xff\x25\0\0\0\0",6) && t==fa_hook_of(k));}
    world_identities=calloc(WORLD_IDENTITY_CAPACITY,sizeof(*world_identities));assert(world_identities);world_identity_installed=1;
    world_identity_count=0;world_identity_sealed=0;
    assert(world_identity_record(0x501,0x96587) && world_identity_record(0x502,0x7336a) && world_identity_record(0x503,0x73371));
    tfa_objects();
    /* refusals */
    fr_installed=0;fa_id_count=0;assert(!fa_arm(540,550) && !strcmp(fa_stage,"refused"));
    fr_installed=1;assert(!fa_arm(0,550) && !strcmp(fa_stage,"window") && !fa_arm(551,550) && !strcmp(fa_stage,"window"));
    world_identity_installed=0;assert(!fa_arm(540,550) && !strcmp(fa_stage,"needs"));world_identity_installed=1;
    assert(fa_watch(0x7336a) && fa_watch(0x7336a) && !fa_watch(0) && fa_id_count==1);
    assert(fa_arm(540,550) && fa_installed && !strcmp(fa_stage,"armed") && fa_arm(540,550) && !fa_arm(1,2) && !fa_watch(0x96587));
    for(unsigned i=0;i<FA_L_SITES;i++) assert(fa_site_state(i)==1);
    assert(fa_owned() && fa_native_gas==(fa_l10_fn)(void*)LX(0x1d1b9d0u) && fa_sites_hooked==FA_L_SITES && fa_sites_skipped==FA_L_SKIPPED);
    /* the scenario */
    stats.issued=546;
    tfa_damage out1,out2,dmg;tfa_damage_init(&out1);tfa_damage_init(&out2);
    const fa_l10_fn gas=(fa_l10_fn)(void*)fa_stub(FA_GAS);
    gas(tfa_attacker,&out1,&out2,NULL,NULL,tfa_target,1,0,1,1);
    assert(tfa_native[FA_GAS]==1 && tfa_native[FA_WL]==1 && tfa_native[FA_DPS]==1 && tfa_native[FA_ADD]==2 && tfa_native[FA_RNG]==1 && tfa_native[FA_DIST]==1);
    dmg=out1;assert(dmg.v[0]==5.25 && dmg.v[1]==10.125 && dmg.v[4]==15.375);
    assert((((tlfa_apply_site)tlfa_site(0xdfc994u))(tfa_target,tfa_attacker,&dmg,0,0,1)&0xffu)==1);
    /* outside the window, then not watched: native once each, nothing kept */
    stats.issued=600;gas(tfa_attacker,&out1,&out2,NULL,NULL,tfa_target,1,0,1,1);
    stats.issued=546;gas(tfa_other,&out1,&out2,NULL,NULL,tfa_other,1,0,1,1);
    assert(tfa_native[FA_GAS]==3 && tfa_native[FA_WL]==2 && tfa_native[FA_DIST]==3 && tfa_native[FA_APPLY]==1);
    static const char *const scopes[TFA_ROWS]={"gas","wl","wl","wl","gas","gas","exec","apply"};
    tfa_check_rows(fa_row,scopes);
    char out[1024];assert(fa_status(out,sizeof(out))>0);
    assert(strstr(out,"installed=1 restored=0 stage=armed window=540..550 ids=1 sites=48 skipped=8 captured=1 nested=0 no_context=0 outside=1 unread=0 open=0 records=8 dropped=0 rows=8"));
    assert(strstr(out," hits=gas:3,wl:2,dps:2,add:4,rng:2,dist:3,apply:1 kept=gas:1,wl:1,dps:1,add:2,rng:1,dist:1,apply:1"));
    /* ownership through fr_owned, then the restoration (fr_restore takes these sites back first) */
    lxf_done=0;lxf_sp_entry_done=0;fsp_installed=0;
    assert(fr_owned());
    unsigned char *site=LX(fa_l_sites[3].site),*line=(unsigned char*)((uintptr_t)(site+1)&~(uintptr_t)15);
    unsigned __int128 l0,l1;memcpy(&l0,line,16);l1=l0^((unsigned __int128)1<<(8*(unsigned)(site+2-line)));
    assert(gate_code_swap128(line,l0,l1) && !fa_owned() && !fr_owned() && gate_code_swap128(line,l1,l0) && fa_owned() && fr_owned());
    fr_restore();assert(!fa_installed && fa_restored && !fa_done && !strcmp(fa_stage,"restored") && !fr_installed);
    for(unsigned i=0;i<FA_L_SITES;i++) assert(fa_site_state(i)==0);
    fr_restored=0;fr_stage="idle";fr_mode=FR_OFF;fa_restored=0;fa_stage="idle";fa_id_count=0;fa_test_rng=NULL;fa_test_time=NULL;
    world_identity_installed=0;world_identity_count=0;
    printf("fp attack FR29b: 48 sites swapped to the stubs and back, the scenario through the real sites (scopes from the return addresses), rows equal to the shared text, window/watch, status, ownership PASS\n");
}
/* The executable's bytes: every listed site a call to its callee inside one 16-byte line, the eight skipped ones calls
 * whose rel32 crosses a line, the callees' first bytes as the hooks' argument layout assumes them, then the arm on these
 * bytes (only the rel32s change) and the exact restoration. */
static void test_fa_elf(void) {
    static const uint32_t skipped[FA_L_SKIPPED][2]={{0x1d1c5feu,0xf05be0u},{0x1d1ca4du,0xf05be0u},{0x1deda7cu,0xf05be0u},{0x1d1e0edu,0x1a491c0u},
        {0x1dec03du,0x1a491c0u},{0x1ded2fcu,0x1a491c0u},{0x1ded5ceu,0x1a491c0u},{0x1dedd5eu,0x1a491c0u}};
    for(unsigned k=0;k<FA_L_SKIPPED;k++) {
        const unsigned char *s=LX(skipped[k][0]);int32_t d;memcpy(&d,s+1,4);
        assert(s[0]==0xe8 && s+5+d==LX(skipped[k][1]) && ((skipped[k][0]+1u)&15u)>12u);
    }
    for(unsigned i=0;i<FA_L_SITES;i++) assert(fa_site_state(i)==0 && ((fa_l_sites[i].site+1u)&15u)<=12u);
    assert(!memcmp(LX(0x1a491c0u),"\x0f\x28\xc8\x66\x0f\xef\xc0",7));                    /* FloatState: movaps xmm1,xmm0 (the bound) */
    assert(!memcmp(LX(0x1deb9d8u),"\xf2\x0f\x11\x44\x24\x58\xf3\x0f\x11\x4c\x24\x78\xf3\x0f\x11\x14\x24\xf3\x0f\x11\x5c\x24\x7c",23)); /* the loop: xmm0..3 */
    assert(!memcmp(LX(0x1d1b9e1u),"\x8b\x84\x24\x10\x06\x00\x00\x8b\xac\x24\x20\x06\x00\x00",14)); /* GetAttackStrength: b7, b9 from the stack */
    assert(!memcmp(LX(0x17f8d92u),"\x66\x0f\xef\xc0\x66\x0f\x2f\x42\x28",9));                /* the apply's damage: the struct in rdx */
    assert(!memcmp(LX(0x1d1c482u),"\xf2\x0f\x10\x84\x24\xe8\x00\x00\x00",9));               /* the loop's call: a7 in xmm0 */
    unsigned char saved[FA_L_SITES][16];
    for(unsigned i=0;i<FA_L_SITES;i++) memcpy(saved[i],(const void*)((uintptr_t)LX(fa_l_sites[i].site+1u)&~(uintptr_t)15),16);
    reset_session();stats.active=1;boot_mode=1;boot_phase=0;stats.issued=0;ui_thread=GetCurrentThreadId();
    thunk=make_thunk();assert(thunk);
    world_identities=calloc(WORLD_IDENTITY_CAPACITY,sizeof(*world_identities));assert(world_identities);world_identity_installed=1;
    world_identity_count=0;world_identity_sealed=0;fr_installed=1;fa_id_count=0;
    assert(fa_watch(0x7336a) && fa_arm(1,600) && fa_owned());
    for(unsigned i=0;i<FA_L_SITES;i++) {
        unsigned char now[16];const unsigned at=(fa_l_sites[i].site+1u)&15u;
        memcpy(now,(const void*)((uintptr_t)LX(fa_l_sites[i].site+1u)&~(uintptr_t)15),16);
        assert(fa_site_state(i)==1 && !memcmp(now,saved[i],at) && !memcmp(now+at+4,saved[i]+at+4,12-at));
    }
    lxf_done=0;lxf_sp_entry_done=0;fsp_installed=0;
    fr_restore();assert(!fa_installed && !fr_installed);
    for(unsigned i=0;i<FA_L_SITES;i++) assert(!memcmp((const void*)((uintptr_t)LX(fa_l_sites[i].site+1u)&~(uintptr_t)15),saved[i],16));
    fr_restored=0;fr_stage="idle";fr_mode=FR_OFF;fa_restored=0;fa_stage="idle";fa_id_count=0;world_identity_installed=0;
    printf("fp attack FR29b on the executable: 48 call sites (and 8 crossing a line, skipped), the callees' argument bytes, armed and restored exactly PASS\n");
}
