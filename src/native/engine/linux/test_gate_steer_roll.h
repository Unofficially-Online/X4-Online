/* The steering's roll alignment in the Windows order (gate_steer_roll.h): x / L against x * (1 / L) (why the order
 * matters); on the real executable the runs' bytes, the pins, both entries installed (jmp rel32 to their stubs: the
 * reciprocal of xmm4 from the page's 1.0f, the multiplications, the jumps back) and restored. Included by
 * test_gate_linux.c. */
/* Inject at real page transitions; successful operations use the real CAS/mprotect. */
static int lxsr_close_failure,lxsr_partial_failure,lxsr_foreign_rollback;
static unsigned lxsr_a_opens;
static const unsigned char lxsr_foreign[5]={0x90,0x91,0x92,0x93,0x94};
static int lxsr_test_protection(void *at,int open) {
    const uintptr_t page=(uintptr_t)at&~(uintptr_t)(GATE_PAGE-1);
    const uintptr_t a=(uintptr_t)LX(LXSR_A_VA)&~(uintptr_t)(GATE_PAGE-1);
    const uintptr_t b=(uintptr_t)LX(LXSR_B_VA)&~(uintptr_t)(GATE_PAGE-1);
    if(open && lxsr_partial_failure) {
        if(page==b) {
            if(lxsr_foreign_rollback) tl_write(LX(LXSR_A_VA),lxsr_foreign,5);
            return 0;
        }
        if(page==a && ++lxsr_a_opens>1) return 0;
    }
    if(!open && lxsr_close_failure) return 0;
    return open?gate_code_open(at,16):gate_code_close(at);
}
static void test_steer_roll_cleanup(void) {
    unsigned char saved_a[7],saved_b[8],to[5];char perms[5];
    assert(!lxsr_installed && !lxsr_page && !lxsr_rx_pending);
    memcpy(saved_a,LX(LXSR_A_VA),7);memcpy(saved_b,LX(LXSR_B_VA),8);
    tl_write(LX(LXSR_A_VA),lxsr_a_original,7);tl_write(LX(LXSR_B_VA),lxsr_b_original,8);
    lxsr_page=gate_near_page(LX(LXSR_A_VA));assert(lxsr_page && lxsr_code(lxsr_page,lxsr_page));
    assert(!mprotect(lxsr_page,GATE_PAGE,PROT_READ|PROT_EXEC) && lxsr_patch(to,0));
    lxsr_test_protect=lxsr_test_protection;lxsr_close_failure=1;
    assert(!lxsr_swap(LXSR_A_VA,lxsr_a_original,to));
    assert(lxsr_installed && !lxsr_restored && lxsr_rx_pending==1 && lxsr_site_state(0)==1 && lxsr_site_state(1)==0);
    {const int active=stats.active;stats.active=1;assert(!lxsr_arm());stats.active=active;}
    assert(!lxsr_check());
    lxsr_restore();assert(lxsr_installed && !lxsr_restored && lxsr_rx_pending==3 && lxsr_state()==0);
    perms_of(LX(LXSR_A_VA),perms);assert(!strcmp(perms,"rwxp"));
    lxsr_close_failure=0;lxsr_restore();assert(!lxsr_installed && lxsr_restored && !lxsr_rx_pending);
    perms_of(LX(LXSR_A_VA),perms);assert(!strcmp(perms,"r-xp"));
    /* B's install fails, then A's rollback fails: A must remain tracked across retry. */
    lxsr_partial_failure=1;lxsr_a_opens=0;
    assert(!lxsr_install() && lxsr_installed && !lxsr_restored && lxsr_site_state(0)==1 && lxsr_site_state(1)==0);
    lxsr_restore();assert(lxsr_installed && !lxsr_restored && lxsr_site_state(0)==1);
    lxsr_partial_failure=0;lxsr_restore();assert(!lxsr_installed && lxsr_restored && lxsr_state()==0);
    /* An intervening foreign write at A must survive the failed rollback and STOP retry. */
    lxsr_partial_failure=lxsr_foreign_rollback=1;lxsr_a_opens=0;
    assert(!lxsr_install() && lxsr_installed && !lxsr_restored);
    lxsr_partial_failure=lxsr_foreign_rollback=0;lxsr_restore();
    assert(lxsr_installed && !lxsr_restored && !memcmp(LX(LXSR_A_VA),lxsr_foreign,5) && lxsr_site_state(1)==0);
    tl_write(LX(LXSR_A_VA),lxsr_a_original,7);lxsr_restore();assert(!lxsr_installed && lxsr_restored);
    /* Restore an owned B even while A is foreign. */
    assert(lxsr_install());tl_write(LX(LXSR_A_VA),lxsr_foreign,5);lxsr_restore();
    assert(lxsr_installed && !lxsr_restored && !memcmp(LX(LXSR_A_VA),lxsr_foreign,5) && lxsr_site_state(1)==0);
    tl_write(LX(LXSR_A_VA),lxsr_a_original,7);lxsr_restore();assert(!lxsr_installed && lxsr_restored);
    tl_write(LX(LXSR_A_VA),saved_a,7);tl_write(LX(LXSR_B_VA),saved_b,8);munmap(lxsr_page,GATE_PAGE);lxsr_page=NULL;
    lxsr_test_protect=NULL;lxsr_restored=0;lxsr_stage="idle";
    printf("steer roll cleanup: post-CAS close failure, partial rollback, foreign ownership and independent site retry PASS\n");
}
static void test_steer_roll_linux(void) {
    test_steer_roll_cleanup();
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
