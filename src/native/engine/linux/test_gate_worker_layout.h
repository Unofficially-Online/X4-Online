/* native_gate_worker_layout on the Linux gate (gate_worker_layout.h): X4's split of W job threads; a fake PathingManager's
 * batch vectors laid out from another PC's counts (grown and shrunk; X4's ~vector, reserve and emplace_back replaced by
 * the test's, which keep X4's batch form: kind, count, a sentinel node of its own, the cursor at it), the refusals (an
 * occupied batch), the indices, the status text; on the real executable the pins, the arm with the path hook, Jolt's
 * vtable slot swapped and restored. No X4 code runs. Included by test_gate_linux.c. */
typedef struct twl_node {struct twl_node *next,*prev;void *agent;} twl_node;
static unsigned twl_dtors,twl_reserves,twl_emplaces,twl_moves;
static void twl_dtor(void *vector) {   /* as 0x6a0bf4: every batch's sentinel and the storage freed */
    unsigned char **v=vector;
    for(unsigned char *e=v[0];e<v[1];e+=LXWL_BATCH) {free(*(void**)(e+0x10));memset(e,0xdd,LXWL_BATCH);}
    free(v[0]);memset(v,0xdd,24);++twl_dtors;
}
static void twl_reserve(void *vector,uint64_t n) {   /* as 0x13d0190 on an empty vector (any other would move batches) */
    unsigned char **v=vector;
    if(v[0] && (uint64_t)(v[2]-v[0])>=n*LXWL_BATCH) return;
    assert(v[0]==v[1]);   /* the module reserves on empty vectors only */
    free(v[0]);v[0]=v[1]=calloc((size_t)(n?n:1),LXWL_BATCH);assert(v[0]);v[2]=v[0]+n*LXWL_BATCH;++twl_reserves;
}
static void twl_emplace(void *vector,const char *kind) {   /* as 0x1f3c7a0 with room: a batch with its own sentinel */
    unsigned char **v=vector;
    if(v[1]==v[2]) {++twl_moves;return;}   /* X4 would reallocate here, copying cursors: must not happen */
    unsigned char *e=v[1];twl_node *s=calloc(1,sizeof(*s));assert(s);s->next=s->prev=s;
    memset(e,0,LXWL_BATCH);e[0]=(unsigned char)*kind;memcpy(e+0x10,&s,8);memcpy(e+0x18,&s,8);v[1]=e+LXWL_BATCH;++twl_emplaces;
}
/* A fake manager's two vectors with n[0] and n[1] empty batches (capacity exactly n), the indices at 3 and 5. */
static void twl_manager(unsigned char *manager,const unsigned n[2]) {
    memset(manager,0,0x400);
    for(unsigned v=0;v<2;v++) {
        unsigned char **vector=(unsigned char**)(manager+LXWL_VECTORS+0x18u*v);
        vector[0]=vector[1]=calloc(n[v]?n[v]:1,LXWL_BATCH);assert(vector[0]);vector[2]=vector[0]+(size_t)n[v]*LXWL_BATCH;
        const char kind=(char)v;for(unsigned i=0;i<n[v];i++) twl_emplace(vector,&kind);
        uint64_t next=v?5:3;memcpy(manager+LXWL_NEXT+8u*v,&next,8);
    }
}
static void twl_free(unsigned char *manager) {
    for(unsigned v=0;v<2;v++) {
        unsigned char **vector=(unsigned char**)(manager+LXWL_VECTORS+0x18u*v);
        for(unsigned char *e=vector[0];e<vector[1];e+=LXWL_BATCH) free(*(void**)(e+0x10));
        free(vector[0]);vector[0]=vector[1]=vector[2]=NULL;
    }
}
static void twl_reset(void) {
    lxwl_armed=lxwl_applied=lxwl_jolt=lxwl_restored=0;lxwl_workers=lxwl_native_workers=lxwl_pc=0;lxwl_pass_at=lxwl_pending_agents=0;
    memset(lxwl_pc_layout,0,sizeof(lxwl_pc_layout));memset(lxwl_layout,0,sizeof(lxwl_layout));memset(lxwl_native,0,sizeof(lxwl_native));
    memset(lxwl_next,0,sizeof(lxwl_next));lxwl_pending=0;lxwl_jolt_value=0;lxwl_stage="idle";twl_dtors=twl_reserves=twl_emplaces=twl_moves=0;
}
/* As lxwl_arm leaves it, without the pins and the slot (the synthetic image has neither). */
static void twl_armed(unsigned w,unsigned pc) {
    twl_reset();lxwl_workers=w;lxwl_split(w,lxwl_layout);lxwl_jolt_value=(int)w;lxwl_pc=pc;
    if(pc) lxwl_split(pc,lxwl_pc_layout);
    lxwl_armed=1;lxwl_pending=1;lxwl_stage="armed";
}
static void test_worker_layout_linux(void) {
    /* X4's split (Linux 0x13e92c0, Windows 0xeabb9e): 30 -> 10 + 20, a 24-thread laptop's 22 -> 8 + 14. */
    static const unsigned split[][3]={{30,10,20},{22,8,14},{14,5,9},{4,2,2},{6,2,4},{32,11,21}};
    for(unsigned i=0;i<sizeof(split)/sizeof(*split);i++) {unsigned o[2];lxwl_split(split[i][0],o);assert(o[0]==split[i][1] && o[1]==split[i][2]);}
    lxwl_reserve_fn saved_reserve=lxwl_reserve;lxwl_emplace_fn saved_emplace=lxwl_emplace;lxwl_dtor_fn saved_dtor=lxwl_dtor;
    lxwl_reserve=twl_reserve;lxwl_emplace=twl_emplace;lxwl_dtor=twl_dtor;
    static unsigned char manager[0x400];const uint64_t saved_issued=stats.issued;
    /* The laptop's 8 + 14 grown to 10 + 20: each vector destroyed, reserved empty, W's batches made, nothing moved;
     * the indices 0; every batch empty with its cursor at its own sentinel. */
    const unsigned laptop[2]={8,14};twl_manager(manager,laptop);twl_armed(30,0);
    assert(!lxwl_check() && !strcmp(lxwl_stage,"not_laid_out"));lxwl_stage="armed";   /* a step before the layout stops */
    uint64_t pending=7;memcpy(manager+LXWL_PENDING,&pending,8);stats.issued=0;twl_emplaces=0;
    assert(lxwl_apply(manager) && lxwl_applied && !lxwl_pending && twl_dtors==2 && twl_reserves==2 && twl_emplaces==30 && !twl_moves);
    assert(lxwl_native[0]==8 && lxwl_native[1]==14);
    lxwl_jolt=0;assert(lxwl_check());   /* laid out (the slot is the ELF test's) */
    for(unsigned v=0;v<2;v++) {
        unsigned char **vector=(unsigned char**)(manager+LXWL_VECTORS+0x18u*v);uint64_t next=9;
        assert((size_t)(vector[1]-vector[0])==(size_t)lxwl_layout[v]*LXWL_BATCH && vector[2]==vector[1]);
        for(unsigned char *e=vector[0];e<vector[1];e+=LXWL_BATCH) assert(e[0]==v && lxwl_batch_empty(e));
        memcpy(&next,manager+LXWL_NEXT+8u*v,8);assert(!next && lxwl_next[v]==(v?5u:3u));
    }
    assert(lxwl_apply(manager) && twl_reserves==2);   /* nothing pending: nothing more */
    char out[320];assert(lxwl_status(out,sizeof(out))>0);
    assert(strstr(out,"armed=1 applied=1 workers=30 native_workers=0 layout=10+20 native=8+14 pc=0 next=3/5 pending=7 jolt=0:30 "));
    assert(strstr(out,"stage=laid_out"));
    twl_free(manager);
    /* The same count already (a 32-thread PC): nothing rebuilt. A 48-thread PC's 11 + 21: both rebuilt for 10 + 20. */
    const unsigned here[2]={10,20};twl_manager(manager,here);twl_armed(30,0);
    assert(lxwl_apply(manager) && !twl_dtors && !twl_reserves && !twl_emplaces);twl_free(manager);
    const unsigned big[2]={11,21};twl_manager(manager,big);twl_armed(30,0);
    assert(lxwl_apply(manager) && twl_dtors==2 && twl_emplaces==30 && !twl_moves);
    for(unsigned v=0;v<2;v++) {unsigned char **vector=(unsigned char**)(manager+LXWL_VECTORS+0x18u*v);assert((size_t)(vector[1]-vector[0])==(size_t)lxwl_layout[v]*LXWL_BATCH);}
    twl_free(manager);
    /* Test only, P=22 on 10 + 20: first a laptop's 8 + 14, then 10 + 20 again. */
    twl_manager(manager,here);twl_armed(30,22);
    assert(lxwl_pc_layout[0]==8 && lxwl_pc_layout[1]==14 && lxwl_apply(manager) && twl_dtors==4 && twl_emplaces==52 && !twl_moves);
    assert(lxwl_status(out,sizeof(out))>0 && strstr(out,"layout=10+20 native=10+20 pc=22 "));
    twl_free(manager);
    /* A batch that already holds an Agent: nothing written, the first step stops. */
    twl_manager(manager,laptop);twl_armed(30,0);
    {unsigned char *e=*(unsigned char**)(manager+LXWL_VECTORS+0x18u)+3*LXWL_BATCH;twl_node *s=*(twl_node**)(e+0x10),*n=calloc(1,sizeof(*n));
     assert(n);n->next=n->prev=s;s->next=s->prev=n;uint64_t one=1;memcpy(e+8,&one,8);memcpy(e+0x18,&n,8);twl_emplaces=0;
     assert(!lxwl_apply(manager) && !strcmp(lxwl_stage,"occupied") && !lxwl_applied && !twl_dtors && !twl_emplaces);
     assert(!lxwl_check() && (size_t)(*(unsigned char**)(manager+LXWL_VECTORS+0x20)-*(unsigned char**)(manager+LXWL_VECTORS+0x18))==14u*LXWL_BATCH);
     uint64_t next=0;memcpy(&next,manager+LXWL_NEXT,8);assert(next==3);   /* untouched */
     s->next=s->prev=s;free(n);memset(e+8,0,8);memcpy(e+0x18,&s,8);}
    twl_free(manager);
    twl_reset();lxwl_reserve=saved_reserve;lxwl_emplace=saved_emplace;lxwl_dtor=saved_dtor;stats.issued=saved_issued;
    printf("worker layout wrapper: X4's split, batches rebuilt for W=30 before the pass, nothing moved, refusals PASS\n");
}
typedef int (*twl_concurrency_fn)(const void *);
static void test_worker_layout_elf(void) {
    assert(lxwl_pins());
    assert(rd64(LX(LXWL_JOLT_SLOT_VA))==LXWL_JOLT_VA);   /* the file's slot holds the function's VA */
    /* Loaded at the executable's base the slot holds the function's address: relocate it for the round, then back. */
    unsigned char *page=(unsigned char*)((uintptr_t)LX(LXWL_JOLT_SLOT_VA)&~(uintptr_t)(GATE_PAGE-1));
    assert(!mprotect(page,GATE_PAGE,PROT_READ|PROT_WRITE));{const void *f=LX(LXWL_JOLT_VA);memcpy(LX(LXWL_JOLT_SLOT_VA),&f,8);}
    /* A JTM with 22 workers and a manager, planted where X4 keeps their pointers. */
    static unsigned char jtm[0x600],manager[0x400];memset(jtm,0,sizeof(jtm));unsigned native=22;memcpy(jtm+LXWL_JTM_WORKERS,&native,4);
    unsigned char *saved_jtm=NULL,*saved_manager=NULL,*p=jtm;
    for(unsigned i=0;i<2;i++) {   /* an earlier test may leave these pages read-only */
        unsigned char *at=(unsigned char*)((uintptr_t)LX(i?LX_PATH_MANAGER_VA:LXWL_JTM_VA)&~(uintptr_t)(GATE_PAGE-1));
        assert(!mprotect(at,GATE_PAGE,PROT_READ|PROT_WRITE));
    }
    memcpy(&saved_jtm,LX(LXWL_JTM_VA),8);memcpy(&saved_manager,LX(LX_PATH_MANAGER_VA),8);
    memcpy(LX(LXWL_JTM_VA),&p,8);p=manager;memcpy(LX(LX_PATH_MANAGER_VA),&p,8);
    const int saved_path=path_installed;
    twl_reset();path_installed=0;
    assert(!lxwl_arm(30,0) && !strcmp(lxwl_stage,"path") && !lxwl_armed);   /* no pathing release hook: refused */
    path_installed=1;
    assert(!lxwl_arm(3,0) && !lxwl_arm(30,33) && !strcmp(lxwl_stage,"value"));
    assert(lxwl_arm(30,0) && lxwl_armed && lxwl_jolt && lxwl_native_workers==22 && lxwl_layout[0]==10 && lxwl_layout[1]==20 && lxwl_pending);
    twl_concurrency_fn get=*(twl_concurrency_fn*)LX(LXWL_JOLT_SLOT_VA);assert(get(NULL)==30);
    assert(lxwl_arm(30,0) && !lxwl_arm(22,0));   /* only the same W again */
    {   /* Someone else's pointer in Jolt's slot: the step stops; the restore leaves it alone. */
        lxwl_applied=1;const uint64_t ours=rd64(LX(LXWL_JOLT_SLOT_VA)),other=(uint64_t)(uintptr_t)twl_dtor;
        assert(!mprotect(page,GATE_PAGE,PROT_READ|PROT_WRITE));memcpy(LX(LXWL_JOLT_SLOT_VA),&other,8);
        assert(!lxwl_check() && !strcmp(lxwl_stage,"slot_changed"));lxwl_restore();assert(lxwl_jolt && !strcmp(lxwl_stage,"restore_foreign"));
        assert(!mprotect(page,GATE_PAGE,PROT_READ|PROT_WRITE));memcpy(LX(LXWL_JOLT_SLOT_VA),&ours,8);
    }
    lxwl_restore();assert(!lxwl_jolt && !lxwl_armed && lxwl_restored && rd64(LX(LXWL_JOLT_SLOT_VA))==(uint64_t)(uintptr_t)LX(LXWL_JOLT_VA));
    twl_reset();path_installed=saved_path;
    memcpy(LX(LXWL_JTM_VA),&saved_jtm,8);memcpy(LX(LX_PATH_MANAGER_VA),&saved_manager,8);
    assert(!mprotect(page,GATE_PAGE,PROT_READ|PROT_WRITE));{const uint64_t va=LXWL_JOLT_VA;memcpy(LX(LXWL_JOLT_SLOT_VA),&va,8);}
    printf("worker layout ELF: pins, the arm with the path hook, Jolt's slot swapped and restored PASS\n");
}
