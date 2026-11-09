/* The obstacle avoidance's angle scaling in the Windows order (gate_avoid_angle.h): x / pi against x * (1 / pi) (why
 * the order matters, and the folded constants are the rounded reciprocals); on the real executable the run's bytes, the
 * pin, the entry installed (jmp rel32 to the stub: both products by the page's constants, the jump back) and restored.
 * Included by test_gate_linux.c. */
static void test_avoid_angle_linux(void) {
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
