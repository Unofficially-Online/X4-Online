/* The pursuit behaviour's speed factor in the Windows order (gate_pursuit_speed.h): ((c + 1) * -0.8) / 1.5 + 1 against
 * 1 - (c + 1) * 0.53333336 (the folded constant is 0.8f / 1.5f rounded; the orders round apart; the reported pair
 * 3eaebbb8 / 3eaebbba exists); on the real executable the run's bytes and constants, the pin, the entry installed (jmp
 * rel32 to the stub: one mulss by the page's constant, the jump back) and restored. Included by test_gate_linux.c. */
/* Synthetic code pages exercise real CAS/mprotect without executing engine code. */
static int lxps_fail_close;
static int lxps_test_protection(void *at,int open) {
    if(!open && lxps_fail_close) return 0;
    return open?gate_code_open(at,16):gate_code_close(at);
}
static void test_pursuit_speed_cleanup(void) {
    unsigned char saved[sizeof(lxps_original)],to[5],foreign[5]={0x90,0x91,0x92,0x93,0x94};char perms[5];
    assert(!lxps_installed && !lxps_page && !lxps_rx_pending);
    memcpy(saved,LX(LXPS_VA),sizeof(saved));tl_write(LX(LXPS_VA),lxps_original,sizeof(saved));
    lxps_page=gate_near_page(LX(LXPS_VA));assert(lxps_page && lxps_code(lxps_page,lxps_page));
    assert(!mprotect(lxps_page,GATE_PAGE,PROT_READ|PROT_EXEC) && lxps_patch(to));
    lxps_test_protect=lxps_test_protection;lxps_fail_close=1;
    assert(!lxps_swap(lxps_original,to));
    assert(lxps_installed && !lxps_restored && lxps_rx_pending && lxps_state()==1);
    perms_of(LX(LXPS_VA),perms);assert(!strcmp(perms,"rwxp"));
    {const int active=stats.active;stats.active=1;assert(!lxps_arm());stats.active=active;}
    assert(!lxps_check());
    lxps_restore(); /* bytes restored, protection still owed */
    assert(lxps_installed && !lxps_restored && lxps_rx_pending && lxps_state()==0);
    perms_of(LX(LXPS_VA),perms);assert(!strcmp(perms,"rwxp"));
    lxps_fail_close=0;lxps_restore();
    assert(!lxps_installed && lxps_restored && !lxps_rx_pending);
    perms_of(LX(LXPS_VA),perms);assert(!strcmp(perms,"r-xp"));
    /* A foreign entry is preserved and prevents a clean-restoration claim. */
    assert(lxps_swap(lxps_original,to));tl_write(LX(LXPS_VA),foreign,5);
    lxps_restore();assert(lxps_installed && !lxps_restored && !memcmp(LX(LXPS_VA),foreign,5));
    tl_write(LX(LXPS_VA),to,5);lxps_restore();assert(!lxps_installed && lxps_restored);
    /* Native bytes supplied externally still require a successful RX transition. */
    assert(lxps_swap(lxps_original,to));tl_write(LX(LXPS_VA),lxps_original,sizeof(saved));
    assert(!mprotect((void*)((uintptr_t)LX(LXPS_VA)&~(uintptr_t)(GATE_PAGE-1)),GATE_PAGE,PROT_READ|PROT_WRITE|PROT_EXEC));
    lxps_restore();assert(!lxps_installed && lxps_restored);
    perms_of(LX(LXPS_VA),perms);assert(!strcmp(perms,"r-xp"));
    /* Exact first-five-byte restoration is insufficient if the instruction tail changed. */
    assert(lxps_swap(lxps_original,to));unsigned char tail=lxps_original[5]^0x40;
    tl_write(LX(LXPS_VA)+5,&tail,1);lxps_restore();
    assert(lxps_installed && !lxps_restored && lxps_state()==0 && LX(LXPS_VA)[5]==tail);
    tl_write(LX(LXPS_VA)+5,lxps_original+5,1);lxps_restore();assert(!lxps_installed && lxps_restored);
    tl_write(LX(LXPS_VA),saved,sizeof(saved));munmap(lxps_page,GATE_PAGE);lxps_page=NULL;
    lxps_test_protect=NULL;lxps_restored=0;lxps_stage="idle";
    printf("pursuit_speed cleanup: post-CAS close failure, retry, foreign entry and exact instruction restoration PASS\n");
}
static void test_pursuit_speed_linux(void) {
    test_pursuit_speed_cleanup();
    unsigned apart=0,pair=0;
    {volatile float k=0.8f/1.5f;uint32_t u;memcpy(&u,(const void*)&k,4);assert(u==0x3f088889u);}
    for(uint32_t bits=0x3e000000u;bits<0x3fc00000u;bits+=0x101u) {
        float t;memcpy(&t,&bits,4);if(!(t>0.0f && t<1.5f)) continue;
        volatile float a=t*-0.8f;volatile float b=a/1.5f;volatile float linux_f=b+1.0f;
        volatile float u=t*0.53333336f;volatile float windows_f=1.0f-u;
        if(linux_f!=windows_f) {uint32_t x,y;float lf=linux_f,wf=windows_f;memcpy(&x,&wf,4);memcpy(&y,&lf,4);apart++;if(x==0x3eaebbb8u && y==0x3eaebbbau) pair++;}
    }
    assert(apart>1000);
    printf("pursuit speed: ((c+1) * -0.8) / 1.5 + 1 against 1 - (c+1) * 0.53333336 apart in %u samples (W36q's pair seen %u times) PASS\n",apart,pair);
}
static void test_pursuit_speed_elf(void) {
    assert(!memcmp(LX(LXPS_VA),lxps_original,sizeof(lxps_original)));
    {uint32_t k;memcpy(&k,LX(0x2d5bfecu),4);assert(k==0xbf4ccccdu);memcpy(&k,LX(0x2d5c060u),4);assert(k==0x3fc00000u);} /* -0.8f, 1.5f */
    reset_session();stats.active=1;boot_mode=1;boot_phase=0;stats.issued=0;ui_thread=GetCurrentThreadId();lxps_installed=lxps_restored=0;
    assert(lxps_pins() && lxps_state()==0 && lxps_arm() && lxps_installed && lxps_state()==1 && lxps_check() && lxps_pins() && lxps_page_ok());
    {int32_t r;const unsigned char *s=lxps_page;uint32_t k;
     assert(LX(LXPS_VA)[0]==0xe9);memcpy(&r,LX(LXPS_VA)+1,4);assert(LX(LXPS_VA)+5+r==lxps_page && !memcmp(LX(LXPS_VA)+5,lxps_original+5,11));
     assert(!memcmp(s,"\xf3\x0f\x59\x1d",4));memcpy(&r,s+4,4);assert(s+8+r==lxps_page+LXPS_K);
     assert(s[8]==0xe9);memcpy(&r,s+9,4);assert(s+13+r==LX(LXPS_BACK));
     memcpy(&k,s+LXPS_K,4);assert(k==0xbf088889u);}
    lxps_restore();assert(!lxps_installed && lxps_restored && lxps_state()==0 && !memcmp(LX(LXPS_VA),lxps_original,sizeof(lxps_original)) && lxps_pins());
    munmap(lxps_page,GATE_PAGE);lxps_page=NULL;lxps_restored=0;lxps_stage="idle";stats.active=0;
    printf("pursuit speed ELF: the run pinned, the entry to its stub (mulss by -0.53333336, back), restored PASS\n");
}
