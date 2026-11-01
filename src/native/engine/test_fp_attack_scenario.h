/* FR29b tests, both gates (test_lockstep_fp_attack.h, linux/test_gate_fp_attack.h): one scenario through the hooks of
 * lockstep_fp_attack.h with stand-in callees, and the FP_ATTACK_ROWs both gates must print for it (compared without
 * the in= field: Linux reaches the sub-calls through their real call sites, the Windows synthetic test calls its hooks).
 *   tick 546, window {540,550}, watch 07336a; the thread's RNG word 1111222233334444, its time 16.183;
 *   GetAttackStrength(attacker 0x501 = 96587, out1, out2, -, -, target 0x502 = 7336a, 1, 0, 1, 1):
 *     distance(target, attacker, 0, mode 2, time) = 1234.5
 *     weapon loop(attacker, wl1, wl2, -, -, 0, -1.0, 1204.5, target, 1, 0, 1, -, 150.0, 30.0, 0, 1):
 *       FloatState(150.0) on the thread's word; damage(weapon 0x503 = 73371, local, 0, 1); local *= 0.5; wl1 += local
 *     out1 += wl1
 *   apply(target, attacker, out1, -, 0, 1) = 1
 *   then GetAttackStrength at tick 600 (outside the window) and at 546 on 0x504 (no saved key, not watched). */
static unsigned char tfa_attacker[0x20],tfa_target[0x20],tfa_weapon[0x20],tfa_other[0x20];
static uint64_t tfa_word;static double tfa_time;
static unsigned tfa_native[FA_KINDS]; /* the stand-ins' calls per kind */
static void tfa_objects(void) {
    const uint64_t a=0x501,t=0x502,w=0x503,o=0x504;
    memcpy(tfa_attacker+8,&a,8);memcpy(tfa_target+8,&t,8);memcpy(tfa_weapon+8,&w,8);memcpy(tfa_other+8,&o,8);
    tfa_word=0x1111222233334444ull;tfa_time=16.183;fa_test_rng=&tfa_word;fa_test_time=&tfa_time;
    memset(tfa_native,0,sizeof(tfa_native));
}
/* A damage struct: a vtable word, five doubles, four floats, the flags. */
typedef struct {uint64_t vtable;double v[5];float f[4];uint16_t flags;uint8_t pad[6];} tfa_damage;
static void tfa_damage_init(tfa_damage *d) {memset(d,0,sizeof(*d));d->vtable=0x2a36550u;d->f[0]=d->f[1]=d->f[2]=d->f[3]=1.0f;}
static void tfa_dps_body(const void *weapon,tfa_damage *out) {
    assert(weapon==tfa_weapon);++tfa_native[FA_DPS];
    out->v[0]=10.5;out->v[1]=20.25;out->v[2]=0.0;out->v[3]=0.0;out->v[4]=30.75;out->f[2]=2.0f;
}
static void tfa_add_body(tfa_damage *acc,const tfa_damage *add) {
    ++tfa_native[FA_ADD];
    for(unsigned k=0;k<4;k++) acc->v[k]=add->v[k]+acc->v[k];
    acc->v[4]=acc->v[4]>add->v[4]?acc->v[4]:add->v[4];
}
static float tfa_rng_body(uint64_t *state,float bound) {
    ++tfa_native[FA_RNG];
    uint64_t s=*state*0x5851f42d4c957f2dull+0x14057b7ef767814full;s=(s>>30)|(s<<34);*state=s;
    return (float)(uint32_t)s*2.32830644e-10f*bound;
}
static void tfa_scale(tfa_damage *d,double k) {for(unsigned i=0;i<5;i++) d->v[i]=d->v[i]*k;}
/* The rows both gates print for the scenario (in= removed). */
#define TFA_ROWS 8u
static const char *const tfa_rows[TFA_ROWS]={
    "index=0 tick=546 call=1 seq=1 kind=dist thread=ui attacker=0000000000096587 target=000000000007336a obj=000000000007336a obj2=0000000000096587 label=- time=40302ed916872b02 rng=1111222233334444:1111222233334444 ret=0 args=40302ed916872b02,0,2,449a5000 "
        "b0=- "
        "b1=- "
        "b2=- "
        "b3=-",
    "index=1 tick=546 call=1 seq=3 kind=rng thread=ui attacker=0000000000096587 target=000000000007336a obj=- obj2=- label=- time=40302ed916872b02 rng=1111222233334444:4770f50dbea91fdd ret=0 args=43160000,42df6e32 "
        "b0=- "
        "b1=- "
        "b2=- "
        "b3=-",
    "index=2 tick=546 call=1 seq=4 kind=dps thread=ui attacker=0000000000096587 target=000000000007336a obj=0000000000073371 obj2=- label=- time=40302ed916872b02 rng=4770f50dbea91fdd:4770f50dbea91fdd ret=0 args=1 "
        "b0=000000000000000000000000000000000000000000000000000000000000000000000000000000000000803f0000803f0000803f0000803f0000000000000000 "
        "b1=00000000000025400000000000403440000000000000000000000000000000000000000000c03e400000803f0000803f000000400000803f0000000000000000 "
        "b2=- "
        "b3=-",
    "index=3 tick=546 call=1 seq=5 kind=add thread=ui attacker=0000000000096587 target=000000000007336a obj=- obj2=- label=wl.out1 time=40302ed916872b02 rng=4770f50dbea91fdd:4770f50dbea91fdd ret=0 args=- "
        "b0=000000000000000000000000000000000000000000000000000000000000000000000000000000000000803f0000803f0000803f0000803f0000000000000000 "
        "b1=00000000000015400000000000402440000000000000000000000000000000000000000000c02e400000803f0000803f000000400000803f0000000000000000 "
        "b2=00000000000015400000000000402440000000000000000000000000000000000000000000c02e400000803f0000803f0000803f0000803f0000000000000000 "
        "b3=-",
    "index=4 tick=546 call=1 seq=2 kind=wl thread=ui attacker=0000000000096587 target=000000000007336a obj=- obj2=000000000007336a label=- time=40302ed916872b02 rng=1111222233334444:4770f50dbea91fdd ret=0 args=0,bff0000000000000,44969000,1,0,1,43160000,41f00000,0,1,0,0,0,0 "
        "b0=000000000000000000000000000000000000000000000000000000000000000000000000000000000000803f0000803f0000803f0000803f0000000000000000 "
        "b1=00000000000015400000000000402440000000000000000000000000000000000000000000c02e400000803f0000803f0000803f0000803f0000000000000000 "
        "b2=000000000000000000000000000000000000000000000000000000000000000000000000000000000000803f0000803f0000803f0000803f0000000000000000 "
        "b3=000000000000000000000000000000000000000000000000000000000000000000000000000000000000803f0000803f0000803f0000803f0000000000000000",
    "index=5 tick=546 call=1 seq=6 kind=add thread=ui attacker=0000000000096587 target=000000000007336a obj=- obj2=- label=gas.out1 time=40302ed916872b02 rng=4770f50dbea91fdd:4770f50dbea91fdd ret=0 args=- "
        "b0=000000000000000000000000000000000000000000000000000000000000000000000000000000000000803f0000803f0000803f0000803f0000000000000000 "
        "b1=00000000000015400000000000402440000000000000000000000000000000000000000000c02e400000803f0000803f0000803f0000803f0000000000000000 "
        "b2=00000000000015400000000000402440000000000000000000000000000000000000000000c02e400000803f0000803f0000803f0000803f0000000000000000 "
        "b3=-",
    "index=6 tick=546 call=1 seq=0 kind=gas thread=ui attacker=0000000000096587 target=000000000007336a obj=- obj2=- label=- time=40302ed916872b02 rng=1111222233334444:4770f50dbea91fdd ret=0 args=0,0,1,0,1,1,0 "
        "b0=000000000000000000000000000000000000000000000000000000000000000000000000000000000000803f0000803f0000803f0000803f0000000000000000 "
        "b1=00000000000015400000000000402440000000000000000000000000000000000000000000c02e400000803f0000803f0000803f0000803f0000000000000000 "
        "b2=000000000000000000000000000000000000000000000000000000000000000000000000000000000000803f0000803f0000803f0000803f0000000000000000 "
        "b3=000000000000000000000000000000000000000000000000000000000000000000000000000000000000803f0000803f0000803f0000803f0000000000000000",
    "index=7 tick=546 call=0 seq=0 kind=apply thread=ui attacker=0000000000096587 target=000000000007336a obj=- obj2=- label=- time=40302ed916872b02 rng=4770f50dbea91fdd:4770f50dbea91fdd ret=1 args=1 "
        "b0=00000000000015400000000000402440000000000000000000000000000000000000000000c02e400000803f0000803f0000803f0000803f0000000000000000 "
        "b1=- "
        "b2=- "
        "b3=-",
};
/* The row without its in= field, into out. */
static void tfa_strip_in(const char *row,char *out,size_t cap) {
    const char *at=strstr(row," in=");const char *end=at?strchr(at+1,' '):NULL;
    if(!at || !end) {snprintf(out,cap,"%s",row);return;}
    snprintf(out,cap,"%.*s%s",(int)(at-row),row,end);
}
/* The rows of the scenario (index 0..7) against tfa_rows; prints them when TFA_PRINT is set. */
static void tfa_check_rows(int (*row)(uint64_t,char *,size_t),const char *const scopes[TFA_ROWS]) {
    char out[2048],stripped[2048];
    for(unsigned i=0;i<TFA_ROWS;i++) {
        const int n=row(i,out,sizeof(out));assert(n>0);
        tfa_strip_in(out,stripped,sizeof(stripped));
        if(getenv("TFA_PRINT")) printf("TFA_ROW %s\n",stripped);
        else if(strcmp(stripped,tfa_rows[i])) {printf("FP_ATTACK_ROW %u differs:\n got  %s\n want %s\n",i,stripped,tfa_rows[i]);assert(0);}
        if(scopes) {char want[32];snprintf(want,sizeof(want)," in=%s ",scopes[i]);assert(strstr(out,want));}
    }
    assert(!row(TFA_ROWS,out,sizeof(out)));
}
