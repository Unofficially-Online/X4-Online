/* Weapon shots on keyed streams (gate_weapon_fire.h): the keyed scope on a stand-in shot, and on the
 * real executable the pins, the two slots, the swap and the restore. Included by test_gate_linux.c. */
static struct {void *weapon,*a2,*a3;uint64_t shot;uint32_t a5,a6;double base;float a8;uint64_t s[4],word;unsigned calls;} twf;
static double twf_original(void *weapon,void *a2,void *a3,uint64_t shot,uint32_t a5,uint32_t a6,double base,float a8,
                           uint64_t s1,uint64_t s2,uint64_t s3,uint64_t s4) {
    twf.weapon=weapon;twf.a2=a2;twf.a3=a3;twf.shot=shot;twf.a5=a5;twf.a6=a6;twf.base=base;twf.a8=a8;
    twf.s[0]=s1;twf.s[1]=s2;twf.s[2]=s3;twf.s[3]=s4;twf.word=*gate_tls_rng(gate_tls_self());++twf.calls;
    *gate_tls_rng(gate_tls_self())^=0x5a5a; /* the shot's own draws move the word */
    return base+(double)shot;
}
static double twf_call(void *weapon,uint64_t shot) {
    return wfire_run(twf_original,gate_tls_rng(gate_tls_self()),weapon,(void*)0x1111,(void*)0x2222,shot,1u,0u,7.25,0.5f,
                     0xa1,0xa2,0xa3,0xa4);
}
static void test_weapon_fire_linux(void) {
    reset_session();stats.active=1;boot_mode=1;boot_phase=3;stats.issued=40;ui_thread=GetCurrentThreadId();
    world_identities=calloc(WORLD_IDENTITY_CAPACITY,sizeof(*world_identities));assert(world_identities);world_identity_installed=1;
    world_identity_count=0;world_identity_sealed=0;assert(world_identity_record(9101,0x301));
    unsigned char mapped[16]={0},unmapped[16]={0};uint64_t runtime=9101;memcpy(mapped+8,&runtime,8);runtime=9999;memcpy(unmapped+8,&runtime,8);
    memset(wfire_ordinals,0,sizeof(wfire_ordinals));wfire_keyed=wfire_unmapped=wfire_native=0;wfire_tick=UINT64_MAX;
    uint64_t *word=gate_tls_rng(gate_tls_self());
    /* Every argument reaches the original as passed: the two bools, the double, the float and the four stack qwords. */
    *word=0x1234;twf.calls=0;assert(twf_call(unmapped,3)==10.25 && twf.calls==1);
    assert(twf.weapon==unmapped && twf.a2==(void*)0x1111 && twf.a3==(void*)0x2222 && twf.shot==3 && twf.a5==1 && twf.a6==0);
    assert(twf.base==7.25 && twf.a8==0.5f && twf.s[0]==0xa1 && twf.s[1]==0xa2 && twf.s[2]==0xa3 && twf.s[3]==0xa4);
    /* No portable key: the natural word, and the shot's draws stay on it. */
    assert(twf.word==0x1234 && *word==(0x1234^0x5a5a) && wfire_unmapped==1 && !wfire_keyed);
    /* A saved weapon: the word is (tick, key, shot, ordinal, class) during the shot, the caller's afterwards. */
    *word=0x7777;twf_call(mapped,3);
    assert(twf.word==lockstep_seed(WFIRE_DOMAIN,40,0x301,3,1,WORLD_KEY_SAVED) && *word==0x7777 && wfire_keyed==1);
    /* The same weapon again in this tick: ordinal 2; whatever the caller's word was, the seed is the same. */
    *word=0x8888;twf_call(mapped,3);assert(twf.word==lockstep_seed(WFIRE_DOMAIN,40,0x301,3,2,WORLD_KEY_SAVED) && *word==0x8888);
    /* Next tick: ordinals restart at 1. */
    stats.issued=41;*word=0x9999;twf_call(mapped,3);assert(twf.word==lockstep_seed(WFIRE_DOMAIN,41,0x301,3,1,WORLD_KEY_SAVED));
    /* Outside an admitted frame the hook calls natively (the stand-in is the original here). */
    char out[512];assert(wfire_status(out,sizeof(out)) && strstr(out,"total_keyed=3") && strstr(out,"unmapped=1"));
    world_identity_installed=0;world_identity_count=0;free(world_identities);world_identities=NULL;
    printf("weapon fire PASS\n");
}
/* The real executable: the pins hold, both slots hold the shot, the swap takes both and the restore gives them back.
 * The file's slots hold the shot's link address (ET_EXEC: the process image sits at LX_IMAGE_BASE, so LX(va)==va there);
 * this test's copy of the image is elsewhere, so the two words are relocated into it first, as fake_data does. */
static void test_weapon_fire_elf(void) {
    void *native=LX(LX_WFIRE_SHOT_VA);
    assert(wfire_pins() && wfire_slot(0)==(void*)(uintptr_t)LX_WFIRE_SHOT_VA && wfire_slot(1)==(void*)(uintptr_t)LX_WFIRE_SHOT_VA);
    for(unsigned i=0;i<WFIRE_ENTRIES;i++) put64(LX(wfire_entries[i]),(uint64_t)(uintptr_t)native);
    assert(wfire_slot(0)==native && wfire_slot(1)==native);
    reset_session();stats.active=1;boot_mode=1;boot_phase=0;stats.issued=0;ui_thread=GetCurrentThreadId();
    world_identity_installed=0;assert(!wfire_arm() && !strcmp(wfire_stage,"needs"));
    world_identity_installed=1;
    assert(wfire_arm() && wfire_installed && wfire_slot(0)==(void*)wfire_shot && wfire_slot(1)==(void*)wfire_shot && wfire_owned());
    assert(wfire_check());
    wfire_restore();assert(!wfire_installed && wfire_restored && wfire_slot(0)==native && wfire_slot(1)==native);
    world_identity_installed=0;
    printf("weapon fire ELF PASS\n");
}
