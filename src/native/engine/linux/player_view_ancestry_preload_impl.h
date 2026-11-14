#ifndef X4_PLAYER_VIEW_ANCESTRY_PRELOAD_IMPL_H
#define X4_PLAYER_VIEW_ANCESTRY_PRELOAD_IMPL_H
#include <sys/uio.h>
#include "player_view_ancestry_preload.h"
#include "gate_world_classes.h"
#include "player_stock_view_policy_guard.h"
#include "player_stock_view_precompare_preload.h"

/* These exact calls have no source arguments. Their enclosing methods retain
 * the component in RBX (weapon) and R12 (engine), respectively. */
static uint64_t (*view_ancestry_originals[2])(void) = {
    (uint64_t (*)(void))0x1d6b1c0u, (uint64_t (*)(void))0x1e07020u
};
static _Atomic unsigned view_precompare_active, view_precompare_inflight;
static _Thread_local unsigned view_precompare_depth;
static player_stock_view_precompare_policy view_precompare_policy;
static uintptr_t (*view_precompare_original)(void) = (uintptr_t (*)(void))0x1dd81c0u;
static _Atomic unsigned view_ancestry_active;
static _Atomic uint64_t view_ancestry_foreign[VIEW_ANCESTRY_SITES];
static uint32_t view_ancestry_owner;
static uint64_t view_ancestry_generation;
static uint64_t view_ancestry_last_walk[VIEW_ANCESTRY_SITES];
static unsigned view_ancestry_candidate_ordinal[VIEW_ANCESTRY_SITES];
static view_ancestry_snapshot view_ancestry_capture;
static unsigned view_ancestry_installed;
static view_ancestry_info view_ancestry_information;
static int view_precompare_owned(void);
static void view_component_precompare_observe(uintptr_t object, uintptr_t ancestor, uintptr_t result);
static void view_component_trace_observe(unsigned site, uintptr_t object, uintptr_t engine_ancestor);
static int view_component_trace_advance(uint64_t generation);

/* Preserves the exact native ship prologue and supplies unwind state for it. */
#if !defined(VIEW_ANCESTRY_TEST) || defined(VIEW_ANCESTRY_SYNTHETIC)
__asm__(".text\n"
        ".hidden view_ancestry_ship_original\n"
        ".type view_ancestry_ship_original,@function\n"
        "view_ancestry_ship_original:\n"
        ".cfi_startproc\n"
        "push %r13\n.cfi_def_cfa_offset 16\n.cfi_offset %r13,-16\n"
        "push %r12\n.cfi_def_cfa_offset 24\n.cfi_offset %r12,-24\n"
        "push %rbp\n.cfi_def_cfa_offset 32\n.cfi_offset %rbp,-32\n"
        "movabs $0x129bad5,%r11\njmp *%r11\n"
        ".cfi_endproc\n.size view_ancestry_ship_original,.-view_ancestry_ship_original\n"
        ".hidden view_ancestry_ship_original_end\nview_ancestry_ship_original_end:\n");
#endif
__asm__(".text\n"
        ".hidden view_precompare_bridge\n"
        ".type view_precompare_bridge,@function\n"
        "view_precompare_bridge:\n.cfi_startproc\nmov %r12,%rdi\nmov %rbx,%rsi\n"
        "jmp view_precompare_dispatch\n.cfi_endproc\n"
        ".size view_precompare_bridge,.-view_precompare_bridge\n"
        ".hidden view_ancestry_weapon_bridge\n"
        ".type view_ancestry_weapon_bridge,@function\n"
        "view_ancestry_weapon_bridge:\n.cfi_startproc\nmov %rbx,%rdi\n"
        "jmp view_ancestry_weapon_dispatch\n.cfi_endproc\n"
        ".size view_ancestry_weapon_bridge,.-view_ancestry_weapon_bridge\n"
        ".hidden view_ancestry_engine_bridge\n"
        ".type view_ancestry_engine_bridge,@function\n"
        "view_ancestry_engine_bridge:\n.cfi_startproc\nmov %r12,%rdi\nmov %rbx,%rsi\n"
        "jmp view_ancestry_engine_dispatch\n.cfi_endproc\n"
        ".size view_ancestry_engine_bridge,.-view_ancestry_engine_bridge\n");
extern uint64_t view_ancestry_ship_original(void *, void *) __attribute__((visibility("hidden")));
#if !defined(VIEW_ANCESTRY_TEST) || defined(VIEW_ANCESTRY_SYNTHETIC)
extern const unsigned char view_ancestry_ship_original_end[] __attribute__((visibility("hidden")));
#endif
extern void view_precompare_bridge(void) __attribute__((visibility("hidden")));
extern void view_ancestry_weapon_bridge(void) __attribute__((visibility("hidden")));
extern void view_ancestry_engine_bridge(void) __attribute__((visibility("hidden")));

static int view_ancestry_copy(void *destination, uintptr_t source, size_t size) {
    struct iovec local = {destination, size};
    struct iovec remote = {(void *)source, size};
    return syscall(SYS_process_vm_readv, (long)syscall(SYS_getpid), &local, 1UL,
                   &remote, 1UL, 0UL) == (long)size;
}
/* Only exact pinned primary Component-class vtables authorize another link read.
 * A readable pointer alone does not identify a Component. */
static int view_ancestry_component_class(uintptr_t vtable) {
    for (unsigned index = 0; index < LXW_CLASS_COUNT; ++index) {
        if (lxw_classes[index].linux_vtable == vtable && !lxw_classes[index].family &&
            !lxw_classes[index].top) return 1;
    }
    return 0;
}
static void view_ancestry_record(unsigned site, uintptr_t self, uintptr_t argument) {
    view_ancestry_site *capture = &view_ancestry_capture.sites[site];
    /* Spread the bounded observations across the session; the last_move
     * value remains a worker-published generation, not a global clock. */
    if (view_ancestry_last_walk[site] &&
        view_ancestry_generation - view_ancestry_last_walk[site] < 6) {
        ++capture->throttled;
        return;
    }
    /* At most 128 bounded walks per site; retain two samples per category. */
    if (capture->examined == 128) { ++capture->overflow; return; }
    /* Rotate through the first 16 calls in successive eligible windows. This
     * avoids always sampling the first object, while preserving the six-tick
     * spacing and bounded number of component reads. An eligible window may
     * span multiple generations if the selected ordinal has not arrived. */
    unsigned desired = (unsigned)(capture->examined % 16u);
    if (view_ancestry_candidate_ordinal[site]++ < desired) {
        ++capture->throttled;
        return;
    }
    view_ancestry_candidate_ordinal[site] = 0;
    ++capture->examined;
    view_ancestry_last_walk[site] = view_ancestry_generation;
    view_ancestry_sample sample = {0};
    sample.generation = view_ancestry_generation;
    sample.tid = view_ancestry_owner;
    sample.self = self;
    sample.argument = argument;
    uintptr_t object = self;
    for (unsigned index = 0; object && index < VIEW_ANCESTRY_DEPTH; ++index) {
        uintptr_t parent = 0;
        view_ancestry_node *node = &sample.nodes[index];
        node->component = object;
        if (object > UINTPTR_MAX - 0x78 ||
            !view_ancestry_copy(&node->vtable, object, sizeof node->vtable) ||
            !view_ancestry_component_class(node->vtable) ||
            !view_ancestry_copy(&node->runtime, object + 8, sizeof node->runtime) ||
            !view_ancestry_copy(&parent, object + 0x70, sizeof parent)) break;
        ++sample.count;
        if (!parent) { sample.complete = 1; break; }
        if (parent == object) break;
        object = parent;
    }
    unsigned first = 0, second = 0;
    for (unsigned index = 0; index < sample.count; ++index) {
        first |= sample.nodes[index].component == view_ancestry_capture.first_ship;
        second |= sample.nodes[index].component == view_ancestry_capture.second_ship;
    }
    if (first != second) sample.category = first ? 1u : 2u;
    else if (!first && sample.complete) sample.category = 3u;
    if (capture->category_count[sample.category] < 2 && capture->count < VIEW_ANCESTRY_SAMPLES) {
        ++capture->category_count[sample.category];
        capture->samples[capture->count++] = sample;
    }
}
static int view_ancestry_admit(unsigned site) {
    if (!atomic_load_explicit(&view_ancestry_active, memory_order_acquire)) return 0;
    if (current_thread() != view_ancestry_owner) {
        atomic_fetch_add_explicit(&view_ancestry_foreign[site], 1, memory_order_relaxed);
        return 0;
    }
    if (!view_ancestry_generation) return 0;
    ++view_ancestry_capture.sites[site].started;
    return 1;
}
__attribute__((visibility("hidden"), noinline)) uintptr_t view_precompare_dispatch(uintptr_t object, uintptr_t ancestor) {
    int before = errno;
    unsigned active = stock_view_policy_enter(&view_precompare_active, &view_precompare_inflight);
    if (active && (view_precompare_depth++ || !view_precompare_owned())) _exit(223);
    errno = before;
    uintptr_t result = view_precompare_original();
    int saved = errno;
    view_component_precompare_observe(object, ancestor, result);
    if (active) {
        uintptr_t routed = result;
        if (!view_precompare_policy(object, ancestor, result, &routed)) _exit(223);
        result = routed;
        --view_precompare_depth;
    }
    stock_view_policy_leave(&view_precompare_inflight);
    errno = saved;
    return result;
}
__attribute__((visibility("hidden"), noinline)) uint64_t view_ancestry_weapon_dispatch(void *object) {
    int before = errno;
#ifdef VIEW_ANCESTRY_SHARED_COMPONENT
    unsigned component_active = stock_view_component_enter();
#endif
    int admitted = view_ancestry_admit(0);
    if (admitted) view_ancestry_record(0, (uintptr_t)object, 0);
    view_component_trace_observe(0, (uintptr_t)object, 0);
    errno = before;
    uint64_t result = view_ancestry_originals[0]();
    int after = errno;
    if (admitted) ++view_ancestry_capture.sites[0].returned;
#ifdef VIEW_ANCESTRY_SHARED_COMPONENT
    result = stock_view_component_finish(component_active, 0, (uintptr_t)object, result);
#endif
    errno = after;
    return result;
}
__attribute__((visibility("hidden"), noinline)) uint64_t view_ancestry_engine_dispatch(void *object, uintptr_t engine_ancestor) {
    int before = errno;
#ifdef VIEW_ANCESTRY_SHARED_COMPONENT
    unsigned component_active = stock_view_component_enter();
#endif
    int admitted = view_ancestry_admit(1);
    if (admitted) view_ancestry_record(1, (uintptr_t)object, 0);
    view_component_trace_observe(1, (uintptr_t)object, engine_ancestor);
    errno = before;
    uint64_t result = view_ancestry_originals[1]();
    int after = errno;
    if (admitted) ++view_ancestry_capture.sites[1].returned;
#ifdef VIEW_ANCESTRY_SHARED_COMPONENT
    result = stock_view_component_finish(component_active, 1, (uintptr_t)object, result);
#endif
    errno = after;
    return result;
}
__attribute__((noinline)) static uint64_t view_ancestry_ship_dispatch(void *object, void *argument) {
    int before = errno;
    int admitted = view_ancestry_admit(2);
    if (admitted) view_ancestry_record(2, (uintptr_t)object, (uintptr_t)argument);
    errno = before;
    uint64_t result = view_ancestry_ship_original(object, argument);
    int after = errno;
    if (admitted) ++view_ancestry_capture.sites[2].returned;
    errno = after;
    return result;
}
/* The pin provider performs full composite image authentication; this
 * resident bind additionally checks both owned callwords and original slot at
 * the binding boundary. No patch is installed or removed here. */
static int view_precompare_owned(void) {
    const view_ancestry_info *info = &view_ancestry_information;
    return view_ancestry_installed && info->version == VIEW_ANCESTRY_VERSION && info->installed == 1 &&
        info->pre_enabled == 1 && info->pre_block == 0x177f9a0u &&
        info->blocks[1] == 0x177f9c0u && info->pre_original_target == 0x1dd81c0u &&
        info->pre_original_slot == (uintptr_t)&view_precompare_original &&
        (uintptr_t)view_precompare_original == info->pre_original_target &&
        __atomic_load_n((uint64_t *)info->pre_block, __ATOMIC_SEQ_CST) == info->pre_patched_word &&
        __atomic_load_n((uint64_t *)info->blocks[1], __ATOMIC_SEQ_CST) == info->patched_words[1];
}
EXPORT int x4mp_stock_view_precompare_bind(player_stock_view_precompare_policy policy) {
    if (!policy || !view_precompare_owned() ||
        !stock_view_policy_begin_bind(&view_precompare_active, &view_precompare_inflight)) return 0;
    if (!view_precompare_owned()) {
        atomic_store_explicit(&view_precompare_active, 0, memory_order_seq_cst);
        return 0;
    }
    view_precompare_policy = policy;
    atomic_store_explicit(&view_precompare_active, 1, memory_order_seq_cst);
    return 1;
}
EXPORT int x4mp_view_ancestry_info(view_ancestry_info *out) {
    if (!out || !view_ancestry_installed) return 0;
    *out = view_ancestry_information;
    return 1;
}
EXPORT int x4mp_view_ancestry_bind(uint32_t owner, uintptr_t first, uintptr_t second) {
    if (!view_ancestry_installed || !owner || current_thread() != owner || !first || !second || first == second ||
        atomic_load_explicit(&view_ancestry_active, memory_order_acquire) || view_ancestry_capture.closed) return 0;
    view_ancestry_owner = owner;
    view_ancestry_capture.first_ship = first;
    view_ancestry_capture.second_ship = second;
    view_ancestry_capture.version = VIEW_ANCESTRY_VERSION;
    view_ancestry_capture.installed = 1;
    atomic_store_explicit(&view_ancestry_active, 1, memory_order_release);
    return 1;
}
EXPORT int x4mp_view_ancestry_tick(uint64_t generation) {
    if (!atomic_load_explicit(&view_ancestry_active, memory_order_acquire) ||
        current_thread() != view_ancestry_owner || !generation || generation <= view_ancestry_generation) return 0;
    for (unsigned site = 0; site < VIEW_ANCESTRY_SITES; ++site) {
        if (view_ancestry_capture.sites[site].started != view_ancestry_capture.sites[site].returned) return 0;
    }
    if (!view_component_trace_advance(generation)) return 0;
    view_ancestry_generation = generation;
    return 1;
}
EXPORT int x4mp_view_ancestry_finish(view_ancestry_snapshot *out) {
    if (!out || !atomic_load_explicit(&view_ancestry_active, memory_order_acquire) ||
        current_thread() != view_ancestry_owner || !view_ancestry_generation) return 0;
    for (unsigned site = 0; site < VIEW_ANCESTRY_SITES; ++site) {
        if (view_ancestry_capture.sites[site].started != view_ancestry_capture.sites[site].returned) return 0;
    }
    atomic_store_explicit(&view_ancestry_active, 0, memory_order_release);
    view_ancestry_capture.closed = 1;
    for (unsigned site = 0; site < VIEW_ANCESTRY_SITES; ++site)
        view_ancestry_capture.sites[site].foreign = atomic_load_explicit(&view_ancestry_foreign[site], memory_order_relaxed);
    *out = view_ancestry_capture;
    return 1;
}
/* All executable edits are pre-main and terminal on any partial failure. */
#if !defined(VIEW_ANCESTRY_TEST) || defined(VIEW_ANCESTRY_SYNTHETIC)
static unsigned char view_ancestry_frames[VIEW_ANCESTRY_SITES + 1][48] __attribute__((aligned(8)));
static int view_ancestry_install(void) {
    static const uintptr_t blocks[] = {0x1113338u, 0x177f9c0u, 0x129bad0u};
    static const unsigned offsets[] = {1, 0, 0};
    static const uint64_t words[] = {
        UINT64_C(0x835b00c57e82e818), UINT64_C(0x75c0840068765be8),
        UINT64_C(0xfd89485554415541)
    };
    const char *pre = getenv("X4_DEV_VIEW_PRECOMPARE_TRACE");
    if (pre && (strcmp(pre, "1") || !getenv("X4_DEV_VIEW_COMPONENT_TRACE") ||
                strcmp(getenv("X4_DEV_VIEW_COMPONENT_TRACE"), "1"))) return 0;
    const uintptr_t pre_block = 0x177f9a0u;
    const uint64_t pre_word = UINT64_C(0x4800658819e80c24);
    if (pre && (!is_rx(pre_block) || __atomic_load_n((uint64_t *)pre_block, __ATOMIC_SEQ_CST) != pre_word ||
        !ui_code_matches(0x1dd81c0u, 85, "a0437b5716ca6a2143d11a0d25f75fb3ca04342de8588818d339ca512e75c4a6"))) return 0;
    if (!single_thread() ||
        !ui_code_matches(0x11132d0u, 133, "48dd904179d64a8b5f75a87b5b989a3f7c215342c71ac5132f07b7b86b8d3b58") ||
        !ui_code_matches(0x1113360u, 76, "5798f27c36fee251792a259c2e5b74ba1e647907ea96fb177bcccb73b8535fdc") ||
        !ui_code_matches(0x1d6b1c0u, 99, "28662e2439a706b8794ec0c9dbe9b2b31aef33695461446cfbba6466a58967f6") ||
        !ui_code_matches(0x177f900u, 224, "d46a2e354ebd8f76037356afe53a778117f45401b2dfd3c64727ae42e8dfd0ef") ||
        !ui_code_matches(0x1e07020u, 51, "7fbd4e7bdb604524025c4a46cc791ae7137b7c1f19270213781e35e5b7890f51") ||
        !ui_code_matches(0x129bad0u, 456, "cff3c5caae429ad58f93402f0d0b754c82d467823d8298c3641b1459a1605e50")) return 0;
    for (unsigned site = 0; site < VIEW_ANCESTRY_SITES; ++site) {
        if (!is_rx(blocks[site]) || __atomic_load_n((uint64_t *)blocks[site], __ATOMIC_SEQ_CST) != words[site]) return 0;
    }
    unsigned char *page = allocate_near();
    if (!page) return 0;
    uintptr_t dispatch[] = {(uintptr_t)view_ancestry_weapon_bridge,
        (uintptr_t)view_ancestry_engine_bridge, (uintptr_t)view_ancestry_ship_dispatch};
    uint64_t replacements[VIEW_ANCESTRY_SITES];
    memset(page, 0xcc, PVQ_PAGE);
    for (unsigned site = 0; site < VIEW_ANCESTRY_SITES; ++site) {
        uintptr_t at = (uintptr_t)(page + site * 16u);
        emit_entry_stub((unsigned char *)at, dispatch[site]);
        int64_t displacement = (int64_t)at - (int64_t)(blocks[site] + offsets[site] + 5);
        if (displacement < INT32_MIN || displacement > INT32_MAX) return 0;
        unsigned char bytes[8];
        memcpy(bytes, &words[site], sizeof bytes);
        bytes[offsets[site]] = site == 2 ? 0xe9 : 0xe8;
        int32_t relative = (int32_t)displacement;
        memcpy(bytes + offsets[site] + 1, &relative, 4);
        memcpy(&replacements[site], bytes, sizeof bytes);
        if (!register_frame_for_entry(at, view_ancestry_frames[site])) return 0;
    }
    uint64_t pre_replacement = 0;
    if (pre) {
        uintptr_t at = (uintptr_t)(page + VIEW_ANCESTRY_SITES * 16u);
        emit_entry_stub((unsigned char *)at, (uintptr_t)view_precompare_bridge);
        int64_t displacement = (int64_t)at - (int64_t)(pre_block + 7);
        if (displacement < INT32_MIN || displacement > INT32_MAX) return 0;
        unsigned char bytes[8]; memcpy(bytes, &pre_word, 8);
        bytes[2] = 0xe8; int32_t relative = (int32_t)displacement;
        memcpy(bytes + 3, &relative, 4); memcpy(&pre_replacement, bytes, 8);
        if (!register_frame_for_entry(at, view_ancestry_frames[VIEW_ANCESTRY_SITES])) return 0;
    }
    if (mprotect(page, PVQ_PAGE, PROT_READ | PROT_EXEC) || !is_rx((uintptr_t)page) || !single_thread()) return 0;
    for (unsigned site = 0; site < VIEW_ANCESTRY_SITES; ++site) {
        uintptr_t base = blocks[site] & ~(uintptr_t)(PVQ_PAGE - 1);
        uint64_t expected = words[site];
        if (mprotect((void *)base, PVQ_PAGE, PROT_READ | PROT_WRITE | PROT_EXEC) ||
            !__atomic_compare_exchange_n((uint64_t *)blocks[site], &expected, replacements[site], 0,
                                         __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST)) return 0;
        __builtin___clear_cache((char *)blocks[site], (char *)blocks[site] + 8);
        if (mprotect((void *)base, PVQ_PAGE, PROT_READ | PROT_EXEC) || !is_rx(blocks[site])) return 0;
    }
    if (pre) {
        uintptr_t base = pre_block & ~(uintptr_t)(PVQ_PAGE - 1);
        uint64_t expected = pre_word;
        if (mprotect((void *)base, PVQ_PAGE, PROT_READ | PROT_WRITE | PROT_EXEC) ||
            !__atomic_compare_exchange_n((uint64_t *)pre_block, &expected, pre_replacement, 0,
                                         __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST)) return 0;
        __builtin___clear_cache((char *)pre_block, (char *)pre_block + 8);
        if (mprotect((void *)base, PVQ_PAGE, PROT_READ | PROT_EXEC) || !is_rx(pre_block)) return 0;
        view_ancestry_information.pre_enabled = 1;
        view_ancestry_information.pre_block = pre_block;
        view_ancestry_information.pre_stub = (uintptr_t)(page + VIEW_ANCESTRY_SITES * 16u);
        view_ancestry_information.pre_dispatch = (uintptr_t)view_precompare_bridge;
        view_ancestry_information.pre_frame = (uintptr_t)view_ancestry_frames[VIEW_ANCESTRY_SITES];
        view_ancestry_information.pre_original_slot = (uintptr_t)&view_precompare_original;
        view_ancestry_information.pre_original_target = (uintptr_t)view_precompare_original;
        view_ancestry_information.pre_original_word = pre_word;
        view_ancestry_information.pre_patched_word = pre_replacement;
    }
    view_ancestry_information.version = VIEW_ANCESTRY_VERSION;
    view_ancestry_information.installed = 1;
    view_ancestry_information.ship_trampoline = (uintptr_t)view_ancestry_ship_original;
    view_ancestry_information.ship_trampoline_size =
        (uint32_t)((uintptr_t)view_ancestry_ship_original_end - (uintptr_t)view_ancestry_ship_original);
    for (unsigned site = 0; site < 2; ++site) {
        view_ancestry_information.original_slots[site] = (uintptr_t)&view_ancestry_originals[site];
        view_ancestry_information.original_targets[site] = (uintptr_t)view_ancestry_originals[site];
    }
    view_ancestry_information.frame_size = sizeof view_ancestry_frames[0];
    view_ancestry_information.stub_size = 14;
    view_ancestry_information.page_size = PVQ_PAGE;
    for (unsigned site = 0; site < VIEW_ANCESTRY_SITES; ++site) {
        view_ancestry_information.blocks[site] = blocks[site];
        view_ancestry_information.stubs[site] = (uintptr_t)(page + site * 16u);
        view_ancestry_information.dispatchers[site] = dispatch[site];
        view_ancestry_information.frames[site] = (uintptr_t)view_ancestry_frames[site];
        view_ancestry_information.original_words[site] = words[site];
        view_ancestry_information.patched_words[site] = replacements[site];
        view_ancestry_information.offsets[site] = offsets[site];
    }
    view_ancestry_installed = 1;
    return 1;
}
#endif /* VIEW_ANCESTRY_TEST */
#include "player_view_component_trace_preload_impl.h"
#endif
