/* native_gate_steer_roll (Linux gate only): the steering update's roll alignment in the Windows order. Included by
 * gate_linux.c only.
 *
 * When a ship's flight behaviour asks for it, U::Path::SteeringFlightControlModel's update (Windows 0xec7200, Linux
 * 0x13e9d50) turns the angular command about the aim (atan2f, a rotation, the command through it) and derives the roll
 * command from the turned command's x and y against the yaw and pitch limits (Linux [rbx+0x37c] and +0x378, Windows
 * [r13+0x3c] and +0x38): scale = min(1, 2 max(|y| / pitch, |x| / yaw)), and near the axis x' = (c yaw |x| - yaw)
 * (2|x| / yaw) + yaw. MSVC hoisted 1 / yaw (0xec8965: divss of 1.0f by the limit) and multiplies |x| and 2|x| by it
 * (0xec896c, 0xec89d3); GCC divides both by the limit (0x13eb964, 0x13ec604). x * (1 / L) and x / L round apart in a
 * share of the values, the roll command (the model's angular z) with them: lx64a against W36p/W36q, actor 11's roll
 * command 10 ulps apart at tick 38 (bf69b048 / bf69b03e) with every input equal, later the steering slot's inputs
 * (actor 3 at tick 100, 11 at 107, 12 at 114, 5 at 121) and actor 5's velocity at tick 124.
 *
 * Here both divisions run in stubs on a page near the image as Windows computes them (xmm3 is the first one's
 * destination and dead at the second: reloaded at 0x13eb9c5 before any read; the second block is entered only from
 * 0x13eb98e):
 *   A 0x13eb961: movaps xmm3,xmm7; divss xmm3,xmm4  ->  movss xmm3,[1.0f]; divss xmm3,xmm4; mulss xmm3,xmm7  -> 0x13eb968
 *   B 0x13ec600: addss xmm7,xmm7; divss xmm7,xmm4   ->  addss xmm7,xmm7; movss xmm3,[1.0f]; divss xmm3,xmm4;
 *                                                       mulss xmm7,xmm3                                      -> 0x13ec608
 * Each run's first five bytes become jmp rel32 (one compare-exchange of its 16-byte line; the native bytes after them
 * stay). No branch in the executable leads inside either run. Pinned (steer_roll_a 0x13eb8b1..0x13eb994 and
 * steer_roll_b 0x13ec5f0..0x13ec619, read with the entries as found). Before the first admitted frame, on the
 * MoveWorker; the per-tick check (stop reason 210) and the restore are this file's. Windows needs nothing. */
#define LXSR_STOP_REASON 210u
#define LXSR_A_VA 0x13eb961u
#define LXSR_A_BACK 0x13eb968u
#define LXSR_B_VA 0x13ec600u
#define LXSR_B_BACK 0x13ec608u
#define LXSR_STUB_A 0x00u
#define LXSR_STUB_B 0x20u
#define LXSR_ONE 0x40u
#define LXSR_CODE 0x44u
static const unsigned char lxsr_a_original[7]={0x0f,0x28,0xdf,0xf3,0x0f,0x5e,0xdc};
static const unsigned char lxsr_b_original[8]={0xf3,0x0f,0x58,0xff,0xf3,0x0f,0x5e,0xfc};
static const struct {uint32_t va,stub;const unsigned char *original;const char *pin;} lxsr_sites[2]={
    {LXSR_A_VA,LXSR_STUB_A,lxsr_a_original,"steer_roll_a"},{LXSR_B_VA,LXSR_STUB_B,lxsr_b_original,"steer_roll_b"}};
static unsigned char *lxsr_page;
/* installed also means cleanup is owed after an unsuccessful arm. A successful CAS
 * and closing the code page are separate obligations; neither is hidden by the other. */
static int lxsr_installed,lxsr_restored;
static unsigned lxsr_rx_pending;
#ifdef GATE_TLS_TEST
static int (*lxsr_test_protect)(void *,int);
#endif
static int lxsr_protect(void *address, int writable) {
#ifdef GATE_TLS_TEST
    if (lxsr_test_protect) {
        return lxsr_test_protect(address, writable);
    }
#endif
    return writable ? gate_code_open(address, 16) : gate_code_close(address);
}
static const char *lxsr_stage="idle";
static int lxsr_rel(unsigned char *out,const unsigned char *page,unsigned char *at,const void *target) {
    const intptr_t d=(intptr_t)target-(intptr_t)(page+(at-out)+4);
    if(d!=(intptr_t)(int32_t)d) return 0;
    const int32_t r=(int32_t)d;memcpy(at,&r,4);return 1;
}
/* The page's code: both stubs and 1.0f; 0 if the page is out of reach. */
static int lxsr_code(unsigned char out[LXSR_CODE],const unsigned char *page) {
    static const unsigned char load[4]={0xf3,0x0f,0x10,0x1d},div[4]={0xf3,0x0f,0x5e,0xdc}; /* movss xmm3,[rip+..]; divss xmm3,xmm4 */
    unsigned char *a=out+LXSR_STUB_A,*b=out+LXSR_STUB_B;const unsigned char *one=page+LXSR_ONE;
    memset(out,0xcc,LXSR_CODE);
    memcpy(a,load,4);if(!lxsr_rel(out,page,a+4,one)) return 0;
    memcpy(a+8,div,4);memcpy(a+12,"\xf3\x0f\x59\xdf",4);                 /* mulss xmm3,xmm7 */
    a[16]=0xe9;if(!lxsr_rel(out,page,a+17,LX(LXSR_A_BACK))) return 0;
    memcpy(b,lxsr_b_original,4);                                          /* addss xmm7,xmm7 */
    memcpy(b+4,load,4);if(!lxsr_rel(out,page,b+8,one)) return 0;
    memcpy(b+12,div,4);memcpy(b+16,"\xf3\x0f\x59\xfb",4);                 /* mulss xmm7,xmm3 */
    b[20]=0xe9;if(!lxsr_rel(out,page,b+21,LX(LXSR_B_BACK))) return 0;
    const float f=1.0f;memcpy(out+LXSR_ONE,&f,4);
    return 1;
}
static int lxsr_page_ok(void) {unsigned char want[LXSR_CODE];return lxsr_page && lxsr_code(want,lxsr_page) && !memcmp(lxsr_page,want,LXSR_CODE);}
static int lxsr_patch(unsigned char out[5],unsigned i) {
    out[0]=0xe9;const intptr_t d=(intptr_t)(lxsr_page+lxsr_sites[i].stub)-(intptr_t)(LX(lxsr_sites[i].va)+5);
    if(!lxsr_page || d!=(intptr_t)(int32_t)d) return 0;
    const int32_t r=(int32_t)d;memcpy(out+1,&r,4);return 1;
}
static int lxsr_site_state(unsigned i) { /* 0 native, 1 ours, -1 other */
    unsigned char now[5],ours[5];
    if(!gate_read_local(LX(lxsr_sites[i].va),now,5)) return -1;
    if(!memcmp(now,lxsr_sites[i].original,5)) return 0;
    if(lxsr_patch(ours,i) && !memcmp(now,ours,5)) return 1;
    return -1;
}
static int lxsr_state(void) {
    const int a=lxsr_site_state(0),b=lxsr_site_state(1);
    return a==0 && b==0?0:a==1 && b==1?1:-1;
}
static int lxsr_pins(void) {
    for(unsigned i=0;i<2;i++) {
        const lx_site *s=site_named(lxsr_sites[i].pin);unsigned char copy[0x100];
        if(!s || s->length>sizeof(copy) || lxsr_sites[i].va<s->va || lxsr_sites[i].va+5>s->va+s->length ||
           !gate_read_local(LX(s->va),copy,s->length)) return 0;
        if(lxsr_site_state(i)==1) memcpy(copy+(lxsr_sites[i].va-s->va),lxsr_sites[i].original,5);
        if(!hash_ok(copy,s->length,s->sha256)) return 0;
    }
    return 1;
}
static int lxsr_swap(unsigned virtual_address, const unsigned char expected_bytes[5], const unsigned char replacement_bytes[5]) {
    unsigned char *code_line = (unsigned char*)((uintptr_t)LX(virtual_address)&~(uintptr_t)15);
    const unsigned patch_offset = (unsigned)(LX(virtual_address)-code_line);
    const unsigned protection_bit = (virtual_address == LXSR_A_VA?1u:2u);
    unsigned __int128 current_line, desired_line;
    if (patch_offset + 5 > 16) {
        return 0;
    }
    pthread_mutex_lock(&gate_code_lock);
    memcpy(&current_line, code_line, 16);
    if (memcmp((unsigned char*)&current_line + patch_offset, expected_bytes, 5)
        || !lxsr_protect(code_line, 1)) {
        pthread_mutex_unlock(&gate_code_lock);
        return 0;
    }
    lxsr_rx_pending |= protection_bit;
    desired_line = current_line;
    memcpy((unsigned char*)&desired_line + patch_offset, replacement_bytes, 5);
    const int patch_written = __sync_val_compare_and_swap((unsigned __int128*)code_line,
        current_line, desired_line) == current_line;
    if (patch_written) {
        lxsr_installed = 1;
        lxsr_restored = 0;
    }
    if (lxsr_protect(code_line, 0)) {
        lxsr_rx_pending &= ~protection_bit;
    }
    else {
        lxsr_installed = 1;
        lxsr_restored = 0;
    }
    pthread_mutex_unlock(&gate_code_lock);
    return patch_written && !(lxsr_rx_pending & protection_bit);
}
static int lxsr_install(void) {
    if(!lxsr_page) lxsr_page=gate_near_page(LX(LXSR_A_VA));
    if(!lxsr_page || mprotect(lxsr_page,GATE_PAGE,PROT_READ|PROT_WRITE)) return 0;
    memset(lxsr_page,0xcc,GATE_PAGE);
    if(!lxsr_code(lxsr_page,lxsr_page) || mprotect(lxsr_page,GATE_PAGE,PROT_READ|PROT_EXEC)) return 0;
    unsigned char a[5],b[5];
    if(!lxsr_patch(a,0) || !lxsr_patch(b,1)) return 0;
    if(!lxsr_swap(LXSR_A_VA,lxsr_a_original,a)) return 0;
    if(!lxsr_swap(LXSR_B_VA,lxsr_b_original,b)) {lxsr_swap(LXSR_A_VA,a,lxsr_a_original);return 0;}
    return 1;
}
/* Before the first admitted frame, on the MoveWorker. */
static int lxsr_arm(void) {
    if (lxsr_installed) {
        return stats.active
            && !lxsr_rx_pending
            && !strcmp(lxsr_stage, "armed")
            && lxsr_state() == 1
            && lxsr_page_ok()
            && lxsr_pins();
    }
    if (!stats.active || (boot_mode && boot_phase) || stats.issued || GetCurrentThreadId() != ui_thread) {
        lxsr_stage = "guard";
        return 0;
    }
    if (lxsr_state() != 0) {
        lxsr_stage = "entry";
        return 0;
    }
    if (!lxsr_pins()) {
        lxsr_stage = "pins";
        return 0;
    }
    if (!lxsr_install()) {
        lxsr_stage = "install";
        return 0;
    }
    lxsr_installed = 1;
    lxsr_restored = 0;
    lxsr_stage = "armed";
    return 1;
}
static int lxsr_check(void) {
    const char *failure_stage = lxsr_rx_pending ? "protection_pending" :
        lxsr_state() != 1 ? "entry_changed" :
        !lxsr_page_ok() ? "page_changed" :
        !lxsr_pins() ? "block_changed" :
        NULL;
    if (failure_stage) {
        lxsr_stage = failure_stage;
        return 0;
    }
    return 1;
}
static void lxsr_restore(void) {
    if (!lxsr_installed) {
        return;
    }
    int original_bytes_restored = 1;
    for (unsigned site_index = 0; site_index < 2; site_index++) {
        unsigned char owned_patch[5], current_bytes[8];
        const unsigned protection_bit = 1u << site_index;
        const unsigned instruction_size = site_index ? sizeof(lxsr_b_original) : sizeof(lxsr_a_original);
        const int entry_state = lxsr_site_state(site_index);
        if (entry_state == 1 && lxsr_patch(owned_patch, site_index)) {
            lxsr_swap(lxsr_sites[site_index].va, owned_patch, lxsr_sites[site_index].original);
        }
        else if (entry_state == 0 || (lxsr_rx_pending & protection_bit)) {
            pthread_mutex_lock(&gate_code_lock);
            if (lxsr_protect(LX(lxsr_sites[site_index].va), 0)) {
                lxsr_rx_pending &= ~protection_bit;
            }
            else {
                lxsr_rx_pending |= protection_bit;
            }
            pthread_mutex_unlock(&gate_code_lock);
        }
        if (!gate_read_local(LX(lxsr_sites[site_index].va), current_bytes, instruction_size)
            || memcmp(current_bytes, lxsr_sites[site_index].original, instruction_size)) {
            original_bytes_restored = 0;
        }
    }
    if (original_bytes_restored && !lxsr_rx_pending) {
        lxsr_installed = 0;
        lxsr_restored = 1;
        lxsr_stage = "restored";
    }
    else {
        lxsr_stage = original_bytes_restored ? "restore_protection" : "restore_pending";
    }
    /* The page stays: a thread may still be inside either stub. */
}
static int lxsr_status(char *out, size_t size) {
    const int written = snprintf(
        out, size, "tick=%llu installed=%d restored=%d state=%d pin=%d page=%d stage=%s rx_pending=%u",
        (unsigned long long)stats.issued, lxsr_installed, lxsr_restored,
        lxsr_state(), lxsr_pins(), lxsr_page_ok(), lxsr_stage, lxsr_rx_pending);
    return written > 0 && (size_t)written < size ? written : 0;
}
static int lua_steer_roll(lua_State *L) {p_lua_pushnumber(L,lxsr_arm());return 1;}
static int lua_steer_roll_status(lua_State *L) {
    char out[200];const int n=accept_ui()?lxsr_status(out,sizeof(out)):0;
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
