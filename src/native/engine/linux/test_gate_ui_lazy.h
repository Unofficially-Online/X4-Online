/* The UI's lazy container updates on the Linux gate (gate_ui_lazy.h). Synthetic: the query rules (a LuaJIT frame or a
 * display site: skipped in block and key, run in observe; a simulation call runs), Windows' seeds at the Linux fields,
 * and the code the module writes run on an executable page (a trampoline that replays a function's first instruction
 * and jumps back, an entry stub that reaches a C function). ELF: the pins, the five entries to their stubs and back.
 * Included by test_gate_linux.c. */
static int tlxul_lua,tlxul_display;
static uint64_t tlxul_word=0x1234;
static int tlxul_from(uint32_t *chain,int *display) {for(unsigned i=0;i<LXUL_CHAIN;i++) chain[i]=0xffffffffu;*display=tlxul_display;return tlxul_lua;}
static uint64_t *tlxul_tls(void) {return &tlxul_word;}
static uint64_t tlxul_key(uint64_t runtime,unsigned *klass) {*klass=WORLD_KEY_SAVED;return runtime==0x111?0x9000:0;}
static int tlxul_entry_calls;
static int tlxul_entry(void) {return ++tlxul_entry_calls+40;}
static void test_ui_lazy_linux(void) {
    int (*from)(uint32_t *,int *)=lxul_from_lua_fn;uint64_t *(*tls)(void)=lxul_tls_word_fn;uint64_t (*key)(uint64_t,unsigned *)=lxul_key_fn;
    const uint64_t issued=stats.issued;const int active=stats.active,phase=boot_phase;const DWORD thread=ui_thread;
    lxul_from_lua_fn=tlxul_from;lxul_tls_word_fn=tlxul_tls;lxul_key_fn=tlxul_key;
    stats.active=1;boot_phase=3;stats.issued=42;ui_thread=GetCurrentThreadId();
    static unsigned char container[0xb00];uint64_t runtime=0x111;double due=1234.5,time=99.25;uint32_t total=17;uint8_t pending=1;
    memcpy(container+8,&runtime,8);memcpy(container+LXUL_TRADE_DUE,&due,8);memcpy(container+LXUL_CARGO_TIME,&time,8);
    memcpy(container+LXUL_UNITS_TOTAL,&total,4);memcpy(container+LXUL_UNITS_PENDING,&pending,1);
    lxul_mode=LXUL_KEY;
    tlxul_lua=1;assert(lxul_ui_query(LXUL_CARGO,container,1,0)==1 && lxul_lua[LXUL_CARGO]==1 && lxul_blocked==1);   /* the Lua UI: skipped */
    tlxul_lua=0;tlxul_display=1;assert(lxul_ui_query(LXUL_CARGO,container,1,0)==1 && lxul_display_blocked==1);    /* the map's drawing */
    tlxul_display=0;assert(lxul_ui_query(LXUL_CARGO,container,1,0)==0 && lxul_sim[LXUL_CARGO]==1);                  /* the simulation */
    lxul_mode=LXUL_OBSERVE;tlxul_lua=1;assert(lxul_ui_query(LXUL_TRADE,container,0,0)==0 && lxul_lua[LXUL_TRADE]==1);
    assert(!lxul_key_word(LXUL_CARGO,container,0,&(uint64_t){0}));                                                  /* observe: native */
    lxul_mode=LXUL_KEY;uint64_t seed=0,bits;memcpy(&bits,&time,8);
    assert(lxul_key_word(LXUL_CARGO,container,bits,&seed)==&tlxul_word && seed==lockstep_seed(LXUL_KEY_DOMAIN,bits,0x9000,0,0,0));
    uint64_t due_bits;memcpy(&due_bits,&due,8);
    assert(lxul_key_word(LXUL_TRADE,container,0,&seed) && seed==lockstep_seed(LXUL_KEY_DOMAIN,42,0x9000,LXUL_TRADE,due_bits,0));
    assert(lxul_key_word(LXUL_UNITS,container,0,&seed) && seed==lockstep_seed(LXUL_KEY_DOMAIN,42,0x9000,LXUL_UNITS,17ull|(1ull<<32),0));
    runtime=0x222;memcpy(container+8,&runtime,8);assert(!lxul_key_word(LXUL_CARGO,container,bits,&seed) && lxul_unkeyed==1);  /* no portable key */
    assert(lxul_keyed[LXUL_CARGO]==1 && lxul_keyed[LXUL_TRADE]==1 && lxul_keyed[LXUL_UNITS]==1);
    {char status[384];assert(lxul_status(status,sizeof(status)) && strstr(status," mode=key frame=42 lua=1/1/0 sim=1/0/0 "));}
    /* the code it writes, run: a function `mov eax,7; ret` at +0x400, its trampoline at +0 returns 7 through the jump
     * back; the entry stub at +0x20 reaches tlxul_entry */
    unsigned char *page=mmap(NULL,0x1000,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);assert(page!=MAP_FAILED);
    static const unsigned char fn[]={0xb8,0x07,0x00,0x00,0x00,0xc3};memset(page,0xcc,0x1000);memcpy(page+0x400,fn,sizeof(fn));
    lxul_write_code(page,page+0x400,5,(void*)tlxul_entry);
    assert(!mprotect(page,0x1000,PROT_READ|PROT_EXEC));
    assert(((int(*)(void))(void*)page)()==7 && ((int(*)(void))(void*)(page+0x20))()==41 && tlxul_entry_calls==1);
    munmap(page,0x1000);
    lxul_mode=LXUL_OFF;lxul_from_lua_fn=from;lxul_tls_word_fn=tls;lxul_key_fn=key;
    stats.issued=issued;stats.active=active;boot_phase=phase;ui_thread=thread;
    memset(lxul_lua,0,sizeof(lxul_lua));memset(lxul_sim,0,sizeof(lxul_sim));memset(lxul_keyed,0,sizeof(lxul_keyed));memset(lxul_callers,0,sizeof(lxul_callers));
    lxul_blocked=lxul_unkeyed=lxul_unmapped=lxul_display_blocked=lxul_display_passed=0;
    puts("ui lazy (Linux): UI queries (LuaJIT or the map's drawing) skipped, simulation calls keyed as Windows' seeds, the written trampoline and entry run PASS");
}
static void test_ui_lazy_elf(void) {
    assert(lxul_pins());
    for(unsigned i=0;i<LXUL_TARGETS;i++) assert(lxul_state(i)==0);
    int (*from)(uint32_t *,int *)=lxul_from_lua_fn;lxul_from_lua_fn=tlxul_from;
    reset_session();stats.active=1;boot_mode=1;boot_phase=0;stats.issued=0;ui_thread=GetCurrentThreadId();
    const int identities=world_identity_installed;world_identity_installed=1;lxul_installed=lxul_restored=0;lxul_done=0;
    assert(lxul_arm(LXUL_KEY) && lxul_installed && lxul_done==0x1fu && lxul_check());
    for(unsigned i=0;i<LXUL_TARGETS;i++) {
        assert(lxul_state(i)==1 && lxul_code_ok(i));
        unsigned char *p=lxul_page+LXUL_STUB*i;void *back=NULL;memcpy(&back,p+lxul_targets[i].stolen+6,8);
        assert(back==LX(lxul_targets[i].va)+lxul_targets[i].stolen && p[lxul_targets[i].stolen]==0xff && p[0x20]==0xff);
    }
    assert(lxul_arm(LXUL_KEY) && !lxul_arm(LXUL_BLOCK));   /* a rerun with the same mode */
    {char status[384];assert(lxul_status(status,sizeof(status)) && strstr(status,"installed=1 mode=key "));}
    lxul_restore();assert(!lxul_installed && lxul_restored && !lxul_done);
    for(unsigned i=0;i<LXUL_TARGETS;i++) assert(lxul_state(i)==0);
    assert(lxul_pins());
    lxul_restored=0;lxul_stage="idle";lxul_from_lua_fn=from;world_identity_installed=identities;stats.active=0;
    puts("ui lazy ELF: the pins, the five entries to their stubs and back PASS");
}
