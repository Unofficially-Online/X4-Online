#define _GNU_SOURCE
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <stdio.h>

static unsigned char *arena;
#define LX(va) (arena + ((uintptr_t)(va) - 0x1640000u))
#define CRE_INSTALL_TEST
static unsigned bind_helper_calls;
static void *bind_seen_controller;
static const void *bind_seen_owner;
static void fake_bind_helper(void *controller, const void *owner) { ++bind_helper_calls; bind_seen_controller = controller; bind_seen_owner = owner; }
#define CRE_BIND_NATIVE(controller, owner) fake_bind_helper((controller), (owner))
static int fail_open, fail_close, fail_cas, fail_cas_also, calls_open, calls_close, calls_cas;
static int reject_pin, fail_thunk;
static int cre_image_pinned(void) { return !reject_pin; }
static unsigned char *cre_allocate(void *near_address) {
    (void)near_address;
    return mmap(NULL, 4096, PROT_READ | PROT_WRITE,
                MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
}
static int cre_thunk_rx(void *page) {
    if (fail_thunk) return 0;
    return !mprotect(page, 4096, PROT_READ | PROT_EXEC);
}
static int cre_is_rx(void *address) { (void)address; return 1; }
static int cre_open(void *address) {
    ++calls_open;
    if (calls_open == fail_open) return 0;
    return !mprotect((void *)((uintptr_t)address & ~(uintptr_t)4095), 4096,
                     PROT_READ | PROT_WRITE | PROT_EXEC);
}
static int cre_close(void *address) {
    ++calls_close;
    if (calls_close == fail_close) return 0;
    return !mprotect((void *)((uintptr_t)address & ~(uintptr_t)4095), 4096,
                     PROT_READ | PROT_EXEC);
}
static int cre_compare_exchange(uint64_t *address, uint64_t expected, uint64_t desired) {
    ++calls_cas;
    if (calls_cas == fail_cas || calls_cas == fail_cas_also) return 0;
    return __atomic_compare_exchange_n(address, &expected, desired, 0,
                                       __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
}
static void cre_lock(void) {}
static void cre_unlock(void) {}
#include "gate_context_refresh_edges.h"
static unsigned native_calls;
static bool native_value;
static void synthetic_original(bool value) { ++native_calls; native_value = value; }
static int auth(void *context, const void *object, cre_phase phase) {
    return context == object && (phase == CRE_BIND || phase == CRE_DESTRUCT);
}
static void clear_state(void) {
    memset(&cre_resident, 0, sizeof(cre_resident));
    calls_open = calls_close = calls_cas = 0;
    fail_open = fail_close = fail_cas = fail_cas_also = reject_pin = fail_thunk = 0;
}
static void reset_words(void) {
    for (unsigned i = 0; i < CRE_EDGE_COUNT; ++i) {
        void *page = (void *)((uintptr_t)LX(cre_word_vas[i]) & ~(uintptr_t)4095);
        assert(!mprotect(page, 4096, PROT_READ | PROT_WRITE | PROT_EXEC));
        memcpy(LX(cre_word_vas[i]), &cre_original_words[i], 8);
        assert(!mprotect(page, 4096, PROT_READ | PROT_EXEC));
    }
}
int main(void) {
    size_t size = 0x30000;
    arena = mmap(NULL, size, PROT_READ | PROT_WRITE | PROT_EXEC,
                 MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    assert(arena != MAP_FAILED);
    /* Native caller has aligned stack for CALL; target is a tail jump stub. */
    unsigned char prologue[] = {0x48,0x83,0xec,0x08};
    unsigned char epilogue[] = {0x48,0x83,0xc4,0x08,0xc3};
    memcpy(LX(0x164c2fc), prologue, sizeof(prologue));
    memcpy(LX(0x164c307), epilogue, sizeof(epilogue));
    cre_emit_jump(LX(CRE_REFRESH_VA), (uintptr_t)synthetic_original);
    int object = 0, other = 0;
    unsigned char *authenticated_caller = LX(0x164c2d0);
    authenticated_caller[0] = 0x53; /* push rbx */
    authenticated_caller[1] = 0x48; authenticated_caller[2] = 0xbb;
    uintptr_t controller_object = (uintptr_t)&object;
    memcpy(authenticated_caller + 3, &controller_object, 8);
    assert(cre_encode_edge(authenticated_caller + 11, 0xe8,
                           (uintptr_t)authenticated_caller + 11, (uintptr_t)LX(0x164c2fc)));
    authenticated_caller[16] = 0x5b; authenticated_caller[17] = 0xc3;
    reset_words();
    void (*call_edge)(void) = (void *)LX(0x164c2fc);
    call_edge(); cre_bind_wrapper((void *)0x1234, (void *)0x5678);
    assert(native_calls == 2 && native_value);
    clear_state(); reject_pin = 1;
    assert(!cre_install(&cre_resident) && !cre_resident.page);
    clear_state(); assert(cre_install(&cre_resident));
    call_edge(); assert(native_calls == 3 && !native_value);
    cre_bind_wrapper((void *)0x1234, (void *)0x5678); assert(native_calls == 4 && native_value);
    assert(bind_helper_calls == 2 && bind_seen_controller == (void *)0x1234 && bind_seen_owner == (void *)0x5678);
    cre_token token = {0};
    void (*call_authenticated)(void) = (void *)authenticated_caller;
    assert(cre_begin(&token, &object, CRE_BIND, auth, &object));
    cre_bind_wrapper(&object, (void *)0x5678); assert(native_calls == 4); assert(cre_end(&token, 1));
    assert(cre_begin(&token, &object, CRE_DESTRUCT, auth, &object));
    call_authenticated(); assert(native_calls == 4); assert(cre_end(&token, 1));
    cre_token wrong_bind = {0};
    assert(cre_begin(&wrong_bind, &other, CRE_BIND, auth, &other));
    cre_bind_wrapper(&object, (void *)0x5678); assert(native_calls == 5);
    assert(!cre_end(&wrong_bind, 0) && atomic_load(&wrong_bind.state) == 2);
    cre_token wrong = {0};
    assert(cre_begin(&wrong, &other, CRE_DESTRUCT, auth, &other));
    call_authenticated(); assert(native_calls == 6);
    assert(!cre_end(&wrong, 0) && atomic_load(&wrong.state) == 2);
    assert(cre_restore(&cre_resident));
    call_edge(); cre_bind_wrapper((void *)0x1234, (void *)0x5678); assert(native_calls == 8);
    clear_state(); reset_words(); fail_cas = 1;
    assert(!cre_install(&cre_resident));
    assert(!cre_resident.owned[0] && !cre_resident.uncertain);
    clear_state(); reset_words(); fail_open = 1;
    assert(!cre_install(&cre_resident));
    assert(!cre_resident.owned[0] && !cre_resident.uncertain);
    clear_state(); reset_words(); fail_close = 1;
    assert(!cre_install(&cre_resident));
    assert(cre_resident.uncertain && !cre_resident.owned[0]);
    assert(!cre_restore(&cre_resident));
    assert(!cre_resident.owned[0] && cre_resident.uncertain);
    clear_state(); reset_words(); assert(cre_install(&cre_resident));
    assert(cre_open(LX(cre_word_vas[0])));
    __atomic_store_n((uint64_t *)LX(cre_word_vas[0]), UINT64_C(0x12345678), __ATOMIC_SEQ_CST);
    assert(cre_close(LX(cre_word_vas[0])));
    assert(!cre_restore(&cre_resident));
    assert(cre_resident.owned[0] && cre_resident.uncertain);
    assert(__atomic_load_n((uint64_t *)LX(cre_word_vas[0]), __ATOMIC_SEQ_CST) == UINT64_C(0x12345678));
    clear_state(); reset_words(); fail_thunk = 1;
    assert(!cre_install(&cre_resident));
    assert(cre_resident.thunk_permission_pending && cre_resident.uncertain);
    fail_thunk = 0; assert(!cre_restore(&cre_resident));
    assert(!cre_resident.thunk_permission_pending && cre_resident.uncertain);
    puts("context refresh installer: ok");
    return 0;
}
