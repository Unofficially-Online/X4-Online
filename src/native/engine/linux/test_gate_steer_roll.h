/* The steering's roll alignment in the Windows order (gate_steer_roll.h): x / L against x * (1 / L) (why the order
 * matters); on the real executable the runs' bytes, the pins, both entries installed (jmp rel32 to their stubs: the
 * reciprocal of xmm4 from the page's 1.0f, the multiplications, the jumps back) and restored. Included by
 * test_gate_linux.c. */
static void test_steer_roll_linux(void) {
    unsigned apart=0;
    for(unsigned i=1;i<=1000;i++) {
        volatile float x=(float)i*0.000917f,l=0.6981317f,r=1.0f/l;
        if(x/l!=x*r) apart++;
    }
    assert(apart>50); /* the two orders round apart often */
    printf("steer roll: x / L against x * (1 / L) apart in %u of 1000 PASS\n",apart);
}
static void test_steer_roll_elf(void) {
    assert(!memcmp(LX(LXSR_A_VA),lxsr_a_original,7) && !memcmp(LX(LXSR_B_VA),lxsr_b_original,8));
    reset_session();stats.active=1;boot_mode=1;boot_phase=0;stats.issued=0;ui_thread=GetCurrentThreadId();lxsr_installed=lxsr_restored=0;
    assert(lxsr_pins() && lxsr_state()==0 && lxsr_arm() && lxsr_installed && lxsr_state()==1 && lxsr_check() && lxsr_pins() && lxsr_page_ok());
    {int32_t r;assert(LX(LXSR_A_VA)[0]==0xe9);memcpy(&r,LX(LXSR_A_VA)+1,4);assert(LX(LXSR_A_VA)+5+r==lxsr_page+LXSR_STUB_A);
     assert(!memcmp(LX(LXSR_A_VA)+5,lxsr_a_original+5,2));
     assert(LX(LXSR_B_VA)[0]==0xe9);memcpy(&r,LX(LXSR_B_VA)+1,4);assert(LX(LXSR_B_VA)+5+r==lxsr_page+LXSR_STUB_B);
     assert(!memcmp(LX(LXSR_B_VA)+5,lxsr_b_original+5,3));}
    {const unsigned char *a=lxsr_page+LXSR_STUB_A,*b=lxsr_page+LXSR_STUB_B;int32_t r;float k;
     memcpy(&k,lxsr_page+LXSR_ONE,4);assert(k==1.0f);
     assert(!memcmp(a,"\xf3\x0f\x10\x1d",4));memcpy(&r,a+4,4);assert(a+8+r==lxsr_page+LXSR_ONE);
     assert(!memcmp(a+8,"\xf3\x0f\x5e\xdc\xf3\x0f\x59\xdf\xe9",9));memcpy(&r,a+17,4);assert(a+21+r==LX(LXSR_A_BACK));
     assert(!memcmp(b,"\xf3\x0f\x58\xff\xf3\x0f\x10\x1d",8));memcpy(&r,b+8,4);assert(b+12+r==lxsr_page+LXSR_ONE);
     assert(!memcmp(b+12,"\xf3\x0f\x5e\xdc\xf3\x0f\x59\xfb\xe9",9));memcpy(&r,b+21,4);assert(b+25+r==LX(LXSR_B_BACK));}
    char status[200];assert(lxsr_status(status,sizeof(status))>0 && strstr(status," state=1 pin=1 page=1 stage=armed"));
    lxsr_restore();assert(!lxsr_installed && lxsr_restored && lxsr_state()==0 && !memcmp(LX(LXSR_A_VA),lxsr_a_original,7) &&
                          !memcmp(LX(LXSR_B_VA),lxsr_b_original,8) && lxsr_pins());
    munmap(lxsr_page,GATE_PAGE);lxsr_page=NULL;lxsr_restored=0;lxsr_stage="idle";stats.active=0;
    printf("steer roll ELF: the runs pinned, both entries to their stubs (the reciprocal, the products, back), restored PASS\n");
}
