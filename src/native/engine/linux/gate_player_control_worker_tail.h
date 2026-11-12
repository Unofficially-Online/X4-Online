#ifndef X4_GATE_PLAYER_CONTROL_WORKER_TAIL_H
#define X4_GATE_PLAYER_CONTROL_WORKER_TAIL_H

/* Default-unused hook at MoveWorker's semaphore-completion call. The owner
 * installs/restores only at a proven boundary excluding this code site and
 * all wrappers. The callback owns experiment activation, worker identity,
 * generation, semaphore/count checks, deadlines and fatal handling. It must
 * return only when the native semaphore may be posted. This hook alone does
 * not park main or exclude renderer, input, physics or other workers.
 *
 * A failed install or restore is never permission to resume: the caller must
 * terminate/discard, including after a reported collision. All binding and RX
 * stub storage remains resident forever. No page is unmapped here. */
#include <errno.h>
#include <stdatomic.h>
#include <stdint.h>
#include <string.h>
#include <sys/mman.h>
#include <limits.h>

#define PLAYER_TAIL_WORD_VA UINT64_C(0x1212768)
#define PLAYER_TAIL_CALL_VA UINT64_C(0x121276a)
#define PLAYER_TAIL_SITE_VA UINT64_C(0x121274c)
#define PLAYER_TAIL_RELEASE_VA UINT64_C(0x1a23be0)
#define PLAYER_TAIL_WORD_ORIGINAL UINT64_C(0x4800811471e802c9)
#define PLAYER_TAIL_IMAGE_SHA "113f73d74570084f6ce18a659b365009eccf579fb66dbabb5dad2b1718045f20"
#define PLAYER_TAIL_SITE_SHA "5b511620560debcade45ced6db6f668c70be498b79199b24345a70245b34d628"
#define PLAYER_TAIL_RELEASE_SHA "2eceb7f08f4e66898425f0ee8ab65e7a04957545fbe7e8d3fc4a8bcd7cf991f2"

typedef void (*player_tail_callback)(void *semaphore, unsigned count);
typedef struct {
    unsigned char *page;
    uint64_t patched_word;
    player_tail_callback callback;
    unsigned word_owned;
    unsigned code_permission_pending;
    unsigned page_permission_pending;
    unsigned uncertain;
    unsigned installed;
} player_tail_installation;
static player_tail_installation player_tail_resident;
static _Atomic(player_tail_installation *) player_tail_binding;

#ifndef PLAYER_TAIL_TEST
static int player_tail_pins(void) {
    unsigned char digest[32];
    char hex[65];
    const lx_site pins[] = {
        {"worker-tail-site", PLAYER_TAIL_SITE_VA, 35, PLAYER_TAIL_SITE_SHA},
        {"worker-semaphore-release", PLAYER_TAIL_RELEASE_VA, 223, PLAYER_TAIL_RELEASE_SHA},
    };
    if (!gate_file_sha256(gate_env.exe, LX_FILE_SIZE, digest))
        return 0;
    hex32(digest, hex);
    return !strcmp(hex, PLAYER_TAIL_IMAGE_SHA) && site_ok(&pins[0]) && site_ok(&pins[1]);
}
static unsigned char *player_tail_allocate(void *near_address) {
    return gate_near_page(near_address);
}
static int player_tail_page_rx(void *page) {
    return !mprotect(page, GATE_PAGE, PROT_READ | PROT_EXEC);
}
static int player_tail_is_rx(void *address) {
    char permissions[5];
    return gate_maps_find((uintptr_t)address, permissions, NULL, 0) && !strcmp(permissions, "r-xp");
}
static int player_tail_open(void *address) {
    return gate_code_open(address, 8);
}
static int player_tail_close(void *address) {
    return gate_code_close(address);
}
static int player_tail_cas(uint64_t *address, uint64_t expected, uint64_t desired) {
    int changed =
        __atomic_compare_exchange_n(address, &expected, desired, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
    if (changed)
        __builtin___clear_cache((char *)address, (char *)address + 8);
    return changed;
}
static void player_tail_lock(void) {
    pthread_mutex_lock(&gate_code_lock);
}
static void player_tail_unlock(void) {
    pthread_mutex_unlock(&gate_code_lock);
}
#define PLAYER_TAIL_READ(address, output, size) gate_read_local((address), (output), (size))
#define PLAYER_TAIL_ORIGINAL(semaphore, count)                                                               \
    ((void (*)(void *, unsigned))LX(PLAYER_TAIL_RELEASE_VA))((semaphore), (count))
#endif

static void player_tail_dispatch(void *semaphore, unsigned count) {
    player_tail_installation *binding = atomic_load_explicit(&player_tail_binding, memory_order_acquire);
    /* Binding is published before the call site and stays resident even after
     * restore, including for a caller that already fetched the old jump. */
    if (binding && binding->callback) {
        int caller_errno = errno;
        binding->callback(semaphore, count);
        errno = caller_errno;
    }
    PLAYER_TAIL_ORIGINAL(semaphore, count);
}

static int player_tail_relative(uintptr_t source, uintptr_t destination, int32_t *result) {
    if (source > INT64_MAX || destination > INT64_MAX)
        return 0;
    int64_t offset = (int64_t)destination - (int64_t)source;
    if (offset < INT32_MIN || offset > INT32_MAX)
        return 0;
    *result = (int32_t)offset;
    return 1;
}

static void player_tail_emit(unsigned char *page) {
    /* jmp [rip+0] preserves the original arguments and return address. */
    page[0] = 0xff;
    page[1] = 0x25;
    memset(page + 2, 0, 4);
    uintptr_t destination = (uintptr_t)player_tail_dispatch;
    memcpy(page + 6, &destination, sizeof destination);
}

/* A caller hashing a live function may substitute the pinned original word
 * only after authenticating the owned patch and complete resident RX stub.
 * Never hide a foreign patch, pending permission or uncertain installation. */
static int player_tail_original_view(uintptr_t va, unsigned char *copy, size_t length,
                                     player_tail_callback expected_callback) {
    if (!copy || va > UINTPTR_MAX - length) return 0;
    if (PLAYER_TAIL_WORD_VA + 8 <= va || PLAYER_TAIL_WORD_VA >= va + length) return 1;
    if (PLAYER_TAIL_WORD_VA < va || PLAYER_TAIL_WORD_VA + 8 > va + length) return 0;
    player_tail_installation *state = &player_tail_resident;
    uint64_t copied = 0, current = 0;
    memcpy(&copied, copy + (PLAYER_TAIL_WORD_VA - va), sizeof copied);
    if (!PLAYER_TAIL_READ(LX(PLAYER_TAIL_WORD_VA), &current, sizeof current) || copied != current)
        return 0;
    if (!state->installed && !state->word_owned && !state->code_permission_pending &&
        !state->page_permission_pending && !state->uncertain)
        return current == PLAYER_TAIL_WORD_ORIGINAL;
    if (!state->installed || !state->word_owned || state->code_permission_pending ||
        state->page_permission_pending || state->uncertain || !state->page || !expected_callback ||
        state->callback != expected_callback ||
        atomic_load_explicit(&player_tail_binding, memory_order_acquire) != state ||
        !player_tail_is_rx(LX(PLAYER_TAIL_WORD_VA)) || !player_tail_is_rx(state->page))
        return 0;
    int32_t displacement;
    if (!player_tail_relative((uintptr_t)LX(PLAYER_TAIL_CALL_VA) + 5,
                              (uintptr_t)state->page, &displacement)) return 0;
    uint64_t expected = PLAYER_TAIL_WORD_ORIGINAL;
    unsigned char call[5] = {0xe8};
    memcpy(call + 1, &displacement, sizeof displacement);
    memcpy((unsigned char *)&expected + 2, call, sizeof call);
    unsigned char actual_stub[14], expected_stub[14];
    player_tail_emit(expected_stub);
    if (state->patched_word != expected || current != expected ||
        !PLAYER_TAIL_READ(state->page, actual_stub, sizeof actual_stub) ||
        memcmp(actual_stub, expected_stub, sizeof actual_stub)) return 0;
    uint64_t original = PLAYER_TAIL_WORD_ORIGINAL;
    memcpy(copy + (PLAYER_TAIL_WORD_VA - va), &original, sizeof original);
    return 1;
}

/* On a close failure the caller retains both ownership and permission
 * obligations. A later restoration attempt may discharge them, but the sticky
 * uncertainty still makes the result non-clean. */
static int player_tail_swap(player_tail_installation *state, int install) {
    uint64_t *address = (uint64_t *)LX(PLAYER_TAIL_WORD_VA);
    uint64_t expected = install ? PLAYER_TAIL_WORD_ORIGINAL : state->patched_word;
    uint64_t replacement = install ? state->patched_word : PLAYER_TAIL_WORD_ORIGINAL;
    player_tail_lock();
    if (!player_tail_is_rx(address)) {
        player_tail_unlock();
        return 0;
    }
    state->code_permission_pending = 1;
    if (!player_tail_open(address)) {
        state->code_permission_pending = 0;
        player_tail_unlock();
        return 0;
    }
    unsigned prior_owned = state->word_owned;
    state->word_owned = 1; /* record the obligation before the atomic mutation */
    int changed = player_tail_cas(address, expected, replacement);
    state->word_owned = changed ? (unsigned)install : prior_owned;
    int closed = player_tail_close(address);
    if (closed)
        state->code_permission_pending = 0;
    else
        state->uncertain = 1;
    player_tail_unlock();
    return changed && closed;
}

static int player_tail_restore(player_tail_installation *state) {
    if (!state || state != &player_tail_resident)
        return 0;
    state->installed = 0;
    if (state->code_permission_pending) {
        player_tail_lock();
        if (player_tail_close(LX(PLAYER_TAIL_WORD_VA)))
            state->code_permission_pending = 0;
        else
            state->uncertain = 1;
        player_tail_unlock();
    }
    if (state->word_owned && !state->code_permission_pending && !player_tail_swap(state, 0))
        state->uncertain = 1;
    if (state->page_permission_pending) {
        if (player_tail_page_rx(state->page))
            state->page_permission_pending = 0;
        else
            state->uncertain = 1;
    }
    return !state->word_owned && !state->code_permission_pending && !state->page_permission_pending &&
           !state->uncertain;
}

static int player_tail_install(player_tail_installation *state, player_tail_callback callback) {
    if (!state || state != &player_tail_resident || !callback || state->page || state->installed ||
        state->uncertain || atomic_load_explicit(&player_tail_binding, memory_order_acquire) ||
        !player_tail_pins())
        return 0;
    uint64_t *word = (uint64_t *)LX(PLAYER_TAIL_WORD_VA);
    if (((uintptr_t)word & 7) || !player_tail_is_rx(word) ||
        __atomic_load_n(word, __ATOMIC_SEQ_CST) != PLAYER_TAIL_WORD_ORIGINAL)
        return 0;
    state->page = player_tail_allocate(LX(PLAYER_TAIL_CALL_VA));
    if (!state->page)
        return 0;
    state->page_permission_pending = 1;
    player_tail_emit(state->page);
    if (!player_tail_page_rx(state->page)) {
        state->uncertain = 1;
        return 0;
    }
    state->page_permission_pending = 0;
    __builtin___clear_cache((char *)state->page, (char *)state->page + 14);
    if (!player_tail_is_rx(state->page)) {
        state->uncertain = 1;
        return 0;
    }
    int32_t displacement;
    if (!player_tail_relative((uintptr_t)LX(PLAYER_TAIL_CALL_VA) + 5, (uintptr_t)state->page, &displacement))
        return 0;
    state->patched_word = PLAYER_TAIL_WORD_ORIGINAL;
    unsigned char patch[5] = {0xe8};
    memcpy(patch + 1, &displacement, sizeof displacement);
    memcpy((unsigned char *)&state->patched_word + 2, patch, sizeof patch);
    state->callback = callback;
    atomic_store_explicit(&player_tail_binding, state, memory_order_release);
    if (!player_tail_swap(state, 1))
        return 0;
    state->installed = 1;
    return 1;
}
#endif
