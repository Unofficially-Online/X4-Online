/* Script positions and rotations, exact through a join (native_gate_join_sref, a join default; SREF, v13 section 87).
 *
 * A script's position and rotation values live outside the scripts, in two process-wide lists of counted nodes (0x50 bytes:
 * the count +0, the list's links +8/+0x10, the value +0x20: a position's four floats, a rotation's three matrix rows of four;
 * the rotation list's head is the pointer at 0x6cf2888). The save writes them in its <script><refs> tables, one <ref> each, and
 * not as they are: a rotation as yaw, pitch and roll in degrees taken from the matrix (0x136bc0: atan2f, asinf, times 57.29578),
 * and of both kinds only the attributes that are 0.0001 or more away from the default (0x1369c0 for a position). The load builds
 * the value again from what the text holds (0x49cbb0, 0x492ce0). A matrix does not come back with the same bits, and a pitch
 * below 0.0001 degrees or a coordinate below 0.1 mm comes back as 0.
 *
 * Where it showed [late joins hq46 .. hq63, 18 Nov]: move.gate works out a ship's place and rotation behind the gate
 * ($destzonepos, $exitrot), waits six to eight seconds and warps the ship there. A ship in that wait at T took the session's
 * rotation on the session and the save's on the joiner: after the warp its rows differed in the last bits of three matrix
 * elements (a yaw 2.4e-7 rad apart, the position equal), its whole fleet followed it, and the census counted 44 ships in
 * different motion 550 ticks after the join. In a join at T=6073 the session held 965 rotations and the save named 727; 275 of
 * the joiner's were within 1e-6 of a session rotation without being it.
 *
 * So the saver notes, while its join save runs, every value the two <ref> writers are given (their calls at 0xcbee23 and
 * 0xcbede9, in the order the save writes them), the package carries the notes (SREF), and the joiner's two <ref> readers (the
 * calls at 0xcc08b0 and 0xcc089f, in the order the load reads them) put the session's bits where the text's value went. The
 * order is the file's on both sides; each value is checked against the one the text gave (a rotation's elements within 1e-3, a
 * position's within 0.2 mm and 1e-5 of the coordinate) and a note that does not fit is searched for up to 16 notes ahead of its
 * kind, so a <ref> one side leaves out costs that one value and no more. Nothing is written when no note fits.
 * Counters in JOIN_STATUS sref= (on : the package's notes : rotations read / changed : positions read / changed : values with
 * no fitting note : values past the last note : notes skipped) and sref_cap= (the notes of this client's last join save). */
#define JOIN_SREF_THUNK 272u
#define JOIN_SREF_ROT 1u
#define JOIN_SREF_POS 2u
#define JOIN_SREF_CAP (1u<<18)
#define JOIN_SREF_AHEAD 16u
typedef void *(*join_sref_fn)(void *,void *,void *);
/* write a rotation, write a position, read a rotation, read a position: the call and what it calls */
static const uint32_t join_sref_site[4]={0xcbee23u,0xcbede9u,0xcc08b0u,0xcc089fu};
static const uint32_t join_sref_entry[4]={0x136bc0u,0x1369c0u,0x49cbb0u,0x492ce0u};
static int join_sref_on;
static join_sref_rec *join_sref_buf;                    /* the saver's notes of its join save (JOIN_SREF_CAP records) */
static volatile LONG join_sref_n,join_sref_over;
static volatile DWORD join_sref_thread;                 /* the saver's thread while its join save runs, 0 otherwise */
static int join_sref_saved;                             /* the last join save ran with both writers taken: its notes are the package's */
static const join_sref_rec *join_sref_rx;               /* the joiner's load: the package's notes and where each kind stands */
static uint32_t join_sref_rx_n,join_sref_rx_at[2];
static volatile LONG join_sref_reading;
static uint64_t join_sref_read[2],join_sref_set[2],join_sref_apart,join_sref_short,join_sref_skipped;
static int32_t join_sref_original[4],join_sref_patched[4];static unsigned char join_sref_taken[4];
static void *(*join_sref_test[4])(void *,void *,void *);   /* tests replace the engine's four functions */
static void *join_sref_call(unsigned i,void *a,void *b,void *c) {
    return join_sref_test[i]?join_sref_test[i](a,b,c):((join_sref_fn)(void *)(image+join_sref_entry[i]))(a,b,c);
}
static unsigned join_sref_bytes(unsigned kind) {return kind==JOIN_SREF_ROT?48u:16u;}
/* The saver: one value a <ref> writer is given. */
static void join_sref_note(unsigned kind,const void *value) {
    if(!join_sref_thread || join_sref_thread!=GetCurrentThreadId() || !join_sref_buf) return;
    const LONG at=InterlockedIncrement(&join_sref_n)-1;
    if(at<0 || at>=(LONG)JOIN_SREF_CAP) {InterlockedExchange(&join_sref_over,1);return;}
    join_sref_rec *r=&join_sref_buf[at];memset(r,0,sizeof(*r));r->kind=kind;
    if(!value || !read_local(value,r->value,join_sref_bytes(kind))) InterlockedExchange(&join_sref_over,1);   /* a hole would shift every later note */
}
/* Is the value the text gave the one this note was written from? Lanes 0-2 of each row. */
static int join_sref_fits(unsigned kind,const unsigned char *mine,const unsigned char *note) {
    const unsigned rows=kind==JOIN_SREF_ROT?3u:1u;
    for(unsigned r=0;r<rows;r++) for(unsigned k=0;k<3u;k++) {
        float a,b;memcpy(&a,mine+16u*r+4u*k,4);memcpy(&b,note+16u*r+4u*k,4);
        const float bound=kind==JOIN_SREF_ROT?1e-3f:2e-4f+1e-5f*fabsf(b);
        if(!(fabsf(a-b)<=bound)) return 0;   /* also a NaN on either side */
    }
    return 1;
}
/* The joiner: the value a <ref> reader just built from the text takes its note's bits. */
static void join_sref_take(unsigned kind,void *out) {
    const unsigned k=kind-1u,bytes=join_sref_bytes(kind);unsigned char mine[48];
    ++join_sref_read[k];
    if(!out || !read_local(out,mine,bytes)) {++join_sref_apart;return;}
    uint32_t at=join_sref_rx_at[k];unsigned tried=0;
    for(;at<join_sref_rx_n && tried<JOIN_SREF_AHEAD;at++) {
        if(join_sref_rx[at].kind!=kind) continue;
        if(join_sref_fits(kind,mine,join_sref_rx[at].value)) break;
        ++tried;
    }
    if(at>=join_sref_rx_n || tried>=JOIN_SREF_AHEAD) {if(tried) ++join_sref_apart;else ++join_sref_short;return;}   /* the place stays */
    join_sref_skipped+=tried;join_sref_rx_at[k]=at+1u;
    if(memcmp(mine,join_sref_rx[at].value,bytes)) {memcpy(out,join_sref_rx[at].value,bytes);++join_sref_set[k];}
}
static __attribute__((noinline)) void *join_sref_hook(void *a,void *b,void *c) {
    const uintptr_t ret=(uintptr_t)__builtin_return_address(0);unsigned i=0;
    while(i<4u && ret!=(uintptr_t)image+join_sref_site[i]+5u) ++i;
    if(i>=4u) return NULL;   /* no other call reaches the slot */
    if(i<2u) {join_sref_note(i?JOIN_SREF_POS:JOIN_SREF_ROT,a);return join_sref_call(i,a,b,c);}
    void *result=join_sref_call(i,a,b,c);
    if(join_sref_reading) join_sref_take(i==2u?JOIN_SREF_ROT:JOIN_SREF_POS,a);
    return result;
}
/* The saver's notes into its side table (after the join save, before the package is built). */
static void join_capture_sref(join_tables *t) {
    if(!join_sref_saved) return;
    join_sref_saved=0;
    const LONG n=join_sref_n;
    if(n<=0 || n>(LONG)JOIN_SREF_CAP || join_sref_over) return;
    t->sref=malloc((size_t)n*sizeof(*t->sref));if(!t->sref) return;
    memcpy(t->sref,join_sref_buf,(size_t)n*sizeof(*t->sref));t->nsref=(uint32_t)n;
}
#ifndef JOIN_OWN_PLATFORM
static const char *const join_sref_pin[4]={
    "43ffddd58f49658d4cec1ce59d69de57e15bc0c3016200be6040922a13246c58",   /* 0x136bc0, 0x1da: a rotation's attributes written */
    "e7c6a711dee60b0087ab3c1964e0e828fc81f1e99322ded62e9c865f58b5cd58",   /* 0x1369c0, 0xd2: a position's attributes written */
    "d47d33dc9251df096f60846c4ec43f75e1718d3233b1335d3c37f893efec251b",   /* 0x49cbb0, 0x22b: a rotation read */
    "38d5ffd77229abb224c2ffaa0843c05af5426361e150787082f10a2dab647a9b"};  /* 0x492ce0, 0x74: a position read */
static const uint32_t join_sref_pin_size[4]={0x1dau,0xd2u,0x22bu,0x74u};
/* Take the writers' calls (side 0) or the readers' (side 1); 1: both taken. */
static int join_sref_install(unsigned side) {
    if(!thunk || !thunk_slot_ready(JOIN_SREF_THUNK)) return 0;
    for(unsigned i=2u*side;i<2u*side+2u;i++) {
        if(!hash_ok(image+join_sref_entry[i],join_sref_pin_size[i],join_sref_pin[i])) return 0;
        if(!join_precise_take(&join_sref_site[i],1u,join_sref_entry[i],JOIN_SREF_THUNK,&join_sref_original[i],&join_sref_patched[i],&join_sref_taken[i],0)) {
            join_precise_give(&join_sref_site[2u*side],2u,&join_sref_original[2u*side],&join_sref_patched[2u*side],&join_sref_taken[2u*side]);
            return 0;
        }
    }
    return 1;
}
static void join_sref_restore(void) {join_precise_give(join_sref_site,4u,join_sref_original,join_sref_patched,join_sref_taken);}
#else
static int join_sref_install(unsigned side) {(void)side;return 0;}   /* the Linux gate: its own entries (not yet) */
static void join_sref_restore(void) {}
#endif
/* Around the saver's join save (its thread, inside the gate's boundary). */
static int join_sref_save_begin(void) {
    join_sref_saved=0;
    if(!join_sref_on) return 0;
    if(!join_sref_buf) join_sref_buf=malloc((size_t)JOIN_SREF_CAP*sizeof(join_sref_rec));
    if(!join_sref_buf || !join_sref_install(0)) return 0;
    InterlockedExchange(&join_sref_n,0);InterlockedExchange(&join_sref_over,0);join_sref_thread=GetCurrentThreadId();
    return 1;
}
static void join_sref_save_end(int saved) {
    join_sref_thread=0;join_sref_restore();
    join_sref_saved=saved && !join_sref_over;
}
/* Around the joiner's load. */
static void join_sref_load_begin(const join_tables *s) {
    join_sref_read[0]=join_sref_read[1]=join_sref_set[0]=join_sref_set[1]=join_sref_apart=join_sref_short=join_sref_skipped=0;
    join_sref_rx=NULL;join_sref_rx_n=0;join_sref_rx_at[0]=join_sref_rx_at[1]=0;
    if(!join_sref_on || s->version<13 || !s->nsref || !join_sref_install(1)) return;
    join_sref_rx=s->sref;join_sref_rx_n=s->nsref;InterlockedExchange(&join_sref_reading,1);
}
static void join_sref_load_end(void) {
    InterlockedExchange(&join_sref_reading,0);join_sref_restore();join_sref_rx=NULL;join_sref_rx_n=0;
}
