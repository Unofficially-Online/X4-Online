/* Contact pairs in the portable order (gate_contact_orient.h). Synthetic: Windows' order on fake bodies (an owner with a
 * portable ID first, then one keyed by runtime ID, then ownerless bodies by BodyID; on, observe, reverse), and a stub
 * that runs: a test site on an executable page whose branch targets return 1 and 0, called as a function with the two
 * bodies, through lxco_common, keeping the callee-saved registers. ELF: the pins, the five windows to their stubs and
 * back. Included by test_gate_linux.c. */
static uint64_t tlxco_key(uint64_t runtime) {return runtime==0x111?0x9000:runtime==0x222?0x8000:0;}
typedef struct {unsigned char *record;uint8_t pad[0x50-8];} tlxco_dummy;
static unsigned char tlxco_mem[4][0x300];
/* body i: BodyID id at +0x70, record at +0x50 -> owner (runtime at +8, records at +0x258/+0x260). */
static unsigned char *tlxco_body(unsigned i,uint32_t id,uint64_t runtime,int own_record) {
    static unsigned char bodies[4][0x80],records[4][0x10];
    unsigned char *b=bodies[i],*r=records[i],*o=tlxco_mem[i];
    memset(b,0,sizeof(bodies[i]));memset(r,0,sizeof(records[i]));memset(o,0,sizeof(tlxco_mem[i]));
    memcpy(b+0x70,&id,4);
    if(runtime) {
        void *rp=r,*op=o;memcpy(b+0x50,&rp,8);memcpy(r,&op,8);memcpy(o+8,&runtime,8);
        if(own_record) memcpy(o+LXIO_RECORDS,&rp,8);
    }
    return b;
}
static unsigned char *tlxco_page_lx_base;
static unsigned char *tlxco_lx(uint32_t va) {return tlxco_page_lx_base+va;}
static void test_contact_orient_linux(void) {
    uint64_t (*keep)(uint64_t)=lxio_owner_key;lxio_owner_key=tlxco_key;
    const uint64_t issued=stats.issued;stats.issued=7;
    unsigned char *a=tlxco_body(0,3,0x111,1),*b=tlxco_body(1,5,0x222,1),*c=tlxco_body(2,1,0x333,1),*d=tlxco_body(3,2,0,0);
    lxco_mode=LXCO_ON;
    /* both portable: by key (0x8000 < 0x9000), so b before a although b has the higher BodyID: native 5<3 false, ours true */
    assert(lxco_cf(b,a,0)==1 && lxco_cf(a,b,0)==0);
    /* portable before runtime-keyed (c's owner 0x333 has no key) before ownerless */
    assert(lxco_cf(a,c,0)==1 && lxco_cf(c,a,0)==0 && lxco_cf(c,d,0)==1 && lxco_cf(d,c,0)==0);
    lxco_mode=LXCO_OBSERVE;assert(lxco_cf(b,a,0)==0 && lxco_cf(a,b,0)==1);  /* the BodyIDs */
    lxco_mode=LXCO_REVERSE;assert(lxco_cf(b,a,0)==0 && lxco_cf(a,b,0)==1);
    assert(lxco_flips[0]>=2 && lxco_unkeyed>=2 && lxco_bodyid>=2);
    /* a stub that runs: x = rdi, y = rsi, jb to "return 1", else "return 0" */
    unsigned char *page=mmap(NULL,0x1000,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);assert(page!=MAP_FAILED);
    memset(page,0xcc,0x1000);tlxco_page_lx_base=page;
    static const unsigned char ret1[]={0xb8,0x01,0x00,0x00,0x00,0xc3},ret0[]={0x31,0xc0,0xc3};
    memcpy(page+0x200,ret1,sizeof(ret1));memcpy(page+0x210,ret0,sizeof(ret0));
    const lxco_site t={"test",0,0x210,0x200,5,LR_RDI,LR_RSI,0x82,0,{0},0,{0}};
    assert(lxco_write_site(page,page,&t,0,tlxco_lx,(void*)lxco_common)==LXCO_STUB);
    assert(!mprotect(page,0x1000,PROT_READ|PROT_EXEC));
    int (*stub)(void *,void *)=(int(*)(void *,void *))(void *)page;
    lxco_mode=LXCO_ON;
    assert(stub(b,a)==1 && stub(a,b)==0 && stub(c,d)==1 && stub(d,c)==0);
    lxco_mode=LXCO_OBSERVE;assert(stub(b,a)==0 && stub(a,b)==1);
    lxco_mode=LXCO_REVERSE;assert(stub(b,a)==0 && stub(a,b)==1);
    munmap(page,0x1000);
    lxco_mode=LXCO_OFF;lxio_owner_key=keep;stats.issued=issued;
    for(unsigned i=0;i<LXCO_SITES;i++) lxco_calls[i]=lxco_flips[i]=0;
    lxco_unkeyed=lxco_bodyid=lxco_agree=lxco_failures=0;
    puts("contact orient (Linux): Windows' order on bodies (portable key, runtime key, BodyID; on/observe/reverse), a stub run through lxco_common PASS");
}
static void test_contact_orient_elf(void) {
    for(unsigned i=0;i<sizeof(lxco_pin_names)/sizeof(*lxco_pin_names);i++) assert(site_ok(site_named(lxco_pin_names[i])));
    for(unsigned i=0;i<LXCO_SITES;i++) assert(lxco_state(i)==0);
    assert(lxco_pins());
    reset_session();stats.active=1;boot_mode=1;boot_phase=0;stats.issued=0;ui_thread=GetCurrentThreadId();
    const int identities=world_identity_installed;world_identity_installed=1;lxco_installed=lxco_restored=0;lxco_done=0;
    assert(lxco_arm(LXCO_ON) && lxco_installed && lxco_done==0x1fu && lxco_check());
    for(unsigned i=0;i<LXCO_SITES;i++) {
        assert(lxco_state(i)==1);
        const unsigned char *s=lxco_page+LXCO_STUB*i,*q=s+lxco_sites[i].pre_n+(lxco_sites[i].pre_jne?6:0);
        assert(!memcmp(s,lxco_sites[i].pre,lxco_sites[i].pre_n) && q[0]==0x48 && q[1]==0x8d && q[2]==0x64); /* lea rsp,[rsp-0x80] */
    }
    assert(lxco_pins());  /* the pins read through the patched windows */
    assert(lxco_arm(LXCO_ON) && !lxco_arm(LXCO_REVERSE)); /* a rerun with the same mode */
    {char status[384];assert(lxco_status(status,sizeof(status)) && strstr(status,"installed=1 mode=on calls=0/0/0/0/0 "));}
    lxco_restore();assert(!lxco_installed && lxco_restored && !lxco_done);
    for(unsigned i=0;i<LXCO_SITES;i++) assert(lxco_state(i)==0);
    lxco_restored=0;lxco_stage="idle";lxco_mode=LXCO_OFF;world_identity_installed=identities;stats.active=0;
    puts("contact orient ELF: the pins, the five windows to their stubs and back PASS");
}
