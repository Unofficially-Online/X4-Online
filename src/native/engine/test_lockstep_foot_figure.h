/* lockstep_foot_figure.h: whose body the wrappers leave out (synthetic; the entry and the call site are checked live). */
static unsigned test_ffig_calls,test_ffig_bodies;static void *test_ffig_last;
static void test_ffig_body(void *body,void *view,unsigned char a,float b,uint64_t c,uint64_t d,uint64_t e,uint64_t f) {
    (void)view;(void)a;(void)c;(void)d;(void)e;(void)f;assert(b==2.5f);++test_ffig_bodies;test_ffig_last=body;
}
/* The NPC routine: its body's queueing (through the wrapper, as the patched call site does), then the rest. */
static void test_ffig_native(void *self,void *view,unsigned char a,uint32_t b,uint32_t c,void *d) {
    (void)b;(void)c;(void)d;ffig_body(self,view,a,2.5f,1,2,3,4);++test_ffig_calls;
}
static void test_foot_figure(void) {
    static unsigned char own[32],other[32];
    ffig_draw_fn saved=ffig_native;ffig_body_fn saved_body=ffig_body_native;const LONG64 was=ffig_own_id;
    ffig_native=test_ffig_native;ffig_body_native=test_ffig_body;test_ffig_calls=test_ffig_bodies=0;ffig_skips=0;
    {uint64_t id=0x162824;memcpy(own+8,&id,8);id=0x16281b;memcpy(other+8,&id,8);}
    InterlockedExchange64(&ffig_own_id,0);
    ffig_draw(own,NULL,0,0,0,NULL);ffig_draw(other,NULL,0,0,0,NULL);
    assert(test_ffig_calls==2 && test_ffig_bodies==2 && !ffig_skips);            /* no own figure: every body is queued */
    InterlockedExchange64(&ffig_own_id,0x162824);
    ffig_draw(own,NULL,0,0,0,NULL);
    assert(test_ffig_calls==3 && test_ffig_bodies==2 && ffig_skips==1 && !ffig_hiding);   /* its routine ran, its body was not queued */
    ffig_draw(other,NULL,0,0,0,NULL);assert(test_ffig_calls==4 && test_ffig_bodies==3 && test_ffig_last==other && ffig_skips==1);
    ffig_draw(NULL,NULL,0,0,0,NULL);assert(test_ffig_calls==5 && test_ffig_bodies==4);
    {char row[96];assert(ffig_status(row,sizeof(row)) && strstr(row,"/162824/1"));}
    ffig_native=saved;ffig_body_native=saved_body;InterlockedExchange64(&ffig_own_id,was);ffig_skips=0;
    puts("foot figure: this client's own figure's body is not queued for drawing, the rest of its routine runs PASS");
}
