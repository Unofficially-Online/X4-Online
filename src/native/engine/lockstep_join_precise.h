/* The join save at full precision (native_gate_join_precise, DEFAULT OFF; report 1.18).
 *
 * The save keeps every double to six significant digits (three decimals from 1000 up): its attribute writer 0x138380 and the
 * script values go through one conversion, 0x13eb20 (out std::string, value, precision; precision >= 100: six significant digits,
 * or the integer digits and three; precision p < 100: p + 1 significant digits below 1, the integer digits and p above), and the
 * load reads them back through the XML reader 0x12fd30's decimal parser 0x14c3090 (digits accumulated with scale *= 0.1, not
 * correctly rounded). A joiner therefore starts from rounded times everywhere [F: j1-91 .. j1-113: lastshottime, attacktime,
 * the AI scripts' $waittime (45,175 values of type time in j633), the bullets' times], where SHOOT, ATTK and the like correct
 * them one kind at a time.
 *
 * The join save is written for the joiner only. With this, while the saver's join save runs on its thread, the 34 calls of the
 * conversion go through the shared thunk slot JOIN_FMT_THUNK (a rel32 swap each: by CAS, the two whose rel32 crosses a 64-byte line
 * by a plain store, which is safe there because the save runs inside the gate's boundary, no engine frame running) and it writes 17
 * significant digits, which name every double uniquely; from join_load to the admission the joiner's 89 calls of the parser go
 * through JOIN_PARSE_THUNK (by CAS; the two crossing ones stay native) and a string of 16 or more significant digits is read
 * exactly (strtod's candidate and its neighbours, the one whose 17 digits are the string's), every shorter one, as the game's data
 * writes them, by the native parser. A float goes through as its double and back to the same float. Pins: both bodies, every
 * site's call to its entry. Counters in JOIN_STATUS precise=. */
#define JOIN_FMT_ENTRY 0x13eb20u
#define JOIN_FMT_THUNK 112u
#define JOIN_PARSE_ENTRY 0x14c3090u
#define JOIN_PARSE_THUNK 1008u
static const uint32_t join_fmt_sites[]={
    0x13839au,0x13be6du,0x3b5a1du,0x3b5d3cu,0x4cd892u,0x64bb73u,0x7b4618u,0x7b4f83u,0x7b58f3u,0x7b6260u,0x8d1d99u,
    0x9a10dau,0x9e5d68u,0xa0bc80u,0xa0bcc3u,0xa0bd06u,0xa0bfbfu,0xa0c002u,0xa0c045u,0xa23c59u,0xc3557au,0xcc1cf6u,
    0xcc1d59u,0xcc1dbcu,0xcc35e3u,0xcc3682u,0xcc3721u,0xccbd39u,0xe30cb1u,0xf4548cu,0xf454edu,0x12308bbu,0x125f1e5u,
    0x125f620u
};
static const uint32_t join_parse_sites[]={
    0x106d18u,0x128a90u,0x12fdadu,0x376ba9u,0x376e72u,0x3f10c7u,0x48ace4u,0x48b6fcu,0x4cbb9bu,0x4cbddbu,0x5eab40u,
    0x621af4u,0x621b78u,0x621c84u,0x621d10u,0x630d7au,0x64ad2bu,0x64ada9u,0x64afabu,0x64b029u,0x67da29u,0x67e4cau,
    0x69fc87u,0x6a3c31u,0x6ad71bu,0x6fc292u,0x6fcd27u,0x6fcdb8u,0x726014u,0x7286a4u,0x7297e8u,0x72ab86u,0x73d631u,
    0x87a14du,0x8d06a1u,0x8d31d2u,0x909afeu,0x94e956u,0x9be594u,0x9be844u,0x9beaf4u,0x9beda4u,0x9bef74u,0x9d1529u,
    0x9e4894u,0x9e5101u,0xa07402u,0xa07487u,0xa07508u,0xa077f5u,0xa0787au,0xa078e2u,0xa23797u,0xa7a775u,0xa7a7f0u,
    0xa7aa0au,0xa7b579u,0xa7b5f4u,0xa7b820u,0xa7c3c9u,0xa7c444u,0xa7c670u,0xa7d1b9u,0xa7d234u,0xa7d460u,0xa7dfa9u,
    0xa7e024u,0xa7e244u,0xd846b4u,0xe2ff8cu,0xe34264u,0xee52d6u,0xef7645u,0xf45846u,0xf458c8u,0x10dc1e6u,0x10e0624u,
    0x10e10e6u,0x10e4005u,0x10eae0eu,0x10ef151u,0x10f3e6du,0x10f40eau,0x10fdf75u,0x110c40eu,0x11672bau,0x12298cbu,0x122f352u,
    0x1258286u
};
#define JOIN_FMT_SITES (sizeof(join_fmt_sites)/sizeof(join_fmt_sites[0]))
#define JOIN_PARSE_SITES (sizeof(join_parse_sites)/sizeof(join_parse_sites[0]))
static int join_precise_on;   /* native_gate_join_precise: both sides */
static volatile DWORD join_fmt_thread;   /* the saver's thread while its join save runs, 0 otherwise */
static volatile LONG join_parse_on;
static volatile LONG64 join_fmt_calls,join_fmt_raised,join_parse_calls,join_parse_long,join_parse_exact,join_parse_far;
static int32_t join_fmt_original[JOIN_FMT_SITES],join_parse_original[JOIN_PARSE_SITES],join_fmt_patched[JOIN_FMT_SITES],
               join_parse_patched[JOIN_PARSE_SITES];
static unsigned char join_fmt_taken[JOIN_FMT_SITES],join_parse_taken[JOIN_PARSE_SITES];
static unsigned join_fmt_sites_on,join_parse_sites_on;

/* The precision argument that makes the conversion write 17 significant digits of v (finite, not 0). */
static unsigned join_fmt_precision(double v) {
    const double a=fabs(v);
    if(a<1.0) return 16u;
    const int d=(int)floor(log10(a));
    return d>=16?0u:(unsigned)(16-d);
}
typedef void *(*join_fmt_fn)(void *,double,unsigned);
static void *join_fmt_native(void *out,double v,unsigned precision) {return ((join_fmt_fn)(void *)(image+JOIN_FMT_ENTRY))(out,v,precision);}
static void *(*join_fmt_next)(void *,double,unsigned)=join_fmt_native;   /* tests replace it */
static __attribute__((noinline)) void *join_fmt_hook(void *out,double v,unsigned precision) {
    InterlockedIncrement64(&join_fmt_calls);
    if(join_fmt_thread && join_fmt_thread==GetCurrentThreadId() && isfinite(v) && v!=0.0) {
        precision=join_fmt_precision(v);InterlockedIncrement64(&join_fmt_raised);
    }
    return join_fmt_next(out,v,precision);
}

/* A decimal string's significant digits (up to 32) and its decimal exponent (value = 0.d1d2.. x 10^exp); 0: not a plain decimal. */
static int join_precise_digits(const char *s,char *digits,unsigned *n,int *exp10) {
    unsigned k=0;int point=-1,lead=1,e=0,before=0;const char *p=s;
    if(*p=='-' || *p=='+') ++p;
    for(;;++p) {
        if(*p=='.') {if(point>=0) return 0;point=0;continue;}
        if(*p<'0' || *p>'9') break;
        if(lead && *p=='0') {if(point>=0) --e;continue;}
        lead=0;
        if(point<0) ++before;
        if(k<32u) digits[k++]=*p;
    }
    if(lead) return 0;   /* zero: the native parser */
    if(*p=='e' || *p=='E') {
        int sign=1,x=0,any=0;++p;
        if(*p=='-' || *p=='+') {if(*p=='-') sign=-1;++p;}
        for(;*p>='0' && *p<='9';++p) {x=x*10+(*p-'0');any=1;if(x>100000) return 0;}
        if(!any) return 0;
        e+=sign*x;
    }
    if(*p && *p!=';' && *p!=' ') return 0;
    while(k>1 && digits[k-1]=='0') --k;
    *n=k;*exp10=before+e;return 1;
}
/* Does v's 17-digit decimal form name these digits? */
static int join_precise_names(double v,const char *digits,unsigned n,int exp10) {
    char b[64],mine[32];unsigned k=0;int e=0;
    if(snprintf(b,sizeof(b),"%.16e",v)<=0) return 0;
    const char *p=b;if(*p=='-') ++p;
    for(;*p && *p!='e';++p) if(*p>='0' && *p<='9' && k<32u) mine[k++]=*p;
    if(*p!='e') return 0;
    e=atoi(p+1)+1;
    while(k>1 && mine[k-1]=='0') --k;
    return k==n && e==exp10 && !memcmp(mine,digits,n);
}
typedef double (*join_parse_fn)(const char *);
static double join_parse_native(const char *s) {return ((join_parse_fn)(void *)(image+JOIN_PARSE_ENTRY))(s);}
static double (*join_parse_next)(const char *)=join_parse_native;   /* tests replace it */
static __attribute__((noinline)) double join_parse_hook(const char *s) {
    InterlockedIncrement64(&join_parse_calls);
    const double native=join_parse_next(s);
    char digits[32];unsigned n=0;int e=0;
    if(!join_parse_on || !s || !join_precise_digits(s,digits,&n,&e) || n<16u) return native;
    InterlockedIncrement64(&join_parse_long);
    double v=strtod(s,NULL);
    if(!isfinite(v)) {InterlockedIncrement64(&join_parse_far);return native;}
    double c=v;
    for(int step=0;step<4;step++) c=nextafter(c,-INFINITY);
    for(int step=0;step<9;step++,c=nextafter(c,INFINITY))
        if(join_precise_names(c,digits,n,e)) {InterlockedIncrement64(&join_parse_exact);return c;}
    InterlockedIncrement64(&join_parse_far);return v;
}

#ifndef JOIN_OWN_PLATFORM
/* A call's rel32 at site+1: by one CAS inside a 64-byte line; across one only with split, by a plain store. 1 swapped. */
static int join_precise_rel(uint32_t site,int32_t expected,int32_t value,int split) {
    unsigned char *rel=image+site+1;DWORD old,ignored;int32_t actual=0;
    const int crossing=((uintptr_t)rel&63)>60;
    if(crossing && !split) return 0;
    if(!VirtualProtect(rel,4,PAGE_EXECUTE_READWRITE,&old)) return 0;
    if(crossing) {memcpy(&actual,rel,4);if(actual==expected) memcpy(rel,&value,4);}
    else actual=(int32_t)InterlockedCompareExchange((volatile LONG*)rel,(LONG)value,(LONG)expected);
    VirtualProtect(rel,4,old,&ignored);FlushInstructionCache(GetCurrentProcess(),image+site,5);
    return actual==expected;
}
static unsigned join_precise_take(const uint32_t *sites,unsigned n,uint32_t entry,unsigned slot,int32_t *original,int32_t *patched,
                                  unsigned char *taken,int split) {
    unsigned on=0;
    for(unsigned i=0;i<n;i++) {
        unsigned char call[5];int32_t rel;
        if(taken[i]) {++on;continue;}
        if(!read_local(image+sites[i],call,5) || call[0]!=0xe8) continue;
        memcpy(&rel,call+1,4);
        const intptr_t distance=(intptr_t)(thunk+slot)-(intptr_t)(image+sites[i]+5);
        if((uintptr_t)(image+sites[i]+5)+(intptr_t)rel!=(uintptr_t)(image+entry) || distance<INT32_MIN || distance>INT32_MAX) continue;
        original[i]=rel;patched[i]=(int32_t)distance;
        if(join_precise_rel(sites[i],original[i],patched[i],split)) {taken[i]=1;++on;}
    }
    return on;
}
static void join_precise_give(const uint32_t *sites,unsigned n,const int32_t *original,const int32_t *patched,unsigned char *taken) {
    for(unsigned i=0;i<n;i++) if(taken[i] && join_precise_rel(sites[i],patched[i],original[i],1)) taken[i]=0;
}
static int join_fmt_install(void) {
    if(!thunk || !thunk_slot_ready(JOIN_FMT_THUNK) ||
       !hash_ok(image+JOIN_FMT_ENTRY,268,"ae75a2da67f1e1c8b1b4a1ec1dc64f999ce39c4e33ac0e71e1332ff22adf3fb6")) return 0;
    join_fmt_sites_on=join_precise_take(join_fmt_sites,(unsigned)JOIN_FMT_SITES,JOIN_FMT_ENTRY,JOIN_FMT_THUNK,join_fmt_original,
                                        join_fmt_patched,join_fmt_taken,1);
    return join_fmt_sites_on!=0;
}
static void join_fmt_restore(void) {join_precise_give(join_fmt_sites,(unsigned)JOIN_FMT_SITES,join_fmt_original,join_fmt_patched,join_fmt_taken);}
static int join_parse_install(void) {
    if(!thunk || !thunk_slot_ready(JOIN_PARSE_THUNK) ||
       !hash_ok(image+JOIN_PARSE_ENTRY,1053,"5b57e095e34143eb960c1d9050a3f1f7afaaca335d1340dec1fb730ead7636be")) return 0;
    join_parse_sites_on=join_precise_take(join_parse_sites,(unsigned)JOIN_PARSE_SITES,JOIN_PARSE_ENTRY,JOIN_PARSE_THUNK,join_parse_original,
                                          join_parse_patched,join_parse_taken,0);
    return join_parse_sites_on!=0;
}
static void join_parse_restore(void) {join_precise_give(join_parse_sites,(unsigned)JOIN_PARSE_SITES,join_parse_original,join_parse_patched,join_parse_taken);}
#else
static int join_fmt_install(void) {return 0;}   /* the Linux gate: its own entries (not yet) */
static void join_fmt_restore(void) {}
static int join_parse_install(void) {return 0;}
static void join_parse_restore(void) {}
#endif
