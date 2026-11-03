/* DEFAULT OFF: native_gate_ui_exports = 'observe' | 'block'. Which UI calls change the
 * simulation on one client only ('observe'), and refusing them ('block').
 *
 * X4's Lua UI reaches the engine through LuaJIT FFI: ffi.C.Name resolves through
 * GetProcAddress, which reads X4.exe's export address table (2378 named exports in 9.00;
 * engine code calls those functions directly, not through the table). After a savegame
 * load X4 builds a new UI Lua state, whose FFI symbols resolve again. So at arm (before the
 * snapshot load) every export whose name does not read as a query (Get/Is/Has/Can/...,
 * UX_QUERY below; 802 exports remain), and whose RVA lies in an executable section (no data
 * exports), gets its table entry redirected to a 16-byte stub
 * `mov eax, index; jmp common` on a page within 4 GB above the image (entries are 32-bit
 * RVAs). The common handler saves the argument registers (rcx, rdx, r8, r9, xmm0-3), calls
 * ux_record(index), restores them and tail-jumps to the original function: no stub frame
 * is on the stack while the engine function runs, so its exceptions unwind as before.
 *
 * ux_record counts the call when it happens in an admitted frame (boot phase 3) outside a
 * relay bracket (gate.ui_exports_relay(1/0) around the probe's own application of relayed
 * commands): a UI write the other clients do not see. Per export: calls; rows (UI_EXPORT_ROW
 * tick, name, thread class) for the first UX_ROWS_PER_EXPORT calls of each export and at most
 * UX_ROWS_PER_TICK per tick. Calls before the boot finishes count apart (boot=). The UI's
 * own presentation calls (map, menus, camera) are among them: the census is how the
 * simulation writes get told apart from those. 'block': an export marked with
 * gate.ui_exports_block(name) that would be counted is refused instead: the common handler
 * returns 0 (rax; the failure value of the bool/int/pointer setters) without calling it,
 * counted as blocked=. Unmarked exports are only counted, as in 'observe'.
 * Relay (with 'block'): an export given a kind string (gate.ui_exports_relay_kinds(name, kinds),
 * one letter per argument from its FFI declaration: U UniverseID, q 64-bit integer or other ID,
 * i 32-bit integer, b bool, f float, d double, s const char*) is refused the same way and its
 * call is captured instead: the arguments are read from the stub's frame (rcx/rdx/r8/r9 or
 * xmm0-3 by position, then the stack), a UniverseID becomes its portable key (saved, or created
 * with native_gate_portable_created; a call naming an object without one is refused, unportable=),
 * a string is copied (at most UX_STRING bytes), and the record (u16 export index, the encoded
 * arguments) is queued for the probe (gate.ui_exports_take). The probe sends it through the input
 * relay and every client, this one too, calls the export at the apply tick inside the relay
 * bracket (tools/dev/native_gate_ffi_relay.lua): the UI's write happens everywhere at one tick.
 * Fail closed: the export directory must lie inside the image and every patched entry
 * must still hold our stub's RVA at each gate tick (stop reason 195); restored at stop. */
#define UX_STOP_REASON 195u /* docs/LOCKSTEP-REGISTRY.txt */
#define UX_MAX 4096u
#define UX_STUB 16u
#define UX_RING 8192u
#define UX_ROWS_PER_EXPORT 4u
#define UX_ROWS_PER_TICK 64u
#define UX_PAGE_BYTES (UX_MAX*UX_STUB+0x1000u)
#define UX_KINDS 12u      /* arguments per relayed export */
#define UX_STRING 64u
#define UX_RECORD 200u    /* encoded bytes per captured call */
#define UX_QUEUE 64u
typedef struct {volatile LONG64 ready;uint64_t tick;uint32_t index;uint8_t thread,blocked;} ux_row_t;
enum {UX_OFF=0,UX_OBSERVE=1,UX_BLOCK=2};
static int ux_installed,ux_restored,ux_mode;
static uint8_t ux_block[UX_MAX];
static volatile LONG64 ux_blocked,ux_captured,ux_unportable,ux_too_long,ux_queue_full;
static char ux_kinds[UX_MAX][UX_KINDS+1];   /* relay kind string, "" = not relayed */
typedef struct {uint16_t length;unsigned char bytes[UX_RECORD];} ux_capture_t;
static ux_capture_t ux_queue[UX_QUEUE];
static unsigned ux_queue_head,ux_queue_tail;  /* UI thread only: capture and take both run there */
static uint64_t (*ux_portable_fn)(uint64_t runtime); /* tests substitute */
static const char *ux_stage="idle";
static unsigned ux_count; /* hooked exports */
static uint32_t ux_slot[UX_MAX],ux_original[UX_MAX]; /* index into AddressOfFunctions, the original RVA */
static const char *ux_name[UX_MAX];
static void *ux_target[UX_MAX]; /* absolute originals, read by the common handler */
static volatile LONG64 ux_calls[UX_MAX],ux_boot_calls,ux_relayed,ux_rows,ux_dropped,ux_tick_rows;
static uint64_t ux_tick_seen;
static volatile LONG ux_relay_depth;
static unsigned char *ux_page;
static uint32_t *ux_functions; /* the image's AddressOfFunctions */
static ux_row_t ux_ring[UX_RING];
static const char *const UX_QUERY[]={"Get","Is","Has","Can","Are","Was","Does","Find","Count","Check","Query","Compare","Convert","Should","Will","Read"};
static int ux_is_query(const char *name) {
    for(unsigned i=0;i<sizeof(UX_QUERY)/sizeof(UX_QUERY[0]);i++) {
        size_t n=strlen(UX_QUERY[i]);
        if(!strncmp(name,UX_QUERY[i],n) && (name[n]<'a' || name[n]>'z')) return 1; /* "Is" in IsX, not in "Issue" */
    }
    return 0;
}
/* The common handler's frame (rsp after its sub): xmm0-3 at 0x20/0x30/0x40/0x50, r9 0x68, r8 0x70,
 * rdx 0x78, rcx 0x80, the return address 0x88, the caller's home area 0x90, stack arguments from 0xb0. */
static uint64_t ux_arg_int(const unsigned char *frame,unsigned i) {
    static const unsigned reg[4]={0x80,0x78,0x70,0x68};uint64_t v;
    memcpy(&v,frame+(i<4?reg[i]:0xb0u+8u*(i-4u)),8);return v;
}
static uint64_t ux_arg_float(const unsigned char *frame,unsigned i) {
    uint64_t v;memcpy(&v,frame+(i<4?0x20u+16u*i:0xb0u+8u*(i-4u)),8);return v;
}
static uint64_t ux_portable_default(uint64_t runtime) {unsigned klass;return world_identity_key(runtime,&klass);}
/* Encode one captured call; 0 when it cannot be carried (unportable object, long string). */
static unsigned ux_encode(uint32_t index,const unsigned char *frame,unsigned char *out) {
    const char *k=ux_kinds[index];unsigned n=0;
    out[n++]=(unsigned char)(index&0xff);out[n++]=(unsigned char)(index>>8);
    for(unsigned i=0;k[i];i++) {
        uint64_t v=(k[i]=='f' || k[i]=='d')?ux_arg_float(frame,i):ux_arg_int(frame,i);
        if(k[i]=='U') {
            uint64_t key=v?(ux_portable_fn?ux_portable_fn:ux_portable_default)(v):0;
            if(v && !key) {InterlockedIncrement64(&ux_unportable);return 0;}
            v=key;
        }
        if(k[i]=='s') {
            const char *text=(const char*)(uintptr_t)v;size_t length=0;
            if(text) {while(length<=UX_STRING && text[length]) ++length;}
            if(length>UX_STRING || n+1+length>UX_RECORD) {InterlockedIncrement64(&ux_too_long);return 0;}
            out[n++]=text?(unsigned char)length:0xffu;
            if(text) {memcpy(out+n,text,length);n+=(unsigned)length;}
            continue;
        }
        unsigned bytes=k[i]=='b'?1u:(k[i]=='i' || k[i]=='f')?4u:8u;
        if(n+bytes>UX_RECORD) {InterlockedIncrement64(&ux_too_long);return 0;}
        memcpy(out+n,&v,bytes);n+=bytes;
    }
    return n;
}
static void ux_capture(uint32_t index,const unsigned char *frame) {
    unsigned char bytes[UX_RECORD];unsigned n=ux_encode(index,frame,bytes);
    if(!n) return;
    if(ux_queue_tail-ux_queue_head>=UX_QUEUE) {InterlockedIncrement64(&ux_queue_full);return;}
    ux_capture_t *c=&ux_queue[ux_queue_tail%UX_QUEUE];c->length=(uint16_t)n;memcpy(c->bytes,bytes,n);++ux_queue_tail;
    InterlockedIncrement64(&ux_captured);
}
/* 1: call the export; 0: refuse it (the common handler returns 0). */
static uint32_t ux_record(uint32_t index,const unsigned char *frame) {
    if(index>=ux_count) return 1;
    if(!(stats.active && boot_phase==3)) {InterlockedIncrement64(&ux_boot_calls);return 1;}
    if(InterlockedCompareExchange(&ux_relay_depth,0,0)) {InterlockedIncrement64(&ux_relayed);return 1;}
    LONG64 n=InterlockedIncrement64(&ux_calls[index]);
    int relay=ux_mode==UX_BLOCK && ux_kinds[index][0] && GetCurrentThreadId()==ui_thread;
    int refuse=ux_mode==UX_BLOCK && (ux_block[index] || relay);
    if(refuse) InterlockedIncrement64(&ux_blocked);
    if(relay) ux_capture(index,frame);
    if(n>(LONG64)UX_ROWS_PER_EXPORT) return !refuse;
    uint64_t tick=stats.issued;
    if(tick!=ux_tick_seen) {ux_tick_seen=tick;InterlockedExchange64(&ux_tick_rows,0);}
    if(InterlockedIncrement64(&ux_tick_rows)>(LONG64)UX_ROWS_PER_TICK) {InterlockedIncrement64(&ux_dropped);return !refuse;}
    LONG64 at=InterlockedIncrement64(&ux_rows)-1;ux_row_t *r=&ux_ring[(uint64_t)at%UX_RING];
    InterlockedExchange64(&r->ready,-1);
    r->tick=tick;r->index=index;r->thread=(uint8_t)(GetCurrentThreadId()==ui_thread?0:GetCurrentThreadId()==main_thread?1:2);
    r->blocked=(uint8_t)refuse;
    InterlockedExchange64(&r->ready,at);
    return !refuse;
}
static uint32_t (*const ux_record_ptr)(uint32_t,const unsigned char *)=ux_record;
/* The common handler: entry rsp = 8 mod 16 (return address), eax = the export's index. */
static const unsigned char ux_common_code[]={
    0x51,0x52,0x41,0x50,0x41,0x51,                         /* push rcx; push rdx; push r8; push r9 */
    0x48,0x83,0xec,0x68,                                   /* sub rsp,0x68: shadow 0x20, xmm 0x40, index +0x60; rsp = 0 mod 16 */
    0xf3,0x0f,0x7f,0x44,0x24,0x20,                         /* movdqu [rsp+0x20],xmm0 */
    0xf3,0x0f,0x7f,0x4c,0x24,0x30,                         /* movdqu [rsp+0x30],xmm1 */
    0xf3,0x0f,0x7f,0x54,0x24,0x40,                         /* movdqu [rsp+0x40],xmm2 */
    0xf3,0x0f,0x7f,0x5c,0x24,0x50,                         /* movdqu [rsp+0x50],xmm3 */
    0x48,0x89,0x44,0x24,0x60,                              /* mov [rsp+0x60],rax */
    0x89,0xc1,                                             /* mov ecx,eax */
    0x48,0x89,0xe2,                                        /* mov rdx,rsp: the frame */
    0x48,0xb8,0,0,0,0,0,0,0,0,                             /* mov rax,imm64 (ux_record) at +0x2e */
    0xff,0xd0,                                             /* call rax */
    0x41,0x89,0xc2,                                        /* mov r10d,eax: the decision */
    0x48,0x8b,0x44,0x24,0x60,                              /* mov rax,[rsp+0x60] */
    0xf3,0x0f,0x6f,0x44,0x24,0x20,                         /* movdqu xmm0,[rsp+0x20] */
    0xf3,0x0f,0x6f,0x4c,0x24,0x30,
    0xf3,0x0f,0x6f,0x54,0x24,0x40,
    0xf3,0x0f,0x6f,0x5c,0x24,0x50,
    0x48,0x83,0xc4,0x68,                                   /* add rsp,0x68 */
    0x41,0x59,0x41,0x58,0x5a,0x59,                         /* pop r9; pop r8; pop rdx; pop rcx */
    0x45,0x85,0xd2,                                        /* test r10d,r10d */
    0x74,0x10,                                             /* jz refuse */
    0x49,0xbb,0,0,0,0,0,0,0,0,                             /* mov r11,imm64 (ux_target) at +0x69 */
    0x89,0xc0,                                             /* mov eax,eax (zero-extend the index) */
    0x41,0xff,0x24,0xc3,                                   /* jmp [r11+rax*8] */
    0x31,0xc0,0xc3,                                        /* refuse: xor eax,eax; ret */
};
#define UX_COMMON_RECORD 0x2eu
#define UX_COMMON_TARGET 0x69u
/* Page layout: common handler at 0, stubs from 0x1000 (16 bytes each). */
static void ux_write_code(unsigned char *page,unsigned count) {
    memset(page,0xcc,UX_PAGE_BYTES);memcpy(page,ux_common_code,sizeof(ux_common_code));
    void *record=(void*)ux_record_ptr,*targets=(void*)ux_target;
    memcpy(page+UX_COMMON_RECORD,&record,8);memcpy(page+UX_COMMON_TARGET,&targets,8);
    for(unsigned i=0;i<count;i++) {
        unsigned char *s=page+0x1000u+i*UX_STUB;
        s[0]=0xb8;memcpy(s+1,&i,4);                         /* mov eax,i */
        int32_t rel=(int32_t)((intptr_t)page-(intptr_t)(s+10));s[5]=0xe9;memcpy(s+6,&rel,4); /* jmp common */
    }
}
static unsigned char *ux_alloc_page(unsigned char *base,size_t size) {
    uintptr_t start=((uintptr_t)base+size+0xffffu)&~(uintptr_t)0xffff;
    for(uintptr_t at=start;at+UX_PAGE_BYTES-(uintptr_t)base<0xffff0000u;at+=0x10000u) {
        unsigned char *p=VirtualAlloc((void*)at,UX_PAGE_BYTES,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
        if(p) return p;
    }
    return NULL;
}
/* The export directory of `base` (an image in memory): functions, names, ordinals, count; 0 if malformed. */
static int ux_directory(unsigned char *base,size_t size,uint32_t **functions,uint32_t **names,uint16_t **ordinals,uint32_t *count,uint32_t *nfunctions) {
    IMAGE_DOS_HEADER *dos=(IMAGE_DOS_HEADER*)base;
    if(size<sizeof(*dos) || dos->e_magic!=IMAGE_DOS_SIGNATURE || (uint32_t)dos->e_lfanew>size-sizeof(IMAGE_NT_HEADERS64)) return 0;
    IMAGE_NT_HEADERS64 *nt=(IMAGE_NT_HEADERS64*)(base+dos->e_lfanew);
    IMAGE_DATA_DIRECTORY d=nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
    if(!d.VirtualAddress || d.Size<sizeof(IMAGE_EXPORT_DIRECTORY) || d.VirtualAddress>size-sizeof(IMAGE_EXPORT_DIRECTORY)) return 0;
    IMAGE_EXPORT_DIRECTORY *e=(IMAGE_EXPORT_DIRECTORY*)(base+d.VirtualAddress);
    if(e->AddressOfFunctions>size || e->AddressOfNames>size || e->AddressOfNameOrdinals>size ||
       (uint64_t)e->NumberOfFunctions*4>size-e->AddressOfFunctions || (uint64_t)e->NumberOfNames*4>size-e->AddressOfNames) return 0;
    *functions=(uint32_t*)(base+e->AddressOfFunctions);*names=(uint32_t*)(base+e->AddressOfNames);
    *ordinals=(uint16_t*)(base+e->AddressOfNameOrdinals);*count=e->NumberOfNames;*nfunctions=e->NumberOfFunctions;return 1;
}
/* 1 if rva lies in an executable section of base: data exports (variables FFI reads) are never redirected. */
static int ux_code_rva(unsigned char *base,uint32_t rva) {
    IMAGE_NT_HEADERS64 *nt=(IMAGE_NT_HEADERS64*)(base+((IMAGE_DOS_HEADER*)base)->e_lfanew);
    IMAGE_SECTION_HEADER *sec=IMAGE_FIRST_SECTION(nt);
    for(unsigned i=0;i<nt->FileHeader.NumberOfSections;i++)
        if((sec[i].Characteristics&IMAGE_SCN_MEM_EXECUTE) && rva>=sec[i].VirtualAddress && rva-sec[i].VirtualAddress<sec[i].Misc.VirtualSize) return 1;
    return 0;
}
static int ux_set_entry(uint32_t *entry,uint32_t value) {
    DWORD old,ignored;
    if(!VirtualProtect(entry,4,PAGE_READWRITE,&old)) return 0;
    InterlockedExchange((volatile LONG*)entry,(LONG)value);
    VirtualProtect(entry,4,old,&ignored);return 1;
}
static void ux_restore(void) {
    if(!ux_installed) return;
    unsigned left=0;
    for(unsigned i=0;i<ux_count;i++) {
        uint32_t stub=(uint32_t)(ux_page+0x1000u+i*UX_STUB-image);
        if(ux_functions[ux_slot[i]]==stub && !ux_set_entry(&ux_functions[ux_slot[i]],ux_original[i])) ++left;
    }
    if(!left) {ux_installed=0;ux_restored=1;ux_stage="restored";} else ux_stage="restore_failed";
}
/* Mark an export for 'block' by name; 1 if it is hooked. */
static int ux_find(const char *name,size_t length) {
    for(unsigned i=0;i<ux_count;i++) if(strlen(ux_name[i])==length && !memcmp(ux_name[i],name,length)) return (int)i;
    return -1;
}
static int ux_mark_block(const char *name,size_t length) {
    int i=ux_find(name,length);if(i<0) return 0;
    ux_block[i]=1;return 1;
}
/* Give a hooked export its relay kind string; 1 on success. */
static int ux_mark_relay(const char *name,size_t length,const char *kinds,size_t count) {
    int i=ux_find(name,length);
    if(i<0 || count>UX_KINDS || strspn(kinds,"Uqibfds")<count) return 0;
    memcpy(ux_kinds[i],kinds,count);ux_kinds[i][count]=0;return 1;
}
/* The oldest captured call as lowercase hex (index then arguments); 0 when none. UI thread. */
static int ux_take(char *out,size_t cap) {
    if(ux_queue_head==ux_queue_tail) return 0;
    const ux_capture_t *c=&ux_queue[ux_queue_head%UX_QUEUE];
    if((size_t)c->length*2+1>cap) return 0;
    for(unsigned i=0;i<c->length;i++) snprintf(out+2*i,3,"%02x",c->bytes[i]);
    ++ux_queue_head;return (int)c->length*2;
}
/* gate.ui_exports('observe'|'block'): UI thread, before the snapshot load. */
static int ux_arm_using(unsigned char *base,size_t size) {
    if(ux_installed) return 1;
    memset(ux_block,0,sizeof(ux_block));memset(ux_kinds,0,sizeof(ux_kinds));
    ux_blocked=ux_captured=ux_unportable=ux_too_long=ux_queue_full=0;ux_queue_head=ux_queue_tail=0;
    uint32_t *functions,*names,count,nfunctions;uint16_t *ordinals;
    if(!ux_directory(base,size,&functions,&names,&ordinals,&count,&nfunctions)) {ux_stage="directory";return 0;}
    unsigned n=0;
    for(uint32_t i=0;i<count;i++) {
        if(names[i]>=size || ordinals[i]>=nfunctions) {ux_stage="names";return 0;}
        const char *name=(const char*)base+names[i];uint32_t rva=functions[ordinals[i]];
        if(ux_is_query(name) || !rva || rva>=size || !ux_code_rva(base,rva)) continue;
        if(n>=UX_MAX) {ux_stage="capacity";return 0;}
        ux_slot[n]=ordinals[i];ux_original[n]=rva;ux_name[n]=name;ux_target[n]=base+rva;++n;
    }
    if(!ux_page) ux_page=ux_alloc_page(base,size);
    DWORD old;
    if(!ux_page || !VirtualProtect(ux_page,UX_PAGE_BYTES,PAGE_READWRITE,&old)) {ux_stage="page";return 0;}
    ux_write_code(ux_page,n);
    if(!VirtualProtect(ux_page,UX_PAGE_BYTES,PAGE_EXECUTE_READ,&old)) {ux_stage="page";return 0;}
    FlushInstructionCache(GetCurrentProcess(),ux_page,UX_PAGE_BYTES);
    ux_functions=functions;ux_count=n;
    for(unsigned i=0;i<n;i++) {
        uint32_t stub=(uint32_t)(ux_page+0x1000u+i*UX_STUB-base);
        if(functions[ux_slot[i]]!=ux_original[i] || !ux_set_entry(&functions[ux_slot[i]],stub)) {ux_installed=1;ux_count=i;ux_restore();ux_stage="patch";return 0;}
    }
    ux_installed=1;ux_restored=0;ux_stage="armed";return 1;
}
static int ux_arm(int mode) {
    if(mode!=UX_OBSERVE && mode!=UX_BLOCK) {ux_stage="mode";return 0;}
    if(ux_installed) return stats.active && ux_mode==mode;
    ux_mode=mode;
    if(!stats.active || !boot_mode || boot_phase || stats.issued || GetCurrentThreadId()!=ui_thread) {ux_stage="state";return 0;}
    return ux_arm_using(image,0x6fa9000u);
}
static int ux_check(void) {
    if(!ux_installed) return 1;
    for(unsigned i=0;i<ux_count;i++) if(ux_functions[ux_slot[i]]!=(uint32_t)(ux_page+0x1000u+i*UX_STUB-image)) {ux_stage="entry_changed";return 0;}
    return 1;
}
static int ux_row_text(uint64_t index,char *out,size_t cap) {
    LONG64 count=InterlockedCompareExchange64(&ux_rows,0,0);int n;
    if((LONG64)index>=count) return 0;
    if(count>(LONG64)UX_RING && index<(uint64_t)(count-(LONG64)UX_RING))
        return (n=snprintf(out,cap,"index=%llu evicted=1",(unsigned long long)index))>0 && (size_t)n<cap?n:0;
    const ux_row_t *r=&ux_ring[index%UX_RING];
    if(InterlockedCompareExchange64((volatile LONG64*)&r->ready,0,0)!=(LONG64)index) return 0;
    static const char *const threads[3]={"ui","main","other"};
    n=snprintf(out,cap,"index=%llu tick=%llu name=%s thread=%s blocked=%u",(unsigned long long)index,(unsigned long long)r->tick,
        r->index<ux_count?ux_name[r->index]:"?",threads[r->thread<3?r->thread:2],(unsigned)r->blocked);
    return n>0 && (size_t)n<cap?n:0;
}
static int ux_status(char *out,size_t cap) {
    uint64_t total=0,distinct=0;
    for(unsigned i=0;i<ux_count;i++) {uint64_t c=(uint64_t)ux_calls[i];total+=c;distinct+=c!=0;}
    unsigned marked=0,relay=0;for(unsigned i=0;i<ux_count;i++) {marked+=ux_block[i];relay+=ux_kinds[i][0]!=0;}
    int n=snprintf(out,cap,"installed=%d mode=%s hooked=%u marked=%u relay=%u captured=%lld unportable=%lld too_long=%lld queue_full=%lld calls=%llu exports_called=%llu blocked=%lld boot=%lld relayed=%lld rows=%lld dropped=%lld restored=%d stage=%s",
        ux_installed,ux_mode==UX_BLOCK?"block":ux_mode==UX_OBSERVE?"observe":"off",ux_count,marked,relay,(long long)ux_captured,
        (long long)ux_unportable,(long long)ux_too_long,(long long)ux_queue_full,(unsigned long long)total,(unsigned long long)distinct,(long long)ux_blocked,(long long)ux_boot_calls,(long long)ux_relayed,
        (long long)ux_rows,(long long)ux_dropped,ux_restored,ux_stage);
    return n>0 && (size_t)n<cap?n:0;
}
