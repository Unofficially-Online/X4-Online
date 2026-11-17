/* lockstep_ui_holds.h: what the UI's two calls of X4's hold routine become (synthetic; the sites are checked live). */
static unsigned test_uih_calls[2];static void *test_uih_last;
static uint64_t test_uih_native(void *component,unsigned char on) {++test_uih_calls[on?1:0];test_uih_last=component;return 7;}
static void test_ui_holds(void) {
    static unsigned char a[16],b[16],c[16];
    uih_keep_fn saved=uih_native_keep;const LONG was=uih_on;
    uih_native_keep=test_uih_native;memset(uih_held,0,sizeof(uih_held));test_uih_calls[0]=test_uih_calls[1]=0;
    uih_adds=uih_drops=uih_passed_adds=uih_passed_drops=0;
    /* Off: X4's own routine, and what it took is remembered. */
    InterlockedExchange(&uih_on,0);
    assert(uih_keep(a,1)==7 && test_uih_calls[1]==1 && test_uih_last==a && uih_passed_adds==1);
    assert(uih_keep(a,1)==7 && uih_keep(b,1)==7 && test_uih_calls[1]==3);
    assert(uih_keep(b,0)==7 && test_uih_calls[0]==1 && uih_passed_drops==1);   /* given back while off */
    /* On: a new hold and its release reach nothing. */
    InterlockedExchange(&uih_on,1);
    assert(uih_keep(c,1)==0 && uih_keep(c,0)==0 && test_uih_calls[1]==3 && test_uih_calls[0]==1 && uih_adds==1 && uih_drops==1);
    /* The two holds X4 took for a before: given back natively, each once; a third release is dropped. */
    assert(uih_keep(a,0)==7 && uih_keep(a,0)==7 && test_uih_calls[0]==3 && test_uih_last==a && uih_passed_drops==3);
    assert(uih_keep(a,0)==0 && test_uih_calls[0]==3 && uih_drops==2);
    assert(uih_keep(b,0)==0 && uih_drops==3);                                   /* b's was given back already */
    /* The holds the UI's map shows at the arming (uih_note_native with a count). */
    uih_note_native(c,3);
    assert(uih_keep(c,0)==7 && uih_keep(c,0)==7 && uih_keep(c,0)==7 && uih_keep(c,0)==0 && test_uih_calls[0]==6);
    {char row[160];assert(uih_status(row,sizeof(row)) && strstr(row,"/1/1/4/3/6/"));}
    /* Arming is refused once ticks were issued. */
    {const uint64_t issued=stats.issued;const int installed=uih_installed;uih_installed=0;stats.issued=5;assert(!uih_arm());stats.issued=issued;uih_installed=installed;}
    uih_native_keep=saved;InterlockedExchange(&uih_on,was);memset(uih_held,0,sizeof(uih_held));
    puts("ui holds: off passes and remembers, on drops, native holds given back once each PASS");
}
