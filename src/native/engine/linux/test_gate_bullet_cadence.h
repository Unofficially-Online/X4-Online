/* Bullet hit tests every frame on the Linux gate (gate_bullet_cadence.h). ELF: the pins, the je to the nop and back
 * ('every'), nothing written ('observe'), the status text. Included by test_gate_linux.c. */
static void test_bullet_cadence_elf(void) {
    for(unsigned i=0;i<sizeof(lxbc_pins)/sizeof(*lxbc_pins);i++) assert(site_ok(site_named(lxbc_pins[i])));
    assert(lxbc_state()==0 && lxbc_pins_ok() && lxbc_site()==0x840f);
    reset_session();stats.active=1;boot_mode=1;boot_phase=0;stats.issued=0;ui_thread=GetCurrentThreadId();
    lxbc_installed=lxbc_restored=lxbc_patched_now=0;
    assert(lxbc_arm(LXBC_EVERY) && lxbc_installed && lxbc_state()==1 && lxbc_site()==0x0f66 && lxbc_check());
    assert(lxbc_pins_ok());                                  /* the pins read through the nop */
    assert(lxbc_arm(LXBC_EVERY) && !lxbc_arm(LXBC_OBSERVE));   /* a rerun with the same mode */
    {char status[768];assert(lxbc_status(status,sizeof(status)) && strstr(status,"mode=every armed=1 installed=1 site=0f66 "));}
    lxbc_restore();assert(!lxbc_installed && lxbc_restored && lxbc_state()==0);
    assert(lxbc_arm(LXBC_OBSERVE) && lxbc_state()==0 && lxbc_check());lxbc_restore();assert(lxbc_state()==0);
    lxbc_restored=0;lxbc_stage="idle";lxbc_mode=LXBC_OFF;stats.active=0;
    puts("bullet cadence ELF: the pins, the je to the nop and back, observe writes nothing PASS");
}
