/* native_gate_avoid_angle (Linux gate only): the obstacle avoidance's angle scaling in the Windows order. Included by
 * gate_linux.c only.
 *
 * The steering behaviours' obstacle avoidance (Windows 0xecb4d0, Linux 0x13c9cf0, the first of the three avoidance
 * terms a behaviour adds to its linear and angular command) scales its two evasion angles: max(a, -pi) by 1 / pi and
 * max(b, -pi/2) by 2 / pi. MSVC multiplies by the folded constants 1/pi (0x3ea2f983, 0xecb8b7) and 2/pi (0x3f22f983,
 * 0xecb8c0); GCC divides by pi and pi/2 (0x13ca01d, 0x13ca024). x * (1 / pi) and x / pi round apart in a share of the
 * values, the evasion command with them: lx65a against W36q, actor 3's linear command 2 ulps apart at tick 99
 * (42df5eb0 / 42df5eb2) while it steers around an obstacle, then the steering slot's inputs at 100 and actor 5's
 * velocity at tick 124.
 *
 * Here the run 0x13ca015..0x13ca02e (maxss xmm4,xmm2; maxss xmm5,xmm0; divss xmm4,xmm7; movaps xmm7,xmm5; divss
 * xmm7,xmm8; jmp 0x13c9e62) runs in a stub on a page near the image with each divss a mulss by the page's constant;
 * xmm7 and xmm8 keep what the native run leaves in them (xmm7 the scaled angle, xmm8 pi/2). The run's first five bytes
 * become jmp rel32 (one compare-exchange of its 16-byte line). 0x13ca015 is a branch target (the start); no branch in
 * the executable leads inside the run. Pinned (avoid_angle 0x13c9fd6..0x13ca02e, read with the entry as found).
 * Before the first admitted frame, on the MoveWorker; the per-tick check (stop reason 212) and the restore are this
 * file's. Windows needs nothing. */
#define LXAV_STOP_REASON 212u
#define LXAV_VA 0x13ca015u
#define LXAV_BACK 0x13c9e62u
#define LXAV_K1 0x30u /* 1/pi */
#define LXAV_K2 0x34u /* 2/pi */
#define LXAV_CODE 0x38u
static const unsigned char lxav_original[25]={0xf3,0x0f,0x5f,0xe2,0xf3,0x0f,0x5f,0xe8,0xf3,0x0f,0x5e,0xe7,0x0f,0x28,0xfd,0xf3,0x41,0x0f,0x5e,0xf8,
                                              0xe9,0x34,0xfe,0xff,0xff};
static unsigned char *lxav_page;
/* installed also means cleanup is owed after an unsuccessful arm. A successful CAS
 * and closing the code page are separate obligations; neither is hidden by the other. */
static int lxav_installed,lxav_restored;
static unsigned lxav_rx_pending;
#ifdef GATE_TLS_TEST
static int (*lxav_test_protect)(void *,int);
#endif
static int lxav_protect(void *address, int writable) {
#ifdef GATE_TLS_TEST
    if (lxav_test_protect) {
        return lxav_test_protect(address, writable);
    }
#endif
    return writable ? gate_code_open(address, 16) : gate_code_close(address);
}
static const char *lxav_stage="idle";
static int lxav_rel(unsigned char *out,const unsigned char *page,unsigned char *at,const void *target) {
    const intptr_t d=(intptr_t)target-(intptr_t)(page+(at-out)+4);
    if(d!=(intptr_t)(int32_t)d) return 0;
    const int32_t r=(int32_t)d;memcpy(at,&r,4);return 1;
}
static int lxav_code(unsigned char out[LXAV_CODE],const unsigned char *page) {
    memset(out,0xcc,LXAV_CODE);
    memcpy(out,lxav_original,8);                                   /* maxss xmm4,xmm2; maxss xmm5,xmm0 */
    memcpy(out+8,"\xf3\x0f\x59\x25",4);if(!lxav_rel(out,page,out+12,page+LXAV_K1)) return 0;  /* mulss xmm4,[1/pi] */
    memcpy(out+16,lxav_original+12,3);                             /* movaps xmm7,xmm5 */
    memcpy(out+19,"\xf3\x0f\x59\x3d",4);if(!lxav_rel(out,page,out+23,page+LXAV_K2)) return 0; /* mulss xmm7,[2/pi] */
    out[27]=0xe9;if(!lxav_rel(out,page,out+28,LX(LXAV_BACK))) return 0;
    const uint32_t k1=0x3ea2f983u,k2=0x3f22f983u;memcpy(out+LXAV_K1,&k1,4);memcpy(out+LXAV_K2,&k2,4);
    return 1;
}
static int lxav_page_ok(void) {unsigned char want[LXAV_CODE];return lxav_page && lxav_code(want,lxav_page) && !memcmp(lxav_page,want,LXAV_CODE);}
static int lxav_patch(unsigned char out[5]) {
    out[0]=0xe9;const intptr_t d=(intptr_t)lxav_page-(intptr_t)(LX(LXAV_VA)+5);
    if(!lxav_page || d!=(intptr_t)(int32_t)d) return 0;
    const int32_t r=(int32_t)d;memcpy(out+1,&r,4);return 1;
}
static int lxav_state(void) { /* 0 native, 1 ours, -1 other */
    unsigned char now[5],ours[5];
    if(!gate_read_local(LX(LXAV_VA),now,5)) return -1;
    if(!memcmp(now,lxav_original,5)) return 0;
    if(lxav_patch(ours) && !memcmp(now,ours,5)) return 1;
    return -1;
}
static int lxav_pins(void) {
    const lx_site *s=site_named("avoid_angle");unsigned char copy[0x80];
    if(!s || s->length>sizeof(copy) || LXAV_VA<s->va || LXAV_VA+sizeof(lxav_original)!=s->va+s->length || lxav_state()<0 ||
       !gate_read_local(LX(s->va),copy,s->length)) return 0;
    memcpy(copy+(LXAV_VA-s->va),lxav_original,5);
    return hash_ok(copy,s->length,s->sha256);
}
static int lxav_swap(const unsigned char expected_bytes[5], const unsigned char replacement_bytes[5]) {
    unsigned char *code_line = (unsigned char*)((uintptr_t)LX(LXAV_VA)&~(uintptr_t)15);
    const unsigned patch_offset = (unsigned)(LX(LXAV_VA)-code_line);
    const unsigned protection_bit = 1u;
    unsigned __int128 current_line, desired_line;
    if (patch_offset + 5 > 16) {
        return 0;
    }
    pthread_mutex_lock(&gate_code_lock);
    memcpy(&current_line, code_line, 16);
    if (memcmp((unsigned char*)&current_line + patch_offset, expected_bytes, 5)
        || !lxav_protect(code_line, 1)) {
        pthread_mutex_unlock(&gate_code_lock);
        return 0;
    }
    lxav_rx_pending |= protection_bit;
    desired_line = current_line;
    memcpy((unsigned char*)&desired_line + patch_offset, replacement_bytes, 5);
    const int patch_written = __sync_val_compare_and_swap((unsigned __int128*)code_line,
        current_line, desired_line) == current_line;
    if (patch_written) {
        lxav_installed = 1;
        lxav_restored = 0;
    }
    if (lxav_protect(code_line, 0)) {
        lxav_rx_pending &= ~protection_bit;
    }
    else {
        lxav_installed = 1;
        lxav_restored = 0;
    }
    pthread_mutex_unlock(&gate_code_lock);
    return patch_written && !(lxav_rx_pending & protection_bit);
}
/* Before the first admitted frame, on the MoveWorker. */
static int lxav_arm(void) {
    if (lxav_installed) {
        return stats.active
            && !lxav_rx_pending
            && !strcmp(lxav_stage, "armed")
            && lxav_state() == 1
            && lxav_page_ok()
            && lxav_pins();
    }
    if (!stats.active || (boot_mode && boot_phase) || stats.issued || GetCurrentThreadId() != ui_thread) {
        lxav_stage = "guard";
        return 0;
    }
    if (lxav_state() != 0) {
        lxav_stage = "entry";
        return 0;
    }
    if (!lxav_pins()) {
        lxav_stage = "pins";
        return 0;
    }
    if (!lxav_page) {
        lxav_page = gate_near_page(LX(LXAV_VA));
    }
    if (!lxav_page || mprotect(lxav_page, GATE_PAGE, PROT_READ|PROT_WRITE)) {
        lxav_stage = "page";
        return 0;
    }
    memset(lxav_page, 0xcc, GATE_PAGE);
    unsigned char entry_patch[5];
    if (!lxav_code(lxav_page, lxav_page)
        || mprotect(lxav_page, GATE_PAGE, PROT_READ|PROT_EXEC)
        || !lxav_patch(entry_patch)
        || !lxav_swap(lxav_original, entry_patch)) {
        lxav_stage = "install";
        return 0;
    }
    lxav_installed = 1;
    lxav_restored = 0;
    lxav_stage = "armed";
    return 1;
}
static int lxav_check(void) {
    const char *failure_stage = lxav_rx_pending ? "protection_pending" :
        lxav_state() != 1 ? "entry_changed" :
        !lxav_page_ok() ? "page_changed" :
        !lxav_pins() ? "block_changed" :
        NULL;
    if (failure_stage) {
        lxav_stage = failure_stage;
        return 0;
    }
    return 1;
}
static void lxav_restore(void) {
    if (!lxav_installed) {
        return;
    }
    unsigned char owned_patch[5], current_bytes[sizeof(lxav_original)];
    const int entry_state = lxav_state();
    if (entry_state == 1 && lxav_patch(owned_patch)) {
        lxav_swap(owned_patch, lxav_original);
    }
    else if (entry_state == 0 || lxav_rx_pending) {
        pthread_mutex_lock(&gate_code_lock);
        if (lxav_protect(LX(LXAV_VA), 0)) {
            lxav_rx_pending = 0;
        }
        else {
            lxav_rx_pending = 1;
        }
        pthread_mutex_unlock(&gate_code_lock);
    }
    const int original_bytes_restored = gate_read_local(LX(LXAV_VA), current_bytes, sizeof(current_bytes))
        && !memcmp(current_bytes, lxav_original, sizeof(current_bytes));
    if (original_bytes_restored && !lxav_rx_pending) {
        lxav_installed = 0;
        lxav_restored = 1;
        lxav_stage = "restored";
    }
    else {
        lxav_stage = original_bytes_restored ? "restore_protection" : "restore_pending";
    }
    /* The page stays; foreign entry bytes are never replaced. */
}
static int lxav_status(char *out, size_t size) {
    const int written = snprintf(
        out, size, "tick=%llu installed=%d restored=%d state=%d pin=%d page=%d stage=%s rx_pending=%u",
        (unsigned long long)stats.issued, lxav_installed, lxav_restored,
        lxav_state(), lxav_pins(), lxav_page_ok(), lxav_stage, lxav_rx_pending);
    return written > 0 && (size_t)written < size ? written : 0;
}
static int lua_avoid_angle(lua_State *L) {p_lua_pushnumber(L,lxav_arm());return 1;}
static int lua_avoid_angle_status(lua_State *L) {
    char out[200];const int n=accept_ui()?lxav_status(out,sizeof(out)):0;
    if(n) p_lua_pushlstring(L,out,(size_t)n);else p_lua_pushnil(L);return 1;
}
