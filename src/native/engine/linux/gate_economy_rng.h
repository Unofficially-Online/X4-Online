/* Experimental load-time economy RNG. The inline detour at 0x1841338 is
 * reached only after the original range branch. The entry stub saves the red
 * zone, all integer registers, flags, and enabled supported vector/x87 state.
 * Its private PCG calculation deliberately leaves ambient RNG untouched when
 * keyed. Missing identities use the original ambient stream and latch failure.
 * The native expression from 0x184136a onward is not modified. */
#include <errno.h>
#define ECONOMY_RNG_DOMAIN UINT64_C(0x58344c45434f4e4f)
#define ECONOMY_RNG_SITE 0x1841330u
#define ECONOMY_RNG_CONTINUE 0x184136au
#define ECONOMY_RNG_XSTATE_MAX 4096u
#define ECONOMY_RNG_VECTOR_MASK UINT64_C(0xe7)
#define ECONOMY_RNG_ALLOWED_XCR0 UINT64_C(0x2e7)

static unsigned char *economy_rng_page;
static unsigned __int128 economy_rng_original, economy_rng_patched;
__attribute__((used)) static uint32_t economy_rng_xstate_mask;
static uint64_t economy_rng_xcr0, economy_rng_keyed, economy_rng_fallback;
static uint64_t economy_rng_key_xor, economy_rng_key_sum, economy_rng_draw_xor, economy_rng_draw_sum;
typedef struct {
    uint64_t saved_id;
    uint64_t last_bits;
    uint64_t prepared_seed;
    uint64_t expected_draw;
    volatile int ready;
} economy_rng_row;
static economy_rng_row economy_rng_rows[128];
static uint64_t economy_rng_row_count;
static volatile int64_t economy_rng_failures;
static uint64_t economy_rng_failure_object, economy_rng_failure_class;
static uint64_t economy_rng_failure_runtime, economy_rng_failure_last;
static unsigned economy_rng_failure_reason, economy_rng_failure_phase;
static int economy_rng_failure_sample_state;
static pthread_rwlock_t economy_rng_readers = PTHREAD_RWLOCK_INITIALIZER;
static int economy_rng_enabled;
/* A thread may have branched to the stub before restoration. Never reuse its
 * page, continuation, or xstate mask for a second generation in this process. */
static int economy_rng_ever_installed;
__attribute__((used)) static uintptr_t economy_rng_continuation;
static const char *economy_rng_stage = "idle";
static const unsigned char economy_rng_block[16] = {
    0x2f,0xd0,0x0f,0x87,0x5f,0x11,0x00,0x00,
    0x48,0xb8,0x2d,0x7f,0x95,0x4c,0x2d,0xf4
};

typedef struct {
    uint32_t size;
    uint32_t offset;
    uint32_t flags;
} economy_rng_xcomponent;

/* Validate CPUID's standard-format user-state ranges before executing XSAVE.
 * Legacy x87/SSE occupy bytes 0..511 and the header occupies 512..575. */
static int economy_rng_layout(uint64_t xcr0, uint64_t supported, uint32_t total,
                              const economy_rng_xcomponent components[8]) {
    uint64_t selected = xcr0 & ECONOMY_RNG_VECTOR_MASK;
    if ((xcr0 & ~ECONOMY_RNG_ALLOWED_XCR0) || (xcr0 & ~supported) ||
        (selected != 3 && selected != 7 && selected != 0xe7) ||
        total < 576 || total > ECONOMY_RNG_XSTATE_MAX) return 0;
    for (unsigned index = 2; index < 8; ++index) {
        if (!(selected & (UINT64_C(1) << index))) continue;
        const economy_rng_xcomponent *component = &components[index];
        if (!component->size || component->offset < 576 || (component->flags & 1) ||
            component->offset > total || component->size > total - component->offset) return 0;
        for (unsigned previous = 2; previous < index; ++previous) {
            if (!(selected & (UINT64_C(1) << previous))) continue;
            const economy_rng_xcomponent *other = &components[previous];
            if (component->offset < other->offset + other->size &&
                other->offset < component->offset + component->size) return 0;
        }
    }
    return 1;
}

/* CPUID and XGETBV run only while arming, before the detour is installed.
 * PKRU is permitted but excluded: neither this helper nor its callees write
 * PKRU. Unsupported XCR0 components are refused rather than lost. */
static int economy_rng_cpu_capable(uint32_t max_leaf, uint32_t features) {
    const uint32_t required = (1u << 26) | (1u << 27);
    return max_leaf >= 13 && (features & required) == required;
}

static int economy_rng_xstate(void) {
    uint32_t eax, ebx, ecx, edx;
    __asm__ volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(0), "c"(0));
    uint32_t max_leaf = eax;
    if (max_leaf < 13) return 0;
    __asm__ volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(1), "c"(0));
    if (!economy_rng_cpu_capable(max_leaf, ecx)) return 0;
    uint32_t low, high;
    __asm__ volatile("xgetbv" : "=a"(low), "=d"(high) : "c"(0));
    economy_rng_xcr0 = ((uint64_t)high << 32) | low;
    __asm__ volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(13), "c"(0));
    uint64_t supported = ((uint64_t)edx << 32) | eax;
    uint32_t total = ebx;
    economy_rng_xcomponent components[8] = {{0}};
    for (unsigned index = 2; index < 8; ++index) {
        if (!(economy_rng_xcr0 & (UINT64_C(1) << index))) continue;
        __asm__ volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
                         : "a"(13), "c"(index));
        components[index] = (economy_rng_xcomponent){eax, ebx, ecx};
    }
    if (!economy_rng_layout(economy_rng_xcr0, supported, total, components)) return 0;
    economy_rng_xstate_mask = (uint32_t)(economy_rng_xcr0 & ECONOMY_RNG_VECTOR_MASK);
    return 1;
}

typedef struct {
    uint64_t word;
    uint64_t fallback;
} economy_rng_choice;
/* Called with a pointer to the original import frame; the object is [RSP],
 * not RBX (which holds the XML node). The identity table is read under its
 * writer mutex; no engine callback or RNG execution holds that mutex. */
#ifdef ECONOMY_RNG_CANARY
static void economy_rng_canary_clobber(void);
__attribute__((used)) static __thread uint64_t economy_rng_test_tls;
#define ECONOMY_RNG_TLS "economy_rng_test_tls@tpoff"
#else
#define ECONOMY_RNG_TLS "0xffffffffffffd8e0"
#endif
__attribute__((used,noinline)) static economy_rng_choice economy_rng_choose(const void *original_stack) {
    int incoming_errno = errno;
#ifdef ECONOMY_RNG_CANARY
    economy_rng_canary_clobber();
#endif
    economy_rng_choice result = {0, 1};
    uintptr_t object = 0, vtable = 0;
    uint64_t runtime = 0, last_bits = 0, saved = 0;
    unsigned reason = 1; /* disabled or outside the permitted loading phase */
    unsigned phase = 0;
    double last;
    pthread_rwlock_rdlock(&economy_rng_readers);
    if (!economy_rng_enabled) goto fallback;
    phase = boot_phase;
    if (!stats.active || !boot_mode || phase == 0 || phase >= 3) goto fallback;
    reason = 2; /* unreadable import frame or object */
    if (!gate_read_local(original_stack, &object, sizeof(object)) || !object ||
        !gate_read_local((void *)object, &vtable, sizeof(vtable)) ||
        !gate_read_local((void *)(object + LXW_OBJ_RUNTIME), &runtime, sizeof(runtime)) ||
        !gate_read_local((void *)(object + 0xa70), &last_bits, sizeof(last_bits))) goto fallback;
    memcpy(&last, &last_bits, sizeof(last));
    reason = 3; /* invalid object, runtime or last time */
    const lxw_class *class_info = lxw_class_of(vtable);
    if (!runtime || !isfinite(last) || !class_info || class_info->family != LXW_FAMILY_NONE || !world_identities)
        goto fallback;
    if (!world_identity_sealed) lxw_resolve(1);
    pthread_mutex_lock(&lxw_insert_lock);
    saved = lxw_identity_probe(runtime);
    pthread_mutex_unlock(&lxw_insert_lock);
    reason = 4; /* no saved identity for this runtime object */
    if (!saved) goto fallback;
    result.word = lockstep_seed(ECONOMY_RNG_DOMAIN, 0, saved, last_bits, 0, 0);
    result.fallback = 0;
    uint64_t draw = result.word * UINT64_C(0x5851f42d4c957f2d) + UINT64_C(0x14057b7ef767814f);
    draw = (draw >> 30) | (draw << 34);
    uint64_t key_hash = lockstep_seed(ECONOMY_RNG_DOMAIN, 1, saved, last_bits, 0, 0);
    uint64_t draw_hash = lockstep_seed(ECONOMY_RNG_DOMAIN, 2, saved, last_bits, result.word, draw);
    __atomic_fetch_xor(&economy_rng_key_xor, key_hash, __ATOMIC_RELAXED);
    __atomic_fetch_add(&economy_rng_key_sum, key_hash, __ATOMIC_RELAXED);
    __atomic_fetch_xor(&economy_rng_draw_xor, draw_hash, __ATOMIC_RELAXED);
    __atomic_fetch_add(&economy_rng_draw_sum, draw_hash, __ATOMIC_RELAXED);
    uint64_t index = __atomic_fetch_add(&economy_rng_row_count, 1, __ATOMIC_RELAXED);
    if (index < 128) {
        economy_rng_row *row = &economy_rng_rows[index];
        row->saved_id = saved;
        row->last_bits = last_bits;
        row->prepared_seed = result.word;
        row->expected_draw = draw;
        __atomic_store_n(&row->ready, 1, __ATOMIC_RELEASE);
    }
    __atomic_add_fetch(&economy_rng_keyed, 1, __ATOMIC_RELAXED);
    pthread_rwlock_unlock(&economy_rng_readers);
    errno = incoming_errno;
    return result;
fallback:;
    int empty = 0;
    if (__atomic_compare_exchange_n(&economy_rng_failure_sample_state, &empty, 1, 0,
                                    __ATOMIC_ACQ_REL, __ATOMIC_RELAXED)) {
        economy_rng_failure_object = object;
        economy_rng_failure_class = vtable;
        economy_rng_failure_runtime = runtime;
        economy_rng_failure_last = last_bits;
        economy_rng_failure_reason = reason;
        economy_rng_failure_phase = phase;
        __atomic_store_n(&economy_rng_failure_sample_state, 2, __ATOMIC_RELEASE);
    }
    __atomic_add_fetch(&economy_rng_fallback, 1, __ATOMIC_RELAXED);
    InterlockedIncrement64(&economy_rng_failures);
    pthread_rwlock_unlock(&economy_rng_readers);
    errno = incoming_errno;
    return result;
}

/* The first 256 bytes below the incoming RSP are reserved before any push,
 * preserving the 128-byte red zone. Fifteen registers and flags follow. R12
 * then addresses this fixed frame; original RSP is R12+384. A separately
 * aligned 4096-byte XSAVE buffer lies below it. Scratch at R12-8..-32 is
 * outside that buffer. The saved RAX/RDX/flags slots are replaced only after
 * the private PCG has produced the original instruction sequence's outputs. */
__attribute__((naked,used,noinline)) static void economy_rng_entry(void) {
    __asm__ volatile(
        "lea -256(%rsp),%rsp\n\t"
        "pushfq\n\t"
        "push %rax\n\tpush %rbx\n\tpush %rcx\n\tpush %rdx\n\tpush %rbp\n\t"
        "push %rsi\n\tpush %rdi\n\tpush %r8\n\tpush %r9\n\tpush %r10\n\t"
        "push %r11\n\tpush %r12\n\tpush %r13\n\tpush %r14\n\tpush %r15\n\t"
        "mov %rsp,%r12\n\t"
        "sub $4096,%rsp\n\ttestb $0,(%rsp)\n\t"
        "sub $256,%rsp\n\tand $-64,%rsp\n\t"
        "mov %rsp,%rdi\n\txor %eax,%eax\n\tmov $512,%ecx\n\tcld\n\trep stosq\n\t"
        "mov economy_rng_xstate_mask(%rip),%eax\n\txor %edx,%edx\n\txsave64 (%rsp)\n\t"
        "lea 384(%r12),%rdi\n\tcall economy_rng_choose\n\t"
        "mov %rax,-8(%r12)\n\tmov %rdx,-16(%r12)\n\t"
        "mov economy_rng_xstate_mask(%rip),%eax\n\txor %edx,%edx\n\txrstor64 (%rsp)\n\t"
        "mov -8(%r12),%rcx\n\tcmpq $0,-16(%r12)\n\tje 1f\n\t"
        "mov %fs:" ECONOMY_RNG_TLS ",%rcx\n\t1:\n\t"
        "pushq 120(%r12)\n\tpopfq\n\t"
        "movabs $0x5851f42d4c957f2d,%rax\n\t"
        "pxor %xmm2,%xmm2\n\t"
        "movabs $0x14057b7ef767814f,%rdx\n\t"
        "imul %rcx,%rax\n\tadd %rdx,%rax\n\tror $30,%rax\n\t"
        "pushfq\n\tpopq -24(%r12)\n\t"
        "cmpq $0,-16(%r12)\n\tje 2f\n\tmov %rax,%fs:" ECONOMY_RNG_TLS "\n\t2:\n\t"
        "mov %rax,112(%r12)\n\tmov %rdx,88(%r12)\n\tmov -24(%r12),%rax\n\tmov %rax,120(%r12)\n\t"
        "mov %r12,%rsp\n\t"
        "pop %r15\n\tpop %r14\n\tpop %r13\n\tpop %r12\n\tpop %r11\n\t"
        "pop %r10\n\tpop %r9\n\tpop %r8\n\tpop %rdi\n\tpop %rsi\n\t"
        "pop %rbp\n\tpop %rdx\n\tpop %rcx\n\tpop %rbx\n\tpop %rax\n\t"
        "popfq\n\tlea 256(%rsp),%rsp\n\tjmp *economy_rng_continuation(%rip)\n\t");
}

static void economy_rng_write_stub(unsigned char *page) {
    /* jmp [rip+0]; absolute target, without modifying registers or flags. */
    static const unsigned char jump[6] = {0xff, 0x25, 0, 0, 0, 0};
    uintptr_t target = (uintptr_t)economy_rng_entry;
    memcpy(page, jump, sizeof(jump));
    memcpy(page + sizeof(jump), &target, sizeof(target));
}

static int economy_rng_owned(void) {
    if (!economy_rng_installed) return 1;
    unsigned char expected[14], current[14];
    unsigned __int128 patch;
    economy_rng_write_stub(expected);
    return economy_rng_page && gate_read_local(economy_rng_page, current, sizeof(current)) &&
        !memcmp(expected, current, sizeof(current)) &&
        gate_read_local(LX(ECONOMY_RNG_SITE), &patch, sizeof(patch)) && patch == economy_rng_patched;
}

static int economy_rng_pins(void) {
    unsigned char body[0xb3];
    if (!gate_read_local(LX(0x18412e3u), body, sizeof(body))) return 0;
    size_t offset = ECONOMY_RNG_SITE - 0x18412e3u;
    if (economy_rng_installed) {
        if (!economy_rng_owned()) return 0;
        memcpy(body + offset, economy_rng_block, sizeof(economy_rng_block));
    } else if (memcmp(body + offset, economy_rng_block, sizeof(economy_rng_block))) {
        return 0;
    }
    return hash_ok(body, sizeof(body), "c0aa157d9dd84cc11b389ba5a216f7a7ac167e4f36f8734f83156707400c3a51") &&
        hash_ok(LX(0x183ed80u), 0x60, "d809929aed79ac4e6223569ff6e6e61a523acca05181cd8ffd2ee9540fc173bb");
}

#ifdef ECONOMY_RNG_CANARY
/* Inject the ambiguous swap result in the synthetic ELF test only. */
static int economy_rng_test_swap_failure;
#endif
static int economy_rng_swap(unsigned __int128 expected, unsigned __int128 desired) {
#ifdef ECONOMY_RNG_CANARY
    int failure = economy_rng_test_swap_failure;
    economy_rng_test_swap_failure = 0;
    if (failure == 2) return 0;
#endif
    int swapped = gate_code_swap128(LX(ECONOMY_RNG_SITE), expected, desired);
#ifdef ECONOMY_RNG_CANARY
    if (failure == 1) return 0;
#endif
    return swapped;
}

static int economy_rng_arm(void) {
    if (economy_rng_installed)
        return stats.active && !__atomic_load_n(&economy_rng_failures, __ATOMIC_RELAXED) && economy_rng_pins();
    if (economy_rng_ever_installed) {
        economy_rng_stage = "one-shot";
        return 0;
    }
    if (!stats.active || !boot_mode || boot_phase || stats.issued ||
        GetCurrentThreadId() != ui_thread || !world_identity_installed) {
        economy_rng_stage = "needs";
        return 0;
    }
    if (!economy_rng_xstate()) {
        economy_rng_stage = "xstate";
        return 0;
    }
    if (!economy_rng_pins()) {
        economy_rng_stage = "pins";
        return 0;
    }
    if (!economy_rng_page) economy_rng_page = gate_near_page(LX(ECONOMY_RNG_SITE));
    if (!economy_rng_page) {
        economy_rng_stage = "page";
        return 0;
    }
    intptr_t delta = (intptr_t)economy_rng_page - (intptr_t)LX(0x184133du);
    if (delta < INT32_MIN || delta > INT32_MAX ||
        mprotect(economy_rng_page, GATE_PAGE, PROT_READ | PROT_WRITE)) {
        economy_rng_stage = "page";
        return 0;
    }
    memset(economy_rng_page, 0xcc, GATE_PAGE);
    economy_rng_write_stub(economy_rng_page);
    if (mprotect(economy_rng_page, GATE_PAGE, PROT_READ | PROT_EXEC)) {
        economy_rng_stage = "page";
        return 0;
    }
    economy_rng_continuation = (uintptr_t)LX(ECONOMY_RNG_CONTINUE);
    memcpy(&economy_rng_original, economy_rng_block, sizeof(economy_rng_original));
    economy_rng_patched = economy_rng_original;
    unsigned char *patch = (unsigned char *)&economy_rng_patched;
    int32_t relative = (int32_t)delta;
    patch[8] = 0xe9;
    memcpy(patch + 9, &relative, sizeof(relative));
    economy_rng_keyed = economy_rng_fallback = 0;
    economy_rng_key_xor = economy_rng_key_sum = economy_rng_draw_xor = economy_rng_draw_sum = 0;
    economy_rng_row_count = 0;
    memset(economy_rng_rows, 0, sizeof(economy_rng_rows));
    economy_rng_failures = 0;
    economy_rng_failure_sample_state = 0;
    economy_rng_restored = 0;
    pthread_rwlock_wrlock(&economy_rng_readers);
    economy_rng_enabled = 1;
    pthread_rwlock_unlock(&economy_rng_readers);
    economy_rng_ever_installed = 1;
    if (!economy_rng_swap(economy_rng_original, economy_rng_patched)) {
        /* swap128 also reports a page-protection close failure as false. A
         * readback of original or foreign bytes cannot prove protection was
         * restored, so retain a process-lifetime failed-cleanup verdict. */
        economy_rng_cleanup_uncertain = 1;
        unsigned __int128 current;
        /* A failed protection reset can follow a successful CAS. Retain
         * ownership conservatively until restoration has been established. */
        economy_rng_installed = !gate_read_local(LX(ECONOMY_RNG_SITE), &current, sizeof(current)) ||
                                current == economy_rng_patched;
        pthread_rwlock_wrlock(&economy_rng_readers);
        economy_rng_enabled = 0;
        pthread_rwlock_unlock(&economy_rng_readers);
        economy_rng_stage = "cas";
        return 0;
    }
    economy_rng_installed = 1;
    economy_rng_stage = "armed";
    return 1;
}

static int economy_rng_check(void) {
    if (__atomic_load_n(&economy_rng_failures, __ATOMIC_RELAXED)) {
        economy_rng_stage = "failure";
        return 0;
    }
    if (!economy_rng_pins()) {
        economy_rng_stage = "ownership";
        return 0;
    }
    return 1;
}

static void economy_rng_restore(void) {
    /* A thread can already be in the immortal stub when the site is swapped.
     * Drain readers, then make every late entrant take the native fallback. */
    pthread_rwlock_wrlock(&economy_rng_readers);
    economy_rng_enabled = 0;
    pthread_rwlock_unlock(&economy_rng_readers);
    if (!economy_rng_installed) return;
    if (economy_rng_owned()) {
        if (economy_rng_swap(economy_rng_patched, economy_rng_original)) {
            economy_rng_installed = 0;
        } else {
            economy_rng_cleanup_uncertain = 1;
            unsigned __int128 current;
            if (gate_read_local(LX(ECONOMY_RNG_SITE), &current, sizeof(current)) &&
                current == economy_rng_original)
                economy_rng_stage = "restore-protection";
            else
                economy_rng_stage = "restore-ownership";
        }
    } else {
        economy_rng_stage = "restore-ownership";
    }
    economy_rng_restored = !economy_rng_installed && !economy_rng_cleanup_uncertain;
}

static int lua_economy_load_rng(lua_State *L) {
    p_lua_pushnumber(L, economy_rng_arm());
    return 1;
}

static int lua_economy_load_rng_status(lua_State *L) {
    char out[512];
    int sampled = __atomic_load_n(&economy_rng_failure_sample_state, __ATOMIC_ACQUIRE) == 2;
    int length = accept_ui() ? snprintf(out, sizeof(out),
        "installed=%d prepared=%llu fallback=%llu failures=%lld xcr0=%llx mask=%x restored=%d cleanup_uncertain=%d stage=%s "
        "rows=%llu keyxor=%016llx keysum=%016llx expected_drawxor=%016llx expected_drawsum=%016llx "
        "failure_reason=%u phase=%u object=%016llx class=%016llx runtime=%016llx last=%016llx",
        economy_rng_installed, (unsigned long long)__atomic_load_n(&economy_rng_keyed, __ATOMIC_RELAXED),
        (unsigned long long)__atomic_load_n(&economy_rng_fallback, __ATOMIC_RELAXED),
        (long long)__atomic_load_n(&economy_rng_failures, __ATOMIC_RELAXED),
        (unsigned long long)economy_rng_xcr0, economy_rng_xstate_mask,
        economy_rng_restored, economy_rng_cleanup_uncertain, economy_rng_stage, (unsigned long long)__atomic_load_n(&economy_rng_row_count, __ATOMIC_RELAXED),
        (unsigned long long)__atomic_load_n(&economy_rng_key_xor, __ATOMIC_RELAXED), (unsigned long long)__atomic_load_n(&economy_rng_key_sum, __ATOMIC_RELAXED),
        (unsigned long long)__atomic_load_n(&economy_rng_draw_xor, __ATOMIC_RELAXED), (unsigned long long)__atomic_load_n(&economy_rng_draw_sum, __ATOMIC_RELAXED),
        sampled ? economy_rng_failure_reason : 0, sampled ? economy_rng_failure_phase : 0,
        (unsigned long long)(sampled ? economy_rng_failure_object : 0),
        (unsigned long long)(sampled ? economy_rng_failure_class : 0),
        (unsigned long long)(sampled ? economy_rng_failure_runtime : 0),
        (unsigned long long)(sampled ? economy_rng_failure_last : 0)) : 0;
    if (length > 0 && (size_t)length < sizeof(out)) p_lua_pushlstring(L, out, (size_t)length);
    else p_lua_pushnil(L);
    return 1;
}

static int lua_economy_load_rng_row(lua_State *L) {
    double index = p_lua_tonumber(L, 1);
    if (!accept_ui() || !isfinite(index) || index < 0 || index >= 128 || floor(index) != index) {
        p_lua_pushnil(L);
        return 1;
    }
    const economy_rng_row *row = &economy_rng_rows[(unsigned)index];
    if (!__atomic_load_n(&row->ready, __ATOMIC_ACQUIRE)) {
        p_lua_pushnil(L);
        return 1;
    }
    char out[160];
    int length = snprintf(out, sizeof(out), "index=%u saved=%016llx last=%016llx seed=%016llx expected_draw=%016llx",
        (unsigned)index, (unsigned long long)row->saved_id, (unsigned long long)row->last_bits,
        (unsigned long long)row->prepared_seed, (unsigned long long)row->expected_draw);
    if (length > 0 && (size_t)length < sizeof(out)) p_lua_pushlstring(L, out, (size_t)length);
    else p_lua_pushnil(L);
    return 1;
}
