/* native_gate_fit_rows = {first, last} (diagnostic, default off; both gates, the same text): every call of the Steering
 * model's flight integration in ticks first..last, one FIT_ROW each, with each argument's leading bytes before and
 * after the call. Windows: the Steering step 0xec7200 calls 0x1197fe0 at 0xec92c0 (it widens the velocities at [rdx]
 * to doubles, calls 0x1197970 -> 0x1195570, and narrows the result back into [rdx]); Linux: the Steering step
 * 0x13e9d50 calls 0x19fc190 at 0x13eacf8 (the velocities in at [rdx], the result out as doubles at [rsi]). The two
 * executables pass the same data in another shape, so rows are paired offline by equal inputs; a pair with equal
 * inputs and different results puts a platform difference inside the integration. Why: the 1203 split of the mixed
 * census (reports/LOCKSTEP-LINUX-CATCHUP-20261104.txt): two Steering ships' velocities 1-3 ulps apart while their
 * model state is equal. The including gate retargets its call's rel32 and calls fit_note before and after the
 * original; rows are claimed atomically (the step may run on a pathing job thread). */
#define FIT_ARGS 5u
#define FIT_BYTES 0x40u
#define FIT_ROWS 8192u
#define FIT_FRAME_TAG 0x4000000000000000ull /* thread | this: a frame-function row (a0 the matrix out, a1 the velocity out, a2 the
                                            * component; Windows 0x3df3f0, Linux 0x18a3980) */
#define FIT_NAV_TAG 0x2000000000000000ull   /* thread | caller RVA << 32 | this: a NavigationController::SetVelocity row (a0 the
                                            * input velocities, a1 the ship, a2 the controller's stored velocity +0xa0, a3 the
                                            * controller; Windows 0xea5790, Linux 0x141fe50) */
#define FIT_STEP_TAG 0x8000000000000000ull /* thread | this: a Steering step row (a0..a2 the bank +0x00/+0x40/+0x80, a3 the model's
                                           * steering output), not an integration call */
typedef struct {uint64_t tick,thread;uint8_t readable[2][FIT_ARGS];unsigned char before[FIT_ARGS][FIT_BYTES],after[FIT_ARGS][FIT_BYTES];} fit_row;
static fit_row *fit_rows;
/* A thread's open step row, between the step observer's before and after calls (no TLS: the Linux gate may need nothing
 * but libc): 64 slots by thread id, claimed by CAS, freed at the after call. */
#define FIT_OPEN 64u
static struct {volatile int64_t thread;fit_row *row;} fit_open[FIT_OPEN];
static void fit_open_put(uint64_t thread,fit_row *row) {
    for(unsigned k=0,i=(unsigned)(thread*0x9e3779b1u)&(FIT_OPEN-1);k<FIT_OPEN;k++,i=(i+1)&(FIT_OPEN-1)) {
        int64_t empty=0;
        if(__atomic_compare_exchange_n(&fit_open[i].thread,&empty,(int64_t)thread,0,__ATOMIC_SEQ_CST,__ATOMIC_SEQ_CST)) {fit_open[i].row=row;return;}
    }
}
static fit_row *fit_open_take(uint64_t thread) {
    for(unsigned k=0,i=(unsigned)(thread*0x9e3779b1u)&(FIT_OPEN-1);k<FIT_OPEN;k++,i=(i+1)&(FIT_OPEN-1))
        if(__atomic_load_n(&fit_open[i].thread,__ATOMIC_SEQ_CST)==(int64_t)thread) {
            fit_row *r=fit_open[i].row;fit_open[i].row=NULL;__atomic_store_n(&fit_open[i].thread,0,__ATOMIC_SEQ_CST);return r;
        }
    return NULL;
}
static volatile int64_t fit_claimed,fit_dropped,fit_calls;
static uint64_t fit_first,fit_last,fit_drained;
static int fit_on;
/* The row for this call (NULL: outside the window or full); `which` 0 before the call, 1 after. */
static fit_row *fit_claim(uint64_t tick) {
    if(!fit_on || !fit_rows || tick<fit_first || tick>fit_last) return NULL;
    __atomic_add_fetch(&fit_calls,1,__ATOMIC_RELAXED);
    const int64_t slot=__atomic_fetch_add(&fit_claimed,1,__ATOMIC_SEQ_CST);
    if(slot<0 || slot>=(int64_t)FIT_ROWS) {__atomic_add_fetch(&fit_dropped,1,__ATOMIC_RELAXED);return NULL;}
    fit_row *r=&fit_rows[slot];memset(r,0,sizeof(*r));r->tick=tick;return r;
}
static void fit_note(fit_row *r,unsigned which,void *const *args,int (*read)(const void *,void *,size_t)) {
    if(!r) return;
    for(unsigned a=0;a<FIT_ARGS;a++) r->readable[which][a]=(uint8_t)(args[a] && read(args[a],which?r->after[a]:r->before[a],FIT_BYTES));
}
static void fit_hex(char *out,const unsigned char *p,unsigned n) {
    static const char d[]="0123456789abcdef";
    for(unsigned i=0;i<n;i++) {out[2*i]=d[p[i]>>4];out[2*i+1]=d[p[i]&15];}
    out[2*n]=0;
}
/* "tick=T row=N thread=H a0=<before>|<after> .. a4=.." ('-' an unreadable argument) */
static int fit_text(uint64_t index,char *out,size_t cap) {
    const int64_t claimed=__atomic_load_n(&fit_claimed,__ATOMIC_SEQ_CST);
    if(!fit_rows || (int64_t)index>=claimed || index>=FIT_ROWS) return 0;
    const fit_row *r=&fit_rows[index];
    int n=snprintf(out,cap,"tick=%llu row=%llu thread=%llx",(unsigned long long)r->tick,(unsigned long long)index,(unsigned long long)r->thread);
    for(unsigned a=0;a<FIT_ARGS && n>0 && (size_t)n+2*(2*FIT_BYTES)+16<cap;a++) {
        n+=snprintf(out+n,cap-(size_t)n," a%u=",a);
        if(r->readable[0][a]) {fit_hex(out+n,r->before[a],FIT_BYTES);n+=2*FIT_BYTES;} else out[n++]='-';
        out[n++]='|';
        if(r->readable[1][a]) {fit_hex(out+n,r->after[a],FIT_BYTES);n+=2*FIT_BYTES;} else out[n++]='-';
        out[n]=0;
    }
    return n>0 && (size_t)n<cap?n:0;
}
