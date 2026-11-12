#define _GNU_SOURCE
#include "player_ui_escape_guard.h"
/* Disposable, pre-main only. The detour and module remain resident. */
#include <dirent.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>
#include "../lockstep_sha256.h"
#include "player_view_query_preload.h"
#include "player_live_pose_preload.h"
#define EXPORT __attribute__((visibility("default")))
#define PVQ_PAGE 4096u
#define PVQ_FILE_SIZE 60459280u
#define PVQ_ENTRY UINT64_C(0xa44e40)
#define PVQ_CONTINUE UINT64_C(0xa44e47)
#define PVQ_GLOBAL UINT64_C(0x3db6948)
static const char image_sha[] = "113f73d74570084f6ce18a659b365009eccf579fb66dbabb5dad2b1718045f20";
static const char function_sha[] = "c4ae051dfa320a46f8049b7e35f5f686c7f4455e98f33ce52b13c2cbe0c134d3";
static const uint64_t original_word = UINT64_C(0x4803371b01158b48);
static player_view_query_info information;
static _Atomic uint32_t owner_tid;
static uint32_t worker_tid;
static uint64_t current_tick;
static unsigned current_probe;
static _Atomic uint64_t ui_calls, worker_calls, other_calls;
static player_view_query_snapshot samples;
static player_view_query_snapshot finished_samples;
static unsigned finished;
static uint32_t current_thread(void);
static uint64_t (*original_query)(void);

/* Each callback keeps its own per-thread nesting; counters are independent
 * of the existing occupied-query receipt and include foreign threads. */
typedef struct {
    _Atomic uint64_t entered, left, queries, foreign, nested, mismatch, inflight, thread_mismatch;
    _Atomic uint32_t first_tid;
} ui_callback_counts;
static ui_callback_counts ui_counts[2];
static _Atomic uintptr_t ui_object;
static _Atomic unsigned ui_installed;
/* REQUEST prevents further admission; the last admitted callback atomically
 * closes the epoch and emits the receipt after returning from stock. */
#define UI_REQUEST (UINT64_C(1) << 63)
#define UI_CLOSED (UINT64_C(1) << 62)
#define UI_COUNT_MASK (UI_CLOSED - 1)
static _Atomic uint64_t ui_epoch;
static void ui_emit_final(void);
static _Thread_local unsigned ui_depth[2];
static _Atomic unsigned ui_overlay_binding; /* 0 unused, 1 pending, 2 executing, 3 consumed */
static unsigned ui_overlay_remaining;
static uint32_t ui_overlay_tid;
static uintptr_t ui_overlay_object;
static player_ui_overlay_callback ui_overlay_enter, ui_overlay_leave;
static void ui_overlay_abort(void) {
    static const char message[] = "X4 UI OVERLAY FATAL preload_boundary\n";
    (void)write(2, message, sizeof message - 1);
    _exit(223);
}
static void (*ui_original[2])(void *);
static const uintptr_t ui_slots[2] = { 0x3ab4988u, 0x3ab4990u };
static const uintptr_t ui_originals[2] = { 0xa77e90u, 0xa60520u };

static void ui_callback_dispatch(unsigned kind, void *object) {
    int before = errno;
    unsigned overlay_active = 0;
    unsigned overlay_state = atomic_load_explicit(&ui_overlay_binding, memory_order_acquire);
    if (!kind && overlay_state == 2) ui_overlay_abort();
    if (!kind && overlay_state == 1) {
        unsigned expected = 1;
        if (current_thread() != ui_overlay_tid || (uintptr_t)object != ui_overlay_object ||
            ui_depth[0] || ui_depth[1] ||
            !atomic_compare_exchange_strong_explicit(&ui_overlay_binding, &expected, 2,
                                                      memory_order_acq_rel, memory_order_acquire)) ui_overlay_abort();
        overlay_active = 1;
    }
    unsigned active = 0;
    if (atomic_load_explicit(&owner_tid, memory_order_acquire)) {
        uint64_t state = atomic_load_explicit(&ui_epoch, memory_order_acquire);
        while (!(state & (UI_REQUEST | UI_CLOSED)) && (state & UI_COUNT_MASK) != UI_COUNT_MASK) {
            if (atomic_compare_exchange_weak_explicit(&ui_epoch, &state, state + 1,
                                                       memory_order_acq_rel, memory_order_acquire)) {
                active = 1;
                break;
            }
        }
        /* A nested call after the finish request still invalidates the scope. */
        if (!active && (state & UI_REQUEST) && (ui_depth[0] || ui_depth[1]))
            atomic_fetch_add(&ui_counts[kind].nested, 1);
    }
    if (active) {
        ui_callback_counts *counts = &ui_counts[kind];
        atomic_fetch_add(&counts->entered, 1);
        atomic_fetch_add(&counts->inflight, 1);
        uint32_t tid = current_thread();
        if (tid != atomic_load_explicit(&owner_tid, memory_order_acquire))
            atomic_fetch_add(&counts->foreign, 1);
        uint32_t first_tid = 0;
        (void)atomic_compare_exchange_strong(&counts->first_tid, &first_tid, tid);
        if (atomic_load(&counts->first_tid) != tid) atomic_fetch_add(&counts->thread_mismatch, 1);
        if (ui_depth[0] || ui_depth[1]) atomic_fetch_add(&counts->nested, 1);
        uintptr_t expected = 0;
        (void)atomic_compare_exchange_strong(&ui_object, &expected, (uintptr_t)object);
        if (atomic_load(&ui_object) != (uintptr_t)object) atomic_fetch_add(&counts->mismatch, 1);
        ++ui_depth[kind];
    }
    errno = before;
    if (overlay_active) {
        player_ui_escape_guard_call(ui_original[kind], object, ui_overlay_enter, ui_overlay_leave);
        if (!ui_overlay_remaining) ui_overlay_abort();
        --ui_overlay_remaining;
        atomic_store_explicit(&ui_overlay_binding, ui_overlay_remaining ? 1 : 3, memory_order_release);
    } else {
        ui_original[kind](object);
    }
    int after = errno;
    if (active) {
        --ui_depth[kind];
        atomic_fetch_add(&ui_counts[kind].left, 1);
        atomic_fetch_sub(&ui_counts[kind].inflight, 1);
        uint64_t prior = atomic_fetch_sub_explicit(&ui_epoch, 1, memory_order_acq_rel);
        if (prior == (UI_REQUEST | 1)) {
            uint64_t expected = UI_REQUEST;
            if (atomic_compare_exchange_strong_explicit(&ui_epoch, &expected, UI_CLOSED,
                                                        memory_order_acq_rel, memory_order_acquire))
                ui_emit_final();
        }
    }
    errno = after;
}
static void ui_move_dispatch(void *object) { ui_callback_dispatch(0, object); }
static void ui_render_dispatch(void *object) { ui_callback_dispatch(1, object); }


/* Recreates the seven-byte RIP-relative load. The compiled symbol has CFI. */
__asm__(".text\n"
        ".hidden player_view_query_original\n"
        ".type player_view_query_original,@function\n"
        "player_view_query_original:\n"
        ".cfi_startproc\n"
        "movabs $0x3db6948,%rdx\n"
        "mov (%rdx),%rdx\n"
        "movabs $0xa44e47,%r11\n"
        "jmp *%r11\n"
        ".cfi_endproc\n"
        ".size player_view_query_original,.-player_view_query_original\n");
extern uint64_t player_view_query_original(void) __attribute__((visibility("hidden")));
static void refuse(void) {
    static const char message[] = "X4 VIEW QUERY PRELOAD REFUSED\n";
    (void)write(2, message, sizeof message - 1);
    _exit(223);
}
static int digest_matches(const unsigned char digest[32], const char *expected) {
    static const char hex[] = "0123456789abcdef";
    for (unsigned index = 0; index < 32; ++index) {
        if (hex[digest[index] >> 4] != expected[2 * index] ||
            hex[digest[index] & 15] != expected[2 * index + 1]) {
            return 0;
        }
    }
    return 1;
}
static int image_matches(void) {
    int fd = open("/proc/self/exe", O_RDONLY | O_CLOEXEC);
    struct stat status;
    if (fd < 0 || fstat(fd, &status) || status.st_size != PVQ_FILE_SIZE) {
        if (fd >= 0) {
            close(fd);
        }
        return 0;
    }
    lockstep_sha256_ctx context;
    lockstep_sha256_init(&context);
    unsigned char buffer[65536], digest[32];
    ssize_t count;
    while ((count = read(fd, buffer, sizeof buffer)) > 0) {
        lockstep_sha256_update(&context, buffer, (size_t)count);
    }
    close(fd);
    if (count < 0) {
        return 0;
    }
    lockstep_sha256_final(&context, digest);
    return digest_matches(digest, image_sha);
}
static int single_thread(void) {
    DIR *directory = opendir("/proc/self/task");
    if (!directory) {
        return 0;
    }
    unsigned count = 0;
    struct dirent *entry;
    errno = 0;
    while ((entry = readdir(directory))) {
        if (entry->d_name[0] != '.') {
            ++count;
        }
    }
    int read_error = errno;
    int close_error = closedir(directory);
    return count == 1 && !read_error && !close_error;
}
static int has_permissions(uintptr_t address, const char *required) {
    FILE *maps = fopen("/proc/self/maps", "r");
    if (!maps) {
        return 0;
    }
    char line[1024], permissions[5];
    unsigned long start, end;
    int found = 0;
    while (fgets(line, sizeof line, maps)) {
        if (sscanf(line, "%lx-%lx %4s", &start, &end, permissions) == 3 &&
            address >= start && address < end) {
            found = !strcmp(permissions, required);
            break;
        }
    }
    fclose(maps);
    return found;
}
static int is_rx(uintptr_t address) { return has_permissions(address, "r-xp"); }
static unsigned char *allocate_near(void) {
    for (uintptr_t address = 0x10000; address < 0x3f0000; address += PVQ_PAGE) {
        void *page = mmap((void *)address, PVQ_PAGE, PROT_READ | PROT_WRITE,
                          MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
        if (page != MAP_FAILED) {
            return page;
        }
    }
    return NULL;
}
static uint32_t current_thread(void) {
    long value = syscall(SYS_gettid);
    return value > 0 && (unsigned long)value <= UINT32_MAX ? (uint32_t)value : 0;
}

/* The entry jump retains its caller's return address. All foreign calls go
 * straight through the same original function; no locks or allocation occur. */
__attribute__((noinline)) static uint64_t player_view_query_dispatch(void) {
    uintptr_t caller = (uintptr_t)__builtin_return_address(0);
    uint64_t result = original_query();
    int saved_errno = errno;
    for (unsigned kind = 0; kind < 2; ++kind)
        if (ui_depth[kind]) atomic_fetch_add(&ui_counts[kind].queries, 1);
    uint32_t tid = current_thread();
    uint32_t owner = atomic_load_explicit(&owner_tid, memory_order_acquire);
    if (owner && tid == owner && !finished) {
        if (current_probe && samples.probe_calls != UINT64_MAX) ++samples.probe_calls;
        atomic_fetch_add_explicit(&ui_calls, 1, memory_order_relaxed);
        if (current_tick && !current_probe) {
            unsigned index = 0;
            unsigned in_image = caller >= UINT64_C(0x656000) && caller < UINT64_C(0x2c730a9);
            for (; index < samples.count; ++index) {
                player_view_query_row *row = &samples.rows[index];
                if (row->caller == caller && row->result == result &&
                    row->probe == current_probe && row->in_image == in_image) {
                    row->last_tick = current_tick;
                    if (row->count != UINT64_MAX) ++row->count;
                    break;
                }
            }
            if (index == samples.count) {
                if (samples.count == PLAYER_VIEW_QUERY_ROWS) {
                    if (samples.overflow != UINT64_MAX) ++samples.overflow;
                } else {
                    samples.rows[samples.count++] = (player_view_query_row){
                        .caller = caller, .result = result, .first_tick = current_tick, .last_tick = current_tick,
                        .count = 1, .in_image = in_image, .probe = current_probe,
                    };
                }
            }
        }
    } else if (owner && tid == worker_tid && tid != owner) {
        atomic_fetch_add_explicit(&worker_calls, 1, memory_order_relaxed);
    } else if (owner && tid != owner) {
        atomic_fetch_add_explicit(&other_calls, 1, memory_order_relaxed);
    }
    errno = saved_errno;
    return result;
}
static int ui_overlay_bind(uint32_t tid, uintptr_t object,
                           player_ui_overlay_callback enter, player_ui_overlay_callback leave,
                           unsigned count) {
    if (!atomic_load_explicit(&ui_installed, memory_order_acquire) || !tid || !object || !enter || !leave ||
        current_thread() != tid || tid != atomic_load_explicit(&owner_tid, memory_order_acquire) ||
        ui_depth[0] != 1 || ui_depth[1] || atomic_load_explicit(&ui_object, memory_order_acquire) != object ||
        atomic_load_explicit(&ui_overlay_binding, memory_order_acquire) ||
        !player_ui_escape_guard_prepare()) return 0;
    ui_overlay_remaining = count;
    ui_overlay_tid = tid;
    ui_overlay_object = object;
    ui_overlay_enter = enter;
    ui_overlay_leave = leave;
    atomic_store_explicit(&ui_overlay_binding, 1, memory_order_release);
    return 1;
}

EXPORT int x4mp_view_query_ui_bind(uint32_t tid, uintptr_t object,
                                   player_ui_overlay_callback enter, player_ui_overlay_callback leave) {
    return ui_overlay_bind(tid, object, enter, leave, 1);
}
EXPORT int x4mp_view_query_ui_bind_eight(uint32_t tid, uintptr_t object,
                                         player_ui_overlay_callback enter, player_ui_overlay_callback leave) {
    return ui_overlay_bind(tid, object, enter, leave, 8);
}
EXPORT int x4mp_view_query_ui_bind_count(uint32_t tid, uintptr_t object,
                                         player_ui_overlay_callback enter, player_ui_overlay_callback leave,
                                         unsigned count) {
    if (count != 8 && count != 160) return 0;
    return ui_overlay_bind(tid, object, enter, leave, count);
}
EXPORT unsigned x4mp_view_query_ui_overlay_progress(void) {
    return atomic_load_explicit(&ui_overlay_binding, memory_order_acquire);
}

EXPORT unsigned x4mp_view_query_ui_info(player_ui_callback_snapshot *out) {
    if (!out || !atomic_load_explicit(&ui_installed, memory_order_acquire)) return 0;
    memset(out, 0, sizeof *out);
    out->installed = 1;
    for (unsigned kind = 0; kind < 2; ++kind) {
        out->slots[kind] = ui_slots[kind];
        out->original[kind] = ui_originals[kind];
        out->original_slot[kind] = (uintptr_t)&ui_original[kind];
        out->replacement[kind] = (uintptr_t)(kind ? ui_render_dispatch : ui_move_dispatch);
    }
    return 1;
}

EXPORT int x4mp_view_query_ui_callbacks(player_ui_callback_snapshot *out) {
    if (!out || !atomic_load(&ui_installed) ||
        current_thread() != atomic_load_explicit(&owner_tid, memory_order_acquire)) return 0;
    memset(out, 0, sizeof *out);
    out->installed = 1;
    out->object = atomic_load(&ui_object);
    out->owner_tid = atomic_load(&owner_tid);
    out->closed = atomic_load_explicit(&ui_epoch, memory_order_acquire) == UI_CLOSED;
    for (unsigned kind = 0; kind < 2; ++kind) {
        ui_callback_counts *counts = &ui_counts[kind];
        out->entered[kind] = atomic_load(&counts->entered);
        out->left[kind] = atomic_load(&counts->left);
        out->queries[kind] = atomic_load(&counts->queries);
        out->foreign[kind] = atomic_load(&counts->foreign);
        out->nested[kind] = atomic_load(&counts->nested);
        out->object_mismatch[kind] = atomic_load(&counts->mismatch);
        out->inflight[kind] = atomic_load(&counts->inflight);
        out->first_tid[kind] = atomic_load(&counts->first_tid);
        out->thread_mismatch[kind] = atomic_load(&counts->thread_mismatch);
        out->slots[kind] = ui_slots[kind];
        out->original[kind] = ui_originals[kind];
        out->original_slot[kind] = (uintptr_t)&ui_original[kind];
        out->replacement[kind] = (uintptr_t)(kind ? ui_render_dispatch : ui_move_dispatch);
    }
    return 1;
}

/* Finish must be requested in the admitted outer Move callback. The callback
 * itself emits only after the original stock function has returned. */
EXPORT int x4mp_view_query_ui_finish(void) {
    if (!atomic_load(&ui_installed) || current_thread() != atomic_load(&owner_tid) ||
        ui_depth[0] != 1 || ui_depth[1] != 0) return 0;
    uint64_t state = atomic_load_explicit(&ui_epoch, memory_order_acquire);
    do {
        if ((state & (UI_REQUEST | UI_CLOSED)) || !(state & UI_COUNT_MASK)) return 0;
    } while (!atomic_compare_exchange_weak_explicit(&ui_epoch, &state, state | UI_REQUEST,
                                                     memory_order_acq_rel, memory_order_acquire));
    return 1;
}

static void ui_emit_final(void) {
    char line[1024];
    ui_callback_counts *move = &ui_counts[0], *render = &ui_counts[1];
    int size = snprintf(line, sizeof line,
        "X4 UI CALLBACK RESULT v=1 owner=%u object=%llx "
        "move=%llu/%llu/%llu/%llu/%llu/%llu/%llu/%llu/%u "
        "render=%llu/%llu/%llu/%llu/%llu/%llu/%llu/%llu/%u\n",
        atomic_load(&owner_tid), (unsigned long long)atomic_load(&ui_object),
#define UI_FIELDS(c) (unsigned long long)atomic_load(&(c)->entered), \
        (unsigned long long)atomic_load(&(c)->left), (unsigned long long)atomic_load(&(c)->queries), \
        (unsigned long long)atomic_load(&(c)->foreign), (unsigned long long)atomic_load(&(c)->nested), \
        (unsigned long long)atomic_load(&(c)->mismatch), (unsigned long long)atomic_load(&(c)->inflight), \
        (unsigned long long)atomic_load(&(c)->thread_mismatch), atomic_load(&(c)->first_tid)
        UI_FIELDS(move), UI_FIELDS(render));
#undef UI_FIELDS
    if (size > 0 && (size_t)size < sizeof line) (void)write(2, line, (size_t)size);
}

EXPORT unsigned x4mp_view_query_info(player_view_query_info *out) {
    if (out) *out = information;
    return PLAYER_VIEW_QUERY_VERSION;
}
EXPORT int x4mp_view_query_configure(uint32_t ui_tid, uint32_t worker) {
    if (!information.installed || !ui_tid || !worker || current_thread() != ui_tid ||
        atomic_load_explicit(&owner_tid, memory_order_acquire)) return 0;
    worker_tid = worker;
    atomic_store_explicit(&owner_tid, ui_tid, memory_order_release);
    return 1;
}
EXPORT int x4mp_view_query_phase(uint64_t tick, unsigned probe) {
    if (!tick || probe > 1 || finished || current_thread() != atomic_load_explicit(&owner_tid, memory_order_acquire)) return 0;
    current_tick = tick;
    current_probe = probe;
    return 1;
}
EXPORT int x4mp_view_query_snapshot(player_view_query_snapshot *out) {
    if (!out || current_thread() != atomic_load_explicit(&owner_tid, memory_order_acquire)) return 0;
    if (finished) {
        *out = finished_samples;
        return 1;
    }
    *out = samples;
    out->ui_calls = atomic_load_explicit(&ui_calls, memory_order_relaxed);
    out->worker_calls = atomic_load_explicit(&worker_calls, memory_order_relaxed);
    out->other_calls = atomic_load_explicit(&other_calls, memory_order_relaxed);
    return 1;
}

EXPORT int x4mp_view_query_finish(player_view_query_snapshot *out) {
    if (!out || current_thread() != atomic_load_explicit(&owner_tid, memory_order_acquire)) return 0;
    if (!finished) {
        if (!x4mp_view_query_snapshot(&finished_samples)) return 0;
        finished = 1;
    }
    *out = finished_samples;
    return 1;
}

static void emit_entry_stub(unsigned char *page, uintptr_t destination) {
    page[0] = 0xff;
    page[1] = 0x25;
    memset(page + 2, 0, 4);
    memcpy(page + 6, &destination, 8);
}
static int make_entry_patch(uintptr_t entry, uintptr_t stub, uint64_t initial, uint64_t *patched) {
    int64_t distance = (int64_t)stub - (int64_t)(entry + 5);
    if (!patched || distance < INT32_MIN || distance > INT32_MAX) return 0;
    unsigned char bytes[8];
    memcpy(bytes, &initial, 8);
    bytes[0] = 0xe9;
    int32_t relative = (int32_t)distance;
    memcpy(bytes + 1, &relative, 4);
    bytes[5] = 0x90;
    bytes[6] = 0x90;
    memcpy(patched, bytes, 8);
    return 1;
}

/* CIE: CFA=rsp+8, return address at CFA-8. FDE covers only the stack-neutral
 * resident entry jump; the static original trampoline has assembler CFI. */
static unsigned char entry_frame[48] __attribute__((aligned(8)));
static int register_frame_for_entry(uintptr_t start, unsigned char *frame) {
    /* Use the process libgcc registry, not Zig's private static unwinder. */
    void *library = dlopen("libgcc_s.so.1", RTLD_NOW | RTLD_LOCAL | RTLD_NODELETE);
    if (!library) return 0;
    void (*register_frame)(void *) = (void (*)(void *))dlsym(library, "__register_frame");
    if (!register_frame) return 0;
    static const unsigned char cie[20] = {
        16,0,0,0, 0,0,0,0, 1,0,1,0x78,16, 0x0c,7,8,0x90,1,0,0
    };
    uint32_t length = 20, cie_offset = 24;
    uint64_t address = start, range = 14;
    memcpy(frame, cie, sizeof cie);
    memcpy(frame + 20, &length, 4);
    memcpy(frame + 24, &cie_offset, 4);
    memcpy(frame + 28, &address, 8);
    memcpy(frame + 36, &range, 8);
    register_frame(frame);
    return 1;
}
static int register_entry_frame(uintptr_t start) {
    return register_frame_for_entry(start, entry_frame);
}

#include "player_live_pose_preload_impl.h"

/* The two slots share a RELRO page. All pins are checked before changing
 * protection; a failure is terminal in the pre-main disposable process. */
static int ui_code_matches(uintptr_t address, size_t length, const char *sha) {
    unsigned char digest[32];
    if (!is_rx(address) || !is_rx(address + length - 1)) return 0;
    lockstep_sha256((const void *)address, length, digest);
    return digest_matches(digest, sha);
}

static int install_ui_slot_pair(uintptr_t first, uintptr_t second,
                                const uintptr_t expected_slots[2],
                                const uintptr_t replacement_slots[2]) {
    uintptr_t slots[2] = { first, second };
    uintptr_t page = first & ~(uintptr_t)(PVQ_PAGE - 1);
    if ((second & ~(uintptr_t)(PVQ_PAGE - 1)) != page ||
        !has_permissions(first, "r--p") || !has_permissions(second, "r--p")) return 0;
    for (unsigned kind = 0; kind < 2; ++kind) {
        if (__atomic_load_n((uintptr_t *)slots[kind], __ATOMIC_SEQ_CST) != expected_slots[kind]) return 0;
    }
    if (mprotect((void *)page, PVQ_PAGE, PROT_READ | PROT_WRITE)) return 0;
    for (unsigned kind = 0; kind < 2; ++kind) {
        uintptr_t expected = expected_slots[kind];
        if (!__atomic_compare_exchange_n((uintptr_t *)slots[kind], &expected, replacement_slots[kind],
                                          0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST)) return 0;
    }
    return !mprotect((void *)page, PVQ_PAGE, PROT_READ) &&
        has_permissions(first, "r--p") && has_permissions(second, "r--p");
}

static int install_ui_callbacks(void) {
    if (!single_thread() ||
        !ui_code_matches(0xa77e90u, 9, "8d6e8f1ea5249f34d12a611b986020299ebc777dd52ba0dcc10d438071374726") ||
        !ui_code_matches(0xa758a0u, 9702, "5326f3dc8d22a5d473cb32cd0f90026a4f0be111c5da07d31dde79dda2a5261c") ||
        !ui_code_matches(0xa60520u, 6, "c6608aaada36fd88d0830d1ce067b198ae98e369c7a1a10a317387c77fea569f") ||
        !ui_code_matches(0xa604e0u, 46, "b39f3fda0e0ee4816d3a75cccb86e31db1e1bdf9044431cadce05847c3c4fd5a")) return 0;
    uintptr_t replacements[2] = { (uintptr_t)ui_move_dispatch, (uintptr_t)ui_render_dispatch };
    for (unsigned kind = 0; kind < 2; ++kind)
        ui_original[kind] = (void (*)(void *))ui_originals[kind];
    if (!install_ui_slot_pair(ui_slots[0], ui_slots[1], ui_originals, replacements)) return 0;
    atomic_store_explicit(&ui_installed, 1, memory_order_release);
    return 1;
}

#ifndef PLAYER_VIEW_QUERY_TEST
__attribute__((constructor)) static void player_view_query_install(void) {
    const char *enabled = getenv("X4MP_VIEW_QUERY_OBSERVER");
    if (!enabled) {
        if (getenv("X4MP_UI_CALLBACK_OBSERVER") || getenv("X4_DEV_UI_LIVE_POSE_OBSERVER")) refuse();
        return;
    }
    if (strcmp(enabled, "1") || !image_matches() || sysconf(_SC_PAGESIZE) != PVQ_PAGE ||
        !is_rx(PVQ_ENTRY) || !is_rx(PVQ_ENTRY + 0x52) ||
        memcmp((const void *)PVQ_ENTRY, &original_word, 8)) refuse();
    unsigned char digest[32];
    lockstep_sha256((const void *)PVQ_ENTRY, 0x53, digest);
    if (!digest_matches(digest, function_sha)) refuse();
    const char *ui_enabled = getenv("X4MP_UI_CALLBACK_OBSERVER");
    if (ui_enabled && (strcmp(ui_enabled, "1") || !install_ui_callbacks())) refuse();
    const char *live_pose = getenv("X4_DEV_UI_LIVE_POSE_OBSERVER");
    if (live_pose && (strcmp(live_pose, "1") || !ui_enabled ||
        !getenv("X4_DEV_UI_SEAT_QUERY") || strcmp(getenv("X4_DEV_UI_SEAT_QUERY"), "1") ||
        !getenv("X4_DEV_UI_OVERLAY_160") || strcmp(getenv("X4_DEV_UI_OVERLAY_160"), "1") ||
        !install_live_pose())) refuse();
    unsigned char *page = allocate_near();
    if (!page) refuse();
    memset(page, 0xcc, PVQ_PAGE);
    emit_entry_stub(page, (uintptr_t)player_view_query_dispatch);
    if (mprotect(page, PVQ_PAGE, PROT_READ | PROT_EXEC) || !is_rx((uintptr_t)page)) refuse();
    original_query = player_view_query_original;
    if (!register_entry_frame((uintptr_t)page)) refuse();
    uint64_t desired, expected = original_word;
    if (!make_entry_patch(PVQ_ENTRY, (uintptr_t)page, original_word, &desired) || !single_thread()) refuse();
    uintptr_t base = PVQ_ENTRY & ~(uintptr_t)(PVQ_PAGE - 1);
    if (mprotect((void *)base, PVQ_PAGE, PROT_READ | PROT_WRITE | PROT_EXEC) ||
        !__atomic_compare_exchange_n((uint64_t *)PVQ_ENTRY, &expected, desired, 0,
                                     __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST)) refuse();
    __builtin___clear_cache((char *)PVQ_ENTRY, (char *)PVQ_ENTRY + 8);
    if (mprotect((void *)base, PVQ_PAGE, PROT_READ | PROT_EXEC) || !is_rx(PVQ_ENTRY)) refuse();
    information = (player_view_query_info){
        .version = PLAYER_VIEW_QUERY_VERSION, .installed = 1,
        .entry = PVQ_ENTRY, .entry_stub = (uintptr_t)page,
        .original_trampoline = (uintptr_t)player_view_query_original,
        .dispatcher = (uintptr_t)player_view_query_dispatch,
        .original_slot = (uintptr_t)&original_query, .entry_frame = (uintptr_t)entry_frame,
        .original_word = original_word, .patched_word = desired,
        .entry_stub_size = 14, .original_trampoline_size = 26,
        .entry_frame_size = sizeof entry_frame, .page_size = PVQ_PAGE,
    };
    static const char active[] = "X4 VIEW QUERY PRELOAD ACTIVE version=1\n";
    (void)write(2, active, sizeof active - 1);
}
#endif
