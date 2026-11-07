/* Hot join (lockstep_join.h): the package byte for byte against native_gate_join.py (golden), its hash and shape
 * refusals, capture from a synthetic AI director, context list and MD scheduler with the engine's layouts (the
 * corrected ones after J1 build 177: the global context list, cues keyed by creator lists and sub-cue slots; after j1-7:
 * static cues by their script and name path), the joiner's restore
 * into a differently loaded synthetic world (other addresses, list orders, tree order, vectors, heap, counter), the
 * refusal diagnostics, the identity mapping, the stand-in proposal rule, and the pins and layouts on the X4 9.00 image.
 * No engine code runs: the tree's insert and erase are a plain binary search tree with the engine's node layout. */
#define TJ_GOLDEN "1f993578efc161e25c224738cfd194775894160839572c3e0dec1ea5006eeab3"   /* version 13 (JOBE .. PCSV; 12: SPLM .. GRAVQ) */
static uint64_t tj_bits(double v) {uint64_t b;memcpy(&b,&v,8);return b;}
static uint32_t tj_f32(float v) {uint32_t b;memcpy(&b,&v,4);return b;}
/* ---- a synthetic AI director ---- */
static unsigned char tj_dir[0x300],tj_sched[0x100];
static uint32_t tj_seq;
static uintptr_t tj_ctx_head,tj_ctx_tail;        /* AI::g_AIScriptContextList's head and tail */
static const uintptr_t tj_ctx_vt=0x7a7a7a7a,tj_cue_vt=0x7b7b7b7b;
typedef struct {unsigned char b[0x200];} tj_ctx_t;
typedef struct {unsigned char b[0x40];} tj_entity_t;
typedef struct {unsigned char b[0x2a0];} tj_cue_t;
static uint64_t tj_key_of(uint64_t runtime,unsigned *klass) {*klass=runtime>=0x9000?WORLD_KEY_CREATED:WORLD_KEY_SAVED;return runtime+0x100000;}
static uint64_t tj_script_of(uintptr_t ctx) {uintptr_t s;memcpy(&s,(void *)(ctx+JOIN_CTX_SCRIPT),8);uint64_t h;memcpy(&h,(void *)(s+0x10),8);return h;}
/* An MSVC std::string as X4 holds a static cue's name (+0xc8) and an MD script's (+0x18): the text inline while the
 * capacity is below 16, else behind the pointer at +0 (heap = 1: that form even for a short text, as after a shrink). */
static void tj_string(uintptr_t at,const char *text,int heap) {
    uint64_t size=strlen(text),cap=size<16 && !heap?15:size<16?16:size;memset((void *)at,0,32);
    if(cap<16) memcpy((void *)at,text,size);else {uintptr_t p=(uintptr_t)text;memcpy((void *)at,&p,8);}
    memcpy((void *)(at+16),&size,8);memcpy((void *)(at+24),&cap,8);
}
static uint64_t tj_fnv(const char *text) {uint64_t h=14695981039346656037ULL;for(;*text;text++) h=(h^(unsigned char)*text)*1099511628211ULL;return h;}
static uintptr_t tj_get(uintptr_t p,unsigned off) {uintptr_t v;memcpy(&v,(void *)(p+off),8);return v;}
static void tj_set(uintptr_t p,unsigned off,uintptr_t v) {memcpy((void *)(p+off),&v,8);}
static void *tj_insert(void *tree,void **out,const double *key,const uintptr_t *ctx) {
    uintptr_t h=(uintptr_t)tree,node=tj_get(h,0x10);
    if(node) tj_set(h,0x10,tj_get(node,0x10));else node=(uintptr_t)calloc(1,0x30);
    memset((void *)node,0,0x30);memcpy((void *)(node+0x20),key,8);tj_set(node,0x28,*ctx);
    uintptr_t cur=tj_get(h,8),parent=h;int right=0;
    while(cur) {parent=cur;double k;memcpy(&k,(void *)(cur+0x20),8);right=k<=*key;cur=tj_get(cur,right?0x10:8);}
    tj_set(node,0,parent);
    if(parent==h) {tj_set(h,8,node);tj_set(h,0x20,node);}
    else {tj_set(parent,right?0x10:8,node);if(!right && tj_get(h,0x20)==parent) tj_set(h,0x20,node);}
    tj_set(h,0x28,tj_get(h,0x28)+1);*out=(void *)node;return out;
}
static void tj_transplant(uintptr_t h,uintptr_t u,uintptr_t v) {
    uintptr_t p=tj_get(u,0);
    if(p==h) tj_set(h,8,v);else if(tj_get(p,8)==u) tj_set(p,8,v);else tj_set(p,0x10,v);
    if(v) tj_set(v,0,p);
}
static void *tj_erase(void *tree,void **out,void *nodep) {
    uintptr_t h=(uintptr_t)tree,z=(uintptr_t)nodep,next=join_tree_next(z,h);
    if(!tj_get(z,8)) tj_transplant(h,z,tj_get(z,0x10));
    else if(!tj_get(z,0x10)) tj_transplant(h,z,tj_get(z,8));
    else {
        uintptr_t y=tj_get(z,0x10);while(tj_get(y,8)) y=tj_get(y,8);
        if(tj_get(y,0)!=z) {tj_transplant(h,y,tj_get(y,0x10));tj_set(y,0x10,tj_get(z,0x10));tj_set(tj_get(y,0x10),0,y);}
        tj_transplant(h,z,y);tj_set(y,8,tj_get(z,8));tj_set(tj_get(y,8),0,y);
    }
    if(tj_get(h,0x20)==z) tj_set(h,0x20,next);
    memset((void *)z,0,0x30);tj_set(z,0x10,tj_get(h,0x10));tj_set(h,0x10,z);tj_set(h,0x28,tj_get(h,0x28)-1);*out=(void *)next;return out;
}
static join_engine tj_engine(void) {
    join_engine e;memset(&e,0,sizeof(e));e.director=tj_dir;e.scheduler=tj_sched;e.md_seq=&tj_seq;e.ctx_vtable=tj_ctx_vt;e.cue_vtable=tj_cue_vt;
    e.ctx_head=(uintptr_t)&tj_ctx_head;e.ctx_tail=(uintptr_t)&tj_ctx_tail;e.insert=tj_insert;e.erase=tj_erase;return e;
}
/* A world. AI: 6 contexts (one without a script) on the global list (the session's in order 0..5, the joiner's in reverse),
 * scripts, entities. MD, by role (the joiner keeps every cue at another address):
 *   script 'TJ_Script': static A 'a' (sub-cue F, nameless), static B 'b' instantiating (sub-cues C 'c', D nameless,
 *   E 'e' instantiating; the loader makes no nameless cue, they test the slot fallback);
 *   B created the roots I1, I2 (B's +0x1a8 = [I1, I2] on both, as the save writes it and the import rebuilds it);
 *   I1's sub-cues [C1, -, -], I2's [C2, D2, E2]; the nested E2 created the root J (its +0x1a8: the session's [J], the
 *   joiner's [empty, J]: the index counts the non-empty entries, as the save writes them). */
enum {TJ_A,TJ_B,TJ_C,TJ_D,TJ_E,TJ_F,TJ_I1,TJ_I2,TJ_C1,TJ_C2,TJ_D2,TJ_E2,TJ_J,TJ_CUES};
typedef struct {tj_ctx_t ctx[6];tj_entity_t ent[6];unsigned char script[3][0x60],mdscript[0x40];uintptr_t decoy[2];uintptr_t vec[2][4];
                tj_cue_t cue[TJ_CUES];unsigned slot[TJ_CUES];uintptr_t kids[4][3],created[2][2];unsigned char heap[10][24];int joiner;} tj_world;
static uintptr_t tj_cue(const tj_world *w,unsigned role) {return (uintptr_t)&w->cue[w->slot[role]];}
static void tj_vector(uintptr_t at,const uintptr_t *items,unsigned n) {tj_set(at,0,(uintptr_t)items);tj_set(at,8,(uintptr_t)(items+n));}
/* The context list in the world's order, with its links both ways. */
static void tj_link(tj_world *w) {
    uintptr_t prev=0;tj_ctx_head=0;
    for(int k=0;k<6;k++) {
        uintptr_t c=(uintptr_t)&w->ctx[w->joiner?5-k:k];
        tj_set(c,JOIN_CTX_PREV,prev);tj_set(c,JOIN_CTX_NEXT,0);
        if(prev) tj_set(prev,JOIN_CTX_NEXT,c);else tj_ctx_head=c;
        prev=c;
    }
    tj_ctx_tail=prev;
}
static void tj_world_build(tj_world *w,int joiner) {
    memset(w,0,sizeof(*w));w->joiner=joiner;uint64_t names[3]={0x5ca1,0x5cb2,0x5cc3};
    for(int s=0;s<3;s++) {memcpy(w->script[s]+0x10,&names[s],8);uint32_t version=s+1;memcpy(w->script[s]+0x58,&version,4);}
    for(int i=0;i<6;i++) {
        uintptr_t c=(uintptr_t)&w->ctx[i];tj_set(c,0,tj_ctx_vt);tj_set(c,8,1);
        uint64_t runtime=i<5?0x1000+i:0x9001;memcpy(w->ent[i].b+8,&runtime,8);tj_set(c,JOIN_CTX_ENTITY,(uintptr_t)&w->ent[i]);
        if(i!=2) tj_set(c,JOIN_CTX_SCRIPT,(uintptr_t)w->script[i%3]);
        double none=-1.0;memcpy(w->ctx[i].b+JOIN_CTX_DUE,&none,8);tj_set(c,JOIN_CTX_NODE,(uintptr_t)(tj_dir+JOIN_DIR_TREE));
        int64_t m1=-1;memcpy(w->ctx[i].b+JOIN_CTX_VEC,&m1,8);memcpy(w->ctx[i].b+JOIN_CTX_VEC+8,&m1,8);
    }
    w->decoy[0]=(uintptr_t)&w->ctx[0];w->decoy[1]=(uintptr_t)&w->ctx[1];
    for(unsigned r=0;r<TJ_CUES;r++) w->slot[r]=joiner?TJ_CUES-1-r:r;
    static const char *const cue_names[TJ_F+1]={"a","b","c","","e",""};
    tj_string((uintptr_t)w->mdscript+JOIN_SCRIPT_NAME,"TJ_Script",joiner);   /* the joiner's strings in the pointer form */
    for(unsigned r=0;r<TJ_CUES;r++) {
        uintptr_t c=tj_cue(w,r);uint32_t state=r==TJ_A || r==TJ_D2?1:2;double act=10.0+r;
        tj_set(c,0,tj_cue_vt);memcpy((void *)(c+JOIN_CUE_STATE),&state,4);memcpy((void *)(c+JOIN_CUE_ACTIVATION),&act,8);
        if(r<=TJ_F) {tj_set(c,JOIN_CUE_DEF,c);tj_string(c+JOIN_CUE_NAME,cue_names[r],joiner);tj_set(c,JOIN_CUE_SCRIPT,(uintptr_t)w->mdscript);}
    }
    uintptr_t A=tj_cue(w,TJ_A),B=tj_cue(w,TJ_B),I1=tj_cue(w,TJ_I1),I2=tj_cue(w,TJ_I2),E2=tj_cue(w,TJ_E2),J=tj_cue(w,TJ_J);
    tj_set(tj_cue(w,TJ_C),JOIN_CUE_PARENT,B);tj_set(tj_cue(w,TJ_D),JOIN_CUE_PARENT,B);tj_set(tj_cue(w,TJ_E),JOIN_CUE_PARENT,B);
    tj_set(tj_cue(w,TJ_F),JOIN_CUE_PARENT,A);
    w->kids[0][0]=tj_cue(w,TJ_C);w->kids[0][1]=tj_cue(w,TJ_D);w->kids[0][2]=tj_cue(w,TJ_E);tj_vector(B+JOIN_CUE_CHILDREN,w->kids[0],3);
    w->kids[1][0]=tj_cue(w,TJ_F);tj_vector(A+JOIN_CUE_CHILDREN,w->kids[1],1);
    static const unsigned defs[][2]={{TJ_I1,TJ_B},{TJ_I2,TJ_B},{TJ_C1,TJ_C},{TJ_C2,TJ_C},{TJ_D2,TJ_D},{TJ_E2,TJ_E},{TJ_J,TJ_E}};
    for(unsigned i=0;i<sizeof(defs)/sizeof(*defs);i++) tj_set(tj_cue(w,defs[i][0]),JOIN_CUE_DEF,tj_cue(w,defs[i][1]));
    static const unsigned roots[][2]={{TJ_I1,TJ_I1},{TJ_I2,TJ_I2},{TJ_C1,TJ_I1},{TJ_C2,TJ_I2},{TJ_D2,TJ_I2},{TJ_E2,TJ_I2},{TJ_J,TJ_J}};
    for(unsigned i=0;i<sizeof(roots)/sizeof(*roots);i++) tj_set(tj_cue(w,roots[i][0]),JOIN_CUE_ROOT,tj_cue(w,roots[i][1]));
    tj_set(tj_cue(w,TJ_C1),JOIN_CUE_PARENT,I1);
    tj_set(tj_cue(w,TJ_C2),JOIN_CUE_PARENT,I2);tj_set(tj_cue(w,TJ_D2),JOIN_CUE_PARENT,I2);tj_set(E2,JOIN_CUE_PARENT,I2);tj_set(J,JOIN_CUE_PARENT,I2);
    w->kids[2][0]=tj_cue(w,TJ_C1);tj_vector(I1+JOIN_CUE_CHILDREN,w->kids[2],3);
    w->kids[3][0]=tj_cue(w,TJ_C2);w->kids[3][1]=tj_cue(w,TJ_D2);w->kids[3][2]=E2;tj_vector(I2+JOIN_CUE_CHILDREN,w->kids[3],3);
    tj_set(I1,JOIN_CUE_CREATOR,B);tj_set(I2,JOIN_CUE_CREATOR,B);tj_set(J,JOIN_CUE_CREATOR,E2);
    w->created[0][0]=I1;w->created[0][1]=I2;tj_vector(B+JOIN_CUE_INSTANCES,w->created[0],2);
    if(joiner) {w->created[1][1]=J;tj_vector(E2+JOIN_CUE_INSTANCES,w->created[1],2);}
    else {w->created[1][0]=J;tj_vector(E2+JOIN_CUE_INSTANCES,w->created[1],1);}
}
static void tj_heap_entry(tj_world *w,int i,double time,uint32_t seq,uint32_t kind,uintptr_t cue) {
    memcpy(w->heap[i],&time,8);memcpy(w->heap[i]+8,&seq,4);memcpy(w->heap[i]+12,&kind,4);memcpy(w->heap[i]+16,&cue,8);
}
static void tj_install(tj_world *w,unsigned heap_entries) {
    memset(tj_dir,0,sizeof(tj_dir));memset(tj_sched,0,sizeof(tj_sched));tj_link(w);
    /* +0x148 is not the context list (a vector of flagged contexts): a decoy the capture must not read. */
    tj_set((uintptr_t)tj_dir,0x148,(uintptr_t)w->decoy);tj_set((uintptr_t)tj_dir,0x150,(uintptr_t)(w->decoy+2));
    tj_set((uintptr_t)(tj_dir+JOIN_DIR_TREE),JOIN_TREE_FIRST,(uintptr_t)(tj_dir+JOIN_DIR_TREE));
    tj_set((uintptr_t)tj_sched,JOIN_MD_HEAP,(uintptr_t)w->heap);tj_set((uintptr_t)tj_sched,JOIN_MD_HEAP+8,(uintptr_t)(w->heap[0]+24*heap_entries));
    for(int v=0;v<2;v++) {tj_set((uintptr_t)tj_dir,JOIN_DIR_VEC+v*0x18,(uintptr_t)w->vec[v]);tj_set((uintptr_t)tj_dir,JOIN_DIR_VEC+v*0x18+8,(uintptr_t)(w->vec[v]+4));}
}
static void tj_wake(tj_world *w,int i,double due,uint32_t reason) {
    void *node=NULL;uintptr_t ctx=(uintptr_t)&w->ctx[i];tj_insert(tj_dir+JOIN_DIR_TREE,&node,&due,&ctx);
    tj_set(ctx,JOIN_CTX_NODE,(uintptr_t)node);memcpy(w->ctx[i].b+JOIN_CTX_DUE,&due,8);memcpy(w->ctx[i].b+JOIN_CTX_REASON,&reason,4);
}
static void tj_tree_order(int *out,unsigned *count,const tj_world *w,double *dues) {
    uintptr_t h=(uintptr_t)(tj_dir+JOIN_DIR_TREE),n=tj_get(h,JOIN_TREE_FIRST);*count=0;
    while(n && n!=h) {
        uintptr_t c=tj_get(n,JOIN_NODE_CTX);for(int i=0;i<6;i++) if(c==(uintptr_t)&w->ctx[i]) out[*count]=i;
        memcpy(&dues[*count],(void *)(n+JOIN_NODE_KEY),8);++*count;n=join_tree_next(n,h);
    }
}
static double tj_precise_native(const char *s) {(void)s;return 1.5;}
static void test_join_precise(void) {
    char d[32];unsigned n=0;int e=0;
    assert(join_precise_digits("25.849666666666664",d,&n,&e) && n==17 && e==2 && !memcmp(d,"25849666666666664",17));
    assert(join_precise_digits("1.2345678901234567e-05",d,&n,&e) && n==17 && e==-4);
    assert(join_precise_digits("-0.00012300000000000001",d,&n,&e) && n==17 && e==-3);
    assert(join_precise_digits("25.8497",d,&n,&e) && n==6 && e==2);
    assert(!join_precise_digits("0",d,&n,&e) && !join_precise_digits("abc",d,&n,&e) && !join_precise_digits("1.2.3",d,&n,&e));
    const double v=25.849666666666664;char b[64];
    snprintf(b,sizeof(b),"%.17g",v);assert(join_precise_digits(b,d,&n,&e) && join_precise_names(v,d,n,e));
    assert(!join_precise_names(nextafter(v,0.0),d,n,e));
    assert(join_fmt_precision(0.5)==16u && join_fmt_precision(25.8)==15u && join_fmt_precision(1234.5)==13u && join_fmt_precision(1e20)==0u);
    /* the parse: a long string back to the very double, a short one left to the native parser */
    double (*saved)(const char *)=join_parse_next;
    join_parse_next=tj_precise_native;join_parse_on=1;
    assert(join_parse_hook(b)==v && join_parse_hook("25.8497")==1.5);
    join_parse_on=0;assert(join_parse_hook(b)==1.5);
    join_parse_next=saved;
    printf("hot join precise save: 17-digit names, exponents, the parse of long strings only, the precision argument PASS\n");
}
static void test_join_golden(void) {
    join_tables t;memset(&t,0,sizeof(t));
    t.gate.tick=600;t.gate.game=tj_bits(27.0);t.gate.real=tj_bits(120.0);t.gate.wire_version=3;t.gate.clients=3;t.gate.lookahead=2;t.gate.tick_hz=30;
    t.gate.slot=1;t.gate.md_seq=4242;t.gate.rid_next=0x7f0001;t.gate.created_generation=5;t.gate.created_live=1;t.gate.identities=2;t.gate.ctx_count=2;
    t.gate.wake_count=1;t.gate.vec_index=1;t.gate.vec0=1;t.gate.md_count=2;t.gate.md_void=1;memset(t.gate.digest,'c',64);t.gate.save_us=1500000;t.gate.capture_us=2500;
    for(int i=0;i<12;i++) t.gate.fixture[i]=i==7?SNAPSHOT_VIEW_DRIFT:SNAPSHOT_VIEW_NAV;   /* j1-9: actor 8 a wreck */
    for(int i=0;i<32;i++) t.save_sha[i]=(unsigned char)i;
    t.save_bytes=123456;
    join_id_rec ids[2]={{0x1001,0x2001,0,0},{0x1002,(1ULL<<63)|0x55,1,0}};join_ctx_rec ctx[2]={{0x2001,0xaaaa,0,3},{(1ULL<<63)|0x55,0xbbbb,1,1}};
    join_wake_rec wake[1]={{0x2001,0xaaaa,tj_bits(27.5),0,3,2,0}};join_vec_rec vec[1]={{(1ULL<<63)|0x55,0xbbbb,1,1,0,0,4,0}};
    join_md_rec md[2]={{0xcccc,0,tj_bits(28.0),tj_bits(20.0),17,2,0,2},{0xdddd,0x1234,tj_bits(27.75),tj_bits(-1.0),18,1,0,1}};
    static char modules[]="ai_wait_rng=superseded\nworld_identities=serialized\n",lua[]="identity=1|ready|00112233\n";
    t.lua=lua;t.lua_len=strlen(lua);t.nlua=1;
    t.ids=ids;t.nids=2;t.ctx=ctx;t.nctx=2;t.wake=wake;t.nwake=1;t.vec=vec;t.nvec=1;t.md=md;t.nmd=2;t.modules=modules;t.modules_len=strlen(modules);t.nmodules=2;
    /* The world side table (j1-11's shapes): a ship's recharging shield, a build storage cargo bay's empty group with the
     * loading clock as changed time, a turret's pose lanes. */
    join_health_rec health[2]={{0x2001,tj_bits(3100.0),tj_bits(1150.0),tj_bits(26.41633),tj_bits(1955.0),JOIN_HEALTH_SHIELD,1},
                               {0x2003,tj_bits(2500.0),0,tj_bits(7.083),0,JOIN_HEALTH_SHIELD,0}};
    join_pose_rec pose[1]={{0x2004,0x2a35000,1,{0}}};for(unsigned i=0;i<12;i++) pose[0].xyz[i]=0x3f800000u+i;
    t.health=health;t.nhealth=2;t.pose=pose;t.npose=1;
    /* Version 3 (j1-12's shapes, test_native_gate_join.py golden_v3): the ship's and the cargo bay's shield parameters, the ship's
     * navigation controller (cursor on the write bank, a stale parameter due, both banks, a pending transition count, a reference
     * to the cargo bay), both objects' attention (the ship an Object with its due), two first-call keys. */
    join_shield_rec shield[2]={{0x2001,tj_bits(15.0),tj_f32(1.0f),tj_f32(120.0f),{0x3f800000u,0x3f800000u,0x3f800000u},0},
                               {0x2003,0,0,0,{0x3f800000u,0x3f800000u,0x3f800000u},0}};
    join_nav_rec nav[1];memset(nav,0,sizeof(nav));
    nav[0].portable=0x2001;nav[0].refresh=tj_bits(26.98);nav[0].params_due=tj_bits(9.075415789763133);nav[0].probe_due=tj_bits(27.5);
    nav[0].relation=1;nav[0].params_on=1;
    for(unsigned i=0;i<18;i++) {nav[0].bank[0].xyz[i]=0x40000000u+i;nav[0].bank[1].xyz[i]=0x40800000u+i;}
    nav[0].bank[0].ref[0]=0x2003;nav[0].bank[0].eta=tj_bits(-1.0);nav[0].bank[0].pending_count=1;nav[0].bank[0].mode=1;nav[0].bank[0].layer=2;
    nav[0].bank[1].eta=tj_bits(-1.0);nav[0].bank[1].pending_ref=0x2003;nav[0].bank[1].pending_count=2;nav[0].bank[1].mode=1;
    nav[0].bank[1].layer=2;nav[0].bank[1].pending_state=1;
    join_attn_rec attn[2]={{0x2001,tj_bits(28.1),5,JOIN_ATTN_DUE},{0x2003,0,3,0}};
    join_key_rec keys[2]={{0x2001,JOIN_KEYS_ATTENTION_ALL,0},{0x2001,JOIN_KEYS_ATTENTION_ACTOR,1}};
    t.shield=shield;t.nshield=2;t.nav=nav;t.nnav=1;t.attn=attn;t.nattn=2;t.keys=keys;t.nkeys=2;
    /* Version 4 (j1-14's shapes, test_native_gate_join.py golden_v4): two batch vectors (two batches round-robin at 1, one empty
     * batch), a flagged batch of the ship and the cargo bay with its cursor on the cargo bay, a batch whose member's owner has no
     * portable ID, the turret's Agent on the new-agent list; a hazard region's two damage fields. */
    join_path_rec path[9]={{0,JOIN_PATH_KIND_MANAGER,0,2,0,1,0},{0,JOIN_PATH_KIND_MANAGER,1,1,0,0,0},{0,JOIN_PATH_KIND_BATCH,0,0,1,2,1},
                           {0,JOIN_PATH_KIND_BATCH,0,1,0,1,0},{0,JOIN_PATH_KIND_BATCH,1,0,0,0,0},{0x2001,JOIN_PATH_KIND_MEMBER,0,0,0,0,0},
                           {0x2003,JOIN_PATH_KIND_MEMBER,0,0,1,0,0},{0,JOIN_PATH_KIND_MEMBER,0,1,0,0,JOIN_PATH_LOCAL},
                           {0x2004,JOIN_PATH_KIND_PENDING,0,0,0,0,0}};
    join_region_rec region[2]={{0x2005,tj_bits(27.316333333333866),tj_bits(28.4),0,0},{0x2005,tj_bits(20.383),tj_bits(21.5),2,0}};
    assert(join_path_shape(path,9) && join_path_shape(path,8) && !join_path_shape(path,7) && !join_path_shape(path+1,8));   /* without the new agent: still whole */
    t.path=path;t.npath=9;t.region=region;t.nregion=2;
    /* Version 5 (j1-15's shapes, test_native_gate_join.py golden_v5): the ship's linear flight control model with one Bezier
     * segment and two parameter pairs, the turret's steering model (kind only). */
    join_flight_rec flight[2];memset(flight,0,sizeof(flight));
    flight[0].portable=0x2001;flight[0].kind=JOIN_FLIGHT_LINEAR;flight[0].segments=1;flight[0].pairs=2;
    for(unsigned i=0;i<JOIN_FLIGHT_STATE;i++) flight[0].state[i]=(unsigned char)(i*7+1);
    flight[1].portable=0x2004;flight[1].kind=0x2c4e9c8;flight[1].flags=JOIN_FLIGHT_OTHER;
    join_spline_rec spline[2];memset(spline,0,sizeof(spline));
    spline[0].portable=0x2001;for(unsigned i=0;i<JOIN_FLIGHT_SEGMENT;i++) spline[0].data[i]=(i>=0x10 && i<0x18)?0:(unsigned char)(i+3);
    spline[1].portable=0x2001;spline[1].part=1;for(unsigned i=0;i<16;i++) spline[1].data[i]=(unsigned char)(0x80+i);
    assert(join_flight_shape(flight,2,spline,2) && !join_flight_shape(flight,2,spline,1) && !join_flight_shape(flight+1,1,spline,2));
    t.flight=flight;t.nflight=2;t.spline=spline;t.nspline=2;
    /* Version 6 (j1-20's shapes, test_native_gate_join.py golden_v6): the ship's Agent (18 values 1.0 .. 18.0, the formation
     * speed mode +0x4d2 and the re-target flag +0xf33 set, one waypoint, a group of three it leads), the turret's (following a
     * formation). */
    join_agent_rec agent[2];memset(agent,0,sizeof(agent));
    agent[0].portable=0x2001;for(unsigned i=0;i<JOIN_AGENT_VALUES;i++) agent[0].value[i]=tj_f32(1.0f+(float)i);
    agent[0].flag[2]=1;agent[0].flag[4]=1;agent[0].waypoints=1;agent[0].members=3;agent[1].portable=0x2004;agent[1].formation=1;
    assert(join_agent_shape(flight,2,agent,2) && !join_agent_shape(flight,2,agent,1) && !join_agent_shape(flight+1,1,agent,1));
    t.agent=agent;t.nagent=2;
    /* Version 7 (j1-21's shapes, test_native_gate_join.py golden_v7): the ship's ROUTE head (its linear model's word the cargo bay,
     * +0xef8 the hazard region, the bytes +0xf5c, +0xf74, +0xf75 set, +0xf70 7) and its one waypoint (the cargo bay, a sub-point on
     * the hazard region), the turret's head (+0xf48 without a portable ID). */
    join_route_rec route[3];memset(route,0,sizeof(route));
    route[0].portable=0x2001;route[0].kind=JOIN_ROUTE_HEAD;route[0].flags=JOIN_ROUTE_LINEAR;route[0].ref[0]=0x2003;route[0].ref[1]=0x2005;
    {const uint16_t n[2]={1,0};const unsigned char bytes[6]={1,0,1,1,0,0};const uint32_t f70=7;
     memcpy(route[0].data,n,4);memcpy(route[0].data+4,bytes,6);memcpy(route[0].data+12,&f70,4);}
    route[1].portable=0x2001;route[1].kind=JOIN_ROUTE_POINT;route[1].flags=JOIN_ROUTE_SUB;route[1].ref[0]=0x2003;route[1].ref[1]=0x2005;
    for(unsigned i=0;i<0x68;i++) route[1].data[i]=(unsigned char)(i*5+2);
    route[1].data[0x48+0xf]=0;for(unsigned i=0;i<0x40;i++) route[1].data[0x68+i]=(unsigned char)(0x40+i);
    route[2].portable=0x2004;route[2].kind=JOIN_ROUTE_HEAD;route[2].flags=1u<<3;
    assert(join_route_shape(agent,2,route,3) && !join_route_shape(agent,2,route,2) && !join_route_shape(agent+1,1,route,3) &&
           join_route_shape(agent+1,1,route+2,1));
    t.route=route;t.nroute=3;
    /* Version 8 (j1-22's shapes, test_native_gate_join.py golden_v8): the ship's PARAM (its read slot's object bytes i*3+1, refresh
     * time 27.9, an ID without a portable ID; its own copy's turn rates 0.5, 0.25, 0.125, the ID the cargo bay, time 28.05, the mode
     * byte +0x190), the turret's (all 0); the weapon's dock controller (start 13.950000000001, animation 3, the first option, lanes
     * 1.0 + k ulp). */
    static join_param_rec param[2];memset(param,0,sizeof(param));
    param[0].portable=0x2001;param[0].flags=1;param[1].portable=0x2004;
    for(unsigned i=0;i<JOIN_PARAM_OBJECT;i++) param[0].slot[0][i]=(unsigned char)(i*3+1);
    {uint64_t time=tj_bits(27.9),id=0x2003,own=tj_bits(28.05);const float turn[3]={0.5f,0.25f,0.125f};
     memcpy(param[0].slot[0]+JOIN_PARAMB_TIME,&time,8);memcpy(param[0].slot[2]+JOIN_PARAM_TURN,turn,12);memcpy(param[0].slot[2]+JOIN_PARAMB_ID,&id,8);
     memcpy(param[0].slot[2]+JOIN_PARAMB_TIME,&own,8);param[0].slot[2][JOIN_PARAMB_TAIL+0x10]=1;}
    assert(join_param_shape(agent,2,param,2) && !join_param_shape(agent,2,param,1) && !join_param_shape(agent+1,1,param,1));
    t.param=param;t.nparam=2;
    join_dock_rec dock[1];memset(dock,0,sizeof(dock));
    dock[0].portable=0x2002;dock[0].kind=DOCK_CONTROLLER_VTABLE;dock[0].time=tj_bits(13.950000000001);dock[0].animation=3;dock[0].option[0]=1;
    for(unsigned k=0;k<JOIN_DOCK_VECTORS*3;k++) dock[0].xyz[k]=0x3f800000u+k;
    assert(join_dock_shape(dock,1));
    t.dock=dock;t.ndock=1;
    /* Version 9 (j1-25's shapes, test_native_gate_join.py golden_v9): the turret's steering model (its carried bytes i*11+3, the
     * target the ship, the second ID without a portable ID, a path job pending, three contacts), a chase behaviour (its carried
     * bytes i*5+1) and the generic default (7, 8, 9), one path point (ROUTE's golden point, list 2). */
    static join_steer_rec steer[1];memset(steer,0,sizeof(steer));
    steer[0].portable=0x2004;steer[0].flags=2u|JOIN_STEER_JOB;steer[0].kind=JOIN_STEER_VTABLE;steer[0].ref[0]=0x2001;
    steer[0].behaviour[0]=0x2c4e840u;steer[0].behaviour[1]=0x2c4edc0u;steer[0].points=1;steer[0].contacts=3;
    assert(join_steer_bytes(join_steer_ranges,JOIN_STEER_NRANGES)==569 && join_steer_bytes(join_steer_classes[2].r,join_steer_classes[2].n)==94);
    for(unsigned i=0;i<569;i++) steer[0].model[i]=(unsigned char)(i*11+3);
    for(unsigned i=0;i<94;i++) steer[0].state[0][i]=(unsigned char)(i*5+1);
    for(unsigned i=0;i<3;i++) steer[0].state[1][i]=(unsigned char)(i+7);
    join_route_rec steerpt[1];steerpt[0]=route[1];steerpt[0].portable=0x2004;steerpt[0].list=JOIN_STEER_LIST;
    assert(join_steer_shape(flight,2,steer,1,steerpt,1) && !join_steer_shape(flight,2,steer,1,steerpt,0) && !join_steer_shape(flight,1,steer,1,steerpt,1) &&
           !join_steer_shape(flight,2,steer,1,route+1,1));
    steer[0].state[1][3]=1;assert(!join_steer_shape(flight,2,steer,1,steerpt,1));steer[0].state[1][3]=0;   /* past the generic class's bytes */
    steer[0].model[569]=1;assert(!join_steer_shape(flight,2,steer,1,steerpt,1));steer[0].model[569]=0;
    steer[0].ref[1]=0x2003;assert(!join_steer_shape(flight,2,steer,1,steerpt,1));steer[0].ref[1]=0;   /* beside its unmapped bit */
    steer[0].behaviour[1]=0x2c4ee80u;assert(!join_steer_shape(flight,2,steer,1,steerpt,1));           /* an unknown class not flagged */
    steer[0].flags|=JOIN_STEER_UNCARRIED1;assert(!join_steer_shape(flight,2,steer,1,steerpt,1));      /* flagged, its state not 0 */
    steer[0].behaviour[1]=0x2c4edc0u;steer[0].flags&=~JOIN_STEER_UNCARRIED1;
    steer[0].flags|=JOIN_STEER_POINTS;assert(!join_steer_shape(flight,2,steer,1,steerpt,1) && join_steer_shape(flight,2,steer,1,steerpt,0));
    steer[0].flags&=~JOIN_STEER_POINTS;
    t.steer=steer;t.nsteer=1;t.steerpt=steerpt;t.nsteerpt=1;
    /* Version 10 (j1-27's shapes, test_native_gate_join.py golden_v10): the turret's motion solver (bytes i*7+1, a pointer into itself at
     * +0x16a0 and a vtable at +0x6a00), two bullets in the cargo bay's zone: a linear one with its shooter, macro and bytes, and one
     * with nothing readable. */
    static join_solver_rec solver[1];memset(solver,0,sizeof(solver));
    solver[0].portable=0x2004;solver[0].flags=JOIN_SOLVER_ACTIVE;solver[0].kind=JOIN_STEER_VTABLE;solver[0].pointers=2;
    for(unsigned i=0;i<JOIN_SOLVER_BYTES;i++) solver[0].data[i]=(unsigned char)(i*7+1);
    solver[0].pointer[0]=(0x16a0u/8u)<<1;solver[0].pointer[1]=(0x6a00u/8u)<<1|1u;
    {uint64_t q=0x3e8;memcpy(solver[0].data+0x16a0,&q,8);q=0x2c1f0f8;memcpy(solver[0].data+0x6a00,&q,8);}
    static join_bullet_rec bullet[2];memset(bullet,0,sizeof(bullet));
    bullet[0].zone=0x2003;bullet[0].shooter=0x2004;bullet[0].position=3;bullet[0].motion_kind=0x2c3f7c0u;
    strcpy(bullet[0].macro,"bullet_ter_s_gatling_01_mk1_macro");
    for(unsigned i=0;i<JOIN_BULLET_BYTES;i++) bullet[0].bullet[i]=(unsigned char)(i*3+5);
    for(unsigned i=0;i<JOIN_BULLET_MOTION;i++) bullet[0].motion[i]=(unsigned char)(i*9+2);
    bullet[1].zone=0x2003;bullet[1].position=7;bullet[1].index=1;bullet[1].flags=JOIN_BULLET_NO_SHOOTER|JOIN_BULLET_NO_MACRO|JOIN_BULLET_NO_MOTION;
    assert(join_solver_shape(solver,1) && join_bullet_shape(bullet,2));
    solver[0].reserved=1;assert(!join_solver_shape(solver,1));solver[0].reserved=0;
    solver[0].flags|=4u;assert(!join_solver_shape(solver,1));solver[0].flags=JOIN_SOLVER_ACTIVE;
    solver[0].pointer[2]=1u;assert(!join_solver_shape(solver,1));solver[0].pointer[2]=0;                 /* past the count */
    {uint32_t e=solver[0].pointer[0];solver[0].pointer[0]=solver[0].pointer[1];solver[0].pointer[1]=e;assert(!join_solver_shape(solver,1));
     solver[0].pointer[1]=solver[0].pointer[0];solver[0].pointer[0]=e;}                                   /* out of offset order */
    {uint64_t q=JOIN_SOLVER_BYTES;memcpy(solver[0].data+0x16a0,&q,8);assert(!join_solver_shape(solver,1));q=0x3e8;memcpy(solver[0].data+0x16a0,&q,8);}   /* out of the object */
    solver[0].flags|=JOIN_SOLVER_OVERFLOW;assert(!join_solver_shape(solver,1));solver[0].flags=JOIN_SOLVER_ACTIVE;   /* overflow beside a list */
    bullet[1].index=2;assert(!join_bullet_shape(bullet,2));bullet[1].index=1;
    bullet[1].position=3;assert(!join_bullet_shape(bullet,2));bullet[1].position=7;
    bullet[1].shooter=1;assert(!join_bullet_shape(bullet,2));bullet[1].shooter=0;
    bullet[0].macro[40]=1;assert(!join_bullet_shape(bullet,2));bullet[0].macro[40]=0;
    bullet[1].macro[0]='x';assert(!join_bullet_shape(bullet,2));bullet[1].macro[0]=0;                     /* a name with its bit */
    t.solver=solver;t.nsolver=1;t.bullet=bullet;t.nbullet=2;
    /* Version 11 (j1-32's shapes, test_native_gate_join.py golden_v11): an active unit owned by the turret with two primary contacts
     * (the ship, one without a portable ID) and one second contact (the cargo bay), a passive unit of the module, the active sources
     * (the cargo bay pending, an unmapped current one) and a field sector seen by the deadlines module. */
    static join_grav_rec grav[2];static join_gravc_rec gravc[3];static join_gravs_rec gravs[4];
    memset(grav,0,sizeof(grav));memset(gravc,0,sizeof(gravc));memset(gravs,0,sizeof(gravs));
    grav[0].portable=0x2004;grav[0].kind=JOIN_GRAV_ACTIVE_VT;grav[0].flags=JOIN_GRAV_ACTIVE;grav[0].count[0]=2;grav[0].count[1]=1;
    for(unsigned i=0x40;i<JOIN_GRAV_BYTES;i++) if(i<0x68 || i>=0xa8) grav[0].unit[i]=(unsigned char)(i*5+3);
    grav[1].portable=0x2005;grav[1].kind=JOIN_GRAV_PASSIVE_VT;
    for(unsigned i=0x40;i<0x68;i++) grav[1].unit[i]=(unsigned char)(i*11+1);
    gravc[0]=(join_gravc_rec){0x2004,0,0,0x2001,0,0};gravc[1]=(join_gravc_rec){0x2004,0,1,0,JOIN_GRAVC_UNMAPPED,0};gravc[2]=(join_gravc_rec){0x2004,1,0,0x2003,0,0};
    gravs[0]=(join_gravs_rec){0x2003,0,JOIN_GRAVS_PENDING,0,0,0};gravs[1]=(join_gravs_rec){0,0,JOIN_GRAVS_CURRENT,0,JOIN_GRAVS_UNMAPPED,0};
    gravs[2]=(join_gravs_rec){0x2006,0x4029000000000000ull,JOIN_GRAVS_SECTOR,7,JOIN_GRAVS_SEEN,0};
    gravs[3]=(join_gravs_rec){0,3,JOIN_GRAVS_CURSOR,40,0,0};
    assert(join_grav_shape(grav,2,gravc,3,gravs,4) && join_grav_shape(grav,2,gravc,3,gravs,3));
    gravs[3].value=40;assert(!join_grav_shape(grav,2,gravc,3,gravs,4));gravs[3].value=3;                 /* a cursor past the list */
    gravs[3].portable=1;assert(!join_grav_shape(grav,2,gravc,3,gravs,4));gravs[3].portable=0;
    {join_gravs_rec two[5];memcpy(two,gravs,sizeof(gravs));two[4]=gravs[3];assert(!join_grav_shape(grav,2,gravc,3,two,5));}   /* twice */
    gravc[1].flags=0;assert(!join_grav_shape(grav,2,gravc,3,gravs,3));gravc[1].flags=JOIN_GRAVC_UNMAPPED;   /* a contact 0 without its flag */
    assert(!join_grav_shape(grav,2,gravc,2,gravs,3));                                                     /* a counted contact missing */
    grav[1].count[0]=1;assert(!join_grav_shape(grav,2,gravc,3,gravs,3));grav[1].count[0]=0;               /* contacts on a passive unit */
    grav[1].portable=0x2004;assert(!join_grav_shape(grav,2,gravc,3,gravs,3));grav[1].portable=0x2005;     /* one owner twice */
    gravs[0].flags=JOIN_GRAVS_SEEN;assert(!join_grav_shape(grav,2,gravc,3,gravs,3));gravs[0].flags=0;    /* SEEN on a source */
    gravs[2].index=0;gravs[2].kind=JOIN_GRAVS_PENDING;assert(!join_grav_shape(grav,2,gravc,3,gravs,3));
    gravs[2].index=7;gravs[2].kind=JOIN_GRAVS_SECTOR;
    t.grav=grav;t.ngrav=2;t.gravc=gravc;t.ngravc=3;t.gravs=gravs;t.ngravs=4;
    /* Version 12 (test_native_gate_join.py golden_v12): the turret's spline model with one route entry owning a spline of one segment and
     * one pair, one segment and two pairs in its first embedded spline; one sector's zone neighbours (two zones each other's, one none);
     * the published view poses of two radar objects (the first in both views, its other view without a pose). */
    static join_splm_rec splm[1];static join_splme_rec splme[1];static join_spline_rec splmseg[4];static join_gravn_rec gravn[3];
    static join_gravp_rec gravp[3];memset(gravp,0,sizeof(gravp));
    gravp[0].portable=0x2003;gravp[0].flags=JOIN_GRAVP_PRESENT;for(unsigned i=0;i<0x40;i++) gravp[0].pose[i]=(unsigned char)(i*17+3);
    gravp[1].portable=0x2003;gravp[1].rel=1;
    gravp[2].portable=0x2006;gravp[2].flags=JOIN_GRAVP_PRESENT;for(unsigned i=0;i<0x40;i++) gravp[2].pose[i]=(unsigned char)(i*19+5);
    assert(join_gravp_shape(gravp,3));
    {join_gravp_rec bad[3];memcpy(bad,gravp,sizeof(bad));bad[1].pose[5]=1;assert(!join_gravp_shape(bad,3));      /* a pose without PRESENT */
     memcpy(bad,gravp,sizeof(bad));bad[1].rel=2;assert(!join_gravp_shape(bad,3));                              /* a third view */
     memcpy(bad,gravp,sizeof(bad));bad[0]=gravp[1];bad[1]=gravp[0];assert(!join_gravp_shape(bad,3));           /* out of order */
     memcpy(bad,gravp,sizeof(bad));bad[2].flags=2;assert(!join_gravp_shape(bad,3));}
    static join_gravq_rec gravq[3];memset(gravq,0,sizeof(gravq));   /* the radar worker's pending posts: a has_scanned post, an unmapped source, another kind */
    gravq[0]=(join_gravq_rec){0x2003,0,3,0,JOIN_GRAVQ_EVENT_VT,0x403c400000000000ull,{0,0}};
    gravq[1]=(join_gravq_rec){0,1,3,JOIN_GRAVQ_UNMAPPED,JOIN_GRAVQ_EVENT_VT,0x403c400000000000ull,{0,0}};
    gravq[2]=(join_gravq_rec){0x2006,2,1,JOIN_GRAVQ_OTHER,0,0,{0x55,0}};
    assert(join_gravq_shape(gravq,3));
    {join_gravq_rec bad[3];memcpy(bad,gravq,sizeof(bad));bad[1].index=2;assert(!join_gravq_shape(bad,3));
     memcpy(bad,gravq,sizeof(bad));bad[1].portable=0x2004;assert(!join_gravq_shape(bad,3));
     memcpy(bad,gravq,sizeof(bad));bad[2].flags=4;assert(!join_gravq_shape(bad,3));}
    static join_jobe_rec jobe[1];memset(jobe,0,sizeof(jobe));   /* the job engine's schedule: last run 23.25, cursor xenon_miner_1 */
    jobe[0].last=0x4037400000000000ull;jobe[0].flags=JOIN_JOBE_ENABLED|JOIN_JOBE_RUNNING;jobe[0].len=13;memcpy(jobe[0].cursor,"xenon_miner_1",13);
    assert(join_jobe_shape(jobe,1) && join_jobe_shape(jobe,0));
    static join_evqt_rec evqt[3];memset(evqt,0,sizeof(evqt));   /* three pending events: two of one source, one of a source without a key */
    {double t=40.0;evqt[0]=(join_evqt_rec){0,0x2ac1710u,0x20du,0,JOIN_EVQT_UNMAPPED,0};memcpy(&evqt[0].time,&t,8);
     t=31.5;evqt[1]=(join_evqt_rec){0x2001,0x2ac17b8u,0x215u,1,0,0};memcpy(&evqt[1].time,&t,8);
     t=30.25;evqt[2]=(join_evqt_rec){0x2001,0x2baf650u,0x20fu,0,0,0};memcpy(&evqt[2].time,&t,8);}
    assert(join_evqt_shape(evqt,3) && join_evqt_shape(evqt,0));
    static join_gravd_rec gravd[2];gravd[0]=(join_gravd_rec){0x2001,0x2003,0,0};gravd[1]=(join_gravd_rec){0x2001,0,1,JOIN_GRAVD_NONE};
    assert(join_gravd_shape(gravd,2));
    {join_gravd_rec bad[2]={gravd[1],gravd[0]};assert(!join_gravd_shape(bad,2));bad[0]=gravd[0];bad[1]=gravd[1];bad[1].flags=0;assert(!join_gravd_shape(bad,2));}
    static join_viewk_rec viewk[2];static join_viewkc_rec viewkc[2];
    viewk[0]=(join_viewk_rec){0x2002,0,0,0x10042u,1};viewk[1]=(join_viewk_rec){0x2002,1,JOIN_VIEWK_UNMAPPED,0x10042u,1};
    viewkc[0]=(join_viewkc_rec){0x2002,0x2003,0,0};viewkc[1]=(join_viewkc_rec){0x2002,0,1,0};
    assert(join_viewk_shape(viewk,2,viewkc,2) && join_viewk_shape(viewk,0,viewkc,0) && !join_viewk_shape(viewk,2,viewkc,1));
    {join_viewk_rec bad[2]={viewk[1],viewk[0]};assert(!join_viewk_shape(bad,2,viewkc,2));
     bad[0]=viewk[0];bad[1]=viewk[1];bad[1].flags=0;assert(!join_viewk_shape(bad,2,viewkc,2));
     bad[1]=viewk[1];bad[0].flags=JOIN_VIEWK_UNREAD;assert(!join_viewk_shape(bad,2,viewkc,2));}
    {join_viewkc_rec bad[2]={viewkc[1],viewkc[0]};assert(!join_viewk_shape(viewk,2,bad,2));}
    static join_viewf_rec viewf[2];viewf[0]=(join_viewf_rec){0x2001,{0x10058u,0x10058u},{1u,1u}};viewf[1]=(join_viewf_rec){0x2002,{0x10042u,0x10042u},{6u,6u}};
    static join_viewp_rec viewp[1];viewp[0].portable=0x2001;for(unsigned k=0;k<64u;k++) viewp[0].pose[k]=(unsigned char)k;
    static join_shoot_rec shoot[1];memset(shoot,0,sizeof(shoot));shoot[0].portable=0x2001;shoot[0].lastshot=0x4039d983c465091eull;
    shoot[0].ctl[0]=0x403c2ed916872ba4ull;shoot[0].ctl[1]=0x403c26508dfea31bull;shoot[0].ctl[2]=0x3f9eb851e0000000ull;
    shoot[0].ctl[3]=0x403c2ed916872ba4ull;shoot[0].ctl[4]=0x403bf31d5acb6fe5ull;for(unsigned k=0;k<32u;k++) shoot[0].aim[k]=(unsigned char)k;
    shoot[0].ctl_vt=0x2b7fcc0u;shoot[0].tol=0x3db2b8c2u;shoot[0].wflags[0]=shoot[0].wflags[1]=shoot[0].wflags[2]=1;shoot[0].cflags[0]=1;shoot[0].has_ctl=1;
    static join_attk_rec attk[1];memset(attk,0,sizeof(attk));attk[0].portable=0x2001;attk[0].attacker=0x2002;
    attk[0].times[0]=attk[0].times[1]=0x403b5987f5df8f7bull;attk[0].method=7u;
    static join_livef_rec livef[1];livef[0]=(join_livef_rec){0x2001,0u,0x20u};
    static join_spec_rec spec[2];spec[0]=(join_spec_rec){0x2001,3u,0x4fu,2u,0u,0x3fc5555555555555ull,0x403c26508dfea31bull};
    spec[1]=(join_spec_rec){0x2001,7u,0x7fu,0u,0u,0u,0x403c2ed916872b02ull};
    static join_navc_rec navc[1];navc[0].portable=0x2001;for(unsigned k=0;k<96u;k++) navc[0].cache[k]=(unsigned char)k;
    static join_jobs_rec jobs[1];jobs[0]=(join_jobs_rec){0xcbf29ce484222325ull,3u,524u,0u,0u};
    static join_gone_rec gone[2]={{0x6a0adull},{0x8000000000001234ull}};
    static join_body_rec body[1];body[0].portable=0x2001;body[0].which=0;body[0].flags=3;body[0].pad=1;
    for(unsigned i=0;i<8;i++) {body[0].pose[i]=0x3f800000u+i;body[0].pre[i]=0x1000u+i;body[0].delta[i]=0x2000u+i;}
    for(unsigned i=0;i<23;i++) body[0].motion[i]=0x40000000u+i;
    static join_bupd_rec bupd[2]={{0x2001,0x401c54fdf3b645a2ull},{0x2003,0x403c2ed916872ba4ull}};
    static join_ldc_rec ldc[1];ldc[0].portable=0x2001;for(unsigned i=0;i<JOIN_LDC_STATE;i++) ldc[0].state[i]=(unsigned char)i;
    static join_nmesh_rec nmesh[4]={{0xa5ec412f2588ab8dull},{16},{0x1234},{0x5678}};
    static join_noct_rec noct[4]={{3,1,0,0x67a818fdb1644740ull,0,0,JOIN_NOCT_HEAD,0,0},{1,0x2001,12,1,2,1,JOIN_NOCT_ENTRY,0,0},
                                  {2,0x2003,12,0,2,0,JOIN_NOCT_ENTRY,1,0},{0xffffffffffffffffull,0x2001,0,0,2,1,JOIN_NOCT_HIDDEN,0,0}};
    static join_navt_rec navt[1];navt[0].portable=0x2001;for(unsigned i=0;i<2*JOIN_NAVT_VECTORS*3;i++) navt[0].xyz[i/15][i%15]=0x3f800000u+i;
    {join_evqt_rec bad[3]={evqt[1],evqt[0],evqt[2]};assert(!join_evqt_shape(bad,3));
     memcpy(bad,evqt,sizeof(bad));bad[0].flags=0;assert(!join_evqt_shape(bad,3));memcpy(bad,evqt,sizeof(bad));bad[1].flags=2;assert(!join_evqt_shape(bad,3));}
    {join_jobe_rec bad=jobe[0];bad.cursor[5]=' ';assert(!join_jobe_shape(&bad,1));bad=jobe[0];bad.len=12;assert(!join_jobe_shape(&bad,1));
     bad=jobe[0];bad.flags=8;assert(!join_jobe_shape(&bad,1));join_jobe_rec two[2]={jobe[0],jobe[0]};assert(!join_jobe_shape(two,2));}
    memset(splm,0,sizeof(splm));memset(splme,0,sizeof(splme));memset(splmseg,0,sizeof(splmseg));
    splm[0].portable=0x2004;splm[0].entries[0]=1;splm[0].segments[0]=1;splm[0].pairs[0]=2;
    for(unsigned i=0;i<JOIN_SPLM_BYTES;i++) splm[0].model[i]=(unsigned char)(i*13+7);
    splme[0].portable=0x2004;splme[0].flags=JOIN_SPLME_SPLINE;splme[0].segments=1;splme[0].pairs=1;
    for(unsigned i=8;i<0x50;i++) splme[0].entry[i]=(unsigned char)(i*3+1);
    for(unsigned i=8;i<0x10;i++) splme[0].spline[i]=(unsigned char)(i*5+2);
    for(unsigned i=0x40;i<0x50;i++) splme[0].spline[i]=(unsigned char)(i*5+2);
    splmseg[0].portable=0x2004;splmseg[0].part=0;for(unsigned i=0;i<0xa0;i++) splmseg[0].data[i]=(i>=0x10 && i<0x18)?0:(unsigned char)(i*7+1);
    splmseg[1].portable=0x2004;splmseg[1].part=1;for(unsigned i=0;i<16;i++) splmseg[1].data[i]=(unsigned char)(i+1);
    splmseg[2].portable=0x2004;splmseg[2].part=4;for(unsigned i=0;i<0xa0;i++) splmseg[2].data[i]=(i>=0x10 && i<0x18)?0:(unsigned char)(i*9+4);
    splmseg[3].portable=0x2004;splmseg[3].part=5;memset(splmseg[3].data,0x11,8);
    gravn[0]=(join_gravn_rec){0x2006,0x2003,0x2005,0,0};gravn[1]=(join_gravn_rec){0x2006,0x2005,0x2003,0,0};gravn[2]=(join_gravn_rec){0x2006,0x2007,0,JOIN_GRAVN_NONE,0};
    assert(join_splm_shape(splm,1,splme,1,splmseg,4) && join_gravn_shape(gravn,3));
    assert(!join_splm_shape(splm,1,splme,0,splmseg,4) && !join_splm_shape(splm,1,splme,1,splmseg,3));
    splmseg[1].data[16]=1;assert(!join_splm_shape(splm,1,splme,1,splmseg,4));splmseg[1].data[16]=0;    /* a third pair past the count */
    splme[0].entry[0]=1;assert(!join_splm_shape(splm,1,splme,1,splmseg,4));splme[0].entry[0]=0;         /* a pointer left in the entry */
    gravn[2].flags=0;assert(!join_gravn_shape(gravn,3));gravn[2].flags=JOIN_GRAVN_NONE;
    {join_gravn_rec swap[3]={gravn[1],gravn[0],gravn[2]};assert(!join_gravn_shape(swap,3));}
    t.splm=splm;t.nsplm=1;t.splme=splme;t.nsplme=1;t.splmseg=splmseg;t.nsplmseg=4;t.gravn=gravn;t.ngravn=3;t.gravp=gravp;t.ngravp=3;t.gravq=gravq;t.ngravq=3;t.jobe=jobe;t.njobe=1;t.evqt=evqt;t.nevqt=3;t.gravd=gravd;t.ngravd=2;t.viewk=viewk;t.nviewk=2;t.viewkc=viewkc;t.nviewkc=2;t.viewf=viewf;t.nviewf=2;t.viewp=viewp;t.nviewp=1;t.shoot=shoot;t.nshoot=1;t.attk=attk;t.nattk=1;t.livef=livef;t.nlivef=1;t.spec=spec;t.nspec=2;t.navc=navc;t.nnavc=1;t.jobs=jobs;t.njobs=1;t.gone=gone;t.ngone=2;t.body=body;t.nbody=1;t.bupd=bupd;t.nbupd=2;t.ldc=ldc;t.nldc=1;t.navt=navt;t.nnavt=1;t.nmesh=nmesh;t.nnmesh=4;t.noct=noct;t.nnoct=4;
    static join_agtt_rec agtt[2]={{0x2001,{0x401c54fdf3b645a2ull,0,0x4028a1f671529a3eull}},{0x2004,{0xbff0000000000000ull,0,0}}};t.agtt=agtt;t.nagtt=2;
    static join_agcb_rec agcb[1];agcb[0].portable=0x2001;for(unsigned i=0;i<JOIN_AGCB_BYTES;i++) agcb[0].block[i]=(unsigned char)i;t.agcb=agcb;t.nagcb=1;
    static join_pbnk_rec pbnk[1];pbnk[0].portable=0x2001;
    for(unsigned role=0;role<2;role++) {
        join_nav_bank *b=&pbnk[0].bank[role];for(unsigned i=0;i<18;i++) b->xyz[i]=0x3f800000u+i;
        b->ref[0]=0x2003;b->ref[1]=0;b->eta=0x4000000000000000ull;b->pending_ref=0;b->pending_count=1;b->mode=3;b->arrived=1;b->notified=0;b->layer=2;b->pending_state=1;
    }
    for(unsigned i=0;i<30;i++) pbnk[0].tail[i/15][i%15]=0x40000000u+i;t.pbnk=pbnk;t.npbnk=1;
    static join_pend_rec pend[2]={{0x2001,0x19u,0},{0x2004,0x8u,0}};t.pend=pend;t.npend=2;
    static join_gravb_rec gravb[2]={{0x16,0,{0x454b1b00u,0x4638f638u,0x48a34182u}},{0x16,2,{0x454b1b00u,0x4638f638u,0x48970c82u}}};t.gravb=gravb;t.ngravb=2;
    static join_viewl_rec viewl[2]={{0x2001,1,1,{0}},{0x2004,1,0,{0}}};t.viewl=viewl;t.nviewl=2;
    static join_ikv_rec ikv[2];memset(ikv,0,sizeof(ikv));ikv[0].portable=ikv[1].portable=0x2001;ikv[1].joint=1;
    for(unsigned k=0;k<JOIN_IKV_STATE;k++) ikv[0].state[k]=(unsigned char)(k+1);t.ikv=ikv;t.nikv=2;
    static join_rcnt_rec rcnt[1]={{0x8000000000002001ull,31,0}};t.rcnt=rcnt;t.nrcnt=1;
    static join_ecos_rec ecos[1]={{0x2001ull,612.5,70.25}};t.ecos=ecos;t.necos=1;
    static join_shpw_rec shpw[1]={{0x2001ull,{1,2,3,4,5,6}}};t.shpw=shpw;t.nshpw=1;
    static join_trdn_rec trdn[1]={{0x2001ull,88.5}};t.trdn=trdn;t.ntrdn=1;
    static join_npca_rec npca[1];npca[0].portable=0x2001ull;npca[0].flags=3u;npca[0].index=12u;
    for(unsigned k=0;k<JOIN_NPCA_BYTES;k++) npca[0].stamps[k]=(unsigned char)k;
    t.npca=npca;t.nnpca=1;
    static join_jobg_rec jobg[1]={{0x2001ull,0x1234ull}};t.jobg=jobg;t.njobg=1;
    static join_code_rec code[1]={{77u,0x123456u}};t.code=code;t.ncode=1;
    static join_engr_rec engr[1]={{0x2001ull,37.25,0x13f800000ull}};t.engr=engr;t.nengr=1;
    static join_shgr_rec shgr[1]={{0x2001ull,0x2002ull,7u,0u,888.0,7.25}};t.shgr=shgr;t.nshgr=1;
    static join_bnds_rec bnds[1]={{0x2001ull,{0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26},0u}};t.bnds=bnds;t.nbnds=1;
    static join_omgs_rec omgs[1];omgs[0].portable=0x2001ull;omgs[0].which=0;omgs[0].pad=0;
    for(unsigned i=0;i<JOIN_OMGS_BYTES;i++) omgs[0].data[i]=(unsigned char)i;
    t.omgs=omgs;t.nomgs=1;
    static join_resv_rec resv[1];resv[0].zone=0x2001ull;resv[0].owner=0x2002ull;resv[0].index=3u;resv[0].expiry=12.5;
    for(unsigned i=0;i<JOIN_RESV_BOX_BYTES;i++) resv[0].box[i]=(unsigned char)i;
    t.resv=resv;t.nresv=1;
    static join_solver_rec pcsv[1];memset(pcsv,0,sizeof(pcsv));pcsv[0].portable=0x2001ull;pcsv[0].kind=PSLOT_VTABLE;pcsv[0].pointers=1;
    pcsv[0].pointer[0]=(0x18u/8u)<<1;pcsv[0].data[0x18]=0x40;
    t.pcsv=pcsv;t.npcsv=1;
    join_buf b={0};assert(!join_package_build(&t,&b));free(b.data);memset(&b,0,sizeof(b));   /* a version 1 table: no package of this build */
    t.version=JOIN_PACKAGE_VERSION;assert(join_package_build(&t,&b));
    unsigned char sha[32];char hex[65];lockstep_sha256(b.data,b.size,sha);hex32(sha,hex);
    if(strcmp(hex,TJ_GOLDEN)) {printf("join golden %s\n",hex);assert(0);}
    {   /* the fixture actors' flight sample generations ride the GATE record's former padding */
        join_tables f=t;f.gate.flight[3]=2;f.gate.flight[11]=1;join_buf fb={0};assert(join_package_build(&f,&fb));
        join_tables fr;const char *fw;assert(join_package_parse(fb.data,fb.size,&fr,&fw) && fr.gate.flight[3]==2 && fr.gate.flight[11]==1 && !fr.gate.flight[0]);
        join_tables_free(&fr);free(fb.data);
    }
    join_tables r;const char *why;assert(join_package_parse(b.data,b.size,&r,&why) && !strcmp(why,"ok"));
    assert(r.nflight==2 && r.nspline==2 && !memcmp(r.flight,flight,sizeof(flight)) && !memcmp(r.spline,spline,sizeof(spline)));
    assert(r.nagent==2 && !memcmp(r.agent,agent,sizeof(agent)) && r.nroute==3 && !memcmp(r.route,route,sizeof(route)));
    assert(r.nparam==2 && !memcmp(r.param,param,sizeof(param)) && r.ndock==1 && !memcmp(r.dock,dock,sizeof(dock)));
    assert(r.nsteer==1 && !memcmp(r.steer,steer,sizeof(steer)) && r.nsteerpt==1 && !memcmp(r.steerpt,steerpt,sizeof(steerpt)));
    assert(r.nsolver==1 && !memcmp(r.solver,solver,sizeof(solver)) && r.nbullet==2 && !memcmp(r.bullet,bullet,sizeof(bullet)));
    assert(r.ngrav==2 && !memcmp(r.grav,grav,sizeof(grav)) && r.ngravc==3 && !memcmp(r.gravc,gravc,sizeof(gravc)) &&
           r.ngravs==4 && !memcmp(r.gravs,gravs,sizeof(gravs)));
    assert(r.nsplm==1 && !memcmp(r.splm,splm,sizeof(splm)) && r.nsplme==1 && !memcmp(r.splme,splme,sizeof(splme)) &&
           r.nsplmseg==4 && !memcmp(r.splmseg,splmseg,sizeof(splmseg)) && r.ngravn==3 && !memcmp(r.gravn,gravn,sizeof(gravn)) &&
           r.ngravp==3 && !memcmp(r.gravp,gravp,sizeof(gravp)) && r.ngravq==3 && !memcmp(r.gravq,gravq,sizeof(gravq)) &&
           r.njobe==1 && !memcmp(r.jobe,jobe,sizeof(jobe)) && r.nevqt==3 && !memcmp(r.evqt,evqt,sizeof(evqt)) &&
           r.ngravd==2 && !memcmp(r.gravd,gravd,sizeof(gravd)) && r.nviewk==2 && !memcmp(r.viewk,viewk,sizeof(viewk)) &&
           r.nviewkc==2 && !memcmp(r.viewkc,viewkc,sizeof(viewkc)) && r.nviewf==2 && !memcmp(r.viewf,viewf,sizeof(viewf)) &&
           r.nviewp==1 && !memcmp(r.viewp,viewp,sizeof(viewp)) &&
           r.nshoot==1 && !memcmp(r.shoot,shoot,sizeof(shoot)) &&
           r.nattk==1 && !memcmp(r.attk,attk,sizeof(attk)) &&
           r.nlivef==1 && !memcmp(r.livef,livef,sizeof(livef)) &&
           r.nspec==2 && !memcmp(r.spec,spec,sizeof(spec)) &&
           r.nnavc==1 && !memcmp(r.navc,navc,sizeof(navc)) &&
           r.njobs==1 && !memcmp(r.jobs,jobs,sizeof(jobs)) &&
           r.ngone==2 && !memcmp(r.gone,gone,sizeof(gone)) &&
           r.nbody==1 && !memcmp(r.body,body,sizeof(body)) &&
           r.nbupd==2 && !memcmp(r.bupd,bupd,sizeof(bupd)) &&
           r.nldc==1 && !memcmp(r.ldc,ldc,sizeof(ldc)) &&
           r.nnavt==1 && !memcmp(r.navt,navt,sizeof(navt)) &&
           r.nnmesh==4 && !memcmp(r.nmesh,nmesh,sizeof(nmesh)) &&
           r.nnoct==4 && !memcmp(r.noct,noct,sizeof(noct)) &&
           r.nagtt==2 && !memcmp(r.agtt,agtt,sizeof(agtt)) &&
           r.nagcb==1 && !memcmp(r.agcb,agcb,sizeof(agcb)) &&
           r.npbnk==1 && !memcmp(r.pbnk,pbnk,sizeof(pbnk)) &&
           r.npend==2 && !memcmp(r.pend,pend,sizeof(pend)) &&
           r.ngravb==2 && !memcmp(r.gravb,gravb,sizeof(gravb)) &&
           r.nviewl==2 && !memcmp(r.viewl,viewl,sizeof(viewl)) &&
           r.nikv==2 && !memcmp(r.ikv,ikv,sizeof(ikv)) &&
           r.nrcnt==1 && !memcmp(r.rcnt,rcnt,sizeof(rcnt)) && r.necos==1 && !memcmp(r.ecos,ecos,sizeof(ecos)) && r.nshpw==1 && !memcmp(r.shpw,shpw,sizeof(shpw)) &&
           r.ntrdn==1 && !memcmp(r.trdn,trdn,sizeof(trdn)) &&
           r.nnpca==1 && !memcmp(r.npca,npca,sizeof(npca)) &&
           r.njobg==1 && !memcmp(r.jobg,jobg,sizeof(jobg)) &&
           r.ncode==1 && !memcmp(r.code,code,sizeof(code)) &&
           r.nengr==1 && !memcmp(r.engr,engr,sizeof(engr)) &&
           r.nshgr==1 && !memcmp(r.shgr,shgr,sizeof(shgr)) &&
           r.nbnds==1 && !memcmp(r.bnds,bnds,sizeof(bnds)) &&
           r.nomgs==1 && !memcmp(r.omgs,omgs,sizeof(omgs)) &&
           r.nresv==1 && !memcmp(r.resv,resv,sizeof(resv)) &&
           r.npcsv==1 && !memcmp(r.pcsv,pcsv,sizeof(pcsv)));
    assert(r.version==13 && r.nhealth==2 && r.npose==1 && !memcmp(r.health,health,sizeof(health)) && !memcmp(r.pose,pose,sizeof(pose)));
    assert(r.npath==9 && r.nregion==2 && !memcmp(r.path,path,sizeof(path)) && !memcmp(r.region,region,sizeof(region)));
    assert(r.nshield==2 && r.nnav==1 && r.nattn==2 && r.nkeys==2 && !memcmp(r.shield,shield,sizeof(shield)) && !memcmp(r.nav,nav,sizeof(nav)) &&
           !memcmp(r.attn,attn,sizeof(attn)) && !memcmp(r.keys,keys,sizeof(keys)));
    assert(r.nids==2 && r.nctx==2 && r.nwake==1 && r.nvec==1 && r.nmd==2 && r.nmodules==2 && r.nlua==1 && !memcmp(r.lua,lua,r.lua_len) &&
           r.gate.md_seq==4242 && r.save_bytes==123456 &&
           !memcmp(r.gate.portable,t.gate.portable,16) && r.md[1].root==0x1234 && !memcmp(r.modules,modules,r.modules_len) &&
           !memcmp(r.gate.fixture,t.gate.fixture,12));
    join_tables_free(&r);
    /* Refusals: the trailer, a section's payload (resealed trailer), the gate's echo of the header, the magic. */
    unsigned char *copy=malloc(b.size);
    memcpy(copy,b.data,b.size);copy[b.size-1]^=1;assert(!join_package_parse(copy,b.size,&r,&why) && !strcmp(why,"package_sha"));join_tables_free(&r);
    size_t ids_payload=96+48+256+48;
    memcpy(copy,b.data,b.size);copy[ids_payload]^=1;lockstep_sha256(copy,b.size-32,copy+b.size-32);
    assert(!join_package_parse(copy,b.size,&r,&why) && !strcmp(why,"section_sha"));join_tables_free(&r);
    memcpy(copy,b.data,b.size);copy[16]^=1;lockstep_sha256(copy,b.size-32,copy+b.size-32);
    assert(!join_package_parse(copy,b.size,&r,&why) && !strcmp(why,"gate_header"));join_tables_free(&r);
    memcpy(copy,b.data,b.size);copy[0]='Y';lockstep_sha256(copy,b.size-32,copy+b.size-32);
    assert(!join_package_parse(copy,b.size,&r,&why) && !strcmp(why,"package_magic"));join_tables_free(&r);
    /* A view past the last one (the GATE hash and the trailer resealed); a changed view: the portable digest. */
    size_t fixture_at=96+48+offsetof(join_gate_rec,fixture)+7;
    memcpy(copy,b.data,b.size);copy[fixture_at]=SNAPSHOT_VIEWS;lockstep_sha256(copy+96+48,256,copy+96+16);lockstep_sha256(copy,b.size-32,copy+b.size-32);
    assert(!join_package_parse(copy,b.size,&r,&why) && !strcmp(why,"gate_fixture"));join_tables_free(&r);
    memcpy(copy,b.data,b.size);copy[fixture_at]=SNAPSHOT_VIEW_NAV;lockstep_sha256(copy+96+48,256,copy+96+16);lockstep_sha256(copy,b.size-32,copy+b.size-32);
    assert(!join_package_parse(copy,b.size,&r,&why) && !strcmp(why,"portable_digest"));join_tables_free(&r);
    /* A changed record of a portable section with its own hash and the trailer resealed: the portable digest refuses. */
    memcpy(copy,b.data,b.size);
    size_t wake_payload=96+48+256+48+2*24+48;   /* header, GATE, IDS (2 records), the WAKE head */copy[wake_payload+16]^=1;lockstep_sha256(copy+wake_payload,40,copy+wake_payload-32);
    lockstep_sha256(copy,b.size-32,copy+b.size-32);
    assert(!join_package_parse(copy,b.size,&r,&why) && !strcmp(why,"portable_digest"));join_tables_free(&r);
    /* The world side table: a changed record (its section hash and the trailer resealed) changes the portable digest; records out of
     * portable order refuse; a version 1 header may not carry the world sections. */
    /* the HEALTH payload: before POSE (head + 1 record), SHIELD (2), NAV (1), ATTN (2), KEYS (2), PATH (9), REGION (2), the trailer */
    const size_t v13_tail=48+sizeof(join_jobe_rec)+48+3*sizeof(join_evqt_rec)+48+2*sizeof(join_gravd_rec)+48+2*sizeof(join_viewk_rec)+48+2*sizeof(join_viewkc_rec)+48+2*sizeof(join_viewf_rec)+48+sizeof(join_viewp_rec)+48+sizeof(join_shoot_rec)+48+sizeof(join_attk_rec)+48+sizeof(join_livef_rec)+48+2*sizeof(join_spec_rec)+48+sizeof(join_navc_rec)+48+sizeof(join_jobs_rec)+48+2*sizeof(join_gone_rec)+48+sizeof(join_body_rec)+48+2*sizeof(join_bupd_rec)+48+sizeof(join_ldc_rec)+48+sizeof(join_navt_rec)+48+4*sizeof(join_nmesh_rec)+48+4*sizeof(join_noct_rec)+48+2*sizeof(join_agtt_rec)+48+sizeof(join_agcb_rec)+48+sizeof(join_pbnk_rec)+48+2*sizeof(join_pend_rec)+48+2*sizeof(join_gravb_rec)+48+2*sizeof(join_viewl_rec)+48+2*sizeof(join_ikv_rec)+48+sizeof(join_rcnt_rec)+48+sizeof(join_ecos_rec)+48+sizeof(join_shpw_rec)+48+sizeof(join_trdn_rec)+48+sizeof(join_npca_rec)+48+sizeof(join_jobg_rec)+48+sizeof(join_code_rec)+48+sizeof(join_engr_rec)+48+sizeof(join_shgr_rec)+48+sizeof(join_bnds_rec)+48+sizeof(join_omgs_rec)+48+sizeof(join_resv_rec)+48+sizeof(join_solver_rec),v12_tail=(48+sizeof(join_splm_rec))+(48+sizeof(join_splme_rec))+(48+4*sizeof(join_spline_rec))+(48+3*sizeof(join_gravn_rec))+(48+3*sizeof(join_gravp_rec))+(48+3*sizeof(join_gravq_rec))+v13_tail,
                 v11_tail=(48+2*sizeof(join_grav_rec))+(48+3*sizeof(join_gravc_rec))+(48+4*sizeof(join_gravs_rec))+v12_tail,
                 v10_tail=(48+sizeof(join_solver_rec))+(48+2*sizeof(join_bullet_rec))+v11_tail,v9_tail=(48+832)+(48+232)+v10_tail,v8_tail=(48+2*1168)+(48+200)+v9_tail,v7_tail=48+3*232+v8_tail,v6_tail=48+2*104+v7_tail,v5_tail=(48+2*232)+(48+2*176)+v6_tail,v4_tail=(48+9*32)+(48+2*32);
    size_t health_payload=b.size-32-v5_tail-v4_tail-(48+64)-(48+2*40)-(48+280)-(48+2*24)-(48+2*16)-2*48;
    memcpy(copy,b.data,b.size);copy[health_payload+48+24]^=1;lockstep_sha256(copy+health_payload,2*48,copy+health_payload-32);
    lockstep_sha256(copy,b.size-32,copy+b.size-32);
    assert(!join_package_parse(copy,b.size,&r,&why) && !strcmp(why,"portable_digest"));join_tables_free(&r);
    memcpy(copy,b.data,b.size);memcpy(copy+health_payload,b.data+health_payload+48,48);memcpy(copy+health_payload+48,b.data+health_payload,48);
    lockstep_sha256(copy+health_payload,2*48,copy+health_payload-32);lockstep_sha256(copy,b.size-32,copy+b.size-32);
    assert(!join_package_parse(copy,b.size,&r,&why) && !strcmp(why,"world_order"));join_tables_free(&r);
    memcpy(copy,b.data,b.size);copy[8]=1;lockstep_sha256(copy,b.size-32,copy+b.size-32);
    assert(!join_package_parse(copy,b.size,&r,&why) && !strcmp(why,"section_head"));join_tables_free(&r);
    memcpy(copy,b.data,b.size);copy[8]=2;lockstep_sha256(copy,b.size-32,copy+b.size-32);   /* a version 2 header with SHIELD..KEYS */
    assert(!join_package_parse(copy,b.size,&r,&why) && !strcmp(why,"section_head"));join_tables_free(&r);
    memcpy(copy,b.data,b.size);copy[8]=3;lockstep_sha256(copy,b.size-32,copy+b.size-32);   /* a version 3 header with PATH, REGION */
    assert(!join_package_parse(copy,b.size,&r,&why) && !strcmp(why,"section_head"));join_tables_free(&r);
    memcpy(copy,b.data,b.size);copy[8]=4;lockstep_sha256(copy,b.size-32,copy+b.size-32);   /* a version 4 header with FLIGHT, SPLINE */
    assert(!join_package_parse(copy,b.size,&r,&why) && !strcmp(why,"section_head"));join_tables_free(&r);
    memcpy(copy,b.data,b.size);copy[8]=5;lockstep_sha256(copy,b.size-32,copy+b.size-32);   /* a version 5 header with AGENT */
    assert(!join_package_parse(copy,b.size,&r,&why) && !strcmp(why,"section_head"));join_tables_free(&r);
    memcpy(copy,b.data,b.size);copy[8]=6;lockstep_sha256(copy,b.size-32,copy+b.size-32);   /* a version 6 header with ROUTE */
    assert(!join_package_parse(copy,b.size,&r,&why) && !strcmp(why,"section_head"));join_tables_free(&r);
    memcpy(copy,b.data,b.size);copy[8]=7;lockstep_sha256(copy,b.size-32,copy+b.size-32);   /* a version 7 header with PARAM, DOCK */
    assert(!join_package_parse(copy,b.size,&r,&why) && !strcmp(why,"section_head"));join_tables_free(&r);
    memcpy(copy,b.data,b.size);copy[8]=8;lockstep_sha256(copy,b.size-32,copy+b.size-32);   /* a version 8 header with STEER, STEERPT */
    assert(!join_package_parse(copy,b.size,&r,&why) && !strcmp(why,"section_head"));join_tables_free(&r);
    memcpy(copy,b.data,b.size);copy[8]=9;lockstep_sha256(copy,b.size-32,copy+b.size-32);   /* a version 9 header with SOLVER, BULLET */
    assert(!join_package_parse(copy,b.size,&r,&why) && !strcmp(why,"section_head"));join_tables_free(&r);
    memcpy(copy,b.data,b.size);copy[8]=10;lockstep_sha256(copy,b.size-32,copy+b.size-32);  /* a version 10 header with GRAV, GRAVC, GRAVS */
    assert(!join_package_parse(copy,b.size,&r,&why) && !strcmp(why,"section_head"));join_tables_free(&r);
    memcpy(copy,b.data,b.size);copy[8]=12;lockstep_sha256(copy,b.size-32,copy+b.size-32);  /* a version 12 header with JOBE */
    assert(!join_package_parse(copy,b.size,&r,&why) && !strcmp(why,"section_head"));join_tables_free(&r);
    memcpy(copy,b.data,b.size);copy[8]=11;lockstep_sha256(copy,b.size-32,copy+b.size-32);  /* a version 11 header with SPLM..GRAVQ */
    assert(!join_package_parse(copy,b.size,&r,&why) && !strcmp(why,"section_head"));join_tables_free(&r);
    /* Version 3: a changed NAV record changes the portable digest; KEYS out of (table, key) order and an actor key without its
     * actor refuse (section hash and trailer resealed). */
    {
        size_t keys_payload=b.size-32-v5_tail-v4_tail-2*16,nav_payload=keys_payload-48-(48+2*24)-280;
        memcpy(copy,b.data,b.size);copy[nav_payload+32]^=1;lockstep_sha256(copy+nav_payload,280,copy+nav_payload-32);
        lockstep_sha256(copy,b.size-32,copy+b.size-32);
        assert(!join_package_parse(copy,b.size,&r,&why) && !strcmp(why,"portable_digest"));join_tables_free(&r);
        memcpy(copy,b.data,b.size);memcpy(copy+keys_payload,b.data+keys_payload+16,16);memcpy(copy+keys_payload+16,b.data+keys_payload,16);
        lockstep_sha256(copy+keys_payload,32,copy+keys_payload-32);lockstep_sha256(copy,b.size-32,copy+b.size-32);
        assert(!join_package_parse(copy,b.size,&r,&why) && !strcmp(why,"world_order"));join_tables_free(&r);
        memcpy(copy,b.data,b.size);memset(copy+keys_payload+16+12,0,4);
        lockstep_sha256(copy+keys_payload,32,copy+keys_payload-32);lockstep_sha256(copy,b.size-32,copy+b.size-32);
        assert(!join_package_parse(copy,b.size,&r,&why) && !strcmp(why,"keys_shape"));join_tables_free(&r);
    }
    {   /* Version 4: a changed PATH record changes the portable digest; a member out of place refuses (path_shape); REGION out of
         * (region, field) order refuses (world_order); a flagged REGION record refuses (region_shape). Hashes resealed. */
        size_t region_payload=b.size-32-v5_tail-2*32,path_payload=region_payload-48-9*32;   /* v5_tail holds AGENT's */
        memcpy(copy,b.data,b.size);copy[path_payload+2*32+20]^=1;   /* the flagged batch's cursor index */
        lockstep_sha256(copy+path_payload,9*32,copy+path_payload-32);lockstep_sha256(copy,b.size-32,copy+b.size-32);
        assert(!join_package_parse(copy,b.size,&r,&why) && !strcmp(why,"portable_digest"));join_tables_free(&r);
        memcpy(copy,b.data,b.size);memcpy(copy+path_payload+5*32,b.data+path_payload+6*32,32);memcpy(copy+path_payload+6*32,b.data+path_payload+5*32,32);
        lockstep_sha256(copy+path_payload,9*32,copy+path_payload-32);lockstep_sha256(copy,b.size-32,copy+b.size-32);
        assert(!join_package_parse(copy,b.size,&r,&why) && !strcmp(why,"path_shape"));join_tables_free(&r);
        memcpy(copy,b.data,b.size);memcpy(copy+region_payload,b.data+region_payload+32,32);memcpy(copy+region_payload+32,b.data+region_payload,32);
        lockstep_sha256(copy+region_payload,2*32,copy+region_payload-32);lockstep_sha256(copy,b.size-32,copy+b.size-32);
        assert(!join_package_parse(copy,b.size,&r,&why) && !strcmp(why,"world_order"));join_tables_free(&r);
        memcpy(copy,b.data,b.size);copy[region_payload+28]=1;
        lockstep_sha256(copy+region_payload,2*32,copy+region_payload-32);lockstep_sha256(copy,b.size-32,copy+b.size-32);
        assert(!join_package_parse(copy,b.size,&r,&why) && !strcmp(why,"region_shape"));join_tables_free(&r);
    }
    {   /* Version 5: a changed FLIGHT state byte changes the portable digest; a SPLINE segment with its vtable bytes set, or out of
         * (portable, part, index) order, refuses (flight_shape). Hashes resealed. */
        size_t spline_payload=b.size-32-v6_tail-2*176,flight_payload=spline_payload-48-2*232;
        memcpy(copy,b.data,b.size);copy[flight_payload+24+0x88-0x68+0x40]^=1;   /* the linear model's last step time */
        lockstep_sha256(copy+flight_payload,2*232,copy+flight_payload-32);lockstep_sha256(copy,b.size-32,copy+b.size-32);
        assert(!join_package_parse(copy,b.size,&r,&why) && !strcmp(why,"portable_digest"));join_tables_free(&r);
        memcpy(copy,b.data,b.size);copy[spline_payload+16+JOIN_FLIGHT_BEZIER_AT]=1;
        lockstep_sha256(copy+spline_payload,2*176,copy+spline_payload-32);lockstep_sha256(copy,b.size-32,copy+b.size-32);
        assert(!join_package_parse(copy,b.size,&r,&why) && !strcmp(why,"flight_shape"));join_tables_free(&r);
        memcpy(copy,b.data,b.size);memcpy(copy+spline_payload,b.data+spline_payload+176,176);memcpy(copy+spline_payload+176,b.data+spline_payload,176);
        lockstep_sha256(copy+spline_payload,2*176,copy+spline_payload-32);lockstep_sha256(copy,b.size-32,copy+b.size-32);
        assert(!join_package_parse(copy,b.size,&r,&why) && !strcmp(why,"flight_shape"));join_tables_free(&r);
    }
    {   /* Version 6: a changed AGENT value changes the portable digest; AGENT out of FLIGHT's order, its reserved word set or a
         * formation past 1 refuses (agent_shape). Hashes resealed. */
        size_t agent_payload=b.size-32-v7_tail-2*104;
        memcpy(copy,b.data,b.size);copy[agent_payload+8+7*4]^=1;   /* the ship's speed limit +0x364 */
        lockstep_sha256(copy+agent_payload,2*104,copy+agent_payload-32);lockstep_sha256(copy,b.size-32,copy+b.size-32);
        assert(!join_package_parse(copy,b.size,&r,&why) && !strcmp(why,"portable_digest"));join_tables_free(&r);
        memcpy(copy,b.data,b.size);memcpy(copy+agent_payload,b.data+agent_payload+104,104);memcpy(copy+agent_payload+104,b.data+agent_payload,104);
        lockstep_sha256(copy+agent_payload,2*104,copy+agent_payload-32);lockstep_sha256(copy,b.size-32,copy+b.size-32);
        assert(!join_package_parse(copy,b.size,&r,&why) && !strcmp(why,"agent_shape"));join_tables_free(&r);
        memcpy(copy,b.data,b.size);copy[agent_payload+100]=1;
        lockstep_sha256(copy+agent_payload,2*104,copy+agent_payload-32);lockstep_sha256(copy,b.size-32,copy+b.size-32);
        assert(!join_package_parse(copy,b.size,&r,&why) && !strcmp(why,"agent_shape"));join_tables_free(&r);
        memcpy(copy,b.data,b.size);copy[agent_payload+104+offsetof(join_agent_rec,formation)]=2;
        lockstep_sha256(copy+agent_payload,2*104,copy+agent_payload-32);lockstep_sha256(copy,b.size-32,copy+b.size-32);
        assert(!join_package_parse(copy,b.size,&r,&why) && !strcmp(why,"agent_shape"));join_tables_free(&r);
    }
    {   /* Version 7: a changed ROUTE point (its removal time) changes the portable digest; heads out of AGENT's order, the padding
         * byte +0x7f set, the point's index past its length or a reference with its unmapped bit refuses (route_shape). */
        size_t route_payload=b.size-32-v8_tail-3*232;
        memcpy(copy,b.data,b.size);copy[route_payload+232+64+0x58]^=1;
        lockstep_sha256(copy+route_payload,3*232,copy+route_payload-32);lockstep_sha256(copy,b.size-32,copy+b.size-32);
        assert(!join_package_parse(copy,b.size,&r,&why) && !strcmp(why,"portable_digest"));join_tables_free(&r);
        memcpy(copy,b.data,b.size);memcpy(copy+route_payload,b.data+route_payload+2*232,232);memcpy(copy+route_payload+232,b.data+route_payload,232);
        memcpy(copy+route_payload+2*232,b.data+route_payload+232,232);
        lockstep_sha256(copy+route_payload,3*232,copy+route_payload-32);lockstep_sha256(copy,b.size-32,copy+b.size-32);
        assert(!join_package_parse(copy,b.size,&r,&why) && !strcmp(why,"route_shape"));join_tables_free(&r);
        memcpy(copy,b.data,b.size);copy[route_payload+232+64+0x48+0xf]=1;
        lockstep_sha256(copy+route_payload,3*232,copy+route_payload-32);lockstep_sha256(copy,b.size-32,copy+b.size-32);
        assert(!join_package_parse(copy,b.size,&r,&why) && !strcmp(why,"route_shape"));join_tables_free(&r);
        memcpy(copy,b.data,b.size);copy[route_payload+232+offsetof(join_route_rec,index)]=1;
        lockstep_sha256(copy+route_payload,3*232,copy+route_payload-32);lockstep_sha256(copy,b.size-32,copy+b.size-32);
        assert(!join_package_parse(copy,b.size,&r,&why) && !strcmp(why,"route_shape"));join_tables_free(&r);
        memcpy(copy,b.data,b.size);copy[route_payload+232+offsetof(join_route_rec,flags)]|=1u;
        lockstep_sha256(copy+route_payload,3*232,copy+route_payload-32);lockstep_sha256(copy,b.size-32,copy+b.size-32);
        assert(!join_package_parse(copy,b.size,&r,&why) && !strcmp(why,"route_shape"));join_tables_free(&r);
    }
    {   /* Version 8: a changed PARAM byte (the own copy's turn rate) or DOCK lane changes the portable digest; PARAM out of AGENT's
         * order, a block's pad set or an ID beside its unmapped bit, DOCK of another controller or an undock record with a dock's
         * vectors refuses (param_shape, dock_shape). Hashes resealed. */
        size_t dock_payload=b.size-32-v9_tail-200,param_payload=dock_payload-48-2*1168;
        memcpy(copy,b.data,b.size);copy[param_payload+16+2*JOIN_PARAM_SLOT+JOIN_PARAM_TURN]^=1;
        lockstep_sha256(copy+param_payload,2*1168,copy+param_payload-32);lockstep_sha256(copy,b.size-32,copy+b.size-32);
        assert(!join_package_parse(copy,b.size,&r,&why) && !strcmp(why,"portable_digest"));join_tables_free(&r);
        memcpy(copy,b.data,b.size);memcpy(copy+param_payload,b.data+param_payload+1168,1168);memcpy(copy+param_payload+1168,b.data+param_payload,1168);
        lockstep_sha256(copy+param_payload,2*1168,copy+param_payload-32);lockstep_sha256(copy,b.size-32,copy+b.size-32);
        assert(!join_package_parse(copy,b.size,&r,&why) && !strcmp(why,"param_shape"));join_tables_free(&r);
        memcpy(copy,b.data,b.size);copy[param_payload+16+JOIN_PARAMB_PAD]=1;
        lockstep_sha256(copy+param_payload,2*1168,copy+param_payload-32);lockstep_sha256(copy,b.size-32,copy+b.size-32);
        assert(!join_package_parse(copy,b.size,&r,&why) && !strcmp(why,"param_shape"));join_tables_free(&r);
        memcpy(copy,b.data,b.size);copy[param_payload+16+JOIN_PARAMB_ID]=5;   /* the read slot's ID beside its unmapped bit */
        lockstep_sha256(copy+param_payload,2*1168,copy+param_payload-32);lockstep_sha256(copy,b.size-32,copy+b.size-32);
        assert(!join_package_parse(copy,b.size,&r,&why) && !strcmp(why,"param_shape"));join_tables_free(&r);
        memcpy(copy,b.data,b.size);copy[dock_payload+32+4*5]^=1;
        lockstep_sha256(copy+dock_payload,200,copy+dock_payload-32);lockstep_sha256(copy,b.size-32,copy+b.size-32);
        assert(!join_package_parse(copy,b.size,&r,&why) && !strcmp(why,"portable_digest"));join_tables_free(&r);
        memcpy(copy,b.data,b.size);{uint32_t other=0x2c51158u;memcpy(copy+dock_payload+8,&other,4);}
        lockstep_sha256(copy+dock_payload,200,copy+dock_payload-32);lockstep_sha256(copy,b.size-32,copy+b.size-32);
        assert(!join_package_parse(copy,b.size,&r,&why) && !strcmp(why,"dock_shape"));join_tables_free(&r);
        memcpy(copy,b.data,b.size);{uint32_t undock=UNDOCK_CONTROLLER_VTABLE;memcpy(copy+dock_payload+8,&undock,4);}
        lockstep_sha256(copy+dock_payload,200,copy+dock_payload-32);lockstep_sha256(copy,b.size-32,copy+b.size-32);
        assert(!join_package_parse(copy,b.size,&r,&why) && !strcmp(why,"dock_shape"));join_tables_free(&r);
        memcpy(copy,b.data,b.size);copy[dock_payload+28]=2;
        lockstep_sha256(copy+dock_payload,200,copy+dock_payload-32);lockstep_sha256(copy,b.size-32,copy+b.size-32);
        assert(!join_package_parse(copy,b.size,&r,&why) && !strcmp(why,"dock_shape"));join_tables_free(&r);
    }
    {   /* Version 9: a changed STEER byte (a model byte) or STEERPT point changes the portable digest; STEER's unused flag bits or a
         * point of another list refuses (steer_shape). Hashes resealed. */
        size_t steerpt_payload=b.size-32-v10_tail-232,steer_payload=steerpt_payload-48-832;
        memcpy(copy,b.data,b.size);copy[steer_payload+48+100]^=1;
        lockstep_sha256(copy+steer_payload,832,copy+steer_payload-32);lockstep_sha256(copy,b.size-32,copy+b.size-32);
        assert(!join_package_parse(copy,b.size,&r,&why) && !strcmp(why,"portable_digest"));join_tables_free(&r);
        memcpy(copy,b.data,b.size);copy[steerpt_payload+64+0x30]^=1;
        lockstep_sha256(copy+steerpt_payload,232,copy+steerpt_payload-32);lockstep_sha256(copy,b.size-32,copy+b.size-32);
        assert(!join_package_parse(copy,b.size,&r,&why) && !strcmp(why,"portable_digest"));join_tables_free(&r);
        memcpy(copy,b.data,b.size);copy[steer_payload+8]|=64;
        lockstep_sha256(copy+steer_payload,832,copy+steer_payload-32);lockstep_sha256(copy,b.size-32,copy+b.size-32);
        assert(!join_package_parse(copy,b.size,&r,&why) && !strcmp(why,"steer_shape"));join_tables_free(&r);
        memcpy(copy,b.data,b.size);copy[steerpt_payload+12]=1;   /* the point's list */
        lockstep_sha256(copy+steerpt_payload,232,copy+steerpt_payload-32);lockstep_sha256(copy,b.size-32,copy+b.size-32);
        assert(!join_package_parse(copy,b.size,&r,&why) && !strcmp(why,"steer_shape"));join_tables_free(&r);
    }
    {join_tables v1;memset(&v1,0,sizeof(v1));v1.version=1;assert(join_version_refused(&v1,JOIN_STRICT) && !join_version_refused(&v1,JOIN_REPORT));
     v1.version=2;assert(join_version_refused(&v1,JOIN_STRICT) && !join_version_refused(&v1,JOIN_REPORT));
     v1.version=3;assert(join_version_refused(&v1,JOIN_STRICT) && !join_version_refused(&v1,JOIN_REPORT) && !strcmp(join_version_name(3),"package_v3"));
     v1.version=4;assert(join_version_refused(&v1,JOIN_STRICT) && !strcmp(join_version_name(4),"package_v4"));
     v1.version=5;assert(join_version_refused(&v1,JOIN_STRICT) && !strcmp(join_version_name(5),"package_v5"));
     v1.version=6;assert(join_version_refused(&v1,JOIN_STRICT) && !strcmp(join_version_name(6),"package_v6"));
     v1.version=7;assert(join_version_refused(&v1,JOIN_STRICT) && !strcmp(join_version_name(7),"package_v7"));
     v1.version=8;assert(join_version_refused(&v1,JOIN_STRICT) && !strcmp(join_version_name(8),"package_v8"));
     v1.version=9;assert(join_version_refused(&v1,JOIN_STRICT) && !strcmp(join_version_name(9),"package_v9"));
     v1.version=10;assert(join_version_refused(&v1,JOIN_STRICT) && !strcmp(join_version_name(10),"package_v10"));
     v1.version=11;assert(join_version_refused(&v1,JOIN_STRICT) && !strcmp(join_version_name(11),"package_v11"));
     v1.version=12;assert(join_version_refused(&v1,JOIN_STRICT) && !strcmp(join_version_name(12),"package_v12"));
     v1.version=13;assert(!join_version_refused(&v1,JOIN_STRICT));}
    {   /* Version 10: SOLVER's bytes and BULLET stay out of the portable digest, SOLVER's model kind is in it; a pointer outside the object
         * refuses (solver_shape), a macro with a byte behind its end (bullet_shape). Hashes resealed. */
        size_t bullet_payload=b.size-32-v11_tail-2*sizeof(join_bullet_rec),solver_payload=bullet_payload-48-sizeof(join_solver_rec);
        memcpy(copy,b.data,b.size);copy[solver_payload+offsetof(join_solver_rec,data)+0x20]^=1;
        lockstep_sha256(copy+solver_payload,sizeof(join_solver_rec),copy+solver_payload-32);lockstep_sha256(copy,b.size-32,copy+b.size-32);
        assert(join_package_parse(copy,b.size,&r,&why));join_tables_free(&r);
        memcpy(copy,b.data,b.size);copy[bullet_payload+offsetof(join_bullet_rec,bullet)+0x20]^=1;
        lockstep_sha256(copy+bullet_payload,2*sizeof(join_bullet_rec),copy+bullet_payload-32);lockstep_sha256(copy,b.size-32,copy+b.size-32);
        assert(join_package_parse(copy,b.size,&r,&why));join_tables_free(&r);
        memcpy(copy,b.data,b.size);copy[solver_payload+offsetof(join_solver_rec,kind)]^=1;
        lockstep_sha256(copy+solver_payload,sizeof(join_solver_rec),copy+solver_payload-32);lockstep_sha256(copy,b.size-32,copy+b.size-32);
        assert(!join_package_parse(copy,b.size,&r,&why) && !strcmp(why,"portable_digest"));join_tables_free(&r);
        memcpy(copy,b.data,b.size);copy[solver_payload+offsetof(join_solver_rec,data)+0x16a0+7]=1;
        lockstep_sha256(copy+solver_payload,sizeof(join_solver_rec),copy+solver_payload-32);lockstep_sha256(copy,b.size-32,copy+b.size-32);
        assert(!join_package_parse(copy,b.size,&r,&why) && !strcmp(why,"solver_shape"));join_tables_free(&r);
        memcpy(copy,b.data,b.size);copy[bullet_payload+offsetof(join_bullet_rec,macro)+60]=1;
        lockstep_sha256(copy+bullet_payload,2*sizeof(join_bullet_rec),copy+bullet_payload-32);lockstep_sha256(copy,b.size-32,copy+b.size-32);
        assert(!join_package_parse(copy,b.size,&r,&why) && !strcmp(why,"bullet_shape"));join_tables_free(&r);
    }
    free(copy);free(b.data);
    puts("hot join package: golden bytes shared with native_gate_join.py, hash, shape and portable digest refusals PASS");
}
/* The session's MD heap: every role once, a cancelled entry; 9 entries. */
static void tj_session_heap(tj_world *w) {
    tj_heap_entry(w,0,8.0,40,1,tj_cue(w,TJ_A));tj_heap_entry(w,1,9.5,41,2,tj_cue(w,TJ_I1));tj_heap_entry(w,2,9.5,39,2,tj_cue(w,TJ_I2));
    tj_heap_entry(w,3,8.5,44,0,0);tj_heap_entry(w,4,12.0,43,1,tj_cue(w,TJ_C2));tj_heap_entry(w,5,12.5,45,1,tj_cue(w,TJ_D2));
    tj_heap_entry(w,6,13.0,46,1,tj_cue(w,TJ_J));tj_heap_entry(w,7,14.0,47,1,tj_cue(w,TJ_F));tj_heap_entry(w,8,12.0,42,1,tj_cue(w,TJ_C1));
}
static void test_join_capture_restore(void) {
    uint64_t (*key)(uint64_t,unsigned *)=join_key;uint64_t (*script)(uintptr_t)=join_script;
    join_key=tj_key_of;join_script=tj_script_of;
    static tj_world session,joiner;tj_world_build(&session,0);tj_world_build(&joiner,1);
    /* The session: wakes 1@5.5, 3@5.5 (a tie, 1 first), 0@7.25, 4@6; vector 0 = [4, hole, 0], vector 1 = [3]; index 1. */
    tj_install(&session,9);
    tj_wake(&session,1,5.5,2);tj_wake(&session,3,5.5,2);tj_wake(&session,0,7.25,1);tj_wake(&session,4,6.0,2);
    session.vec[0][0]=(uintptr_t)&session.ctx[4];session.vec[0][2]=(uintptr_t)&session.ctx[0];session.vec[1][0]=(uintptr_t)&session.ctx[3];
    session.vec[0][3]=0;uint32_t mask=4;memcpy(session.ctx[4].b+JOIN_CTX_MASK,&mask,4);mask=1;memcpy(session.ctx[0].b+JOIN_CTX_MASK,&mask,4);
    mask=2;memcpy(session.ctx[3].b+JOIN_CTX_MASK,&mask,4);uint32_t index=1;memcpy(tj_dir+JOIN_DIR_VEC_INDEX,&index,4);
    tj_set((uintptr_t)tj_dir,JOIN_DIR_VEC+8,(uintptr_t)(session.vec[0]+3));tj_set((uintptr_t)tj_dir,JOIN_DIR_VEC+0x18+8,(uintptr_t)(session.vec[1]+1));
    tj_session_heap(&session);tj_seq=48;
    join_engine e=tj_engine();join_tables t;memset(&t,0,sizeof(t));
    assert(join_capture_tables(&e,&t));
    /* Every context of the list (not the 2 of the director's +0x148). */
    assert(t.nctx==6 && t.nwake==4 && t.nvec==3 && t.nmd==8 && t.gate.md_void==1 && t.gate.md_seq==48 && t.gate.vec_index==1 && t.gate.vec0==2 && t.gate.vec1==1);
    assert(t.wake[0].owner==0x101001 && t.wake[1].owner==0x101003 && join_double(t.wake[3].due)==7.25 && t.wake[2].owner==0x101004);
    assert(t.vec[0].owner==0x101004 && t.vec[0].position==0 && t.vec[1].owner==0x101000 && t.vec[1].position==1 && t.vec[1].mask==1 && t.vec[2].vector==1);
    /* Cue keys (capture order A, I1, I2, C2, D2, J, F, C1): statics by their script and name path (the nameless F by its
     * slot under A); an instance by its static cue's path; the roots I1/I2 by their index in B's created list; the nested
     * C1/C2 by their root and slot; D2 in the same root, another slot; J by its creator E2's key and its index in E2's list. */
    const join_md_rec *m=t.md;
    const uint64_t ka=tj_fnv("TJ_Script.a"),kb=tj_fnv("TJ_Script.b"),kc=tj_fnv("TJ_Script.b.c"),kd=tj_fnv("TJ_Script.b.#1"),
                   ke=tj_fnv("TJ_Script.b.e"),kf=tj_fnv("TJ_Script.a.#0");
    assert(m[0].name==ka && !m[0].root && !m[0].ordinal && m[0].state==1 && m[1].state==2);
    assert(m[1].name==kb && m[1].root==(lockstep_mix(kb^lockstep_mix(1))|1u) && !m[1].ordinal);
    assert(m[2].name==kb && m[2].root==(lockstep_mix(kb^lockstep_mix(2))|1u) && !m[2].ordinal);
    assert(m[3].name==kc && m[3].root==m[2].root && m[3].ordinal && m[7].name==kc && m[7].root==m[1].root && m[7].ordinal==m[3].ordinal);
    assert(m[4].name==kd && m[4].root==m[2].root && m[4].ordinal && m[4].ordinal!=m[3].ordinal);
    assert(m[5].name==ke && m[5].root && m[5].root!=m[1].root && m[5].root!=m[2].root && !m[5].ordinal);
    assert(m[6].name==kf && !m[6].root && !m[6].ordinal);
    /* The two contexts of entity 5 (runtime 0x9001 -> created) and context 2 (no script) keep distinct keys. */
    assert(t.ctx[5].klass==WORLD_KEY_CREATED && t.gate.ambiguous==0);
    /* The joiner loaded the same world differently: every cue and context at another address, the context list in another
     * order, other wakes and order, other vectors, other heap times, seqs and order. */
    tj_install(&joiner,8);
    tj_wake(&joiner,3,3.0,2);tj_wake(&joiner,1,4.0,2);tj_wake(&joiner,2,9.0,1);
    joiner.vec[0][0]=(uintptr_t)&joiner.ctx[0];joiner.vec[0][1]=(uintptr_t)&joiner.ctx[4];joiner.vec[1][0]=(uintptr_t)&joiner.ctx[3];
    tj_set((uintptr_t)tj_dir,JOIN_DIR_VEC+8,(uintptr_t)(joiner.vec[0]+2));tj_set((uintptr_t)tj_dir,JOIN_DIR_VEC+0x18+8,(uintptr_t)(joiner.vec[1]+1));
    int64_t pos=0;memcpy(joiner.ctx[0].b+JOIN_CTX_VEC,&pos,8);pos=1;memcpy(joiner.ctx[4].b+JOIN_CTX_VEC,&pos,8);pos=0;memcpy(joiner.ctx[3].b+JOIN_CTX_VEC+8,&pos,8);
    static const unsigned jroles[8]={TJ_I2,TJ_C2,TJ_I1,TJ_A,TJ_J,TJ_D2,TJ_F,TJ_C1};
    for(int i=0;i<8;i++) tj_heap_entry(&joiner,i,7.0+0.05*i,(uint32_t)(i+1),jroles[i]==TJ_I1 || jroles[i]==TJ_I2?2:1,tj_cue(&joiner,jroles[i]));
    tj_seq=9;
    memset(&join_rx,0,sizeof(join_rx));join_rx.t=t;join_rx.mode=JOIN_STRICT;
    assert(join_admit_ai(&e));
    assert(!join_rx.ctx_missing && !join_rx.ctx_extra && join_rx.wake_erased==3 && join_rx.wake_applied==4 && !join_rx.wake_missing && join_rx.vec_applied==3);
    int order[8];double dues[8];unsigned count;tj_tree_order(order,&count,&joiner,dues);
    assert(count==4 && order[0]==1 && order[1]==3 && order[2]==4 && order[3]==0 && dues[0]==5.5 && dues[1]==5.5 && dues[2]==6.0 && dues[3]==7.25);
    double due;memcpy(&due,joiner.ctx[2].b+JOIN_CTX_DUE,8);assert(due==-1.0 && tj_get((uintptr_t)&joiner.ctx[2],JOIN_CTX_NODE)==(uintptr_t)(tj_dir+JOIN_DIR_TREE));
    memcpy(&due,joiner.ctx[0].b+JOIN_CTX_DUE,8);assert(due==7.25);
    assert(joiner.vec[0][0]==(uintptr_t)&joiner.ctx[4] && joiner.vec[0][1]==(uintptr_t)&joiner.ctx[0] && joiner.vec[1][0]==(uintptr_t)&joiner.ctx[3]);
    memcpy(&pos,joiner.ctx[0].b+JOIN_CTX_VEC,8);assert(pos==1);memcpy(&mask,joiner.ctx[4].b+JOIN_CTX_MASK,4);assert(mask==4);
    memcpy(&index,tj_dir+JOIN_DIR_VEC_INDEX,4);assert(index==1);
    assert(join_admit_md(&e) && join_rx.md_applied==8 && !join_rx.md_missing && !join_rx.md_extra && !join_rx.md_ambiguous && tj_seq==48);
    /* The heap in the session's (time, seq) order with the joiner's own cues. */
    static const unsigned want_roles[8]={TJ_A,TJ_I2,TJ_I1,TJ_C1,TJ_C2,TJ_D2,TJ_J,TJ_F};
    static const double want_times[8]={8.0,9.5,9.5,12.0,12.0,12.5,13.0,14.0};static const uint32_t want_seqs[8]={40,39,41,42,43,45,46,47};
    for(int i=0;i<8;i++) {
        double time;uint32_t seq;uintptr_t c;memcpy(&time,joiner.heap[i],8);memcpy(&seq,joiner.heap[i]+8,4);memcpy(&c,joiner.heap[i]+16,8);
        assert(time==want_times[i] && seq==want_seqs[i] && c==tj_cue(&joiner,want_roles[i]));
    }
    /* Strict refusals: a context the session never had; a heap entry it never had. */
    memset(&join_rx,0,sizeof(join_rx));join_rx.t=t;join_rx.mode=JOIN_STRICT;
    uint64_t other=0x1777;memcpy(joiner.ent[5].b+8,&other,8);
    assert(!join_admit_ai(&e) && join_rx.ctx_missing==1 && join_rx.ctx_extra==1 && !strcmp(join_rx.detail,"contexts_differ"));
    memset(&join_rx,0,sizeof(join_rx));join_rx.t=t;join_rx.mode=JOIN_REPORT;
    assert(join_admit_ai(&e) && join_rx.ctx_missing==1 && join_rx.wake_applied==4);
    uint32_t state=1;memcpy((void *)(tj_cue(&joiner,TJ_C2)+JOIN_CUE_STATE),&state,4);
    memset(&join_rx,0,sizeof(join_rx));join_rx.t=t;join_rx.mode=JOIN_STRICT;
    assert(!join_admit_md(&e) && join_rx.md_missing==1 && join_rx.md_extra==1 && !strcmp(join_rx.detail,"md_heap_differs"));
    /* A root created after the save on the joiner only (B's list [I1, X, I2]): I2's index moves, so do its tree's keys. */
    state=2;memcpy((void *)(tj_cue(&joiner,TJ_C2)+JOIN_CUE_STATE),&state,4);
    uintptr_t grown[3]={tj_cue(&joiner,TJ_I1),tj_cue(&joiner,TJ_C),tj_cue(&joiner,TJ_I2)};tj_vector(tj_cue(&joiner,TJ_B)+JOIN_CUE_INSTANCES,grown,3);
    memset(&join_rx,0,sizeof(join_rx));join_rx.t=t;join_rx.mode=JOIN_REPORT;
    assert(join_admit_md(&e) && join_rx.md_applied==4 && join_rx.md_missing==4 && join_rx.md_extra==4);  /* I2, C2, D2, J */
    join_tables_free(&t);memset(&join_rx,0,sizeof(join_rx));
    for(int i=0;i<64;i++) {uintptr_t f=tj_get((uintptr_t)(tj_dir+JOIN_DIR_TREE),0x10);if(!f) break;tj_set((uintptr_t)(tj_dir+JOIN_DIR_TREE),0x10,tj_get(f,0x10));free((void *)f);}
    join_key=key;join_script=script;
    puts("hot join side table: every context of the global list, AI tree, wake vectors and MD heap (cues keyed by name paths, "
         "creator lists and sub-cue slots) captured, restored into a world loaded at other addresses in the session's order, "
         "strict refusals PASS");
}
/* A refusal names its record: the part, the site, the counts so far, the record's index and fields, the check. */
static int tj_has(const char *text,const char *format,...) {
    char want[256];va_list list;va_start(list,format);vsnprintf(want,sizeof(want),format,list);va_end(list);
    if(strstr(text,want)) return 1;
    printf("missing '%s' in '%s'\n",want,text);return 0;
}
static void test_join_refusals(void) {
    uint64_t (*key)(uint64_t,unsigned *)=join_key;uint64_t (*script)(uintptr_t)=join_script;
    join_key=tj_key_of;join_script=tj_script_of;
    static tj_world w;join_engine e=tj_engine();join_tables t;
#define TJ_REFUSED() do{tj_world_build(&w,0);tj_install(&w,9);tj_session_heap(&w);memset(&t,0,sizeof(t));}while(0)
    /* A root missing from its creator's list (md#2 = I2). */
    TJ_REFUSED();w.created[0][1]=0;
    assert(!join_capture_tables(&e,&t) && tj_has(join_tables_why,"tables:md:22:6/0/0/2,md#2,kind=2,seq=39,time=9.5,cue=%llx,",JOIN_P(tj_cue(&w,TJ_I2))) &&
           tj_has(join_tables_why,"check=creator_index,root=%llx,creator=%llx,created=2,why=-1",JOIN_P(tj_cue(&w,TJ_I2)),JOIN_P(tj_cue(&w,TJ_B))));
    join_tables_free(&t);
    /* A nested cue its parent does not hold in a sub-cue slot (md#4 = C2). */
    TJ_REFUSED();w.kids[3][0]=0;
    assert(!join_capture_tables(&e,&t) && tj_has(join_tables_why,"tables:md:22:6/0/0/3,md#4,kind=1,seq=43,time=12,cue=%llx,",JOIN_P(tj_cue(&w,TJ_C2))) &&
           tj_has(join_tables_why,"check=slot,cue=%llx,at=%llx,parent=%llx,root=%llx,slots=3,why=-1",JOIN_P(tj_cue(&w,TJ_C2)),JOIN_P(tj_cue(&w,TJ_C2)),
                  JOIN_P(tj_cue(&w,TJ_I2)),JOIN_P(tj_cue(&w,TJ_I2))));
    join_tables_free(&t);
    /* No cue at all; a nested instance without a root; J's creator without a static cue. */
    TJ_REFUSED();tj_set(tj_cue(&w,TJ_D2),0,0x1234);
    assert(!join_capture_tables(&e,&t) && tj_has(join_tables_why,"md#5,kind=1,seq=45,time=12.5,cue=%llx,check=cue_vtable,cue=%llx,vtable=1234",
                                                 JOIN_P(tj_cue(&w,TJ_D2)),JOIN_P(tj_cue(&w,TJ_D2))));
    join_tables_free(&t);
    TJ_REFUSED();tj_set(tj_cue(&w,TJ_C2),JOIN_CUE_ROOT,0);
    assert(!join_capture_tables(&e,&t) && tj_has(join_tables_why,"check=root,cue=%llx,def=%llx,root=0",JOIN_P(tj_cue(&w,TJ_C2)),JOIN_P(tj_cue(&w,TJ_C))));
    join_tables_free(&t);
    TJ_REFUSED();tj_set(tj_cue(&w,TJ_E2),JOIN_CUE_DEF,0);
    assert(!join_capture_tables(&e,&t) && tj_has(join_tables_why,"md#6,") &&
           tj_has(join_tables_why,"check=creator_def,root=%llx,creator=%llx,def=0",JOIN_P(tj_cue(&w,TJ_J)),JOIN_P(tj_cue(&w,TJ_E2))));
    join_tables_free(&t);
    /* The context list: a broken back link, a tail that is not the last context, an entry that is no AIScriptContext. */
    TJ_REFUSED();tj_set((uintptr_t)&w.ctx[3],JOIN_CTX_PREV,(uintptr_t)&w.ctx[1]);
    assert(!join_capture_tables(&e,&t) && tj_has(join_tables_why,"tables:ctx:1:0/0/0/0,check=list_prev,index=3,ctx=%llx,prev=%llx,expected=%llx",
                                                 JOIN_P(&w.ctx[3]),JOIN_P(&w.ctx[1]),JOIN_P(&w.ctx[2])));
    join_tables_free(&t);
    TJ_REFUSED();tj_ctx_tail=(uintptr_t)&w.ctx[4];
    assert(!join_capture_tables(&e,&t) && tj_has(join_tables_why,"tables:ctx:1:0/0/0/0,check=list_tail,count=6,last=%llx,tail=%llx",
                                                 JOIN_P(&w.ctx[5]),JOIN_P(&w.ctx[4])));
    join_tables_free(&t);
    TJ_REFUSED();tj_set((uintptr_t)&w.ctx[4],0,0x55);
    assert(!join_capture_tables(&e,&t) && tj_has(join_tables_why,"tables:ctx:2:4/0/0/0,ctx#4,check=ctx_vtable,ctx=%llx,vtable=55,id=1",JOIN_P(&w.ctx[4])));
    join_tables_free(&t);
    /* A tree node whose context is gone; a tree that counts more nodes than it links. */
    TJ_REFUSED();{void *node=NULL;double due=5.5;uintptr_t gone=(uintptr_t)&w.ent[0];tj_insert(tj_dir+JOIN_DIR_TREE,&node,&due,&gone);
    assert(!join_capture_tables(&e,&t) && tj_has(join_tables_why,"tables:wake:11:6/0/0/0,wake#0,check=ctx_vtable,ctx=%llx,vtable=0,id=1000,node=%llx",
                                                 JOIN_P(&w.ent[0]),JOIN_P(node)));}
    join_tables_free(&t);
    TJ_REFUSED();tj_wake(&w,1,5.5,2);tj_set((uintptr_t)(tj_dir+JOIN_DIR_TREE),JOIN_TREE_SIZE,2);
    assert(!join_capture_tables(&e,&t) && tj_has(join_tables_why,"tables:wake:14:6/1/0/0,check=tree_count,seen=1,size=2"));
    join_tables_free(&t);
    /* The joiner's admission names its record the same way. */
    TJ_REFUSED();assert(join_capture_tables(&e,&t));
    static tj_world j;tj_world_build(&j,1);tj_install(&j,9);tj_session_heap(&j);j.created[1][1]=0;
    memset(&join_rx,0,sizeof(join_rx));join_rx.t=t;join_rx.mode=JOIN_REPORT;
    assert(!join_admit_md(&e) && join_rx.why==JOIN_WHY_CAPTURE && tj_has(join_rx.detail,"cue_key:md#6,kind=1,seq=46,time=13,cue=%llx,",JOIN_P(tj_cue(&j,TJ_J))) &&
           tj_has(join_rx.detail,"check=creator_index,root=%llx,creator=%llx,created=2,why=-1",JOIN_P(tj_cue(&j,TJ_J)),JOIN_P(tj_cue(&j,TJ_E2))));
    memset(&join_rx,0,sizeof(join_rx));join_rx.t=t;join_rx.mode=JOIN_REPORT;tj_ctx_head=0;
    assert(!join_admit_ai(&e) && tj_has(join_rx.detail,"contexts:check=list_tail,count=0,last=0,tail=%llx",JOIN_P(tj_ctx_tail)));
    join_tables_free(&t);memset(&join_rx,0,sizeof(join_rx));
#undef TJ_REFUSED
    for(int i=0;i<64;i++) {uintptr_t f=tj_get((uintptr_t)(tj_dir+JOIN_DIR_TREE),0x10);if(!f) break;tj_set((uintptr_t)(tj_dir+JOIN_DIR_TREE),0x10,tj_get(f,0x10));free((void *)f);}
    join_key=key;join_script=script;
    puts("hot join refusals name their record: part, site, counts, the record's fields and the check (creator list, sub-cue slot, "
         "cue vtable, root, creator, context list links and entries, tree node and count; the joiner's cue_key and contexts) PASS");
}
/* The static cue path key (J1 j1-7 refused a static cue four levels below a root, every level unnamed: the name map's
 * walk gives up before the late names of the large story scripts). Roles: Story_Criminal Start > Setup >
 * Setup_Character_Maestro > PATCH_AddStoryMentors_Maestro (pending its delayed actions, kind 2) and Story_Thefan
 * Start > Setup (the same names in another script). The joiner's cues lie at other addresses, its strings in the
 * pointer form. */
enum {TJS_START,TJS_SETUP,TJS_CHAR,TJS_LEAF,TJS_START2,TJS_SETUP2,TJS_CUES};
typedef struct {tj_cue_t cue[TJS_CUES];unsigned char script[2][0x40];unsigned char heap[2][24];uintptr_t kids[TJS_CUES][1];int joiner;} tj_story;
static uintptr_t tjs_cue(tj_story *s,unsigned role) {return (uintptr_t)&s->cue[s->joiner?TJS_CUES-1-role:role];}
static void tj_story_build(tj_story *s,int joiner) {
    memset(s,0,sizeof(*s));s->joiner=joiner;
    static const char *const names[TJS_CUES]={"Start","Setup","Setup_Character_Maestro","PATCH_AddStoryMentors_Maestro","Start","Setup"};
    tj_string((uintptr_t)s->script[0]+JOIN_SCRIPT_NAME,"Story_Criminal",joiner);tj_string((uintptr_t)s->script[1]+JOIN_SCRIPT_NAME,"Story_Thefan",joiner);
    for(unsigned r=0;r<TJS_CUES;r++) {
        uintptr_t c=tjs_cue(s,r);uint32_t state=2;double act=29.0;
        tj_set(c,0,tj_cue_vt);tj_set(c,JOIN_CUE_DEF,c);memcpy((void *)(c+JOIN_CUE_STATE),&state,4);memcpy((void *)(c+JOIN_CUE_ACTIVATION),&act,8);
        tj_set(c,JOIN_CUE_SCRIPT,(uintptr_t)s->script[r<TJS_START2?0:1]);tj_string(c+JOIN_CUE_NAME,names[r],joiner);
        if(r!=TJS_START && r!=TJS_START2) {   /* the parent and its sub-cue slot */
            uintptr_t parent=tjs_cue(s,r-1);tj_set(c,JOIN_CUE_PARENT,parent);s->kids[r][0]=c;tj_vector(parent+JOIN_CUE_CHILDREN,s->kids[r],1);
        }
    }
    double time[2]={30.0,31.0};uint32_t seq[2]={11541,11542},kind[2]={2,1};unsigned role[2]={TJS_LEAF,TJS_SETUP2};
    for(int i=0;i<2;i++) {
        int at=joiner?1-i:i;double t=joiner?time[i]-20.0:time[i];uint32_t q=joiner?seq[i]-11000:seq[i];uintptr_t c=tjs_cue(s,role[i]);
        memcpy(s->heap[at],&t,8);memcpy(s->heap[at]+8,&q,4);memcpy(s->heap[at]+12,&kind[i],4);memcpy(s->heap[at]+16,&c,8);
    }
    memset(tj_sched,0,sizeof(tj_sched));tj_set((uintptr_t)tj_sched,JOIN_MD_HEAP,(uintptr_t)s->heap);
    tj_set((uintptr_t)tj_sched,JOIN_MD_HEAP+8,(uintptr_t)(s->heap[0]+48));
}
static void test_join_static_keys(void) {
    static tj_story s,j;join_engine e=tj_engine();join_tables t;memset(&t,0,sizeof(t));
    const uint64_t leaf=tj_fnv("Story_Criminal.Start.Setup.Setup_Character_Maestro.PATCH_AddStoryMentors_Maestro");
    tj_story_build(&s,0);tj_seq=11543;join_keys_reset();
    assert(join_capture_md(&e,&t) && t.nmd==2 && t.gate.md_seq==11543);
    assert(t.md[0].name==leaf && !t.md[0].root && !t.md[0].ordinal && t.md[0].kind==2 && t.md[0].seq==11541 && t.md[0].state==2);
    assert(t.md[1].name==tj_fnv("Story_Thefan.Start.Setup") && t.md[1].name!=tj_fnv("Story_Criminal.Start.Setup") && !t.md[1].root);
    /* The joiner: the same cues at other addresses, every string behind a pointer, other heap order, times and seqs. */
    tj_story_build(&j,1);tj_seq=9;join_keys_reset();
    memset(&join_rx,0,sizeof(join_rx));join_rx.t=t;join_rx.mode=JOIN_STRICT;
    assert(join_admit_md(&e) && join_rx.md_applied==2 && !join_rx.md_missing && !join_rx.md_extra && !join_rx.md_ambiguous && tj_seq==11543);
    {double time;uint32_t seq;uintptr_t c;memcpy(&time,j.heap[0],8);memcpy(&seq,j.heap[0]+8,4);memcpy(&c,j.heap[0]+16,8);
     assert(time==30.0 && seq==11541 && c==tjs_cue(&j,TJS_LEAF));}
    memset(&join_rx,0,sizeof(join_rx));
    /* A nameless cue below the root by its slot (the loader makes none; the fallback is defined). */
    tj_story_build(&s,0);tj_string(tjs_cue(&s,TJS_CHAR)+JOIN_CUE_NAME,"",0);join_keys_reset();join_tables_free(&t);
    assert(join_capture_md(&e,&t) && t.md[0].name==tj_fnv("Story_Criminal.Start.Setup.#0.PATCH_AddStoryMentors_Maestro"));
    join_tables_free(&t);
    /* Refusals: the record (j1-7's fields) and the check with the static cue's fields. */
#define TJS_REFUSED(check,...) do{join_keys_reset();assert(!join_capture_md(&e,&t));join_tables_free(&t);\
    assert(tj_has(join_fault,"md#0,kind=2,seq=11541,time=30,cue=%llx,check=" check,JOIN_P(tjs_cue(&s,TJS_LEAF)),__VA_ARGS__));}while(0)
    uintptr_t L=tjs_cue(&s,TJS_LEAF),S0=tjs_cue(&s,TJS_START),S1=tjs_cue(&s,TJS_SETUP),S2=tjs_cue(&s,TJS_CHAR);
    tj_story_build(&s,0);tj_string(S0+JOIN_CUE_NAME,"",0);
    TJS_REFUSED("static_root,static=%llx,root=%llx,depth=4",JOIN_P(L),JOIN_P(S0));
    tj_story_build(&s,0);tj_set(S1,JOIN_CUE_SCRIPT,(uintptr_t)s.script[1]);
    TJS_REFUSED("static_parent,static=%llx,at=%llx,depth=2,vtable=%llx,def=%llx,script=%llx,expected=%llx",JOIN_P(L),JOIN_P(S1),
                JOIN_P(tj_cue_vt),JOIN_P(S1),JOIN_P(s.script[1]),JOIN_P(s.script[0]));
    tj_story_build(&s,0);tj_set(S2,JOIN_CUE_DEF,L);
    TJS_REFUSED("static_parent,static=%llx,at=%llx,depth=1,vtable=%llx,def=%llx",JOIN_P(L),JOIN_P(S2),JOIN_P(tj_cue_vt),JOIN_P(L));
    tj_story_build(&s,0);tj_set(L,JOIN_CUE_SCRIPT,0);
    TJS_REFUSED("static_script,static=%llx,script=0,size=0",JOIN_P(L));
    tj_story_build(&s,0);tj_string((uintptr_t)s.script[0]+JOIN_SCRIPT_NAME,"",0);
    TJS_REFUSED("static_script,static=%llx,script=%llx,size=0",JOIN_P(L),JOIN_P(s.script[0]));
    tj_story_build(&s,0);{uint64_t cap=3;memcpy((void *)(S2+JOIN_CUE_NAME+24),&cap,8);}
    TJS_REFUSED("static_string,static=%llx,at=%llx,size=23",JOIN_P(L),JOIN_P(S2));
    tj_story_build(&s,0);tj_set(S0,JOIN_CUE_PARENT,L);
    TJS_REFUSED("static_depth,static=%llx,at=%llx,depth=64",JOIN_P(L),JOIN_P(L));
    tj_story_build(&s,0);tj_string(S2+JOIN_CUE_NAME,"",0);tj_vector(S1+JOIN_CUE_CHILDREN,s.kids[TJS_CHAR],0);
    TJS_REFUSED("static_slot,static=%llx,at=%llx,parent=%llx,slots=0,why=-1",JOIN_P(L),JOIN_P(S2),JOIN_P(S1));
#undef TJS_REFUSED
    /* The joiner names its record the same way (cue_key:). */
    tj_story_build(&s,0);join_keys_reset();assert(join_capture_md(&e,&t));
    tj_story_build(&j,1);tj_string(tjs_cue(&j,TJS_START)+JOIN_CUE_NAME,"",1);
    memset(&join_rx,0,sizeof(join_rx));join_rx.t=t;join_rx.mode=JOIN_REPORT;
    assert(!join_admit_md(&e) && join_rx.why==JOIN_WHY_CAPTURE && tj_has(join_rx.detail,"cue_key:md#1,kind=2,seq=541,time=10,cue=%llx,"
           "check=static_root,static=%llx,root=%llx,depth=4",JOIN_P(tjs_cue(&j,TJS_LEAF)),JOIN_P(tjs_cue(&j,TJS_LEAF)),JOIN_P(tjs_cue(&j,TJS_START))));
    join_tables_free(&t);memset(&join_rx,0,sizeof(join_rx));memset(tj_sched,0,sizeof(tj_sched));join_keys_reset();
    puts("hot join static cue keys: script and name path (j1-7's four-level story chain pending kind 2), the same at other "
         "addresses and in the pointer form, another script apart, a nameless sub-cue by its slot; refusals static_root, "
         "static_parent, static_script, static_string, static_depth, static_slot and the joiner's cue_key PASS");
}
/* J1 j1-8: md_applied 243, md_missing 11,987: the save writes a cue's time with six significant digits, so the joiner's
 * activation (+0x1f8) is the session's rounded and only the entries whose activation survives that rounding paired. Now
 * the pairing leaves the activation out and the admission gives each paired cue the session's bits back; one further off
 * than the rounding counts far (strict refuses). Unpaired entries are classified by the first key part without a
 * counterpart and sampled with every key part, contexts too. */
static int tj_row_has(const char *needle) {
    for(unsigned i=1;join_unpaired_row(i);i++) if(strstr(join_unpaired_row(i),needle)) return 1;
    printf("no unpaired row with '%s' among %u\n",needle,join_rx.nrows);
    for(unsigned i=1;join_unpaired_row(i);i++) printf("  %s\n",join_unpaired_row(i));
    fflush(stdout);return 0;
}
static double tj_g6(double v) {char text[32];snprintf(text,sizeof(text),"%g",v);return strtod(text,NULL);}
static void test_join_activation_rows(void) {
    uint64_t (*key)(uint64_t,unsigned *)=join_key;uint64_t (*script)(uintptr_t)=join_script;
    join_key=tj_key_of;join_script=tj_script_of;
    static tj_world session,joiner;tj_world_build(&session,0);tj_world_build(&joiner,1);
    /* The session's activations as a running clock leaves them (28.199666...), the joiner's as the save wrote them. */
    double acts[TJ_CUES];unsigned differ=0;
    for(unsigned r=0;r<TJ_CUES;r++) {
        acts[r]=r%3==0?10.0+r:28.0+r+1.0/3.0;memcpy((void *)(tj_cue(&session,r)+JOIN_CUE_ACTIVATION),&acts[r],8);
        double loaded=tj_g6(acts[r]);memcpy((void *)(tj_cue(&joiner,r)+JOIN_CUE_ACTIVATION),&loaded,8);
    }
    tj_install(&session,9);tj_session_heap(&session);tj_seq=48;
    join_engine e=tj_engine();join_tables t;memset(&t,0,sizeof(t));
    assert(join_capture_tables(&e,&t) && t.nmd==8);
    for(uint32_t i=0;i<t.nmd;i++) if(tj_g6(join_double(t.md[i].activation))!=join_double(t.md[i].activation)) ++differ;
    assert(differ>=4);
    tj_install(&joiner,8);
    static const unsigned jroles[8]={TJ_I2,TJ_C2,TJ_I1,TJ_A,TJ_J,TJ_D2,TJ_F,TJ_C1};
    for(int i=0;i<8;i++) tj_heap_entry(&joiner,i,7.0+0.05*i,(uint32_t)(i+1),jroles[i]==TJ_I1 || jroles[i]==TJ_I2?2:1,tj_cue(&joiner,jroles[i]));
    memset(&join_rx,0,sizeof(join_rx));join_rx.t=t;join_rx.mode=JOIN_STRICT;
    assert(join_admit_md(&e) && join_rx.md_applied==8 && !join_rx.md_missing && !join_rx.md_extra && join_rx.md_activation_fixed==differ &&
           !join_rx.md_activation_far && !join_rx.nrows && tj_seq==48);
    for(unsigned k=0;k<8;k++) {
        double back;memcpy(&back,(void *)(tj_cue(&joiner,jroles[k])+JOIN_CUE_ACTIVATION),8);assert(back==acts[jroles[k]]);
    }
    /* Further off than the save's rounding: far (report applies it, strict refuses). */
    double off=acts[TJ_C1]+0.01;memcpy((void *)(tj_cue(&joiner,TJ_C1)+JOIN_CUE_ACTIVATION),&off,8);
    tj_install(&joiner,8);for(int i=0;i<8;i++) tj_heap_entry(&joiner,i,7.0+0.05*i,(uint32_t)(i+1),jroles[i]==TJ_I1 || jroles[i]==TJ_I2?2:1,tj_cue(&joiner,jroles[i]));
    memset(&join_rx,0,sizeof(join_rx));join_rx.t=t;join_rx.mode=JOIN_STRICT;
    assert(!join_admit_md(&e) && join_rx.md_applied==8 && join_rx.md_activation_far==1 && !strcmp(join_rx.detail,"md_activation_far"));
    memcpy((void *)(tj_cue(&joiner,TJ_C1)+JOIN_CUE_ACTIVATION),&off,8);
    tj_install(&joiner,8);for(int i=0;i<8;i++) tj_heap_entry(&joiner,i,7.0+0.05*i,(uint32_t)(i+1),jroles[i]==TJ_I1 || jroles[i]==TJ_I2?2:1,tj_cue(&joiner,jroles[i]));
    memset(&join_rx,0,sizeof(join_rx));join_rx.t=t;join_rx.mode=JOIN_REPORT;
    assert(join_admit_md(&e) && join_rx.md_activation_far==1);
    {double back;memcpy(&back,(void *)(tj_cue(&joiner,TJ_C1)+JOIN_CUE_ACTIVATION),8);assert(back==acts[TJ_C1]);}
    /* Unpaired: C2 in another state on the joiner -> kind_state, both sides sampled with every key part. */
    uint32_t state=1;memcpy((void *)(tj_cue(&joiner,TJ_C2)+JOIN_CUE_STATE),&state,4);
    tj_install(&joiner,8);for(int i=0;i<8;i++) tj_heap_entry(&joiner,i,7.0+0.05*i,(uint32_t)(i+1),jroles[i]==TJ_I1 || jroles[i]==TJ_I2?2:1,tj_cue(&joiner,jroles[i]));
    memset(&join_rx,0,sizeof(join_rx));join_rx.t=t;join_rx.mode=JOIN_REPORT;
    assert(join_admit_md(&e) && join_rx.md_missing==1 && join_rx.md_extra==1 && join_rx.md_diff[3]==1 && !join_rx.md_diff[0] &&
           !join_rx.md_diff[1] && !join_rx.md_diff[2] && join_rx.nrows==2);
    {char want[256];
     snprintf(want,sizeof(want),"md side=session name=%016llx ",(unsigned long long)tj_fnv("TJ_Script.b.c"));assert(tj_row_has(want));
     assert(tj_row_has("kind=1 state=2 ") && tj_row_has("path=TJ_Script.b.c") && tj_row_has("kind=1 state=1 ") &&
            tj_row_has(" creator=TJ_Script.b index=1/2 slots=0"));
     snprintf(want,sizeof(want),"md side=joiner name=%016llx ",(unsigned long long)tj_fnv("TJ_Script.b.c"));assert(tj_row_has(want));}
    state=2;memcpy((void *)(tj_cue(&joiner,TJ_C2)+JOIN_CUE_STATE),&state,4);
    /* A root created after the save on the joiner only (B's list [I1, X, I2]): I2's tree (I2, C2, D2) and J (made by E2 of that
     * tree) differ by their root; the joiner's J by an instance creator (E2's list [-, J]: index 0 of 2). */
    uintptr_t grown[3]={tj_cue(&joiner,TJ_I1),tj_cue(&joiner,TJ_C),tj_cue(&joiner,TJ_I2)};tj_vector(tj_cue(&joiner,TJ_B)+JOIN_CUE_INSTANCES,grown,3);
    tj_install(&joiner,8);for(int i=0;i<8;i++) tj_heap_entry(&joiner,i,7.0+0.05*i,(uint32_t)(i+1),jroles[i]==TJ_I1 || jroles[i]==TJ_I2?2:1,tj_cue(&joiner,jroles[i]));
    memset(&join_rx,0,sizeof(join_rx));join_rx.t=t;join_rx.mode=JOIN_REPORT;
    assert(join_admit_md(&e) && join_rx.md_applied==4 && join_rx.md_missing==4 && join_rx.md_extra==4 && join_rx.md_diff[1]==4 && join_rx.nrows==8);
    assert(tj_row_has(" creator=TJ_Script.b index=2/3 slots=-") && tj_row_has(" creator=inst:TJ_Script.b.e index=0/2 slots=-") &&
           tj_row_has(" creator=TJ_Script.b index=2/3 slots=1"));
    /* Contexts: entity 5 keyed otherwise on the joiner; one row per side. */
    tj_world_build(&joiner,1);tj_install(&joiner,0);
    uint64_t other=0x1777;memcpy(joiner.ent[5].b+8,&other,8);
    memset(&join_rx,0,sizeof(join_rx));join_rx.t=t;join_rx.mode=JOIN_REPORT;
    assert(join_admit_ai(&e) && join_rx.ctx_missing==1 && join_rx.ctx_extra==1 && join_rx.nrows==2);
    {char want[160];snprintf(want,sizeof(want),"ctx side=joiner class=0 owner=%016llx ",(unsigned long long)(0x1777+0x100000));assert(tj_row_has(want));
     snprintf(want,sizeof(want)," entity_id=1777");assert(tj_row_has(want));
     snprintf(want,sizeof(want),"ctx side=session class=1 owner=%016llx ",(unsigned long long)(0x9001+0x100000));assert(tj_row_has(want));}
    assert(!join_unpaired_row(0) && !join_unpaired_row(3));
    join_tables_free(&t);memset(&join_rx,0,sizeof(join_rx));
    for(int i=0;i<64;i++) {uintptr_t f=tj_get((uintptr_t)(tj_dir+JOIN_DIR_TREE),0x10);if(!f) break;tj_set((uintptr_t)(tj_dir+JOIN_DIR_TREE),0x10,tj_get(f,0x10));free((void *)f);}
    join_key=key;join_script=script;
    puts("hot join MD pairing without the activation (j1-8: the save's six-digit times), the session's activation restored, far "
         "ones counted and refused in strict mode; unpaired entries classified (kind_state, root) and sampled with every key part "
         "(static path, creator, created index, sub-cue slots), contexts too PASS");
}
/* A joiner's configuration against the package: the fixture's saved IDs must be saved identities of it, the session past tick
 * 4; boot binds only a tick-0 snapshot can satisfy refuse. */
static void test_join_config_refused(void) {
    join_tables t;memset(&t,0,sizeof(t));char why[64];
    join_id_rec ids[13];for(unsigned i=0;i<12;i++) ids[i]=(join_id_rec){0x6c689+i,0x176a13+i,WORLD_KEY_SAVED,0};
    ids[12]=(join_id_rec){0x7000,0x176a13+11,WORLD_KEY_CREATED,0};
    t.ids=ids;t.nids=12;t.gate.tick=633;
    assert(!join_config_refused(&t,why,sizeof(why)));                                /* no fixture configured */
    snapshot_configured=1;for(unsigned i=0;i<12;i++) snapshot_actors[i].saved=0x176a13+i;
    /* The session's views at T: missing (a package of a build before them), one the joiner cannot bind, then j1-9's. */
    assert(join_config_refused(&t,why,sizeof(why)) && !strcmp(why,"fixture_view_missing"));
    for(unsigned i=0;i<12;i++) t.gate.fixture[i]=SNAPSHOT_VIEW_NAV;
    t.gate.fixture[7]=SNAPSHOT_VIEW_OTHER;assert(join_config_refused(&t,why,sizeof(why)) && !strcmp(why,"fixture_view:8:other"));
    t.gate.fixture[7]=SNAPSHOT_VIEW_LOST;assert(join_config_refused(&t,why,sizeof(why)) && !strcmp(why,"fixture_view:8:lost"));
    t.gate.fixture[0]=SNAPSHOT_VIEW_UNSET;t.gate.fixture[7]=SNAPSHOT_VIEW_DRIFT;
    assert(join_config_refused(&t,why,sizeof(why)) && !strcmp(why,"fixture_view:1:unset"));
    t.gate.fixture[0]=SNAPSHOT_VIEW_BARE;assert(!join_config_refused(&t,why,sizeof(why)));t.gate.fixture[0]=SNAPSHOT_VIEW_NAV;
    assert(!join_config_refused(&t,why,sizeof(why)));
    t.ids[11].klass=WORLD_KEY_CREATED;t.nids=13;
    assert(join_config_refused(&t,why,sizeof(why)) && !strcmp(why,"fixture_unmapped:0000000000176a1e"));
    t.ids[11].klass=WORLD_KEY_SAVED;t.gate.tick=3;
    assert(join_config_refused(&t,why,sizeof(why)) && !strcmp(why,"fixture_early:3"));
    t.gate.tick=633;InterlockedExchange(&world_trace_early,1);
    assert(join_config_refused(&t,why,sizeof(why)) && !strcmp(why,"boot_bind:world_trace"));InterlockedExchange(&world_trace_early,0);
    origin_started=1;assert(join_config_refused(&t,why,sizeof(why)) && !strcmp(why,"boot_bind:origin"));origin_started=0;
    transition_saved_count=1;assert(join_config_refused(&t,why,sizeof(why)) && !strcmp(why,"boot_bind:transition"));transition_saved_count=0;
    snapshot_configured=0;memset(snapshot_actors,0,sizeof(snapshot_actors));
    puts("hot join configuration: the fixture's saved IDs against the package (unmapped, a created identity, a session before tick 4), "
         "the session's fixture views (missing, unbindable), boot-only binds refused PASS");
}
static void test_join_identities_and_wire(void) {
    memset(&join_rx,0,sizeof(join_rx));
    join_id_rec ids[3]={{0x1001,0x2001,WORLD_KEY_SAVED,0},{0x1002,(1ULL<<63)|(600ULL<<40)|0x55,WORLD_KEY_CREATED,0},{0x1003,0x2003,WORLD_KEY_SAVED,0}};
    join_rx.t.ids=ids;join_rx.t.nids=3;assert(join_idmap_build());join_rx.loaded=1;
    uint64_t p=0;assert(join_import_active() && join_import(0x1001,0x77,&p)==0 && p==0x2001);
    assert(join_import(0x4242,0x78,&p)==2 && join_rx.unmapped==1);
    assert(join_import(0x1002,0x79,&p)==1 && (join_rx.mapped_created==1 || join_rx.created_failures==1));
    VirtualFree(join_rx.idmap,0,MEM_RELEASE);
    /* The stand-in rule: before the handover tick a permit's own-slot blob is not this client's proposal. */
    join_rx.based=1;join_rx.handover=UINT64_MAX;join_rx.t.gate.tick=600;
    assert(join_own(650,"abcd")==gate_proposal_any && join_unsealed_ok(650));
    assert(join_handover(700) && !join_handover(710) && join_own(699,"abcd")==gate_proposal_any && !strcmp(join_own(700,"abcd"),"abcd") && !join_unsealed_ok(700));
    char blobs[3][GATE_BLOB_HEX_MAX+1];
    const char *p1="NGGO|3|t|2|651|aa|4d014a07010403580200|bb",*p2="NGGO|3|t|2|651|aa|4d01|bb";
    assert(gate_permit_blobs(p1,strlen(p1),"NGGO|3|t|2|651",2,3,gate_proposal_any,blobs)==1 && !strcmp(blobs[1],"4d014a07010403580200"));
    assert(gate_permit_blobs(p2,strlen(p2),"NGGO|3|t|2|651",2,3,"",blobs)==-1);
    memset(&join_rx,0,sizeof(join_rx));assert(join_own(5,"x")[0]=='x');
    puts("hot join identities (saved, created, unmapped) and the stand-in proposal rule PASS");
}
/* A page this gate could own near the image: private, committed, executable, within a 32-bit displacement of it. */
static unsigned char *tj_near_page(void) {
    for(uintptr_t at=((uintptr_t)image+0x6fa9000u+0xffffu)&~(uintptr_t)0xffffu,n=0;n<4096;n++,at+=0x10000u) {
        unsigned char *p=VirtualAlloc((void *)at,0x1000,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE);
        if(p) return p;
    }
    return NULL;
}
static void tj_rel32(unsigned rva,const unsigned char *target) {int32_t rel=(int32_t)((intptr_t)target-(intptr_t)(image+rva+5));memcpy(image+rva+1,&rel,4);}
static unsigned tj_flip_rva;
static int tj_pristine_flipped(unsigned rva,unsigned size,unsigned char *out) {
    if(!join_pristine_native(rva,size,out)) return 0;
    if(rva==tj_flip_rva) out[size/2]^=1;
    return 1;
}
static void test_join_native_image(const char *path) {
    FILE *file=fopen(path,"rb");assert(file);IMAGE_DOS_HEADER dos;IMAGE_NT_HEADERS64 nt;
    assert(fread(&dos,1,sizeof(dos),file)==sizeof(dos) && dos.e_magic==IMAGE_DOS_SIGNATURE && dos.e_lfanew>0 && dos.e_lfanew<4096);
    assert(!fseek(file,dos.e_lfanew,SEEK_SET) && fread(&nt,1,sizeof(nt),file)==sizeof(nt) && nt.OptionalHeader.SizeOfImage==0x6fa9000);
    unsigned char *prior=image;image=VirtualAlloc(NULL,nt.OptionalHeader.SizeOfImage,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);assert(image);
    for(unsigned i=0;i<nt.FileHeader.NumberOfSections;i++) {
        IMAGE_SECTION_HEADER section;assert(fread(&section,1,sizeof(section),file)==sizeof(section));long next=ftell(file);
        if(section.SizeOfRawData) {assert(!fseek(file,section.PointerToRawData,SEEK_SET));assert(fread(image+section.VirtualAddress,1,section.SizeOfRawData,file)==section.SizeOfRawData);}
        assert(!fseek(file,next,SEEK_SET));
    }
    fclose(file);
    WCHAR wide[MAX_PATH];assert(MultiByteToWideChar(CP_UTF8,0,path,-1,wide,MAX_PATH)>0);
    join_exe_override=wide;join_pins_reset();char why[48];
    assert(join_pins_check(why,sizeof(why)) && join_pins_state==1 && join_patched==0);
    /* The globals the pinned code names: g_AIDirector in SetTimedWake, g_MDScheduler in Cue_ResumeAfterLoad, the seq counter in
     * MDScheduler_Schedule, the save's game object, file and name strings at its only call. */
    int32_t rel;memcpy(&rel,image+0x10668d1+3,4);assert(image[0x10668d1]==0x48 && 0x10668d8+rel==JOIN_AI_DIRECTOR);
    memcpy(&rel,image+0x1115a57+3,4);assert(0x1115a5e+rel==JOIN_MD_SCHEDULER);
    memcpy(&rel,image+0x1122b17+2,4);assert(0x1122b1d+rel==JOIN_MD_SEQ);
    memcpy(&rel,image+0x9fe7c3+3,4);assert(0x9fe7ca+rel==JOIN_SAVE_GAME);
    memcpy(&rel,image+0x9fe7ca+1,4);assert(0x9fe7cf+rel==JOIN_SAVE);
    /* NAV's selectors: RefreshFromPathingState reads g_PathingStateBufferIndex 0x38f4ee4 (the read selector the cursor is relative
     * to); WaitAndSwapBuffers writes it and the write selector 0x2f4b6f4 (lockstep_world_nav.h reads both). */
    memcpy(&rel,image+0xea2a69+2,4);assert(image[0xea2a69]==0x8b && 0xea2a6f+rel==0x38f4ee4);
    memcpy(&rel,image+0xeac1d9+2,4);assert(image[0xeac1d9]==0x89 && 0xeac1df+rel==0x2f4b6f4);
    memcpy(&rel,image+0xeac1df+2,4);assert(image[0xeac1df]==0x89 && 0xeac1e5+rel==0x38f4ee4);
    /* The layouts the capture reads (corrected after J1 build 177), as the pinned code writes them: the context list's head and tail
     * globals and its links in the AIScriptContext constructor and destructor; the cue vtable and the parent, static cue,
     * creator and root fields in Cue_CreateInstance; the creator's list in Cue_CheckConditions; Cue_Destroy's removal from
     * the creator's list (the last entry moves into the hole); the sub-cue slot in FUN_111dbb0; RequestWake's vector index
     * and base; the heap entry's seq, kind and cue in MDScheduler_Schedule, its cue in MDScheduler_Update; the created list
     * and a root's creator and root in the MD import, the created list in the MD export. */
    memcpy(&rel,image+0x10631db+3,4);assert(0x10631e2+rel==JOIN_CTX_TAIL);
    memcpy(&rel,image+0x10631fd+3,4);assert(0x1063204+rel==JOIN_CTX_LIST);
    memcpy(&rel,image+0x10635b8+3,4);assert(0x10635bf+rel==JOIN_CTX_LIST);
    memcpy(&rel,image+0x10635d0+3,4);assert(0x10635d7+rel==JOIN_CTX_TAIL);
    memcpy(&rel,image+0x1111ab5+3,4);assert(0x1111abc+rel==JOIN_CUE_VTABLE && JOIN_CUE_VTABLE==SCRIPT_RNG_CUE_VT);
    static const struct {unsigned rva;const char *op;unsigned n;uint32_t disp;} layout[]={
        {0x10631e2,"\x48\x89\x81",3,JOIN_CTX_PREV},{0x10631f5,"\x48\x05",2,JOIN_CTX_NEXT},{0x1063207,"\x48\x89\xa9",3,JOIN_CTX_NEXT},
        {0x10635af,"\x48\x8d\x90",3,JOIN_CTX_NEXT},{0x10635c7,"\x48\x89\x81",3,JOIN_CTX_PREV},
        {0x1111b88,"\x48\x89\x83",3,JOIN_CUE_PARENT},{0x1111b96,"\x48\x89\x83",3,JOIN_CUE_DEF},{0x1111ba7,"\x48\x89\x83",3,JOIN_CUE_CREATOR},
        {0x1111caf,"\x48\x89\x83",3,JOIN_CUE_ROOT},{0x111c482,"\x48\x8d\x8e",3,JOIN_CUE_INSTANCES},
        {0x1113a75,"\x4c\x8b\x87",3,JOIN_CUE_CREATOR},{0x1113a7c,"\x49\x8b\x90",3,JOIN_CUE_INSTANCES+8},{0x1113aa8,"\x48\x8b\x42\xf8",4,UINT32_MAX},
        {0x111dc95,"\x48\x8b\x85",3,JOIN_CUE_CHILDREN},{0x111dc9c,"\x48\x89\x14\xd8",4,UINT32_MAX},
        {0x106f919,"\x8b\x81",2,JOIN_DIR_VEC_INDEX},{0x106f92e,"\x48\x81\xc1",3,JOIN_DIR_VEC},
        {0x1122b21,"\x89\x4a\x08",3,UINT32_MAX},{0x1122b24,"\x44\x89\x42\x0c",4,UINT32_MAX},{0x1122b2b,"\x4c\x89\x4a\x10",4,UINT32_MAX},
        {0x1121ff8,"\x48\x8b\x48\x10",4,UINT32_MAX},
        {0x1112b2b,"\x4d\x8d\xb4\x24",4,JOIN_CUE_INSTANCES},{0x11125a7,"\x4d\x89\x94\x24",4,JOIN_CUE_CREATOR},
        {0x11125b6,"\x4d\x89\xa4\x24",4,JOIN_CUE_ROOT},{0x111b022,"\x49\x8b\xb6",3,JOIN_CUE_INSTANCES},
        /* The static cue path key (j1-7): the name (+0xc8), script (+0x150), parent (+0x158) and static cue (+0x160) as the
         * XML constructor 0x1111170, a library user's sub-cue copy 0x1111dd0 (its library cue's name, its parent's
         * script) and the MD import write them; the import's second pass reads a copy's name size and gives it its library
         * cue's name; the script constructors (XML, save) put the script's name at +0x18. */
        {0x1111203,"\x4c\x8d\xa6",3,JOIN_CUE_NAME},{0x1111317,"\x4c\x89\xae",3,JOIN_CUE_SCRIPT},{0x1111326,"\x48\x89\x86",3,JOIN_CUE_PARENT},
        {0x111144a,"\x48\x89\xb6",3,JOIN_CUE_DEF},{0x1111e63,"\x48\x8d\x93",3,JOIN_CUE_NAME},{0x1111e6a,"\x48\x8d\x8e",3,JOIN_CUE_NAME},
        {0x1111ef6,"\x49\x8b\x86",3,JOIN_CUE_SCRIPT},{0x1111efd,"\x48\x89\x86",3,JOIN_CUE_SCRIPT},{0x1111f04,"\x4c\x89\xb6",3,JOIN_CUE_PARENT},
        {0x1112041,"\x48\x89\xb6",3,JOIN_CUE_DEF},{0x111227a,"\x49\x8d\xb4\x24",4,JOIN_CUE_NAME},
        {0x111233e,"\x4d\x89\xb4\x24",4,JOIN_CUE_SCRIPT},{0x111234d,"\x4d\x89\x84\x24",4,JOIN_CUE_PARENT},
        {0x11125f5,"\x4d\x89\xa4\x24",4,JOIN_CUE_DEF},{0x1118e9c,"\x49\x83\xbf",3,JOIN_CUE_NAME+0x10},
        {0x1118ee5,"\x48\x8d\xb7",3,JOIN_CUE_NAME},{0x1118f1d,"\x49\x8d\x8f",3,JOIN_CUE_NAME},
        {0x110e04d,"\x48\x83\xc1\x18",4,UINT32_MAX},{0x110e1e1,"\x48\x8d\x79\x18",4,UINT32_MAX},
        /* The world side table: the pose getters read their four vectors from controller+0x10 and +0x20 (movups xmm,[rcx+d8]). */
        {JOIN_POSE_GETTER_10+5,"\x0f\x10\x41\x10",4,UINT32_MAX},{JOIN_POSE_GETTER_10+0x1c,"\x0f\x10\x41\x40",4,UINT32_MAX},
        {JOIN_POSE_GETTER_20+0x11,"\x0f\x10\x41\x20",4,UINT32_MAX},{JOIN_POSE_GETTER_20+0x15,"\x0f\x10\x59\x50",4,UINT32_MAX},
        /* Version 3 (j1-12): RefreshFromPathingState compares the cursor (+0xc8) with the read selector, adds the pending entries
         * (+0x50), writes the refresh time (+0xc0) and the cursor, indexes the banks (+0x30) by 0x1a0 and reads the eta +0x150;
         * SetAttention writes the level +0x1ec; the attention timer compares and writes the due +0x2e0. */
        {0xea2a6f,"\x3b\x87",2,JOIN_NAV_CURSOR},{0xea2aa2,"\x48\x03\x57\x50",4,UINT32_MAX},{0xea2aa6,"\xf2\x0f\x11\x87",4,JOIN_NAV_REFRESH},
        {0xea2ab3,"\x89\x87",2,JOIN_NAV_CURSOR},{0xea2ace,"\x48\x69\xc8",3,WORLD_NAV_BANK_BYTES},{0xea2ad5,"\x48\x8b\x47\x30",4,UINT32_MAX},
        {0xea2ad9,"\x66\x0f\x2f\x84\x08",5,0x150},{0x3d56eb,"\x89\xae",2,JOIN_ATTENTION},{0x6d2043,"\x66\x0f\x2f\x87",4,JOIN_ATTENTION_DUE},
        {0x6d208a,"\xf2\x0f\x11\x8f",4,JOIN_ATTENTION_DUE},
        /* Version 4 (j1-14): Run waits on +0x18, stores the pass time +0x200, tests and clears the skip flag +0x208 and releases
         * +0x10; the load reset sets the flag; CreateAgent appends to the new-agent list (sentinel +0x178, count +0x170, a 0x18-byte
         * node); RunPass walks that list (+0x178, next +8, Agent +0x10) into AssignAgentToBatch and clears it (+0x170), which counts
         * round robin over the vectors (+0x1b8 over +0x180..+0x188, +0x1c0 over +0x198..+0x1a0); a batch's push inserts before its
         * cursor (list +0x10); Region::Move reads the field vector +0x288. */
        {0xeac005,"\x48\x8b\x4b\x18",4,UINT32_MAX},{0xeac06f,"\xf2\x0f\x11\xb3",4,JOIN_PATH_TIME},{0xeac0c0,"\x80\xbb",2,JOIN_PATH_SKIP},
        {0xeac0c6,"\x00",1,UINT32_MAX},{0xeac0e2,"\xc6\x83",2,JOIN_PATH_SKIP},{0xeac0e8,"\x00",1,UINT32_MAX},
        {0xeac10e,"\x48\x8b\x4b\x10",4,UINT32_MAX},{0xeac356,"\xc6\x86",2,JOIN_PATH_SKIP},{0xeac35c,"\x01",1,UINT32_MAX},
        {0xeac691,"\x49\x8b\x9e",3,JOIN_PATH_PENDING+8},{0xeac69f,"\x49\xff\x86",3,JOIN_PATH_PENDING},{0xeac6c1,"\xb9\x18\x00\x00\x00",5,UINT32_MAX},
        {0xeac6cb,"\x48\x89\x70\x10",4,UINT32_MAX},{0xeae498,"\x49\x8b\xbd",3,JOIN_PATH_PENDING+8},{0xeae4b0,"\x48\x8b\x53\x10",4,UINT32_MAX},
        {0xeae4bc,"\x48\x8b\x5b\x08",4,UINT32_MAX},{0xeae4c5,"\x49\x8d\x8d",3,JOIN_PATH_PENDING},
        {0xeaf2d4,"\x48\x8b\x93",3,JOIN_PATH_NEXT+8},{0xeaf2ea,"\x48\x8b\x8b",3,JOIN_PATH_VECTORS+0x18},{0xeaf302,"\x48\x8b\x83",3,JOIN_PATH_VECTORS+0x20},
        {0xeaf32e,"\x48\x8b\x8b",3,JOIN_PATH_NEXT},{0xeaf349,"\x48\x8b\x83",3,JOIN_PATH_VECTORS},{0xeaf35c,"\x48\x8b\x83",3,JOIN_PATH_VECTORS+8},
        {0xf16894,"\x48\x8b\x59\x10",4,UINT32_MAX},{0xf168d4,"\x48\x89\x18",3,UINT32_MAX},{0xf168d7,"\x48\x89\x78\x08",4,UINT32_MAX},
        {0x767428,"\x49\x8b\x9e",3,REGION_RNG_FIELDS}};
    for(unsigned i=0;i<sizeof(layout)/sizeof(*layout);i++) {
        uint32_t disp=0;memcpy(&disp,image+layout[i].rva+layout[i].n,4);
        if(memcmp(image+layout[i].rva,layout[i].op,layout[i].n) || (layout[i].disp!=UINT32_MAX && disp!=layout[i].disp)) {
            printf("join layout %x\n",layout[i].rva);assert(0);
        }
    }
    /* The session's own hooks inside the pinned functions, as the modules install them (live j1-4 refused the save on these):
     * lockstep_ai.h's call at 106e3f6, lockstep_script_rng_md.h's at 112208b, lockstep_md_resume_rng.h's at 1115bad (a new
     * displacement of X4's own E8), an entry jump (E9) and an entry call through a pointer of ours (FF15, lockstep_world_queue.h's
     * form). Then: refused when anything else differs. */
    unsigned char *page=tj_near_page();assert(page);memset(page,0xc3,0x1000);
    assert(image[0x106e3f6]==0xe8 && image[0x112208b]==0xe8 && image[0x1115bad]==0xe8);
    unsigned char saved[5][8];const unsigned sites[5]={0x106e3f6,0x112208b,0x1115bad,0x10669c0,JOIN_TREE_ERASE};
    for(unsigned i=0;i<5;i++) memcpy(saved[i],image+sites[i],8);
    tj_rel32(0x106e3f6,page+0x10);tj_rel32(0x112208b,page+0x20);tj_rel32(0x1115bad,page+0x30);
    image[0x10669c0]=0xe9;tj_rel32(0x10669c0,page+0x40);
    {unsigned char *target=page+0x50;memcpy(page+0x100,&target,8);
     int32_t r=(int32_t)((intptr_t)(page+0x100)-(intptr_t)(image+JOIN_TREE_ERASE+6));image[JOIN_TREE_ERASE]=0xff;image[JOIN_TREE_ERASE+1]=0x15;memcpy(image+JOIN_TREE_ERASE+2,&r,4);}
    assert(join_pins_check(why,sizeof(why)) && join_patched==5 && !strcmp(join_patched_text,"106e3f6,10669c0,b4b810,112208b,1115bad"));
    /* A byte that is no hook; a jump into X4 itself; a pointer to another module's code; X4's own call with its own target. */
    image[0x106d250+0x40]^=1;assert(!join_pins_check(why,sizeof(why)) && !strcmp(why,"patch:106d290"));image[0x106d250+0x40]^=1;
    {unsigned char keep[5];memcpy(keep,image+0x10668b0,5);image[0x10668b0]=0xe9;tj_rel32(0x10668b0,image+0x1000);
     assert(!join_pins_check(why,sizeof(why)) && !strncmp(why,"patch:10668b",12));memcpy(image+0x10668b0,keep,5);}
    {unsigned char *foreign=(unsigned char *)(void *)GetProcAddress(GetModuleHandleW(L"kernel32.dll"),"GetTickCount");assert(foreign);
     memcpy(page+0x100,&foreign,8);assert(!join_pins_check(why,sizeof(why)) && !strcmp(why,"patch:b4b810"));
     unsigned char *target=page+0x50;memcpy(page+0x100,&target,8);}
    assert(join_pins_check(why,sizeof(why)) && join_patched==5);
    {   /* J1 j1-16: the travel snapshot's disp32 byte inside the pinned Agent update is ours only while the module says so. */
        uint64_t keep,patched;memcpy(&keep,image+TSNAP_READ_WORD,8);patched=keep^(0x0full<<56);memcpy(image+TSNAP_READ_WORD,&patched,8);
        assert(!join_pins_check(why,sizeof(why)) && !strcmp(why,"patch:e918d7"));
        tsnap_read_installed=1;tsnap_read_patched=patched;
        assert(join_pins_check(why,sizeof(why)) && join_patched==6 && strstr(join_patched_text,"e918d0"));
        tsnap_read_patched=patched^1;assert(!join_pins_check(why,sizeof(why)) && !strcmp(why,"patch:e918d7"));
        tsnap_read_installed=0;tsnap_read_patched=0;memcpy(image+TSNAP_READ_WORD,&keep,8);
    }
    {   /* J1 j1-17 (patch:eb28d2, an exact_reciprocal rewrite): a byte our modules changed before the arming is kept; one changed
         * after it (and no hook of ours) still refuses. */
        unsigned char keep=image[0x106d250+0x40];image[0x106d250+0x40]^=0x10;
        assert(!join_pins_check(why,sizeof(why)) && !strcmp(why,"patch:106d290"));
        assert(join_pins_baseline() && join_pins_check(why,sizeof(why)) && join_armed_kept>1 && join_patched==0);   /* the hooks, armed before, are kept too */
        image[0x106d250+0x41]^=0x10;assert(!join_pins_check(why,sizeof(why)) && !strcmp(why,"patch:106d291"));image[0x106d250+0x41]^=0x10;
        image[0x106d250+0x40]=keep;
        for(unsigned i=0;i<JOIN_PINS;i++) {free(join_armed_bytes[i]);join_armed_bytes[i]=NULL;}
    }
    for(unsigned i=0;i<5;i++) memcpy(image+sites[i],saved[i],8);
    assert(join_pins_check(why,sizeof(why)) && join_patched==0 && !strcmp(join_patched_text,"-"));
    const unsigned tampers[]={0x106d250+0x40,JOIN_TREE_INSERT+8,JOIN_TREE_ERASE+8,0x1122ae0+0x30,0x115a6f0+0x20,JOIN_SAVE+0x40,0x9fe7c5};
    for(unsigned i=0;i<sizeof(tampers)/sizeof(*tampers);i++) {
        image[tampers[i]]^=1;assert(!join_pins_check(why,sizeof(why)) && !strncmp(why,"patch:",6));image[tampers[i]]^=1;
    }
    /* The file: a pinned range that is not the pin; no executable to read. */
    tj_flip_rva=0x1121f80;join_pristine_read=tj_pristine_flipped;join_pins_reset();
    assert(!join_pins_check(why,sizeof(why)) && !strcmp(why,"pins:1121f80") && join_pins_state==-1);
    join_pristine_read=join_pristine_native;join_exe_override=L"C:/nonexistent/X4.exe";join_pins_reset();
    assert(!join_pins_check(why,sizeof(why)) && !strcmp(why,"pins:file"));
    join_exe_override=NULL;join_pins_reset();
    assert(VirtualFree(page,0,MEM_RELEASE));
    assert(VirtualFree(image,0,MEM_RELEASE));image=prior;
    puts("hot join native pins (AI director, MD scheduler, tree insert/erase, save and its call site) from the file, the session's "
         "hooks inside them accepted, foreign bytes, jumps and file tampers refused PASS");
}
/* The world side table (lockstep_join_world.h) on synthetic objects with the census's layouts: hull +0x280, the shield
 * group +0x290 (a ShieldRechargeSource at +0x90 whose +8 is the group), a pure-transform controller +0x200 (getter at
 * vtable+0x30: 0x10ba20 reads four vectors at +0x10, 0x46ff70 at +0x20). The session (j1-11's shapes): a ship with a
 * recharging shield (navigation controller: no pose) and its weapon reading the same group, a build storage cargo bay
 * with an empty group whose changed time is the loading clock, a turret and a station module with poses, a shield whose
 * recharge is complete at T (captured in the census's full form), an object of an unknown class, one with an unknown
 * controller getter, an absent one. The joiner holds them at other addresses and runtime IDs, as its load left them
 * (the shield's changed time rounded, the empty group's its own loading clock, the rotations rebuilt from rounded
 * degrees). The admission writes what differs; then each object reads as the session's (shield_state, hull, lanes). */
enum {TW_SHIP,TW_WEAPON,TW_CARGO,TW_TURRET,TW_MODULE,TW_FULL,TW_UNKNOWN,TW_ODD,TW_OBJECTS};
typedef struct {
    unsigned char object[TW_OBJECTS][0x300],group[4][0xc0],controller[4][0x80];uint64_t runtime[TW_OBJECTS+1];
    __attribute__((aligned(16))) unsigned char nav[0x100];__attribute__((aligned(16))) unsigned char agent[0x1000];
    int created;   /* the joiner's load made one object of its own (a created identity the package does not name) */
} tw_world;
static tw_world tw_session,tw_joiner;
static unsigned char *tj_solver_fresh;   /* the engine's preparation of a solver, as join_admit_solver calls it */
static uintptr_t tj_solver_cell;   /* the one address the synthetic process maps (join_mapped) */
static int tj_solver_mapped(uint64_t q) {return q==tj_solver_cell;}
static void tj_gravq_cs(void *cs,int enter) {(void)cs;(void)enter;}   /* the synthetic worker's ready lock */
static void *tj_jobinit_filter;static unsigned tj_jobinit_n;   /* JobEngine::Init, recorded */
static unsigned tj_bootev_n;static void tj_bootev(void) {++tj_bootev_n;}   /* the event pass, counted */
static unsigned char *tj_jobinit_flag_seen,tj_jobinit_flag;   /* a job's +0x469 as Init saw it */
static uint64_t tj_jobinit(void *engine,void *filter) {(void)engine;tj_jobinit_filter=filter;++tj_jobinit_n;if(tj_jobinit_flag_seen) tj_jobinit_flag|=*tj_jobinit_flag_seen;return 1;}
/* EVQT: the class's type (slot 1), the engine's removal and post, recorded */
static uint32_t tj_evqt_type(uint32_t cls) {return cls==0x2baf650u?0x20fu:cls==0x2ac17b8u?0x215u:cls==0x2ac1710u?0x20du:0u;}
static void *tj_evqt_rm_src[8];static int tj_evqt_rm_type[8];static unsigned tj_evqt_rms;
static void tj_evqt_remove(void *src,int type,double before) {(void)before;if(tj_evqt_rms<8) {tj_evqt_rm_src[tj_evqt_rms]=src;tj_evqt_rm_type[tj_evqt_rms]=type;}++tj_evqt_rms;}
static void *tj_evqt_post_src[8];static uint64_t *tj_evqt_post_ev[8];static double tj_evqt_post_t[8];static unsigned tj_evqt_posts;
static void tj_evqt_post(void *src,void *ev,double t,char imm) {(void)imm;if(tj_evqt_posts<8) {tj_evqt_post_src[tj_evqt_posts]=src;tj_evqt_post_ev[tj_evqt_posts]=ev;tj_evqt_post_t[tj_evqt_posts]=t;}++tj_evqt_posts;}
static char tj_jobe_text[JOIN_JOBE_CURSOR];static size_t tj_jobe_n;static void *tj_jobe_at;   /* the engine's string assign, recorded */
static void *tj_jobe_assign(void *s,const char *text,size_t n) {memcpy(tj_jobe_text,text,n);tj_jobe_n=n;tj_jobe_at=s;return s;}
static void *tj_solver_prepare(void *agent) {
    if(tj_solver_fresh) *(unsigned char **)((unsigned char *)agent+JOIN_AGENT_SOLVER)=tj_solver_fresh;
    return tj_solver_fresh;
}
static tw_world *tw_find_world;
/* The radar's engine calls as join_admit_grav makes them (version 11): no locks, the joiner's units by owner, the passive unit maker. */
static tw_world *tj_grav_world;static unsigned char *tj_grav_units[2];static unsigned tj_grav_made;
static void tj_grav_lock(void *cs,int enter) {(void)cs;(void)enter;}
static void *tj_grav_lookup(void *w,uint64_t runtime) {
    (void)w;
    if(runtime==tj_grav_world->runtime[TW_SHIP]) return tj_grav_units[0];
    if(runtime==tj_grav_world->runtime[TW_WEAPON] && tj_grav_made) return tj_grav_units[1];
    return NULL;
}
static void *tj_grav_passive(void *w,void *object) {
    (void)w;
    if(object!=tj_grav_world->object[TW_WEAPON] || !tj_grav_units[1]) return NULL;
    *(void **)tj_grav_units[1]=image+JOIN_GRAV_PASSIVE_VT;memcpy(tj_grav_units[1]+8,&tj_grav_world->runtime[TW_WEAPON],8);++tj_grav_made;
    return tj_grav_units[1];
}
static const uint64_t tw_portable[TW_OBJECTS+1]={0x2001,0x2002,0x2003,0x2004,0x2005,0x2008,0x2006,0x2007,0x2009};
#define TW_CREATED_RUNTIME 0x6001u
#define TW_CREATED_KEY ((1ULL<<63)|0x77u)
static void *tw_find(void *map,uint64_t runtime,unsigned mode) {
    assert(map==(void *)tw_find_world && mode==4);
    for(unsigned i=0;i<TW_OBJECTS;i++) if(tw_find_world->runtime[i]==runtime) return tw_find_world->object[i];
    return NULL;
}
static int tw_ids(join_tables *t) {
    t->nids=TW_OBJECTS+1+(tw_find_world->created?1u:0u);t->ids=calloc(t->nids,sizeof(*t->ids));if(!t->ids) return 0;
    for(unsigned i=0;i<=TW_OBJECTS;i++) t->ids[i]=(join_id_rec){tw_find_world->runtime[i],tw_portable[i],WORLD_KEY_SAVED,0};
    if(tw_find_world->created) t->ids[TW_OBJECTS+1]=(join_id_rec){TW_CREATED_RUNTIME,TW_CREATED_KEY,WORLD_KEY_CREATED,0};
    return 1;
}
/* The world's portable key of a runtime ID (NAV's references, KEYS' actors). */
static uint64_t tw_key(uint64_t runtime,unsigned *klass) {
    *klass=WORLD_KEY_SAVED;
    for(unsigned i=0;i<=TW_OBJECTS;i++) if(tw_find_world->runtime[i]==runtime) return tw_portable[i];
    *klass=WORLD_KEY_NONE;return 0;
}
static void tw_group(unsigned char *g,double value,double changed,double maximum,double delay,float rate,float factor,unsigned count) {
    memset(g,0,0xc0);*(void **)(g+JOIN_SHIELD_SOURCE)=image+JOIN_SHIELD_SOURCE_VT;*(void **)(g+JOIN_SHIELD_SOURCE+8)=g;
    *(uintptr_t *)(g+JOIN_SHIELD_BEGIN)=0x1000;*(uintptr_t *)(g+JOIN_SHIELD_BEGIN+8)=0x1000+8*count;
    memcpy(g+JOIN_SHIELD_VALUE,&value,8);memcpy(g+JOIN_SHIELD_CHANGED,&changed,8);memcpy(g+JOIN_SHIELD_MAX,&maximum,8);
    memcpy(g+JOIN_SHIELD_DELAY,&delay,8);memcpy(g+JOIN_SHIELD_RATE,&rate,4);memcpy(g+JOIN_SHIELD_FACTOR,&factor,4);
}
static void tw_object(tw_world *w,unsigned i,unsigned vt,double hull,unsigned char *group,unsigned char *controller) {
    unsigned char *o=w->object[i];memset(o,0,0x300);*(void **)o=image+vt;memcpy(o+8,&w->runtime[i],8);memcpy(o+JOIN_HULL,&hull,8);
    *(unsigned char **)(o+JOIN_SHIELD_GROUP)=group;*(unsigned char **)(o+JOIN_CONTROLLER)=controller;
}
static void tw_controller(unsigned char *c,unsigned vt,float base) {
    memset(c,0,0x80);*(void **)c=image+vt;
    for(unsigned i=0;i<20;i++) {float f=base+0.25f*(float)i;memcpy(c+0x10+4*i,&f,4);}
}
/* The pathing state's read and write selectors (WaitAndSwapBuffers 0xeac170): the session read 1 at T, the joiner 0 (j1-12). */
static void tw_phase(int joiner) {*(uint32_t *)(image+0x38f4ee4)=joiner?0u:1u;*(uint32_t *)(image+0x2f4b6f4)=joiner?1u:0u;}
/* The ship's navigation controller and pathing Agent with lockstep_world_nav.h's links. The session (j1-12 at T): cursor 1 on
 * the read bank, a stale parameter due, the probe due, two different states in the banks with mode 1, a reference to the cargo
 * bay in the read bank, pending transition counts 1 and 2, the write bank's pending entry naming the cargo bay. The joiner (its
 * loader): cursor 1 (now the write bank), no parameter due, both banks the save's rounded state with mode 27, no references,
 * no counts. */
static void tw_nav(tw_world *w,int joiner) {
    unsigned char *c=w->nav,*a=w->agent,*ship=w->object[TW_SHIP];memset(c,0,sizeof(w->nav));memset(a,0,sizeof(w->agent));
    *(void **)c=image+JOIN_NAV_VTABLE;*(void **)(c+0x28)=ship;*(void **)(c+0x30)=a+0xad0;*(void **)(c+0x38)=a+0xe10;*(void **)(c+0x40)=a;
    *(void **)(c+0x48)=a+0x6e0;*(void **)(c+0x50)=a+0xed0;*(void **)(a+0xef0)=ship;
    double refresh=26.98,due=joiner?-1.0:9.075415789763133,probe=joiner?7.5:27.5,eta=-1.0;uint32_t cursor=1;
    memcpy(c+JOIN_NAV_REFRESH,&refresh,8);memcpy(c+JOIN_NAV_CURSOR,&cursor,4);memcpy(c+JOIN_NAV_PARAMS_DUE,&due,8);c[JOIN_NAV_PARAMS_ON]=1;
    memcpy(a+0x6e0+JOIN_NAV_PROBE_DUE,&probe,8);a[0x6e0+JOIN_NAV_PROBE_ON]=1;
    for(unsigned p=0;p<2;p++) {
        unsigned char *bank=a+0xad0+p*0x1a0;uint32_t mode=joiner?27u:1u;
        for(unsigned v=0;v<6;v++) for(unsigned l=0;l<4;l++) {
            float f=joiner?100.25f+(float)(4*v+l):(p?100.0f:200.0f)+(float)(4*v+l);if(l==3) f=joiner?-1.0f:1.0f;   /* w lanes: not the census's */
            memcpy(bank+16*v+4*l,&f,4);
        }
        memcpy(bank+0xcc,&mode,4);memcpy(bank+0x150,&eta,8);bank[0x15b]=2;
        if(!joiner && p==1) memcpy(bank+0xb0,&w->runtime[TW_CARGO],8);
    }
    if(!joiner) {
        uint64_t count1=1,count0=2;memcpy(a+0xed0+16+8,&count1,8);memcpy(a+0xed0,&w->runtime[TW_CARGO],8);memcpy(a+0xed0+8,&count0,8);
    }
}
static void tw_level(tw_world *w,unsigned i,uint32_t level,double due) {
    memcpy(w->object[i]+JOIN_ATTENTION,&level,4);memcpy(w->object[i]+JOIN_ATTENTION_DUE,&due,8);
}
static void tw_build(tw_world *w,int joiner) {
    memset(w,0,sizeof(*w));
    for(unsigned i=0;i<=TW_OBJECTS;i++) w->runtime[i]=(joiner?0x5001:0x1001)+i;
    /* the ship's group: the session's changed time 26.41633..., the joiner's the save's 26.4163 (and its rate summed in another
     * order); the cargo bay's empty group: each side's loading clock; the full one: value 100 of 200, its recharge complete at 12 s,
     * before T = 30 (the joiner: as saved). */
    tw_group(w->group[0],1150.0,joiner?26.4163:26.416333333333,1955.0,15.0,joiner?1.0000001f:1.0f,120.0f,1);
    tw_group(w->group[1],0.0,joiner?28.183:7.083,0.0,0.0,0.0f,0.0f,0);
    tw_group(w->group[2],100.0,1.0,200.0,1.0,10.0f,1.0f,1);
    tw_controller(w->controller[1],0x2a35000,joiner?-0.9999999f:-1.0f);     /* the turret's rotation rebuilt from rounded degrees */
    tw_controller(w->controller[2],0x2a35100,joiner?0.0174531f:0.0174533f);
    tw_controller(w->controller[3],0x2a35200,3.0f);
    tw_object(w,TW_SHIP,0x4700,3100.0,w->group[0],w->nav);
    tw_object(w,TW_WEAPON,0x4500,500.0,w->group[0],NULL);
    tw_object(w,TW_CARGO,0x4500,2500.0,w->group[1],NULL);
    tw_object(w,TW_TURRET,0x4300,1000.0,NULL,w->controller[1]);
    tw_object(w,TW_MODULE,0x4400,0.0,NULL,w->controller[2]);
    tw_object(w,TW_FULL,0x4500,800.0,w->group[2],NULL);
    tw_object(w,TW_UNKNOWN,0x4600,1.0,NULL,NULL);
    tw_object(w,TW_ODD,0x4400,0.0,NULL,w->controller[3]);
    tw_nav(w,joiner);
    /* attention (j1-12): the session's ship at 5 (an Object: its timer due), the cargo bay at 3; the joiner's both at 2 */
    tw_level(w,TW_SHIP,joiner?2u:5u,joiner?7.0:28.1);tw_level(w,TW_CARGO,joiner?2u:3u,0.0);
    for(unsigned i=TW_WEAPON;i<TW_OBJECTS;i++) if(i!=TW_CARGO) tw_level(w,i,2u,0.0);
}
static void tw_admit(tw_world *joiner_world,const join_tables *package,int mode) {
    memset(&join_rx,0,sizeof(join_rx));join_rx.t=*package;join_rx.mode=mode;tw_find_world=joiner_world;join_world_map=joiner_world;tw_phase(1);
}
/* The census's navigation tuple of the world's ship (lockstep_world_nav.h, as the census encodes it). */
static void tw_census_nav(tw_world *w,int joiner,unsigned char timing[WORLD_NAV_TIMING_BYTES],unsigned char motion[WORLD_NAV_MOTION_BYTES]) {
    world_nav_snapshot n;const char *why;unsigned char raw[WORLD_NAV_RAW_BYTES];unsigned flags=0;tw_find_world=w;tw_phase(joiner);
    assert(world_nav_capture(join_read,join_nav_key,(uintptr_t)image,(uintptr_t)w->object[TW_SHIP],(uintptr_t)w->nav,w->runtime[TW_SHIP],&n,&why));
    world_nav_encode(&n,timing,motion,raw,&flags);assert(!flags);
}
/* The gate's first-call tables as the session left them at T (attention_all and the nav parameter init know the ship, fixture
 * actor 1 is the ship and had its attention first refresh); restored by tw_tables_reset. */
static uint64_t tw_actor_id;static unsigned tw_snapshot_configured;static uint64_t tw_snapshot_saved;
static struct {uint64_t *attention_all,*probe,*params;unsigned attention_all_count,probe_count,params_count,attention_init[ACTOR_CAPACITY],
               nav_init[ACTOR_CAPACITY];int attention_all_armed,probe_armed,params_armed;} tw_saved;
static void tw_tables_save(int restore) {
    if(!restore) {
        tw_saved.attention_all=malloc(sizeof(attention_all_table));tw_saved.probe=malloc(sizeof(nav_probe_table));tw_saved.params=malloc(sizeof(nav_params_table));
        assert(tw_saved.attention_all && tw_saved.probe && tw_saved.params);
        memcpy(tw_saved.attention_all,attention_all_table,sizeof(attention_all_table));memcpy(tw_saved.probe,nav_probe_table,sizeof(nav_probe_table));
        memcpy(tw_saved.params,nav_params_table,sizeof(nav_params_table));tw_saved.attention_all_count=attention_all_count;
        tw_saved.probe_count=nav_probe_count;tw_saved.params_count=nav_params_count;tw_saved.attention_all_armed=attention_all_armed;
        tw_saved.probe_armed=nav_probe_armed;tw_saved.params_armed=nav_params_armed;
        memcpy(tw_saved.attention_init,attention_initialized,sizeof(attention_initialized));memcpy(tw_saved.nav_init,nav_initialized,sizeof(nav_initialized));
        memset(attention_initialized,0,sizeof(attention_initialized));memset(nav_initialized,0,sizeof(nav_initialized));
        return;
    }
    memcpy(attention_all_table,tw_saved.attention_all,sizeof(attention_all_table));memcpy(nav_probe_table,tw_saved.probe,sizeof(nav_probe_table));
    memcpy(nav_params_table,tw_saved.params,sizeof(nav_params_table));attention_all_count=tw_saved.attention_all_count;
    nav_probe_count=tw_saved.probe_count;nav_params_count=tw_saved.params_count;attention_all_armed=tw_saved.attention_all_armed;
    nav_probe_armed=tw_saved.probe_armed;nav_params_armed=tw_saved.params_armed;
    memcpy(attention_initialized,tw_saved.attention_init,sizeof(attention_initialized));memcpy(nav_initialized,tw_saved.nav_init,sizeof(nav_initialized));
    free(tw_saved.attention_all);free(tw_saved.probe);free(tw_saved.params);
}
static void tw_tables_reset(int armed) {
    memset(attention_all_table,0,sizeof(attention_all_table));attention_all_count=0;attention_all_armed=armed;
    memset(nav_probe_table,0,sizeof(nav_probe_table));nav_probe_count=0;nav_probe_armed=armed;
    memset(nav_params_table,0,sizeof(nav_params_table));nav_params_count=0;nav_params_armed=armed;
    attention_initialized[0]=0;nav_initialized[0]=0;
}
/* Version 4 (lockstep_join_world.h, report 1.8) on a synthetic pathing manager with the engine's list layout (a 0x18-byte node
 * {prev, next, Agent}, a ring through a sentinel; a batch {flag, count, sentinel, cursor}; the new-agent list's count +0x170 and
 * sentinel +0x178; the vectors +0x180/+0x198, the round-robin indices +0x1b8/+0x1c0) and on hazard regions (U::Region with its
 * field vector +0x288, DamageFields with last +0x10f0 and due +0x10f8). The session (j1-14's shapes): a flagged batch of the
 * ship and the cargo bay with its cursor on the cargo bay, a batch whose member's owner has no portable ID, an empty batch, the
 * turret's Agent new. The joiner as its load left it: every batch empty, every loaded Agent on the new-agent list in its load
 * order, one Agent of its own. */
enum {TP_SHIP,TP_CARGO,TP_LOCAL,TP_TURRET,TP_EXTRA,TP_AGENTS};
typedef struct {uintptr_t prev,next,agent;} tp_node;
typedef struct {
    __attribute__((aligned(16))) unsigned char manager[0x240];unsigned char batches[2][2*JOIN_PATH_BATCH];
    tp_node sentinel[4],node[TP_AGENTS];unsigned char agent[TP_AGENTS][0xf00],owner[TP_AGENTS][0x20];
    unsigned char region[0x300],field[3][0x1100];uintptr_t fields[3];
} tp_world;
static tp_world tp_session,tp_joiner;
static const uint64_t tp_portable[TP_AGENTS]={0x2001,0x2003,0,0x2004,(1ULL<<63)|0x99};
#define TP_REGION_KEY 0x2005u
static tp_world *tp_keys;
static uint64_t tp_key(uint64_t runtime,unsigned *klass) {
    for(unsigned i=0;i<TP_AGENTS;i++) if(tp_portable[i] && runtime==0x7000u+i+(tp_keys==&tp_joiner?0x100u:0u))
        {*klass=tp_portable[i]>>63?WORLD_KEY_CREATED:WORLD_KEY_SAVED;return tp_portable[i];}
    *klass=WORLD_KEY_NONE;return 0;
}
static void tp_ring(tp_node *sentinel) {sentinel->prev=sentinel->next=(uintptr_t)sentinel;sentinel->agent=0;}
static void tp_push(tp_node *sentinel,tp_node *node) {join_path_append((uintptr_t)sentinel,(uintptr_t)node);}
static void tp_batch(tp_world *w,unsigned v,unsigned b,unsigned char flag,unsigned sentinel) {
    unsigned char *at=w->batches[v]+b*JOIN_PATH_BATCH;tp_ring(&w->sentinel[sentinel]);at[0]=flag;
    *(uint64_t *)(at+8)=0;*(uintptr_t *)(at+0x10)=(uintptr_t)&w->sentinel[sentinel];*(uintptr_t *)(at+0x18)=(uintptr_t)&w->sentinel[sentinel];
}
static void tp_member(tp_world *w,unsigned v,unsigned b,unsigned agent) {
    unsigned char *at=w->batches[v]+b*JOIN_PATH_BATCH;tp_push(*(tp_node **)(at+0x10),&w->node[agent]);++*(uint64_t *)(at+8);
}
static void tp_pending(tp_world *w,unsigned agent) {tp_push(&w->sentinel[3],&w->node[agent]);++*(uint64_t *)(w->manager+JOIN_PATH_PENDING);}
/* joiner: the load order of the new-agent list, the agents present (a mask). */
static void tp_build(tp_world *w,int joiner,const unsigned *order,unsigned n) {
    memset(w,0,sizeof(*w));unsigned char *m=w->manager;
    for(unsigned i=0;i<TP_AGENTS;i++) {
        uint64_t runtime=0x7000u+i+(joiner?0x100u:0u);memcpy(w->owner[i]+8,&runtime,8);
        *(uintptr_t *)(w->agent[i]+PATH_DET_AGENT_OWNER)=(uintptr_t)w->owner[i];w->node[i].agent=(uintptr_t)w->agent[i];
    }
    uintptr_t v0[2]={(uintptr_t)w->batches[0],(uintptr_t)w->batches[0]+2*JOIN_PATH_BATCH},v1[2]={(uintptr_t)w->batches[1],(uintptr_t)w->batches[1]+JOIN_PATH_BATCH};
    memcpy(m+JOIN_PATH_VECTORS,v0,16);memcpy(m+JOIN_PATH_VECTORS+0x18,v1,16);
    tp_batch(w,0,0,1,0);tp_batch(w,0,1,0,1);tp_batch(w,1,0,0,2);tp_ring(&w->sentinel[3]);
    *(uintptr_t *)(m+JOIN_PATH_PENDING+8)=(uintptr_t)&w->sentinel[3];*(int32_t *)(m+JOIN_PATH_STATE)=2;m[JOIN_PATH_WAITED]=1;m[JOIN_PATH_SKIP]=1;
    if(!joiner) {
        tp_member(w,0,0,TP_SHIP);tp_member(w,0,0,TP_CARGO);*(uintptr_t *)(w->batches[0]+0x18)=(uintptr_t)&w->node[TP_CARGO];
        tp_member(w,0,1,TP_LOCAL);*(uintptr_t *)(w->batches[0]+JOIN_PATH_BATCH+0x18)=(uintptr_t)&w->node[TP_LOCAL];
        tp_pending(w,TP_TURRET);*(uint64_t *)(m+JOIN_PATH_NEXT)=1;m[JOIN_PATH_SKIP]=0;
    } else for(unsigned i=0;i<n;i++) tp_pending(w,order[i]);
    /* a region: a DamageField, a field of another kind, a DamageField */
    *(void **)w->region=image+JOIN_REGION_VTABLE;{uint64_t runtime=joiner?0x7205u:0x7105u;memcpy(w->region+8,&runtime,8);}
    for(unsigned f=0;f<3;f++) {
        *(void **)w->field[f]=image+(f==1?0x2c4b400u:REGION_RNG_FIELD_VTABLE);w->fields[f]=(uintptr_t)w->field[f];
        double last=joiner?28.183:f?20.383:27.316333333333866,due=joiner?28.183:f?21.5:28.4;
        memcpy(w->field[f]+REGION_RNG_LAST,&last,8);memcpy(w->field[f]+REGION_RNG_DUE,&due,8);
    }
    uintptr_t fv[2]={(uintptr_t)w->fields,(uintptr_t)(w->fields+3)};memcpy(w->region+REGION_RNG_FIELDS,fv,16);
}
/* Every batch's ring and the new-agent list as portable IDs in list order ('-' an Agent without one), with the cursor ('*'). */
static void tp_layout(tp_world *w,char *out,size_t cap) {
    size_t used=0;out[0]=0;tp_keys=w;
    for(unsigned v=0;v<2;v++) for(unsigned b=0;b<(v?1u:2u);b++) {
        unsigned char *at=w->batches[v]+b*JOIN_PATH_BATCH;uintptr_t s=*(uintptr_t *)(at+0x10),cursor=*(uintptr_t *)(at+0x18),*nodes=NULL;
        uint64_t count=*(uint64_t *)(at+8);assert(join_path_ring(s,count,&nodes,"test"));
        used+=(size_t)snprintf(out+used,cap-used,"%s%u.%u:",used?" ":"",v,b);
        for(uint64_t i=0;i<count;i++) {uint64_t k=join_path_owner(nodes[i]);used+=(size_t)snprintf(out+used,cap-used,"%s%llx%s",i?",":"",(unsigned long long)k,nodes[i]==cursor?"*":"");}
        if(!count) {assert(cursor==s);used+=(size_t)snprintf(out+used,cap-used,"-");}
        free(nodes);
    }
    uintptr_t *nodes=NULL;uint64_t count=*(uint64_t *)(w->manager+JOIN_PATH_PENDING);
    assert(join_path_ring(*(uintptr_t *)(w->manager+JOIN_PATH_PENDING+8),count,&nodes,"test"));
    used+=(size_t)snprintf(out+used,cap-used," new:");
    for(uint64_t i=0;i<count;i++) used+=(size_t)snprintf(out+used,cap-used,"%s%llx",i?",":"",(unsigned long long)join_path_owner(nodes[i]));
    free(nodes);
    snprintf(out+used,cap-used," next=%llu/%llu",(unsigned long long)*(uint64_t *)(w->manager+JOIN_PATH_NEXT),(unsigned long long)*(uint64_t *)(w->manager+JOIN_PATH_NEXT+8));
}
static unsigned tp_primes;
static int tp_prime_stub(unsigned char *manager,char *why,size_t cap) {(void)why;(void)cap;++tp_primes;manager[JOIN_PATH_SKIP]=0;return 1;}
/* PathingManager::Run for one wake (0xeabf00): wait, the pass time from TimerBase, a pass only without the skip flag, the flag cleared, done. */
static volatile LONG tp_passes;
static DWORD WINAPI tp_run(void *arg) {
    unsigned char *m=arg;
    if(WaitForSingleObject(*(HANDLE *)(m+JOIN_PATH_WAKE),5000)!=WAIT_OBJECT_0) return 1;
    memcpy(m+JOIN_PATH_TIME,image+GAME_RVA,8);
    if(!m[JOIN_PATH_SKIP]) InterlockedIncrement(&tp_passes);
    m[JOIN_PATH_SKIP]=0;ReleaseSemaphore(*(HANDLE *)(m+JOIN_PATH_DONE),1,NULL);return 0;
}
static void *tp_find(void *map,uint64_t runtime,unsigned mode) {
    tp_world *w=map;assert(mode==4);
    uint64_t own;memcpy(&own,w->region+8,8);return runtime==own?w->region:NULL;
}
static void test_join_path_region(void) {
    unsigned char *saved_manager;memcpy(&saved_manager,image+JOIN_PATH_MANAGER,8);
    uint64_t (*key)(uint64_t,unsigned *)=join_key;join_find_fn find=join_world_find;void *map=join_world_map;
    int (*prime)(unsigned char *,char *,size_t)=join_path_prime;double game_saved;memcpy(&game_saved,image+GAME_RVA,8);
    join_key=tp_key;join_path_prime=tp_prime_stub;join_world_find=tp_find;
    /* The saver: PATH as golden_v4 (the round-robin index of vector 0 at 1), REGION of the two DamageFields. */
    tp_build(&tp_session,0,NULL,0);tp_keys=&tp_session;*(unsigned char **)(image+JOIN_PATH_MANAGER)=tp_session.manager;
    join_tables t;memset(&t,0,sizeof(t));assert(join_capture_path(&t));
    const join_path_rec want[9]={{0,JOIN_PATH_KIND_MANAGER,0,2,0,1,0},{0,JOIN_PATH_KIND_MANAGER,1,1,0,0,0},{0,JOIN_PATH_KIND_BATCH,0,0,1,2,1},
                                 {0,JOIN_PATH_KIND_BATCH,0,1,0,1,0},{0,JOIN_PATH_KIND_BATCH,1,0,0,0,0},{0x2001,JOIN_PATH_KIND_MEMBER,0,0,0,0,0},
                                 {0x2003,JOIN_PATH_KIND_MEMBER,0,0,1,0,0},{0,JOIN_PATH_KIND_MEMBER,0,1,0,0,JOIN_PATH_LOCAL},
                                 {0x2004,JOIN_PATH_KIND_PENDING,0,0,0,0,0}};
    assert(t.npath==9 && !memcmp(t.path,want,sizeof(want)));
    {uint32_t cap=0;assert(join_capture_region(&t,tp_session.region,TP_REGION_KEY,&cap) && t.nregion==2);
     assert(t.region[0].region==TP_REGION_KEY && t.region[0].field==0 && t.region[0].last==tj_bits(27.316333333333866) && t.region[0].due==tj_bits(28.4) &&
            t.region[1].field==2 && t.region[1].last==tj_bits(20.383) && t.region[1].due==tj_bits(21.5));}
    char layout[256];tp_layout(&tp_session,layout,sizeof(layout));
    assert(!strcmp(layout,"0.0:2001,2003* 0.1:0* 1.0:- new:2004 next=1/0"));
    /* A broken ring, a cursor outside its batch: the capture refuses, naming them. */
    {join_tables bad;memset(&bad,0,sizeof(bad));tp_session.node[TP_CARGO].prev=0x1234;
     assert(!join_capture_path(&bad) && strstr(join_fault,"path,check=batch_link,index=1") && strstr(join_fault,"vector=0,batch=0"));
     tp_session.node[TP_CARGO].prev=(uintptr_t)&tp_session.node[TP_SHIP];join_tables_free(&bad);memset(&bad,0,sizeof(bad));
     *(uintptr_t *)(tp_session.batches[0]+0x18)=(uintptr_t)&tp_session.node[TP_TURRET];
     assert(!join_capture_path(&bad) && strstr(join_fault,"path,check=cursor,vector=0,batch=0"));
     *(uintptr_t *)(tp_session.batches[0]+0x18)=(uintptr_t)&tp_session.node[TP_CARGO];join_tables_free(&bad);}
    /* The joiner (report): its load order turret, ship, its own Agent, the local one, the cargo bay. The batches as the session's, the
     * cursor on the cargo bay, the local member not placed (its batch empty, the sentinel its cursor), the session's new Agent on the
     * list, the joiner's two others parked in their order, the round-robin indices; one wake. */
    static const unsigned order[]={TP_TURRET,TP_SHIP,TP_EXTRA,TP_LOCAL,TP_CARGO};
    tp_build(&tp_joiner,1,order,5);tp_keys=&tp_joiner;*(unsigned char **)(image+JOIN_PATH_MANAGER)=tp_joiner.manager;
    t.version=JOIN_PACKAGE_VERSION;memset(&join_rx,0,sizeof(join_rx));tp_primes=0;
    assert(join_admit_path(&t,0));
    assert(join_rx.path_members==2 && join_rx.path_pending==1 && join_rx.path_missing==0 && join_rx.path_local==1 && join_rx.path_extra==2 &&
           join_rx.path_shape==0 && join_rx.path_primed==1 && tp_primes==1 && !tp_joiner.manager[JOIN_PATH_SKIP]);
    tp_layout(&tp_joiner,layout,sizeof(layout));
    assert(!strcmp(layout,"0.0:2001,2003* 0.1:- 1.0:- new:2004 next=1/0"));
    {uintptr_t *parked=NULL;assert(join_path_ring((uintptr_t)join_path_parked,2,&parked,"test") && parked[0]==(uintptr_t)&tp_joiner.node[TP_EXTRA] &&
                                   parked[1]==(uintptr_t)&tp_joiner.node[TP_LOCAL]);free(parked);
     join_path_parked[0]=join_path_parked[1]=(uintptr_t)join_path_parked;}
    {int extra=0;for(unsigned i=0;i<join_rx.nrows;i++) extra|=strstr(join_rx.rows[i],"path side=joiner id=8000000000000099 why=extra")!=NULL;assert(extra);}
    /* Strict: the local member and the extras refuse before anything is written. */
    tp_build(&tp_joiner,1,order,5);memset(&join_rx,0,sizeof(join_rx));tp_primes=0;
    assert(join_admit_path(&t,1) && join_rx.path_local==1 && join_rx.path_extra==2 && !join_rx.path_members && !tp_primes);
    tp_layout(&tp_joiner,layout,sizeof(layout));assert(!strcmp(layout,"0.0:- 0.1:- 1.0:- new:2004,2001,8000000000000099,0,2003 next=0/0"));
    /* Strict and whole: a session without the local member, a joiner without Agents of its own. Every Agent placed. */
    {join_tables s=t;join_path_rec clean[8];memcpy(clean,want,sizeof(clean));
     clean[3].value=0;clean[3].position=0;clean[7]=want[8];s.path=clean;s.npath=8;assert(join_path_shape(clean,8));
     static const unsigned loaded[]={TP_CARGO,TP_TURRET,TP_SHIP};tp_build(&tp_joiner,1,loaded,3);memset(&join_rx,0,sizeof(join_rx));
     assert(join_admit_path(&s,1) && join_rx.path_members==2 && join_rx.path_pending==1 && !join_rx.path_extra && !join_rx.path_local &&
            !join_rx.path_shape && join_rx.path_primed==1);
     tp_layout(&tp_joiner,layout,sizeof(layout));assert(!strcmp(layout,"0.0:2001,2003* 0.1:- 1.0:- new:2004 next=1/0"));
     /* again on that joiner: its batches are not empty any more (shape, nothing written) */
     memset(&join_rx,0,sizeof(join_rx));assert(join_admit_path(&s,0) && join_rx.path_shape==1 && !join_rx.path_members &&
                                              strstr(join_rx.rows[0],"path side=joiner why=shape check=batch,vector=0,batch=0"));
     /* another number of batches: shape */
     tp_build(&tp_joiner,1,loaded,3);*(uintptr_t *)(tp_joiner.manager+JOIN_PATH_VECTORS+8)-=JOIN_PATH_BATCH;memset(&join_rx,0,sizeof(join_rx));
     assert(join_admit_path(&s,0) && join_rx.path_shape==1 && strstr(join_rx.rows[0],"check=batches,vector=0,joiner=1,session=2"));
     /* a session Agent the joiner does not have: missing (report places the rest) */
     static const unsigned short_order[]={TP_TURRET,TP_SHIP};tp_build(&tp_joiner,1,short_order,2);memset(&join_rx,0,sizeof(join_rx));
     assert(join_admit_path(&s,0) && join_rx.path_missing==1 && join_rx.path_members==1 && strstr(join_rx.rows[0],"path side=session id=0000000000002003 why=missing kind=2 at=0/0/1"));
     tp_layout(&tp_joiner,layout,sizeof(layout));assert(!strcmp(layout,"0.0:2001* 0.1:- 1.0:- new:2004 next=1/0"));}
    /* The wake itself: PathingManager::Run on a thread with the engine's two semaphores. The skip flag keeps the pass out; the pass
     * time is TimerBase's; a pass in flight (waited 0) is not woken. */
    {
        unsigned char *m=tp_joiner.manager;HANDLE wake=CreateSemaphoreW(NULL,0,1,NULL),done=CreateSemaphoreW(NULL,0,1,NULL);assert(wake && done);
        memcpy(m+JOIN_PATH_WAKE,&wake,8);memcpy(m+JOIN_PATH_DONE,&done,8);double game=28.183;memcpy(image+GAME_RVA,&game,8);
        m[JOIN_PATH_SKIP]=0;m[JOIN_PATH_WAITED]=1;*(int32_t *)(m+JOIN_PATH_STATE)=2;tp_passes=0;
        HANDLE thread=CreateThread(NULL,0,tp_run,m,0,NULL);assert(thread);char why[96];
        assert(join_path_prime_native(m,why,sizeof(why)) && !m[JOIN_PATH_SKIP] && !tp_passes);
        double time;memcpy(&time,m+JOIN_PATH_TIME,8);assert(time==28.183);
        assert(WaitForSingleObject(thread,5000)==WAIT_OBJECT_0);CloseHandle(thread);
        m[JOIN_PATH_WAITED]=0;assert(!join_path_prime_native(m,why,sizeof(why)) && !strcmp(why,"state=2,waited=0"));
        CloseHandle(wake);CloseHandle(done);memset(m+JOIN_PATH_DONE,0,16);
    }
    /* REGION into the joiner's region (another runtime ID): both fields' times written; a field index that is no DamageField, a region
     * the joiner lacks. */
    {
        join_tables mine;memset(&mine,0,sizeof(mine));join_id_rec id={0x7205u,TP_REGION_KEY,WORLD_KEY_SAVED,0};mine.ids=&id;mine.nids=1;
        join_world_map=&tp_joiner;memset(&join_rx,0,sizeof(join_rx));
        assert(join_admit_region(&t,&mine) && join_rx.region_applied==2 && join_rx.region_fixed==2 && !join_rx.region_missing && !join_rx.region_shape);
        for(unsigned f=0;f<3;f+=2) assert(!memcmp(tp_joiner.field[f]+REGION_RNG_LAST,tp_session.field[f]+REGION_RNG_LAST,16));
        assert(join_admit_region(&t,&mine) && join_rx.region_applied==4 && join_rx.region_fixed==2);   /* nothing differs any more */
        t.region[1].field=1;memset(&join_rx,0,sizeof(join_rx));
        assert(join_admit_region(&t,&mine) && join_rx.region_shape==1 && strstr(join_rx.rows[0],"region side=joiner id=0000000000002005 field=1 why=shape"));
        t.region[1].field=2;id.runtime=0x7999u;memset(&join_rx,0,sizeof(join_rx));
        assert(join_admit_region(&t,&mine) && join_rx.region_missing==2 && strstr(join_rx.rows[0],"region side=session id=0000000000002005 field=0 why=missing"));
    }
    memset(&join_rx,0,sizeof(join_rx));join_tables_free(&t);
    memcpy(image+JOIN_PATH_MANAGER,&saved_manager,8);memcpy(image+GAME_RVA,&game_saved,8);
    join_key=key;join_path_prime=prime;join_world_find=find;join_world_map=map;
    puts("hot join PATH and REGION: the pathing manager's batches (members by owner, cursor, flag), round-robin indices and new agents "
         "captured; rebuilt on a joiner from its new-agent list in the session's layout (local, extra, missing, shape, strict without "
         "writes), its pathing thread woken once with the skip flag; damage field pass times written PASS");
}
static unsigned char tj_flight_pool[4][0x400];static unsigned tj_flight_allocs;static int tj_flight_fail;
static void *tj_flight_alloc(size_t bytes) {return tj_flight_fail || tj_flight_allocs>=4 || bytes>0x400?NULL:tj_flight_pool[tj_flight_allocs++];}
static __attribute__((aligned(16))) unsigned char tj_big_pool[24][0x400];static unsigned tj_big_allocs;   /* versions 11, 12: more and larger lists */
static void *tj_big_alloc(size_t bytes) {return tj_big_allocs>=24 || bytes>0x400?NULL:tj_big_pool[tj_big_allocs++];}
/* The engine's FlightPoint vectors as the joiner's ROUTE admission drives them (synthetic): clear sets end = begin; push_back
 * copy-constructs at the end what the engine's copy copies (+0x10..+0x67, +0x70..+0x7e, +0x80..+0x8f; +0x08 and +0x7f stay as
 * the buffer had them; the sub-point into a pool slot), first into a fresh pool buffer of four entries when the vector has no
 * room. tj_route_drop: the copy zeroes the reference (the engine's when it finds no object for it). */
static __attribute__((aligned(16))) unsigned char tj_route_pool[4][4*JOIN_AGENT_WAYPOINT],tj_route_subs[8][JOIN_ROUTE_SUBSIZE];
static unsigned tj_route_pools,tj_route_nsubs,tj_route_clears,tj_route_pushes;static int tj_route_drop;
static void tj_route_reset(void) {tj_route_pools=tj_route_nsubs=tj_route_clears=tj_route_pushes=0;tj_route_drop=0;}
static void tj_route_clear(void *vector) {uintptr_t *v=vector;v[1]=v[0];++tj_route_clears;}
static void tj_route_push(void *vector,const void *point) {
    uintptr_t *v=vector,sub=0;const unsigned char *src=point;++tj_route_pushes;
    if(v[1]==v[2]) {
        assert(tj_route_pools<4);unsigned char *p=tj_route_pool[tj_route_pools++];const size_t used=v[1]-v[0];
        assert(used+JOIN_AGENT_WAYPOINT<=sizeof(tj_route_pool[0]));memset(p,0xee,sizeof(tj_route_pool[0]));
        if(used) memcpy(p,(void *)v[0],used);
        v[0]=(uintptr_t)p;v[1]=v[0]+used;v[2]=v[0]+sizeof(tj_route_pool[0]);
    }
    unsigned char *dst=(unsigned char *)v[1];
    *(void **)dst=image+JOIN_ROUTE_POINT_VT;memcpy(dst+0x10,src+0x10,0x58);memcpy(dst+0x70,src+0x70,0xf);memcpy(dst+0x80,src+0x80,0x10);
    if(tj_route_drop) memset(dst+0x10,0,8);
    memcpy(&sub,src+JOIN_ROUTE_SUBPOINT,8);
    if(sub) {
        assert(tj_route_nsubs<8);unsigned char *u=tj_route_subs[tj_route_nsubs++];memset(u,0,JOIN_ROUTE_SUBSIZE);
        *(void **)u=image+JOIN_ROUTE_NAV_VT;memcpy(u+0x10,(const void *)(sub+0x10),0x50);sub=(uintptr_t)u;
    }
    memcpy(dst+JOIN_ROUTE_SUBPOINT,&sub,8);v[1]+=JOIN_AGENT_WAYPOINT;
}
static void test_join_world(void) {
    join_find_fn find=join_world_find;int (*ids)(join_tables *)=join_world_ids;unsigned classes=world_class_count;
    join_route_clear_fn route_clear=join_route_clear;join_route_push_fn route_push=join_route_push;
    join_route_clear=tj_route_clear;join_route_push=tj_route_push;tj_route_reset();
    uint64_t (*key)(uint64_t,unsigned *)=join_key;
    /* Version 4 needs a pathing manager on both sides (here one without batches or new Agents; test_join_path_region has one with). */
    unsigned char *saved_manager;memcpy(&saved_manager,image+JOIN_PATH_MANAGER,8);int (*prime)(unsigned char *,char *,size_t)=join_path_prime;
    static __attribute__((aligned(16))) unsigned char tw_manager[0x240];static tp_node tw_pending;memset(tw_manager,0,sizeof(tw_manager));
    tp_ring(&tw_pending);*(uintptr_t *)(tw_manager+JOIN_PATH_PENDING+8)=(uintptr_t)&tw_pending;*(unsigned char **)(image+JOIN_PATH_MANAGER)=tw_manager;
    join_path_prime=tp_prime_stub;tp_primes=0;
    tw_actor_id=actor_ids[0];tw_snapshot_configured=snapshot_configured;tw_snapshot_saved=snapshot_actors[0].saved;tw_tables_save(0);
    world_classes[world_class_count++]=(world_class_cache){(uintptr_t)(image+0x4300),3};   /* destructible, positional */
    world_classes[world_class_count++]=(world_class_cache){(uintptr_t)(image+0x4400),2};   /* positional */
    world_classes[world_class_count++]=(world_class_cache){(uintptr_t)(image+0x4500),1};   /* destructible */
    world_classes[world_class_count++]=(world_class_cache){(uintptr_t)(image+0x4600),0x80000000u};
    world_classes[world_class_count++]=(world_class_cache){(uintptr_t)(image+0x4700),7};   /* destructible, positional, Object (a ship) */
    *(void **)(image+0x2a35000+0x30)=image+JOIN_POSE_GETTER_10;*(void **)(image+0x2a35100+0x30)=image+JOIN_POSE_GETTER_20;
    *(void **)(image+0x2a35200+0x30)=image+0x123450;
    join_world_find=tw_find;join_world_ids=tw_ids;join_key=tw_key;
    tw_build(&tw_session,0);tw_build(&tw_joiner,1);
    /* The session's first-call tables at T. */
    tw_find_world=&tw_session;tw_tables_reset(1);
    assert(attention_all_first_call(0x2001)==1 && nav_first_call(nav_params_table,&nav_params_count,0x2001)==1);
    actor_ids[0]=tw_session.runtime[TW_SHIP];attention_initialized[0]=1;
    /* The saver: every present object of the identity table, the full shield in the census's form at T = 30 (the ship's recharges until 48 s). */
    join_tables t;memset(&t,0,sizeof(t));tw_find_world=&tw_session;join_world_map=&tw_session;tw_phase(0);assert(tw_ids(&t));
    assert(join_capture_world(&t,30.0));
    assert(t.nhealth==5 && t.npose==2 && t.nshield==4 && t.nnav==1 && t.nattn==7 && t.nkeys==3 && t.npath==2 && !t.nregion);
    assert(t.health[0].portable==0x2001 && t.health[0].flags==JOIN_HEALTH_SHIELD && t.health[0].count==1 && t.health[0].hull==tj_bits(3100.0) &&
           t.health[0].changed==tj_bits(26.416333333333) && t.health[0].maximum==tj_bits(1955.0));
    assert(t.health[1].portable==0x2002 && t.health[1].changed==t.health[0].changed && t.health[1].hull==tj_bits(500.0));
    assert(t.health[2].portable==0x2003 && t.health[2].value==0 && t.health[2].changed==tj_bits(7.083) && t.health[2].count==0);
    assert(t.health[3].portable==0x2004 && t.health[3].flags==0 && t.health[3].hull==tj_bits(1000.0));
    assert(t.health[4].portable==0x2008 && t.health[4].flags==(JOIN_HEALTH_SHIELD|JOIN_HEALTH_FULL) && t.health[4].value==tj_bits(200.0) &&
           t.health[4].changed==0);
    assert(t.pose[0].portable==0x2004 && t.pose[0].kind==0x2a35000 && t.pose[0].form==1 && t.pose[1].portable==0x2005 && t.pose[1].form==2);
    {float lane;memcpy(&lane,&t.pose[1].xyz[3],4);assert(lane==0.0174533f+0.25f*(float)8);}   /* vector 1 x: +0x30, the ninth float from +0x10 */
    /* SHIELD: the ship's and its weapon's group (rate 1.0), the cargo bay's, the full one's; NAV: the ship, by role. */
    assert(t.shield[0].portable==0x2001 && t.shield[0].delay==tj_bits(15.0) && t.shield[0].rate==tj_f32(1.0f) && t.shield[0].factor==tj_f32(120.0f) &&
           t.shield[0].mods[0]==0x3f800000u && !t.shield[0].flags && t.shield[1].portable==0x2002 && t.shield[3].portable==0x2008);
    {
        const join_nav_rec *n=&t.nav[0];float f;
        assert(n->portable==0x2001 && n->relation==0 && n->refresh==tj_bits(26.98) && n->params_due==tj_bits(9.075415789763133) && n->params_on==1 &&
               n->probe_due==tj_bits(27.5) && n->probe_on==1 && !n->flags);
        memcpy(&f,&n->bank[0].xyz[0],4);assert(f==100.0f);memcpy(&f,&n->bank[1].xyz[0],4);assert(f==200.0f);   /* role 0 = bank 1 (read) */
        assert(n->bank[0].ref[0]==0x2003 && !n->bank[1].ref[0] && n->bank[0].pending_count==1 && n->bank[0].pending_state==WORLD_NAV_NULL &&
               n->bank[1].pending_count==2 && n->bank[1].pending_state==WORLD_NAV_SAVED && n->bank[1].pending_ref==0x2003 && n->bank[0].mode==1 &&
               n->bank[1].layer==2);
    }
    {   /* J1 j1-13 (saver_disagree): an off probe's due is per process, so it is not carried; an armed one is. */
        unsigned char *probe_on=tw_session.agent+0x6e0+JOIN_NAV_PROBE_ON;join_nav_rec off;
        *probe_on=0;
        assert(join_nav_capture(tw_session.object[TW_SHIP],tw_session.nav,tw_session.runtime[TW_SHIP],0x2001,&off,NULL) && !off.probe_on && !off.probe_due);
        *probe_on=1;
    }
    assert(t.attn[0].portable==0x2001 && t.attn[0].level==5 && t.attn[0].flags==JOIN_ATTN_DUE && t.attn[0].due==tj_bits(28.1) &&
           t.attn[2].portable==0x2003 && t.attn[2].level==3 && !t.attn[2].flags);
    assert(t.keys[0].table==JOIN_KEYS_ATTENTION_ALL && t.keys[0].key==0x2001 && t.keys[1].table==JOIN_KEYS_NAV_PARAMS &&
           t.keys[2].table==JOIN_KEYS_ATTENTION_ACTOR && t.keys[2].actor==1 && t.keys[2].key==0x2001);
    /* Through the package: the records survive byte for byte and enter the portable digest. */
    t.version=JOIN_PACKAGE_VERSION;t.gate.tick=633;memset(t.gate.digest,'d',64);
    join_buf b={0};assert(join_package_build(&t,&b));
    join_tables r;const char *why;assert(join_package_parse(b.data,b.size,&r,&why) && r.nhealth==5 && r.npose==2 &&
                                         !memcmp(r.health,t.health,5*sizeof(*r.health)) && !memcmp(r.pose,t.pose,2*sizeof(*r.pose)) &&
                                         r.nnav==1 && !memcmp(r.nav,t.nav,sizeof(*r.nav)) && r.nattn==7 && r.nkeys==3 && r.nshield==4);
    unsigned char portable[16];memcpy(portable,r.gate.portable,16);
    r.health[2].changed^=1;assert(join_portable(&r,r.gate.portable) && memcmp(portable,r.gate.portable,16));r.health[2].changed^=1;
    r.pose[0].xyz[0]^=1;assert(join_portable(&r,r.gate.portable) && memcmp(portable,r.gate.portable,16));r.pose[0].xyz[0]^=1;
    r.nav[0].bank[1].mode^=1;assert(join_portable(&r,r.gate.portable) && memcmp(portable,r.gate.portable,16));r.nav[0].bank[1].mode^=1;
    r.attn[1].level^=1;assert(join_portable(&r,r.gate.portable) && memcmp(portable,r.gate.portable,16));r.attn[1].level^=1;
    memcpy(r.gate.portable,portable,16);free(b.data);
    /* A group that is no ShieldRechargeSource's: the capture refuses naming the object; a navigation controller whose Agent names
     * another owner: the capture refuses naming it and the census check. */
    {join_tables bad;memset(&bad,0,sizeof(bad));assert(tw_ids(&bad));*(void **)(tw_session.group[1]+JOIN_SHIELD_SOURCE)=image+0x1234;
     assert(!join_capture_world(&bad,30.0) && !strcmp(join_fault,"shield,id=2003"));
     *(void **)(tw_session.group[1]+JOIN_SHIELD_SOURCE)=image+JOIN_SHIELD_SOURCE_VT;join_tables_free(&bad);
     memset(&bad,0,sizeof(bad));assert(tw_ids(&bad));*(void **)(tw_session.agent+0xef0)=tw_session.object[TW_CARGO];
     assert(!join_capture_world(&bad,30.0) && !strcmp(join_fault,"nav,id=2001,why=nav_owner_invalid"));
     *(void **)(tw_session.agent+0xef0)=tw_session.object[TW_SHIP];join_tables_free(&bad);}
    unsigned char session_timing[WORLD_NAV_TIMING_BYTES],session_motion[WORLD_NAV_MOTION_BYTES],timing[WORLD_NAV_TIMING_BYTES],motion[WORLD_NAV_MOTION_BYTES];
    tw_census_nav(&tw_session,0,session_timing,session_motion);
    tw_census_nav(&tw_joiner,1,timing,motion);assert(memcmp(timing,session_timing,sizeof(timing)) && memcmp(motion,session_motion,sizeof(motion)));
    /* The joiner (strict): everything placed, what differs written, then every object reads as the session's. Its own tables empty,
     * armed; the fixture configured with the ship as actor 1. */
    tw_tables_reset(1);snapshot_configured=1;snapshot_actors[0].saved=0x2001;
    tw_admit(&tw_joiner,&r,JOIN_STRICT);assert(join_admit_world());
    assert(join_rx.health_applied==5 && join_rx.health_hull_fixed==0 && join_rx.health_shield_fixed==3 && !join_rx.health_missing &&
           !join_rx.health_shape && !join_rx.health_conflict && join_rx.pose_applied==2 && join_rx.pose_fixed==2 && !join_rx.pose_missing &&
           !join_rx.pose_shape);
    assert(join_rx.shield_applied==4 && join_rx.shield_fixed==1 && !join_rx.shield_mods && !join_rx.shield_conflict && !join_rx.shield_shape);
    assert(join_rx.nav_applied==1 && join_rx.nav_fixed==1 && !join_rx.nav_missing && !join_rx.nav_shape && !join_rx.nav_unmapped);
    assert(join_rx.nav_field[JOIN_NAVF_CURSOR]==1 && join_rx.nav_field[JOIN_NAVF_REFRESH]==0 && join_rx.nav_field[JOIN_NAVF_PARAMS]==1 &&
           join_rx.nav_field[JOIN_NAVF_PROBE]==1 && join_rx.nav_field[JOIN_NAVF_VECTORS]==1 && join_rx.nav_field[JOIN_NAVF_REFS]==1 &&
           join_rx.nav_field[JOIN_NAVF_MODE]==1 && join_rx.nav_field[JOIN_NAVF_ETA]==0 && join_rx.nav_field[JOIN_NAVF_LAYER]==0 &&
           join_rx.nav_field[JOIN_NAVF_PENDING]==1);
    assert(join_rx.attn_applied==7 && join_rx.attn_level_fixed==2 && join_rx.attn_due_fixed==1 && !join_rx.attn_missing && !join_rx.attn_shape);
    assert(join_rx.keys_applied[JOIN_KEYS_ATTENTION_ALL]==1 && join_rx.keys_applied[JOIN_KEYS_NAV_PARAMS]==1 &&
           join_rx.keys_applied[JOIN_KEYS_ATTENTION_ACTOR]==1 && !join_rx.keys_known && !join_rx.keys_unarmed && !join_rx.keys_actor &&
           !join_rx.created_extra);
    assert(!join_rx.path_members && !join_rx.path_missing && !join_rx.path_shape && join_rx.path_primed==1 && tp_primes==1 && !join_rx.region_applied);
    assert(attention_all_first_call(0x2001)==0 && nav_first_call(nav_params_table,&nav_params_count,0x2001)==0 && attention_initialized[0] &&
           !nav_initialized[0] && nav_probe_count==0);
    assert(join_rx.nrows==4 && strstr(join_rx.rows[0],"shield side=joiner id=0000000000002001 why=fixed") &&
           strstr(join_rx.rows[1],"nav side=joiner id=0000000000002001 fixed=cursor,params,probe,vectors,refs,mode,pending relation=0") &&
           strstr(join_rx.rows[2],"attn side=joiner id=0000000000002001 level=2/5") && strstr(join_rx.rows[3],"attn side=joiner id=0000000000002003 level=2/3"));
    {char text[256];join_nav_fields_text(text,sizeof(text));assert(!strcmp(text,"1:0:1:1:1:1:1:0:0:1"));}
    for(unsigned i=0;i<TW_OBJECTS;i++) {
        char a[192],c[192];assert(shield_state(tw_session.object[i],a) && shield_state(tw_joiner.object[i],c));
        if(i!=TW_FULL) assert(!strcmp(a,c));   /* the full one: the session's raw state, the joiner's its census form */
        assert(!memcmp(tw_session.object[i]+JOIN_HULL,tw_joiner.object[i]+JOIN_HULL,8));
        assert(!memcmp(tw_session.object[i]+JOIN_ATTENTION,tw_joiner.object[i]+JOIN_ATTENTION,4));
    }
    assert(!memcmp(tw_session.object[TW_SHIP]+JOIN_ATTENTION_DUE,tw_joiner.object[TW_SHIP]+JOIN_ATTENTION_DUE,8));
    /* The census reads the joiner's ship as the session's: the cursor's relation, both banks by role, the references and pending
     * entries by portable ID, the dues; the joiner's selectors untouched, its cursor now its read bank, the w lanes its own. */
    tw_census_nav(&tw_joiner,1,timing,motion);
    assert(!memcmp(timing,session_timing,sizeof(timing)) && !memcmp(motion,session_motion,sizeof(motion)));
    {uint32_t cursor;float w;memcpy(&cursor,tw_joiner.nav+JOIN_NAV_CURSOR,4);memcpy(&w,tw_joiner.agent+0xad0+12,4);
     assert(cursor==0 && w==-1.0f && *(uint32_t *)(image+0x38f4ee4)==0 && *(uint64_t *)(tw_joiner.agent+0xed0+16)==tw_joiner.runtime[TW_CARGO] &&
            *(uint64_t *)(tw_joiner.agent+0xad0+0xb0)==tw_joiner.runtime[TW_CARGO]);}
    {double v,c;memcpy(&v,tw_joiner.group[2]+JOIN_SHIELD_VALUE,8);memcpy(&c,tw_joiner.group[2]+JOIN_SHIELD_CHANGED,8);assert(v==200.0 && c==0.0);}
    for(unsigned k=1;k<3;k++) {
        unsigned off=k==1?0x10:0x20;
        for(unsigned v=0;v<4;v++) assert(!memcmp(tw_session.controller[k]+off+16*v,tw_joiner.controller[k]+off+16*v,12));
        float w,want=(k==1?-0.9999999f:0.0174531f)+0.25f*(float)((off-0x10)/4+3);memcpy(&w,tw_joiner.controller[k]+off+12,4);assert(w==want);   /* w lanes untouched */
    }
    /* Again: nothing differs, nothing written; the keys are known now. */
    assert(join_admit_world() && join_rx.health_shield_fixed==3 && join_rx.pose_fixed==2 && join_rx.nav_fixed==1 && join_rx.attn_level_fixed==2 &&
           join_rx.shield_fixed==1 && join_rx.keys_known==3);
    /* Report mode: a missing object, another group shape, another controller getter; the ship's controller without its Agent's
     * owner (nav shape), the cargo bay's level and the actor key on an unconfigured fixture, a table not armed. */
    tw_build(&tw_joiner,1);*(uint64_t *)(tw_joiner.object[TW_CARGO]+8)=0x9999;   /* gone: the lookup finds another object */
    tw_group(tw_joiner.group[2],100.0,1.0,250.0,1.0,10.0f,1.0f,1);
    tw_controller(tw_joiner.controller[2],0x2a35000,0.0174531f);
    *(void **)(tw_joiner.agent+0xef0)=NULL;tw_tables_reset(0);snapshot_configured=0;
    tw_admit(&tw_joiner,&r,JOIN_REPORT);assert(join_admit_world());
    assert(join_rx.health_missing==1 && join_rx.health_shape==1 && join_rx.pose_shape==1 && join_rx.health_applied==3 && join_rx.pose_applied==1 &&
           !join_rx.health_conflict);
    assert(join_rx.shield_missing==1 && join_rx.nav_shape==1 && !join_rx.nav_applied && join_rx.attn_missing==1 && join_rx.attn_applied==6 &&
           join_rx.keys_unarmed==2 && join_rx.keys_actor==1);
    assert(join_rx.nrows>=3 && strstr(join_rx.rows[0],"health side=session id=0000000000002003 why=missing runtime=5003") &&
           strstr(join_rx.rows[1],"health side=joiner id=0000000000002008 why=shape group=1/1 max=406f400000000000/4069000000000000") &&
           strstr(join_rx.rows[2],"pose side=joiner id=0000000000002005 why=shape classes=2 kind=2a35000/2a35100 form=1/2"));
    {int nav_row=0,keys_row=0;for(unsigned i=0;i<join_rx.nrows;i++) {
        nav_row|=strstr(join_rx.rows[i],"nav side=joiner id=0000000000002001 why=shape check=nav_owner_invalid")!=NULL;
        keys_row|=strstr(join_rx.rows[i],"keys side=session table=4 actor=1 key=0000000000002001 why=actor")!=NULL;
     }
     assert(nav_row && keys_row);}
    tw_admit(&tw_joiner,&r,JOIN_STRICT);assert(!join_admit_world() && !strcmp(join_rx.detail,"world:health_missing"));
    tw_build(&tw_joiner,1);tw_controller(tw_joiner.controller[2],0x2a35000,0.0174531f);
    tw_admit(&tw_joiner,&r,JOIN_STRICT);assert(!join_admit_world() && !strcmp(join_rx.detail,"world:pose_shape"));
    /* Strict, v3 only: a navigation shape, a table not armed, a created identity of the joiner's own. */
    tw_build(&tw_joiner,1);*(void **)(tw_joiner.agent+0xef0)=NULL;tw_tables_reset(1);snapshot_configured=1;
    tw_admit(&tw_joiner,&r,JOIN_STRICT);assert(!join_admit_world() && !strcmp(join_rx.detail,"world:nav_shape"));
    tw_build(&tw_joiner,1);tw_tables_reset(0);
    tw_admit(&tw_joiner,&r,JOIN_STRICT);assert(!join_admit_world() && !strcmp(join_rx.detail,"world:keys_unarmed"));
    tw_build(&tw_joiner,1);tw_joiner.created=1;tw_tables_reset(1);
    tw_admit(&tw_joiner,&r,JOIN_REPORT);join_unmapped_note(TW_CREATED_RUNTIME);assert(join_admit_world());
    assert(join_rx.created_extra==1 && join_rx.created_extra_loaded==1 && join_unmapped_has(TW_CREATED_RUNTIME) && !join_unmapped_has(0x6002));
    {int row=0;for(unsigned i=0;i<join_rx.nrows;i++) row|=strstr(join_rx.rows[i],"created side=joiner id=8000000000000077 runtime=6001 vtable=0 loaded=1")!=NULL;assert(row);}
    tw_build(&tw_joiner,1);tw_joiner.created=1;tw_tables_reset(1);
    tw_admit(&tw_joiner,&r,JOIN_STRICT);assert(!join_admit_world() && !strcmp(join_rx.detail,"world:created_extra") && !join_rx.created_extra_loaded);
    tw_joiner.created=0;
    /* The session's weapon reads a group of its own (the same shape, another changed time); the joiner's reads its ship's: the
     * second write wins and the ship's check after all writes names the conflict. */
    {join_tables two;memset(&two,0,sizeof(two));tw_build(&tw_session,0);
     tw_group(tw_session.group[3],1150.0,20.0,1955.0,15.0,1.0f,120.0f,1);*(unsigned char **)(tw_session.object[TW_WEAPON]+JOIN_SHIELD_GROUP)=tw_session.group[3];
     tw_find_world=&tw_session;join_world_map=&tw_session;tw_phase(0);assert(tw_ids(&two) && join_capture_world(&two,30.0));two.version=JOIN_PACKAGE_VERSION;
     tw_build(&tw_joiner,1);tw_tables_reset(1);tw_admit(&tw_joiner,&two,JOIN_REPORT);assert(join_admit_world() && join_rx.health_conflict==1 &&
         strstr(join_rx.rows[0],"health side=joiner id=0000000000002001 why=conflict") && !join_rx.shield_conflict);
     tw_admit(&tw_joiner,&two,JOIN_STRICT);assert(!join_admit_world() && !strcmp(join_rx.detail,"world:health_conflict"));
     memset(&join_rx,0,sizeof(join_rx));join_tables_free(&two);}
    /* Version 5 (j1-15, report 1.9): the ship's linear flight control model. The session's: a leg under way (its last step at
     * 28.05, five frames before T+1; started at 27.8), a spline of two Bezier segments and three parameter pairs, the spline flag
     * set. The joiner's as its load left it: new (last step -1, the new-leg flag 1, an empty spline). The admission writes the
     * spline into buffers of the engine's allocator, then the state; the joiner's model then reads as the session's. */
    {
        static __attribute__((aligned(16))) unsigned char model[2][0x3a0],segs[2*JOIN_FLIGHT_SEGMENT];static uint64_t pairs[4];
        void *(*alloc)(size_t)=join_flight_alloc;join_flight_alloc=tj_flight_alloc;
#define TJ_FLIGHT_WORLDS(session_kind,joiner_kind) do { \
        tw_build(&tw_session,0);tw_build(&tw_joiner,1); \
        for(unsigned side=0;side<2;side++) { \
            tw_world *w=side?&tw_joiner:&tw_session;unsigned char *m=model[side];memset(m,0,sizeof(model[side])); \
            *(void **)m=image+(side?(joiner_kind):(session_kind));*(void **)(m+JOIN_FLIGHT_OWNER)=w->agent; \
            *(void **)(m+JOIN_FLIGHT_SPLINE)=image+JOIN_FLIGHT_SPLINE_VT;*(unsigned char **)(w->agent+JOIN_AGENT_MODEL)=m; \
            double last=side?-1.0:28.05,start=side?-1.0:27.8;float progress=side?0.0f:0.375f; \
            memcpy(m+0x88,&last,8);memcpy(m+0x80,&start,8);memcpy(m+0x90,&progress,4);m[0x94]=side?1:0;m[0x130]=side?0:1; \
            {float accel=251.0f,limit=side?2500.0f:2706.0f;memcpy(w->agent+0x340,&accel,4);memcpy(w->agent+0x364,&limit,4);} \
            for(unsigned i=0;i<16;i++) {float f=side?0.0f:100.5f+(float)i;memcpy(m+0x20+4*i,&f,4);} \
        } \
        for(unsigned k=0;k<2;k++) {unsigned char *g=segs+k*JOIN_FLIGHT_SEGMENT;memset(g,0x11*(k+1),JOIN_FLIGHT_SEGMENT); \
                                   *(void **)(g+JOIN_FLIGHT_BEZIER_AT)=image+JOIN_FLIGHT_BEZIER_VT;} \
        for(unsigned k=0;k<4;k++) pairs[k]=0x3f80000000000000ULL+k; \
        uintptr_t sv[3]={(uintptr_t)segs,(uintptr_t)segs+2*JOIN_FLIGHT_SEGMENT,(uintptr_t)segs+2*JOIN_FLIGHT_SEGMENT}; \
        uintptr_t pv[3]={(uintptr_t)pairs,(uintptr_t)(pairs+3),(uintptr_t)(pairs+4)}; \
        memcpy(model[0]+JOIN_FLIGHT_SEGMENTS_AT,sv,24);memcpy(model[0]+JOIN_FLIGHT_PAIRS_AT,pv,24); \
        tj_flight_allocs=0;tj_flight_fail=0; \
    } while(0)
#define TJ_FLIGHT_CAPTURE(tables) do {memset(&(tables),0,sizeof(tables));tw_find_world=&tw_session;join_world_map=&tw_session;tw_phase(0); \
        assert(tw_ids(&(tables)) && join_capture_world(&(tables),30.0));(tables).version=JOIN_PACKAGE_VERSION;} while(0)
        join_tables f;TJ_FLIGHT_WORLDS(JOIN_FLIGHT_LINEAR,JOIN_FLIGHT_LINEAR);TJ_FLIGHT_CAPTURE(f);
        assert(f.nflight==1 && f.nspline==3 && f.flight[0].portable==0x2001 && f.flight[0].kind==JOIN_FLIGHT_LINEAR && !f.flight[0].flags &&
               f.flight[0].segments==2 && f.flight[0].pairs==3 && join_flight_shape(f.flight,1,f.spline,3));
        {double last;memcpy(&last,f.flight[0].state+0x40+(0x88-0x68),8);assert(last==28.05);}   /* the step clock rides the record */
        assert(!f.spline[0].part && f.spline[1].index==1 && f.spline[2].part==1 && !f.spline[2].index && !f.spline[0].data[JOIN_FLIGHT_BEZIER_AT] &&
               f.spline[0].data[0]==0x11 && f.spline[1].data[0]==0x22 && !memcmp(f.spline[2].data,pairs,24));
        {join_buf b={0};join_tables r5;const char *why;assert(join_package_build(&f,&b) && join_package_parse(b.data,b.size,&r5,&why) &&
         r5.nflight==1 && r5.nspline==3 && !memcmp(r5.flight,f.flight,sizeof(*f.flight)) && !memcmp(r5.spline,f.spline,3*sizeof(*f.spline)));
         join_tables_free(&r5);free(b.data);}
        /* Version 6: the ship's Agent rides along (the session's speed limit 2706, the joiner's 2500, both accelerating at 251). */
        assert(f.nagent==1 && f.agent[0].portable==0x2001 && f.agent[0].value[0]==tj_f32(251.0f) && f.agent[0].value[7]==tj_f32(2706.0f) &&
               join_agent_shape(f.flight,1,f.agent,1));
        tw_tables_reset(1);tw_admit(&tw_joiner,&f,JOIN_STRICT);assert(join_admit_world());
        assert(join_rx.flight_applied==1 && join_rx.flight_fixed==1 && join_rx.flight_spline==1 && tj_flight_allocs==2 && !join_rx.flight_missing &&
               !join_rx.flight_kind && !join_rx.flight_other && !join_rx.flight_shape && !join_rx.flight_alloc && !join_rx.flight_word);
        {float limit;memcpy(&limit,tw_joiner.agent+0x364,4);
         assert(limit==2706.0f && join_rx.agent_applied==1 && join_rx.agent_fixed==1 && !join_rx.agent_state && !join_rx.agent_missing &&
                !join_rx.agent_shape && join_rx.agent_field[7]==1 && !join_rx.agent_field[0]);
         char fields[512];join_agent_fields_text(fields,sizeof(fields));assert(!strcmp(fields,"a364:1"));
         int row=0;for(unsigned i=0;i<join_rx.nrows;i++) row|=strstr(join_rx.rows[i],"agent side=joiner id=0000000000002001 why=values fields=a364 first=a364:45292000/451c4000")!=NULL;
         assert(row);}
        {unsigned char a[JOIN_FLIGHT_STATE],c[JOIN_FLIGHT_STATE];uintptr_t v[3],u[3];
         assert(join_flight_state_read(model[0],a) && join_flight_state_read(model[1],c) && !memcmp(a,c,sizeof(a)) && model[1][0x94]==0);
         /* J1 j1-18: the padding after the leg length and after the spline flags is per process: carried as 0. */
         {unsigned char k0[5]={model[0][0xac],model[0][0xad],model[0][0xae],model[0][0xaf],model[0][0x133]};
          memset(model[0]+0xac,0xff,4);model[0][0x133]=0xe8;assert(join_flight_state_read(model[0],a) && !memcmp(a,c,sizeof(a)));
          memcpy(model[0]+0xac,k0,4);model[0][0x133]=k0[4];}
         memcpy(v,model[1]+JOIN_FLIGHT_SEGMENTS_AT,24);assert(v[0]==(uintptr_t)tj_flight_pool[0] && v[1]==v[0]+2*JOIN_FLIGHT_SEGMENT && v[2]==v[1] &&
                                                              !memcmp((void *)v[0],segs,2*JOIN_FLIGHT_SEGMENT));
         memcpy(u,model[1]+JOIN_FLIGHT_PAIRS_AT,24);assert(u[0]==(uintptr_t)tj_flight_pool[1] && u[1]==u[0]+24 && u[2]==u[1] && !memcmp((void *)u[0],pairs,24));}
        /* Again: the joiner's own buffers have the room, nothing allocated, nothing differs. */
        tw_admit(&tw_joiner,&f,JOIN_REPORT);assert(join_admit_world() && join_rx.flight_applied==1 && !join_rx.flight_fixed && !join_rx.flight_spline &&
                                                    tj_flight_allocs==2 && join_rx.agent_applied==1 && !join_rx.agent_fixed);
        /* The step of T+1 (JOIN_STATUS t1=): the admission listed the model it wrote; the joiner's step then restarted the leg after
         * a SetState (+0x6d) with another target, the probe counts each; a later tick probes nothing and disarms. */
        {
            const double now=28.216333333333;char status[3584];
            join_t1_arm(633);assert(join_t1.armed && join_t1.listed==1);
            memcpy(model[1]+0x88,&now,8);memcpy(model[1]+0x80,&now,8);model[1][0x6d]=1;model[1][0x20]^=1;
            join_t1_probe(633,now);assert(join_t1.armed && !join_t1.probed);   /* T itself: not yet */
            join_t1_probe(634,now);
            assert(!join_t1.armed && join_t1.probed==1 && join_t1.stepped==1 && join_t1.newleg==1 && join_t1.pending==1 && join_t1.retarget==1 &&
                   !join_t1.missing);
            assert(join_status(status,sizeof(status)) && strstr(status," t1=1:1:1:1:1:1:0") && strstr(status," flight=1:0:0:0:0:0:0:0:0 agent=1:0:0:0:0 agent_fields=-"));
            join_t1_probe(634,now);assert(join_t1.probed==1);   /* once */
            model[1][0x20]^=1;model[1][0x6d]=0;
            /* A saver: its capture listed the session's model; a full update along the leg (stepped only). */
            join_tables g;TJ_FLIGHT_CAPTURE(g);join_t1_arm(633);assert(join_t1.listed==1);
            memcpy(model[0]+0x88,&now,8);join_t1_probe(634,now);
            assert(join_t1.probed==1 && join_t1.stepped==1 && !join_t1.newleg && !join_t1.pending && !join_t1.retarget && !join_t1.missing);
            /* Armed, then the tick passed: nothing probed. And an object gone: missing. */
            join_tables_free(&g);TJ_FLIGHT_CAPTURE(g);join_t1_arm(633);join_t1_probe(636,now);assert(!join_t1.armed && !join_t1.probed);
            join_tables_free(&g);TJ_FLIGHT_CAPTURE(g);join_t1_arm(633);*(unsigned char **)(tw_session.agent+JOIN_AGENT_MODEL)=NULL;
            join_t1_probe(634,now);assert(join_t1.missing==1 && !join_t1.probed);
            *(unsigned char **)(tw_session.agent+JOIN_AGENT_MODEL)=model[0];join_tables_free(&g);join_t1_reset();
            tw_find_world=&tw_joiner;join_world_map=&tw_joiner;tw_phase(1);
        }
        /* Report, then strict: another kind on the joiner; a smaller buffer of the joiner's own; the allocator failing. */
        TJ_FLIGHT_WORLDS(JOIN_FLIGHT_LINEAR,0x2c4e9c8);tw_tables_reset(1);tw_admit(&tw_joiner,&f,JOIN_REPORT);
        assert(join_admit_world() && join_rx.flight_kind==1 && !join_rx.flight_applied);
        {int row=0;for(unsigned i=0;i<join_rx.nrows;i++) row|=strstr(join_rx.rows[i],"flight side=joiner id=0000000000002001 why=kind kind=2c4f610/2c4e9c8")!=NULL;assert(row);}
        tw_admit(&tw_joiner,&f,JOIN_STRICT);assert(!join_admit_world() && !strcmp(join_rx.detail,"world:flight_kind"));
        TJ_FLIGHT_WORLDS(JOIN_FLIGHT_LINEAR,JOIN_FLIGHT_LINEAR);
        {static unsigned char own[JOIN_FLIGHT_SEGMENT];uintptr_t sv[3]={(uintptr_t)own,(uintptr_t)own,(uintptr_t)own+JOIN_FLIGHT_SEGMENT};
         memcpy(model[1]+JOIN_FLIGHT_SEGMENTS_AT,sv,24);}
        tw_tables_reset(1);tw_admit(&tw_joiner,&f,JOIN_STRICT);assert(!join_admit_world() && join_rx.flight_shape==1 && !strcmp(join_rx.detail,"world:flight_shape") &&
                                                                     !tj_flight_allocs);
        TJ_FLIGHT_WORLDS(JOIN_FLIGHT_LINEAR,JOIN_FLIGHT_LINEAR);tj_flight_fail=1;tw_tables_reset(1);
        tw_admit(&tw_joiner,&f,JOIN_STRICT);assert(!join_admit_world() && join_rx.flight_alloc==1 && !strcmp(join_rx.detail,"world:flight_alloc"));
        {double last;memcpy(&last,model[1]+0x88,8);assert(last==-1.0);}   /* nothing of the record written */
        join_tables_free(&f);join_t1_reset();
        /* The session's other model kinds and what a record cannot carry: a highway model (kind only; a steering model is STEER's
         * from version 9, a spline model SPLM's from version 12, below), a segment that is no BezierSpline (no spline records), a word
         * at +0x60 (the rest written, the word the joiner's own). */
        TJ_FLIGHT_WORLDS(0x2c4f6f0,0x2c4f6f0);TJ_FLIGHT_CAPTURE(f);
        assert(f.nflight==1 && f.flight[0].flags==JOIN_FLIGHT_OTHER && f.flight[0].kind==0x2c4f6f0 && !f.nspline && !f.nsteer && !f.nsplm);
        tw_tables_reset(1);tw_admit(&tw_joiner,&f,JOIN_STRICT);assert(!join_admit_world() && join_rx.flight_other==1 && !strcmp(join_rx.detail,"world:flight_other"));
        join_tables_free(&f);
        TJ_FLIGHT_WORLDS(JOIN_FLIGHT_LINEAR,JOIN_FLIGHT_LINEAR);*(void **)(segs+JOIN_FLIGHT_SEGMENT+JOIN_FLIGHT_BEZIER_AT)=image+0x2a84ca8;TJ_FLIGHT_CAPTURE(f);
        assert(f.nflight==1 && f.flight[0].flags==JOIN_FLIGHT_UNCARRIED && !f.flight[0].segments && !f.flight[0].pairs && !f.nspline);
        tw_tables_reset(1);tw_admit(&tw_joiner,&f,JOIN_REPORT);assert(join_admit_world() && join_rx.flight_other==1 && !join_rx.flight_applied);
        join_tables_free(&f);
        TJ_FLIGHT_WORLDS(JOIN_FLIGHT_LINEAR,JOIN_FLIGHT_LINEAR);*(uint64_t *)(model[0]+JOIN_FLIGHT_WORD_AT)=0x1234;*(uint64_t *)(model[1]+JOIN_FLIGHT_WORD_AT)=7;
        TJ_FLIGHT_CAPTURE(f);assert(f.flight[0].flags==JOIN_FLIGHT_WORD && f.nspline==3);
        /* J1 j1-20: a version 6 package counts it as word, not other; from version 7 ROUTE carries the word as a portable ID, and
         * 0x1234 names no object of the session's: route unmapped, the word stays the joiner's (a mapped word: the ROUTE test below). */
        f.version=6;tw_tables_reset(1);tw_admit(&tw_joiner,&f,JOIN_REPORT);
        assert(join_admit_world() && join_rx.flight_applied==1 && join_rx.flight_word==1 && !join_rx.flight_other && !join_rx.route_applied &&
               *(uint64_t *)(model[1]+JOIN_FLIGHT_WORD_AT)==7 && model[1][0x130]==1);
        tw_admit(&tw_joiner,&f,JOIN_STRICT);assert(!join_admit_world() && !strcmp(join_rx.detail,"world:flight_word"));
        f.version=JOIN_PACKAGE_VERSION;assert(f.nroute==1 && f.route[0].flags==(JOIN_ROUTE_LINEAR|1u) && !f.route[0].ref[0]);
        tw_admit(&tw_joiner,&f,JOIN_REPORT);
        assert(join_admit_world() && join_rx.flight_applied==1 && !join_rx.flight_word && join_rx.route_unmapped==1 &&
               *(uint64_t *)(model[1]+JOIN_FLIGHT_WORD_AT)==7);
        tw_admit(&tw_joiner,&f,JOIN_STRICT);assert(!join_admit_world() && !strcmp(join_rx.detail,"world:route_unmapped"));
        join_tables_free(&f);
        /* Version 6, what AGENT only counts: the session's Agent leads a group of two and has no re-target pending, the joiner's
         * leads none, has the re-target flag set and a waypoint not applied yet (no FlightPoint: ROUTE's read-back of it fails,
         * the rebuild clears it). Report counts them per field, strict refuses (members); from version 7 the flags are written
         * and the vectors are ROUTE's. */
        TJ_FLIGHT_WORLDS(JOIN_FLIGHT_LINEAR,JOIN_FLIGHT_LINEAR);
        {
            static uintptr_t members[2];static __attribute__((aligned(16))) unsigned char group[0x60],waypoint[JOIN_AGENT_WAYPOINT];
            uintptr_t mv[2]={(uintptr_t)members,(uintptr_t)(members+2)};
            uintptr_t wv[3]={(uintptr_t)waypoint,(uintptr_t)waypoint+JOIN_AGENT_WAYPOINT,(uintptr_t)waypoint+JOIN_AGENT_WAYPOINT};
            memset(group,0,sizeof(group));memcpy(group+0x48,mv,16);*(unsigned char **)(tw_session.agent+JOIN_AGENT_GROUP)=group;
            tw_joiner.agent[0xf33]=1;memcpy(tw_joiner.agent+JOIN_AGENT_PENDING,wv,24);
            TJ_FLIGHT_CAPTURE(f);
            assert(f.nagent==1 && f.agent[0].members==2 && !f.agent[0].flag[4] && !f.agent[0].pending && !f.agent[0].formation);
            tw_tables_reset(1);tw_admit(&tw_joiner,&f,JOIN_REPORT);tj_route_reset();
            assert(join_admit_world() && join_rx.agent_applied==1 && join_rx.agent_fixed==1 && join_rx.agent_state==1 && tw_joiner.agent[0xf33]==0 &&
                   join_rx.agent_field[22]==1 && join_rx.agent_field[29]==1 && join_rx.agent_field[30]==1 && !join_rx.agent_field[28]);
            assert(join_rx.route_applied==1 && join_rx.route_rebuilt==1 && tj_route_clears==2 && !tj_route_pushes);
            {uintptr_t u[3];memcpy(u,tw_joiner.agent+JOIN_AGENT_PENDING,24);assert(u[1]==u[0]);}
            char fields[512];join_agent_fields_text(fields,sizeof(fields));assert(!strcmp(fields,"a364:1,bf33:1,pending:1,members:1"));
            int row=0;
            for(unsigned i=0;i<join_rx.nrows;i++)
                row|=strstr(join_rx.rows[i],"agent side=joiner id=0000000000002001 why=state fields=a364,bf33,pending,members f33=0/1 waypoints=0/0"
                                            " pending=0/1 members=2/0 formation=0/0")!=NULL;
            assert(row);
            tw_admit(&tw_joiner,&f,JOIN_STRICT);assert(!join_admit_world() && !strcmp(join_rx.detail,"world:agent_state"));
            /* An Agent whose waypoint vector is out of shape: agent_shape (and ROUTE's). */
            wv[1]=wv[0]+1;memcpy(tw_joiner.agent+JOIN_AGENT_PENDING,wv,16);tw_admit(&tw_joiner,&f,JOIN_REPORT);
            assert(join_admit_world() && join_rx.agent_shape==1 && !join_rx.agent_applied && join_rx.route_shape==1 && !join_rx.route_applied);
            tw_admit(&tw_joiner,&f,JOIN_STRICT);assert(!join_admit_world() && !strcmp(join_rx.detail,"world:agent_shape"));
            *(unsigned char **)(tw_session.agent+JOIN_AGENT_GROUP)=NULL;join_tables_free(&f);join_t1_reset();
        }
        /* Version 7 (j1-21): the session's Agent holds one applied waypoint (a FlightPoint on the cargo bay with a sub-point on the
         * turret), none pending, the re-target flag clear, its IDs (+0xef8 the module, +0xf60 the cargo bay) and bytes, the model's
         * word the cargo bay; the joiner's, as its load left it, the waypoint pending with the save's rounded transform and removal
         * time, the flag set, no IDs, the word 0. The admission rebuilds both vectors through the engine's clear and push_back
         * (synthetic here) and writes the IDs, bytes and word: the joiner then holds the session's entry with its own runtime IDs. */
        {
            static __attribute__((aligned(16))) unsigned char wp[2][JOIN_AGENT_WAYPOINT],sp[2][JOIN_ROUTE_SUBSIZE];
#define TJ_ROUTE_WORLDS() do { \
            TJ_FLIGHT_WORLDS(JOIN_FLIGHT_LINEAR,JOIN_FLIGHT_LINEAR);tj_route_reset(); \
            for(unsigned side=0;side<2;side++) { \
                tw_world *w=side?&tw_joiner:&tw_session;unsigned char *e=wp[side],*u=sp[side]; \
                memset(e,side?0x5a:0xa5,JOIN_AGENT_WAYPOINT);memset(u,0,JOIN_ROUTE_SUBSIZE);   /* +0x08 and +0x7f: per process */ \
                *(void **)e=image+JOIN_ROUTE_POINT_VT;memcpy(e+0x10,&w->runtime[TW_CARGO],8);memset(e+0x18,0,8); \
                for(unsigned i=0;i<16;i++) {float x=(side?100.25f:100.0f)+(float)i;memcpy(e+0x20+4*i,&x,4);} \
                {uint32_t key=17,behaviour=2;memcpy(e+0x60,&key,4);memcpy(e+0x64,&behaviour,4);} \
                *(unsigned char **)(e+JOIN_ROUTE_SUBPOINT)=u;memset(e+0x70,0,0xf);e[0x75]=1;e[0x76]=1;e[0x7d]=1; \
                {double removal=side?30.1163:30.116333333333;float m=-1.0f;memcpy(e+0x80,&removal,8);memset(e+0x88,0,4);memcpy(e+0x8c,&m,4);} \
                *(void **)u=image+JOIN_ROUTE_NAV_VT;memcpy(u+0x10,&w->runtime[TW_TURRET],8); \
                for(unsigned i=0;i<16;i++) {float x=7.0f+(float)i;memcpy(u+0x20+4*i,&x,4);} \
                uintptr_t v[3]={(uintptr_t)e,(uintptr_t)e+JOIN_AGENT_WAYPOINT,(uintptr_t)e+JOIN_AGENT_WAYPOINT}; \
                memcpy(w->agent+(side?JOIN_AGENT_PENDING:JOIN_AGENT_WAYPOINTS),v,24); \
                if(side) w->agent[0xf33]=1; \
                else {uint32_t f70=3;memcpy(w->agent+0xef8,&w->runtime[TW_MODULE],8);memcpy(w->agent+0xf60,&w->runtime[TW_CARGO],8); \
                      w->agent[0xf5c]=1;w->agent[0xf7b]=1;memcpy(w->agent+JOIN_ROUTE_F70,&f70,4);memcpy(model[0]+JOIN_FLIGHT_WORD_AT,&w->runtime[TW_CARGO],8);} \
            } \
        } while(0)
            TJ_ROUTE_WORLDS();TJ_FLIGHT_CAPTURE(f);
            assert(f.nagent==1 && f.agent[0].waypoints==1 && !f.agent[0].pending && f.nroute==2 && join_route_shape(f.agent,1,f.route,2));
            {const join_route_rec *h=&f.route[0],*q=&f.route[1];uint16_t n[2];memcpy(n,h->data,4);
             assert(h->kind==JOIN_ROUTE_HEAD && h->flags==JOIN_ROUTE_LINEAR && h->ref[0]==0x2003 && h->ref[1]==0x2005 && !h->ref[2] && !h->ref[3] &&
                    h->ref[4]==0x2003 && n[0]==1 && !n[1] && h->data[4]==1 && h->data[9]==1 && h->data[12]==3);
             assert(q->kind==JOIN_ROUTE_POINT && !q->list && !q->index && q->flags==JOIN_ROUTE_SUB && q->ref[0]==0x2003 && q->ref[1]==0x2004 &&
                    !q->data[0x48+0xf] && !memcmp(q->data,wp[0]+0x20,0x48) && !memcmp(q->data+0x68,sp[0]+0x20,0x40));}
            {join_buf b={0};join_tables r7;const char *why;assert(join_package_build(&f,&b) && join_package_parse(b.data,b.size,&r7,&why) &&
             r7.nroute==2 && !memcmp(r7.route,f.route,2*sizeof(*f.route)));join_tables_free(&r7);free(b.data);}
            tw_tables_reset(1);tw_admit(&tw_joiner,&f,JOIN_STRICT);assert(join_admit_world());
            assert(join_rx.route_applied==1 && join_rx.route_rebuilt==1 && join_rx.route_fixed==1 && !join_rx.route_missing && !join_rx.route_other &&
                   !join_rx.route_unmapped && !join_rx.route_shape && tj_route_clears==2 && tj_route_pushes==1 && tw_joiner.agent[0xf33]==0);
            {uintptr_t v[3],u[3];unsigned char *e,*sub;uint64_t id;memcpy(v,tw_joiner.agent+JOIN_AGENT_WAYPOINTS,24);memcpy(u,tw_joiner.agent+JOIN_AGENT_PENDING,24);
             assert(v[0]==(uintptr_t)tj_route_pool[0] && v[1]==v[0]+JOIN_AGENT_WAYPOINT && u[1]==u[0]);
             e=(unsigned char *)v[0];memcpy(&id,e+0x10,8);memcpy(&sub,e+JOIN_ROUTE_SUBPOINT,8);
             assert(id==tw_joiner.runtime[TW_CARGO] && !memcmp(e+0x20,wp[0]+0x20,0x48) && !memcmp(e+0x70,wp[0]+0x70,0xf) && !memcmp(e+0x80,wp[0]+0x80,0x10) &&
                    e[0x7f]==0xee && sub==tj_route_subs[0]);
             memcpy(&id,sub+0x10,8);assert(id==tw_joiner.runtime[TW_TURRET] && !memcmp(sub+0x20,sp[0]+0x20,0x40));}
            {uint64_t a,c,word;uint32_t f70;memcpy(&a,tw_joiner.agent+0xef8,8);memcpy(&c,tw_joiner.agent+0xf60,8);memcpy(&word,model[1]+JOIN_FLIGHT_WORD_AT,8);
             memcpy(&f70,tw_joiner.agent+JOIN_ROUTE_F70,4);
             assert(a==tw_joiner.runtime[TW_MODULE] && c==tw_joiner.runtime[TW_CARGO] && word==tw_joiner.runtime[TW_CARGO] && tw_joiner.agent[0xf5c]==1 &&
                    tw_joiner.agent[0xf7b]==1 && f70==3 && !join_rx.flight_word);}
            {char status[3584];assert(join_status(status,sizeof(status)) && strstr(status," route=1:1:1:0:0:0:0 param=1:0:0:0:0:0:0:0:0 dock=0:0:0:0:0 steer=0:0:0:0:0:0:0:0:0 t1="));}
            /* Again: the vectors equal, nothing rebuilt or written. */
            tw_admit(&tw_joiner,&f,JOIN_STRICT);
            assert(join_admit_world() && join_rx.route_applied==1 && !join_rx.route_rebuilt && !join_rx.route_fixed && tj_route_clears==2 &&
                   tj_route_pushes==1);
            join_tables_free(&f);join_t1_reset();
            /* The engine's copy finds no object for the reference (it zeroes it): the read-back differs, route shape; strict refuses. */
            TJ_ROUTE_WORLDS();TJ_FLIGHT_CAPTURE(f);tj_route_drop=1;tw_tables_reset(1);tw_admit(&tw_joiner,&f,JOIN_REPORT);
            assert(join_admit_world() && join_rx.route_shape==1 && !join_rx.route_applied && tj_route_pushes==1);
            {int row=0;for(unsigned i=0;i<join_rx.nrows;i++) row|=strstr(join_rx.rows[i],"route side=joiner id=0000000000002001 why=shape check=rebuilt waypoints=1 pending=0")!=NULL;assert(row);}
            TJ_ROUTE_WORLDS();tj_route_drop=1;tw_admit(&tw_joiner,&f,JOIN_STRICT);assert(!join_admit_world() && !strcmp(join_rx.detail,"world:route_shape"));
            join_tables_free(&f);join_t1_reset();
            /* A reference without a portable ID on the session: route unmapped, the joiner's Agent untouched (its pending waypoint stays). */
            TJ_ROUTE_WORLDS();{uint64_t lost=0x7777;memcpy(wp[0]+0x10,&lost,8);}TJ_FLIGHT_CAPTURE(f);
            assert(f.route[1].flags==(JOIN_ROUTE_SUB|1u) && !f.route[1].ref[0] && join_route_shape(f.agent,1,f.route,2));
            tw_tables_reset(1);tw_admit(&tw_joiner,&f,JOIN_REPORT);
            assert(join_admit_world() && join_rx.route_unmapped==1 && !join_rx.route_applied && !tj_route_clears);
            {uintptr_t u[3];memcpy(u,tw_joiner.agent+JOIN_AGENT_PENDING,24);assert(u[1]==u[0]+JOIN_AGENT_WAYPOINT);}
            tw_admit(&tw_joiner,&f,JOIN_STRICT);assert(!join_admit_world() && !strcmp(join_rx.detail,"world:route_unmapped"));
            join_tables_free(&f);join_t1_reset();
            /* A connection pointer (+0x18) on the session's entry, an entry that is no FlightPoint: route other. */
            TJ_ROUTE_WORLDS();{uint64_t connection=0x1234;memcpy(wp[0]+0x18,&connection,8);}TJ_FLIGHT_CAPTURE(f);
            assert(f.route[1].flags==(JOIN_ROUTE_SUB|JOIN_ROUTE_CONN));
            tw_tables_reset(1);tw_admit(&tw_joiner,&f,JOIN_REPORT);assert(join_admit_world() && join_rx.route_other==1 && !tj_route_clears);
            tw_admit(&tw_joiner,&f,JOIN_STRICT);assert(!join_admit_world() && !strcmp(join_rx.detail,"world:route_other"));
            join_tables_free(&f);join_t1_reset();
            TJ_ROUTE_WORLDS();*(void **)wp[0]=image+0x2c4f000;TJ_FLIGHT_CAPTURE(f);
            assert(f.nroute==1 && f.route[0].flags==(JOIN_ROUTE_LINEAR|JOIN_ROUTE_OTHER) && join_route_shape(f.agent,1,f.route,1));
            tw_tables_reset(1);tw_admit(&tw_joiner,&f,JOIN_REPORT);assert(join_admit_world() && join_rx.route_other==1);
            {int row=0;for(unsigned i=0;i<join_rx.nrows;i++) row|=strstr(join_rx.rows[i],"route side=session id=0000000000002001 why=other flags=300 points=0 waypoints=1 pending=0")!=NULL;assert(row);}
            join_tables_free(&f);join_t1_reset();
#undef TJ_ROUTE_WORLDS
        }
        /* Version 8, PARAM (j1-22: the turn rates +0x378..+0x380 of the Agent's own flight parameter slot, which the step and the
         * update's angular clamp read, are not in AGENT): the session's three slots (its read slot +0x1a0 and write slot +0x000 at T,
         * selectors 1/0) with their own bytes, the read slot's ID the cargo bay, the write slot's an object without a portable ID;
         * the joiner's (read slot +0x000, write slot +0x1a0) as its load left them. The admission writes each role's carried bytes:
         * the joiner's slot of a role then holds the session's slot of that role, its ID as the joiner's runtime ID; the scratch
         * +0x140..+0x15f and an unmapped ID stay the joiner's. */
        {
            TJ_FLIGHT_WORLDS(JOIN_FLIGHT_LINEAR,JOIN_FLIGHT_LINEAR);
            static unsigned char before[2][3][JOIN_PARAM_STRIDE];
            for(unsigned side=0;side<2;side++) {
                tw_world *w=side?&tw_joiner:&tw_session;
                for(unsigned k=0;k<3;k++) {
                    unsigned char *at=w->agent+k*JOIN_PARAM_STRIDE;
                    for(unsigned i=0;i<JOIN_PARAM_STRIDE;i++) at[i]=(unsigned char)(i*7u+k*31u+(side?5u:0u));
                    const float turn[3]={side?0.4f:0.5f,0.25f,0.125f};memcpy(at+JOIN_PARAM_TURN,turn,12);
                    uint64_t id=side?0x5555:k==1?w->runtime[TW_CARGO]:k==0?0x7777:0;memcpy(at+JOIN_PARAM_ID,&id,8);
                    for(unsigned i=0x190;i<0x194;i++) at[i]&=1u;   /* the mode bytes, AGENT's flags */
                    memcpy(before[side][k],at,JOIN_PARAM_STRIDE);
                }
            }
            TJ_FLIGHT_CAPTURE(f);
            assert(f.nparam==1 && f.param[0].portable==0x2001 && f.param[0].flags==2u && join_param_shape(f.agent,1,f.param,1));
            {   /* J1 j1-23: two slots apart only in the padding pack to the same block. */
                unsigned char s1[JOIN_PARAM_SPAN],s2[JOIN_PARAM_SPAN],b1[JOIN_PARAM_SLOT],b2[JOIN_PARAM_SLOT];
                for(unsigned i=0;i<sizeof(s1);i++) s1[i]=(unsigned char)(i*7);
                memcpy(s2,s1,sizeof(s1));memcpy(s2+0x5c,"eyes",4);memcpy(s2+0xe9,"own",3);memcpy(s2+0x13c,"evem",4);
                join_param_pack(s1,0x2001,b1);join_param_pack(s2,0x2001,b2);assert(!memcmp(b1,b2,sizeof(b1)) && !b1[0x5c] && !b1[0xe9] && !b1[0x13c]);
                s2[0x58]^=1;join_param_pack(s2,0x2001,b2);assert(memcmp(b1,b2,sizeof(b1)));   /* a real field still counts */
            }
            {uint64_t id;memcpy(&id,f.param[0].slot[0]+JOIN_PARAMB_ID,8);assert(id==0x2003);   /* the read role: the session's +0x1a0 */
             unsigned char e[3][JOIN_PARAM_OBJECT];memcpy(e[0],before[0][1],JOIN_PARAM_OBJECT);memcpy(e[1],before[0][0],JOIN_PARAM_OBJECT);
             memcpy(e[2],before[0][2],JOIN_PARAM_OBJECT);for(unsigned k=0;k<3;k++) join_param_pad_copy(e[k],NULL);   /* padding carried as 0 */
             assert(!memcmp(f.param[0].slot[0],e[0],JOIN_PARAM_OBJECT) && !memcmp(f.param[0].slot[1],e[1],JOIN_PARAM_OBJECT) &&
                    !memcmp(f.param[0].slot[2],e[2],JOIN_PARAM_OBJECT));
             memcpy(&id,f.param[0].slot[1]+JOIN_PARAMB_ID,8);assert(!id);memcpy(&id,f.param[0].slot[2]+JOIN_PARAMB_ID,8);assert(!id);
             assert(!memcmp(f.param[0].slot[2]+JOIN_PARAMB_TIME,before[0][2]+JOIN_PARAM_TIME,8) &&
                    !memcmp(f.param[0].slot[2]+JOIN_PARAMB_TAIL,before[0][2]+JOIN_PARAM_TAIL,JOIN_PARAM_TAIL_LEN));}
            {join_buf b={0};join_tables r8;const char *why;assert(join_package_build(&f,&b) && join_package_parse(b.data,b.size,&r8,&why) &&
             r8.nparam==1 && !memcmp(r8.param,f.param,sizeof(*f.param)));join_tables_free(&r8);free(b.data);}
            tw_tables_reset(1);tw_admit(&tw_joiner,&f,JOIN_REPORT);assert(join_admit_world());
            assert(join_rx.param_applied==1 && join_rx.param_fixed==1 && join_rx.param_unmapped==1 && join_rx.param_read==1 && join_rx.param_write==1 &&
                   join_rx.param_own==1 && join_rx.param_turn==1 && !join_rx.param_missing && !join_rx.param_shape);
            for(unsigned role=0;role<3;role++) {
                const unsigned mine=role==0?0u:role==1?1u:2u,theirs=role==0?1u:role==1?0u:2u;   /* joiner read 0 / write 1, session read 1 / write 0 */
                const unsigned char *j=tw_joiner.agent+mine*JOIN_PARAM_STRIDE,*q=before[0][theirs];uint64_t id;
                unsigned char qe[JOIN_PARAM_OBJECT];memcpy(qe,q,JOIN_PARAM_OBJECT);join_param_pad_copy(qe,j);   /* the joiner keeps its padding */
                assert(!memcmp(j,qe,JOIN_PARAM_OBJECT) && !memcmp(j+JOIN_PARAM_VECTOR,q+JOIN_PARAM_VECTOR,16) && !memcmp(j+JOIN_PARAM_TIME,q+JOIN_PARAM_TIME,8) &&
                       !memcmp(j+JOIN_PARAM_TAIL,q+JOIN_PARAM_TAIL,JOIN_PARAM_TAIL_LEN));
                assert(!memcmp(j+JOIN_PARAM_OBJECT,before[1][mine]+JOIN_PARAM_OBJECT,0x20));   /* the scratch: the joiner's */
                memcpy(&id,j+JOIN_PARAM_ID,8);
                assert(id==(role==0?tw_joiner.runtime[TW_CARGO]:role==1?0x5555u:0u));   /* the write role's ID had no portable ID: kept */
            }
            {float turn;memcpy(&turn,tw_joiner.agent+JOIN_PARAM_OWN+JOIN_PARAM_TURN,4);assert(turn==0.5f);}
            {int row=0;for(unsigned i=0;i<join_rx.nrows;i++) row|=strstr(join_rx.rows[i],"param side=session id=0000000000002001 why=unmapped flags=2")!=NULL;assert(row);
             row=0;for(unsigned i=0;i<join_rx.nrows;i++) row|=strstr(join_rx.rows[i],"param side=joiner id=0000000000002001 why=values first=read+0:")!=NULL;assert(row);}
            {char status[3584];assert(join_status(status,sizeof(status)) && strstr(status," param=1:1:0:0:1:1:1:1:1 dock=0:0:0:0:0 steer=0:0:0:0:0:0:0:0:0 t1="));}
            /* Again: nothing differs (the unmapped ID counted again, the joiner's kept); strict refuses the unmapped ID. */
            tw_admit(&tw_joiner,&f,JOIN_REPORT);assert(join_admit_world() && join_rx.param_applied==1 && !join_rx.param_fixed && join_rx.param_unmapped==1);
            tw_admit(&tw_joiner,&f,JOIN_STRICT);assert(!join_admit_world() && !strcmp(join_rx.detail,"world:param_unmapped"));
            join_tables_free(&f);join_t1_reset();
            /* Without the unmapped ID strict admits it; joiner selectors that are not one each: param shape (NAV's too), nothing written. */
            {uint64_t none=0;memcpy(tw_session.agent+JOIN_PARAM_ID,&none,8);}
            TJ_FLIGHT_CAPTURE(f);assert(!f.param[0].flags);
            tw_tables_reset(1);tw_admit(&tw_joiner,&f,JOIN_STRICT);assert(join_admit_world() && join_rx.param_applied==1 && !join_rx.param_unmapped);
            {uint64_t id;memcpy(&id,tw_joiner.agent+JOIN_PARAM_STRIDE+JOIN_PARAM_ID,8);assert(!id);}
            tw_admit(&tw_joiner,&f,JOIN_REPORT);*(uint32_t *)(image+JOIN_PARAM_WRITE_INDEX)=0u;
            assert(join_admit_world() && join_rx.param_shape==1 && !join_rx.param_applied);
            {int row=0;for(unsigned i=0;i<join_rx.nrows;i++) row|=strstr(join_rx.rows[i],"param side=joiner id=0000000000002001 why=shape check=selectors")!=NULL;assert(row);}
            tw_phase(1);join_tables_free(&f);join_t1_reset();
        }
        /* Version 9, STEER (j1-25: the 13 ships with a steering or spline model were the census's motion at T+1; FLIGHT held their
         * kind only): the session's steering model mid-dogfight (every byte its own pattern, the target the cargo bay and the module,
         * a chase behaviour and the generic default, one path point on the cargo bay, two avoidance contacts, its avoidance time
         * 28.1), the joiner's as its load left it (other bytes, the same behaviour classes with other fields, no path, no contacts,
         * the avoidance time -1). Every byte no constructor writes and every vector's w lane differs per side: never carried. The
         * admission writes the carried bytes, maps the IDs, rebuilds the path through the engine's vector functions (synthetic here)
         * and writes the behaviours: the joiner's model then reads as the session's in every carried byte and keeps its own elsewhere
         * (its pointers, the avoidance cache, the w lanes, the padding). */
        {
            static __attribute__((aligned(16))) unsigned char beh[2][2][0xa0],pt[JOIN_AGENT_WAYPOINT],contacts[2*JOIN_STEER_CONTACT];
            static unsigned char before[2][0x3a0],bbefore[2][2][0xa0],carried[0x3a0],bcarried[2][0xa0];
#define TJ_STEER_WORLDS() do { \
            TJ_FLIGHT_WORLDS(JOIN_STEER_VTABLE,JOIN_STEER_VTABLE);tj_route_reset(); \
            for(unsigned side=0;side<2;side++) { \
                tw_world *w=side?&tw_joiner:&tw_session;unsigned char *m=model[side]; \
                for(unsigned i=0x18;i<0x3a0;i++) m[i]=(unsigned char)(i*13u+(side?101u:7u)); \
                memset(m+0x60,0,0x48);memset(m+JOIN_STEER_PATH,0,0x18);memset(m+JOIN_STEER_JOB_AT,0,8); \
                *(unsigned char **)(m+JOIN_FLIGHT_OWNER)=w->agent; \
                {double avoid=side?-1.0:28.1;memcpy(m+0x88,&avoid,8);} \
                for(unsigned b=0;b<2;b++) { \
                    unsigned char *o=beh[side][b];for(unsigned i=0;i<0xa0;i++) o[i]=(unsigned char)(i*7u+b*3u+(side?50u:1u)); \
                    *(void **)o=image+(b?0x2c4edc0u:0x2c4e840u);*(unsigned char **)(o+8)=m;*(unsigned char **)(m+JOIN_STEER_BEHAVIOURS+8u*b)=o; \
                } \
                if(!side) { \
                    memcpy(m+JOIN_STEER_IDS_AT0,&w->runtime[TW_CARGO],8);memcpy(m+JOIN_STEER_IDS_AT1,&w->runtime[TW_MODULE],8); \
                    memset(pt,0xa5,sizeof(pt));*(void **)pt=image+JOIN_ROUTE_POINT_VT;memcpy(pt+0x10,&w->runtime[TW_CARGO],8);memset(pt+0x18,0,8); \
                    memset(pt+JOIN_ROUTE_SUBPOINT,0,8); \
                    uintptr_t v[3]={(uintptr_t)pt,(uintptr_t)pt+JOIN_AGENT_WAYPOINT,(uintptr_t)pt+JOIN_AGENT_WAYPOINT};memcpy(m+JOIN_STEER_PATH,v,24); \
                    uintptr_t c[3]={(uintptr_t)contacts,(uintptr_t)contacts+2*JOIN_STEER_CONTACT,(uintptr_t)contacts+2*JOIN_STEER_CONTACT}; \
                    memcpy(m+JOIN_STEER_CONTACTS,c,24); \
                } \
                memcpy(before[side],m,0x3a0);memcpy(bbefore[side],beh[side],sizeof(beh[side])); \
            } \
        } while(0)
            memset(carried,0,sizeof(carried));memset(bcarried,0,sizeof(bcarried));
            for(unsigned i=0;i<JOIN_STEER_NRANGES;i++) for(unsigned k=0;k<join_steer_ranges[i].len;k++)
                if(!join_steer_ranges[i].vectors || k%16u<12u) carried[join_steer_ranges[i].at+k]=1;
            for(unsigned b=0;b<2;b++) {const join_steer_class *c=&join_steer_classes[join_steer_class_of(b?0x2c4edc0u:0x2c4e840u)];
                for(unsigned i=0;i<c->n;i++) for(unsigned k=0;k<c->r[i].len;k++) if(!c->r[i].vectors || k%16u<12u) bcarried[b][c->r[i].at+k]=1;}
            assert(!carried[0x18] && !carried[0x2c] && !carried[0x60] && !carried[0x70] && !carried[0x80] && !carried[0x88] && !carried[0x90] &&
                   !carried[0xb4] && !carried[0x118] && !carried[0x1c8] && !carried[0x274] && !carried[0x300] && !carried[0x310] && !carried[0x320] &&
                   !carried[0x344] && !carried[0x360] && !carried[0x381] && !carried[0x388] && !carried[0x398] && carried[0x33c] && carried[0x340]);
            TJ_STEER_WORLDS();TJ_FLIGHT_CAPTURE(f);
            assert(f.nflight==1 && f.flight[0].flags==JOIN_FLIGHT_OTHER && f.nsteer==1 && f.nsteerpt==1 && !f.nspline);
            const join_steer_rec *r=&f.steer[0];
            assert(r->portable==0x2001 && r->kind==JOIN_STEER_VTABLE && !r->flags && r->ref[0]==0x2003 && r->ref[1]==0x2005 && r->behaviour[0]==0x2c4e840u &&
                   r->behaviour[1]==0x2c4edc0u && r->points==1 && r->contacts==2 && join_steer_shape(f.flight,1,f.steer,1,f.steerpt,1));
            assert(f.steerpt[0].portable==0x2001 && f.steerpt[0].list==JOIN_STEER_LIST && f.steerpt[0].ref[0]==0x2003 && !f.steerpt[0].flags);
            {   /* What is not carried does not reach the record: the padding, the w lanes, the avoidance time, the behaviours' own +8. */
                model[0][0x118]^=0xff;model[0][0x2c]^=0xff;model[0][0x88]^=0xff;beh[0][0][0x78]^=0xff;beh[0][1][0x19]^=0xff;
                join_tables g;TJ_FLIGHT_CAPTURE(g);assert(g.nsteer==1 && !memcmp(g.steer,f.steer,sizeof(*f.steer)));join_tables_free(&g);
                model[0][0x118]^=0xff;model[0][0x2c]^=0xff;model[0][0x88]^=0xff;beh[0][0][0x78]^=0xff;beh[0][1][0x19]^=0xff;
            }
            {join_buf b={0};join_tables r9;const char *why;assert(join_package_build(&f,&b) && join_package_parse(b.data,b.size,&r9,&why) &&
             r9.nsteer==1 && !memcmp(r9.steer,f.steer,sizeof(*f.steer)) && r9.nsteerpt==1 && !memcmp(r9.steerpt,f.steerpt,sizeof(*f.steerpt)));
             join_tables_free(&r9);free(b.data);}
            tw_tables_reset(1);tw_admit(&tw_joiner,&f,JOIN_STRICT);assert(join_admit_world());
            assert(join_rx.steer_applied==1 && join_rx.steer_fixed==1 && join_rx.steer_rebuilt==1 && !join_rx.steer_missing && !join_rx.steer_kind &&
                   !join_rx.steer_shape && !join_rx.steer_unmapped && !join_rx.steer_other && !join_rx.steer_job && !join_rx.flight_other &&
                   tj_route_clears==1 && tj_route_pushes==1);
            for(unsigned i=0x18;i<0x3a0;i++) {
                if(i>=0x60 && i<0x68) continue;   /* the IDs, below */
                if(i>=0x80 && i<0x88) continue;
                if(i>=JOIN_STEER_PATH && i<JOIN_STEER_PATH+0x18) continue;   /* the path vector, rebuilt */
                assert(model[1][i]==(carried[i]?before[0][i]:before[1][i]));
            }
            {uint64_t a,c;memcpy(&a,model[1]+JOIN_STEER_IDS_AT0,8);memcpy(&c,model[1]+JOIN_STEER_IDS_AT1,8);
             assert(a==tw_joiner.runtime[TW_CARGO] && c==tw_joiner.runtime[TW_MODULE]);
             double avoid;memcpy(&avoid,model[1]+0x88,8);assert(avoid==-1.0);}   /* the avoidance cache: the joiner's own */
            for(unsigned b=0;b<2;b++) for(unsigned i=0;i<(b?0x20u:0xa0u);i++)
                assert(beh[1][b][i]==(bcarried[b][i]?bbefore[0][b][i]:bbefore[1][b][i]));   /* vtable and +8: the joiner's */
            {uintptr_t v[3];memcpy(v,model[1]+JOIN_STEER_PATH,24);
             assert(v[1]==v[0]+JOIN_AGENT_WAYPOINT && v[0]==(uintptr_t)tj_route_pool[0] && !memcmp((void *)(v[0]+0x10),&tw_joiner.runtime[TW_CARGO],8) &&
                    !memcmp((void *)(v[0]+0x20),pt+0x20,0x48));}
            {char status[3584];assert(join_status(status,sizeof(status)) && strstr(status," steer=1:1:0:0:0:0:0:0:1 t1="));}
            /* Again: every carried byte equal, nothing written or rebuilt. */
            tw_admit(&tw_joiner,&f,JOIN_STRICT);
            assert(join_admit_world() && join_rx.steer_applied==1 && !join_rx.steer_fixed && !join_rx.steer_rebuilt && tj_route_clears==1);
            /* A version 8 package (the same tables): FLIGHT counts the steering model other, STEER is not admitted. */
            f.version=8;tw_admit(&tw_joiner,&f,JOIN_REPORT);assert(join_admit_world() && join_rx.flight_other==1 && !join_rx.steer_applied);
            f.version=JOIN_PACKAGE_VERSION;join_tables_free(&f);join_t1_reset();
            /* A path job pending at T on the session: counted, the rest written; strict refuses it. */
            TJ_STEER_WORLDS();*(uintptr_t *)(model[0]+JOIN_STEER_JOB_AT)=(uintptr_t)contacts;TJ_FLIGHT_CAPTURE(f);
            assert(f.steer[0].flags==JOIN_STEER_JOB);
            tw_tables_reset(1);tw_admit(&tw_joiner,&f,JOIN_REPORT);
            assert(join_admit_world() && join_rx.steer_job==1 && join_rx.steer_applied==1 && join_rx.steer_fixed==1 && !*(uintptr_t *)(model[1]+JOIN_STEER_JOB_AT));
            {int row=0;for(unsigned i=0;i<join_rx.nrows;i++) row|=strstr(join_rx.rows[i],"steer side=session id=0000000000002001 why=job")!=NULL;assert(row);}
            tw_admit(&tw_joiner,&f,JOIN_STRICT);assert(!join_admit_world() && !strcmp(join_rx.detail,"world:steer_job"));
            join_tables_free(&f);join_t1_reset();
            /* The joiner's active behaviour of another class: steer other (the default still written); a session's behaviour of a class
             * this build does not carry: flagged, kind only, steer other; strict refuses both. */
            TJ_STEER_WORLDS();*(void **)beh[1][0]=image+0x2c4d288u;TJ_FLIGHT_CAPTURE(f);
            tw_tables_reset(1);tw_admit(&tw_joiner,&f,JOIN_REPORT);
            assert(join_admit_world() && join_rx.steer_other==1 && join_rx.steer_applied==1 && beh[1][1][0x18]==bbefore[0][1][0x18] &&
                   beh[1][0][0x10]==bbefore[1][0][0x10]);
            {int row=0;for(unsigned i=0;i<join_rx.nrows;i++)
                 row|=strstr(join_rx.rows[i],"steer side=session id=0000000000002001 why=other what=2 flags=0 behaviours=2c4e840/2c4edc0 points=1")!=NULL;
             assert(row);}
            tw_admit(&tw_joiner,&f,JOIN_STRICT);assert(!join_admit_world() && !strcmp(join_rx.detail,"world:steer_other"));
            join_tables_free(&f);join_t1_reset();
            TJ_STEER_WORLDS();*(void **)beh[0][1]=image+0x2c4ee80u;*(void **)beh[1][1]=image+0x2c4ee80u;TJ_FLIGHT_CAPTURE(f);
            assert(f.steer[0].flags==JOIN_STEER_UNCARRIED1 && f.steer[0].behaviour[1]==0x2c4ee80u && join_steer_zero(f.steer[0].state[1],JOIN_STEER_STATE) &&
                   join_steer_shape(f.flight,1,f.steer,1,f.steerpt,1));
            tw_tables_reset(1);tw_admit(&tw_joiner,&f,JOIN_REPORT);
            assert(join_admit_world() && join_rx.steer_other==1 && !memcmp(beh[1][1]+0x10,bbefore[1][1]+0x10,0x10));
            join_tables_free(&f);join_t1_reset();
            /* The target without a portable ID: the record flags it, the joiner keeps its IDs (the rest written); strict refuses. */
            TJ_STEER_WORLDS();{uint64_t none=0x7777;memcpy(model[0]+JOIN_STEER_IDS_AT0,&none,8);}TJ_FLIGHT_CAPTURE(f);
            assert(f.steer[0].flags==1u && !f.steer[0].ref[0] && f.steer[0].ref[1]==0x2005);
            tw_tables_reset(1);tw_admit(&tw_joiner,&f,JOIN_REPORT);
            assert(join_admit_world() && join_rx.steer_unmapped==1 && join_rx.steer_fixed==1 && !*(uint64_t *)(model[1]+JOIN_STEER_IDS_AT0) &&
                   model[1][0x33c]==before[0][0x33c]);
            tw_admit(&tw_joiner,&f,JOIN_STRICT);assert(!join_admit_world() && !strcmp(join_rx.detail,"world:steer_unmapped"));
            join_tables_free(&f);join_t1_reset();
            /* Another model kind on the joiner: FLIGHT's kind and STEER's, nothing written. */
            TJ_STEER_WORLDS();TJ_FLIGHT_CAPTURE(f);*(void **)model[1]=image+JOIN_FLIGHT_LINEAR;
            tw_tables_reset(1);tw_admit(&tw_joiner,&f,JOIN_REPORT);
            assert(join_admit_world() && join_rx.flight_kind==1 && join_rx.steer_kind==1 && !join_rx.steer_applied && model[1][0x33c]==before[1][0x33c]);
            {int row=0;for(unsigned i=0;i<join_rx.nrows;i++) row|=strstr(join_rx.rows[i],"steer side=joiner id=0000000000002001 why=kind kind=2c4e9c8/2c4f610")!=NULL;
             assert(row);}
            join_tables_free(&f);join_t1_reset();
            /* Version 10, SOLVER (j1-27: ten steering models with every carried byte equal still differed at T+1: they fly through the
             * Agent's motion solver, which no save holds): the session's solver with its own bytes, a pointer into itself and a vtable; the
             * joiner's Agent has none, the engine's preparation (synthetic here) makes one with other bytes. The admission writes every byte
             * and makes the pointers this object's and this image's; again: nothing written, none created. */
            {
                static __attribute__((aligned(16))) unsigned char so[2][JOIN_SOLVER_BYTES];
#define TJ_SOLVER_WORLDS() do { \
                TJ_STEER_WORLDS();tj_solver_fresh=so[1]; \
                for(unsigned side=0;side<2;side++) for(unsigned i=0;i<JOIN_SOLVER_BYTES;i++) so[side][i]=(unsigned char)(i*13u+(side?71u:3u)); \
                *(uintptr_t *)(so[0]+0x16a0)=(uintptr_t)so[0]+0x3e8;*(uintptr_t *)(so[0]+0x6a00)=(uintptr_t)image+0x2c1f0f8; \
                *(uintptr_t *)(so[1]+0x16a0)=(uintptr_t)so[1]+0x3e8;*(uintptr_t *)(so[1]+0x6a00)=(uintptr_t)image+0x2c1f0f8; \
                *(unsigned char **)(tw_session.agent+JOIN_AGENT_SOLVER)=so[0];*(unsigned char **)(tw_joiner.agent+JOIN_AGENT_SOLVER)=NULL; \
            } while(0)
                join_solver_prepare_fn prepare=join_solver_prepare;join_solver_prepare=tj_solver_prepare;
                TJ_SOLVER_WORLDS();TJ_FLIGHT_CAPTURE(f);
                {
                    const join_solver_rec *s=&f.solver[0];uint64_t q;
                    assert(f.nsolver==1 && s->portable==0x2001 && s->flags==JOIN_SOLVER_ACTIVE && s->kind==JOIN_STEER_VTABLE && s->pointers==2 && !s->reserved);
                    assert(s->pointer[0]==((0x16a0u/8u)<<1) && s->pointer[1]==((0x6a00u/8u)<<1|1u) && !s->pointer[2] && join_solver_shape(s,1));
                    memcpy(&q,s->data+0x16a0,8);assert(q==0x3e8);memcpy(&q,s->data+0x6a00,8);assert(q==0x2c1f0f8);
                    assert(s->data[0x10]==so[0][0x10] && join_t1x.n==1 && join_t1x.portable[0]==0x2001);
                }
                {join_buf b={0};join_tables r10;const char *why;assert(join_package_build(&f,&b) && join_package_parse(b.data,b.size,&r10,&why) &&
                 r10.nsolver==1 && !memcmp(r10.solver,f.solver,sizeof(*f.solver)));join_tables_free(&r10);free(b.data);}
                {unsigned char a[16],c[16];assert(join_portable(&f,a));f.solver[0].data[0x10]^=1;assert(join_portable(&f,c) && !memcmp(a,c,16));
                 f.solver[0].data[0x10]^=1;f.solver[0].kind^=1;assert(join_portable(&f,c) && memcmp(a,c,16));f.solver[0].kind^=1;}
                join_t1_reset();tw_tables_reset(1);tw_admit(&tw_joiner,&f,JOIN_STRICT);assert(join_admit_world());
                assert(join_rx.solver_applied==1 && join_rx.solver_fixed==1 && join_rx.solver_created==1 && !join_rx.solver_missing && !join_rx.solver_shape && !join_rx.solver_kind);
                assert(*(unsigned char **)(tw_joiner.agent+JOIN_AGENT_SOLVER)==so[1]);
                for(unsigned i=0;i<JOIN_SOLVER_BYTES;i+=8) {
                    if(i==0x16a0) assert(*(uintptr_t *)(so[1]+i)==(uintptr_t)so[1]+0x3e8);
                    else if(i==0x6a00) assert(*(uintptr_t *)(so[1]+i)==(uintptr_t)image+0x2c1f0f8);
                    else assert(!memcmp(so[1]+i,so[0]+i,8));
                }
                {char status[3584];assert(join_status(status,sizeof(status)) && strstr(status," solver=1:1:1:0:0:0"));}
                /* The rows of T+1 name the model's wanted velocities, its avoidance cache and the solver's clock. */
                {
                    assert(join_t1x.n==1 && join_t1x.runtime[0]==tw_joiner.runtime[TW_SHIP]);
                    join_t1_arm(633);join_t1_probe(633,0.0);assert(!join_t1x_row(1));join_t1_probe(634,0.0);
                    const char *row=join_t1x_row(1);char want[80];
                    snprintf(want,sizeof(want),"id=%016llx kind=%x solver=1 rem=",(unsigned long long)0x2001,JOIN_STEER_VTABLE);
                    assert(row && strstr(row,want) && strstr(row," contacts=0 state=0 want=") && !join_t1x_row(2));
                    join_t1_reset();
                }
                tw_admit(&tw_joiner,&f,JOIN_STRICT);
                assert(join_admit_world() && join_rx.solver_applied==1 && !join_rx.solver_fixed && !join_rx.solver_created);
                join_t1_reset();
                /* Version 12 (j1-41): the session's vector begin (an address of its memory) rides as JOIN_SOLVER_HEAP and the joiner keeps
                 * its own value there; an int pair that only looks like an address (0, 0x7a) is written. A version 11 package keeps the
                 * joiner's own for both. */
                {
                    const uint64_t vec=0x00007ff012345670ull,pair=0x0000007a00000000ull;
                    int (*mapped)(uint64_t)=join_mapped;join_mapped=tj_solver_mapped;tj_solver_cell=(uintptr_t)vec;
                    join_tables h;TJ_SOLVER_WORLDS();
                    memcpy(so[0]+0x81c8,&vec,8);memcpy(so[0]+0x81d0,&pair,8);
                    TJ_FLIGHT_CAPTURE(h);uint64_t q,own[2];
                    memcpy(&q,h.solver[0].data+0x81c8,8);assert(q==JOIN_SOLVER_HEAP && join_cap.solver_mapped==1);
                    memcpy(&q,h.solver[0].data+0x81d0,8);assert(q==pair);
                    {char status[3584];assert(join_status(status,sizeof(status)) && strstr(status," solver_mapped=1"));}
                    memcpy(own,so[1]+0x81c8,16);
                    join_t1_reset();tw_tables_reset(1);tw_admit(&tw_joiner,&h,JOIN_STRICT);assert(join_admit_world() && join_rx.solver_heap==1);
                    memcpy(&q,so[1]+0x81c8,8);assert(q==own[0]);memcpy(&q,so[1]+0x81d0,8);assert(q==pair);
                    /* version 11 (the session's raw address again): both kept, the old test */
                    memcpy(so[1]+0x81c8,own,16);*(unsigned char **)(tw_joiner.agent+JOIN_AGENT_SOLVER)=NULL;
                    h.version=11;memcpy(h.solver[0].data+0x81c8,&vec,8);join_t1_reset();tw_tables_reset(1);tw_admit(&tw_joiner,&h,JOIN_REPORT);
                    assert(join_admit_world() && join_rx.solver_heap==2);
                    memcpy(&q,so[1]+0x81c8,8);assert(q==own[0]);memcpy(&q,so[1]+0x81d0,8);assert(q==own[1]);
                    join_mapped=mapped;join_tables_free(&h);join_t1_reset();
                }
                TJ_SOLVER_WORLDS();
                /* A version 9 package (the same tables): SOLVER is not admitted. */
                f.version=9;tw_admit(&tw_joiner,&f,JOIN_REPORT);assert(join_admit_world() && !join_rx.solver_applied);
                f.version=JOIN_PACKAGE_VERSION;join_t1_reset();
                /* The preparation gives no solver: counted, nothing else touched; strict refuses it. */
                *(unsigned char **)(tw_joiner.agent+JOIN_AGENT_SOLVER)=NULL;tj_solver_fresh=NULL;
                tw_admit(&tw_joiner,&f,JOIN_REPORT);assert(join_admit_world() && join_rx.solver_shape==1 && !join_rx.solver_applied);
                tw_admit(&tw_joiner,&f,JOIN_STRICT);assert(!join_admit_world() && !strcmp(join_rx.detail,"world:solver_shape"));
                tj_solver_fresh=so[1];join_tables_free(&f);join_t1_reset();
                /* Another model class on the joiner: counted, the solver still written. */
                TJ_SOLVER_WORLDS();TJ_FLIGHT_CAPTURE(f);*(void **)model[1]=image+JOIN_FLIGHT_LINEAR;
                tw_admit(&tw_joiner,&f,JOIN_REPORT);assert(join_admit_world() && join_rx.solver_kind==1 && join_rx.solver_applied==1);
                join_tables_free(&f);join_t1_reset();
                /* A session Agent without a solver: no record. A dormant one under a linear model: no T+1 row; past the cap: counted. */
                TJ_SOLVER_WORLDS();*(unsigned char **)(tw_session.agent+JOIN_AGENT_SOLVER)=NULL;TJ_FLIGHT_CAPTURE(f);
                assert(!f.nsolver && !join_t1x.n);join_tables_free(&f);join_t1_reset();
                {
                    join_tables g;memset(&g,0,sizeof(g));uint32_t cap=0;
                    *(unsigned char **)(tw_session.agent+JOIN_AGENT_SOLVER)=so[0];join_solver_dormant=0;join_cap.solver_over=0;
                    assert(join_capture_solver(&g,tw_session.agent,tw_session.runtime[TW_SHIP],0x2001,JOIN_FLIGHT_LINEAR,&cap) && g.nsolver==1 && !g.solver[0].flags && !join_t1x.n);
                    join_solver_dormant=JOIN_SOLVER_DORMANT;
                    assert(join_capture_solver(&g,tw_session.agent,tw_session.runtime[TW_SHIP],0x2001,JOIN_FLIGHT_LINEAR,&cap) && g.nsolver==1 && join_cap.solver_over==1);
                    /* More pointers than the list holds: flagged, raw, not applied. */
                    for(unsigned i=0;i<JOIN_SOLVER_POINTERS+1u;i++) *(uintptr_t *)(so[0]+0x100+8u*i)=(uintptr_t)image+0x1000u;
                    assert(join_capture_solver(&g,tw_session.agent,tw_session.runtime[TW_SHIP],0x2002,JOIN_STEER_VTABLE,&cap) && g.nsolver==2);
                    const join_solver_rec *o=&g.solver[1];
                    assert(o->flags==(JOIN_SOLVER_ACTIVE|JOIN_SOLVER_OVERFLOW) && !o->pointers && !o->pointer[0] && *(uintptr_t *)(o->data+0x100)==(uintptr_t)image+0x1000u && join_solver_shape(o,1));
                    join_tables_free(&g);join_t1_reset();
                }
                /* The census of the bullets (j1-28/29: none in any zone's children vector): every live U::Bullet of the component
                 * registry, in slot order, keyed by its parent zone (a zone of the identity table by its portable ID, a temp zone by its
                 * runtime ID with the bit). A synthetic registry: one block of 9 slots; a dead slot, another class, a NULL block. */
                {
                    static __attribute__((aligned(16))) unsigned char bu[3][JOIN_BULLET_BYTES],mo[JOIN_BULLET_MOTION],ma[0x48],tz[0x80];
                    static __attribute__((aligned(16))) unsigned char reg[0x58+32*8],block[3*32];
                    TJ_SOLVER_WORLDS();unsigned char *zone=tw_session.object[TW_MODULE];
                    for(unsigned k=0;k<3;k++) {for(unsigned i=0;i<JOIN_BULLET_BYTES;i++) bu[k][i]=(unsigned char)(i*3u+k+5u);*(void **)bu[k]=image+JOIN_BULLET_VTABLE;
                        memset(bu[k]+0x60,0,8);memset(bu[k]+JOIN_CONTROLLER,0,8);memset(bu[k]+JOIN_BULLET_SHOOTER,0,8);*(unsigned char **)(bu[k]+JOIN_PARENT)=zone;}
                    *(unsigned char **)(bu[0]+0x60)=ma;*(unsigned char **)(bu[0]+JOIN_BULLET_SHOOTER)=tw_session.object[TW_SHIP];*(unsigned char **)(bu[0]+JOIN_CONTROLLER)=mo;
                    *(uintptr_t *)(bu[1]+0x10)=(uintptr_t)bu[1]+0x20;
                    memset(tz,0,sizeof(tz));*(void **)tz=image+JOIN_ZONE_VTABLE;{uint64_t rid=0x7777;memcpy(tz+8,&rid,8);}*(unsigned char **)(bu[2]+JOIN_PARENT)=tz;
                    memset(ma,0,sizeof(ma));*(void **)ma=image+JOIN_MACRO_VTABLE;memcpy(ma+0x20,"bullet_x",8);{uint64_t l=8,c=15;memcpy(ma+0x30,&l,8);memcpy(ma+0x38,&c,8);}
                    for(unsigned i=0;i<JOIN_BULLET_MOTION;i++) mo[i]=(unsigned char)(i*9u+2u);
                    *(void **)mo=image+JOIN_BULLET_LINEAR;
                    memset(reg,0,sizeof(reg));memset(block,0,sizeof(block));*(unsigned char **)(reg+JOIN_REGISTRY_BLOCKS)=block;
                    /* slots: 2 bu[0] live, 3 the cargo bay live, 4 bu[1] live, 5 bu[2] dead (bit 2 clear), 7 bu[2] live; nibbles at bit 16+4e */
                    {
                        void *slot[9]={0};unsigned nib[9]={0};
                        slot[2]=bu[0];nib[2]=5;slot[3]=tw_session.object[TW_CARGO];nib[3]=5;slot[4]=bu[1];nib[4]=0xd;slot[5]=bu[2];nib[5]=1;slot[7]=bu[2];nib[7]=5;
                        for(unsigned k=0;k<3;k++) {uint32_t flags=0;for(unsigned e=0;e<3;e++) {memcpy(block+32*k+8*e,&slot[3*k+e],8);flags|=nib[3*k+e]<<(16+4*e);}
                            memcpy(block+32*k+0x1c,&flags,4);}
                    }
                    join_bullet_registry=reg;join_bullet_block_slots=9;
                    TJ_FLIGHT_CAPTURE(f);
                    assert(f.nbullet==3 && join_bullet_shape(f.bullet,3));
                    const join_bullet_rec *b0=&f.bullet[0],*b1=&f.bullet[1],*b2=&f.bullet[2];
                    assert(b0->zone==tw_portable[TW_MODULE] && b0->shooter==0x2001 && b0->position==2 && !b0->index && !b0->flags && b0->motion_kind==JOIN_BULLET_LINEAR &&
                           !strcmp(b0->macro,"bullet_x") && !memcmp(b0->bullet+0x20,bu[0]+0x20,8) && b0->motion[8]==mo[8]);
                    {uint64_t q;memcpy(&q,b0->bullet,8);assert(q==JOIN_BULLET_VTABLE);memcpy(&q,b0->motion,8);assert(q==JOIN_BULLET_LINEAR);}
                    assert(b1->zone==tw_portable[TW_MODULE] && b1->position==4 && b1->index==1 && !b1->shooter && !b1->macro[0] &&
                           b1->flags==(JOIN_BULLET_NO_SHOOTER|JOIN_BULLET_NO_MACRO|JOIN_BULLET_NO_MOTION));
                    {uint64_t q;memcpy(&q,b1->bullet+0x10,8);assert(q==0x20);}
                    assert(b2->zone==(0x7777u|JOIN_BULLET_ZONE_RUNTIME) && b2->position==7 && !b2->index);
                    assert(join_cap.bullet_blocks==1 && join_cap.bullet_live==4 && join_cap.bullet_unkeyed==1 && !join_cap.bullet_fault);
                    {char status[3584];assert(join_status(status,sizeof(status)) && strstr(status," bullet_walk=1:4:1"));}
                    {join_buf b={0};join_tables r10;const char *why;assert(join_package_build(&f,&b) && join_package_parse(b.data,b.size,&r10,&why) &&
                     r10.nbullet==3 && !memcmp(r10.bullet,f.bullet,3*sizeof(*f.bullet)));join_tables_free(&r10);free(b.data);}
                    join_tables_free(&f);join_t1_reset();
                    /* A bullet without a parent: counted as a fault, not recorded, the save goes on. */
                    *(unsigned char **)(bu[1]+JOIN_PARENT)=NULL;TJ_FLIGHT_CAPTURE(f);
                    assert(f.nbullet==2 && join_cap.bullet_fault==1);join_tables_free(&f);join_t1_reset();
                    join_bullet_registry=NULL;join_bullet_block_slots=1u<<20;
                }
                /* Version 11, the radar (j1-32: the joiner's units new, 262 of 2,638 passive units at T+1): a synthetic AI::Gravidar per side
                 * with the engine's layout (a bucket tree holding the ship's active unit, another the weapon's passive one; the active
                 * sources; one field sector). The session's unit has contacts (the cargo bay, the module, one without a portable ID; a
                 * second list with the turret), its factor and times; the joiner's is new, the passive one missing. The capture keys
                 * everything by portable ID; the admission writes the times, rebuilds both lists in the joiner's runtime IDs, sorted,
                 * makes the passive unit through the engine (a stub here), writes the sources and the sector's deadline. */
                {
                    static __attribute__((aligned(16))) unsigned char gw[2][0x6a00],gnode[3][0x30],gunit[2][JOIN_GRAV_BYTES],gpass[2][0x68];
                    static uint64_t gprimary[3],gsecond[1],gsrc[1];static uintptr_t gsectors[4];
                    TJ_SOLVER_WORLDS();
                    unsigned char *sector[2]={tw_session.object[TW_FULL],tw_joiner.object[TW_FULL]};uintptr_t sector_vt[2];
                    memset(gw,0,sizeof(gw));memset(gnode,0,sizeof(gnode));memset(gunit,0,sizeof(gunit));memset(gpass,0,sizeof(gpass));
                    for(unsigned side=0;side<2;side++) {
                        tw_world *w=side?&tw_joiner:&tw_session;
                        *(void **)gw[side]=image+JOIN_GRAV_WORKER_VT;
                        *(void **)gunit[side]=image+JOIN_GRAV_ACTIVE_VT;memcpy(gunit[side]+8,&w->runtime[TW_SHIP],8);
                        float factor=side?1.0f:0.5f;memcpy(gunit[side]+0x44,&factor,4);
                        double times[3]={side?-1.0:25.0,side?-1.0:25.7,side?-1.0:22.5};memcpy(gunit[side]+0xa8,times,24);
                        uint32_t flags=3;memcpy(gunit[side]+0xc4,&flags,4);
                        memcpy(&sector_vt[side],sector[side],8);*(void **)sector[side]=image+JOIN_GRAV_SECTOR_VT;
                        sector[side][0x442]=1;double deadline=side?0.0:12.5;memcpy(sector[side]+0x448,&deadline,8);
                    }
                    *(unsigned char **)(gnode[0]+0x28)=gunit[0];*(unsigned char **)(gw[0]+0x2890+3*0x40+8)=gnode[0];
                    *(void **)gpass[0]=image+JOIN_GRAV_PASSIVE_VT;memcpy(gpass[0]+8,&tw_session.runtime[TW_WEAPON],8);
                    {float factor=0.25f;memcpy(gpass[0]+0x44,&factor,4);}
                    *(unsigned char **)(gnode[1]+0x28)=gpass[0];*(unsigned char **)(gw[0]+0x2890+7*0x40+8)=gnode[1];
                    gprimary[0]=tw_session.runtime[TW_CARGO];gprimary[1]=tw_session.runtime[TW_MODULE];gprimary[2]=0xdead;
                    qsort(gprimary,3,8,join_u64_cmp);gsecond[0]=tw_session.runtime[TW_TURRET];
                    {uintptr_t v[3]={(uintptr_t)gprimary,(uintptr_t)(gprimary+3),(uintptr_t)(gprimary+3)};memcpy(gunit[0]+0x68,v,24);}
                    {uintptr_t v[3]={(uintptr_t)gsecond,(uintptr_t)(gsecond+1),(uintptr_t)(gsecond+1)};memcpy(gunit[0]+0x80,v,24);}
                    gsrc[0]=(uint64_t)(uintptr_t)tw_session.object[TW_CARGO];
                    {uintptr_t v[3]={(uintptr_t)gsrc,(uintptr_t)(gsrc+1),(uintptr_t)(gsrc+1)};memcpy(gw[0]+0x48,v,24);}
                    gsectors[0]=(uintptr_t)sector[0];
                    {uintptr_t v[2]={(uintptr_t)gsectors,(uintptr_t)(gsectors+4)};memcpy(gw[0]+0x68b8,v,16);}
                    /* version 12: the sector's two zone groups (the module's and the cargo bay's zones), each other's neighbour on the
                     * session, none on the joiner (its load built the lists otherwise) */
                    static __attribute__((aligned(16))) unsigned char ggrp[2][2][JOIN_GRAVN_GROUP];static uint64_t gnb[2];static uintptr_t jsectors[4];
                    memset(ggrp,0,sizeof(ggrp));gnb[0]=1;gnb[1]=0;
                    for(unsigned side=0;side<2;side++) {
                        tw_world *w=side?&tw_joiner:&tw_session;
                        *(unsigned char **)ggrp[side][0]=w->object[TW_MODULE];*(unsigned char **)ggrp[side][1]=w->object[TW_CARGO];
                    }
                    for(unsigned g=0;g<2;g++) {uintptr_t v[3]={(uintptr_t)&gnb[g],(uintptr_t)(&gnb[g]+1),(uintptr_t)(&gnb[g]+1)};memcpy(ggrp[0][g]+0x38,v,24);}
                    gsectors[1]=(uintptr_t)ggrp[0];gsectors[2]=(uintptr_t)(ggrp[0]+2);
                    memset(jsectors,0,sizeof(jsectors));jsectors[0]=(uintptr_t)sector[1];jsectors[1]=(uintptr_t)ggrp[1];jsectors[2]=(uintptr_t)(ggrp[1]+2);
                    {uintptr_t v[2]={(uintptr_t)jsectors,(uintptr_t)(jsectors+4)};memcpy(gw[1]+0x68b8,v,16);}
                    void *(*grav_alloc)(size_t)=join_flight_alloc;join_flight_alloc=tj_big_alloc;tj_big_allocs=0;
                    void (*lock)(void *,int)=join_grav_lock;void *(*lookup)(void *,uint64_t)=join_grav_lookup;void *(*passive)(void *,void *)=join_grav_passive_new;
                    join_grav_lock=tj_grav_lock;join_grav_lookup=tj_grav_lookup;join_grav_passive_new=tj_grav_passive;
                    /* version 12, GRAVP (j1-40: the session's scan read older published poses than the joiner's): the ship's poses, the
                     * session's in the current view only, the joiner's in both (its load published them); every other pose slot empty */
                    static __attribute__((aligned(16))) unsigned char gpose[3][0x40];static uint64_t gslot[2][TW_OBJECTS][2];
                    const uint32_t gcur=join_view_index();
                    for(unsigned side=0;side<2;side++) {
                        tw_world *w=side?&tw_joiner:&tw_session;
                        for(unsigned o=0;o<TW_OBJECTS;o++) for(unsigned v=0;v<2;v++) {
                            unsigned char *at=w->object[o]+JOIN_VIEW_SLOTS+v*0x80u+JOIN_VIEW_POSE;memcpy(&gslot[side][o][v],at,8);memset(at,0,8);
                        }
                    }
                    for(unsigned i=0;i<0x40;i++) {gpose[0][i]=(unsigned char)(i*7+1);gpose[1][i]=(unsigned char)(i*11+2);gpose[2][i]=(unsigned char)(i*13+3);}
                    *(unsigned char **)(tw_session.object[TW_SHIP]+JOIN_VIEW_SLOTS+gcur*0x80u+JOIN_VIEW_POSE)=gpose[0];
                    *(unsigned char **)(tw_joiner.object[TW_SHIP]+JOIN_VIEW_SLOTS+gcur*0x80u+JOIN_VIEW_POSE)=gpose[1];
                    *(unsigned char **)(tw_joiner.object[TW_SHIP]+JOIN_VIEW_SLOTS+(gcur^1u)*0x80u+JOIN_VIEW_POSE)=gpose[2];
                    /* version 12, GRAVQ (j1-42: the pass of T's has_scanned posts wait in the worker's list; the session delivers them at
                     * T+1): the session's list holds a post for the cargo bay's unit and a node of another kind; the joiner's is empty */
                    static __attribute__((aligned(16))) uint64_t qsent[2][8],qnode[2][8],qevent[3];
                    void (*qcs)(void *,int)=join_gravq_cs;join_gravq_cs=tj_gravq_cs;
                    memset(qsent,0,sizeof(qsent));memset(qnode,0,sizeof(qnode));
                    qevent[0]=(uintptr_t)image+JOIN_GRAVQ_EVENT_VT;qevent[1]=qevent[2]=0;
                    qnode[0][0]=(uintptr_t)qnode[1];qnode[0][1]=(uintptr_t)qsent[0];qnode[0][2]=3;qnode[0][3]=tw_session.runtime[TW_CARGO];
                    qnode[0][4]=(uintptr_t)qevent;qnode[0][5]=0x403c400000000000ull;
                    qnode[1][0]=(uintptr_t)qsent[0];qnode[1][1]=(uintptr_t)qnode[0];qnode[1][2]=1;qnode[1][3]=tw_session.runtime[TW_CARGO];
                    qsent[0][0]=(uintptr_t)qnode[0];qsent[0][1]=(uintptr_t)qnode[1];
                    qsent[1][0]=qsent[1][1]=(uintptr_t)qsent[1];
                    for(unsigned side=0;side<2;side++) {uint64_t v[2]={(uintptr_t)qsent[side],side?0u:2u};memcpy(gw[side]+JOIN_GRAVQ_LIST,v,16);gw[side][AI_SCHED_W_READY]=1;}
                    join_grav_worker_test=gw[0];TJ_FLIGHT_CAPTURE(f);
                    assert(f.ngravq==2 && join_gravq_shape(f.gravq,2) && f.gravq[0].portable==tw_portable[TW_CARGO] && f.gravq[0].kind==3 &&
                           !f.gravq[0].flags && f.gravq[0].event==JOIN_GRAVQ_EVENT_VT && f.gravq[0].time==0x403c400000000000ull &&
                           f.gravq[1].kind==1 && f.gravq[1].flags==JOIN_GRAVQ_OTHER && !join_cap.gravq_busy);
                    assert(f.ngrav==2 && f.ngravc==4 && f.ngravs==2 && join_grav_shape(f.grav,f.ngrav,f.gravc,f.ngravc,f.gravs,f.ngravs));
                    {   /* both views of every radar object up to the sector, sorted; only the ship's current one holds a pose */
                        const join_gravp_rec *g=NULL;uint32_t present=0;
                        for(uint32_t i=0;i<f.ngravp;i++) {if(!g && f.gravp[i].portable==0x2001) g=&f.gravp[i];present+=f.gravp[i].flags==JOIN_GRAVP_PRESENT;}
                        assert(g && f.ngravp>=4 && !(f.ngravp&1u) && join_gravp_shape(f.gravp,f.ngravp) && present==1 && !g->rel &&
                               g->flags==JOIN_GRAVP_PRESENT && !memcmp(g->pose,gpose[0],0x40) && g[1].portable==0x2001 && g[1].rel==1 && !g[1].flags);
                        for(uint32_t i=0;i<f.ngravp;i++) assert(f.gravp[i].portable!=tw_portable[TW_FULL]);   /* the sector: not walked */
                    }
                    assert(f.ngravn==2 && join_gravn_shape(f.gravn,f.ngravn) && f.gravn[0].sector==tw_portable[TW_FULL] &&
                           f.gravn[0].zone==tw_portable[TW_CARGO] && f.gravn[0].neighbour==tw_portable[TW_MODULE] &&
                           f.gravn[1].zone==tw_portable[TW_MODULE] && f.gravn[1].neighbour==tw_portable[TW_CARGO]);
                    {
                        const join_grav_rec *a=&f.grav[0],*p=&f.grav[1];float factor;double times[3];
                        assert(a->portable==0x2001 && a->kind==JOIN_GRAV_ACTIVE_VT && a->flags==JOIN_GRAV_ACTIVE && a->count[0]==3 && a->count[1]==1 && !a->count[2]);
                        memcpy(&factor,a->unit+0x44,4);memcpy(times,a->unit+0xa8,24);assert(factor==0.5f && times[0]==25.0 && times[2]==22.5);
                        for(unsigned i=0;i<0x40;i++) assert(!a->unit[i]);
                        for(unsigned i=0x68;i<0xa8;i++) assert(!a->unit[i]);
                        assert(p->portable==0x2002 && p->kind==JOIN_GRAV_PASSIVE_VT && !p->flags && !p->count[0] && join_cap.grav_passive==1);
                        uint32_t unmapped=0;for(unsigned i=0;i<3;i++) unmapped+=f.gravc[i].flags==JOIN_GRAVC_UNMAPPED;
                        assert(unmapped==1 && f.gravc[3].list==1 && f.gravc[3].contact==0x2004);
                        assert(f.gravs[0].kind==JOIN_GRAVS_PENDING && f.gravs[0].portable==0x2003 && f.gravs[1].kind==JOIN_GRAVS_SECTOR &&
                               f.gravs[1].portable==tw_portable[TW_FULL] && f.gravs[1].value==0x4029000000000000ull && !f.gravs[1].flags);
                    }
                    {join_buf b={0};join_tables r11;const char *why;assert(join_package_build(&f,&b) && join_package_parse(b.data,b.size,&r11,&why) &&
                     r11.ngrav==2 && r11.ngravc==4 && r11.ngravs==2 && !memcmp(r11.grav,f.grav,2*sizeof(*f.grav)));join_tables_free(&r11);free(b.data);}
                    /* The joiner: its unit new, the passive one missing, no sources; the admission (report mode: the other sections' counts
                     * are not this test's) makes it the session's. */
                    tj_grav_world=&tw_joiner;tj_grav_units[0]=gunit[1];tj_grav_units[1]=gpass[1];tj_grav_made=0;
                    join_grav_worker_test=gw[1];join_t1_reset();tw_admit(&tw_joiner,&f,JOIN_REPORT);assert(join_admit_world());
                    assert(join_rx.grav_applied==2 && join_rx.grav_created==1 && join_rx.grav_contacts==3 && join_rx.grav_unmapped==1 &&
                           join_rx.grav_buffers==2 && !join_rx.grav_missing && !join_rx.grav_kind && !join_rx.grav_shape);
                    {
                        float factor;double times[3];uintptr_t v[3];uint64_t want[2]={tw_joiner.runtime[TW_CARGO],tw_joiner.runtime[TW_MODULE]};
                        memcpy(&factor,gunit[1]+0x44,4);memcpy(times,gunit[1]+0xa8,24);assert(factor==0.5f && times[0]==25.0 && times[1]==25.7 && times[2]==22.5);
                        qsort(want,2,8,join_u64_cmp);
                        memcpy(v,gunit[1]+0x68,24);assert(v[1]-v[0]==16 && !memcmp((void *)v[0],want,16));
                        memcpy(v,gunit[1]+0x80,24);assert(v[1]-v[0]==8 && *(uint64_t *)v[0]==tw_joiner.runtime[TW_TURRET]);
                        memcpy(&factor,gpass[1]+0x44,4);assert(tj_grav_made==1 && factor==0.25f);
                        memcpy(v,gw[1]+0x48,24);assert(v[1]-v[0]==8 && *(unsigned char **)v[0]==tw_joiner.object[TW_CARGO]);
                        double deadline;memcpy(&deadline,sector[1]+0x448,8);assert(deadline==12.5 && join_rx.gravs_sectors==1 && join_rx.gravs_lists==2);
                    }
                    {char status[4096];assert(join_status(status,sizeof(status)) && strstr(status," grav=2:2:0:0:0:3:1:2:1 gravs=2:0:1:2"));}
                    /* GRAVP: the ship's current pose now the session's; its other view's pose (the session has none) left, counted */
                    assert(!memcmp(gpose[1],gpose[0],0x40) && join_rx.gravp_fixed==1 && join_rx.gravp_extra==1 && !join_rx.gravp_equal &&
                           !join_rx.gravp_missing && !join_rx.gravp_absent);
                    {char status[4096];assert(join_status(status,sizeof(status)) && strstr(status," gravp=1:0:0:0:1"));}
                    {   /* GRAVQ: the post appended to the joiner's empty list (its runtime ID, this process's vtable), the other node counted */
                        uint64_t *n=(uint64_t *)(uintptr_t)qsent[1][0],*e,size;
                        assert(n!=qsent[1] && qsent[1][1]==(uintptr_t)n && n[0]==(uintptr_t)qsent[1] && n[1]==(uintptr_t)qsent[1] && n[2]==3 &&
                               n[3]==tw_joiner.runtime[TW_CARGO] && n[5]==0x403c400000000000ull && !n[6] && !n[7]);
                        memcpy(&size,gw[1]+JOIN_GRAVQ_LIST+8,8);e=(uint64_t *)(uintptr_t)n[4];
                        assert(size==1 && e && e[0]==(uintptr_t)image+JOIN_GRAVQ_EVENT_VT && !e[1] && !e[2]);
                        assert(join_rx.gravq_posted==1 && join_rx.gravq_other==1 && !join_rx.gravq_unmapped && !join_rx.gravq_busy &&
                               !join_rx.gravq_alloc && !join_rx.gravq_had);
                        char status[4096];assert(join_status(status,sizeof(status)) && strstr(status," gravq=1:0:1:0:0:0 gravq_cap="));
                    }
                    {   /* the joiner's groups now each other's neighbours, in its own indices */
                        uintptr_t v[3];
                        memcpy(v,ggrp[1][0]+0x38,24);assert(v[1]-v[0]==8 && *(uint64_t *)v[0]==1);
                        memcpy(v,ggrp[1][1]+0x38,24);assert(v[1]-v[0]==8 && *(uint64_t *)v[0]==0);
                        assert(join_rx.gravn_groups==2 && join_rx.gravn_fixed==2 && !join_rx.gravn_missing && !join_rx.gravn_unfound);
                    }
                    {   /* GRAVN extra (opt-in): a third joiner group the session has no record of (the turret's "zone": a zone the joiner's load
                         * grouped before the session added it), its own neighbour list {0}. Off: group 0 gets the session's list {1} alone;
                         * on: {1, 2}, the extra group's own list untouched, counted. */
                        static __attribute__((aligned(16))) unsigned char g3[3][JOIN_GRAVN_GROUP];static uint64_t nb3[3][4];
                        for(unsigned on=0;on<2;on++) {
                            memset(g3,0,sizeof(g3));memset(nb3,0,sizeof(nb3));
                            *(unsigned char **)g3[0]=tw_joiner.object[TW_MODULE];*(unsigned char **)g3[1]=tw_joiner.object[TW_CARGO];
                            *(unsigned char **)g3[2]=tw_joiner.object[TW_TURRET];nb3[2][0]=0;
                            for(unsigned g=0;g<3;g++) {uintptr_t v[3]={(uintptr_t)nb3[g],(uintptr_t)(nb3[g]+(g==2?1:0)),(uintptr_t)(nb3[g]+4)};memcpy(g3[g]+0x38,v,24);}
                            jsectors[1]=(uintptr_t)g3;jsectors[2]=(uintptr_t)(g3+3);
                            join_gravn_extra_on=(int)on;join_t1_reset();tw_admit(&tw_joiner,&f,JOIN_REPORT);assert(join_admit_world());
                            uintptr_t v[3];
                            memcpy(v,g3[0]+0x38,24);
                            if(on) assert(v[1]-v[0]==16 && ((uint64_t *)v[0])[0]==1 && ((uint64_t *)v[0])[1]==2 && join_gravn_kept==1);
                            else assert(v[1]-v[0]==8 && *(uint64_t *)v[0]==1 && !join_gravn_kept);
                            memcpy(v,g3[1]+0x38,24);assert(v[1]-v[0]==8 && *(uint64_t *)v[0]==0);
                            memcpy(v,g3[2]+0x38,24);assert(v[1]-v[0]==8 && *(uint64_t *)v[0]==0);
                            assert(join_rx.gravn_missing==1 && join_rx.gravn_groups==2);
                            if(on) {char status[8192];assert(join_status(status,sizeof(status)) && strstr(status," gravn_kept=1:1 "));}
                        }
                        join_gravn_extra_on=0;jsectors[1]=(uintptr_t)ggrp[1];jsectors[2]=(uintptr_t)(ggrp[1]+2);
                        printf("join GRAVN extra: a joiner-only zone group kept in its neighbours' rewritten lists (opt-in), untouched itself PASS\n");
                    }
                    /* Strict: a unit the joiner cannot find refuses. */
                    tj_grav_units[0]=NULL;tw_admit(&tw_joiner,&f,JOIN_STRICT);assert(!join_admit_world() && !strcmp(join_rx.detail,"world:grav_missing"));
                    join_tables_free(&f);join_t1_reset();
                    for(unsigned side=0;side<2;side++) {memcpy(sector[side],&sector_vt[side],8);sector[side][0x442]=0;memset(sector[side]+0x448,0,8);}
                    for(unsigned side=0;side<2;side++) {
                        tw_world *w=side?&tw_joiner:&tw_session;
                        for(unsigned o=0;o<TW_OBJECTS;o++) for(unsigned v=0;v<2;v++) memcpy(w->object[o]+JOIN_VIEW_SLOTS+v*0x80u+JOIN_VIEW_POSE,&gslot[side][o][v],8);
                    }
                    join_gravq_cs=qcs;memset(gw,0,sizeof(gw));
                    join_grav_worker_test=NULL;join_grav_lock=lock;join_grav_lookup=lookup;join_grav_passive_new=passive;join_flight_alloc=grav_alloc;
                }
                /* Version 13, the joiner's JobEngine::Init (j1-56/57/59): from its package load to its admission the call gets an empty job
                 * filter (no initial spawns) in place of none or the load's own; every call outside that window passes through. */
                {
                    uint64_t (*fn)(void *,void *)=join_jobinit_fn;join_jobinit_fn=tj_jobinit;int ints[2];struct {int *b,*e;} own={ints,ints+2};
                    /* a DB of two jobs (the header, a root with a right child; the second has +0x469 set) */
                    static __attribute__((aligned(16))) unsigned char db[0x40],node[2][0x500];memset(db,0,sizeof(db));memset(node,0,sizeof(node));
                    *(unsigned char **)(node[0]+0)=db+0x10;*(unsigned char **)(node[0]+0x10)=node[1];*(unsigned char **)(node[1]+0)=node[0];
                    node[1][0x4b1]=1;*(unsigned char **)(db+0x30)=node[0];{uint64_t c=2;memcpy(db+0x38,&c,8);}
                    join_jobdb_test=db;tj_jobinit_flag_seen=&node[1][0x4b1];
                    join_t1_reset();memset(&join_rx,0,sizeof(join_rx));join_rx.t.version=13;join_rx.loaded=1;
                    assert(join_job_init(NULL,NULL)==1 && tj_jobinit_filter==&join_jobinit_none && !join_jobinit_none.begin && !join_jobinit_none.end);
                    assert(join_job_init(NULL,&own)==1 && tj_jobinit_filter==&join_jobinit_none);   /* the load's own filter too (j1-59) */
                    assert(join_jobinit_jobs==2 && join_jobinit_cleared==2 && !tj_jobinit_flag && node[1][0x4b1]==1);   /* off in Init, back after */
                    tj_jobinit_flag_seen=NULL;
                    join_rx.admitted=1;assert(join_job_init(NULL,&own)==1 && tj_jobinit_filter==&own);
                    join_rx.admitted=0;join_rx.t.version=12;assert(join_job_init(NULL,NULL)==1 && !tj_jobinit_filter);
                    assert(tj_jobinit_n==4 && join_jobinit_empty==2 && join_jobinit_calls==4);
                    assert(join_jobinit_cleared==2);   /* nothing cleared outside the window */
                    join_jobdb_test=NULL;join_jobinit_cleared=0;
                    join_jobinit_fn=fn;join_jobinit_calls=join_jobinit_empty=0;memset(&join_rx,0,sizeof(join_rx));join_t1_reset();
                }
                /* The boot event passes held by class (report 1.18): from a version 13 joiner's package load to its admission the
                 * pass's due events of the classes the session had due at T are swapped for stand-ins and kept; the pass always runs. */
                {
                    void (*fn)(void)=join_bootev_fn;join_bootev_fn=tj_bootev;tj_bootev_n=0;
                    join_t1_reset();memset(&join_rx,0,sizeof(join_rx));join_rx.t.version=13;join_rx.loaded=1;
                    static join_evqt_rec ev[3];double t=10.0;memcpy(&join_rx.t.gate.game,&t,8);
                    ev[0]=(join_evqt_rec){0x2001,0x2ba7280u,0x1u,0,0,0};memcpy(&ev[0].time,&t,8);           /* due at T: held */
                    ev[1]=(join_evqt_rec){0x2001,0x2ac1710u,0x20du,1,0,0};memcpy(&ev[1].time,&t,8);          /* rebuilt: not held */
                    t=10.5;ev[2]=(join_evqt_rec){0x2002,0x2ae9af0u,0x2u,0,0,0};memcpy(&ev[2].time,&t,8);     /* later: not held */
                    join_rx.t.evqt=ev;join_rx.t.nevqt=3;join_bootev_classes();
                    assert(join_bootev_nclasses==1 && join_bootev_class[0]==0x2ba7280u);
                    {void **s=calloc(1,0x40);s[0]=(void *)join_bootev_standin_vt;
                     assert(((int (*)(void *))join_bootev_standin_vt[1])(s)==JOIN_BOOTEV_STANDIN_TYPE);((void (*)(void *,int))join_bootev_standin_vt[0])(s,1);}
                    static unsigned char hdr[0x40];memset(hdr,0,sizeof(hdr));join_evq_head_test=hdr;   /* an empty schedule */
                    join_boot_events();assert(tj_bootev_n==1 && join_bootev_held==1 && join_bootev_n==0);   /* nothing due: nothing kept */
                    join_rx.admitted=1;join_boot_events();assert(tj_bootev_n==2 && join_bootev_held==1);
                    join_rx.admitted=0;join_rx.t.version=12;join_boot_events();assert(tj_bootev_n==3 && join_bootev_held==1);
                    join_rx.t.evqt=NULL;join_rx.t.nevqt=0;join_bootev_nclasses=0;join_evq_head_test=NULL;
                    join_bootev_fn=fn;join_bootev_calls=join_bootev_held=0;memset(&join_rx,0,sizeof(join_rx));join_t1_reset();
                }
                /* Version 13, EVQT (j1-50: the joiner's own UpdateEngineParametersEvents up to 8 s from the session's): the session's ship
                 * waits for an UpdateEngineParametersEvent at 30.25 and an UpdateTradeOffersEvent at 31.5 (its tree: the first the root, the
                 * second its right child; the schedule: one node); the joiner's ship for its own UpdateEngineParametersEvent at 33. The
                 * capture keys both; the admission removes the joiner's of that type and posts the session's two at their times. */
                {
                    static uint64_t g[2][8],q[2][8],node[2][6],e[3][6],ev[3][3];
                    unsigned char *ship[2]={tw_session.object[TW_SHIP],tw_joiner.object[TW_SHIP]};uint64_t keep[2];
                    memset(g,0,sizeof(g));memset(q,0,sizeof(q));memset(node,0,sizeof(node));memset(e,0,sizeof(e));memset(ev,0,sizeof(ev));
                    for(unsigned side=0;side<2;side++) {memcpy(&keep[side],ship[side]+0x20,8);uint64_t qa=(uintptr_t)q[side];memcpy(ship[side]+0x20,&qa,8);}
                    ev[0][0]=(uintptr_t)image+0x2baf650u;ev[1][0]=(uintptr_t)image+0x2ac17b8u;ev[2][0]=(uintptr_t)image+0x2baf650u;
                    double t;
                    /* the session: q[0] root e[0] (30.25) with right child e[1] (31.5) */
                    e[0][0]=(uintptr_t)q[0];e[0][2]=(uintptr_t)e[1];t=30.25;memcpy(&e[0][4],&t,8);e[0][5]=(uintptr_t)ev[0];
                    e[1][0]=(uintptr_t)e[0];t=31.5;memcpy(&e[1][4],&t,8);e[1][5]=(uintptr_t)ev[1];
                    q[0][1]=(uintptr_t)e[0];q[0][4]=(uintptr_t)e[0];q[0][5]=2;
                    /* the joiner: q[1] root e[2] (33) */
                    e[2][0]=(uintptr_t)q[1];t=33.0;memcpy(&e[2][4],&t,8);e[2][5]=(uintptr_t)ev[2];q[1][1]=(uintptr_t)e[2];q[1][4]=(uintptr_t)e[2];q[1][5]=1;
                    for(unsigned side=0;side<2;side++) {
                        node[side][0]=(uintptr_t)g[side];t=side?33.0:30.25;memcpy(&node[side][4],&t,8);node[side][5]=(uintptr_t)ship[side];
                        g[side][0]=(uintptr_t)node[side];g[side][3]=(uintptr_t)node[side];g[side][4]=1;
                    }
                    uint32_t (*type)(uint32_t)=join_evqt_type;join_evqt_type=tj_evqt_type;
                    join_tables f;join_evq_head_test=(unsigned char *)g[0];TJ_FLIGHT_CAPTURE(f);
                    assert(f.nevqt==2 && join_evqt_shape(f.evqt,2) && f.evqt[0].portable==0x2001 && f.evqt[0].cls==0x2ac17b8u && f.evqt[0].type==0x215u &&
                           f.evqt[0].index==1 && f.evqt[1].cls==0x2baf650u && f.evqt[1].type==0x20fu && !f.evqt[1].index && !f.evqt[1].flags);
                    {double a,b2;memcpy(&a,&f.evqt[0].time,8);memcpy(&b2,&f.evqt[1].time,8);assert(a==31.5 && b2==30.25);}
                    void (*rm)(void *,int,double)=join_evqt_remove;void (*post)(void *,void *,double,char)=join_evqt_post;
                    join_evqt_remove=tj_evqt_remove;join_evqt_post=tj_evqt_post;tj_evqt_rms=tj_evqt_posts=0;
                    void *(*alloc)(size_t)=join_flight_alloc;join_flight_alloc=tj_big_alloc;tj_big_allocs=0;
                    join_evq_head_test=(unsigned char *)g[1];join_t1_reset();tw_admit(&tw_joiner,&f,JOIN_REPORT);assert(join_admit_world());
                    assert(tj_evqt_rms==1 && tj_evqt_rm_src[0]==ship[1] && tj_evqt_rm_type[0]==0x20f && join_rx.evqt_removed==1);
                    assert(tj_evqt_posts==2 && join_rx.evqt_posted==2 && !join_rx.evqt_missing && !join_rx.evqt_other && !join_rx.evqt_alloc);
                    assert(tj_evqt_post_src[0]==ship[1] && tj_evqt_post_ev[0][0]==(uintptr_t)image+0x2ac17b8u && tj_evqt_post_t[0]==31.5 &&
                           !tj_evqt_post_ev[0][1] && !tj_evqt_post_ev[0][2] && tj_evqt_post_ev[1][0]==(uintptr_t)image+0x2baf650u && tj_evqt_post_t[1]==30.25);
                    {char status[4096];assert(join_status(status,sizeof(status)) && strstr(status," evqt=1:2:0:0:0 "));}
                    join_flight_alloc=alloc;join_evqt_remove=rm;join_evqt_post=post;join_evqt_type=type;join_evq_head_test=NULL;
                    for(unsigned side=0;side<2;side++) memcpy(ship[side]+0x20,&keep[side],8);
                    join_tables_free(&f);join_t1_reset();
                }
                /* Version 12, JOBE (j1-45: the joiner's load ran a full job pass, the session's ran at 634 from its cursor): the session's
                 * engine (last run 23.25, a heap cursor of 20 bytes), the joiner's (never run, an empty cursor); the admission writes the
                 * time and assigns the cursor through the engine's std::string assign (a stub here). */
                {
                    static __attribute__((aligned(16))) unsigned char eng[2][0x68];static char heap[32];
                    memset(eng,0,sizeof(eng));
                    for(unsigned side=0;side<2;side++) *(void **)eng[side]=image+JOIN_JOBE_VT;
                    eng[0][8]=1;eng[0][9]=1;{double t=23.25;memcpy(eng[0]+0x10,&t,8);}
                    memcpy(heap,"argon_scout_patrol_s",20);*(char **)(eng[0]+0x18)=heap;{uint64_t v[2]={20,31};memcpy(eng[0]+0x28,v,16);}
                    eng[1][8]=1;{double t=-1.0;memcpy(eng[1]+0x10,&t,8);uint64_t v[2]={0,15};memcpy(eng[1]+0x28,v,16);}
                    static unsigned char sw;sw=1;join_jobe_switch_test=&sw;
                    join_tables f;join_jobe_engine_test=eng[0];TJ_FLIGHT_CAPTURE(f);
                    assert(f.njobe==1 && join_jobe_shape(f.jobe,1) && f.jobe[0].flags==(JOIN_JOBE_ENABLED|JOIN_JOBE_RUNNING|JOIN_JOBE_GATE) && f.jobe[0].len==20 &&
                           !memcmp(f.jobe[0].cursor,"argon_scout_patrol_s",20) && f.jobe[0].last==0x4037400000000000ull);
                    void *(*assign)(void *,const char *,size_t)=join_jobe_assign;join_jobe_assign=tj_jobe_assign;
                    /* the joiner's load: every import turns the switch off (Init passes nothing), the admission turns it back on */
                    /* (the joiner's engine still running from the main menu's game: the hold stops it too) */
                    eng[1][9]=1;join_jobe_engine_test=eng[1];
                    join_t1_reset();tw_admit(&tw_joiner,&f,JOIN_REPORT);join_jobe_hold();join_jobe_hold();
                    assert(!sw && !eng[1][9] && join_rx.jobe_held==0x101);
                    assert(join_admit_world() && sw==1 && eng[1][8]==1 && eng[1][9]==1);
                    double t;memcpy(&t,eng[1]+0x10,8);
                    assert(t==23.25 && join_rx.jobe_applied==1 && !join_rx.jobe_missing && join_rx.jobe_prev_last==0xbff0000000000000ull && !join_rx.jobe_prev_len &&
                           tj_jobe_n==20 && !memcmp(tj_jobe_text,"argon_scout_patrol_s",20) && tj_jobe_at==eng[1]+0x18);
                    {char status[4096];assert(join_status(status,sizeof(status)) && strstr(status," jobe=1:0:bff0000000000000:0:257 "));}
                    join_jobe_engine_test=NULL;join_t1_reset();tw_admit(&tw_joiner,&f,JOIN_REPORT);assert(join_admit_world() && join_rx.jobe_missing==1);
                    join_jobe_assign=assign;join_jobe_switch_test=NULL;join_tables_free(&f);join_t1_reset();
                }
                /* Version 12, the spline models (j1-27: the joiner's route empty, the ship brakes): the session's model with a route of
                 * two entries (the first owning a CompositeSpline of one segment and three pairs, the second none; the deque's offset 6
                 * of a map of 8) and its first embedded spline holding two segments; the joiner's as the load leaves it. The capture
                 * keys it, the admission rebuilds the route in this process (offset 0, blocks and the spline from the allocator) and
                 * writes the carried bytes. */
                {
                    static __attribute__((aligned(16))) unsigned char sm[2][JOIN_SPLM_BYTES],sblk[2][0x50],scs[0x50],sseg[JOIN_FLIGHT_SEGMENT],eseg[2*JOIN_FLIGHT_SEGMENT];
                    static uintptr_t smap[8];static uint64_t spairs[3];
                    TJ_SOLVER_WORLDS();
                    unsigned char *agent[2]={tw_session.agent,tw_joiner.agent},*saved_model[2];
                    for(unsigned side=0;side<2;side++) {
                        unsigned char *m=sm[side];
                        memcpy(&saved_model[side],agent[side]+JOIN_AGENT_MODEL,8);
                        for(unsigned i=0;i<JOIN_SPLM_BYTES;i++) m[i]=side?0:(unsigned char)(i*11+5);
                        *(void **)m=image+JOIN_SPLINE_VTABLE;*(unsigned char **)(m+JOIN_FLIGHT_OWNER)=agent[side];
                        memset(m+0x70,0,0x50);memset(m+0x428,0,0x28);
                        for(unsigned l=0;l<2;l++) {memset(m+JOIN_SPLM_EMB0+l*0x50+0x10,0,0x30);*(void **)(m+JOIN_SPLM_EMB0+l*0x50)=image+JOIN_FLIGHT_SPLINE_VT;}
                        *(unsigned char **)(agent[side]+JOIN_AGENT_MODEL)=m;
                    }
                    /* the session's route and splines */
                    for(unsigned k=0;k<2;k++) {unsigned char *g=eseg+k*JOIN_FLIGHT_SEGMENT;memset(g,0x21*(k+1),JOIN_FLIGHT_SEGMENT);*(void **)(g+JOIN_FLIGHT_BEZIER_AT)=image+JOIN_FLIGHT_BEZIER_VT;}
                    {uintptr_t v[3]={(uintptr_t)eseg,(uintptr_t)eseg+2*JOIN_FLIGHT_SEGMENT,(uintptr_t)eseg+2*JOIN_FLIGHT_SEGMENT};memcpy(sm[0]+JOIN_SPLM_EMB0+0x10,v,24);}
                    memset(sseg,0x33,sizeof(sseg));*(void **)(sseg+JOIN_FLIGHT_BEZIER_AT)=image+JOIN_FLIGHT_BEZIER_VT;
                    for(unsigned k=0;k<3;k++) spairs[k]=0x4000000000000000ull+k;
                    memset(scs,0,sizeof(scs));*(void **)scs=image+JOIN_FLIGHT_SPLINE_VT;{float f=1.0f;memcpy(scs+8,&f,4);}
                    {uintptr_t v[3]={(uintptr_t)sseg,(uintptr_t)sseg+JOIN_FLIGHT_SEGMENT,(uintptr_t)sseg+JOIN_FLIGHT_SEGMENT};memcpy(scs+0x10,v,24);}
                    {uintptr_t v[3]={(uintptr_t)spairs,(uintptr_t)(spairs+3),(uintptr_t)(spairs+3)};memcpy(scs+0x28,v,24);}
                    {float len=1234.5f;memcpy(scs+0x40,&len,4);}
                    for(unsigned k=0;k<2;k++) {for(unsigned i=0;i<0x50;i++) sblk[k][i]=(unsigned char)(i*3+k+9);}
                    *(unsigned char **)sblk[0]=scs;memset(sblk[1],0,8);
                    memset(smap,0,sizeof(smap));smap[6]=(uintptr_t)sblk[0];smap[7]=(uintptr_t)sblk[1];
                    {uint64_t d[5]={0x1234,(uint64_t)(uintptr_t)smap,8,6,2};memcpy(sm[0]+0x70,d,40);}
                    TJ_FLIGHT_CAPTURE(f);
                    assert(f.nsplm==1 && f.splm[0].portable==0x2001 && !f.splm[0].flags && f.splm[0].entries[0]==2 && !f.splm[0].entries[1] &&
                           f.splm[0].segments[0]==2 && !f.splm[0].pairs[0] && f.nsplme==2 && f.nsplmseg==4 &&
                           join_splm_shape(f.splm,f.nsplm,f.splme,f.nsplme,f.splmseg,f.nsplmseg));
                    assert(f.splme[0].flags==JOIN_SPLME_SPLINE && f.splme[0].segments==1 && f.splme[0].pairs==3 && !f.splme[1].flags &&
                           f.splme[0].entry[0x10]==sblk[0][0x10] && !f.splme[0].entry[8] && f.splme[1].entry[0x20]==sblk[1][0x20]);
                    for(unsigned i=0;i<8;i++) assert(!f.splme[0].entry[i] && !f.splme[0].spline[i]);
                    {float len;memcpy(&len,f.splme[0].spline+0x40,4);assert(len==1234.5f);}
                    for(unsigned k=0;k<JOIN_SPLM_SKIPS;k++) for(unsigned i=0;i<join_splm_skip[k].len;i++) assert(!f.splm[0].model[join_splm_skip[k].at+i]);
                    assert(f.splm[0].model[0x300]==sm[0][0x300]);
                    {join_buf b={0};join_tables r12;const char *why;assert(join_package_build(&f,&b) && join_package_parse(b.data,b.size,&r12,&why) &&
                     r12.nsplm==1 && r12.nsplme==2 && r12.nsplmseg==4);join_tables_free(&r12);free(b.data);}
                    /* the joiner: an empty route; the admission rebuilds it from a larger pool */
                    void *(*alloc)(size_t)=join_flight_alloc;join_flight_alloc=tj_big_alloc;tj_big_allocs=0;
                    join_t1_reset();tw_admit(&tw_joiner,&f,JOIN_REPORT);assert(join_admit_world());
                    assert(join_rx.splm_applied==1 && join_rx.splm_entries==2 && !join_rx.splm_missing && !join_rx.splm_kind && !join_rx.splm_shape &&
                           !join_rx.splm_alloc && !join_rx.splm_other);
                    {
                        unsigned char *m=sm[1];uint64_t d[5];memcpy(d,m+0x70,40);
                        assert(d[1] && d[2]==8 && d[3]==0 && d[4]==2);
                        unsigned char *b0=*(unsigned char **)(uintptr_t)d[1],*b1=((unsigned char **)(uintptr_t)d[1])[1],*cs=*(unsigned char **)b0;
                        assert(b0 && b1 && cs && !*(void **)b1 && !memcmp(b0+0x10,sblk[0]+0x10,0x40) && !memcmp(b1+0x10,sblk[1]+0x10,0x40));
                        assert(*(void **)cs==image+JOIN_FLIGHT_SPLINE_VT);
                        uintptr_t v[3];memcpy(v,cs+0x10,24);assert(v[1]-v[0]==JOIN_FLIGHT_SEGMENT && !memcmp((void *)v[0],sseg,JOIN_FLIGHT_SEGMENT));
                        memcpy(v,cs+0x28,24);assert(v[1]-v[0]==24 && !memcmp((void *)v[0],spairs,24));
                        memcpy(v,m+JOIN_SPLM_EMB0+0x10,24);assert(v[1]-v[0]==2*JOIN_FLIGHT_SEGMENT && !memcmp((void *)v[0],eseg,2*JOIN_FLIGHT_SEGMENT));
                        assert(m[0x300]==sm[0][0x300] && m[0xc4]==sm[0][0xc4] && !m[0x210] && *(unsigned char **)(m+JOIN_FLIGHT_OWNER)==tw_joiner.agent);
                    }
                    {char status[4096];assert(join_status(status,sizeof(status)) && strstr(status," splm=1:1:0:0:0:0:0:2"));}
                    /* a joiner route not empty: refused for that model (shape), strict stops */
                    join_t1_reset();tw_admit(&tw_joiner,&f,JOIN_REPORT);assert(join_admit_world() && join_rx.splm_shape==1 && !join_rx.splm_applied);
                    tw_admit(&tw_joiner,&f,JOIN_STRICT);assert(!join_admit_world() && !strcmp(join_rx.detail,"world:splm_shape"));
                    join_flight_alloc=alloc;join_tables_free(&f);join_t1_reset();
                    for(unsigned side=0;side<2;side++) memcpy(agent[side]+JOIN_AGENT_MODEL,&saved_model[side],8);
                }

                join_solver_prepare=prepare;
#undef TJ_SOLVER_WORLDS
            }
#undef TJ_STEER_WORLDS
        }
#undef TJ_FLIGHT_WORLDS
#undef TJ_FLIGHT_CAPTURE
        tw_build(&tw_session,0);tw_build(&tw_joiner,1);join_flight_alloc=alloc;memset(&join_rx,0,sizeof(join_rx));
    }
    /* Version 8, DOCK (j1-22: 48 ships docking differ at T+1, their start times and vectors rounded by the save): the odd object
     * docking on both sides, the session's start 13.950000000001 and exact lanes, the joiner's as its load left it (13.95, rounded
     * lanes, its own w lanes). The admission writes the census's fields: the census string (lockstep_dock.h) is then equal, the w
     * lanes stay the joiner's. Another controller kind on the joiner, a controller another object owns: dock kind, dock shape. */
    {
        static __attribute__((aligned(16))) unsigned char dc[2][0x140];char census[2][512];
        for(unsigned side=0;side<2;side++) {
            tw_world *w=side?&tw_joiner:&tw_session;unsigned char *c=dc[side];memset(c,0,sizeof(dc[side]));
            *(void **)c=image+DOCK_CONTROLLER_VTABLE;*(unsigned char **)(c+0x70)=w->object[TW_ODD];
            for(unsigned k=0;k<JOIN_DOCK_VECTORS;k++) for(unsigned l=0;l<4;l++) {
                float f=l==3?(side?9.0f:-9.0f):side?100.1235f+(float)(4*k+l):100.123456f+(float)(4*k+l);memcpy(c+join_dock_at(k)+4*l,&f,4);
            }
            double start=side?13.95:13.950000000001;uint32_t animation=2;memcpy(c+0x60,&start,8);memcpy(c+0x120,&animation,4);c[0x124]=1;
            *(unsigned char **)(w->object[TW_ODD]+JOIN_CONTROLLER)=c;
        }
        join_tables d;memset(&d,0,sizeof(d));tw_find_world=&tw_session;join_world_map=&tw_session;tw_phase(0);
        assert(tw_ids(&d) && join_capture_world(&d,30.0));d.version=JOIN_PACKAGE_VERSION;
        assert(d.ndock==1 && d.dock[0].portable==0x2007 && d.dock[0].kind==DOCK_CONTROLLER_VTABLE && d.dock[0].time==tj_bits(13.950000000001) &&
               d.dock[0].animation==2 && d.dock[0].option[0]==1 && !d.dock[0].option[1] && d.npose==2 && join_dock_shape(d.dock,1));
        {float x;memcpy(&x,&d.dock[0].xyz[3*13+2],4);assert(x==100.123456f+54.0f);}   /* vector 13 z: +0x110+8 */
        {join_buf b={0};join_tables r8;const char *why;assert(join_package_build(&d,&b) && join_package_parse(b.data,b.size,&r8,&why) &&
         r8.ndock==1 && !memcmp(r8.dock,d.dock,sizeof(*d.dock)));join_tables_free(&r8);free(b.data);}
        assert(dock_controller_state(dc[0],tw_session.object[TW_ODD],census[0]) && dock_controller_state(dc[1],tw_joiner.object[TW_ODD],census[1]) &&
               strcmp(census[0],census[1]));
        tw_tables_reset(1);tw_admit(&tw_joiner,&d,JOIN_STRICT);assert(join_admit_world());
        assert(join_rx.dock_applied==1 && join_rx.dock_fixed==1 && !join_rx.dock_missing && !join_rx.dock_kind && !join_rx.dock_shape);
        assert(dock_controller_state(dc[1],tw_joiner.object[TW_ODD],census[1]) && !strcmp(census[0],census[1]));
        {float w;memcpy(&w,dc[1]+join_dock_at(5)+12,4);assert(w==9.0f);}
        tw_admit(&tw_joiner,&d,JOIN_REPORT);assert(join_admit_world() && join_rx.dock_applied==1 && !join_rx.dock_fixed);
        *(void **)dc[1]=image+UNDOCK_CONTROLLER_VTABLE;tw_admit(&tw_joiner,&d,JOIN_REPORT);assert(join_admit_world() && join_rx.dock_kind==1);
        {int row=0;for(unsigned i=0;i<join_rx.nrows;i++) row|=strstr(join_rx.rows[i],"dock side=joiner id=0000000000002007 why=kind kind=2c3fdc0/2c3e568")!=NULL;assert(row);}
        tw_admit(&tw_joiner,&d,JOIN_STRICT);assert(!join_admit_world() && !strcmp(join_rx.detail,"world:dock_kind"));
        *(void **)dc[1]=image+DOCK_CONTROLLER_VTABLE;*(unsigned char **)(dc[1]+0x70)=tw_joiner.object[TW_SHIP];
        tw_admit(&tw_joiner,&d,JOIN_REPORT);assert(join_admit_world() && join_rx.dock_shape==1 && !join_rx.dock_applied);
        tw_admit(&tw_joiner,&d,JOIN_STRICT);assert(!join_admit_world() && !strcmp(join_rx.detail,"world:dock_shape"));
        /* An undock controller on the session: eight vectors, one option; the joiner's undock controller written alike. */
        for(unsigned side=0;side<2;side++) {
            tw_world *w=side?&tw_joiner:&tw_session;unsigned char *c=dc[side];*(void **)c=image+UNDOCK_CONTROLLER_VTABLE;
            *(unsigned char **)(c+0x70)=w->object[TW_ODD];uint32_t animation=side?4u:5u;memcpy(c+0xc0,&animation,4);c[0xc4]=1;
        }
        join_tables_free(&d);memset(&d,0,sizeof(d));tw_find_world=&tw_session;join_world_map=&tw_session;tw_phase(0);
        assert(tw_ids(&d) && join_capture_world(&d,30.0));d.version=JOIN_PACKAGE_VERSION;
        assert(d.ndock==1 && d.dock[0].kind==UNDOCK_CONTROLLER_VTABLE && d.dock[0].animation==5 && !d.dock[0].xyz[3*8] && join_dock_shape(d.dock,1));
        tw_admit(&tw_joiner,&d,JOIN_STRICT);assert(join_admit_world() && join_rx.dock_fixed==1);
        assert(dock_controller_state(dc[0],tw_session.object[TW_ODD],census[0]) && dock_controller_state(dc[1],tw_joiner.object[TW_ODD],census[1]) &&
               !strcmp(census[0],census[1]));
        join_tables_free(&d);tw_build(&tw_session,0);tw_build(&tw_joiner,1);memset(&join_rx,0,sizeof(join_rx));
    }
    /* Version 2 and 1 packages: HEALTH and POSE only, nothing else placed; nothing at all. */
    {join_tables v2=r;v2.version=2;tw_build(&tw_joiner,1);tw_tables_reset(1);tw_admit(&tw_joiner,&v2,JOIN_REPORT);
     assert(join_admit_world() && join_rx.health_applied==5 && !join_rx.nav_applied && !join_rx.attn_applied && !join_rx.shield_applied &&
            !join_rx.keys_applied[JOIN_KEYS_ATTENTION_ALL]);}
    {join_tables v1;memset(&v1,0,sizeof(v1));v1.version=1;tw_admit(&tw_joiner,&v1,JOIN_REPORT);assert(join_admit_world() && !join_rx.health_applied);}
    memset(&join_rx,0,sizeof(join_rx));join_tables_free(&r);join_tables_free(&t);
    tw_tables_save(1);actor_ids[0]=tw_actor_id;snapshot_configured=tw_snapshot_configured;snapshot_actors[0].saved=tw_snapshot_saved;
    join_world_find=find;join_world_ids=ids;join_world_map=NULL;world_class_count=classes;join_key=key;
    memcpy(image+JOIN_PATH_MANAGER,&saved_manager,8);join_path_prime=prime;join_route_clear=route_clear;join_route_push=route_push;
    puts("hot join world side table: health (hull, shield value and changed time, the census's full form, shared groups), poses, "
         "shield parameters, navigation banks by role against the joiner's own selectors (the census tuple equal after the "
         "admission), attention levels and dues, the first-call keys, a linear flight model's leg, step clock and spline (into "
         "buffers of the engine's allocator), its Agent's step inputs and its waypoint entries (rebuilt through the engine's vector "
         "clear and push_back, the references by portable ID) captured, through the package and its portable digest, restored into a "
         "world loaded at other addresses; the step of T+1 probed on both sides; missing, shape, conflict, unarmed, actor, "
         "created_extra, flight kind/shape/alloc/other/word, agent state/shape, route shape/unmapped/other, the Agent's flight parameter "
         "slots by role (param unmapped/shape) and a dock and undock controller (the census string equal after the admission; dock "
         "kind/shape) and version 1/2 refusals PASS");
}
/* BODY: the Jolt body behind an XPhys record (lockstep_join_body.h), on owned memory: the chain body_state reads, a moving and a
 * static body, the checks that refuse, and a read record round trip. */
static void test_join_body(void) {
    static unsigned char object[0x300],record[0x80],xbody[0x10],scene[0x28],system_[0x1c0],iface[0x10],manager[0x18],body[0x78],motion[0x80];
    static unsigned char *bodies[4];
    memset(object,0,sizeof(object));memset(record,0,sizeof(record));memset(body,0,sizeof(body));memset(motion,0,sizeof(motion));
    unsigned char *p;uint32_t id=2;uint64_t count=4;
    p=record;memcpy(object+JOIN_BODY_RECORD,&p,8);p=object;memcpy(record,&p,8);p=xbody;memcpy(record+0x10,&p,8);memcpy(xbody+8,&id,4);
    p=scene;memcpy(record+0x18,&p,8);p=system_;memcpy(scene+0x20,&p,8);p=iface;memcpy(system_+0x1b8,&p,8);p=manager;memcpy(iface+8,&p,8);
    memcpy(manager,&count,8);p=(unsigned char*)bodies;memcpy(manager+0x10,&p,8);bodies[2]=body;memcpy(body+0x70,&id,4);
    p=record;memcpy(body+0x50,&p,8);p=motion;memcpy(body+0x48,&p,8);
    for(unsigned i=0;i<32;i++) {body[i]=(unsigned char)(i+1);record[0x40+i]=(unsigned char)(0x40+i);record[0x60+i]=(unsigned char)(0x80+i);}
    for(unsigned i=0;i<JOIN_BODY_MOTION_BYTES;i++) motion[i]=(unsigned char)(0xa0+i);
    uint32_t flags=0x11;memcpy(record+0x38,&flags,4);
    unsigned char *r,*b,*m;join_body_rec rec;
    assert(join_body_locate(object,0,&r,&b,&m) && r==record && b==body && m==motion && !join_body_locate(object,1,&r,&b,&m));
    assert(join_body_read(object,0,0x2001,&rec) && rec.portable==0x2001 && rec.which==0 && rec.flags==0x11 && rec.pad==1 &&
           !memcmp(rec.pose,body,32) && !memcmp(rec.motion,motion,JOIN_BODY_MOTION_BYTES) && !memcmp(rec.pre,record+0x40,32) && !memcmp(rec.delta,record+0x60,32));
    /* A static body (no motion properties) reads without motion; each broken link refuses. */
    p=NULL;memcpy(body+0x48,&p,8);assert(join_body_read(object,0,0x2001,&rec) && !rec.pad);p=motion;memcpy(body+0x48,&p,8);
    p=object+8;memcpy(record,&p,8);assert(!join_body_locate(object,0,&r,&b,&m));p=object;memcpy(record,&p,8);
    id=3;memcpy(body+0x70,&id,4);assert(!join_body_locate(object,0,&r,&b,&m));id=2;memcpy(body+0x70,&id,4);
    p=record+8;memcpy(body+0x50,&p,8);assert(!join_body_locate(object,0,&r,&b,&m));p=record;memcpy(body+0x50,&p,8);
    count=2;memcpy(manager,&count,8);assert(!join_body_locate(object,0,&r,&b,&m));count=4;memcpy(manager,&count,8);
    id=0xffffffffu;memcpy(xbody+8,&id,4);assert(!join_body_locate(object,0,&r,&b,&m));id=2;memcpy(xbody+8,&id,4);
    assert(join_body_locate(object,0,&r,&b,&m));
    /* Records sort by (portable, which), a duplicate refuses. */
    join_tables t;memset(&t,0,sizeof(t));uint32_t cap=0;
    join_body_rec a=rec,c=rec;a.portable=0x3001;c.which=1;
    assert(JOIN_PUSH(t.body,t.nbody,cap,a) && JOIN_PUSH(t.body,t.nbody,cap,c) && JOIN_PUSH(t.body,t.nbody,cap,rec) && join_body_sort(&t));
    assert(t.body[0].portable==0x2001 && !t.body[0].which && t.body[1].which==1 && t.body[2].portable==0x3001 && join_body_cap==3);
    assert(JOIN_PUSH(t.body,t.nbody,cap,rec) && !join_body_sort(&t));
    free(t.body);join_body_cap=0;
    {   /* JOBS: the lengths of a job state's ship vectors (a broken vector reads 65535) */
        static unsigned char state[0x50];static uint64_t ships[3];uintptr_t v[2]={(uintptr_t)ships,(uintptr_t)(ships+3)};
        memset(state,0,sizeof(state));memcpy(state+JOIN_JOBS_WAITING,v,16);v[1]=(uintptr_t)ships;memcpy(state+JOIN_JOBS_ACTIVE,v,16);
        assert(join_jobs_ships(state,JOIN_JOBS_WAITING)==3u && !join_jobs_ships(state,JOIN_JOBS_ACTIVE));
        v[1]=(uintptr_t)ships+4u;memcpy(state+JOIN_JOBS_ACTIVE,v,16);assert(join_jobs_ships(state,JOIN_JOBS_ACTIVE)==0xffffu);
    }
    printf("hot join BODY: the Jolt body behind a record, moving and static, refusals, order; JOBS ship vectors PASS\n");
}
/* RESV (lockstep_join_world.h): a zone's reservation tree walked in order as 0x835aa9 .. 0x835aef takes it, a count that does not
 * match refused; the capture's records by the zone's and the owner's portable IDs and the index; the admission writes the
 * session's box and expiry into the joiner's node with the same key, counts missing and extra nodes. */
static _Alignas(16) unsigned char tresv_zone[0x500],tresv_node[4][0xb0];
static uint64_t tresv_key(uint64_t runtime,unsigned *klass) {*klass=0;return runtime?runtime|0x100000ull:0;}
static void *tresv_find(void *map,uint64_t runtime,unsigned mode) {(void)map;(void)mode;return runtime==0x77ull?tresv_zone:NULL;}
static int tresv_visit(unsigned char *node,void *p) {uint64_t *seen=(uint64_t*)p,index=0;memcpy(&index,node+JOIN_RESV_INDEX,8);seen[++seen[0]]=index;return 1;}
static void tresv_link(unsigned char *n,unsigned char *parent,unsigned char *left,unsigned char *right) {memcpy(n,&parent,8);memcpy(n+8,&left,8);memcpy(n+0x10,&right,8);}
static void tresv_build(void) {
    memset(tresv_zone,0,sizeof(tresv_zone));memset(tresv_node,0,sizeof(tresv_node));
    uintptr_t vt=(uintptr_t)image+JOIN_ZONE_VTABLE;uint64_t runtime=0x77ull;memcpy(tresv_zone,&vt,8);memcpy(tresv_zone+8,&runtime,8);
    unsigned char *h=tresv_zone+JOIN_RESV_TREE,*a=tresv_node[0],*b=tresv_node[1],*c=tresv_node[2];
    /* root b (index 2), its left a (1), its right c (3); the header's +8 the root */
    tresv_link(b,h,a,c);tresv_link(a,b,NULL,NULL);tresv_link(c,b,NULL,NULL);memcpy(h+8,&b,8);
    memcpy(tresv_zone+JOIN_RESV_BEGIN,&a,8);uint64_t count=3;memcpy(tresv_zone+JOIN_RESV_COUNT,&count,8);
    for(unsigned i=0;i<3;i++) {
        uint64_t owner=0x500ull+i,index=i+1u;double expiry=40.0+i;unsigned char *n=tresv_node[i==0?0:i==1?1:2];
        memcpy(n+JOIN_RESV_OWNER,&owner,8);memcpy(n+JOIN_RESV_INDEX,&index,8);memcpy(n+JOIN_RESV_EXPIRY,&expiry,8);
        for(unsigned k=0;k<JOIN_RESV_BOX_BYTES;k++) n[JOIN_RESV_BOX+k]=(unsigned char)(0x10*i+k);
    }
}
/* PCSV (lockstep_join_world.h): a PlayerController's own solver block (+0x3b0) captured as SOLVER keeps an Agent's (a pointer into
 * itself relative, an image pointer as its RVA, an address of the saver's memory marked) and admitted into the joiner's controller
 * of the same ship (the pointers absolute, the joiner's own value at the mark, nothing outside the block touched); a ship without
 * a PlayerController and one the joiner lacks count missing, a record of another kind shape. */
static _Alignas(16) unsigned char tpcsv_obj[2][0x300],tpcsv_ctl[2][JOIN_PCSV_AT+JOIN_SOLVER_BYTES+0x40];
static uint64_t tpcsv_cell;
static int tpcsv_mapped(uint64_t q) {return q==tpcsv_cell;}
static void *tpcsv_find(void *map,uint64_t runtime,unsigned mode) {(void)map;(void)mode;return runtime==0x81ull?tpcsv_obj[1]:NULL;}
static void test_join_pcsv(void) {
    for(unsigned side=0;side<2;side++) {
        unsigned char *c=tpcsv_ctl[side],*sv=c+JOIN_PCSV_AT;uintptr_t vt=(uintptr_t)image+PSLOT_VTABLE;
        const uint64_t runtime=side?0x81ull:0x82ull;
        memset(tpcsv_obj[side],0,sizeof(tpcsv_obj[side]));memcpy(tpcsv_obj[side]+8,&runtime,8);memcpy(tpcsv_obj[side]+JOIN_CONTROLLER,&c,8);
        for(unsigned i=0;i<sizeof(tpcsv_ctl[side]);i++) c[i]=(unsigned char)(i*13u+(side?71u:3u));
        memcpy(c,&vt,8);
        *(uintptr_t *)(sv+0x40)=(uintptr_t)sv+0x100;                  /* into the block itself */
        *(uintptr_t *)(sv+0x48)=(uintptr_t)image+0x2c1f0f8;           /* a vtable */
    }
    assert(join_pcsv_controller(tpcsv_obj[0])==tpcsv_ctl[0] && !join_pcsv_controller(NULL));
    int (*mapped)(uint64_t)=join_mapped;join_mapped=tpcsv_mapped;tpcsv_cell=0x00007ff012345670ull;
    memcpy(tpcsv_ctl[0]+JOIN_PCSV_AT+0x81c8,&tpcsv_cell,8);
    join_tables t;memset(&t,0,sizeof(t));join_pcsv_capn=0;
    assert(join_capture_pcsv(&t,tpcsv_ctl[0],0x2082ull) && t.npcsv==1);
    const join_solver_rec *r=&t.pcsv[0];uint64_t q;
    assert(r->portable==0x2082ull && r->kind==PSLOT_VTABLE && !r->flags && !r->reserved && r->pointers==2 &&
           r->pointer[0]==((0x40u/8u)<<1) && r->pointer[1]==((0x48u/8u)<<1|1u) && !r->pointer[2] && join_solver_shape(r,1));
    memcpy(&q,r->data+0x40,8);assert(q==0x100);memcpy(&q,r->data+0x48,8);assert(q==0x2c1f0f8);
    memcpy(&q,r->data+0x81c8,8);assert(q==JOIN_SOLVER_HEAP);
    assert(r->data[0x10]==tpcsv_ctl[0][JOIN_PCSV_AT+0x10]);
    /* the admission: the joiner's ship 0x81 (portable 0x2082) with its own controller; a second record for a ship it lacks */
    join_find_fn find=join_world_find;void *map=join_world_map;join_world_find=tpcsv_find;join_world_map=tpcsv_obj;
    static join_solver_rec recs[2];memcpy(&recs[0],r,sizeof(*r));memcpy(&recs[1],r,sizeof(*r));recs[1].portable=0x2083ull;
    join_tables s,mine;memset(&s,0,sizeof(s));memset(&mine,0,sizeof(mine));s.version=JOIN_PACKAGE_VERSION;s.pcsv=recs;s.npcsv=2;
    join_id_rec mine_ids[1]={{0x81ull,0x2082ull,0,0}};mine.ids=mine_ids;mine.nids=1;
    unsigned char *a=tpcsv_ctl[0]+JOIN_PCSV_AT,*b=tpcsv_ctl[1]+JOIN_PCSV_AT;
    uint64_t own;memcpy(&own,b+0x81c8,8);
    const unsigned char before=tpcsv_ctl[1][JOIN_PCSV_AT-1],after=b[JOIN_SOLVER_BYTES];
    join_pcsv_on=0;join_admit_pcsv(&s,&mine);assert(!join_pcsv_written && !join_pcsv_equal && !join_pcsv_missing && !join_pcsv_shape);
    join_pcsv_on=1;join_admit_pcsv(&s,&mine);
    assert(join_pcsv_written==1 && !join_pcsv_equal && join_pcsv_missing==1 && !join_pcsv_shape);
    for(unsigned i=0;i<JOIN_SOLVER_BYTES;i+=8) {
        if(i==0x40) assert(*(uintptr_t *)(b+i)==(uintptr_t)b+0x100);
        else if(i==0x48) assert(*(uintptr_t *)(b+i)==(uintptr_t)image+0x2c1f0f8);
        else if(i==0x81c8) {memcpy(&q,b+i,8);assert(q==own);}
        else assert(!memcmp(a+i,b+i,8));
    }
    assert(tpcsv_ctl[1][JOIN_PCSV_AT-1]==before && b[JOIN_SOLVER_BYTES]==after);   /* the controller outside the block untouched */
    {char status[8192];assert(join_status(status,sizeof(status)) && strstr(status," pcsv=1:1:0:1:0 "));}
    join_admit_pcsv(&s,&mine);assert(!join_pcsv_written && join_pcsv_equal==1 && join_pcsv_missing==1);   /* again: nothing left */
    {   /* the joiner's ship has another controller: missing, nothing written */
        uintptr_t vt=(uintptr_t)image+JOIN_NAV_VTABLE;memcpy(tpcsv_ctl[1],&vt,8);b[0x10]^=1;
        join_admit_pcsv(&s,&mine);assert(join_pcsv_missing==2 && !join_pcsv_written && !join_pcsv_equal && b[0x10]!=a[0x10]);
        vt=(uintptr_t)image+PSLOT_VTABLE;memcpy(tpcsv_ctl[1],&vt,8);
    }
    recs[0].kind=JOIN_STEER_VTABLE;join_admit_pcsv(&s,&mine);assert(join_pcsv_shape==1 && !join_pcsv_written && b[0x10]!=a[0x10]);
    recs[0].kind=PSLOT_VTABLE;join_admit_pcsv(&s,&mine);assert(join_pcsv_written==1 && b[0x10]==a[0x10]);
    join_pcsv_on=0;join_world_find=find;join_world_map=map;join_mapped=mapped;free(t.pcsv);
    printf("join PCSV: a PlayerController's solver block captured as SOLVER (relative, RVA, heap mark), admitted into the joiner's controller (absolute, own heap value, nothing else), missing and shape PASS\n");
}
static void test_join_resv(void) {
    tresv_build();
    uint64_t seen[8]={0};assert(join_resv_walk(tresv_zone,tresv_visit,seen)==3 && seen[0]==3 && seen[1]==1 && seen[2]==2 && seen[3]==3);
    {uint64_t two=2;memcpy(tresv_zone+JOIN_RESV_COUNT,&two,8);assert(join_resv_walk(tresv_zone,NULL,NULL)==-1);two=3;memcpy(tresv_zone+JOIN_RESV_COUNT,&two,8);}
    assert(join_resv_zone(tresv_zone) && !join_resv_zone(tresv_node[0]));
    /* the capture: three records by (zone, owner, index), the owners' portable keys */
    uint64_t (*key)(uint64_t,unsigned *)=join_key;join_find_fn find=join_world_find;void *map=join_world_map;
    join_key=tresv_key;join_world_find=tresv_find;join_world_map=tresv_zone;
    join_tables t;memset(&t,0,sizeof(t));join_id_rec ids[1]={{0x77ull,0x2077ull,0,0}};t.ids=ids;t.nids=1;
    join_capture_resv(&t);
    assert(t.nresv==3 && t.resv[0].zone==0x2077ull && t.resv[0].owner==(0x500ull|0x100000ull) && t.resv[0].index==1u && t.resv[2].index==3u &&
           t.resv[1].expiry==41.0 && t.resv[1].box[5]==0x15);
    /* the admission: the joiner's same tree with other boxes and expiries; one session record has no node, one node no record */
    join_resv_rec s_rec[3];memcpy(s_rec,t.resv,sizeof(s_rec));free(t.resv);
    s_rec[2].index=9u;                                                    /* no joiner node with index 9 */
    for(unsigned i=0;i<3;i++) {memset(tresv_node[i]+JOIN_RESV_BOX,0xee,JOIN_RESV_BOX_BYTES);double e=-1.0;memcpy(tresv_node[i]+JOIN_RESV_EXPIRY,&e,8);}
    memcpy(tresv_node[1]+JOIN_RESV_BOX,s_rec[1].box,JOIN_RESV_BOX_BYTES);memcpy(tresv_node[1]+JOIN_RESV_EXPIRY,&s_rec[1].expiry,8); /* equal already */
    join_tables s,mine;memset(&s,0,sizeof(s));memset(&mine,0,sizeof(mine));s.resv=s_rec;s.nresv=3;
    join_id_rec mine_ids[4]={{0x77ull,0x2077ull,0,0},{0x500ull,0x100500ull,0,0},{0x501ull,0x100501ull,0,0},{0x502ull,0x100502ull,0,0}};
    mine.ids=mine_ids;mine.nids=4;   /* sorted by portable */
    join_resv_on=0;join_admit_resv(&s,&mine);assert(!join_resv_written && !join_resv_equal && !join_resv_missing);
    join_resv_on=1;join_admit_resv(&s,&mine);
    assert(join_resv_written==1 && join_resv_equal==1 && join_resv_missing==1 && join_resv_extra==1);
    assert(!memcmp(tresv_node[0]+JOIN_RESV_BOX,s_rec[0].box,JOIN_RESV_BOX_BYTES) && !memcmp(tresv_node[0]+JOIN_RESV_EXPIRY,&s_rec[0].expiry,8));
    {double e;memcpy(&e,tresv_node[2]+JOIN_RESV_EXPIRY,8);assert(e==-1.0 && tresv_node[2][JOIN_RESV_BOX]==0xee);}   /* the unmatched node untouched */
    join_admit_resv(&s,&mine);assert(join_resv_written==0 && join_resv_equal==2 && join_resv_missing==1);   /* again: nothing left */
    join_resv_on=0;join_key=key;join_world_find=find;join_world_map=map;
    printf("join RESV: the reservation tree in order (count checked), the capture by zone/owner/index, the admission's box and expiry writes, missing and extra PASS\n");
}
/* OMGS admission write (lockstep_join_world.h join_omgs_write): the raw runs land, the masked words and the vector triples are not
 * written raw; an empty session vector empties the joiner's own (its begin and capacity kept), a non-empty one leaves it. */
static void test_join_omgs_write(void) {
    join_omgs_masks();assert(join_omgs_nmask>=35u && join_omgs_vec[0]==0x50u+0x190u+0x168u && join_omgs_vec[5]==0x50u+2u*0x4f0u+0x340u+0x168u);
    static unsigned char o[JOIN_OMGS_SIZE];static join_omgs_rec r;
    for(unsigned i=0;i<JOIN_OMGS_SIZE;i++) o[i]=0xee;
    memset(&r,0,sizeof(r));r.portable=0x2001;for(unsigned i=0;i<JOIN_OMGS_BYTES;i++) r.data[i]=(unsigned char)(i*7u+1u);
    uint64_t own[2][3];
    for(unsigned v=0;v<JOIN_OMGS_VECTORS;v++) {
        const unsigned at=join_omgs_vec[v];uint64_t s[3]={0x1e8cfcf7f70ull,0x1e8cfcf7f70ull,0x1e8cfcf7f78ull};   /* the saver's: empty */
        if(v==1u) {s[1]=s[0]+16u;s[2]=s[0]+32u;}                                                          /* the saver's: 2 doubles */
        memcpy(r.data+(at-JOIN_OMGS_FROM),s,24);
        uint64_t j[3]={0x2277bb48000ull+v*64u,0x2277bb48000ull+v*64u+24u,0x2277bb48000ull+v*64u+40u};          /* the joiner's: 3 doubles */
        memcpy(o+at,j,24);if(v<2u) memcpy(own[v],j,24);
    }
    join_omgs_vec_cleared=join_omgs_vec_equal=join_omgs_vec_kept=join_omgs_vec_differs=0;
    assert(join_omgs_write(o,&r)==1);
    uint64_t got[3];memcpy(got,o+join_omgs_vec[0],24);
    assert(got[0]==own[0][0] && got[1]==own[0][0] && got[2]==own[0][2]);   /* emptied in place: the joiner's storage */
    memcpy(got,o+join_omgs_vec[1],24);assert(!memcmp(got,own[1],24));     /* a non-empty session vector: left alone */
    assert(join_omgs_vec_cleared==5u && join_omgs_vec_differs==1u && !join_omgs_vec_kept && !join_omgs_vec_equal);
    for(unsigned k=0;k<join_omgs_nmask;k++) for(unsigned b=0;b<join_omgs_mask[k].len;b++) assert(o[join_omgs_mask[k].at+b]==0xee);
    for(unsigned i=0;i<JOIN_OMGS_FROM;i++) assert(o[i]==0xee);   /* the vtable and the marker bytes: never */
    assert(o[0x50]==r.data[0x40] && o[0x2b0+0x50]==r.data[0x2b0+0x40] && o[JOIN_OMGS_SIZE-1u]==r.data[JOIN_OMGS_BYTES-1u]);
    assert(join_omgs_write(o,&r)==0 && join_omgs_vec_equal==5u);   /* again: nothing left to write */
    printf("hot join OMGS: raw runs, masks, the vector triples emptied in place or left PASS\n");
}
static void test_join(void) {
    test_join_precise();test_join_golden();test_join_capture_restore();test_join_refusals();test_join_static_keys();test_join_activation_rows();
    test_join_config_refused();test_join_identities_and_wire();test_join_world();test_join_path_region();test_join_omgs_write();test_join_resv();test_join_pcsv();
}
