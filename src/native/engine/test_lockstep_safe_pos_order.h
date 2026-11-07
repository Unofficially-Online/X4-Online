/* native_gate_safe_pos_order (lockstep_safe_pos_order.h): the GetSafePos solver's boxes sorted at its entry. Synthetic
 * scenes (FR27's generator, test_lockstep_fp_redirect.h) on the portable solver fsp_solve, which reproduces the native
 * solver's bits (FR27 native image test): the same boxes in two orders give points apart, sorted they never do. */
static void tspo_reverse(unsigned char *boxes,unsigned n) {
    unsigned char t[SPO_BOX];
    for(unsigned i=0;i<n/2;i++) {
        unsigned char *a=boxes+i*SPO_BOX,*b=boxes+(n-1-i)*SPO_BOX;memcpy(t,a,SPO_BOX);memcpy(a,b,SPO_BOX);memcpy(b,t,SPO_BOX);
    }
}
static void test_safe_pos_order(void) {
    /* the order: the bytes the solver reads; the padding +0x24..+0x2f does not count */
    _Alignas(16) unsigned char a[SPO_BOX],b[SPO_BOX];memset(a,0,sizeof(a));memset(b,0,sizeof(b));
    a[0x10]=1;b[0x10]=1;b[0x28]=0x55;assert(!spo_compare(a,b) && !spo_compare(b,a));
    b[0x30]=1;assert(spo_compare(a,b)<0 && spo_compare(b,a)>0);
    b[0x30]=0;b[0x23]=1;assert(spo_compare(a,b)<0);
    tfsp_seed=0x5150u;unsigned scenes=0,apart=0,moved=0;
    for(unsigned i=0;i<4000;i++) {
        static tfsp_scene s,x,y;tfsp_make(&s);
        if(s.n<2) continue;
        ++scenes;x=s;y=s;tspo_reverse(y.boxes,y.n);
        /* unsorted: the native order dependence */
        {   static tfsp_scene u,v;u=x;v=y;
            fsp_solve(u.boxes,u.n,u.p,u.r,u.dir,u.a5,u.y,NULL,u.a9);fsp_solve(v.boxes,v.n,v.p,v.r,v.dir,v.a5,v.y,NULL,v.a9);
            apart+=memcmp(u.p,v.p,12)!=0;
        }
        moved+=spo_sort_boxes(x.boxes,x.n);spo_sort_boxes(y.boxes,y.n);
        assert(!memcmp(x.boxes,y.boxes,x.n*SPO_BOX)); /* one order for one set */
        assert(!spo_sort_boxes(x.boxes,x.n)); /* sorted stays */
        for(unsigned k=1;k<x.n;k++) assert(spo_compare(x.boxes+(k-1)*SPO_BOX,x.boxes+k*SPO_BOX)<=0);
        fsp_solve(x.boxes,x.n,x.p,x.r,x.dir,x.a5,x.y,NULL,x.a9);fsp_solve(y.boxes,y.n,y.p,y.r,y.dir,y.a5,y.y,NULL,y.a9);
        assert(!memcmp(x.p,y.p,16));
    }
    assert(scenes>2500 && apart>0 && moved>0);
    /* the vector the hook gets: refused vectors are counted and left alone */
    {   static tfsp_scene s;tfsp_seed=7u;do tfsp_make(&s); while(s.n<3);
        const LONG64 i0=spo_invalid,s0=spo_sorted;
        uintptr_t bad[3]={(uintptr_t)s.boxes,(uintptr_t)(s.boxes+SPO_BOX+1),0};spo_sort_vector(bad);
        uintptr_t back[3]={(uintptr_t)(s.boxes+SPO_BOX),(uintptr_t)s.boxes,0};spo_sort_vector(back);spo_sort_vector(NULL);
        assert(spo_invalid==i0+3 && spo_sorted==s0);
        tspo_reverse(s.boxes,s.n);
        uintptr_t good[3]={(uintptr_t)s.boxes,(uintptr_t)(s.boxes+s.n*SPO_BOX),(uintptr_t)(s.boxes+s.n*SPO_BOX)};spo_sort_vector(good);
        assert(spo_sorted==s0+1 && spo_max_n>=(LONG64)s.n);
        for(unsigned k=1;k<s.n;k++) assert(spo_compare(s.boxes+(k-1)*SPO_BOX,s.boxes+k*SPO_BOX)<=0);
        spo_calls=spo_sorted=spo_moved=spo_invalid=spo_max_n=0;
    }
    {char out[320];assert(spo_status(out,sizeof(out))>0 && strstr(out,"installed=0 sort=0 restored=0 stage=idle"));}
    printf("safe pos order: one order per box set (padding ignored), %u of %u two-or-more-box scenes apart unsorted, 0 sorted, refused vectors PASS\n",
           apart,scenes);
}
static char tspo_rows[1<<16];static size_t tspo_length;
static void tspo_sink(const char *text,int n) {
    assert(n>0 && tspo_length+(size_t)n<sizeof(tspo_rows));memcpy(tspo_rows+tspo_length,text,(size_t)n);tspo_length+=(size_t)n;tspo_rows[tspo_length]=0;
}
/* On the executable's bytes: the pins, the entry armed and owned (the view keeps the pin), the native solver through the
 * hooked entry on reversed scenes agreeing bit for bit where the unhooked solver does not, restored. */
static void test_safe_pos_order_native_image(const char *path) {
    unsigned char *prior=image,*saved_pm=pmath_page,*saved_spo=spo_page;image=test_pmath_map(path);pmath_page=NULL;spo_page=NULL;
    assert(spo_pins() && spo_state()==0);
    image[SPO_SOLVER+0x100]^=1;assert(!spo_pins());image[SPO_SOLVER+0x100]^=1;
    assert(!memcmp(image+SPO_SOLVER,spo_original,7) && !memcmp(image+SPO_SOLVER,fr_sp_original,5) && SPO_RESUME==FR_SP_RESUME);
    /* the solver with its callees and the two portable-math entries it calls made executable; the portable math installed */
    static const uint32_t exec_pages[3][2]={{0x14e1000u,0x4000u},{0x1750000u,0x1000u},{0x1a1a000u,0x1000u}};
    DWORD exec_old[3];
    for(unsigned i=0;i<3;i++) {
        assert(VirtualProtect(image+exec_pages[i][0],exec_pages[i][1],PAGE_EXECUTE_READWRITE,&exec_old[i]));
        FlushInstructionCache(GetCurrentProcess(),image+exec_pages[i][0],exec_pages[i][1]);
    }
    pmath_page=pmath_alloc_page();assert(pmath_page && pmath_write_page());
    for(unsigned i=0;i<PMATH_COUNT;i++) memcpy(pmath_original[i],image+pmath_sites[i].rva,5);
    assert(pmath_switch(1)==1);
    typedef uint64_t (*entry_fn)(void *,float *,float,float *,float,uint64_t,uint64_t,float *,float);
    const entry_fn entry=(entry_fn)(void*)(image+SPO_SOLVER);
    /* unhooked: the native solver's own order dependence */
    unsigned apart=0,scenes=0;tfsp_seed=0x5151u;
    for(unsigned i=0;i<1500;i++) {
        static tfsp_scene x,y;tfsp_make(&x);if(x.n<2) continue;
        ++scenes;y=x;tspo_reverse(y.boxes,y.n);
        uintptr_t vx[3]={(uintptr_t)x.boxes,(uintptr_t)(x.boxes+x.n*FSP_BOX),(uintptr_t)(x.boxes+x.n*FSP_BOX)};
        uintptr_t vy[3]={(uintptr_t)y.boxes,(uintptr_t)(y.boxes+y.n*FSP_BOX),(uintptr_t)(y.boxes+y.n*FSP_BOX)};
        entry(vx,x.p,x.r,x.dir,x.a5,0x1234,(uint64_t)x.y,NULL,x.a9);entry(vy,y.p,y.r,y.dir,y.a5,0x1234,(uint64_t)y.y,NULL,y.a9);
        apart+=memcmp(x.p,y.p,12)!=0;
    }
    assert(scenes>900 && apart>0);
    /* armed: refused outside the UI thread's boot window, then installed */
    const uint32_t sa=stats.active;const uint64_t si=stats.issued;const DWORD su=ui_thread;const int sb=boot_mode;
    stats.active=1;stats.issued=5;ui_thread=GetCurrentThreadId();boot_mode=0;
    assert(!spo_arm(1) && !strcmp(spo_stage,"state"));stats.issued=0;
    assert(spo_arm(1) && spo_installed && spo_state()==1 && spo_owned() && spo_check() && !strcmp(spo_stage,"armed"));
    assert(spo_pins() && spo_arm(1) && !spo_arm(0)); /* the view keeps the pin; a rerun; not another mode */
    {unsigned char viewed[5];assert(read_local(image+SPO_SOLVER,viewed,5) && !memcmp(viewed,spo_original,5) && image[SPO_SOLVER]==0xe9);}
    unsigned hooked=0;tfsp_seed=0x5151u;
    for(unsigned i=0;i<1500;i++) {
        static tfsp_scene x,y;tfsp_make(&x);if(x.n<2) continue;
        ++hooked;y=x;tspo_reverse(y.boxes,y.n);
        uintptr_t vx[3]={(uintptr_t)x.boxes,(uintptr_t)(x.boxes+x.n*FSP_BOX),(uintptr_t)(x.boxes+x.n*FSP_BOX)};
        uintptr_t vy[3]={(uintptr_t)y.boxes,(uintptr_t)(y.boxes+y.n*FSP_BOX),(uintptr_t)(y.boxes+y.n*FSP_BOX)};
        entry(vx,x.p,x.r,x.dir,x.a5,0x1234,(uint64_t)x.y,NULL,x.a9);entry(vy,y.p,y.r,y.dir,y.a5,0x1234,(uint64_t)y.y,NULL,y.a9);
        assert(!memcmp(x.p,y.p,16)); /* one result per box set */
    }
    assert(hooked==scenes && spo_calls==2*(LONG64)hooked && spo_sorted==2*(LONG64)hooked && spo_moved>0 && !spo_invalid);
    {char out[320];assert(spo_status(out,sizeof(out))>0 && strstr(out,"installed=1 sort=1 restored=0 stage=armed"));}
    /* SAFE_POS_DUMP: the calls in the window, the boxes as given (before the sort, padding zeroed), the result; to the
     * sink at the tick check */
    {   assert(!spo_dump_set(0,3) && !spo_dump_set(5,4) && !spo_dump_set(1.5,3));
        char *rows=tspo_rows;tspo_length=0;rows[0]=0;
        spo_dump_sink=tspo_sink;assert(spo_dump_set(7,7));
        static tfsp_scene x;tfsp_seed=99u;do tfsp_make(&x); while(x.n<3);
        unsigned char *bh=x.boxes+SPO_BOX;memset(bh+0x24,0x77,0x0c); /* padding: zeroed in the row */
        static unsigned char given[6*SPO_BOX];memcpy(given,x.boxes,x.n*SPO_BOX);
        uintptr_t vx[3]={(uintptr_t)x.boxes,(uintptr_t)(x.boxes+x.n*FSP_BOX),(uintptr_t)(x.boxes+x.n*FSP_BOX)};
        stats.issued=6;entry(vx,x.p,x.r,x.dir,x.a5,0x1234,(uint64_t)x.y,NULL,x.a9);assert(!spo_dump_rows); /* outside */
        memcpy(x.boxes,given,x.n*SPO_BOX);stats.issued=7;
        float p0[4];memcpy(p0,x.p,16);
        entry(vx,x.p,x.r,x.dir,x.a5,0x1234,(uint64_t)x.y,NULL,x.a9);
        assert(spo_dump_rows==1 && !tspo_length && spo_check() && tspo_length);stats.issued=0;
        char want[6*SPO_BOX*2+1];memset(given+SPO_BOX+0x24,0,0x0c);
        for(unsigned i=0;i<x.n;i++) memset(given+i*SPO_BOX+0x24,0,0x0c);
        spo_hex(want,given,x.n*SPO_BOX);
        char ph[33],qh[33];spo_hex(ph,p0,16);spo_hex(qh,x.p,16);
        char head[96];snprintf(head,sizeof(head),"NATIVE_GATE SAFE_POS_DUMP tick=7 seq=1 thread=");
        assert(strstr(rows,head) && strstr(rows,"caller=") && strstr(rows,want) && strstr(rows,ph) && strstr(rows,qh) && strstr(rows," move=- "));
        {char nn[24];snprintf(nn,sizeof(nn)," n=%u ",x.n);assert(strstr(rows,nn));}
        assert(!strcmp(rows+tspo_length-2,"\r\n") && !strstr(rows+1,"[X4LAN]"));
        spo_dump_from=spo_dump_to=0;spo_dump_sink=NULL;spo_dump_rows=spo_dump_dropped=0;spo_dump_seq=0;
        free(spo_dump_buffer);spo_dump_buffer=NULL;spo_dump_used=spo_dump_capacity=0;
    }
    /* a foreign change at a gate tick: owned no longer, the check fails once and stays failed */
    {   unsigned char ours[5];memcpy(ours,image+SPO_SOLVER,5);unsigned char other[5]={0xe9,0,0,0,0};
        assert(view_cas_bytes(SPO_SOLVER,ours,other,5) && !spo_owned() && !spo_check() && !spo_check() && !strcmp(spo_stage,"site_changed"));
        assert(view_cas_bytes(SPO_SOLVER,other,ours,5) && spo_owned() && !spo_check());spo_failures=0;assert(spo_check());
    }
    spo_restore();assert(!spo_installed && spo_restored && spo_state()==0 && !strcmp(spo_stage,"restored") && !spo_any);
    assert(!memcmp(image+SPO_SOLVER,spo_original,7) && spo_pins());
    stats.active=sa;stats.issued=si;ui_thread=su;boot_mode=sb;
    assert(pmath_switch(0)==1);VirtualFree(pmath_page,0,MEM_RELEASE);pmath_page=saved_pm;
    for(unsigned i=0;i<3;i++) {DWORD ignored;assert(VirtualProtect(image+exec_pages[i][0],exec_pages[i][1],exec_old[i],&ignored));}
    VirtualFree(spo_page,0,MEM_RELEASE);spo_page=saved_spo;spo_restored=0;spo_sort_on=0;spo_stage="idle";
    spo_calls=spo_sorted=spo_moved=spo_invalid=spo_max_n=spo_failures=0;
    VirtualFree(image,0,MEM_RELEASE);image=prior;
    printf("safe pos order native image: solver 0x14e4320 pinned (tamper refused), %u of %u reversed scenes apart through the native entry, armed/owned (the view keeps the pin), "
           "0 apart through the hooked entry, SAFE_POS_DUMP rows, a foreign entry fails the tick check, restored PASS\n",apart,scenes);
}
