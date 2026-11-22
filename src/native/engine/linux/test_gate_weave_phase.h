/* The weaving behaviours' phase in the Windows order (gate_weave_phase.h): (t * pi) / n against t * (pi / n) (the folded
 * constants are pi / 3 and pi / 5 rounded; the orders round apart); on the real executable the runs' bytes and
 * constants, the pins, both entries installed (jmp rel32 to their stubs: one mulss by the page's constant, the jump
 * back) and restored. Included by test_gate_linux.c. */
static void test_weave_phase_linux(void) {
    const float pi=3.14159274f;unsigned apart=0;
    {volatile float a=pi/3.0f,b=pi/5.0f;uint32_t u;memcpy(&u,(const void*)&a,4);assert(u==0x3f860a92u);memcpy(&u,(const void*)&b,4);assert(u==0x3f20d97cu);}
    for(unsigned i=1;i<=1000;i++) {
        volatile float t=(float)i*0.0333333f,p=t*pi;volatile float linux_phase=p/3.0f;volatile float windows_phase=t*1.0471976f;
        if(linux_phase!=windows_phase) apart++;
    }
    assert(apart>50);
    printf("weave phase: (t * pi) / 3 against t * (pi / 3) apart in %u of 1000 PASS\n",apart);
}
static void test_weave_phase_elf(void) {
    for(unsigned i=0;i<LXWP_SITES;i++) assert(!memcmp(LX(lxwp_sites[i].va),lxwp_sites[i].original,LXWP_RUN));
    {uint32_t k;memcpy(&k,LX(0x13de0ecu+8u+0x1a4684cu),4);assert(k==0x40400000u);memcpy(&k,LX(0x13de4acu+8u+0x1a478d4u),4);assert(k==0x40a00000u);} /* 3.0f, 5.0f */
    reset_session();stats.active=1;boot_mode=1;boot_phase=0;stats.issued=0;ui_thread=GetCurrentThreadId();lxwp_installed=lxwp_restored=0;
    assert(lxwp_pins() && lxwp_state()==0 && lxwp_arm() && lxwp_installed && lxwp_state()==1 && lxwp_check() && lxwp_pins() && lxwp_page_ok());
    for(unsigned i=0;i<LXWP_SITES;i++) {
        int32_t r;const unsigned char *at=LX(lxwp_sites[i].va),*s=lxwp_page+i*LXWP_STUB;uint32_t k;
        assert(at[0]==0xe9);memcpy(&r,at+1,4);assert(at+5+r==s && !memcmp(at+5,lxwp_sites[i].original+5,11));
        assert(!memcmp(s,"\xf3\x0f\x59\x05",4));memcpy(&r,s+4,4);assert(s+8+r==s+0x10);
        assert(s[8]==0xe9);memcpy(&r,s+9,4);assert(s+13+r==at+LXWP_RUN);
        memcpy(&k,s+0x10,4);assert(k==lxwp_sites[i].constant);
    }
    lxwp_restore();assert(!lxwp_installed && lxwp_restored && lxwp_state()==0 && lxwp_pins());
    for(unsigned i=0;i<LXWP_SITES;i++) assert(!memcmp(LX(lxwp_sites[i].va),lxwp_sites[i].original,LXWP_RUN));
    munmap(lxwp_page,GATE_PAGE);lxwp_page=NULL;lxwp_restored=0;lxwp_stage="idle";stats.active=0;
    printf("weave phase ELF: both runs pinned, the entries to their stubs (mulss by pi/3 and pi/5, back), restored PASS\n");
}
