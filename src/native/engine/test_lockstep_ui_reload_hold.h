/* lockstep_ui_reload_hold.h on a stand-in site and flag: the hold goes in at the first live pass, is checked every pass,
 * notes a reload asked for (the first tick, the passes), never clears X4's flag, and gives X4's bytes back; other bytes
 * at the site refuse; not armed, nothing is touched. The image's own bytes are checked by the on-foot image test. The
 * reload itself needs the game. */
static void test_ui_reload_hold(void) {
    static unsigned char site[16];static volatile unsigned char flag;
    uint64_t v=UIRH_ORIGINAL,now=0;char row[128];
    const int armed=uirh_armed,patched=uirh_patched,held=uirh_held;unsigned char *const site_was=uirh_site;
    const volatile unsigned char *const flag_was=uirh_flag;const uint64_t first=uirh_first_tick,passes=uirh_passes;const char *const why=uirh_why;
    memcpy(site+1,&v,8);flag=0;
    uirh_site=site+1;uirh_flag=&flag;uirh_patched=0;uirh_held=0;uirh_first_tick=0;uirh_passes=0;uirh_why="-";
    /* not armed: a pass is nothing */
    uirh_armed=0;assert(uirh_pass(40) && !uirh_patched);memcpy(&now,site+1,8);assert(now==UIRH_ORIGINAL);
    /* armed: the first live pass puts the jump in; nothing asked for yet */
    uirh_armed=1;assert(uirh_pass(41) && uirh_patched && !uirh_held);memcpy(&now,site+1,8);assert(now==UIRH_PATCHED && site[1]==0xeb && site[2]==0x0e);
    assert(uirh_pass(42) && !uirh_held && !uirh_passes);
    /* a reload asked for at 50: noted, the flag stays X4's */
    flag=1;assert(uirh_pass(50) && uirh_held && uirh_first_tick==50 && uirh_pass(51) && uirh_passes==2 && uirh_first_tick==50 && flag==1);
    assert(uirh_status(row,sizeof(row)) && !strcmp(row," reload_hold=1:1:1:50:2"));
    /* the site changed under the hold: the pass fails, the restore refuses and keeps the mark */
    site[3]^=1;assert(!uirh_pass(52) && !strcmp(uirh_why,"reload_hold_lost") && !uirh_uninstall() && uirh_patched);site[3]^=1;
    /* the stop: X4's bytes again, its flag still set for its own next frame */
    assert(uirh_uninstall() && !uirh_patched && uirh_uninstall());memcpy(&now,site+1,8);assert(now==UIRH_ORIGINAL && flag==1);
    /* other bytes at the site: no hold, the pass fails */
    site[1]^=1;uirh_why="-";assert(!uirh_pass(60) && !uirh_patched && !strcmp(uirh_why,"reload_hold_install"));site[1]^=1;
    uirh_armed=armed;uirh_patched=patched;uirh_held=held;uirh_site=site_was;uirh_flag=flag_was;uirh_first_tick=first;uirh_passes=passes;uirh_why=why;
    puts("ui reload hold: the jump in at the first live pass and out at the stop, a held reload noted and X4's flag kept, a changed site refused PASS");
}
