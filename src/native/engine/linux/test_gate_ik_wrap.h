/* A joint's wrapped target in the Windows order (gate_ik_wrap.h): the two orders on lx52a's and W36l's tick-15..17
 * words (the Windows order gives Windows' targets); on the real executable the constants the tail uses (2pi, pi), the
 * tail rewritten in place and restored. Included by test_gate_linux.c. */
static float tiw_wrap(float a,float v,int windows) {
    const float two_pi=6.2831855f,pi=3.14159274f;
    volatile float t1=fmodf(a,two_pi);if(t1<0.0f) t1+=two_pi;
    volatile float x=(t1-v)+pi;volatile float t2=fmodf(x,two_pi);if(t2<0.0f) t2+=two_pi;
    volatile float r=windows?(t2+v)-pi:(t2-pi)+v;
    return r;
}
static uint32_t tiw_bits(float f) {uint32_t u;memcpy(&u,&f,4);return u;}
static float tiw_float(uint32_t u) {float f;memcpy(&f,&u,4);return f;}
static void test_ik_wrap_linux(void) {
    static const uint32_t rows[3][3]={{0x3c4e2fadu,0x3b857210u,0x3c4e3000u},{0x3c514e08u,0x3c057210u,0x3c514e00u},{0x3c584810u,0x3c584810u,0x3c584800u}};
    for(unsigned i=0;i<3;i++) assert(tiw_bits(tiw_wrap(tiw_float(rows[i][0]),tiw_float(rows[i][1]),1))==rows[i][2]); /* Windows' targets */
    assert(tiw_bits(tiw_wrap(tiw_float(0x3c4e2b00u),0.0f,1))==tiw_bits(tiw_wrap(tiw_float(0x3c4e2b00u),0.0f,0))); /* value 0: both orders agree */
    printf("ik wrap: (t2 + value) - pi gives W36l's targets from lx52a's words; value 0 agrees PASS\n");
}
static void test_ik_wrap_elf(void) {
    assert(!memcmp(LX(LXIW_VA),lxiw_original,11));
    {float k;memcpy(&k,LX(0x2e24944u),4);assert(tiw_bits(k)==0x40c90fdbu);memcpy(&k,LX(0x2e27154u),4);assert(tiw_bits(k)==0x40490fdbu);} /* 2pi, pi */
    reset_session();stats.active=1;boot_mode=1;boot_phase=0;stats.issued=0;ui_thread=GetCurrentThreadId();lxiw_installed=lxiw_restored=0;
    assert(lxiw_pins() && lxiw_state()==0 && lxiw_arm() && lxiw_installed && lxiw_state()==1 && lxiw_check() && lxiw_pins());
    assert(!memcmp(LX(LXIW_VA),lxiw_windows,11) && !memcmp(LX(LXIW_VA)+11,"\xe9",1));            /* the jmp after it stays */
    char status[200];assert(lxiw_status(status,sizeof(status))>0 && strstr(status," state=1 pin=1 stage=armed"));
    lxiw_restore();assert(!lxiw_installed && lxiw_restored && lxiw_state()==0 && !memcmp(LX(LXIW_VA),lxiw_original,11) && lxiw_pins());
    lxiw_restored=0;lxiw_stage="idle";stats.active=0;
    printf("ik wrap ELF: 2pi and pi as the tail reads them, the tail rewritten in place (two lines) and restored PASS\n");
}
