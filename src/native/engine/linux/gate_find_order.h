/* find_* candidate lists on saved IDs on the Linux X4 9.00 executable: the Linux twin of
 * lockstep_find_order.h, whose canonical order, rows and status are shared. Included by
 * gate_linux.c only, after the script RNG, whose scope names a row. The mechanism and the census
 * evidence are in that header.
 *
 * Linux sites: Scripts::FindComponentAction vtable 0x3b00a68, Execute 0xd87ad0 in slot 4 (Windows 3,
 * one more destructor), builds the candidate vector at rbp-0x40 and calls the result store 0x1d2b330
 * at 0xd87b33 with rdi the declaration, rsi the vector, rdx the Exec, rcx the component. The rel32
 * lies in the aligned 8-byte word 0xd87b30, so that word changes in one compare-exchange.
 *
 * Windows tail-jumps to the store so an observer behind it sees the native return address. No such
 * observer is ported, so this hook calls it.
 *
 * The second site, the range finds (find_sector_in_range / find_cluster_in_range; lockstep_find_order.h):
 * Scripts::FindSpaceInRangeAction vtable 0x3b00918, Execute 0xda9920 (slot 4, 0x23c0 bytes) collects the spaces into
 * the same tree on its stack (rsp+0x120: +8 root, +0x20 leftmost, +0x28 count; node +0 parent, +8 left, +0x10 right,
 * +0x20 Component*, +0x28 jumps), keyed by the Component* value, so the result is in the process's address order.
 * GCC inlines Windows' store 0xc602e0 and its walk 0xd2ada0 (the Random::U64 pick at 0xdaad06), entered at 0xdaa0e5
 * and 0xdaa348; both begin with the same 12 bytes (pxor xmm0,xmm0; movaps [rsp+0x160],xmm0) and nothing jumps
 * inside them. Each entry's first 5 bytes become a jmp rel32 (one 16-byte line, one CAS) to a stub that keeps the
 * red zone, the caller-saved registers, the flags and the FPU/SSE state (fxsave64), calls the shared sort with the
 * tree, then does the 12 bytes and returns to entry+12. W30 against lx20: the one object that differed (a salvage
 * ship) searched 300 ticks later on Linux after its sector pick (lib.find.sectors.inrange, site=space rows on
 * Windows only). Pin: find_space_execute (start); the per-tick ownership check reads the first entry's rel32. */
#define FIND_ORDER_OWN_PLATFORM
#define FIND_ORDER_SITE (LX_FIND_ORDER_SITE_VA-LX_IMAGE_BASE)
#define FIND_ORDER_ASSIGN (LX_FIND_ORDER_ASSIGN_VA-LX_IMAGE_BASE)
#define FIND_ORDER_THUNK 0x580u /* jmp [rip] -> find_order_assign */
#define FIND_ORDER_SPACE_OWN_SITE
#define FIND_ORDER_SPACE_SITE (LX_FIND_SPACE_SITE_A_VA-LX_IMAGE_BASE)
#define FIND_ORDER_SPACE_THUNK 0u /* unused: the stubs have their own page */
static int find_order_space_pinned(void);
static int find_order_space_swap(int32_t expected,int32_t value);
static int find_order_space_prepare(void);
#define __try
#define __except(filter) if(0)
#define LONG long /* the status prints the active word with %ld */
#define thunk_slot_ready(offset) 1
static int find_order_pinned(void) {return 1;} /* the gate's site pins, checked at start */
static int find_order_swap(int32_t expected,int32_t value);
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#include "lockstep_find_order.h"
#pragma GCC diagnostic pop
#undef __try
#undef __except
#undef LONG
#undef thunk_slot_ready
/* Two bytes of the word stay as they are: the end of mov rsi,r13 and the E8. */
static int find_order_swap(int32_t expected,int32_t value) {
    unsigned char *word=LX(LX_FIND_ORDER_SITE_VA&~7u);
    const unsigned at=(LX_FIND_ORDER_SITE_VA&7u)+1u;
    uint64_t before=rd64(word),after=before;int32_t rel;
    memcpy(&rel,(unsigned char*)&before+at,4);
    if(rel!=expected) return 0;
    memcpy((unsigned char*)&after+at,&value,4);
    return gate_code_swap64(word,before,after);
}
static __attribute__((noinline)) uintptr_t find_order_assign(void *declaration,void *vector,void *exec,void *component) {
    find_order_process((uintptr_t)__builtin_return_address(0),vector,read_local,world_identity_key);
    return ((find_assign_fn)(void*)LX(LX_FIND_ORDER_ASSIGN_VA))(declaration,vector,exec,component);
}
static void lxo_thunk_write(unsigned char *page) {lxw_jump_write(page+FIND_ORDER_THUNK,(void*)find_order_assign);}
/* ---- the range finds' two store entries ---- */
#define LXFS_STUB 128u
static const unsigned char lxfs_original[12]={0x66,0x0f,0xef,0xc0, 0x0f,0x29,0x84,0x24,0x60,0x01,0x00,0x00};
static const uint32_t lxfs_sites[2]={LX_FIND_SPACE_SITE_A_VA,LX_FIND_SPACE_SITE_B_VA};
static unsigned char *lxfs_page;
/* The stub's call: the tree at the native rsp+0x120; the admission takes the first entry's address as its caller. */
static void lxfs_sort(void *tree) {
    find_order_space_process((uintptr_t)(image+FIND_ORDER_SPACE_SITE+5),tree,read_local,world_identity_key);
}
/* lea rsp,[rsp-0x80]; push rax,rcx,rdx,rsi,rdi,r8..r11; pushfq; sub rsp,0x200; fxsave64 [rsp]; lea rdi,[rsp+0x3f0]
 * (the native rsp+0x120); movabs rax,fn; call rax; fxrstor64 [rsp]; add rsp,0x200; popfq; pop r11..r8,rdi..rax;
 * lea rsp,[rsp+0x80]; pxor xmm0,xmm0; movaps [rsp+0x160],xmm0; jmp [rip] back. The native rsp is 16-aligned here
 * (Execute: 6 pushes, sub rsp,0x1b8), so the call and fxsave64 are aligned too. */
static void lxfs_stub_bytes(unsigned char out[LXFS_STUB],const void *fn,const void *back) {
    static const unsigned char head[]={0x48,0x8d,0x64,0x24,0x80, 0x50,0x51,0x52,0x56,0x57, 0x41,0x50,0x41,0x51,0x41,0x52,0x41,0x53, 0x9c,
        0x48,0x81,0xec,0x00,0x02,0x00,0x00, 0x48,0x0f,0xae,0x04,0x24, 0x48,0x8d,0xbc,0x24,0xf0,0x03,0x00,0x00, 0x48,0xb8};
    static const unsigned char tail[]={0xff,0xd0, 0x48,0x0f,0xae,0x0c,0x24, 0x48,0x81,0xc4,0x00,0x02,0x00,0x00, 0x9d,
        0x41,0x5b,0x41,0x5a,0x41,0x59,0x41,0x58, 0x5f,0x5e,0x5a,0x59,0x58, 0x48,0x8d,0xa4,0x24,0x80,0x00,0x00,0x00,
        0x66,0x0f,0xef,0xc0, 0x0f,0x29,0x84,0x24,0x60,0x01,0x00,0x00, 0xff,0x25,0,0,0,0};
    _Static_assert(sizeof(head)+8+sizeof(tail)+8<=LXFS_STUB,"stub size");
    unsigned n=0;memset(out,0xcc,LXFS_STUB);
    memcpy(out+n,head,sizeof(head));n+=sizeof(head);memcpy(out+n,&fn,8);n+=8;
    memcpy(out+n,tail,sizeof(tail));n+=sizeof(tail);memcpy(out+n,&back,8);
}
static void lxfs_entry(unsigned char out[5],unsigned i) {
    int32_t d=(int32_t)((intptr_t)(lxfs_page+i*LXFS_STUB)-(intptr_t)(LX(lxfs_sites[i])+5));out[0]=0xe9;memcpy(out+1,&d,4);
}
/* 0 native, 1 our jmp, -1 anything else. */
static int lxfs_state(unsigned i) {
    unsigned char b[12],want[5];
    if(!gate_read_local(LX(lxfs_sites[i]),b,12)) return -1;
    if(!memcmp(b,lxfs_original,12)) return 0;
    if(lxfs_page && !memcmp(b+5,lxfs_original+5,7)) {lxfs_entry(want,i);if(!memcmp(b,want,5)) return 1;}
    return -1;
}
static int lxfs_swap(unsigned i,const unsigned char from[5],const unsigned char to[5]) {
    unsigned char *line=LX(lxfs_sites[i]&~15u);const unsigned at=lxfs_sites[i]&15u;
    unsigned char a[16],b[16];unsigned __int128 expected,desired;
    if(at+5>16) return 0;
    memcpy(a,line,16);memcpy(b,a,16);memcpy(a+at,from,5);memcpy(b+at,to,5);
    memcpy(&expected,a,16);memcpy(&desired,b,16);
    return gate_code_swap128(line,expected,desired);
}
/* The start pin (Execute whole) and Execute in the vtable's slot 4. */
static int find_order_space_pinned(void) {
    const lx_site *s=site_named("find_space_execute");uint64_t slot=0;
    return s && s->va==LX_FIND_SPACE_EXECUTE_VA && s->va<=lxfs_sites[0] && lxfs_sites[1]+12<=s->va+s->length &&
        gate_read_local(LX(LX_FIND_SPACE_VTABLE+0x20u),&slot,8) && slot==(uint64_t)(uintptr_t)LX(LX_FIND_SPACE_EXECUTE_VA);
}
/* Both entries native, the stubs written; the rel32s the shared ownership check compares (the first entry's). */
static int find_order_space_prepare(void) {
    if(lxfs_state(0)!=0 || lxfs_state(1)!=0) return 0;
    if(!lxfs_page) lxfs_page=gate_near_page(LX(lxfs_sites[0]));
    if(!lxfs_page) return 0;
    for(unsigned i=0;i<2;i++) {
        intptr_t d=(intptr_t)(lxfs_page+i*LXFS_STUB)-(intptr_t)(LX(lxfs_sites[i])+5);if(d<INT32_MIN || d>INT32_MAX) return 0;
    }
    if(mprotect(lxfs_page,GATE_PAGE,PROT_READ|PROT_WRITE)) return 0;
    memset(lxfs_page,0xcc,GATE_PAGE);
    for(unsigned i=0;i<2;i++) lxfs_stub_bytes(lxfs_page+i*LXFS_STUB,(void*)lxfs_sort,LX(lxfs_sites[i])+12);
    if(mprotect(lxfs_page,GATE_PAGE,PROT_READ|PROT_EXEC)) return 0;
    unsigned char e[5];lxfs_entry(e,0);
    memcpy(&find_order_space_patched_rel,e+1,4);memcpy(&find_order_space_original_rel,lxfs_original+1,4);
    return 1;
}
/* original -> patched installs both entries (or neither); patched -> original restores ours (a foreign value stays). */
static int find_order_space_swap(int32_t expected,int32_t value) {
    unsigned char e[5];
    if(expected==find_order_space_original_rel && value==find_order_space_patched_rel) {
        lxfs_entry(e,0);if(!lxfs_swap(0,lxfs_original,e)) return 0;
        unsigned char e1[5];lxfs_entry(e1,1);
        if(!lxfs_swap(1,lxfs_original,e1)) {(void)lxfs_swap(0,e,lxfs_original);return 0;}
        return 1;
    }
    if(expected==find_order_space_patched_rel && value==find_order_space_original_rel) {
        int ok=1;
        for(unsigned i=2;i-->0;) {
            int state=lxfs_state(i);
            if(state==1) {lxfs_entry(e,i);if(!lxfs_swap(i,e,lxfs_original)) ok=0;}
            else if(state!=0) ok=0;
        }
        return ok;
    }
    return 0;
}
static int lua_find_order(lua_State *L) {
    double mode=p_lua_tonumber(L,1);
    p_lua_pushnumber(L,isfinite(mode) && (mode==FIND_ORDER_OBSERVE || mode==FIND_ORDER_SORT)?find_order_arm((int)mode):0);
    return 1;
}
static int lua_find_order_status(lua_State *L) {
    char out[1024];int n=accept_ui()?find_order_status(out,sizeof(out)):0;
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);
    return 1;
}
static int lua_find_order_row(lua_State *L) {
    double index=p_lua_tonumber(L,1);char out[1024];int n=0;
    if(accept_ui() && isfinite(index) && index>=0 && index<9007199254740992.0 && floor(index)==index)
        n=find_order_row((uint64_t)index,out,sizeof(out));
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);
    return 1;
}
