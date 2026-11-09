/* native_gate_nav_refresh_watch (lockstep_nav_refresh_watch.h): the refresh prediction (the original's own test), the
 * row, the sink; on the executable's bytes the entry armed, owned, the view keeping the pins over RefreshFromPathingState,
 * a foreign entry failing the tick check, restored. The hook's call into the game is not executed here. */
static char tnrw_rows[4096];static size_t tnrw_length;
static void tnrw_sink(const char *text,int n) {
    assert(n>0 && tnrw_length+(size_t)n<sizeof(tnrw_rows));memcpy(tnrw_rows+tnrw_length,text,(size_t)n);tnrw_length+=(size_t)n;tnrw_rows[tnrw_length]=0;
}
static void test_nav_refresh_watch(void) {
    _Alignas(16) unsigned char nav[0x100],owner[0x10];memset(nav,0,sizeof(nav));memset(owner,0,sizeof(owner));
    const uint64_t id=0x0000000000a1b2c3ull;memcpy(owner+8,&id,8);unsigned char *op=owner;memcpy(nav+NRW_OWNER,&op,8);
    double stamp=38.5;uint32_t cursor=0;memcpy(nav+NRW_STAMP,&stamp,8);memcpy(nav+NRW_CURSOR,&cursor,4);
    nrw_before b;
    nrw_read_before(nav,1,1,38.6,&b);assert(b.ok && b.refresh && b.owner==id && b.cursor==0 && b.stamp==38.5);
    nrw_read_before(nav,0,1,38.6,&b);assert(b.ok && !b.refresh);   /* the cursor is current */
    nrw_read_before(nav,1,1,38.5,&b);assert(b.ok && !b.refresh);   /* not past the stamp */
    {const double nan=__builtin_nan("");nrw_read_before(nav,1,1,nan,&b);assert(b.ok && !b.refresh);} /* unordered: jbe skips */
    nrw_read_before(nav,1,0,0.0,&b);assert(b.ok && !b.refresh && !b.have_now); /* no TLS block yet */
    nrw_read_before(NULL,1,1,38.6,&b);assert(!b.ok && !b.refresh);
    /* the row: both times as bits, '-' without the thread's time, won */
    char row[320];nrw_read_before(nav,1,1,38.6,&b);
    int n=nrw_row(row,sizeof(row),1049,0,0x5ca2b3,&b,0);assert(n>0);
    char want[200];uint64_t sb,nb;memcpy(&sb,&b.stamp,8);memcpy(&nb,&b.now,8);
    snprintf(want,sizeof(want),"ui=0 caller=5ca2b3 nav=0000000000a1b2c3 sel=1 cursor_before=0 t_before=%016llx now=%016llx won=0",(unsigned long long)sb,(unsigned long long)nb);
    assert(strstr(row,"[X4LAN] NATIVE_GATE NAV_REFRESH tick=1049 thread=") && strstr(row,want) && row[n-2]=='\r' && row[n-1]=='\n');
    b.have_now=0;n=nrw_row(row,sizeof(row),7,1,0,&b,1);assert(n>0 && strstr(row," now=- won=1"));
    /* the sink at the tick check's flush */
    nrw_sink=tnrw_sink;tnrw_length=0;nrw_append(row,n);assert(nrw_rows==1 && !tnrw_length);nrw_flush();assert(tnrw_length==(size_t)n && !nrw_used);
    nrw_flush();assert(tnrw_length==(size_t)n);
    nrw_sink=NULL;free(nrw_buffer);nrw_buffer=NULL;nrw_used=nrw_capacity=0;nrw_rows=0;
    {char out[400];assert(nrw_status(out,sizeof(out))>0 && strstr(out,"installed=0 restored=0 stage=idle window=0:0"));}
    printf("nav refresh watch: the refresh prediction (stamp, cursor, unordered time, no TLS), the row (times as bits, won), the sink PASS\n");
}
static void test_nav_refresh_watch_native_image(const char *path) {
    unsigned char *prior=image,*saved=nrw_page;image=test_pmath_map(path);nrw_page=NULL;
    assert(nrw_pins() && nrw_state()==0 && !memcmp(image+NRW_ENTRY,nrw_original,5));
    image[NRW_ENTRY+0x80]^=1;assert(!nrw_pins());image[NRW_ENTRY+0x80]^=1;
    /* the facts the hook's prediction mirrors: the stamp and cursor compares, the stores, the selector's address */
    assert(!memcmp(image+0xea2a5b,"\x66\x0f\x2f\x87\xc0\x00\x00\x00\x0f\x86",10)); /* comisd xmm0,[rdi+0xc0]; jbe */
    {int32_t d;memcpy(&d,image+0xea2a6b,4);assert(0xea2a6fu+(uint32_t)d==NRW_SELECTOR && !memcmp(image+0xea2a69,"\x8b\x05",2));}
    assert(!memcmp(image+0xea2a6f,"\x3b\x87\xc8\x00\x00\x00\x0f\x84",8)); /* cmp eax,[rdi+0xc8]; je */
    assert(!memcmp(image+0xea2aa6,"\xf2\x0f\x11\x87\xc0\x00\x00\x00",8) && !memcmp(image+0xea2ab3,"\x89\x87\xc8\x00\x00\x00",6));
    const uint32_t sa=stats.active;const uint64_t si=stats.issued;const DWORD su=ui_thread;const int sb=boot_mode;
    stats.active=1;stats.issued=0;ui_thread=GetCurrentThreadId();boot_mode=0;
    assert(!nrw_arm(0,3) && !strcmp(nrw_stage,"window"));
    stats.issued=4;assert(!nrw_arm(1040,1060) && !strcmp(nrw_stage,"state"));stats.issued=0;
    assert(nrw_arm(1040,1060) && nrw_installed && nrw_state()==1 && nrw_owned() && nrw_check() && !strcmp(nrw_stage,"armed"));
    assert(nrw_arm(1040,1060) && !nrw_arm(1040,1061) && !nrw_arm(1039,1060)); /* again: the same window answers 1, another is refused */
    assert(nrw_from==1040 && nrw_to==1060 && !strcmp(nrw_stage,"armed"));
    /* the view: the hook's pin and the 537-byte pins of the join and the world hash over the function pass */
    assert(nrw_pins() && image[NRW_ENTRY]==0xe9 && hash_ok(image+NRW_ENTRY,NRW_SIZE,NRW_SHA));
    {unsigned char viewed[8];assert(read_local(image+NRW_ENTRY-1,viewed,8) && !memcmp(viewed+1,nrw_original,5));}
    {char out[400];assert(nrw_status(out,sizeof(out))>0 && strstr(out,"installed=1 restored=0 stage=armed window=1040:1060"));}
    {   unsigned char ours[5];memcpy(ours,image+NRW_ENTRY,5);unsigned char other[5]={0xe9,1,0,0,0};
        assert(view_cas_bytes(NRW_ENTRY,ours,other,5) && !nrw_owned() && !nrw_check() && !strcmp(nrw_stage,"site_changed"));
        assert(view_cas_bytes(NRW_ENTRY,other,ours,5) && nrw_owned() && !nrw_check());nrw_failures=0;assert(nrw_check());
    }
    nrw_restore();assert(!nrw_installed && nrw_restored && nrw_state()==0 && !strcmp(nrw_stage,"restored") && !nrw_any && nrw_pins());
    stats.active=sa;stats.issued=si;ui_thread=su;boot_mode=sb;
    VirtualFree(nrw_page,0,MEM_RELEASE);nrw_page=saved;nrw_restored=0;nrw_stage="idle";nrw_from=nrw_to=0;
    nrw_calls_ui=nrw_calls_other=nrw_refresh_ui=nrw_refresh_other=nrw_lost=nrw_rows=nrw_dropped=nrw_failures=0;
    VirtualFree(image,0,MEM_RELEASE);image=prior;
    printf("nav refresh watch native image: RefreshFromPathingState 0xea2a20 pinned (tamper refused), its compares and stores as the prediction reads them, "
           "window/state refusals, armed/owned (the view keeps both pins), a foreign entry fails the tick check, restored PASS\n");
}
