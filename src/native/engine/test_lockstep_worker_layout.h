/* native_gate_worker_layout (lockstep_worker_layout.h): X4's split of W job threads into batches; on the relocated
 * X4.exe the pins, the arm and Jolt's concurrency slot (called through the vtable), then the batch vectors of a fake
 * PathingManager laid out from another PC's counts (grown and shrunk; X4's reserve, batch constructor and destroy
 * replaced by the test's, which keep X4's element form), the refusals (an occupied batch, a changed pin, a value out of
 * range), the step check and the restore. No X4 code but GetMaxConcurrency runs. */
typedef struct test_wlay_node {struct test_wlay_node *next,*prev;void *agent;} test_wlay_node;
static unsigned test_wlay_reserves,test_wlay_ctors,test_wlay_destroyed;
/* As X4's reserve 0xf1fad0 with its copy 0xf28d00: every batch made anew (kind, a new sentinel, the members copied)
 * but its cursor copied from the old batch, whose sentinel is then freed. */
static void test_wlay_reserve(void *vector,uint64_t n) {
    unsigned char **v=vector,*begin=v[0],*end=v[1];size_t used=(size_t)(end-begin);
    unsigned char *fresh=calloc((size_t)n,WLAY_BATCH);assert(fresh && used<=n*WLAY_BATCH);
    for(size_t at=0;at<used;at+=WLAY_BATCH) {
        test_wlay_node *s=calloc(1,sizeof(*s));assert(s);s->next=s->prev=s;
        assert(!*(uint64_t*)(begin+at+8));   /* empty batches only */
        fresh[at]=begin[at];memcpy(fresh+at+0x10,&s,8);memcpy(fresh+at+0x18,begin+at+0x18,8);
        free(*(void**)(begin+at+0x10));memset(begin+at,0xdd,WLAY_BATCH);
    }
    free(begin);v[0]=fresh;v[1]=fresh+used;v[2]=fresh+n*WLAY_BATCH;++test_wlay_reserves;
}
static void *test_wlay_ctor(void *list,void *element) {
    uint64_t *l=list;test_wlay_node *s=calloc(1,sizeof(*s));assert(s && (unsigned char*)list==(unsigned char*)element+8);
    s->next=s->prev=s;l[0]=0;l[1]=(uint64_t)(uintptr_t)s;l[2]=(uint64_t)(uintptr_t)s;++test_wlay_ctors;return list;
}
static void test_wlay_destroy(void *first,void *last) {
    for(unsigned char *e=first;e!=last;e+=WLAY_BATCH) {free(*(void**)(e+0x10));memset(e,0xdd,WLAY_BATCH);++test_wlay_destroyed;}
}
/* A fake manager's two vectors with n[0] and n[1] empty batches (capacity exactly n), the indices at 3 and 5. */
static void test_wlay_manager(unsigned char *manager,const unsigned n[2]) {
    memset(manager,0,0x400);
    for(unsigned v=0;v<2;v++) {
        unsigned char **vector=(unsigned char**)(manager+WLAY_VECTORS+0x18u*v);
        vector[0]=vector[1]=vector[2]=NULL;
        unsigned char *b=calloc(n[v]?n[v]:1,WLAY_BATCH);assert(b);vector[0]=vector[1]=b;vector[2]=b+(size_t)n[v]*WLAY_BATCH;
        for(unsigned i=0;i<n[v];i++) {unsigned char *e=vector[1];e[0]=(unsigned char)v;test_wlay_ctor(e+8,e);vector[1]=e+WLAY_BATCH;}
        uint64_t next=v?5:3;memcpy(manager+WLAY_NEXT+8u*v,&next,8);
    }
}
static void test_wlay_free(unsigned char *manager) {
    for(unsigned v=0;v<2;v++) {
        unsigned char **vector=(unsigned char**)(manager+WLAY_VECTORS+0x18u*v);
        for(unsigned char *e=vector[0];e<vector[1];e+=WLAY_BATCH) free(*(void**)(e+0x10));
        free(vector[0]);vector[0]=vector[1]=vector[2]=NULL;
    }
}
static void test_wlay_reset(void) {
    wlay_armed=wlay_applied=wlay_jolt=wlay_restored=0;wlay_workers=wlay_native_workers=wlay_pc=0;memset(wlay_pc_layout,0,sizeof(wlay_pc_layout));wlay_pass_at=wlay_pending_agents=0;
    memset(wlay_layout,0,sizeof(wlay_layout));memset(wlay_native,0,sizeof(wlay_native));memset(wlay_next,0,sizeof(wlay_next));
    InterlockedExchange(&wlay_pending,0);wlay_jolt_value=0;wlay_stage="idle";
}
typedef int (*test_wlay_concurrency_fn)(const void *);
static void test_worker_layout_native_image(const char *path) {
    /* X4's own split (the constructor at 0xeabb9e): this PC 30 -> 10 + 20 (read live), a 24-thread PC 22 -> 8 + 14. */
    static const unsigned split[][3]={{30,10,20},{22,8,14},{14,5,9},{4,2,2},{6,2,4},{32,11,21}};
    for(unsigned i=0;i<sizeof(split)/sizeof(*split);i++) {unsigned o[2];wlay_split(split[i][0],o);assert(o[0]==split[i][1] && o[1]==split[i][2]);}
    unsigned char *prior=image;const uint64_t saved_issued=stats.issued;
    wlay_reserve_fn saved_reserve=wlay_reserve;wlay_ctor_fn saved_ctor=wlay_ctor;wlay_destroy_fn saved_destroy=wlay_destroy;
    image=test_pmath_map(path);test_wlay_reset();
    assert(wlay_pins() && *(void**)(image+WLAY_JOLT_SLOT)==(void*)(image+WLAY_JOLT_FN));
    /* The JTM with 22 workers (a 12-core/24-thread PC) and its GetMaxConcurrency, the real function, through the vtable. */
    static unsigned char jtm[0x600],manager[0x400];memset(jtm,0,sizeof(jtm));unsigned native=22;memcpy(jtm+WLAY_JTM_WORKERS,&native,4);
    unsigned char *p=jtm;memcpy(image+WLAY_JTM,&p,8);
    test_wlay_concurrency_fn get=*(test_wlay_concurrency_fn*)(image+WLAY_JOLT_SLOT);assert(get(NULL)==22);
    const unsigned laptop[2]={8,14};test_wlay_manager(manager,laptop);p=manager;memcpy(image+PATH_DET_MANAGER,&p,8);
    wlay_reserve=test_wlay_reserve;wlay_ctor=test_wlay_ctor;wlay_destroy=test_wlay_destroy;
    test_wlay_reserves=test_wlay_ctors=test_wlay_destroyed=0;
    /* Out of range: refused, nothing written. */
    assert(!wlay_arm(3,0) && !wlay_arm(33,0) && !wlay_arm(30,3) && !wlay_armed && !strcmp(wlay_stage,"value"));
    assert(*(void**)(image+WLAY_JOLT_SLOT)==(void*)(image+WLAY_JOLT_FN));
    /* Armed for 30: Jolt hears 30 at once; the batches wait for the pathing wake. Only the same W again. */
    assert(wlay_arm(30,0) && wlay_armed && !wlay_applied && wlay_native_workers==22 && wlay_layout[0]==10 && wlay_layout[1]==20);
    get=*(test_wlay_concurrency_fn*)(image+WLAY_JOLT_SLOT);assert(get(NULL)==30 && wlay_arm(30,0) && !wlay_arm(22,0) && !wlay_arm(30,22));
    assert(!wlay_check() && !strcmp(wlay_stage,"not_laid_out"));   /* a step before the layout stops */
    /* The wake: 8 + 14 grown to 10 + 20 as the constructor grows (the old batches destroyed, one reserve on the empty
     * vector, all made anew: a reserve over batches would leave their cursors on freed sentinels), the indices 0,
     * every batch empty with its cursor at its own sentinel. */
    uint64_t pending=7;memcpy(manager+WLAY_PENDING,&pending,8);stats.issued=0;
    test_wlay_ctors=0;assert(wlay_apply(manager) && wlay_applied && wlay_check() && !InterlockedCompareExchange(&wlay_pending,0,0));
    assert(test_wlay_reserves==2 && test_wlay_ctors==30 && test_wlay_destroyed==22 && wlay_native[0]==8 && wlay_native[1]==14);
    for(unsigned v=0;v<2;v++) {
        unsigned char **vector=(unsigned char**)(manager+WLAY_VECTORS+0x18u*v);uint64_t next=9;
        assert((size_t)(vector[1]-vector[0])==(size_t)wlay_layout[v]*WLAY_BATCH && vector[2]==vector[1]);
        for(unsigned char *e=vector[0];e<vector[1];e+=WLAY_BATCH) assert(e[0]==v && wlay_batch_empty(e));
        memcpy(&next,manager+WLAY_NEXT+8u*v,8);assert(!next && wlay_next[v]==(v?5u:3u));
    }
    assert(wlay_apply(manager) && test_wlay_reserves==2);   /* nothing pending: nothing more */
    {   /* Why: X4's reserve over existing batches leaves each cursor on the old (freed) sentinel. */
        unsigned char scratch[0x400];const unsigned two[2]={2,2};test_wlay_manager(scratch,two);
        test_wlay_reserve(scratch+WLAY_VECTORS,4);unsigned char *e=*(unsigned char**)(scratch+WLAY_VECTORS);
        assert(*(uint64_t*)(e+0x18)!=*(uint64_t*)(e+0x10) && !wlay_batch_empty(e));
        *(uint64_t*)(e+0x18)=*(uint64_t*)(e+0x10);*(uint64_t*)(e+0x20+0x18)=*(uint64_t*)(e+0x20+0x10);test_wlay_free(scratch);
    }
    char out[320];assert(wlay_status(out,sizeof(out))>0);
    assert(strstr(out,"armed=1 applied=1 workers=30 native_workers=22 layout=10+20 native=8+14 pc=0 next=3/5 pending=7 jolt=1:30 "));
    assert(strstr(out,"stage=laid_out"));
    /* Someone else's pointer in Jolt's slot: the step stops; the restore leaves it alone. */
    {void *ours=*(void**)(image+WLAY_JOLT_SLOT);*(void**)(image+WLAY_JOLT_SLOT)=(void*)test_wlay_destroy;
     assert(!wlay_check() && !strcmp(wlay_stage,"slot_changed"));wlay_restore();assert(wlay_jolt && !strcmp(wlay_stage,"restore_foreign"));
     *(void**)(image+WLAY_JOLT_SLOT)=ours;}
    wlay_restore();assert(!wlay_jolt && !wlay_armed && wlay_restored && *(void**)(image+WLAY_JOLT_SLOT)==(void*)(image+WLAY_JOLT_FN));
    get=*(test_wlay_concurrency_fn*)(image+WLAY_JOLT_SLOT);assert(get(NULL)==22);
    test_wlay_free(manager);
    /* A 48-thread PC (32 workers: 11 + 21) shrunk to 10 + 20: X4's destroy for the surplus, no reserve. */
    test_wlay_reset();const unsigned big[2]={11,21};test_wlay_manager(manager,big);test_wlay_reserves=test_wlay_destroyed=0;
    assert(wlay_arm(30,0) && wlay_apply(manager) && wlay_check() && !test_wlay_reserves && test_wlay_destroyed==2);
    for(unsigned v=0;v<2;v++) {unsigned char **vector=(unsigned char**)(manager+WLAY_VECTORS+0x18u*v);assert((size_t)(vector[1]-vector[0])==(size_t)wlay_layout[v]*WLAY_BATCH);}
    wlay_restore();test_wlay_free(manager);
    /* Test only, P=22 on this PC's 10 + 20: first a 24-thread PC's 8 + 14 (destroy 2 + 6), then 10 + 20 again (8 made). */
    test_wlay_reset();const unsigned here[2]={10,20};test_wlay_manager(manager,here);test_wlay_reserves=test_wlay_ctors=test_wlay_destroyed=0;
    assert(wlay_arm(30,22) && wlay_pc==22 && wlay_pc_layout[0]==8 && wlay_pc_layout[1]==14 && wlay_apply(manager) && wlay_check());
    assert(test_wlay_destroyed==8 && test_wlay_ctors==8 && !test_wlay_reserves && wlay_native[0]==10 && wlay_native[1]==20);
    for(unsigned v=0;v<2;v++) {unsigned char **vector=(unsigned char**)(manager+WLAY_VECTORS+0x18u*v);assert((size_t)(vector[1]-vector[0])==(size_t)wlay_layout[v]*WLAY_BATCH);
        for(unsigned char *e=vector[0];e<vector[1];e+=WLAY_BATCH) assert(e[0]==v && wlay_batch_empty(e));}
    assert(wlay_status(out,sizeof(out))>0 && strstr(out,"layout=10+20 native=10+20 pc=22 "));
    wlay_restore();test_wlay_free(manager);
    /* A batch that already holds an Agent (a world loaded before the arm): nothing written, the first step stops. */
    test_wlay_reset();test_wlay_manager(manager,laptop);
    {unsigned char *e=*(unsigned char**)(manager+WLAY_VECTORS+0x18u)+3*WLAY_BATCH;test_wlay_node *s=*(test_wlay_node**)(e+0x10),*n=calloc(1,sizeof(*n));
     assert(n);n->next=n->prev=s;s->next=s->prev=n;uint64_t one=1;memcpy(e+8,&one,8);memcpy(e+0x18,&n,8);
     test_wlay_reserves=test_wlay_ctors=0;
     assert(wlay_arm(30,0) && !wlay_apply(manager) && !strcmp(wlay_stage,"occupied") && !wlay_applied && !test_wlay_reserves && !test_wlay_ctors);
     assert(!wlay_check() && (size_t)(*(unsigned char**)(manager+WLAY_VECTORS+0x20)-*(unsigned char**)(manager+WLAY_VECTORS+0x18))==14u*WLAY_BATCH);
     uint64_t next=0;memcpy(&next,manager+WLAY_NEXT,8);assert(next==3);   /* untouched */
     s->next=s->prev=s;free(n);}
    wlay_restore();test_wlay_free(manager);
    /* A changed pin (the batch constructor): refused before anything is written. */
    test_wlay_reset();image[WLAY_BATCH_CTOR+4]^=1;
    assert(!wlay_arm(30,0) && !strcmp(wlay_stage,"pins") && *(void**)(image+WLAY_JOLT_SLOT)==(void*)(image+WLAY_JOLT_FN));
    image[WLAY_BATCH_CTOR+4]^=1;
    test_wlay_reset();wlay_reserve=saved_reserve;wlay_ctor=saved_ctor;wlay_destroy=saved_destroy;stats.issued=saved_issued;
    assert(VirtualFree(image,0,MEM_RELEASE));image=prior;
    puts("worker layout: X4's split of W threads, batches grown and shrunk to W=30's before the pass, Jolt's slot, refusals PASS");
}
