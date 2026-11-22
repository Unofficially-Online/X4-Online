/* The sound starts' private draws (gate_sound_rng.h): the wrappers run FloatState and I32 on a private word and put the
 * caller's back (synthetic); on the real executable the pins, the three sites to their stubs (the mov replayed, the call
 * through the page, the jump back; the retargeted call) and back. Included by test_gate_linux.c. */
static uint64_t tlsnd_seen;
static float tlsnd_float(float bound) {uint64_t *w=gate_tls_rng(gate_tls_self());tlsnd_seen=*w;*w+=1;return bound*0.5f;}
static int tlsnd_i32(int n) {uint64_t *w=gate_tls_rng(gate_tls_self());tlsnd_seen=*w;*w+=7;return n-1;}
static void test_sound_rng_linux(void) {
    uint64_t *word=gate_tls_rng(gate_tls_self());assert(word);
    const uint64_t saved=*word;*word=0x1122334455667788ull;
    lxsnd_float_override=tlsnd_float;lxsnd_i32_override=tlsnd_i32;lxsnd_float_calls=lxsnd_choice_calls=lxsnd_counter=lxsnd_failures=0;
    assert(lxsnd_float(4.0f)==2.0f && *word==0x1122334455667788ull && tlsnd_seen!=0x1122334455667788ull);
    const uint64_t first=tlsnd_seen;
    assert(lxsnd_i32(5)==4 && *word==0x1122334455667788ull && tlsnd_seen!=0x1122334455667788ull && tlsnd_seen!=first);
    assert(lxsnd_float_calls==1 && lxsnd_choice_calls==1 && !lxsnd_failures);
    /* the private words follow the call counter only, never the simulation's word */
    *word=0x99u;lxsnd_counter=0;lxsnd_float(1.0f);const uint64_t a=tlsnd_seen;*word=0x77u;lxsnd_counter=0;lxsnd_float(1.0f);assert(tlsnd_seen==a && *word==0x77u);
    lxsnd_float_override=NULL;lxsnd_i32_override=NULL;lxsnd_float_calls=lxsnd_choice_calls=lxsnd_counter=0;*word=saved;
    puts("sound rng (Linux): FloatState and I32 on a private word from the call counter, the caller's word put back PASS");
}
static void test_sound_rng_elf(void) {
    for(unsigned i=0;i<sizeof(lxsnd_pin_names)/sizeof(*lxsnd_pin_names);i++) assert(site_ok(site_named(lxsnd_pin_names[i])));
    assert(lxsnd_state()==0);
    {int32_t r;memcpy(&r,LX(0x1a62e8eu)+1,4);assert(LX(0x1a62e8eu)[0]==0xe8 && LX(0x1a62e8eu)+5+r==LX(LXSND_FLOAT_VA));
     memcpy(&r,LX(0x1a63a4du)+1,4);assert(LX(0x1a63a4du)[0]==0xe8 && LX(0x1a63a4du)+5+r==LX(LXSND_I32_VA));
     memcpy(&r,LX(0x1a63ae4u)+1,4);assert(LX(0x1a63ae4u)+5+r==LX(LXSND_I32_VA));}
    reset_session();stats.active=1;boot_mode=1;boot_phase=0;stats.issued=0;ui_thread=GetCurrentThreadId();lxsnd_installed=lxsnd_restored=0;
    assert(lxsnd_arm() && lxsnd_installed && lxsnd_state()==1 && lxsnd_page_ok() && lxsnd_check());
    for(unsigned i=0;i<2;i++) {
        const unsigned char *at=LX(lxsnd_sites[i].va),*s=lxsnd_page+lxsnd_sites[i].stub;int32_t r;void *f;
        assert(at[0]==0xe9);memcpy(&r,at+1,4);assert(at+5+r==s);
        assert(!memcmp(s,lxsnd_sites[i].original,5) && s[7]==0xff && s[8]==0x15);
        memcpy(&r,s+9,4);memcpy(&f,s+13+r,8);assert(f==(i==0?(void*)lxsnd_float:(void*)lxsnd_i32));
        assert(s[13]==0xe9);memcpy(&r,s+14,4);assert(s+18+r==LX(lxsnd_sites[i].back));
    }
    {const unsigned char *at=LX(lxsnd_sites[2].va),*s=lxsnd_page+0x50;int32_t r;void *f;
     assert(at[0]==0xe8);memcpy(&r,at+1,4);assert(at+5+r==s && s[0]==0xff && s[1]==0x25);memcpy(&r,s+2,4);memcpy(&f,s+6+r,8);assert(f==(void*)lxsnd_i32);}
    {char status[256];assert(lxsnd_status(status,sizeof(status)) && strstr(status,"sound=0/0 sound_installed=1"));}
    lxsnd_restore();assert(!lxsnd_installed && lxsnd_restored && lxsnd_state()==0 && lxsnd_pins());
    munmap(lxsnd_page,GATE_PAGE);lxsnd_page=NULL;lxsnd_restored=0;lxsnd_stage="idle";stats.active=0;
    puts("sound rng ELF: pins, the two jumps to their stubs (mov replayed, call through the page, back), the retargeted call, restored PASS");
}
