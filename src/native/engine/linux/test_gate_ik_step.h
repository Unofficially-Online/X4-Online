/* The joints' fixed-step update in the Windows order (gate_ik_step.h): x / 0.02f against x * 50.0f (why the order
 * matters); on the real executable the runs' bytes, the pins, both entries installed (jmp rel32 to their stubs; the
 * stubs' multiplications by the page's 50.0f, the copied instructions, the jumps back) and restored. Included by
 * test_gate_linux.c. */
/* Inject a failed CAS or a successful CAS reported as failure, as can happen
 * when gate_code_swap128 writes successfully but cannot restore RX. */
static unsigned lxis_test_calls,lxis_test_fail_mask,lxis_test_after_mask;
static int lxis_test_close_fail(void *at) {(void)at;return 0;}
static int lxis_test_swap(void *at,unsigned __int128 expected,unsigned __int128 value) {
    const unsigned bit=1u<<lxis_test_calls++;
    if(lxis_test_fail_mask & bit) return 0;
    const int ok=gate_code_swap128(at,expected,value);
    return (lxis_test_after_mask & bit)?0:ok;
}
static void lxis_test_inject(unsigned fail_mask,unsigned after_mask) {
    lxis_test_calls=0;lxis_test_fail_mask=fail_mask;lxis_test_after_mask=after_mask;
    lxis_code_swap=lxis_test_swap;
}
static void test_ik_step_linux(void) {
    const float dt=0.02f;unsigned apart=0;
    for(unsigned i=1;i<=1000;i++) {volatile float x=(float)i*0.000173f;if(x/dt!=x*50.0f) apart++;}
    assert(apart>100); /* the two orders round apart often */
    {uint32_t bits;memcpy(&bits,&dt,4);assert(bits==0x3ca3d70au);}
    printf("ik step: x / 0.02f against x * 50.0f apart in %u of 1000 PASS\n",apart);
}
static void test_ik_step_elf(void) {
    assert(!memcmp(LX(LXIS_A_VA),lxis_a_original,16) && !memcmp(LX(LXIS_C_VA),lxis_c_original,19));
    {float k;memcpy(&k,LX(0x2c76dd0u),4);assert(k==0.02f);}
    reset_session();stats.active=1;boot_mode=1;boot_phase=0;stats.issued=0;ui_thread=GetCurrentThreadId();lxis_installed=lxis_restored=0;
    assert(lxis_pins() && lxis_state()==0 && lxis_arm() && lxis_installed && lxis_state()==1 && lxis_check() && lxis_pins() && lxis_page_ok());
    /* the entries: jmp rel32 to the stubs; the rest of each run as it was */
    {int32_t r;assert(LX(LXIS_A_VA)[0]==0xe9);memcpy(&r,LX(LXIS_A_VA)+1,4);assert(LX(LXIS_A_VA)+5+r==lxis_page+LXIS_STUB_A);
     assert(!memcmp(LX(LXIS_A_VA)+5,lxis_a_original+5,11));
     assert(LX(LXIS_C_VA)[0]==0xe9);memcpy(&r,LX(LXIS_C_VA)+1,4);assert(LX(LXIS_C_VA)+5+r==lxis_page+LXIS_STUB_C);
     assert(!memcmp(LX(LXIS_C_VA)+5,lxis_c_original+5,14));}
    /* the stubs: mulss by the page's 50.0f where the run divided by xmm1, the copied instructions, the jumps back */
    {const unsigned char *a=lxis_page+LXIS_STUB_A,*c=lxis_page+LXIS_STUB_C;int32_t r;float k;
     memcpy(&k,lxis_page+LXIS_FIFTY,4);assert(k==50.0f);
     assert(!memcmp(a,"\xf3\x0f\x59\x2d",4));memcpy(&r,a+4,4);assert(a+8+r==lxis_page+LXIS_FIFTY);
     assert(!memcmp(a+8,"\x0f\x28\xc5\xf3\x41\x0f\x5c\xc1",8));
     assert(!memcmp(a+16,"\xf3\x0f\x59\x05",4));memcpy(&r,a+20,4);assert(a+24+r==lxis_page+LXIS_FIFTY);
     assert(a[24]==0xe9);memcpy(&r,a+25,4);assert(a+29+r==LX(LXIS_A_BACK));
     assert(!memcmp(c,lxis_c_original,15) && !memcmp(c+15,"\xf3\x0f\x59\x05",4));memcpy(&r,c+19,4);assert(c+23+r==lxis_page+LXIS_FIFTY);
     assert(c[23]==0xe9);memcpy(&r,c+24,4);assert(c+28+r==LX(LXIS_C_BACK));}
    char status[200];assert(lxis_status(status,sizeof(status))>0 && strstr(status," state=1 pin=1 page=1 stage=armed"));
    lxis_restore();assert(!lxis_installed && lxis_restored && lxis_state()==0 && !memcmp(LX(LXIS_A_VA),lxis_a_original,16) &&
                          !memcmp(LX(LXIS_C_VA),lxis_c_original,19) && lxis_pins());
    unsigned char *first_page=lxis_page;
    /* A CAS can have succeeded even when the helper returned failure. */
    lxis_test_inject(0,1u);
    assert(!lxis_arm() && !lxis_installed && !lxis_cleanup_pending() && lxis_state()==0);
    assert(lxis_page!=first_page);
    munmap(lxis_page,GATE_PAGE);
    /* Even native bytes remain unresolved until RX protection is confirmed. */
    lxis_test_inject(0,1u);lxis_code_close=lxis_test_close_fail;
    assert(!lxis_arm() && lxis_own_a && lxis_cleanup_pending() && lxis_state()==0);
    assert(!lxis_arm());
    lxis_code_close=gate_code_close;lxis_restore();
    assert(!lxis_cleanup_pending() && lxis_restored);
    munmap(lxis_page,GATE_PAGE);
    /* C fails, then A's rollback fails: retain ownership and reject rearm. */
    lxis_test_inject((1u<<1)|(1u<<2),0);
    assert(!lxis_arm() && lxis_own_a && !lxis_own_c && lxis_cleanup_pending());
    assert(!lxis_arm() && !strcmp(lxis_stage,"cleanup_pending"));
    lxis_test_inject(0,0);lxis_restore();
    assert(!lxis_cleanup_pending() && lxis_state()==0 && lxis_restored);
    munmap(lxis_page,GATE_PAGE);
    /* A failed restore must not prevent the independent C restore. */
    lxis_test_inject(0,0);assert(lxis_arm());
    lxis_test_inject(1u,0);lxis_restore();
    assert(!lxis_installed && lxis_own_a && !lxis_own_c && lxis_cleanup_pending());
    lxis_test_inject(0,0);lxis_restore();assert(!lxis_cleanup_pending() && lxis_state()==0);
    munmap(lxis_page,GATE_PAGE);
    /* Foreign bytes are never overwritten, but the other site is restored. */
    lxis_test_inject(0,0);assert(lxis_arm());
    {unsigned char jump[5],foreign[5]={0xcc,0xcc,0xcc,0xcc,0xcc};
     assert(lxis_patch(jump,LXIS_A_VA,LXIS_STUB_A) && lxis_swap(LXIS_A_VA,jump,foreign));
     lxis_restore();assert(lxis_own_a && !lxis_own_c && lxis_cleanup_pending());
     assert(!memcmp(LX(LXIS_A_VA),foreign,5) && lxis_site_state(LXIS_C_VA,LXIS_STUB_C,lxis_c_original)==0);
     assert(lxis_swap(LXIS_A_VA,foreign,jump));lxis_restore();assert(!lxis_cleanup_pending() && lxis_state()==0);}
    munmap(lxis_page,GATE_PAGE);munmap(first_page,GATE_PAGE);lxis_page=NULL;
    lxis_code_swap=gate_code_swap128;lxis_restored=0;lxis_stage="idle";stats.active=0;
    printf("ik step ELF: the runs pinned, both entries to their stubs (mulss by 50.0f, the copied instructions, back), restored PASS\n");
}
