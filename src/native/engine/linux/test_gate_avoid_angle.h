/* The obstacle avoidance's angle scaling in the Windows order (gate_avoid_angle.h): x / pi against x * (1 / pi) (why
 * the order matters, and the folded constants are the rounded reciprocals); on the real executable the run's bytes, the
 * pin, the entry installed (jmp rel32 to the stub: both products by the page's constants, the jump back) and restored.
 * Included by test_gate_linux.c. */
/* Synthetic code pages exercise real CAS/mprotect without executing engine code. */
static int lxav_fail_close;
static int lxav_test_protection(void *at,int open) {
    if(!open && lxav_fail_close) return 0;
    return open?gate_code_open(at,16):gate_code_close(at);
}
static void test_avoid_angle_cleanup(void) {
    unsigned char saved[sizeof(lxav_original)],to[5],foreign[5]={0x90,0x91,0x92,0x93,0x94};char perms[5];
    assert(!lxav_installed && !lxav_page && !lxav_rx_pending);
    memcpy(saved,LX(LXAV_VA),sizeof(saved));tl_write(LX(LXAV_VA),lxav_original,sizeof(saved));
    lxav_page=gate_near_page(LX(LXAV_VA));assert(lxav_page && lxav_code(lxav_page,lxav_page));
    assert(!mprotect(lxav_page,GATE_PAGE,PROT_READ|PROT_EXEC) && lxav_patch(to));
    lxav_test_protect=lxav_test_protection;lxav_fail_close=1;
    assert(!lxav_swap(lxav_original,to));
    assert(lxav_installed && !lxav_restored && lxav_rx_pending && lxav_state()==1);
    perms_of(LX(LXAV_VA),perms);assert(!strcmp(perms,"rwxp"));
    {const int active=stats.active;stats.active=1;assert(!lxav_arm());stats.active=active;}
    assert(!lxav_check());
    lxav_restore(); /* bytes restored, protection still owed */
    assert(lxav_installed && !lxav_restored && lxav_rx_pending && lxav_state()==0);
    perms_of(LX(LXAV_VA),perms);assert(!strcmp(perms,"rwxp"));
    lxav_fail_close=0;lxav_restore();
    assert(!lxav_installed && lxav_restored && !lxav_rx_pending);
    perms_of(LX(LXAV_VA),perms);assert(!strcmp(perms,"r-xp"));
    /* A foreign entry is preserved and prevents a clean-restoration claim. */
    assert(lxav_swap(lxav_original,to));tl_write(LX(LXAV_VA),foreign,5);
    lxav_restore();assert(lxav_installed && !lxav_restored && !memcmp(LX(LXAV_VA),foreign,5));
    tl_write(LX(LXAV_VA),to,5);lxav_restore();assert(!lxav_installed && lxav_restored);
    /* Native bytes supplied externally still require a successful RX transition. */
    assert(lxav_swap(lxav_original,to));tl_write(LX(LXAV_VA),lxav_original,sizeof(saved));
    assert(!mprotect((void*)((uintptr_t)LX(LXAV_VA)&~(uintptr_t)(GATE_PAGE-1)),GATE_PAGE,PROT_READ|PROT_WRITE|PROT_EXEC));
    lxav_restore();assert(!lxav_installed && lxav_restored);
    perms_of(LX(LXAV_VA),perms);assert(!strcmp(perms,"r-xp"));
    /* Exact first-five-byte restoration is insufficient if the instruction tail changed. */
    assert(lxav_swap(lxav_original,to));unsigned char tail=lxav_original[5]^0x40;
    tl_write(LX(LXAV_VA)+5,&tail,1);lxav_restore();
    assert(lxav_installed && !lxav_restored && lxav_state()==0 && LX(LXAV_VA)[5]==tail);
    tl_write(LX(LXAV_VA)+5,lxav_original+5,1);lxav_restore();assert(!lxav_installed && lxav_restored);
    tl_write(LX(LXAV_VA),saved,sizeof(saved));munmap(lxav_page,GATE_PAGE);lxav_page=NULL;
    lxav_test_protect=NULL;lxav_restored=0;lxav_stage="idle";
    printf("avoid_angle cleanup: post-CAS close failure, retry, foreign entry and exact instruction restoration PASS\n");
}
static void test_avoid_angle_linux(void) {
    test_avoid_angle_cleanup();
    const float pi=3.14159274f,half=1.57079637f;unsigned apart=0;
    {volatile float a=1.0f/pi,b=1.0f/half;uint32_t u;memcpy(&u,(const void*)&a,4);assert(u==0x3ea2f983u);memcpy(&u,(const void*)&b,4);assert(u==0x3f22f983u);}
    for(unsigned i=1;i<=1000;i++) {volatile float x=-(float)i*0.00157f,r=1.0f/half;if(x/half!=x*r) apart++;}
    assert(apart>50);
    printf("avoid angle: x / (pi/2) against x * (2/pi) apart in %u of 1000 PASS\n",apart);
}
static void test_avoid_angle_elf(void) {
    assert(!memcmp(LX(LXAV_VA),lxav_original,sizeof(lxav_original)));
    {uint32_t k;memcpy(&k,LX(0x2e27154u),4);assert(k==0x40490fdbu);memcpy(&k,LX(0x2e27174u),4);assert(k==0x3fc90fdbu);} /* pi, pi/2: the divisors */
    reset_session();stats.active=1;boot_mode=1;boot_phase=0;stats.issued=0;ui_thread=GetCurrentThreadId();lxav_installed=lxav_restored=0;
    assert(lxav_pins() && lxav_state()==0 && lxav_arm() && lxav_installed && lxav_state()==1 && lxav_check() && lxav_pins() && lxav_page_ok());
    {int32_t r;const unsigned char *s=lxav_page;uint32_t k;
     assert(LX(LXAV_VA)[0]==0xe9);memcpy(&r,LX(LXAV_VA)+1,4);assert(LX(LXAV_VA)+5+r==lxav_page && !memcmp(LX(LXAV_VA)+5,lxav_original+5,20));
     assert(!memcmp(s,"\xf3\x0f\x5f\xe2\xf3\x0f\x5f\xe8\xf3\x0f\x59\x25",12));memcpy(&r,s+12,4);assert(s+16+r==lxav_page+LXAV_K1);
     assert(!memcmp(s+16,"\x0f\x28\xfd\xf3\x0f\x59\x3d",7));memcpy(&r,s+23,4);assert(s+27+r==lxav_page+LXAV_K2);
     assert(s[27]==0xe9);memcpy(&r,s+28,4);assert(s+32+r==LX(LXAV_BACK));
     memcpy(&k,s+LXAV_K1,4);assert(k==0x3ea2f983u);memcpy(&k,s+LXAV_K2,4);assert(k==0x3f22f983u);}
    lxav_restore();assert(!lxav_installed && lxav_restored && lxav_state()==0 && !memcmp(LX(LXAV_VA),lxav_original,sizeof(lxav_original)) && lxav_pins());
    munmap(lxav_page,GATE_PAGE);lxav_page=NULL;lxav_restored=0;lxav_stage="idle";stats.active=0;
    printf("avoid angle ELF: the run pinned, the entry to its stub (mulss by 1/pi and 2/pi, back), restored PASS\n");
}
